#include "save/ComponentRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/AnimalComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/components/ProgressionComponents.h"
#include "ecs/components/CommonComponents.h"
#include "game/Companion.h"
#include "game/FireComponents.h"
#include "game/ForageComponents.h"
#include "game/HomesteadComponents.h"
#include "game/Objectives.h"
#include "game/MissionComponents.h"
#include "game/PoiComponents.h"
#include "game/SettlementNpc.h"
#include "game/SettlementReputation.h"
#include "game/StructureComponents.h"
#include "game/Traps.h"
#include "game/VehicleComponents.h"
#include "game/VehicleMods.h"
#include "items/ItemComponents.h"
#include "script/ModDataComponents.h"
#include "save/BinaryReader.h"
#include "save/BinaryWriter.h"

namespace zl::save {
namespace {

constexpr std::uint32_t kMaxItemDefIdBytes = 64;
constexpr std::uint32_t kMaxPoiClaimBytes = 129;
constexpr int kMaxItemStack = 9999;
constexpr int kMaxItemLoadedAmmo = 200;
constexpr std::uint32_t kMinItemBytes = 12;

bool Fail(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
    return false;
}

bool Finite(float value) {
    return std::isfinite(value);
}

bool ValidItem(const ecs::ItemInstance& item) {
    return !item.defId.empty() &&
           item.defId.size() <= kMaxItemDefIdBytes &&
           item.count >= 1 && item.count <= kMaxItemStack &&
           Finite(item.condition) && item.condition >= 0.0f &&
           item.condition <= 1.0f && item.loadedAmmo >= 0 &&
           item.loadedAmmo <= kMaxItemLoadedAmmo;
}

bool WriteItem(const ecs::ItemInstance& item, BinaryWriter& writer) {
    if (!ValidItem(item)) {
        return false;
    }
    return writer.WriteString(item.defId, kMaxItemDefIdBytes) &&
           writer.Write(static_cast<std::int32_t>(item.count)) &&
           writer.Write(item.condition) &&
           writer.Write(static_cast<std::int32_t>(item.loadedAmmo));
}

bool ReadItem(BinaryReader& reader, ecs::ItemInstance* item) {
    std::string defId;
    std::int32_t count = 0;
    float condition = 0.0f;
    if (!reader.ReadString(defId, kMaxItemDefIdBytes) || !reader.Read(count) ||
        !reader.Read(condition)) {
        return false;
    }
    if (defId.empty() || count < 1 || count > kMaxItemStack ||
        !Finite(condition) || condition < 0.0f || condition > 1.0f) {
        return false;
    }
    std::int32_t loadedAmmo = 0;
    if (reader.Header().version >= kSaveComponentVersionLoadedAmmo &&
        !reader.Read(loadedAmmo)) {
        return false;
    }
    if (loadedAmmo < 0 || loadedAmmo > kMaxItemLoadedAmmo) {
        return false;
    }
    item->defId = std::move(defId);
    item->count = static_cast<int>(count);
    item->condition = condition;
    item->loadedAmmo = static_cast<int>(loadedAmmo);
    return true;
}

bool WriteContainer(const ecs::Container& container, BinaryWriter& writer) {
    if (!Finite(container.capacityKg) || container.capacityKg < 0.0f ||
        container.items.size() > kMaxContainerElements) {
        return false;
    }
    if (!writer.Write(container.capacityKg) ||
        !writer.Write(static_cast<std::uint32_t>(container.items.size()))) {
        return false;
    }
    for (const ecs::ItemInstance& item : container.items) {
        if (!WriteItem(item, writer)) {
            return false;
        }
    }
    return true;
}

bool ReadContainer(BinaryReader& reader, ecs::Container* container) {
    float capacity = 0.0f;
    std::uint32_t count = 0;
    if (!reader.Read(capacity) || !reader.Read(count)) {
        return false;
    }
    if (!Finite(capacity) || capacity < 0.0f ||
        count > kMaxContainerElements ||
        count > reader.RemainingInSegment() / kMinItemBytes) {
        return false;
    }
    ecs::Container loaded;
    loaded.capacityKg = capacity;
    loaded.items.reserve(std::min<std::uint32_t>(count, 4096));
    for (std::uint32_t i = 0; i < count; ++i) {
        ecs::ItemInstance item;
        if (!ReadItem(reader, &item)) {
            return false;
        }
        loaded.items.push_back(std::move(item));
    }
    *container = std::move(loaded);
    return true;
}

bool WriteLootContainer(const ecs::LootContainer& loot, BinaryWriter& writer) {
    return !loot.tableId.empty() && loot.tableId.size() <= kMaxItemDefIdBytes &&
           writer.WriteString(loot.tableId, kMaxItemDefIdBytes) &&
           writer.Write(loot.rollSeed) && writer.Write(loot.searched);
}

bool ReadLootContainer(BinaryReader& reader, ecs::LootContainer* loot) {
    std::string tableId;
    std::uint64_t rollSeed = 0;
    bool searched = false;
    if (!reader.ReadString(tableId, kMaxItemDefIdBytes) ||
        !reader.Read(rollSeed) || !reader.Read(searched)) {
        return false;
    }
    if (tableId.empty() || tableId.size() > kMaxItemDefIdBytes) {
        return false;
    }
    loot->tableId = std::move(tableId);
    loot->rollSeed = rollSeed;
    loot->searched = searched;
    return true;
}

bool WritePlayerStorage(const ecs::PlayerStorage& storage, BinaryWriter& writer) {
    return storage.maxStacks >= 1 && storage.maxStacks <= 256 &&
           writer.Write(static_cast<std::int32_t>(storage.maxStacks));
}

bool ReadPlayerStorage(BinaryReader& reader, ecs::PlayerStorage* storage) {
    std::int32_t maxStacks = 0;
    if (!reader.Read(maxStacks) || maxStacks < 1 || maxStacks > 256) {
        return false;
    }
    storage->maxStacks = static_cast<int>(maxStacks);
    return true;
}

bool WriteVehicle(const ecs::Vehicle& vehicle, BinaryWriter& writer) {
    const std::uint8_t type = static_cast<std::uint8_t>(vehicle.type);
    return Finite(vehicle.condition) && vehicle.condition >= 0.0f &&
           vehicle.condition <= 1.0f && Finite(vehicle.fuel) &&
           vehicle.fuel >= 0.0f && vehicle.fuel <= 1.0f && type <= 2 &&
           Finite(vehicle.heading) &&
           writer.Write(vehicle.condition) && writer.Write(vehicle.fuel) &&
           writer.Write(static_cast<std::uint8_t>(vehicle.headlights ? 1 : 0)) &&
           writer.Write(type) && writer.Write(vehicle.heading);
}

bool ReadVehicle(BinaryReader& reader, ecs::Vehicle* vehicle) {
    float condition = 0.0f;
    float fuel = 0.0f;
    if (!reader.Read(condition) || !reader.Read(fuel) || !Finite(condition) ||
        condition < 0.0f || condition > 1.0f || !Finite(fuel) || fuel < 0.0f ||
        fuel > 1.0f) {
        return false;
    }
    std::uint8_t headlights = 0;
    if (reader.Header().version >= 2 && !reader.Read(headlights)) {
        return false;
    }
    if (headlights > 1) {
        return false;
    }
    std::uint8_t type = static_cast<std::uint8_t>(ecs::VehicleType::Pickup);
    float heading = 1.5707963267948966f;
    if (reader.Header().version >= 3) {
        if (!reader.Read(type) || type > 2 || !reader.Read(heading) ||
            !Finite(heading)) {
            return false;
        }
    }
    vehicle->condition = condition;
    vehicle->fuel = fuel;
    vehicle->headlights = headlights != 0;
    vehicle->type = static_cast<ecs::VehicleType>(type);
    vehicle->heading = heading;
    return true;
}

bool WriteVehicleMods(const ecs::VehicleMods& mods, BinaryWriter& writer) {
    std::unordered_set<std::string> seen;
    for (const std::string& id : mods.defIds) {
        if (!id.empty() &&
            (!game::IsValidVehicleModToken(id) || !seen.insert(id).second)) {
            return false;
        }
        if (!writer.WriteString(id, game::kMaxVehicleModIdBytes)) {
            return false;
        }
    }
    return true;
}

bool ReadVehicleMods(BinaryReader& reader, ecs::VehicleMods* mods) {
    ecs::VehicleMods loaded;
    std::unordered_set<std::string> seen;
    for (std::string& id : loaded.defIds) {
        if (!reader.ReadString(id, game::kMaxVehicleModIdBytes)) {
            return false;
        }
        if (!id.empty() &&
            (!game::IsValidVehicleModToken(id) || !seen.insert(id).second)) {
            return false;
        }
    }
    *mods = std::move(loaded);
    return true;
}

bool WriteStructureOwner(const ecs::StructureOwner& owner, BinaryWriter& writer) {
    return writer.Write(owner.ownerStableId);
}

bool ReadStructureOwner(BinaryReader& reader, ecs::StructureOwner* owner) {
    return reader.Read(owner->ownerStableId);
}

bool WriteFarmPlot(const ecs::FarmPlot& plot, BinaryWriter& writer) {
    if (plot.cropId.size() > kMaxItemDefIdBytes || !Finite(plot.growthMinutes) ||
        plot.growthMinutes < 0.0f || !Finite(plot.waterMinutes) ||
        plot.waterMinutes < 0.0f || !Finite(plot.dryMinutes) ||
        plot.dryMinutes < 0.0f || !Finite(plot.growMinutes) ||
        plot.growMinutes <= 0.0f || plot.harvestYield < 0 ||
        plot.harvestYield > 999 ||
        static_cast<std::uint8_t>(plot.stage) > 3) {
        return false;
    }
    std::uint8_t flags = 0;
    if (plot.diseased) {
        flags |= 1u << 0;
    }
    if (plot.fertilized) {
        flags |= 1u << 1;
    }
    return writer.WriteString(plot.cropId, kMaxItemDefIdBytes) &&
           writer.Write(plot.growthMinutes) && writer.Write(plot.waterMinutes) &&
           writer.Write(plot.dryMinutes) && writer.Write(plot.growMinutes) &&
           writer.Write(static_cast<std::uint8_t>(plot.stage)) &&
           writer.Write(static_cast<std::int32_t>(plot.harvestYield)) &&
           writer.Write(flags);
}

bool ReadFarmPlot(BinaryReader& reader, ecs::FarmPlot* plot) {
    std::string cropId;
    float growth = 0.0f;
    float water = 0.0f;
    float dry = 0.0f;
    float grow = 0.0f;
    std::uint8_t stage = 0;
    std::int32_t yield = 0;
    std::uint8_t flags = 0;
    if (!reader.ReadString(cropId, kMaxItemDefIdBytes) ||
        !reader.Read(growth) || !reader.Read(water) || !reader.Read(dry) ||
        !reader.Read(grow) || !reader.Read(stage) || !reader.Read(yield)) {
        return false;
    }
    if (reader.Header().version >= kSaveComponentVersionFarmPlot &&
        !reader.Read(flags)) {
        return false;
    }
    if (!Finite(growth) || growth < 0.0f || !Finite(water) ||
        water < 0.0f || !Finite(dry) || dry < 0.0f || !Finite(grow) ||
        grow <= 0.0f || stage > 3 || yield < 0 || yield > 999 ||
        (flags & ~0x03u) != 0) {
        return false;
    }
    plot->cropId = std::move(cropId);
    plot->growthMinutes = growth;
    plot->waterMinutes = water;
    plot->dryMinutes = dry;
    plot->growMinutes = grow;
    plot->stage = static_cast<ecs::CropStage>(stage);
    plot->harvestYield = static_cast<int>(yield);
    plot->diseased = (flags & 0x01u) != 0;
    plot->fertilized = (flags & 0x02u) != 0;
    return true;
}

bool WriteGenerator(const ecs::Generator& generator, BinaryWriter& writer) {
    return Finite(generator.fuelMinutes) && generator.fuelMinutes >= 0.0f &&
           Finite(generator.radius) && generator.radius >= 0.0f &&
           writer.Write(generator.fuelMinutes) &&
           writer.Write(generator.radius) && writer.Write(generator.running);
}

bool ReadGenerator(BinaryReader& reader, ecs::Generator* generator) {
    float fuel = 0.0f;
    float radius = 0.0f;
    bool running = false;
    if (!reader.Read(fuel) || !reader.Read(radius) || !reader.Read(running) ||
        !Finite(fuel) || fuel < 0.0f || !Finite(radius) || radius < 0.0f) {
        return false;
    }
    generator->fuelMinutes = fuel;
    generator->radius = radius;
    generator->running = running;
    return true;
}

bool WriteColdStorage(const ecs::ColdStorage& storage, BinaryWriter& writer) {
    return writer.Write(storage.powered);
}

bool ReadColdStorage(BinaryReader& reader, ecs::ColdStorage* storage) {
    bool powered = false;
    if (!reader.Read(powered)) {
        return false;
    }
    storage->powered = powered;
    return true;
}

bool WriteFloodlight(const ecs::Floodlight& floodlight,
                     BinaryWriter& writer) {
    return writer.Write(floodlight.on) && writer.Write(floodlight.powered);
}

bool ReadFloodlight(BinaryReader& reader, ecs::Floodlight* floodlight) {
    bool on = true;
    bool powered = false;
    if (!reader.Read(on) || !reader.Read(powered)) {
        return false;
    }
    floodlight->on = on;
    floodlight->powered = powered;
    return true;
}

bool WriteSpikeTrap(const ecs::SpikeTrap& trap, BinaryWriter& writer) {
    return Finite(trap.cooldown) && trap.cooldown >= 0.0f &&
           Finite(trap.cooldownSeconds) && trap.cooldownSeconds >= 0.0f &&
           Finite(trap.damage) && trap.damage >= 0.0f &&
           Finite(trap.radius) && trap.radius > 0.0f &&
           Finite(trap.staggerSeconds) && trap.staggerSeconds >= 0.0f &&
           trap.uses >= 0 && trap.uses <= 999 &&
           writer.Write(trap.armed) && writer.Write(trap.cooldown) &&
           writer.Write(trap.cooldownSeconds) && writer.Write(trap.damage) &&
           writer.Write(trap.radius) && writer.Write(trap.staggerSeconds) &&
           writer.Write(static_cast<std::int32_t>(trap.uses));
}

bool ReadSpikeTrap(BinaryReader& reader, ecs::SpikeTrap* trap) {
    bool armed = false;
    float cooldown = 0.0f;
    float cooldownSeconds = 0.0f;
    float damage = 0.0f;
    float radius = 0.0f;
    float stagger = 0.0f;
    std::int32_t uses = 0;
    if (!reader.Read(armed) || !reader.Read(cooldown) ||
        !reader.Read(cooldownSeconds) || !reader.Read(damage) ||
        !reader.Read(radius) || !reader.Read(stagger) || !reader.Read(uses)) {
        return false;
    }
    if (!Finite(cooldown) || cooldown < 0.0f || !Finite(cooldownSeconds) ||
        cooldownSeconds < 0.0f || !Finite(damage) || damage < 0.0f ||
        !Finite(radius) || radius <= 0.0f || !Finite(stagger) ||
        stagger < 0.0f || uses < 0 || uses > 999) {
        return false;
    }
    trap->armed = armed;
    trap->cooldown = cooldown;
    trap->cooldownSeconds = cooldownSeconds;
    trap->damage = damage;
    trap->radius = radius;
    trap->staggerSeconds = stagger;
    trap->uses = static_cast<int>(uses);
    return true;
}

bool WritePlacedTrap(const ecs::PlacedTrap& trap, BinaryWriter& writer) {
    return game::IsValidTrapToken(trap.defId) && Finite(trap.cooldown) &&
           trap.cooldown >= 0.0f && trap.uses >= 0 && trap.uses <= 999 &&
           Finite(trap.armRemaining) && trap.armRemaining >= 0.0f &&
           writer.WriteString(trap.defId, 64) && writer.Write(trap.armed) &&
           writer.Write(trap.cooldown) &&
           writer.Write(static_cast<std::int32_t>(trap.uses)) &&
           writer.Write(trap.owner) && writer.Write(trap.armRemaining);
}

bool ReadPlacedTrap(BinaryReader& reader, ecs::PlacedTrap* trap) {
    ecs::PlacedTrap loaded;
    std::int32_t uses = 0;
    if (!reader.ReadString(loaded.defId, 64) ||
        !game::IsValidTrapToken(loaded.defId) || !reader.Read(loaded.armed) ||
        !reader.Read(loaded.cooldown) || !reader.Read(uses) ||
        !reader.Read(loaded.owner)) {
        return false;
    }
    if (!Finite(loaded.cooldown) || loaded.cooldown < 0.0f || uses < 0 ||
        uses > 999) {
        return false;
    }
    if (reader.Header().version >= kSaveComponentVersionPlacedTrap) {
        if (!reader.Read(loaded.armRemaining) ||
            !Finite(loaded.armRemaining) || loaded.armRemaining < 0.0f) {
            return false;
        }
    }
    loaded.uses = static_cast<int>(uses);
    *trap = std::move(loaded);
    return true;
}

bool WriteMerchantStock(const ecs::MerchantStock& stock, BinaryWriter& writer) {
    if (stock.restockDay < 0) {
        return false;
    }
    for (const int count : stock.stock) {
        if (count < 0 || count > 999) {
            return false;
        }
        if (!writer.Write(static_cast<std::int32_t>(count))) {
            return false;
        }
    }
    return writer.Write(static_cast<std::int32_t>(stock.restockDay));
}

bool ReadMerchantStock(BinaryReader& reader, ecs::MerchantStock* stock) {
    ecs::MerchantStock loaded;
    for (int& count : loaded.stock) {
        std::int32_t value = 0;
        if (!reader.Read(value) || value < 0 || value > 999) {
            return false;
        }
        count = static_cast<int>(value);
    }
    std::int32_t day = 0;
    if (!reader.Read(day) || day < 0) {
        return false;
    }
    loaded.restockDay = static_cast<int>(day);
    *stock = loaded;
    return true;
}

bool ValidSettlementStateId(std::string_view id) {
    return !id.empty() && id.size() <= game::kMaxSettlementStateIdBytes &&
           std::all_of(id.begin(), id.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '.';
           });
}

bool WriteSettlementState(const game::SettlementState& state,
                          BinaryWriter& writer) {
    if (state.settlements.size() > game::kMaxSettlementStates) {
        return false;
    }
    if (!writer.Write(static_cast<std::uint16_t>(state.settlements.size()))) {
        return false;
    }
    std::unordered_set<std::string> seen;
    for (const game::SettlementStateEntry& entry : state.settlements) {
        if (!ValidSettlementStateId(entry.id) ||
            !seen.insert(entry.id).second ||
            !Finite(entry.guardHealth) || entry.guardHealth < -1.0f ||
            entry.guardHealth > 1.0e6f) {
            return false;
        }
        if (!writer.WriteString(entry.id, game::kMaxSettlementStateIdBytes) ||
            !writer.Write(entry.fallen) ||
            !writer.Write(entry.guardHealth) ||
            !writer.Write(static_cast<std::int32_t>(entry.lastNightKey))) {
            return false;
        }
    }
    return true;
}

bool ReadSettlementState(BinaryReader& reader, game::SettlementState* state) {
    std::uint16_t count = 0;
    if (!reader.Read(count) || count > game::kMaxSettlementStates ||
        state == nullptr) {
        return false;
    }
    game::SettlementState loaded;
    loaded.settlements.reserve(count);
    std::unordered_set<std::string> seen;
    for (std::uint16_t i = 0; i < count; ++i) {
        game::SettlementStateEntry entry;
        std::int32_t lastNightKey = 0;
        if (!reader.ReadString(entry.id, game::kMaxSettlementStateIdBytes) ||
            !ValidSettlementStateId(entry.id) ||
            !seen.insert(entry.id).second || !reader.Read(entry.fallen) ||
            !reader.Read(entry.guardHealth) || !Finite(entry.guardHealth) ||
            entry.guardHealth < -1.0f || entry.guardHealth > 1.0e6f ||
            !reader.Read(lastNightKey)) {
            return false;
        }
        entry.lastNightKey = static_cast<int>(lastNightKey);
        loaded.settlements.push_back(std::move(entry));
    }
    *state = std::move(loaded);
    return true;
}

bool WriteSettlementReputation(const game::SettlementReputation& reputation,
                               BinaryWriter& writer) {
    if (reputation.settlements.size() >
            game::kMaxSettlementReputationEntries ||
        reputation.appliedKeys.size() > game::kMaxSettlementReputationKeys ||
        !writer.Write(
            static_cast<std::uint16_t>(reputation.settlements.size()))) {
        return false;
    }
    std::unordered_set<std::string> seen;
    for (const game::SettlementReputationEntry& entry :
         reputation.settlements) {
        if (!game::IsValidSettlementReputationId(entry.settlementId) ||
            !seen.insert(entry.settlementId).second ||
            entry.score < game::kSettlementReputationMinScore ||
            entry.score > game::kSettlementReputationMaxScore) {
            return false;
        }
        if (!writer.WriteString(entry.settlementId,
                                game::kMaxSettlementReputationIdBytes) ||
            !writer.Write(static_cast<std::int32_t>(entry.score))) {
            return false;
        }
    }
    if (!writer.Write(
            static_cast<std::uint16_t>(reputation.appliedKeys.size()))) {
        return false;
    }
    seen.clear();
    for (const std::string& key : reputation.appliedKeys) {
        if (key.empty() ||
            key.size() > game::kMaxSettlementReputationKeyBytes ||
            !seen.insert(key).second ||
            !writer.WriteString(key, game::kMaxSettlementReputationKeyBytes)) {
            return false;
        }
    }
    return true;
}

bool ReadSettlementReputation(BinaryReader& reader,
                              game::SettlementReputation* reputation) {
    std::uint16_t entryCount = 0;
    if (!reader.Read(entryCount) ||
        entryCount > game::kMaxSettlementReputationEntries ||
        reputation == nullptr) {
        return false;
    }
    game::SettlementReputation loaded;
    loaded.settlements.reserve(entryCount);
    std::unordered_set<std::string> seen;
    for (std::uint16_t i = 0; i < entryCount; ++i) {
        game::SettlementReputationEntry entry;
        std::int32_t score = 0;
        if (!reader.ReadString(entry.settlementId,
                               game::kMaxSettlementReputationIdBytes) ||
            !game::IsValidSettlementReputationId(entry.settlementId) ||
            !seen.insert(entry.settlementId).second ||
            !reader.Read(score) ||
            score < game::kSettlementReputationMinScore ||
            score > game::kSettlementReputationMaxScore) {
            return false;
        }
        entry.score = static_cast<int>(score);
        loaded.settlements.push_back(std::move(entry));
    }
    std::uint16_t keyCount = 0;
    if (!reader.Read(keyCount) ||
        keyCount > game::kMaxSettlementReputationKeys) {
        return false;
    }
    loaded.appliedKeys.reserve(keyCount);
    seen.clear();
    for (std::uint16_t i = 0; i < keyCount; ++i) {
        std::string key;
        if (!reader.ReadString(key, game::kMaxSettlementReputationKeyBytes) ||
            key.empty() || !seen.insert(key).second) {
            return false;
        }
        loaded.appliedKeys.push_back(std::move(key));
    }
    *reputation = std::move(loaded);
    return true;
}

bool ValidForageZoneId(std::string_view id) {
    return !id.empty() && id.size() <= kMaxItemDefIdBytes &&
           std::all_of(id.begin(), id.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '-';
           });
}

