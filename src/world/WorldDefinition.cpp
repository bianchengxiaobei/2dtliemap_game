#include "world/WorldDefinition.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <numeric>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Constants.h"
#include "core/Log.h"
#include "world/MapLoader.h"

namespace zl::world {
namespace {

constexpr const char* kLogModule = "world";
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

class MapHasher {
public:
    void Bytes(const void* data, std::size_t size) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            m_hash ^= bytes[i];
            m_hash *= kFnvPrime;
        }
    }

    void U32(std::uint32_t value) {
        const std::uint8_t bytes[4] = {
            static_cast<std::uint8_t>(value & 0xffu),
            static_cast<std::uint8_t>((value >> 8u) & 0xffu),
            static_cast<std::uint8_t>((value >> 16u) & 0xffu),
            static_cast<std::uint8_t>((value >> 24u) & 0xffu),
        };
        Bytes(bytes, sizeof(bytes));
    }

    void U64(std::uint64_t value) {
        const std::uint8_t bytes[8] = {
            static_cast<std::uint8_t>(value & 0xffu),
            static_cast<std::uint8_t>((value >> 8u) & 0xffu),
            static_cast<std::uint8_t>((value >> 16u) & 0xffu),
            static_cast<std::uint8_t>((value >> 24u) & 0xffu),
            static_cast<std::uint8_t>((value >> 32u) & 0xffu),
            static_cast<std::uint8_t>((value >> 40u) & 0xffu),
            static_cast<std::uint8_t>((value >> 48u) & 0xffu),
            static_cast<std::uint8_t>((value >> 56u) & 0xffu),
        };
        Bytes(bytes, sizeof(bytes));
    }

    void String(std::string_view value) {
        U32(static_cast<std::uint32_t>(value.size()));
        if (!value.empty()) {
            Bytes(value.data(), value.size());
        }
    }

    std::uint64_t Value() const { return m_hash; }

private:
    std::uint64_t m_hash = kFnvOffset;
};

bool Fail(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = message;
    }
    ZL_ERROR(kLogModule, "{}", message);
    return false;
}

void WarnUnknownFields(const nlohmann::json& node,
                       std::initializer_list<std::string_view> known,
                       const std::string& context) {
    for (auto it = node.begin(); it != node.end(); ++it) {
        const bool found = std::any_of(known.begin(), known.end(),
                                       [&](std::string_view key) { return it.key() == key; });
        if (!found) {
            ZL_WARN(kLogModule, "world.json {} 未知字段已忽略: {}", context, it.key());
        }
    }
}

bool ReadStringField(const nlohmann::json& node, const char* key, const std::string& context,
                     std::string* out, std::string* error) {
    if (!node.contains(key)) {
        return Fail(error, "world.json " + context + " 缺少 " + key);
    }
    if (!node[key].is_string()) {
        return Fail(error, "world.json " + context + " 的 " + key + " 必须是字符串");
    }
    *out = node[key].get<std::string>();
    if (out->empty()) {
        return Fail(error, "world.json " + context + " 的 " + key + " 不能为空");
    }
    return true;
}

bool ReadOptionalStringField(const nlohmann::json& node, const char* key,
                             const std::string& context, std::string* out,
                             std::string* error) {
    if (!node.contains(key)) {
        out->clear();
        return true;
    }
    if (!node[key].is_string()) {
        return Fail(error, "world.json " + context + " 的 " + key + " 必须是字符串");
    }
    *out = node[key].get<std::string>();
    return true;
}

bool ReadDoubleField(const nlohmann::json& node, const char* key, const std::string& context,
                     double* out, std::string* error) {
    if (!node.contains(key)) {
        return Fail(error, "world.json " + context + " 缺少 " + key);
    }
    if (!node[key].is_number()) {
        return Fail(error, "world.json " + context + " 的 " + key + " 必须是数字");
    }
    const double value = node[key].get<double>();
    if (!std::isfinite(value)) {
        return Fail(error, "world.json " + context + " 的 " + key + " 必须是有限数字");
    }
    *out = value;
    return true;
}

