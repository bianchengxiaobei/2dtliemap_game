#include "ai/PathfindingService.h"

#include <algorithm>
#include <mutex>
#include <utility>

#include "core/Log.h"
#include "core/TaskScheduler.h"
#include "world/Chunk.h"
#include "world/WorldCoords.h"

namespace zl::ai {
namespace {

constexpr const char* kLogModule = "ai";
// 快照规模防守上限（起终点 chunk 包围盒，超出 → 即时失败）：
// village（12×12 chunk）远小于上限；stress 大图超距查询直接拒绝，避免巨型分配。
constexpr int kMaxSpanChunks = 64;    // 单轴 chunk 数上限
constexpr int kMaxAreaChunks = 2048;

std::uint64_t PackKey(int cx, int cy) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(cy));
}

}  // namespace

// 跨线程共享：工作线程写结果/统计，主线程读；服务析构后在途任务仍安全
struct PathfindingService::Core {
    std::mutex resultsMutex;
    std::deque<PathResult> results;

    std::mutex statsMutex;
    Stats stats;
};

PathfindingService::PathfindingService(core::TaskScheduler* tasks, MapChunkBounds bounds,
                                       AStarOptions options)
    : m_tasks(tasks), m_bounds(bounds), m_options(options),
      m_core(std::make_shared<Core>()) {}

PathfindingService::~PathfindingService() = default;

void PathfindingService::OnChunkLoaded(const world::Chunk& chunk) {
    m_chunks[PackKey(chunk.ChunkX(), chunk.ChunkY())] = BuildChunkPassability(chunk);
}

void PathfindingService::OnChunkUnloading(int chunkX, int chunkY) {
    m_chunks.erase(PackKey(chunkX, chunkY));
}

bool PathfindingService::IsPassable(int gx, int gy) const {
    const world::ChunkCoord coord = world::ChunkOf(gx, gy);
    const auto it = m_chunks.find(PackKey(coord.cx, coord.cy));
    if (it == m_chunks.end()) {
        return false;  // 未加载 = 未知 = 不可通行（与 PathSnapshot 一致）
    }
    const world::SquareLocal local = world::SquareLocalOf(gx, gy);
    return !it->second.IsBlocked(local.lx, local.ly);
}

std::shared_ptr<PathSnapshot> PathfindingService::BuildSnapshot(int sx, int sy, int gx,
                                                                int gy) const {
    const world::ChunkCoord startChunk = world::ChunkOf(sx, sy);
    const world::ChunkCoord goalChunk = world::ChunkOf(gx, gy);
    const int minCX = m_bounds.x0;
    const int maxCX = m_bounds.x0 + m_bounds.width - 1;
    const int minCY = m_bounds.y0;
    const int maxCY = m_bounds.y0 + m_bounds.height - 1;
    const auto inMap = [&](const world::ChunkCoord& c) {
        return c.cx >= minCX && c.cx <= maxCX && c.cy >= minCY && c.cy <= maxCY;
    };
    if (!inMap(startChunk) || !inMap(goalChunk)) {
        return nullptr;  // 越界 → 即时 Invalid
    }

    const int cx0 = std::clamp(std::min(startChunk.cx, goalChunk.cx) - 1, minCX, maxCX);
    const int cx1 = std::clamp(std::max(startChunk.cx, goalChunk.cx) + 1, minCX, maxCX);
    const int cy0 = std::clamp(std::min(startChunk.cy, goalChunk.cy) - 1, minCY, maxCY);
    const int cy1 = std::clamp(std::max(startChunk.cy, goalChunk.cy) + 1, minCY, maxCY);
    const int spanX = cx1 - cx0 + 1;
    const int spanY = cy1 - cy0 + 1;
    if (spanX > kMaxSpanChunks || spanY > kMaxSpanChunks || spanX * spanY > kMaxAreaChunks) {
        return nullptr;  // 超距（大图长距离）→ 即时 Invalid
    }

    auto snapshot = std::make_shared<PathSnapshot>(cx0, cy0, cx1, cy1);
    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            const auto it = m_chunks.find(PackKey(cx, cy));
            if (it != m_chunks.end()) {
                snapshot->SetChunk(cx, cy, it->second);
            }
            // 未缓存（未加载）的 chunk 保持"未填充 = 阻挡"
        }
    }
    return snapshot;
}

std::uint64_t PathfindingService::Submit(int startX, int startY, int goalX, int goalY) {
    const std::uint64_t queryId = m_nextQueryId++;
    {
        std::lock_guard<std::mutex> lock(m_core->statsMutex);
        ++m_core->stats.submitted;
        ++m_core->stats.pending;
    }

    std::shared_ptr<PathSnapshot> snapshot = BuildSnapshot(startX, startY, goalX, goalY);
    if (snapshot == nullptr) {
        PathResult result;
        result.queryId = queryId;
        result.path.outcome = AStarResult::Outcome::Invalid;
        {
            std::lock_guard<std::mutex> lock(m_core->statsMutex);
            ++m_core->stats.completed;
            --m_core->stats.pending;
            ++m_core->stats.invalid;
        }
        std::lock_guard<std::mutex> lock(m_core->resultsMutex);
        m_core->results.push_back(std::move(result));
        ZL_WARN(kLogModule, "寻路请求越界/超距：({},{}) → ({},{})", startX, startY, goalX,
                goalY);
        return queryId;
    }

    QueryJob job;
    job.queryId = queryId;
    job.startX = startX;
    job.startY = startY;
    job.goalX = goalX;
    job.goalY = goalY;
    job.snapshot = std::move(snapshot);
    job.options = m_options;

    if (m_tasks == nullptr) {
        ExecuteQuery(m_core, job);  // 同步模式（单测 / 无后台）
    } else {
        const std::shared_ptr<Core> core = m_core;
        m_tasks->Submit([core, job = std::move(job)]() { ExecuteQuery(core, job); });
    }
    return queryId;
}

void PathfindingService::ExecuteQuery(const std::shared_ptr<Core>& core, const QueryJob& job) {
    AStarResult path =
        PlanPath(*job.snapshot, job.startX, job.startY, job.goalX, job.goalY, job.options);

    {
        std::lock_guard<std::mutex> lock(core->statsMutex);
        Stats& stats = core->stats;
        ++stats.completed;
        --stats.pending;
        stats.lastMs = path.ms;
        stats.avgMs += (path.ms - stats.avgMs) / static_cast<double>(stats.completed);
        switch (path.outcome) {
            case AStarResult::Outcome::Found:
                ++stats.found;
                break;
            case AStarResult::Outcome::Unreachable:
                ++stats.unreachable;
                break;
            case AStarResult::Outcome::Invalid:
                ++stats.invalid;
                break;
            case AStarResult::Outcome::BudgetExceeded:
                ++stats.budget;
                break;
        }
    }

    PathResult result;
    result.queryId = job.queryId;
    result.path = std::move(path);
    std::lock_guard<std::mutex> lock(core->resultsMutex);
    core->results.push_back(std::move(result));
}

bool PathfindingService::TryTakeResult(PathResult* out) {
    std::lock_guard<std::mutex> lock(m_core->resultsMutex);
    if (m_core->results.empty()) {
        return false;
    }
    *out = std::move(m_core->results.front());
    m_core->results.pop_front();
    return true;
}

PathfindingService::Stats PathfindingService::GetStats() const {
    std::lock_guard<std::mutex> lock(m_core->statsMutex);
    return m_core->stats;
}

}  // namespace zl::ai
