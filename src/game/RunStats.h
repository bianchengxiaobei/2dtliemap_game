#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "characters/components/CombatComponents.h"

namespace zl::game {

struct RunSummary {
    bool victory = false;
    ecs::DamageCause deathCause = ecs::DamageCause::Unknown;
    int day = 1;
    int hour = 0;
    int minute = 0;
    double survivalSeconds = 0.0;
    std::uint64_t kills = 0;
    std::uint64_t injuries = 0;
    float damageTaken = 0.0f;
};

inline const char* DeathCauseLabel(ecs::DamageCause cause) {
    switch (cause) {
        case ecs::DamageCause::Blunt:
            return "钝击";
        case ecs::DamageCause::Bite:
            return "撕咬";
        case ecs::DamageCause::Claw:
            return "爪击";
        case ecs::DamageCause::Debug:
            return "调试伤害";
        case ecs::DamageCause::Bleed:
            return "流血";
        case ecs::DamageCause::Starvation:
            return "饥饿";
        case ecs::DamageCause::Dehydration:
            return "脱水";
        case ecs::DamageCause::Infection:
            return "感染";
        case ecs::DamageCause::Hypothermia:
            return "失温";
        case ecs::DamageCause::Fire:
            return "火焰";
        case ecs::DamageCause::Poison:
            return "食物中毒";
        case ecs::DamageCause::Unknown:
            return "未知";
    }
    return "未知";
}

inline std::string FormatSurvivalTime(double seconds) {
    const auto total = static_cast<std::uint64_t>(
        std::llround(std::max(0.0, seconds)));
    const std::uint64_t hours = total / 3600u;
    const std::uint64_t minutes = (total % 3600u) / 60u;
    const std::uint64_t remaining = total % 60u;
    if (hours > 0) {
        return std::to_string(hours) + " 小时 " + std::to_string(minutes) + " 分";
    }
    if (minutes > 0) {
        return std::to_string(minutes) + " 分 " + std::to_string(remaining) + " 秒";
    }
    return std::to_string(remaining) + " 秒";
}

inline std::string TwoDigits(int value) {
    return value < 10 ? "0" + std::to_string(value) : std::to_string(value);
}

inline std::string RunSummaryText(const RunSummary& summary) {
    std::string out;
    if (!summary.victory) {
        out += "死因：";
        out += DeathCauseLabel(summary.deathCause);
        out += "<br/>";
    }
    out += "存活：";
    out += FormatSurvivalTime(summary.survivalSeconds);
    out += "（第 ";
    out += std::to_string(std::max(1, summary.day));
    out += " 天 ";
    out += TwoDigits(std::clamp(summary.hour, 0, 23));
    out += ":";
    out += TwoDigits(std::clamp(summary.minute, 0, 59));
    out += "）<br/>击杀：";
    out += std::to_string(summary.kills);
    out += " 只僵尸<br/>受伤：";
    out += std::to_string(summary.injuries);
    out += " 次";
    if (summary.damageTaken > 0.0f) {
        out += "（累计 ";
        out += std::to_string(
            static_cast<long long>(std::lround(summary.damageTaken)));
        out += " 点）";
    }
    return out;
}

}  // namespace zl::game
