#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Constants.h"
#include "world/GridSquare.h"

namespace zl::world {

class Chunk;

// M3-T3：与运行时 Chunk 分离的"纯数据"结构（docs/00 §3.2）：
// 后台线程解析/生成产出 ChunkData → 推入结果队列 → 主线程 ApplyChunkData 挂接进世界。
// 禁止后台线程直接写 world 结构。
struct ChunkObjectData {
    std::int16_t localX = 0;
    std::int16_t localY = 0;
    std::int8_t z = 0;
    SquareLayer layer = SquareLayer::Furniture;
    std::string sprite;  // 逻辑名（AssetProvider 解析）
    std::int16_t ox = 0;
    std::int16_t oy = 0;
};

struct RoomRectData {
    std::int16_t localX = 0;
    std::int16_t localY = 0;
    std::int16_t w = 0;
    std::int16_t h = 0;
    std::int8_t z = 0;
    std::int32_t id = -1;
    std::string name;
};

struct ChunkData {
    int chunkX = 0;
    int chunkY = 0;
    bool fromFile = false;  // 来自 chunk 文件；false = 稀疏默认数据（文件缺失）
    std::array<std::string, core::kSquaresPerChunk> floors;  // [ly * kChunkSize + lx]
    std::vector<ChunkObjectData> objects;
    std::vector<RoomRectData> rooms;
};

// 主线程挂接：把纯数据写进运行时 Chunk（chunk 坐标必须一致）。
// 接受非 const 引用：floors/objects 的字符串会被搬移（避免挂接阶段拷贝）。
void ApplyChunkData(ChunkData& data, Chunk& chunk);

}  // namespace zl::world
