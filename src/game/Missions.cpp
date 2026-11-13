#include "game/Missions.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "core/Config.h"
#include "core/Log.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "missions";
constexpr std::size_t kMaxMissionIdBytes = 64;
constexpr std::size_t kMaxMissionTextBytes = 128;
constexpr std::size_t kMaxMissionObjectives = 8;
constexpr std::size_t kMaxMissionDefs = 64;

bool ValidToken(std::string_view value) {
    if (value.empty() || value.size() > kMaxMissionIdBytes) {
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

bool ParseKind(std::string_view value, MissionKind* out) {
    if (value == "once") {
        *out = MissionKind::Once;
        return true;
    }
    if (value == "daily") {
        *out = MissionKind::Daily;
        return true;
    }
    if (value == "random") {
        *out = MissionKind::Random;
        return true;
    }
    return false;
}

bool ParseSource(std::string_view value, MissionSource* out) {
    if (value == "radio") {
        *out = MissionSource::Radio;
        return true;
    }
    if (value == "note") {
        *out = MissionSource::Note;
        return true;
    }
    if (value == "npc") {
        *out = MissionSource::Npc;
        return true;
    }
    if (value == "base") {
        *out = MissionSource::Base;
        return true;
    }
    return false;
}

bool ParseTaskKind(std::string_view value, MissionTaskKind* out) {
    if (value == "generic") {
        *out = MissionTaskKind::Generic;
        return true;
    }
    if (value == "help") {
        *out = MissionTaskKind::Help;
        return true;
    }
    if (value == "escort") {
        *out = MissionTaskKind::Escort;
        return true;
    }
    if (value == "clear") {
        *out = MissionTaskKind::Clear;
        return true;
    }
    if (value == "supply") {
        *out = MissionTaskKind::Supply;
        return true;
    }
    return false;
}

bool ParseObjectiveKind(std::string_view value, MissionObjectiveKind* out) {
    if (value == "collect") {
        *out = MissionObjectiveKind::CollectItem;
        return true;
    }
    if (value == "interact") {
        *out = MissionObjectiveKind::Interact;
        return true;
    }
    if (value == "kill") {
        *out = MissionObjectiveKind::Kill;
        return true;
    }
    if (value == "loot") {
        *out = MissionObjectiveKind::Loot;
        return true;
    }
    if (value == "poi") {
        *out = MissionObjectiveKind::Poi;
        return true;
    }
    if (value == "reach") {
        *out = MissionObjectiveKind::Reach;
        return true;
    }
    return false;
}

bool ValidSkill(std::string_view value) {
    return value == "melee" || value == "building" || value == "crafting" ||
           value == "survival";
}

bool ParseObjective(const nlohmann::json& node, MissionObjectiveDef* out) {
    if (!node.is_object()) {
        return false;
    }
    std::string kindName;
    if (!ReadString(node, "kind", 32, &kindName) ||
        !ParseObjectiveKind(kindName, &out->kind)) {
        return false;
    }
    if (!ReadString(node, "description", kMaxMissionTextBytes,
                    &out->description)) {
        return false;
    }
    if (node.contains("count")) {
        if (!ReadInt(node, "count", 1, 100000, &out->count)) {
            return false;
        }
    }
    const bool needsTarget = out->kind == MissionObjectiveKind::CollectItem ||
                             out->kind == MissionObjectiveKind::Interact ||
                             out->kind == MissionObjectiveKind::Poi;
    if (node.contains("target")) {
        if (!ReadString(node, "target", kMaxMissionIdBytes, &out->target) ||
            (!ValidToken(out->target) && out->target != "any")) {
            return false;
        }
    } else if (needsTarget) {
        return false;
    }
    if (out->kind == MissionObjectiveKind::Loot && out->target.empty()) {
        out->target = "any";
    }
    if (out->kind == MissionObjectiveKind::Kill && out->target.empty()) {
        out->target = "any";
    }
    if (out->kind == MissionObjectiveKind::Reach) {
        if (!ReadFloat(node, "x", -1000000.0f, 1000000.0f, &out->x) ||
            !ReadFloat(node, "y", -1000000.0f, 1000000.0f, &out->y)) {
            return false;
        }
        if (node.contains("radius") &&
            !ReadFloat(node, "radius", 0.25f, 64.0f, &out->radius)) {
            return false;
        }
    }
    return true;
}

bool ParseReward(const nlohmann::json& node, MissionRewardDef* out) {
    if (!node.is_object() || !ReadString(node, "label", kMaxMissionTextBytes,
                                         &out->label)) {
        return false;
    }
    if (node.contains("xp") &&
        !ReadFloat(node, "xp", 0.0f, 1000000.0f, &out->xp)) {
        return false;
    }
    if (node.contains("skill")) {
        if (!ReadString(node, "skill", 32, &out->skill) ||
            !ValidSkill(out->skill)) {
            return false;
        }
    }
    if (node.contains("item")) {
        if (!ReadString(node, "item", kMaxMissionIdBytes, &out->item) ||
            !ValidToken(out->item)) {
            return false;
        }
        if (node.contains("itemCount") &&
            !ReadInt(node, "itemCount", 1, 9999, &out->itemCount)) {
            return false;
        }
    }
    if (node.contains("extraItem")) {
        if (!ReadString(node, "extraItem", kMaxMissionIdBytes,
                        &out->extraItem) ||
            !ValidToken(out->extraItem)) {
            return false;
        }
        if (node.contains("extraItemCount") &&
            !ReadInt(node, "extraItemCount", 1, 9999, &out->extraItemCount)) {
            return false;
        }
    }
    if (node.contains("blueprint")) {
        if (!ReadString(node, "blueprint", kMaxMissionIdBytes,
                        &out->blueprint) ||
            !ValidToken(out->blueprint)) {
            return false;
        }
    }
    return !out->label.empty();
}

bool PrerequisiteCycle(const std::vector<MissionDef>& defs,
                       const MissionDef& def) {
    std::unordered_set<std::string> seen;
    std::string current = def.prerequisite;
    while (!current.empty()) {
        if (!seen.insert(current).second || current == def.id) {
            return true;
        }
        const auto it = std::find_if(
            defs.begin(), defs.end(),
            [&](const MissionDef& candidate) { return candidate.id == current; });
        if (it == defs.end()) {
            return false;
        }
        current = it->prerequisite;
    }
    return false;
}

std::uint32_t MixHash(std::uint32_t hash, std::uint32_t value) {
    hash ^= value;
    hash *= 16777619u;
    return hash;
}

}  // namespace

bool LoadMissionConfig(const core::Config& config, MissionConfig* out,
                       std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "任务配置输出为空";
        }
        return false;
    }
    MissionConfig result;
    const nlohmann::json& root = config.Raw();
    if (!root.contains("missions")) {
        *out = std::move(result);
        return true;
    }
    const nlohmann::json& node = root["missions"];
    if (!node.is_object()) {
        if (error != nullptr) {
            *error = "missions 必须是对象";
        }
        return false;
    }
    result.enabled = node.value("enabled", false);
    if (!result.enabled) {
        *out = std::move(result);
        return true;
    }
    if (node.contains("randomSeed")) {
        if (!node["randomSeed"].is_number_unsigned() &&
            !node["randomSeed"].is_number_integer()) {
            if (error != nullptr) {
                *error = "missions.randomSeed 必须是整数";
            }
            return false;
        }
        const auto seed = node["randomSeed"].get<std::int64_t>();
        if (seed < 0 || seed > std::numeric_limits<std::uint32_t>::max()) {
            if (error != nullptr) {
                *error = "missions.randomSeed 超出范围";
            }
            return false;
        }
        result.randomSeed = static_cast<std::uint32_t>(seed);
    }

    if (!node.contains("defs") || !node["defs"].is_array()) {
        if (error != nullptr) {
            *error = "missions.defs 必须是数组";
        }
        return false;
    }
    std::unordered_set<std::string> ids;
    for (const nlohmann::json& entry : node["defs"]) {
        if (result.defs.size() >= kMaxMissionDefs) {
            ZL_WARN(kLogModule, "任务定义超过上限 {}，忽略其余条目",
                    kMaxMissionDefs);
            break;
        }
        if (!entry.is_object()) {
            ZL_WARN(kLogModule, "任务条目不是对象，已跳过");
            continue;
        }
        MissionDef def;
        if (!ReadString(entry, "id", kMaxMissionIdBytes, &def.id) ||
            !ValidToken(def.id)) {
            ZL_WARN(kLogModule, "任务 id 非法，已跳过");
            continue;
        }
        if (ids.contains(def.id)) {
            ZL_WARN(kLogModule, "任务 id 重复 '{}'，已跳过", def.id);
            continue;
        }
        if (!ReadString(entry, "name", kMaxMissionTextBytes, &def.name)) {
            ZL_WARN(kLogModule, "任务 '{}' 缺少 name，已跳过", def.id);
            continue;
        }
        if (entry.contains("chain")) {
            if (!ReadString(entry, "chain", kMaxMissionIdBytes, &def.chain) ||
                !ValidToken(def.chain)) {
                ZL_WARN(kLogModule, "任务 '{}' chain 非法，已跳过", def.id);
                continue;
            }
        } else {
            def.chain = def.id;
        }
        if (entry.contains("order") &&
            !ReadInt(entry, "order", 1, 1000, &def.order)) {
            ZL_WARN(kLogModule, "任务 '{}' order 非法，已跳过", def.id);
            continue;
        }
        std::string kindName;
        if (!ReadString(entry, "kind", 32, &kindName) ||
            !ParseKind(kindName, &def.kind)) {
            ZL_WARN(kLogModule, "任务 '{}' kind 非法，已跳过", def.id);
            continue;
        }
        std::string sourceName;
        if (!ReadString(entry, "source", 32, &sourceName) ||
            !ParseSource(sourceName, &def.source)) {
            ZL_WARN(kLogModule, "任务 '{}' source 非法，已跳过", def.id);
            continue;
        }
        if (entry.contains("task")) {
            std::string taskName;
            if (!ReadString(entry, "task", 32, &taskName) ||
                !ParseTaskKind(taskName, &def.task)) {
                ZL_WARN(kLogModule, "任务 '{}' task 非法，已跳过", def.id);
                continue;
            }
        }
        if (entry.contains("autoActivate")) {
            if (!entry["autoActivate"].is_boolean()) {
                ZL_WARN(kLogModule, "任务 '{}' autoActivate 非法，已跳过",
                        def.id);
                continue;
            }
            def.autoActivate = entry["autoActivate"].get<bool>();
        }
        if (entry.contains("timeLimitDays") &&
            !ReadInt(entry, "timeLimitDays", 0, 365, &def.timeLimitDays)) {
            ZL_WARN(kLogModule, "任务 '{}' timeLimitDays 非法，已跳过", def.id);
            continue;
        }
        if (entry.contains("retryDays") &&
            !ReadInt(entry, "retryDays", 1, 30, &def.retryDays)) {
            ZL_WARN(kLogModule, "任务 '{}' retryDays 非法，已跳过", def.id);
            continue;
        }
        if (entry.contains("prerequisite")) {
            if (!entry["prerequisite"].is_string()) {
                ZL_WARN(kLogModule, "任务 '{}' prerequisite 非法，已跳过", def.id);
                continue;
            }
            def.prerequisite = entry["prerequisite"].get<std::string>();
            if (!def.prerequisite.empty() &&
                (def.prerequisite.size() > kMaxMissionIdBytes ||
                 !ValidToken(def.prerequisite))) {
                ZL_WARN(kLogModule, "任务 '{}' prerequisite 非法，已跳过", def.id);
                continue;
            }
        }
        if (entry.contains("minDay") &&
            !ReadInt(entry, "minDay", 1, 100000, &def.minDay)) {
            ZL_WARN(kLogModule, "任务 '{}' minDay 非法，已跳过", def.id);
            continue;
        }
        if (entry.contains("minSettlementReputation") &&
            !ReadInt(entry, "minSettlementReputation",
                     kSettlementReputationMinScore,
                     kSettlementReputationMaxScore,
                     &def.minSettlementReputation)) {
            ZL_WARN(kLogModule,
                    "任务 '{}' minSettlementReputation 非法，已跳过", def.id);
            continue;
        }
        if (entry.contains("chance") &&
            !ReadFloat(entry, "chance", 0.0f, 1.0f, &def.chance)) {
            ZL_WARN(kLogModule, "任务 '{}' chance 非法，已跳过", def.id);
            continue;
        }

        const auto objectivesIt = entry.find("objectives");
        if (objectivesIt == entry.end() || !objectivesIt->is_array() ||
            objectivesIt->empty()) {
            ZL_WARN(kLogModule, "任务 '{}' 缺少 objectives，已跳过", def.id);
            continue;
        }
        for (const nlohmann::json& objectiveNode : *objectivesIt) {
            if (def.objectives.size() >= kMaxMissionObjectives) {
                break;
            }
            MissionObjectiveDef objective;
            if (!ParseObjective(objectiveNode, &objective)) {
                ZL_WARN(kLogModule, "任务 '{}' 的目标条目非法，已跳过任务", def.id);
                def.objectives.clear();
                break;
            }
            def.objectives.push_back(std::move(objective));
        }
        if (def.objectives.empty()) {
            continue;
        }
        const auto rewardIt = entry.find("reward");
        if (rewardIt == entry.end() ||
            !ParseReward(*rewardIt, &def.reward)) {
            ZL_WARN(kLogModule, "任务 '{}' reward 非法，已跳过", def.id);
            continue;
        }
        ids.insert(def.id);
        result.defs.push_back(std::move(def));
    }

    std::vector<MissionDef> valid;
    valid.reserve(result.defs.size());
    for (MissionDef& def : result.defs) {
        if (!def.prerequisite.empty() &&
            ids.find(def.prerequisite) == ids.end()) {
            ZL_WARN(kLogModule, "任务 '{}' 的前置 '{}' 不存在，已跳过",
                    def.id, def.prerequisite);
            continue;
        }
        if (!def.prerequisite.empty() && PrerequisiteCycle(result.defs, def)) {
            ZL_WARN(kLogModule, "任务 '{}' 的前置链存在环，已跳过", def.id);
            continue;
        }
        valid.push_back(std::move(def));
    }
    result.defs = std::move(valid);
    if (result.defs.empty()) {
        ZL_WARN(kLogModule, "missions.enabled=true 但没有有效任务定义");
    }
    *out = std::move(result);
    return true;
}

