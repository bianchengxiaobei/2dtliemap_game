#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zl::world {
struct WorldDefinition;
}

namespace zl::game {

enum class StrategicOwner : std::uint8_t {
    Neutral = 0,
    Human = 1,
    Contested = 2,
    Infested = 3,
};

// M32-T11：争夺区域背后的敌方派系。None 仅用于非争夺占位；
// 当前 v1 只区分感染者与人类掠夺者，不改变 owner 枚举。
enum class ContestedFaction : std::uint8_t {
    None = 0,
    Infected = 1,
    Raiders = 2,
};

enum class StrategicSiegePhase : std::uint8_t {
    None = 0,
    Warning = 1,
    Active = 2,
    Resolving = 3,
    Recovering = 4,
};

enum class StrategicEventKind : std::uint8_t {
    Owner = 0,
    Resource = 1,
    Warning = 2,
    Siege = 3,
    Recovery = 4,
    Refugee = 5,
};

// M32-T5：失守后的反攻阶段。ClearCore/Repair/Counterattack 都在 Infested
// 区域进行；Restoring 表示反攻守住后的 Contested 恢复窗口。
enum class StrategicRecoveryStage : std::uint8_t {
    None = 0,
    ClearCore = 1,
    Repair = 2,
    Counterattack = 3,
    Restoring = 4,
};

const char* ContestedFactionName(ContestedFaction faction);
const char* ContestedFactionLabel(ContestedFaction faction);
bool ParseContestedFaction(std::string_view value, ContestedFaction* out);
const char* StrategicOwnerName(StrategicOwner owner);
const char* StrategicOwnerLabel(StrategicOwner owner);
bool ParseStrategicOwner(std::string_view value, StrategicOwner* out);
const char* StrategicSiegePhaseName(StrategicSiegePhase phase);
const char* StrategicSiegePhaseLabel(StrategicSiegePhase phase);
const char* StrategicEventKindName(StrategicEventKind kind);
const char* StrategicRecoveryStageName(StrategicRecoveryStage stage);
const char* StrategicRecoveryStageLabel(StrategicRecoveryStage stage);
const char* StrategicIntelName(int intelLevel);
int StrategicIntelErrorPercent(int intelLevel);
int StrategicIntelTimeErrorMinutes(int intelLevel);

struct StrategicResources {
    int people = 0;
    int supplies = 0;
    int ammo = 0;
    int fuel = 0;
};

struct StrategicRegionConfig {
    std::string id;
    // M32-T4：详细围攻锚点。村庄用聚落中心，农场/诊所/检查站用 POI；
    // 缺省时地图/围攻回退到 world region 矩形中心。
    bool hasAnchor = false;
    float anchorX = 0.0f;
    float anchorY = 0.0f;
    int strategicValue = 1;
    StrategicOwner initialOwner = StrategicOwner::Neutral;
    ContestedFaction contestedFaction = ContestedFaction::Infected;
    int garrison = 0;
    int fortification = 0;
    int defense = 0;
    int supply = 0;
    int population = 0;
    int pressure = 0;
    int morale = 100;
    int intelLevel = 0;
    bool coreSafe = false;
    bool remote = false;
    std::string refugeeTarget;
    std::vector<std::string> attackRoutes;
};

struct StrategicAutoResolveConfig {
    float defensePerGarrison = 8.0f;
    float defensePerFortification = 2.0f;
    float defensePerSupply = 0.25f;
    float enemyStrengthPerPressure = 0.7f;
    float heldPressureRelief = 35.0f;
    float lostPressureRelief = 15.0f;
    int heldPeople = 0;
    int heldSupplies = -8;
    int heldAmmo = -10;
    int heldFuel = 0;
    int lostPeople = -1;
    int lostSupplies = -15;
    int lostAmmo = -10;
    int lostFuel = 0;
    // M32-T6：结算后的伤亡/损失按战况严重度推导。守住时工事和守军仍有
    // 损耗；失守时由 BeginRecovery 撤离/清空，核心区争夺只损失一部分。
    int heldGarrisonLossMax = 2;
    int heldFortificationLoss = 1;
    int heldFacilityDamageMax = 40;
    int coreFacilityDamage = 50;
    int coreGarrisonLossPercent = 50;
    int heldRegionSupplyLoss = 5;
    int lostRegionSupplyLoss = 100;
    int coreRegionSupplyLoss = 50;
};

struct StrategicMissionRewardConfig {
    std::string missionId;
    // 空表示全局资源；指定区域时同时调整该区域补给/守军/人口。
    std::string regionId;
    StrategicResources resources;
    int regionSupply = 0;
    int regionGarrison = 0;
    int regionPopulation = 0;
    int relieveRefugees = 0;
    int pressureRelief = 0;
};

struct StrategicEconomyConfig {
    bool enabled = true;
    int intervalMinutes = 1440;
    int suppliesPerStrategicValue = 2;
    int supplyCostPerPopulation = 1;
    int supplyCostPerGarrison = 1;
    int supplyCostPerRefugee = 1;
    int regionSupplyRestorePerDay = 5;
    int tradeAmmoPerDay = 1;
    int tradeSuppliesPerAmmo = 4;
    int tradeFuelPerDay = 0;
    int tradeSuppliesPerFuel = 8;
    int tradeReserveSupplies = 20;
    int fortificationRepairPerDay = 1;
    float shortagePressurePerSupply = 1.0f;
    float shortageMoralePerSupply = 1.0f;
    std::vector<StrategicMissionRewardConfig> missionRewards;

