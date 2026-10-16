#pragma once

#include <string>
#include <unordered_map>

#include "world/ChunkData.h"

namespace zl::world {

// M3-T3：地图 JSON → ChunkData（格式规范见 docs/mapformat.md）。
// 纯函数、无内部状态：可在后台线程调用。
// 硬错误（打不开/JSON 损坏/结构非法/坐标与路径不一致）→ 返回 false + error 文案；
// 条目级问题（坐标越界、未知层名、未知 legend 字符）→ ZL_WARN 并跳过/取默认值。
class MapLoader {
public:
    struct Meta {
        std::string rootDir;  // 地图目录（chunk 文件相对它解析）
        std::string name;
        std::string defaultFloor = "tile.floor.grass";
        std::unordered_map<char, std::string> legend;
        int chunkX0 = 0;
        int chunkY0 = 0;
        int chunksX = 0;
        int chunksY = 0;  // bounds.chunksX/chunksY > 0
    };

    // 解析 map.json（format 门禁 / bounds / legend / 默认地面）
    static bool LoadMeta(const std::string& mapDir, Meta* meta, std::string* error);

    static bool InBounds(const Meta& meta, int chunkX, int chunkY);
    static std::string ChunkFilePath(const Meta& meta, int chunkX, int chunkY);

    // 解析单 chunk 文件；**文件缺失 = 默认地形数据**（out->fromFile=false，返回 true）
    static bool LoadChunk(const Meta& meta, int chunkX, int chunkY, ChunkData* out,
                          std::string* error);
};

}  // namespace zl::world
