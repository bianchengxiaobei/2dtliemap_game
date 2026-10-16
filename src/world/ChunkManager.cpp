#include "world/ChunkManager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "core/Log.h"
#include "world/Chunk.h"
#include "world/ChunkData.h"
#include "world/WorldCoords.h"

namespace zl::world {
namespace {

constexpr const char* kLogModule = "world";

}  // namespace

ChunkManager::ChunkManager(World& world, MapLoader::Meta meta, core::TaskScheduler& tasks,
                           int loadRadius)
    : m_world(world), m_meta(std::move(meta)), m_tasks(tasks),
      m_loadRadius(std::max(0, loadRadius)) {}

std::uint64_t ChunkManager::PackKey(int chunkX, int chunkY) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkX)) << 32) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkY));
}

int ChunkManager::Chebyshev(int ax, int ay, int bx, int by) {
    return std::max(std::abs(ax - bx), std::abs(ay - by));
}

bool ChunkManager::InRange(int chunkX, int chunkY) const {
    return m_hasCam && Chebyshev(chunkX, chunkY, m_camChunkX, m_camChunkY) <= m_loadRadius;
}

void ChunkManager::Update(float cameraGridX, float cameraGridY) {
    const int cx = FloorDiv(static_cast<int>(std::floor(cameraGridX)), core::kChunkSize);
    const int cy = FloorDiv(static_cast<int>(std::floor(cameraGridY)), core::kChunkSize);
    const bool cameraMoved = !m_hasCam || cx != m_camChunkX || cy != m_camChunkY;

    if (cameraMoved) {
        m_hasCam = true;
        m_camChunkX = cx;
        m_camChunkY = cy;

        // 1) 卸载离开范围的已 Loaded chunk（Loading 的由 HandleFinished 取消）
        std::vector<std::uint64_t> toUnload;
        for (const std::uint64_t key : m_active) {
            const int px = static_cast<std::int32_t>(key >> 32);
            const int py = static_cast<std::int32_t>(key & 0xffffffffu);
            if (!InRange(px, py)) {
                toUnload.push_back(key);
            }
        }
        std::size_t unloaded = 0;
        for (const std::uint64_t key : toUnload) {
            const int px = static_cast<std::int32_t>(key >> 32);
            const int py = static_cast<std::int32_t>(key & 0xffffffffu);
            Chunk* chunk = FindActiveChunk(px, py);
            if (chunk != nullptr && chunk->State() == ChunkState::Loaded) {
                chunk->SetState(ChunkState::Unloading);
                chunk->SetState(ChunkState::Unloaded);  // 不变量：Unloaded ⇒ 数据已清空
                RemoveActiveChunk(px, py);
                m_active.erase(key);
                m_stats.loadedChunks = std::max(0, m_stats.loadedChunks - 1);
                ++m_stats.totalUnloaded;
                ++unloaded;
                ZL_DEBUG(kLogModule, "chunk ({},{}) Loaded→Unloading→Unloaded", px, py);
            }
        }
        if (unloaded > 0) {
            const std::size_t cells = m_world.RemoveEmptyCells();
            if (cells > 0) {
                ZL_DEBUG(kLogModule, "回收空 cell {} 个", cells);
            }
        }

        // 2) 新进入范围的 chunk 入队（正方形范围，Chebyshev 半径）
        for (int dy = -m_loadRadius; dy <= m_loadRadius; ++dy) {
            for (int dx = -m_loadRadius; dx <= m_loadRadius; ++dx) {
                const int nx = cx + dx;
                const int ny = cy + dy;
                const std::uint64_t key = PackKey(nx, ny);
                if (m_active.contains(key) || m_giveUp.contains(key) || m_pendingSet.contains(key)) {
                    continue;
                }
                m_pendingRequests.push_back(key);
                m_pendingSet.insert(key);
            }
        }

        // 3) 清理已离开范围的失败记录（离开再回来时允许重试）
        for (auto it = m_failAttempts.begin(); it != m_failAttempts.end();) {
            const int px = static_cast<std::int32_t>(it->first >> 32);
            const int py = static_cast<std::int32_t>(it->first & 0xffffffffu);
            if (!InRange(px, py)) {
                m_giveUp.erase(it->first);
                it = m_failAttempts.erase(it);
            } else {
                ++it;
            }
        }
    }

    // 4) 按帧预算创建/派发（相机静止时也继续消费积压）
    DrainRequests(kRequestBudget);
}

void ChunkManager::DrainRequests(int budget) {
    int dispatched = 0;
    while (dispatched < budget && !m_pendingRequests.empty()) {
        const std::uint64_t key = m_pendingRequests.front();
        m_pendingRequests.pop_front();
        m_pendingSet.erase(key);
        const int px = static_cast<std::int32_t>(key >> 32);
        const int py = static_cast<std::int32_t>(key & 0xffffffffu);
        if (!InRange(px, py) || m_active.contains(key) || m_giveUp.contains(key)) {
            continue;  // 相机又动了：过期请求直接丢弃
        }
        RequestLoad(px, py);
        ++dispatched;
    }
}

