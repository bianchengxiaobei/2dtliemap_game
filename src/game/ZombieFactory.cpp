#include "game/ZombieFactory.h"

#include <algorithm>

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "core/EventBus.h"
#include "core/GameEvents.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/ZombieVariants.h"
#include "physics/PhysicsWorld.h"

namespace zl::game {

namespace {
constexpr const char* kZombieSpriteName = "character.zombie";
}  // namespace

ecs::Entity SpawnZombie(ecs::EcsWorld& world, physics::PhysicsWorld* physics, float px, float py,
                        core::EventBus* events, ecs::ZombieVariantKind variantKind) {
    const ZombieVariantProfile& profile = ZombieVariantProfileFor(variantKind);
    const float visualScale = std::clamp(profile.visualScale, 0.5f, 2.0f);
    const ecs::Collider baseCollider;
    const ecs::Collider collider{baseCollider.radius * visualScale,
                                 baseCollider.height * visualScale,
                                 baseCollider.category, baseCollider.mask};
    entt::registry& registry = world.Registry();
    const ecs::Entity e = world.Create();
    registry.emplace<ecs::Position>(e, ecs::Position{px, py, 0});
    registry.emplace<ecs::Velocity>(e, ecs::Velocity{});
    registry.emplace<ecs::Collider>(e, collider);
    registry.emplace<ecs::SpriteRef>(
        e, ecs::SpriteRef{ecs::HashName(kZombieSpriteName), 0, profile.tint});
    registry.emplace<ecs::Facing>(e, ecs::Facing{});
    registry.emplace<ecs::AnimState>(e, ecs::AnimState{});
    registry.emplace<ecs::ActionState>(e, ecs::ActionState{});
    ecs::Health health;
    health.max = 100.0f * profile.healthScale;
    health.current = health.max;
    registry.emplace<ecs::Health>(e, health);
    registry.emplace<ecs::Needs>(e, ecs::Needs{});
    registry.emplace<ecs::ZombieVariant>(e, MakeZombieVariant(variantKind));

    if (physics != nullptr) {
        ecs::RigidBody rigid;
        rigid.body = physics->CreateCharacterBody(collider.radius, collider.height, px, py, 0);
        registry.emplace<ecs::RigidBody>(e, rigid);
    }

    // M5-T2/T3：AI 状态机 + 感知（初始"可疑点" = 出生位置，避免无感知事件时巡点去 (0,0)）
    registry.emplace<ecs::AiState>(e, ecs::AiState{});
    ecs::PerceptionState percep;
    percep.lastKnownX = px;
    percep.lastKnownY = py;
    registry.emplace<ecs::PerceptionState>(e, percep);
    registry.emplace<ecs::MoveIntent>(e, ecs::MoveIntent{});
    registry.emplace<ecs::PathRequest>(e, ecs::PathRequest{});
    registry.emplace<ecs::PathFollow>(e, ecs::PathFollow{});
    registry.emplace<ecs::LodState>(e, ecs::LodState{});        // M5-T5：LodSystem 逐帧写档位
    registry.emplace<ecs::AttackIntent>(e, ecs::AttackIntent{});  // M5-T6：攻击脉冲 → 伤害管线
    if (events != nullptr) {  // M7-T4：OnZombieSpawn（排队：tick 末统一派发）
        events->EmitQueued(core::kOnZombieSpawn,
                           core::ZombieEvent{world.IdOf(e).id, 0, px, py, 0});
    }
    return e;
}

}  // namespace zl::game
