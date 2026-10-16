#pragma once

namespace zl::ecs {
class EcsWorld;
}

namespace zl::physics {
class PhysicsWorld;
}

namespace zl::characters {

// M5-T1：死亡流程。固定链位置：PhysicsSync 之后（读到本 tick 最新位置）、Animation 之前。
// 判定：Health.current <= 0 → 1) 销毁物理体（有 RigidBody 时，实体销毁不会自动回收 body）；
// 2) 延迟销毁实体；3) 延迟创建尸体实体（同格 Position + Corpse 溯源 + corpse 精灵）。
// 销毁与尸体创建都在 tick 末 Flush 统一生效（EcsWorld 语义），本 tick 内实体仍可被读出。
class DeathSystem {
public:
    explicit DeathSystem(physics::PhysicsWorld* physics) : m_physics(physics) {}

    void Update(ecs::EcsWorld& world);

private:
    physics::PhysicsWorld* m_physics = nullptr;  // 可为 nullptr（无物理的测试/降级）
};

}  // namespace zl::characters
