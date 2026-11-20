#include "game/Poi.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "core/Config.h"
#include "core/Log.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "poi";
constexpr std::size_t kMaxPoiIdBytes = 64;
constexpr std::size_t kMaxPoiContainers = 16;
constexpr std::size_t kMaxPoiStages = 8;
constexpr std::size_t kMaxPoiStageNext = 4;
constexpr std::size_t kMaxPoiStageRequires = 4;
constexpr std::size_t kMaxPoiEntries = 8;
constexpr std::size_t kMaxPoiKeys = 8;
constexpr std::size_t kMaxPoiBranches = 2;
constexpr std::size_t kMaxPoiNarratives = 16;
constexpr std::size_t kMaxPoiHintBytes = 256;
constexpr std::size_t kMaxPoiNarrativeBytes = 512;

bool ValidPoiId(std::string_view id) {
    if (id.empty() || id.size() > kMaxPoiIdBytes) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '.';
    });
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

bool ReadInt(const nlohmann::json& node, const char* key, int lo, int hi, int* out) {
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

bool ReadString(const nlohmann::json& node, const char* key, std::string* out) {
    if (!node.contains(key) || !node[key].is_string()) {
        return false;
    }
    std::string value = node[key].get<std::string>();
    if (value.empty() || value.size() > kMaxPoiIdBytes) {
        return false;
    }
    *out = std::move(value);
    return true;
}

bool ReadText(const nlohmann::json& node, const char* key, std::size_t maxBytes,
              std::string* out) {
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

bool ReadBool(const nlohmann::json& node, const char* key, bool* out) {
    if (!node.contains(key) || !node[key].is_boolean()) {
        return false;
    }
    *out = node[key].get<bool>();
    return true;
}

bool ReadOptionalId(const nlohmann::json& node, const char* key,
                    std::string* out) {
    if (!node.contains(key)) {
        return true;
    }
    if (!node[key].is_string()) {
        return false;
    }
    std::string value = node[key].get<std::string>();
    if (!ValidPoiId(value)) {
        return false;
    }
    *out = std::move(value);
    return true;
}

bool ReadOptionalText(const nlohmann::json& node, const char* key,
                      std::size_t maxBytes, std::string* out) {
    if (!node.contains(key)) {
        return true;
    }
    return ReadText(node, key, maxBytes, out);
}

bool ReadIdList(const nlohmann::json& node, const char* key, std::size_t max,
                std::vector<std::string>* out) {
    if (!node.contains(key)) {
        return true;
    }
    const nlohmann::json& list = node[key];
    if (!list.is_array() || list.size() > max) {
        return false;
    }
    std::unordered_set<std::string> seen;
    for (const nlohmann::json& item : list) {
        if (!item.is_string()) {
            return false;
        }
        std::string id = item.get<std::string>();
        if (!ValidPoiId(id) || !seen.insert(id).second) {
            return false;
        }
        out->push_back(std::move(id));
    }
    return true;
}

std::optional<PoiStageKind> ParseStageKind(std::string_view value) {
    if (value == "reach") {
        return PoiStageKind::Reach;
    }
    if (value == "interact") {
        return PoiStageKind::Interact;
    }
    if (value == "kill") {
        return PoiStageKind::Kill;
    }
    if (value == "loot") {
        return PoiStageKind::Loot;
    }
    if (value == "power") {
        return PoiStageKind::Power;
    }
    if (value == "choice") {
        return PoiStageKind::Choice;
    }
    return std::nullopt;
}

bool ValidBossMechanic(std::string_view value) {
    return value.empty() || value == "summon" || value == "charge" ||
           value == "fortify";
}

PoiGuardKind ParseGuardKind(std::string_view value) {
    if (value == "fast") {
        return PoiGuardKind::Fast;
    }
    if (value == "strong") {
        return PoiGuardKind::Strong;
    }
    if (value == "armored") {
        return PoiGuardKind::Armored;
    }
    if (value == "normal") {
        return PoiGuardKind::Normal;
    }
    return PoiGuardKind::Mixed;
}

bool StageExists(const std::vector<PoiStageDef>& stages, std::string_view id) {
    return std::any_of(stages.begin(), stages.end(),
                       [&](const PoiStageDef& stage) { return stage.id == id; });
}

bool KeyExists(const std::vector<PoiKeyDef>& keys, std::string_view id) {
    return std::any_of(keys.begin(), keys.end(),
                       [&](const PoiKeyDef& key) { return key.id == id; });
}

bool ParsePoiStage(const nlohmann::json& node, const PoiDef& def,
                   PoiStageDef* out) {
    if (!node.is_object()) {
        return false;
    }
    if (!ReadString(node, "id", &out->id) || !ValidPoiId(out->id)) {
        return false;
    }
    if (!node.contains("kind") || !node["kind"].is_string()) {
        return false;
    }
    const std::optional<PoiStageKind> kind =
        ParseStageKind(node["kind"].get<std::string>());
    if (!kind.has_value()) {
        return false;
    }
    out->kind = *kind;

    if (node.contains("hint") &&
        !ReadText(node, "hint", kMaxPoiHintBytes, &out->hint)) {
        return false;
    }
    if (node.contains("target") &&
        !ReadText(node, "target", kMaxPoiIdBytes, &out->target)) {
        return false;
    }
    if (out->target.empty() &&
        (out->kind == PoiStageKind::Interact ||
         out->kind == PoiStageKind::Loot || out->kind == PoiStageKind::Power)) {
        return false;
    }

    const bool hasX = node.contains("x");
    const bool hasY = node.contains("y");
    if (hasX != hasY) {
        return false;
    }
    out->x = def.markerX;
    out->y = def.markerY;
    if (hasX && (!ReadFloat(node, "x", -1000000.0f, 1000000.0f, &out->x) ||
                 !ReadFloat(node, "y", -1000000.0f, 1000000.0f, &out->y))) {
        return false;
    }
    if (node.contains("radius") &&
        !ReadFloat(node, "radius", 0.5f, 64.0f, &out->radius)) {
        return false;
    }
    if (out->kind == PoiStageKind::Reach && !hasX) {
        return false;
    }
    if (node.contains("count") &&
        !ReadInt(node, "count", 1, 9999, &out->count)) {
        return false;
    }
    if (!ReadIdList(node, "next", kMaxPoiStageNext, &out->next)) {
        return false;
    }
    if (out->kind == PoiStageKind::Choice) {
        if (out->next.size() < 2 || out->next.size() > kMaxPoiStageNext) {
            return false;
        }
    } else if (out->next.size() > 1) {
        return false;
    }
    if (!ReadIdList(node, "requires", kMaxPoiStageRequires,
                    &out->requiredKeys)) {
        return false;
    }

    if (node.contains("reward")) {
        if (!node["reward"].is_object()) {
            return false;
        }
        const nlohmann::json& reward = node["reward"];
        if (reward.contains("xp") &&
            !ReadFloat(reward, "xp", 0.0f, 100000.0f, &out->reward.xp)) {
            return false;
        }
        if (reward.contains("item")) {
            if (!ReadString(reward, "item", &out->reward.item)) {
                return false;
            }
            if (reward.contains("count") &&
                !ReadInt(reward, "count", 1, 9999, &out->reward.count)) {
                return false;
            }
        } else if (reward.contains("count")) {
            return false;
        }
    }
    return true;
}

void ParsePoiKeys(const nlohmann::json& entry, PoiDef* def) {
    if (!entry.contains("keys")) {
        return;
    }
    if (!entry["keys"].is_array()) {
        ZL_WARN(kLogModule, "POI '{}' keys 必须是数组，已忽略", def->id);
        return;
    }
    std::unordered_set<std::string> ids;
    for (const nlohmann::json& node : entry["keys"]) {
        if (def->keys.size() >= kMaxPoiKeys) {
            ZL_WARN(kLogModule, "POI '{}' 钥匙超过上限 {}，其余已忽略", def->id,
                    kMaxPoiKeys);
            break;
        }
        if (!node.is_object()) {
            continue;
        }
        PoiKeyDef key;
        if (!ReadString(node, "id", &key.id) || !ValidPoiId(key.id) ||
            !ids.insert(key.id).second) {
            ZL_WARN(kLogModule, "POI '{}' 钥匙 id 非法或重复，已跳过", def->id);
            continue;
        }
        if (!ReadString(node, "item", &key.item)) {
            ZL_WARN(kLogModule, "POI '{}' 钥匙 '{}' 缺少 item，已跳过", def->id,
                    key.id);
            continue;
        }
        if (!ReadOptionalText(node, "name", kMaxPoiIdBytes, &key.name) ||
            !ReadOptionalId(node, "source", &key.source)) {
            ZL_WARN(kLogModule, "POI '{}' 钥匙 '{}' 字段非法，已跳过", def->id,
                    key.id);
            continue;
        }
        if (node.contains("consumed") &&
            !ReadBool(node, "consumed", &key.consumed)) {
            ZL_WARN(kLogModule, "POI '{}' 钥匙 '{}' consumed 非法，已跳过",
                    def->id, key.id);
            continue;
        }
        def->keys.push_back(std::move(key));
    }
}

void ParsePoiEntries(const nlohmann::json& entry, PoiDef* def) {
    if (!entry.contains("entries")) {
        return;
    }
    if (!entry["entries"].is_array()) {
        ZL_WARN(kLogModule, "POI '{}' entries 必须是数组，已忽略", def->id);
        return;
    }
    std::unordered_set<std::string> ids;
    for (const nlohmann::json& node : entry["entries"]) {
        if (def->entries.size() >= kMaxPoiEntries) {
            ZL_WARN(kLogModule, "POI '{}' 入口超过上限 {}，其余已忽略", def->id,
                    kMaxPoiEntries);
            break;
        }
        if (!node.is_object()) {
            continue;
        }
        PoiEntryDef item;
        if (!ReadString(node, "id", &item.id) || !ValidPoiId(item.id) ||
            !ids.insert(item.id).second) {
            ZL_WARN(kLogModule, "POI '{}' 入口 id 非法或重复，已跳过", def->id);
            continue;
        }
        if (!ReadOptionalText(node, "label", kMaxPoiIdBytes, &item.label) ||
            !ReadFloat(node, "x", -1000000.0f, 1000000.0f, &item.x) ||
            !ReadFloat(node, "y", -1000000.0f, 1000000.0f, &item.y) ||
            !ReadOptionalId(node, "stage", &item.stage) ||
            !ReadOptionalId(node, "requires", &item.requiredKey)) {
            ZL_WARN(kLogModule, "POI '{}' 入口 '{}' 字段非法，已跳过", def->id,
                    item.id);
            continue;
        }
        if ((!item.stage.empty() && !StageExists(def->stages, item.stage)) ||
            (!item.requiredKey.empty() &&
             !KeyExists(def->keys, item.requiredKey))) {
            ZL_WARN(kLogModule, "POI '{}' 入口 '{}' 引用缺失，已跳过", def->id,
                    item.id);
            continue;
        }
        def->entries.push_back(std::move(item));
    }
}

void ParsePoiBranches(const nlohmann::json& entry, PoiDef* def) {
    if (!entry.contains("branches")) {
        return;
    }
    if (!entry["branches"].is_array()) {
        ZL_WARN(kLogModule, "POI '{}' branches 必须是数组，已忽略", def->id);
        return;
    }
    std::unordered_set<std::string> ids;
    for (const nlohmann::json& node : entry["branches"]) {
        if (def->branches.size() >= kMaxPoiBranches) {
            ZL_WARN(kLogModule, "POI '{}' 分支超过上限 {}，其余已忽略", def->id,
                    kMaxPoiBranches);
            break;
        }
        if (!node.is_object()) {
            continue;
        }
        PoiBranchDef branch;
        if (!ReadString(node, "id", &branch.id) || !ValidPoiId(branch.id) ||
            !ids.insert(branch.id).second) {
            ZL_WARN(kLogModule, "POI '{}' 分支 id 非法或重复，已跳过", def->id);
            continue;
        }
        branch.name = branch.id;
        if (!ReadOptionalText(node, "name", kMaxPoiIdBytes, &branch.name) ||
            !ReadOptionalId(node, "from", &branch.from)) {
            ZL_WARN(kLogModule, "POI '{}' 分支 '{}' 字段非法，已跳过", def->id,
                    branch.id);
            continue;
        }
        if (!node.contains("stages") || !node["stages"].is_array() ||
            node["stages"].empty() ||
            !ReadIdList(node, "stages", kMaxPoiStages, &branch.stages)) {
            ZL_WARN(kLogModule, "POI '{}' 分支 '{}' stages 非法，已跳过", def->id,
                    branch.id);
            continue;
        }
        bool valid = branch.from.empty() || StageExists(def->stages, branch.from);
        for (const std::string& stageId : branch.stages) {
            valid = valid && StageExists(def->stages, stageId);
        }
        if (!valid) {
            ZL_WARN(kLogModule, "POI '{}' 分支 '{}' 引用缺失阶段，已跳过", def->id,
                    branch.id);
            continue;
        }
        def->branches.push_back(std::move(branch));
    }
}

void ParsePoiBoss(const nlohmann::json& entry, PoiDef* def) {
    if (!entry.contains("boss")) {
        return;
    }
    if (!entry["boss"].is_object()) {
        ZL_WARN(kLogModule, "POI '{}' boss 必须是对象，已忽略", def->id);
        return;
    }
    const nlohmann::json& node = entry["boss"];
    PoiBossDef boss;
    if (!ReadString(node, "name", &boss.name) ||
        !ReadString(node, "variant", &boss.variant) ||
        !ReadOptionalId(node, "stage", &boss.stage) ||
        !ReadOptionalText(node, "mechanic", kMaxPoiIdBytes, &boss.mechanic) ||
        !ValidBossMechanic(boss.mechanic) ||
        (node.contains("count") &&
         !ReadInt(node, "count", 1, 8, &boss.count))) {
        ZL_WARN(kLogModule, "POI '{}' boss 字段非法，已禁用", def->id);
        return;
    }
    if (!boss.stage.empty() && !StageExists(def->stages, boss.stage)) {
        ZL_WARN(kLogModule, "POI '{}' boss 引用缺失阶段，已禁用", def->id);
        return;
    }
    boss.enabled = true;
    def->boss = std::move(boss);
}

void ParsePoiNarratives(const nlohmann::json& entry, PoiDef* def) {
    if (!entry.contains("narrative")) {
        return;
    }
    if (!entry["narrative"].is_array()) {
        ZL_WARN(kLogModule, "POI '{}' narrative 必须是数组，已忽略", def->id);
        return;
    }
    std::unordered_set<std::string> ids;
    for (const nlohmann::json& node : entry["narrative"]) {
        if (def->narrative.size() >= kMaxPoiNarratives) {
            ZL_WARN(kLogModule, "POI '{}' 叙事超过上限 {}，其余已忽略", def->id,
                    kMaxPoiNarratives);
            break;
        }
        if (!node.is_object()) {
            continue;
        }
        PoiNarrativeDef note;
        if (!ReadString(node, "id", &note.id) || !ValidPoiId(note.id) ||
            !ids.insert(note.id).second ||
            !ReadText(node, "text", kMaxPoiNarrativeBytes, &note.text) ||
            !ReadOptionalId(node, "stage", &note.stage) ||
            !ReadOptionalText(node, "item", kMaxPoiIdBytes, &note.item) ||
            (!note.stage.empty() && !StageExists(def->stages, note.stage))) {
            ZL_WARN(kLogModule, "POI '{}' 叙事条目非法或引用缺失，已跳过",
                    def->id);
            continue;
        }
        def->narrative.push_back(std::move(note));
    }
}

void PrunePoiStages(PoiDef* def) {
    bool changed = true;
    while (changed) {
        changed = false;
        std::unordered_map<std::string, int> stageIndex;
        for (std::size_t i = 0; i < def->stages.size(); ++i) {
            stageIndex.emplace(def->stages[i].id, static_cast<int>(i));
        }

        std::vector<PoiKeyDef> keptKeys;
        keptKeys.reserve(def->keys.size());
        for (const PoiKeyDef& key : def->keys) {
            if (!key.source.empty() && !stageIndex.contains(key.source)) {
                ZL_WARN(kLogModule, "POI '{}' 钥匙 '{}' 来源阶段缺失，已跳过",
                        def->id, key.id);
                changed = true;
                continue;
            }
            keptKeys.push_back(key);
        }
        if (keptKeys.size() != def->keys.size()) {
            def->keys = std::move(keptKeys);
        }

        std::unordered_map<std::string, const PoiKeyDef*> keyMap;
        for (const PoiKeyDef& key : def->keys) {
            keyMap.emplace(key.id, &key);
        }

        std::vector<PoiStageDef> kept;
        kept.reserve(def->stages.size());
        for (std::size_t i = 0; i < def->stages.size(); ++i) {
            const PoiStageDef& stage = def->stages[i];
            bool valid = true;
            for (const std::string& nextId : stage.next) {
                const auto it = stageIndex.find(nextId);
                if (it == stageIndex.end() ||
                    it->second <= static_cast<int>(i)) {
                    valid = false;
                    break;
                }
            }
            if (valid && stage.kind == PoiStageKind::Choice &&
                (stage.next.size() < 2 ||
                 stage.next.size() > kMaxPoiStageNext)) {
                valid = false;
            }
            if (valid) {
                for (const std::string& keyId : stage.requiredKeys) {
                    const auto it = keyMap.find(keyId);
                    if (it == keyMap.end()) {
                        valid = false;
                        break;
                    }
                    if (!it->second->source.empty()) {
                        const auto sourceIt = stageIndex.find(it->second->source);
                        if (sourceIt == stageIndex.end() ||
                            sourceIt->second >= static_cast<int>(i)) {
                            valid = false;
                            break;
                        }
                    }
                }
            }
            if (!valid) {
                ZL_WARN(kLogModule, "POI '{}' 阶段 '{}' 引用/顺序非法，已跳过",
                        def->id, stage.id);
                changed = true;
                continue;
            }
            kept.push_back(stage);
        }
        if (kept.size() != def->stages.size()) {
            def->stages = std::move(kept);
        }
    }
}

}  // namespace

bool LoadPoiConfig(const core::Config& config, PoiConfig* out, std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "POI 配置输出为空";
        }
        return false;
    }
    PoiConfig result;
    const nlohmann::json& root = config.Raw();
    if (!root.contains("pois")) {
        *out = std::move(result);
        return true;
    }
    const nlohmann::json& node = root["pois"];
    if (!node.is_object()) {
        if (error != nullptr) {
            *error = "pois 必须是对象";
        }
        return false;
    }
    result.enabled = node.value("enabled", false);
    if (!result.enabled) {
        *out = std::move(result);
        return true;
    }
    float guideRange = result.guideRange;
    if (node.contains("guideRange") &&
        (!ReadFloat(node, "guideRange", 1.0f, 500.0f, &guideRange))) {
        ZL_WARN(kLogModule, "pois.guideRange 非法，使用默认值 {}", result.guideRange);
    }
    result.guideRange = guideRange;

    result.boss.healthScale = std::clamp(
        static_cast<float>(config.Get<double>("pois.boss.healthScale",
                                             result.boss.healthScale)),
        1.0f, 10.0f);
    result.boss.resetRange = std::clamp(
        static_cast<float>(config.Get<double>("pois.boss.resetRange",
                                             result.boss.resetRange)),
        4.0f, 64.0f);
    result.boss.resetSeconds = std::clamp(
        static_cast<float>(config.Get<double>("pois.boss.resetSeconds",
                                             result.boss.resetSeconds)),
        1.0f, 120.0f);
    result.boss.summonIntervalSeconds = std::clamp(
        static_cast<float>(config.Get<double>(
            "pois.boss.summonIntervalSeconds",
            result.boss.summonIntervalSeconds)),
        1.0f, 120.0f);
    result.boss.summonCount = std::clamp(
        config.Get<int>("pois.boss.summonCount", result.boss.summonCount), 1,
        8);
    result.boss.summonMax = std::clamp(
        config.Get<int>("pois.boss.summonMax", result.boss.summonMax), 1, 16);
    result.boss.summonTotal = std::clamp(
        config.Get<int>("pois.boss.summonTotal", result.boss.summonTotal), 1,
        32);
    result.boss.chargeIntervalSeconds = std::clamp(
        static_cast<float>(config.Get<double>(
            "pois.boss.chargeIntervalSeconds",
            result.boss.chargeIntervalSeconds)),
        1.0f, 120.0f);
    result.boss.chargeSpeedScale = std::clamp(
        static_cast<float>(config.Get<double>("pois.boss.chargeSpeedScale",
                                             result.boss.chargeSpeedScale)),
        1.0f, 4.0f);
    result.boss.chargeSeconds = std::clamp(
        static_cast<float>(config.Get<double>("pois.boss.chargeSeconds",
                                             result.boss.chargeSeconds)),
        0.2f, 10.0f);

    if (!node.contains("defs") || !node["defs"].is_array()) {
        if (error != nullptr) {
            *error = "pois.defs 必须是数组";
        }
        return false;
    }
    std::unordered_set<std::string> ids;
    for (const nlohmann::json& entry : node["defs"]) {
        if (!entry.is_object()) {
            ZL_WARN(kLogModule, "POI 条目不是对象，已跳过");
            continue;
        }
        PoiDef def;
        if (!ReadString(entry, "id", &def.id) || !ValidPoiId(def.id)) {
            ZL_WARN(kLogModule, "POI id 非法，已跳过");
            continue;
        }
        if (ids.contains(def.id)) {
            ZL_WARN(kLogModule, "POI id 重复 '{}'，已跳过", def.id);
            continue;
        }
        if (!ReadString(entry, "name", &def.name)) {
            ZL_WARN(kLogModule, "POI '{}' 缺少 name，已跳过", def.id);
            continue;
        }
        int threat = 1;
        if (entry.contains("threat") && !ReadInt(entry, "threat", 1, 3, &threat)) {
            ZL_WARN(kLogModule, "POI '{}' threat 非法，已跳过", def.id);
            continue;
        }
        def.threat = threat;
        if (!ReadFloat(entry, "markerX", -1000000.0f, 1000000.0f, &def.markerX) ||
            !ReadFloat(entry, "markerY", -1000000.0f, 1000000.0f, &def.markerY)) {
            ZL_WARN(kLogModule, "POI '{}' markerX/markerY 非法，已跳过", def.id);
            continue;
        }

        const auto containersIt = entry.find("containers");
        if (containersIt == entry.end() || !containersIt->is_array()) {
            ZL_WARN(kLogModule, "POI '{}' 缺少 containers，已跳过", def.id);
            continue;
        }
        for (const nlohmann::json& containerNode : *containersIt) {
            if (def.containers.size() >= kMaxPoiContainers) {
                break;
            }
            if (!containerNode.is_object()) {
                continue;
            }
            PoiContainerDef container;
            if (!ReadFloat(containerNode, "x", -1000000.0f, 1000000.0f, &container.x) ||
                !ReadFloat(containerNode, "y", -1000000.0f, 1000000.0f, &container.y) ||
                !ReadString(containerNode, "tableId", &container.tableId) ||
                !ReadString(containerNode, "sprite", &container.sprite)) {
                ZL_WARN(kLogModule, "POI '{}' 的容器条目非法，已跳过", def.id);
                continue;
            }
            def.containers.push_back(std::move(container));
        }
        if (def.containers.empty()) {
            ZL_WARN(kLogModule, "POI '{}' 没有有效容器，已跳过", def.id);
            continue;
        }

        const auto enemiesIt = entry.find("enemies");
        if (enemiesIt != entry.end() && enemiesIt->is_object()) {
            int count = def.enemies.count;
            float radius = def.enemies.radius;
            if (enemiesIt->contains("count") &&
                !ReadInt(*enemiesIt, "count", 0, 32, &count)) {
                ZL_WARN(kLogModule, "POI '{}' enemies.count 非法，使用 0", def.id);
                count = 0;
            }
            int armoredCount = 0;
            if (enemiesIt->contains("armoredCount") &&
                !ReadInt(*enemiesIt, "armoredCount", 0, 32, &armoredCount)) {
                ZL_WARN(kLogModule, "POI '{}' enemies.armoredCount 非法，使用 0",
                        def.id);
                armoredCount = 0;
            }
            armoredCount = std::clamp(armoredCount, 0, count);
            if (enemiesIt->contains("radius") &&
                !ReadFloat(*enemiesIt, "radius", 1.0f, 32.0f, &radius)) {
                ZL_WARN(kLogModule, "POI '{}' enemies.radius 非法，使用默认值", def.id);
            }
            std::string kind = "mixed";
            if (enemiesIt->contains("kind") && enemiesIt->at("kind").is_string()) {
                kind = enemiesIt->at("kind").get<std::string>();
            } else if (enemiesIt->contains("kind")) {
                ZL_WARN(kLogModule, "POI '{}' enemies.kind 非法，使用 mixed", def.id);
            }
            def.enemies.count = count;
            def.enemies.radius = radius;
            def.enemies.kind = ParseGuardKind(kind);
            def.enemies.armoredCount = armoredCount;
        }

        PoiTaskDef task;
        bool hasTask = false;
        const auto taskIt = entry.find("task");
        if (taskIt != entry.end()) {
            if (!taskIt->is_object()) {
                ZL_WARN(kLogModule, "POI '{}' task 必须是对象，已跳过", def.id);
                continue;
            }
            task.description = taskIt->value("description", std::string("搜索主物资"));
            if (!ReadString(*taskIt, "targetTable", &task.targetTable)) {
                ZL_WARN(kLogModule, "POI '{}' task.targetTable 非法，已跳过", def.id);
                continue;
            }
            const bool targetFound = std::any_of(
                def.containers.begin(), def.containers.end(),
                [&](const PoiContainerDef& container) {
                    return container.tableId == task.targetTable;
                });
            if (!targetFound) {
                ZL_WARN(kLogModule, "POI '{}' task 目标表不在容器列表中，已跳过",
                        def.id);
                continue;
            }
            if (taskIt->contains("rewardXp") &&
                !ReadFloat(*taskIt, "rewardXp", 0.0f, 100000.0f, &task.rewardXp)) {
                ZL_WARN(kLogModule, "POI '{}' task.rewardXp 非法，使用 0", def.id);
                task.rewardXp = 0.0f;
            }
            if (taskIt->contains("rewardItem")) {
                if (!ReadString(*taskIt, "rewardItem", &task.rewardItem)) {
                    ZL_WARN(kLogModule, "POI '{}' task.rewardItem 非法，忽略奖励物品",
                            def.id);
                } else {
                    int rewardCount = task.rewardCount;
                    if (!ReadInt(*taskIt, "rewardCount", 1, 9999, &rewardCount)) {
                        ZL_WARN(kLogModule, "POI '{}' task.rewardCount 非法，使用 1",
                                def.id);
                        rewardCount = 1;
                    }
                    task.rewardCount = rewardCount;
                }
            }
            hasTask = true;
        }

        const auto stagesIt = entry.find("stages");
        if (stagesIt != entry.end()) {
            if (!stagesIt->is_array()) {
                ZL_WARN(kLogModule, "POI '{}' stages 必须是数组，已忽略", def.id);
            } else {
                std::unordered_set<std::string> stageIds;
                for (const nlohmann::json& stageNode : *stagesIt) {
                    if (def.stages.size() >= kMaxPoiStages) {
                        ZL_WARN(kLogModule, "POI '{}' 阶段超过上限 {}，其余已忽略",
                                def.id, kMaxPoiStages);
                        break;
                    }
                    PoiStageDef stage;
                    if (!ParsePoiStage(stageNode, def, &stage)) {
                        ZL_WARN(kLogModule, "POI '{}' 阶段条目非法，已跳过", def.id);
                        continue;
                    }
                    if (!stageIds.insert(stage.id).second) {
                        ZL_WARN(kLogModule, "POI '{}' 阶段 id 重复 '{}'，已跳过",
                                def.id, stage.id);
                        continue;
                    }
                    def.stages.push_back(std::move(stage));
                }
            }
        }

        ParsePoiKeys(entry, &def);
        PrunePoiStages(&def);
        if (def.stages.empty() && !hasTask) {
            ZL_WARN(kLogModule, "POI '{}' 缺少有效 task/stages，已跳过", def.id);
            continue;
        }
        ParsePoiEntries(entry, &def);
        ParsePoiBranches(entry, &def);
        ParsePoiBoss(entry, &def);
        ParsePoiNarratives(entry, &def);
        def.task = std::move(task);
        ids.insert(def.id);
        result.defs.push_back(std::move(def));
    }

    if (result.defs.empty()) {
        ZL_WARN(kLogModule, "pois.enabled=true 但没有有效 POI 定义");
    }
    *out = std::move(result);
    return true;
}

