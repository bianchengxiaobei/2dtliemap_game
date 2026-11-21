#include "game/Traps.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Config.h"
#include "core/Log.h"
#include "ecs/components/CommonComponents.h"
#include "game/BuildingRules.h"
#include "game/StructureComponents.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "traps";

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

bool ReadNumber(const nlohmann::json& node, const char* key, float lo, float hi,
                float* out, std::string* error) {
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
            *error = std::string(key) + " is outside [" + std::to_string(lo) +
                     ", " + std::to_string(hi) + "]";
        }
        return false;
    }
    *out = static_cast<float>(value);
    return true;
}

bool ReadInt(const nlohmann::json& node, const char* key, int lo, int hi,
             int* out, std::string* error) {
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

bool KnownTrigger(std::string_view value) {
    return value == "zombie" || value == "animal";
}

bool ParseTriggerMask(const nlohmann::json& node, std::uint8_t* out,
                      std::string* error) {
    if (!node.contains("triggerMask")) {
        return true;
    }
    if (!node["triggerMask"].is_array() || node["triggerMask"].empty()) {
        if (error != nullptr) {
            *error = "triggerMask must be a non-empty array";
        }
        return false;
    }
    std::uint8_t mask = 0;
    for (const nlohmann::json& entry : node["triggerMask"]) {
        if (!entry.is_string()) {
            if (error != nullptr) {
                *error = "triggerMask entries must be strings";
            }
            return false;
        }
        const std::string value = entry.get<std::string>();
        if (!KnownTrigger(value)) {
            if (error != nullptr) {
                *error = "triggerMask contains unknown target '" + value + "'";
            }
            return false;
        }
        if (value == "zombie") {
            mask |= kTrapTriggerZombie;
        } else {
            mask |= kTrapTriggerAnimal;
        }
    }
    *out = mask;
    return true;
}

bool ParseFire(const nlohmann::json& node, TrapDef* out, std::string* error) {
    if (!node.contains("fire")) {
        return true;
    }
    if (!node["fire"].is_object()) {
        if (error != nullptr) {
            *error = "fire must be an object";
        }
        return false;
    }
    const nlohmann::json& fire = node["fire"];
    for (const auto& [key, value] : fire.items()) {
        if (key != "radius" && key != "minutes") {
            if (error != nullptr) {
                *error = "fire." + key + " is unknown";
            }
            return false;
        }
    }
    TrapFireDef parsed;
    if (!ReadNumber(fire, "radius", 0.5f, 3.0f, &parsed.radius, error) ||
        !ReadNumber(fire, "minutes", 1.0f, 240.0f, &parsed.minutes, error) ||
        parsed.radius <= 0.0f || parsed.minutes <= 0.0f) {
        return false;
    }
    out->fire = parsed;
    return true;
}

bool BuildingExists(const std::vector<BuildableDef>& buildables,
                    std::string_view id) {
    return std::any_of(buildables.begin(), buildables.end(),
                       [id](const BuildableDef& def) { return def.id == id; });
}

bool ParseTrapDef(const nlohmann::json& node,
                  const std::vector<BuildableDef>& buildables, TrapDef* out,
                  std::string* error) {
    if (!node.is_object()) {
        if (error != nullptr) {
            *error = "definition must be an object";
        }
        return false;
    }
    static constexpr std::array<std::string_view, 12> keys = {
        "id",         "name",       "buildingId", "damage",
        "radius",     "cooldownSeconds", "uses", "staggerSeconds",
        "noiseRadius", "triggerMask", "armSeconds", "fire",
    };
    for (const auto& [key, value] : node.items()) {
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
            if (error != nullptr) {
                *error = "unknown field '" + key + "'";
            }
            return false;
        }
    }
    if (!ReadString(node, "id", kMaxTrapIdBytes, &out->id) ||
        !IsValidTrapToken(out->id)) {
        if (error != nullptr) {
            *error = "id is invalid";
        }
        return false;
    }
    if (!ReadString(node, "name", kMaxTrapNameBytes, &out->name)) {
        if (error != nullptr) {
            *error = "name is invalid";
        }
        return false;
    }
    if (!ReadString(node, "buildingId", kMaxTrapIdBytes, &out->buildingId) ||
        !IsValidTrapToken(out->buildingId)) {
        if (error != nullptr) {
            *error = "buildingId is invalid";
        }
        return false;
    }
    if (!BuildingExists(buildables, out->buildingId)) {
        if (error != nullptr) {
            *error = "buildingId references unknown building '" +
                     out->buildingId + "'";
        }
        return false;
    }
    if (!ReadNumber(node, "damage", 0.0f, 100.0f, &out->damage, error) ||
        !ReadNumber(node, "radius", 0.5f, 3.0f, &out->radius, error) ||
        !ReadNumber(node, "cooldownSeconds", 0.0f, 600.0f,
                    &out->cooldownSeconds, error) ||
        !ReadInt(node, "uses", 1, 99, &out->uses, error) ||
        !ReadNumber(node, "staggerSeconds", 0.0f, 10.0f,
                    &out->staggerSeconds, error) ||
        !ReadNumber(node, "noiseRadius", 0.0f, 32.0f, &out->noiseRadius,
                    error) ||
        !ReadNumber(node, "armSeconds", 0.0f, 10.0f, &out->armSeconds, error) ||
        !ParseTriggerMask(node, &out->triggerMask, error) ||
        !ParseFire(node, out, error)) {
        return false;
    }
    if (out->triggerMask == 0) {
        if (error != nullptr) {
            *error = "triggerMask must not be empty";
        }
        return false;
    }
    if (out->damage <= 0.0f && out->noiseRadius <= 0.0f && !out->fire) {
        if (error != nullptr) {
            *error = "trap must define damage, noiseRadius or fire";
        }
        return false;
    }
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

}  // namespace

