#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "core/GameTime.h"
#include "ui/InventoryUi.h"
#include "ui/MenuUi.h"

namespace zl::game {
struct GameContext;
}

namespace zl::ui {
struct HudState;
}

namespace zl::script {

// 脚本返回值用的三元位置（x/y 为连续格坐标，z 为楼层）。
struct ScriptVec3 {
    float x = 0.0f;
    float y = 0.0f;
    int z = 0;
};

// Save 列表的展示快照；不向脚本暴露 SQLite/path 细节。
struct ScriptSaveInfo {
    std::string id;
    std::string name;
    std::string updatedText;
    std::string mapId;
    std::string gameVersion;
};

// M7-T3：Host API 薄包装（docs/03 §4）：
//  - 只暴露简单签名（数值/字符串/稳定 ID 句柄），不暴露任何内部类；
//  - 实体一律用 StableId（0 = 无效；跨存档有效）；
//  - 本类不依赖任何脚本库——各后端（Lua/Python）把它直译为同名脚本表；
//  - API 版本：mod.json 的 "api" 字段与本常量比对（M7-T5 加载器执行校验）。
class ScriptApi {
public:
    static constexpr int kApiVersion = 1;

    void Attach(game::GameContext* ctx) { m_ctx = ctx; }

    // ---- Game ----
    std::int64_t GetTick() const;
    std::uint64_t GetPlayerId() const;  // 0 = 无玩家
    core::DateTime GetTime() const;     // 无游戏时间源时返回默认日期
    double GetTotalGameMinutes() const;

    // ---- HUD / 核心 UI（只读快照 + 玩家可执行的核心 UI 动作）----
    bool GetHudState(ui::HudState& out) const;
    bool ToggleInventory() const;
    bool IsInventoryOpen() const;
    bool IsMenuOpen() const;

    // ---- Menu / core UI（主菜单/暂停菜单的脚本控制器）----
    bool GetMenuState(ui::MenuState& out) const;
    bool ApplyMenuSettings(const ui::MenuSettings& settings, std::string& error) const;
    bool MenuAction(const std::string& action) const;
    std::string MenuKeyName(int rmlKey) const;
    void PlayUiClick() const;

    // ---- Inventory / 核心背包 UI（脚本控制器读取状态并提交背包动作）----
    bool GetInventoryState(ui::InventoryState& out) const;
    bool InventoryAction(const ui::InventoryAction& action) const;
    void PlayInventoryClick() const;

    // ---- Save / 存档界面 ----
    bool ListSaves(std::vector<ScriptSaveInfo>& out, std::string& error) const;
    bool LoadSave(const std::string& id, std::string& error) const;
    bool DeleteSave(const std::string& id, std::string& error) const;
    bool QuickSave() const;

    // ---- Player ----
    bool GetPlayerPosition(ScriptVec3& out) const;
    bool TeleportPlayer(float x, float y, int z) const;  // 只改游戏状态，不改本对象
    bool GetPlayerHealth(float& out) const;
    int AddPlayerItem(const std::string& defId, int count) const;  // 返回实际加入数

    // ---- World ----
    bool IsPassable(int gx, int gy) const;  // z=0 地面层；未加载 chunk = 不可通行
    bool SpawnItem(const std::string& defId, float x, float y, int z) const;

    // ---- Zombies ----
    std::uint64_t SpawnZombie(float x, float y, int z) const;  // 0 = 失败
    int ZombieCount() const;
    bool GetZombiePosition(std::uint64_t handle, ScriptVec3& out) const;

    // ---- Items（只读注册表查询）----
    bool ItemExists(const std::string& defId) const;
    std::string ItemName(const std::string& defId) const;

private:
    entt::entity FindPlayer() const;

    game::GameContext* m_ctx = nullptr;
};

}  // namespace zl::script
