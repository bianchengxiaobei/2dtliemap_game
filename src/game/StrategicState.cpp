#include "game/StrategicState.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Constants.h"
#include "world/WorldDefinition.h"

namespace zl::game {
namespace {

constexpr std::size_t kMaxRegionIdBytes = 64;
constexpr std::size_t kMaxEventIdBytes = 128;
constexpr std::size_t kMaxRegions = 64;
constexpr std::size_t kMaxEvents = 256;
constexpr int kMaxStrategicValue = 1000000;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool Fail(std::string* error, std::string message) {
    SetError(error, std::move(message));
    return false;
}

bool ValidRegionId(std::string_view id) {
    if (id.empty() || id.size() > kMaxRegionIdBytes) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '.';
    });
}

bool ReadInt(const nlohmann::json& node, const char* key, int lo, int hi,
             int* out) {
    if (!node.contains(key) || !node[key].is_number_integer()) {
        return false;
    }
    const int value = node[key].get<int>();
    if (value < lo || value > hi) {
        return false;
    }
    *out = value;
    return true;
}

bool ReadBool(const nlohmann::json& node, const char* key, bool* out) {
    if (!node.contains(key) || !node[key].is_boolean()) {
        return false;
    }
    *out = node[key].get<bool>();
    return true;
}

bool ReadFloat(const nlohmann::json& node, const char* key, float lo, float hi,
               float* out) {
    if (!node.contains(key) ||
        (!node[key].is_number_integer() && !node[key].is_number_float())) {
        return false;
    }
    const float value = node[key].get<float>();
    if (!std::isfinite(value) || value < lo || value > hi) {
        return false;
    }
    *out = value;
    return true;
}