bool ValidForageHarvest(const ecs::ForageHarvest& harvest) {
    return ValidForageZoneId(harvest.zoneId) &&
           harvest.x >= -1000000 && harvest.x <= 1000000 &&
           harvest.y >= -1000000 && harvest.y <= 1000000 &&
           harvest.z >= 0 && harvest.z <= 16 &&
           std::isfinite(harvest.availableAtMinutes) &&
           harvest.availableAtMinutes >= 0.0 &&
           harvest.availableAtMinutes <= 1.0e9;
}

bool WriteForageState(const ecs::ForageState& state, BinaryWriter& writer) {
    if (state.harvested.size() > ecs::kMaxForageHarvests) {
        return false;
    }
    std::vector<ecs::ForageHarvest> harvests = state.harvested;
    std::sort(harvests.begin(), harvests.end(),
              [](const ecs::ForageHarvest& left,
                 const ecs::ForageHarvest& right) {
                  return std::tie(left.zoneId, left.x, left.y, left.z) <
                         std::tie(right.zoneId, right.x, right.y, right.z);
              });
    if (!writer.Write(static_cast<std::uint32_t>(harvests.size()))) {
        return false;
    }
    for (const ecs::ForageHarvest& harvest : harvests) {
        if (!ValidForageHarvest(harvest) ||
            !writer.WriteString(harvest.zoneId, kMaxItemDefIdBytes) ||
            !writer.Write(static_cast<std::int32_t>(harvest.x)) ||
            !writer.Write(static_cast<std::int32_t>(harvest.y)) ||
            !writer.Write(static_cast<std::int32_t>(harvest.z)) ||
            !writer.Write(harvest.availableAtMinutes)) {
            return false;
        }
    }
    return true;
}

bool ReadForageState(BinaryReader& reader, ecs::ForageState* state) {
    std::uint32_t count = 0;
    if (!reader.Read(count) || count > ecs::kMaxForageHarvests ||
        count > reader.RemainingInSegment() / 24) {
        return false;
    }
    ecs::ForageState loaded;
    loaded.harvested.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        ecs::ForageHarvest harvest;
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t z = 0;
        if (!reader.ReadString(harvest.zoneId, kMaxItemDefIdBytes) ||
            !reader.Read(x) || !reader.Read(y) || !reader.Read(z) ||
            !reader.Read(harvest.availableAtMinutes)) {
            return false;
        }
        harvest.x = x;
        harvest.y = y;
        harvest.z = z;
        if (!ValidForageHarvest(harvest)) {
            return false;
        }
        const bool duplicate = std::any_of(
            loaded.harvested.begin(), loaded.harvested.end(),
            [&](const ecs::ForageHarvest& existing) {
                return existing.zoneId == harvest.zoneId && existing.x == x &&
                       existing.y == y && existing.z == z;
            });
        if (duplicate) {
            return false;
        }
        loaded.harvested.push_back(std::move(harvest));
    }
    *state = std::move(loaded);
    return true;
}

bool ValidFireMaterialId(std::string_view id) {
    return !id.empty() && id.size() <= kMaxItemDefIdBytes &&
           std::all_of(id.begin(), id.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '-';
           });
}

bool ValidFireAnchor(const ecs::FireAnchor& anchor) {
    return anchor.x >= -1000000 && anchor.x <= 1000000 &&
           anchor.y >= -1000000 && anchor.y <= 1000000 &&
           anchor.z >= 0 && anchor.z <= 16;
}

bool WriteFireAnchor(const ecs::FireAnchor& anchor, BinaryWriter& writer) {
    return ValidFireAnchor(anchor) &&
           writer.Write(static_cast<std::int32_t>(anchor.x)) &&
           writer.Write(static_cast<std::int32_t>(anchor.y)) &&
           writer.Write(static_cast<std::int32_t>(anchor.z));
}

bool ReadFireAnchor(BinaryReader& reader, ecs::FireAnchor* anchor) {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    if (!reader.Read(x) || !reader.Read(y) || !reader.Read(z)) {
        return false;
    }
    const ecs::FireAnchor loaded{x, y, z};
    if (!ValidFireAnchor(loaded)) {
        return false;
    }
    *anchor = loaded;
    return true;
}

