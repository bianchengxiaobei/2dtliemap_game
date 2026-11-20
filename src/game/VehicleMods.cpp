#include "game/VehicleMods.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Config.h"
#include "core/Log.h"
#include "game/VehicleComponents.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "vehicle-mods";

bool ValidToken(std::string_view value) {
    if (value.empty() || value.size() > kMaxVehicleModIdBytes) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '.';
    });
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

bool ReadOptionalString(const nlohmann::json& node, const char* key,
                        std::size_t maxBytes, std::string* out) {
    if (!node.contains(key)) {
        return true;
    }
    if (!node[key].is_string()) {
        return false;
    }
    std::string value = node[key].get<std::string>();
    if (value.size() > maxBytes) {
        return false;
    }
    *out = std::move(value);
    return true;
}

bool ReadOptionalFloat(const nlohmann::json& node, const char* key, float lo,
                       float hi, float* out, std::string* error) {
    if (!node.contains(key)) {
        return true;
    }
    if (!node[key].is_number()) {
        if (error != nullptr) {
            *error = std::string(key) + " must be a number";
        }
        return false;
    }
    const double value = node[key].get<double>();
    if (!std::isfinite(value) || value < static_cast<double>(lo) ||
        value > static_cast<double>(hi)) {
        if (error != nullptr) {
            *error = std::string(key) + " is outside [" +
                     std::to_string(lo) + ", " + std::to_string(hi) + "]";
        }
        return false;
    }
    *out = static_cast<float>(value);
    return true;
}

bool ReadOptionalInt(const nlohmann::json& node, const char* key, int lo,
                     int hi, int* out, std::string* error) {
    if (!node.contains(key)) {
        return true;
    }
    if (!node[key].is_number_integer()) {
        if (error != nullptr) {
            *error = std::string(key) + " must be an integer";
        }
        return false;
    }
    std::int64_t value = 0;
    if (node[key].is_number_unsigned()) {
        const std::uint64_t raw = node[key].get<std::uint64_t>();
        if (raw > static_cast<std::uint64_t>(
                      std::numeric_limits<std::int64_t>::max())) {
            if (error != nullptr) {
                *error = std::string(key) + " is too large";
            }
            return false;
        }
        value = static_cast<std::int64_t>(raw);
    } else {
        value = node[key].get<std::int64_t>();
    }
    if (value < lo || value > hi) {
        if (error != nullptr) {
            *error = std::string(key) + " is outside [" + std::to_string(lo) +
                     ", " + std::to_string(hi) + "]";
        }
        return false;
    }
    *out = static_cast<int>(value);
    return true;
}

bool ReadJsonFile(const std::filesystem::path& path, nlohmann::json* out,
                  std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error != nullptr) {
            *error = "cannot read " + path.generic_string();
        }
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    try {
        *out = nlohmann::json::parse(buffer.str());
    } catch (const std::exception& e) {
        if (error != nullptr) {
            *error = path.generic_string() + ": JSON parse failed: " + e.what();
        }
        return false;
    }
    return true;
}

bool ItemReferenceValid(const items::ItemRegistry* items,
                        const std::string& itemId) {
    return items == nullptr || items->Find(itemId) != nullptr;
}

