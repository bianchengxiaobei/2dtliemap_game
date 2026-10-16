#pragma once

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
                                   int subLayer, render::DrawItem* out) {
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
        *out = item;
    }

    // 帧选择（纯函数，可单测）：
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
