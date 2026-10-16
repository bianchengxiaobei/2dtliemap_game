#include "ecs/SpatialIndex.h"

#include <cmath>

#include "core/Constants.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "world/WorldCoords.h"

namespace zl::ecs {

void SpatialIndex::Update(EcsWorld& world) {
    // 1) 清出已销毁 / 入队销毁的实体（Alive 已含延迟销毁语义）
    for (auto it = m_entityChunk.begin(); it != m_entityChunk.end();) {
        if (!world.Alive(it->first)) {
            RemoveFromBucket(it->first, it->second);
            it = m_entityChunk.erase(it);
        } else {
            ++it;
        }
    }

    // 2) 扫 Position 增量维护：新实体入桶、跨 chunk 移动换桶
    //    （已入销毁队列的实体 registry 里仍 valid，必须按 Alive 跳过，否则会被重新入桶）
    world.Registry().view<Position>().each([&](const Entity e, const Position& p) {
        if (!world.Alive(e)) {
            return;
        }
        const world::ChunkCoord cc =
            world::ChunkOf(static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)));
        const std::uint64_t key = PackChunk(cc.cx, cc.cy);
        const auto it = m_entityChunk.find(e);
        if (it == m_entityChunk.end()) {
            m_buckets[key].push_back(e);
            m_entityChunk.emplace(e, key);
        } else if (it->second != key) {
            RemoveFromBucket(e, it->second);
            m_buckets[key].push_back(e);
            it->second = key;
        }
    });
}

void SpatialIndex::Clear() {
    m_buckets.clear();
    m_entityChunk.clear();
}

void SpatialIndex::RemoveFromBucket(Entity e, std::uint64_t key) {
    const auto it = m_buckets.find(key);
    if (it == m_buckets.end()) {
        return;
    }
    std::vector<Entity>& bucket = it->second;
    for (std::size_t i = 0; i < bucket.size(); ++i) {
        if (bucket[i] == e) {
            bucket[i] = bucket.back();  // swap-erase（桶内顺序无意义）
            bucket.pop_back();
            break;
        }
    }
    if (bucket.empty()) {
        m_buckets.erase(it);
    }
}

}  // namespace zl::ecs
