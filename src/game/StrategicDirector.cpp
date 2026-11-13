#include "game/StrategicDirector.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace zl::game {
namespace {

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool Fail(std::string* error, std::string message) {
    SetError(error, std::move(message));
    return false;
}

std::uint64_t Mix(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31u);
}

std::uint64_t AttackSeed(std::uint64_t seed, std::string_view regionId,
                         std::int64_t attackMinute, std::uint64_t salt) {
    std::uint64_t hash = seed ^ (static_cast<std::uint64_t>(attackMinute) *
                                 0x9e3779b97f4a7c15ull);
    for (const unsigned char c : regionId) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ull;
    }
    return Mix(hash ^ salt);
}

std::string WorldEventKey(std::string_view eventId) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char c : eventId) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ull;
    }
    return "worldevent." + std::to_string(Mix(hash));
}

int WarningMinutes(const StrategicConfig& config,
                   const StrategicRegionState& region) {
    const StrategicRegionConfig* def = config.Find(region.id);
    const bool remote = def != nullptr && def->remote;
    const int hours = remote ? config.remoteWarningHours : config.warningHours;
    return std::max(1, hours) * 60;
}

int ReportedStrength(int actual, int intelLevel, std::uint64_t seed,
                     std::string_view regionId, std::int64_t attackMinute) {
    const int pct = StrategicIntelErrorPercent(intelLevel);
    if (pct == 0) {
        return actual;
    }
    const std::uint64_t roll =
        AttackSeed(seed, regionId, attackMinute, 0x535452454e475448ull);
    const int span = pct * 2 + 1;
    const int offset = static_cast<int>(roll % static_cast<std::uint64_t>(span)) -
                       pct;
    const double scaled =
        static_cast<double>(actual) * (100.0 + static_cast<double>(offset)) /
        100.0;
    return std::max(1, static_cast<int>(std::llround(scaled)));
}

std::int64_t ReportedAttack(std::int64_t attackMinute, int intelLevel,
                            std::uint64_t seed, std::string_view regionId) {
    const int span = StrategicIntelTimeErrorMinutes(intelLevel);
    if (span == 0) {
        return attackMinute;
    }
    const std::uint64_t roll =
        AttackSeed(seed, regionId, attackMinute, 0x54494d455354414dull);
    const int offset = static_cast<int>(roll % static_cast<std::uint64_t>(
                                                      span * 2 + 1)) -
                       span;
    return std::max<std::int64_t>(0, attackMinute + offset);
}

int EnemyStrength(float pressure, const StrategicConfig& config) {
    const double value =
        static_cast<double>(std::clamp(pressure, 0.0f, 100.0f)) *
        static_cast<double>(config.autoResolve.enemyStrengthPerPressure);
    return std::clamp(static_cast<int>(std::llround(value)), 1, 1000000);
}

struct DefenseBreakdown {
    int total = 0;
    int garrison = 0;
    int fortification = 0;
    int supply = 0;
};

DefenseBreakdown ComputeDefense(const StrategicRegionState& region,
                                const StrategicConfig& config) {
    DefenseBreakdown out;
    out.garrison = std::max(
        0, static_cast<int>(std::llround(
               static_cast<double>(region.garrison) *
               static_cast<double>(config.autoResolve.defensePerGarrison))));
    out.fortification = std::max(
        0, static_cast<int>(std::llround(
               static_cast<double>(region.fortification) *
               static_cast<double>(
                   config.autoResolve.defensePerFortification))));
    out.supply = std::max(
        0, static_cast<int>(std::llround(
               static_cast<double>(region.supply) *
               static_cast<double>(config.autoResolve.defensePerSupply))));
    const double value = static_cast<double>(region.defense) +
                         static_cast<double>(out.garrison) +
                         static_cast<double>(out.fortification) +
                         static_cast<double>(out.supply);
    out.total = std::clamp(static_cast<int>(std::llround(value)), 0, 1000000);
    return out;
}

int DefenseScore(const StrategicRegionState& region,
                 const StrategicConfig& config) {
    return ComputeDefense(region, config).total;
}

int BattleSeverity(int enemyStrength, int defenseScore) {
    if (defenseScore <= 0) {
        return 100;
    }
    const double ratio =
        static_cast<double>(std::max(0, enemyStrength)) /
        static_cast<double>(defenseScore);
    return std::clamp(static_cast<int>(std::llround(ratio * 100.0)), 0, 100);
}

struct BattleAttrition {
    int garrisonLoss = 0;
    int fortificationLoss = 0;
    int facilityDamage = 0;
    int regionSupplyLoss = 0;
};

BattleAttrition ComputeAttrition(const StrategicRegionState& region,
                                 const StrategicConfig& config, bool held,
                                 bool coreProtected, int enemyStrength,
                                 int defenseScore) {
    const StrategicAutoResolveConfig& autoResolve = config.autoResolve;
    BattleAttrition attrition;
    const int severity = BattleSeverity(enemyStrength, defenseScore);
    if (held) {
        if (region.garrison > 0 && severity > 0) {
            attrition.garrisonLoss = std::min(
                region.garrison,
                std::max(0, static_cast<int>(
                                std::ceil(static_cast<double>(severity) / 50.0))));
            attrition.garrisonLoss =
                std::min(attrition.garrisonLoss,
                         std::max(0, autoResolve.heldGarrisonLossMax));
        }
        attrition.fortificationLoss = std::min(
            region.fortification, std::max(0, autoResolve.heldFortificationLoss));
        attrition.facilityDamage = std::clamp(
            static_cast<int>(std::ceil(
                static_cast<double>(severity) *
                static_cast<double>(autoResolve.heldFacilityDamageMax) / 100.0)),
            0, 100);
        attrition.regionSupplyLoss =
            std::min(static_cast<int>(std::lround(region.supply)),
                     std::max(0, autoResolve.heldRegionSupplyLoss));
    } else if (coreProtected) {
        attrition.garrisonLoss = std::min(
            region.garrison,
            static_cast<int>(std::ceil(
                static_cast<double>(region.garrison) *
                static_cast<double>(autoResolve.coreGarrisonLossPercent) / 100.0)));
        attrition.fortificationLoss = region.fortification;
        attrition.facilityDamage = std::clamp(
            std::max(region.facilityDamage, autoResolve.coreFacilityDamage), 0,
            100);
        attrition.regionSupplyLoss = std::min(
            static_cast<int>(std::lround(region.supply)),
            std::max(0, autoResolve.coreRegionSupplyLoss));
    } else {
        attrition.garrisonLoss = region.garrison;
        attrition.fortificationLoss = region.fortification;
        attrition.facilityDamage = 100;
        attrition.regionSupplyLoss = std::min(
            static_cast<int>(std::lround(region.supply)),
            std::max(0, autoResolve.lostRegionSupplyLoss));
    }
    return attrition;
}

