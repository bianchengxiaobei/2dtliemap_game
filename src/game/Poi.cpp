#include "game/Poi.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "core/Config.h"
#include "core/Log.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "poi";
constexpr std::size_t kMaxPoiIdBytes = 64;
constexpr std::size_t kMaxPoiContainers = 16;

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

        const auto taskIt = entry.find("task");
        if (taskIt == entry.end() || !taskIt->is_object()) {
            ZL_WARN(kLogModule, "POI '{}' 缺少 task，已跳过", def.id);
            continue;
        }
        PoiTaskDef task;
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
            ZL_WARN(kLogModule, "POI '{}' task 目标表不在容器列表中，已跳过", def.id);
            continue;
        }
        if (taskIt->contains("rewardXp") &&
            !ReadFloat(*taskIt, "rewardXp", 0.0f, 100000.0f, &task.rewardXp)) {
            ZL_WARN(kLogModule, "POI '{}' task.rewardXp 非法，使用 0", def.id);
            task.rewardXp = 0.0f;
        }
        if (taskIt->contains("rewardItem")) {
            if (!ReadString(*taskIt, "rewardItem", &task.rewardItem)) {
                ZL_WARN(kLogModule, "POI '{}' task.rewardItem 非法，忽略奖励物品", def.id);
            } else {
                int rewardCount = task.rewardCount;
                if (!ReadInt(*taskIt, "rewardCount", 1, 9999, &rewardCount)) {
                    ZL_WARN(kLogModule, "POI '{}' task.rewardCount 非法，使用 1", def.id);
                    rewardCount = 1;
                }
                task.rewardCount = rewardCount;
            }
        }
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

}  // namespace zl::game
