#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

#include "core/GameTime.h"
#include "ui/InventoryUi.h"
#include "ui/MenuUi.h"

namespace zl::game {
struct GameContext;
}

namespace zl::net {
struct Command;
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

struct ScriptSquareObject {
    std::string sprite;
    float hp = -1.0f;
    bool playerBuilt = false;
    bool blocksMovement = false;
};

struct ScriptSquare {
    bool loaded = false;
    bool hasFloor = false;
    ScriptSquareObject floor;
    std::vector<ScriptSquareObject> wall;
    std::vector<ScriptSquareObject> furniture;
    std::vector<ScriptSquareObject> item;
};

// Save 列表的展示快照；不向脚本暴露 SQLite/path 细节。
struct ScriptSaveInfo {
    std::string id;
    std::string name;
    std::string updatedText;
    std::string mapId;
    std::string gameVersion;
};

// M35-F3：写动作统一返回；error 可直接打日志。
struct ScriptActionResult {
    bool ok = false;
    bool queued = false;
    std::string error;
};

// M35-F3-T3：GiveXp 的即时奖励快照（不是玩家当前总等级/总 Perk）。
struct ScriptProgressionAward {
    int characterLevels = 0;
    int skillLevels = 0;
    int perkPoints = 0;
    float characterXp = 0.0f;
    float skillXp = 0.0f;
};

struct ScriptXpResult {
    ScriptActionResult result;
    ScriptProgressionAward award;
};

// ---- M35-F1：玩家只读状态（Lua table / Python dict 的同名快照）----
struct ScriptNeeds {
    float hunger = 0.0f;
    float thirst = 0.0f;
    float fatigue = 0.0f;
    float nutrition = 0.0f;
    float health = 0.0f;
    float healthMax = 0.0f;
    float moveScale = 1.0f;
    float damageScale = 1.0f;
    int warning = 0;
};

struct ScriptStamina {
    float current = 0.0f;
    float max = 0.0f;
    bool exhausted = false;
};

struct ScriptWound {
    std::string type;  // blunt/bite/claw/fire/debug
    float severity = 0.0f;
    bool bandaged = false;
    bool bleeding = false;
};

struct ScriptWounds {
    int count = 0;
    std::vector<ScriptWound> wounds;
};

struct ScriptPain {
    float level = 0.0f;
    float baseLevel = 0.0f;
    float reliefRemaining = 0.0f;
};

struct ScriptInfection {
    std::string stage;  // none/incubating/fever/severe
    float progress = 0.0f;
    float slowRemaining = 0.0f;
    float timeToNextStage = 0.0f;
};

struct ScriptClimate {
    float bodyC = 0.0f;
    float wetness = 0.0f;
    float ambientC = 0.0f;
    float effectiveC = 0.0f;
    float insulation = 0.0f;
    float waterproof = 0.0f;
    bool indoor = false;
    bool nearFire = false;
    int warning = 0;
};

struct ScriptSkillProgress {
    std::string id;
    int level = 0;
    float xp = 0.0f;
    float xpNext = 0.0f;
};

struct ScriptProgression {
    int level = 1;
    float xp = 0.0f;
    float xpNext = 0.0f;
    int perkPoints = 0;
    std::vector<ScriptSkillProgress> skills;
    std::vector<std::string> perks;
};

struct ScriptItemStack {
    int index = -1;
    std::string defId;
    std::string name;
    std::string type;
    int count = 0;
    float condition = 0.0f;
    int loadedAmmo = 0;
    float weightKg = 0.0f;
    int maxStack = 0;
    bool usable = false;
    bool equippable = false;
    bool perishable = false;
    float spoilDays = 0.0f;
};

struct ScriptInventory {
    float weightKg = 0.0f;
    float capacityKg = 0.0f;
    int totalStacks = 0;
    bool truncated = false;
    std::vector<ScriptItemStack> stacks;
};

struct ScriptEquipment {
    bool hasPrimary = false;
    ScriptItemStack primary;
    bool hasBody = false;
    ScriptItemStack body;
};

struct ScriptWeather {
    std::string kind;  // clear/rain/fog/cold
    float ambientC = 0.0f;
    bool isNight = false;
};

struct ScriptEntityInfo {
    std::uint64_t id = 0;
    std::string kind;  // player/zombie/animal/companion/npc/vehicle/container/storage/item/corpse/fire/other
    float x = 0.0f;
    float y = 0.0f;
    int z = 0;
    bool hasHealth = false;
    float health = 0.0f;
    float healthMax = 0.0f;
    std::string defId;
    std::string subKind;
    std::uint64_t ownerId = 0;
    int count = 0;
    std::string tableId;
    bool searched = false;
    bool downed = false;
    bool dead = false;
    std::string command;
};

struct ScriptEntityQuery {
    int count = 0;
    bool truncated = false;
    std::vector<ScriptEntityInfo> entities;
};

struct ScriptContainerInfo {
    std::uint64_t id = 0;
    std::string tableId;
    bool searched = false;
    bool isStorage = false;
    float capacityKg = 0.0f;
    float weightKg = 0.0f;
    int totalStacks = 0;
    bool truncated = false;
    std::vector<ScriptItemStack> stacks;
};

// ---- M35-F1-T7：目录类只读 ----
struct ScriptItemInfo {
    std::string id;
    std::string name;
    std::string icon;
    float weight = 0.0f;
    int maxStack = 0;
    std::string type;
    float spoilDays = 0.0f;
    float repairEfficiency = 0.0f;
    bool usable = false;
    bool equippable = false;
    bool throwable = false;
    bool ranged = false;
};

struct ScriptRecipeIngredient {
    std::string defId;
    std::string label;
    int required = 0;
    int have = 0;
    float wear = 0.0f;
};

struct ScriptRecipeInfo {
    int index = 0;
    std::string id;
    std::string name;
    std::string category;
    std::string categoryLabel;
    std::string outputDefId;
    std::string outputLabel;
    int outputCount = 0;
    float seconds = 0.0f;
    std::string station;
    std::string stationLabel;
    bool stationNearby = true;
    bool known = true;
    bool canCraft = false;
    std::vector<ScriptRecipeIngredient> ingredients;
    std::vector<ScriptRecipeIngredient> tools;
    bool hasCookware = false;
    ScriptRecipeIngredient cookware;
    bool hasFuel = false;
    ScriptRecipeIngredient fuel;
};

struct ScriptRecipeList {
    int count = 0;
    bool truncated = false;
    std::vector<ScriptRecipeInfo> recipes;
};

struct ScriptCraftQueueEntry {
    std::string recipeId;
    int count = 0;
};

struct ScriptCraftState {
    bool active = false;
    std::string recipeId;
    std::string outputDefId;
    std::string outputLabel;
    int outputCount = 0;
    float elapsed = 0.0f;
    float duration = 0.0f;
    float progress = 0.0f;
    int batchTotal = 0;
    int batchCompleted = 0;
    std::vector<ScriptCraftQueueEntry> queue;
};

struct ScriptBuildableInfo {
    int index = 0;
    std::string id;
    std::string name;
    std::string sprite;
    int woodCost = 0;
    float maxHp = 0.0f;
    bool blocksMovement = false;
    float storageKg = 0.0f;
    int storageSlots = 0;
    std::string layer;
    std::string category;
    bool door = false;
    std::string requiresMission;
};

struct ScriptBuildableList {
    int count = 0;
    bool truncated = false;
    std::vector<ScriptBuildableInfo> buildables;
};

struct ScriptMissionObjective {
    std::string kind;
    std::string target;
    int count = 0;
    int progress = 0;
    std::string description;
};

struct ScriptMissionReward {
    float xp = 0.0f;
    std::string skill;
    std::string item;
    int itemCount = 0;
    std::string extraItem;
    int extraItemCount = 0;
    std::string blueprint;
    std::string label;
};

struct ScriptMissionInfo {
    int index = 0;
    std::string id;
    std::string name;
    std::string chain;
    int order = 0;
    std::string kind;
    std::string source;
    std::string task;
    std::string status;  // active/completed/failed/available/locked
    int day = 0;
    std::vector<int> progress;
    std::vector<ScriptMissionObjective> objectives;
    ScriptMissionReward reward;
};

struct ScriptMissionList {
    int count = 0;
    bool truncated = false;
    std::vector<ScriptMissionInfo> missions;
};

// M7-T3：Host API 薄包装（docs/03 §4）：
//  - 只暴露简单签名（数值/字符串/稳定 ID 句柄），不暴露任何内部类；
//  - 实体一律用 StableId（0 = 无效；跨存档有效）；
//  - 本类不依赖任何脚本库——各后端（Lua/Python）把它直译为同名脚本表；
//  - API 版本：mod.json 的 "api" 字段与本常量比对（M7-T5 加载器执行校验）。
class ScriptApi {
public:
    static constexpr int kApiVersion = 2;

