#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace zl::core {
class Config;
}

namespace zl::game {

// M19-F2：小型 POI 数据与纯规则。地图布局在 tools/gen_map.py；
// 运行时配置在 config/game.json 的 pois.defs，二者坐标由 Python 数据测试交叉校验。
enum class PoiGuardKind : std::uint8_t {
    Normal = 0,
    Fast = 1,
    Strong = 2,
    Mixed = 3,
    Armored = 4,  // M23-F6-T3：重甲军械守卫
};

struct PoiContainerDef {
    float x = 0.0f;
    float y = 0.0f;
    std::string tableId;
    std::string sprite = "object.crate";
};

struct PoiEnemyDef {
    int count = 0;
    float radius = 8.0f;
    PoiGuardKind kind = PoiGuardKind::Mixed;
    int armoredCount = 0;  // 前 N 个守军替换为重甲变体（不超过 count）
};

struct PoiTaskDef {
    std::string description;
    std::string targetTable;
    float rewardXp = 0.0f;
    std::string rewardItem;
    int rewardCount = 1;
};

// M33-F5-T1：多阶段 POI 的只读数据契约。阶段按数组顺序推进，
// next 只能引用后面的阶段；具体运行时状态由 F5-T2 维护。
enum class PoiStageKind : std::uint8_t {
    Reach = 0,
    Interact = 1,
    Kill = 2,
    Loot = 3,
    Power = 4,
    Choice = 5,
};

struct PoiStageRewardDef {
    float xp = 0.0f;
    std::string item;
    int count = 1;
};

struct PoiStageDef {
    std::string id;
    PoiStageKind kind = PoiStageKind::Reach;
    std::string hint;
    std::string target;
    float x = 0.0f;
    float y = 0.0f;
    float radius = 1.5f;
    int count = 1;
    std::vector<std::string> next;
    std::vector<std::string> requiredKeys;  // PoiKeyDef.id
    PoiStageRewardDef reward;
};

struct PoiEntryDef {
    std::string id;
    std::string label;
    float x = 0.0f;
    float y = 0.0f;
    std::string stage;        // 可选：进入后从该阶段开始
    std::string requiredKey;  // 可选：PoiKeyDef.id
};

struct PoiKeyDef {
    std::string id;
    std::string item;
    std::string name;
    std::string source;  // 可选：发放该钥匙的阶段 id；空表示外部来源
    bool consumed = true;
};

struct PoiBranchDef {
    std::string id;
    std::string name;
    std::string from;  // 可选：分支起始阶段 id
    std::vector<std::string> stages;
};

struct PoiBossDef {
    bool enabled = false;
    std::string name;
    std::string variant;
    std::string stage;     // 可选：缺省为最后一个阶段
    std::string mechanic;  // summon / charge / fortify；空表示无特殊机制
    int count = 1;
};

struct PoiNarrativeDef {
    std::string id;
    std::string stage;  // 可选：绑定阶段
    std::string text;
    std::string item;  // 可选：可作为搜索物的物品 id
};

// M33-F5-T4：阶段战斗敌人。Boss/精英计入 kill 阶段目标，召唤物不计入。
enum class PoiStageEnemyRole : std::uint8_t {
    Boss = 0,
    Elite = 1,
    Summon = 2,
};

enum class PoiBossMechanic : std::uint8_t {
    None = 0,
    Summon = 1,
    Charge = 2,
    Fortify = 3,
};

struct PoiStageEnemySpec {
    PoiStageEnemyRole role = PoiStageEnemyRole::Elite;
    PoiGuardKind kind = PoiGuardKind::Armored;
    int index = 0;
};

struct PoiDef {
    std::string id;
    std::string name;
    int threat = 1;
    float markerX = 0.0f;
    float markerY = 0.0f;
    std::vector<PoiContainerDef> containers;
    PoiEnemyDef enemies;
    PoiTaskDef task;
    // 以下字段均可选；为空时保持 v1 单段 POI 行为。
    std::vector<PoiStageDef> stages;
    std::vector<PoiEntryDef> entries;
    std::vector<PoiKeyDef> keys;
    std::vector<PoiBranchDef> branches;
    PoiBossDef boss;
    std::vector<PoiNarrativeDef> narrative;
};

// M33-F5-T2：世界级阶段状态。所有玩家共享，服务器/单机权威推进。
struct PoiProgress {
    std::string poiId;
    int currentStage = -1;
    int failedStage = -1;
    std::vector<std::string> completedStages;
    std::string chosenBranch;
    std::vector<std::string> acquiredKeys;
    bool powered = false;
    bool failed = false;
    bool awaitingChoice = false;
    bool completed = false;
    int attempts = 0;
    int stageKills = 0;
    int lastLiveGuards = 0;
};

enum class PoiStageSignal : std::uint8_t {
    Interact = 0,
    Loot = 1,
    Power = 2,
    Kill = 3,
};

struct PoiStageUpdate {
    bool started = false;
    bool stageCompleted = false;
    bool advanced = false;
    bool awaitingChoice = false;
    bool poiCompleted = false;
    bool blocked = false;
    bool failed = false;
    bool rewardAvailable = false;
    std::string stageId;
    std::string nextStageId;
    PoiStageRewardDef reward;
};

PoiProgress* FindPoiProgress(std::vector<PoiProgress>* progress,
                             std::string_view poiId);
const PoiProgress* FindPoiProgress(const std::vector<PoiProgress>& progress,
                                   std::string_view poiId);
PoiProgress& EnsurePoiProgress(std::vector<PoiProgress>* progress,
                               std::string poiId);
bool IsPoiStageCompleted(const PoiProgress& progress, std::string_view stageId);
bool StartPoiProgress(const PoiDef& def, PoiProgress* progress);
bool StartPoiProgressAtStage(const PoiDef& def, PoiProgress* progress,
                             std::string_view stageId);
const PoiStageDef* FindPoiStage(const PoiDef& def, std::string_view stageId);
const PoiKeyDef* FindPoiKey(const PoiDef& def, std::string_view keyId);
const PoiEntryDef* FindPoiEntry(const PoiDef& def, std::string_view entryId);
const PoiStageDef* CurrentPoiStage(const PoiDef& def,
                                   const PoiProgress& progress);
bool PoiStageKeysMet(const PoiDef& def, const PoiProgress& progress,
                     const PoiStageDef& stage);
bool MarkPoiKeyAcquired(std::vector<std::string>* keys,
                        std::string_view keyId);
std::vector<const PoiKeyDef*> PoiStageKeyAwards(const PoiDef& def,
                                                std::string_view stageId);
std::vector<const PoiBranchDef*> PoiAvailableBranches(
    const PoiDef& def, const PoiProgress& progress);
std::string PoiKeyDisplayName(const PoiKeyDef& key);
std::string PoiStageObjectiveText(const PoiDef& def,
                                  const PoiProgress& progress,
                                  const PoiStageDef& stage);
std::string PoiStageRequiredKeysText(const PoiDef& def,
                                     const PoiProgress& progress,
                                     const PoiStageDef& stage);
std::string PoiChoiceText(const PoiDef& def, const PoiProgress& progress);
// M33-F5-T5：结构化阶段 HUD、地图标记与叙事提示。单机/服务器客户端共用。
std::string PoiStageProgressText(const PoiDef& def,
                                 const PoiProgress& progress);
std::string PoiStageHudObjective(const PoiDef& def,
                                 const PoiProgress& progress);
std::string PoiStageHudRequirements(const PoiDef& def,
                                    const PoiProgress& progress);
std::string PoiStageNarrativeText(const PoiDef& def,
                                  std::string_view stageId);
std::string PoiStageMapStatusText(const PoiDef& def,
                                  const PoiProgress& progress);
std::string PoiStageResultTitle(const PoiDef& def, bool completed);
std::string PoiStageResultText(const PoiDef& def,
                               const PoiProgress& progress, bool completed);
bool PoiStageMapPoint(const PoiDef& def, const PoiProgress& progress,
                      float* x, float* y);
PoiStageUpdate TickPoiStage(const PoiDef& def, PoiProgress* progress, float x,
                            float y);
PoiStageUpdate SignalPoiStage(const PoiDef& def, PoiProgress* progress,
                              PoiStageSignal signal, int amount = 1);
PoiStageUpdate ChoosePoiBranch(const PoiDef& def, PoiProgress* progress,
                               std::string_view branchId);
bool FailPoiProgress(PoiProgress* progress);
bool RetryPoiProgress(const PoiDef& def, PoiProgress* progress);
bool ClaimPoiStageReward(std::vector<std::string>* claims,
                         std::string_view poiId, std::string_view stageId);

// M33-F5-T4：Boss 机制与阶段敌人计划。纯数据，单机/服务器共用。
const char* PoiStageEnemyRoleName(PoiStageEnemyRole role);
PoiBossMechanic PoiBossMechanicFor(std::string_view mechanic);
const char* PoiBossMechanicName(PoiBossMechanic mechanic);
const char* PoiBossMechanicText(PoiBossMechanic mechanic);
const PoiStageDef* PoiBossStage(const PoiDef& def);
bool PoiStageHasBoss(const PoiDef& def, const PoiStageDef& stage);
std::vector<PoiStageEnemySpec> BuildPoiStageEnemyPlan(
    const PoiDef& def, const PoiStageDef& stage);

struct PoiBossRuntimeConfig {
    float healthScale = 2.0f;
    float resetRange = 24.0f;
    float resetSeconds = 10.0f;
    float summonIntervalSeconds = 8.0f;
    int summonCount = 2;
    int summonMax = 4;
    int summonTotal = 6;
    float chargeIntervalSeconds = 8.0f;
    float chargeSpeedScale = 2.8f;
    float chargeSeconds = 2.0f;
};

struct PoiConfig {
    bool enabled = false;
    float guideRange = 120.0f;
    PoiBossRuntimeConfig boss;
    std::vector<PoiDef> defs;