bool ReadOptionalDoubleField(const nlohmann::json& node, const char* key,
                             const std::string& context, double* out,
                             std::string* error) {
    if (!node.contains(key)) {
        return true;
    }
    return ReadDoubleField(node, key, context, out, error);
}

bool ReadIntegerValue(const nlohmann::json& value, const std::string& context,
                      std::int64_t* out, std::string* error) {
    if (value.is_number_unsigned()) {
        const std::uint64_t unsignedValue = value.get<std::uint64_t>();
        if (unsignedValue > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return Fail(error, "world.json " + context + " 超出 int64 范围");
        }
        *out = static_cast<std::int64_t>(unsignedValue);
        return true;
    }
    if (!value.is_number_integer()) {
        return Fail(error, "world.json " + context + " 必须是整数");
    }
    *out = value.get<std::int64_t>();
    return true;
}

bool ReadIntField(const nlohmann::json& node, const char* key, const std::string& context,
                  int lo, int hi, int* out, std::string* error) {
    if (!node.contains(key)) {
        return Fail(error, "world.json " + context + " 缺少 " + key);
    }
    std::int64_t value = 0;
    if (!ReadIntegerValue(node[key], context + "." + key, &value, error)) {
        return false;
    }
    if (value < lo || value > hi) {
        return Fail(error, "world.json " + context + " 的 " + key + " 超出范围 [" +
                               std::to_string(lo) + ", " + std::to_string(hi) + "]");
    }
    *out = static_cast<int>(value);
    return true;
}

bool ReadOptionalBoolField(const nlohmann::json& node, const char* key,
                           const std::string& context, bool* out,
                           std::string* error) {
    if (!node.contains(key)) {
        return true;
    }
    if (!node[key].is_boolean()) {
        return Fail(error, "world.json " + context + " 的 " + key + " 必须是布尔值");
    }
    *out = node[key].get<bool>();
    return true;
}

bool ReadBounds(const nlohmann::json& node, const std::string& context, WorldBounds* out,
                std::string* error) {
    if (!node.is_object()) {
        return Fail(error, "world.json " + context + " 必须是对象");
    }
    if (!ReadIntField(node, "chunkX0", context, std::numeric_limits<int>::min() / 2,
                      std::numeric_limits<int>::max() / 2, &out->chunkX0, error) ||
        !ReadIntField(node, "chunkY0", context, std::numeric_limits<int>::min() / 2,
                      std::numeric_limits<int>::max() / 2, &out->chunkY0, error) ||
        !ReadIntField(node, "chunksX", context, 1, std::numeric_limits<int>::max() / 2,
                      &out->chunksX, error) ||
        !ReadIntField(node, "chunksY", context, 1, std::numeric_limits<int>::max() / 2,
                      &out->chunksY, error)) {
        return false;
    }
    WarnUnknownFields(node, {"chunkX0", "chunkY0", "chunksX", "chunksY"}, context);
    return true;
}

bool ReadRect(const nlohmann::json& node, const std::string& context, WorldRect* out,
              std::string* error) {
    if (!node.is_object()) {
        return Fail(error, "world.json " + context + " 必须是对象");
    }
    if (!ReadIntField(node, "chunkX", context, std::numeric_limits<int>::min() / 2,
                      std::numeric_limits<int>::max() / 2, &out->chunkX, error) ||
        !ReadIntField(node, "chunkY", context, std::numeric_limits<int>::min() / 2,
                      std::numeric_limits<int>::max() / 2, &out->chunkY, error) ||
        !ReadIntField(node, "chunksW", context, 1, std::numeric_limits<int>::max() / 2,
                      &out->chunksW, error) ||
        !ReadIntField(node, "chunksH", context, 1, std::numeric_limits<int>::max() / 2,
                      &out->chunksH, error)) {
        return false;
    }
    WarnUnknownFields(node, {"chunkX", "chunkY", "chunksW", "chunksH"}, context);
    return true;
}

