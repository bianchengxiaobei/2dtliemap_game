#pragma once

#include <cstdint>

#include <bgfx/bgfx.h>

#include "render/IsoMath.h"

namespace zl::render {

// M4-T2：painter 绘制项抽成公共类型（世界对象 + ECS 实体共用一份列表与排序）。
// 收集两段（对齐 PZ SpriteRenderer 批处理思路）：
//    stage 0 = z=0 地面批（互不遮挡，按纹理分批，1~3 个 drawcall）；
//    stage 1 = 其余一切（含实体高度精灵），全局画家序。
struct DrawItem {
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    SpriteRect rect;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    uint32_t color = 0xffffffff;
    int stage = 0;  // 0=地面批，1=对象批
    int z = 0;
    int diag = 0;  // x + y（画家序主轴）
    int gx = 0;
    int layer = 0;            // 同格绘制层序（世界层 0..kSquareLayerCount-1，实体在更上层）
    uint64_t tieBreaker = 0;  // 同格同层决胜（实体用 StableId，保证确定性；世界对象为 0）
};

// 全局画家序（唯一排序入口）：stage → z → diag → gx → layer → tieBreaker
inline bool DrawItemLess(const DrawItem& a, const DrawItem& b) {
    if (a.stage != b.stage) {
        return a.stage < b.stage;
    }
    if (a.z != b.z) {
        return a.z < b.z;
    }
    if (a.diag != b.diag) {
        return a.diag < b.diag;
    }
    if (a.gx != b.gx) {
        return a.gx < b.gx;
    }
    if (a.layer != b.layer) {
        return a.layer < b.layer;
    }
    return a.tieBreaker < b.tieBreaker;
}

// 稳定哈希（变体选取；同输入每帧恒定 → 不闪烁）。
// 世界对象用 (wx, wy, 层枚举)；实体用 StableId（移动不换帧）。
inline uint32_t HashPick(int x, int y, uint32_t salt) {
    uint32_t h = static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u ^
                 salt * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

inline uint32_t HashPick(uint64_t key, uint32_t salt) {
    uint64_t h = key * 0x9E3779B97F4A7C15ull + salt * 0xC2B2AE3D27D4EB4Full;
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    return static_cast<uint32_t>(h);
}

}  // namespace zl::render
