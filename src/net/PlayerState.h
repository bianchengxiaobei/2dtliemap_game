#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace zl::net {

// M13-T7：服务器 → 单个客户端的背包/装备/配方/制作状态。
//
// 只发给拥有该玩家的会话，不进入全体可见的 ZLR1 实体快照；客户端用它在网络模式
// 构造只读 UI 镜像，所有修改仍必须通过 ZLC1 命令回服务器执行。
inline constexpr std::uint32_t kPlayerStateMagic = 0x31504C5Au;  // "ZLP1"
inline constexpr std::uint16_t kPlayerStateVersion = 23;
inline constexpr std::size_t kPlayerStateHeaderSize = 24;
inline constexpr std::size_t kMaxPlayerStateBytes = 32 * 1024;
inline constexpr std::size_t kMaxPlayerStateBagItems = 64;
inline constexpr std::size_t kMaxPlayerStateKnownRecipes = 256;
inline constexpr std::size_t kMaxPlayerStateCraftQueue = 16;
inline constexpr std::size_t kMaxPlayerStateStringBytes = 128;
inline constexpr std::size_t kMaxPlayerStateStatusBytes = 512;
inline constexpr std::size_t kMaxPlayerStateSiegeBytes = 512;
inline constexpr std::size_t kMaxPlayerStatePerks = 64;
inline constexpr std::size_t kMaxPlayerStatePoiProgress = 32;
inline constexpr std::size_t kMaxPlayerStatePoiStages = 32;
inline constexpr std::uint8_t kPlayerPoiStageAwaitingChoice = 1u << 0;
inline constexpr std::uint8_t kPlayerPoiStageFailed = 1u << 1;
inline constexpr std::uint8_t kPlayerPoiStagePowered = 1u << 2;
inline constexpr std::size_t kMaxPlayerStateMissions = 64;
inline constexpr std::size_t kMaxPlayerStateMissionObjectives = 16;
inline constexpr std::size_t kMaxPlayerStateSleepStatusBytes = 512;
inline constexpr std::size_t kMaxPlayerStateForageNodes = 32;
inline constexpr std::size_t kMaxPlayerStateFishingSpots = 12;
inline constexpr std::size_t kMaxPlayerStateSettlements = 32;
inline constexpr std::size_t kMaxPlayerStateSettlementReputation = 32;
inline constexpr std::size_t kMaxPlayerStateSettlementNoticeBytes = 512;
inline constexpr std::uint8_t kPlayerSurvivalModeNone = 0;
inline constexpr std::uint8_t kPlayerSurvivalModeForage = 1;
inline constexpr std::uint8_t kPlayerSurvivalModeFishing = 2;
inline constexpr std::uint8_t kPlayerSurvivalPhaseActive = 1;
inline constexpr std::uint8_t kPlayerSurvivalPhaseBiting = 2;
inline constexpr std::uint8_t kPlayerSleepPhaseNone = 0;
inline constexpr std::uint8_t kPlayerSleepPhaseVote = 1;
inline constexpr std::uint8_t kPlayerSleepPhaseSleeping = 2;
inline constexpr std::uint8_t kPlayerCombatStanceNone = 0;
inline constexpr std::uint8_t kPlayerCombatStanceBlocking = 1;
inline constexpr std::uint8_t kPlayerCombatStanceDodge = 2;

struct PlayerStateItem {
    std::string defId;
    int count = 1;
    float condition = 1.0f;
};

struct PlayerStateEquipment {
    bool present = false;
    PlayerStateItem item;
    int loadedAmmo = 0;  // M23-F1-T7：仅远程武器有效；0=空
};

// M23-F1-T7：owner-only 远程武器镜像。弹匣数来自 Equipment，本结构补充
// 背包备弹、换弹进度与服务器发号，供 HUD 只读展示；客户端不推进换弹。
struct PlayerRangedState {
    bool valid = false;
    int loaded = 0;
    int magazine = 0;
    int reserve = 0;
    bool reloading = false;
    float reloadElapsed = 0.0f;
    float reloadDuration = 0.0f;
    int reloadAmount = 0;
    std::uint32_t shotSerial = 0;
};