bool MergeMissionFiles(const std::vector<std::string>& files,
                       const core::Config& config, MissionConfig* out,
                       MissionMergeStats* stats, std::string* error,
                       std::string* acceptedJson) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "任务配置输出为空";
        }
        return false;
    }
    if (!out->enabled || files.empty()) {
        return true;
    }
    const nlohmann::json& root = config.Raw();
    if (!root.contains("missions") || !root["missions"].is_object()) {
        if (error != nullptr) {
            *error = "基础 missions 配置不存在";
        }
        return false;
    }
    nlohmann::json accepted = root["missions"];
    if (!accepted.contains("defs") || !accepted["defs"].is_array()) {
        if (error != nullptr) {
            *error = "基础 missions.defs 必须是数组";
        }
        return false;
    }
    accepted["enabled"] = true;

    MissionMergeStats mergedStats;
    for (const std::string& file : files) {
        std::ifstream input(file, std::ios::binary);
        if (!input) {
            ZL_WARN(kLogModule, "跳过 mod 任务文件 {}：无法读取", file);
            continue;
        }
        nlohmann::json doc;
        try {
            input >> doc;
        } catch (const std::exception& e) {
            ZL_WARN(kLogModule, "跳过 mod 任务文件 {}：JSON 解析失败：{}", file,
                    e.what());
            continue;
        }
        if (!doc.is_object() || !doc.contains("format") ||
            !doc["format"].is_string() ||
            doc["format"].get<std::string>() != "missions-v1") {
            ZL_WARN(kLogModule, "跳过 mod 任务文件 {}：format 必须是 missions-v1",
                    file);
            continue;
        }
        if (!doc.contains("defs") || !doc["defs"].is_array() ||
            doc["defs"].empty()) {
            ZL_WARN(kLogModule, "跳过 mod 任务文件 {}：defs 必须是非空数组",
                    file);
            continue;
        }

        nlohmann::json candidate = accepted;
        for (const nlohmann::json& def : doc["defs"]) {
            candidate["defs"].push_back(def);
        }
        core::Config candidateConfig;
        candidateConfig.Set("missions", candidate);
        MissionConfig parsed;
        std::string parseError;
        const std::size_t expected =
            out->defs.size() + doc["defs"].size();
        if (!LoadMissionConfig(candidateConfig, &parsed, &parseError) ||
            parsed.defs.size() != expected) {
            ZL_WARN(kLogModule,
                    "跳过 mod 任务文件 {}：任务定义无效/重复或前置不存在", file);
            continue;
        }
        accepted = std::move(candidate);
        *out = std::move(parsed);
        ++mergedStats.filesLoaded;
        mergedStats.defsAdded += static_cast<int>(doc["defs"].size());
    }
    if (stats != nullptr) {
        *stats = mergedStats;
    }
    if (acceptedJson != nullptr) {
        *acceptedJson = accepted.dump();
    }
    return true;
}

