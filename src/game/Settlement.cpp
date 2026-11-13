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

bool ReadOptionalString(const nlohmann::json& node, const char* key,
                        std::size_t maxBytes, std::string* out,
                        std::string* error) {
    if (!node.contains(key)) {
        return true;
    }
    if (!ReadString(node, key, maxBytes, out)) {
        if (error != nullptr) {
            *error = std::string(key) + " 非法";
        }
        return false;
    }
    return true;
}

bool ParseServices(const nlohmann::json& entry, SettlementServiceMask* out,
                   std::string* error) {
    if (!entry.contains("services")) {
        *out = kDefaultSettlementServices;
        return true;
    }
    const nlohmann::json& services = entry["services"];
    if (!services.is_array() || services.empty() ||
        services.size() > 4) {
        if (error != nullptr) {
            *error = "services 必须是 1~4 项数组";
        }
        return false;
    }
    SettlementServiceMask mask = 0;
    for (const nlohmann::json& serviceNode : services) {
        if (!serviceNode.is_string()) {
            if (error != nullptr) {
                *error = "services 条目必须是字符串";
            }
            return false;
        }
        SettlementService service = SettlementService::Trade;
        const std::string value = serviceNode.get<std::string>();
        if (!ParseSettlementService(value, &service)) {
            if (error != nullptr) {
                *error = "未知聚落服务 '" + value + "'";
            }
            return false;
        }
        const SettlementServiceMask bit =
            static_cast<SettlementServiceMask>(1u << static_cast<unsigned>(
                                                       service));
        if ((mask & bit) != 0) {
            if (error != nullptr) {
                *error = "services 条目重复";
            }
            return false;
        }
        mask = static_cast<SettlementServiceMask>(mask | bit);
    }
    *out = mask;
    return true;
}

bool ParseReputationTiers(const nlohmann::json& node,
                          SettlementReputationConfig* out,
                          std::string* error) {
    if (!node.is_array() ||
        node.size() != kSettlementReputationTierCount) {
        if (error != nullptr) {
            *error = "reputation.tiers 必须是 5 项数组";
        }
        return false;
    }
    for (std::size_t i = 0; i < kSettlementReputationTierCount; ++i) {
        const nlohmann::json& entry = node[i];
        if (!entry.is_object()) {
            if (error != nullptr) {
                *error = "reputation.tiers 条目必须是对象";
            }
            return false;
        }
        const auto tier = static_cast<SettlementReputationTier>(i);
        std::string id;
        if (!ReadString(entry, "id", kMaxSettlementIdBytes, &id) ||
            id != SettlementReputationTierName(tier)) {
            if (error != nullptr) {
                *error = "reputation.tiers id 顺序必须是 hostile/wary/neutral/friendly/trusted";
            }
            return false;
        }
        std::string name = out->tiers[i].name;
        if (!ReadOptionalString(entry, "name", kMaxSettlementNameBytes, &name,
                                error)) {
            return false;
        }
        int minScore = 0;
        int maxScore = 0;
        if (!ReadInt(entry, "minScore", kSettlementReputationMinScore,
                     kSettlementReputationMaxScore, &minScore) ||
            !ReadInt(entry, "maxScore", kSettlementReputationMinScore,
                     kSettlementReputationMaxScore, &maxScore) ||
            minScore > maxScore) {
            if (error != nullptr) {
                *error = "reputation.tiers 分数范围非法";
            }
            return false;
        }
        if (i == 0 && minScore != kSettlementReputationMinScore) {
            if (error != nullptr) {
                *error = "reputation.tiers 首段必须从 -100 开始";
            }
            return false;
        }
        if (i > 0 && minScore != out->tiers[i - 1].maxScore + 1) {
            if (error != nullptr) {
                *error = "reputation.tiers 分数区间必须连续";
            }
            return false;
        }
        if (i + 1 == kSettlementReputationTierCount &&
            maxScore != kSettlementReputationMaxScore) {
            if (error != nullptr) {
                *error = "reputation.tiers 末段必须到 100 结束";
            }
            return false;
        }
        out->tiers[i].tier = tier;
        out->tiers[i].id = std::move(id);
        out->tiers[i].name = std::move(name);
        out->tiers[i].minScore = minScore;
        out->tiers[i].maxScore = maxScore;
    }
    return true;
}

