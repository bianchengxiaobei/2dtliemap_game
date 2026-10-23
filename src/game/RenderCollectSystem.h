#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/SpatialIndex.h"
#include "game/AssetProvider.h"
#include "render/DrawList.h"
#include "world/GridSquare.h"

namespace zl::game {

// M4-T2：ECS 实体 → painter"高度精灵"通道（docs/00 §6.1 固定链的 RenderCollectSystem）。
//
// 渲染帧收集（相机随时可变，不在模拟 tick 里做），在场景对象收集后把可见实体的
// 高度精灵追加进**同一份** DrawItem 列表，统一走 render::DrawItemLess 全局画家序排序。
//
// 层级约定：实体排在同格世界层之上（kEntityLayerBase = kSquareLayerCount），
// `SpriteRef::layer` 作为实体通道内的子层；同格同层用 StableId 决胜（顺序确定）。
class RenderCollectSystem {
public:
    static constexpr int kEntityLayerBase = world::kSquareLayerCount;

    // 世界格坐标下的可见范围（含余量）
    struct ViewRect {
        int gxMin = 0;
        int gyMin = 0;
        int gxMax = 0;
        int gyMax = 0;
        bool Contains(float x, float y) const {
            return x >= static_cast<float>(gxMin) && x <= static_cast<float>(gxMax) + 1.0f &&
                   y >= static_cast<float>(gyMin) && y <= static_cast<float>(gyMax) + 1.0f;
        }
    };

    // 纯函数（便于单测）：组件数据 + 已解析帧 → 一个 DrawItem；不查任何资源。
    // 实体锚点 = 脚下格中心（SpriteDrawRect 的 (px, py, z)），与 IsoMath 约定一致。
    static void FillEntityDrawItem(float px, float py, int z, uint32_t tint, uint64_t stableId,
                                   const render::AtlasFrame& frame, bgfx::TextureHandle texture,
                                   int subLayer, render::DrawItem* out, int roomId = -1) {
        render::DrawItem item;
        item.texture = texture;
        item.rect = render::SpriteDrawRect(px, py, z, frame.fx, frame.fy, frame.ox, frame.oy,
                                           frame.w, frame.h);
        item.u0 = frame.u0;
        item.v0 = frame.v0;
        item.u1 = frame.u1;
        item.v1 = frame.v1;
        item.color = tint;
        if (z >= 1) {
            // 楼上同世界规则：整体压到 35% 不透明（M3-T6 临时方案）
            const uint32_t alpha = (tint >> 24) * 0x59u / 0xFFu;
            item.color = (alpha << 24) | (tint & 0x00FFFFFFu);
        }
        item.stage = 1;  // 实体永远不进地面批
        item.z = z;
        item.diag = static_cast<int>(std::floor(px)) + static_cast<int>(std::floor(py));
        item.gx = static_cast<int>(std::floor(px));
        item.layer = kEntityLayerBase + subLayer;
        item.tieBreaker = stableId;
        item.roomId = roomId;
        *out = item;
    }

    // M14-T1：把画家序在玩家之后（会被玩家遮挡/遮挡玩家）、且屏幕矩形与
    // 玩家相交的 z=0 墙/门降到 alpha（0..255）；返回被修改的项数。
    // playerIndex 必须是 items 中玩家 DrawItem 的下标（调用前已按 DrawItemLess 排序）。
    // 只改颜色，不改排序/碰撞/存档；纯函数内联，测试目标无需额外链接 cpp。
    static std::size_t FadeOccludingWalls(const render::SpriteRect& playerRect,
                                          std::size_t playerIndex, std::uint32_t alpha,
                                          std::vector<render::DrawItem>& items) {
        if (alpha >= 0xFFu || playerRect.w <= 0.0f || playerRect.h <= 0.0f ||
            playerIndex >= items.size()) {
            return 0;
        }
        const std::uint32_t targetAlpha = alpha & 0xFFu;
        std::size_t faded = 0;
        for (std::size_t i = playerIndex + 1; i < items.size(); ++i) {
            render::DrawItem& item = items[i];
            if (item.stage != 1 || item.z != 0 ||
                item.layer != static_cast<int>(world::SquareLayer::Wall)) {
                continue;
            }
            const bool overlaps = item.rect.x < playerRect.x + playerRect.w &&
                                  item.rect.x + item.rect.w > playerRect.x &&
                                  item.rect.y < playerRect.y + playerRect.h &&
                                  item.rect.y + item.rect.h > playerRect.y;
            if (!overlaps) {
                continue;
            }
            const std::uint32_t currentAlpha = item.color >> 24;
            const std::uint32_t nextAlpha = std::min(currentAlpha, targetAlpha);
            if (nextAlpha >= currentAlpha) {
                continue;
            }
            item.color = (nextAlpha << 24) | (item.color & 0x00FFFFFFu);
            ++faded;
        }
        return faded;
    }