bool ReadPoiIds(const nlohmann::json& node, const std::string& context,
                std::vector<std::string>* out, std::string* error) {
    if (!node.contains("poiIds")) {
        return true;
    }
    if (!node["poiIds"].is_array()) {
        return Fail(error, "world.json " + context + " 的 poiIds 必须是数组");
    }
    std::unordered_set<std::string> seen;
    for (std::size_t i = 0; i < node["poiIds"].size(); ++i) {
        const nlohmann::json& entry = node["poiIds"][i];
        if (!entry.is_string() || entry.get<std::string>().empty()) {
            return Fail(error, "world.json " + context + " 的 poiIds[" + std::to_string(i) +
                                   "] 必须是非空字符串");
        }
        std::string id = entry.get<std::string>();
        if (!seen.insert(id).second) {
            return Fail(error, "world.json " + context + " 的 poiIds 重复: " + id);
        }
        out->push_back(std::move(id));
    }
    return true;
}

bool IsValidTemplateId(std::string_view id) {
    if (id.empty() || id.size() > 128 || id == "." || id == "..") {
        return false;
    }
    for (const char ch : id) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (!std::isalnum(c) && ch != '_' && ch != '-' && ch != '.') {
            return false;
        }
    }
    return true;
}

bool RectInsideBounds(const WorldRect& rect, const WorldBounds& bounds) {
    const std::int64_t x0 = rect.chunkX;
    const std::int64_t y0 = rect.chunkY;
    const std::int64_t x1 = x0 + rect.chunksW;
    const std::int64_t y1 = y0 + rect.chunksH;
    const std::int64_t bx0 = bounds.chunkX0;
    const std::int64_t by0 = bounds.chunkY0;
    const std::int64_t bx1 = bx0 + bounds.chunksX;
    const std::int64_t by1 = by0 + bounds.chunksY;
    return x0 >= bx0 && y0 >= by0 && x1 <= bx1 && y1 <= by1;
}

bool RectsOverlap(const WorldRect& a, const WorldRect& b) {
    const std::int64_t ax1 = static_cast<std::int64_t>(a.chunkX) + a.chunksW;
    const std::int64_t ay1 = static_cast<std::int64_t>(a.chunkY) + a.chunksH;
    const std::int64_t bx1 = static_cast<std::int64_t>(b.chunkX) + b.chunksW;
    const std::int64_t by1 = static_cast<std::int64_t>(b.chunkY) + b.chunksH;
    return static_cast<std::int64_t>(a.chunkX) < bx1 &&
           static_cast<std::int64_t>(b.chunkX) < ax1 &&
           static_cast<std::int64_t>(a.chunkY) < by1 &&
           static_cast<std::int64_t>(b.chunkY) < ay1;
}

struct WorldExtent {
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
};

WorldExtent ExtentOf(const WorldBounds& bounds) {
    const double minX = static_cast<double>(bounds.chunkX0) * core::kChunkSize;
    const double minY = static_cast<double>(bounds.chunkY0) * core::kChunkSize;
    const double maxX =
        (static_cast<double>(bounds.chunkX0) + static_cast<double>(bounds.chunksX)) *
        core::kChunkSize;
    const double maxY =
        (static_cast<double>(bounds.chunkY0) + static_cast<double>(bounds.chunksY)) *
        core::kChunkSize;
    return {minX, minY, maxX, maxY};
}

bool ParseRegion(const nlohmann::json& node, const std::string& context, WorldRegion* out,
                 std::string* error) {
    if (!node.is_object()) {
        return Fail(error, "world.json " + context + " 必须是对象");
    }
    if (!ReadStringField(node, "id", context, &out->id, error)) {
        return false;
    }
    if (!node.contains("rect")) {
        return Fail(error, "world.json " + context + " 缺少 rect");
    }
    if (!ReadRect(node["rect"], context + ".rect", &out->rect, error)) {
        return false;
    }
    if (!ReadStringField(node, "biome", context, &out->biome, error) ||
        !ReadPoiIds(node, context, &out->poiIds, error)) {
        return false;
    }
    WarnUnknownFields(node, {"id", "rect", "biome", "poiIds"}, context);
    return true;
}

