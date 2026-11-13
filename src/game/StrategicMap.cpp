#include "game/StrategicMap.h"

#include <algorithm>

#include "game/StrategicDirector.h"
#include "net/StrategicProtocol.h"

namespace zl::game {

const StrategicMapRegion* StrategicMapState::FindRegion(
    std::string_view id) const {
    const auto found = std::find_if(
        regions.begin(), regions.end(),
        [&](const StrategicMapRegion& region) { return region.id == id; });
    return found != regions.end() ? &*found : nullptr;
}

StrategicMapState BuildStrategicMapState(const StrategicConfig& config,
                                         const StrategicState& state,
                                         std::int64_t gameMinute,
                                         std::size_t maxEvents) {
    StrategicMapState out;
    out.enabled = config.enabled;
    out.gameMinute = std::max<std::int64_t>(0, gameMinute);
    out.tickMinutes = config.tickMinutes;
    out.warningHours = config.warningHours;
    out.remoteWarningHours = config.remoteWarningHours;
    out.autoResolveGraceMinutes = config.autoResolveGraceMinutes;
    out.resources = state.resources;
    if (!config.enabled) {
        return out;
    }

    out.regions.reserve(state.regions.size());
    for (const StrategicRegionState& region : state.regions) {
        const StrategicRegionConfig* def = config.Find(region.id);
        StrategicMapRegion entry;
        entry.id = region.id;
        entry.owner = region.owner;
        entry.contestedFaction = def != nullptr
                                     ? def->contestedFaction
                                     : ContestedFaction::Infected;
        entry.siegePhase = region.siegePhase;
        entry.pressure = std::clamp(region.pressure, 0.0f, 100.0f);
        entry.defense = std::clamp(region.defense, 0.0f, 100.0f);
        entry.supply = std::clamp(region.supply, 0.0f, 100.0f);
        entry.morale = std::clamp(region.morale, 0.0f, 100.0f);
        entry.population = std::max(0, region.population);
        entry.garrison = std::max(0, region.garrison);
        entry.fortification = std::max(0, region.fortification);
        entry.intelLevel = std::clamp(region.intelLevel, 0, 3);
        entry.coreSafe = region.coreSafe;
        entry.strategicValue = def != nullptr
                                   ? std::max(1, def->strategicValue)
                                   : 1;
        entry.remote = def != nullptr && def->remote;
        entry.facilityDamage = std::clamp(region.facilityDamage, 0, 100);
        entry.refugees = std::max(0, region.refugees);
        entry.infectionCores = std::max(0, region.infectionCores);
        entry.recoveryStage = region.recoveryStage;
        entry.recoveryReadyMinute = region.recoveryReadyMinute;

        StrategicWarningInfo warning;
        if (DescribeStrategicWarning(region, config, out.gameMinute,
                                     &warning)) {
            entry.warning.valid = true;
            entry.warning.warningMinute = warning.warningMinute;
            entry.warning.attackMinute = warning.attackMinute;
            entry.warning.reportedAttackMinute =
                warning.reportedAttackMinute;
            entry.warning.warningMinutes = warning.warningMinutes;
            entry.warning.remainingMinutes = warning.remainingMinutes;
            entry.warning.intelLevel = warning.intelLevel;
            entry.warning.enemyStrength = warning.enemyStrength;
            entry.warning.reportedEnemyStrength =
                warning.reportedEnemyStrength;
            entry.warning.defenseScore = warning.defenseScore;
        }
        out.regions.push_back(std::move(entry));
    }

    const std::size_t count =
        std::min(maxEvents, state.events.size());
    out.events.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const StrategicEvent& event = state.events[state.events.size() - 1u - i];
        StrategicMapEvent entry;
        entry.id = event.id;
        entry.regionId = event.regionId;
        entry.kind = event.kind;
        entry.gameMinute = event.gameMinute;
        entry.ownerAfter = event.ownerAfter;
        entry.delta.people = event.people;
        entry.delta.supplies = event.supplies;
        entry.delta.ammo = event.ammo;
        entry.delta.fuel = event.fuel;
        out.events.push_back(std::move(entry));
    }
    return out;
}

StrategicMapState BuildStrategicMapStateFromSnapshot(
    const net::StrategicSnapshot& snapshot) {
    StrategicMapState out;
    out.enabled = snapshot.enabled;
    out.gameMinute = std::max<std::int64_t>(0, snapshot.gameMinute);
    out.tickMinutes = snapshot.tickMinutes;
    out.warningHours = snapshot.warningHours;
    out.remoteWarningHours = snapshot.remoteWarningHours;
    out.autoResolveGraceMinutes = snapshot.autoResolveGraceMinutes;
    out.resources.people = snapshot.people;
    out.resources.supplies = snapshot.supplies;
    out.resources.ammo = snapshot.ammo;
    out.resources.fuel = snapshot.fuel;
    out.regions.reserve(snapshot.regions.size());
    for (const net::StrategicRegionSnapshot& region : snapshot.regions) {
        StrategicMapRegion entry;
        entry.id = region.id;
        entry.owner = static_cast<StrategicOwner>(region.owner);
        entry.contestedFaction =
            static_cast<ContestedFaction>(region.contestedFaction);
        entry.siegePhase = static_cast<StrategicSiegePhase>(region.siegePhase);
        entry.pressure = region.pressure;
        entry.defense = region.defense;
        entry.supply = region.supply;
        entry.morale = region.morale;
        entry.population = region.population;
        entry.garrison = region.garrison;
        entry.fortification = region.fortification;
        entry.intelLevel = region.intelLevel;
        entry.strategicValue = region.strategicValue;
        entry.coreSafe = region.coreSafe;
        entry.remote = region.remote;
        entry.facilityDamage = region.facilityDamage;
        entry.refugees = region.refugees;
        entry.infectionCores = region.infectionCores;
        entry.recoveryStage =
            static_cast<StrategicRecoveryStage>(region.recoveryStage);
        entry.recoveryReadyMinute = region.recoveryReadyMinute;
        entry.warning.valid = region.warningValid;
        entry.warning.warningMinute = region.warningMinute;
        entry.warning.attackMinute = region.attackMinute;
        entry.warning.reportedAttackMinute = region.reportedAttackMinute;
        entry.warning.warningMinutes = region.warningMinutes;
        entry.warning.remainingMinutes = region.remainingMinutes;
        entry.warning.intelLevel = region.warningIntelLevel;
        entry.warning.enemyStrength = region.enemyStrength;
        entry.warning.reportedEnemyStrength = region.reportedEnemyStrength;
        entry.warning.defenseScore = region.warningDefenseScore;
        out.regions.push_back(std::move(entry));
    }
    out.events.reserve(snapshot.events.size());
    for (const net::StrategicEventSnapshot& event : snapshot.events) {
        StrategicMapEvent entry;
        entry.id = event.id;
        entry.regionId = event.regionId;
        entry.kind = static_cast<StrategicEventKind>(event.kind);
        entry.gameMinute = event.gameMinute;
        entry.ownerAfter = static_cast<StrategicOwner>(event.ownerAfter);
        entry.delta.people = event.people;
        entry.delta.supplies = event.supplies;
        entry.delta.ammo = event.ammo;
        entry.delta.fuel = event.fuel;
        out.events.push_back(std::move(entry));
    }
    return out;
}

}  // namespace zl::game
