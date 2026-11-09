#include "game/Animals.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "items/ItemRegistry.h"
#include "items/LootTable.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "animals";
constexpr const char* kFormat = "animals-v1";
constexpr std::size_t kMaxAnimals = 64;
constexpr std::size_t kMaxIdLength = 64;
constexpr std::size_t kMaxNameLength = 64;
constexpr std::size_t kMaxSpriteLength = 128;

bool ValidId(std::string_view value) {
    return !value.empty() && value.size() <= kMaxIdLength &&
           std::all_of(value.begin(), value.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '-';
           });
}

bool CheckKeys(const nlohmann::json& entry,
               const std::unordered_set<std::string>& allowed,
               const std::string& where, std::string* error) {
    for (auto it = entry.begin(); it != entry.end(); ++it) {
        if (!allowed.contains(it.key())) {
            if (error != nullptr) {
                *error = where + " 未知字段 '" + it.key() + "'";
            }
            return false;
        }
    }
    return true;
}

bool ReadString(const nlohmann::json& node, const char* key, std::size_t maxLength,
                std::string* out, std::string* error, const std::string& where) {
    if (!node.contains(key) || !node[key].is_string()) {
        if (error != nullptr) {
            *error = where + " 缺少字符串字段 '" + key + "'";
        }
        return false;
    }
    const std::string value = node[key].get<std::string>();
    if (value.empty() || value.size() > maxLength) {
        if (error != nullptr) {
            *error = where + " 字段 '" + key + "' 长度非法";
        }
        return false;
    }
    *out = value;
    return true;
}

bool ReadFloat(const nlohmann::json& node, const char* key, float lo, float hi,
               float default_value, float* out, std::string* error,
               const std::string& where) {
    if (!node.contains(key)) {
        *out = default_value;
        return true;
    }
    if (!node[key].is_number()) {
        if (error != nullptr) {
            *error = where + " 字段 '" + key + "' 必须是数字";
        }
        return false;
    }
    const double value = node[key].get<double>();
    if (!std::isfinite(value) || value < static_cast<double>(lo) ||
        value > static_cast<double>(hi)) {
        if (error != nullptr) {
            *error = where + " 字段 '" + key + "' 超出范围";
        }
        return false;
    }
    *out = static_cast<float>(value);
    return true;
}

bool ReadRequiredFloat(const nlohmann::json& node, const char* key, float lo,
                       float hi, float* out, std::string* error,
                       const std::string& where) {
    if (!node.contains(key)) {
        if (error != nullptr) {
            *error = where + " 缺少数字字段 '" + key + "'";
        }
        return false;
    }
    return ReadFloat(node, key, lo, hi, 0.0f, out, error, where);
}

bool ReadInt(const nlohmann::json& node, const char* key, int lo, int hi,
             int default_value, int* out, std::string* error,
             const std::string& where) {
    if (!node.contains(key)) {
        *out = default_value;
        return true;
    }
    if (!node[key].is_number_integer()) {
        if (error != nullptr) {
            *error = where + " 字段 '" + key + "' 必须是整数";
        }
        return false;
    }
    const long long value = node[key].get<long long>();
    if (value < lo || value > hi) {
        if (error != nullptr) {
            *error = where + " 字段 '" + key + "' 超出范围";
        }
        return false;
    }
    *out = static_cast<int>(value);
    return true;
}