void ApplyAttrition(StrategicRegionState& region,
                    const BattleAttrition& attrition) {
    region.garrison = std::max(0, region.garrison - attrition.garrisonLoss);
    region.fortification =
        std::max(0, region.fortification - attrition.fortificationLoss);
    region.facilityDamage =
        std::clamp(std::max(region.facilityDamage, attrition.facilityDamage), 0,
                   100);
    region.supply = std::clamp(
        region.supply - static_cast<float>(attrition.regionSupplyLoss), 0.0f,
        100.0f);
}

bool HasPlayer(const StrategicTickInput& input, std::string_view regionId) {
    return std::find(input.playerRegions.begin(), input.playerRegions.end(),
                     regionId) != input.playerRegions.end();
}

std::string WarningEventId(const StrategicRegionState& region,
                           std::int64_t attackMinute) {
    return "warning." + region.id + "." + std::to_string(attackMinute);
}

std::string SiegeEventId(const StrategicRegionState& region,
                         std::int64_t attackMinute) {
    return "siege." + region.id + "." + std::to_string(attackMinute);
}

bool IssueWarning(StrategicState& state, const StrategicConfig& config,
                  StrategicRegionState& region, std::int64_t gameMinute,
                  StrategicTickReport* report, std::string* error) {
    const int warningMinutes = WarningMinutes(config, region);
    const std::int64_t attackMinute = gameMinute + warningMinutes;
    const std::string eventId = WarningEventId(region, attackMinute);
    if (state.FindEvent(eventId) != nullptr) {
        region.siegePhase = StrategicSiegePhase::Warning;
        region.nextAttackMinute = attackMinute;
        return true;
    }

    const float projectedPressure = std::clamp(
        region.pressure + config.pressurePerHour *
                              (static_cast<float>(warningMinutes) / 60.0f),
        0.0f, 100.0f);
    const int enemyStrength = EnemyStrength(projectedPressure, config);
    if (!state.ApplyEvent(region.id, StrategicEventKind::Warning, eventId,
                          gameMinute, region.owner, {}, error)) {
        return false;
    }
    region.siegePhase = StrategicSiegePhase::Warning;
    region.nextAttackMinute = attackMinute;

    if (report != nullptr) {
        const DefenseBreakdown defense = ComputeDefense(region, config);
        StrategicWarningInfo info;
        info.regionId = region.id;
        info.warningMinute = gameMinute;
        info.attackMinute = attackMinute;
        info.warningMinutes = warningMinutes;
        info.remainingMinutes = warningMinutes;
        info.intelLevel = std::clamp(region.intelLevel, 0, 3);
        info.enemyStrength = enemyStrength;
        info.reportedEnemyStrength =
            ReportedStrength(enemyStrength, info.intelLevel, config.seed,
                             region.id, attackMinute);
        info.reportedAttackMinute = ReportedAttack(
            attackMinute, info.intelLevel, config.seed, region.id);
        info.defenseScore = defense.total;
        info.defenseFromGarrison = defense.garrison;
        info.defenseFromFortification = defense.fortification;
        info.defenseFromSupply = defense.supply;
        report->warnings.push_back(std::move(info));
    }
    return true;
}

