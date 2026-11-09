#include "game/Fire.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Log.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "fire";
constexpr const char* kFormat = "fire-v1";
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

bool ReadSprites(const nlohmann::json& node, std::vector<std::string>* out,
                 std::string* error, const std::string& where) {
    if (!node.contains("sprites") || !node["sprites"].is_array() ||
        node["sprites"].empty()) {
        if (error != nullptr) {
            *error = where + " 缺少非空数组字段 'sprites'";
        }
        return false;
    }
    std::unordered_set<std::string> seen;
    out->clear();
    for (const nlohmann::json& value : node["sprites"]) {
        if (!value.is_string()) {
            if (error != nullptr) {
                *error = where + " 字段 'sprites' 只能包含字符串";
            }
            return false;
        }
        std::string sprite = value.get<std::string>();
        if (sprite.empty() || sprite.size() > kMaxSpriteLength) {
            if (error != nullptr) {
                *error = where + " 字段 'sprites' 含非法空串或超长值";
            }
            return false;
        }
        if (!seen.insert(sprite).second) {
            if (error != nullptr) {
                *error = where + " 字段 'sprites' 含重复值 '" + sprite + "'";
            }
            return false;
        }
        out->push_back(std::move(sprite));
    }
    return true;
}

bool ReadDrops(const nlohmann::json& node, std::vector<FireDrop>* out,
               std::string* error, const std::string& where) {
    out->clear();
    if (!node.contains("charredDrops")) {
        return true;
    }
    const nlohmann::json& drops = node["charredDrops"];
    if (!drops.is_array() || drops.size() > 8) {
        if (error != nullptr) {
            *error = where + " 字段 'charredDrops' 必须是最多 8 项的数组";
        }
        return false;
    }
    std::unordered_set<std::string> seen;
    for (std::size_t i = 0; i < drops.size(); ++i) {
        const nlohmann::json& entry = drops[i];
        if (!entry.is_object()) {
            if (error != nullptr) {
                *error = where + ".charredDrops[" + std::to_string(i) +
                         "] 必须是对象";
            }
            return false;
        }
        const std::unordered_set<std::string> allowed = {"item", "count"};
        if (!CheckKeys(entry, allowed,
                       where + ".charredDrops[" + std::to_string(i) + "]",
                       error)) {
            return false;
        }
        FireDrop drop;
        if (!ReadString(entry, "item", kMaxIdLength, &drop.itemId, error,
                        where + ".charredDrops[" + std::to_string(i) + "]")) {
            return false;
        }
        if (!ValidId(drop.itemId) || !seen.insert(drop.itemId).second) {
            if (error != nullptr) {
                *error = where + ".charredDrops[" + std::to_string(i) +
                         "] item 非法或重复";
            }
            return false;
        }
        if (!entry.contains("count") || !entry["count"].is_number_integer()) {
            if (error != nullptr) {
                *error = where + ".charredDrops[" + std::to_string(i) +
                         "] 缺少整数 count";
            }
            return false;
        }
        const int count = entry["count"].get<int>();
        if (count < 1 || count > 99) {
            if (error != nullptr) {
                *error = where + ".charredDrops[" + std::to_string(i) +
                         "] count 超出范围";
            }
            return false;
        }
        drop.count = count;
        out->push_back(std::move(drop));
    }
    return true;
}

