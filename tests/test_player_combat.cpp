#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "characters/components/CombatComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/systems/CombatRules.h"
#include "characters/systems/CombatSystem.h"
#include "ecs/EcsWorld.h"
#include "game/PlayerCombat.h"
#include "game/ZombieVariants.h"
#include "items/ItemDef.h"
#include "items/ItemRegistry.h"

using namespace zl;

namespace {

items::ItemDef MakeTestWeapon(float weight) {
    items::ItemDef weapon;
    weapon.id = "test_weapon";
    weapon.name = "test weapon";
    weapon.type = items::ItemType::Weapon;
    weapon.weight = weight;
    weapon.combat.damage = 30.0f;
    weapon.combat.range = 1.6f;
    weapon.combat.arcDegrees = 100.0f;
    weapon.combat.cooldownSeconds = 0.50f;
    weapon.combat.knockback = 3.0f;
    weapon.combat.staggerSeconds = 0.35f;
    weapon.combat.wearPerHit = 0.01f;
    return weapon;
}

items::WeaponAttackParams BaseParams() {
    items::WeaponAttackParams params;
    params.damage = 40.0f;
    params.range = 1.7f;
    params.arcDegrees = 100.0f;
    params.cooldownSeconds = 0.50f;
    params.knockback = 3.0f;
    params.staggerSeconds = 0.35f;
    return params;
}

}  // namespace

TEST_CASE("M30-T3: light attack uses weapon data and fills derived fields") {
    items::ItemDef knife = MakeTestWeapon(0.4f);
    knife.combat.damage = 22.0f;
    knife.combat.range = 1.5f;
    knife.combat.cooldownSeconds = 0.40f;
    knife.combat.knockback = 2.0f;
    knife.combat.staggerSeconds = 0.25f;

    const game::PlayerMeleeAttackPlan plan =
        game::ResolvePlayerMeleeAttack(&knife, BaseParams(), false,
                                       characters::DefaultCombatRuleConfig());
    CHECK_FALSE(plan.heavy);
    CHECK(plan.params.damage == doctest::Approx(22.0f));
    CHECK(plan.params.range == doctest::Approx(1.5f));
    CHECK(plan.params.arcDegrees == doctest::Approx(100.0f));
    CHECK(plan.params.cooldownSeconds == doctest::Approx(0.40f));
    CHECK(plan.params.knockback == doctest::Approx(2.0f));
    CHECK(plan.params.staggerSeconds == doctest::Approx(0.25f));
    CHECK(plan.params.wearPerHit == doctest::Approx(0.01f));
    CHECK(plan.params.staminaCost == doctest::Approx(8.8f));
    CHECK(plan.params.windupSeconds == doctest::Approx(0.044f));
    CHECK(plan.params.poiseDamage == doctest::Approx(6.6f));
}

TEST_CASE("M30-T3: heavy attack derives from light and applies explicit overrides") {
    items::ItemDef weapon = MakeTestWeapon(3.0f);
    weapon.heavy = items::WeaponHeavyCombat{};
    weapon.heavy->damage = 70.0f;
    weapon.heavy->range = 2.1f;
    weapon.heavy->staminaCost = 25.0f;
    weapon.heavy->poiseDamage = 30.0f;

    const game::PlayerMeleeAttackPlan plan =
        game::ResolvePlayerMeleeAttack(&weapon, BaseParams(), true,
                                       characters::DefaultCombatRuleConfig());
    CHECK(plan.heavy);
    CHECK(plan.params.damage == doctest::Approx(70.0f));
    CHECK(plan.params.range == doctest::Approx(2.1f));
    CHECK(plan.params.staminaCost == doctest::Approx(25.0f));
    CHECK(plan.params.poiseDamage == doctest::Approx(30.0f));
    CHECK(plan.params.arcDegrees == doctest::Approx(90.0f));
    CHECK(plan.params.cooldownSeconds == doctest::Approx(0.775f));
    CHECK(plan.params.knockback == doctest::Approx(3.75f));
    CHECK(plan.params.staggerSeconds == doctest::Approx(0.4375f));
    CHECK(plan.params.wearPerHit == doctest::Approx(0.0125f));
    CHECK(plan.params.windupSeconds == doctest::Approx(0.314f));
}