const char* MissionKindName(MissionKind kind) {
    switch (kind) {
        case MissionKind::Daily:
            return "daily";
        case MissionKind::Random:
            return "random";
        case MissionKind::Once:
        default:
            return "once";
    }
}

const char* MissionSourceName(MissionSource source) {
    switch (source) {
        case MissionSource::Note:
            return "note";
        case MissionSource::Npc:
            return "npc";
        case MissionSource::Base:
            return "base";
        case MissionSource::Radio:
        default:
            return "radio";
    }
}

const char* MissionSourceLabel(MissionSource source) {
    switch (source) {
        case MissionSource::Note:
            return "纸条";
        case MissionSource::Npc:
            return "幸存者";
        case MissionSource::Base:
            return "基地";
        case MissionSource::Radio:
        default:
            return "电台";
    }
}

const char* MissionTaskKindName(MissionTaskKind kind) {
    switch (kind) {
        case MissionTaskKind::Help:
            return "help";
        case MissionTaskKind::Escort:
            return "escort";
        case MissionTaskKind::Clear:
            return "clear";
        case MissionTaskKind::Supply:
            return "supply";
        case MissionTaskKind::Generic:
        default:
            return "generic";
    }
}

const char* MissionTaskKindLabel(MissionTaskKind kind) {
    switch (kind) {
        case MissionTaskKind::Help:
            return "求助";
        case MissionTaskKind::Escort:
            return "护送";
        case MissionTaskKind::Clear:
            return "清剿";
        case MissionTaskKind::Supply:
            return "物资";
        case MissionTaskKind::Generic:
        default:
            return "委托";
    }
}

