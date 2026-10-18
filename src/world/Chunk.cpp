#include "world/Chunk.h"

#include "core/Log.h"

namespace zl::world {
namespace {

constexpr int kPersistedLayers = static_cast<int>(SquareLayer::Mark);  // Mark 临时层不存档

std::uint64_t HashMix(std::uint64_t hash, std::uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ull;  // FNV-1a 64
    return hash;
}

std::uint64_t HashString(std::uint64_t hash, const std::string& value) {
    hash = HashMix(hash, value.size());
    for (const char c : value) {
        hash = HashMix(hash, static_cast<unsigned char>(c));
    }
    return hash;
}

std::uint64_t HashSquare(const GridSquare& square) {
    std::uint64_t hash = 1469598103934665603ull;
    hash = HashMix(hash, static_cast<std::uint32_t>(
                             static_cast<std::int32_t>(square.RoomId())));
    for (int layer = 0; layer < kPersistedLayers; ++layer) {
        const auto& objects = square.Layer(static_cast<SquareLayer>(layer));
        hash = HashMix(hash, objects.size());
        for (const SquareObject& object : objects) {
            hash = HashString(hash, object.sprite);
            hash = HashMix(hash, static_cast<std::uint16_t>(object.offsetX));
            hash = HashMix(hash, static_cast<std::uint16_t>(object.offsetY));
            hash = HashMix(hash, object.blocksMovement ? 1u : 0u);
        }
    }
    return hash;
}

}  // namespace

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
    m_dirty = true;
    return m_squares[static_cast<std::size_t>(Index(lx, ly, z))];
}

void Chunk::CaptureBaseline() {
    for (std::size_t i = 0; i < m_squares.size(); ++i) {
        m_baseHashes[i] = HashSquare(m_squares[i]);
    }
    m_hasBaseline = true;
    m_dirty = false;
}

bool Chunk::IsSquareModified(int lx, int ly, int z) const {
    ZL_ASSERT(lx >= 0 && lx < core::kChunkSize && ly >= 0 && ly < core::kChunkSize && z >= 0 &&
                  z < core::kMaxZ,
              "chunk 内格坐标越界: ({},{},{})", lx, ly, z);
    if (!m_hasBaseline) {
        return false;
    }
    const auto index = static_cast<std::size_t>(Index(lx, ly, z));
    return HashSquare(m_squares[index]) != m_baseHashes[index];
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
    m_baseHashes.fill(0);
    m_hasBaseline = false;
    m_dirty = false;
}

}  // namespace zl::world
