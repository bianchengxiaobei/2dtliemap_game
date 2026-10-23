#pragma once

namespace zl::core {
class EventBus;
}

namespace zl::ecs {
class EcsWorld;
}

namespace zl::physics {
class PhysicsWorld;
}

namespace zl::characters {

// M5-T1：死亡流程。固定链位置：PhysicsSync 之后（读到本 tick 最新位置）、Animation 之前。
// M5-T6 起两段式：HP<=0 → 1) 进入倒地窗口（Dying，停步/冻结速度，保留角色资源播放
// death 片段；击退仍可滑行）→ 2) 窗口结束才销毁物理体 + 延迟销毁实体 + 生成尸体实体
// （完整表现链：硬直→倒地→尸体）。销毁与尸体创建都在 tick 末 Flush 统一生效。
// M7-T4：events 非空时，阶段 2 发 OnZombieDead / OnPlayerDeath（排队派发）。
class DeathSystem {
public:
    static constexpr float kDefaultFallSeconds = 0.5f;  // 倒地窗口（config combat.deathFallSeconds）

    explicit DeathSystem(physics::PhysicsWorld* physics,
                         float fallSeconds = kDefaultFallSeconds,
                         core::EventBus* events = nullptr)
        : m_physics(physics), m_fallSeconds(fallSeconds), m_events(events) {}

    void Update(ecs::EcsWorld& world, float dt);

private:
    physics::PhysicsWorld* m_physics = nullptr;  // 可为 nullptr（无物理的测试/降级）
    float m_fallSeconds = kDefaultFallSeconds;
    core::EventBus* m_events = nullptr;          // 可为 nullptr（不发死亡事件）
};

}  // namespace zl::characters