// M23-F2a-T6：服务器规划的附近觅食节点。ready=false 时 respawnMinutes
// 是服务器给出的剩余刷新时间；客户端只读用于标记和 HUD。
struct PlayerForageNodeState {
    std::string zoneId;
    int x = 0;
    int y = 0;
    int z = 0;
    bool ready = true;
    float respawnMinutes = 0.0f;
    float seconds = 0.0f;
};

struct PlayerFishingSpotState {
    std::string spotId;
    int x = 0;
    int y = 0;
    int z = 0;
    int waterX = 0;
    int waterY = 0;
    int waterZ = 0;
    float seconds = 0.0f;
};

// M23-F2a-T6：owner-only 服务器权威采集/钓鱼动作。mode 0=空闲，
// phase 1=搜索/等待，2=咬钩；progress 只由服务器推进。
struct PlayerSurvivalState {
    std::uint8_t mode = kPlayerSurvivalModeNone;
    std::uint8_t phase = 0;
    std::string name;
    std::string zoneId;
    std::string tableId;
    std::string spotId;
    int x = 0;
    int y = 0;
    int z = 0;
    int waterX = 0;
    int waterY = 0;
    int waterZ = 0;
    float progress = 0.0f;
    float duration = 0.0f;
    float biteRemaining = 0.0f;
};

// M23-F2b-T6：owner-only 服务器权威动物剥取动作。corpseStableId 指向
// 服务器复制实体；客户端只读进度并在本地 HUD 展示。
struct PlayerAnimalHarvestState {
    bool active = false;
    std::uint64_t corpseStableId = 0;
    std::string speciesId;
    std::string name;
    int x = 0;
    int y = 0;
    int z = 0;
    float progress = 0.0f;
    float duration = 0.0f;
};

struct PlayerStateCraftQueue {
    std::string recipeId;
    int count = 1;
};

// M19-F1：技能镜像。xpNext=0 表示 MAX；客户端只读展示，不自行结算。
struct PlayerStateSkill {
    std::string id;
    int level = 0;
    float xp = 0.0f;
    float xpNext = 0.0f;
};

// M21-F1b：服务器权威守夜状态镜像。方向为 8 扇区索引（-1 = 未确定），
// phase 对应 game::BaseSiegePhase；status 是服务器生成的 HUD 文本，
// 客户端只读展示，不本地推进波次。
// M21-F1c：target/breach 为世界格中心坐标，仅用于客户端地图标记；
// 目标/破口有效性由 valid 标志保护，避免旧版本或中止事件残留 0,0 标记。
struct PlayerSiegeState {
    std::uint8_t phase = 0;
    std::int8_t primaryDirection = -1;
    std::int8_t secondaryDirection = -1;
    std::int8_t currentDirection = -1;
    bool breached = false;
    std::uint8_t waveIndex = 0;
    std::uint8_t waveCount = 0;
    std::uint8_t tone = 0;  // 0=info, 1=warn, 2=fail
    bool targetValid = false;
    float targetX = 0.0f;
    float targetY = 0.0f;
    bool breachValid = false;
    float breachX = 0.0f;
    float breachY = 0.0f;
    std::string status;
};

// M21-F2b：服务器权威电力摘要。设备数量/开关/供电标志随 ZLR1 设施复制，
// 这里只带全局燃料/负载/告警，客户端不本地扣燃料或计算电击。
struct PlayerPowerState {
    float fuelMinutes = 0.0f;
    float load = 0.0f;
    bool lowFuel = false;
    bool outage = false;
};

// M23-F4-T6：服务器权威聚落状态镜像。守卫生命/失守/夜袭只由服务器推进，
// 客户端据此拦截交易/对话并显示状态；夜袭实体仍走 ZLR1。
struct PlayerSettlementProgress {
    std::string id;
    bool fallen = false;
    bool raidActive = false;
    float guardHealth = 0.0f;
    float guardHealthMax = 0.0f;
};

struct PlayerSettlementReputation {
    std::string id;
    int score = 0;
};