bool ParseNode(const nlohmann::json& node, const std::string& context, WorldNode* out,
               std::string* error) {
    if (!node.is_object()) {
        return Fail(error, "world.json " + context + " 必须是对象");
    }
    if (!ReadStringField(node, "id", context, &out->id, error) ||
        !ReadStringField(node, "kind", context, &out->kind, error) ||
        !ReadDoubleField(node, "x", context, &out->x, error) ||
        !ReadDoubleField(node, "y", context, &out->y, error) ||
        !ReadOptionalBoolField(node, "isolated", context, &out->isolated, error)) {
        return false;
    }
    WarnUnknownFields(node, {"id", "kind", "x", "y", "isolated"}, context);
    return true;
}

bool ParseEdge(const nlohmann::json& node, const std::string& context, WorldEdge* out,
               std::string* error) {
    if (!node.is_object()) {
        return Fail(error, "world.json " + context + " 必须是对象");
    }
    if (!ReadStringField(node, "a", context, &out->a, error) ||
        !ReadStringField(node, "b", context, &out->b, error) ||
        !ReadStringField(node, "kind", context, &out->kind, error) ||
        !ReadDoubleField(node, "width", context, &out->width, error) ||
        !ReadOptionalBoolField(node, "vehicle", context, &out->vehiclePassable, error)) {
        return false;
    }
    if (out->width <= 0.0) {
        return Fail(error, "world.json " + context + " 的 width 必须大于 0");
    }
    WarnUnknownFields(node, {"a", "b", "kind", "width", "vehicle"}, context);
    return true;
}

bool ParsePoi(const nlohmann::json& node, const std::string& context, const std::string& mapDir,
              const WorldExtent& extent, WorldPoi* out, std::string* error) {
    if (!node.is_object()) {
        return Fail(error, "world.json " + context + " 必须是对象");
    }
    if (!ReadStringField(node, "id", context, &out->id, error) ||
        !ReadStringField(node, "template", context, &out->templateId, error) ||
        !ReadDoubleField(node, "x", context, &out->x, error) ||
        !ReadDoubleField(node, "y", context, &out->y, error) ||
        !ReadDoubleField(node, "radius", context, &out->radius, error)) {
        return false;
    }
    if (out->radius <= 0.0) {
        return Fail(error, "world.json " + context + " 的 radius 必须大于 0");
    }
    if (!node.contains("threat")) {
        out->threat = 0;
    } else if (!ReadIntField(node, "threat", context, 0, 5, &out->threat, error)) {
        return false;
    }
    if (!ReadOptionalStringField(node, "lootProfile", context, &out->lootProfile, error)) {
        return false;
    }
    if (out->x - out->radius < extent.minX || out->x + out->radius > extent.maxX ||
        out->y - out->radius < extent.minY || out->y + out->radius > extent.maxY) {
        return Fail(error, "world.json " + context + " 的 POI 越界: " + out->id);
    }
    if (!IsValidTemplateId(out->templateId)) {
        return Fail(error, "world.json " + context + " 的模板名非法: " + out->templateId);
    }
    const std::filesystem::path templatePath =
        std::filesystem::path(mapDir) / "templates" / (out->templateId + ".json");
    std::error_code ec;
    if (!std::filesystem::is_regular_file(templatePath, ec) || ec) {
        return Fail(error, "world.json " + context + " 的 POI 模板不存在: " + out->templateId);
    }
    WarnUnknownFields(node, {"id", "template", "x", "y", "radius", "threat", "lootProfile"},
                      context);
    return true;
}

