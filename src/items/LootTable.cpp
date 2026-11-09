#include "items/LootTable.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "items/InventoryOps.h"

namespace zl::items {
namespace {

constexpr const char* kLogModule = "items";
constexpr const char* kFormat = "loot-v1";
constexpr std::size_t kMaxIdLength = 64;
constexpr std::size_t kMaxNameLength = 64;
constexpr int kMaxRolls = 16;
constexpr int kMaxWeight = 1000;
constexpr float kMaxCapacityKg = 2000.0f;

bool LowerId(std::string_view value) {
    return !value.empty() && value.size() <= kMaxIdLength &&
           std::all_of(value.begin(), value.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
                      c == '-';
           });
}

bool CheckKeys(const nlohmann::json& entry, const std::unordered_set<std::string>& allowed,
               const std::string& where, const std::string& hint, std::string* error) {
    for (auto it = entry.begin(); it != entry.end(); ++it) {
        if (!allowed.contains(it.key())) {
            if (error != nullptr) {
                *error = where + " 未知字段 '" + it.key() + "'（允许: " + hint + "）";
            }
            return false;
        }
    }
    return true;
}

bool ParseEntry(const nlohmann::json& node, const ItemRegistry& items, bool guaranteed,
                const std::string& where, LootEntry* out, std::string* error) {
    const auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    if (!node.is_object()) {
        return fail(where + " 必须是对象");
    }
    const std::string hint =
        guaranteed ? "defId/count" : "defId/weight/min/max";
    const std::unordered_set<std::string> allowed =
        guaranteed ? std::unordered_set<std::string>{"defId", "count"}
                   : std::unordered_set<std::string>{"defId", "weight", "min", "max"};
    if (!CheckKeys(node, allowed, where, hint, error)) {
        return false;
    }
    if (!node.contains("defId") || !node["defId"].is_string()) {
        return fail(where + " 缺少字符串字段 'defId'");
    }
    const std::string defId = node["defId"].get<std::string>();
    const ItemDef* def = items.Find(defId);
    if (def == nullptr) {
        return fail(where + " 引用了未知物品 '" + defId + "'");
    }
    LootEntry entry;
    entry.defId = defId;
    if (guaranteed) {
        if (!node.contains("count") || !node["count"].is_number_integer()) {
            return fail(where + " 缺少整数字段 'count'");
        }
        entry.minCount = node["count"].get<int>();
        entry.maxCount = entry.minCount;
    } else {
        if (!node.contains("weight") || !node["weight"].is_number_integer()) {
            return fail(where + " 缺少整数字段 'weight'");
        }
        entry.weight = node["weight"].get<int>();
        if (entry.weight < 1 || entry.weight > kMaxWeight) {
            return fail(where + " weight 必须是 1~" +
                        std::to_string(kMaxWeight));
        }
        if (!node.contains("min") || !node.contains("max") ||
            !node["min"].is_number_integer() || !node["max"].is_number_integer()) {
            return fail(where + " 缺少整数字段 'min'/'max'");
        }
        entry.minCount = node["min"].get<int>();
        entry.maxCount = node["max"].get<int>();
    }
    if (entry.minCount < 1 || entry.maxCount < entry.minCount ||
        entry.maxCount > def->maxStack) {
        return fail(where + " 数量必须是 1~" + std::to_string(def->maxStack) +
                    " 且 min <= max");
    }
    *out = std::move(entry);
    return true;
}

struct SplitMix64 {
    std::uint64_t state = 0;

    std::uint64_t Next() {
        state += 0x9E3779B97F4A7C15ull;
        std::uint64_t value = state;
        value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27U)) * 0x94D049BB133111EBull;
        return value ^ (value >> 31U);
    }
};

int AddLoot(const ItemRegistry& items, ecs::Container& target, std::string_view defId,
            int count, LootRollResult* result) {
    if (count <= 0) {
        return 0;
    }
    const int added = AddItem(target, items,
                              ecs::ItemInstance{std::string(defId), count, 1.0f});
    if (added > 0) {
        ++result->stacksAdded;
        result->itemsAdded += added;
    }
    return added;
}

}  // namespace

bool LootTableRegistry::LoadFile(const std::filesystem::path& path,
                                 const ItemRegistry& items, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        if (error != nullptr) {
            *error = "无法打开战利品文件: " + path.string();
        }
        ZL_ERROR(kLogModule, "无法打开战利品文件: {}", path.string());
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return LoadFromString(buffer.str(), path.string(), items, error);
}

