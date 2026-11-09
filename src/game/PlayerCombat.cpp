#include "game/PlayerCombat.h"

#include <algorithm>
#include <cmath>

namespace zl::game {
namespace {

float PositiveOr(float value, float fallback) {
    return std::isfinite(value) && value > 0.0f ? value : fallback;
}

items::WeaponAttackParams FillDerivedFields(items::WeaponAttackParams params,
                                             float weightKg) {
    params.staminaCost = PositiveOr(
        params.staminaCost, characters::AttackStaminaCostForWeight(weightKg));
    params.windupSeconds = PositiveOr(
        params.windupSeconds, characters::LightWindupSecondsForWeight(weightKg));
    params.poiseDamage = PositiveOr(
        params.poiseDamage, characters::AttackPoiseDamageForWeight(weightKg));
    return params;
}

characters::AttackRuleValues ToRuleValues(
    const items::WeaponAttackParams& params) {
    characters::AttackRuleValues values;
    values.damage = params.damage;
    values.range = params.range;
    values.arcDegrees = params.arcDegrees;
    values.cooldownSeconds = params.cooldownSeconds;
    values.knockback = params.knockback;
    values.staggerSeconds = params.staggerSeconds;
    values.wearPerHit = params.wearPerHit;
    values.staminaCost = params.staminaCost;
    values.windupSeconds = params.windupSeconds;
    values.poiseDamage = params.poiseDamage;
    return values;
}

items::WeaponAttackParams ToWeaponParams(
    const characters::AttackRuleValues& values) {
    items::WeaponAttackParams params;
    params.damage = values.damage;
    params.range = values.range;
    params.arcDegrees = values.arcDegrees;
    params.cooldownSeconds = values.cooldownSeconds;
    params.knockback = values.knockback;
    params.staggerSeconds = values.staggerSeconds;
    params.wearPerHit = values.wearPerHit;
    params.staminaCost = values.staminaCost;
    params.windupSeconds = values.windupSeconds;
    params.poiseDamage = values.poiseDamage;
    return params;
}

}  // namespace

PlayerMeleeAttackPlan ResolvePlayerMeleeAttack(
    const items::ItemDef* weapon, const items::WeaponAttackParams& base,
    bool heavy, const characters::CombatRuleConfig& config) {
    const float weightKg = weapon != nullptr ? weapon->weight : 0.0f;

    items::WeaponAttackParams light = base;
    if (weapon != nullptr) {
        light = items::ResolveWeaponAttack(weapon->combat, light);
    }
    light = FillDerivedFields(light, weightKg);

    PlayerMeleeAttackPlan plan;
    plan.heavy = heavy;
    if (!heavy) {
        plan.params = light;
        return plan;
    }

    const characters::AttackRuleValues derived =
        characters::DeriveHeavyAttack(ToRuleValues(light), weightKg, config);
    items::WeaponAttackParams heavyParams = ToWeaponParams(derived);
    if (weapon != nullptr && weapon->heavy.has_value()) {
        heavyParams =
            items::ResolveWeaponAttack(*weapon->heavy, heavyParams);
        // T1 数据契约：heavy 的 stamina/windup/poise 非正数仍按重量派生。
        heavyParams.staminaCost =
            PositiveOr(heavyParams.staminaCost, derived.staminaCost);
        heavyParams.windupSeconds =
            PositiveOr(heavyParams.windupSeconds, derived.windupSeconds);
        heavyParams.poiseDamage =
            PositiveOr(heavyParams.poiseDamage, derived.poiseDamage);
    }
    plan.params = heavyParams;
    return plan;
}

PlayerBlockPlan ResolvePlayerBlock(const items::ItemDef* weapon) {
    PlayerBlockPlan plan;
    if (weapon == nullptr) {
        return plan;
    }
    if (weapon->combat.blockReduction.has_value() &&
        std::isfinite(*weapon->combat.blockReduction)) {
        plan.blockReduction =
            std::clamp(*weapon->combat.blockReduction, 0.0f, 0.95f);
    }
    if (weapon->combat.blockStaminaCost.has_value() &&
        std::isfinite(*weapon->combat.blockStaminaCost)) {
        plan.blockStaminaCost =
            std::clamp(*weapon->combat.blockStaminaCost, 0.0f, 100.0f);
    }
    return plan;
}

PlayerDodgePlan ResolvePlayerDodge(
    float worldDirX, float worldDirY, ecs::Direction facing,
    const characters::CombatRuleConfig& config) {
    PlayerDodgePlan plan;
    plan.speed = characters::DodgeSpeed(config);
    const float length =
        std::isfinite(worldDirX) && std::isfinite(worldDirY)
            ? std::sqrt(worldDirX * worldDirX + worldDirY * worldDirY)
            : 0.0f;
    if (length > 0.0001f) {
        plan.dirX = worldDirX / length;
        plan.dirY = worldDirY / length;
        return plan;
    }
    FacingAttackDirection(facing, &plan.dirX, &plan.dirY);
    return plan;
}

void FacingAttackDirection(ecs::Direction dir, float* x, float* y) {
    // Facing 是屏幕 4 向；AttackIntent.dir 是世界格方向，做等距逆变换。
    constexpr float kIso = 0.70710678f;
    float dirX = kIso;
    float dirY = kIso;
    switch (dir) {
        case ecs::Direction::Down:
            break;
        case ecs::Direction::Left:
            dirX = -kIso;
            dirY = kIso;
            break;
        case ecs::Direction::Right:
            dirX = kIso;
            dirY = -kIso;
            break;
        case ecs::Direction::Up:
            dirX = -kIso;
            dirY = -kIso;
            break;
    }
    if (x != nullptr) {
        *x = dirX;
    }
    if (y != nullptr) {
        *y = dirY;
    }
}

}  // namespace zl::game
