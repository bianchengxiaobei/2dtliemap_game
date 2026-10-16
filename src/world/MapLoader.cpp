#include "world/MapLoader.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>

#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "world/WorldCoords.h"

namespace zl::world {
namespace {

constexpr const char* kFormat = "zomboidlike-map-v1";
constexpr const char* kLogModule = "world";

std::optional<SquareLayer> ParseLayerName(std::string_view name) {
    if (name == "floor") {
        return SquareLayer::Floor;  // 仅用于 z≥1 的楼上地面（z=0 由 floors 网格表达）
    }
    if (name == "wall") {
        return SquareLayer::Wall;
    }
    if (name == "furniture") {
        return SquareLayer::Furniture;
    }
    if (name == "item") {
        return SquareLayer::Item;
    }
    if (name == "mark") {
        return SquareLayer::Mark;
    }
    return std::nullopt;
}

// json 取整数并做范围检查；越界返回 false（T 收窄自 int，已校验）
template <typename T>
bool ReadIntInRange(const nlohmann::json& node, const char* key, int lo, int hi, T* out) {
    if (!node.contains(key) || !node[key].is_number_integer()) {
        return false;
    }
    const int value = node[key].get<int>();
    if (value < lo || value > hi) {
        return false;
    }
    *out = static_cast<T>(value);
    return true;
}

}  // namespace

bool MapLoader::LoadMeta(const std::string& mapDir, Meta* meta, std::string* error) {
    const std::string path = (std::filesystem::path(mapDir) / "map.json").string();
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        *error = "无法打开地图元数据: " + path;
        ZL_ERROR(kLogModule, "{}", *error);
        return false;
    }

    const nlohmann::json doc = nlohmann::json::parse(file, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        *error = "map.json JSON 解析失败: " + path;
        ZL_ERROR(kLogModule, "{}", *error);
        return false;
    }

    const std::string format = doc.value("format", "");
    if (format != kFormat) {
        *error = "地图格式不受支持: format=\"" + format + "\"（期望 \"" + kFormat + "\"）: " + path;
        ZL_ERROR(kLogModule, "{}", *error);
        return false;
    }

    if (!doc.contains("bounds") || !doc["bounds"].is_object()) {
        *error = "map.json 缺少 bounds: " + path;
        ZL_ERROR(kLogModule, "{}", *error);
        return false;
    }
    const nlohmann::json& bounds = doc["bounds"];
    Meta result;
    result.rootDir = mapDir;
    result.name = doc.value("name", "");
    if (!ReadIntInRange(bounds, "chunkX0", INT32_MIN / 2, INT32_MAX / 2, &result.chunkX0) ||
        !ReadIntInRange(bounds, "chunkY0", INT32_MIN / 2, INT32_MAX / 2, &result.chunkY0) ||
        !ReadIntInRange(bounds, "chunksX", 1, INT32_MAX / 2, &result.chunksX) ||
        !ReadIntInRange(bounds, "chunksY", 1, INT32_MAX / 2, &result.chunksY)) {
        *error = "map.json bounds 字段非法（需 chunkX0/chunkY0/chunksX/chunksY，尺寸 > 0）: " + path;
        ZL_ERROR(kLogModule, "{}", *error);
        return false;
    }

    if (doc.contains("terrain") && doc["terrain"].is_object()) {
        result.defaultFloor = doc["terrain"].value("defaultFloor", result.defaultFloor);
    }

    if (doc.contains("legend")) {
        if (!doc["legend"].is_object()) {
            *error = "map.json legend 必须是对象: " + path;
            ZL_ERROR(kLogModule, "{}", *error);
            return false;
        }
        for (const auto& [key, value] : doc["legend"].items()) {
            if (key.size() != 1 || !value.is_string()) {
                ZL_WARN(kLogModule, "legend 条目非法已忽略: \"{}\"（需单字符 → 逻辑名）", key);
                continue;
            }
            result.legend[key[0]] = value.get<std::string>();
        }
    }

    *meta = std::move(result);
    return true;
}

