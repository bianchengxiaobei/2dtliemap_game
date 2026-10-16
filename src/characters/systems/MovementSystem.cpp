#include "characters/systems/MovementSystem.h"

#include <btBulletDynamicsCommon.h>

#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"

namespace zl::characters {

void MovementSystem::Update(ecs::EcsWorld& world, float dt) {
    entt::registry& registry = world.Registry();
    registry.view<ecs::Position, ecs::Velocity>().each(
        [&](const ecs::Entity e, ecs::Position& position, ecs::Velocity& velocity) {
            const auto* rigid = registry.try_get<ecs::RigidBody>(e);
            if (rigid != nullptr && rigid->body != nullptr) {
                // 物理实体：速度交给 Bullet（XY 平面；z 锁死不参与运动）
                rigid->body->activate();
                rigid->body->setLinearVelocity(btVector3(velocity.x, velocity.y, 0.0f));
            } else {
                // 无物理实体：简单积分
                position.x += velocity.x * dt;
                position.y += velocity.y * dt;
            }
        });
}

}  // namespace zl::characters