const char* MissionObjectiveKindName(MissionObjectiveKind kind) {
    switch (kind) {
        case MissionObjectiveKind::Interact:
            return "interact";
        case MissionObjectiveKind::Kill:
            return "kill";
        case MissionObjectiveKind::Loot:
            return "loot";
        case MissionObjectiveKind::Poi:
            return "poi";
        case MissionObjectiveKind::Reach:
            return "reach";
        case MissionObjectiveKind::CollectItem:
        default:
            return "collect";
    }
}

const char* MissionRunStatusName(ecs::MissionRunStatus status) {
    switch (status) {
        case ecs::MissionRunStatus::Completed:
            return "completed";
        case ecs::MissionRunStatus::Failed:
            return "failed";
        case ecs::MissionRunStatus::Active:
        default:
            return "active";
    }
}

const ecs::MissionRunState* FindMissionState(const ecs::MissionLog& log,
                                             std::string_view id) {
    for (const ecs::MissionRunState& state : log.missions) {
        if (state.id == id) {
            return &state;
        }
    }
    return nullptr;
}

ecs::MissionRunState* FindMissionState(ecs::MissionLog* log,
                                       std::string_view id) {
    if (log == nullptr) {
        return nullptr;
    }
    for (ecs::MissionRunState& state : log->missions) {
        if (state.id == id) {
            return &state;
        }
    }
    return nullptr;
}

