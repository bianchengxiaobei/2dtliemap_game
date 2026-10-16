#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <entt/entt.hpp>

#include "ecs/EntityHash.h"
#include "ecs/SystemScheduler.h"
#include "ecs/components/CommonComponents.h"

namespace zl::ecs {

// M4-T1：ECS 世界封装（docs/00 §6.1、docs/02 §5.6）。
//
//  - 实体创建/销毁 API：**销毁一律走延迟队列**，tick 末 Flush 统一回收（遍历中增删安全）；
//  - StableId 自动附着 + 双向映射：跨模块 / 脚本 / 存档只用稳定 ID，
//    绝不用 entt::entity 数值跨界；已入销毁队列的实体按“不存在”处理；
//  - registry 主线程独占：后台线程（寻路快照等）不得触碰 registry，只读导出的纯数据快照；
//  - 系统更新顺序由 SystemScheduler 集中注册（固定链见 SystemScheduler.h 注释）。
class EcsWorld {
public:
    using Entity = entt::entity;
    static constexpr Entity kNull = entt::null;

    // ---- 创建 ----

    // 立即创建并附着 StableId（主线程逻辑用；遍历 view 过程中请改用 QueueCreate）
    Entity Create();

    // 延迟创建：fn 在 Flush 时执行（可返回 kNull 放弃创建）
    using SpawnFn = std::function<Entity(EcsWorld&)>;
    void QueueCreate(SpawnFn fn);

    // ---- 销毁（延迟）----

    // 入延迟销毁队列；对无效句柄 / 重复入队是安全空操作
    void Destroy(Entity e);

    // 存活 = 句柄有效且未在销毁队列中
    bool Alive(Entity e) const;

    // ---- 稳定 ID ----

    StableId IdOf(Entity e) const;                     // 无 StableId 组件返回 {0}
    Entity FindById(std::uint64_t stableId) const;     // 未找到/已入销毁队列返回 kNull

    // ---- tick 驱动 ----

    // 处理延迟队列：先统一销毁、再统一创建（创建 fn 里再 QueueCreate 的排到下轮）
    void Flush();

    // 每 tick：Flush → 按注册顺序跑系统 → Flush（tick 末统一回收）
    void Update(float dt);

    // ---- 访问 ----

    entt::registry& Registry() { return m_registry; }
    const entt::registry& Registry() const { return m_registry; }
    SystemScheduler& Systems() { return m_systems; }

    // ---- 统计（日志 / 测试）----

    // registry 内实体数（含已入销毁队列、尚未 Flush 的）
    std::size_t AliveCount() const { return m_aliveCount; }
    std::size_t PendingDestroyCount() const { return m_destroyQueue.size(); }
    std::size_t StableIdCount() const { return m_byStableId.size(); }

private:
    entt::registry m_registry;
    SystemScheduler m_systems;

    // 稳定 ID → 实体 双向映射（实体 → ID 走 StableId 组件）
    std::unordered_map<std::uint64_t, Entity> m_byStableId;
    std::uint64_t m_nextStableId = 1;

    std::vector<Entity> m_destroyQueue;
    std::unordered_set<Entity, EntityHash> m_destroyQueued;  // 去重（O(1) 查“是否待销毁”）
    std::vector<SpawnFn> m_createQueue;
    std::size_t m_aliveCount = 0;  // == 已 Create 未回收（EnTT 3.16 registry 无 size()，自维护）
};

}  // namespace zl::ecs