bool ValidFireSource(const ecs::FireSource& source) {
    const auto phase = static_cast<std::uint8_t>(source.phase);
    if (!ValidFireMaterialId(source.materialId) || phase > 3 ||
        !Finite(source.ignitionProgress) ||
        !Finite(source.ignitionDuration) ||
        !Finite(source.burnRemaining) || !Finite(source.burnDuration) ||
        !Finite(source.smokePerSecond) || !Finite(source.heatRadius) ||
        !Finite(source.smolderElapsed) || !Finite(source.spreadAccumulator) ||
        source.ignitionProgress < 0.0f || source.ignitionDuration < 0.0f ||
        source.ignitionDuration > 3600.0f ||
        source.ignitionProgress > source.ignitionDuration + 0.001f ||
        source.burnDuration < 0.0f || source.burnDuration > 1.0e7f ||
        source.burnRemaining < 0.0f ||
        source.burnRemaining > source.burnDuration + 0.001f ||
        source.smokePerSecond < 0.0f || source.smokePerSecond > 1.0e5f ||
        source.heatRadius < 0.0f || source.heatRadius > 1.0e5f ||
        source.smolderElapsed < 0.0f || source.smolderElapsed > 1.0e6f ||
        source.spreadAccumulator < 0.0f ||
        source.spreadAccumulator > 1.0e6f) {
        return false;
    }
    return source.phase == ecs::FirePhase::Extinguished ||
           source.burnDuration > 0.0f;
}

bool WriteFireSource(const ecs::FireSource& source, BinaryWriter& writer) {
    if (!ValidFireSource(source)) {
        return false;
    }
    return writer.WriteString(source.materialId, kMaxItemDefIdBytes) &&
           writer.Write(static_cast<std::uint8_t>(source.phase)) &&
           writer.Write(source.ignitionProgress) &&
           writer.Write(source.ignitionDuration) &&
           writer.Write(source.burnRemaining) &&
           writer.Write(source.burnDuration) &&
           writer.Write(source.smokePerSecond) &&
           writer.Write(source.heatRadius) &&
           writer.Write(source.smolderElapsed) &&
           writer.Write(source.spreadAccumulator);
}

bool ReadFireSource(BinaryReader& reader, ecs::FireSource* source) {
    ecs::FireSource loaded;
    std::uint8_t phase = 0;
    if (!reader.ReadString(loaded.materialId, kMaxItemDefIdBytes) ||
        !reader.Read(phase) || !reader.Read(loaded.ignitionProgress) ||
        !reader.Read(loaded.ignitionDuration) ||
        !reader.Read(loaded.burnRemaining) ||
        !reader.Read(loaded.burnDuration) ||
        !reader.Read(loaded.smokePerSecond) ||
        !reader.Read(loaded.heatRadius) ||
        !reader.Read(loaded.smolderElapsed) ||
        !reader.Read(loaded.spreadAccumulator) || phase > 3) {
        return false;
    }
    loaded.phase = static_cast<ecs::FirePhase>(phase);
    if (!ValidFireSource(loaded)) {
        return false;
    }
    *source = std::move(loaded);
    return true;
}

bool ValidBurningZombie(const ecs::BurningZombie& burning) {
    return Finite(burning.remaining) && burning.remaining >= 0.0f &&
           burning.remaining <= 120.0f &&
           Finite(burning.damageAccumulator) &&
           burning.damageAccumulator >= 0.0f &&
           burning.damageAccumulator <= 3600.0f &&
           Finite(burning.spreadAccumulator) &&
           burning.spreadAccumulator >= 0.0f &&
           burning.spreadAccumulator <= 3600.0f &&
           Finite(burning.panicRemaining) && burning.panicRemaining >= 0.0f &&
           burning.panicRemaining <= 120.0f &&
           Finite(burning.panicSpeedScale) &&
           burning.panicSpeedScale >= 0.5f &&
           burning.panicSpeedScale <= 3.0f &&
           Finite(burning.panicChaseRadius) &&
           burning.panicChaseRadius >= 0.0f &&
           burning.panicChaseRadius <= 64.0f;
}

bool WriteBurningZombie(const ecs::BurningZombie& burning,
                        BinaryWriter& writer) {
    return ValidBurningZombie(burning) && writer.Write(burning.remaining) &&
           writer.Write(burning.damageAccumulator) &&
           writer.Write(burning.spreadAccumulator) &&
           writer.Write(burning.panicRemaining) &&
           writer.Write(burning.panicSpeedScale) &&
           writer.Write(burning.panicChaseRadius);
}

bool ReadBurningZombie(BinaryReader& reader, ecs::BurningZombie* burning) {
    ecs::BurningZombie loaded;
    if (!reader.Read(loaded.remaining) ||
        !reader.Read(loaded.damageAccumulator) ||
        !reader.Read(loaded.spreadAccumulator) ||
        !reader.Read(loaded.panicRemaining) ||
        !reader.Read(loaded.panicSpeedScale) ||
        !reader.Read(loaded.panicChaseRadius) ||
        !ValidBurningZombie(loaded)) {
        return false;
    }
    *burning = loaded;
    return true;
}

bool ValidAnimal(const ecs::Animal& animal) {
    return ValidForageZoneId(animal.speciesId) &&
           static_cast<std::uint8_t>(animal.kind) <=
               static_cast<std::uint8_t>(ecs::AnimalKind::Dangerous);
}

bool WriteAnimal(const ecs::Animal& animal, BinaryWriter& writer) {
    return ValidAnimal(animal) &&
           writer.WriteString(animal.speciesId, kMaxItemDefIdBytes) &&
           writer.Write(static_cast<std::uint8_t>(animal.kind));
}

bool ReadAnimal(BinaryReader& reader, ecs::Animal* animal) {
    ecs::Animal loaded;
    std::uint8_t kind = 0;
    if (!reader.ReadString(loaded.speciesId, kMaxItemDefIdBytes) ||
        !reader.Read(kind) ||
        kind > static_cast<std::uint8_t>(ecs::AnimalKind::Dangerous)) {
        return false;
    }
    loaded.kind = static_cast<ecs::AnimalKind>(kind);
    if (!ValidAnimal(loaded)) {
        return false;
    }
    *animal = std::move(loaded);
    return true;
}

bool ValidFlee(const ecs::Flee& flee) {
    return Finite(flee.remaining) && flee.remaining >= 0.0f &&
           flee.remaining <= 120.0f && Finite(flee.directionX) &&
           flee.directionX >= -4.0f && flee.directionX <= 4.0f &&
           Finite(flee.directionY) && flee.directionY >= -4.0f &&
           flee.directionY <= 4.0f && Finite(flee.threatX) &&
           flee.threatX >= -1000000.0f && flee.threatX <= 1000000.0f &&
           Finite(flee.threatY) && flee.threatY >= -1000000.0f &&
           flee.threatY <= 1000000.0f && Finite(flee.alertness) &&
           flee.alertness >= 0.0f && flee.alertness <= 1.0f;
}

bool WriteFlee(const ecs::Flee& flee, BinaryWriter& writer) {
    return ValidFlee(flee) && writer.Write(flee.active) &&
           writer.Write(flee.remaining) && writer.Write(flee.directionX) &&
           writer.Write(flee.directionY) && writer.Write(flee.threatX) &&
           writer.Write(flee.threatY) && writer.Write(flee.alertness);
}

bool ReadFlee(BinaryReader& reader, ecs::Flee* flee) {
    ecs::Flee loaded;
    if (!reader.Read(loaded.active) || !reader.Read(loaded.remaining) ||
        !reader.Read(loaded.directionX) || !reader.Read(loaded.directionY) ||
        !reader.Read(loaded.threatX) || !reader.Read(loaded.threatY) ||
        !reader.Read(loaded.alertness) || !ValidFlee(loaded)) {
        return false;
    }
    *flee = loaded;
    return true;
}

bool ValidAnimalCorpse(const ecs::AnimalCorpse& corpse) {
    return ValidForageZoneId(corpse.speciesId);
}

bool WriteAnimalCorpse(const ecs::AnimalCorpse& corpse,
                       BinaryWriter& writer) {
    return ValidAnimalCorpse(corpse) &&
           writer.WriteString(corpse.speciesId, kMaxItemDefIdBytes) &&
           writer.Write(corpse.harvested);
}

bool ReadAnimalCorpse(BinaryReader& reader, ecs::AnimalCorpse* corpse) {
    ecs::AnimalCorpse loaded;
    if (!reader.ReadString(loaded.speciesId, kMaxItemDefIdBytes) ||
        !reader.Read(loaded.harvested) || !ValidAnimalCorpse(loaded)) {
        return false;
    }
    *corpse = std::move(loaded);
    return true;
}

bool ValidAnimalSavedRecord(const ecs::AnimalSavedRecord& record) {
    return ValidForageZoneId(record.speciesId) &&
           Finite(record.x) && record.x >= -1000000.0f &&
           record.x <= 1000000.0f && Finite(record.y) &&
           record.y >= -1000000.0f && record.y <= 1000000.0f &&
           Finite(record.health) && record.health >= 0.0f &&
           record.health <= 1000000.0f && Finite(record.fleeRemaining) &&
           record.fleeRemaining >= 0.0f && record.fleeRemaining <= 120.0f &&
           Finite(record.fleeDirX) && record.fleeDirX >= -4.0f &&
           record.fleeDirX <= 4.0f && Finite(record.fleeDirY) &&
           record.fleeDirY >= -4.0f && record.fleeDirY <= 4.0f &&
           Finite(record.threatX) && record.threatX >= -1000000.0f &&
           record.threatX <= 1000000.0f && Finite(record.threatY) &&
           record.threatY >= -1000000.0f && record.threatY <= 1000000.0f &&
           Finite(record.alertness) && record.alertness >= 0.0f &&
           record.alertness <= 1.0f;
}

bool ValidAnimalWorldState(const ecs::AnimalWorldState& state) {
    if (state.chunks.size() > ecs::kMaxAnimalWorldStateChunks) {
        return false;
    }
    std::set<std::pair<int, int>> seen;
    std::size_t animalCount = 0;
    for (const ecs::AnimalSavedChunk& chunk : state.chunks) {
        if (chunk.chunkX < -1000000 || chunk.chunkX > 1000000 ||
            chunk.chunkY < -1000000 || chunk.chunkY > 1000000 ||
            chunk.animals.size() >
                ecs::kMaxAnimalWorldStateAnimalsPerChunk ||
            !seen.emplace(chunk.chunkX, chunk.chunkY).second) {
            return false;
        }
        animalCount += chunk.animals.size();
        if (animalCount > ecs::kMaxAnimalWorldStateAnimals) {
            return false;
        }
        for (const ecs::AnimalSavedRecord& record : chunk.animals) {
            if (!ValidAnimalSavedRecord(record)) {
                return false;
            }
        }
    }
    return true;
}

bool WriteAnimalWorldState(const ecs::AnimalWorldState& state,
                           BinaryWriter& writer) {
    if (!ValidAnimalWorldState(state) ||
        !writer.Write(static_cast<std::uint32_t>(state.chunks.size()))) {
        return false;
    }
    for (const ecs::AnimalSavedChunk& chunk : state.chunks) {
        if (!writer.Write(static_cast<std::int32_t>(chunk.chunkX)) ||
            !writer.Write(static_cast<std::int32_t>(chunk.chunkY)) ||
            !writer.Write(
                static_cast<std::uint16_t>(chunk.animals.size()))) {
            return false;
        }
        for (const ecs::AnimalSavedRecord& record : chunk.animals) {
            if (!writer.WriteString(record.speciesId, kMaxItemDefIdBytes) ||
                !writer.Write(record.x) || !writer.Write(record.y) ||
                !writer.Write(record.health) || !writer.Write(record.fleeing) ||
                !writer.Write(record.fleeRemaining) ||
                !writer.Write(record.fleeDirX) ||
                !writer.Write(record.fleeDirY) ||
                !writer.Write(record.threatX) || !writer.Write(record.threatY) ||
                !writer.Write(record.alertness)) {
                return false;
            }
        }
    }
    return true;
}

bool ReadAnimalWorldState(BinaryReader& reader, ecs::AnimalWorldState* state) {
    std::uint32_t chunkCount = 0;
    if (!reader.Read(chunkCount) ||
        chunkCount > ecs::kMaxAnimalWorldStateChunks ||
        chunkCount > reader.RemainingInSegment() / 10) {
        return false;
    }
    ecs::AnimalWorldState loaded;
    loaded.chunks.reserve(std::min<std::uint32_t>(chunkCount, 4096));
    std::set<std::pair<int, int>> seen;
    std::size_t animalCount = 0;
    for (std::uint32_t i = 0; i < chunkCount; ++i) {
        std::int32_t chunkX = 0;
        std::int32_t chunkY = 0;
        std::uint16_t count = 0;
        if (!reader.Read(chunkX) || !reader.Read(chunkY) ||
            !reader.Read(count) ||
            count > ecs::kMaxAnimalWorldStateAnimalsPerChunk ||
            count > reader.RemainingInSegment() / 24) {
            return false;
        }
        ecs::AnimalSavedChunk chunk;
        chunk.chunkX = chunkX;
        chunk.chunkY = chunkY;
        if (chunkX < -1000000 || chunkX > 1000000 || chunkY < -1000000 ||
            chunkY > 1000000 ||
            !seen.emplace(chunkX, chunkY).second) {
            return false;
        }
        chunk.animals.reserve(count);
        for (std::uint16_t j = 0; j < count; ++j) {
            ecs::AnimalSavedRecord record;
            if (!reader.ReadString(record.speciesId, kMaxItemDefIdBytes) ||
                !reader.Read(record.x) || !reader.Read(record.y) ||
                !reader.Read(record.health) || !reader.Read(record.fleeing) ||
                !reader.Read(record.fleeRemaining) ||
                !reader.Read(record.fleeDirX) ||
                !reader.Read(record.fleeDirY) ||
                !reader.Read(record.threatX) || !reader.Read(record.threatY) ||
                !reader.Read(record.alertness) ||
                !ValidAnimalSavedRecord(record)) {
                return false;
            }
            chunk.animals.push_back(std::move(record));
            ++animalCount;
            if (animalCount > ecs::kMaxAnimalWorldStateAnimals) {
                return false;
            }
        }
        loaded.chunks.push_back(std::move(chunk));
    }
    *state = std::move(loaded);
    return true;
}