int MissionObjectiveProgress(const MissionObjectiveDef& objective,
                             const ecs::MissionRunState* state,
                             std::size_t index) {
    (void)objective;
    if (state == nullptr || index >= state->progress.size()) {
        return 0;
    }
    return std::max(0, state->progress[index]);
}

bool IsMissionObjectiveComplete(const MissionObjectiveDef& objective,
                                const ecs::MissionRunState* state,
                                std::size_t index) {
    return MissionObjectiveProgress(objective, state, index) >= objective.count;
}

bool IsMissionComplete(const MissionDef& def,
                       const ecs::MissionRunState& state) {
    if (def.objectives.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < def.objectives.size(); ++i) {
        if (!IsMissionObjectiveComplete(def.objectives[i], &state, i)) {
            return false;
        }
    }
    return true;
}

std::string MissionObjectiveLine(const MissionObjectiveDef& objective,
                                 const ecs::MissionRunState* state,
                                 std::size_t index) {
    const int progress = MissionObjectiveProgress(objective, state, index);
    std::ostringstream out;
    out << objective.description << ' ' << std::min(progress, objective.count)
        << '/' << objective.count;
    return out.str();
}

std::string MissionGuideText(const MissionDef& def,
                             const ecs::MissionRunState& state,
                             std::string_view directionHint) {
    if (def.id.empty()) {
        return {};
    }
    std::ostringstream out;
    out << "任务[" << MissionSourceLabel(def.source) << "]";
    if (def.task != MissionTaskKind::Generic) {
        out << "[" << MissionTaskKindLabel(def.task) << "]";
    }
    out << " " << def.name;
    if (def.kind == MissionKind::Daily) {
        out << " · 每日";
    } else if (def.kind == MissionKind::Random) {
        out << " · 随机事件";
    }
    out << " · ";
    for (std::size_t i = 0; i < def.objectives.size(); ++i) {
        if (i > 0) {
            out << "；";
        }
        out << MissionObjectiveLine(def.objectives[i], &state, i);
    }
    if (!directionHint.empty()) {
        out << " · " << directionHint;
    }
    if (def.timeLimitDays > 0) {
        out << " · 限" << def.timeLimitDays << "天";
    }
    if (!def.reward.label.empty()) {
        out << " · 奖励 " << def.reward.label;
    }
    out << " · L切换 K放弃";
    return out.str();
}

