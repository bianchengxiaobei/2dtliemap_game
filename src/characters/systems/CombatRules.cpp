#include "characters/systems/CombatRules.h"

#include <algorithm>
#include <cmath>

namespace zl::characters {
namespace {

constexpr float kLightStaminaBase = 8.0f;
constexpr float kLightStaminaPerWeight = 2.0f;
constexpr float kLightStaminaMin = 6.0f;
constexpr float kLightStaminaMax = 18.0f;
constexpr float kHeavyStaminaMin = 8.0f;
constexpr float kHeavyStaminaMax = 32.0f;

constexpr float kLightPoiseBase = 6.0f;
constexpr float kLightPoisePerWeight = 1.5f;
constexpr float kLightPoiseMin = 6.0f;
constexpr float kLightPoiseMax = 12.0f;
constexpr float kHeavyPoiseMin = 18.0f;
constexpr float kHeavyPoiseMax = 45.0f;

constexpr float kLightWindupBase = 0.04f;
constexpr float kLightWindupPerWeight = 0.01f;
constexpr float kLightWindupMax = 0.12f;
constexpr float kHeavyWindupBase = 0.26f;
constexpr float kHeavyWindupPerWeight = 0.018f;
constexpr float kHeavyWindupMin = 0.30f;
constexpr float kHeavyWindupMax = 0.45f;

float ClampFinite(float value, float minimum, float maximum, float fallback) {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, minimum, maximum);
}

float FiniteNonNegative(float value) {
    return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
}

float NonNegativeWeight(float weightKg) {
    return ClampFinite(weightKg, 0.0f, 1000.0f, 0.0f);
}

float ValidOrDefault(float value, float fallback, float minimum, float maximum) {
    if (!std::isfinite(value) || value < minimum || value > maximum) {
        return fallback;
    }
    return value;
}

}  // namespace

CombatRuleConfig DefaultCombatRuleConfig() {
    return {};
}