bool WriteEquipment(const ecs::Equipment& equipment, BinaryWriter& writer) {
    for (const auto& slot : equipment.slots) {
        if (!writer.Write(slot.has_value())) {
            return false;
        }
        if (slot.has_value() && !WriteItem(*slot, writer)) {
            return false;
        }
    }
    return true;
}

bool ReadEquipment(BinaryReader& reader, ecs::Equipment* equipment) {
    ecs::Equipment loaded;
    for (auto& slot : loaded.slots) {
        bool present = false;
        if (!reader.Read(present)) {
            return false;
        }
        if (!present) {
            slot.reset();
            continue;
        }
        ecs::ItemInstance item;
        if (!ReadItem(reader, &item)) {
            return false;
        }
        slot = std::move(item);
    }
    *equipment = std::move(loaded);
    return true;
}

bool ValidRecipeId(std::string_view id) {
    if (id.empty() || id.size() > kMaxItemDefIdBytes) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '.';
    });
}

bool ValidPoiClaimKey(std::string_view key) {
    if (key.empty() || key.size() > kMaxPoiClaimBytes) {
        return false;
    }
    return std::all_of(key.begin(), key.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '.';
    });
}

bool ValidCompanionTrait(std::string_view trait) {
    return game::IsValidCompanionToken(trait) &&
           trait.size() <= game::kMaxCompanionTraitBytes;
}

bool ValidCompanionState(const game::CompanionState& state) {
    if (state.loyalty < 0 || state.loyalty > 100 ||
        static_cast<std::uint8_t>(state.command) > 2u ||
        (state.downed && state.dead) ||
        !Finite(state.downedRemaining) || state.downedRemaining < 0.0f ||
        state.downedRemaining > 300.0f ||
        (!state.downed && state.downedRemaining > 0.0f) ||
        static_cast<std::uint8_t>(state.personalStatus) > 4u ||
        state.personalStage < 0 ||
        state.personalStage >
            static_cast<int>(game::kMaxCompanionQuestStages) ||
        state.personalProgress < 0 || state.personalProgress > 1000000 ||
        state.personalRewardMask > 0xFFu ||
        state.extraTraits.size() > game::kMaxCompanionTraits ||
        !Finite(state.inventory.capacityKg) ||
        state.inventory.capacityKg < 0.0f ||
        state.inventory.capacityKg > game::kCompanionMaxInventoryKg ||
        state.inventory.items.size() >
            game::kMaxCompanionInventoryEntries) {
        return false;
    }
    for (std::size_t i = 0; i < state.extraTraits.size(); ++i) {
        if (!ValidCompanionTrait(state.extraTraits[i])) {
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (state.extraTraits[j] == state.extraTraits[i]) {
                return false;
            }
        }
    }
    return true;
}

bool WriteCompanion(const game::Companion& companion,
                    BinaryWriter& writer) {
    if (!ValidRecipeId(companion.defId) ||
        companion.ownerStableId == 0 ||
        !ValidRecipeId(companion.homeSettlementId)) {
        return false;
    }
    return writer.WriteString(companion.defId,
                              game::kMaxCompanionIdBytes) &&
           writer.Write(companion.ownerStableId) &&
           writer.WriteString(companion.homeSettlementId,
                              game::kMaxCompanionIdBytes);
}

bool ReadCompanion(BinaryReader& reader, game::Companion* companion) {
    game::Companion loaded;
    if (!reader.ReadString(loaded.defId, game::kMaxCompanionIdBytes) ||
        !reader.Read(loaded.ownerStableId) ||
        !reader.ReadString(loaded.homeSettlementId,
                           game::kMaxCompanionIdBytes) ||
        !ValidRecipeId(loaded.defId) || loaded.ownerStableId == 0 ||
        !ValidRecipeId(loaded.homeSettlementId)) {
        return false;
    }
    *companion = std::move(loaded);
    return true;
}

bool WriteCompanionState(const game::CompanionState& state,
                         BinaryWriter& writer) {
    if (!ValidCompanionState(state)) {
        return false;
    }
    if (!writer.Write(static_cast<std::int32_t>(state.loyalty)) ||
        !writer.Write(static_cast<std::uint8_t>(state.command)) ||
        !writer.Write(state.standby) || !writer.Write(state.downed) ||
        !writer.Write(state.dead) || !writer.Write(state.downedRemaining) ||
        !writer.Write(static_cast<std::uint8_t>(state.personalStatus)) ||
        !writer.Write(static_cast<std::int32_t>(state.personalStage)) ||
        !writer.Write(static_cast<std::int32_t>(state.personalProgress)) ||
        !writer.Write(state.personalRewardMask) ||
        !writer.Write(static_cast<std::uint32_t>(state.extraTraits.size()))) {
        return false;
    }
    for (const std::string& trait : state.extraTraits) {
        if (!writer.WriteString(trait, game::kMaxCompanionTraitBytes)) {
            return false;
        }
    }
    if (!WriteContainer(state.inventory, writer) ||
        !WriteEquipment(state.equipment, writer)) {
        return false;
    }
    return true;
}

bool ReadCompanionState(BinaryReader& reader, game::CompanionState* state) {
    std::int32_t loyalty = 0;
    std::uint8_t command = 0;
    bool standby = false;
    bool downed = false;
    bool dead = false;
    float downedRemaining = 0.0f;
    if (!reader.Read(loyalty) || !reader.Read(command) ||
        !reader.Read(standby) || !reader.Read(downed) || !reader.Read(dead) ||
        !reader.Read(downedRemaining)) {
        return false;
    }
    if (loyalty < 0 || loyalty > 100 || command > 2) {
        return false;
    }
    game::CompanionState loaded;
    loaded.loyalty = static_cast<int>(loyalty);
    loaded.command = static_cast<game::CompanionCommand>(command);
    loaded.standby = standby;
    loaded.downed = downed;
    loaded.dead = dead;
    loaded.downedRemaining = downedRemaining;
    if (reader.Header().version >=
        kSaveComponentVersionCompanionState) {
        std::uint8_t personalStatus = 0;
        std::int32_t personalStage = 0;
        std::int32_t personalProgress = 0;
        std::uint32_t rewardMask = 0;
        std::uint32_t traitCount = 0;
        if (!reader.Read(personalStatus) || !reader.Read(personalStage) ||
            !reader.Read(personalProgress) || !reader.Read(rewardMask) ||
            !reader.Read(traitCount) || personalStatus > 4 ||
            personalStage < 0 ||
            personalStage >
                static_cast<std::int32_t>(game::kMaxCompanionQuestStages) ||
            personalProgress < 0 || personalProgress > 1000000 ||
            rewardMask > 0xFFu ||
            traitCount > game::kMaxCompanionTraits) {
            return false;
        }
        loaded.personalStatus =
            static_cast<game::CompanionQuestStatus>(personalStatus);
        loaded.personalStage = static_cast<int>(personalStage);
        loaded.personalProgress = static_cast<int>(personalProgress);
        loaded.personalRewardMask = rewardMask;
        loaded.extraTraits.reserve(traitCount);
        for (std::uint32_t i = 0; i < traitCount; ++i) {
            std::string trait;
            if (!reader.ReadString(trait, game::kMaxCompanionTraitBytes) ||
                !ValidCompanionTrait(trait)) {
                return false;
            }
            loaded.extraTraits.push_back(std::move(trait));
        }
    }
    if (!ReadContainer(reader, &loaded.inventory) ||
        !ReadEquipment(reader, &loaded.equipment) ||
        !ValidCompanionState(loaded)) {
        return false;
    }
    *state = std::move(loaded);
    return true;
}

bool WriteKnownRecipes(const ecs::KnownRecipes& known, BinaryWriter& writer) {
    std::vector<std::string> ids = known.recipeIds;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    if (ids.size() > 256) {
        return false;
    }
    if (!writer.Write(static_cast<std::uint32_t>(ids.size()))) {
        return false;
    }
    for (const std::string& id : ids) {
        if (!ValidRecipeId(id) || !writer.WriteString(id, kMaxItemDefIdBytes)) {
            return false;
        }
    }
    return true;
}

bool ReadKnownRecipes(BinaryReader& reader, ecs::KnownRecipes* known) {
    std::uint32_t count = 0;
    if (!reader.Read(count)) {
        return false;
    }
    if (count > 256 || count > reader.RemainingInSegment() / 5) {
        return false;
    }
    ecs::KnownRecipes loaded;
    loaded.recipeIds.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::string id;
        if (!reader.ReadString(id, kMaxItemDefIdBytes) || !ValidRecipeId(id) ||
            std::find(loaded.recipeIds.begin(), loaded.recipeIds.end(), id) !=
                loaded.recipeIds.end()) {
            return false;
        }
        loaded.recipeIds.push_back(std::move(id));
    }
    *known = std::move(loaded);
    return true;
}

bool ValidCraftingJob(const ecs::CraftingJob& job) {
    if (job.queue.size() > static_cast<std::size_t>(ecs::kMaxCraftQueueEntries)) {
        return false;
    }
    for (const ecs::CraftQueueItem& entry : job.queue) {
        if (!ValidRecipeId(entry.recipeId) || entry.count < 1 ||
            entry.count > ecs::kMaxCraftBatchCount) {
            return false;
        }
    }
    if (!job.active) {
        return job.recipeId.empty() && job.elapsed == 0.0f &&
               job.duration == 0.0f && job.toolEfficiency == 1.0f &&
               job.batchTotal == 0 && job.batchCompleted == 0 &&
               job.queue.empty();
    }
    return ValidRecipeId(job.recipeId) && Finite(job.elapsed) &&
           Finite(job.duration) && Finite(job.toolEfficiency) &&
           job.duration > 0.0f && job.duration <= 3600.0f &&
           job.elapsed >= 0.0f && job.elapsed <= job.duration + 1e-4f &&
           job.toolEfficiency > 0.0f && job.toolEfficiency <= 1.0f &&
           job.batchTotal >= 1 && job.batchTotal <= ecs::kMaxCraftBatchCount &&
           job.batchCompleted >= 0 && job.batchCompleted < job.batchTotal;
}

bool WriteCraftingJob(const ecs::CraftingJob& job, BinaryWriter& writer) {
    if (!ValidCraftingJob(job) || !writer.Write(job.active) ||
        !writer.WriteString(job.recipeId, kMaxItemDefIdBytes) ||
        !writer.Write(job.elapsed) || !writer.Write(job.duration) ||
        !writer.Write(job.toolEfficiency) || !writer.Write(job.batchTotal) ||
        !writer.Write(job.batchCompleted)) {
        return false;
    }
    if (!writer.Write(static_cast<std::uint32_t>(job.queue.size()))) {
        return false;
    }
    for (const ecs::CraftQueueItem& entry : job.queue) {
        if (!writer.WriteString(entry.recipeId, kMaxItemDefIdBytes) ||
            !writer.Write(entry.count)) {
            return false;
        }
    }
    return true;
}

bool ReadCraftingJob(BinaryReader& reader, ecs::CraftingJob* job) {
    ecs::CraftingJob loaded;
    if (!reader.Read(loaded.active) ||
        !reader.ReadString(loaded.recipeId, kMaxItemDefIdBytes) ||
        !reader.Read(loaded.elapsed) || !reader.Read(loaded.duration) ||
        !reader.Read(loaded.toolEfficiency) || !reader.Read(loaded.batchTotal) ||
        !reader.Read(loaded.batchCompleted)) {
        return false;
    }
    std::uint32_t queueCount = 0;
    if (!reader.Read(queueCount) ||
        queueCount > static_cast<std::uint32_t>(ecs::kMaxCraftQueueEntries) ||
        queueCount > reader.RemainingInSegment() / 8u) {
        return false;
    }
    loaded.queue.reserve(queueCount);
    for (std::uint32_t i = 0; i < queueCount; ++i) {
        ecs::CraftQueueItem entry;
        if (!reader.ReadString(entry.recipeId, kMaxItemDefIdBytes) ||
            !reader.Read(entry.count)) {
            return false;
        }
        loaded.queue.push_back(std::move(entry));
    }
    if (!ValidCraftingJob(loaded)) {
        return false;
    }
    *job = std::move(loaded);
    return true;
}

bool ValidHealth(const ecs::Health& health) {
    return Finite(health.current) && Finite(health.max) &&
           health.max > 0.0f && health.current >= 0.0f &&
           health.current <= health.max;
}

bool ValidNeeds(const ecs::Needs& needs) {
    return Finite(needs.hunger) && Finite(needs.thirst) &&
           Finite(needs.fatigue) && needs.hunger >= 0.0f &&
           needs.hunger <= 1.0f && needs.thirst >= 0.0f &&
           needs.thirst <= 1.0f && needs.fatigue >= 0.0f &&
           needs.fatigue <= 1.0f;
}

bool ValidFoodState(const ecs::FoodState& food) {
    return Finite(food.nutrition) && food.nutrition >= 0.0f &&
           food.nutrition <= 1.0f && Finite(food.poisonRemaining) &&
           food.poisonRemaining >= 0.0f && food.poisonRemaining <= 86'400.0f &&
           Finite(food.poisonDamagePerSecond) &&
           food.poisonDamagePerSecond >= 0.0f &&
           food.poisonDamagePerSecond <= 100.0f &&
           Finite(food.poisonTotal) && food.poisonTotal >= 0.0f &&
           food.poisonTotal <= 86'400.0f &&
           food.poisonTotal + 1e-4f >= food.poisonRemaining;
}

bool WriteFoodState(const ecs::FoodState& food, BinaryWriter& writer) {
    return ValidFoodState(food) && writer.Write(food.nutrition) &&
           writer.Write(food.poisonRemaining) &&
           writer.Write(food.poisonDamagePerSecond) &&
           writer.Write(food.poisonTotal);
}

