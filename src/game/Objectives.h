#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace zl::ecs {

enum class ObjectiveStep : std::uint8_t {
    FindRadioPart = 0,
    RepairRadio = 1,
    ReachExtraction = 2,
    Complete = 3,
};

struct Objective {
    ObjectiveStep step = ObjectiveStep::FindRadioPart;
};

inline constexpr std::uint8_t kObjectiveStepCount = 4;

}  // namespace zl::ecs

namespace zl::game {

inline constexpr const char* kRadioPartDefId = "radio_part";

inline bool IsObjectiveStepValid(std::uint8_t step) {
    return step < ecs::kObjectiveStepCount;
}

inline ecs::ObjectiveStep AdvanceObjective(ecs::ObjectiveStep step, bool hasPart,
                                           bool atExtraction) {
    if (step == ecs::ObjectiveStep::FindRadioPart && hasPart) {
        return ecs::ObjectiveStep::RepairRadio;
    }
    if (step == ecs::ObjectiveStep::ReachExtraction && atExtraction) {
        return ecs::ObjectiveStep::Complete;
    }
    return step;
}

inline const char* ObjectiveTone(ecs::ObjectiveStep step) {
    switch (step) {
        case ecs::ObjectiveStep::Complete:
            return "ok";
        case ecs::ObjectiveStep::RepairRadio:
        case ecs::ObjectiveStep::ReachExtraction:
            return "warn";
        case ecs::ObjectiveStep::FindRadioPart:
        default:
            return "info";
    }
}

inline const char* ObjectiveStepName(ecs::ObjectiveStep step) {
    switch (step) {
        case ecs::ObjectiveStep::FindRadioPart:
            return "find_radio_part";
        case ecs::ObjectiveStep::RepairRadio:
            return "repair_radio";
        case ecs::ObjectiveStep::ReachExtraction:
            return "reach_extraction";
        case ecs::ObjectiveStep::Complete:
            return "complete";
        default:
            return "unknown";
    }
}

inline std::string ObjectiveText(ecs::ObjectiveStep step, bool hasPart,
                                 float extractionX, float extractionY) {
    switch (step) {
        case ecs::ObjectiveStep::FindRadioPart:
            return "目标：进入房屋，搜寻无线电零件";
        case ecs::ObjectiveStep::RepairRadio:
            if (!hasPart) {
                return "目标：零件丢失，回屋内寻找无线电零件";
            }
            return "目标：带零件到电台，按 E 修理";
        case ecs::ObjectiveStep::ReachExtraction: {
            char text[128];
            std::snprintf(text, sizeof(text), "目标：前往撤离点 (%.0f, %.0f)",
                          static_cast<double>(extractionX),
                          static_cast<double>(extractionY));
            return text;
        }
        case ecs::ObjectiveStep::Complete:
            return "目标完成：已到达撤离点";
        default:
            return "目标：--";
    }
}

}  // namespace zl::game