    const PoiDef* Find(std::string_view id) const {
        for (const PoiDef& def : defs) {
            if (def.id == id) {
                return &def;
            }
        }
        return nullptr;
    }
};

// 解析并校验 pois 段；结构性错误失败，单条非法定义跳过并告警。
bool LoadPoiConfig(const core::Config& config, PoiConfig* out, std::string* error);

struct PoiMergeStats {
    int filesLoaded = 0;
    int defsAdded = 0;
    int defsSkipped = 0;
};

bool MergePoiFile(const std::filesystem::path& path, PoiConfig* out,
                  PoiMergeStats* stats = nullptr, std::string* error = nullptr);
bool MergePoiJson(std::string_view json, const std::string& source,
                  PoiConfig* out, PoiMergeStats* stats = nullptr,
                  std::string* error = nullptr);

const char* PoiGuardKindName(PoiGuardKind kind);
PoiGuardKind PoiGuardKindForSpawn(const PoiEnemyDef& enemies, int index);
const char* PoiStageKindName(PoiStageKind kind);
const char* PoiThreatLabel(int threat);

// 玩家到 POI 的方向（格坐标；y 向下为南）。
std::string PoiDirectionLabel(float dx, float dy);

// 当前任务提示；targetSearched 与剩余守军共同决定文案。
std::string PoiTaskText(const PoiDef& def, bool targetSearched, int liveGuards);

// HUD 指引：POI 名称、威胁、方向/距离、任务进度。
std::string PoiGuideText(const PoiDef& def, float dx, float dy, float distance,
                         bool targetSearched, int liveGuards);
const char* PoiGuideTone(const PoiDef& def, bool targetSearched, int liveGuards);

bool IsPoiCompleted(const std::vector<std::string>& completed, std::string_view id);
bool MarkPoiCompleted(std::vector<std::string>* completed, std::string id);

// 任务完成条件：目标物资已搜索，且该 POI 的守军全部清除。
bool ShouldCompletePoiTask(const PoiDef& def, bool targetSearched, int liveGuards);

}  // namespace zl::game