bool ReadFoodState(BinaryReader& reader, ecs::FoodState* food) {
    ecs::FoodState value;
    if (!reader.Read(value.nutrition) || !reader.Read(value.poisonRemaining) ||
        !reader.Read(value.poisonDamagePerSecond) ||
        !reader.Read(value.poisonTotal) || !ValidFoodState(value)) {
        return false;
    }
    *food = value;
    return true;
}

bool WriteObjective(const ecs::Objective& objective, BinaryWriter& writer) {
    const auto step = static_cast<std::uint8_t>(objective.step);
    return game::IsObjectiveStepValid(step) && writer.Write(step);
}

bool ReadObjective(BinaryReader& reader, ecs::Objective* objective) {
    std::uint8_t step = 0;
    if (!reader.Read(step) || !game::IsObjectiveStepValid(step)) {
        return false;
    }
    objective->step = static_cast<ecs::ObjectiveStep>(step);
    return true;
}

bool ValidVariantScale(float value) {
    return Finite(value) && value >= 0.25f && value <= 4.0f;
}

bool WriteZombieVariant(const ecs::ZombieVariant& variant, BinaryWriter& writer) {
    const auto kind = static_cast<std::uint8_t>(variant.kind);
    return kind <= static_cast<std::uint8_t>(ecs::ZombieVariantKind::Armored) &&
           ValidVariantScale(variant.speedScale) &&
           ValidVariantScale(variant.damageScale) &&
           ValidVariantScale(variant.visualScale) && writer.Write(kind) &&
           writer.Write(variant.speedScale) && writer.Write(variant.damageScale) &&
           writer.Write(variant.visualScale);
}

bool ReadZombieVariant(BinaryReader& reader, ecs::ZombieVariant* variant) {
    std::uint8_t kind = 0;
    ecs::ZombieVariant loaded;
    if (!reader.Read(kind) || kind > static_cast<std::uint8_t>(ecs::ZombieVariantKind::Armored) ||
        !reader.Read(loaded.speedScale) || !reader.Read(loaded.damageScale) ||
        !reader.Read(loaded.visualScale) || !ValidVariantScale(loaded.speedScale) ||
        !ValidVariantScale(loaded.damageScale) || !ValidVariantScale(loaded.visualScale)) {
        return false;
    }
    loaded.kind = static_cast<ecs::ZombieVariantKind>(kind);
    *variant = loaded;
    return true;
}

bool WriteZombieSpecial(const ecs::ZombieSpecial& special,
                        BinaryWriter& writer) {
    const auto kind = static_cast<std::uint8_t>(special.kind);
    return kind <= static_cast<std::uint8_t>(ecs::ZombieSpecialKind::Breaker) &&
           Finite(special.cooldown) && special.cooldown >= 0.0f &&
           special.cooldown <= 3600.0f && Finite(special.windup) &&
           special.windup >= 0.0f && special.windup <= 60.0f &&
           Finite(special.windupDuration) && special.windupDuration >= 0.0f &&
           special.windupDuration <= 60.0f &&
           special.windup <= special.windupDuration + 0.001f &&
           writer.Write(kind) && writer.Write(special.cooldown) &&
           writer.Write(special.windup) && writer.Write(special.windupDuration) &&
           writer.Write(special.serial);
}

bool ReadZombieSpecial(BinaryReader& reader, ecs::ZombieSpecial* special) {
    std::uint8_t kind = 0;
    ecs::ZombieSpecial loaded;
    if (!reader.Read(kind) ||
        kind > static_cast<std::uint8_t>(ecs::ZombieSpecialKind::Breaker) ||
        !reader.Read(loaded.cooldown) || !reader.Read(loaded.windup) ||
        !reader.Read(loaded.windupDuration) || !reader.Read(loaded.serial) ||
        !Finite(loaded.cooldown) || loaded.cooldown < 0.0f ||
        loaded.cooldown > 3600.0f || !Finite(loaded.windup) ||
        loaded.windup < 0.0f || loaded.windup > 60.0f ||
        !Finite(loaded.windupDuration) || loaded.windupDuration < 0.0f ||
        loaded.windupDuration > 60.0f ||
        loaded.windup > loaded.windupDuration + 0.001f) {
        return false;
    }
    loaded.kind = static_cast<ecs::ZombieSpecialKind>(kind);
    *special = loaded;
    return true;
}

bool ValidWoundSource(ecs::DamageType type) {
    return type == ecs::DamageType::Blunt || type == ecs::DamageType::Claw ||
           type == ecs::DamageType::Bite;
}

bool ValidWounds(const ecs::Wounds& wounds) {
    if (wounds.count > ecs::kMaxWounds) {
        return false;
    }
    for (std::uint8_t i = 0; i < wounds.count; ++i) {
        const ecs::WoundEntry& entry = wounds.entries[i];
        if (!ValidWoundSource(entry.source) || !Finite(entry.severity) ||
            !Finite(entry.bleedRate) || !Finite(entry.bleedRemaining) ||
            !Finite(entry.bleedDuration) || entry.severity < 0.0f ||
            entry.severity > 1.0f || entry.bleedRate < 0.0f ||
            entry.bleedRate > 50.0f || entry.bleedRemaining < 0.0f ||
            entry.bleedDuration < 0.0f || entry.bleedDuration > 3600.0f ||
            entry.bleedRemaining > entry.bleedDuration ||
            (entry.bandaged && (entry.bleedRate > 0.0f ||
                                entry.bleedRemaining > 0.0f))) {
            return false;
        }
    }
    return true;
}

bool WriteWounds(const ecs::Wounds& wounds, BinaryWriter& writer) {
    if (!ValidWounds(wounds) || !writer.Write(wounds.count)) {
        return false;
    }
    for (std::uint8_t i = 0; i < wounds.count; ++i) {
        const ecs::WoundEntry& entry = wounds.entries[i];
        if (!writer.Write(static_cast<std::uint8_t>(entry.source)) ||
            !writer.Write(entry.severity) || !writer.Write(entry.bleedRate) ||
            !writer.Write(entry.bleedRemaining) || !writer.Write(entry.bleedDuration) ||
            !writer.Write(entry.bandaged)) {
            return false;
        }
    }
    return true;
}

bool ReadWounds(BinaryReader& reader, ecs::Wounds* wounds) {
    std::uint8_t count = 0;
    if (!reader.Read(count) || count > ecs::kMaxWounds) {
        return false;
    }
    ecs::Wounds loaded;
    loaded.count = count;
    for (std::uint8_t i = 0; i < count; ++i) {
        std::uint8_t source = 0;
        ecs::WoundEntry& entry = loaded.entries[i];
        if (!reader.Read(source) || !reader.Read(entry.severity) ||
            !reader.Read(entry.bleedRate) || !reader.Read(entry.bleedRemaining) ||
            !reader.Read(entry.bleedDuration) || !reader.Read(entry.bandaged) ||
            source > static_cast<std::uint8_t>(ecs::DamageType::Claw)) {
            return false;
        }
        entry.source = static_cast<ecs::DamageType>(source);
    }
    if (!ValidWounds(loaded)) {
        return false;
    }
    *wounds = loaded;
    return true;
}

bool ValidPain(const ecs::Pain& pain) {
    return Finite(pain.baseLevel) && pain.baseLevel >= 0.0f && pain.baseLevel <= 1.0f &&
           Finite(pain.reliefStrength) && pain.reliefStrength >= 0.0f &&
           pain.reliefStrength <= 1.0f && Finite(pain.reliefRemaining) &&
           pain.reliefRemaining >= 0.0f && Finite(pain.reliefDuration) &&
           pain.reliefDuration >= 0.0f && pain.reliefDuration <= 600.0f &&
           pain.reliefRemaining <= pain.reliefDuration + 0.001f &&
           Finite(pain.maxMoveSlow) && pain.maxMoveSlow >= 0.0f &&
           pain.maxMoveSlow <= 0.9f && Finite(pain.maxAttackCooldownIncrease) &&
           pain.maxAttackCooldownIncrease >= 0.0f &&
           pain.maxAttackCooldownIncrease <= 2.0f;
}

bool WritePain(const ecs::Pain& pain, BinaryWriter& writer) {
    return ValidPain(pain) && writer.Write(pain.baseLevel) &&
           writer.Write(pain.reliefStrength) && writer.Write(pain.reliefRemaining) &&
           writer.Write(pain.reliefDuration) && writer.Write(pain.maxMoveSlow) &&
           writer.Write(pain.maxAttackCooldownIncrease);
}

bool ReadPain(BinaryReader& reader, ecs::Pain* pain) {
    ecs::Pain loaded;
    if (!reader.Read(loaded.baseLevel) || !reader.Read(loaded.reliefStrength) ||
        !reader.Read(loaded.reliefRemaining) || !reader.Read(loaded.reliefDuration) ||
        !reader.Read(loaded.maxMoveSlow) ||
        !reader.Read(loaded.maxAttackCooldownIncrease) || !ValidPain(loaded)) {
        return false;
    }
    *pain = loaded;
    return true;
}

bool ValidInfection(const ecs::Infection& infection) {
    const auto stage = static_cast<std::uint8_t>(infection.stage);
    if (stage > static_cast<std::uint8_t>(ecs::InfectionStage::Severe) ||
        !Finite(infection.progress) || infection.progress < 0.0f ||
        infection.progress > 1.0f || !Finite(infection.slowStrength) ||
        infection.slowStrength < 0.0f || infection.slowStrength > 1.0f ||
        !Finite(infection.slowRemaining) || infection.slowRemaining < 0.0f ||
        infection.slowRemaining > 3600.0f ||
        !Finite(infection.slowDuration) || infection.slowDuration < 0.0f ||
        infection.slowDuration > 3600.0f ||
        infection.slowRemaining > infection.slowDuration + 0.001f ||
        !Finite(infection.timeToNextStage) ||
        infection.timeToNextStage < 0.0f ||
        infection.timeToNextStage > 86400.0f) {
        return false;
    }
    if (infection.stage == ecs::InfectionStage::None) {
        return infection.progress <= 0.0001f &&
               infection.slowStrength <= 0.0001f &&
               infection.slowRemaining <= 0.0001f &&
               infection.timeToNextStage <= 0.001f;
    }
    return true;
}

bool WriteInfection(const ecs::Infection& infection, BinaryWriter& writer) {
    if (!ValidInfection(infection)) {
        return false;
    }
    return writer.Write(static_cast<std::uint8_t>(infection.stage)) &&
           writer.Write(infection.progress) &&
           writer.Write(infection.slowStrength) &&
           writer.Write(infection.slowRemaining) &&
           writer.Write(infection.slowDuration) &&
           writer.Write(infection.timeToNextStage);
}

bool ReadInfection(BinaryReader& reader, ecs::Infection* infection) {
    std::uint8_t stage = 0;
    ecs::Infection loaded;
    if (!reader.Read(stage) || stage > static_cast<std::uint8_t>(
                                          ecs::InfectionStage::Severe) ||
        !reader.Read(loaded.progress) || !reader.Read(loaded.slowStrength) ||
        !reader.Read(loaded.slowRemaining) || !reader.Read(loaded.slowDuration) ||
        !reader.Read(loaded.timeToNextStage)) {
        return false;
    }
    loaded.stage = static_cast<ecs::InfectionStage>(stage);
    if (!ValidInfection(loaded)) {
        return false;
    }
    *infection = loaded;
    return true;
}

bool ValidClimate(const ecs::Climate& climate) {
    return Finite(climate.bodyC) && climate.bodyC >= 20.0f &&
           climate.bodyC <= 45.0f && Finite(climate.wetness) &&
           climate.wetness >= 0.0f && climate.wetness <= 1.0f;
}

bool WriteClimate(const ecs::Climate& climate, BinaryWriter& writer) {
    return ValidClimate(climate) && writer.Write(climate.bodyC) &&
           writer.Write(climate.wetness);
}

bool ReadClimate(BinaryReader& reader, ecs::Climate* climate) {
    ecs::Climate loaded;
    if (!reader.Read(loaded.bodyC) || !reader.Read(loaded.wetness) ||
        !ValidClimate(loaded)) {
        return false;
    }
    *climate = loaded;
    return true;
}

