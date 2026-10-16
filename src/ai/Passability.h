#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "core/Constants.h"

namespace zl::world {
class Chunk;
}

namespace zl::ai {

// M4-T6：单 chunk 通行位图（z=0 地面层）——A* 的缓存粒度。
// 行 ly ∈ [0, kChunkSize)，位 lx ∈ [0, kChunkSize)，bit=1 阻挡。
// 数据与物理静态体同源（SquareObject::blocksMovement）；chunk 加载时构建、
// 卸载时删除（"局部重建"：只在 chunk 生命周期边界变化，不做整图重建）。
struct ChunkPassability {
    std::array<std::uint16_t, static_cast<std::size_t>(core::kChunkSize)> rows{};

    bool IsBlocked(int lx, int ly) const {
        return ((rows[static_cast<std::size_t>(ly)] >> lx) & 1u) != 0;
    }
    void SetBlocked(int lx, int ly) {
        rows[static_cast<std::size_t>(ly)] |= static_cast<std::uint16_t>(1u << lx);
    }
};

// 从已加载 chunk 生成位图（主线程调用；z≠0 层不参与地面寻路）
ChunkPassability BuildChunkPassability(const world::Chunk& chunk);

// 只读快照：后台线程 A* 的唯一数据源（构造后不再修改，可跨线程共享）。
// 覆盖 [minCX, maxCX]×[minCY, maxCY] 的 chunk 矩形；未 SetChunk 的 chunk
// 与区域外一律视为阻挡（未知区不可通行）。
class PathSnapshot {
public:
    PathSnapshot(int minCX, int minCY, int maxCX, int maxCY);

    void SetChunk(int cx, int cy, const ChunkPassability& bits);

    int MinCX() const { return m_minCX; }
    int MinCY() const { return m_minCY; }
    int MaxCX() const { return m_maxCX; }
    int MaxCY() const { return m_maxCY; }
    int MinGX() const { return m_minCX * core::kChunkSize; }
    int MinGY() const { return m_minCY * core::kChunkSize; }
    int MaxGX() const { return (m_maxCX + 1) * core::kChunkSize - 1; }
    int MaxGY() const { return (m_maxCY + 1) * core::kChunkSize - 1; }

    bool InRegion(int gx, int gy) const {
        return gx >= MinGX() && gx <= MaxGX() && gy >= MinGY() && gy <= MaxGY();
    }
    bool IsBlocked(int gx, int gy) const;

private:
    int m_minCX = 0;
    int m_minCY = 0;
    int m_maxCX = 0;
    int m_maxCY = 0;
    int m_widthChunks = 0;
    std::vector<std::uint16_t> m_rows;      // [chunk 行][ly][chunk 列] 的行位图
    std::vector<std::uint8_t> m_filled;     // 每 chunk 是否已填充（0 = 全阻挡）
};

// M4-T7：螺旋环搜索——返回距 (x,y) 最近的"可走"格（含自身，r=0 起逐环外扩；
// 环内按 dy、dx 升序扫描，结果确定）。maxRadius 内找不到返回 false。
// walkable 由调用方给定（点击移动：目标格校验时用 !IsSolidSquare）。
bool FindNearestWalkable(int x, int y, int maxRadius,
                         const std::function<bool(int, int)>& walkable, int* outX, int* outY);

}  // namespace zl::ai