bool MergePoiJson(std::string_view json, const std::string& source,
                  PoiConfig* out, PoiMergeStats* stats, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "POI 配置输出为空";
        }
        return false;
    }
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(json);
    } catch (const std::exception& e) {
        if (error != nullptr) {
            *error = source + ": JSON 解析失败：" + e.what();
        }
        return false;
    }
    if (!doc.is_object()) {
        if (error != nullptr) {
            *error = source + ": 根节点必须是对象";
        }
        return false;
    }
    if (!doc.contains("format") || !doc["format"].is_string() ||
        doc["format"].get<std::string>() != "pois-v1") {
        if (error != nullptr) {
            *error = source + ": format 必须是 pois-v1";
        }
        return false;
    }
    if (!doc.contains("defs") || !doc["defs"].is_array() || doc["defs"].empty()) {
        if (error != nullptr) {
            *error = source + ": defs 必须是非空数组";
        }
        return false;
    }

    core::Config modConfig;
    nlohmann::json pois = nlohmann::json::object();
    pois["enabled"] = true;
    pois["guideRange"] = out->guideRange;
    pois["defs"] = doc["defs"];
    modConfig.Set("pois", pois);
    PoiConfig parsed;
    if (!LoadPoiConfig(modConfig, &parsed, error)) {
        return false;
    }

    const std::size_t before = out->defs.size();
    for (PoiDef& def : parsed.defs) {
        if (out->Find(def.id) != nullptr) {
            ZL_WARN(kLogModule, "POI id 重复 '{}'（{}），已跳过", def.id,
                    source);
            continue;
        }
        out->defs.push_back(std::move(def));
    }
    if (stats != nullptr) {
        ++stats->filesLoaded;
        stats->defsAdded += static_cast<int>(out->defs.size() - before);
        stats->defsSkipped += static_cast<int>(doc["defs"].size() -
                                               (out->defs.size() - before));
    }
    return true;
}

