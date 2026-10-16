#pragma once

namespace zl::ecs {
class EcsWorld;
}

namespace zl::characters {

// M4-T3：Velocity → 位移（docs/00 §6.1 固定链：PhysicsSyncSystem 之前）。
// 有 RigidBody 的实体：把 Velocity 写给物理体（积分在 Bullet step 内完成，
// PhysicsSyncSystem 回读位置）；无 RigidBody：直接积分 Position（简单/无物理实体）。
class MovementSystem {
public:
    void Update(ecs::EcsWorld& world, float dt);
};

}  // namespace zl::characters
