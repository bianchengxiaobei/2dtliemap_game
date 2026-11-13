#include "world/ChunkManager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <utility>
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
                           int loadRadius, int requestBudget, int attachBudget,
                           double attachBudgetMs, int residentRadius,
                           bool enforceMapBounds)
    : m_world(world),
      m_meta(std::move(meta)),
      m_tasks(tasks),
      m_loadRadius(std::max(0, loadRadius)),
      m_residentRadius(residentRadius >= 0
                           ? std::max(std::max(0, loadRadius), residentRadius)
                           : std::max(0, loadRadius) + 2),
      m_requestBudget(std::max(1, requestBudget)),
      m_attachBudget(std::max(1, attachBudget)),
      m_attachBudgetMs(std::max(0.0, attachBudgetMs)),
      m_enforceMapBounds(enforceMapBounds) {
    m_streamingConfig.loadRadius = m_loadRadius;
    m_streamingConfig.residentRadius = m_residentRadius;
}

std::uint64_t ChunkManager::PackKey(int chunkX, int chunkY) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkX)) << 32) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkY));
}

int ChunkManager::Chebyshev(int ax, int ay, int bx, int by) {
    return std::max(std::abs(ax - bx), std::abs(ay - by));
}

bool ChunkManager::InMapBounds(int chunkX, int chunkY) const {
    return !m_enforceMapBounds || MapLoader::InBounds(m_meta, chunkX, chunkY);
}

bool ChunkManager::InRange(int chunkX, int chunkY) const {
    if (!m_hasLoadFocus || !InMapBounds(chunkX, chunkY)) {
        return false;
    }
    for (const LoadWindow& window : m_loadWindows) {
        if (Chebyshev(chunkX, chunkY, window.chunkX, window.chunkY) <=
            m_loadRadius) {
            return true;
        }
    }
    return false;
}

void ChunkManager::SetPrediction(float velocityX, float velocityY) {
    m_predictionVelocityX = std::isfinite(velocityX) ? velocityX : 0.0f;
    m_predictionVelocityY = std::isfinite(velocityY) ? velocityY : 0.0f;
}

void ChunkManager::SetPredictionConfig(int maxLookaheadChunks,
                                       float lookaheadSeconds) {
    m_streamingConfig.maxLookaheadChunks = std::max(0, maxLookaheadChunks);
    m_streamingConfig.lookaheadSeconds = std::max(0.0f, lookaheadSeconds);
    m_hasLoadFocus = false;  // force the next Update to recompute the window
}

