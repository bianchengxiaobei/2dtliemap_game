#include "characters/systems/StealthRules.h"

#include <algorithm>
#include <cmath>

namespace zl::characters {
namespace {

float ValidOrDefault(float value, float fallback, float minimum, float maximum) {
    if (!std::isfinite(value) || value < minimum || value > maximum) {
        return fallback;
    }
    return value;
}

}  // namespace

StealthRuleConfig DefaultStealthRuleConfig() {
    return {};
}

StealthRuleConfig SanitizeStealthRuleConfig(const StealthRuleConfig& config) {
    const StealthRuleConfig defaults;
    StealthRuleConfig out;
    out.enabled = config.enabled;
    out.crouchSpeedScale =
        ValidOrDefault(config.crouchSpeedScale, defaults.crouchSpeedScale, 0.1f, 1.0f);
    out.stepNoiseScale =
        ValidOrDefault(config.stepNoiseScale, defaults.stepNoiseScale, 0.0f, 1.0f);
    out.visibilityScale =
        ValidOrDefault(config.visibilityScale, defaults.visibilityScale, 0.05f, 1.0f);
    out.backstabMultiplier =
        ValidOrDefault(config.backstabMultiplier, defaults.backstabMultiplier, 1.0f, 3.0f);
    out.backstabHalfAngleDegrees = ValidOrDefault(
        config.backstabHalfAngleDegrees, defaults.backstabHalfAngleDegrees, 0.0f, 180.0f);
    out.exposeOnAttack = config.exposeOnAttack;
    return out;
}

StealthScales StealthScalesFor(bool crouched, const StealthRuleConfig& config) {
    if (!crouched || !config.enabled) {
        return {};
    }
    const StealthRuleConfig rules = SanitizeStealthRuleConfig(config);
    return StealthScales{rules.crouchSpeedScale, rules.stepNoiseScale,
                         rules.visibilityScale};
}

bool StealthExitRequired(bool crouched, StealthExitReason reason) {
    if (!crouched) {
        return false;
    }
    switch (reason) {
        case StealthExitReason::Attack:
        case StealthExitReason::Throw:
        case StealthExitReason::Damage:
        case StealthExitReason::Vehicle:
        case StealthExitReason::Downed:
            return true;
    }
    return false;
}

float StealthFootstepRadius(float baseRadius, bool crouched,
                           const StealthRuleConfig& config) {
    const float base = std::isfinite(baseRadius) && baseRadius > 0.0f
                           ? baseRadius
                           : 0.0f;
    if (!crouched || !config.enabled) {
        return base;
    }
    const StealthRuleConfig rules = SanitizeStealthRuleConfig(config);
    return base * rules.stepNoiseScale;
}

bool StealthTargetUnaware(ecs::AiStatus targetStatus, float alertness) {
    if (!std::isfinite(alertness) || alertness < 0.0f || alertness >= 0.5f) {
        return false;
    }
    switch (targetStatus) {
        case ecs::AiStatus::Idle:
        case ecs::AiStatus::Wander:
            return true;
        case ecs::AiStatus::Chase:
        case ecs::AiStatus::Attack:
        case ecs::AiStatus::SearchLastKnown:
        case ecs::AiStatus::Dead:
            return false;
    }
    return false;
}

bool StealthBehindTarget(float targetFacingX, float targetFacingY,
                         float targetToAttackerX, float targetToAttackerY,
                         float halfAngleDegrees) {
    if (!std::isfinite(targetFacingX) || !std::isfinite(targetFacingY) ||
        !std::isfinite(targetToAttackerX) || !std::isfinite(targetToAttackerY)) {
        return false;
    }
    const float facingLen =
        std::sqrt(targetFacingX * targetFacingX + targetFacingY * targetFacingY);
    const float toAttackerLen = std::sqrt(targetToAttackerX * targetToAttackerX +
                                          targetToAttackerY * targetToAttackerY);
    if (facingLen < 0.0001f || toAttackerLen < 0.0001f) {
        return false;
    }
    const float backX = -targetFacingX / facingLen;
    const float backY = -targetFacingY / facingLen;
    const float toX = targetToAttackerX / toAttackerLen;
    const float toY = targetToAttackerY / toAttackerLen;
    const float half =
        std::isfinite(halfAngleDegrees)
            ? std::clamp(halfAngleDegrees, 0.0f, 180.0f)
            : 0.0f;
    constexpr float kDegToRad = 0.017453292f;
    const float threshold = std::cos(half * kDegToRad) - 0.0001f;
    return backX * toX + backY * toY >= threshold;
}

float StealthBackstabDamageScale(bool eligible, float multiplier) {
    if (!eligible || !std::isfinite(multiplier) || multiplier <= 1.0f) {
        return 1.0f;
    }
    return std::clamp(multiplier, 1.0f, 3.0f);
}

}  // namespace zl::characters