bool KnownEffectKey(std::string_view key) {
    static constexpr std::array<std::string_view, 12> keys = {
        "crashConditionScale",     "zombieConditionScale",
        "ramConditionScale",       "ramDamageScale",
        "trunkCapacityKg",         "trunkStacks",
        "headlightRangeScale",     "headlightBrightnessScale",
        "maxSpeedScale",           "accelerationScale",
        "fuelUseScale",            "conditionWearScale",
    };
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

bool ParseEffects(const nlohmann::json& node, VehicleModEffects* out,
                  std::string* error) {
    if (!node.contains("effects")) {
        return true;
    }
    if (!node["effects"].is_object()) {
        if (error != nullptr) {
            *error = "effects must be an object";
        }
        return false;
    }
    const nlohmann::json& effects = node["effects"];
    for (const auto& [key, value] : effects.items()) {
        if (!KnownEffectKey(key)) {
            if (error != nullptr) {
                *error = "effects." + key + " is unknown";
            }
            return false;
        }
    }
    if (!ReadOptionalFloat(effects, "crashConditionScale", 0.25f, 2.0f,
                           &out->crashConditionScale, error) ||
        !ReadOptionalFloat(effects, "zombieConditionScale", 0.25f, 2.0f,
                           &out->zombieConditionScale, error) ||
        !ReadOptionalFloat(effects, "ramConditionScale", 0.25f, 2.0f,
                           &out->ramConditionScale, error) ||
        !ReadOptionalFloat(effects, "ramDamageScale", 0.25f, 3.0f,
                           &out->ramDamageScale, error) ||
        !ReadOptionalFloat(effects, "trunkCapacityKg", -20.0f, 80.0f,
                           &out->trunkCapacityKg, error) ||
        !ReadOptionalInt(effects, "trunkStacks", -2, 6, &out->trunkStacks,
                         error) ||
        !ReadOptionalFloat(effects, "headlightRangeScale", 0.5f, 3.0f,
                           &out->headlightRangeScale, error) ||
        !ReadOptionalFloat(effects, "headlightBrightnessScale", 0.5f, 3.0f,
                           &out->headlightBrightnessScale, error) ||
        !ReadOptionalFloat(effects, "maxSpeedScale", 0.5f, 2.0f,
                           &out->maxSpeedScale, error) ||
        !ReadOptionalFloat(effects, "accelerationScale", 0.5f, 2.0f,
                           &out->accelerationScale, error) ||
        !ReadOptionalFloat(effects, "fuelUseScale", 0.5f, 3.0f,
                           &out->fuelUseScale, error) ||
        !ReadOptionalFloat(effects, "conditionWearScale", 0.25f, 2.0f,
                           &out->conditionWearScale, error)) {
        return false;
    }
    return true;
}

bool ParseMaterials(const nlohmann::json& node, VehicleModDef* out,
                    const items::ItemRegistry* items, std::string* error) {
    if (!node.contains("materials") || !node["materials"].is_array()) {
        if (error != nullptr) {
            *error = "materials must be an array";
        }
        return false;
    }
    const nlohmann::json& materials = node["materials"];
    if (materials.empty() || materials.size() > kMaxVehicleModMaterials) {
        if (error != nullptr) {
            *error = "materials must contain 1-4 entries";
        }
        return false;
    }
    std::unordered_set<std::string> seen;
    for (const nlohmann::json& entry : materials) {
        if (!entry.is_object()) {
            if (error != nullptr) {
                *error = "material must be an object";
            }
            return false;
        }
        VehicleModMaterial material;
        if (!ReadString(entry, "item", kMaxVehicleModIdBytes,
                        &material.item) ||
            !ValidToken(material.item) || !ItemReferenceValid(items, material.item)) {
            if (error != nullptr) {
                *error = "material item is invalid or unknown";
            }
            return false;
        }
        if (!ReadOptionalInt(entry, "count", 1, 99, &material.count, error)) {
            return false;
        }
        if (!seen.insert(material.item).second) {
            if (error != nullptr) {
                *error = "duplicate material item '" + material.item + "'";
            }
            return false;
        }
        out->materials.push_back(std::move(material));
    }
    return true;
}

bool ParseExcludes(const nlohmann::json& node, VehicleModDef* out,
                   std::string* error) {
    if (!node.contains("excludes")) {
        return true;
    }
    if (!node["excludes"].is_array()) {
        if (error != nullptr) {
            *error = "excludes must be an array";
        }
        return false;
    }
    const nlohmann::json& excludes = node["excludes"];
    if (excludes.size() > kMaxVehicleModExcludes) {
        if (error != nullptr) {
            *error = "excludes exceeds limit";
        }
        return false;
    }
    std::unordered_set<std::string> seen;
    for (const nlohmann::json& entry : excludes) {
        if (!entry.is_string()) {
            if (error != nullptr) {
                *error = "exclude id must be a string";
            }
            return false;
        }
        std::string id = entry.get<std::string>();
        if (!ValidToken(id)) {
            if (error != nullptr) {
                *error = "exclude id is invalid";
            }
            return false;
        }
        if (id == out->id) {
            if (error != nullptr) {
                *error = "mod must not exclude itself";
            }
            return false;
        }
        if (!seen.insert(id).second) {
            if (error != nullptr) {
                *error = "duplicate exclude id '" + id + "'";
            }
            return false;
        }
        out->excludes.push_back(std::move(id));
    }
    return true;
}

bool ParseVehicleMod(const nlohmann::json& node, VehicleModDef* out,
                     const items::ItemRegistry* items, std::string* error) {
    if (!node.is_object()) {
        if (error != nullptr) {
            *error = "definition must be an object";
        }
        return false;
    }
    if (!ReadString(node, "id", kMaxVehicleModIdBytes, &out->id) ||
        !ValidToken(out->id)) {
        if (error != nullptr) {
            *error = "id is invalid";
        }
        return false;
    }
    if (!ReadString(node, "name", kMaxVehicleModNameBytes, &out->name)) {
        if (error != nullptr) {
            *error = "name is invalid";
        }
        return false;
    }
    if (!ReadOptionalString(node, "description", kMaxVehicleModTextBytes,
                            &out->description)) {
        if (error != nullptr) {
            *error = "description is invalid";
        }
        return false;
    }
    std::string slot;
    if (!ReadString(node, "slot", 32, &slot) ||
        !ParseVehicleModSlot(slot, &out->slot)) {
        if (error != nullptr) {
            *error = "slot is invalid";
        }
        return false;
    }
    if (!ParseEffects(node, &out->effects, error)) {
        return false;
    }
    if (!ParseMaterials(node, out, items, error)) {
        return false;
    }
    if (!ReadOptionalFloat(node, "installSeconds", 0.1f, 600.0f,
                           &out->installSeconds, error) ||
        !ReadOptionalFloat(node, "weightKg", 0.0f, 250.0f, &out->weightKg,
                           error)) {
        return false;
    }
    return ParseExcludes(node, out, error);
}

bool AppendVehicleModDocument(const nlohmann::json& doc,
                              const std::string& source,
                              VehicleModsConfig* out,
                              VehicleModMergeStats* stats, bool allowEmpty,
                              const items::ItemRegistry* items,
                              std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "vehicle mod config output is null";
        }
        return false;
    }
    if (!doc.is_object()) {
        if (error != nullptr) {
            *error = source + ": root must be an object";
        }
        return false;
    }
    if (!doc.contains("format") || !doc["format"].is_string() ||
        doc["format"].get<std::string>() != "vehicle_mods-v1") {
        if (error != nullptr) {
            *error = source + ": format must be vehicle_mods-v1";
        }
        return false;
    }
    if (!doc.contains("defs") || !doc["defs"].is_array()) {
        if (error != nullptr) {
            *error = source + ": defs must be an array";
        }
        return false;
    }
    if (doc["defs"].empty()) {
        if (!allowEmpty) {
            if (error != nullptr) {
                *error = source + ": defs must not be empty";
            }
            return false;
        }
        return true;
    }

    std::vector<VehicleModDef> parsed;
    int skipped = 0;
    for (const nlohmann::json& entry : doc["defs"]) {
        if (out->defs.size() + parsed.size() >= kMaxVehicleModDefs) {
            ZL_WARN(kLogModule, "{}: vehicle mod count exceeds limit", source);
            ++skipped;
            break;
        }
        VehicleModDef def;
        std::string defError;
        if (!ParseVehicleMod(entry, &def, items, &defError)) {
            ZL_WARN(kLogModule, "{}: {}", source, defError);
            ++skipped;
            continue;
        }
        const bool duplicate =
            std::any_of(out->defs.begin(), out->defs.end(),
                        [&](const VehicleModDef& existing) {
                            return existing.id == def.id;
                        }) ||
            std::any_of(parsed.begin(), parsed.end(),
                        [&](const VehicleModDef& existing) {
                            return existing.id == def.id;
                        });
        if (duplicate) {
            ZL_WARN(kLogModule, "{}: duplicate vehicle mod id '{}'", source,
                    def.id);
            ++skipped;
            continue;
        }
        parsed.push_back(std::move(def));
    }

    bool removed = true;
    while (removed) {
        removed = false;
        std::unordered_set<std::string> known;
        for (const VehicleModDef& def : out->defs) {
            known.insert(def.id);
        }
        for (const VehicleModDef& def : parsed) {
            known.insert(def.id);
        }
        for (auto it = parsed.begin(); it != parsed.end();) {
            bool missing = false;
            for (const std::string& exclude : it->excludes) {
                if (known.find(exclude) == known.end()) {
                    ZL_WARN(kLogModule,
                            "{}: vehicle mod '{}' excludes unknown '{}'",
                            source, it->id, exclude);
                    missing = true;
                    break;
                }
            }
            if (!missing) {
                ++it;
                continue;
            }
            it = parsed.erase(it);
            ++skipped;
            removed = true;
        }
    }

    const int added = static_cast<int>(parsed.size());
    out->defs.insert(out->defs.end(), std::make_move_iterator(parsed.begin()),
                     std::make_move_iterator(parsed.end()));
    if (stats != nullptr) {
        stats->defsAdded += added;
        stats->defsSkipped += skipped;
    }
    if (added == 0) {
        ZL_WARN(kLogModule, "{}: no valid vehicle mod definitions", source);
    }
    return true;
}

}  // namespace