bool LootTableRegistry::LoadFromString(std::string_view text, const std::string& source,
                                       const ItemRegistry& items, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    auto fail = [&](const std::string& message) {
        const std::string full = source + ": " + message;
        if (error != nullptr) {
            *error = full;
        }
        ZL_ERROR(kLogModule, "{}", full);
        return false;
    };

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& e) {
        return fail("JSON 语法错误 — " + std::string(e.what()));
    } catch (const std::exception& e) {
        return fail("JSON 解析失败 — " + std::string(e.what()));
    }
    if (!doc.is_object()) {
        return fail("根节点必须是对象");
    }
    if (!doc.contains("format") || !doc["format"].is_string() ||
        doc["format"].get<std::string>() != kFormat) {
        return fail(std::string("缺少或不受支持 format（期望 \"") + kFormat + "\"）");
    }
    std::unordered_set<std::string> rootKeys = {"format", "seed", "tables"};
    if (!CheckKeys(doc, rootKeys, "根节点", "format/seed/tables", error)) {
        return fail(error != nullptr ? *error : "根节点字段非法");
    }
    std::uint64_t seed = 20261022u;
    if (doc.contains("seed")) {
        if (doc["seed"].is_number_unsigned()) {
            seed = doc["seed"].get<std::uint64_t>();
        } else if (doc["seed"].is_number_integer()) {
            const std::int64_t value = doc["seed"].get<std::int64_t>();
            if (value < 0) {
                return fail("seed 必须是非负整数");
            }
            seed = static_cast<std::uint64_t>(value);
        } else {
            return fail("seed 必须是非负整数");
        }
    }

    if (!doc.contains("tables") || !doc["tables"].is_array()) {
        return fail("缺少 tables 数组");
    }
    if (doc["tables"].empty()) {
        return fail("tables 数组为空（至少需要 1 张表）");
    }

    std::vector<LootTable> tables;
    std::unordered_map<std::string, std::size_t> byId;
    for (std::size_t i = 0; i < doc["tables"].size(); ++i) {
        const std::string where = "tables[" + std::to_string(i) + "]";
        const nlohmann::json& entry = doc["tables"][i];
        if (!entry.is_object()) {
            return fail(where + " 必须是对象");
        }
        const std::unordered_set<std::string> tableKeys = {
            "id", "name", "capacityKg", "rolls", "guaranteed", "entries"};
        if (!CheckKeys(entry, tableKeys, where,
                       "id/name/capacityKg/rolls/guaranteed/entries", error)) {
            return fail(error != nullptr ? *error : where + " 字段非法");
        }

        LootTable table;
        if (!entry.contains("id") || !entry["id"].is_string()) {
            return fail(where + " 缺少字符串字段 'id'");
        }
        table.id = entry["id"].get<std::string>();
        if (!LowerId(table.id)) {
            return fail(where + " id 非法（1~64，仅小写字母/数字/下划线/连字符）");
        }
        if (byId.contains(table.id)) {
            return fail(where + " 表 id 重复: " + table.id);
        }
        if (!entry.contains("name") || !entry["name"].is_string() ||
            entry["name"].get<std::string>().empty() ||
            entry["name"].get<std::string>().size() > kMaxNameLength) {
            return fail(where + " name 必须是 1~64 字符");
        }
        table.name = entry["name"].get<std::string>();
        if (entry.contains("capacityKg")) {
            if (!entry["capacityKg"].is_number()) {
                return fail(where + " capacityKg 必须是数字");
            }
            const double capacity = entry["capacityKg"].get<double>();
            if (!std::isfinite(capacity) || capacity <= 0.0 ||
                capacity > kMaxCapacityKg) {
                return fail(where + " capacityKg 超出范围");
            }
            table.capacityKg = static_cast<float>(capacity);
        }
        if (!entry.contains("rolls") || !entry["rolls"].is_number_integer()) {
            return fail(where + " 缺少整数字段 'rolls'");
        }
        table.rolls = entry["rolls"].get<int>();
        if (table.rolls < 0 || table.rolls > kMaxRolls) {
            return fail(where + " rolls 必须是 0~" + std::to_string(kMaxRolls));
        }

        if (entry.contains("guaranteed")) {
            if (!entry["guaranteed"].is_array()) {
                return fail(where + " guaranteed 必须是数组");
            }
            for (std::size_t j = 0; j < entry["guaranteed"].size(); ++j) {
                LootEntry item;
                if (!ParseEntry(entry["guaranteed"][j], items, true,
                                where + ".guaranteed[" + std::to_string(j) + "]", &item,
                                error)) {
                    return fail(error != nullptr ? *error : where + " 保底条目非法");
                }
                table.guaranteed.push_back(std::move(item));
            }
        }

        if (entry.contains("entries")) {
            if (!entry["entries"].is_array()) {
                return fail(where + " entries 必须是数组");
            }
            for (std::size_t j = 0; j < entry["entries"].size(); ++j) {
                LootEntry item;
                if (!ParseEntry(entry["entries"][j], items, false,
                                where + ".entries[" + std::to_string(j) + "]", &item,
                                error)) {
                    return fail(error != nullptr ? *error : where + " 随机条目非法");
                }
                table.entries.push_back(std::move(item));
            }
        }
        if (table.rolls > 0 && table.entries.empty()) {
            return fail(where + " rolls > 0 时 entries 不能为空");
        }

        byId.emplace(table.id, tables.size());
        tables.push_back(std::move(table));
    }

    m_tables = std::move(tables);
    m_byId = std::move(byId);
    m_seed = seed;
    return true;
}