bool ReadStringArray(const nlohmann::json& node, const char* key,
                     std::size_t maxItems, std::vector<std::string>* out,
                     std::string* error, const std::string& where) {
    out->clear();
    if (!node.contains(key)) {
        return true;
    }
    if (!node[key].is_array() || node[key].size() > maxItems) {
        if (error != nullptr) {
            *error = where + " 字段 '" + key + "' 必须是不超过 " +
                     std::to_string(maxItems) + " 项的数组";
        }
        return false;
    }
    std::unordered_set<std::string> seen;
    for (const nlohmann::json& value : node[key]) {
        if (!value.is_string()) {
            if (error != nullptr) {
                *error = where + " 字段 '" + key + "' 只能包含字符串";
            }
            return false;
        }
        std::string text = value.get<std::string>();
        if (!ValidId(text)) {
            if (error != nullptr) {
                *error = where + " 字段 '" + key + "' 含非法 id";
            }
            return false;
        }
        if (!seen.insert(text).second) {
            if (error != nullptr) {
                *error = where + " 字段 '" + key + "' 含重复 id '" + text + "'";
            }
            return false;
        }
        out->push_back(std::move(text));
    }
    return true;
}

bool ParseSpawnRule(const nlohmann::json& node, AnimalSpawnRule* out,
                    std::string* error, const std::string& where) {
    if (!node.is_object()) {
        if (error != nullptr) {
            *error = where + " 必须是对象";
        }
        return false;
    }
    const std::unordered_set<std::string> allowed = {
        "forest", "meadow", "poi", "maxPerChunk", "poiIds"};
    if (!CheckKeys(node, allowed, where, error)) {
        return false;
    }

    AnimalSpawnRule rule;
    if (!ReadFloat(node, "forest", 0.0f, 1.0f, 0.0f, &rule.forest, error,
                   where) ||
        !ReadFloat(node, "meadow", 0.0f, 1.0f, 0.0f, &rule.meadow, error,
                   where) ||
        !ReadFloat(node, "poi", 0.0f, 1.0f, 0.0f, &rule.poi, error, where) ||
        !ReadInt(node, "maxPerChunk", 0, 8, 0, &rule.maxPerChunk, error,
                 where) ||
        !ReadStringArray(node, "poiIds", 16, &rule.poiIds, error, where)) {
        return false;
    }
    const bool anyWeight = rule.forest > 0.0f || rule.meadow > 0.0f ||
                           rule.poi > 0.0f;
    if (anyWeight && rule.maxPerChunk <= 0) {
        if (error != nullptr) {
            *error = where + " 配置生成权重时 maxPerChunk 必须大于 0";
        }
        return false;
    }
    if (!anyWeight && rule.maxPerChunk != 0) {
        if (error != nullptr) {
            *error = where + " 生成权重全为 0 时 maxPerChunk 必须为 0";
        }
        return false;
    }
    if (rule.poiIds.size() > 0 && rule.poi <= 0.0f) {
        if (error != nullptr) {
            *error = where + " 配置 poiIds 时 poi 权重必须大于 0";
        }
        return false;
    }
    *out = std::move(rule);
    return true;
}

bool ParseKind(const nlohmann::json& node, ecs::AnimalKind* out,
               std::string* error, const std::string& where) {
    std::string text;
    if (!ReadString(node, "kind", 16, &text, error, where)) {
        return false;
    }
    if (text == "passive") {
        *out = ecs::AnimalKind::Passive;
        return true;
    }
    if (text == "dangerous") {
        *out = ecs::AnimalKind::Dangerous;
        return true;
    }
    if (error != nullptr) {
        *error = where + " 字段 'kind' 必须是 passive 或 dangerous";
    }
    return false;
}

