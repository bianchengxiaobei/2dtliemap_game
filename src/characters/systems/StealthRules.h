#pragma once

#include "ai/components/AiComponents.h"

namespace zl::characters {

struct StealthRuleConfig {
    bool enabled = true;
    float crouchSpeedScale = 0.55f;
    float stepNoiseScale = 0.45f;
    float visibilityScale = 0.65f;
    float backstabMultiplier = 1.6f;
    float backstabHalfAngleDegrees = 60.0f;
    bool exposeOnAttack = true;
};

struct StealthScales {
    float speedScale = 1.0f;
    float noiseScale = 1.0f;
    float visibilityScale = 1.0f;
};

enum class StealthExitReason {
    Attack,
    Throw,
    Damage,
    Vehicle,
    Downed,
};

StealthRuleConfig DefaultStealthRuleConfig();
StealthRuleConfig SanitizeStealthRuleConfig(const StealthRuleConfig& config);
StealthScales StealthScalesFor(bool crouched, const StealthRuleConfig& config);
bool StealthExitRequired(bool crouched, StealthExitReason reason);
float StealthFootstepRadius(float baseRadius, bool crouched,
                           const StealthRuleConfig& config);

// M34-F1-T4：背刺判定。targetStatus/alertness 来自 AiState/PerceptionState；
// 背后判定使用“目标 -> 攻击者”向量与目标朝向的反方向比较，半角由配置给出。
bool StealthTargetUnaware(ecs::AiStatus targetStatus, float alertness);
bool StealthBehindTarget(float targetFacingX, float targetFacingY,
                         float targetToAttackerX, float targetToAttackerY,
                         float halfAngleDegrees);
// 只对符合条件的一次命中返回倍率；1 = 不增伤，非法/越界回退安全值。
float StealthBackstabDamageScale(bool eligible, float multiplier);

}  // namespace zl::characters
