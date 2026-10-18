#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core/Constants.h"
#include "world/GridSquare.h"

namespace zl::world {

// 流式加载生命周期（docs/00 §4.3）。主线程推进状态，后台线程只读状态与数据。
enum class ChunkState : std::uint8_t {
    Unloaded = 0,   // 未加载（或已卸载完，可复用）
    Loading = 1,    // 后台线程解析/生成中
    Loaded = 2,     // 已挂接进世界，可读
    Unloading = 3,  // 正在从世界摘除
};

const char* ToString(ChunkState state);

// 合法迁移（M3-T4 按此推进，非法迁移 ZL_ASSERT 拦截）：
//   Unloaded → Loading → Loaded → Unloading → Unloaded
//   Loading → Unloaded（解析失败/取消回退）
bool IsChunkTransitionAllowed(ChunkState from, ChunkState to);

// Chunk = 10×10 格 × kMaxZ 层的柱块（10×10×8 = 800 格），流式加载/卸载与存档单位。
// 内部布局 [z][ly][lx]：与渲染"逐层收集"的遍历顺序一致。
class Chunk {
public:
    Chunk() = default;
    Chunk(int chunkX, int chunkY);

    int ChunkX() const { return m_chunkX; }
    int ChunkY() const { return m_chunkY; }

    // 本 chunk 起始世界格坐标
    static int WorldOriginX(int chunkX) { return chunkX * core::kChunkSize; }
    static int WorldOriginY(int chunkY) { return chunkY * core::kChunkSize; }

    static constexpr int Index(int lx, int ly, int z) {
        return (z * core::kChunkSize + ly) * core::kChunkSize + lx;
    }

    // 局部格坐标 lx/ly ∈ [0, kChunkSize)、z ∈ [0, kMaxZ)
    // 非 const 访问视为“本 chunk 可能被修改”，设置 dirty；只读请用 const Chunk。
    GridSquare& Square(int lx, int ly, int z);
    const GridSquare& Square(int lx, int ly, int z) const;

    // M10-T2：chunk 修改标记与母地图基线。
    // 读档流程：应用母地图 → CaptureBaseline() → 应用 chunk diff；这样 diff 中的格
    // 仍然相对母地图“已修改”，下次保存会继续写回。离开 Loaded 前由存档系统读取 diff。
    void CaptureBaseline();  // 以当前格状态作为母地图基线，并清除 dirty
    bool HasBaseline() const { return m_hasBaseline; }
    bool IsDirty() const { return m_dirty; }
    void MarkDirty() { m_dirty = true; }
    void ClearDirty() { m_dirty = false; }
    bool IsSquareModified(int lx, int ly, int z) const;

    ChunkState State() const { return m_state.load(std::memory_order_acquire); }
    // 非法迁移 ZL_ASSERT 拦截；进入 Unloaded 时自动清空全部格数据
    // （不变量：Unloaded ⇒ 无数据，卸载后的 chunk 对象可被复用池直接复用）
    void SetState(ChunkState state);

    // 强制清空数据并回到 Unloaded（异常路径用：任意当前状态都可调用）
    void Reset();

    static constexpr int kSquareCount = core::kSquaresPerChunkAllZ;

private:
    void ClearData();

    std::array<GridSquare, static_cast<std::size_t>(kSquareCount)> m_squares{};
    std::array<std::uint64_t, static_cast<std::size_t>(kSquareCount)> m_baseHashes{};
    int m_chunkX = 0;
    int m_chunkY = 0;
    std::atomic<ChunkState> m_state{ChunkState::Unloaded};  // 跨线程观察（T4）
    bool m_hasBaseline = false;
    bool m_dirty = false;
};

}  // namespace zl::world