bool ParseMaterial(const nlohmann::json& node, std::string* error,
                   const std::string& where, FireMaterialDef* out) {
    if (!node.is_object()) {
        if (error != nullptr) {
            *error = where + " 必须是对象";
        }
        return false;
    }
    const std::unordered_set<std::string> allowed = {
        "id", "name", "sprites", "flammability", "charredDrops"};
    if (!CheckKeys(node, allowed, where, error)) {
        return false;
    }

    FireMaterialDef material;
    if (!ReadString(node, "id", kMaxIdLength, &material.id, error, where)) {
        return false;
    }
    if (!ValidId(material.id)) {
        if (error != nullptr) {
            *error = where +
                     " id 非法（1~64，仅小写字母/数字/下划线/连字符）";
        }
        return false;
    }
    if (!ReadString(node, "name", kMaxNameLength, &material.name, error,
                    where) ||
        !ReadSprites(node, &material.sprites, error, where)) {
        return false;
    }

    if (!node.contains("flammability") || !node["flammability"].is_object()) {
        if (error != nullptr) {
            *error = where + " 缺少对象字段 'flammability'";
        }
        return false;
    }
    const nlohmann::json& flam = node["flammability"];
    const std::unordered_set<std::string> flamKeys = {
        "ignitionThreshold", "burnSeconds", "smokePerSecond", "heatRadius"};
    if (!CheckKeys(flam, flamKeys, where + ".flammability", error)) {
        return false;
    }
    FireMaterialDef parsed;
    parsed.id = std::move(material.id);
    parsed.name = std::move(material.name);
    parsed.sprites = std::move(material.sprites);
    if (!ReadFloat(flam, "ignitionThreshold", 0.0f, 1.0f, 0.35f,
                   &parsed.flammability.ignitionThreshold, error,
                   where + ".flammability") ||
        !ReadFloat(flam, "burnSeconds", 1.0f, 7200.0f, 60.0f,
                   &parsed.flammability.burnSeconds, error,
                   where + ".flammability") ||
        !ReadFloat(flam, "smokePerSecond", 0.0f, 100.0f, 0.5f,
                   &parsed.flammability.smokePerSecond, error,
                   where + ".flammability") ||
        !ReadFloat(flam, "heatRadius", 0.0f, 64.0f, 2.0f,
                   &parsed.flammability.heatRadius, error,
                   where + ".flammability") ||
        !ReadDrops(node, &parsed.charredDrops, error, where)) {
        return false;
    }
    *out = std::move(parsed);
    return true;
}

}  // namespace

bool FireRegistry::LoadFile(const std::filesystem::path& path,
                            std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        if (error != nullptr) {
            *error = "无法打开火焰数据文件: " + path.string();
        }
        ZL_ERROR(kLogModule, "无法打开火焰数据文件: {}", path.string());
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return LoadFromString(buffer.str(), path.string(), error);
}

bool FireRegistry::LoadFromString(std::string_view text,
                                  const std::string& source,
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
    const std::unordered_set<std::string> rootKeys = {"format", "materials"};
    if (!CheckKeys(doc, rootKeys, "根节点", error)) {
        return fail(error != nullptr ? *error : "根节点字段非法");
    }
    if (!doc.contains("materials") || !doc["materials"].is_array() ||
        doc["materials"].empty()) {
        return fail("缺少非空 materials 数组");
    }

    std::vector<FireMaterialDef> materials;
    std::unordered_map<std::string, std::size_t> byId;
    std::unordered_map<std::string, std::size_t> bySprite;
    for (std::size_t i = 0; i < doc["materials"].size(); ++i) {
        const std::string where = "materials[" + std::to_string(i) + "]";
        FireMaterialDef material;
        if (!ParseMaterial(doc["materials"][i], error, where, &material)) {
            return fail(error != nullptr ? *error : where + " 非法");
        }
        if (byId.contains(material.id)) {
            return fail(where + " 材料 id 重复: " + material.id);
        }
        for (const std::string& sprite : material.sprites) {
            if (bySprite.contains(sprite)) {
                return fail(where + " sprite 重复: " + sprite);
            }
        }
        byId.emplace(material.id, materials.size());
        for (const std::string& sprite : material.sprites) {
            bySprite.emplace(sprite, materials.size());
        }
        materials.push_back(std::move(material));
    }

    m_materials = std::move(materials);
    m_byId = std::move(byId);
    m_bySprite = std::move(bySprite);
    return true;
}

const FireMaterialDef* FireRegistry::Find(std::string_view id) const {
    const auto it = m_byId.find(std::string(id));
    return it == m_byId.end() ? nullptr : &m_materials[it->second];
}

const FireMaterialDef* FireRegistry::FindBySprite(std::string_view sprite) const {
    const auto it = m_bySprite.find(std::string(sprite));
    return it == m_bySprite.end() ? nullptr : &m_materials[it->second];
}

const FireMaterialDef* FindFireMaterial(const FireRegistry& registry,
                                        const world::GridSquare& square,
                                        world::SquareLayer layer) {
    for (const world::SquareObject& object : square.Layer(layer)) {
        if (const FireMaterialDef* material =
                registry.FindBySprite(object.sprite);
            material != nullptr) {
            return material;
        }
    }
    return nullptr;
}

const FireMaterialDef* FindFireMaterial(const FireRegistry& registry,
                                        const world::GridSquare& square) {
    constexpr world::SquareLayer kLayers[] = {
        world::SquareLayer::Furniture, world::SquareLayer::Wall,
        world::SquareLayer::Floor, world::SquareLayer::Item};
    for (const world::SquareLayer layer : kLayers) {
        if (const FireMaterialDef* material =
                FindFireMaterial(registry, square, layer);
            material != nullptr) {
            return material;
        }
    }
    return nullptr;
}

}  // namespace zl::game