// M19-F2：服务器权威 POI 进度；客户端只读用于指引/完成态。
struct PlayerPoiProgress {
    std::string id;
    bool completed = false;
    bool targetSearched = false;
    int liveGuards = 0;
};

// M33-F5-T2：服务器权威多阶段 POI 只读镜像。
struct PlayerPoiStageProgress {
    std::string id;
    std::uint8_t stageIndex = 0;
    std::uint8_t stageCount = 0;
    std::uint8_t completedStageCount = 0;
    std::uint8_t flags = 0;
    std::string currentStage;
    std::string chosenBranch;
};

// M19-F3：服务器权威任务日志镜像；客户端只读用于 HUD/焦点切换。
struct PlayerMissionProgress {
    std::string id;
    std::vector<int> progress;
    std::uint8_t status = 0;  // 0=active, 1=completed, 2=failed
    int day = 1;
    bool rewardClaimed = false;
};

// M22-F1：服务器权威需求镜像；联机 HUD 不再显示“状态 --”。
struct PlayerNeedsState {
    bool valid = false;
    float hunger = 0.0f;
    float thirst = 0.0f;
    float fatigue = 0.0f;
    std::uint8_t warning = 0;
};

// M23-F5-T4：服务器权威营养/食物中毒镜像。客户端只读，不本地推进或扣血。
struct PlayerFoodState {
    bool valid = false;
    float nutrition = 0.0f;
    float poisonRemaining = 0.0f;
    float poisonDamagePerSecond = 0.0f;
};

struct PlayerVehicleState {
    bool valid = false;
    std::uint64_t vehicleStableId = 0;
    bool driving = false;
    bool passenger = false;
    float fuel = 0.0f;
    float condition = 0.0f;
    // M26-V12：车型与车身朝向；旧版本客户端按标准皮卡/南向回退。
    std::uint8_t vehicleType = 1;
    float heading = 1.5707963267948966f;
};

// M30-T6：owner-only 服务器权威战斗状态镜像。客户端只读展示，不能上传体力/
// 伤害/冷却；stance 0=None 1=Blocking 2=Dodge。
struct PlayerCombatState {
    bool valid = false;
    float stamina = 0.0f;
    float staminaMax = 0.0f;
    std::uint8_t stance = kPlayerCombatStanceNone;
    float parryWindowLeft = 0.0f;
    float guardBreakLeft = 0.0f;
    float dodgeInvulnLeft = 0.0f;
};

// M22-F1：服务器权威联机睡眠/时间投票镜像；phase 0=无，1=投票，2=睡眠中。
// status 是服务器生成的 HUD 操作提示，客户端只读展示并发 SleepVote/SleepCancel。
// M34-F1-T5：owner-only 服务器权威潜行摘要。客户端只读展示；旧 v22 负载
// 默认站立（noiseRadius=2.5、visibilityScale=1）。
struct PlayerStealthState {
    bool valid = false;
    bool crouched = false;
    float noiseRadius = 2.5f;
    float visibilityScale = 1.0f;
};

struct PlayerSleepState {
    std::uint8_t phase = 0;
    std::uint8_t tone = 0;
    bool voted = false;
    std::uint8_t yesCount = 0;
    std::uint8_t eligibleCount = 0;
    std::uint64_t initiator = 0;
    float startMinutes = 0.0f;
    float targetMinutes = 0.0f;
    float voteSeconds = 0.0f;
    float progress = 0.0f;
    std::string status;
};

// M14/M13：服务器权威主目标步骤镜像（0=找零件，1=修电台，2=撤离，3=完成）。
struct PlayerObjectiveState {
    bool valid = false;
    std::uint8_t step = 0;
};

struct PlayerState {
    std::uint64_t playerStableId = 0;
    std::uint64_t serverTick = 0;
    float capacityKg = 20.0f;

    // M13-T8：生命/死亡/复活。alive=false 时 health 必须为 0，
    // respawnSeconds 为服务器计算的剩余复活秒数（仅供 UI，不信任客户端）。
    float health = 100.0f;
    float healthMax = 100.0f;
    bool alive = true;
    float respawnSeconds = 0.0f;