bool ParseSpawn(const nlohmann::json& node, WorldSpawn* out, std::string* error) {
    if (!node.is_object()) {
        return Fail(error, "world.json spawn 必须是对象");
    }
    if (!ReadOptionalDoubleField(node, "defaultZombieDensity", "spawn",
                                 &out->defaultZombieDensity, error)) {
        return false;
    }
    if (out->defaultZombieDensity < 0.0) {
        return Fail(error, "world.json spawn.defaultZombieDensity 不能为负");
    }
    if (node.contains("biomes")) {
        if (!node["biomes"].is_object()) {
            return Fail(error, "world.json spawn.biomes 必须是对象");
        }
        for (auto it = node["biomes"].begin(); it != node["biomes"].end(); ++it) {
            if (!it.value().is_number()) {
                return Fail(error, "world.json spawn.biomes." + it.key() + " 必须是数字");
            }
            const double density = it.value().get<double>();
            if (!std::isfinite(density) || density < 0.0) {
                return Fail(error, "world.json spawn.biomes." + it.key() +
                                       " 必须是非负有限数字");
            }
            out->biomes[it.key()] = density;
        }
    }
    WarnUnknownFields(node, {"defaultZombieDensity", "biomes"}, "spawn");
    return true;
}

class DisjointSet {
public:
    explicit DisjointSet(std::size_t size) : m_parent(size) {
        std::iota(m_parent.begin(), m_parent.end(), 0);
    }

    std::size_t Find(std::size_t value) {
        if (m_parent[value] != value) {
            m_parent[value] = Find(m_parent[value]);
        }
        return m_parent[value];
    }

    void Unite(std::size_t a, std::size_t b) {
        const std::size_t rootA = Find(a);
        const std::size_t rootB = Find(b);
        if (rootA != rootB) {
            m_parent[rootB] = rootA;
        }
    }

private:
    std::vector<std::size_t> m_parent;
};

bool ValidateGraph(const std::vector<WorldNode>& nodes, const std::vector<WorldEdge>& edges,
                   std::string* error) {
    std::unordered_map<std::string, std::size_t> indices;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (!indices.emplace(nodes[i].id, i).second) {
            return Fail(error, "world.json nodes 的 id 重复: " + nodes[i].id);
        }
    }

    DisjointSet sets(nodes.size());
    for (const WorldEdge& edge : edges) {
        const auto a = indices.find(edge.a);
        const auto b = indices.find(edge.b);
        if (a == indices.end() || b == indices.end()) {
            return Fail(error, "world.json edges 引用了不存在的节点: " + edge.a + " -> " +
                                   edge.b);
        }
        if (edge.a == edge.b) {
            return Fail(error, "world.json edges 出现自环: " + edge.a);
        }
        if (nodes[a->second].isolated || nodes[b->second].isolated) {
            return Fail(error, "world.json isolated 节点不能出现在 edges 中: " + edge.a + " -> " +
                                   edge.b);
        }
        sets.Unite(a->second, b->second);
    }

    std::optional<std::size_t> root;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].isolated) {
            continue;
        }
        if (!root.has_value()) {
            root = sets.Find(i);
        } else if (sets.Find(i) != *root) {
            return Fail(error, "world.json 道路图不连通，孤立节点: " + nodes[i].id);
        }
    }
    return true;
}