const char* VehicleModSlotName(VehicleModSlot slot) {
    switch (slot) {
        case VehicleModSlot::Armor:
            return "armor";
        case VehicleModSlot::Storage:
            return "storage";
        case VehicleModSlot::Plow:
            return "plow";
        case VehicleModSlot::Lights:
            return "lights";
        case VehicleModSlot::Engine:
            return "engine";
    }
    return "armor";
}

bool ParseVehicleModSlot(std::string_view value, VehicleModSlot* out) {
    if (out == nullptr) {
        return false;
    }
    if (value == "armor") {
        *out = VehicleModSlot::Armor;
        return true;
    }
    if (value == "storage") {
        *out = VehicleModSlot::Storage;
        return true;
    }
    if (value == "plow") {
        *out = VehicleModSlot::Plow;
        return true;
    }
    if (value == "lights") {
        *out = VehicleModSlot::Lights;
        return true;
    }
    if (value == "engine") {
        *out = VehicleModSlot::Engine;
        return true;
    }
    return false;
}

bool IsValidVehicleModToken(std::string_view value) {
    return ValidToken(value);
}

const VehicleModDef* VehicleModsConfig::Find(std::string_view id) const {
    for (const VehicleModDef& def : defs) {
        if (def.id == id) {
            return &def;
        }
    }
    return nullptr;
}