TEST_CASE("M30-T3: legacy weapon and fists derive heavy attack from the table") {
    items::ItemDef weapon = MakeTestWeapon(5.0f);
    weapon.combat.damage = 40.0f;
    weapon.combat.cooldownSeconds = 0.60f;
    const game::PlayerMeleeAttackPlan legacy =
        game::ResolvePlayerMeleeAttack(&weapon, BaseParams(), true,
                                       characters::DefaultCombatRuleConfig());
    CHECK(legacy.params.damage == doctest::Approx(66.0f));
    CHECK(legacy.params.range == doctest::Approx(1.7f));
    CHECK(legacy.params.arcDegrees == doctest::Approx(90.0f));
    CHECK(legacy.params.cooldownSeconds == doctest::Approx(0.93f));
    CHECK(legacy.params.staminaCost == doctest::Approx(32.0f));
    CHECK(legacy.params.windupSeconds == doctest::Approx(0.35f));
    CHECK(legacy.params.poiseDamage == doctest::Approx(30.0f));

    const game::PlayerMeleeAttackPlan fists =
        game::ResolvePlayerMeleeAttack(nullptr, BaseParams(), true,
                                       characters::DefaultCombatRuleConfig());
    CHECK(fists.params.damage == doctest::Approx(66.0f));
    CHECK(fists.params.range == doctest::Approx(1.8f));
    CHECK(fists.params.staminaCost == doctest::Approx(14.4f));
    CHECK(fists.params.windupSeconds == doctest::Approx(0.30f));
    CHECK(fists.params.poiseDamage == doctest::Approx(18.0f));
}

TEST_CASE("M30-T3: empty swing uses the facing direction") {
    constexpr float kIso = 0.70710678f;
    float x = 0.0f;
    float y = 0.0f;
    game::FacingAttackDirection(ecs::Direction::Down, &x, &y);
    CHECK(x == doctest::Approx(kIso));
    CHECK(y == doctest::Approx(kIso));
    game::FacingAttackDirection(ecs::Direction::Left, &x, &y);
    CHECK(x == doctest::Approx(-kIso));
    CHECK(y == doctest::Approx(kIso));
    game::FacingAttackDirection(ecs::Direction::Right, &x, &y);
    CHECK(x == doctest::Approx(kIso));
    CHECK(y == doctest::Approx(-kIso));
    game::FacingAttackDirection(ecs::Direction::Up, &x, &y);
    CHECK(x == doctest::Approx(-kIso));
    CHECK(y == doctest::Approx(-kIso));
}

TEST_CASE("M30-T3: repeated attacks stop on insufficient stamina and trigger exhaustion") {
    ecs::EcsWorld world;
    characters::CombatSystem combat;
    const ecs::Entity player = world.Create();
    world.Registry().emplace<ecs::CombatStamina>(
        player, ecs::CombatStamina{100.0f, 100.0f, 0.0f, 0.0f});
    world.Registry().emplace<ecs::CombatStance>(player, ecs::CombatStance{});

    for (int i = 0; i < 5; ++i) {
        CHECK(combat.TrySpendStamina(world, player, 18.0f));
    }
    CHECK(world.Registry().get<ecs::CombatStamina>(player).current ==
          doctest::Approx(10.0f));
    CHECK_FALSE(combat.TrySpendStamina(world, player, 18.0f));
    CHECK(world.Registry().get<ecs::CombatStamina>(player).current ==
          doctest::Approx(10.0f));

    world.Registry().get<ecs::CombatStamina>(player) =
        ecs::CombatStamina{10.0f, 100.0f, 0.0f, 0.0f};
    CHECK(combat.TrySpendStamina(world, player, 6.0f));
    CHECK(world.Registry().get<ecs::CombatStamina>(player).exhausted ==
          doctest::Approx(0.6f));
    CHECK_FALSE(combat.TrySpendStamina(world, player, 1.0f));
}

