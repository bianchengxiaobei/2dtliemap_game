#pragma once

#include <glm/glm.hpp>

#include "core/Constants.h"

namespace zl::render {

// M2-T4：世界格坐标 ↔ 屏幕像素的换算唯一入口（docs/00 §5.4）。
// 禁止其它地方手写等距公式。
//
// 约定（画布枢轴规则，见 devlog"绘制对齐"）：
//   ToScreen(gx, gy, z) 给出格子的「菱形中心」像素坐标，
//   它同时是：地面接触点（角色脚底）、鼠标拾取点。
//   sx = (gx - gy) * kTileW/2
//   sy = (gx + gy) * kTileH/2 - z * kFloorPx
glm::vec2 ToScreen(float gridX, float gridY, int z = 0);

// 反解：屏幕像素 → 连续格坐标（不做取整；z 取绘制平面层）
glm::vec2 FromScreen(const glm::vec2& screenPx, int z = 0);

// 拾取结果：hit=false 表示落在有效地图范围之外（边界未命中处理）
struct GridPick {
    bool hit = false;
    int x = 0;
    int y = 0;
};

// 鼠标拾取：屏幕像素 → 整格坐标。
// 落在格边界上时确定性归属：floor(连续格坐标)，与菱形剖分一致。
GridPick PickCell(const glm::vec2& screenPx, int z, int mapWidth, int mapHeight);

// 无限世界拾取：不做范围检查，hit 恒为 true（世界向两侧延伸，M3-T5）
GridPick PickCellUnbounded(const glm::vec2& screenPx, int z);

// 精灵绘制矩形（世界像素空间，左上角 + 尺寸）。
// 画布枢轴 (fx/2, fy - kTileH/2) 对准格中心：
//   drawTL = ToScreen(gx, gy, z) - (fx/2, fy - kTileH/2) + (ox, oy)
// 对 64×128 画布的地面块（ox=0, oy=96）即 = 中心 - (32, 16)，菱形与格子重合。
struct SpriteRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};
SpriteRect SpriteDrawRect(float gridX, float gridY, int z, int fx, int fy, int ox, int oy,
                          int w, int h);

}  // namespace zl::render