std::size_t VehicleModSlotIndex(VehicleModSlot slot) {
    const std::size_t index = static_cast<std::size_t>(slot);
    return index < ecs::kVehicleModSlotCount ? index : 0;
}

const std::string& InstalledVehicleModId(const ecs::VehicleMods& mods,
                                         VehicleModSlot slot) {
    return mods.defIds[VehicleModSlotIndex(slot)];
}

const VehicleModDef* InstalledVehicleMod(const VehicleModsConfig& config,
                                         const ecs::VehicleMods& mods,
                                         VehicleModSlot slot) {
    const std::string& id = InstalledVehicleModId(mods, slot);
    return id.empty() ? nullptr : config.Find(id);
}

std::vector<const VehicleModDef*> VehicleModCandidatesForSlot(
    const VehicleModsConfig& config, VehicleModSlot slot) {
    std::vector<const VehicleModDef*> candidates;
    for (const VehicleModDef& def : config.defs) {
        if (def.slot == slot) {
            candidates.push_back(&def);
        }
    }
    return candidates;
}

bool VehicleModsHasAny(const ecs::VehicleMods& mods) {
    return std::any_of(mods.defIds.begin(), mods.defIds.end(),
                       [](const std::string& id) { return !id.empty(); });
}

namespace {

float ClampScale(float value, float lo, float hi) {
    if (!std::isfinite(value)) {
        return 1.0f;
    }
    return std::clamp(value, lo, hi);
}

float WeightDenominator(float value, float fallback) {
    if (!std::isfinite(value) || value <= 0.0f) {
        return fallback;
    }
    return std::clamp(value, 50.0f, 5000.0f);
}

}  // namespace

VehicleModStats VehicleModStatsFor(const VehicleModsConfig& config,
                                   const ecs::VehicleMods* mods) {
    VehicleModStats stats;
    if (mods == nullptr) {
        return stats;
    }
    for (const std::string& id : mods->defIds) {
        if (id.empty()) {
            continue;
        }
        const VehicleModDef* def = config.Find(id);
        if (def == nullptr) {
            continue;
        }
        stats.effects.crashConditionScale *= def->effects.crashConditionScale;
        stats.effects.zombieConditionScale *= def->effects.zombieConditionScale;
        stats.effects.ramConditionScale *= def->effects.ramConditionScale;
        stats.effects.ramDamageScale *= def->effects.ramDamageScale;
        stats.effects.headlightRangeScale *=
            def->effects.headlightRangeScale;
        stats.effects.headlightBrightnessScale *=
            def->effects.headlightBrightnessScale;
        stats.effects.maxSpeedScale *= def->effects.maxSpeedScale;
        stats.effects.accelerationScale *= def->effects.accelerationScale;
        stats.effects.fuelUseScale *= def->effects.fuelUseScale;
        stats.effects.conditionWearScale *= def->effects.conditionWearScale;
        stats.effects.trunkCapacityKg += def->effects.trunkCapacityKg;
        stats.effects.trunkStacks += def->effects.trunkStacks;
        stats.totalWeightKg +=
            std::isfinite(def->weightKg) ? std::max(0.0f, def->weightKg) : 0.0f;
    }
    stats.effects.crashConditionScale =
        ClampScale(stats.effects.crashConditionScale, 0.25f, 2.0f);
    stats.effects.zombieConditionScale =
        ClampScale(stats.effects.zombieConditionScale, 0.25f, 2.0f);
    stats.effects.ramConditionScale =
        ClampScale(stats.effects.ramConditionScale, 0.25f, 2.0f);
    stats.effects.ramDamageScale =
        ClampScale(stats.effects.ramDamageScale, 0.25f, 3.0f);
    stats.effects.trunkCapacityKg =
        std::clamp(stats.effects.trunkCapacityKg, -20.0f, 80.0f);
    stats.effects.trunkStacks =
        std::clamp(stats.effects.trunkStacks, -2, 6);
    stats.effects.headlightRangeScale =
        ClampScale(stats.effects.headlightRangeScale, 0.5f, 3.0f);
    stats.effects.headlightBrightnessScale =
        ClampScale(stats.effects.headlightBrightnessScale, 0.5f, 3.0f);
    stats.effects.maxSpeedScale =
        ClampScale(stats.effects.maxSpeedScale, 0.5f, 2.0f);
    stats.effects.accelerationScale =
        ClampScale(stats.effects.accelerationScale, 0.5f, 2.0f);
    stats.effects.fuelUseScale =
        ClampScale(stats.effects.fuelUseScale, 0.5f, 3.0f);
    stats.effects.conditionWearScale =
        ClampScale(stats.effects.conditionWearScale, 0.25f, 2.0f);
    stats.totalWeightKg = std::clamp(stats.totalWeightKg, 0.0f, 500.0f);

    const float accelerationKg =
        WeightDenominator(config.weightAccelerationKg, 400.0f);
    const float fuelKg = WeightDenominator(config.weightFuelKg, 600.0f);
    stats.accelerationWeightScale =
        std::clamp(1.0f / (1.0f + stats.totalWeightKg / accelerationKg),
                   0.35f, 1.0f);
    stats.fuelWeightScale =
        std::clamp(1.0f + stats.totalWeightKg / fuelKg, 1.0f, 3.0f);
    return stats;
}

