#pragma once

#include <array>
#include <cstddef>
#include <memory>

#include "core/Constants.h"
#include "world/Chunk.h"

namespace zl::world {

// Cell = 30×30 chunks（= 300×300 格；docs/00 §4.1），地图大区/加载单位。
// chunk 槽位按需创建（稀疏）；cell 自身的增删由 World 管理。
class Cell {
public:
    Cell() = default;
    Cell(int cellX, int cellY);

    int CellX() const { return m_cellX; }
    int CellY() const { return m_cellY; }

    // 本 cell 起始世界格坐标
    static int WorldOriginX(int cellX) { return cellX * core::kCellSize; }
    static int WorldOriginY(int cellY) { return cellY * core::kCellSize; }

    // 局部 chunk 坐标 lcx/lcy ∈ [0, kChunksPerCell)；GetChunk 未创建返回 nullptr
    Chunk* GetChunk(int lcx, int lcy);
    const Chunk* GetChunk(int lcx, int lcy) const;
    Chunk& GetOrCreateChunk(int lcx, int lcy);
    bool RemoveChunk(int lcx, int lcy);  // 返回是否真的移除（应仅在卸载完成后调用）

    // 世界格坐标直达（chunk 未创建时返回 nullptr）
    GridSquare* GetSquare(int wx, int wy, int z);
    GridSquare& GetOrCreateSquare(int wx, int wy, int z);

    std::size_t LoadedChunkCount() const;
    bool IsEmpty() const { return LoadedChunkCount() == 0; }

    template <typename F>
    void ForEachChunk(F&& f) {
        for (auto& slot : m_chunks) {
            if (slot) {
                f(*slot);
            }
        }
    }

    template <typename F>
    void ForEachChunk(F&& f) const {
        for (const auto& slot : m_chunks) {
            if (slot) {
                f(static_cast<const Chunk&>(*slot));
            }
        }
    }

private:
    static constexpr std::size_t kSlotCount =
        static_cast<std::size_t>(core::kChunksPerCell) * static_cast<std::size_t>(core::kChunksPerCell);
    static std::size_t SlotIndex(int lcx, int lcy);

    std::array<std::unique_ptr<Chunk>, kSlotCount> m_chunks{};
    int m_cellX = 0;
    int m_cellY = 0;
};

}  // namespace zl::world
