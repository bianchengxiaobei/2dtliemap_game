#pragma once

#include "characters/components/CharacterComponents.h"
#include "characters/systems/CombatRules.h"
#include "items/ItemDef.h"

namespace zl::game {

// M30-T3：单机玩家近战起手参数。轻击沿用武器 combat 覆盖；
// 重击先按 T1 规则从轻击派生，再让武器 combat.heavy 的显式字段覆盖。
struct PlayerMeleeAttackPlan {
    items::WeaponAttackParams params;
    bool heavy = false;
};

PlayerMeleeAttackPlan ResolvePlayerMeleeAttack(
    const items::ItemDef* weapon, const items::WeaponAttackParams& base,
    bool heavy, const characters::CombatRuleConfig& config);

// M30-T4：主手武器的格挡覆盖值；<0 表示使用全局配置。
struct PlayerBlockPlan {
    float blockReduction = -1.0f;
    float blockStaminaCost = -1.0f;
};

PlayerBlockPlan ResolvePlayerBlock(const items::ItemDef* weapon);

// M30-T4：闪避方向（世界格空间）与速度。worldDir 为零时回退到 Facing。
struct PlayerDodgePlan {
    float dirX = 0.0f;
    float dirY = 1.0f;
    float speed = 0.0f;
};

PlayerDodgePlan ResolvePlayerDodge(float worldDirX, float worldDirY,
                                   ecs::Direction facing,
                                   const characters::CombatRuleConfig& config);

// 无目标空挥时使用玩家当前朝向；Facing 是屏幕 4 向，输出世界格方向。
void FacingAttackDirection(ecs::Direction dir, float* x, float* y);

}  // namespace zl::game
