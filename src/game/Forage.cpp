#include "game/Forage.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "game/Poi.h"
#include "items/LootTable.h"
#include "world/GridSquare.h"
#include "world/World.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "forage";
constexpr const char* kFormat = "forage-v1";
constexpr std::size_t kMaxIdLength = 64;
constexpr std::size_t kMaxNameLength = 64;

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

bool ReadString(const nlohmann::json& node, const char* key, std::string* out,
                std::string* error, const std::string& where) {
    if (!node.contains(key) || !node[key].is_string()) {
        if (error != nullptr) {
            *error = where + " 缺少字符串字段 '" + key + "'";
        }
        return false;
    }
    const std::string value = node[key].get<std::string>();
    if (value.empty() || value.size() > kMaxNameLength) {
        if (error != nullptr) {
            *error = where + " 字段 '" + key + "' 长度非法";
        }
        return false;
    }
    *out = value;
    return true;
}

bool ReadId(const nlohmann::json& node, const char* key, std::string* out,
            std::string* error, const std::string& where) {
    if (!ReadString(node, key, out, error, where)) {
        return false;
    }
    if (!ValidId(*out)) {
        if (error != nullptr) {
            *error = where + " id 非法（1~64，仅小写字母/数字/下划线/连字符）";
        }
        return false;
    }
    return true;
}

