#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

#include "world/Cell.h"
#include "world/WorldCoords.h"

namespace zl::world {

// World = cell 稀疏网格（docs/00 §4.1）。cell 按需创建，世界向两侧延伸（坐标可为负）。
// 流式加载/卸载策略由 M3-T4 的 ChunkManager 驱动，World 只负责结构本身。
class World {
public:
    // cell 坐标链路（不存在返回 nullptr / 创建）
    Cell* GetCell(int cellX, int cellY);
    const Cell* GetCell(int cellX, int cellY) const;
    Cell& GetOrCreateCell(int cellX, int cellY);
    bool RemoveCell(int cellX, int cellY);

    // 世界格坐标直达（cell/chunk 链路未创建时返回 nullptr）
    GridSquare* GetSquare(int wx, int wy, int z);
    GridSquare& GetOrCreateSquare(int wx, int wy, int z);

    // 回收所有"无已创建 chunk"的空 cell（卸载流程收尾用），返回移除数
    std::size_t RemoveEmptyCells();

    std::size_t LoadedCellCount() const { return m_cells.size(); }
    std::size_t LoadedChunkCount() const;
    void Clear();

private:
    static std::uint64_t PackCellKey(int cellX, int cellY);

    std::unordered_map<std::uint64_t, std::unique_ptr<Cell>> m_cells;
};

}  // namespace zl::world