const char* TrapTriggerName(std::uint8_t bit) {
    if (bit == kTrapTriggerZombie) {
        return "zombie";
    }
    if (bit == kTrapTriggerAnimal) {
        return "animal";
    }
    return "unknown";
}

bool IsValidTrapToken(std::string_view value) {
    if (value.empty() || value.size() > kMaxTrapIdBytes) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '.';
    });
}

const TrapDef* TrapDefsConfig::Find(std::string_view id) const {
    for (const TrapDef& def : defs) {
        if (def.id == id) {
            return &def;
        }
    }
    return nullptr;
}

const TrapDef* TrapDefsConfig::FindByBuilding(
    std::string_view buildingId) const {
    for (const TrapDef& def : defs) {
        if (def.buildingId == buildingId) {
            return &def;
        }
    }
    return nullptr;
}

namespace {

std::string TrapNumberText(float value) {
    if (!std::isfinite(value)) {
        return "0";
    }
    std::string text = std::to_string(std::max(0.0f, value));
    const std::size_t dot = text.find('.');
    if (dot != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    return text.empty() ? "0" : text;
}

}  // namespace

std::string TrapEffectText(const TrapDef& trap) {
    if (trap.fire.has_value()) {
        return "火焰 " + TrapNumberText(trap.fire->radius) + " 格 · " +
               TrapNumberText(trap.fire->minutes) + " 分钟";
    }
    if (trap.damage > 0.0f) {
        std::string text = TrapNumberText(trap.damage) + " 伤害";
        if (trap.staggerSeconds > 0.0f) {
            text += " · 硬直 " + TrapNumberText(trap.staggerSeconds) + " 秒";
        }
        return text;
    }
    if (trap.noiseRadius > 0.0f) {
        return "噪声 " + TrapNumberText(trap.noiseRadius) + " 格";
    }
    return "无直接效果";
}

std::string TrapBuildSummary(const TrapDef& trap) {
    std::string text = TrapEffectText(trap) + " · " +
                       std::to_string(std::max(0, trap.uses)) + " 次";
    if (trap.cooldownSeconds > 0.0f) {
        text += "/冷却 " + TrapNumberText(trap.cooldownSeconds) + "s";
    }
    return text;
}

bool TrapCanToggle(const ecs::PlacedTrap& trap) {
    return trap.uses > 0;
}

bool SetPlacedTrapArmed(ecs::PlacedTrap& trap, bool armed) {
    if (armed && !TrapCanToggle(trap)) {
        return false;
    }
    trap.armed = armed;
    trap.armRemaining = 0.0f;
    return true;
}

bool AdvancePlacedTrapArming(ecs::PlacedTrap& trap, float seconds) {
    if (trap.armed || trap.armRemaining <= 0.0f || trap.uses <= 0 ||
        !std::isfinite(seconds) || seconds <= 0.0f) {
        return false;
    }
    trap.armRemaining = std::max(0.0f, trap.armRemaining - seconds);
    if (trap.armRemaining > 0.0001f) {
        return false;
    }
    trap.armRemaining = 0.0f;
    trap.armed = true;
    return true;
}

ecs::Entity SpawnPlacedTrap(ecs::EcsWorld& world, float x, float y, int z,
                            const TrapDef& trap,
                            const BuildableDef& buildable,
                            std::uint64_t owner) {
    if (!std::isfinite(x) || !std::isfinite(y) || trap.id.empty() ||
        trap.uses <= 0 || !std::isfinite(trap.armSeconds) ||
        trap.armSeconds < 0.0f) {
        return ecs::EcsWorld::kNull;
    }
    const ecs::Entity entity = world.Create();
    if (entity == ecs::EcsWorld::kNull) {
        return ecs::EcsWorld::kNull;
    }
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(entity, ecs::Position{x, y, z});
    registry.emplace<ecs::SpriteRef>(
        entity,
        ecs::SpriteRef{ecs::HashName(BuildableSprite(buildable, false)), 0,
                       0xFFFFFFFFu});
    ecs::PlacedTrap placed;
    placed.defId = trap.id;
    placed.armed = trap.armSeconds <= 0.0f;
    placed.cooldown = 0.0f;
    placed.uses = trap.uses;
    placed.owner = owner;
    placed.armRemaining = std::max(0.0f, trap.armSeconds);
    registry.emplace<ecs::PlacedTrap>(entity, std::move(placed));
    registry.emplace_or_replace<ecs::StructureOwner>(
        entity, ecs::StructureOwner{owner});
    registry.emplace_or_replace<ecs::NoCombatTarget>(entity);
    return entity;
}

bool MergeTrapJson(std::string_view json, const std::string& source,
                   TrapDefsConfig* out, TrapMergeStats* stats,
                   const std::vector<BuildableDef>& buildables,
                   std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "trap config output is null";
        }
        return false;
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
    if (!doc.is_object()) {
        if (error != nullptr) {
            *error = source + ": root must be an object";
        }
        return false;
    }
    if (!doc.contains("format") || !doc["format"].is_string() ||
        doc["format"].get<std::string>() != "traps-v1") {
        if (error != nullptr) {
            *error = source + ": format must be traps-v1";
        }
        return false;
    }
    if (!doc.contains("defs") || !doc["defs"].is_array()) {
        if (error != nullptr) {
            *error = source + ": defs must be an array";
        }
        return false;
    }