bool ReadSeed(const nlohmann::json& node, const char* key,
              std::uint64_t* out) {
    if (!node.contains(key) ||
        (!node[key].is_number_integer() && !node[key].is_number_unsigned())) {
        return false;
    }
    try {
        const std::int64_t value = node[key].get<std::int64_t>();
        if (value < 0) {
            return false;
        }
        *out = static_cast<std::uint64_t>(value);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool ReadString(const nlohmann::json& node, const char* key,
                std::size_t maxBytes, std::string* out) {
    if (!node.contains(key) || !node[key].is_string()) {
        return false;
    }
    std::string value = node[key].get<std::string>();
    if (value.empty() || value.size() > maxBytes) {
        return false;
    }
    *out = std::move(value);
    return true;
}

bool ReadResources(const nlohmann::json& node, StrategicResources* out,
                   std::string* error) {
    if (!node.is_object()) {
        return Fail(error, "initialResources 必须是对象");
    }
    StrategicResources resources;
    if (node.contains("people") &&
        !ReadInt(node, "people", 0, kMaxStrategicValue, &resources.people)) {
        return Fail(error, "initialResources.people 非法");
    }
    if (node.contains("supplies") &&
        !ReadInt(node, "supplies", 0, kMaxStrategicValue,
                 &resources.supplies)) {
        return Fail(error, "initialResources.supplies 非法");
    }
    if (node.contains("ammo") &&
        !ReadInt(node, "ammo", 0, kMaxStrategicValue, &resources.ammo)) {
        return Fail(error, "initialResources.ammo 非法");
    }
    if (node.contains("fuel") &&
        !ReadInt(node, "fuel", 0, kMaxStrategicValue, &resources.fuel)) {
        return Fail(error, "initialResources.fuel 非法");
    }
    *out = resources;
    return true;
}

bool ParseAutoResolve(const nlohmann::json& node,
                      StrategicAutoResolveConfig* out, std::string* error) {
    if (!node.is_object()) {
        return Fail(error, "autoResolve 必须是对象");
    }
    StrategicAutoResolveConfig config;
    if (node.contains("defensePerGarrison") &&
        !ReadFloat(node, "defensePerGarrison", 0.0f, 1000.0f,
                   &config.defensePerGarrison)) {
        return Fail(error, "autoResolve.defensePerGarrison 非法");
    }
    if (node.contains("defensePerFortification") &&
        !ReadFloat(node, "defensePerFortification", 0.0f, 1000.0f,
                   &config.defensePerFortification)) {
        return Fail(error, "autoResolve.defensePerFortification 非法");
    }
    if (node.contains("defensePerSupply") &&
        !ReadFloat(node, "defensePerSupply", 0.0f, 10.0f,
                   &config.defensePerSupply)) {
        return Fail(error, "autoResolve.defensePerSupply 非法");
    }
    if (node.contains("enemyStrengthPerPressure") &&
        !ReadFloat(node, "enemyStrengthPerPressure", 0.0f, 10.0f,
                   &config.enemyStrengthPerPressure)) {
        return Fail(error, "autoResolve.enemyStrengthPerPressure 非法");
    }
    if (node.contains("heldPressureRelief") &&
        !ReadFloat(node, "heldPressureRelief", 0.0f, 100.0f,
                   &config.heldPressureRelief)) {
        return Fail(error, "autoResolve.heldPressureRelief 非法");
    }
    if (node.contains("lostPressureRelief") &&
        !ReadFloat(node, "lostPressureRelief", 0.0f, 100.0f,
                   &config.lostPressureRelief)) {
        return Fail(error, "autoResolve.lostPressureRelief 非法");
    }
    if (node.contains("heldPeople") &&
        !ReadInt(node, "heldPeople", -kMaxStrategicValue, kMaxStrategicValue,
                 &config.heldPeople)) {
        return Fail(error, "autoResolve.heldPeople 非法");
    }
    if (node.contains("heldSupplies") &&
        !ReadInt(node, "heldSupplies", -kMaxStrategicValue, kMaxStrategicValue,
                 &config.heldSupplies)) {
        return Fail(error, "autoResolve.heldSupplies 非法");
    }
    if (node.contains("heldAmmo") &&
        !ReadInt(node, "heldAmmo", -kMaxStrategicValue, kMaxStrategicValue,
                 &config.heldAmmo)) {
        return Fail(error, "autoResolve.heldAmmo 非法");
    }
    if (node.contains("heldFuel") &&
        !ReadInt(node, "heldFuel", -kMaxStrategicValue, kMaxStrategicValue,
                 &config.heldFuel)) {
        return Fail(error, "autoResolve.heldFuel 非法");
    }
    if (node.contains("lostPeople") &&
        !ReadInt(node, "lostPeople", -kMaxStrategicValue, kMaxStrategicValue,
                 &config.lostPeople)) {
        return Fail(error, "autoResolve.lostPeople 非法");
    }
    if (node.contains("lostSupplies") &&
        !ReadInt(node, "lostSupplies", -kMaxStrategicValue, kMaxStrategicValue,
                 &config.lostSupplies)) {
        return Fail(error, "autoResolve.lostSupplies 非法");
    }
    if (node.contains("lostAmmo") &&
        !ReadInt(node, "lostAmmo", -kMaxStrategicValue, kMaxStrategicValue,
                 &config.lostAmmo)) {
        return Fail(error, "autoResolve.lostAmmo 非法");
    }
    if (node.contains("lostFuel") &&
        !ReadInt(node, "lostFuel", -kMaxStrategicValue, kMaxStrategicValue,
                 &config.lostFuel)) {
        return Fail(error, "autoResolve.lostFuel 非法");
    }
    if (node.contains("heldGarrisonLossMax") &&
        !ReadInt(node, "heldGarrisonLossMax", 0, 10000,
                 &config.heldGarrisonLossMax)) {
        return Fail(error, "autoResolve.heldGarrisonLossMax 非法");
    }
    if (node.contains("heldFortificationLoss") &&
        !ReadInt(node, "heldFortificationLoss", 0, 100,
                 &config.heldFortificationLoss)) {
        return Fail(error, "autoResolve.heldFortificationLoss 非法");
    }
    if (node.contains("heldFacilityDamageMax") &&
        !ReadInt(node, "heldFacilityDamageMax", 0, 100,
                 &config.heldFacilityDamageMax)) {
        return Fail(error, "autoResolve.heldFacilityDamageMax 非法");
    }
    if (node.contains("coreFacilityDamage") &&
        !ReadInt(node, "coreFacilityDamage", 0, 100,
                 &config.coreFacilityDamage)) {
        return Fail(error, "autoResolve.coreFacilityDamage 非法");
    }
    if (node.contains("coreGarrisonLossPercent") &&
        !ReadInt(node, "coreGarrisonLossPercent", 0, 100,
                 &config.coreGarrisonLossPercent)) {
        return Fail(error, "autoResolve.coreGarrisonLossPercent 非法");
    }
    if (node.contains("heldRegionSupplyLoss") &&
        !ReadInt(node, "heldRegionSupplyLoss", 0, 100,
                 &config.heldRegionSupplyLoss)) {
        return Fail(error, "autoResolve.heldRegionSupplyLoss 非法");
    }
    if (node.contains("lostRegionSupplyLoss") &&
        !ReadInt(node, "lostRegionSupplyLoss", 0, 100,
                 &config.lostRegionSupplyLoss)) {
        return Fail(error, "autoResolve.lostRegionSupplyLoss 非法");
    }
    if (node.contains("coreRegionSupplyLoss") &&
        !ReadInt(node, "coreRegionSupplyLoss", 0, 100,
                 &config.coreRegionSupplyLoss)) {
        return Fail(error, "autoResolve.coreRegionSupplyLoss 非法");
    }
    *out = config;
    return true;
}

bool ParseRecovery(const nlohmann::json& node,
                   StrategicRecoveryConfig* out, std::string* error) {
    if (!node.is_object()) {
        return Fail(error, "recovery 必须是对象");
    }
    StrategicRecoveryConfig config;
    if (node.contains("delayMinutes") &&
        !ReadInt(node, "delayMinutes", 0, 1000000, &config.delayMinutes)) {
        return Fail(error, "recovery.delayMinutes 非法");
    }
    if (node.contains("coreCount") &&
        !ReadInt(node, "coreCount", 0, 16, &config.coreCount)) {
        return Fail(error, "recovery.coreCount 非法");
    }
    if (node.contains("coreAmmoCost") &&
        !ReadInt(node, "coreAmmoCost", 0, 1000000, &config.coreAmmoCost)) {
        return Fail(error, "recovery.coreAmmoCost 非法");
    }
    if (node.contains("repairSupplies") &&
        !ReadInt(node, "repairSupplies", 0, 1000000,
                 &config.repairSupplies)) {
        return Fail(error, "recovery.repairSupplies 非法");
    }
    if (node.contains("repairAmmo") &&
        !ReadInt(node, "repairAmmo", 0, 1000000, &config.repairAmmo)) {
        return Fail(error, "recovery.repairAmmo 非法");
    }
    if (node.contains("restoreMinutes") &&
        !ReadInt(node, "restoreMinutes", 0, 1000000,
                 &config.restoreMinutes)) {
        return Fail(error, "recovery.restoreMinutes 非法");
    }
    if (node.contains("retryMinutes") &&
        !ReadInt(node, "retryMinutes", 0, 1000000, &config.retryMinutes)) {
        return Fail(error, "recovery.retryMinutes 非法");
    }
    if (node.contains("refugeePressurePerPerson") &&
        !ReadInt(node, "refugeePressurePerPerson", 0, 100,
                 &config.refugeePressurePerPerson)) {
        return Fail(error, "recovery.refugeePressurePerPerson 非法");
    }
    if (node.contains("refugeeSupplyPerPerson") &&
        !ReadInt(node, "refugeeSupplyPerPerson", 0, 100,
                 &config.refugeeSupplyPerPerson)) {
        return Fail(error, "recovery.refugeeSupplyPerPerson 非法");
    }
    if (node.contains("reclaimGarrison") &&
        !ReadInt(node, "reclaimGarrison", 0, 10000,
                 &config.reclaimGarrison)) {
        return Fail(error, "recovery.reclaimGarrison 非法");
    }
    if (node.contains("reclaimPopulation") &&
        !ReadInt(node, "reclaimPopulation", 0, 10000,
                 &config.reclaimPopulation)) {
        return Fail(error, "recovery.reclaimPopulation 非法");
    }
    if (node.contains("reclaimPeople") &&
        !ReadInt(node, "reclaimPeople", -kMaxStrategicValue,
                 kMaxStrategicValue, &config.reclaimPeople)) {
        return Fail(error, "recovery.reclaimPeople 非法");
    }
    if (node.contains("reclaimSupplies") &&
        !ReadInt(node, "reclaimSupplies", -kMaxStrategicValue,
                 kMaxStrategicValue, &config.reclaimSupplies)) {
        return Fail(error, "recovery.reclaimSupplies 非法");
    }
    if (node.contains("reclaimAmmo") &&
        !ReadInt(node, "reclaimAmmo", -kMaxStrategicValue,
                 kMaxStrategicValue, &config.reclaimAmmo)) {
        return Fail(error, "recovery.reclaimAmmo 非法");
    }
    if (node.contains("reclaimFuel") &&
        !ReadInt(node, "reclaimFuel", -kMaxStrategicValue,
                 kMaxStrategicValue, &config.reclaimFuel)) {
        return Fail(error, "recovery.reclaimFuel 非法");
    }
    if (node.contains("refugeeMission")) {
        if (!ReadString(node, "refugeeMission", kMaxRegionIdBytes,
                        &config.refugeeMission) ||
            !ValidRegionId(config.refugeeMission)) {
            return Fail(error, "recovery.refugeeMission 非法");
        }
    }
    *out = config;
    return true;
}

bool ParseMissionReward(const nlohmann::json& entry,
                        StrategicMissionRewardConfig* out,
                        std::string* error) {
    if (!entry.is_object()) {
        return Fail(error, "economy.missionRewards 条目必须是对象");
    }
    StrategicMissionRewardConfig reward;
    if (!ReadString(entry, "mission", 64, &reward.missionId)) {
        return Fail(error, "economy.missionRewards.mission 非法");
    }
    if (entry.contains("region")) {
        if (!ReadString(entry, "region", kMaxRegionIdBytes, &reward.regionId) ||
            !ValidRegionId(reward.regionId)) {
            return Fail(error, "economy.missionRewards.region 非法");
        }
    }
    if (entry.contains("people") &&
        !ReadInt(entry, "people", 0, kMaxStrategicValue,
                 &reward.resources.people)) {
        return Fail(error, "economy.missionRewards.people 非法");
    }
    if (entry.contains("supplies") &&
        !ReadInt(entry, "supplies", 0, kMaxStrategicValue,
                 &reward.resources.supplies)) {
        return Fail(error, "economy.missionRewards.supplies 非法");
    }
    if (entry.contains("ammo") &&
        !ReadInt(entry, "ammo", 0, kMaxStrategicValue,
                 &reward.resources.ammo)) {
        return Fail(error, "economy.missionRewards.ammo 非法");
    }
    if (entry.contains("fuel") &&
        !ReadInt(entry, "fuel", 0, kMaxStrategicValue,
                 &reward.resources.fuel)) {
        return Fail(error, "economy.missionRewards.fuel 非法");
    }
    if (entry.contains("regionSupply") &&
        !ReadInt(entry, "regionSupply", 0, 100, &reward.regionSupply)) {
        return Fail(error, "economy.missionRewards.regionSupply 非法");
    }
    if (entry.contains("regionGarrison") &&
        !ReadInt(entry, "regionGarrison", 0, 10000,
                 &reward.regionGarrison)) {
        return Fail(error, "economy.missionRewards.regionGarrison 非法");
    }
    if (entry.contains("regionPopulation") &&
        !ReadInt(entry, "regionPopulation", 0, 10000,
                 &reward.regionPopulation)) {
        return Fail(error, "economy.missionRewards.regionPopulation 非法");
    }
    if (entry.contains("relieveRefugees") &&
        !ReadInt(entry, "relieveRefugees", 0, 10000,
                 &reward.relieveRefugees)) {
        return Fail(error, "economy.missionRewards.relieveRefugees 非法");
    }
    if (entry.contains("pressureRelief") &&
        !ReadInt(entry, "pressureRelief", 0, 100,
                 &reward.pressureRelief)) {
        return Fail(error, "economy.missionRewards.pressureRelief 非法");
    }
    *out = std::move(reward);
    return true;
}

bool ParseEconomy(const nlohmann::json& node, StrategicEconomyConfig* out,
                  std::string* error) {
    if (!node.is_object()) {
        return Fail(error, "economy 必须是对象");
    }
    StrategicEconomyConfig config;
    if (node.contains("enabled") && !ReadBool(node, "enabled", &config.enabled)) {
        return Fail(error, "economy.enabled 必须是布尔值");
    }
    if (node.contains("intervalMinutes") &&
        !ReadInt(node, "intervalMinutes", 1, 10080,
                 &config.intervalMinutes)) {
        return Fail(error, "economy.intervalMinutes 非法");
    }
    if (node.contains("suppliesPerStrategicValue") &&
        !ReadInt(node, "suppliesPerStrategicValue", 0, 100,
                 &config.suppliesPerStrategicValue)) {
        return Fail(error, "economy.suppliesPerStrategicValue 非法");
    }
    if (node.contains("supplyCostPerPopulation") &&
        !ReadInt(node, "supplyCostPerPopulation", 0, 100,
                 &config.supplyCostPerPopulation)) {
        return Fail(error, "economy.supplyCostPerPopulation 非法");
    }
    if (node.contains("supplyCostPerGarrison") &&
        !ReadInt(node, "supplyCostPerGarrison", 0, 100,
                 &config.supplyCostPerGarrison)) {
        return Fail(error, "economy.supplyCostPerGarrison 非法");
    }
    if (node.contains("supplyCostPerRefugee") &&
        !ReadInt(node, "supplyCostPerRefugee", 0, 100,
                 &config.supplyCostPerRefugee)) {
        return Fail(error, "economy.supplyCostPerRefugee 非法");
    }
    if (node.contains("regionSupplyRestorePerDay") &&
        !ReadInt(node, "regionSupplyRestorePerDay", 0, 100,
                 &config.regionSupplyRestorePerDay)) {
        return Fail(error, "economy.regionSupplyRestorePerDay 非法");
    }
    if (node.contains("tradeAmmoPerDay") &&
        !ReadInt(node, "tradeAmmoPerDay", 0, 100,
                 &config.tradeAmmoPerDay)) {
        return Fail(error, "economy.tradeAmmoPerDay 非法");
    }
    if (node.contains("tradeSuppliesPerAmmo") &&
        !ReadInt(node, "tradeSuppliesPerAmmo", 1, 1000,
                 &config.tradeSuppliesPerAmmo)) {
        return Fail(error, "economy.tradeSuppliesPerAmmo 非法");
    }
    if (node.contains("tradeFuelPerDay") &&
        !ReadInt(node, "tradeFuelPerDay", 0, 100,
                 &config.tradeFuelPerDay)) {
        return Fail(error, "economy.tradeFuelPerDay 非法");
    }
    if (node.contains("tradeSuppliesPerFuel") &&
        !ReadInt(node, "tradeSuppliesPerFuel", 1, 1000,
                 &config.tradeSuppliesPerFuel)) {
        return Fail(error, "economy.tradeSuppliesPerFuel 非法");
    }
    if (node.contains("tradeReserveSupplies") &&
        !ReadInt(node, "tradeReserveSupplies", 0, kMaxStrategicValue,
                 &config.tradeReserveSupplies)) {
        return Fail(error, "economy.tradeReserveSupplies 非法");
    }
    if (node.contains("fortificationRepairPerDay") &&
        !ReadInt(node, "fortificationRepairPerDay", 0, 100,
                 &config.fortificationRepairPerDay)) {
        return Fail(error, "economy.fortificationRepairPerDay 非法");
    }
    if (node.contains("shortagePressurePerSupply") &&
        !ReadFloat(node, "shortagePressurePerSupply", 0.0f, 100.0f,
                   &config.shortagePressurePerSupply)) {
        return Fail(error, "economy.shortagePressurePerSupply 非法");
    }
    if (node.contains("shortageMoralePerSupply") &&
        !ReadFloat(node, "shortageMoralePerSupply", 0.0f, 100.0f,
                   &config.shortageMoralePerSupply)) {
        return Fail(error, "economy.shortageMoralePerSupply 非法");
    }
    if (node.contains("missionRewards")) {
        const auto& rewards = node["missionRewards"];
        if (!rewards.is_array() || rewards.size() > 32) {
            return Fail(error, "economy.missionRewards 必须是最多 32 个条目");
        }
        std::unordered_set<std::string> missions;
        for (const auto& entry : rewards) {
            StrategicMissionRewardConfig reward;
            if (!ParseMissionReward(entry, &reward, error)) {
                return false;
            }
            if (!missions.insert(reward.missionId).second) {
                return Fail(error, "economy.missionRewards mission 重复：" +
                                       reward.missionId);
            }
            config.missionRewards.push_back(std::move(reward));
        }
    }
    *out = std::move(config);
    return true;
}

bool ParseContribution(const nlohmann::json& node,
                       StrategicContributionConfig* out, std::string* error) {
    if (!node.is_object()) {
        return Fail(error, "contribution 必须是对象");
    }
    StrategicContributionConfig config;
    if (node.contains("rewardXp") &&
        !ReadInt(node, "rewardXp", 0, kMaxStrategicValue,
                 &config.rewardXp)) {
        return Fail(error, "contribution.rewardXp 非法");
    }
    if (node.contains("donatePerSupply") &&
        !ReadInt(node, "donatePerSupply", 0, 100,
                 &config.donatePerSupply)) {
        return Fail(error, "contribution.donatePerSupply 非法");
    }
    if (node.contains("clearCore") &&
        !ReadInt(node, "clearCore", 0, 10000, &config.clearCore)) {
        return Fail(error, "contribution.clearCore 非法");
    }
    if (node.contains("repairFacility") &&
        !ReadInt(node, "repairFacility", 0, 10000,
                 &config.repairFacility)) {
        return Fail(error, "contribution.repairFacility 非法");
    }
    if (node.contains("reclaim") &&
        !ReadInt(node, "reclaim", 0, 10000, &config.reclaim)) {
        return Fail(error, "contribution.reclaim 非法");
    }
    if (node.contains("mission") &&
        !ReadInt(node, "mission", 0, 10000, &config.mission)) {
        return Fail(error, "contribution.mission 非法");
    }
    if (node.contains("defenseRewardXp") &&
        !ReadInt(node, "defenseRewardXp", 0, 10000,
                 &config.defenseRewardXp)) {
        return Fail(error, "contribution.defenseRewardXp 非法");
    }
    if (node.contains("defenseDamagePerPoint") &&
        !ReadInt(node, "defenseDamagePerPoint", 1, 10000,
                 &config.defenseDamagePerPoint)) {
        return Fail(error, "contribution.defenseDamagePerPoint 非法");
    }
    if (node.contains("defenseKillAssistPoints") &&
        !ReadInt(node, "defenseKillAssistPoints", 0, 10000,
                 &config.defenseKillAssistPoints)) {
        return Fail(error, "contribution.defenseKillAssistPoints 非法");
    }
    if (node.contains("minContribution") &&
        !ReadInt(node, "minContribution", 0, kMaxStrategicValue,
                 &config.minContribution)) {
        return Fail(error, "contribution.minContribution 非法");
    }
    *out = config;
    return true;
}

bool ParseRegion(const nlohmann::json& entry, StrategicRegionConfig* out,
                 std::string* error) {
    if (!entry.is_object()) {
        return Fail(error, "regions 条目必须是对象");
    }
    StrategicRegionConfig region;
    if (!ReadString(entry, "id", kMaxRegionIdBytes, &region.id) ||
        !ValidRegionId(region.id)) {
        return Fail(error, "战略区域 id 非法");
    }
    const bool hasAnchorX = entry.contains("anchorX");
    const bool hasAnchorY = entry.contains("anchorY");
    if (hasAnchorX != hasAnchorY) {
        return Fail(error, "anchorX/anchorY 必须同时存在：" + region.id);
    }
    if (hasAnchorX) {
        if (!ReadFloat(entry, "anchorX", 0.0f, 1000000.0f,
                       &region.anchorX) ||
            !ReadFloat(entry, "anchorY", 0.0f, 1000000.0f,
                       &region.anchorY)) {
            return Fail(error, "anchorX/anchorY 非法：" + region.id);
        }
        region.hasAnchor = true;
    }
    if (entry.contains("strategicValue") &&
        !ReadInt(entry, "strategicValue", 1, 10, &region.strategicValue)) {
        return Fail(error, "strategicValue 必须是 1~10：" + region.id);
    }
    std::string ownerValue;
    if (!ReadString(entry, "initialOwner", 32, &ownerValue) ||
        !ParseStrategicOwner(ownerValue, &region.initialOwner)) {
        return Fail(error, "initialOwner 非法：" + region.id);
    }
    if (entry.contains("contestFaction")) {
        std::string factionValue;
        if (!ReadString(entry, "contestFaction", 32, &factionValue) ||
            !ParseContestedFaction(factionValue, &region.contestedFaction)) {
            return Fail(error, "contestFaction 非法：" + region.id);
        }
    }
    if (entry.contains("garrison") &&
        !ReadInt(entry, "garrison", 0, 10000, &region.garrison)) {
        return Fail(error, "garrison 非法：" + region.id);
    }
    if (entry.contains("fortification") &&
        !ReadInt(entry, "fortification", 0, 100, &region.fortification)) {
        return Fail(error, "fortification 必须是 0~100：" + region.id);
    }
    if (entry.contains("defense") &&
        !ReadInt(entry, "defense", 0, 100, &region.defense)) {
        return Fail(error, "defense 必须是 0~100：" + region.id);
    }
    if (!entry.contains("defense")) {
        region.defense = region.fortification;
    }
    if (entry.contains("supply") &&
        !ReadInt(entry, "supply", 0, 100, &region.supply)) {
        return Fail(error, "supply 必须是 0~100：" + region.id);
    }
    if (entry.contains("population") &&
        !ReadInt(entry, "population", 0, kMaxStrategicValue,
                 &region.population)) {
        return Fail(error, "population 非法：" + region.id);
    }
    if (entry.contains("pressure") &&
        !ReadInt(entry, "pressure", 0, 100, &region.pressure)) {
        return Fail(error, "pressure 必须是 0~100：" + region.id);
    }
    if (entry.contains("morale") &&
        !ReadInt(entry, "morale", 0, 100, &region.morale)) {
        return Fail(error, "morale 必须是 0~100：" + region.id);
    }
    if (entry.contains("intelLevel") &&
        !ReadInt(entry, "intelLevel", 0, 3, &region.intelLevel)) {
        return Fail(error, "intelLevel 必须是 0~3：" + region.id);
    }
    if (entry.contains("coreSafe") &&
        !ReadBool(entry, "coreSafe", &region.coreSafe)) {
        return Fail(error, "coreSafe 必须是布尔值：" + region.id);
    }
    if (entry.contains("remote") &&
        !ReadBool(entry, "remote", &region.remote)) {
        return Fail(error, "remote 必须是布尔值：" + region.id);
    }
    if (entry.contains("refugeeTarget")) {
        if (!ReadString(entry, "refugeeTarget", kMaxRegionIdBytes,
                        &region.refugeeTarget) ||
            !ValidRegionId(region.refugeeTarget)) {
            return Fail(error, "refugeeTarget 非法：" + region.id);
        }
    }
    if (entry.contains("attackRoutes")) {
        const auto& routes = entry["attackRoutes"];
        if (!routes.is_array() || routes.size() > 16) {
            return Fail(error, "attackRoutes 必须是最多 16 个字符串：" +
                                   region.id);
        }
        for (const auto& route : routes) {
            if (!route.is_string()) {
                return Fail(error, "attackRoutes 条目必须是字符串：" +
                                       region.id);
            }
            const std::string value = route.get<std::string>();
            if (value.empty() || value.size() > kMaxRegionIdBytes) {
                return Fail(error, "attackRoutes 条目非法：" + region.id);
            }
            region.attackRoutes.push_back(value);
        }
    }
    *out = std::move(region);
    return true;
}

bool IsValidOwner(StrategicOwner owner) {
    return static_cast<std::uint8_t>(owner) <= 3;
}

bool IsValidSiegePhase(StrategicSiegePhase phase) {
    return static_cast<std::uint8_t>(phase) <= 4;
}

bool IsValidRecoveryStage(StrategicRecoveryStage stage) {
    return static_cast<std::uint8_t>(stage) <= 4;
}

void AddResources(StrategicResources* resources, int people, int supplies,
                  int ammo, int fuel) {
    const auto add = [](int* value, int delta) {
        const std::int64_t sum =
            static_cast<std::int64_t>(*value) + static_cast<std::int64_t>(delta);
        *value = static_cast<int>(
            std::clamp<std::int64_t>(sum, 0, kMaxStrategicValue));
    };
    add(&resources->people, people);
    add(&resources->supplies, supplies);
    add(&resources->ammo, ammo);
    add(&resources->fuel, fuel);
}

bool EventMatches(const StrategicEvent& event, std::string_view regionId,
                  StrategicEventKind kind, bool checkOwner,
                  StrategicOwner ownerAfter, const StrategicResources& delta) {
    if (event.regionId != regionId || event.kind != kind ||
        event.people != delta.people || event.supplies != delta.supplies ||
        event.ammo != delta.ammo || event.fuel != delta.fuel) {
        return false;
    }
    return !checkOwner || event.ownerAfter == ownerAfter;
}

}  // namespace

const char* ContestedFactionName(ContestedFaction faction) {
    switch (faction) {
        case ContestedFaction::None:
            return "None";
        case ContestedFaction::Infected:
            return "Infected";
        case ContestedFaction::Raiders:
            return "Raiders";
    }
    return "Unknown";
}

const char* ContestedFactionLabel(ContestedFaction faction) {
    switch (faction) {
        case ContestedFaction::None:
            return "无";
        case ContestedFaction::Infected:
            return "感染者";
        case ContestedFaction::Raiders:
            return "掠夺者";
    }
    return "未知";
}

bool ParseContestedFaction(std::string_view value, ContestedFaction* out) {
    if (out == nullptr) {
        return false;
    }
    if (value == "None" || value == "none") {
        *out = ContestedFaction::None;
        return true;
    }
    if (value == "Infected" || value == "infected") {
        *out = ContestedFaction::Infected;
        return true;
    }
    if (value == "Raiders" || value == "raiders") {
        *out = ContestedFaction::Raiders;
        return true;
    }
    return false;
}

const char* StrategicOwnerName(StrategicOwner owner) {
    switch (owner) {
        case StrategicOwner::Neutral:
            return "Neutral";
        case StrategicOwner::Human:
            return "Human";
        case StrategicOwner::Contested:
            return "Contested";
        case StrategicOwner::Infested:
            return "Infested";
    }
    return "Unknown";
}

const char* StrategicOwnerLabel(StrategicOwner owner) {
    switch (owner) {
        case StrategicOwner::Neutral:
            return "中立";
        case StrategicOwner::Human:
            return "人类";
        case StrategicOwner::Contested:
            return "争夺";
        case StrategicOwner::Infested:
            return "沦陷";
    }
    return "未知";
}

bool ParseStrategicOwner(std::string_view value, StrategicOwner* out) {
    if (out == nullptr) {
        return false;
    }
    if (value == "Neutral") {
        *out = StrategicOwner::Neutral;
        return true;
    }
    if (value == "Human") {
        *out = StrategicOwner::Human;
        return true;
    }
    if (value == "Contested") {
        *out = StrategicOwner::Contested;
        return true;
    }
    if (value == "Infested") {
        *out = StrategicOwner::Infested;
        return true;
    }
    return false;
}

const char* StrategicSiegePhaseName(StrategicSiegePhase phase) {
    switch (phase) {
        case StrategicSiegePhase::None:
            return "None";
        case StrategicSiegePhase::Warning:
            return "Warning";
        case StrategicSiegePhase::Active:
            return "Active";
        case StrategicSiegePhase::Resolving:
            return "Resolving";
        case StrategicSiegePhase::Recovering:
            return "Recovering";
    }
    return "Unknown";
}

const char* StrategicSiegePhaseLabel(StrategicSiegePhase phase) {
    switch (phase) {
        case StrategicSiegePhase::None:
            return "平静";
        case StrategicSiegePhase::Warning:
            return "预警";
        case StrategicSiegePhase::Active:
            return "围攻";
        case StrategicSiegePhase::Resolving:
            return "结算";
        case StrategicSiegePhase::Recovering:
            return "恢复";
    }
    return "未知";
}

const char* StrategicEventKindName(StrategicEventKind kind) {
    switch (kind) {
        case StrategicEventKind::Owner:
            return "归属";
        case StrategicEventKind::Resource:
            return "资源";
        case StrategicEventKind::Warning:
            return "预警";
        case StrategicEventKind::Siege:
            return "围攻";
        case StrategicEventKind::Recovery:
            return "恢复";
        case StrategicEventKind::Refugee:
            return "难民";
    }
    return "事件";
}

const char* StrategicRecoveryStageName(StrategicRecoveryStage stage) {
    switch (stage) {
        case StrategicRecoveryStage::None:
            return "None";
        case StrategicRecoveryStage::ClearCore:
            return "ClearCore";
        case StrategicRecoveryStage::Repair:
            return "Repair";
        case StrategicRecoveryStage::Counterattack:
            return "Counterattack";
        case StrategicRecoveryStage::Restoring:
            return "Restoring";
    }
    return "Unknown";
}

const char* StrategicRecoveryStageLabel(StrategicRecoveryStage stage) {
    switch (stage) {
        case StrategicRecoveryStage::None:
            return "稳定";
        case StrategicRecoveryStage::ClearCore:
            return "清理感染核心";
        case StrategicRecoveryStage::Repair:
            return "修复设施";
        case StrategicRecoveryStage::Counterattack:
            return "反攻波次";
        case StrategicRecoveryStage::Restoring:
            return "恢复中";
    }
    return "未知";
}

const char* StrategicIntelName(int intelLevel) {
    switch (std::clamp(intelLevel, 0, 3)) {
        case 3:
            return "高";
        case 2:
            return "中";
        case 1:
            return "低";
        default:
            return "未知";
    }
}

int StrategicIntelErrorPercent(int intelLevel) {
    switch (std::clamp(intelLevel, 0, 3)) {
        case 3:
            return 0;
        case 2:
            return 8;
        case 1:
            return 25;
        default:
            return 40;
    }
}

int StrategicIntelTimeErrorMinutes(int intelLevel) {
    switch (std::clamp(intelLevel, 0, 3)) {
        case 3:
            return 0;
        case 2:
            return 180;
        case 1:
            return 360;
        default:
            return 720;
    }
}

const StrategicRegionConfig* StrategicConfig::Find(std::string_view id) const {
    for (const StrategicRegionConfig& region : regions) {
        if (region.id == id) {
            return &region;
        }
    }
    return nullptr;
}

const StrategicMissionRewardConfig* StrategicEconomyConfig::FindMission(
    std::string_view missionId) const {
    for (const StrategicMissionRewardConfig& reward : missionRewards) {
        if (reward.missionId == missionId) {
            return &reward;
        }
    }
    return nullptr;
}

bool StrategicState::Initialize(const StrategicConfig& config,
                                std::string* error) {
    regions.clear();
    events.clear();
    contributions.clear();
    resources = config.initialResources;
    lastTickMinute = 0;
    if (!config.enabled) {
        SetError(error, {});
        return true;
    }
    regions.reserve(config.regions.size());
    for (const StrategicRegionConfig& def : config.regions) {
        StrategicRegionState state;
        state.id = def.id;
        state.owner = def.initialOwner;
        state.pressure = static_cast<float>(def.pressure);
        state.defense = static_cast<float>(def.defense);
        state.supply = static_cast<float>(def.supply);
        state.morale = static_cast<float>(def.morale);
        state.population = def.population;
        state.intelLevel = def.intelLevel;
        state.garrison = def.garrison;
        state.fortification = def.fortification;
        state.coreSafe = def.coreSafe;
        state.facilityDamage = 0;
        state.refugees = 0;
        state.infectionCores = def.initialOwner == StrategicOwner::Infested
                                   ? config.recovery.coreCount
                                   : 0;
        state.recoveryStage = StrategicRecoveryStage::None;
        state.recoveryReadyMinute = -1;
        regions.push_back(std::move(state));
    }
    return Validate(error);
}

StrategicRegionState* StrategicState::FindRegion(std::string_view id) {
    for (StrategicRegionState& region : regions) {
        if (region.id == id) {
            return &region;
        }
    }
    return nullptr;
}

const StrategicRegionState* StrategicState::FindRegion(
    std::string_view id) const {
    for (const StrategicRegionState& region : regions) {
        if (region.id == id) {
            return &region;
        }
    }
    return nullptr;
}

const StrategicEvent* StrategicState::FindEvent(std::string_view id) const {
    for (const StrategicEvent& event : events) {
        if (event.id == id) {
            return &event;
        }
    }
    return nullptr;
}

bool StrategicState::ApplyEvent(std::string_view regionId,
                                StrategicEventKind kind,
                                std::string eventId,
                                std::int64_t gameMinute,
                                StrategicOwner ownerAfter,
                                const StrategicResources& delta,
                                std::string* error) {
    if (static_cast<std::uint8_t>(kind) > 5) {
        return Fail(error, "战略事件类型非法");
    }
    if (!IsValidOwner(ownerAfter)) {
        return Fail(error, "战略区域 owner 非法");
    }
    if (eventId.empty() || eventId.size() > kMaxEventIdBytes) {
        return Fail(error, "战略事件 id 非法");
    }
    if (gameMinute < 0) {
        return Fail(error, "战略事件时间非法");
    }
    StrategicRegionState* region = nullptr;
    if (regionId != "global") {
        region = FindRegion(regionId);
        if (region == nullptr) {
            return Fail(error, "战略区域不存在：" + std::string(regionId));
        }
    } else if (kind != StrategicEventKind::Resource) {
        return Fail(error, "全局战略事件只能是资源事件");
    }
    const bool checkOwner = kind != StrategicEventKind::Resource;
    if (const StrategicEvent* existing = FindEvent(eventId);
        existing != nullptr) {
        if (EventMatches(*existing, regionId, kind, checkOwner, ownerAfter,
                         delta)) {
            return true;
        }
        return Fail(error, "战略事件 id 冲突：" + eventId);
    }

    if (region != nullptr) {
        if (kind == StrategicEventKind::Owner ||
            kind == StrategicEventKind::Siege) {
            region->owner = ownerAfter;
        }
        region->lastEventMinute = std::max(region->lastEventMinute, gameMinute);
    }
    lastTickMinute = std::max(lastTickMinute, gameMinute);
    AddResources(&resources, delta.people, delta.supplies, delta.ammo,
                 delta.fuel);
    StrategicEvent event;
    event.id = std::move(eventId);
    event.regionId = std::string(regionId);
    event.kind = kind;
    event.gameMinute = gameMinute;
    event.ownerAfter = ownerAfter;
    event.people = delta.people;
    event.supplies = delta.supplies;
    event.ammo = delta.ammo;
    event.fuel = delta.fuel;
    events.push_back(std::move(event));
    return Validate(error);
}

bool StrategicState::ApplyOwnerChange(std::string_view regionId,
                                      StrategicOwner owner,
                                      std::string eventId,
                                      std::int64_t gameMinute,
                                      const StrategicResources& delta,
                                      std::string* error) {
    return ApplyEvent(regionId, StrategicEventKind::Owner, std::move(eventId),
                      gameMinute, owner, delta, error);
}

bool StrategicState::ApplyResourceDelta(std::string_view regionId,
                                        const StrategicResources& delta,
                                        std::string eventId,
                                        std::int64_t gameMinute,
                                        std::string* error) {
    StrategicRegionState* region = nullptr;
    if (regionId != "global") {
        region = FindRegion(regionId);
        if (region == nullptr) {
            return Fail(error, "战略区域不存在：" + std::string(regionId));
        }
    }
    const StrategicOwner ownerAfter =
        region != nullptr ? region->owner : StrategicOwner::Neutral;
    return ApplyEvent(regionId, StrategicEventKind::Resource,
                      std::move(eventId), gameMinute, ownerAfter, delta,
                      error);
}

bool StrategicState::AdvanceTick(std::int64_t gameMinute, std::string* error) {
    if (gameMinute < 0) {
        return Fail(error, "战略 tick 时间非法");
    }
    if (gameMinute < lastTickMinute) {
        return Fail(error, "战略 tick 不能倒退");
    }
    lastTickMinute = gameMinute;
    return true;
}

bool StrategicState::Validate(std::string* error) const {
    if (resources.people < 0 || resources.supplies < 0 ||
        resources.ammo < 0 || resources.fuel < 0) {
        return Fail(error, "战略资源不能为负");
    }
    if (regions.size() > kMaxRegions) {
        return Fail(error, "战略区域数量超过上限");
    }
    std::unordered_set<std::string> regionIds;
    for (const StrategicRegionState& region : regions) {
        if (!ValidRegionId(region.id)) {
            return Fail(error, "战略区域 id 非法：" + region.id);
        }
        if (!regionIds.insert(region.id).second) {
            return Fail(error, "战略区域 id 重复：" + region.id);
        }
        if (!IsValidOwner(region.owner)) {
            return Fail(error, "战略区域 owner 非法：" + region.id);
        }
        if (!IsValidSiegePhase(region.siegePhase) ||
            !IsValidRecoveryStage(region.recoveryStage)) {
            return Fail(error, "战略区域围攻/恢复阶段非法：" + region.id);
        }
        if (!std::isfinite(region.pressure) || !std::isfinite(region.defense) ||
            !std::isfinite(region.supply) || !std::isfinite(region.morale)) {
            return Fail(error, "战略区域数值不是有限数：" + region.id);
        }
        if (region.pressure < 0.0f || region.pressure > 100.0f ||
            region.defense < 0.0f || region.defense > 100.0f ||
            region.supply < 0.0f || region.supply > 100.0f ||
            region.morale < 0.0f || region.morale > 100.0f) {
            return Fail(error, "战略区域数值越界：" + region.id);
        }
        if (region.population < 0 || region.intelLevel < 0 ||
            region.intelLevel > 3 || region.garrison < 0 ||
            region.fortification < 0 || region.facilityDamage < 0 ||
            region.facilityDamage > 100 || region.refugees < 0 ||
            region.infectionCores < 0 || region.recoveryReadyMinute < -1 ||
            region.nextAttackMinute < -1 ||
            region.lastEventMinute < -1 ||
            region.refugeeHost.size() > kMaxRegionIdBytes) {
            return Fail(error, "战略区域摘要字段非法：" + region.id);
        }
    }
    for (const StrategicRegionState& region : regions) {
        if (!region.refugeeHost.empty() &&
            regionIds.find(region.refugeeHost) == regionIds.end()) {
            return Fail(error, "战略区域难民宿主不存在：" + region.id);
        }
    }
    if (events.size() > kMaxEvents) {
        return Fail(error, "战略事件数量超过上限");
    }
    std::unordered_set<std::string> eventIds;
    for (const StrategicEvent& event : events) {
        if (event.id.empty() || event.id.size() > kMaxEventIdBytes) {
            return Fail(error, "战略事件 id 非法");
        }
        if (!eventIds.insert(event.id).second) {
            return Fail(error, "战略事件 id 重复：" + event.id);
        }
        if (event.regionId != "global" &&
            regionIds.find(event.regionId) == regionIds.end()) {
            return Fail(error, "战略事件区域不存在：" + event.regionId);
        }
        if (static_cast<std::uint8_t>(event.kind) > 5 ||
            !IsValidOwner(event.ownerAfter) || event.gameMinute < 0) {
            return Fail(error, "战略事件字段非法：" + event.id);
        }
        const auto inRange = [](int value) {
            return value >= -kMaxStrategicValue && value <= kMaxStrategicValue;
        };
        if (!inRange(event.people) || !inRange(event.supplies) ||
            !inRange(event.ammo) || !inRange(event.fuel)) {
            return Fail(error, "战略事件资源变化越界：" + event.id);
        }
    }
    if (contributions.size() > 1024u) {
        return Fail(error, "战略贡献记录数量超过上限");
    }
    std::unordered_set<std::string> contributionKeys;
    for (const StrategicContributionRecord& entry : contributions) {
        if (entry.stableId == 0 || entry.regionId.empty() ||
            entry.regionId.size() > kMaxRegionIdBytes ||
            entry.points < 0) {
            return Fail(error, "战略贡献记录字段非法");
        }
        const std::string key =
            std::to_string(entry.stableId) + ":" + entry.regionId;
        if (!contributionKeys.insert(key).second) {
            return Fail(error, "战略贡献记录重复");
        }
    }
    SetError(error, {});
    return true;
}

bool LoadStrategicConfig(const std::string& path, StrategicConfig* out,
                         std::string* error) {
    if (out == nullptr) {
        return Fail(error, "战略配置输出为空");
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return Fail(error, "无法打开战略配置：" + path);
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (!file.good() && !file.eof()) {
        return Fail(error, "读取战略配置失败：" + path);
    }
    return ParseStrategicConfig(buffer.str(), path, out, error);
}

bool ParseStrategicConfig(std::string_view json, const std::string& source,
                          StrategicConfig* out, std::string* error) {
    if (out == nullptr) {
        return Fail(error, "战略配置输出为空");
    }
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json.begin(), json.end());
    } catch (const std::exception& ex) {
        return Fail(error, "战略配置 JSON 解析失败（" + source + "）：" +
                               ex.what());
    }
    if (!root.is_object()) {
        return Fail(error, "战略配置根节点必须是对象：" + source);
    }
    StrategicConfig config;
    std::string format;
    if (!ReadString(root, "format", 64, &format) ||
        format != "zomboidlike-strategic-v1") {
        return Fail(error, "战略配置 format 必须是 zomboidlike-strategic-v1：" +
                               source);
    }
    config.format = format;
    if (root.contains("enabled") &&
        !ReadBool(root, "enabled", &config.enabled)) {
        return Fail(error, "战略配置 enabled 必须是布尔值：" + source);
    }
    if (root.contains("seed") && !ReadSeed(root, "seed", &config.seed)) {
        return Fail(error, "seed 必须是非负整数：" + source);
    }
    if (root.contains("tickMinutes") &&
        !ReadInt(root, "tickMinutes", 1, 1440, &config.tickMinutes)) {
        return Fail(error, "tickMinutes 必须是 1~1440：" + source);
    }
    if (root.contains("warningHours") &&
        !ReadInt(root, "warningHours", 1, 720, &config.warningHours)) {
        return Fail(error, "warningHours 必须是 1~720：" + source);
    }
    if (root.contains("remoteWarningHours") &&
        !ReadInt(root, "remoteWarningHours", 1, 1440,
                 &config.remoteWarningHours)) {
        return Fail(error, "remoteWarningHours 必须是 1~1440：" + source);
    }
    if (config.remoteWarningHours < config.warningHours) {
        return Fail(error, "remoteWarningHours 不能小于 warningHours：" +
                               source);
    }
    if (root.contains("autoResolveGraceMinutes") &&
        !ReadInt(root, "autoResolveGraceMinutes", 0, 1000000,
                 &config.autoResolveGraceMinutes)) {
        return Fail(error, "autoResolveGraceMinutes 非法：" + source);
    }
    if (root.contains("pressurePerHour") &&
        !ReadFloat(root, "pressurePerHour", 0.0f, 10.0f,
                   &config.pressurePerHour)) {
        return Fail(error, "pressurePerHour 必须是 0~10：" + source);
    }
    if (root.contains("attackPressure") &&
        !ReadFloat(root, "attackPressure", 0.0f, 100.0f,
                   &config.attackPressure)) {
        return Fail(error, "attackPressure 必须是 0~100：" + source);
    }
    if (root.contains("autoResolve") &&
        !ParseAutoResolve(root["autoResolve"], &config.autoResolve, error)) {
        return false;
    }
    if (root.contains("recovery") &&
        !ParseRecovery(root["recovery"], &config.recovery, error)) {
        return false;
    }
    if (root.contains("economy") &&
        !ParseEconomy(root["economy"], &config.economy, error)) {
        return false;
    }
    if (root.contains("contribution") &&
        !ParseContribution(root["contribution"], &config.contribution, error)) {
        return false;
    }
    if (root.contains("initialResources")) {
        if (!ReadResources(root["initialResources"], &config.initialResources,
                           error)) {
            return false;
        }
    }
    if (!root.contains("regions") || !root["regions"].is_array()) {
        return Fail(error, "战略配置缺少 regions 数组：" + source);
    }
    const auto& regions = root["regions"];
    if (regions.size() > kMaxRegions) {
        return Fail(error, "战略区域数量超过 64：" + source);
    }
    if (config.enabled && regions.empty()) {
        return Fail(error, "战略配置启用时 regions 不能为空：" + source);
    }
    std::unordered_set<std::string> ids;
    for (const auto& entry : regions) {
        StrategicRegionConfig region;
        if (!ParseRegion(entry, &region, error)) {
            return false;
        }
        if (!ids.insert(region.id).second) {
            return Fail(error, "战略区域 id 重复：" + region.id);
        }
        config.regions.push_back(std::move(region));
    }
    for (const StrategicRegionConfig& region : config.regions) {
        if (region.refugeeTarget.empty()) {
            continue;
        }
        if (region.refugeeTarget == region.id ||
            ids.find(region.refugeeTarget) == ids.end()) {
            return Fail(error, "refugeeTarget 必须指向其他战略区域：" +
                                   region.id);
        }
    }
    for (const StrategicMissionRewardConfig& reward :
         config.economy.missionRewards) {
        if (!reward.regionId.empty() &&
            ids.find(reward.regionId) == ids.end()) {
            return Fail(error, "economy.missionRewards.region 不存在：" +
                                   reward.regionId);
        }
    }
    *out = std::move(config);
    SetError(error, {});
    return true;
}

