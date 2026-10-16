#include "world/World.h"

namespace zl::world {

std::uint64_t World::PackCellKey(int cellX, int cellY) {
    // 两个 int32 位模式直接拼 64 位 key：负数经 uint32 转换后唯一（二进制补码）
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cellX)) << 32) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(cellY));
}

Cell* World::GetCell(int cellX, int cellY) {
    const auto it = m_cells.find(PackCellKey(cellX, cellY));
    return it == m_cells.end() ? nullptr : it->second.get();
}

const Cell* World::GetCell(int cellX, int cellY) const {
    const auto it = m_cells.find(PackCellKey(cellX, cellY));
    return it == m_cells.end() ? nullptr : it->second.get();
}

Cell& World::GetOrCreateCell(int cellX, int cellY) {
    const std::uint64_t key = PackCellKey(cellX, cellY);
    auto it = m_cells.find(key);
    if (it == m_cells.end()) {
        it = m_cells.emplace(key, std::make_unique<Cell>(cellX, cellY)).first;
    }
    return *it->second;
}

bool World::RemoveCell(int cellX, int cellY) {
    return m_cells.erase(PackCellKey(cellX, cellY)) > 0;
}

GridSquare* World::GetSquare(int wx, int wy, int z) {
    const CellCoord cellCoord = CellOf(wx, wy);
    Cell* cell = GetCell(cellCoord.cx, cellCoord.cy);
    return cell ? cell->GetSquare(wx, wy, z) : nullptr;
}

GridSquare& World::GetOrCreateSquare(int wx, int wy, int z) {
    const CellCoord cellCoord = CellOf(wx, wy);
    return GetOrCreateCell(cellCoord.cx, cellCoord.cy).GetOrCreateSquare(wx, wy, z);
}

std::size_t World::RemoveEmptyCells() {
    std::size_t removed = 0;
    for (auto it = m_cells.begin(); it != m_cells.end();) {
        if (it->second->IsEmpty()) {
            it = m_cells.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

std::size_t World::LoadedChunkCount() const {
    std::size_t count = 0;
    for (const auto& entry : m_cells) {
        count += entry.second->LoadedChunkCount();
    }
    return count;
}

void World::Clear() {
    m_cells.clear();
}

}  // namespace zl::world
