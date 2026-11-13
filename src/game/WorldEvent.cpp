#include "game/WorldEvent.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Config.h"
#include "core/Log.h"
#include "items/ItemRegistry.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "world-events";

bool ValidToken(std::string_view value) {
    if (value.empty() || value.size() > kMaxWorldEventIdBytes) {
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

bool ReadInt(const nlohmann::json& node, const char* key, int lo, int hi,
             int* out) {
    if (!node.contains(key) || !node[key].is_number_integer()) {
        return false;
    }
    std::int64_t value = 0;
    if (node[key].is_number_unsigned()) {
        const std::uint64_t raw = node[key].get<std::uint64_t>();
        if (raw > static_cast<std::uint64_t>(
                      std::numeric_limits<std::int64_t>::max())) {
            return false;
        }
        value = static_cast<std::int64_t>(raw);
    } else {
        value = node[key].get<std::int64_t>();
    }
    if (value < lo || value > hi) {
        return false;
    }
    *out = static_cast<int>(value);
    return true;
}

bool ReadFloat(const nlohmann::json& node, const char* key, float lo, float hi,
               float* out) {
    if (!node.contains(key) || !node[key].is_number()) {
        return false;
    }
    const double value = node[key].get<double>();
    if (!std::isfinite(value) || value < static_cast<double>(lo) ||
        value > static_cast<double>(hi)) {
        return false;
    }
    *out = static_cast<float>(value);
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

bool ParseStage(const nlohmann::json& node, WorldEventStageDef* out,
                const items::ItemRegistry* items, std::string* error) {
    if (!node.is_object()) {
        if (error != nullptr) {
            *error = "stage must be an object";
        }
        return false;
    }
    WorldEventStageDef stage;
    std::string kindName;
    if (!ReadString(node, "kind", kMaxWorldEventKindBytes, &kindName) ||
        !ParseWorldEventStageKind(kindName, &stage.kind)) {
        if (error != nullptr) {
            *error = "stage kind is invalid";
        }
        return false;
    }
    if (node.contains("target")) {
        if (!ReadString(node, "target", kMaxWorldEventTargetBytes,
                        &stage.target) ||
            !ValidToken(stage.target)) {
            if (error != nullptr) {
                *error = "stage target is invalid";
            }
            return false;
        }
    }
    const bool needsTarget = stage.kind == WorldEventStageKind::Interact ||
                             stage.kind == WorldEventStageKind::Kill ||
                             stage.kind == WorldEventStageKind::Loot;
    if (needsTarget && stage.target.empty()) {
        if (error != nullptr) {
            *error = "stage target is required";
        }
        return false;
    }
    if (node.contains("count") &&
        !ReadInt(node, "count", 1, 999, &stage.count)) {
        if (error != nullptr) {
            *error = "stage count must be 1..999";
        }
        return false;
    }
    if (node.contains("radius") &&
        !ReadFloat(node, "radius", 0.5f, 128.0f, &stage.radius)) {
        if (error != nullptr) {
            *error = "stage radius must be 0.5..128";
        }
        return false;
    }
    if (!ReadOptionalString(node, "description", kMaxWorldEventTextBytes,
                            &stage.description)) {
        if (error != nullptr) {
            *error = "stage description is invalid";
        }
        return false;
    }
    (void)items;
    *out = std::move(stage);
    return true;
}

bool ParseChoice(const nlohmann::json& node, WorldEventChoiceDef* out,
                 const items::ItemRegistry* items, std::string* error) {
    if (!node.is_object()) {
        if (error != nullptr) {
            *error = "choice must be an object";
        }
        return false;
    }
    WorldEventChoiceDef choice;
    if (!ReadString(node, "id", kMaxWorldEventIdBytes, &choice.id) ||
        !ValidToken(choice.id)) {
        if (error != nullptr) {
            *error = "choice id is invalid";
        }
        return false;
    }
    if (!ReadString(node, "label", kMaxWorldEventNameBytes, &choice.label)) {
        if (error != nullptr) {
            *error = "choice label is required";
        }
        return false;
    }
    if (node.contains("reputation") &&
        !ReadInt(node, "reputation", -100, 100, &choice.reputation)) {
        if (error != nullptr) {
            *error = "choice reputation must be -100..100";
        }
        return false;
    }
    if (node.contains("rewardXp") &&
        !ReadFloat(node, "rewardXp", 0.0f, 1000000.0f, &choice.rewardXp)) {
        if (error != nullptr) {
            *error = "choice rewardXp must be 0..1000000";
        }
        return false;
    }
    if (node.contains("pressure") &&
        !ReadInt(node, "pressure", -100, 100, &choice.pressure)) {
        if (error != nullptr) {
            *error = "choice pressure must be -100..100";
        }
        return false;
    }
    if (node.contains("rewardItem")) {
        if (!ReadString(node, "rewardItem", kMaxWorldEventIdBytes,
                        &choice.rewardItem) ||
            !ValidToken(choice.rewardItem)) {
            if (error != nullptr) {
                *error = "choice rewardItem is invalid";
            }
            return false;
        }
        if (items != nullptr && items->Find(choice.rewardItem) == nullptr) {
            if (error != nullptr) {
                *error = "choice rewardItem does not exist";
            }
            return false;
        }
        if (node.contains("rewardItemCount") &&
            !ReadInt(node, "rewardItemCount", 1, 9999,
                     &choice.rewardItemCount)) {
            if (error != nullptr) {
                *error = "choice rewardItemCount must be 1..9999";
            }
            return false;
        }
    } else if (node.contains("rewardItemCount")) {
        if (error != nullptr) {
            *error = "choice rewardItemCount requires rewardItem";
        }
        return false;
    }
    *out = std::move(choice);
    return true;
}

bool ParseEvent(const nlohmann::json& entry, WorldEventDef* out,
                const items::ItemRegistry* items, std::string* error) {
    if (!entry.is_object()) {
        if (error != nullptr) {
            *error = "world event entry must be an object";
        }
        return false;
    }
    WorldEventDef def;
    if (!ReadString(entry, "id", kMaxWorldEventIdBytes, &def.id) ||
        !ValidToken(def.id)) {
        if (error != nullptr) {
            *error = "world event id is invalid";
        }
        return false;
    }
    if (!ReadString(entry, "name", kMaxWorldEventNameBytes, &def.name)) {
        if (error != nullptr) {
            *error = "world event name is required";
        }
        return false;
    }
    if (entry.contains("kind") &&
        (!ReadString(entry, "kind", kMaxWorldEventKindBytes, &def.kind) ||
         !ValidToken(def.kind))) {
        if (error != nullptr) {
            *error = "world event kind is invalid";
        }
        return false;
    }
    if (entry.contains("minDay") &&
        !ReadInt(entry, "minDay", 1, 100000, &def.minDay)) {
        if (error != nullptr) {
            *error = "world event minDay must be 1..100000";
        }
        return false;
    }
    if (entry.contains("maxDay") &&
        !ReadInt(entry, "maxDay", 1, 100000, &def.maxDay)) {
        if (error != nullptr) {
            *error = "world event maxDay must be 1..100000";
        }
        return false;
    }
    if (def.maxDay < def.minDay) {
        if (error != nullptr) {
            *error = "world event maxDay must be >= minDay";
        }
        return false;
    }
    if (entry.contains("weight") &&
        !ReadFloat(entry, "weight", 0.01f, 1000.0f, &def.weight)) {
        if (error != nullptr) {
            *error = "world event weight must be 0.01..1000";
        }
        return false;
    }
    if (entry.contains("cooldownDays") &&
        !ReadInt(entry, "cooldownDays", 0, 365, &def.cooldownDays)) {
        if (error != nullptr) {
            *error = "world event cooldownDays must be 0..365";
        }
        return false;
    }
    if (entry.contains("warningMinutes") &&
        !ReadInt(entry, "warningMinutes", 0, 1440, &def.warningMinutes)) {
        if (error != nullptr) {
            *error = "world event warningMinutes must be 0..1440";
        }
        return false;
    }
    if (entry.contains("durationMinutes") &&
        !ReadInt(entry, "durationMinutes", 1, 1440, &def.durationMinutes)) {
        if (error != nullptr) {
            *error = "world event durationMinutes must be 1..1440";
        }
        return false;
    }
    if (!ReadOptionalString(entry, "region", kMaxWorldEventIdBytes,
                            &def.region) ||
        (!def.region.empty() && def.region != "any" &&
         !ValidToken(def.region))) {
        if (error != nullptr) {
            *error = "world event region is invalid";
        }
        return false;
    }
    if (def.region.empty()) {
        def.region = "any";
    }
    if (entry.contains("failureReputation") &&
        !ReadInt(entry, "failureReputation", -100, 100,
                 &def.failureReputation)) {
        if (error != nullptr) {
            *error = "world event failureReputation must be -100..100";
        }
        return false;
    }
    if (entry.contains("failurePressure") &&
        !ReadInt(entry, "failurePressure", 0, 100, &def.failurePressure)) {
        if (error != nullptr) {
            *error = "world event failurePressure must be 0..100";
        }
        return false;
    }

    const auto stagesIt = entry.find("stages");
    if (stagesIt == entry.end() || !stagesIt->is_array() ||
        stagesIt->empty() || stagesIt->size() > kMaxWorldEventStages) {
        if (error != nullptr) {
            *error = "world event stages must contain 1..8 entries";
        }
        return false;
    }
    for (const nlohmann::json& stageNode : *stagesIt) {
        WorldEventStageDef stage;
        std::string stageError;
        if (!ParseStage(stageNode, &stage, items, &stageError)) {
            if (error != nullptr) {
                *error = "world event '" + def.id + "': " + stageError;
            }
            return false;
        }
        def.stages.push_back(std::move(stage));
    }

    const auto choicesIt = entry.find("choices");
    if (choicesIt == entry.end() || !choicesIt->is_array() ||
        choicesIt->empty() || choicesIt->size() > kMaxWorldEventChoices) {
        if (error != nullptr) {
            *error = "world event choices must contain 1..8 entries";
        }
        return false;
    }
    std::unordered_set<std::string> choiceIds;
    for (const nlohmann::json& choiceNode : *choicesIt) {
        WorldEventChoiceDef choice;
        std::string choiceError;
        if (!ParseChoice(choiceNode, &choice, items, &choiceError)) {
            if (error != nullptr) {
                *error = "world event '" + def.id + "': " + choiceError;
            }
            return false;
        }
        if (!choiceIds.insert(choice.id).second) {
            if (error != nullptr) {
                *error = "world event '" + def.id +
                         "' has duplicate choice id";
            }
            return false;
        }
        def.choices.push_back(std::move(choice));
    }

    if (entry.contains("nextEvent")) {
        if (!ReadOptionalString(entry, "nextEvent", kMaxWorldEventIdBytes,
                                &def.nextEvent) ||
            (!def.nextEvent.empty() && !ValidToken(def.nextEvent))) {
            if (error != nullptr) {
                *error = "world event nextEvent is invalid";
            }
            return false;
        }
    }

    *out = std::move(def);
    return true;
}

const WorldEventDef* FindEvent(const std::vector<WorldEventDef>& defs,
                               std::string_view id) {
    for (const WorldEventDef& def : defs) {
        if (def.id == id) {
            return &def;
        }
    }
    return nullptr;
}

bool AppendWorldEventDocument(const nlohmann::json& doc,
                              const std::string& source,
                              WorldEventConfig* out,
                              WorldEventMergeStats* stats, bool allowEmpty,
                              const items::ItemRegistry* items,
                              std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "world event config output is null";
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
        doc["format"].get<std::string>() != "world-events-v1") {
        if (error != nullptr) {
            *error = source + ": format must be world-events-v1";
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

    std::vector<WorldEventDef> parsed;
    int skipped = 0;
    std::string skippedReason;
    for (const nlohmann::json& entry : doc["defs"]) {
        if (out->defs.size() + parsed.size() >= kMaxWorldEventDefs) {
            skippedReason = "world event count exceeds limit";
            ++skipped;
            break;
        }
        WorldEventDef def;
        std::string defError;
        if (!ParseEvent(entry, &def, items, &defError)) {
            ZL_WARN(kLogModule, "{}: {}", source, defError);
            skippedReason = defError;
            ++skipped;
            continue;
        }
        if (out->Find(def.id) != nullptr || FindEvent(parsed, def.id) != nullptr) {
            ZL_WARN(kLogModule, "{}: duplicate world event id '{}'", source,
                    def.id);
            skippedReason = "duplicate world event id '" + def.id + "'";
            ++skipped;
            continue;
        }
        parsed.push_back(std::move(def));
    }

    std::vector<WorldEventDef> candidate;
    candidate.reserve(out->defs.size() + parsed.size());
    candidate.insert(candidate.end(), out->defs.begin(), out->defs.end());
    const std::size_t firstNew = candidate.size();
    candidate.insert(candidate.end(), std::make_move_iterator(parsed.begin()),
                     std::make_move_iterator(parsed.end()));

    std::vector<bool> keep(candidate.size(), true);
    int acceptedNew = 0;
    for (std::size_t i = 0; i < candidate.size(); ++i) {
        if (i < firstNew || WorldEventChainValid(candidate, candidate[i])) {
            if (i >= firstNew) {
                ++acceptedNew;
            }
            continue;
        }
        ZL_WARN(kLogModule, "{}: nextEvent chain is invalid for '{}'", source,
                candidate[i].id);
        skippedReason = "invalid nextEvent chain";
        ++skipped;
        keep[i] = false;
    }

    if (acceptedNew <= 0) {
        if (error != nullptr) {
            *error = source + ": " +
                     (skippedReason.empty() ? "no valid world event definitions"
                                            : skippedReason);
        }
        return false;
    }

    std::vector<WorldEventDef> accepted;
    accepted.reserve(candidate.size());
    for (std::size_t i = 0; i < candidate.size(); ++i) {
        if (keep[i]) {
            accepted.push_back(std::move(candidate[i]));
        }
    }

    const int added = acceptedNew;
    out->defs = std::move(accepted);
    if (stats != nullptr) {
        stats->defsAdded += added;
        stats->defsSkipped += skipped;
    }
    return true;
}

}  // namespace

const char* WorldEventStageKindName(WorldEventStageKind kind) {
    switch (kind) {
        case WorldEventStageKind::Reach:
            return "reach";
        case WorldEventStageKind::Interact:
            return "interact";
        case WorldEventStageKind::Kill:
            return "kill";
        case WorldEventStageKind::Loot:
            return "loot";
        case WorldEventStageKind::Defend:
            return "defend";
        case WorldEventStageKind::Choice:
            return "choice";
    }
    return "reach";
}

bool ParseWorldEventStageKind(std::string_view value,
                              WorldEventStageKind* out) {
    if (out == nullptr) {
        return false;
    }
    if (value == "reach") {
        *out = WorldEventStageKind::Reach;
        return true;
    }
    if (value == "interact") {
        *out = WorldEventStageKind::Interact;
        return true;
    }
    if (value == "kill") {
        *out = WorldEventStageKind::Kill;
        return true;
    }
    if (value == "loot") {
        *out = WorldEventStageKind::Loot;
        return true;
    }
    if (value == "defend") {
        *out = WorldEventStageKind::Defend;
        return true;
    }
    if (value == "choice") {
        *out = WorldEventStageKind::Choice;
        return true;
    }
    return false;
}

bool WorldEventChainValid(const std::vector<WorldEventDef>& defs,
                          const WorldEventDef& def) {
    std::unordered_set<std::string> seen;
    const WorldEventDef* current = &def;
    for (std::size_t depth = 0; depth <= kMaxWorldEventChainDepth; ++depth) {
        if (!seen.insert(current->id).second) {
            return false;
        }
        if (current->nextEvent.empty()) {
            return true;
        }
        const WorldEventDef* next = FindEvent(defs, current->nextEvent);
        if (next == nullptr) {
            return false;
        }
        current = next;
    }
    return false;
}

bool WorldEventHasNext(const WorldEventConfig& config,
                       const WorldEventDef& def) {
    return !def.nextEvent.empty() && config.Find(def.nextEvent) != nullptr;
}

bool LoadWorldEventConfig(const core::Config& config, WorldEventConfig* out,
                          std::string* error,
                          const items::ItemRegistry* items) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "world event config output is null";
        }
        return false;
    }
    WorldEventConfig result;
    const nlohmann::json& root = config.Raw();
    if (!root.is_object()) {
        *out = std::move(result);
        return true;
    }
    const auto nodeIt = root.find("worldEvents");
    if (nodeIt == root.end() || !nodeIt->is_object()) {
        if (nodeIt != root.end()) {
            if (error != nullptr) {
                *error = "worldEvents must be an object";
            }
            return false;
        }
        *out = std::move(result);
        return true;
    }
    const nlohmann::json& node = *nodeIt;
    if (node.contains("enabled") && !node["enabled"].is_boolean()) {
        if (error != nullptr) {
            *error = "worldEvents.enabled must be a boolean";
        }
        return false;
    }
    result.enabled = node.value("enabled", false);
    if (!result.enabled) {
        *out = std::move(result);
        return true;
    }
    if (node.contains("randomSeed")) {
        if (!node["randomSeed"].is_number_integer() &&
            !node["randomSeed"].is_number_unsigned()) {
            if (error != nullptr) {
                *error = "worldEvents.randomSeed must be an integer";
            }
            return false;
        }
        std::uint64_t seed = 0;
        if (node["randomSeed"].is_number_unsigned()) {
            seed = node["randomSeed"].get<std::uint64_t>();
        } else {
            const std::int64_t signedSeed =
                node["randomSeed"].get<std::int64_t>();
            if (signedSeed < 0) {
                if (error != nullptr) {
                    *error = "worldEvents.randomSeed is out of range";
                }
                return false;
            }
            seed = static_cast<std::uint64_t>(signedSeed);
        }
        if (seed > std::numeric_limits<std::uint32_t>::max()) {
            if (error != nullptr) {
                *error = "worldEvents.randomSeed is out of range";
            }
            return false;
        }
        result.randomSeed = static_cast<std::uint32_t>(seed);
    }
    std::string path = "assets/world_events.json";
    if (node.contains("file")) {
        if (!node["file"].is_string() ||
            node["file"].get<std::string>().empty()) {
            if (error != nullptr) {
                *error = "worldEvents.file must be a non-empty string";
            }
            return false;
        }
        path = node["file"].get<std::string>();
    }
    nlohmann::json doc;
    if (!ReadJsonFile(path, &doc, error)) {
        return false;
    }
    if (!AppendWorldEventDocument(doc, path, &result, nullptr, true, items,
                                  error)) {
        return false;
    }
    if (result.defs.empty()) {
        ZL_WARN(kLogModule, "{}: no world event definitions", path);
    }
    *out = std::move(result);
    return true;
}

bool MergeWorldEventJson(std::string_view json, const std::string& source,
                         WorldEventConfig* out, WorldEventMergeStats* stats,
                         std::string* error,
                         const items::ItemRegistry* items) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "world event config output is null";
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
    if (!AppendWorldEventDocument(doc, source, out, stats, false, items,
                                  error)) {
        return false;
    }
    if (stats != nullptr) {
        ++stats->filesLoaded;
    }
    return true;
}

bool MergeWorldEventFile(const std::filesystem::path& path,
                         WorldEventConfig* out, WorldEventMergeStats* stats,
                         std::string* error, const items::ItemRegistry* items) {
    nlohmann::json doc;
    if (!ReadJsonFile(path, &doc, error)) {
        return false;
    }
    return MergeWorldEventJson(doc.dump(), path.generic_string(), out, stats,
                               error, items);
}

}  // namespace zl::game