bool LootTableRegistry::MergeFile(const std::filesystem::path& path,
                                  const ItemRegistry& items, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        if (error != nullptr) {
            *error = "无法打开战利品文件: " + path.string();
        }
        ZL_ERROR(kLogModule, "无法打开战利品文件: {}", path.string());
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return MergeFromString(buffer.str(), path.string(), items, error);
}

bool LootTableRegistry::MergeFromString(std::string_view text,
                                        const std::string& source,
                                        const ItemRegistry& items,
                                        std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    LootTableRegistry parsed;
    if (!parsed.LoadFromString(text, source, items, error)) {
        return false;
    }
    for (const LootTable& table : parsed.m_tables) {
        if (m_byId.contains(table.id)) {
            const std::string message =
                source + ": 表 id 与已加载战利品表冲突: " + table.id;
            if (error != nullptr) {
                *error = message;
            }
            ZL_ERROR(kLogModule, "{}", message);
            return false;
        }
    }
    for (LootTable& table : parsed.m_tables) {
        const std::string id = table.id;
        m_byId.emplace(id, m_tables.size());
        m_tables.push_back(std::move(table));
    }
    return true;
}

const LootTable* LootTableRegistry::Find(std::string_view id) const {
    const auto it = m_byId.find(std::string(id));
    return it == m_byId.end() ? nullptr : &m_tables[it->second];
}

std::uint64_t DeriveLootSeed(std::uint64_t baseSeed, std::string_view tableId,
                             std::uint64_t salt) {
    std::uint64_t hash = 1469598103934665603ull ^ (baseSeed + 0x9E3779B97F4A7C15ull);
    for (const char c : tableId) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 1099511628211ull;
    }
    hash ^= salt;
    hash *= 1099511628211ull;
    hash ^= hash >> 33U;
    hash *= 0xFF51AFD7ED558CCDull;
    hash ^= hash >> 33U;
    return hash;
}

LootRollResult RollLootTable(const LootTable& table, const ItemRegistry& items,
                             ecs::Container& target, std::uint64_t seed) {
    LootRollResult result;
    for (const LootEntry& entry : table.guaranteed) {
        const int count = std::clamp(entry.minCount, 1, entry.maxCount);
        AddLoot(items, target, entry.defId, count, &result);
    }
    if (table.rolls <= 0 || table.entries.empty()) {
        return result;
    }

    std::uint64_t totalWeight = 0;
    for (const LootEntry& entry : table.entries) {
        totalWeight += static_cast<std::uint64_t>(std::max(entry.weight, 1));
    }
    if (totalWeight == 0) {
        return result;
    }

    SplitMix64 rng{seed};
    for (int roll = 0; roll < table.rolls; ++roll) {
        const std::uint64_t pick = rng.Next() % totalWeight;
        std::uint64_t accumulated = 0;
        const LootEntry* selected = nullptr;
        for (const LootEntry& entry : table.entries) {
            accumulated += static_cast<std::uint64_t>(std::max(entry.weight, 1));
            if (pick < accumulated) {
                selected = &entry;
                break;
            }
        }
        if (selected == nullptr) {
            continue;
        }
        const int span = std::max(selected->maxCount - selected->minCount + 1, 1);
        const int count =
            selected->minCount + static_cast<int>(rng.Next() % static_cast<std::uint64_t>(span));
        AddLoot(items, target, selected->defId, count, &result);
    }
    return result;
}

}  // namespace zl::items
