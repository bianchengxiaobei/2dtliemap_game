#include "ecs/EcsWorld.h"

#include <utility>

#include "core/Log.h"

namespace zl::ecs {

EcsWorld::Entity EcsWorld::Create() {
    const Entity e = m_registry.create();
    const std::uint64_t id = m_nextStableId++;
    m_registry.emplace<StableId>(e, StableId{id});
    m_byStableId.emplace(id, e);
    ++m_aliveCount;
    return e;
}

void EcsWorld::QueueCreate(SpawnFn fn) {
    if (fn) {
        m_createQueue.push_back(std::move(fn));
    }
}

void EcsWorld::Destroy(Entity e) {
    if (e == kNull || !m_registry.valid(e)) {
        ZL_TRACE("ecs", "Destroy 忽略无效句柄");
        return;
    }
    if (!m_destroyQueued.insert(e).second) {
        return;  // 已在队列里，重复销毁是空操作
    }
    m_destroyQueue.push_back(e);
}

bool EcsWorld::Alive(Entity e) const {
    return e != kNull && m_registry.valid(e) && !m_destroyQueued.contains(e);
}

StableId EcsWorld::IdOf(Entity e) const {
    if (e == kNull || !m_registry.valid(e)) {
        return {};
    }
    if (const auto* sid = m_registry.try_get<StableId>(e)) {
        return *sid;
    }
    return {};
}

EcsWorld::Entity EcsWorld::FindById(std::uint64_t stableId) const {
    const auto it = m_byStableId.find(stableId);
    if (it == m_byStableId.end()) {
        return kNull;
    }
    // 已入销毁队列的按“不存在”处理：跨模块拿到旧 ID 不会操作到将死的实体
    return Alive(it->second) ? it->second : kNull;
}

void EcsWorld::Flush() {
    if (m_destroyQueue.empty() && m_createQueue.empty()) {
        return;
    }

    // 先统一销毁：摘除稳定 ID 映射后再还给实体池
    for (const Entity e : m_destroyQueue) {
        if (!m_registry.valid(e)) {
            continue;
        }
        if (const auto* sid = m_registry.try_get<StableId>(e)) {
            m_byStableId.erase(sid->id);
        }
        m_registry.destroy(e);
        --m_aliveCount;
    }
    m_destroyQueue.clear();
    m_destroyQueued.clear();

    // 再统一创建；swap 出来执行，fn 里再 QueueCreate 的排到下轮（保证本轮有限）
    std::vector<SpawnFn> spawns;
    spawns.swap(m_createQueue);
    for (auto& fn : spawns) {
        (void)fn(*this);
    }
}

void EcsWorld::Update(float dt) {
    Flush();  // 上一帧残留（正常为空）
    // 系统内 Destroy：Alive() 立即为假，但内存延迟回收（本 tick 内 registry.valid 仍真，
    // 别的系统读到的是本帧最后状态；请一律用 Alive() 判断存活）
    m_systems.Run(*this, dt);
    Flush();  // tick 末统一回收
}

}  // namespace zl::ecs
