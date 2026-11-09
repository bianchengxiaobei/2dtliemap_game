#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <optional>
#include <string>

#include "characters/systems/CombatRules.h"
#include "core/Config.h"
#include "game/WorldSimulation.h"

using namespace zl::characters;

TEST_CASE("M30-T1：战斗规则默认配置与非法值回退") {
    const CombatRuleConfig defaults = DefaultCombatRuleConfig();
    CHECK(defaults.staminaMax == doctest::Approx(100.0f));
    CHECK(defaults.staminaRegenPerSecond == doctest::Approx(18.0f));
    CHECK(defaults.blockDamageScale == doctest::Approx(0.35f));
    CHECK(defaults.parryWindowSeconds == doctest::Approx(0.18f));
    CHECK(defaults.dodgeStaminaCost == doctest::Approx(18.0f));

    CombatRuleConfig invalid;
    invalid.staminaMax = -1.0f;
    invalid.staminaRegenPerSecond = std::numeric_limits<float>::quiet_NaN();
    invalid.heavyDamageScale = 0.0f;
    invalid.blockDamageScale = 2.0f;
    invalid.blockMinStaminaCost = 40.0f;
    invalid.blockMaxStaminaCost = 10.0f;
    invalid.dodgeDistance = -3.0f;
    invalid.poiseResetRatio = 1.5f;
    const CombatRuleConfig safe = SanitizeCombatRuleConfig(invalid);
    CHECK(safe.staminaMax == doctest::Approx(defaults.staminaMax));
    CHECK(safe.staminaRegenPerSecond ==
          doctest::Approx(defaults.staminaRegenPerSecond));
    CHECK(safe.heavyDamageScale == doctest::Approx(defaults.heavyDamageScale));
    CHECK(safe.blockDamageScale == doctest::Approx(defaults.blockDamageScale));
    CHECK(safe.blockMinStaminaCost ==
          doctest::Approx(defaults.blockMinStaminaCost));
    CHECK(safe.blockMaxStaminaCost ==
          doctest::Approx(defaults.blockMaxStaminaCost));
    CHECK(safe.dodgeDistance == doctest::Approx(defaults.dodgeDistance));
    CHECK(safe.poiseResetRatio == doctest::Approx(defaults.poiseResetRatio));

    CombatRuleConfig custom;
    custom.staminaMax = 120.0f;
    custom.blockMinStaminaCost = 4.0f;
    custom.blockMaxStaminaCost = 20.0f;
    const CombatRuleConfig kept = SanitizeCombatRuleConfig(custom);
    CHECK(kept.staminaMax == doctest::Approx(120.0f));
    CHECK(kept.blockMinStaminaCost == doctest::Approx(4.0f));
    CHECK(kept.blockMaxStaminaCost == doctest::Approx(20.0f));
}

TEST_CASE("M30-T1：战斗体力上限、回复与耗尽阈值") {
    const CombatRuleConfig config = DefaultCombatRuleConfig();
    CHECK(EffectiveMaxStamina(0.0f, config) == doctest::Approx(100.0f));
    CHECK(EffectiveMaxStamina(1.0f, config) == doctest::Approx(70.0f));
    CHECK(EffectiveMaxStamina(2.0f, config) == doctest::Approx(70.0f));
    CHECK(EffectiveMaxStamina(-1.0f, config) == doctest::Approx(100.0f));
    CHECK(EffectiveMaxStamina(std::numeric_limits<float>::quiet_NaN(), config) ==
          doctest::Approx(100.0f));

    CHECK(StaminaRegenPerSecond(0.0f, config) == doctest::Approx(18.0f));
    CHECK(StaminaRegenPerSecond(1.0f, config) == doctest::Approx(9.0f));
    CHECK(StaminaRegenPerSecond(std::numeric_limits<float>::infinity(), config) ==
          doctest::Approx(18.0f));

    CHECK(StaminaExhausted(5.0f, config));
    CHECK(StaminaExhausted(0.0f, config));
    CHECK_FALSE(StaminaExhausted(5.01f, config));
    CHECK_FALSE(StaminaExhausted(std::numeric_limits<float>::quiet_NaN(), config));
}

