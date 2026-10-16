#include "physics/PhysicsSyncSystem.h"

#include <btBulletDynamicsCommon.h>

#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "physics/PhysicsWorld.h"

namespace zl::physics {

void PhysicsSyncSystem::Update(ecs::EcsWorld& world, float dt) {
    if (m_physics == nullptr) {
        return;
    }

    // 1) 物理积分（body 速度已由 MovementSystem 在上一系统写入）
    m_physics->Step(dt);

    // 2) 组件 ← 体：位置以物理为权威；3) 组件 ← 体：实际速度回写
    entt::registry& registry = world.Registry();
    registry.view<ecs::RigidBody, ecs::Position>().each([&](const ecs::Entity e,
                                                            ecs::RigidBody& rigid,
                                                            ecs::Position& position) {
        if (rigid.body == nullptr) {
            return;
        }
        const btTransform& transform = rigid.body->getWorldTransform();
        position.x = static_cast<float>(transform.getOrigin().x());
        position.y = static_cast<float>(transform.getOrigin().y());

        if (auto* velocity = registry.try_get<ecs::Velocity>(e)) {
            const btVector3 linear = rigid.body->getLinearVelocity();
            velocity->x = static_cast<float>(linear.x());
            velocity->y = static_cast<float>(linear.y());
        }
    });
}

}  // namespace zl::physics
