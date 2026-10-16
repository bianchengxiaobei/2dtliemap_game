#include "render/IsoMath.h"

#include <cmath>

namespace zl::render {

glm::vec2 ToScreen(float gridX, float gridY, int z) {
    const float halfW = static_cast<float>(core::kTileW) * 0.5f;
    const float halfH = static_cast<float>(core::kTileH) * 0.5f;
    return {(gridX - gridY) * halfW, (gridX + gridY) * halfH - z * static_cast<float>(core::kFloorPx)};
}

glm::vec2 FromScreen(const glm::vec2& screenPx, int z) {
    const float halfW = static_cast<float>(core::kTileW) * 0.5f;
    const float halfH = static_cast<float>(core::kTileH) * 0.5f;
    const float u = screenPx.x / halfW;                                        // gx - gy
    const float v = (screenPx.y + z * static_cast<float>(core::kFloorPx)) / halfH;  // gx + gy
    return {(u + v) * 0.5f, (v - u) * 0.5f};
}

GridPick PickCell(const glm::vec2& screenPx, int z, int mapWidth, int mapHeight) {
    // floor∘反解 的天然菱形分区中心在锚点下方半格（kTileH/2）；
    // 先把查询点上移半格，使分区菱形与「中心=锚点」的绘制约定严格重合
    // （否则悬停高亮相对地面块会有半格竖直偏移）。
    const glm::vec2 shifted = screenPx + glm::vec2(0.0f, static_cast<float>(core::kTileH) * 0.5f);
    const glm::vec2 g = FromScreen(shifted, z);
    GridPick pick;
    pick.x = static_cast<int>(std::floor(g.x));
    pick.y = static_cast<int>(std::floor(g.y));
    pick.hit = pick.x >= 0 && pick.y >= 0 && pick.x < mapWidth && pick.y < mapHeight;
    return pick;
}

SpriteRect SpriteDrawRect(float gridX, float gridY, int z, int fx, int fy, int ox, int oy,
                          int w, int h) {
    const glm::vec2 center = ToScreen(gridX, gridY, z);
    const float pivotX = static_cast<float>(fx) * 0.5f;
    const float pivotY = static_cast<float>(fy) - static_cast<float>(core::kTileH) * 0.5f;
    SpriteRect rect;
    rect.x = center.x - pivotX + static_cast<float>(ox);
    rect.y = center.y - pivotY + static_cast<float>(oy);
    rect.w = static_cast<float>(w);
    rect.h = static_cast<float>(h);
    return rect;
}

}  // namespace zl::render
