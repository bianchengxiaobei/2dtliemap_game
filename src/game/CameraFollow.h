#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "core/Constants.h"

namespace zl::game {

// M4-T5：相机跟随的纯函数（可单测；不含状态，调用方只管时序）。

// 指数趋近（帧率无关）：每步向 target 收敛 (1 - e^(-rate·dt))。
// 性质：连续两步 dt ≈ 一步 2dt（指数可乘性）；rate<=0 = 瞬间到位；dt<=0 = 不动。
// 用指数而非 lerp(固定系数)：拖帧时不会"追过头"或收敛变快，60/144Hz 手感一致。
inline glm::vec2 CameraFollowStep(const glm::vec2& current, const glm::vec2& target, float rate,
                                  float dt) {
    if (rate <= 0.0f) {
        return target;
    }
    if (dt <= 0.0f) {
        return current;
    }
    const float t = 1.0f - std::exp(-rate * dt);
    return current + (target - current) * t;
}

// 地图格矩形 [gx0, gx1] × [gy0, gy1] → 世界像素 AABB（菱形投影的包围盒）。
// ToScreen（z=0）是线性变换：sx=(gx-gy)·kTileW/2, sy=(gx+gy)·kTileH/2；
// 旋转矩形的 AABB = 四角投影的 min/max（取整数角点即可，格中心为整数坐标）。
inline void MapPixelBounds(float gx0, float gy0, float gx1, float gy1, glm::vec2* outMinPx,
                           glm::vec2* outMaxPx) {
    const float halfW = static_cast<float>(core::kTileW) * 0.5f;
    const float halfH = static_cast<float>(core::kTileH) * 0.5f;
    const float xs[4] = {(gx0 - gy0) * halfW, (gx1 - gy0) * halfW, (gx0 - gy1) * halfW,
                         (gx1 - gy1) * halfW};
    const float ys[4] = {(gx0 + gy0) * halfH, (gx1 + gy0) * halfH, (gx0 + gy1) * halfH,
                         (gx1 + gy1) * halfH};
    *outMinPx = {xs[0], ys[0]};
    *outMaxPx = {xs[0], ys[0]};
    for (int i = 1; i < 4; ++i) {
        outMinPx->x = std::min(outMinPx->x, xs[i]);
        outMinPx->y = std::min(outMinPx->y, ys[i]);
        outMaxPx->x = std::max(outMaxPx->x, xs[i]);
        outMaxPx->y = std::max(outMaxPx->y, ys[i]);
    }
}

}  // namespace zl::game