    void Attach(game::GameContext* ctx) { m_ctx = ctx; }

    // ---- Game ----
    int GetApiVersion() const { return kApiVersion; }
    bool IsMultiplayer() const;
    std::string GetNetMode() const;  // direct/loopback/client
    std::int64_t GetTick() const;
    std::uint64_t GetPlayerId() const;  // 0 = 无玩家
    core::DateTime GetTime() const;     // 无游戏时间源时返回默认日期
    double GetTotalGameMinutes() const;

    // ---- HUD / 核心 UI（HUD 快照 + Mod 字段 + 玩家可执行的核心 UI 动作）----
    bool GetHudState(ui::HudState& out) const;
    bool RegisterHudField(const std::string& modId, const std::string& id,
                          const std::string& label) const;
    bool SetHudField(const std::string& modId, const std::string& id,
                     const std::string& text, const std::string& tone) const;
    bool ClearHudField(const std::string& modId, const std::string& id) const;
    void RemoveHudFields(const std::string& modId) const;
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

    // ---- M35-F4-T2：Mod.Data 跨存档脚本状态 ----
    // 联机一律禁用（Get/Keys 空，Set/Remove/Clear=false）；写前做单值/单 mod/
    // 全局上限校验，失败不产生部分写入。
    std::optional<std::string> GetModData(const std::string& modId,
                                          const std::string& key) const;
    bool SetModData(const std::string& modId, const std::string& key,
                    const std::string& value) const;
    bool RemoveModData(const std::string& modId, const std::string& key) const;
    std::vector<std::string> ListModDataKeys(const std::string& modId) const;
    bool ClearModData(const std::string& modId) const;

