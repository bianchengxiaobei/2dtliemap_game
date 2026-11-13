#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/StrategicState.h"

namespace zl::game {

struct StrategicTickInput {
    std::int64_t gameMinute = 0;
    bool anyPlayerOnline = true;
    bool detailedEnabled = false;
    std::vector<std::string> playerRegions;
};

struct StrategicWarningInfo {
    std::string regionId;
    std::int64_t warningMinute = 0;
    std::int64_t attackMinute = 0;
    int warningMinutes = 0;
    int remainingMinutes = 0;
    int intelLevel = 0;
    int enemyStrength = 0;
    int reportedEnemyStrength = 0;
    std::int64_t reportedAttackMinute = 0;
    int defenseScore = 0;
    int defenseFromGarrison = 0;
    int defenseFromFortification = 0;
    int defenseFromSupply = 0;
};

struct StrategicResolutionInfo {
    std::string regionId;
    std::int64_t attackMinute = 0;
    std::int64_t resolvedMinute = 0;
    int defenseScore = 0;
    int defenseFromGarrison = 0;
    int defenseFromFortification = 0;
    int defenseFromSupply = 0;
    int enemyStrength = 0;
    bool held = false;
    bool coreProtected = false;
    StrategicOwner ownerAfter = StrategicOwner::Neutral;
    StrategicResources resourceDelta;
    int garrisonBefore = 0;
    int garrisonAfter = 0;
    int populationBefore = 0;
    int populationAfter = 0;
    int fortificationBefore = 0;
    int fortificationAfter = 0;
    int facilityDamageBefore = 0;
    int facilityDamageAfter = 0;
    int regionSupplyBefore = 0;
    int regionSupplyAfter = 0;
};

struct StrategicEconomyInfo {
    int day = 0;
    int days = 1;
    int producedSupplies = 0;
    int consumedSupplies = 0;
    int shortageSupplies = 0;
    int restoredRegionSupply = 0;
    int repairedFortification = 0;
    int tradedAmmo = 0;
    int tradedFuel = 0;
};

struct StrategicReclaimResolution {
    std::string regionId;
    std::int64_t attackMinute = 0;
    std::int64_t resolvedMinute = 0;
    bool held = false;
    bool breached = false;
    char rating = '-';
};

struct StrategicTickReport {
    std::vector<StrategicWarningInfo> warnings;
    std::vector<StrategicResolutionInfo> resolutions;
    std::vector<std::string> detailedRegions;
    std::vector<std::string> recoveryRegions;
    std::vector<std::string> completedRecoveries;
    std::vector<StrategicEconomyInfo> economy;
};

bool AdvanceStrategic(StrategicState& state, const StrategicConfig& config,
                      const StrategicTickInput& input,
                      StrategicTickReport* report = nullptr,
                      std::string* error = nullptr);

struct StrategicDetailedResolution {
    std::string regionId;
    std::int64_t attackMinute = 0;
    std::int64_t resolvedMinute = 0;
    bool held = false;
    bool breached = false;
    char rating = '-';
};

// M32-T4：详细围攻结果回写。与自动结算使用同一个 siege.<region>.<minute>
// 事件 id，重复调用只清阶段、不重复扣资源。
bool ResolveStrategicSiege(StrategicState& state, const StrategicConfig& config,
                           const StrategicDetailedResolution& resolution,
                           std::string* error = nullptr);

// M32-T5：失守后的三步反攻。清理核心/修复设施由玩家在锚点交互触发，
// 反攻波次由 RegionalSiege 的 reclaim 模式执行，结果只回写一次。
bool ClearStrategicRecoveryCore(StrategicState& state,
                                const StrategicConfig& config,
                                std::string_view regionId,
                                std::int64_t gameMinute,
                                std::string* error = nullptr);
bool RepairStrategicRecoveryFacility(StrategicState& state,
                                     const StrategicConfig& config,
                                     std::string_view regionId,
                                     std::int64_t gameMinute,
                                     std::string* error = nullptr);
bool ResolveStrategicReclaim(StrategicState& state,
                             const StrategicConfig& config,
                             const StrategicReclaimResolution& resolution,
                             std::string* error = nullptr);

// M32-T6：护送/补给等任务完成时把玩家准备转成战略资源或区域防御。
// 以 mission.<id> 为幂等事件，重复调用不会重复发放。
bool ApplyStrategicMissionReward(StrategicState& state,
                                 const StrategicConfig& config,
                                 std::string_view missionId,
                                 std::int64_t gameMinute,
                                 std::string* error = nullptr);

struct StrategicWorldEventResult {
    bool applied = false;
    bool alreadyApplied = false;
    float pressureBefore = 0.0f;
    float pressureAfter = 0.0f;
};

bool ApplyStrategicWorldEventEffect(
    StrategicState& state, const StrategicConfig& config,
    std::string_view regionId, int pressureDelta,
    std::string_view worldEventId, std::int64_t gameMinute,
    StrategicWorldEventResult* out = nullptr, std::string* error = nullptr);

// Reconstructs the current warning view from persisted state. This does not
// mutate the strategic state and uses the same deterministic intel roll as
// AdvanceStrategic, so map/HUD estimates match the eventual auto resolution.
bool DescribeStrategicWarning(const StrategicRegionState& region,
                              const StrategicConfig& config,
                              std::int64_t gameMinute,
                              StrategicWarningInfo* out);

}  // namespace zl::game
