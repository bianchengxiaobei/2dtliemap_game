#include <doctest/doctest.h>

#include <cmath>

#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/components/ProgressionComponents.h"
#include "characters/systems/CombatSystem.h"
#include "characters/systems/MovementSystem.h"
#include "characters/systems/NeedsSystem.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "items/InventoryOps.h"

using namespace zl;

using zl::characters::CombatSystem;
using zl::characters::MovementSystem;
using zl::characters::NeedsParams;
using zl::characters::NeedsSystem;
using zl::ecs::EcsWorld;
using zl::ecs::Health;
using zl::ecs::MoveIntent;
using zl::ecs::Needs;
using zl::ecs::PlayerTag;
using zl::ecs::Position;
using zl::ecs::Velocity;

namespace {

NeedsParams InertParams() {
    NeedsParams params;
    params.gameSecondsPerDay = 10.0f;
    params.hungerPerDay = 0.1f;
    params.thirstPerDay = 0.2f;
    params.fatiguePerDay = 0.05f;
    params.hungerDamageThreshold = 1.0f;
    params.thirstMoveThreshold = 1.0f;
    params.fatigueMoveThreshold = 1.0f;
    params.fatigueDamageThreshold = 1.0f;
    params.hungerCritical = 1.0f;
    params.thirstCritical = 1.0f;
    params.starvationHpPerSecond = 0.0f;
    params.dehydrationHpPerSecond = 0.0f;
    return params;
}

}  // namespace

TEST_CASE("NeedsSystem：玩家需求按固定 tick 衰减、钳制，非玩家不衰减") {
    EcsWorld world;
    entt::registry& registry = world.Registry();
    const ecs::Entity player = world.Create();
    registry.emplace<PlayerTag>(player);
    registry.emplace<Health>(player, Health{100.0f, 100.0f});
    registry.emplace<Needs>(player, Needs{0.5f, 0.5f, 0.5f});

    const ecs::Entity zombie = world.Create();
    registry.emplace<Health>(zombie, Health{100.0f, 100.0f});
    registry.emplace<Needs>(zombie, Needs{0.5f, 0.5f, 0.5f});

    NeedsSystem system(InertParams());
    system.Update(world, 10.0f);

    const Needs& afterOne = registry.get<Needs>(player);
    CHECK(afterOne.hunger == doctest::Approx(0.6f));
    CHECK(afterOne.thirst == doctest::Approx(0.7f));
    CHECK(afterOne.fatigue == doctest::Approx(0.55f));

    system.Update(world, 1000.0f);
    const Needs& clamped = registry.get<Needs>(player);
    CHECK(clamped.hunger == 1.0f);
    CHECK(clamped.thirst == 1.0f);
    CHECK(clamped.fatigue == 1.0f);

    const Needs& untouched = registry.get<Needs>(zombie);
    CHECK(untouched.hunger == 0.5f);
    CHECK(untouched.thirst == 0.5f);
    CHECK(untouched.fatigue == 0.5f);
}

TEST_CASE("NeedsSystem：阈值降低移速/伤害，危险阈值产生持续伤害") {
    EcsWorld world;
    entt::registry& registry = world.Registry();
    const ecs::Entity player = world.Create();
    registry.emplace<PlayerTag>(player);
    registry.emplace<Health>(player, Health{100.0f, 100.0f});
    registry.emplace<Needs>(player, Needs{0.6f, 0.6f, 0.6f});

    NeedsSystem system(NeedsParams{});
    system.Update(world, 0.0f);
    {
        const Needs& needs = registry.get<Needs>(player);
        CHECK(needs.moveScale == 1.0f);
        CHECK(needs.damageScale == 1.0f);
        CHECK(needs.warning == 0);
        CHECK(needs.healthDrainPerSecond == 0.0f);
    }

    auto& needs = registry.get<Needs>(player);
    needs.hunger = 0.80f;
    needs.thirst = 0.80f;
    needs.fatigue = 0.80f;
    system.Update(world, 0.0f);
    CHECK(needs.moveScale < 1.0f);
    CHECK(needs.damageScale < 1.0f);
    CHECK(needs.warning == 1);
    CHECK(registry.get<Health>(player).current == 100.0f);

    needs.hunger = 0.95f;
    needs.thirst = 0.95f;
    needs.fatigue = 0.95f;
    system.Update(world, 1.0f);
    CHECK(needs.warning == 2);
    CHECK(needs.healthDrainPerSecond > 0.0f);
    CHECK(registry.get<Health>(player).current < 100.0f);
    REQUIRE(registry.try_get<ecs::LastDamage>(player) != nullptr);
    CHECK(registry.get<ecs::LastDamage>(player).cause ==
          ecs::DamageCause::Dehydration);
}