bool ValidProgressionId(std::string_view id) {
    if (id.empty() || id.size() > 64) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

bool ValidProgression(const ecs::Progression& progression) {
    if (progression.level < 1 || progression.level > 99 ||
        !Finite(progression.xp) || progression.xp < 0.0f ||
        progression.xp > 1.0e9f || progression.perkPoints < 0 ||
        progression.perkPoints > 99 || progression.perks.size() > 64) {
        return false;
    }
    for (const ecs::SkillProgress& skill : progression.skills) {
        if (skill.level < 0 || skill.level > 99 || !Finite(skill.xp) ||
            skill.xp < 0.0f || skill.xp > 1.0e9f) {
            return false;
        }
    }
    std::unordered_set<std::string> seen;
    for (const std::string& perk : progression.perks) {
        if (!ValidProgressionId(perk) || !seen.insert(perk).second) {
            return false;
        }
    }
    return true;
}

bool WriteProgression(const ecs::Progression& progression,
                      BinaryWriter& writer) {
    if (!ValidProgression(progression)) {
        return false;
    }
    std::vector<std::string> perks = progression.perks;
    std::sort(perks.begin(), perks.end());
    if (!writer.Write(static_cast<std::int32_t>(progression.level)) ||
        !writer.Write(progression.xp) ||
        !writer.Write(static_cast<std::int32_t>(progression.perkPoints))) {
        return false;
    }
    for (const ecs::SkillProgress& skill : progression.skills) {
        if (!writer.Write(static_cast<std::int32_t>(skill.level)) ||
            !writer.Write(skill.xp)) {
            return false;
        }
    }
    if (!writer.Write(static_cast<std::uint32_t>(perks.size()))) {
        return false;
    }
    for (const std::string& perk : perks) {
        if (!writer.WriteString(perk, 64)) {
            return false;
        }
    }
    return true;
}

bool ReadProgression(BinaryReader& reader, ecs::Progression* progression) {
    std::int32_t level = 0;
    std::int32_t perkPoints = 0;
    ecs::Progression loaded;
    if (!reader.Read(level) || !reader.Read(loaded.xp) || !reader.Read(perkPoints) ||
        level < 1 || level > 99 || perkPoints < 0 || perkPoints > 99) {
        return false;
    }
    loaded.level = level;
    loaded.perkPoints = perkPoints;
    for (ecs::SkillProgress& skill : loaded.skills) {
        std::int32_t skillLevel = 0;
        if (!reader.Read(skillLevel) || !reader.Read(skill.xp) || skillLevel < 0 ||
            skillLevel > 99) {
            return false;
        }
        skill.level = skillLevel;
    }
    std::uint32_t perkCount = 0;
    if (!reader.Read(perkCount) || perkCount > 64 ||
        perkCount > reader.RemainingInSegment() / 5) {
        return false;
    }
    std::unordered_set<std::string> seen;
    loaded.perks.reserve(perkCount);
    for (std::uint32_t i = 0; i < perkCount; ++i) {
        std::string perk;
        if (!reader.ReadString(perk, 64) || !ValidProgressionId(perk) ||
            !seen.insert(perk).second) {
            return false;
        }
        loaded.perks.push_back(std::move(perk));
    }
    if (!ValidProgression(loaded)) {
        return false;
    }
    *progression = std::move(loaded);
    return true;
}

bool WritePoiGuard(const ecs::PoiGuard& guard, BinaryWriter& writer) {
    return ValidRecipeId(guard.poiId) &&
           writer.WriteString(guard.poiId, kMaxItemDefIdBytes);
}

bool ReadPoiGuard(BinaryReader& reader, ecs::PoiGuard* guard) {
    std::string poiId;
    if (!reader.ReadString(poiId, kMaxItemDefIdBytes) || !ValidRecipeId(poiId)) {
        return false;
    }
    guard->poiId = std::move(poiId);
    return true;
}

bool ValidPoiStageEnemy(const ecs::PoiStageEnemy& enemy) {
    return ValidRecipeId(enemy.poiId) && ValidRecipeId(enemy.stageId) &&
           static_cast<std::uint8_t>(enemy.role) <=
               static_cast<std::uint8_t>(game::PoiStageEnemyRole::Summon);
}

bool WritePoiStageEnemy(const ecs::PoiStageEnemy& enemy,
                        BinaryWriter& writer) {
    if (!ValidPoiStageEnemy(enemy)) {
        return false;
    }
    const auto role = static_cast<std::uint8_t>(enemy.role);
    return writer.WriteString(enemy.poiId, kMaxItemDefIdBytes) &&
           writer.WriteString(enemy.stageId, kMaxItemDefIdBytes) &&
           writer.Write(role);
}

bool ReadPoiStageEnemy(BinaryReader& reader, ecs::PoiStageEnemy* enemy) {
    std::string poiId;
    std::string stageId;
    std::uint8_t role = 0;
    if (!reader.ReadString(poiId, kMaxItemDefIdBytes) ||
        !reader.ReadString(stageId, kMaxItemDefIdBytes) || !reader.Read(role) ||
        role > static_cast<std::uint8_t>(game::PoiStageEnemyRole::Summon)) {
        return false;
    }
    enemy->poiId = std::move(poiId);
    enemy->stageId = std::move(stageId);
    enemy->role = static_cast<game::PoiStageEnemyRole>(role);
    return ValidPoiStageEnemy(*enemy);
}

bool WriteRandomEventActor(const ecs::RandomEventActor& actor,
                           BinaryWriter& writer) {
    return ValidRecipeId(actor.eventId) && actor.index >= 0 &&
           actor.index <= 1000 &&
           writer.WriteString(actor.eventId, kMaxItemDefIdBytes) &&
           writer.Write(static_cast<std::int32_t>(actor.index));
}

bool ReadRandomEventActor(BinaryReader& reader,
                          ecs::RandomEventActor* actor) {
    std::string eventId;
    std::int32_t index = 0;
    if (!reader.ReadString(eventId, kMaxItemDefIdBytes) ||
        !ValidRecipeId(eventId) || !reader.Read(index) || index < 0 ||
        index > 1000) {
        return false;
    }
    actor->eventId = std::move(eventId);
    actor->index = index;
    return true;
}

bool WritePoiTasks(const ecs::PoiTasks& tasks, BinaryWriter& writer) {
    std::vector<std::string> ids = tasks.completed;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::vector<std::string> claims = tasks.rewardClaims;
    std::sort(claims.begin(), claims.end());
    claims.erase(std::unique(claims.begin(), claims.end()), claims.end());
    if (ids.size() > 64 || claims.size() > 256) {
        return false;
    }
    if (!writer.Write(static_cast<std::uint32_t>(ids.size()))) {
        return false;
    }
    for (const std::string& id : ids) {
        if (!ValidRecipeId(id) || !writer.WriteString(id, kMaxItemDefIdBytes)) {
            return false;
        }
    }
    if (!writer.Write(static_cast<std::uint32_t>(claims.size()))) {
        return false;
    }
    for (const std::string& claim : claims) {
        if (!ValidPoiClaimKey(claim) ||
            !writer.WriteString(claim, kMaxPoiClaimBytes)) {
            return false;
        }
    }
    return true;
}

bool ValidPoiProgress(const game::PoiProgress& poi) {
    if (!ValidRecipeId(poi.poiId) || poi.currentStage < -1 ||
        poi.currentStage > 8 || poi.failedStage < -1 ||
        poi.failedStage > 8 || poi.completedStages.size() > 8 ||
        poi.acquiredKeys.size() > 8 || poi.attempts < 0 ||
        poi.attempts > 1000000 || poi.stageKills < 0 ||
        poi.stageKills > 9999 || poi.lastLiveGuards < 0 ||
        poi.lastLiveGuards > 9999 ||
        (poi.completed && poi.currentStage != -1) ||
        (poi.awaitingChoice &&
         (poi.completed || poi.currentStage < 0)) ||
        (!poi.chosenBranch.empty() && !ValidRecipeId(poi.chosenBranch))) {
        return false;
    }
    std::set<std::string> stages;
    for (const std::string& stageId : poi.completedStages) {
        if (!ValidRecipeId(stageId) || !stages.insert(stageId).second) {
            return false;
        }
    }
    std::set<std::string> keys;
    for (const std::string& keyId : poi.acquiredKeys) {
        if (!ValidRecipeId(keyId) || !keys.insert(keyId).second) {
            return false;
        }
    }
    return true;
}

bool ValidPoiWorldProgress(const ecs::PoiWorldProgress& state) {
    if (state.pois.size() > ecs::kMaxPoiWorldProgress) {
        return false;
    }
    std::set<std::string> seen;
    for (const game::PoiProgress& poi : state.pois) {
        if (!ValidPoiProgress(poi) || !seen.insert(poi.poiId).second) {
            return false;
        }
    }
    return true;
}

bool WritePoiWorldProgress(const ecs::PoiWorldProgress& state,
                           BinaryWriter& writer) {
    if (!ValidPoiWorldProgress(state) ||
        !writer.Write(static_cast<std::uint32_t>(state.pois.size()))) {
        return false;
    }
    for (const game::PoiProgress& poi : state.pois) {
        if (!writer.WriteString(poi.poiId, kMaxItemDefIdBytes) ||
            !writer.Write(static_cast<std::int32_t>(poi.currentStage)) ||
            !writer.Write(static_cast<std::int32_t>(poi.failedStage)) ||
            !writer.Write(static_cast<std::uint16_t>(
                poi.completedStages.size()))) {
            return false;
        }
        for (const std::string& stageId : poi.completedStages) {
            if (!writer.WriteString(stageId, kMaxItemDefIdBytes)) {
                return false;
            }
        }
        if (!writer.WriteString(poi.chosenBranch, kMaxItemDefIdBytes) ||
            !writer.Write(static_cast<std::uint16_t>(
                poi.acquiredKeys.size()))) {
            return false;
        }
        for (const std::string& keyId : poi.acquiredKeys) {
            if (!writer.WriteString(keyId, kMaxItemDefIdBytes)) {
                return false;
            }
        }
        if (!writer.Write(poi.powered) || !writer.Write(poi.failed) ||
            !writer.Write(poi.awaitingChoice) || !writer.Write(poi.completed) ||
            !writer.Write(static_cast<std::int32_t>(poi.attempts)) ||
            !writer.Write(static_cast<std::int32_t>(poi.stageKills)) ||
            !writer.Write(static_cast<std::int32_t>(poi.lastLiveGuards))) {
            return false;
        }
    }
    return true;
}

bool ReadPoiWorldProgress(BinaryReader& reader,
                          ecs::PoiWorldProgress* state) {
    std::uint32_t count = 0;
    if (!reader.Read(count) || count > ecs::kMaxPoiWorldProgress ||
        count > reader.RemainingInSegment() / 16) {
        return false;
    }
    ecs::PoiWorldProgress loaded;
    loaded.pois.reserve(count);
    std::set<std::string> seen;
    for (std::uint32_t i = 0; i < count; ++i) {
        game::PoiProgress poi;
        std::int32_t currentStage = -1;
        std::int32_t failedStage = -1;
        std::uint16_t completedCount = 0;
        if (!reader.ReadString(poi.poiId, kMaxItemDefIdBytes) ||
            !reader.Read(currentStage) || !reader.Read(failedStage) ||
            !reader.Read(completedCount) || completedCount > 8 ||
            completedCount > reader.RemainingInSegment() / 4) {
            return false;
        }
        poi.currentStage = currentStage;
        poi.failedStage = failedStage;
        poi.completedStages.reserve(completedCount);
        for (std::uint16_t j = 0; j < completedCount; ++j) {
            std::string stageId;
            if (!reader.ReadString(stageId, kMaxItemDefIdBytes)) {
                return false;
            }
            poi.completedStages.push_back(std::move(stageId));
        }
        std::uint16_t keyCount = 0;
        if (!reader.ReadString(poi.chosenBranch, kMaxItemDefIdBytes) ||
            !reader.Read(keyCount) || keyCount > 8 ||
            keyCount > reader.RemainingInSegment() / 4) {
            return false;
        }
        poi.acquiredKeys.reserve(keyCount);
        for (std::uint16_t j = 0; j < keyCount; ++j) {
            std::string keyId;
            if (!reader.ReadString(keyId, kMaxItemDefIdBytes)) {
                return false;
            }
            poi.acquiredKeys.push_back(std::move(keyId));
        }
        std::int32_t attempts = 0;
        std::int32_t stageKills = 0;
        std::int32_t lastLiveGuards = 0;
        if (!reader.Read(poi.powered) || !reader.Read(poi.failed) ||
            !reader.Read(poi.awaitingChoice) || !reader.Read(poi.completed) ||
            !reader.Read(attempts) || !reader.Read(stageKills) ||
            !reader.Read(lastLiveGuards)) {
            return false;
        }
        poi.attempts = attempts;
        poi.stageKills = stageKills;
        poi.lastLiveGuards = lastLiveGuards;
        if (!ValidPoiProgress(poi) || !seen.insert(poi.poiId).second) {
            return false;
        }
        loaded.pois.push_back(std::move(poi));
    }
    *state = std::move(loaded);
    return true;
}

bool ReadPoiTasks(BinaryReader& reader, ecs::PoiTasks* tasks) {
    std::uint32_t count = 0;
    if (!reader.Read(count) || count > 64 ||
        count > reader.RemainingInSegment() / 5) {
        return false;
    }
    ecs::PoiTasks loaded;
    loaded.completed.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::string id;
        if (!reader.ReadString(id, kMaxItemDefIdBytes) || !ValidRecipeId(id) ||
            std::find(loaded.completed.begin(), loaded.completed.end(), id) !=
                loaded.completed.end()) {
            return false;
        }
        loaded.completed.push_back(std::move(id));
    }
    if (reader.Header().version >= kSaveComponentVersionPoiTasks) {
        std::uint32_t claimCount = 0;
        if (!reader.Read(claimCount) || claimCount > 256 ||
            claimCount > reader.RemainingInSegment() / 3) {
            return false;
        }
        loaded.rewardClaims.reserve(claimCount);
        for (std::uint32_t i = 0; i < claimCount; ++i) {
            std::string claim;
            if (!reader.ReadString(claim, kMaxPoiClaimBytes) ||
                !ValidPoiClaimKey(claim) ||
                std::find(loaded.rewardClaims.begin(),
                          loaded.rewardClaims.end(),
                          claim) != loaded.rewardClaims.end()) {
                return false;
            }
            loaded.rewardClaims.push_back(std::move(claim));
        }
    }
    *tasks = std::move(loaded);
    return true;
}

bool ValidMissionLogDay(int day) {
    return day >= 1 && day <= 100000;
}

bool WriteMissionLog(const ecs::MissionLog& log, BinaryWriter& writer) {
    if (!ValidMissionLogDay(log.lastDay) || log.missions.size() > 64 ||
        log.randomDay < 0 || log.randomDay > 100000) {
        return false;
    }
    std::vector<const ecs::MissionRunState*> states;
    states.reserve(log.missions.size());
    for (const ecs::MissionRunState& state : log.missions) {
        if (!ValidRecipeId(state.id) || !ValidMissionLogDay(state.day) ||
            state.progress.size() > 8 ||
            static_cast<std::uint8_t>(state.status) > 2 ||
            (!state.settlementId.empty() &&
             !ValidSettlementStateId(state.settlementId))) {
            return false;
        }
        for (const int value : state.progress) {
            if (value < 0 || value > 1000000) {
                return false;
            }
        }
        states.push_back(&state);
    }
    std::sort(states.begin(), states.end(),
              [](const ecs::MissionRunState* left,
                 const ecs::MissionRunState* right) {
                  return left->id < right->id;
              });
    for (std::size_t i = 1; i < states.size(); ++i) {
        if (states[i - 1]->id == states[i]->id) {
            return false;
        }
    }
    if (!writer.Write(static_cast<std::int32_t>(log.lastDay)) ||
        !writer.Write(static_cast<std::int32_t>(log.randomDay)) ||
        !writer.Write(static_cast<std::uint32_t>(states.size()))) {
        return false;
    }
    for (const ecs::MissionRunState* state : states) {
        if (!writer.WriteString(state->id, kMaxItemDefIdBytes) ||
            !writer.Write(static_cast<std::uint8_t>(state->status)) ||
            !writer.Write(static_cast<std::int32_t>(state->day)) ||
            !writer.Write(static_cast<std::uint8_t>(state->rewardClaimed ? 1 : 0)) ||
            !writer.Write(static_cast<std::uint32_t>(state->progress.size()))) {
            return false;
        }
        for (const int value : state->progress) {
            if (!writer.Write(static_cast<std::int32_t>(value))) {
                return false;
            }
        }
        if (!writer.WriteString(state->settlementId,
                                game::kMaxSettlementReputationIdBytes)) {
            return false;
        }
    }
    return true;
}