bool ComputeMapHashImpl(const std::filesystem::path& root, std::uint64_t* hash,
                        std::string* error) {
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec) || ec) {
        return Fail(error, "地图目录不存在: " + root.string());
    }

    std::vector<std::pair<std::string, std::filesystem::path>> files;
    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        if (ec) {
            return Fail(error, "遍历地图目录失败: " + ec.message());
        }
        if (iterator->is_regular_file(ec)) {
            if (ec) {
                return Fail(error, "读取地图文件信息失败: " + ec.message());
            }
            const std::filesystem::path relative = std::filesystem::relative(iterator->path(), root, ec);
            if (ec) {
                return Fail(error, "计算地图相对路径失败: " + ec.message());
            }
            files.emplace_back(relative.generic_string(), iterator->path());
        } else if (ec) {
            return Fail(error, "读取地图目录项失败: " + ec.message());
        }
        iterator.increment(ec);
    }

    if (files.empty()) {
        return Fail(error, "地图目录没有可哈希文件: " + root.string());
    }
    std::sort(files.begin(), files.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

    MapHasher hasher;
    for (const auto& [relative, path] : files) {
        const std::uintmax_t size = std::filesystem::file_size(path, ec);
        if (ec) {
            return Fail(error, "读取地图文件大小失败: " + path.string());
        }
        hasher.String(relative);
        hasher.U64(static_cast<std::uint64_t>(size));

        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            return Fail(error, "无法打开地图文件: " + path.string());
        }
        std::array<char, 64 * 1024> buffer{};
        while (stream) {
            stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = stream.gcount();
            if (count > 0) {
                hasher.Bytes(buffer.data(), static_cast<std::size_t>(count));
            }
        }
        if (!stream.eof()) {
            return Fail(error, "读取地图文件失败: " + path.string());
        }
    }

    *hash = hasher.Value();
    return true;
}

}  // namespace

