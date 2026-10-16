#include "game/ZombieFactory.h"

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "physics/PhysicsWorld.h"

namespace zl::game {

ecs::Entity SpawnZombie(ecs::EcsWorld& world, physics::PhysicsWorld* physics, float px, float py) {
    entt::registry& registry = world.Registry();
    const ecs::Entity e = world.Create();
    registry.emplace<ecs::Position>(e, ecs::Position{px, py, 0});
    registry.emplace<ecs::Velocity>(e, ecs::Velocity{});
    registry.emplace<ecs::Collider>(e, ecs::Collider{});
    registry.emplace<ecs::SpriteRef>(
        e, ecs::SpriteRef{ecs::HashName("character.placeholder"), 0, 0xFFFFFFFFu});
    registry.emplace<ecs::Facing>(e, ecs::Facing{});
    registry.emplace<ecs::AnimState>(e, ecs::AnimState{});
    registry.emplace<ecs::Health>(e, ecs::Health{});  // M5-T1：死亡流程（HP<=0 → 尸体）
    registry.emplace<ecs::Needs>(e, ecs::Needs{});

    // M4-T3：物理胶囊（位置/速度由 Bullet 接管）
    if (physics != nullptr) {
        const ecs::Collider def;
        ecs::RigidBody rigid;
        rigid.body = physics->CreateCharacterBody(def.radius, def.height, px, py, 0);
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
    return e;
}

}  // namespace zl::game