VehicleModStats VehicleModStatsFor(const VehicleModsConfig& config,
                                   const ecs::VehicleMods& mods) {
    return VehicleModStatsFor(config, &mods);
}

float VehicleModMaxSpeedScale(const VehicleModStats& stats) {
    return ClampScale(stats.effects.maxSpeedScale, 0.5f, 2.0f);
}

float VehicleModAccelerationScale(const VehicleModStats& stats) {
    return ClampScale(stats.effects.accelerationScale *
                          stats.accelerationWeightScale,
                      0.25f, 3.0f);
}

float VehicleModFuelUseScale(const VehicleModStats& stats) {
    return ClampScale(stats.effects.fuelUseScale * stats.fuelWeightScale,
                      0.25f, 4.0f);
}

float VehicleModCrashWearScale(const VehicleModStats& stats) {
    return ClampScale(stats.effects.crashConditionScale *
                          stats.effects.conditionWearScale,
                      0.1f, 4.0f);
}

float VehicleModZombieWearScale(const VehicleModStats& stats) {
    return ClampScale(stats.effects.zombieConditionScale *
                          stats.effects.conditionWearScale,
                      0.1f, 4.0f);
}

float VehicleModRamWearScale(const VehicleModStats& stats) {
    return ClampScale(stats.effects.ramConditionScale *
                          stats.effects.conditionWearScale,
                      0.1f, 4.0f);
}

float VehicleModRamDamageScale(const VehicleModStats& stats) {
    return ClampScale(stats.effects.ramDamageScale, 0.25f, 3.0f);
}

float VehicleModHeadlightRangeScale(const VehicleModStats& stats) {
    return ClampScale(stats.effects.headlightRangeScale, 0.5f, 3.0f);
}

float VehicleModHeadlightBrightnessScale(const VehicleModStats& stats) {
    return ClampScale(stats.effects.headlightBrightnessScale, 0.5f, 3.0f);
}

float VehicleModTrunkCapacityKg(const VehicleModStats& stats,
                                float baseCapacityKg) {
    const float base =
        std::isfinite(baseCapacityKg) ? std::max(0.0f, baseCapacityKg) : 40.0f;
    return std::clamp(base + stats.effects.trunkCapacityKg, 1.0f, 1000.0f);
}

int VehicleModTrunkStacks(const VehicleModStats& stats, int baseStacks) {
    return std::clamp(baseStacks + stats.effects.trunkStacks, 1, 8);
}

bool ApplyVehicleModTrunkCapacity(const VehicleModStats& stats,
                                  float baseCapacityKg,
                                  ecs::Container* container) {
    if (container == nullptr) {
        return false;
    }
    const float capacity = VehicleModTrunkCapacityKg(stats, baseCapacityKg);
    if (std::fabs(container->capacityKg - capacity) <= 1.0e-4f) {
        return false;
    }
    container->capacityKg = capacity;
    return true;
}

namespace {

std::string SignedPercent(float scale) {
    const int percent =
        static_cast<int>(std::lround((scale - 1.0f) * 100.0f));
    return (percent > 0 ? "+" : "") + std::to_string(percent) + "%";
}

std::string SignedKg(float value) {
    const int amount = static_cast<int>(std::lround(value));
    return (amount > 0 ? "+" : "") + std::to_string(amount) + "kg";
}

void AppendPart(std::string* out, const std::string& part) {
    if (out == nullptr || part.empty()) {
        return;
    }
    if (!out->empty()) {
        *out += "，";
    }
    *out += part;
}

bool NonDefault(float value, float expected = 1.0f) {
    return std::isfinite(value) && std::fabs(value - expected) > 1.0e-3f;
}

}  // namespace