CombatRuleConfig SanitizeCombatRuleConfig(const CombatRuleConfig& config) {
    const CombatRuleConfig defaults;
    CombatRuleConfig out;

    out.staminaMax = ValidOrDefault(config.staminaMax, defaults.staminaMax, 1.0f, 1000.0f);
    out.staminaRegenPerSecond = ValidOrDefault(
        config.staminaRegenPerSecond, defaults.staminaRegenPerSecond, 0.0f, 1000.0f);
    out.staminaRegenDelaySeconds =
        ValidOrDefault(config.staminaRegenDelaySeconds,
                       defaults.staminaRegenDelaySeconds, 0.0f, 60.0f);
    out.staminaExhaustedThreshold =
        ValidOrDefault(config.staminaExhaustedThreshold,
                       defaults.staminaExhaustedThreshold, 0.0f, 1000.0f);
    out.staminaExhaustedSeconds =
        ValidOrDefault(config.staminaExhaustedSeconds,
                       defaults.staminaExhaustedSeconds, 0.0f, 60.0f);
    out.fatigueMaxPenalty =
        ValidOrDefault(config.fatigueMaxPenalty, defaults.fatigueMaxPenalty, 0.0f, 1.0f);
    out.fatigueRegenPenalty = ValidOrDefault(
        config.fatigueRegenPenalty, defaults.fatigueRegenPenalty, 0.0f, 1.0f);

    out.heavyDamageScale = ValidOrDefault(
        config.heavyDamageScale, defaults.heavyDamageScale, 0.1f, 10.0f);
    out.heavyCooldownScale = ValidOrDefault(
        config.heavyCooldownScale, defaults.heavyCooldownScale, 0.1f, 10.0f);
    out.heavyRangeBonus = ValidOrDefault(
        config.heavyRangeBonus, defaults.heavyRangeBonus, 0.0f, 10.0f);
    out.heavyArcScale =
        ValidOrDefault(config.heavyArcScale, defaults.heavyArcScale, 0.05f, 2.0f);
    out.heavyKnockbackScale = ValidOrDefault(
        config.heavyKnockbackScale, defaults.heavyKnockbackScale, 0.1f, 10.0f);
    out.heavyStaggerScale = ValidOrDefault(
        config.heavyStaggerScale, defaults.heavyStaggerScale, 0.1f, 10.0f);
    out.heavyWearScale =
        ValidOrDefault(config.heavyWearScale, defaults.heavyWearScale, 0.1f, 10.0f);
    out.heavyStaminaScale = ValidOrDefault(
        config.heavyStaminaScale, defaults.heavyStaminaScale, 0.1f, 10.0f);
    out.heavyPoiseScale = ValidOrDefault(
        config.heavyPoiseScale, defaults.heavyPoiseScale, 0.1f, 10.0f);

    out.blockMoveScale =
        ValidOrDefault(config.blockMoveScale, defaults.blockMoveScale, 0.1f, 1.0f);
    out.blockArcDegrees = ValidOrDefault(
        config.blockArcDegrees, defaults.blockArcDegrees, 0.0f, 360.0f);
    out.blockDamageScale =
        ValidOrDefault(config.blockDamageScale, defaults.blockDamageScale, 0.0f, 1.0f);
    out.blockKnockbackScale = ValidOrDefault(
        config.blockKnockbackScale, defaults.blockKnockbackScale, 0.0f, 2.0f);
    out.blockBaseStaminaCost = ValidOrDefault(
        config.blockBaseStaminaCost, defaults.blockBaseStaminaCost, 0.0f, 1000.0f);
    out.blockStaminaPerWeight = ValidOrDefault(
        config.blockStaminaPerWeight, defaults.blockStaminaPerWeight, 0.0f, 1000.0f);
    out.blockMinStaminaCost = ValidOrDefault(
        config.blockMinStaminaCost, defaults.blockMinStaminaCost, 0.0f, 1000.0f);
    out.blockMaxStaminaCost = ValidOrDefault(
        config.blockMaxStaminaCost, defaults.blockMaxStaminaCost, 0.0f, 1000.0f);
    if (out.blockMinStaminaCost > out.blockMaxStaminaCost) {
        out.blockMinStaminaCost = defaults.blockMinStaminaCost;
        out.blockMaxStaminaCost = defaults.blockMaxStaminaCost;
    }

    out.parryWindowSeconds = ValidOrDefault(
        config.parryWindowSeconds, defaults.parryWindowSeconds, 0.0f, 5.0f);
    out.parryStaminaCost = ValidOrDefault(
        config.parryStaminaCost, defaults.parryStaminaCost, 0.0f, 1000.0f);
    out.parryCooldownSeconds = ValidOrDefault(
        config.parryCooldownSeconds, defaults.parryCooldownSeconds, 0.0f, 60.0f);
    out.parryStaggerSeconds = ValidOrDefault(
        config.parryStaggerSeconds, defaults.parryStaggerSeconds, 0.0f, 60.0f);
    out.guardBreakSeconds = ValidOrDefault(
        config.guardBreakSeconds, defaults.guardBreakSeconds, 0.0f, 60.0f);

    out.dodgeStaminaCost = ValidOrDefault(
        config.dodgeStaminaCost, defaults.dodgeStaminaCost, 0.0f, 1000.0f);
    out.dodgeDurationSeconds = ValidOrDefault(
        config.dodgeDurationSeconds, defaults.dodgeDurationSeconds, 0.01f, 10.0f);
    out.dodgeInvulnSeconds = ValidOrDefault(
        config.dodgeInvulnSeconds, defaults.dodgeInvulnSeconds, 0.0f, 10.0f);
    out.dodgeCooldownSeconds = ValidOrDefault(
        config.dodgeCooldownSeconds, defaults.dodgeCooldownSeconds, 0.0f, 60.0f);
    out.dodgeDistance =
        ValidOrDefault(config.dodgeDistance, defaults.dodgeDistance, 0.0f, 100.0f);

    out.poiseRegenPerSecond = ValidOrDefault(
        config.poiseRegenPerSecond, defaults.poiseRegenPerSecond, 0.0f, 1000.0f);
    out.poiseRegenDelaySeconds = ValidOrDefault(
        config.poiseRegenDelaySeconds, defaults.poiseRegenDelaySeconds, 0.0f, 60.0f);
    out.poiseBreakStaggerSeconds = ValidOrDefault(
        config.poiseBreakStaggerSeconds, defaults.poiseBreakStaggerSeconds, 0.0f, 60.0f);
    out.poiseResetRatio =
        ValidOrDefault(config.poiseResetRatio, defaults.poiseResetRatio, 0.0f, 1.0f);
    out.poiseMaxNormal = ValidOrDefault(
        config.poiseMaxNormal, defaults.poiseMaxNormal, 1.0f, 1000.0f);
    out.poiseMaxFast = ValidOrDefault(
        config.poiseMaxFast, defaults.poiseMaxFast, 1.0f, 1000.0f);
    out.poiseMaxStrong = ValidOrDefault(
        config.poiseMaxStrong, defaults.poiseMaxStrong, 1.0f, 1000.0f);
    out.poiseMaxArmored = ValidOrDefault(
        config.poiseMaxArmored, defaults.poiseMaxArmored, 1.0f, 1000.0f);
    out.armoredStaggerPoiseThreshold = ValidOrDefault(
        config.armoredStaggerPoiseThreshold,
        defaults.armoredStaggerPoiseThreshold, 0.0f, 1000.0f);
    return out;
}