    // ---- Player ----
    bool GetPlayerPosition(ScriptVec3& out) const;
    bool TeleportPlayer(float x, float y, int z) const;  // 只改游戏状态，不改本对象
    bool GetPlayerHealth(float& out) const;
    bool GetPlayerNeeds(ScriptNeeds& out) const;
    bool GetPlayerStamina(ScriptStamina& out) const;
    bool GetPlayerWounds(ScriptWounds& out) const;
    bool GetPlayerPain(ScriptPain& out) const;
    bool GetPlayerInfection(ScriptInfection& out) const;
    bool GetPlayerClimate(ScriptClimate& out) const;
    bool GetPlayerProgression(ScriptProgression& out) const;
    bool GetPlayerInventory(int offset, int limit, ScriptInventory& out) const;
    bool GetPlayerEquipment(ScriptEquipment& out) const;
    int CountPlayerItem(const std::string& defId) const;
    bool HasPlayerItem(const std::string& defId, int count) const;
    int AddPlayerItem(const std::string& defId, int count) const;  // 返回实际加入数

    // ---- M35-F3：命令化动作 ----
    // 填 playerStableId/tick，再走 GameContext::submitPlayerCommand；
    // 不在本层判断网络模式（Direct/Loopback/KCP 由 WorldDemoState 注入入口决定）。
    ScriptActionResult SubmitCommand(net::Command command) const;
    ScriptActionResult UseItem(int index = -1) const;
    ScriptActionResult EquipItem(int index, const std::string& slot = "primary") const;
    ScriptActionResult Unequip(const std::string& slot) const;
    ScriptActionResult DropItem(int index = -1) const;
    ScriptActionResult PickupNearest() const;
    ScriptActionResult Pickup(std::uint64_t entityId) const;
    ScriptActionResult Attack(std::uint64_t targetId = 0, bool heavy = false) const;

    // ---- M35-F3-T3：单机直接状态效果（联机一律 ok=false）----
    // ApplyDamage 仍走 CombatSystem::ApplyDamage（护甲/格挡/闪避/伤口均生效）；
    // 无方向参数，有 Facing 时按正面来袭处理，Debug/Fire 仍不可格挡。
    ScriptActionResult ApplyDamage(float amount,
                                   const std::string& damageType = "blunt") const;
    // 增量修改需求并钳制到 [0,1]；负值/NaN 拒绝，0/缺省表示不变。
    ScriptActionResult AdjustNeeds(float hunger = 0.0f, float thirst = 0.0f,
                                   float fatigue = 0.0f) const;
    // 直接发放角色+技能经验；返回本次升级/Perk 点，内部已 RefreshDerived。
    ScriptXpResult GiveXp(const std::string& skill, float amount) const;

    // ---- M35-F3-T4：制作动作 ----
    // Start/Cancel 走 InventoryAction 命令（Direct 立即执行，loopback/KCP 排队）；
    // 提交前校验配方/学会/材料/站点。Learn 仅单机，直接追加 KnownRecipes。
    ScriptActionResult StartCraft(const std::string& recipeId, int count = 1) const;
    ScriptActionResult CancelCraft() const;
    ScriptActionResult LearnRecipe(const std::string& recipeId) const;

