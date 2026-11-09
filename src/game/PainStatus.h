#pragma once

#include <cmath>
#include <string>

#include "characters/components/CombatComponents.h"

namespace zl::game {

inline std::string PainStatusText(const ecs::Pain& pain, std::string* tone = nullptr) {
    const float level = pain.level;
    const char* label = level >= 0.6f ? "严重" : level >= 0.3f ? "中度" : "轻微";
    const int percent = static_cast<int>(std::lround(level * 100.0f));
    const int moveSlow =
        static_cast<int>(std::lround((1.0f - pain.moveScale) * 100.0f));
    const int attackSlow =
        static_cast<int>(std::lround((pain.attackCooldownScale - 1.0f) * 100.0f));
    std::string text = "疼痛：" + std::string(label) + " " + std::to_string(percent) +
                       "%（移速 -" + std::to_string(moveSlow) + "%，挥击间隔 +" +
                       std::to_string(attackSlow) + "%）";
    if (pain.reliefRemaining > 0.0f) {
        text += "｜止痛 " +
                std::to_string(static_cast<int>(std::ceil(pain.reliefRemaining))) + "s";
    }
    if (tone != nullptr) {
        *tone = level >= 0.6f ? "fail" : level >= 0.3f ? "warn" : "info";
    }
    return text;
}

}  // namespace zl::game