bool ParseReputationSources(const nlohmann::json& node,
                            SettlementReputationSources* out,
                            std::string* error) {
    if (!node.is_object()) {
        if (error != nullptr) {
            *error = "reputation.sources 必须是对象";
        }
        return false;
    }
    const std::array<std::pair<const char*, int*>, 6> fields{{
        {"missionComplete", &out->missionComplete},
        {"defenseComplete", &out->defenseComplete},
        {"trade", &out->trade},
        {"missionAbandon", &out->missionAbandon},
        {"attackNpc", &out->attackNpc},
        {"theft", &out->theft},
    }};
    for (const auto& [key, value] : fields) {
        if (!node.contains(key)) {
            continue;
        }
        if (!ReadInt(node, key, -100, 100, value)) {
            if (error != nullptr) {
                *error = std::string("reputation.sources.") + key +
                         " 必须是 -100~100";
            }
            return false;
        }
    }
    return true;
}

bool ParseReputationPriceMultipliers(const nlohmann::json& node,
                                     SettlementReputationConfig* out,
                                     std::string* error) {
    if (!node.is_object()) {
        if (error != nullptr) {
            *error = "reputation.priceMultipliers 必须是对象";
        }
        return false;
    }
    for (const auto& [key, value] : node.items()) {
        bool known = false;
        for (const SettlementReputationTierDef& tier : out->tiers) {
            if (tier.id == key) {
                known = true;
                break;
            }
        }
        if (!known) {
            if (error != nullptr) {
                *error = "reputation.priceMultipliers 未知等级 '" + key + "'";
            }
            return false;
        }
        if (!value.is_object()) {
            if (error != nullptr) {
                *error = "reputation.priceMultipliers." + key + " 必须是对象";
            }
            return false;
        }
        for (const auto& [field, ignored] : value.items()) {
            (void)ignored;
            if (field != "buy" && field != "sell") {
                if (error != nullptr) {
                    *error = "reputation.priceMultipliers." + key +
                             " 只支持 buy/sell";
                }
                return false;
            }
        }
        for (SettlementPriceMultiplierDef& multiplier : out->priceMultipliers) {
            if (SettlementReputationTierName(multiplier.tier) != key) {
                continue;
            }
            if (!ReadFloat(value, "buy", 0.1f, 5.0f, &multiplier.buy) ||
                !ReadFloat(value, "sell", 0.1f, 5.0f, &multiplier.sell)) {
                if (error != nullptr) {
                    *error = "reputation.priceMultipliers." + key +
                             " 的 buy/sell 必须是 0.1~5.0";
                }
                return false;
            }
            break;
        }
    }
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
    if (entry.contains("guardEngageRadius") &&
        !ReadFloat(entry, "guardEngageRadius", 0.0f, 64.0f,
                   &defense.guardEngageRadius)) {
        if (error != nullptr) {
            *error = "defense.guardEngageRadius 非法";
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
    if (!ReadOptionalString(entry, "regionId", kMaxSettlementIdBytes,
                            &def.regionId, error) ||
        (!def.regionId.empty() && !ValidSettlementId(def.regionId))) {
        if (error != nullptr && error->empty()) {
            *error = "聚落 regionId 非法";
        }
        return false;
    }
    if (!ParseServices(entry, &def.services, error)) {
        return false;
    }
    if (entry.contains("startingReputation") &&
        !ReadInt(entry, "startingReputation", kSettlementReputationMinScore,
                 kSettlementReputationMaxScore, &def.startingReputation)) {
        if (error != nullptr) {
            *error = "聚落 startingReputation 必须是 -100~100";
        }
        return false;
    }
    if (!ReadOptionalString(entry, "dialogueProfile",
                            kMaxSettlementIdBytes, &def.dialogueProfile,
                            error) ||
        (!def.dialogueProfile.empty() &&
         !ValidSettlementId(def.dialogueProfile))) {
        if (error != nullptr && error->empty()) {
            *error = "聚落 dialogueProfile 非法";
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
    if (!hasMerchant || (!hasGuard && !hasRequester)) {
        if (error != nullptr) {
            *error = "聚落 '" + def.id +
                     "' 必须包含 merchant，且至少包含 guard 或 requester";
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

bool LoadSettlementReputationConfig(const core::Config& config,
                                    SettlementReputationConfig* out,
                                    std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "聚落声望配置输出为空";
        }
        return false;
    }
    SettlementReputationConfig result;
    const nlohmann::json& root = config.Raw();
    if (!root.is_object()) {
        *out = std::move(result);
        return true;
    }
    const auto settlementsIt = root.find("settlements");
    if (settlementsIt == root.end() || !settlementsIt->is_object()) {
        *out = std::move(result);
        return true;
    }
    const auto reputationIt = settlementsIt->find("reputation");
    if (reputationIt == settlementsIt->end()) {
        *out = std::move(result);
        return true;
    }
    if (!reputationIt->is_object()) {
        if (error != nullptr) {
            *error = "settlements.reputation 必须是对象";
        }
        return false;
    }
    const nlohmann::json& reputation = *reputationIt;
    if (reputation.contains("enabled") &&
        !ReadBool(reputation, "enabled", &result.enabled)) {
        if (error != nullptr) {
            *error = "settlements.reputation.enabled 必须是布尔值";
        }
        return false;
    }
    if (reputation.contains("dailyTradeGainLimit") &&
        !ReadInt(reputation, "dailyTradeGainLimit", 0, 100,
                 &result.dailyTradeGainLimit)) {
        if (error != nullptr) {
            *error = "settlements.reputation.dailyTradeGainLimit 必须是 0~100";
        }
        return false;
    }
    if (reputation.contains("tiers") &&
        !ParseReputationTiers(reputation["tiers"], &result, error)) {
        return false;
    }
    if (reputation.contains("sources") &&
        !ParseReputationSources(reputation["sources"], &result.sources,
                                error)) {
        return false;
    }
    if (reputation.contains("priceMultipliers") &&
        !ParseReputationPriceMultipliers(reputation["priceMultipliers"],
                                         &result, error)) {
        return false;
    }
    *out = std::move(result);
    return true;
}

bool LoadSettlementConfig(const core::Config& config, SettlementConfig* out,
                          std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "聚落配置输出为空";
        }
        return false;
    }
    SettlementConfig result;
    if (!LoadSettlementReputationConfig(config, &result.reputation, error)) {
        return false;
    }
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

const char* SettlementServiceName(SettlementService service) {
    switch (service) {
        case SettlementService::Trade:
            return "trade";
        case SettlementService::Missions:
            return "missions";
        case SettlementService::Defense:
            return "defense";
        case SettlementService::Recruit:
            return "recruit";
    }
    return "trade";
}

bool ParseSettlementService(std::string_view value, SettlementService* out) {
    if (out == nullptr) {
        return false;
    }
    if (value == "trade") {
        *out = SettlementService::Trade;
        return true;
    }
    if (value == "missions") {
        *out = SettlementService::Missions;
        return true;
    }
    if (value == "defense") {
        *out = SettlementService::Defense;
        return true;
    }
    if (value == "recruit") {
        *out = SettlementService::Recruit;
        return true;
    }
    return false;
}

bool HasSettlementService(const SettlementDef& def, SettlementService service) {
    const SettlementServiceMask bit =
        static_cast<SettlementServiceMask>(1u << static_cast<unsigned>(service));
    return (def.services & bit) != 0;
}

const char* SettlementReputationTierName(SettlementReputationTier tier) {
    switch (tier) {
        case SettlementReputationTier::Hostile:
            return "hostile";
        case SettlementReputationTier::Wary:
            return "wary";
        case SettlementReputationTier::Neutral:
            return "neutral";
        case SettlementReputationTier::Friendly:
            return "friendly";
        case SettlementReputationTier::Trusted:
            return "trusted";
    }
    return "neutral";
}

bool ParseSettlementReputationTier(std::string_view value,
                                   SettlementReputationTier* out) {
    if (out == nullptr) {
        return false;
    }
    for (std::size_t i = 0; i < kSettlementReputationTierCount; ++i) {
        const auto tier = static_cast<SettlementReputationTier>(i);
        if (value == SettlementReputationTierName(tier)) {
            *out = tier;
            return true;
        }
    }
    return false;
}

const SettlementReputationTierDef* FindSettlementReputationTier(
    const SettlementReputationConfig& config, SettlementReputationTier tier) {
    for (const SettlementReputationTierDef& def : config.tiers) {
        if (def.tier == tier) {
            return &def;
        }
    }
    return nullptr;
}

const SettlementPriceMultiplierDef* FindSettlementPriceMultiplier(
    const SettlementReputationConfig& config, SettlementReputationTier tier) {
    for (const SettlementPriceMultiplierDef& def : config.priceMultipliers) {
        if (def.tier == tier) {
            return &def;
        }
    }
    return nullptr;
}

const SettlementReputationTierDef* SettlementReputationTierForScore(
    const SettlementReputationConfig& config, int score) {
    const int clamped =
        std::clamp(score, kSettlementReputationMinScore,
                   kSettlementReputationMaxScore);
    for (const SettlementReputationTierDef& def : config.tiers) {
        if (clamped >= def.minScore && clamped <= def.maxScore) {
            return &def;
        }
    }
    return FindSettlementReputationTier(config,
                                        SettlementReputationTier::Neutral);
}

bool SettlementIsHostile(const SettlementReputationConfig& config, int score) {
    if (!config.enabled) {
        return false;
    }
    const SettlementReputationTierDef* tier =
        SettlementReputationTierForScore(config, score);
    return tier != nullptr && tier->tier == SettlementReputationTier::Hostile;
}

bool SettlementCanUseService(const SettlementDef& def,
                             const SettlementReputationConfig& config,
                             SettlementService service, int score) {
    if (!HasSettlementService(def, service)) {
        return false;
    }
    if (service == SettlementService::Defense) {
        return true;
    }
    return !config.enabled || !SettlementIsHostile(config, score);
}

bool SettlementProvidesDefense(const SettlementDef& def) {
    return def.defense.enabled &&
           HasSettlementService(def, SettlementService::Defense);
}

int SettlementAdjustedPrice(int basePrice, float multiplier) {
    if (basePrice <= 0) {
        return 0;
    }
    const float safeMultiplier = std::clamp(multiplier, 0.1f, 5.0f);
    return std::max(1, static_cast<int>(std::lround(
                           static_cast<float>(basePrice) * safeMultiplier)));
}

int SettlementBuyPrice(int baseCost,
                       const SettlementReputationConfig& config, int score) {
    if (baseCost <= 0) {
        return 0;
    }
    if (!config.enabled) {
        return baseCost;
    }
    const SettlementReputationTierDef* tier =
        SettlementReputationTierForScore(config, score);
    const SettlementPriceMultiplierDef* multiplier =
        tier != nullptr ? FindSettlementPriceMultiplier(config, tier->tier)
                        : nullptr;
    return SettlementAdjustedPrice(
        baseCost, multiplier != nullptr ? multiplier->buy : 1.0f);
}

int SettlementSellPrice(int baseValue,
                        const SettlementReputationConfig& config, int score) {
    if (baseValue <= 0) {
        return 0;
    }
    if (!config.enabled) {
        return baseValue;
    }
    const SettlementReputationTierDef* tier =
        SettlementReputationTierForScore(config, score);
    const SettlementPriceMultiplierDef* multiplier =
        tier != nullptr ? FindSettlementPriceMultiplier(config, tier->tier)
                        : nullptr;
    return SettlementAdjustedPrice(
        baseValue, multiplier != nullptr ? multiplier->sell : 1.0f);
}

std::string SettlementServicesText(SettlementServiceMask services) {
    std::string text;
    const auto append = [&](const char* label) {
        if (!text.empty()) {
            text += "/";
        }
        text += label;
    };
    if ((services & kSettlementServiceTrade) != 0) {
        append("交易");
    }
    if ((services & kSettlementServiceMissions) != 0) {
        append("委托");
    }
    if ((services & kSettlementServiceDefense) != 0) {
        append("防御");
    }
    if ((services & kSettlementServiceRecruit) != 0) {
        append("招募");
    }
    return text.empty() ? "无" : text;
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