WorldDefinitionStatus LoadWorldDefinition(const std::string& mapDir, WorldDefinition* out,
                                          std::string* error) {
    if (out == nullptr) {
        Fail(error, "world.json 输出为空");
        return WorldDefinitionStatus::Invalid;
    }
    *out = WorldDefinition{};
    if (error != nullptr) {
        error->clear();
    }

    const std::filesystem::path worldPath =
        std::filesystem::path(mapDir) / "world.json";
    std::error_code ec;
    const bool exists = std::filesystem::exists(worldPath, ec);
    if (ec) {
        Fail(error, "检查 world.json 失败: " + ec.message());
        return WorldDefinitionStatus::Invalid;
    }
    if (!exists) {
        return WorldDefinitionStatus::Missing;
    }

    MapLoader::Meta mapMeta;
    std::string mapError;
    if (!MapLoader::LoadMeta(mapDir, &mapMeta, &mapError)) {
        if (error != nullptr) {
            *error = mapError;
        }
        return WorldDefinitionStatus::Invalid;
    }

    std::ifstream file(worldPath, std::ios::binary);
    if (!file) {
        Fail(error, "无法打开 world.json: " + worldPath.string());
        return WorldDefinitionStatus::Invalid;
    }
    const nlohmann::json doc = nlohmann::json::parse(file, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        Fail(error, "world.json JSON 解析失败: " + worldPath.string());
        return WorldDefinitionStatus::Invalid;
    }

    if (!doc.contains("format") || !doc["format"].is_string()) {
        Fail(error, "world.json 缺少 format");
        return WorldDefinitionStatus::Invalid;
    }
    const std::string format = doc["format"].get<std::string>();
    if (format != kWorldFormat) {
        Fail(error, "world 格式不受支持: format=\"" + format + "\"（期望 \"" +
                        std::string(kWorldFormat) + "\"）: " + worldPath.string());
        return WorldDefinitionStatus::Invalid;
    }

    WorldDefinition result;
    result.format = format;
    if (!ReadStringField(doc, "mapId", "world", &result.mapId, error)) {
        return WorldDefinitionStatus::Invalid;
    }

    if (!doc.contains("seed")) {
        Fail(error, "world.json 缺少 seed");
        return WorldDefinitionStatus::Invalid;
    }
    if (!ReadIntegerValue(doc["seed"], "seed", &result.seed, error)) {
        return WorldDefinitionStatus::Invalid;
    }

    if (!doc.contains("generatorVersion")) {
        Fail(error, "world.json 缺少 generatorVersion");
        return WorldDefinitionStatus::Invalid;
    }
    std::int64_t generatorVersion = 0;
    if (!ReadIntegerValue(doc["generatorVersion"], "generatorVersion", &generatorVersion, error)) {
        return WorldDefinitionStatus::Invalid;
    }
    if (generatorVersion < 1 ||
        generatorVersion > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
        Fail(error, "world.json generatorVersion 超出范围 [1, 4294967295]");
        return WorldDefinitionStatus::Invalid;
    }
    result.generatorVersion = static_cast<std::uint32_t>(generatorVersion);

    if (!doc.contains("bounds") || !doc["bounds"].is_object()) {
        Fail(error, "world.json 缺少 bounds 或 bounds 不是对象");
        return WorldDefinitionStatus::Invalid;
    }
    if (!ReadBounds(doc["bounds"], "bounds", &result.bounds, error)) {
        return WorldDefinitionStatus::Invalid;
    }
    if (result.bounds.chunkX0 != mapMeta.chunkX0 || result.bounds.chunkY0 != mapMeta.chunkY0 ||
        result.bounds.chunksX != mapMeta.chunksX || result.bounds.chunksY != mapMeta.chunksY) {
        Fail(error, "world.json bounds 与 map.json 不一致: " + worldPath.string());
        return WorldDefinitionStatus::Invalid;
    }

    if (!doc.contains("regions") || !doc["regions"].is_array() || doc["regions"].empty()) {
        Fail(error, "world.json 缺少非空 regions 数组");
        return WorldDefinitionStatus::Invalid;
    }
    std::unordered_set<std::string> regionIds;
    for (std::size_t i = 0; i < doc["regions"].size(); ++i) {
        const std::string context = "regions[" + std::to_string(i) + "]";
        WorldRegion region;
        if (!ParseRegion(doc["regions"][i], context, &region, error)) {
            return WorldDefinitionStatus::Invalid;
        }
        if (!regionIds.insert(region.id).second) {
            Fail(error, "world.json regions 的 id 重复: " + region.id);
            return WorldDefinitionStatus::Invalid;
        }
        if (!RectInsideBounds(region.rect, result.bounds)) {
            Fail(error, "world.json " + context + " 越界: " + region.id);
            return WorldDefinitionStatus::Invalid;
        }
        for (const WorldRegion& existing : result.regions) {
            if (RectsOverlap(existing.rect, region.rect)) {
                Fail(error, "world.json regions 重叠: " + existing.id + " / " + region.id);
                return WorldDefinitionStatus::Invalid;
            }
        }
        result.regions.push_back(std::move(region));
    }

    if (!doc.contains("nodes") || !doc["nodes"].is_array() || doc["nodes"].empty()) {
        Fail(error, "world.json 缺少非空 nodes 数组");
        return WorldDefinitionStatus::Invalid;
    }
    const WorldExtent extent = ExtentOf(result.bounds);
    std::unordered_set<std::string> nodeIds;
    for (std::size_t i = 0; i < doc["nodes"].size(); ++i) {
        const std::string context = "nodes[" + std::to_string(i) + "]";
        WorldNode node;
        if (!ParseNode(doc["nodes"][i], context, &node, error)) {
            return WorldDefinitionStatus::Invalid;
        }
        if (node.x < extent.minX || node.x >= extent.maxX || node.y < extent.minY ||
            node.y >= extent.maxY) {
            Fail(error, "world.json " + context + " 越界: " + node.id);
            return WorldDefinitionStatus::Invalid;
        }
        result.nodes.push_back(std::move(node));
    }

    if (!doc.contains("edges") || !doc["edges"].is_array()) {
        Fail(error, "world.json 缺少 edges 数组");
        return WorldDefinitionStatus::Invalid;
    }
    for (std::size_t i = 0; i < doc["edges"].size(); ++i) {
        const std::string context = "edges[" + std::to_string(i) + "]";
        WorldEdge edge;
        if (!ParseEdge(doc["edges"][i], context, &edge, error)) {
            return WorldDefinitionStatus::Invalid;
        }
        result.edges.push_back(std::move(edge));
    }
    if (!ValidateGraph(result.nodes, result.edges, error)) {
        return WorldDefinitionStatus::Invalid;
    }

    if (!doc.contains("pois") || !doc["pois"].is_array()) {
        Fail(error, "world.json 缺少 pois 数组");
        return WorldDefinitionStatus::Invalid;
    }
    std::unordered_set<std::string> poiIds;
    for (std::size_t i = 0; i < doc["pois"].size(); ++i) {
        const std::string context = "pois[" + std::to_string(i) + "]";
        WorldPoi poi;
        if (!ParsePoi(doc["pois"][i], context, mapDir, extent, &poi, error)) {
            return WorldDefinitionStatus::Invalid;
        }
        if (!poiIds.insert(poi.id).second) {
            Fail(error, "world.json pois 的 id 重复: " + poi.id);
            return WorldDefinitionStatus::Invalid;
        }
        result.pois.push_back(std::move(poi));
    }
    for (const WorldRegion& region : result.regions) {
        for (const std::string& poiId : region.poiIds) {
            if (poiIds.find(poiId) == poiIds.end()) {
                Fail(error, "world.json region " + region.id + " 引用了不存在的 POI: " + poiId);
                return WorldDefinitionStatus::Invalid;
            }
        }
    }

    if (doc.contains("spawn") && !ParseSpawn(doc["spawn"], &result.spawn, error)) {
        return WorldDefinitionStatus::Invalid;
    }

    WarnUnknownFields(doc,
                      {"format", "mapId", "seed", "generatorVersion", "bounds", "regions",
                       "nodes", "edges", "pois", "spawn"},
                      "根对象");
    if (!ComputeMapHashImpl(mapDir, &result.mapHash, error)) {
        return WorldDefinitionStatus::Invalid;
    }

    *out = std::move(result);
    return WorldDefinitionStatus::Loaded;
}