bool ParseAnimal(const nlohmann::json& node, const items::ItemRegistry& items,
                 const items::LootTableRegistry& lootTables, AnimalDef* out,
                 std::string* error, const std::string& where) {
    if (!node.is_object()) {
        if (error != nullptr) {
            *error = where + " 必须是对象";
        }
        return false;
    }
    const std::unordered_set<std::string> allowed = {
        "id", "name", "kind", "sprite", "maxHealth", "wanderSpeed",
        "fleeSpeed", "alertRadius", "fleeRadius", "hearingRadius",
        "fleeSeconds", "damage", "attackRange", "attackCooldown",
        "corpseSprite", "corpseSearchSeconds", "corpseTool",
        "corpseToolWear", "lootTableId", "spawn"};
    if (!CheckKeys(node, allowed, where, error)) {
        return false;
    }

    AnimalDef animal;
    if (!ReadString(node, "id", kMaxIdLength, &animal.id, error, where)) {
        return false;
    }
    if (!ValidId(animal.id)) {
        if (error != nullptr) {
            *error = where + " id 非法（1~64，仅小写字母/数字/下划线/连字符）";
        }
        return false;
    }
    if (!ReadString(node, "name", kMaxNameLength, &animal.name, error, where) ||
        !ParseKind(node, &animal.kind, error, where) ||
        !ReadString(node, "sprite", kMaxSpriteLength, &animal.sprite, error,
                    where) ||
        !ReadRequiredFloat(node, "maxHealth", 1.0f, 10000.0f,
                           &animal.maxHealth, error, where) ||
        !ReadRequiredFloat(node, "wanderSpeed", 0.1f, 20.0f,
                           &animal.wanderSpeed, error, where) ||
        !ReadRequiredFloat(node, "fleeSpeed", 0.1f, 30.0f,
                           &animal.fleeSpeed, error, where) ||
        !ReadRequiredFloat(node, "alertRadius", 0.1f, 64.0f,
                           &animal.alertRadius, error, where) ||
        !ReadRequiredFloat(node, "fleeRadius", 0.1f, 64.0f,
                           &animal.fleeRadius, error, where) ||
        !ReadRequiredFloat(node, "hearingRadius", 0.0f, 128.0f,
                           &animal.hearingRadius, error, where) ||
        !ReadRequiredFloat(node, "fleeSeconds", 0.1f, 60.0f,
                           &animal.fleeSeconds, error, where) ||
        !ReadRequiredFloat(node, "damage", 0.0f, 500.0f, &animal.damage, error,
                           where) ||
        !ReadRequiredFloat(node, "attackRange", 0.0f, 16.0f,
                           &animal.attackRange, error, where) ||
        !ReadRequiredFloat(node, "attackCooldown", 0.0f, 30.0f,
                           &animal.attackCooldown, error, where) ||
        !ReadString(node, "lootTableId", kMaxIdLength, &animal.lootTableId,
                    error, where)) {
        return false;
    }

    if (animal.fleeSpeed < animal.wanderSpeed) {
        if (error != nullptr) {
            *error = where + " fleeSpeed 不能小于 wanderSpeed";
        }
        return false;
    }
    if (animal.kind == ecs::AnimalKind::Dangerous) {
        if (animal.damage <= 0.0f || animal.attackRange <= 0.0f ||
            animal.attackCooldown <= 0.0f) {
            if (error != nullptr) {
                *error = where +
                         " dangerous 动物必须配置正 damage/attackRange/attackCooldown";
            }
            return false;
        }
    } else if (animal.damage != 0.0f || animal.attackRange != 0.0f ||
               animal.attackCooldown != 0.0f) {
        if (error != nullptr) {
            *error = where +
                     " passive 动物的 damage/attackRange/attackCooldown 必须为 0";
        }
        return false;
    }

    if (!node.contains("corpseSprite")) {
        animal.corpseSprite = "corpse.placeholder";
    } else if (!ReadString(node, "corpseSprite", kMaxSpriteLength,
                           &animal.corpseSprite, error, where)) {
        return false;
    }
    if (!ReadFloat(node, "corpseSearchSeconds", 0.1f, 30.0f, 1.5f,
                   &animal.corpseSearchSeconds, error, where)) {
        return false;
    }
    if (node.contains("corpseTool")) {
        if (!ReadString(node, "corpseTool", kMaxIdLength, &animal.corpseTool,
                        error, where) ||
            !ValidId(animal.corpseTool)) {
            if (error != nullptr) {
                *error = where + " 字段 'corpseTool' 非法";
            }
            return false;
        }
        if (items.Find(animal.corpseTool) == nullptr) {
            if (error != nullptr) {
                *error = where + " corpseTool 引用了未知物品 '" +
                         animal.corpseTool + "'";
            }
            return false;
        }
    }
    if (!ReadFloat(node, "corpseToolWear", 0.0f, 1.0f, 0.0f,
                   &animal.corpseToolWear, error, where)) {
        return false;
    }
    if (animal.corpseTool.empty() && animal.corpseToolWear != 0.0f) {
        if (error != nullptr) {
            *error = where + " 未配置 corpseTool 时 corpseToolWear 必须为 0";
        }
        return false;
    }
    if (lootTables.Find(animal.lootTableId) == nullptr) {
        if (error != nullptr) {
            *error = where + " lootTableId 引用了未知战利品表 '" +
                     animal.lootTableId + "'";
        }
        return false;
    }
    if (node.contains("spawn")) {
        if (!ParseSpawnRule(node["spawn"], &animal.spawn, error,
                            where + ".spawn")) {
            return false;
        }
    }
    *out = std::move(animal);
    return true;
}

}