TEST_CASE("M30-T1：轻重击与武器重量派生") {
    const CombatRuleConfig config = DefaultCombatRuleConfig();
    CHECK(AttackStaminaCostForWeight(0.0f) == doctest::Approx(8.0f));
    CHECK(AttackStaminaCostForWeight(3.0f) == doctest::Approx(14.0f));
    CHECK(AttackStaminaCostForWeight(1000.0f) == doctest::Approx(18.0f));
    CHECK(AttackStaminaCostForWeight(std::numeric_limits<float>::quiet_NaN()) ==
          doctest::Approx(8.0f));

    CHECK(AttackPoiseDamageForWeight(0.0f) == doctest::Approx(6.0f));
    CHECK(AttackPoiseDamageForWeight(3.0f) == doctest::Approx(10.5f));
    CHECK(AttackPoiseDamageForWeight(1000.0f) == doctest::Approx(12.0f));
    CHECK(LightWindupSecondsForWeight(3.0f) == doctest::Approx(0.07f));
    CHECK(LightWindupSecondsForWeight(1000.0f) == doctest::Approx(0.12f));
    CHECK(HeavyWindupSecondsForWeight(3.0f) == doctest::Approx(0.314f));
    CHECK(HeavyWindupSecondsForWeight(0.0f) == doctest::Approx(0.30f));
    CHECK(HeavyWindupSecondsForWeight(1000.0f) == doctest::Approx(0.45f));

    AttackRuleValues light;
    light.damage = 50.0f;
    light.range = 1.9f;
    light.arcDegrees = 100.0f;
    light.cooldownSeconds = 0.60f;
    light.knockback = 4.0f;
    light.staggerSeconds = 0.40f;
    light.wearPerHit = 0.02f;
    light.staminaCost = 12.0f;
    light.poiseDamage = 9.0f;
    const AttackRuleValues heavy = DeriveHeavyAttack(light, 3.0f, config);
    CHECK(heavy.damage == doctest::Approx(82.5f));
    CHECK(heavy.range == doctest::Approx(2.0f));
    CHECK(heavy.arcDegrees == doctest::Approx(90.0f));
    CHECK(heavy.cooldownSeconds == doctest::Approx(0.93f));
    CHECK(heavy.knockback == doctest::Approx(5.0f));
    CHECK(heavy.staggerSeconds == doctest::Approx(0.50f));
    CHECK(heavy.wearPerHit == doctest::Approx(0.025f));
    CHECK(heavy.staminaCost == doctest::Approx(21.6f));
    CHECK(heavy.poiseDamage == doctest::Approx(22.5f));
    CHECK(heavy.windupSeconds == doctest::Approx(0.314f));

    light.staminaCost = 0.0f;
    light.poiseDamage = 0.0f;
    const AttackRuleValues derived =
        DeriveHeavyAttack(light, 3.0f, config);
    CHECK(derived.staminaCost == doctest::Approx(25.2f));
    CHECK(derived.poiseDamage == doctest::Approx(26.25f));

    light.damage = std::numeric_limits<float>::quiet_NaN();
    const AttackRuleValues safe = DeriveHeavyAttack(light, 3.0f, config);
    CHECK(std::isfinite(safe.damage));
    CHECK(safe.damage == doctest::Approx(0.0f));
}

TEST_CASE("M30-T1：格挡、弹反、闪避与硬直规则") {
    const CombatRuleConfig config = DefaultCombatRuleConfig();
    CHECK(BlockDamageScale(std::nullopt, config) == doctest::Approx(0.35f));
    CHECK(BlockDamageScale(0.65f, config) == doctest::Approx(0.35f));
    CHECK(BlockDamageScale(1.0f, config) == doctest::Approx(0.05f));
    CHECK(BlockDamageScale(-1.0f, config) == doctest::Approx(1.0f));
    CHECK(BlockDamageScale(std::numeric_limits<float>::quiet_NaN(), config) ==
          doctest::Approx(0.35f));

    CHECK(BlockStaminaCost(0.0f, std::nullopt, config) == doctest::Approx(10.0f));
    CHECK(BlockStaminaCost(3.0f, std::nullopt, config) == doctest::Approx(17.0f));
    CHECK(BlockStaminaCost(1000.0f, std::nullopt, config) == doctest::Approx(30.0f));
    CHECK(BlockStaminaCost(3.0f, 16.0f, config) == doctest::Approx(16.0f));
    CHECK(BlockStaminaCost(3.0f, -1.0f, config) == doctest::Approx(17.0f));

    CHECK(ParryWindowActive(0.0f, config));
    CHECK(ParryWindowActive(0.18f, config));
    CHECK_FALSE(ParryWindowActive(0.181f, config));
    CHECK_FALSE(ParryWindowActive(-0.1f, config));
    CHECK_FALSE(ParryWindowActive(std::numeric_limits<float>::quiet_NaN(), config));

    CHECK(BlockMoveScale(config) == doctest::Approx(0.65f));
    CHECK(BlockArcDegrees(config) == doctest::Approx(140.0f));
    CHECK(BlockKnockbackScale(config) == doctest::Approx(0.50f));
    CHECK(ParryStaminaCost(config) == doctest::Approx(6.0f));
    CHECK(ParryCooldownSeconds(config) == doctest::Approx(0.70f));
    CHECK(ParryStaggerSeconds(config) == doctest::Approx(0.55f));
    CHECK(GuardBreakSeconds(config) == doctest::Approx(0.80f));
    CHECK(DodgeStaminaCost(config) == doctest::Approx(18.0f));
    CHECK(DodgeDurationSeconds(config) == doctest::Approx(0.28f));
    CHECK(DodgeInvulnSeconds(config) == doctest::Approx(0.18f));
    CHECK(DodgeCooldownSeconds(config) == doctest::Approx(0.65f));
    CHECK(DodgeDistance(config) == doctest::Approx(1.50f));
    CHECK(PoiseRegenPerSecond(config) == doctest::Approx(10.0f));
    CHECK(PoiseRegenDelaySeconds(config) == doctest::Approx(1.20f));
    CHECK(PoiseBreakStaggerSeconds(config) == doctest::Approx(0.55f));
    CHECK(PoiseResetRatio(config) == doctest::Approx(0.60f));
}

