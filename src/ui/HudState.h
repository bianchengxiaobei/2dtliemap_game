#pragma once

#include <array>
#include <string>

namespace zl::ui {

struct HudSlot {
    std::string label;
    int count = 0;
};

struct HudState {
    bool active = false;
    std::string day;
    std::string time;
    std::string speed;
    bool playerValid = false;
    float health = 0.0f;
    float healthMax = 100.0f;
    float stamina = 0.0f;
    float staminaMax = 100.0f;
    bool needsValid = false;
    std::string needsStatus;
    std::string needsTone = "info";
    std::string objective;
    std::string objectiveTone = "info";
    std::string objectiveHint;
    std::array<HudSlot, 5> slots{};
    int selectedSlot = 0;
    std::string notice;
    std::string noticeTone = "info";
    float noticeRemaining = 0.0f;
    float noticeDuration = 1.0f;
};

}  // namespace zl::ui