    // ---- M35-F3-T5：建造动作 ----
    // CanPlace 仅单机本地预检；Place/Demolish/Repair/ToggleDoor 在 Direct 直调
    // GameContext hook，loopback/KCP 提交 PlaceStructure/DemolishStructure/
    // RepairStructure/ToggleDoor 命令。
    ScriptActionResult CanPlaceBuild(const std::string& defId, int wx, int wy,
                                     bool vertical = false) const;
    ScriptActionResult PlaceBuild(const std::string& defId, int wx, int wy,
                                  bool vertical = false) const;
    ScriptActionResult DemolishBuild(int wx, int wy) const;
    ScriptActionResult RepairBuild(int wx, int wy) const;
    ScriptActionResult ToggleBuildDoor(int wx, int wy) const;

    // ---- World ----
    bool IsPassable(int gx, int gy) const;  // z=0 地面层；未加载 chunk = 不可通行
    bool GetSquare(int gx, int gy, int z, ScriptSquare& out) const;
    bool SpawnItem(const std::string& defId, float x, float y, int z) const;
    bool EmitSound(float x, float y, float radius,
                   const std::string& kind = "generic",
                   const std::string& clip = "") const;
    // M35-F5-T3：只播放已有 clip（不含扩展名/路径）；不进入 AI 听觉。
    bool PlayAudio2D(const std::string& clip, float volume = 1.0f) const;
    bool PlayAudio3D(const std::string& clip, float x, float y,
                     float radius = 0.0f, float volume = 1.0f) const;
    bool GetWeather(ScriptWeather& out) const;
    float GetTimeOfDay() const;  // 0~1

    // ---- Entities（M35-F1：只读实体/容器查询）----
    bool GetEntity(std::uint64_t id, ScriptEntityInfo& out) const;
    bool QueryEntities(float x, float y, int z, float radius,
                       const std::string& kind, int limit,
                       ScriptEntityQuery& out) const;
    // M35-F5-T4：绑定层带 mod 归属；C++ 直调可继续用上面的无归属重载。
    bool QueryEntitiesForMod(const std::string& modId, float x, float y, int z,
                             float radius, const std::string& kind, int limit,
                             ScriptEntityQuery& out) const;
    // 每个 tick 末汇总一次查询次数/截断次数（仅日志，不强制限流）。
    void FlushQueryStats() const;
    bool GetEntityContainer(std::uint64_t id, ScriptContainerInfo& out) const;

    // ---- Zombies ----
    // variant: normal/fast/strong/armored；0 = 失败。
    std::uint64_t SpawnZombie(float x, float y, int z,
                              const std::string& variant = "normal") const;
    int ZombieCount() const;
    bool GetZombiePosition(std::uint64_t handle, ScriptVec3& out) const;

    // ---- Items（只读注册表查询）----
    bool ItemExists(const std::string& defId) const;
    std::string ItemName(const std::string& defId) const;
    bool GetItemInfo(const std::string& defId, ScriptItemInfo& out) const;

    // ---- Craft / Build / Mission（M35-F1-T7：目录类只读）----
    bool ListRecipes(ScriptRecipeList& out) const;
    bool GetCraftState(ScriptCraftState& out) const;
    bool IsRecipeKnown(const std::string& recipeId) const;
    bool ListBuildables(ScriptBuildableList& out) const;
    bool ListMissions(const std::string& filter, ScriptMissionList& out) const;
    bool GetMission(const std::string& id, ScriptMissionInfo& out) const;

    int RegisterItems(const std::string& modId, const std::string& json,
                      std::string& error) const;
    int RegisterRecipes(const std::string& modId, const std::string& json,
                        std::string& error) const;
    int RegisterBuildings(const std::string& modId, const std::string& json,
                          std::string& error) const;
    int RegisterPois(const std::string& modId, const std::string& json,
                     std::string& error) const;
    int RegisterMissions(const std::string& modId, const std::string& json,
                         std::string& error) const;

private:
    struct QueryTickStats {
        std::int64_t tick = 0;
        int queries = 0;
        int truncated = 0;
    };

    entt::entity FindPlayer() const;
    bool ValidateBagIndex(int index, bool allowDefault, std::string& error) const;
    bool ParseEquipSlot(const std::string& slot, int& out) const;
    bool ValidateAttackTarget(std::uint64_t targetId, std::string& error) const;

    game::GameContext* m_ctx = nullptr;
    mutable std::unordered_map<std::string, QueryTickStats> m_queryStats;
};

}  // namespace zl::script
