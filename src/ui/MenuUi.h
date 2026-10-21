#pragma once

#include <functional>
#include <string>
#include <vector>

namespace zl::ui {

// M9-T6：主菜单/暂停菜单动作（设置页内部的“返回/应用”不经过游戏状态）。
enum class MenuActionType {
    NewGame,
    Multiplayer,
    Continue,
    Resume,
    ReturnToMainMenu,
    Quit,
};

struct MenuKeyBinding {
    std::string action;  // config input.bindings.* key
    std::string label;   // 中文操作名
    std::string key;     // 平台键名（W / SPACE / F1 …）
};

struct MenuSettings {
    int volume = 65;         // Master
    int musicVolume = 70;    // Music
    int sfxVolume = 85;      // SFX
    int ambientVolume = 55;  // Ambient
    std::vector<MenuKeyBinding> bindings;
};

// 脚本菜单控制器读取的只读状态。原生 MenuPanel 与脚本控制共用这份数据模型。
struct MenuState {
    bool open = false;
    bool pauseMode = false;
    bool canContinue = false;
    MenuSettings settings;
};

// 设置页的固定键位行（UI 行顺序）；GameApp 用同一张表读取/写回 config。
struct MenuKeyBindingDef {
    const char* action;
    const char* label;
    const char* elementId;
    const char* defaultKey;
};

inline constexpr MenuKeyBindingDef kMenuBindingDefs[] = {
    {"MoveUp", "向上移动", "bind-MoveUp", "W"},
    {"MoveDown", "向下移动", "bind-MoveDown", "S"},
    {"MoveLeft", "向左移动", "bind-MoveLeft", "A"},
    {"MoveRight", "向右移动", "bind-MoveRight", "D"},
    {"Attack", "攻击", "bind-Attack", "SPACE"},
    {"Pickup", "拾取", "bind-Pickup", "F"},
    {"Use", "使用", "bind-Use", "E"},
    {"Equip", "装备", "bind-Equip", "R"},
    {"Drop", "丢弃", "bind-Drop", "G"},
    {"Pause", "暂停/倍速", "bind-Pause", "F1"},
};

using MenuActionHandler = std::function<void(MenuActionType)>;
using MenuSettingsHandler =
    std::function<bool(const MenuSettings& settings, std::string& error)>;

}  // namespace zl::ui