float AnchorDistanceSq(const StrategicConfig& config,
                       const StrategicRegionState& a,
                       const StrategicRegionState& b) {
    const StrategicRegionConfig* da = config.Find(a.id);
    const StrategicRegionConfig* db = config.Find(b.id);
    const float ax = da != nullptr && da->hasAnchor ? da->anchorX : 0.0f;
    const float ay = da != nullptr && da->hasAnchor ? da->anchorY : 0.0f;
    const float bx = db != nullptr && db->hasAnchor ? db->anchorX : 0.0f;
    const float by = db != nullptr && db->hasAnchor ? db->anchorY : 0.0f;
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

StrategicRegionState* FindRefugeeHost(StrategicState& state,
                                      const StrategicConfig& config,
                                      const StrategicRegionState& lost) {
    const StrategicRegionConfig* def = config.Find(lost.id);
    if (def != nullptr && !def->refugeeTarget.empty()) {
        StrategicRegionState* target = state.FindRegion(def->refugeeTarget);
        if (target != nullptr && target != &lost &&
            target->owner == StrategicOwner::Human) {
            return target;
        }
    }
    StrategicRegionState* best = nullptr;
    float bestDistance = std::numeric_limits<float>::max();
    for (StrategicRegionState& candidate : state.regions) {
        if (&candidate == &lost || candidate.owner != StrategicOwner::Human) {
            continue;
        }
        const float distance = AnchorDistanceSq(config, lost, candidate);
        if (distance < bestDistance ||
            (distance == bestDistance &&
             (best == nullptr || candidate.id < best->id))) {
            bestDistance = distance;
            best = &candidate;
        }
    }
    return best;
}

bool BeginRecovery(StrategicState& state, const StrategicConfig& config,
                   StrategicRegionState& region, std::int64_t gameMinute,
                   std::string* error) {
    const StrategicRecoveryConfig& recovery = config.recovery;
    const int evacuees = std::max(0, region.population - 1) + region.refugees;
    StrategicRegionState* host = FindRefugeeHost(state, config, region);
    region.refugees = 0;
    region.refugeeHost.clear();
    if (host != nullptr) {
        host->refugees = std::clamp(host->refugees + evacuees, 0, 1000000);
        host->pressure = std::clamp(
            host->pressure + static_cast<float>(evacuees) *
                                 static_cast<float>(
                                     recovery.refugeePressurePerPerson),
            0.0f, 100.0f);
        host->supply = std::clamp(
            host->supply - static_cast<float>(evacuees) *
                               static_cast<float>(
                                   recovery.refugeeSupplyPerPerson),
            0.0f, 100.0f);
        const std::string eventId =
            "refugee." + region.id + "." + std::to_string(gameMinute);
        if (state.FindEvent(eventId) == nullptr &&
            !state.ApplyEvent(host->id, StrategicEventKind::Refugee, eventId,
                              gameMinute, host->owner, {}, error)) {
            return false;
        }
        region.refugeeHost = host->id;
    }

    region.facilityDamage = 100;
    region.infectionCores = std::max(0, recovery.coreCount);
    region.recoveryStage = region.infectionCores > 0
                               ? StrategicRecoveryStage::ClearCore
                               : StrategicRecoveryStage::Repair;
    region.recoveryReadyMinute =
        gameMinute + std::max(0, recovery.delayMinutes);
    region.population = 0;
    region.garrison = 0;
    return true;
}

bool FinalizeRecovery(StrategicState& state, const StrategicConfig& config,
                      StrategicRegionState& region,
                      std::int64_t gameMinute,
                      StrategicTickReport* report, std::string* error) {
    if (region.recoveryStage != StrategicRecoveryStage::Restoring ||
        region.recoveryReadyMinute < 0 ||
        gameMinute < region.recoveryReadyMinute) {
        return true;
    }
    const StrategicRecoveryConfig& recovery = config.recovery;
    int returned = 0;
    StrategicRegionState* host = region.refugeeHost.empty()
                                     ? nullptr
                                     : state.FindRegion(region.refugeeHost);
    if (host != nullptr && host != &region) {
        returned = std::min(
            host->refugees,
            std::min(std::max(0, recovery.reclaimPopulation), 1000000));
        host->refugees = std::max(0, host->refugees - returned);
        host->pressure = std::clamp(
            host->pressure - static_cast<float>(returned) *
                                 static_cast<float>(
                                     recovery.refugeePressurePerPerson),
            0.0f, 100.0f);
    }
    const int basePopulation = std::max(region.population,
                                        std::max(0, recovery.reclaimPopulation));
    const StrategicResources reward{recovery.reclaimPeople,
                                    recovery.reclaimSupplies,
                                    recovery.reclaimAmmo,
                                    recovery.reclaimFuel};
    const std::string eventId =
        "recovery." + region.id + "." + std::to_string(gameMinute);
    if (!state.ApplyEvent(region.id, StrategicEventKind::Recovery, eventId,
                          gameMinute, StrategicOwner::Human, reward, error)) {
        return false;
    }
    region.owner = StrategicOwner::Human;
    region.facilityDamage = 0;
    region.infectionCores = 0;
    region.recoveryStage = StrategicRecoveryStage::None;
    region.recoveryReadyMinute = -1;
    region.population = basePopulation + returned;
    region.garrison = std::max(region.garrison,
                               std::max(0, recovery.reclaimGarrison));
    region.refugeeHost.clear();
    region.nextAttackMinute = -1;
    region.siegePhase = StrategicSiegePhase::None;
    region.pressure = std::max(0.0f, region.pressure - 20.0f);
    if (report != nullptr) {
        report->completedRecoveries.push_back(region.id);
    }
    return true;
}

void AddClamped(StrategicResources* resources, const StrategicResources& delta) {
    const auto add = [](int* value, int change) {
        *value = std::clamp(*value + change, 0, 1000000);
    };
    add(&resources->people, delta.people);
    add(&resources->supplies, delta.supplies);
    add(&resources->ammo, delta.ammo);
    add(&resources->fuel, delta.fuel);
}

void ApplyDailyEconomy(StrategicState& state, const StrategicConfig& config,
                       std::int64_t gameMinute, StrategicTickReport* report) {
    const StrategicEconomyConfig& economy = config.economy;
    if (!economy.enabled || economy.intervalMinutes <= 0) {
        return;
    }
    const std::int64_t lastDay = state.lastTickMinute / economy.intervalMinutes;
    const std::int64_t currentDay = gameMinute / economy.intervalMinutes;
    if (currentDay <= lastDay) {
        return;
    }
    const int days = static_cast<int>(std::clamp<std::int64_t>(
        currentDay - lastDay, 1, 30));
    StrategicEconomyInfo info;
    info.day = static_cast<int>(currentDay);
    info.days = days;

    int produced = 0;
    int population = 0;
    int garrison = 0;
    int refugees = 0;
    for (StrategicRegionState& region : state.regions) {
        if (region.owner == StrategicOwner::Human) {
            const StrategicRegionConfig* def = config.Find(region.id);
            if (def != nullptr) {
                produced += std::max(0, def->strategicValue) *
                            std::max(0, economy.suppliesPerStrategicValue) * days;
            }
        }
        if (region.owner == StrategicOwner::Human ||
            region.owner == StrategicOwner::Contested) {
            population += std::max(0, region.population);
            garrison += std::max(0, region.garrison);
            refugees += std::max(0, region.refugees);
        }
    }
    const std::int64_t consumed64 =
        (static_cast<std::int64_t>(population) *
             std::max(0, economy.supplyCostPerPopulation) +
         static_cast<std::int64_t>(garrison) *
             std::max(0, economy.supplyCostPerGarrison) +
         static_cast<std::int64_t>(refugees) *
             std::max(0, economy.supplyCostPerRefugee)) *
        days;
    const int consumed = static_cast<int>(std::clamp<std::int64_t>(
        consumed64, 0, std::numeric_limits<int>::max()));
    const int available =
        std::max(0, state.resources.supplies) + std::max(0, produced);
    const int shortage = std::max(0, consumed - available);
    state.resources.supplies = std::max(0, available - consumed);
    state.resources.supplies =
        std::min(state.resources.supplies, 1000000);
    info.producedSupplies = produced;
    info.consumedSupplies = std::min(consumed, available);
    info.shortageSupplies = shortage;

    if (shortage > 0) {
        for (StrategicRegionState& region : state.regions) {
            if (region.owner != StrategicOwner::Human &&
                region.owner != StrategicOwner::Contested) {
                continue;
            }
            region.pressure = std::clamp(
                region.pressure +
                    static_cast<float>(shortage) *
                        std::max(0.0f, economy.shortagePressurePerSupply),
                0.0f, 100.0f);
            region.morale = std::clamp(
                region.morale -
                    static_cast<float>(shortage) *
                        std::max(0.0f, economy.shortageMoralePerSupply),
                0.0f, 100.0f);
        }
    }

    const int restorePerRegion =
        std::max(0, economy.regionSupplyRestorePerDay) * days;
    for (StrategicRegionState& region : state.regions) {
        if (region.owner != StrategicOwner::Human || restorePerRegion <= 0 ||
            state.resources.supplies <= 0) {
            continue;
        }
        const int restore = std::min(
            {restorePerRegion,
             std::max(0, static_cast<int>(std::lround(100.0f - region.supply))),
             state.resources.supplies});
        region.supply =
            std::clamp(region.supply + static_cast<float>(restore), 0.0f, 100.0f);
        state.resources.supplies -= restore;
        info.restoredRegionSupply += restore;
    }

    int reserve = std::max(0, economy.tradeReserveSupplies);
    int tradeBudget = std::max(0, state.resources.supplies - reserve);
    const int ammoCost = std::max(1, economy.tradeSuppliesPerAmmo);
    const int ammoDesired = std::max(0, economy.tradeAmmoPerDay) * days;
    const int ammo = std::min(ammoDesired, tradeBudget / ammoCost);
    if (ammo > 0) {
        tradeBudget -= ammo * ammoCost;
        state.resources.supplies -= ammo * ammoCost;
        AddClamped(&state.resources, StrategicResources{0, 0, ammo, 0});
        info.tradedAmmo = ammo;
    }
    const int fuelCost = std::max(1, economy.tradeSuppliesPerFuel);
    const int fuelDesired = std::max(0, economy.tradeFuelPerDay) * days;
    const int fuel = std::min(fuelDesired, tradeBudget / fuelCost);
    if (fuel > 0) {
        state.resources.supplies -= fuel * fuelCost;
        AddClamped(&state.resources, StrategicResources{0, 0, 0, fuel});
        info.tradedFuel = fuel;
    }

    const int repairPerRegion =
        std::max(0, economy.fortificationRepairPerDay) * days;
    if (repairPerRegion > 0) {
        for (StrategicRegionState& region : state.regions) {
            if (region.owner != StrategicOwner::Human ||
                region.siegePhase != StrategicSiegePhase::None ||
                region.garrison <= 0) {
                continue;
            }
            const StrategicRegionConfig* def = config.Find(region.id);
            const int maxFortification =
                def != nullptr ? std::max(0, def->fortification) : 0;
            const int repaired = std::max(
                0, std::min(repairPerRegion, maxFortification - region.fortification));
            region.fortification += repaired;
            info.repairedFortification += repaired;
        }
    }
    if (report != nullptr) {
        report->economy.push_back(info);
    }
}

bool ResolveAuto(StrategicState& state, const StrategicConfig& config,
                 StrategicRegionState& region, std::int64_t gameMinute,
                 StrategicTickReport* report, std::string* error) {
    const std::int64_t attackMinute = region.nextAttackMinute;
    if (attackMinute < 0) {
        return Fail(error, "战略区域缺少围攻时间：" + region.id);
    }
    const std::string eventId = SiegeEventId(region, attackMinute);
    if (state.FindEvent(eventId) != nullptr) {
        region.siegePhase = StrategicSiegePhase::None;
        region.nextAttackMinute = -1;
        return true;
    }

    const DefenseBreakdown defense = ComputeDefense(region, config);
    const int enemyStrength = EnemyStrength(region.pressure, config);
    const bool held = defense.total >= enemyStrength;
    const bool coreProtected = !held && region.coreSafe;
    StrategicOwner ownerAfter = held ? StrategicOwner::Human
                                     : (coreProtected
                                            ? StrategicOwner::Contested
                                            : StrategicOwner::Infested);
    if (held && region.owner == StrategicOwner::Contested) {
        ownerAfter = StrategicOwner::Human;
    }

    const int garrisonBefore = region.garrison;
    const int populationBefore = region.population;
    const int fortificationBefore = region.fortification;
    const int facilityDamageBefore = region.facilityDamage;
    const int regionSupplyBefore =
        static_cast<int>(std::lround(region.supply));
    const BattleAttrition attrition =
        ComputeAttrition(region, config, held, coreProtected, enemyStrength,
                         defense.total);

    StrategicResources delta;
    if (held) {
        delta.people = config.autoResolve.heldPeople;
        delta.supplies = config.autoResolve.heldSupplies;
        delta.ammo = config.autoResolve.heldAmmo;
        delta.fuel = config.autoResolve.heldFuel;
    } else {
        delta.people = config.autoResolve.lostPeople;
        delta.supplies = config.autoResolve.lostSupplies;
        delta.ammo = config.autoResolve.lostAmmo;
        delta.fuel = config.autoResolve.lostFuel;
    }

    if (!state.ApplyEvent(region.id, StrategicEventKind::Siege, eventId,
                          gameMinute, ownerAfter, delta, error)) {
        return false;
    }
    ApplyAttrition(region, attrition);

    region.pressure =
        std::max(0.0f, region.pressure -
                           (held ? config.autoResolve.heldPressureRelief
                                 : config.autoResolve.lostPressureRelief));
    region.siegePhase = StrategicSiegePhase::None;
    region.nextAttackMinute = -1;
    if (!held && !coreProtected) {
        if (!BeginRecovery(state, config, region, gameMinute, error)) {
            return false;
        }
    }

    if (report != nullptr) {
        StrategicResolutionInfo info;
        info.regionId = region.id;
        info.attackMinute = attackMinute;
        info.resolvedMinute = gameMinute;
        info.defenseScore = defense.total;
        info.defenseFromGarrison = defense.garrison;
        info.defenseFromFortification = defense.fortification;
        info.defenseFromSupply = defense.supply;
        info.enemyStrength = enemyStrength;
        info.held = held;
        info.coreProtected = coreProtected;
        info.ownerAfter = ownerAfter;
        info.resourceDelta = delta;
        info.garrisonBefore = garrisonBefore;
        info.garrisonAfter = region.garrison;
        info.populationBefore = populationBefore;
        info.populationAfter = region.population;
        info.fortificationBefore = fortificationBefore;
        info.fortificationAfter = region.fortification;
        info.facilityDamageBefore = facilityDamageBefore;
        info.facilityDamageAfter = region.facilityDamage;
        info.regionSupplyBefore = regionSupplyBefore;
        info.regionSupplyAfter = static_cast<int>(std::lround(region.supply));
        report->resolutions.push_back(std::move(info));
    }

    if (coreProtected) {
        if (!IssueWarning(state, config, region, gameMinute, report, error)) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool ResolveStrategicSiege(StrategicState& state, const StrategicConfig& config,
                           const StrategicDetailedResolution& resolution,
                           std::string* error) {
    if (!config.enabled) {
        SetError(error, {});
        return true;
    }
    StrategicRegionState* region = state.FindRegion(resolution.regionId);
    if (region == nullptr) {
        return Fail(error, "详细围攻区域不存在：" + resolution.regionId);
    }
    if (resolution.resolvedMinute < 0) {
        return Fail(error, "详细围攻结算时间非法：" + resolution.regionId);
    }
    const std::int64_t attackMinute =
        region->nextAttackMinute >= 0 ? region->nextAttackMinute
                                      : resolution.attackMinute;
    if (attackMinute < 0) {
        return Fail(error, "详细围攻缺少攻击时间：" + resolution.regionId);
    }
    const std::string eventId = SiegeEventId(*region, attackMinute);
    if (state.FindEvent(eventId) != nullptr) {
        region->siegePhase = StrategicSiegePhase::None;
        region->nextAttackMinute = -1;
        SetError(error, {});
        return true;
    }
    if (region->siegePhase != StrategicSiegePhase::Active &&
        region->siegePhase != StrategicSiegePhase::Warning) {
        return Fail(error, "详细围攻区域阶段非法：" + resolution.regionId);
    }

    const bool held = resolution.held;
    const bool coreProtected = !held && region->coreSafe;
    StrategicOwner ownerAfter = held
                                    ? StrategicOwner::Human
                                    : (coreProtected
                                           ? StrategicOwner::Contested
                                           : StrategicOwner::Infested);
    if (held && region->owner == StrategicOwner::Contested) {
        ownerAfter = StrategicOwner::Human;
    }

    const DefenseBreakdown defense = ComputeDefense(*region, config);
    const int enemyStrength = EnemyStrength(region->pressure, config);
    const BattleAttrition attrition =
        ComputeAttrition(*region, config, held, coreProtected, enemyStrength,
                         defense.total);
    StrategicResources delta;
    if (held) {
        delta.people = config.autoResolve.heldPeople;
        delta.supplies = config.autoResolve.heldSupplies;
        delta.ammo = config.autoResolve.heldAmmo;
        delta.fuel = config.autoResolve.heldFuel;
    } else {
        delta.people = config.autoResolve.lostPeople;
        delta.supplies = config.autoResolve.lostSupplies;
        delta.ammo = config.autoResolve.lostAmmo;
        delta.fuel = config.autoResolve.lostFuel;
    }

    if (!state.ApplyEvent(region->id, StrategicEventKind::Siege, eventId,
                          resolution.resolvedMinute, ownerAfter, delta,
                          error)) {
        return false;
    }
    ApplyAttrition(*region, attrition);

    region->pressure =
        std::max(0.0f, region->pressure -
                           (held ? config.autoResolve.heldPressureRelief
                                 : config.autoResolve.lostPressureRelief));
    region->siegePhase = StrategicSiegePhase::None;
    region->nextAttackMinute = -1;
    if (!held && !coreProtected) {
        if (!BeginRecovery(state, config, *region, resolution.resolvedMinute,
                           error)) {
            return false;
        }
    }
    if (coreProtected) {
        if (!IssueWarning(state, config, *region, resolution.resolvedMinute,
                          nullptr, error)) {
            return false;
        }
    }
    SetError(error, {});
    return true;
}

bool ClearStrategicRecoveryCore(StrategicState& state,
                                const StrategicConfig& config,
                                std::string_view regionId,
                                std::int64_t gameMinute,
                                std::string* error) {
    if (!config.enabled) {
        SetError(error, {});
        return true;
    }
    StrategicRegionState* region = state.FindRegion(regionId);
    if (region == nullptr) {
        return Fail(error, "反攻区域不存在：" + std::string(regionId));
    }
    if (region->owner != StrategicOwner::Infested ||
        region->recoveryStage != StrategicRecoveryStage::ClearCore) {
        return Fail(error, "区域当前不在清理感染核心阶段");
    }
    if (gameMinute < 0 || gameMinute < region->recoveryReadyMinute) {
        return Fail(error, "反攻窗口尚未开启");
    }
    if (region->infectionCores <= 0) {
        return Fail(error, "区域没有感染核心");
    }
    const int cost = std::max(0, config.recovery.coreAmmoCost);
    if (state.resources.ammo < cost) {
        return Fail(error, "清理感染核心所需弹药不足");
    }
    const std::string eventId =
        "recovery.core." + region->id + "." + std::to_string(gameMinute);
    StrategicResources delta;
    delta.ammo = -cost;
    if (state.FindEvent(eventId) == nullptr &&
        !state.ApplyEvent(region->id, StrategicEventKind::Recovery, eventId,
                          gameMinute, region->owner, delta, error)) {
        return false;
    }
    region->infectionCores = 0;
    region->recoveryStage = StrategicRecoveryStage::Repair;
    region->recoveryReadyMinute = gameMinute;
    SetError(error, {});
    return true;
}

bool RepairStrategicRecoveryFacility(StrategicState& state,
                                     const StrategicConfig& config,
                                     std::string_view regionId,
                                     std::int64_t gameMinute,
                                     std::string* error) {
    if (!config.enabled) {
        SetError(error, {});
        return true;
    }
    StrategicRegionState* region = state.FindRegion(regionId);
    if (region == nullptr) {
        return Fail(error, "反攻区域不存在：" + std::string(regionId));
    }
    if (region->owner != StrategicOwner::Infested ||
        region->recoveryStage != StrategicRecoveryStage::Repair) {
        return Fail(error, "区域当前不在修复设施阶段");
    }
    if (gameMinute < 0 || gameMinute < region->recoveryReadyMinute) {
        return Fail(error, "反攻窗口尚未开启");
    }
    const int supplies = std::max(0, config.recovery.repairSupplies);
    const int ammo = std::max(0, config.recovery.repairAmmo);
    if (state.resources.supplies < supplies || state.resources.ammo < ammo) {
        return Fail(error, "修复设施所需补给/弹药不足");
    }
    const std::string eventId =
        "recovery.repair." + region->id + "." + std::to_string(gameMinute);
    StrategicResources delta;
    delta.supplies = -supplies;
    delta.ammo = -ammo;
    if (state.FindEvent(eventId) == nullptr &&
        !state.ApplyEvent(region->id, StrategicEventKind::Recovery, eventId,
                          gameMinute, region->owner, delta, error)) {
        return false;
    }
    region->facilityDamage = 0;
    region->recoveryStage = StrategicRecoveryStage::Counterattack;
    region->recoveryReadyMinute = gameMinute;
    SetError(error, {});
    return true;
}

bool ResolveStrategicReclaim(StrategicState& state,
                             const StrategicConfig& config,
                             const StrategicReclaimResolution& resolution,
                             std::string* error) {
    if (!config.enabled) {
        SetError(error, {});
        return true;
    }
    StrategicRegionState* region = state.FindRegion(resolution.regionId);
    if (region == nullptr) {
        return Fail(error, "反攻区域不存在：" + resolution.regionId);
    }
    const std::string eventId =
        "reclaim." + region->id + "." + std::to_string(resolution.attackMinute);
    if (const StrategicEvent* existing = state.FindEvent(eventId);
        existing != nullptr) {
        if (existing->ownerAfter == StrategicOwner::Contested) {
            region->owner = StrategicOwner::Contested;
            region->recoveryStage = StrategicRecoveryStage::Restoring;
            region->recoveryReadyMinute =
                existing->gameMinute + std::max(0, config.recovery.restoreMinutes);
        } else {
            region->recoveryStage = StrategicRecoveryStage::Counterattack;
            region->recoveryReadyMinute =
                existing->gameMinute + std::max(0, config.recovery.retryMinutes);
        }
        SetError(error, {});
        return true;
    }
    if (region->owner != StrategicOwner::Infested ||
        region->recoveryStage != StrategicRecoveryStage::Counterattack) {
        return Fail(error, "区域当前不在反攻波次阶段");
    }
    if (resolution.resolvedMinute < 0) {
        return Fail(error, "反攻结算时间非法");
    }
    const bool held = resolution.held;
    if (held) {
        const StrategicResources reward{config.recovery.reclaimPeople,
                                        config.recovery.reclaimSupplies,
                                        config.recovery.reclaimAmmo,
                                        config.recovery.reclaimFuel};
        if (!state.ApplyEvent(region->id, StrategicEventKind::Recovery, eventId,
                              resolution.resolvedMinute,
                              StrategicOwner::Contested, reward, error)) {
            return false;
        }
        region->owner = StrategicOwner::Contested;
        region->facilityDamage = 0;
        region->infectionCores = 0;
        region->recoveryStage = StrategicRecoveryStage::Restoring;
        region->recoveryReadyMinute =
            resolution.resolvedMinute + std::max(0, config.recovery.restoreMinutes);
        region->siegePhase = StrategicSiegePhase::None;
        region->nextAttackMinute = -1;
    } else {
        const std::string failId =
            "reclaim.fail." + region->id + "." +
            std::to_string(resolution.resolvedMinute);
        if (state.FindEvent(failId) == nullptr &&
            !state.ApplyEvent(region->id, StrategicEventKind::Recovery, failId,
                              resolution.resolvedMinute, region->owner, {},
                              error)) {
            return false;
        }
        region->recoveryStage = StrategicRecoveryStage::Counterattack;
        region->recoveryReadyMinute =
            resolution.resolvedMinute + std::max(0, config.recovery.retryMinutes);
        region->siegePhase = StrategicSiegePhase::None;
        region->nextAttackMinute = -1;
    }
    SetError(error, {});
    return true;
}

bool ApplyStrategicMissionReward(StrategicState& state,
                                 const StrategicConfig& config,
                                 std::string_view missionId,
                                 std::int64_t gameMinute,
                                 std::string* error) {
    if (!config.enabled) {
        SetError(error, {});
        return true;
    }
    const StrategicMissionRewardConfig* reward =
        config.economy.FindMission(missionId);
    if (reward == nullptr) {
        SetError(error, {});
        return true;
    }
    if (gameMinute < 0) {
        return Fail(error, "战略任务奖励时间非法");
    }
    const std::string eventId = "mission." + std::string(missionId);
    if (state.FindEvent(eventId) != nullptr) {
        SetError(error, {});
        return true;
    }
    const std::string regionId =
        reward->regionId.empty() ? std::string("global") : reward->regionId;
    StrategicRegionState* region =
        reward->regionId.empty() ? nullptr : state.FindRegion(reward->regionId);
    if (!reward->regionId.empty() && region == nullptr) {
        return Fail(error, "战略任务奖励区域不存在：" + reward->regionId);
    }
    const StrategicOwner ownerAfter =
        region != nullptr ? region->owner : StrategicOwner::Neutral;
    if (!state.ApplyEvent(regionId, StrategicEventKind::Resource, eventId,
                          gameMinute, ownerAfter, reward->resources, error)) {
        return false;
    }
    if (region != nullptr) {
        region->supply = std::clamp(
            region->supply + static_cast<float>(reward->regionSupply), 0.0f,
            100.0f);
        region->garrison =
            std::clamp(region->garrison + reward->regionGarrison, 0, 1000000);
        region->population =
            std::clamp(region->population + reward->regionPopulation, 0,
                       1000000);
        region->pressure = std::clamp(
            region->pressure - static_cast<float>(reward->pressureRelief),
            0.0f, 100.0f);
    }
    int remainingRefugees = std::max(0, reward->relieveRefugees);
    const auto relieve = [&](StrategicRegionState* host) {
        if (host == nullptr || remainingRefugees <= 0 || host->refugees <= 0) {
            return;
        }
        const int relieved = std::min(host->refugees, remainingRefugees);
        host->refugees -= relieved;
        host->pressure = std::clamp(
            host->pressure -
                static_cast<float>(relieved) *
                    static_cast<float>(
                        config.recovery.refugeePressurePerPerson),
            0.0f, 100.0f);
        remainingRefugees -= relieved;
    };
    relieve(region);
    if (remainingRefugees > 0) {
        for (StrategicRegionState& host : state.regions) {
            relieve(&host);
            if (remainingRefugees <= 0) {
                break;
            }
        }
    }
    SetError(error, {});
    return true;
}

bool ApplyStrategicWorldEventEffect(
    StrategicState& state, const StrategicConfig& config,
    std::string_view regionId, int pressureDelta,
    std::string_view worldEventId, std::int64_t gameMinute,
    StrategicWorldEventResult* out, std::string* error) {
    StrategicWorldEventResult local;
    StrategicWorldEventResult* result = out != nullptr ? out : &local;
    *result = {};
    if (!config.enabled) {
        SetError(error, {});
        return true;
    }
    if (worldEventId.empty() || gameMinute < 0 || pressureDelta < -100 ||
        pressureDelta > 100) {
        return Fail(error, "世界事件战略效果参数非法");
    }
    StrategicRegionState* region = state.FindRegion(regionId);
    if (region == nullptr) {
        SetError(error, {});
        return true;
    }
    result->pressureBefore = region->pressure;
    result->pressureAfter = region->pressure;
    if (pressureDelta == 0) {
        SetError(error, {});
        return true;
    }
    const std::string eventId = WorldEventKey(worldEventId);
    if (state.FindEvent(eventId) != nullptr) {
        result->alreadyApplied = true;
        SetError(error, {});
        return true;
    }
    if (!state.ApplyEvent(regionId, StrategicEventKind::Resource, eventId,
                          gameMinute, region->owner, {}, error)) {
        return false;
    }
    region->pressure = std::clamp(
        region->pressure + static_cast<float>(pressureDelta), 0.0f, 100.0f);
    result->applied = true;
    result->pressureAfter = region->pressure;
    SetError(error, {});
    return true;
}

bool DescribeStrategicWarning(const StrategicRegionState& region,
                              const StrategicConfig& config,
                              std::int64_t gameMinute,
                              StrategicWarningInfo* out) {
    if (out == nullptr) {
        return false;
    }
    *out = StrategicWarningInfo{};
    if (region.siegePhase != StrategicSiegePhase::Warning ||
        region.nextAttackMinute < 0) {
        return false;
    }

    const int warningMinutes = WarningMinutes(config, region);
    const std::int64_t attackMinute = region.nextAttackMinute;
    const std::int64_t warningMinute =
        std::max<std::int64_t>(0, attackMinute - warningMinutes);
    const int remainingMinutes = static_cast<int>(
        std::clamp<std::int64_t>(attackMinute - gameMinute, 0,
                                 static_cast<std::int64_t>(warningMinutes)));
    const float projectedPressure = std::clamp(
        region.pressure +
            config.pressurePerHour *
                (static_cast<float>(remainingMinutes) / 60.0f),
        0.0f, 100.0f);
    const int enemyStrength = EnemyStrength(projectedPressure, config);
    const DefenseBreakdown defense = ComputeDefense(region, config);

    out->regionId = region.id;
    out->warningMinute = warningMinute;
    out->attackMinute = attackMinute;
    out->warningMinutes = warningMinutes;
    out->remainingMinutes = remainingMinutes;
    out->intelLevel = std::clamp(region.intelLevel, 0, 3);
    out->enemyStrength = enemyStrength;
    out->reportedEnemyStrength = ReportedStrength(
        enemyStrength, out->intelLevel, config.seed, region.id, attackMinute);
    out->reportedAttackMinute =
        ReportedAttack(attackMinute, out->intelLevel, config.seed, region.id);
    out->defenseScore = defense.total;
    out->defenseFromGarrison = defense.garrison;
    out->defenseFromFortification = defense.fortification;
    out->defenseFromSupply = defense.supply;
    return true;
}

bool AdvanceStrategic(StrategicState& state, const StrategicConfig& config,
                      const StrategicTickInput& input,
                      StrategicTickReport* report, std::string* error) {
    if (report != nullptr) {
        *report = StrategicTickReport{};
    }
    if (!config.enabled) {
        SetError(error, {});
        return true;
    }
    if (config.tickMinutes <= 0 || !std::isfinite(config.pressurePerHour) ||
        !std::isfinite(config.attackPressure) ||
        config.pressurePerHour < 0.0f || config.attackPressure < 0.0f ||
        config.attackPressure > 100.0f ||
        !std::isfinite(config.autoResolve.defensePerGarrison) ||
        !std::isfinite(config.autoResolve.defensePerSupply) ||
        !std::isfinite(config.autoResolve.enemyStrengthPerPressure) ||
        !std::isfinite(config.autoResolve.heldPressureRelief) ||
        !std::isfinite(config.autoResolve.lostPressureRelief) ||
        !std::isfinite(config.autoResolve.defensePerFortification) ||
        config.autoResolve.defensePerFortification < 0.0f ||
        config.autoResolve.heldGarrisonLossMax < 0 ||
        config.autoResolve.heldFortificationLoss < 0 ||
        config.autoResolve.heldFacilityDamageMax < 0 ||
        config.autoResolve.coreFacilityDamage < 0 ||
        config.autoResolve.coreGarrisonLossPercent < 0 ||
        !std::isfinite(config.economy.shortagePressurePerSupply) ||
        !std::isfinite(config.economy.shortageMoralePerSupply) ||
        config.economy.shortagePressurePerSupply < 0.0f ||
        config.economy.shortageMoralePerSupply < 0.0f) {
        return Fail(error, "战略配置 tick/压力/结算参数非法");
    }
    if (input.gameMinute < 0 || input.gameMinute < state.lastTickMinute) {
        return Fail(error, "战略 tick 时间倒退");
    }
    if (input.gameMinute - state.lastTickMinute < config.tickMinutes) {
        SetError(error, {});
        return true;
    }
    if (!state.Validate(error)) {
        return false;
    }

    const double elapsedHours =
        static_cast<double>(input.gameMinute - state.lastTickMinute) / 60.0;
    if (elapsedHours > 0.0) {
        for (StrategicRegionState& region : state.regions) {
            if (region.owner == StrategicOwner::Neutral ||
                region.owner == StrategicOwner::Infested) {
                continue;
            }
            region.pressure = std::clamp(
                region.pressure + config.pressurePerHour *
                                      static_cast<float>(elapsedHours),
                0.0f, 100.0f);
        }
    }
    ApplyDailyEconomy(state, config, input.gameMinute, report);

    for (StrategicRegionState& region : state.regions) {
        if (region.recoveryStage == StrategicRecoveryStage::Restoring) {
            if (!FinalizeRecovery(state, config, region, input.gameMinute,
                                  report, error)) {
                return false;
            }
            // 恢复完成的同一 tick 不再立刻生成下一次预警；下次战略 tick 再判。
            continue;
        }
        if (region.recoveryStage == StrategicRecoveryStage::Counterattack) {
            if (input.detailedEnabled && HasPlayer(input, region.id) &&
                region.recoveryReadyMinute >= 0 &&
                input.gameMinute >= region.recoveryReadyMinute) {
                if (report != nullptr) {
                    report->recoveryRegions.push_back(region.id);
                }
            }
            continue;
        }
        if (region.recoveryStage == StrategicRecoveryStage::ClearCore ||
            region.recoveryStage == StrategicRecoveryStage::Repair) {
            continue;
        }
        if (region.owner == StrategicOwner::Neutral ||
            region.owner == StrategicOwner::Infested) {
            if (region.siegePhase != StrategicSiegePhase::None) {
                region.siegePhase = StrategicSiegePhase::None;
                region.nextAttackMinute = -1;
            }
            continue;
        }

        if (region.siegePhase == StrategicSiegePhase::Resolving ||
            region.siegePhase == StrategicSiegePhase::Recovering) {
            continue;
        }
        if (region.siegePhase == StrategicSiegePhase::Warning ||
            region.siegePhase == StrategicSiegePhase::Active) {
            if (region.nextAttackMinute < 0) {
                return Fail(error, "战略区域缺少围攻时间：" + region.id);
            }
            if (input.gameMinute < region.nextAttackMinute) {
                continue;
            }

            const bool playerPresent = HasPlayer(input, region.id);
            if (input.detailedEnabled && playerPresent) {
                if (region.siegePhase == StrategicSiegePhase::Warning) {
                    region.siegePhase = StrategicSiegePhase::Active;
                    region.lastEventMinute = input.gameMinute;
                }
                // Active + 玩家回到区域时也重新上报，WorldSimulation 用
                // 控制器是否已运行做幂等；这样离开后返回可重新进入详细围攻。
                if (report != nullptr) {
                    report->detailedRegions.push_back(region.id);
                }
                continue;
            }

            const std::int64_t graceStart =
                std::max(region.nextAttackMinute, region.lastEventMinute);
            const std::int64_t graceEnd =
                graceStart + std::max(0, config.autoResolveGraceMinutes);
            if (region.siegePhase == StrategicSiegePhase::Active) {
                if (input.gameMinute < graceEnd) {
                    continue;
                }
            } else if (!input.anyPlayerOnline &&
                       input.gameMinute < graceEnd) {
                continue;
            }

            if (!ResolveAuto(state, config, region, input.gameMinute, report,
                             error)) {
                return false;
            }
            continue;
        }

        if (region.pressure >= config.attackPressure) {
            if (!IssueWarning(state, config, region, input.gameMinute, report,
                              error)) {
                return false;
            }
        }
    }

    if (!state.AdvanceTick(input.gameMinute, error)) {
        return false;
    }
    SetError(error, {});
    return true;
}

}  // namespace zl::game
