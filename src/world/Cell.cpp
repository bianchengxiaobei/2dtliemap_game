#include "world/Cell.h"

#include "core/Log.h"
#include "world/WorldCoords.h"

namespace zl::world {

std::size_t Cell::SlotIndex(int lcx, int lcy) {
    ZL_ASSERT(lcx >= 0 && lcx < core::kChunksPerCell && lcy >= 0 && lcy < core::kChunksPerCell,
              "cell 内 chunk 坐标越界: ({},{})", lcx, lcy);
    return static_cast<std::size_t>(lcy) * static_cast<std::size_t>(core::kChunksPerCell) +
           static_cast<std::size_t>(lcx);
}

Cell::Cell(int cellX, int cellY) : m_cellX(cellX), m_cellY(cellY) {}

Chunk* Cell::GetChunk(int lcx, int lcy) {
    return m_chunks[SlotIndex(lcx, lcy)].get();
}

const Chunk* Cell::GetChunk(int lcx, int lcy) const {
    return m_chunks[SlotIndex(lcx, lcy)].get();
}

Chunk& Cell::GetOrCreateChunk(int lcx, int lcy) {
    auto& slot = m_chunks[SlotIndex(lcx, lcy)];
    if (!slot) {
        slot = std::make_unique<Chunk>(m_cellX * core::kChunksPerCell + lcx,
                                       m_cellY * core::kChunksPerCell + lcy);
    }
    return *slot;
}

bool Cell::RemoveChunk(int lcx, int lcy) {
    auto& slot = m_chunks[SlotIndex(lcx, lcy)];
    if (!slot) {
        return false;
    }
    ZL_ASSERT(slot->State() == ChunkState::Unloaded, "移除 chunk 前必须完成卸载: ({},{})",
              slot->ChunkX(), slot->ChunkY());
    slot.reset();
    return true;
}

GridSquare* Cell::GetSquare(int wx, int wy, int z) {
    const ChunkCoord chunkCoord = ChunkOf(wx, wy);
    const ChunkLocal chunkLocal = ChunkLocalOf(chunkCoord.cx, chunkCoord.cy);
    Chunk* chunk = GetChunk(chunkLocal.lcx, chunkLocal.lcy);
    if (!chunk) {
        return nullptr;
    }
    const SquareLocal local = SquareLocalOf(wx, wy);
    return &chunk->Square(local.lx, local.ly, z);
}

GridSquare& Cell::GetOrCreateSquare(int wx, int wy, int z) {
    const ChunkCoord chunkCoord = ChunkOf(wx, wy);
    const ChunkLocal chunkLocal = ChunkLocalOf(chunkCoord.cx, chunkCoord.cy);
    const SquareLocal local = SquareLocalOf(wx, wy);
    return GetOrCreateChunk(chunkLocal.lcx, chunkLocal.lcy).Square(local.lx, local.ly, z);
}

std::size_t Cell::LoadedChunkCount() const {
    std::size_t count = 0;
    for (const auto& slot : m_chunks) {
        if (slot) {
            ++count;
        }
    }
    return count;
}

}  // namespace zl::world