void ChunkManager::UpdateFocuses(const std::vector<FocusPoint>& focuses) {
    std::vector<FocusPoint> valid;
    valid.reserve(focuses.size());
    for (const FocusPoint& focus : focuses) {
        if (std::isfinite(focus.gridX) && std::isfinite(focus.gridY)) {
            valid.push_back(focus);
        }
    }
    if (valid.empty()) {
        return;
    }
    if (valid.size() == 1) {
        Update(valid.front().gridX, valid.front().gridY);
        return;
    }

    std::vector<LoadWindow> cameraWindows;
    cameraWindows.reserve(valid.size());
    for (const FocusPoint& focus : valid) {
        cameraWindows.push_back(LoadWindow{
            FloorDiv(static_cast<int>(std::floor(focus.gridX)), core::kChunkSize),
            FloorDiv(static_cast<int>(std::floor(focus.gridY)), core::kChunkSize)});
    }
    const bool cameraMoved =
        !m_hasCam || cameraWindows.size() != m_cameraWindows.size() ||
        !std::equal(cameraWindows.begin(), cameraWindows.end(),
                    m_cameraWindows.begin(),
                    [](const LoadWindow& left, const LoadWindow& right) {
                        return left.chunkX == right.chunkX &&
                               left.chunkY == right.chunkY;
                    });
    if (cameraMoved) {
        m_hasCam = true;
        m_cameraWindows = cameraWindows;
        m_camChunkX = cameraWindows.front().chunkX;
        m_camChunkY = cameraWindows.front().chunkY;
        UpdateResident(cameraWindows);
    }

    std::vector<LoadWindow> windows = cameraWindows;
    std::sort(windows.begin(), windows.end(),
              [](const LoadWindow& left, const LoadWindow& right) {
                  return left.chunkX != right.chunkX
                             ? left.chunkX < right.chunkX
                             : left.chunkY < right.chunkY;
              });
    windows.erase(std::unique(windows.begin(), windows.end(),
                              [](const LoadWindow& left,
                                 const LoadWindow& right) {
                                  return left.chunkX == right.chunkX &&
                                         left.chunkY == right.chunkY;
                              }),
                  windows.end());
    const bool loadMoved =
        !m_hasLoadFocus || windows.size() != m_loadWindows.size() ||
        !std::equal(windows.begin(), windows.end(), m_loadWindows.begin(),
                    [](const LoadWindow& left, const LoadWindow& right) {
                        return left.chunkX == right.chunkX &&
                               left.chunkY == right.chunkY;
                    });
    m_stats.lookaheadChunks = 0;
    m_stats.predictedChunkX = windows.front().chunkX;
    m_stats.predictedChunkY = windows.front().chunkY;
    m_stats.focusCount = static_cast<int>(windows.size());
    if (!cameraMoved && !loadMoved) {
        DrainRequests(m_requestBudget);
        return;
    }

    m_hasLoadFocus = true;
    m_loadWindows = std::move(windows);
    m_loadChunkX = m_loadWindows.front().chunkX;
    m_loadChunkY = m_loadWindows.front().chunkY;

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
            if (m_onChunkUnloading) {
                m_onChunkUnloading(*chunk, px, py);
            }
            chunk->SetState(ChunkState::Unloading);
            chunk->SetState(ChunkState::Unloaded);
            RemoveActiveChunk(px, py);
            m_active.erase(key);
            m_stats.loadedChunks = std::max(0, m_stats.loadedChunks - 1);
            ++m_stats.totalUnloaded;
            ++unloaded;
        }
    }
    if (unloaded > 0) {
        const std::size_t cells = m_world.RemoveEmptyCells();
        if (cells > 0) {
            ZL_DEBUG(kLogModule, "recycled empty cells {}", cells);
        }
    }

    struct Candidate {
        int chunkX = 0;
        int chunkY = 0;
        int distance = 0;
    };
    std::vector<Candidate> candidates;
    for (const LoadWindow& window : m_loadWindows) {
        for (int dy = -m_loadRadius; dy <= m_loadRadius; ++dy) {
            for (int dx = -m_loadRadius; dx <= m_loadRadius; ++dx) {
                const int nx = window.chunkX + dx;
                const int ny = window.chunkY + dy;
                if (!InMapBounds(nx, ny)) {
                    continue;
                }
                const std::uint64_t key = PackKey(nx, ny);
                if (m_active.contains(key) || m_giveUp.contains(key) ||
                    m_pendingSet.contains(key)) {
                    continue;
                }
                candidates.push_back(Candidate{
                    nx, ny, std::max(std::abs(dx), std::abs(dy))});
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& left, const Candidate& right) {
                  if (left.distance != right.distance) {
                      return left.distance < right.distance;
                  }
                  if (left.chunkX != right.chunkX) {
                      return left.chunkX < right.chunkX;
                  }
                  return left.chunkY < right.chunkY;
              });
    for (const Candidate& candidate : candidates) {
        const std::uint64_t key = PackKey(candidate.chunkX, candidate.chunkY);
        if (m_active.contains(key) || m_giveUp.contains(key) ||
            m_pendingSet.contains(key)) {
            continue;
        }
        m_pendingRequests.push_back(key);
        m_pendingSet.insert(key);
    }

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
    DrainRequests(m_requestBudget);
}

void ChunkManager::Update(float cameraGridX, float cameraGridY) {
    const int cx = FloorDiv(static_cast<int>(std::floor(cameraGridX)), core::kChunkSize);
    const int cy = FloorDiv(static_cast<int>(std::floor(cameraGridY)), core::kChunkSize);
    const bool cameraMoved = !m_hasCam || cx != m_camChunkX ||
                             cy != m_camChunkY ||
                             m_cameraWindows.size() != 1;

    const ChunkOffset offset = ComputeLookahead(
        m_predictionVelocityX, m_predictionVelocityY, m_streamingConfig);
    int loadX = cx + offset.dx;
    int loadY = cy + offset.dy;
    if (m_enforceMapBounds && m_meta.chunksX > 0 && m_meta.chunksY > 0) {
        const int maxChunkX = m_meta.chunkX0 + m_meta.chunksX - 1;
        const int maxChunkY = m_meta.chunkY0 + m_meta.chunksY - 1;
        loadX = std::clamp(loadX, m_meta.chunkX0, maxChunkX);
        loadY = std::clamp(loadY, m_meta.chunkY0, maxChunkY);
    }
    const bool loadMoved =
        !m_hasLoadFocus || loadX != m_loadChunkX || loadY != m_loadChunkY;

    m_stats.lookaheadChunks = std::max(std::abs(offset.dx), std::abs(offset.dy));
    m_stats.predictedChunkX = loadX;
    m_stats.predictedChunkY = loadY;
    m_stats.focusCount = 1;
    if (!cameraMoved && !loadMoved) {
        DrainRequests(m_requestBudget);
        return;
    }

    if (cameraMoved) {
        m_hasCam = true;
        m_camChunkX = cx;
        m_camChunkY = cy;
        m_cameraWindows = {LoadWindow{cx, cy}};
        UpdateResident(m_cameraWindows);
    }

    m_hasLoadFocus = true;
    m_loadChunkX = loadX;
    m_loadChunkY = loadY;
    m_loadWindows.clear();
    m_loadWindows.push_back(LoadWindow{loadX, loadY});

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
            if (m_onChunkUnloading) {
                m_onChunkUnloading(*chunk, px, py);  // 数据仍可读：关联资源在此移除
            }
            chunk->SetState(ChunkState::Unloading);
            chunk->SetState(ChunkState::Unloaded);  // 不变量：Unloaded ⇒ 数据已清空
            RemoveActiveChunk(px, py);
            m_active.erase(key);
            m_stats.loadedChunks = std::max(0, m_stats.loadedChunks - 1);
            ++m_stats.totalUnloaded;
            ++unloaded;
            ZL_TRACE(kLogModule, "chunk ({},{}) Loaded→Unloading→Unloaded", px, py);
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
            const int nx = loadX + dx;
            const int ny = loadY + dy;
            if (!InMapBounds(nx, ny)) {
                continue;
            }
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

    // 4) 按帧预算创建/派发（相机静止时也继续消费积压）
    DrainRequests(m_requestBudget);
}

