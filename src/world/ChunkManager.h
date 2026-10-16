#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "core/TaskScheduler.h"
#include "world/MapLoader.h"
#include "world/World.h"

namespace zl::world {

class Chunk;

// M3-T4：以相机为中心的 chunk 流式加载/卸载（docs/00 §4.3）。
//
// 线程模型（docs/00 §3.2 铁律）：
//   - 后台线程只做"读文件 → ChunkData（纯数据）"，产出入本类的线程安全队列；
//   - 主线程 Update()（差集/派发/卸载）与 ProcessResults()（挂接/取消/重试）
//     是唯一接触 World 的地方。
// 生命周期：Unloaded→Loading→Loaded→Unloading→Unloaded（Chunk 状态机）。
//
// 结果队列不用 TaskScheduler::SubmitResult 而自带：后台任务经 shared_ptr 持有队列与
// 作业对象，manager 先于任务析构时也不会写悬空指针（任务结果静默丢弃）。
class ChunkManager {
public:
    struct Stats {
        int loadedChunks = 0;   // 当前驻留（Loaded）
        int loadingChunks = 0;  // 后台解析中（Loading）
        std::uint64_t totalLoaded = 0;
        std::uint64_t totalUnloaded = 0;
        std::uint64_t totalCancelled = 0;  // 解析完成时相机已离开 → 直接丢弃
        std::uint64_t totalFailed = 0;
    };

    ChunkManager(World& world, MapLoader::Meta meta, core::TaskScheduler& tasks,
                 int loadRadius = core::kChunkLoadRadius);

    // 相机中心（世界格坐标，允许小数）变化时调用；相机未跨 chunk 时为空操作。
    void Update(float cameraGridX, float cameraGridY);

    // 主线程每帧调用：消费后台解析结果（挂接/取消/重试/放弃）
    void ProcessResults();

    const Stats& GetStats() const { return m_stats; }
    const std::deque<double>& RecentLoadMs() const { return m_recentLoadMs; }
    bool HasPendingWork() const { return m_stats.loadingChunks > 0; }

    int LoadRadius() const { return m_loadRadius; }

private:
    struct LoadJob {
        int chunkX = 0;
        int chunkY = 0;
        ChunkData data;
        bool ok = false;
        std::string error;
        double ms = 0.0;
    };

    struct JobQueue {
        std::mutex mutex;
        std::deque<std::shared_ptr<LoadJob>> items;
    };

    static std::uint64_t PackKey(int chunkX, int chunkY);
    static int Chebyshev(int ax, int ay, int bx, int by);

    void RequestLoad(int chunkX, int chunkY);
    void DrainRequests(int budget);  // 每帧限量创建/派发，避免相机快移时主线程尖峰
    void HandleFinished(const std::shared_ptr<LoadJob>& job);
    Chunk* FindActiveChunk(int chunkX, int chunkY);
    bool RemoveActiveChunk(int chunkX, int chunkY);  // 返回是否移除成功
    bool InRange(int chunkX, int chunkY) const;

    static constexpr int kMaxLoadAttempts = 3;
    static constexpr int kRequestBudget = 4;  // 每次 Update 最多派发的 chunk 数
    static constexpr int kAttachBudget = 8;   // 每次 ProcessResults 最多挂接的结果数
    static constexpr std::size_t kRecentLoadCap = 4096;

    World& m_world;
    MapLoader::Meta m_meta;  // 值持有：后台任务捕获副本使用
    core::TaskScheduler& m_tasks;
    int m_loadRadius;

    std::shared_ptr<JobQueue> m_queue = std::make_shared<JobQueue>();

    bool m_hasCam = false;
    int m_camChunkX = 0;
    int m_camChunkY = 0;

    std::unordered_set<std::uint64_t> m_active;              // 本 manager 拥有（驻留或加载中）
    std::deque<std::uint64_t> m_pendingRequests;             // 待创建/派发（按帧预算消费）
    std::unordered_set<std::uint64_t> m_pendingSet;          // m_pendingRequests 去重
    std::unordered_map<std::uint64_t, int> m_failAttempts;   // 失败重试计数
    std::unordered_set<std::uint64_t> m_giveUp;              // 达重试上限，离开范围后清除

    Stats m_stats;
    std::deque<double> m_recentLoadMs;
};

}  // namespace zl::world
