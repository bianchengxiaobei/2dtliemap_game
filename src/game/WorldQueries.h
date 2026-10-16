#pragma once

#include "core/Constants.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"
#include "world/World.h"
#include "world/WorldCoords.h"

namespace zl::game {

// M5-T2：世界查询小工具（点击校验 / AiSystem 游荡目标挑选共用）。
// 语义：只认已挂接（Loaded）chunk —— 未加载 / 越界一律按"不可用"处理。

// 查已挂接 chunk（cell / chunk 链路未创建或未挂接 → nullptr）
inline world::Chunk* FindChunkInWorld(world::World& world, int chunkX, int chunkY) {
    const world::CellCoord cell{world::FloorDiv(chunkX, core::kChunksPerCell),
                                world::FloorDiv(chunkY, core::kChunksPerCell)};
    world::Cell* owner = world.GetCell(cell.cx, cell.cy);
    if (owner == nullptr) {
        return nullptr;
    }
    const world::ChunkLocal local = world::ChunkLocalOf(chunkX, chunkY);
    return owner->GetChunk(local.lcx, local.lcy);
}

// 格是否阻挡（"未加载 = 未知 = 阻挡"；任一层的对象 blocksMovement 即阻挡）
inline bool IsSquareBlocked(world::World& world, int wx, int wy, int z) {
    const world::ChunkCoord cc = world::ChunkOf(wx, wy);
    const world::Chunk* chunk = FindChunkInWorld(world, cc.cx, cc.cy);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
        return true;
    }
    const world::SquareLocal local = world::SquareLocalOf(wx, wy);
    const world::GridSquare& square = chunk->Square(local.lx, local.ly, z);
    for (int i = 0; i < world::kSquareLayerCount; ++i) {
        for (const world::SquareObject& object :
             square.Layer(static_cast<world::SquareLayer>(i))) {
            if (object.blocksMovement) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace zl::game