bool MergePoiFile(const std::filesystem::path& path, PoiConfig* out,
                  PoiMergeStats* stats, std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        if (error != nullptr) {
            *error = "无法打开 POI 文件: " + path.generic_string();
        }
        ZL_ERROR(kLogModule, "无法打开 POI 文件: {}", path.generic_string());
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return MergePoiJson(buffer.str(), path.generic_string(), out, stats, error);
}

const char* PoiGuardKindName(PoiGuardKind kind) {
    switch (kind) {
        case PoiGuardKind::Fast:
            return "fast";
        case PoiGuardKind::Strong:
            return "strong";
        case PoiGuardKind::Armored:
            return "armored";
        case PoiGuardKind::Normal:
            return "normal";
        case PoiGuardKind::Mixed:
        default:
            return "mixed";
    }
}

PoiGuardKind PoiGuardKindForSpawn(const PoiEnemyDef& enemies, int index) {
    if (index < enemies.armoredCount) {
        return PoiGuardKind::Armored;
    }
    if (enemies.kind != PoiGuardKind::Mixed) {
        return enemies.kind;
    }
    switch (index % 3) {
        case 1:
            return PoiGuardKind::Fast;
        case 2:
            return PoiGuardKind::Strong;
        case 0:
        default:
            return PoiGuardKind::Normal;
    }
}

