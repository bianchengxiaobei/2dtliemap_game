#pragma once

class btRigidBody;

namespace zl::ecs {

// M4-T3：Bullet 刚体句柄（**非拥有**：生命周期由 physics::PhysicsWorld 管理）。
// 有本组件的实体由物理驱动：MovementSystem 把 Velocity 写进体，
// PhysicsSyncSystem 在 step 后回读 transform → Position，并把实际速度回写给 Velocity。
struct RigidBody {
    btRigidBody* body = nullptr;
};

}  // namespace zl::ecs