    int added = 0;
    int skipped = 0;
    for (const nlohmann::json& entry : doc["defs"]) {
        if (out->defs.size() >= kMaxTrapDefs) {
            ZL_WARN(kLogModule, "{}: trap count exceeds limit", source);
            ++skipped;
            break;
        }
        TrapDef def;
        std::string defError;
        if (!ParseTrapDef(entry, buildables, &def, &defError)) {
            ZL_WARN(kLogModule, "{}: {}", source, defError);
            ++skipped;
            continue;
        }
        const bool duplicate =
            std::any_of(out->defs.begin(), out->defs.end(),
                        [&](const TrapDef& existing) {
                            return existing.id == def.id;
                        });
        if (duplicate) {
            ZL_WARN(kLogModule, "{}: duplicate trap id '{}'", source, def.id);
            ++skipped;
            continue;
        }
        out->defs.push_back(std::move(def));
        ++added;
    }
    if (stats != nullptr) {
        stats->defsAdded += added;
        stats->defsSkipped += skipped;
    }
    return true;
}

bool MergeTrapFile(const std::filesystem::path& path, TrapDefsConfig* out,
                   TrapMergeStats* stats,
                   const std::vector<BuildableDef>& buildables,
                   std::string* error) {
    nlohmann::json doc;
    if (!ReadJsonFile(path, &doc, error)) {
        return false;
    }
    if (!MergeTrapJson(doc.dump(), path.generic_string(), out, stats,
                       buildables, error)) {
        return false;
    }
    if (stats != nullptr) {
        ++stats->filesLoaded;
    }
    return true;
}

bool LoadTrapDefsConfig(const core::Config& config,
                    const std::vector<BuildableDef>& buildables,
                    TrapDefsConfig* out, std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "trap config output is null";
        }
        return false;
    }
    out->enabled = config.Get<bool>("trapDefs.enabled", false);
    out->defs.clear();
    if (!out->enabled) {
        return true;
    }
    const std::string file =
        config.Get<std::string>("trapDefs.file", "assets/traps.json");
    TrapMergeStats stats;
    if (!MergeTrapFile(file, out, &stats, buildables, error)) {
        if (error != nullptr) {
            *error = "base trap config " + file + ": " + *error;
        }
        return false;
    }
    if (out->defs.empty()) {
        if (error != nullptr) {
            *error = "base trap config " + file + ": no valid trap defs";
        }
        return false;
    }
    ZL_INFO(kLogModule, "trap config: {} defs from {}", out->defs.size(), file);
    return true;
}

}  // namespace zl::game