std::string VehicleModEffectSummary(const VehicleModDef& def) {
    const VehicleModEffects& effects = def.effects;
    std::string out;
    if (NonDefault(effects.crashConditionScale)) {
        AppendPart(&out, "撞墙车况 " +
                             SignedPercent(effects.crashConditionScale));
    }
    if (NonDefault(effects.zombieConditionScale)) {
        AppendPart(&out, "撞尸车况 " +
                             SignedPercent(effects.zombieConditionScale));
    }
    if (NonDefault(effects.ramConditionScale)) {
        AppendPart(&out, "撞击车况 " +
                             SignedPercent(effects.ramConditionScale));
    }
    if (NonDefault(effects.ramDamageScale)) {
        AppendPart(&out, "撞击伤害 " +
                             SignedPercent(effects.ramDamageScale));
    }
    if (NonDefault(effects.trunkCapacityKg, 0.0f)) {
        AppendPart(&out, "后备箱 " + SignedKg(effects.trunkCapacityKg));
    }
    if (NonDefault(effects.headlightRangeScale)) {
        AppendPart(&out, "车灯范围 " +
                             SignedPercent(effects.headlightRangeScale));
    }
    if (NonDefault(effects.headlightBrightnessScale)) {
        AppendPart(&out, "车灯亮度 " +
                             SignedPercent(effects.headlightBrightnessScale));
    }
    if (NonDefault(effects.maxSpeedScale)) {
        AppendPart(&out, "极速 " + SignedPercent(effects.maxSpeedScale));
    }
    if (NonDefault(effects.accelerationScale)) {
        AppendPart(&out, "加速 " + SignedPercent(effects.accelerationScale));
    }
    if (NonDefault(effects.fuelUseScale)) {
        AppendPart(&out, "油耗 " + SignedPercent(effects.fuelUseScale));
    }
    if (NonDefault(effects.conditionWearScale)) {
        AppendPart(&out, "车况损耗 " +
                             SignedPercent(effects.conditionWearScale));
    }
    return out.empty() ? "无额外效果" : out;
}

std::string VehicleModStatsSummary(const VehicleModStats& stats,
                                   float baseCapacityKg) {
    const float base =
        std::isfinite(baseCapacityKg) ? std::max(0.0f, baseCapacityKg) : 40.0f;
    const float capacity = VehicleModTrunkCapacityKg(stats, base);
    std::string out = "总重 " +
                      std::to_string(static_cast<int>(
                          std::lround(stats.totalWeightKg))) +
                      "kg · 后备箱 " +
                      std::to_string(static_cast<int>(std::lround(capacity))) +
                      "kg";
    if (NonDefault(capacity, base)) {
        out += "（" + SignedKg(capacity - base) + "）";
    }
    const float maxSpeed = VehicleModMaxSpeedScale(stats);
    const float acceleration = VehicleModAccelerationScale(stats);
    const float fuel = VehicleModFuelUseScale(stats);
    const float crash = VehicleModCrashWearScale(stats);
    const float zombie = VehicleModZombieWearScale(stats);
    const float ramDamage = VehicleModRamDamageScale(stats);
    const float lights = VehicleModHeadlightRangeScale(stats);
    std::string effects;
    if (NonDefault(maxSpeed)) {
        AppendPart(&effects, "极速 " + SignedPercent(maxSpeed));
    }
    if (NonDefault(acceleration)) {
        AppendPart(&effects, "加速 " + SignedPercent(acceleration));
    }
    if (NonDefault(fuel)) {
        AppendPart(&effects, "油耗 " + SignedPercent(fuel));
    }
    if (NonDefault(crash)) {
        AppendPart(&effects, "撞墙 " + SignedPercent(crash));
    }
    if (NonDefault(zombie)) {
        AppendPart(&effects, "撞尸 " + SignedPercent(zombie));
    }
    if (NonDefault(ramDamage)) {
        AppendPart(&effects, "撞击 " + SignedPercent(ramDamage));
    }
    if (NonDefault(lights)) {
        AppendPart(&effects, "车灯 " + SignedPercent(lights));
    }
    if (effects.empty()) {
        out += " · 无改装效果";
    } else {
        out += " · " + effects;
    }
    return out;
}

bool VehicleModTargetInRange(float playerX, float playerY, float playerZ,
                             float targetX, float targetY, float targetZ,
                             float range, bool targetAlive,
                             bool targetIsVehicle) {
    if (!targetAlive || !targetIsVehicle || !std::isfinite(range) ||
        range < 0.0f) {
        return false;
    }
    if (!std::isfinite(playerX) || !std::isfinite(playerY) ||
        !std::isfinite(playerZ) || !std::isfinite(targetX) ||
        !std::isfinite(targetY) || !std::isfinite(targetZ)) {
        return false;
    }
    if (std::fabs(playerZ - targetZ) > 0.01f) {
        return false;
    }
    const float dx = targetX - playerX;
    const float dy = targetY - playerY;
    return dx * dx + dy * dy <= range * range;
}