bool ReadStringArray(const nlohmann::json& node, const char* key, bool required,
                     std::vector<std::string>* out, std::string* error,
                     const std::string& where) {
    out->clear();
    if (!node.contains(key)) {
        if (required) {
            if (error != nullptr) {
                *error = where + " 缺少数组字段 '" + key + "'";
            }
            return false;
        }
        return true;
    }
    if (!node[key].is_array()) {
        if (error != nullptr) {
            *error = where + " 字段 '" + key + "' 必须是数组";
        }
        return false;
    }
    if (required && node[key].empty()) {
        if (error != nullptr) {
            *error = where + " 数组字段 '" + key + "' 不能为空";
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
        if (text.empty() || text.size() > kMaxIdLength) {
            if (error != nullptr) {
                *error = where + " 字段 '" + key + "' 含非法空串或超长值";
            }
            return false;
        }
        if (!seen.insert(text).second) {
            if (error != nullptr) {
                *error = where + " 字段 '" + key + "' 含重复值 '" + text + "'";
            }
            return false;
        }
        out->push_back(std::move(text));
    }
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

bool ReadPoiIds(const nlohmann::json& node, std::vector<std::string>* out,
                float* radius, std::string* error, const std::string& where) {
    if (!ReadStringArray(node, "poiIds", false, out, error, where)) {
        return false;
    }
    if (!ReadFloat(node, "poiRadius", 0.0f, 512.0f, 0.0f, radius, error,
                   where)) {
        return false;
    }
    if (!out->empty() && *radius <= 0.0f) {
        if (error != nullptr) {
            *error = where + " 配置 poiIds 时 poiRadius 必须大于 0";
        }
        return false;
    }
    return true;
}

bool ParseZone(const nlohmann::json& entry, const items::LootTableRegistry& loot,
               ForageZoneDef* out, std::string* error, const std::string& where) {
    const std::unordered_set<std::string> allowed = {
        "id",          "name",       "tableId",       "floors",
        "requiredNearbySprites", "excludedNearbySprites", "poiIds",
        "poiRadius",   "nearbyRadius", "density",     "spacing",
        "maxNodes",    "respawnMinutes", "searchSeconds", "toolDefId",
        "toolWear",    "survivalXp"};
    if (!CheckKeys(entry, allowed, where, error)) {
        return false;
    }
    ForageZoneDef zone;
    if (!ReadId(entry, "id", &zone.id, error, where) ||
        !ReadString(entry, "name", &zone.name, error, where)) {
        return false;
    }
    if (!ReadString(entry, "tableId", &zone.tableId, error, where)) {
        return false;
    }
    if (loot.Find(zone.tableId) == nullptr) {
        if (error != nullptr) {
            *error = where + " 引用了未知战利品表 '" + zone.tableId + "'";
        }
        return false;
    }
    if (!ReadStringArray(entry, "floors", true, &zone.floors, error, where) ||
        !ReadStringArray(entry, "requiredNearbySprites", false,
                         &zone.requiredNearbySprites, error, where) ||
        !ReadStringArray(entry, "excludedNearbySprites", false,
                         &zone.excludedNearbySprites, error, where) ||
        !ReadPoiIds(entry, &zone.poiIds, &zone.poiRadius, error, where)) {
        return false;
    }
    for (const std::string& required : zone.requiredNearbySprites) {
        if (std::find(zone.excludedNearbySprites.begin(),
                      zone.excludedNearbySprites.end(),
                      required) != zone.excludedNearbySprites.end()) {
            if (error != nullptr) {
                *error = where + " requiredNearbySprites 与 excludedNearbySprites 冲突";
            }
            return false;
        }
    }
    if (!ReadFloat(entry, "nearbyRadius", 0.0f, 16.0f, 4.0f,
                   &zone.nearbyRadius, error, where) ||
        !ReadFloat(entry, "density", 0.0f, 1.0f, 0.08f, &zone.density, error,
                   where) ||
        !ReadInt(entry, "spacing", 0, 64, 4, &zone.spacing, error, where) ||
        !ReadInt(entry, "maxNodes", 1, 1024, 64, &zone.maxNodes, error, where) ||
        !ReadInt(entry, "respawnMinutes", 0, 10080, 180,
                 &zone.respawnMinutes, error, where) ||
        !ReadFloat(entry, "searchSeconds", 0.5f, 30.0f, 2.5f,
                   &zone.searchSeconds, error, where) ||
        !ReadFloat(entry, "toolWear", 0.0f, 1.0f, 0.0f, &zone.toolWear,
                   error, where) ||
        !ReadFloat(entry, "survivalXp", 0.0f, 200.0f, 6.0f,
                   &zone.survivalXp, error, where)) {
        return false;
    }
    if (entry.contains("toolDefId")) {
        if (!entry["toolDefId"].is_string()) {
            if (error != nullptr) {
                *error = where + " 字段 'toolDefId' 必须是字符串";
            }
            return false;
        }
        zone.toolDefId = entry["toolDefId"].get<std::string>();
        if (!ValidId(zone.toolDefId)) {
            if (error != nullptr) {
                *error = where +
                         " toolDefId 非法（1~64，仅小写字母/数字/下划线/连字符）";
            }
            return false;
        }
        if (zone.toolWear <= 0.0f) {
            zone.toolWear = 0.01f;
        }
    } else if (zone.toolWear > 0.0f) {
        if (error != nullptr) {
            *error = where + " 配置 toolWear 时必须提供 toolDefId";
        }
        return false;
    }
    *out = std::move(zone);
    return true;
}

bool ParseSpot(const nlohmann::json& entry, const items::LootTableRegistry& loot,
               FishingSpotDef* out, std::string* error, const std::string& where) {
    const std::unordered_set<std::string> allowed = {
        "id", "name", "tableId", "trashTableId", "waterFloors", "poiIds",
        "poiRadius", "bankRadius", "density", "spacing", "maxSpots",
        "respawnMinutes", "waitSeconds", "biteWindowSeconds", "catchChance",
        "survivalXp", "rodDefId", "rodWear", "baitDefId", "baitCost"};
    if (!CheckKeys(entry, allowed, where, error)) {
        return false;
    }
    FishingSpotDef spot;
    if (!ReadId(entry, "id", &spot.id, error, where) ||
        !ReadString(entry, "name", &spot.name, error, where)) {
        return false;
    }
    if (!ReadString(entry, "tableId", &spot.tableId, error, where)) {
        return false;
    }
    if (loot.Find(spot.tableId) == nullptr) {
        if (error != nullptr) {
            *error = where + " 引用了未知战利品表 '" + spot.tableId + "'";
        }
        return false;
    }
    if (!ReadStringArray(entry, "waterFloors", true, &spot.waterFloors, error,
                         where) ||
        !ReadPoiIds(entry, &spot.poiIds, &spot.poiRadius, error, where) ||
        !ReadInt(entry, "bankRadius", 1, 8, 3, &spot.bankRadius, error, where) ||
        !ReadFloat(entry, "density", 0.0f, 1.0f, 0.3f, &spot.density, error,
                   where) ||
        !ReadInt(entry, "spacing", 0, 64, 4, &spot.spacing, error, where) ||
        !ReadInt(entry, "maxSpots", 1, 1024, 12, &spot.maxSpots, error, where) ||
        !ReadInt(entry, "respawnMinutes", 0, 10080, 360,
                 &spot.respawnMinutes, error, where) ||
        !ReadFloat(entry, "waitSeconds", 1.0f, 60.0f, 5.0f,
                   &spot.waitSeconds, error, where) ||
        !ReadFloat(entry, "biteWindowSeconds", 0.5f, 10.0f, 2.0f,
                   &spot.biteWindowSeconds, error, where) ||
        !ReadFloat(entry, "catchChance", 0.05f, 0.95f, 0.6f,
                   &spot.catchChance, error, where) ||
        !ReadFloat(entry, "survivalXp", 0.0f, 200.0f, 10.0f,
                   &spot.survivalXp, error, where) ||
        !ReadFloat(entry, "rodWear", 0.0f, 1.0f, 0.02f, &spot.rodWear,
                   error, where) ||
        !ReadInt(entry, "baitCost", 0, 10, 1, &spot.baitCost, error, where)) {
        return false;
    }
    if (entry.contains("trashTableId")) {
        if (!entry["trashTableId"].is_string()) {
            if (error != nullptr) {
                *error = where + " 字段 'trashTableId' 必须是字符串";
            }
            return false;
        }
        spot.trashTableId = entry["trashTableId"].get<std::string>();
        if (spot.trashTableId.empty() ||
            spot.trashTableId.size() > kMaxNameLength) {
            if (error != nullptr) {
                *error = where + " 字段 'trashTableId' 长度非法";
            }
            return false;
        }
        if (loot.Find(spot.trashTableId) == nullptr) {
            if (error != nullptr) {
                *error = where + " 引用了未知垃圾战利品表 '" + spot.trashTableId + "'";
            }
            return false;
        }
    } else {
        spot.trashTableId = spot.tableId;
    }
    auto readItemId = [&](const char* key, std::string* value) {
        if (!entry.contains(key)) {
            return true;
        }
        if (!entry[key].is_string()) {
            if (error != nullptr) {
                *error = where + " 字段 '" + key + "' 必须是字符串";
            }
            return false;
        }
        *value = entry[key].get<std::string>();
        if (value->empty()) {
            value->clear();
            return true;
        }
        if (!ValidId(*value)) {
            if (error != nullptr) {
                *error = where + " 字段 '" + key + "' 非法（1~64，仅小写字母/数字/下划线/连字符）";
            }
            return false;
        }
        return true;
    };
    if (!readItemId("rodDefId", &spot.rodDefId) ||
        !readItemId("baitDefId", &spot.baitDefId)) {
        return false;
    }
    if (spot.rodDefId.empty()) {
        spot.rodWear = 0.0f;
    } else if (spot.rodWear <= 0.0f) {
        spot.rodWear = 0.01f;
    }
    if (spot.baitDefId.empty()) {
        spot.baitCost = 0;
    } else if (spot.baitCost <= 0) {
        if (error != nullptr) {
            *error = where + " 配置 baitDefId 时 baitCost 必须大于 0";
        }
        return false;
    }
    if (spot.baitDefId.empty() && spot.baitCost != 0) {
        if (error != nullptr) {
            *error = where + " 未配置 baitDefId 时 baitCost 必须为 0";
        }
        return false;
    }
    *out = std::move(spot);
    return true;
}

struct ParsedForage {
    std::uint64_t seed = 20261029u;
    std::vector<ForageZoneDef> zones;
    std::vector<FishingSpotDef> spots;
};

bool ReadDoc(std::string_view text, const std::string& source,
             const items::LootTableRegistry& loot, ParsedForage* out,
             std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = source + ": " + message;
        }
        ZL_ERROR(kLogModule, "{}: {}", source, message);
        return false;
    };

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(text);
    } catch (const std::exception& e) {
        return fail("JSON 解析失败 — " + std::string(e.what()));
    }
    if (!doc.is_object()) {
        return fail("根节点必须是对象");
    }
    if (!doc.contains("format") || !doc["format"].is_string() ||
        doc["format"].get<std::string>() != kFormat) {
        return fail(std::string("format 必须是 ") + kFormat);
    }
    const std::unordered_set<std::string> rootKeys = {
        "format", "seed", "forageZones", "fishingSpots"};
    if (!CheckKeys(doc, rootKeys, "根节点", error)) {
        return fail(error != nullptr ? *error : "根节点字段非法");
    }
    if (!doc.contains("forageZones") || !doc["forageZones"].is_array()) {
        return fail("缺少 forageZones 数组");
    }
    if (!doc.contains("fishingSpots") || !doc["fishingSpots"].is_array()) {
        return fail("缺少 fishingSpots 数组");
    }
    if (doc["forageZones"].empty() && doc["fishingSpots"].empty()) {
        return fail("forageZones 与 fishingSpots 不能同时为空");
    }

    std::uint64_t seed = 20261029u;
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

    std::vector<ForageZoneDef> zones;
    std::unordered_set<std::string> zoneIds;
    for (std::size_t i = 0; i < doc["forageZones"].size(); ++i) {
        const std::string where = "forageZones[" + std::to_string(i) + "]";
        if (!doc["forageZones"][i].is_object()) {
            return fail(where + " 必须是对象");
        }
        ForageZoneDef zone;
        if (!ParseZone(doc["forageZones"][i], loot, &zone, error, where)) {
            return fail(error != nullptr ? *error : where + " 非法");
        }
        if (!zoneIds.insert(zone.id).second) {
            return fail(where + " id 重复: " + zone.id);
        }
        zones.push_back(std::move(zone));
    }

    std::vector<FishingSpotDef> spots;
    std::unordered_set<std::string> spotIds;
    for (std::size_t i = 0; i < doc["fishingSpots"].size(); ++i) {
        const std::string where = "fishingSpots[" + std::to_string(i) + "]";
        if (!doc["fishingSpots"][i].is_object()) {
            return fail(where + " 必须是对象");
        }
        FishingSpotDef spot;
        if (!ParseSpot(doc["fishingSpots"][i], loot, &spot, error, where)) {
            return fail(error != nullptr ? *error : where + " 非法");
        }
        if (!spotIds.insert(spot.id).second) {
            return fail(where + " id 重复: " + spot.id);
        }
        spots.push_back(std::move(spot));
    }

    out->seed = seed;
    out->zones = std::move(zones);
    out->spots = std::move(spots);
    return true;
}