bool ValidateStrategicAgainstWorld(const StrategicConfig& config,
                                   const world::WorldDefinition& world,
                                   std::string* error) {
    if (!config.enabled) {
        return true;
    }
    std::unordered_set<std::string> worldIds;
    for (const world::WorldRegion& region : world.regions) {
        worldIds.insert(region.id);
    }
    for (const StrategicRegionConfig& region : config.regions) {
        if (worldIds.find(region.id) == worldIds.end()) {
            return Fail(error, "战略区域不在 world.json 中：" + region.id);
        }
        if (!region.hasAnchor) {
            continue;
        }
        const auto found = std::find_if(
            world.regions.begin(), world.regions.end(),
            [&](const world::WorldRegion& item) {
                return item.id == region.id;
            });
        if (found == world.regions.end()) {
            return Fail(error, "战略区域锚点无法校验：" + region.id);
        }
        const float x0 =
            static_cast<float>(found->rect.chunkX * core::kChunkSize);
        const float y0 =
            static_cast<float>(found->rect.chunkY * core::kChunkSize);
        const float x1 =
            x0 + static_cast<float>(found->rect.chunksW * core::kChunkSize);
        const float y1 =
            y0 + static_cast<float>(found->rect.chunksH * core::kChunkSize);
        if (region.anchorX < x0 || region.anchorX >= x1 ||
            region.anchorY < y0 || region.anchorY >= y1) {
            return Fail(error, "战略区域锚点不在 world region 内：" +
                                   region.id);
        }
    }
    SetError(error, {});
    return true;
}