bool MapLoader::InBounds(const Meta& meta, int chunkX, int chunkY) {
    return chunkX >= meta.chunkX0 && chunkX < meta.chunkX0 + meta.chunksX && chunkY >= meta.chunkY0 &&
           chunkY < meta.chunkY0 + meta.chunksY;
}

std::string MapLoader::ChunkFilePath(const Meta& meta, int chunkX, int chunkY) {
    const ChunkLocal local = ChunkLocalOf(chunkX, chunkY);
    const std::filesystem::path dir = std::filesystem::path(meta.rootDir) / "chunks" /
                                      ("c" + std::to_string(chunkX - local.lcx) + "_" +
                                       std::to_string(chunkY - local.lcy));
    return (dir / ("chunk_" + std::to_string(chunkX) + "_" + std::to_string(chunkY) + ".json"))
        .string();
}

bool MapLoader::LoadChunk(const Meta& meta, int chunkX, int chunkY, ChunkData* out,
                          std::string* error) {
    out->chunkX = chunkX;
    out->chunkY = chunkY;
    out->fromFile = false;
    out->floors.fill(meta.defaultFloor);
    out->objects.clear();
    out->rooms.clear();

    const std::string path = ChunkFilePath(meta, chunkX, chunkY);
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) {
        *error = "检查 chunk 文件失败: " + path + "（" + ec.message() + "）";
        ZL_ERROR(kLogModule, "{}", *error);
        return false;
    }
    if (!exists) {
        return true;  // 稀疏地图快速路径：默认地形、无对象
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        *error = "无法打开 chunk 文件: " + path;
        ZL_ERROR(kLogModule, "{}", *error);
        return false;
    }
    const nlohmann::json doc = nlohmann::json::parse(file, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        *error = "chunk JSON 解析失败: " + path;
        ZL_ERROR(kLogModule, "{}", *error);
        return false;
    }

    // cx/cy 与路径一致性（防止文件被挪错位置）
    const int fileCx = doc.value("cx", INT32_MIN);
    const int fileCy = doc.value("cy", INT32_MIN);
    if (fileCx != chunkX || fileCy != chunkY) {
        *error = "chunk 文件声明坐标 (" + std::to_string(fileCx) + "," + std::to_string(fileCy) +
                 ") 与路径 (" + std::to_string(chunkX) + "," + std::to_string(chunkY) +
                 ") 不一致: " + path;
        ZL_ERROR(kLogModule, "{}", *error);
        return false;
    }

    // floors：10 行 × 10 字符，经 legend 展开
    if (doc.contains("floors") && !doc["floors"].is_null()) {
        const nlohmann::json& floors = doc["floors"];
        if (!floors.is_array() || floors.size() != static_cast<std::size_t>(core::kChunkSize)) {
            *error = "floors 必须是 " + std::to_string(core::kChunkSize) + " 行字符串数组: " + path;
            ZL_ERROR(kLogModule, "{}", *error);
            return false;
        }
        for (int ly = 0; ly < core::kChunkSize; ++ly) {
            if (!floors[static_cast<std::size_t>(ly)].is_string()) {
                *error = "floors 第 " + std::to_string(ly) + " 行不是字符串: " + path;
                ZL_ERROR(kLogModule, "{}", *error);
                return false;
            }
            const std::string row = floors[static_cast<std::size_t>(ly)].get<std::string>();
            if (row.size() != static_cast<std::size_t>(core::kChunkSize)) {
                *error = "floors 第 " + std::to_string(ly) + " 行长度 " + std::to_string(row.size()) +
                         " ≠ " + std::to_string(core::kChunkSize) + ": " + path;
                ZL_ERROR(kLogModule, "{}", *error);
                return false;
            }
            for (int lx = 0; lx < core::kChunkSize; ++lx) {
                const auto it = meta.legend.find(row[static_cast<std::size_t>(lx)]);
                if (it == meta.legend.end()) {
                    ZL_WARN(kLogModule, "floors({},{}) 字符 '{}' 不在 legend 中，取默认地面: {}", lx,
                            ly, row[static_cast<std::size_t>(lx)], path);
                    continue;
                }
                out->floors[static_cast<std::size_t>(ly) * core::kChunkSize + lx] = it->second;
            }
        }
    }

    // objects：坐标越界/未知层 → warn + 跳过该条目
    if (doc.contains("objects") && doc["objects"].is_array()) {
        out->objects.reserve(doc["objects"].size());
        for (const auto& node : doc["objects"]) {
            if (!node.is_object()) {
                ZL_WARN(kLogModule, "objects 条目不是对象已跳过: {}", path);
                continue;
            }
            ChunkObjectData object;
            if (!ReadIntInRange(node, "x", 0, core::kChunkSize - 1, &object.localX) ||
                !ReadIntInRange(node, "y", 0, core::kChunkSize - 1, &object.localY)) {
                ZL_WARN(kLogModule, "objects 条目坐标非法已跳过: {}", path);
                continue;
            }
            int z = 0;
            if (node.contains("z") &&
                !ReadIntInRange(node, "z", 0, core::kMaxZ - 1, &z)) {
                ZL_WARN(kLogModule, "objects 条目 z 非法已跳过: {}", path);
                continue;
            }
            object.z = static_cast<std::int8_t>(z);

            const std::string layerName = node.value("layer", "");
            const std::optional<SquareLayer> layer = ParseLayerName(layerName);
            if (!layer) {
                ZL_WARN(kLogModule, "objects 条目层名非法 \"{}\" 已跳过: {}", layerName, path);
                continue;
            }
            object.layer = *layer;

            if (!node.contains("sprite") || !node["sprite"].is_string() ||
                node["sprite"].get<std::string>().empty()) {
                ZL_WARN(kLogModule, "objects 条目缺少 sprite 已跳过: {}", path);
                continue;
            }
            object.sprite = node["sprite"].get<std::string>();
            int ox = 0;
            int oy = 0;
            if ((node.contains("ox") && !ReadIntInRange(node, "ox", -4096, 4096, &ox)) ||
                (node.contains("oy") && !ReadIntInRange(node, "oy", -4096, 4096, &oy))) {
                ZL_WARN(kLogModule, "objects 条目 ox/oy 非法已跳过: {}", path);
                continue;
            }
            object.ox = static_cast<std::int16_t>(ox);
            object.oy = static_cast<std::int16_t>(oy);
            out->objects.push_back(std::move(object));
        }
    }

    // rooms：矩形越界 → warn + 跳过该条目
    if (doc.contains("rooms") && doc["rooms"].is_array()) {
        out->rooms.reserve(doc["rooms"].size());
        for (const auto& node : doc["rooms"]) {
            if (!node.is_object()) {
                ZL_WARN(kLogModule, "rooms 条目不是对象已跳过: {}", path);
                continue;
            }
            RoomRectData room;
            if (!ReadIntInRange(node, "x", 0, core::kChunkSize - 1, &room.localX) ||
                !ReadIntInRange(node, "y", 0, core::kChunkSize - 1, &room.localY) ||
                !ReadIntInRange(node, "w", 1, core::kChunkSize, &room.w) ||
                !ReadIntInRange(node, "h", 1, core::kChunkSize, &room.h) ||
                room.localX + room.w > core::kChunkSize || room.localY + room.h > core::kChunkSize) {
                ZL_WARN(kLogModule, "rooms 条目矩形非法（应为 chunk 内的 [x,x+w)×[y,y+h)）已跳过: {}",
                        path);
                continue;
            }
            int z = 0;
            if (node.contains("z") && !ReadIntInRange(node, "z", 0, core::kMaxZ - 1, &z)) {
                ZL_WARN(kLogModule, "rooms 条目 z 非法已跳过: {}", path);
                continue;
            }
            room.z = static_cast<std::int8_t>(z);
            room.id = node.value("id", -1);
            room.name = node.value("name", "");
            out->rooms.push_back(std::move(room));
        }
    }

    out->fromFile = true;
    return true;
}

}  // namespace zl::world