const char* PoiStageKindName(PoiStageKind kind) {
    switch (kind) {
        case PoiStageKind::Interact:
            return "interact";
        case PoiStageKind::Kill:
            return "kill";
        case PoiStageKind::Loot:
            return "loot";
        case PoiStageKind::Power:
            return "power";
        case PoiStageKind::Choice:
            return "choice";
        case PoiStageKind::Reach:
        default:
            return "reach";
    }
}

const char* PoiThreatLabel(int threat) {
    switch (threat) {
        case 3:
            return "高";
        case 2:
            return "中";
        case 1:
        default:
            return "低";
    }
}

std::string PoiDirectionLabel(float dx, float dy) {
    constexpr float kDeadZone = 0.35f;
    const bool north = dy < -kDeadZone;
    const bool south = dy > kDeadZone;
    const bool east = dx > kDeadZone;
    const bool west = dx < -kDeadZone;
    if (north && east) {
        return "东北";
    }
    if (north && west) {
        return "西北";
    }
    if (south && east) {
        return "东南";
    }
    if (south && west) {
        return "西南";
    }
    if (north) {
        return "北";
    }
    if (south) {
        return "南";
    }
    if (east) {
        return "东";
    }
    if (west) {
        return "西";
    }
    return "附近";
}

std::string PoiTaskText(const PoiDef& def, bool targetSearched, int liveGuards) {
    if (targetSearched && liveGuards > 0) {
        return "物资已搜，清剿剩余守军";
    }
    if (!targetSearched && liveGuards == 0) {
        return "区域已清，搜索主物资";
    }
    if (targetSearched) {
        return "任务已完成";
    }
    return def.task.description.empty() ? "清剿守军并搜索主物资"
                                        : def.task.description;
}

