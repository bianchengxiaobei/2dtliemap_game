#include "game/Settlement.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Config.h"
#include "core/Log.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "settlement";
constexpr std::size_t kMaxSettlementIdBytes = 64;
constexpr std::size_t kMaxSettlementNameBytes = 128;
constexpr std::size_t kMaxMissionIdBytes = 64;

bool ValidSettlementId(std::string_view id) {
    if (id.empty() || id.size() > kMaxSettlementIdBytes) {
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

bool ReadBool(const nlohmann::json& node, const char* key, bool* out) {
    if (!node.contains(key) || !node[key].is_boolean()) {
        return false;
    }
    *out = node[key].get<bool>();
    return true;
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

bool ParseDefense(const nlohmann::json& entry, SettlementDefenseConfig* out,
                  std::string* error) {
    if (!entry.is_object()) {
        if (error != nullptr) {
            *error = "defense 必须是对象";
        }
        return false;
    }
    SettlementDefenseConfig defense;
    defense.enabled = true;
    if (entry.contains("enabled") && !ReadBool(entry, "enabled", &defense.enabled)) {
        if (error != nullptr) {
            *error = "defense.enabled 必须是布尔值";
        }
        return false;
    }
    if (entry.contains("minDay") &&
        !ReadInt(entry, "minDay", 1, 10000, &defense.minDay)) {
        if (error != nullptr) {
            *error = "defense.minDay 必须是 1~10000";
        }
        return false;
    }
    if (entry.contains("nightStartHour") &&
        !ReadInt(entry, "nightStartHour", 0, 23, &defense.nightStartHour)) {
        if (error != nullptr) {
            *error = "defense.nightStartHour 必须是 0~23";
        }
        return false;
    }
    if (entry.contains("nightEndHour") &&
        !ReadInt(entry, "nightEndHour", 0, 23, &defense.nightEndHour)) {
        if (error != nullptr) {
            *error = "defense.nightEndHour 必须是 0~23";
        }
        return false;
    }
    if (defense.nightStartHour == defense.nightEndHour) {
        if (error != nullptr) {
            *error = "defense 夜袭起止小时不能相同";
        }
        return false;
    }
    if (entry.contains("activationRadius") &&
        !ReadFloat(entry, "activationRadius", 0.0f, 128.0f,
                   &defense.activationRadius)) {
        if (error != nullptr) {
            *error = "defense.activationRadius 非法";
        }
        return false;
    }
    if (entry.contains("spawnRadius") &&
        !ReadFloat(entry, "spawnRadius", 0.0f, 64.0f, &defense.spawnRadius)) {
        if (error != nullptr) {
            *error = "defense.spawnRadius 非法";
        }
        return false;
    }
    if (entry.contains("baseRaiders") &&
        !ReadInt(entry, "baseRaiders", 0, 12, &defense.baseRaiders)) {
        if (error != nullptr) {
            *error = "defense.baseRaiders 必须是 0~12";
        }
        return false;
    }
    if (entry.contains("raidersPerNight") &&
        !ReadInt(entry, "raidersPerNight", 0, 12, &defense.raidersPerNight)) {
        if (error != nullptr) {
            *error = "defense.raidersPerNight 必须是 0~12";
        }
        return false;
    }
    if (entry.contains("maxRaiders") &&
        !ReadInt(entry, "maxRaiders", 0, 24, &defense.maxRaiders)) {
        if (error != nullptr) {
            *error = "defense.maxRaiders 必须是 0~24";
        }
        return false;
    }
    if (defense.maxRaiders < defense.baseRaiders) {
        if (error != nullptr) {
            *error = "defense.maxRaiders 不能小于 baseRaiders";
        }
        return false;
    }
    if (entry.contains("guardBehavior")) {
        std::string behavior;
        if (!ReadString(entry, "guardBehavior", 32, &behavior) ||
            !ParseSettlementGuardBehavior(behavior, &defense.guardBehavior)) {
            if (error != nullptr) {
                *error = "defense.guardBehavior 必须是 defend/retreat";
            }
            return false;
        }
    }
    if (entry.contains("guardHealth") &&
        !ReadFloat(entry, "guardHealth", 1.0f, 1000.0f,
                   &defense.guardHealth)) {
        if (error != nullptr) {
            *error = "defense.guardHealth 非法";
        }
        return false;
    }
    if (entry.contains("guardDamage") &&
        !ReadFloat(entry, "guardDamage", 0.0f, 100.0f,
                   &defense.guardDamage)) {
        if (error != nullptr) {
            *error = "defense.guardDamage 非法";
        }
        return false;
    }
    if (entry.contains("guardRange") &&
        !ReadFloat(entry, "guardRange", 0.5f, 8.0f, &defense.guardRange)) {
        if (error != nullptr) {
            *error = "defense.guardRange 非法";
        }
        return false;
    }
    if (entry.contains("guardAttackSeconds") &&
        !ReadFloat(entry, "guardAttackSeconds", 0.2f, 10.0f,
                   &defense.guardAttackSeconds)) {
        if (error != nullptr) {
            *error = "defense.guardAttackSeconds 非法";
        }
        return false;
    }
    if (entry.contains("raiderHealth") &&
        !ReadFloat(entry, "raiderHealth", 1.0f, 1000.0f,
                   &defense.raiderHealth)) {
        if (error != nullptr) {
            *error = "defense.raiderHealth 非法";
        }
        return false;
    }
    if (entry.contains("raiderDamage") &&
        !ReadFloat(entry, "raiderDamage", 0.0f, 100.0f,
                   &defense.raiderDamage)) {
        if (error != nullptr) {
            *error = "defense.raiderDamage 非法";
        }
        return false;
    }
    if (entry.contains("raiderRange") &&
        !ReadFloat(entry, "raiderRange", 0.5f, 8.0f, &defense.raiderRange)) {
        if (error != nullptr) {
            *error = "defense.raiderRange 非法";
        }
        return false;
    }
    if (entry.contains("raiderAttackSeconds") &&
        !ReadFloat(entry, "raiderAttackSeconds", 0.2f, 10.0f,
                   &defense.raiderAttackSeconds)) {
        if (error != nullptr) {
            *error = "defense.raiderAttackSeconds 非法";
        }
        return false;
    }
    if (entry.contains("raiderSpeed") &&
        !ReadFloat(entry, "raiderSpeed", 0.5f, 8.0f, &defense.raiderSpeed)) {
        if (error != nullptr) {
            *error = "defense.raiderSpeed 非法";
        }
        return false;
    }
    *out = defense;
    return true;
}

bool ParseNpcPoint(const nlohmann::json& entry, SettlementNpcPoint* out,
                   std::string* error) {
    if (!entry.is_object()) {
        if (error != nullptr) {
            *error = "npcPoints 条目不是对象";
        }
        return false;
    }
    SettlementNpcPoint point;
    if (!ReadString(entry, "id", kMaxSettlementIdBytes, &point.id) ||
        !ValidSettlementId(point.id)) {
        if (error != nullptr) {
            *error = "NPC id 非法";
        }
        return false;
    }
    std::string kind;
    if (!ReadString(entry, "kind", kMaxSettlementIdBytes, &kind) ||
        !ParseSettlementNpcKind(kind, &point.kind)) {
        if (error != nullptr) {
            *error = "NPC kind 非法";
        }
        return false;
    }
    if (!ReadFloat(entry, "x", -1000000.0f, 1000000.0f, &point.x) ||
        !ReadFloat(entry, "y", -1000000.0f, 1000000.0f, &point.y) ||
        !ReadFloat(entry, "wanderRadius", 0.0f, 16.0f,
                   &point.wanderRadius)) {
        if (error != nullptr) {
            *error = "NPC x/y/wanderRadius 非法";
        }
        return false;
    }
    *out = std::move(point);
    return true;
}

bool ParseSettlementDef(const nlohmann::json& entry, SettlementDef* out,
                        std::string* error) {
    if (!entry.is_object()) {
        if (error != nullptr) {
            *error = "聚落条目不是对象";
        }
        return false;
    }
    SettlementDef def;
    if (!ReadString(entry, "id", kMaxSettlementIdBytes, &def.id) ||
        !ValidSettlementId(def.id)) {
        if (error != nullptr) {
            *error = "聚落 id 非法";
        }
        return false;
    }
    if (!ReadString(entry, "name", kMaxSettlementNameBytes, &def.name)) {
        if (error != nullptr) {
            *error = "聚落 name 非法";
        }
        return false;
    }
    if (!ReadFloat(entry, "centerX", -1000000.0f, 1000000.0f,
                   &def.centerX) ||
        !ReadFloat(entry, "centerY", -1000000.0f, 1000000.0f,
                   &def.centerY) ||
        !ReadFloat(entry, "safeRadius", 1.0f, 128.0f, &def.safeRadius)) {
        if (error != nullptr) {
            *error = "聚落 centerX/centerY/safeRadius 非法";
        }
        return false;
    }

    const auto pointsIt = entry.find("npcPoints");
    if (pointsIt == entry.end() || !pointsIt->is_array() ||
        pointsIt->empty() || pointsIt->size() > kMaxSettlementNpcPoints) {
        if (error != nullptr) {
            *error = "npcPoints 必须是非空数组且不超过 16 项";
        }
        return false;
    }
    std::unordered_set<std::string> npcIds;
    bool hasMerchant = false;
    bool hasGuard = false;
    bool hasRequester = false;
    for (const nlohmann::json& pointNode : *pointsIt) {
        SettlementNpcPoint point;
        std::string pointError;
        if (!ParseNpcPoint(pointNode, &point, &pointError)) {
            if (error != nullptr) {
                *error = "聚落 '" + def.id + "' " + pointError;
            }
            return false;
        }
        if (!npcIds.insert(point.id).second) {
            if (error != nullptr) {
                *error = "聚落 '" + def.id + "' NPC id 重复";
            }
            return false;
        }
        if (!IsInsideSettlement(def, point.x, point.y)) {
            if (error != nullptr) {
                *error = "聚落 '" + def.id + "' NPC 不在安全区内";
            }
            return false;
        }
        switch (point.kind) {
            case SettlementNpcKind::Merchant:
                hasMerchant = true;
                break;
            case SettlementNpcKind::Guard:
                hasGuard = true;
                break;
            case SettlementNpcKind::Requester:
                hasRequester = true;
                break;
        }
        def.npcPoints.push_back(std::move(point));
    }
    if (!hasMerchant || !hasGuard || !hasRequester) {
        if (error != nullptr) {
            *error = "聚落 '" + def.id + "' 必须包含 merchant/guard/requester";
        }
        return false;
    }

    const auto stockIt = entry.find("merchantStock");
    if (stockIt == entry.end() || !stockIt->is_array() ||
        stockIt->size() != kSettlementMerchantStockCount) {
        if (error != nullptr) {
            *error = "merchantStock 必须是 5 项数组";
        }
        return false;
    }
    for (std::size_t i = 0; i < kSettlementMerchantStockCount; ++i) {
        if (!(*stockIt)[i].is_number_integer()) {
            if (error != nullptr) {
                *error = "merchantStock 条目必须是整数";
            }
            return false;
        }
        const int value = (*stockIt)[i].get<int>();
        if (value < 0 || value > 99) {
            if (error != nullptr) {
                *error = "merchantStock 条目必须在 0~99";
            }
            return false;
        }
        def.merchantStock[i] = value;
    }
    if (!ReadInt(entry, "restockDays", 1, 30, &def.restockDays)) {
        if (error != nullptr) {
            *error = "restockDays 必须是 1~30 的整数";
        }
        return false;
    }

    const auto poolIt = entry.find("missionPool");
    if (poolIt == entry.end() || !poolIt->is_array() || poolIt->empty() ||
        poolIt->size() > kMaxSettlementMissionPool) {
        if (error != nullptr) {
            *error = "missionPool 必须是非空数组且不超过 32 项";
        }
        return false;
    }
    std::unordered_set<std::string> missionIds;
    for (const nlohmann::json& missionNode : *poolIt) {
        if (!missionNode.is_string()) {
            if (error != nullptr) {
                *error = "missionPool 条目必须是字符串";
            }
            return false;
        }
        std::string id = missionNode.get<std::string>();
        if (id.empty() || id.size() > kMaxMissionIdBytes ||
            !missionIds.insert(id).second) {
            if (error != nullptr) {
                *error = "missionPool 任务 id 非法或重复";
            }
            return false;
        }
        def.missionPool.push_back(std::move(id));
    }

    if (const auto defenseIt = entry.find("defense");
        defenseIt != entry.end()) {
        std::string defenseError;
        if (!ParseDefense(*defenseIt, &def.defense, &defenseError)) {
            if (error != nullptr) {
                *error = "聚落 '" + def.id + "' " + defenseError;
            }
            return false;
        }
    }

    *out = std::move(def);
    return true;
}

bool AppendSettlementDocument(const nlohmann::json& doc,
                              const std::string& source, SettlementConfig* out,
                              SettlementMergeStats* stats,
                              std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "聚落配置输出为空";
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
        doc["format"].get<std::string>() != "settlements-v1") {
        if (error != nullptr) {
            *error = source + ": format 必须是 settlements-v1";
        }
        return false;
    }
    if (!doc.contains("defs") || !doc["defs"].is_array() ||
        doc["defs"].empty()) {
        if (error != nullptr) {
            *error = source + ": defs 必须是非空数组";
        }
        return false;
    }

    int added = 0;
    int skipped = 0;
    std::string skippedReason;
    for (const nlohmann::json& entry : doc["defs"]) {
        SettlementDef def;
        std::string defError;
        if (!ParseSettlementDef(entry, &def, &defError)) {
            ZL_WARN(kLogModule, "{}: {}", source, defError);
            skippedReason = defError;
            ++skipped;
            continue;
        }
        if (out->Find(def.id) != nullptr) {
            ZL_WARN(kLogModule, "{}: 聚落 id 重复 '{}'，已跳过", source,
                    def.id);
            skippedReason = "聚落 id 重复 '" + def.id + "'";
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
    if (added == 0) {
        if (error != nullptr) {
            *error = source + ": " +
                     (skippedReason.empty() ? "没有有效聚落定义"
                                            : skippedReason);
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
            *error = "无法读取 " + path.generic_string();
        }
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    try {
        *out = nlohmann::json::parse(buffer.str());
    } catch (const std::exception& e) {
        if (error != nullptr) {
            *error = path.generic_string() + ": JSON 解析失败：" + e.what();
        }
        return false;
    }
    return true;
}

bool ParseJsonText(std::string_view json, const std::string& source,
                   nlohmann::json* out, std::string* error) {
    try {
        *out = nlohmann::json::parse(json);
    } catch (const std::exception& e) {
        if (error != nullptr) {
            *error = source + ": JSON 解析失败：" + e.what();
        }
        return false;
    }
    return true;
}

}  // namespace

bool LoadSettlementConfig(const core::Config& config, SettlementConfig* out,
                          std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "聚落配置输出为空";
        }
        return false;
    }
    SettlementConfig result;
    result.enabled = config.Get<bool>("settlements.enabled", false);
    if (!result.enabled) {
        *out = std::move(result);
        return true;
    }
    const std::string path = config.Get<std::string>(
        "settlements.file", "assets/settlements.json");
    nlohmann::json doc;
    if (!ReadJsonFile(path, &doc, error)) {
        return false;
    }
    if (!AppendSettlementDocument(doc, path, &result, nullptr, error)) {
        return false;
    }
    *out = std::move(result);
    return true;
}

bool MergeSettlementJson(std::string_view json, const std::string& source,
                         SettlementConfig* out, SettlementMergeStats* stats,
                         std::string* error) {
    nlohmann::json doc;
    if (!ParseJsonText(json, source, &doc, error)) {
        return false;
    }
    if (!AppendSettlementDocument(doc, source, out, stats, error)) {
        return false;
    }
    if (stats != nullptr) {
        ++stats->filesLoaded;
    }
    return true;
}

bool MergeSettlementFile(const std::filesystem::path& path,
                         SettlementConfig* out, SettlementMergeStats* stats,
                         std::string* error) {
    nlohmann::json doc;
    if (!ReadJsonFile(path, &doc, error)) {
        return false;
    }
    return MergeSettlementJson(doc.dump(), path.generic_string(), out, stats,
                               error);
}

const char* SettlementNpcKindName(SettlementNpcKind kind) {
    switch (kind) {
        case SettlementNpcKind::Merchant:
            return "merchant";
        case SettlementNpcKind::Guard:
            return "guard";
        case SettlementNpcKind::Requester:
            return "requester";
    }
    return "guard";
}

bool ParseSettlementNpcKind(std::string_view value, SettlementNpcKind* out) {
    if (out == nullptr) {
        return false;
    }
    if (value == "merchant") {
        *out = SettlementNpcKind::Merchant;
        return true;
    }
    if (value == "guard") {
        *out = SettlementNpcKind::Guard;
        return true;
    }
    if (value == "requester") {
        *out = SettlementNpcKind::Requester;
        return true;
    }
    return false;
}

const char* SettlementGuardBehaviorName(SettlementGuardBehavior behavior) {
    return behavior == SettlementGuardBehavior::Retreat ? "retreat" : "defend";
}

bool ParseSettlementGuardBehavior(std::string_view value,
                                  SettlementGuardBehavior* out) {
    if (out == nullptr) {
        return false;
    }
    if (value == "defend") {
        *out = SettlementGuardBehavior::Defend;
        return true;
    }
    if (value == "retreat") {
        *out = SettlementGuardBehavior::Retreat;
        return true;
    }
    return false;
}

const SettlementNpcPoint* FindSettlementNpc(const SettlementDef& def,
                                            SettlementNpcKind kind) {
    return def.FindNpc(kind);
}

const SettlementDef* PrimarySettlement(const SettlementConfig& config) {
    return config.enabled && !config.defs.empty() ? &config.defs.front()
                                                  : nullptr;
}

const SettlementDef* FindSettlementContaining(const SettlementConfig& config,
                                              float x, float y) {
    for (const SettlementDef& def : config.defs) {
        if (IsInsideSettlement(def, x, y)) {
            return &def;
        }
    }
    return nullptr;
}

bool IsInsideSettlement(const SettlementDef& def, float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y) || def.safeRadius <= 0.0f) {
        return false;
    }
    const float dx = x - def.centerX;
    const float dy = y - def.centerY;
    return dx * dx + dy * dy <= def.safeRadius * def.safeRadius;
}

}  // namespace zl::game
