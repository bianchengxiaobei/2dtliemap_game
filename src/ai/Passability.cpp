#include "ai/Passability.h"

#include "world/Chunk.h"
#include "world/GridSquare.h"
#include "world/WorldCoords.h"

namespace zl::ai {

ChunkPassability BuildChunkPassability(const world::Chunk& chunk) {
    ChunkPassability out;
    for (int ly = 0; ly < core::kChunkSize; ++ly) {
        for (int lx = 0; lx < core::kChunkSize; ++lx) {
            const world::GridSquare& square = chunk.Square(lx, ly, 0);
            bool blocked = false;
            for (int layer = 0; layer < world::kSquareLayerCount && !blocked; ++layer) {
                for (const world::SquareObject& object :
                     square.Layer(static_cast<world::SquareLayer>(layer))) {
                    if (object.blocksMovement) {
                        blocked = true;
                        break;
                    }
                }
            }
            if (blocked) {
                out.SetBlocked(lx, ly);
            }
        }
    }
    return out;
}

PathSnapshot::PathSnapshot(int minCX, int minCY, int maxCX, int maxCY)
    : m_minCX(minCX), m_minCY(minCY), m_maxCX(maxCX), m_maxCY(maxCY),
      m_widthChunks(maxCX - minCX + 1) {
    const std::size_t chunks = static_cast<std::size_t>(m_widthChunks) *
                               static_cast<std::size_t>(maxCY - minCY + 1);
    m_rows.assign(chunks * core::kChunkSize, 0u);
    m_filled.assign(chunks, 0u);
}

void PathSnapshot::SetChunk(int cx, int cy, const ChunkPassability& bits) {
    if (cx < m_minCX || cx > m_maxCX || cy < m_minCY || cy > m_maxCY) {
        return;
    }
    const std::size_t chunkIndex =
        static_cast<std::size_t>((cy - m_minCY) * m_widthChunks + (cx - m_minCX));
    m_filled[chunkIndex] = 1u;
    const std::size_t base = chunkIndex * core::kChunkSize;
    for (int ly = 0; ly < core::kChunkSize; ++ly) {
        m_rows[base + ly] = bits.rows[ly];
    }
}

bool PathSnapshot::IsBlocked(int gx, int gy) const {
    const world::ChunkCoord chunk = world::ChunkOf(gx, gy);
    if (chunk.cx < m_minCX || chunk.cx > m_maxCX || chunk.cy < m_minCY ||
        chunk.cy > m_maxCY) {
        return true;  // 区域外 = 阻挡
    }
    const std::size_t chunkIndex =
        static_cast<std::size_t>((chunk.cy - m_minCY) * m_widthChunks + (chunk.cx - m_minCX));
    if (m_filled[chunkIndex] == 0u) {
        return true;  // 未加载/未填充 = 阻挡
    }
    const world::SquareLocal local = world::SquareLocalOf(gx, gy);
    const std::uint16_t row = m_rows[chunkIndex * core::kChunkSize + local.ly];
    return ((row >> local.lx) & 1u) != 0;
}

}  // namespace zl::ai