TEST_CASE("M30-T7：combat.rules 配置读取、默认回退与钳制") {
    zl::core::Config shipped;
    const std::string path = std::string(ZL_SOURCE_DIR) + "/config/game.json";
    REQUIRE_MESSAGE(shipped.LoadFile(path), "config/game.json 加载失败");
    const CombatRuleConfig defaults = zl::game::LoadCombatRuleConfig(shipped);
    CHECK(defaults.staminaMax == doctest::Approx(100.0f));
    CHECK(defaults.heavyDamageScale == doctest::Approx(1.65f));
    CHECK(defaults.blockDamageScale == doctest::Approx(0.35f));
    CHECK(defaults.parryWindowSeconds == doctest::Approx(0.18f));
    CHECK(defaults.dodgeDistance == doctest::Approx(1.50f));
    CHECK(defaults.poiseMaxArmored == doctest::Approx(55.0f));
    CHECK(defaults.armoredStaggerPoiseThreshold == doctest::Approx(16.0f));

    zl::core::Config custom;
    custom.Set("combat.rules.staminaMax", 120.0);
    custom.Set("combat.rules.staminaRegenPerSecond", 24.0);
    custom.Set("combat.rules.heavyDamageScale", 2.0);
    custom.Set("combat.rules.blockDamageScale", 0.20);
    custom.Set("combat.rules.parryWindowSeconds", 0.30);
    custom.Set("combat.rules.dodgeStaminaCost", 12.0);
    custom.Set("combat.rules.poiseMaxNormal", 30.0);
    custom.Set("combat.rules.poiseMaxFast", 18.0);
    custom.Set("combat.rules.poiseMaxStrong", 40.0);
    custom.Set("combat.rules.poiseMaxArmored", 70.0);
    custom.Set("combat.rules.armoredStaggerPoiseThreshold", 22.0);
    const CombatRuleConfig kept = zl::game::LoadCombatRuleConfig(custom);
    CHECK(kept.staminaMax == doctest::Approx(120.0f));
    CHECK(kept.staminaRegenPerSecond == doctest::Approx(24.0f));
    CHECK(kept.heavyDamageScale == doctest::Approx(2.0f));
    CHECK(kept.blockDamageScale == doctest::Approx(0.20f));
    CHECK(kept.parryWindowSeconds == doctest::Approx(0.30f));
    CHECK(kept.dodgeStaminaCost == doctest::Approx(12.0f));
    CHECK(kept.poiseMaxNormal == doctest::Approx(30.0f));
    CHECK(kept.poiseMaxFast == doctest::Approx(18.0f));
    CHECK(kept.poiseMaxStrong == doctest::Approx(40.0f));
    CHECK(kept.poiseMaxArmored == doctest::Approx(70.0f));
    CHECK(kept.armoredStaggerPoiseThreshold == doctest::Approx(22.0f));

    zl::core::Config invalid;
    invalid.Set("combat.rules.staminaMax", 0.0);
    invalid.Set("combat.rules.blockDamageScale", 2.0);
    invalid.Set("combat.rules.dodgeDistance", -3.0);
    invalid.Set("combat.rules.poiseMaxArmored", 0.0);
    invalid.Set("combat.rules.armoredStaggerPoiseThreshold", -1.0);
    const CombatRuleConfig safe = zl::game::LoadCombatRuleConfig(invalid);
    CHECK(safe.staminaMax == doctest::Approx(DefaultCombatRuleConfig().staminaMax));
    CHECK(safe.blockDamageScale ==
          doctest::Approx(DefaultCombatRuleConfig().blockDamageScale));
    CHECK(safe.dodgeDistance ==
          doctest::Approx(DefaultCombatRuleConfig().dodgeDistance));
    CHECK(safe.poiseMaxArmored ==
          doctest::Approx(DefaultCombatRuleConfig().poiseMaxArmored));
    CHECK(safe.armoredStaggerPoiseThreshold ==
          doctest::Approx(
              DefaultCombatRuleConfig().armoredStaggerPoiseThreshold));
}