    // M14-T10：玩家进入房间后把该房间上方（z≥1）的楼板/墙全部剖掉。
    // 匹配键是 z=0 地面格的房间 ID，因此不会误伤相邻房屋的上层；只改本帧颜色。
    static std::size_t CutawayUpperLayersInRoom(int roomId,
                                                std::vector<render::DrawItem>& items) {
        if (roomId < 0) {
            return 0;
        }
        std::size_t hidden = 0;
        for (render::DrawItem& item : items) {
            if (item.z >= 1 && item.roomId == roomId && (item.color >> 24) != 0u) {
                item.color &= 0x00FFFFFFu;
                ++hidden;
            }
        }
        return hidden;
    }

    // M14-T2/T3：动画 v2 片段选帧；帧表由 manifest 的 animations 段提供。
    static std::size_t PickClipFrameIndex(const AssetProvider::AnimationClip* clip,
                                          const ecs::AnimState* anim,
                                          const ecs::Facing* facing) {
        if (clip == nullptr || anim == nullptr || facing == nullptr ||
            clip->framesPerDir == 0 || clip->frames.empty()) {
            return 0;
        }
        const std::size_t perDir = static_cast<std::size_t>(clip->framesPerDir);
        const std::size_t frame =
            std::min<std::size_t>(anim->frame, perDir - 1);
        const std::size_t index =
            static_cast<std::size_t>(facing->dir) * perDir + frame;
        return index < clip->frames.size() ? index : 0;
    }

    // 旧 8 帧回退（纯函数，可单测）：v2 的 3/4 帧索引不能直接套到旧资源，
    // 统一夹到每向 0/1 帧；无 AnimState/Facing 时仍走 HashPick。
    static std::size_t PickLegacyFrameIndex(const ecs::AnimState* anim,
                                            const ecs::Facing* facing,
                                            std::uint64_t stableId,
                                            std::size_t frameCount) {
        if (anim != nullptr && anim->clip != ecs::AnimClip::Legacy) {
            ecs::AnimState fallback = *anim;
            fallback.frame = std::min<std::uint8_t>(anim->frame, 1);
            fallback.framesPerDir = 2;
            return PickFrameIndex(&fallback, facing, stableId, frameCount);
        }
        return PickFrameIndex(anim, facing, stableId, frameCount);
    }

    // 旧 8 帧回退（纯函数，可单测）：
    //   有 AnimState + Facing（动画实体）→ index = dir * framesPerDir + frame（帧序约定）；
    //   否则（世界对象式静态变体）→ HashPick(stableId) 确定性伪随机。
    static std::size_t PickFrameIndex(const ecs::AnimState* anim, const ecs::Facing* facing,
                                      std::uint64_t stableId, std::size_t frameCount) {
        if (frameCount == 0) {
            return 0;
        }
        if (anim != nullptr && facing != nullptr && anim->framesPerDir > 0) {
            const std::size_t index = static_cast<std::size_t>(facing->dir) *
                                          static_cast<std::size_t>(anim->framesPerDir) +
                                      anim->frame;
            return index < frameCount ? index : index % frameCount;
        }
        return render::HashPick(stableId, 0x5EC7u) % frameCount;
    }

    // 主线程渲染帧：把可见范围内（Position + SpriteRef）实体的高度精灵追加进 items。
    // 帧选择见 PickFrameIndex；资源缺失静默跳过（与 PushObject 同策略）。
    static void Collect(ecs::EcsWorld& world, const ecs::SpatialIndex& index,
                        const AssetProvider& assets, const ViewRect& view,
                        std::vector<render::DrawItem>& items);
};

}  // namespace zl::game
