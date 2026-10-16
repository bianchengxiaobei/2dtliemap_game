#pragma once

// 全局常量集中处（docs/00 §4.1）；先给默认值，可调，改这里不改各处。

namespace zl::core {

// 世界
inline constexpr int kChunkSize = 10;        // chunk 边长（格）
inline constexpr int kCellSize = 300;        // cell 边长（格），30×30 chunks
inline constexpr int kMaxZ = 8;              // z 层数，0=地面
inline constexpr int kChunkLoadRadius = 3;   // 以相机为中心加载的 chunk 半径

// 世界结构派生常量（M3-T1：Chunk 的扁平数组尺寸等只允许引用这里，禁止各处手算）
inline constexpr int kChunksPerCell = kCellSize / kChunkSize;              // 30×30 chunks
inline constexpr int kSquaresPerChunk = kChunkSize * kChunkSize;           // 100（单 z 层）
inline constexpr int kSquaresPerChunkAllZ = kSquaresPerChunk * kMaxZ;      // 800（含全部 z）

// 等距渲染
inline constexpr int kTileW = 64;            // 等距菱形精灵占屏宽（2:1）
inline constexpr int kTileH = 32;
inline constexpr int kFloorPx = 96;          // 每 z 层垂直偏移像素（= 3×kTileH，对齐 PZ）

// 模拟
inline constexpr int kTickRate = 60;         // 固定 tick 频率 Hz
inline constexpr double kTickDt = 1.0 / kTickRate;

}  // namespace zl::core
