#include "characters/systems/MovementSystem.h"

#include <algorithm>

#include <btBulletDynamicsCommon.h>

#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"

namespace zl::characters {

void MovementSystem::Update(ecs::EcsWorld& world, float dt) {
    entt::registry& registry = world.Registry();
    registry.view<ecs::Position, ecs::Velocity>().each(
        [&](const ecs::Entity e, ecs::Position& position, ecs::Velocity& velocity) {
            // M4-T4：有 MoveIntent（输入/AI 意图）时先落到 Velocity——意图本 tick 生效
            if (const auto* intent = registry.try_get<ecs::MoveIntent>(e); intent != nullptr) {
                velocity.x = intent->x;
                velocity.y = intent->y;
            }
            if (registry.all_of<ecs::PlayerTag>(e)) {
                if (const auto* needs = registry.try_get<ecs::Needs>(e); needs != nullptr) {
                    const float scale = std::clamp(needs->moveScale, 0.05f, 1.0f);
                    velocity.x *= scale;
                    velocity.y *= scale;
                }
                if (const auto* pain = registry.try_get<ecs::Pain>(e); pain != nullptr) {
                    const float scale = std::clamp(pain->moveScale, 0.05f, 1.0f);
                    velocity.x *= scale;
                    velocity.y *= scale;
                }
                if (const auto* armor = registry.try_get<ecs::DamageResist>(e);
                    armor != nullptr) {
                    const float scale = std::clamp(armor->moveScale, 0.5f, 1.0f);
                    velocity.x *= scale;
                    velocity.y *= scale;
                }
                if (const auto* climate = registry.try_get<ecs::Climate>(e);
                    climate != nullptr) {
                    const float scale = std::clamp(climate->moveScale, 0.05f, 1.0f);
                    velocity.x *= scale;
                    velocity.y *= scale;
                }
            }
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