bool CanInstallVehicleMod(const VehicleModsConfig& config,
                          const ecs::VehicleMods& mods, std::string_view id,
                          std::string* error) {
    const VehicleModDef* def = config.Find(id);
    if (def == nullptr) {
        if (error != nullptr) {
            *error = "未知改装定义";
        }
        return false;
    }
    const std::string& occupied = InstalledVehicleModId(mods, def->slot);
    if (!occupied.empty()) {
        if (error != nullptr) {
            *error = "该槽位已安装改装";
        }
        return false;
    }
    for (const std::string& installedId : mods.defIds) {
        if (installedId.empty()) {
            continue;
        }
        if (std::find(def->excludes.begin(), def->excludes.end(),
                      installedId) != def->excludes.end()) {
            if (error != nullptr) {
                *error = "与已安装改装互斥";
            }
            return false;
        }
        const VehicleModDef* installed = config.Find(installedId);
        if (installed != nullptr &&
            std::find(installed->excludes.begin(), installed->excludes.end(),
                      def->id) != installed->excludes.end()) {
            if (error != nullptr) {
                *error = "与已安装改装互斥";
            }
            return false;
        }
    }
    return true;
}

bool InstallVehicleMod(const VehicleModsConfig& config, ecs::VehicleMods* mods,
                       std::string_view id, std::string* error) {
    if (mods == nullptr) {
        if (error != nullptr) {
            *error = "车辆改装状态不可用";
        }
        return false;
    }
    if (!CanInstallVehicleMod(config, *mods, id, error)) {
        return false;
    }
    const VehicleModDef* def = config.Find(id);
    mods->defIds[VehicleModSlotIndex(def->slot)] = def->id;
    return true;
}

std::string RemoveVehicleMod(ecs::VehicleMods* mods, VehicleModSlot slot) {
    if (mods == nullptr) {
        return {};
    }
    std::string& installed = mods->defIds[VehicleModSlotIndex(slot)];
    std::string removed = std::move(installed);
    installed.clear();
    return removed;
}

float VehicleModRemoveSeconds(const VehicleModsConfig& config,
                              const VehicleModDef& def) {
    const float scale = std::clamp(config.removeSecondsScale, 0.1f, 2.0f);
    return std::clamp(def.installSeconds * scale, 0.1f, 600.0f);
}

std::vector<VehicleModMaterial> VehicleModRefundMaterials(
    const VehicleModDef& def, float ratio) {
    std::vector<VehicleModMaterial> refund;
    const float clamped = std::clamp(ratio, 0.0f, 1.0f);
    for (const VehicleModMaterial& material : def.materials) {
        const int count = static_cast<int>(
            std::floor(static_cast<float>(material.count) * clamped));
        if (count > 0) {
            refund.push_back(VehicleModMaterial{material.item, count});
        }
    }
    return refund;
}

bool VehicleModMaterialCountsMet(const ecs::Container& container,
                                 const items::ItemRegistry& items,
                                 const VehicleModDef& def,
                                 std::string* summary) {
    bool met = true;
    std::string text;
    for (const VehicleModMaterial& material : def.materials) {
        const int have = items::CountOf(container, material.item);
        if (!text.empty()) {
            text += "、";
        }
        const items::ItemDef* item = items.Find(material.item);
        text += item != nullptr ? item->name : material.item;
        text += " " + std::to_string(have) + "/" +
                std::to_string(material.count);
        if (have < material.count) {
            met = false;
        }
    }
    if (summary != nullptr) {
        *summary = std::move(text);
    }
    return met;
}

bool ConsumeVehicleModMaterials(ecs::Container& container,
                                const items::ItemRegistry& items,
                                const VehicleModDef& def,
                                std::string* error) {
    ecs::Container trial = container;
    for (const VehicleModMaterial& material : def.materials) {
        if (items.Find(material.item) == nullptr) {
            if (error != nullptr) {
                *error = "材料未注册：" + material.item;
            }
            return false;
        }
        if (items::RemoveItem(trial, material.item, material.count) !=
            material.count) {
            if (error != nullptr) {
                *error = "缺少材料：" + material.item;
            }
            return false;
        }
    }
    container = std::move(trial);
    return true;
}

std::vector<VehicleModMaterial> AddVehicleModRefund(
    ecs::Container& container, const items::ItemRegistry& items,
    const VehicleModDef& def, float ratio) {
    std::vector<VehicleModMaterial> overflow;
    for (const VehicleModMaterial& material : VehicleModRefundMaterials(
             def, ratio)) {
        if (items.Find(material.item) == nullptr) {
            continue;
        }
        ecs::ItemInstance item{material.item, material.count, 1.0f};
        const int added = items::AddItem(container, items, item);
        const int left = material.count - added;
        if (left > 0) {
            overflow.push_back(VehicleModMaterial{material.item, left});
        }
    }
    return overflow;
}

