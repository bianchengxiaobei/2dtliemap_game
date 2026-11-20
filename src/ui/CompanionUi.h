#pragma once

#include <string>

namespace zl::ui {

// M33-F3-T7：同伴 HUD/面板的只读快照。文本在游戏层统一生成，UiSystem 只负责
// 写入固定元素，避免把物品/委托细节散落到 RmlUi 代码。
struct CompanionUiState {
    bool valid = false;
    bool open = false;
    std::string name;
    std::string health;
    std::string status;
    std::string trust;
    std::string equipment;
    std::string bag;
    std::string quest;
    std::string hint;
    std::string alert;
    std::string alertTone = "info";
    bool downed = false;
    bool lowHealth = false;
    bool away = false;
    bool stuck = false;
    bool standby = false;
    bool dead = false;
    bool canCommand = false;
    bool canGift = false;
    bool canRescue = false;
    float healthCurrent = 0.0f;
    float healthMax = 0.0f;
    int loyalty = 0;

    bool operator==(const CompanionUiState& other) const = default;
};

}  // namespace zl::ui