std::unordered_map<std::uint64_t, int> AllocateSiegeKillAssist(
    const std::unordered_map<std::uint64_t, float>& targetDamage,
    int totalPoints) {
    std::unordered_map<std::uint64_t, int> result;
    if (totalPoints <= 0 || targetDamage.empty()) {
        return result;
    }
    struct Share {
        std::uint64_t stableId = 0;
        float damage = 0.0f;
        int points = 0;
        std::int64_t remainder = 0;
    };
    std::vector<Share> shares;
    float totalDamage = 0.0f;
    for (const auto& [stableId, damage] : targetDamage) {
        if (stableId == 0 || damage <= 0.0f || !std::isfinite(damage)) {
            continue;
        }
        shares.push_back(Share{stableId, damage, 0, 0});
        totalDamage += damage;
    }
    if (shares.empty() || totalDamage <= 0.0f) {
        return result;
    }
    for (Share& share : shares) {
        const std::int64_t weighted =
            static_cast<std::int64_t>(totalPoints) * 1000000;
        const std::int64_t scaled = static_cast<std::int64_t>(
            weighted * (static_cast<double>(share.damage) / totalDamage));
        share.points = static_cast<int>(scaled / 1000000);
        share.remainder = scaled % 1000000;
    }
    std::sort(shares.begin(), shares.end(),
              [](const Share& left, const Share& right) {
                  if (left.remainder != right.remainder) {
                      return left.remainder > right.remainder;
                  }
                  if (left.damage != right.damage) {
                      return left.damage > right.damage;
                  }
                  return left.stableId < right.stableId;
              });
    int awarded = 0;
    for (const Share& share : shares) {
        awarded += share.points;
    }
    int leftover = totalPoints - awarded;
    for (std::size_t i = 0; i < shares.size() && leftover > 0;
         ++i, --leftover) {
        ++shares[i].points;
    }
    for (const Share& share : shares) {
        if (share.points > 0) {
            result[share.stableId] = share.points;
        }
    }
    return result;
}

}  // namespace zl::game