TEST_CASE("NeedsSystem：食物/饮水恢复后派生警告与倍率回落") {
    EcsWorld world;
    entt::registry& registry = world.Registry();
    const ecs::Entity player = world.Create();
    registry.emplace<PlayerTag>(player);
    registry.emplace<Health>(player, Health{100.0f, 100.0f});
    registry.emplace<Needs>(player, Needs{0.90f, 0.90f, 0.20f});

    NeedsSystem system(NeedsParams{});
    system.Update(world, 0.0f);
    CHECK(registry.get<Needs>(player).warning == 2);

    items::UseEffect effect;
    effect.hunger = 0.35f;
    effect.thirst = 0.40f;
    auto& needs = registry.get<Needs>(player);
    auto& health = registry.get<Health>(player);
    items::ApplyUseEffect(effect, health.max, &health.current, &needs.hunger,
                          &needs.thirst, &needs.fatigue);
    system.Update(world, 0.0f);

    CHECK(needs.hunger == doctest::Approx(0.55f));
    CHECK(needs.thirst == doctest::Approx(0.50f));
    CHECK(needs.warning == 0);
    CHECK(needs.damageScale == 1.0f);
    CHECK(needs.moveScale == 1.0f);
}

TEST_CASE("Needs 派生倍率：MovementSystem 降速、CombatSystem 降伤") {
    EcsWorld world;
    entt::registry& registry = world.Registry();
    const ecs::Entity player = world.Create();
    registry.emplace<PlayerTag>(player);
    registry.emplace<Position>(player, Position{0.0f, 0.0f, 0});
    registry.emplace<Velocity>(player, Velocity{});
    registry.emplace<MoveIntent>(player, MoveIntent{4.0f, 0.0f});
    Needs playerNeeds;
    playerNeeds.moveScale = 0.5f;
    registry.emplace<Needs>(player, playerNeeds);

    MovementSystem movement;
    movement.Update(world, 1.0f / 60.0f);
    CHECK(registry.get<Velocity>(player).x == doctest::Approx(2.0f));

    const ecs::Entity attacker = world.Create();
    registry.emplace<Position>(attacker, Position{0.0f, 0.0f, 0});
    ecs::AttackIntent attack;
    attack.dirX = 1.0f;
    attack.dirY = 0.0f;
    attack.damage = 40.0f;
    attack.range = 2.0f;
    attack.arcDegrees = 180.0f;
    attack.pending = true;
    registry.emplace<ecs::AttackIntent>(attacker, attack);
    Needs attackerNeeds;
    attackerNeeds.damageScale = 0.5f;
    registry.emplace<Needs>(attacker, attackerNeeds);

    const ecs::Entity target = world.Create();
    registry.emplace<Position>(target, Position{1.0f, 0.0f, 0});
    registry.emplace<Health>(target, Health{100.0f, 100.0f});

    CombatSystem combat;
    combat.Update(world, 1.0f / 60.0f);
    CHECK(registry.get<Health>(target).current == doctest::Approx(80.0f));
}

TEST_CASE("M19-F1 生存技能/Perk 降低需求增长") {
    EcsWorld world;
    entt::registry& registry = world.Registry();
    const ecs::Entity player = world.Create();
    registry.emplace<PlayerTag>(player);
    registry.emplace<Health>(player, Health{100.0f, 100.0f});
    registry.emplace<Needs>(player, Needs{0.0f, 0.0f, 0.0f});
    ecs::Progression progression;
    progression.skills[ecs::SkillIndex(ecs::SkillKind::Survival)] =
        ecs::SkillProgress{4, 0.0f};
    progression.hungerDecayScale = 0.8f;
    progression.thirstDecayScale = 0.8f;
    progression.fatigueDecayScale = 0.8f;
    registry.emplace<ecs::Progression>(player, progression);

    NeedsSystem system(InertParams());
    system.Update(world, 10.0f);
    const Needs& needs = registry.get<Needs>(player);
    CHECK(needs.hunger == doctest::Approx(0.08f));
    CHECK(needs.thirst == doctest::Approx(0.16f));
    CHECK(needs.fatigue == doctest::Approx(0.04f));
}