TEST_CASE("M30-T3: empty swing still consumes the swing without a target") {
    ecs::EcsWorld world;
    characters::CombatSystem combat;
    const ecs::Entity player = world.Create();
    auto& registry = world.Registry();
    registry.emplace<ecs::Position>(player, ecs::Position{0.0f, 0.0f, 0});
    ecs::AttackIntent attack;
    attack.dirX = 0.0f;
    attack.dirY = 1.0f;
    attack.range = 1.5f;
    attack.arcDegrees = 100.0f;
    attack.damage = 10.0f;
    attack.windupSeconds = 0.10f;
    attack.windupLeft = 0.10f;
    attack.pending = true;
    registry.emplace<ecs::AttackIntent>(player, attack);

    combat.Update(world, 0.01f);
    CHECK(registry.get<ecs::AttackIntent>(player).pending);
    CHECK(registry.get<ecs::ActionState>(player).action ==
          ecs::ActorAction::Attacking);
    CHECK(registry.get<ecs::ActionState>(player).duration ==
          doctest::Approx(0.10f));
    combat.Update(world, 0.10f);
    CHECK_FALSE(registry.get<ecs::AttackIntent>(player).pending);
    CHECK(combat.GetStats().swings == 1u);
    CHECK(combat.GetStats().hits == 0u);
}

TEST_CASE("M30-T4: weapon block overrides fall back to global rules") {
    const game::PlayerBlockPlan fists = game::ResolvePlayerBlock(nullptr);
    CHECK(fists.blockReduction == doctest::Approx(-1.0f));
    CHECK(fists.blockStaminaCost == doctest::Approx(-1.0f));

    items::ItemDef legacy = MakeTestWeapon(1.0f);
    const game::PlayerBlockPlan legacyPlan = game::ResolvePlayerBlock(&legacy);
    CHECK(legacyPlan.blockReduction == doctest::Approx(-1.0f));
    CHECK(legacyPlan.blockStaminaCost == doctest::Approx(-1.0f));

    items::ItemDef shield = MakeTestWeapon(2.0f);
    shield.combat.blockReduction = 0.55f;
    shield.combat.blockStaminaCost = 16.0f;
    const game::PlayerBlockPlan plan = game::ResolvePlayerBlock(&shield);
    CHECK(plan.blockReduction == doctest::Approx(0.55f));
    CHECK(plan.blockStaminaCost == doctest::Approx(16.0f));

    shield.combat.blockReduction = 0.99f;
    shield.combat.blockStaminaCost = 150.0f;
    const game::PlayerBlockPlan clamped = game::ResolvePlayerBlock(&shield);
    CHECK(clamped.blockReduction == doctest::Approx(0.95f));
    CHECK(clamped.blockStaminaCost == doctest::Approx(100.0f));
}

TEST_CASE("M30-T4: dodge direction normalizes, falls back to facing, and uses shared speed") {
    const characters::CombatRuleConfig rules =
        characters::DefaultCombatRuleConfig();
    constexpr float kIso = 0.70710678f;

    const game::PlayerDodgePlan diagonal = game::ResolvePlayerDodge(
        3.0f, 4.0f, ecs::Direction::Down, rules);
    CHECK(diagonal.dirX == doctest::Approx(0.6f));
    CHECK(diagonal.dirY == doctest::Approx(0.8f));
    CHECK(diagonal.speed == doctest::Approx(1.5f / 0.28f));

    const game::PlayerDodgePlan left = game::ResolvePlayerDodge(
        0.0f, 0.0f, ecs::Direction::Left, rules);
    CHECK(left.dirX == doctest::Approx(-kIso));
    CHECK(left.dirY == doctest::Approx(kIso));
    CHECK(left.speed == doctest::Approx(characters::DodgeSpeed(rules)));

    const game::PlayerDodgePlan invalid = game::ResolvePlayerDodge(
        std::numeric_limits<float>::quiet_NaN(), 0.0f, ecs::Direction::Right,
        rules);
    CHECK(invalid.dirX == doctest::Approx(kIso));
    CHECK(invalid.dirY == doctest::Approx(-kIso));
}