bool LoadVehicleModConfig(const core::Config& config, VehicleModsConfig* out,
                          std::string* error,
                          const items::ItemRegistry* items) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "vehicle mod config output is null";
        }
        return false;
    }
    VehicleModsConfig result;
    const nlohmann::json& root = config.Raw();
    if (!root.is_object()) {
        *out = std::move(result);
        return true;
    }
    const auto nodeIt = root.find("vehicleMods");
    if (nodeIt == root.end()) {
        *out = std::move(result);
        return true;
    }
    if (!nodeIt->is_object()) {
        if (error != nullptr) {
            *error = "vehicleMods must be an object";
        }
        return false;
    }
    const nlohmann::json& node = *nodeIt;
    if (node.contains("enabled") && !node["enabled"].is_boolean()) {
        if (error != nullptr) {
            *error = "vehicleMods.enabled must be a boolean";
        }
        return false;
    }
    result.enabled = node.value("enabled", false);
    if (!result.enabled) {
        *out = std::move(result);
        return true;
    }
    if (node.contains("removeRefundRatio")) {
        if (!node["removeRefundRatio"].is_number()) {
            if (error != nullptr) {
                *error = "vehicleMods.removeRefundRatio must be a number";
            }
            return false;
        }
        const float ratio = node["removeRefundRatio"].get<float>();
        if (!std::isfinite(ratio)) {
            if (error != nullptr) {
                *error = "vehicleMods.removeRefundRatio must be finite";
            }
            return false;
        }
        result.removeRefundRatio = std::clamp(ratio, 0.0f, 1.0f);
    }
    if (node.contains("removeSecondsScale")) {
        if (!node["removeSecondsScale"].is_number()) {
            if (error != nullptr) {
                *error = "vehicleMods.removeSecondsScale must be a number";
            }
            return false;
        }
        const float scale = node["removeSecondsScale"].get<float>();
        if (!std::isfinite(scale)) {
            if (error != nullptr) {
                *error = "vehicleMods.removeSecondsScale must be finite";
            }
            return false;
        }
        result.removeSecondsScale = std::clamp(scale, 0.1f, 2.0f);
    }
    const auto readWeight = [&](const char* key, float* out) {
        if (!node.contains(key)) {
            return true;
        }
        if (!node[key].is_number()) {
            if (error != nullptr) {
                *error = std::string("vehicleMods.") + key +
                         " must be a number";
            }
            return false;
        }
        const float value = node[key].get<float>();
        if (!std::isfinite(value) || value <= 0.0f) {
            if (error != nullptr) {
                *error = std::string("vehicleMods.") + key +
                         " must be positive and finite";
            }
            return false;
        }
        *out = std::clamp(value, 50.0f, 5000.0f);
        return true;
    };
    if (!readWeight("weightAccelerationKg",
                    &result.weightAccelerationKg) ||
        !readWeight("weightFuelKg", &result.weightFuelKg)) {
        return false;
    }
    std::string path = "assets/vehicle_mods.json";
    if (node.contains("file")) {
        if (!node["file"].is_string() ||
            node["file"].get<std::string>().empty()) {
            if (error != nullptr) {
                *error = "vehicleMods.file must be a non-empty string";
            }
            return false;
        }
        path = node["file"].get<std::string>();
    }
    nlohmann::json doc;
    if (!ReadJsonFile(path, &doc, error)) {
        return false;
    }
    if (!AppendVehicleModDocument(doc, path, &result, nullptr, true, items,
                                  error)) {
        return false;
    }
    *out = std::move(result);
    return true;
}

bool MergeVehicleModJson(std::string_view json, const std::string& source,
                         VehicleModsConfig* out,
                         VehicleModMergeStats* stats, std::string* error,
                         const items::ItemRegistry* items) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "vehicle mod config output is null";
        }
        return false;
    }
    if (!out->enabled) {
        return true;
    }
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(json);
    } catch (const std::exception& e) {
        if (error != nullptr) {
            *error = source + ": JSON parse failed: " + e.what();
        }
        return false;
    }
    if (!AppendVehicleModDocument(doc, source, out, stats, false, items,
                                  error)) {
        return false;
    }
    if (stats != nullptr) {
        ++stats->filesLoaded;
    }
    return true;
}

bool MergeVehicleModFile(const std::filesystem::path& path,
                         VehicleModsConfig* out,
                         VehicleModMergeStats* stats, std::string* error,
                         const items::ItemRegistry* items) {
    nlohmann::json doc;
    if (!ReadJsonFile(path, &doc, error)) {
        return false;
    }
    return MergeVehicleModJson(doc.dump(), path.generic_string(), out, stats,
                               error, items);
}

}  // namespace zl::game