std::string PoiGuideText(const PoiDef& def, float dx, float dy, float distance,
                         bool targetSearched, int liveGuards) {
    if (!def.task.targetTable.empty() && targetSearched && liveGuards <= 0) {
        return "POI · " + def.name + " · 已完成";
    }
    std::ostringstream out;
    out << "POI · " << def.name << " [威胁" << PoiThreatLabel(def.threat) << "] "
        << PoiDirectionLabel(dx, dy) << ' ' << std::fixed << std::setprecision(0)
        << std::max(0.0f, distance) << "格 · "
        << PoiTaskText(def, targetSearched, liveGuards);
    return out.str();
}

const char* PoiGuideTone(const PoiDef& def, bool targetSearched, int liveGuards) {
    if (!def.task.targetTable.empty() && targetSearched && liveGuards <= 0) {
        return "ok";
    }
    if (def.threat >= 2 || liveGuards > 0) {
        return "warn";
    }
    return "info";
}

bool IsPoiCompleted(const std::vector<std::string>& completed, std::string_view id) {
    return std::find(completed.begin(), completed.end(), id) != completed.end();
}

bool MarkPoiCompleted(std::vector<std::string>* completed, std::string id) {
    if (completed == nullptr || id.empty() ||
        IsPoiCompleted(*completed, id)) {
        return false;
    }
    completed->push_back(std::move(id));
    return true;
}

bool ShouldCompletePoiTask(const PoiDef& def, bool targetSearched, int liveGuards) {
    return !def.task.targetTable.empty() && targetSearched && liveGuards <= 0;
}