bool ComputeMapHash(const std::string& mapDir, std::uint64_t* hash, std::string* error) {
    if (hash == nullptr) {
        return Fail(error, "mapHash 输出为空");
    }
    if (error != nullptr) {
        error->clear();
    }
    return ComputeMapHashImpl(std::filesystem::path(mapDir), hash, error);
}

std::string FormatMapHash(std::uint64_t hash) {
    constexpr char kHex[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int i = 15; i >= 0; --i) {
        result[static_cast<std::size_t>(i)] = kHex[hash & 0xfu];
        hash >>= 4u;
    }
    return result;
}

WorldIdentity MakeWorldIdentity(const WorldDefinition& definition) {
    return {definition.mapId, definition.seed, definition.generatorVersion,
            definition.mapHash};
}

WorldSaveCompatibility CheckWorldSaveCompatibility(const WorldDefinition& definition,
                                                   const WorldIdentity* saved,
                                                   std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (saved == nullptr) {
        return WorldSaveCompatibility::LegacyNoManifest;
    }

    std::string details;
    if (saved->mapId != definition.mapId) {
        details += "mapId=\"" + saved->mapId + "\"（当前 \"" + definition.mapId + "\"）；";
    }
    if (saved->seed != definition.seed) {
        details += "seed=" + std::to_string(saved->seed) + "（当前 " +
                   std::to_string(definition.seed) + "）；";
    }
    if (saved->generatorVersion != definition.generatorVersion) {
        details += "generatorVersion=" + std::to_string(saved->generatorVersion) + "（当前 " +
                   std::to_string(definition.generatorVersion) + "）；";
    }
    if (saved->mapHash != definition.mapHash) {
        details += "mapHash=" + FormatMapHash(saved->mapHash) + "（当前 " +
                   FormatMapHash(definition.mapHash) + "）；";
    }
    if (details.empty()) {
        return WorldSaveCompatibility::Compatible;
    }
    if (error != nullptr) {
        *error = "世界存档与当前地图不匹配：" + details +
                 "为避免把旧建筑/容器套到新地形，已停止加载。";
    }
    return WorldSaveCompatibility::Mismatch;
}

}  // namespace zl::world