const char* AnimalKindName(ecs::AnimalKind kind) {
    switch (kind) {
        case ecs::AnimalKind::Dangerous:
            return "dangerous";
        case ecs::AnimalKind::Passive:
        default:
            return "passive";
    }
}

float AnimalVisualScale(std::string_view speciesId) {
    if (speciesId == "deer") {
        return 0.85f;
    }
    if (speciesId == "rabbit") {
        return 0.45f;
    }
    if (speciesId == "boar") {
        return 1.10f;
    }
    if (speciesId == "chicken") {
        return 0.55f;
    }
    if (speciesId == "goat") {
        return 1.0f;
    }
    return 1.0f;
}

bool AnimalRegistry::LoadFile(const std::filesystem::path& path,
                              const items::ItemRegistry& items,
                              const items::LootTableRegistry& lootTables,
                              std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        if (error != nullptr) {
            *error = "无法打开动物数据文件: " + path.string();
        }
        ZL_ERROR(kLogModule, "无法打开动物数据文件: {}", path.string());
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return LoadFromString(buffer.str(), path.string(), items, lootTables, error);
}

bool AnimalRegistry::LoadFromString(std::string_view text,
                                    const std::string& source,
                                    const items::ItemRegistry& items,
                                    const items::LootTableRegistry& lootTables,
                                    std::string* error) {
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
        return fail(std::string("缺少或不受支持 format（期望 \"") + kFormat +
                    "\"）");
    }
    const std::unordered_set<std::string> rootKeys = {"format", "animals"};
    if (!CheckKeys(doc, rootKeys, "根节点", error)) {
        return fail(error != nullptr ? *error : "根节点字段非法");
    }
    if (!doc.contains("animals") || !doc["animals"].is_array() ||
        doc["animals"].empty()) {
        return fail("缺少非空 animals 数组");
    }
    if (doc["animals"].size() > kMaxAnimals) {
        return fail("animals 条目超过上限 " + std::to_string(kMaxAnimals));
    }

    std::vector<AnimalDef> animals;
    std::unordered_map<std::string, std::size_t> byId;
    animals.reserve(doc["animals"].size());
    for (std::size_t i = 0; i < doc["animals"].size(); ++i) {
        const std::string where = "animals[" + std::to_string(i) + "]";
        AnimalDef animal;
        if (!ParseAnimal(doc["animals"][i], items, lootTables, &animal, error,
                         where)) {
            return fail(error != nullptr ? *error : where + " 非法");
        }
        if (byId.contains(animal.id)) {
            return fail(where + " 动物 id 重复: " + animal.id);
        }
        byId.emplace(animal.id, animals.size());
        animals.push_back(std::move(animal));
    }

    m_animals = std::move(animals);
    m_byId = std::move(byId);
    return true;
}

const AnimalDef* AnimalRegistry::Find(std::string_view id) const {
    const auto it = m_byId.find(std::string(id));
    return it == m_byId.end() ? nullptr : &m_animals[it->second];
}

}
