#pragma once

#include <algorithm>
#include <cmath>

#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"

namespace zl::game {

struct CombatPresentationState {
    bool visible = false;
    bool blocking = false;
    bool dodging = false;
    bool parryActive = false;
    bool guardBroken = false;
    bool invulnerable = false;
    float blockAlpha = 0.0f;
    float dodgeAlpha = 0.0f;
    float dodgeDirX = 0.0f;
    float dodgeDirY = 0.0f;
};

// M30-T10：一次近战挥击的程序化表现参数。使用 ActionState 的演示时长驱动
// 弧形轨迹，而不是新增手绘帧；远端没有 AttackIntent 时按普通轻击半径回退。
struct MeleeSwingPresentation {
    bool active = false;
    bool heavy = false;
    float progress = 0.0f;
    float alpha = 0.0f;
    float radius = 44.0f;
    float halfAngleRadians = 0.85f;
    float thickness = 4.0f;
};

inline MeleeSwingPresentation ResolveMeleeSwingPresentation(
    const ecs::ActionState* action, const ecs::AttackIntent* attack,
    float fallbackDurationSeconds = 0.28f) {
    MeleeSwingPresentation result;
    if (action == nullptr || action->action != ecs::ActorAction::Attacking ||
        !std::isfinite(action->elapsed)) {
        return result;
    }
    float duration = action->duration;
    if (!std::isfinite(duration) || duration <= 0.0f) {
        duration = std::isfinite(fallbackDurationSeconds)
                       ? std::max(0.01f, fallbackDurationSeconds)
                       : 0.28f;
    }
    const float progress =
        std::clamp(action->elapsed / duration, 0.0f, 1.0f);
    result.active = true;
    result.heavy = attack != nullptr && attack->kind == ecs::AttackKind::Heavy;
    result.progress = progress;
    result.alpha = 0.20f + 0.80f *
                                std::sin(progress * 3.14159265358979323846f);
    float range = attack != nullptr && std::isfinite(attack->range)
                      ? attack->range
                      : 1.7f;
    range = std::clamp(range, 0.0f, 4.0f);
    result.radius = 30.0f + range * 8.0f + (result.heavy ? 8.0f : 0.0f);
    result.halfAngleRadians = result.heavy ? 1.05f : 0.85f;
    result.thickness = result.heavy ? 7.0f : 4.0f;
    return result;
}

inline CombatPresentationState ResolveCombatPresentation(
    const ecs::CombatStance& stance, float parryWindowSeconds,
    float dodgeDurationSeconds) {
    CombatPresentationState result;
    const bool dodgeActive =
        std::isfinite(stance.dodgeLeft) && stance.dodgeLeft > 0.0f;
    result.dodging = dodgeActive;
    result.blocking = !dodgeActive && stance.blocking;
    result.guardBroken = std::isfinite(stance.guardBreakLeft) &&
                         stance.guardBreakLeft > 0.0f;
    result.visible = result.blocking || result.dodging || result.guardBroken;
    if (!result.visible) {
        return result;
    }

    if (result.blocking) {
        const float window =
            std::isfinite(parryWindowSeconds)
                ? std::max(0.0f, parryWindowSeconds)
                : 0.0f;
        result.parryActive =
            window > 0.0f && std::isfinite(stance.blockElapsed) &&
            stance.blockElapsed >= 0.0f &&
            stance.blockElapsed <= window;
        result.blockAlpha = result.parryActive ? 1.0f : 0.72f;
    }

    if (result.dodging) {
        const float duration =
            std::isfinite(dodgeDurationSeconds) && dodgeDurationSeconds > 0.0f
                ? dodgeDurationSeconds
                : 0.28f;
        result.dodgeAlpha =
            std::clamp(stance.dodgeLeft / duration, 0.0f, 1.0f);
        const float length =
            std::sqrt(stance.dodgeDirX * stance.dodgeDirX +
                      stance.dodgeDirY * stance.dodgeDirY);
        if (std::isfinite(length) && length > 0.0001f) {
            result.dodgeDirX = stance.dodgeDirX / length;
            result.dodgeDirY = stance.dodgeDirY / length;
        }
        result.invulnerable = std::isfinite(stance.dodgeInvulnLeft) &&
                              stance.dodgeInvulnLeft > 0.0f;
    }
    return result;
}

}  // namespace zl::game