namespace {

int PoiStageIndex(const PoiDef& def, std::string_view stageId) {
    for (std::size_t i = 0; i < def.stages.size(); ++i) {
        if (def.stages[i].id == stageId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool AddUniqueString(std::vector<std::string>* values, const std::string& value) {
    if (values == nullptr || value.empty() ||
        std::find(values->begin(), values->end(), value) != values->end()) {
        return false;
    }
    values->push_back(value);
    return true;
}

PoiStageUpdate CompletePoiStage(const PoiDef& def, PoiProgress* progress,
                                int index) {
    PoiStageUpdate out;
    if (progress == nullptr || index < 0 ||
        index >= static_cast<int>(def.stages.size())) {
        return out;
    }
    const PoiStageDef& stage = def.stages[static_cast<std::size_t>(index)];
    if (!AddUniqueString(&progress->completedStages, stage.id)) {
        return out;
    }
    progress->failed = false;
    progress->failedStage = -1;
    out.stageCompleted = true;
    out.stageId = stage.id;
    out.reward = stage.reward;
    out.rewardAvailable =
        stage.reward.xp > 0.0f || !stage.reward.item.empty();
    for (const PoiKeyDef& key : def.keys) {
        if (key.source == stage.id) {
            AddUniqueString(&progress->acquiredKeys, key.id);
        }
    }
    if (stage.kind == PoiStageKind::Choice) {
        progress->currentStage = index;
        progress->awaitingChoice = true;
        progress->stageKills = 0;
        out.awaitingChoice = true;
        return out;
    }
    progress->awaitingChoice = false;
    progress->stageKills = 0;
    if (stage.next.empty()) {
        progress->currentStage = -1;
        progress->completed = true;
        out.poiCompleted = true;
        return out;
    }
    const int next = PoiStageIndex(def, stage.next.front());
    if (next < 0 || next <= index) {
        progress->currentStage = -1;
        progress->completed = true;
        out.poiCompleted = true;
        return out;
    }
    progress->currentStage = next;
    out.advanced = true;
    out.nextStageId = def.stages[static_cast<std::size_t>(next)].id;
    return out;
}

}  // namespace

PoiProgress* FindPoiProgress(std::vector<PoiProgress>* progress,
                             std::string_view poiId) {
    if (progress == nullptr) {
        return nullptr;
    }
    for (PoiProgress& item : *progress) {
        if (item.poiId == poiId) {
            return &item;
        }
    }
    return nullptr;
}

const PoiProgress* FindPoiProgress(const std::vector<PoiProgress>& progress,
                                   std::string_view poiId) {
    for (const PoiProgress& item : progress) {
        if (item.poiId == poiId) {
            return &item;
        }
    }
    return nullptr;
}

PoiProgress& EnsurePoiProgress(std::vector<PoiProgress>* progress,
                               std::string poiId) {
    if (progress == nullptr) {
        static PoiProgress empty;
        empty = PoiProgress{};
        return empty;
    }
    if (PoiProgress* existing = FindPoiProgress(progress, poiId);
        existing != nullptr) {
        return *existing;
    }
    PoiProgress created;
    created.poiId = std::move(poiId);
    progress->push_back(std::move(created));
    return progress->back();
}

bool IsPoiStageCompleted(const PoiProgress& progress,
                         std::string_view stageId) {
    return std::find(progress.completedStages.begin(),
                     progress.completedStages.end(),
                     stageId) != progress.completedStages.end();
}

bool StartPoiProgress(const PoiDef& def, PoiProgress* progress) {
    return StartPoiProgressAtStage(def, progress, {});
}

bool StartPoiProgressAtStage(const PoiDef& def, PoiProgress* progress,
                             std::string_view stageId) {
    if (progress == nullptr || def.stages.empty() || progress->completed ||
        progress->failed || progress->currentStage >= 0) {
        return false;
    }
    int index = 0;
    if (!stageId.empty()) {
        index = PoiStageIndex(def, stageId);
        if (index < 0) {
            return false;
        }
    }
    if (IsPoiStageCompleted(*progress, def.stages[index].id)) {
        return false;
    }
    progress->currentStage = index;
    progress->failedStage = -1;
    progress->awaitingChoice = false;
    progress->attempts = std::max(1, progress->attempts);
    progress->stageKills = 0;
    progress->lastLiveGuards = 0;
    return true;
}

const PoiStageDef* FindPoiStage(const PoiDef& def, std::string_view stageId) {
    const int index = PoiStageIndex(def, stageId);
    return index >= 0 ? &def.stages[static_cast<std::size_t>(index)]
                      : nullptr;
}

const PoiStageDef* PoiBossStage(const PoiDef& def) {
    if (!def.boss.enabled || def.stages.empty()) {
        return nullptr;
    }
    if (def.boss.stage.empty()) {
        return &def.stages.back();
    }
    return FindPoiStage(def, def.boss.stage);
}

bool PoiStageHasBoss(const PoiDef& def, const PoiStageDef& stage) {
    if (stage.kind != PoiStageKind::Kill) {
        return false;
    }
    const PoiStageDef* bossStage = PoiBossStage(def);
    return bossStage != nullptr && bossStage->id == stage.id;
}

std::vector<PoiStageEnemySpec> BuildPoiStageEnemyPlan(
    const PoiDef& def, const PoiStageDef& stage) {
    std::vector<PoiStageEnemySpec> out;
    if (stage.kind != PoiStageKind::Kill) {
        return out;
    }
    const int total = std::clamp(stage.count, 1, 9999);
    int bossCount = 0;
    PoiGuardKind bossKind = PoiGuardKind::Armored;
    if (PoiStageHasBoss(def, stage)) {
        bossCount = std::clamp(def.boss.count, 1, total);
        const PoiGuardKind parsed = ParseGuardKind(def.boss.variant);
        bossKind = parsed == PoiGuardKind::Mixed ? PoiGuardKind::Armored : parsed;
    }
    const PoiGuardKind parsedElite = ParseGuardKind(stage.target);
    const PoiGuardKind eliteKind =
        parsedElite == PoiGuardKind::Mixed ? PoiGuardKind::Armored : parsedElite;
    out.reserve(static_cast<std::size_t>(total));
    for (int i = 0; i < bossCount; ++i) {
        out.push_back(PoiStageEnemySpec{PoiStageEnemyRole::Boss, bossKind, i});
    }
    for (int i = bossCount; i < total; ++i) {
        out.push_back(PoiStageEnemySpec{PoiStageEnemyRole::Elite, eliteKind, i});
    }
    return out;
}

const char* PoiStageEnemyRoleName(PoiStageEnemyRole role) {
    switch (role) {
        case PoiStageEnemyRole::Boss:
            return "boss";
        case PoiStageEnemyRole::Summon:
            return "summon";
        case PoiStageEnemyRole::Elite:
        default:
            return "elite";
    }
}

PoiBossMechanic PoiBossMechanicFor(std::string_view mechanic) {
    if (mechanic == "summon") {
        return PoiBossMechanic::Summon;
    }
    if (mechanic == "charge") {
        return PoiBossMechanic::Charge;
    }
    if (mechanic == "fortify") {
        return PoiBossMechanic::Fortify;
    }
    return PoiBossMechanic::None;
}

const char* PoiBossMechanicName(PoiBossMechanic mechanic) {
    switch (mechanic) {
        case PoiBossMechanic::Summon:
            return "summon";
        case PoiBossMechanic::Charge:
            return "charge";
        case PoiBossMechanic::Fortify:
            return "fortify";
        case PoiBossMechanic::None:
        default:
            return "none";
    }
}

const char* PoiBossMechanicText(PoiBossMechanic mechanic) {
    switch (mechanic) {
        case PoiBossMechanic::Summon:
            return "召唤增援";
        case PoiBossMechanic::Charge:
            return "冲锋";
        case PoiBossMechanic::Fortify:
            return "破坏工事";
        case PoiBossMechanic::None:
        default:
            return "无特殊机制";
    }
}

const PoiKeyDef* FindPoiKey(const PoiDef& def, std::string_view keyId) {
    for (const PoiKeyDef& key : def.keys) {
        if (key.id == keyId) {
            return &key;
        }
    }
    return nullptr;
}

const PoiEntryDef* FindPoiEntry(const PoiDef& def, std::string_view entryId) {
    for (const PoiEntryDef& entry : def.entries) {
        if (entry.id == entryId) {
            return &entry;
        }
    }
    return nullptr;
}

const PoiStageDef* CurrentPoiStage(const PoiDef& def,
                                   const PoiProgress& progress) {
    if (progress.currentStage < 0 ||
        progress.currentStage >= static_cast<int>(def.stages.size())) {
        return nullptr;
    }
    return &def.stages[static_cast<std::size_t>(progress.currentStage)];
}

bool PoiStageKeysMet(const PoiDef& def, const PoiProgress& progress,
                     const PoiStageDef& stage) {
    static_cast<void>(def);
    for (const std::string& keyId : stage.requiredKeys) {
        if (std::find(progress.acquiredKeys.begin(),
                      progress.acquiredKeys.end(),
                      keyId) == progress.acquiredKeys.end()) {
            return false;
        }
    }
    return true;
}

bool MarkPoiKeyAcquired(std::vector<std::string>* keys,
                        std::string_view keyId) {
    if (keys == nullptr || !ValidPoiId(keyId) || keys->size() >= 8) {
        return false;
    }
    if (std::find(keys->begin(), keys->end(), keyId) != keys->end()) {
        return false;
    }
    keys->emplace_back(keyId);
    return true;
}

std::vector<const PoiKeyDef*> PoiStageKeyAwards(const PoiDef& def,
                                                std::string_view stageId) {
    std::vector<const PoiKeyDef*> out;
    for (const PoiKeyDef& key : def.keys) {
        if (key.source == stageId && !key.item.empty()) {
            out.push_back(&key);
        }
    }
    return out;
}

std::vector<const PoiBranchDef*> PoiAvailableBranches(
    const PoiDef& def, const PoiProgress& progress) {
    std::vector<const PoiBranchDef*> out;
    const PoiStageDef* stage = CurrentPoiStage(def, progress);
    if (stage == nullptr || stage->kind != PoiStageKind::Choice) {
        return out;
    }
    for (const PoiBranchDef& branch : def.branches) {
        if (!branch.from.empty() && branch.from != stage->id) {
            continue;
        }
        bool future = false;
        for (const std::string& stageId : branch.stages) {
            const int index = PoiStageIndex(def, stageId);
            if (index > progress.currentStage) {
                future = true;
                break;
            }
        }
        if (future) {
            out.push_back(&branch);
        }
    }
    return out;
}

std::string PoiKeyDisplayName(const PoiKeyDef& key) {
    if (!key.name.empty()) {
        return key.name;
    }
    if (!key.item.empty()) {
        return key.item;
    }
    return key.id;
}

std::string PoiStageObjectiveText(const PoiDef& def,
                                  const PoiProgress& progress,
                                  const PoiStageDef& stage) {
    static_cast<void>(def);
    if (!stage.hint.empty()) {
        return stage.hint;
    }
    switch (stage.kind) {
        case PoiStageKind::Reach:
            return "前往目标地点";
        case PoiStageKind::Interact:
            return "操作 " + stage.target;
        case PoiStageKind::Kill: {
            const int count = std::max(1, stage.count);
            const std::string progressText =
                std::to_string(std::min(count, std::max(0, progress.stageKills))) +
                "/" + std::to_string(count);
            if (PoiStageHasBoss(def, stage)) {
                return "击败 " + def.boss.name + "（" +
                       PoiBossMechanicText(
                           PoiBossMechanicFor(def.boss.mechanic)) +
                       "） " + progressText;
            }
            return "清除目标：" + progressText;
        }
        case PoiStageKind::Loot:
            return "取得 " + stage.target + " ×" +
                   std::to_string(std::max(1, stage.count));
        case PoiStageKind::Power:
            return "启动发电机";
        case PoiStageKind::Choice:
            return "选择路线";
    }
    return "继续探索";
}

std::string PoiStageRequiredKeysText(const PoiDef& def,
                                     const PoiProgress& progress,
                                     const PoiStageDef& stage) {
    std::string out;
    for (const std::string& keyId : stage.requiredKeys) {
        if (std::find(progress.acquiredKeys.begin(),
                      progress.acquiredKeys.end(),
                      keyId) != progress.acquiredKeys.end()) {
            continue;
        }
        const PoiKeyDef* key = FindPoiKey(def, keyId);
        if (!out.empty()) {
            out += "、";
        }
        out += key != nullptr ? PoiKeyDisplayName(*key) : keyId;
    }
    return out;
}

std::string PoiChoiceText(const PoiDef& def, const PoiProgress& progress) {
    const auto branches = PoiAvailableBranches(def, progress);
    std::string out;
    for (std::size_t i = 0; i < branches.size(); ++i) {
        if (!out.empty()) {
            out += " / ";
        }
        out += std::to_string(i + 1) + " " +
               (branches[i]->name.empty() ? branches[i]->id
                                          : branches[i]->name);
    }
    return out;
}

std::string PoiStageProgressText(const PoiDef& def,
                                 const PoiProgress& progress) {
    if (def.stages.empty()) {
        return "阶段探索";
    }
    if (progress.completed) {
        return "阶段 " + std::to_string(def.stages.size()) + "/" +
               std::to_string(def.stages.size()) + " · 完成";
    }
    int index = progress.currentStage;
    if (index < 0) {
        index = progress.failedStage;
    }
    if (index < 0 || index >= static_cast<int>(def.stages.size())) {
        return "阶段探索 · 未开始";
    }
    std::string out = "阶段 " + std::to_string(index + 1) + "/" +
                      std::to_string(def.stages.size());
    if (progress.failed) {
        out += " · 失败";
    } else if (progress.awaitingChoice) {
        out += " · 待选路线";
    }
    return out;
}

std::string PoiStageHudObjective(const PoiDef& def,
                                 const PoiProgress& progress) {
    if (progress.completed) {
        return "全部阶段完成，奖励已结算";
    }
    if (progress.failed) {
        return "阶段失败：返回入口或 POI 标记附近重试";
    }
    const PoiStageDef* stage = CurrentPoiStage(def, progress);
    if (stage == nullptr) {
        return "接近入口开始阶段探索";
    }
    return PoiStageObjectiveText(def, progress, *stage);
}

std::string PoiStageHudRequirements(const PoiDef& def,
                                    const PoiProgress& progress) {
    if (progress.completed) {
        return "按 M 查看世界地图 · 阶段奖励不会重复";
    }
    if (progress.failed) {
        return "自动重试：入口 6 格内 / 标记 16 格内";
    }
    const PoiStageDef* stage = CurrentPoiStage(def, progress);
    if (stage == nullptr) {
        return "靠近 POI 标记或入口";
    }
    std::string out;
    const std::string keys =
        PoiStageRequiredKeysText(def, progress, *stage);
    if (!keys.empty()) {
        out = "钥匙：" + keys;
    }
    if (stage->kind == PoiStageKind::Power) {
        if (!out.empty()) {
            out += " · ";
        }
        out += progress.powered ? "供电：已恢复" : "供电：需要发电机燃料";
    } else if (progress.powered) {
        if (!out.empty()) {
            out += " · ";
        }
        out += "供电：已恢复";
    }
    if (stage->kind == PoiStageKind::Choice ||
        progress.awaitingChoice) {
        const std::string choices = PoiChoiceText(def, progress);
        if (!choices.empty()) {
            if (!out.empty()) {
                out += " · ";
            }
            out += "路线：" + choices + "（按 1/2）";
        }
    }
    if (PoiStageHasBoss(def, *stage)) {
        if (!out.empty()) {
            out += " · ";
        }
        out += "Boss：" + def.boss.name + " · " +
               PoiBossMechanicText(
                   PoiBossMechanicFor(def.boss.mechanic));
    }
    if (out.empty()) {
        return "无额外条件";
    }
    return out;
}

std::string PoiStageNarrativeText(const PoiDef& def,
                                  std::string_view stageId) {
    std::string out;
    int count = 0;
    for (const PoiNarrativeDef& note : def.narrative) {
        if (note.stage != stageId || note.text.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += " · ";
        }
        out += note.text;
        if (!note.item.empty()) {
            out += "（文件：" + note.item + "）";
        }
        if (++count >= 2) {
            break;
        }
    }
    return out;
}

std::string PoiStageMapStatusText(const PoiDef& def,
                                  const PoiProgress& progress) {
    if (def.stages.empty()) {
        return {};
    }
    if (progress.completed) {
        return "已完成";
    }
    int index = progress.currentStage;
    if (index < 0) {
        index = progress.failedStage;
    }
    if (index < 0 || index >= static_cast<int>(def.stages.size())) {
        return "阶段探索";
    }
    std::string out = "阶段 " + std::to_string(index + 1) + "/" +
                      std::to_string(def.stages.size());
    const std::size_t completed =
        std::min(progress.completedStages.size(), def.stages.size());
    if (completed > 0) {
        out += " · 已完成 " + std::to_string(completed) + "/" +
               std::to_string(def.stages.size());
    }
    if (progress.failed) {
        out += " · 失败可重试";
    } else if (progress.awaitingChoice) {
        out += " · 待选路线";
    }
    return out;
}

std::string PoiStageResultTitle(const PoiDef& def, bool completed) {
    return def.name + (completed ? " · 阶段探索完成"
                                 : " · 阶段探索失败");
}

std::string PoiStageResultText(const PoiDef& def,
                               const PoiProgress& progress, bool completed) {
    if (completed) {
        return "全部 " + std::to_string(def.stages.size()) +
               " 阶段完成 · 奖励已结算 · 按 M 查看地图";
    }
    int index = progress.failedStage;
    if (index < 0) {
        index = progress.currentStage;
    }
    const std::string stage =
        index >= 0 && index < static_cast<int>(def.stages.size())
            ? std::to_string(index + 1) + "/" +
                  std::to_string(def.stages.size())
            : std::string("-");
    return "阶段 " + stage +
           " 失败 · 回到入口或 POI 标记附近自动重试";
}

bool PoiStageMapPoint(const PoiDef& def, const PoiProgress& progress,
                      float* x, float* y) {
    if (x == nullptr || y == nullptr) {
        return false;
    }
    const PoiStageDef* stage = CurrentPoiStage(def, progress);
    if (stage == nullptr) {
        return false;
    }
    *x = stage->x;
    *y = stage->y;
    return true;
}

PoiStageUpdate TickPoiStage(const PoiDef& def, PoiProgress* progress, float x,
                            float y) {
    PoiStageUpdate out;
    if (progress == nullptr || def.stages.empty() || progress->completed ||
        progress->failed) {
        return out;
    }
    if (progress->currentStage < 0) {
        if (!StartPoiProgress(def, progress)) {
            return out;
        }
        out.started = true;
    }
    if (progress->awaitingChoice) {
        out.awaitingChoice = true;
        return out;
    }
    const PoiStageDef* stage = CurrentPoiStage(def, *progress);
    if (stage == nullptr) {
        return out;
    }
    if (!PoiStageKeysMet(def, *progress, *stage)) {
        out.blocked = true;
        return out;
    }
    if (stage->kind != PoiStageKind::Reach) {
        return out;
    }
    const float dx = x - stage->x;
    const float dy = y - stage->y;
    if (dx * dx + dy * dy > stage->radius * stage->radius) {
        return out;
    }
    PoiStageUpdate completed = CompletePoiStage(def, progress,
                                                progress->currentStage);
    completed.started = out.started;
    return completed;
}

PoiStageUpdate SignalPoiStage(const PoiDef& def, PoiProgress* progress,
                              PoiStageSignal signal, int amount) {
    PoiStageUpdate out;
    if (progress == nullptr || def.stages.empty() || progress->completed ||
        progress->failed) {
        return out;
    }
    if (progress->currentStage < 0) {
        if (!StartPoiProgress(def, progress)) {
            return out;
        }
        out.started = true;
    }
    if (progress->awaitingChoice) {
        out.awaitingChoice = true;
        return out;
    }
    const PoiStageDef* stage = CurrentPoiStage(def, *progress);
    if (stage == nullptr) {
        return out;
    }
    if (!PoiStageKeysMet(def, *progress, *stage)) {
        out.blocked = true;
        return out;
    }
    if (signal == PoiStageSignal::Interact) {
        if (stage->kind != PoiStageKind::Interact) {
            return out;
        }
        PoiStageUpdate completed =
            CompletePoiStage(def, progress, progress->currentStage);
        completed.started = out.started;
        return completed;
    }
    if (signal == PoiStageSignal::Power) {
        if (stage->kind != PoiStageKind::Power) {
            return out;
        }
        progress->powered = true;
        PoiStageUpdate completed =
            CompletePoiStage(def, progress, progress->currentStage);
        completed.started = out.started;
        return completed;
    }
    if (amount <= 0 || stage->count <= 0) {
        return out;
    }
    if (signal == PoiStageSignal::Kill) {
        if (stage->kind != PoiStageKind::Kill) {
            return out;
        }
    } else if (signal == PoiStageSignal::Loot) {
        if (stage->kind != PoiStageKind::Loot) {
            return out;
        }
    } else {
        return out;
    }
    progress->stageKills =
        std::min(9999, progress->stageKills + std::max(0, amount));
    if (progress->stageKills < stage->count) {
        return out;
    }
    PoiStageUpdate completed =
        CompletePoiStage(def, progress, progress->currentStage);
    completed.started = out.started;
    return completed;
}

PoiStageUpdate ChoosePoiBranch(const PoiDef& def, PoiProgress* progress,
                               std::string_view branchId) {
    PoiStageUpdate out;
    if (progress == nullptr || progress->completed || progress->failed ||
        progress->currentStage < 0) {
        return out;
    }
    const PoiStageDef* choice = CurrentPoiStage(def, *progress);
    if (choice == nullptr || choice->kind != PoiStageKind::Choice) {
        return out;
    }
    if (!progress->awaitingChoice) {
        if (!PoiStageKeysMet(def, *progress, *choice)) {
            out.blocked = true;
            return out;
        }
        out = CompletePoiStage(def, progress, progress->currentStage);
        if (!out.awaitingChoice) {
            return out;
        }
    }
    const PoiBranchDef* selected = nullptr;
    for (const PoiBranchDef& branch : def.branches) {
        if (branch.id == branchId &&
            (branch.from.empty() || branch.from == choice->id)) {
            selected = &branch;
            break;
        }
    }
    if (selected == nullptr) {
        out.awaitingChoice = true;
        return out;
    }
    int next = -1;
    for (const std::string& stageId : selected->stages) {
        const int index = PoiStageIndex(def, stageId);
        if (index > progress->currentStage) {
            next = index;
            break;
        }
    }
    if (next < 0) {
        out.awaitingChoice = true;
        return out;
    }
    progress->chosenBranch = selected->id;
    progress->currentStage = next;
    progress->awaitingChoice = false;
    progress->stageKills = 0;
    progress->lastLiveGuards = 0;
    out.advanced = true;
    out.awaitingChoice = false;
    out.nextStageId = def.stages[static_cast<std::size_t>(next)].id;
    return out;
}

bool FailPoiProgress(PoiProgress* progress) {
    if (progress == nullptr || progress->completed || progress->failed ||
        progress->currentStage < 0) {
        return false;
    }
    progress->failed = true;
    progress->failedStage = progress->currentStage;
    progress->awaitingChoice = false;
    return true;
}

bool RetryPoiProgress(const PoiDef& def, PoiProgress* progress) {
    if (progress == nullptr || !progress->failed || def.stages.empty()) {
        return false;
    }
    progress->failed = false;
    progress->attempts = std::min(1000000, std::max(1, progress->attempts) + 1);
    progress->stageKills = 0;
    progress->lastLiveGuards = 0;
    int stage = progress->failedStage;
    progress->failedStage = -1;

    if (stage < 0 || stage >= static_cast<int>(def.stages.size())) {
        stage = 0;
    }
    while (stage < static_cast<int>(def.stages.size()) &&
           IsPoiStageCompleted(*progress, def.stages[stage].id) &&
           def.stages[stage].kind != PoiStageKind::Choice) {
        ++stage;
    }
    if (stage >= static_cast<int>(def.stages.size())) {
        progress->currentStage = -1;
        progress->completed = true;
        progress->awaitingChoice = false;
        return true;
    }
    progress->currentStage = stage;
    progress->awaitingChoice =
        def.stages[stage].kind == PoiStageKind::Choice &&
        IsPoiStageCompleted(*progress, def.stages[stage].id);
    return true;
}

bool ClaimPoiStageReward(std::vector<std::string>* claims,
                         std::string_view poiId, std::string_view stageId) {
    if (claims == nullptr || !ValidPoiId(poiId) || !ValidPoiId(stageId) ||
        claims->size() >= 256) {
        return false;
    }
    std::string key(poiId);
    key.push_back('.');
    key.append(stageId);
    if (key.size() > kMaxPoiIdBytes * 2 + 1) {
        return false;
    }
    if (std::find(claims->begin(), claims->end(), key) != claims->end()) {
        return false;
    }
    claims->push_back(std::move(key));
    return true;
}

}  // namespace zl::game