bool ReadMissionLog(BinaryReader& reader, ecs::MissionLog* log) {
    std::int32_t lastDay = 0;
    std::int32_t randomDay = 0;
    std::uint32_t count = 0;
    if (!reader.Read(lastDay) || !ValidMissionLogDay(lastDay) ||
        !reader.Read(randomDay) || randomDay < 0 || randomDay > 100000 ||
        !reader.Read(count) || count > 64 ||
        count > reader.RemainingInSegment() / 10) {
        return false;
    }
    ecs::MissionLog loaded;
    loaded.lastDay = lastDay;
    loaded.randomDay = randomDay;
    loaded.missions.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        ecs::MissionRunState state;
        std::uint8_t status = 0;
        std::int32_t day = 0;
        std::uint8_t rewardClaimed = 0;
        std::uint32_t progressCount = 0;
        if (!reader.ReadString(state.id, kMaxItemDefIdBytes) ||
            !ValidRecipeId(state.id) || !reader.Read(status) || status > 2 ||
            !reader.Read(day) || !ValidMissionLogDay(day) ||
            !reader.Read(rewardClaimed) || rewardClaimed > 1 ||
            !reader.Read(progressCount) || progressCount > 8 ||
            progressCount > reader.RemainingInSegment() / 4) {
            return false;
        }
        state.status = static_cast<ecs::MissionRunStatus>(status);
        state.day = day;
        state.rewardClaimed = rewardClaimed != 0;
        state.progress.reserve(progressCount);
        for (std::uint32_t p = 0; p < progressCount; ++p) {
            std::int32_t value = 0;
            if (!reader.Read(value) || value < 0 || value > 1000000) {
                return false;
            }
            state.progress.push_back(value);
        }
        if (reader.Header().version >= kSaveComponentVersionMissionLog) {
            if (!reader.ReadString(state.settlementId,
                                   game::kMaxSettlementReputationIdBytes) ||
                (!state.settlementId.empty() &&
                 !ValidSettlementStateId(state.settlementId))) {
                return false;
            }
        }
        if (std::any_of(loaded.missions.begin(), loaded.missions.end(),
                        [&](const ecs::MissionRunState& existing) {
                            return existing.id == state.id;
                        })) {
            return false;
        }
        loaded.missions.push_back(std::move(state));
    }
    *log = std::move(loaded);
    return true;
}

bool WriteModData(const ecs::ModData& data, BinaryWriter& writer) {
    if (!ecs::IsValidModData(data) ||
        !writer.Write(static_cast<std::uint32_t>(data.mods.size()))) {
        return false;
    }
    for (const auto& [modId, entries] : data.mods) {
        if (!writer.WriteString(modId, ecs::kMaxModDataModIdBytes) ||
            !writer.Write(static_cast<std::uint32_t>(entries.size()))) {
            return false;
        }
        for (const ecs::ModDataEntry& entry : entries) {
            if (!writer.WriteString(entry.key, ecs::kMaxModDataKeyBytes) ||
                !writer.WriteString(entry.value, ecs::kMaxModDataValueBytes)) {
                return false;
            }
        }
    }
    return true;
}

bool ReadModData(BinaryReader& reader, ecs::ModData* data) {
    std::uint32_t modCount = 0;
    if (!reader.Read(modCount) ||
        modCount > ecs::kMaxModDataMods ||
        modCount > reader.RemainingInSegment() / 9u) {
        return false;
    }
    ecs::ModData loaded;
    loaded.mods.reserve(std::min<std::uint32_t>(modCount, 256));
    std::unordered_set<std::string> modIds;
    std::size_t totalBytes = 0;
    for (std::uint32_t i = 0; i < modCount; ++i) {
        std::string modId;
        std::uint32_t entryCount = 0;
        if (!reader.ReadString(modId, ecs::kMaxModDataModIdBytes) ||
            !ecs::IsValidModDataId(modId) ||
            !modIds.insert(modId).second || !reader.Read(entryCount) ||
            entryCount > ecs::kMaxModDataEntriesPerMod ||
            entryCount > reader.RemainingInSegment() / 9u) {
            return false;
        }
        std::vector<ecs::ModDataEntry> entries;
        entries.reserve(
            std::min<std::uint32_t>(entryCount,
                                    static_cast<std::uint32_t>(
                                        ecs::kMaxModDataEntriesPerMod)));
        std::unordered_set<std::string> keys;
        for (std::uint32_t j = 0; j < entryCount; ++j) {
            ecs::ModDataEntry entry;
            if (!reader.ReadString(entry.key, ecs::kMaxModDataKeyBytes) ||
                !reader.ReadString(entry.value, ecs::kMaxModDataValueBytes) ||
                !ecs::IsValidModDataKey(entry.key) ||
                !keys.insert(entry.key).second) {
                return false;
            }
            totalBytes += entry.key.size() + entry.value.size();
            if (totalBytes > ecs::kMaxModDataTotalBytes) {
                return false;
            }
            entries.push_back(std::move(entry));
        }
        loaded.mods.emplace_back(std::move(modId), std::move(entries));
    }
    if (!ecs::IsValidModData(loaded)) {
        return false;
    }
    *data = std::move(loaded);
    return true;
}

}  // namespace

bool ComponentRegistry::Register(ComponentCodec codec, std::string* error) {
    if (codec.type == 0) {
        return Fail(error, "组件类型 ID 不能为 0");
    }
    if (codec.name.empty()) {
        return Fail(error, "组件名称不能为空");
    }
    if (codec.version == 0) {
        return Fail(error, "组件版本不能为 0");
    }
    if (codec.minimumVersion == 0 || codec.minimumVersion > codec.version) {
        return Fail(error, "组件最低兼容版本非法: " + codec.name);
    }
    if (!codec.has || !codec.write || !codec.read) {
        return Fail(error, "组件注册缺少 has/write/read");
    }
    if (m_byType.contains(codec.type)) {
        return Fail(error, "组件类型重复注册: " + codec.name);
    }
    m_byType.emplace(codec.type, m_codecs.size());
    m_codecs.push_back(std::move(codec));
    return true;
}

const ComponentCodec* ComponentRegistry::Find(std::uint32_t type) const {
    const auto it = m_byType.find(type);
    return it == m_byType.end() ? nullptr : &m_codecs[it->second];
}

