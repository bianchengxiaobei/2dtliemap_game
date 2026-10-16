#include "world/Chunk.h"

#include "core/Log.h"

namespace zl::world {

const char* ToString(ChunkState state) {
    switch (state) {
        case ChunkState::Unloaded:
            return "Unloaded";
        case ChunkState::Loading:
            return "Loading";
        case ChunkState::Loaded:
            return "Loaded";
        case ChunkState::Unloading:
            return "Unloading";
    }
    return "Unknown";
}

bool IsChunkTransitionAllowed(ChunkState from, ChunkState to) {
    switch (from) {
        case ChunkState::Unloaded:
            return to == ChunkState::Loading;
        case ChunkState::Loading:
            return to == ChunkState::Loaded || to == ChunkState::Unloaded;
        case ChunkState::Loaded:
            return to == ChunkState::Unloading;
        case ChunkState::Unloading:
            return to == ChunkState::Unloaded;
    }
    return false;
}

Chunk::Chunk(int chunkX, int chunkY) : m_chunkX(chunkX), m_chunkY(chunkY) {
    const int x0 = WorldOriginX(chunkX);
    const int y0 = WorldOriginY(chunkY);
    for (int z = 0; z < core::kMaxZ; ++z) {
        for (int ly = 0; ly < core::kChunkSize; ++ly) {
            for (int lx = 0; lx < core::kChunkSize; ++lx) {
                m_squares[static_cast<std::size_t>(Index(lx, ly, z))].SetCoords(x0 + lx, y0 + ly, z);
            }
        }
    }
}

GridSquare& Chunk::Square(int lx, int ly, int z) {
    ZL_ASSERT(lx >= 0 && lx < core::kChunkSize && ly >= 0 && ly < core::kChunkSize && z >= 0 &&
                  z < core::kMaxZ,
              "chunk 内格坐标越界: ({},{},{})", lx, ly, z);
    return m_squares[static_cast<std::size_t>(Index(lx, ly, z))];
}

const GridSquare& Chunk::Square(int lx, int ly, int z) const {
    ZL_ASSERT(lx >= 0 && lx < core::kChunkSize && ly >= 0 && ly < core::kChunkSize && z >= 0 &&
                  z < core::kMaxZ,
              "chunk 内格坐标越界: ({},{},{})", lx, ly, z);
    return m_squares[static_cast<std::size_t>(Index(lx, ly, z))];
}

void Chunk::SetState(ChunkState state) {
    const ChunkState from = State();
    ZL_ASSERT(IsChunkTransitionAllowed(from, state), "非法 chunk 状态迁移: {} -> {}", ToString(from),
              ToString(state));
    if (state == ChunkState::Unloaded) {
        ClearData();
    }
    m_state.store(state, std::memory_order_release);
}

void Chunk::Reset() {
    ClearData();
    m_state.store(ChunkState::Unloaded, std::memory_order_release);
}

void Chunk::ClearData() {
    for (auto& square : m_squares) {
        square.Clear();
    }
}

}  // namespace zl::world