    const StrategicMissionRewardConfig* FindMission(
        std::string_view missionId) const;
};

struct StrategicRecoveryConfig {
    int delayMinutes = 1440;
    int coreCount = 1;
    int coreAmmoCost = 0;
    int repairSupplies = 12;
    int repairAmmo = 0;
    int restoreMinutes = 1440;
    int retryMinutes = 120;
    int refugeePressurePerPerson = 5;
    int refugeeSupplyPerPerson = 2;
    int reclaimGarrison = 1;
    int reclaimPopulation = 2;
    int reclaimPeople = 0;
    int reclaimSupplies = 10;
    int reclaimAmmo = 5;
    int reclaimFuel = 0;
    std::string refugeeMission = "refugee_relief";
};

struct StrategicContributionConfig {
    int rewardXp = 120;
    int donatePerSupply = 1;
    int clearCore = 20;
    int repairFacility = 15;
    int reclaim = 30;
    int mission = 25;
    int defenseRewardXp = 80;
    int defenseDamagePerPoint = 10;
    // 守卫击杀助攻：按对目标的玩家伤害比例分配，仅给实际伤害者。
    int defenseKillAssistPoints = 5;
    int minContribution = 1;
};

struct StrategicConfig {
    bool enabled = true;
    std::string format = "zomboidlike-strategic-v1";
    std::uint64_t seed = 20261110u;
    int tickMinutes = 30;
    int warningHours = 48;
    int remoteWarningHours = 84;
    int autoResolveGraceMinutes = 2880;
    float pressurePerHour = 0.25f;
    float attackPressure = 70.0f;
    StrategicAutoResolveConfig autoResolve;
    StrategicRecoveryConfig recovery;
    StrategicEconomyConfig economy;
    StrategicContributionConfig contribution;
    StrategicResources initialResources;
    std::vector<StrategicRegionConfig> regions;

    const StrategicRegionConfig* Find(std::string_view id) const;
};

struct StrategicRegionState {
    std::string id;
    StrategicOwner owner = StrategicOwner::Neutral;
    StrategicSiegePhase siegePhase = StrategicSiegePhase::None;
    float pressure = 0.0f;
    float defense = 0.0f;
    float supply = 0.0f;
    float morale = 100.0f;
    int population = 0;
    int intelLevel = 0;
    int garrison = 0;
    int fortification = 0;
    bool coreSafe = false;
    int facilityDamage = 0;
    int refugees = 0;
    int infectionCores = 0;
    StrategicRecoveryStage recoveryStage = StrategicRecoveryStage::None;
    std::int64_t recoveryReadyMinute = -1;
    std::string refugeeHost;
    std::int64_t nextAttackMinute = -1;
    std::int64_t lastEventMinute = -1;
};

struct StrategicEvent {
    std::string id;
    std::string regionId;
    StrategicEventKind kind = StrategicEventKind::Owner;
    std::int64_t gameMinute = 0;
    StrategicOwner ownerAfter = StrategicOwner::Neutral;
    int people = 0;
    int supplies = 0;
    int ammo = 0;
    int fuel = 0;
};

std::unordered_map<std::uint64_t, int> AllocateSiegeKillAssist(
    const std::unordered_map<std::uint64_t, float>& targetDamage,
    int totalPoints);

struct StrategicContributionRecord {
    std::uint64_t stableId = 0;
    std::string regionId;
    int points = 0;
};

struct StrategicState {
    std::vector<StrategicRegionState> regions;
    std::vector<StrategicEvent> events;
    std::vector<StrategicContributionRecord> contributions;
    StrategicResources resources;
    std::int64_t lastTickMinute = 0;

    bool Initialize(const StrategicConfig& config, std::string* error = nullptr);

    StrategicRegionState* FindRegion(std::string_view id);
    const StrategicRegionState* FindRegion(std::string_view id) const;
    const StrategicEvent* FindEvent(std::string_view id) const;

    bool ApplyEvent(std::string_view regionId, StrategicEventKind kind,
                    std::string eventId, std::int64_t gameMinute,
                    StrategicOwner ownerAfter,
                    const StrategicResources& delta = {},
                    std::string* error = nullptr);
    bool ApplyOwnerChange(std::string_view regionId, StrategicOwner owner,
                          std::string eventId, std::int64_t gameMinute,
                          const StrategicResources& delta = {},
                          std::string* error = nullptr);
    bool ApplyResourceDelta(std::string_view regionId,
                            const StrategicResources& delta,
                            std::string eventId, std::int64_t gameMinute,
                            std::string* error = nullptr);
    bool AdvanceTick(std::int64_t gameMinute, std::string* error = nullptr);

    bool Validate(std::string* error = nullptr) const;
};

bool LoadStrategicConfig(const std::string& path, StrategicConfig* out,
                         std::string* error = nullptr);
bool ParseStrategicConfig(std::string_view json, const std::string& source,
                          StrategicConfig* out, std::string* error = nullptr);
bool ValidateStrategicAgainstWorld(const StrategicConfig& config,
                                   const world::WorldDefinition& world,
                                   std::string* error = nullptr);

}  // namespace zl::game
