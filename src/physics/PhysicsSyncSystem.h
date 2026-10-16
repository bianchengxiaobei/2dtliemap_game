#pragma once

namespace zl::ecs {
class EcsWorld;
}

namespace zl::physics {

class PhysicsWorld;

// M4-T3：组件 ↔ Bullet 双向同步（docs/00 §6.1 固定链：MovementSystem 之后）。
// 每 tick：
//   1) Step(dt)——Bullet 积分（速度已由 MovementSystem 写入体）；
//   2) 体 transform → Position（权威位置来自物理）；
//   3) 体实际速度 → Velocity 回写（撞墙后速度归零/沿墙分量保留，供动画与 AI 使用）。
class PhysicsSyncSystem {
public:
    explicit PhysicsSyncSystem(PhysicsWorld* physics) : m_physics(physics) {}

    void Update(ecs::EcsWorld& world, float dt);

private:
    PhysicsWorld* m_physics = nullptr;
};

}  // namespace zl::physics
