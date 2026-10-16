#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "ai/AStar.h"
#include "ai/Passability.h"

namespace zl::core {
class TaskScheduler;
}
namespace zl::world {
class Chunk;
}

namespace zl::ai {

class PathSnapshot;

// 地图 chunk 边界（= MapLoader::Meta 的 bounds，构造时拷贝；运行期不变）
struct MapChunkBounds {
    int x0 = 0;
    int y0 = 0;
    int width = 0;
    int height = 0;
};

// 一条完成的寻路结果（主线程经 TryTakeResult 消费）
struct PathResult {
    std::uint64_t queryId = 0;
    AStarResult path;
};

// M4-T6：寻路服务（docs/00 §3.2 线程模型）。
//
//  - passability 位图缓存：chunk 加载时构建、卸载时删除（主线程，随
//    ChunkManager 回调调用）——"chunk 级缓存、局部重建"；
//  - Submit：主线程**立即拷贝只读快照**（区域 = 起终点 chunk 包围盒 + 1 chunk 边距、
//    夹取地图范围），后台线程 A* 只读快照——查询期间流式加载不影响在途计算；
//  - 结果回投到带锁队列（TryTakeResult FIFO）；工作线程只碰共享 Core，
//    服务先于线程池销毁也安全（在途任务持 Core 引用，结果静默丢弃）。
//
// tasks == nullptr 时降级为同步执行（单测 / 无后台模式）。
class PathfindingService {
public:
    struct Stats {
        std::uint64_t submitted = 0;  // 全部请求（含即时失败）
        std::uint64_t completed = 0;  // 已出结果
        std::uint64_t found = 0;
        std::uint64_t unreachable = 0;
        std::uint64_t invalid = 0;    // 越界 / 超距
        std::uint64_t budget = 0;     // 展开超预算
        double lastMs = 0.0;          // 最近一次查询耗时（工作线程内计时）
        double avgMs = 0.0;           // 累计平均
        int pending = 0;              // 在途查询
    };

    PathfindingService(core::TaskScheduler* tasks, MapChunkBounds bounds,
                       AStarOptions options = {});
    ~PathfindingService();

    PathfindingService(const PathfindingService&) = delete;
    PathfindingService& operator=(const PathfindingService&) = delete;

    // chunk 生命周期（主线程；与物理静态体回调同处调用）
    void OnChunkLoaded(const world::Chunk& chunk);
    void OnChunkUnloading(int chunkX, int chunkY);

    // 通行查询（M7-T3）：世界格坐标（z=0 地面层位图）；未加载 chunk 一律视为不可通行
    // （与 PathSnapshot 语义一致；供脚本 World.IsPassable 使用）。
    bool IsPassable(int gx, int gy) const;

    // 发起寻路（主线程）。返回 queryId（> 0），与结果一一对应。
    std::uint64_t Submit(int startX, int startY, int goalX, int goalY);

    // 取一条完成结果（主线程，FIFO；无则返回 false）
    bool TryTakeResult(PathResult* out);

    Stats GetStats() const;  // 加锁拷贝
    std::size_t CachedChunks() const { return m_chunks.size(); }

private:
    struct Core;  // 跨线程共享状态（结果队列 + 统计）；定义在 .cpp
    struct QueryJob {
        std::uint64_t queryId = 0;
        int startX = 0;
        int startY = 0;
        int goalX = 0;
        int goalY = 0;
        std::shared_ptr<const PathSnapshot> snapshot;
        AStarOptions options;
    };

    std::shared_ptr<PathSnapshot> BuildSnapshot(int sx, int sy, int gx, int gy) const;
    static void ExecuteQuery(const std::shared_ptr<Core>& core, const QueryJob& job);

    core::TaskScheduler* m_tasks = nullptr;
    MapChunkBounds m_bounds;
    AStarOptions m_options;

    std::unordered_map<std::uint64_t, ChunkPassability> m_chunks;  // 主线程独占
    std::uint64_t m_nextQueryId = 1;

    std::shared_ptr<Core> m_core;
};

}  // namespace zl::ai