float EffectiveMaxStamina(float fatigue, const CombatRuleConfig& config) {
    const CombatRuleConfig rules = SanitizeCombatRuleConfig(config);
    const float normalized = ClampFinite(fatigue, 0.0f, 1.0f, 0.0f);
    return rules.staminaMax * (1.0f - rules.fatigueMaxPenalty * normalized);
}

float StaminaRegenPerSecond(float fatigue, const CombatRuleConfig& config) {
    const CombatRuleConfig rules = SanitizeCombatRuleConfig(config);
    const float normalized = ClampFinite(fatigue, 0.0f, 1.0f, 0.0f);
    return rules.staminaRegenPerSecond * (1.0f - rules.fatigueRegenPenalty * normalized);
}

bool StaminaExhausted(float stamina, const CombatRuleConfig& config) {
    const CombatRuleConfig rules = SanitizeCombatRuleConfig(config);
    return std::isfinite(stamina) && stamina <= rules.staminaExhaustedThreshold;
}

float AttackStaminaCostForWeight(float weightKg) {
    const float weight = NonNegativeWeight(weightKg);
    return std::clamp(kLightStaminaBase + weight * kLightStaminaPerWeight,
                      kLightStaminaMin, kLightStaminaMax);
}

float AttackPoiseDamageForWeight(float weightKg) {
    const float weight = NonNegativeWeight(weightKg);
    return std::clamp(kLightPoiseBase + weight * kLightPoisePerWeight,
                      kLightPoiseMin, kLightPoiseMax);
}

float LightWindupSecondsForWeight(float weightKg) {
    const float weight = NonNegativeWeight(weightKg);
    return std::clamp(kLightWindupBase + weight * kLightWindupPerWeight, 0.0f,
                      kLightWindupMax);
}

float HeavyWindupSecondsForWeight(float weightKg) {
    const float weight = NonNegativeWeight(weightKg);
    return std::clamp(kHeavyWindupBase + weight * kHeavyWindupPerWeight,
                      kHeavyWindupMin, kHeavyWindupMax);
}

AttackRuleValues DeriveHeavyAttack(const AttackRuleValues& light, float weightKg,
                                   const CombatRuleConfig& config) {
    const CombatRuleConfig rules = SanitizeCombatRuleConfig(config);
    const float weight = NonNegativeWeight(weightKg);

    AttackRuleValues heavy;
    heavy.damage = FiniteNonNegative(light.damage) * rules.heavyDamageScale;
    heavy.range = FiniteNonNegative(light.range) + rules.heavyRangeBonus;
    heavy.arcDegrees = FiniteNonNegative(light.arcDegrees) * rules.heavyArcScale;
    heavy.cooldownSeconds =
        FiniteNonNegative(light.cooldownSeconds) * rules.heavyCooldownScale;
    heavy.knockback = FiniteNonNegative(light.knockback) * rules.heavyKnockbackScale;
    heavy.staggerSeconds =
        FiniteNonNegative(light.staggerSeconds) * rules.heavyStaggerScale;
    heavy.wearPerHit = FiniteNonNegative(light.wearPerHit) * rules.heavyWearScale;

    const float lightStamina = light.staminaCost > 0.0f
                                   ? FiniteNonNegative(light.staminaCost)
                                   : AttackStaminaCostForWeight(weight);
    heavy.staminaCost = std::clamp(lightStamina * rules.heavyStaminaScale,
                                   kHeavyStaminaMin, kHeavyStaminaMax);

    const float lightPoise = light.poiseDamage > 0.0f
                                 ? FiniteNonNegative(light.poiseDamage)
                                 : AttackPoiseDamageForWeight(weight);
    heavy.poiseDamage =
        std::clamp(lightPoise * rules.heavyPoiseScale, kHeavyPoiseMin, kHeavyPoiseMax);
    heavy.windupSeconds = HeavyWindupSecondsForWeight(weight);
    return heavy;
}