const char* MissionGuideTone(const MissionDef& def) {
    return def.kind == MissionKind::Random ? "warn" : "info";
}

std::string MissionEventGuideText(const MissionDef& def,
                                  const ecs::MissionRunState& state,
                                  std::string_view directionHint) {
    if (def.id.empty()) {
        return {};
    }
    std::ostringstream out;
    out << "事件 · " << def.name << " · ";
    for (std::size_t i = 0; i < def.objectives.size(); ++i) {
        if (i > 0) {
            out << "；";
        }
        out << MissionObjectiveLine(def.objectives[i], &state, i);
    }
    if (!directionHint.empty()) {
        out << " · " << directionHint;
    }
    if (!def.reward.label.empty()) {
        out << " · 奖励 " << def.reward.label;
    }
    return out.str();
}

int MissionFocusPriority(const MissionDef& def) {
    // M31-T10: the authored vertical-slice chain is the main line. It must
    // outrank nearby mod/side missions so HUD/map guidance stays on the
    // intended route; random events still use the separate event guide.
    if (def.chain == "m31_slice") {
        return -1;
    }
    if (def.kind == MissionKind::Random) {
        return 0;
    }
    if (def.kind == MissionKind::Once) {
        return 1;
    }
    return 2;
}

bool MissionFocusLess(const MissionFocusCandidate& a,
                      const MissionFocusCandidate& b) {
    if (a.def == nullptr || b.def == nullptr) {
        return a.def != nullptr;
    }
    const int priorityA = MissionFocusPriority(*a.def);
    const int priorityB = MissionFocusPriority(*b.def);
    if (priorityA != priorityB) {
        return priorityA < priorityB;
    }
    if (a.distance != b.distance) {
        return a.distance < b.distance;
    }
    if (a.def->order != b.def->order) {
        return a.def->order < b.def->order;
    }
    return a.def->id < b.def->id;
}

