#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ecs/EntityHash.h"

namespace zl::ecs {

class EcsWorld;

// M4-T2：chunk 粒度均匀网格空间索引（docs/00 §6.1 空间索引）。
//
// 约定：
//  - 每 tick 由 SpatialIndexSystem 调一次 `Update`：扫 `view<Position>` 增量维护——
//    新建实体入桶、跨 chunk 移动换桶、已销毁（含入队销毁）实体清出；
//  - 查询为只读、回调式（不分配、零拷贝）；**z / 距离过滤由调用方做**
//    （索引按 (cx,cy) 二维分桶，楼层不参与分桶）；
//  - 不允许在拿到索引里实体后就假设其有效：跨 tick 使用一律 `EcsWorld::Alive()` 复核；
//  - 想移除索引跟踪必须销毁实体（只 remove<Position> 不销毁不在支持范围）。
class SpatialIndex {
public:
    void Update(EcsWorld& world);
    void Clear();

    // 闭区间 chunk 范围遍历（回调收到 entt::entity）
    template <typename Fn>
    void ForEachInChunks(int cx0, int cy0, int cx1, int cy1, Fn&& fn) const {
        for (int cy = cy0; cy <= cy1; ++cy) {
            for (int cx = cx0; cx <= cx1; ++cx) {
                const auto it = m_buckets.find(PackChunk(cx, cy));
                if (it == m_buckets.end()) {
                    continue;
                }
                for (const Entity e : it->second) {
                    fn(e);
                }
            }
        }
    }

    std::size_t EntityCount() const { return m_entityChunk.size(); }
    std::size_t BucketCount() const { return m_buckets.size(); }  // 非空桶数
    std::size_t BucketSize(int cx, int cy) const {
        const auto it = m_buckets.find(PackChunk(cx, cy));
        return it == m_buckets.end() ? 0u : it->second.size();
    }

private:
    static std::uint64_t PackChunk(int cx, int cy) noexcept {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32) |
               static_cast<std::uint64_t>(static_cast<std::uint32_t>(cy));
    }
    void RemoveFromBucket(Entity e, std::uint64_t key);

    std::unordered_map<std::uint64_t, std::vector<Entity>> m_buckets;  // chunk → 实体
    std::unordered_map<Entity, std::uint64_t, EntityHash> m_entityChunk;  // 实体 → 所在桶
};

}  // namespace zl::ecs
