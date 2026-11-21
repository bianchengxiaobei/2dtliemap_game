#pragma once

// M36-T6：遮挡玩家的植被压平/淡化（纯逻辑，不依赖 world/ECS/渲染后端）。
// 参考 PZ `IsoCell.flattenAnyFoliage` 的“树冠不整棵挡住角色”行为；
// 只改本帧 DrawItem，不回写世界/碰撞/采集/燃烧状态。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "render/DrawList.h"

namespace zl::render {

// 只处理画家序在玩家之后、屏幕矩形与玩家相交的 foliage：
//   1) 以底边为锚点按 keepRatio 压扁（树根仍贴地）；
//   2) 颜色 alpha 降到配置上限（不抬高已有透明度）。
inline std::size_t FlattenOccludingFoliage(const SpriteRect& playerRect,
                                            std::size_t playerIndex, float alpha,
                                            float keepRatio,
                                            std::vector<DrawItem>& items) {
    if (playerRect.w <= 0.0f || playerRect.h <= 0.0f || playerIndex >= items.size()) {
        return 0;
    }
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    const std::uint32_t targetAlpha =
        static_cast<std::uint32_t>(clampedAlpha * 255.0f + 0.5f);
    const float clampedRatio = std::clamp(keepRatio, 0.05f, 1.0f);

    std::size_t changed = 0;
    for (std::size_t i = playerIndex + 1; i < items.size(); ++i) {
        DrawItem& item = items[i];
        if (!item.foliage || item.stage != 1 || item.rect.w <= 0.0f ||
            item.rect.h <= 0.0f || (item.color >> 24) == 0u) {
            continue;
        }
        const bool overlaps = item.rect.x < playerRect.x + playerRect.w &&
                              item.rect.x + item.rect.w > playerRect.x &&
                              item.rect.y < playerRect.y + playerRect.h &&
                              item.rect.y + item.rect.h > playerRect.y;
        if (!overlaps) {
            continue;
        }
        bool itemChanged = false;
        if (clampedRatio < 1.0f) {
            const float bottom = item.rect.y + item.rect.h;
            item.rect.h = std::max(1.0f, item.rect.h * clampedRatio);
            item.rect.y = bottom - item.rect.h;
            itemChanged = true;
        }
        const std::uint32_t currentAlpha = item.color >> 24;
        if (targetAlpha < currentAlpha) {
            item.color = (targetAlpha << 24) | (item.color & 0x00FFFFFFu);
            itemChanged = true;
        }
        if (itemChanged) {
            ++changed;
        }
    }
    return changed;
}

}  // namespace zl::render