bool MissionRandomRoll(std::uint32_t seed, int day, std::string_view id,
                       float chance) {
    if (chance <= 0.0f) {
        return false;
    }
    if (chance >= 1.0f) {
        return true;
    }
    std::uint32_t hash = 2166136261u;
    hash = MixHash(hash, seed);
    hash = MixHash(hash, static_cast<std::uint32_t>(std::max(0, day)));
    for (const char c : id) {
        hash = MixHash(hash, static_cast<std::uint8_t>(c));
    }
    constexpr std::uint32_t kBasis = 10000u;
    return (hash % kBasis) < static_cast<std::uint32_t>(
                                 std::round(chance * static_cast<float>(kBasis)));
}

bool MissionHasNextStep(const MissionConfig& config, const MissionDef& def) {
    return std::any_of(config.defs.begin(), config.defs.end(),
                       [&](const MissionDef& candidate) {
                           return candidate.chain == def.chain &&
                                  candidate.order > def.order;
                       });
}

bool MissionCanActivate(const MissionDef& def, const ecs::MissionLog& log,
                        int day) {
    if (def.kind != MissionKind::Once || day < def.minDay) {
        return false;
    }
    if (!def.prerequisite.empty()) {
        const ecs::MissionRunState* prerequisite =
            FindMissionState(log, def.prerequisite);
        if (prerequisite == nullptr ||
            prerequisite->status != ecs::MissionRunStatus::Completed) {
            return false;
        }
    }
    const ecs::MissionRunState* state = FindMissionState(log, def.id);
    if (state == nullptr) {
        return true;
    }
    if (state->status == ecs::MissionRunStatus::Completed) {
        return false;
    }
    if (state->status == ecs::MissionRunStatus::Active) {
        return false;
    }
    return day >= state->day + std::max(1, def.retryDays);
}

bool MissionMeetsSettlementReputation(const MissionDef& def, int score) {
    return std::clamp(score, kSettlementReputationMinScore,
                      kSettlementReputationMaxScore) >=
           std::clamp(def.minSettlementReputation,
                      kSettlementReputationMinScore,
                      kSettlementReputationMaxScore);
}

int MissionDeadlineDay(const MissionDef& def,
                       const ecs::MissionRunState& state) {
    if (def.timeLimitDays <= 0) {
        return 0;
    }
    return state.day + std::max(1, def.timeLimitDays);
}

bool MissionIsExpired(const MissionDef& def, const ecs::MissionRunState& state,
                      int day) {
    if (state.status != ecs::MissionRunStatus::Active) {
        return false;
    }
    const int deadline = MissionDeadlineDay(def, state);
    return deadline > 0 && day >= deadline;
}

bool AbandonMission(ecs::MissionLog* log, std::string_view id, int day) {
    ecs::MissionRunState* state = FindMissionState(log, id);
    if (state == nullptr || state->status != ecs::MissionRunStatus::Active) {
        return false;
    }
    state->status = ecs::MissionRunStatus::Failed;
    state->rewardClaimed = false;
    if (day > 0) {
        state->day = day;
    }
    return true;
}

}  // namespace zl::game