float BlockDamageScale(std::optional<float> blockReduction,
                       const CombatRuleConfig& config) {
    const CombatRuleConfig rules = SanitizeCombatRuleConfig(config);
    if (blockReduction.has_value() && std::isfinite(*blockReduction)) {
        const float reduction = std::clamp(*blockReduction, 0.0f, 0.95f);
        return 1.0f - reduction;
    }
    return rules.blockDamageScale;
}

float BlockStaminaCost(float weightKg, std::optional<float> blockStamina,
                       const CombatRuleConfig& config) {
    const CombatRuleConfig rules = SanitizeCombatRuleConfig(config);
    if (blockStamina.has_value() && std::isfinite(*blockStamina) &&
        *blockStamina >= 0.0f) {
        return std::clamp(*blockStamina, 0.0f, 1000.0f);
    }
    const float weight = NonNegativeWeight(weightKg);
    return std::clamp(rules.blockBaseStaminaCost +
                          weight * rules.blockStaminaPerWeight,
                      rules.blockMinStaminaCost, rules.blockMaxStaminaCost);
}

bool ParryWindowActive(float blockElapsedSeconds, const CombatRuleConfig& config) {
    if (!std::isfinite(blockElapsedSeconds) || blockElapsedSeconds < 0.0f) {
        return false;
    }
    return blockElapsedSeconds <= ParryWindowSeconds(config);
}

float BlockMoveScale(const CombatRuleConfig& config) {
    return SanitizeCombatRuleConfig(config).blockMoveScale;
}

float BlockArcDegrees(const CombatRuleConfig& config) {
    return SanitizeCombatRuleConfig(config).blockArcDegrees;
}

float BlockKnockbackScale(const CombatRuleConfig& config) {
    return SanitizeCombatRuleConfig(config).blockKnockbackScale;
}

float ParryWindowSeconds(const CombatRuleConfig& config) {
    return SanitizeCombatRuleConfig(config).parryWindowSeconds;
}

float ParryStaminaCost(const CombatRuleConfig& config) {
    return SanitizeCombatRuleConfig(config).parryStaminaCost;
}

float ParryCooldownSeconds(const CombatRuleConfig& config) {
    return SanitizeCombatRuleConfig(config).parryCooldownSeconds;
}

float ParryStaggerSeconds(const CombatRuleConfig& config) {
    return SanitizeCombatRuleConfig(config).parryStaggerSeconds;
}

float GuardBreakSeconds(const CombatRuleConfig& config) {
    return SanitizeCombatRuleConfig(config).guardBreakSeconds;
}

float DodgeStaminaCost(const CombatRuleConfig& config) {
    return SanitizeCombatRuleConfig(config).dodgeStaminaCost;
}

float DodgeDurationSeconds(const CombatRuleConfig& config) {
    return SanitizeCombatRuleConfig(config).dodgeDurationSeconds;
}

float DodgeInvulnSeconds(const CombatRuleConfig& config) {
    return SanitizeCombatRuleConfig(config).dodgeInvulnSeconds;
}

float DodgeCooldownSeconds(const CombatRuleConfig& config) {
    return SanitizeCombatRuleConfig(config).dodgeCooldownSeconds;
}

float DodgeDistance(const CombatRuleConfig& config) {
    return SanitizeCombatRuleConfig(config).dodgeDistance;
}

float DodgeSpeed(const CombatRuleConfig& config) {
    const CombatRuleConfig rules = SanitizeCombatRuleConfig(config);
    if (rules.dodgeDurationSeconds <= 0.0f) {
        return 0.0f;
    }
    return rules.dodgeDistance / rules.dodgeDurationSeconds;
}

float PoiseRegenPerSecond(const CombatRuleConfig& config) {
    return SanitizeCombatRuleConfig(config).poiseRegenPerSecond;
}

float PoiseRegenDelaySeconds(const CombatRuleConfig& config) {
    return SanitizeCombatRuleConfig(config).poiseRegenDelaySeconds;
}

float PoiseBreakStaggerSeconds(const CombatRuleConfig& config) {
    return SanitizeCombatRuleConfig(config).poiseBreakStaggerSeconds;
}

float PoiseResetRatio(const CombatRuleConfig& config) {
    return SanitizeCombatRuleConfig(config).poiseResetRatio;
}

}  // namespace zl::characters
