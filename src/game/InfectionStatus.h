#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include "characters/components/CombatComponents.h"

namespace zl::game {

inline const char* InfectionStageLabel(ecs::InfectionStage stage) {
    switch (stage) {
        case ecs::InfectionStage::Incubating:
            return "潜伏";
        case ecs::InfectionStage::Fever:
            return "发热/疲劳";
        case ecs::InfectionStage::Severe:
            return "重症";
        case ecs::InfectionStage::None:
            return "";
    }
    return "";
}

inline std::string InfectionDurationText(float seconds) {
    const int total = static_cast<int>(std::ceil(std::max(0.0f, seconds)));
    if (total >= 60) {
        const int minutes = total / 60;
        const int remaining = total % 60;
        if (remaining == 0) {
            return std::to_string(minutes) + " 分";
        }
        return std::to_string(minutes) + " 分 " + std::to_string(remaining) + " 秒";
    }
    return std::to_string(total) + " 秒";
}

inline std::string InfectionStatusText(const ecs::Infection& infection,
                                       std::string* tone = nullptr) {
    if (infection.stage == ecs::InfectionStage::None) {
        if (tone != nullptr) {
            *tone = "info";
        }
        return {};
    }
    const char* stage = InfectionStageLabel(infection.stage);
    const int progress = static_cast<int>(std::lround(
        std::clamp(infection.progress, 0.0f, 1.0f) * 100.0f));
    std::string text = "感染：" + std::string(stage) + " " +
                       std::to_string(progress) + "%";
    if (infection.timeToNextStage > 0.5f) {
        const char* next =
            infection.stage == ecs::InfectionStage::Incubating ? "发热"
            : infection.stage == ecs::InfectionStage::Fever   ? "重症"
                                                               : "死亡";
        text += " · " + std::string(next) + "约 " +
                InfectionDurationText(infection.timeToNextStage) + "后";
    }
    if (infection.slowRemaining > 0.5f) {
        text += " · 抗生素抑制 " +
                InfectionDurationText(infection.slowRemaining);
    }
    if (tone != nullptr) {
        *tone = infection.stage == ecs::InfectionStage::Severe ? "fail" : "warn";
    }
    return text;
}

}  // namespace zl::game
