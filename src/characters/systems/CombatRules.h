#pragma once

#include <optional>

namespace zl::characters {

// M30-T1：战斗规则纯函数与配置契约。这里不读写 ECS/网络/物品注册表；
// 上层负责把武器数据、疲劳和运行时状态传进来，保证单机/服务器共用同一套数值。
struct CombatRuleConfig {
    float staminaMax = 100.0f;
    float staminaRegenPerSecond = 18.0f;
    float staminaRegenDelaySeconds = 0.8f;
    float staminaExhaustedThreshold = 5.0f;
    float staminaExhaustedSeconds = 0.6f;
    float fatigueMaxPenalty = 0.30f;
    float fatigueRegenPenalty = 0.50f;

    float heavyDamageScale = 1.65f;
    float heavyCooldownScale = 1.55f;
    float heavyRangeBonus = 0.10f;
    float heavyArcScale = 0.90f;
    float heavyKnockbackScale = 1.25f;
    float heavyStaggerScale = 1.25f;
    float heavyWearScale = 1.25f;
    float heavyStaminaScale = 1.80f;
    float heavyPoiseScale = 2.50f;

    float blockMoveScale = 0.65f;
    float blockArcDegrees = 140.0f;
    float blockDamageScale = 0.35f;
    float blockKnockbackScale = 0.50f;
    float blockBaseStaminaCost = 8.0f;
    float blockStaminaPerWeight = 3.0f;
    float blockMinStaminaCost = 10.0f;
    float blockMaxStaminaCost = 30.0f;

    float parryWindowSeconds = 0.18f;
    float parryStaminaCost = 6.0f;
    float parryCooldownSeconds = 0.70f;
    float parryStaggerSeconds = 0.55f;
    float guardBreakSeconds = 0.80f;

    float dodgeStaminaCost = 18.0f;
    float dodgeDurationSeconds = 0.28f;
    float dodgeInvulnSeconds = 0.18f;
    float dodgeCooldownSeconds = 0.65f;
    float dodgeDistance = 1.50f;

    float poiseRegenPerSecond = 10.0f;
    float poiseRegenDelaySeconds = 1.20f;
    float poiseBreakStaggerSeconds = 0.55f;
    float poiseResetRatio = 0.60f;

    float poiseMaxNormal = 20.0f;
    float poiseMaxFast = 14.0f;
    float poiseMaxStrong = 32.0f;
    float poiseMaxArmored = 55.0f;
    float armoredStaggerPoiseThreshold = 16.0f;
};

// 非法/NaN/越界字段回退默认值；合法字段保留。
CombatRuleConfig DefaultCombatRuleConfig();
CombatRuleConfig SanitizeCombatRuleConfig(const CombatRuleConfig& config);

// 攻击参数的数据契约。staminaCost/windupSeconds/poiseDamage <= 0 表示由重量派生。
struct AttackRuleValues {
    float damage = 1.0f;
    float range = 1.0f;
    float arcDegrees = 100.0f;
    float cooldownSeconds = 0.35f;
    float knockback = 0.0f;
    float staggerSeconds = 0.0f;
    float wearPerHit = 0.0f;
    float staminaCost = 0.0f;
    float windupSeconds = 0.0f;
    float poiseDamage = 0.0f;
};

float EffectiveMaxStamina(float fatigue, const CombatRuleConfig& config);
float StaminaRegenPerSecond(float fatigue, const CombatRuleConfig& config);
bool StaminaExhausted(float stamina, const CombatRuleConfig& config);

float AttackStaminaCostForWeight(float weightKg);
float AttackPoiseDamageForWeight(float weightKg);
float LightWindupSecondsForWeight(float weightKg);
float HeavyWindupSecondsForWeight(float weightKg);

// 由轻击参数派生重击默认值；显式 heavy 数据由上层覆盖。
AttackRuleValues DeriveHeavyAttack(const AttackRuleValues& light, float weightKg,
                                   const CombatRuleConfig& config);

// blockReduction 为 0~0.95 的减伤比例（0.65 = 伤害 ×0.35）；缺省用配置。
float BlockDamageScale(std::optional<float> blockReduction,
                       const CombatRuleConfig& config);
float BlockStaminaCost(float weightKg, std::optional<float> blockStamina,
                       const CombatRuleConfig& config);
bool ParryWindowActive(float blockElapsedSeconds, const CombatRuleConfig& config);

float BlockMoveScale(const CombatRuleConfig& config);
float BlockArcDegrees(const CombatRuleConfig& config);
float BlockKnockbackScale(const CombatRuleConfig& config);
float ParryWindowSeconds(const CombatRuleConfig& config);
float ParryStaminaCost(const CombatRuleConfig& config);
float ParryCooldownSeconds(const CombatRuleConfig& config);
float ParryStaggerSeconds(const CombatRuleConfig& config);
float GuardBreakSeconds(const CombatRuleConfig& config);

float DodgeStaminaCost(const CombatRuleConfig& config);
float DodgeDurationSeconds(const CombatRuleConfig& config);
float DodgeInvulnSeconds(const CombatRuleConfig& config);
float DodgeCooldownSeconds(const CombatRuleConfig& config);
float DodgeDistance(const CombatRuleConfig& config);
// 闪避位移速度 = 距离 / 持续时间；CombatSystem 与单机输入共用。
float DodgeSpeed(const CombatRuleConfig& config);

float PoiseRegenPerSecond(const CombatRuleConfig& config);
float PoiseRegenDelaySeconds(const CombatRuleConfig& config);
float PoiseBreakStaggerSeconds(const CombatRuleConfig& config);
float PoiseResetRatio(const CombatRuleConfig& config);

}  // namespace zl::characters