void ChunkManager::RequestLoad(int chunkX, int chunkY) {
    const CellCoord cellCoord{FloorDiv(chunkX, core::kChunksPerCell),
                              FloorDiv(chunkY, core::kChunksPerCell)};
    Cell& cell = m_world.GetOrCreateCell(cellCoord.cx, cellCoord.cy);
    const ChunkLocal local = ChunkLocalOf(chunkX, chunkY);
    Chunk& chunk = cell.GetOrCreateChunk(local.lcx, local.lcy);
    chunk.SetState(ChunkState::Loading);
    m_active.insert(PackKey(chunkX, chunkY));
    ++m_stats.loadingChunks;
    ZL_DEBUG(kLogModule, "chunk ({},{}) Unloaded→Loading", chunkX, chunkY);

    auto job = std::make_shared<LoadJob>();
    job->chunkX = chunkX;
    job->chunkY = chunkY;
    auto queue = m_queue;
    const MapLoader::Meta meta = m_meta;  // 后台线程独立副本
    m_tasks.Submit([job, queue, meta]() {
        const auto begin = std::chrono::steady_clock::now();
        job->ok = MapLoader::LoadChunk(meta, job->chunkX, job->chunkY, &job->data, &job->error);
        const auto end = std::chrono::steady_clock::now();
        job->ms = std::chrono::duration<double, std::milli>(end - begin).count();
        std::lock_guard lock(queue->mutex);
        queue->items.push_back(job);
    });
}

void ChunkManager::ProcessResults() {
    for (int handled = 0; handled < kAttachBudget; ++handled) {
        std::shared_ptr<LoadJob> job;
        {
            std::lock_guard lock(m_queue->mutex);
            if (m_queue->items.empty()) {
                return;
            }
            job = std::move(m_queue->items.front());
            m_queue->items.pop_front();
        }
        HandleFinished(job);
    }
}

void ChunkManager::HandleFinished(const std::shared_ptr<LoadJob>& job) {
    const std::uint64_t key = PackKey(job->chunkX, job->chunkY);
    m_stats.loadingChunks = std::max(0, m_stats.loadingChunks - 1);
    Chunk* chunk = FindActiveChunk(job->chunkX, job->chunkY);

    if (!job->ok) {
        ++m_stats.totalFailed;
        ZL_WARN(kLogModule, "chunk ({},{}) 加载失败: {}", job->chunkX, job->chunkY, job->error);
        if (chunk != nullptr) {
            chunk->SetState(ChunkState::Unloaded);
        }
        RemoveActiveChunk(job->chunkX, job->chunkY);
        m_active.erase(key);

        const int attempts = ++m_failAttempts[key];
        if (InRange(job->chunkX, job->chunkY) && attempts < kMaxLoadAttempts) {
            ZL_DEBUG(kLogModule, "chunk ({},{}) 重试 {}/{}", job->chunkX, job->chunkY, attempts,
                     kMaxLoadAttempts);
            RequestLoad(job->chunkX, job->chunkY);
        } else if (InRange(job->chunkX, job->chunkY)) {
            m_giveUp.insert(key);
            ZL_ERROR(kLogModule, "chunk ({},{}) 连续 {} 次加载失败，放弃（离开范围后自动重试）",
                     job->chunkX, job->chunkY, kMaxLoadAttempts);
        }
        return;
    }

    if (InRange(job->chunkX, job->chunkY) && chunk != nullptr) {
        ApplyChunkData(job->data, *chunk);
        chunk->SetState(ChunkState::Loaded);
        ++m_stats.loadedChunks;
        ++m_stats.totalLoaded;
        m_recentLoadMs.push_back(job->ms);
        if (m_recentLoadMs.size() > kRecentLoadCap) {
            m_recentLoadMs.pop_front();
        }
        ZL_DEBUG(kLogModule, "chunk ({},{}) Loading→Loaded（{:.2f} ms，{} 对象，{} 房间）",
                 job->chunkX, job->chunkY, job->ms, job->data.objects.size(),
                 job->data.rooms.size());
    } else {
        // 解析完成时相机已离开：取消（数据直接丢弃）
        if (chunk != nullptr) {
            chunk->SetState(ChunkState::Unloaded);
        }
        RemoveActiveChunk(job->chunkX, job->chunkY);
        m_active.erase(key);
        ++m_stats.totalCancelled;
        ZL_DEBUG(kLogModule, "chunk ({},{}) 解析完成但相机已离开 → 取消（{:.2f} ms）", job->chunkX,
                 job->chunkY, job->ms);
    }
}

Chunk* ChunkManager::FindActiveChunk(int chunkX, int chunkY) {
    const CellCoord cellCoord{FloorDiv(chunkX, core::kChunksPerCell),
                              FloorDiv(chunkY, core::kChunksPerCell)};
    Cell* cell = m_world.GetCell(cellCoord.cx, cellCoord.cy);
    if (cell == nullptr) {
        return nullptr;
    }
    const ChunkLocal local = ChunkLocalOf(chunkX, chunkY);
    return cell->GetChunk(local.lcx, local.lcy);
}

double ChunkManager::LoadMsPercentile(double percentile) const {
    if (m_recentLoadMs.empty()) {
        return 0.0;
    }
    std::vector<double> sorted(m_recentLoadMs.begin(), m_recentLoadMs.end());
    std::sort(sorted.begin(), sorted.end());
    const double clamped = std::clamp(percentile, 0.0, 1.0);
    const std::size_t index = static_cast<std::size_t>(
        std::min(static_cast<double>(sorted.size()) - 1.0,
                 std::floor(clamped * static_cast<double>(sorted.size()))));
    return sorted[index];
}

bool ChunkManager::RemoveActiveChunk(int chunkX, int chunkY) {
    const CellCoord cellCoord{FloorDiv(chunkX, core::kChunksPerCell),
                              FloorDiv(chunkY, core::kChunksPerCell)};
    Cell* cell = m_world.GetCell(cellCoord.cx, cellCoord.cy);
    if (cell == nullptr) {
        return false;
    }
    const ChunkLocal local = ChunkLocalOf(chunkX, chunkY);
    return cell->RemoveChunk(local.lcx, local.lcy);
}

}  // namespace zl::world