bool RegisterBuiltinComponents(ComponentRegistry& registry, std::string* error) {
    if (!registry.Register<ecs::Position>(
            kSaveComponentPosition, "Position",
            [](const ecs::Position& position, BinaryWriter& writer) {
                return Finite(position.x) && Finite(position.y) &&
                       writer.Write(position.x) && writer.Write(position.y) &&
                       writer.Write(position.z);
            },
            [](BinaryReader& reader, ecs::Position* position) {
                float x = 0.0f;
                float y = 0.0f;
                int z = 0;
                if (!reader.Read(x) || !reader.Read(y) || !reader.Read(z) ||
                    !Finite(x) || !Finite(y)) {
                    return false;
                }
                *position = ecs::Position{x, y, z};
                return true;
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Velocity>(
            kSaveComponentVelocity, "Velocity",
            [](const ecs::Velocity& velocity, BinaryWriter& writer) {
                return Finite(velocity.x) && Finite(velocity.y) &&
                       writer.Write(velocity.x) && writer.Write(velocity.y);
            },
            [](BinaryReader& reader, ecs::Velocity* velocity) {
                float x = 0.0f;
                float y = 0.0f;
                if (!reader.Read(x) || !reader.Read(y) || !Finite(x) ||
                    !Finite(y)) {
                    return false;
                }
                *velocity = ecs::Velocity{x, y};
                return true;
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::SpriteRef>(
            kSaveComponentSprite, "SpriteRef",
            [](const ecs::SpriteRef& sprite, BinaryWriter& writer) {
                return writer.Write(sprite.sprite) && writer.Write(sprite.layer) &&
                       writer.Write(sprite.tint);
            },
            [](BinaryReader& reader, ecs::SpriteRef* sprite) {
                std::uint32_t texture = 0;
                std::int32_t layer = 0;
                std::uint32_t tint = 0;
                if (!reader.Read(texture) || !reader.Read(layer) ||
                    !reader.Read(tint)) {
                    return false;
                }
                *sprite = ecs::SpriteRef{texture, layer, tint};
                return true;
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Facing>(
            kSaveComponentFacing, "Facing",
            [](const ecs::Facing& facing, BinaryWriter& writer) {
                const auto value = static_cast<std::uint8_t>(facing.dir);
                return value <= static_cast<std::uint8_t>(ecs::Direction::Up) &&
                       writer.Write(value);
            },
            [](BinaryReader& reader, ecs::Facing* facing) {
                std::uint8_t value = 0;
                if (!reader.Read(value) ||
                    value > static_cast<std::uint8_t>(ecs::Direction::Up)) {
                    return false;
                }
                facing->dir = static_cast<ecs::Direction>(value);
                return true;
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Health>(
            kSaveComponentHealth, "Health",
            [](const ecs::Health& health, BinaryWriter& writer) {
                return ValidHealth(health) && writer.Write(health.current) &&
                       writer.Write(health.max);
            },
            [](BinaryReader& reader, ecs::Health* health) {
                float current = 0.0f;
                float maximum = 0.0f;
                if (!reader.Read(current) || !reader.Read(maximum)) {
                    return false;
                }
                const ecs::Health value{current, maximum};
                if (!ValidHealth(value)) {
                    return false;
                }
                *health = value;
                return true;
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Needs>(
            kSaveComponentNeeds, "Needs",
            [](const ecs::Needs& needs, BinaryWriter& writer) {
                return ValidNeeds(needs) && writer.Write(needs.hunger) &&
                       writer.Write(needs.thirst) && writer.Write(needs.fatigue);
            },
            [](BinaryReader& reader, ecs::Needs* needs) {
                ecs::Needs value;
                if (!reader.Read(value.hunger) || !reader.Read(value.thirst) ||
                    !reader.Read(value.fatigue) || !ValidNeeds(value)) {
                    return false;
                }
                *needs = value;
                return true;
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::FoodState>(
            kSaveComponentFoodState, "FoodState",
            [](const ecs::FoodState& food, BinaryWriter& writer) {
                return WriteFoodState(food, writer);
            },
            [](BinaryReader& reader, ecs::FoodState* food) {
                return ReadFoodState(reader, food);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::PlayerTag>(
            kSaveComponentPlayerTag, "PlayerTag",
            [](const ecs::PlayerTag&, BinaryWriter&) { return true; },
            [](BinaryReader&, ecs::PlayerTag*) { return true; },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::KnownRecipes>(
            kSaveComponentKnownRecipes, "KnownRecipes",
            [](const ecs::KnownRecipes& known, BinaryWriter& writer) {
                return WriteKnownRecipes(known, writer);
            },
            [](BinaryReader& reader, ecs::KnownRecipes* known) {
                return ReadKnownRecipes(reader, known);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::CraftingJob>(
            kSaveComponentCraftingJob, "CraftingJob",
            [](const ecs::CraftingJob& job, BinaryWriter& writer) {
                return WriteCraftingJob(job, writer);
            },
            [](BinaryReader& reader, ecs::CraftingJob* job) {
                return ReadCraftingJob(reader, job);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Objective>(
            kSaveComponentObjective, "Objective",
            [](const ecs::Objective& objective, BinaryWriter& writer) {
                return WriteObjective(objective, writer);
            },
            [](BinaryReader& reader, ecs::Objective* objective) {
                return ReadObjective(reader, objective);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::ZombieVariant>(
            kSaveComponentZombieVariant, "ZombieVariant",
            [](const ecs::ZombieVariant& variant, BinaryWriter& writer) {
                return WriteZombieVariant(variant, writer);
            },
            [](BinaryReader& reader, ecs::ZombieVariant* variant) {
                return ReadZombieVariant(reader, variant);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::ZombieSpecial>(
            kSaveComponentZombieSpecial, "ZombieSpecial",
            [](const ecs::ZombieSpecial& special, BinaryWriter& writer) {
                return WriteZombieSpecial(special, writer);
            },
            [](BinaryReader& reader, ecs::ZombieSpecial* special) {
                return ReadZombieSpecial(reader, special);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Wounds>(
            kSaveComponentWounds, "Wounds",
            [](const ecs::Wounds& wounds, BinaryWriter& writer) {
                return WriteWounds(wounds, writer);
            },
            [](BinaryReader& reader, ecs::Wounds* wounds) {
                return ReadWounds(reader, wounds);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Pain>(
            kSaveComponentPain, "Pain",
            [](const ecs::Pain& pain, BinaryWriter& writer) {
                return WritePain(pain, writer);
            },
            [](BinaryReader& reader, ecs::Pain* pain) { return ReadPain(reader, pain); },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Infection>(
            kSaveComponentInfection, "Infection",
            [](const ecs::Infection& infection, BinaryWriter& writer) {
                return WriteInfection(infection, writer);
            },
            [](BinaryReader& reader, ecs::Infection* infection) {
                return ReadInfection(reader, infection);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Climate>(
            kSaveComponentClimate, "Climate",
            [](const ecs::Climate& climate, BinaryWriter& writer) {
                return WriteClimate(climate, writer);
            },
            [](BinaryReader& reader, ecs::Climate* climate) {
                return ReadClimate(reader, climate);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Progression>(
            kSaveComponentProgression, "Progression",
            [](const ecs::Progression& progression, BinaryWriter& writer) {
                return WriteProgression(progression, writer);
            },
            [](BinaryReader& reader, ecs::Progression* progression) {
                return ReadProgression(reader, progression);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::PoiGuard>(
            kSaveComponentPoiGuard, "PoiGuard",
            [](const ecs::PoiGuard& guard, BinaryWriter& writer) {
                return WritePoiGuard(guard, writer);
            },
            [](BinaryReader& reader, ecs::PoiGuard* guard) {
                return ReadPoiGuard(reader, guard);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::RandomEventActor>(
            kSaveComponentRandomEventActor, "RandomEventActor",
            [](const ecs::RandomEventActor& actor, BinaryWriter& writer) {
                return WriteRandomEventActor(actor, writer);
            },
            [](BinaryReader& reader, ecs::RandomEventActor* actor) {
                return ReadRandomEventActor(reader, actor);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::PoiTasks>(
            kSaveComponentPoiTasks, "PoiTasks",
            [](const ecs::PoiTasks& tasks, BinaryWriter& writer) {
                return WritePoiTasks(tasks, writer);
            },
            [](BinaryReader& reader, ecs::PoiTasks* tasks) {
                return ReadPoiTasks(reader, tasks);
            },
            kSaveComponentVersionPoiTasks, error, kSaveComponentVersion)) {
        return false;
    }

    if (!registry.Register<ecs::PoiWorldProgress>(
            kSaveComponentPoiWorldProgress, "PoiWorldProgress",
            [](const ecs::PoiWorldProgress& state, BinaryWriter& writer) {
                return WritePoiWorldProgress(state, writer);
            },
            [](BinaryReader& reader, ecs::PoiWorldProgress* state) {
                return ReadPoiWorldProgress(reader, state);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::PoiStageEnemy>(
            kSaveComponentPoiStageEnemy, "PoiStageEnemy",
            [](const ecs::PoiStageEnemy& enemy, BinaryWriter& writer) {
                return WritePoiStageEnemy(enemy, writer);
            },
            [](BinaryReader& reader, ecs::PoiStageEnemy* enemy) {
                return ReadPoiStageEnemy(reader, enemy);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::MissionLog>(
            kSaveComponentMissionLog, "MissionLog",
            [](const ecs::MissionLog& log, BinaryWriter& writer) {
                return WriteMissionLog(log, writer);
            },
            [](BinaryReader& reader, ecs::MissionLog* log) {
                return ReadMissionLog(reader, log);
            },
            kSaveComponentVersionMissionLog, error,
            kSaveComponentVersion)) {
        return false;
    }

    if (!registry.Register<ecs::FarmPlot>(
            kSaveComponentFarmPlot, "FarmPlot",
            [](const ecs::FarmPlot& plot, BinaryWriter& writer) {
                return WriteFarmPlot(plot, writer);
            },
            [](BinaryReader& reader, ecs::FarmPlot* plot) {
                return ReadFarmPlot(reader, plot);
            },
            kSaveComponentVersionFarmPlot, error,
            kSaveComponentVersion)) {
        return false;
    }

    if (!registry.Register<ecs::Generator>(
            kSaveComponentGenerator, "Generator",
            [](const ecs::Generator& generator, BinaryWriter& writer) {
                return WriteGenerator(generator, writer);
            },
            [](BinaryReader& reader, ecs::Generator* generator) {
                return ReadGenerator(reader, generator);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::ColdStorage>(
            kSaveComponentColdStorage, "ColdStorage",
            [](const ecs::ColdStorage& storage, BinaryWriter& writer) {
                return WriteColdStorage(storage, writer);
            },
            [](BinaryReader& reader, ecs::ColdStorage* storage) {
                return ReadColdStorage(reader, storage);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Floodlight>(
            kSaveComponentFloodlight, "Floodlight",
            [](const ecs::Floodlight& floodlight, BinaryWriter& writer) {
                return WriteFloodlight(floodlight, writer);
            },
            [](BinaryReader& reader, ecs::Floodlight* floodlight) {
                return ReadFloodlight(reader, floodlight);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::SpikeTrap>(
            kSaveComponentSpikeTrap, "SpikeTrap",
            [](const ecs::SpikeTrap& trap, BinaryWriter& writer) {
                return WriteSpikeTrap(trap, writer);
            },
            [](BinaryReader& reader, ecs::SpikeTrap* trap) {
                return ReadSpikeTrap(reader, trap);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::PlacedTrap>(
            kSaveComponentPlacedTrap, "PlacedTrap",
            [](const ecs::PlacedTrap& trap, BinaryWriter& writer) {
                return WritePlacedTrap(trap, writer);
            },
            [](BinaryReader& reader, ecs::PlacedTrap* trap) {
                return ReadPlacedTrap(reader, trap);
            },
            kSaveComponentVersionPlacedTrap, error,
            kSaveComponentVersion)) {
        return false;
    }

    if (!registry.Register<ecs::MerchantStock>(
            kSaveComponentMerchantStock, "MerchantStock",
            [](const ecs::MerchantStock& stock, BinaryWriter& writer) {
                return WriteMerchantStock(stock, writer);
            },
            [](BinaryReader& reader, ecs::MerchantStock* stock) {
                return ReadMerchantStock(reader, stock);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<game::SettlementState>(
            kSaveComponentSettlementState, "SettlementState",
            [](const game::SettlementState& state, BinaryWriter& writer) {
                return WriteSettlementState(state, writer);
            },
            [](BinaryReader& reader, game::SettlementState* state) {
                return ReadSettlementState(reader, state);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<game::SettlementReputation>(
            kSaveComponentSettlementReputation, "SettlementReputation",
            [](const game::SettlementReputation& reputation,
               BinaryWriter& writer) {
                return WriteSettlementReputation(reputation, writer);
            },
            [](BinaryReader& reader, game::SettlementReputation* reputation) {
                return ReadSettlementReputation(reader, reputation);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::ForageState>(
            kSaveComponentForage, "ForageState",
            [](const ecs::ForageState& state, BinaryWriter& writer) {
                return WriteForageState(state, writer);
            },
            [](BinaryReader& reader, ecs::ForageState* state) {
                return ReadForageState(reader, state);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::FireAnchor>(
            kSaveComponentFireAnchor, "FireAnchor",
            [](const ecs::FireAnchor& anchor, BinaryWriter& writer) {
                return WriteFireAnchor(anchor, writer);
            },
            [](BinaryReader& reader, ecs::FireAnchor* anchor) {
                return ReadFireAnchor(reader, anchor);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::FireSource>(
            kSaveComponentFireSource, "FireSource",
            [](const ecs::FireSource& source, BinaryWriter& writer) {
                return WriteFireSource(source, writer);
            },
            [](BinaryReader& reader, ecs::FireSource* source) {
                return ReadFireSource(reader, source);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::BurningZombie>(
            kSaveComponentBurningZombie, "BurningZombie",
            [](const ecs::BurningZombie& burning, BinaryWriter& writer) {
                return WriteBurningZombie(burning, writer);
            },
            [](BinaryReader& reader, ecs::BurningZombie* burning) {
                return ReadBurningZombie(reader, burning);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::ItemInstance>(
            kSaveComponentItem, "ItemInstance",
            [](const ecs::ItemInstance& item, BinaryWriter& writer) {
                return WriteItem(item, writer);
            },
            [](BinaryReader& reader, ecs::ItemInstance* item) {
                return ReadItem(reader, item);
            },
            kSaveComponentVersionLoadedAmmo, error,
            kSaveComponentVersion)) {
        return false;
    }

    if (!registry.Register<ecs::Container>(
            kSaveComponentContainer, "Container",
            [](const ecs::Container& container, BinaryWriter& writer) {
                return WriteContainer(container, writer);
            },
            [](BinaryReader& reader, ecs::Container* container) {
                return ReadContainer(reader, container);
            },
            kSaveComponentVersionLoadedAmmo, error,
            kSaveComponentVersion)) {
        return false;
    }

    if (!registry.Register<ecs::LootContainer>(
            kSaveComponentLootContainer, "LootContainer",
            [](const ecs::LootContainer& loot, BinaryWriter& writer) {
                return WriteLootContainer(loot, writer);
            },
            [](BinaryReader& reader, ecs::LootContainer* loot) {
                return ReadLootContainer(reader, loot);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::PlayerStorage>(
            kSaveComponentPlayerStorage, "PlayerStorage",
            [](const ecs::PlayerStorage& storage, BinaryWriter& writer) {
                return WritePlayerStorage(storage, writer);
            },
            [](BinaryReader& reader, ecs::PlayerStorage* storage) {
                return ReadPlayerStorage(reader, storage);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::StructureOwner>(
            kSaveComponentStructureOwner, "StructureOwner",
            [](const ecs::StructureOwner& owner, BinaryWriter& writer) {
                return WriteStructureOwner(owner, writer);
            },
            [](BinaryReader& reader, ecs::StructureOwner* owner) {
                return ReadStructureOwner(reader, owner);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Equipment>(
            kSaveComponentEquipment, "Equipment",
            [](const ecs::Equipment& equipment, BinaryWriter& writer) {
                return WriteEquipment(equipment, writer);
            },
            [](BinaryReader& reader, ecs::Equipment* equipment) {
                return ReadEquipment(reader, equipment);
            },
            kSaveComponentVersionLoadedAmmo, error,
            kSaveComponentVersion)) {
        return false;
    }

    if (!registry.Register<ecs::Corpse>(
            kSaveComponentCorpse, "Corpse",
            [](const ecs::Corpse& corpse, BinaryWriter& writer) {
                return writer.Write(corpse.sourceId);
            },
            [](BinaryReader& reader, ecs::Corpse* corpse) {
                return reader.Read(corpse->sourceId);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Animal>(
            kSaveComponentAnimal, "Animal",
            [](const ecs::Animal& animal, BinaryWriter& writer) {
                return WriteAnimal(animal, writer);
            },
            [](BinaryReader& reader, ecs::Animal* animal) {
                return ReadAnimal(reader, animal);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Flee>(
            kSaveComponentFlee, "Flee",
            [](const ecs::Flee& flee, BinaryWriter& writer) {
                return WriteFlee(flee, writer);
            },
            [](BinaryReader& reader, ecs::Flee* flee) {
                return ReadFlee(reader, flee);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::AnimalCorpse>(
            kSaveComponentAnimalCorpse, "AnimalCorpse",
            [](const ecs::AnimalCorpse& corpse, BinaryWriter& writer) {
                return WriteAnimalCorpse(corpse, writer);
            },
            [](BinaryReader& reader, ecs::AnimalCorpse* corpse) {
                return ReadAnimalCorpse(reader, corpse);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Vehicle>(
            kSaveComponentVehicle, "Vehicle",
            [](const ecs::Vehicle& vehicle, BinaryWriter& writer) {
                return WriteVehicle(vehicle, writer);
            },
            [](BinaryReader& reader, ecs::Vehicle* vehicle) {
                return ReadVehicle(reader, vehicle);
            },
            kSaveComponentVersionVehicle, error, kSaveComponentVersion)) {
        return false;
    }

    if (!registry.Register<ecs::VehicleMods>(
            kSaveComponentVehicleMods, "VehicleMods",
            [](const ecs::VehicleMods& mods, BinaryWriter& writer) {
                return WriteVehicleMods(mods, writer);
            },
            [](BinaryReader& reader, ecs::VehicleMods* mods) {
                return ReadVehicleMods(reader, mods);
            },
            kSaveComponentVersionVehicleMods, error, kSaveComponentVersion)) {
        return false;
    }

    if (!registry.Register<game::Companion>(
            kSaveComponentCompanion, "Companion",
            [](const game::Companion& companion, BinaryWriter& writer) {
                return WriteCompanion(companion, writer);
            },
            [](BinaryReader& reader, game::Companion* companion) {
                return ReadCompanion(reader, companion);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<game::CompanionState>(
            kSaveComponentCompanionState, "CompanionState",
            [](const game::CompanionState& state, BinaryWriter& writer) {
                return WriteCompanionState(state, writer);
            },
            [](BinaryReader& reader, game::CompanionState* state) {
                return ReadCompanionState(reader, state);
            },
            kSaveComponentVersionCompanionState, error,
            kSaveComponentVersionLoadedAmmo)) {
        return false;
    }

    if (!registry.Register<ecs::AnimalWorldState>(
            kSaveComponentAnimalWorldState, "AnimalWorldState",
            [](const ecs::AnimalWorldState& state, BinaryWriter& writer) {
                return WriteAnimalWorldState(state, writer);
            },
            [](BinaryReader& reader, ecs::AnimalWorldState* state) {
                return ReadAnimalWorldState(reader, state);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    ComponentCodec modData;
    modData.type = kSaveComponentModData;
    modData.name = "ModData";
    modData.version = kSaveComponentVersion;
    modData.minimumVersion = kSaveComponentVersion;
    modData.has = [](const entt::registry& registry, entt::entity entity) {
        return registry.all_of<ecs::ModData>(entity);
    };
    modData.write = [](const entt::registry& registry, entt::entity entity,
                       BinaryWriter& writer) {
        const auto* data = registry.try_get<ecs::ModData>(entity);
        return data != nullptr && WriteModData(*data, writer);
    };
    modData.read = [](BinaryReader& reader, entt::registry& registry,
                      entt::entity entity) {
        ecs::ModData data;
        if (!ReadModData(reader, &data)) {
            return false;
        }
        registry.emplace_or_replace<ecs::ModData>(entity, std::move(data));
        registry.emplace_or_replace<ecs::ModDataTag>(entity);
        return true;
    };
    return registry.Register(std::move(modData), error);
}

}  // namespace zl::save