TEST_CASE("M30-T7: axe light/heavy variant TTK, stamina and block tradeoffs") {
    items::ItemRegistry registry;
    std::string error;
    const std::string path = std::string(ZL_SOURCE_DIR) + "/assets/items.json";
    REQUIRE_MESSAGE(registry.LoadFile(path, &error), error);
    const items::ItemDef* axe = registry.Find("axe");
    REQUIRE(axe != nullptr);

    items::WeaponAttackParams base;
    base.damage = 40.0f;
    base.range = 1.7f;
    base.arcDegrees = 100.0f;
    base.cooldownSeconds = 0.5f;
    base.knockback = 3.0f;
    base.staggerSeconds = 0.35f;
    const characters::CombatRuleConfig rules =
        characters::DefaultCombatRuleConfig();
    const game::PlayerMeleeAttackPlan light =
        game::ResolvePlayerMeleeAttack(axe, base, false, rules);
    const game::PlayerMeleeAttackPlan heavy =
        game::ResolvePlayerMeleeAttack(axe, base, true, rules);

    struct Result {
        int hits = 0;
        float ttk = 0.0f;
        float stamina = 0.0f;
        int poiseHits = 0;
    };
    const auto evaluate = [&](ecs::ZombieVariantKind kind,
                              const game::PlayerMeleeAttackPlan& plan) {
        const game::ZombieVariantProfile& profile =
            game::ZombieVariantProfileFor(kind);
        const float hp = 100.0f * profile.healthScale;
        const float damage =
            plan.params.damage * (1.0f - profile.damageReduction);
        Result result;
        result.hits = static_cast<int>(std::ceil(hp / damage));
        result.ttk = plan.params.windupSeconds +
                     static_cast<float>(result.hits - 1) *
                         plan.params.cooldownSeconds;
        result.stamina =
            static_cast<float>(result.hits) * plan.params.staminaCost;
        result.poiseHits = static_cast<int>(
            std::ceil(game::ZombiePoiseMaxFor(kind, rules) /
                      plan.params.poiseDamage));
        return result;
    };

    const Result lightNormal =
        evaluate(ecs::ZombieVariantKind::Normal, light);
    const Result lightFast = evaluate(ecs::ZombieVariantKind::Fast, light);
    const Result lightStrong =
        evaluate(ecs::ZombieVariantKind::Strong, light);
    const Result lightArmored =
        evaluate(ecs::ZombieVariantKind::Armored, light);
    const Result heavyNormal =
        evaluate(ecs::ZombieVariantKind::Normal, heavy);
    const Result heavyFast = evaluate(ecs::ZombieVariantKind::Fast, heavy);
    const Result heavyStrong =
        evaluate(ecs::ZombieVariantKind::Strong, heavy);
    const Result heavyArmored =
        evaluate(ecs::ZombieVariantKind::Armored, heavy);

    CHECK(lightNormal.hits == 2);
    CHECK(lightFast.hits == 2);
    CHECK(lightStrong.hits == 3);
    CHECK(lightArmored.hits == 5);
    CHECK(heavyNormal.hits == 2);
    CHECK(heavyFast.hits == 1);
    CHECK(heavyStrong.hits == 2);
    CHECK(heavyArmored.hits == 4);

    CHECK(heavyNormal.ttk > lightNormal.ttk);
    CHECK(heavyFast.ttk < lightFast.ttk);
    CHECK(lightStrong.stamina < heavyStrong.stamina);
    CHECK(heavyArmored.stamina > rules.staminaMax);
    CHECK(lightArmored.poiseHits == 5);
    CHECK(heavyArmored.poiseHits == 2);

    CHECK(characters::BlockDamageScale(0.55f, rules) == doctest::Approx(0.45f));
    CHECK(characters::BlockStaminaCost(axe->weight, 16.0f, rules) ==
          doctest::Approx(16.0f));
    CHECK(8.0f * characters::BlockDamageScale(0.55f, rules) ==
          doctest::Approx(3.6f));
    CHECK(static_cast<int>(std::floor(rules.staminaMax / 16.0f)) == 6);
}

