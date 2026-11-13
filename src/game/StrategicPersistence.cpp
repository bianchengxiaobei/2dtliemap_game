#include "game/StrategicPersistence.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>
#include <utility>

#include "game/StrategicState.h"
#include "save/WorldManifest.h"

namespace zl::game {
namespace {

bool Fail(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
    return false;
}

const save::ManifestStrategicRegion* FindSavedRegion(
    const save::WorldManifest& manifest, std::string_view id) {
    for (const save::ManifestStrategicRegion& region :
         manifest.strategicRegions) {
        if (region.id == id) {
            return &region;
        }
    }
    return nullptr;
}

}  // namespace

bool CaptureStrategicState(const StrategicState& state,
                           save::WorldManifest* manifest, std::string* error) {
    if (manifest == nullptr) {
        return Fail(error, "world manifest 输出为空");
    }
    if (!state.Validate(error)) {
        return false;
    }
    manifest->version = std::max<std::uint16_t>(manifest->version, 5);
    manifest->strategicTickMinute = state.lastTickMinute;
    manifest->strategicResources.people = state.resources.people;
    manifest->strategicResources.supplies = state.resources.supplies;
    manifest->strategicResources.ammo = state.resources.ammo;
    manifest->strategicResources.fuel = state.resources.fuel;
    manifest->strategicRegions.clear();
    manifest->strategicRegions.reserve(state.regions.size());
    for (const StrategicRegionState& region : state.regions) {
        save::ManifestStrategicRegion saved;
        saved.id = region.id;
        saved.owner = static_cast<std::uint8_t>(region.owner);
        saved.siegePhase = static_cast<std::uint8_t>(region.siegePhase);
        saved.intelLevel = static_cast<std::uint8_t>(region.intelLevel);
        saved.coreSafe = region.coreSafe;
        saved.garrison = region.garrison;
        saved.fortification = region.fortification;
        saved.population = region.population;
        saved.pressure = region.pressure;
        saved.defense = region.defense;
        saved.supply = region.supply;
        saved.morale = region.morale;
        saved.facilityDamage = region.facilityDamage;
        saved.refugees = region.refugees;
        saved.infectionCores = region.infectionCores;
        saved.recoveryStage = static_cast<std::uint8_t>(region.recoveryStage);
        saved.recoveryReadyMinute = region.recoveryReadyMinute;
        saved.refugeeHost = region.refugeeHost;
        saved.nextAttackMinute = region.nextAttackMinute;
        saved.lastEventMinute = region.lastEventMinute;
        manifest->strategicRegions.push_back(std::move(saved));
    }
    manifest->strategicEvents.clear();
    manifest->strategicEvents.reserve(state.events.size());
    for (const StrategicEvent& event : state.events) {
        save::ManifestStrategicEvent saved;
        saved.id = event.id;
        saved.regionId = event.regionId;
        saved.kind = static_cast<std::uint8_t>(event.kind);
        saved.ownerAfter = static_cast<std::uint8_t>(event.ownerAfter);
        saved.gameMinute = event.gameMinute;
        saved.people = event.people;
        saved.supplies = event.supplies;
        saved.ammo = event.ammo;
        saved.fuel = event.fuel;
        manifest->strategicEvents.push_back(std::move(saved));
    }
    manifest->strategicContributions.clear();
    manifest->strategicContributions.reserve(state.contributions.size());
    for (const StrategicContributionRecord& contribution :
         state.contributions) {
        save::ManifestStrategicContribution saved;
        saved.stableId = contribution.stableId;
        saved.regionId = contribution.regionId;
        saved.points = contribution.points;
        manifest->strategicContributions.push_back(std::move(saved));
    }
    return true;
}

bool RestoreStrategicState(const save::WorldManifest& manifest,
                           const StrategicConfig& config,
                           StrategicState* state, std::string* error) {
    if (state == nullptr) {
        return Fail(error, "战略状态输出为空");
    }
    if (manifest.strategicRegions.empty()) {
        if (manifest.strategicTickMinute != 0 ||
            manifest.strategicResources.people != 0 ||
            manifest.strategicResources.supplies != 0 ||
            manifest.strategicResources.ammo != 0 ||
            manifest.strategicResources.fuel != 0 ||
            !manifest.strategicEvents.empty() ||
            !manifest.strategicContributions.empty()) {
            return Fail(error, "存档战略状态不完整");
        }
        return state->Initialize(config, error);
    }
    if (!config.enabled) {
        return Fail(error, "存档含战略状态但战略配置已禁用");
    }

    std::unordered_set<std::string> savedIds;
    for (const save::ManifestStrategicRegion& saved :
         manifest.strategicRegions) {
        if (!savedIds.insert(saved.id).second) {
            return Fail(error, "存档战略区域 id 重复：" + saved.id);
        }
    }
    StrategicState restored;
    restored.lastTickMinute = manifest.strategicTickMinute;
    restored.resources.people = manifest.strategicResources.people;
    restored.resources.supplies = manifest.strategicResources.supplies;
    restored.resources.ammo = manifest.strategicResources.ammo;
    restored.resources.fuel = manifest.strategicResources.fuel;
    restored.regions.reserve(config.regions.size());
    for (const StrategicRegionConfig& def : config.regions) {
        const save::ManifestStrategicRegion* saved =
            FindSavedRegion(manifest, def.id);
        if (saved == nullptr) {
            return Fail(error, "存档缺少战略区域：" + def.id);
        }
        if (saved->owner > 3 || saved->siegePhase > 4 ||
            saved->intelLevel > 3 || saved->garrison < 0 ||
            saved->fortification < 0 || saved->population < 0 ||
            saved->facilityDamage < 0 || saved->facilityDamage > 100 ||
            saved->refugees < 0 || saved->infectionCores < 0 ||
            saved->recoveryStage > 4 || saved->recoveryReadyMinute < -1 ||
            saved->refugeeHost.size() > 128 ||
            saved->nextAttackMinute < -1 || saved->lastEventMinute < -1 ||
            !std::isfinite(saved->pressure) ||
            !std::isfinite(saved->defense) ||
            !std::isfinite(saved->supply) ||
            !std::isfinite(saved->morale)) {
            return Fail(error, "存档战略区域字段非法：" + def.id);
        }
        StrategicRegionState region;
        region.id = def.id;
        region.owner = static_cast<StrategicOwner>(saved->owner);
        region.siegePhase =
            static_cast<StrategicSiegePhase>(saved->siegePhase);
        region.intelLevel = saved->intelLevel;
        region.coreSafe = saved->coreSafe;
        region.garrison = saved->garrison;
        region.fortification = saved->fortification;
        region.population = saved->population;
        region.pressure = saved->pressure;
        region.defense = saved->defense;
        region.supply = saved->supply;
        region.morale = saved->morale;
        region.facilityDamage = saved->facilityDamage;
        region.refugees = saved->refugees;
        region.infectionCores = saved->infectionCores;
        region.recoveryStage =
            static_cast<StrategicRecoveryStage>(saved->recoveryStage);
        region.recoveryReadyMinute = saved->recoveryReadyMinute;
        region.refugeeHost = saved->refugeeHost;
        region.nextAttackMinute = saved->nextAttackMinute;
        region.lastEventMinute = saved->lastEventMinute;
        restored.regions.push_back(std::move(region));
    }
    if (restored.regions.size() != manifest.strategicRegions.size()) {
        return Fail(error, "存档包含未知战略区域");
    }
    restored.events.reserve(manifest.strategicEvents.size());
    for (const save::ManifestStrategicEvent& saved :
         manifest.strategicEvents) {
        StrategicEvent event;
        event.id = saved.id;
        event.regionId = saved.regionId;
        event.kind = static_cast<StrategicEventKind>(saved.kind);
        event.ownerAfter = static_cast<StrategicOwner>(saved.ownerAfter);
        event.gameMinute = saved.gameMinute;
        event.people = saved.people;
        event.supplies = saved.supplies;
        event.ammo = saved.ammo;
        event.fuel = saved.fuel;
        restored.events.push_back(std::move(event));
    }
    restored.contributions.reserve(manifest.strategicContributions.size());
    for (const save::ManifestStrategicContribution& saved :
         manifest.strategicContributions) {
        StrategicContributionRecord contribution;
        contribution.stableId = saved.stableId;
        contribution.regionId = saved.regionId;
        contribution.points = saved.points;
        restored.contributions.push_back(std::move(contribution));
    }
    if (!restored.Validate(error)) {
        return false;
    }
    *state = std::move(restored);
    return true;
}

}  // namespace zl::game