std::uint64_t Mix(std::uint64_t hash, std::uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ull;
    return hash;
}

std::uint64_t HashSite(std::uint64_t seed, std::string_view id, int x, int y,
                       int z) {
    std::uint64_t hash = 1469598103934665603ull ^ seed;
    for (const char c : id) {
        hash = Mix(hash, static_cast<std::uint8_t>(c));
    }
    hash = Mix(hash, static_cast<std::uint32_t>(x));
    hash = Mix(hash, static_cast<std::uint32_t>(y));
    hash = Mix(hash, static_cast<std::uint32_t>(z));
    hash += 0x9E3779B97F4A7C15ull;
    hash = (hash ^ (hash >> 30U)) * 0xBF58476D1CE4E5B9ull;
    hash = (hash ^ (hash >> 27U)) * 0x94D049BB133111EBull;
    return hash ^ (hash >> 31U);
}

double UnitHash(std::uint64_t value) {
    return static_cast<double>(value >> 11U) * (1.0 / 9007199254740992.0);
}

bool Contains(const std::vector<std::string>& values, std::string_view value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool IsWalkable(const world::GridSquare& square) {
    if (!square.HasFloor()) {
        return false;
    }
    for (int layer = 0; layer < world::kSquareLayerCount; ++layer) {
        for (const world::SquareObject& object :
             square.Layer(static_cast<world::SquareLayer>(layer))) {
            if (object.blocksMovement) {
                return false;
            }
        }
    }
    return true;
}

bool HasSpriteWithin(const world::World& world, int x, int y, int z,
                     const std::vector<std::string>& sprites, float radius) {
    if (sprites.empty()) {
        return false;
    }
    const int extent = static_cast<int>(std::ceil(radius));
    const int radiusSq = extent * extent;
    for (int dy = -extent; dy <= extent; ++dy) {
        for (int dx = -extent; dx <= extent; ++dx) {
            if (dx * dx + dy * dy > radiusSq) {
                continue;
            }
            const world::GridSquare* square = world.GetSquare(x + dx, y + dy, z);
            if (square == nullptr) {
                continue;
            }
            for (int layer = 0; layer < world::kSquareLayerCount; ++layer) {
                for (const world::SquareObject& object :
                     square->Layer(static_cast<world::SquareLayer>(layer))) {
                    if (Contains(sprites, object.sprite)) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool PoiAllows(const PoiConfig& pois, const std::vector<std::string>& ids,
               float radius, int x, int y) {
    if (ids.empty()) {
        return true;
    }
    const float radiusSq = radius * radius;
    for (const std::string& id : ids) {
        const PoiDef* poi = pois.Find(id);
        if (poi == nullptr) {
            continue;
        }
        const float dx = poi->markerX - static_cast<float>(x);
        const float dy = poi->markerY - static_cast<float>(y);
        if (dx * dx + dy * dy <= radiusSq) {
            return true;
        }
    }
    return false;
}

bool ZoneAllows(const ForageZoneDef& zone, const PoiConfig& pois,
                const world::World& world, const world::GridSquare& square,
                int x, int y) {
    const world::SquareObject* floor = square.Floor();
    if (floor == nullptr || !Contains(zone.floors, floor->sprite) ||
        !IsWalkable(square) || square.RoomId() >= 0) {
        return false;
    }
    if (!zone.requiredNearbySprites.empty() &&
        !HasSpriteWithin(world, x, y, 0, zone.requiredNearbySprites,
                         zone.nearbyRadius)) {
        return false;
    }
    if (HasSpriteWithin(world, x, y, 0, zone.excludedNearbySprites,
                        zone.nearbyRadius)) {
        return false;
    }
    return PoiAllows(pois, zone.poiIds, zone.poiRadius, x, y);
}

bool FarEnough(int x, int y, const std::vector<ForageNode>& nodes, int spacing) {
    if (spacing <= 0) {
        return true;
    }
    const int spacingSq = spacing * spacing;
    for (const ForageNode& node : nodes) {
        const int dx = node.x - x;
        const int dy = node.y - y;
        if (dx * dx + dy * dy < spacingSq) {
            return false;
        }
    }
    return true;
}

bool WaterAt(const world::World& world, int x, int y,
             const std::vector<std::string>& floors) {
    const world::GridSquare* square = world.GetSquare(x, y, 0);
    if (square == nullptr) {
        return false;
    }
    const world::SquareObject* floor = square->Floor();
    return floor != nullptr && Contains(floors, floor->sprite);
}

}  // namespace

bool ForageRegistry::LoadFile(const std::filesystem::path& path,
                              const items::LootTableRegistry& lootTables,
                              std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        if (error != nullptr) {
            *error = "无法打开觅食数据文件: " + path.string();
        }
        ZL_ERROR(kLogModule, "无法打开觅食数据文件: {}", path.string());
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return LoadFromString(buffer.str(), path.string(), lootTables, error);
}

bool ForageRegistry::LoadFromString(
    std::string_view text, const std::string& source,
    const items::LootTableRegistry& lootTables, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    ParsedForage parsed;
    if (!ReadDoc(text, source, lootTables, &parsed, error)) {
        return false;
    }
    ForageRegistry result;
    result.m_seed = parsed.seed;
    for (ForageZoneDef& zone : parsed.zones) {
        const std::string id = zone.id;
        result.m_zoneById.emplace(id, result.m_zones.size());
        result.m_zones.push_back(std::move(zone));
    }
    for (FishingSpotDef& spot : parsed.spots) {
        const std::string id = spot.id;
        result.m_spotById.emplace(id, result.m_spots.size());
        result.m_spots.push_back(std::move(spot));
    }
    *this = std::move(result);
    return true;
}

bool ForageRegistry::MergeFile(const std::filesystem::path& path,
                               const items::LootTableRegistry& lootTables,
                               std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        if (error != nullptr) {
            *error = "无法打开觅食数据文件: " + path.string();
        }
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return MergeFromString(buffer.str(), path.string(), lootTables, error);
}

bool ForageRegistry::MergeFromString(
    std::string_view text, const std::string& source,
    const items::LootTableRegistry& lootTables, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    ForageRegistry parsed;
    if (!parsed.LoadFromString(text, source, lootTables, error)) {
        return false;
    }
    for (const ForageZoneDef& zone : parsed.m_zones) {
        if (m_zoneById.contains(zone.id)) {
            if (error != nullptr) {
                *error = source + ": 觅食区域 id 冲突: " + zone.id;
            }
            return false;
        }
    }
    for (const FishingSpotDef& spot : parsed.m_spots) {
        if (m_spotById.contains(spot.id)) {
            if (error != nullptr) {
                *error = source + ": 钓鱼点 id 冲突: " + spot.id;
            }
            return false;
        }
    }
    for (ForageZoneDef& zone : parsed.m_zones) {
        const std::string id = zone.id;
        m_zoneById.emplace(id, m_zones.size());
        m_zones.push_back(std::move(zone));
    }
    for (FishingSpotDef& spot : parsed.m_spots) {
        const std::string id = spot.id;
        m_spotById.emplace(id, m_spots.size());
        m_spots.push_back(std::move(spot));
    }
    return true;
}

const ForageZoneDef* ForageRegistry::FindZone(std::string_view id) const {
    const auto it = m_zoneById.find(std::string(id));
    return it == m_zoneById.end() ? nullptr : &m_zones[it->second];
}

const FishingSpotDef* ForageRegistry::FindSpot(std::string_view id) const {
    const auto it = m_spotById.find(std::string(id));
    return it == m_spotById.end() ? nullptr : &m_spots[it->second];
}

std::vector<ForageNode> PlanForageNodes(const ForageRegistry& registry,
                                        const world::World& world,
                                        const PoiConfig& pois,
                                        const ForageBounds& bounds) {
    std::vector<ForageNode> nodes;
    if (bounds.maxX < bounds.minX || bounds.maxY < bounds.minY) {
        return nodes;
    }
    std::unordered_set<std::uint64_t> occupied;
    for (const ForageZoneDef& zone : registry.Zones()) {
        if (zone.maxNodes <= 0 || zone.density <= 0.0f) {
            continue;
        }
        int placed = 0;
        for (int y = bounds.minY; y <= bounds.maxY && placed < zone.maxNodes; ++y) {
            for (int x = bounds.minX; x <= bounds.maxX && placed < zone.maxNodes;
                 ++x) {
                const world::GridSquare* square = world.GetSquare(x, y, 0);
                if (square == nullptr || !ZoneAllows(zone, pois, world, *square, x, y)) {
                    continue;
                }
                const double roll =
                    UnitHash(HashSite(registry.Seed(), zone.id, x, y, 0));
                if (roll >= static_cast<double>(zone.density)) {
                    continue;
                }
                if (!FarEnough(x, y, nodes, zone.spacing)) {
                    continue;
                }
                const std::uint64_t key =
                    (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x))
                     << 32U) |
                    static_cast<std::uint32_t>(y);
                if (!occupied.insert(key).second) {
                    continue;
                }
                nodes.push_back(ForageNode{zone.id, zone.tableId, x, y, 0});
                ++placed;
            }
        }
    }
    return nodes;
}

bool FishingSpotWaterValid(const FishingSpotDef& def, const world::World& world,
                           const FishingSpot& spot) {
    return WaterAt(world, spot.waterX, spot.waterY, def.waterFloors);
}

std::vector<FishingSpot> PlanFishingSpots(const ForageRegistry& registry,
                                          const world::World& world,
                                          const PoiConfig& pois,
                                          const ForageBounds& bounds) {
    std::vector<FishingSpot> spots;
    if (bounds.maxX < bounds.minX || bounds.maxY < bounds.minY) {
        return spots;
    }
    for (const FishingSpotDef& def : registry.Spots()) {
        int placed = 0;
        for (int wy = bounds.minY; wy <= bounds.maxY && placed < def.maxSpots; ++wy) {
            for (int wx = bounds.minX; wx <= bounds.maxX && placed < def.maxSpots;
                 ++wx) {
                if (!WaterAt(world, wx, wy, def.waterFloors)) {
                    continue;
                }
                int bestX = 0;
                int bestY = 0;
                std::uint64_t bestHash = std::numeric_limits<std::uint64_t>::max();
                bool found = false;
                for (int dy = -def.bankRadius; dy <= def.bankRadius; ++dy) {
                    for (int dx = -def.bankRadius; dx <= def.bankRadius; ++dx) {
                        if (dx == 0 && dy == 0) {
                            continue;
                        }
                        if (dx * dx + dy * dy > def.bankRadius * def.bankRadius) {
                            continue;
                        }
                        const int bx = wx + dx;
                        const int by = wy + dy;
                        const world::GridSquare* square =
                            world.GetSquare(bx, by, 0);
                        if (square == nullptr || !IsWalkable(*square) ||
                            square->RoomId() >= 0 ||
                            WaterAt(world, bx, by, def.waterFloors) ||
                            !PoiAllows(pois, def.poiIds, def.poiRadius, bx, by)) {
                            continue;
                        }
                        const std::uint64_t hash =
                            HashSite(registry.Seed(), def.id, bx, by, wy * 31 + wx);
                        if (UnitHash(hash) >= static_cast<double>(def.density)) {
                            continue;
                        }
                        if (!found || hash < bestHash) {
                            found = true;
                            bestX = bx;
                            bestY = by;
                            bestHash = hash;
                        }
                    }
                }
                if (!found) {
                    continue;
                }
                const bool duplicate = std::any_of(
                    spots.begin(), spots.end(), [&](const FishingSpot& spot) {
                        return spot.x == bestX && spot.y == bestY;
                    });
                if (duplicate) {
                    continue;
                }
                bool farEnough = true;
                for (const FishingSpot& spot : spots) {
                    const int dx = spot.x - bestX;
                    const int dy = spot.y - bestY;
                    if (def.spacing > 0 &&
                        dx * dx + dy * dy < def.spacing * def.spacing) {
                        farEnough = false;
                        break;
                    }
                }
                if (!farEnough) {
                    continue;
                }
                spots.push_back(FishingSpot{def.id, def.tableId, bestX, bestY, 0,
                                            wx, wy, 0});
                ++placed;
            }
        }
    }
    return spots;
}

}  // namespace zl::game