void ChunkManager::UpdateResident(const std::vector<LoadWindow>& windows) {
    m_resident.clear();
    for (const LoadWindow& window : windows) {
        for (int dy = -m_residentRadius; dy <= m_residentRadius; ++dy) {
            for (int dx = -m_residentRadius; dx <= m_residentRadius; ++dx) {
                const int nx = window.chunkX + dx;
                const int ny = window.chunkY + dy;
                if (!InMapBounds(nx, ny)) {
                    continue;
                }
                m_resident.insert(PackKey(nx, ny));
            }
        }
    }
    m_stats.residentChunks = static_cast<int>(m_resident.size());
}

bool ChunkManager::IsResident(int chunkX, int chunkY) const {
    return m_resident.contains(PackKey(chunkX, chunkY));
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
    ZL_TRACE(kLogModule, "chunk ({},{}) Unloaded→Loading", chunkX, chunkY);

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
    const auto begin = std::chrono::steady_clock::now();
    int handled = 0;
    for (; handled < m_attachBudget; ++handled) {
        if (handled > 0) {
            const double elapsed = std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() - begin)
                                       .count();
            if (elapsed >= m_attachBudgetMs) {
                ++m_stats.attachBudgetHits;
                break;
            }
        }
        std::shared_ptr<LoadJob> job;
        {
            std::lock_guard lock(m_queue->mutex);
            if (m_queue->items.empty()) {
                break;
            }
            job = std::move(m_queue->items.front());
            m_queue->items.pop_front();
        }
        HandleFinished(job);
    }
    m_stats.lastAttachMs = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - begin)
                               .count();
    m_stats.maxAttachBatch = std::max(m_stats.maxAttachBatch, handled);
}

void ChunkManager::ForEachLoadedChunk(
    const std::function<void(const Chunk&)>& callback) {
    if (!callback) {
        return;
    }
    std::vector<std::uint64_t> keys(m_active.begin(), m_active.end());
    std::sort(keys.begin(), keys.end());
    for (const std::uint64_t key : keys) {
        const int px = static_cast<std::int32_t>(key >> 32);
        const int py = static_cast<std::int32_t>(key & 0xffffffffu);
        const Chunk* chunk = FindActiveChunk(px, py);
        if (chunk != nullptr && chunk->State() == ChunkState::Loaded) {
            callback(*chunk);
        }
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
            ZL_TRACE(kLogModule, "chunk ({},{}) 重试 {}/{}", job->chunkX, job->chunkY, attempts,
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
        // M10-T2：母地图基线必须在应用存档 diff 之前捕获（diff 之后由存档系统叠加）。
        chunk->CaptureBaseline();
        chunk->SetState(ChunkState::Loaded);
        ++m_stats.loadedChunks;
        ++m_stats.totalLoaded;
        m_recentLoadMs.push_back(job->ms);
        if (m_recentLoadMs.size() > kRecentLoadCap) {
            m_recentLoadMs.pop_front();
        }
        ZL_TRACE(kLogModule, "chunk ({},{}) Loading→Loaded（{:.2f} ms，{} 对象，{} 房间）",
                 job->chunkX, job->chunkY, job->ms, job->data.objects.size(),
                 job->data.rooms.size());
        if (m_onChunkLoaded) {
            m_onChunkLoaded(*chunk, job->chunkX, job->chunkY);  // 物理静态体等在此生成
        }
    } else {
        // 解析完成时相机已离开：取消（数据直接丢弃）
        if (chunk != nullptr) {
            chunk->SetState(ChunkState::Unloaded);
        }
        RemoveActiveChunk(job->chunkX, job->chunkY);
        m_active.erase(key);
        ++m_stats.totalCancelled;
        ZL_TRACE(kLogModule, "chunk ({},{}) 解析完成但相机已离开 → 取消（{:.2f} ms）", job->chunkX,
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

void ChunkManager::SetChunkCallbacks(ChunkCallback onLoaded, ChunkCallback onUnloading) {
    m_onChunkLoaded = std::move(onLoaded);
    m_onChunkUnloading = std::move(onUnloading);
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