    std::vector<PlayerStateItem> bag;
    std::array<PlayerStateEquipment, 2> equipment{};
    PlayerRangedState ranged;
    std::vector<PlayerForageNodeState> forageNodes;
    std::vector<PlayerFishingSpotState> fishingSpots;
    PlayerSurvivalState survival;
    PlayerAnimalHarvestState animalHarvest;
    std::vector<std::string> knownRecipes;

    bool craftingActive = false;
    std::string craftingRecipeId;
    float craftingElapsed = 0.0f;
    float craftingDuration = 0.0f;
    float craftingToolEfficiency = 1.0f;
    int batchTotal = 0;
    int batchCompleted = 0;
    std::vector<PlayerStateCraftQueue> craftQueue;

    // M19-F1：服务器权威成长快照 + 当前 Perk 选项（只读镜像）。
    int progressionLevel = 1;
    float progressionXp = 0.0f;
    float progressionXpNext = 0.0f;
    int perkPoints = 0;
    std::array<PlayerStateSkill, 4> skills{};
    std::vector<std::string> perks;
    std::vector<std::string> perkChoices;

    // M20-F1：服务器权威感染镜像（只读）。stage: 0=无/1=潜伏/2=发热/3=重症。
    std::uint8_t infectionStage = 0;
    float infectionProgress = 0.0f;
    float infectionTimeToNextStage = 0.0f;
    float infectionSlowStrength = 0.0f;
    float infectionSlowRemaining = 0.0f;
    float infectionSlowDuration = 0.0f;

    // M20-F2：服务器权威体温/潮湿镜像；旧 v4 缺失时回退 37°C/0%。
    float climateBodyC = 37.0f;
    float climateWetness = 0.0f;

    // M21-F1b：服务器权威守夜状态；旧 v5 缺失时保持 Idle/空 HUD。
    PlayerSiegeState siege;

    // M21-F2b：服务器权威电力摘要；旧 v6 缺失时保持 0/无告警。
    PlayerPowerState power;

    // M19-F2/F3：服务器权威 POI/任务日志；旧 v8 缺失时保持空。
    std::vector<PlayerPoiProgress> poiProgress;
    // M33-F5-T2：旧 v21 缺失时保持空。
    std::vector<PlayerPoiStageProgress> poiStageProgress;
    std::vector<PlayerMissionProgress> missions;
    int missionLastDay = 1;
    int missionRandomDay = 0;

    // M22-F1：旧 v9 缺失时保持 invalid/默认值。
    PlayerNeedsState needs;
    // M23-F5-T4：旧 v14 缺失时保持 invalid/默认值。
    PlayerFoodState food;
    // M25-T3：服务器权威驾驶态/车辆 ID/燃料/车况（owner-only）。
    PlayerVehicleState vehicle;
    // M30-T6：服务器权威战斗体力/姿态（owner-only）。
    PlayerCombatState combat;
    // M34-F1-T5：服务器权威潜行摘要（owner-only）。
    PlayerStealthState stealth;
    // M23-F4-T6：聚落权威状态与服务器提示（owner-only）。
    std::vector<PlayerSettlementProgress> settlements;
    std::vector<PlayerSettlementReputation> settlementReputation;
    std::uint32_t settlementNoticeSerial = 0;
    std::uint8_t settlementNoticeTone = 0;
    std::string settlementNotice;
    PlayerSleepState sleep;

    // M14/M13：旧 v10 缺失时保持 invalid，不影响单机/旧客户端。
    PlayerObjectiveState objective;

    std::uint32_t statusSequence = 0;
    std::uint8_t statusTone = 0;  // 0=info, 1=ok, 2=fail
    std::string status;
};

std::vector<std::uint8_t> EncodePlayerState(const PlayerState& state,
                                            std::string* error = nullptr);
bool DecodePlayerState(std::span<const std::uint8_t> bytes, PlayerState* state,
                       std::string* error = nullptr);

}  // namespace zl::net
