#pragma once

#include "core/Constants.h"

namespace zl::world {

// M3-T1：世界格坐标 (wx, wy) ↔ cell / chunk / 格内局部坐标 的唯一换算入口。
// 世界向两侧延伸，支持负数；**凡坐标分解一律走这里**，禁止在别处手写 (a / b)
// —— C++ 整数除法朝向零截断，负数分解会错（如 -1/10 == 0，但 -1 应属于 chunk -1）。

// 数学向下取整除法：FloorDiv(-1, 10) == -1
constexpr int FloorDiv(int a, int b) noexcept {
    const int q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

// 数学取模：结果始终落在 [0, b)（b > 0）；FloorMod(-1, 10) == 9
constexpr int FloorMod(int a, int b) noexcept {
    const int r = a % b;
    return r < 0 ? r + b : r;
}

struct CellCoord {
    int cx = 0;
    int cy = 0;
};

struct ChunkCoord {
    int cx = 0;  // 全局 chunk 坐标（可为负）
    int cy = 0;
};

struct SquareLocal {
    int lx = 0;  // 格在 chunk 内，[0, kChunkSize)
    int ly = 0;
};

struct ChunkLocal {
    int lcx = 0;  // chunk 在 cell 内，[0, kChunksPerCell)
    int lcy = 0;
};

// 世界格坐标 → 所属 cell
constexpr CellCoord CellOf(int wx, int wy) noexcept {
    return {FloorDiv(wx, core::kCellSize), FloorDiv(wy, core::kCellSize)};
}

// 世界格坐标 → 所属 chunk（全局 chunk 坐标）
constexpr ChunkCoord ChunkOf(int wx, int wy) noexcept {
    return {FloorDiv(wx, core::kChunkSize), FloorDiv(wy, core::kChunkSize)};
}

// 世界格坐标 → chunk 内局部格坐标
constexpr SquareLocal SquareLocalOf(int wx, int wy) noexcept {
    return {FloorMod(wx, core::kChunkSize), FloorMod(wy, core::kChunkSize)};
}

// 全局 chunk 坐标 → 在 cell 内的局部 chunk 坐标
constexpr ChunkLocal ChunkLocalOf(int chunkX, int chunkY) noexcept {
    return {FloorMod(chunkX, core::kChunksPerCell), FloorMod(chunkY, core::kChunksPerCell)};
}

}  // namespace zl::world
