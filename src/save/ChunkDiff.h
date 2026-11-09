#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "world/Chunk.h"
#include "world/GridSquare.h"

namespace zl::save {

// M10-T2：单个 chunk 的“相对母地图差异”。
//
// 只保存被修改过的格；每个格保存完整状态（房间 ID + 所有持久层），
// 因此删除墙、清空家具、改地面、丢物品都能用一个 patch 表达。
// Mark（血迹/脚印）是临时层，不进入基线、不写入存档。
// v2 增加 SquareObject::hp / playerBuilt；v3 增加地板覆盖物的 underlay；
// v4 增加 structureId / ownerStableId；解码兼容 v1/v2/v3。
inline constexpr std::uint16_t kChunkDiffVersion = 4;
inline constexpr std::uint16_t kChunkDiffMinVersion = 1;

struct ChunkSquareDiff {
    std::int16_t lx = 0;
    std::int16_t ly = 0;
    std::int8_t z = 0;
    std::int16_t roomId = -1;
    std::array<std::vector<world::SquareObject>,
               static_cast<std::size_t>(world::kSquareLayerCount)>
        layers;
};

struct ChunkDiff {
    std::int32_t chunkX = 0;
    std::int32_t chunkY = 0;
    std::vector<ChunkSquareDiff> squares;
};

// 与 chunk.CaptureBaseline() 捕获的母地图状态比较，只收集实际变化的格。
// chunk 必须先 CaptureBaseline（ChunkManager 在世界挂接后会调用）。
bool CaptureChunkDiff(const world::Chunk& chunk, ChunkDiff* out,
                      std::string* error = nullptr);

// X-F1 阶段 2b：无条件采集单个格子的完整持久状态。用于删除后该格已回到
// 母地图状态、不再出现在 CaptureChunkDiff 中的情况；客户端应用后仍会清掉旧结构。
bool CaptureChunkSquareDiff(const world::Chunk& chunk, int lx, int ly, int z,
                            ChunkDiff* out,
                            std::string* error = nullptr);

// 把 diff 应用到已加载母地图的 chunk 上；目标格会先清空再写入 patch。
// 调用顺序必须为：ApplyChunkData → CaptureBaseline → ApplyChunkDiff。
bool ApplyChunkDiff(const ChunkDiff& diff, world::Chunk& chunk,
                    std::string* error = nullptr);

// chunk_x_y.bin 的段负载编码（段 magic=ZLCD，含段头）。
std::vector<std::uint8_t> EncodeChunkDiff(const ChunkDiff& diff,
                                          std::string* error = nullptr);
bool DecodeChunkDiff(std::span<const std::uint8_t> bytes, ChunkDiff* out,
                     std::string* error = nullptr);

// 值为 true 表示格式合法且已写入 out；空 diff 也合法（调用方决定是否落盘）。
bool ValidateChunkDiff(const ChunkDiff& diff, std::string* error = nullptr);

// X-F1：把单个 chunk 的 diff 拆成“每格一条”的消息，避免联机同步时单包超过 KCP
// 帧上限；每条子 diff 仍保持完整的格状态（含 hp/playerBuilt/underlay）。
std::vector<ChunkDiff> SplitChunkDiffBySquare(const ChunkDiff& diff);

}  // namespace zl::save
