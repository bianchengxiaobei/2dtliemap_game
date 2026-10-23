#include "game/RenderCollectSystem.h"

#include <algorithm>

#include "characters/components/CharacterComponents.h"
#include "core/Constants.h"
#include "ecs/components/CommonComponents.h"
#include "world/WorldCoords.h"

namespace zl::game {

void RenderCollectSystem::Collect(ecs::EcsWorld& world, const ecs::SpatialIndex& index,
                                  const AssetProvider& assets, const ViewRect& view,
                                  std::vector<render::DrawItem>& items) {
    entt::registry& registry = world.Registry();
    const int cx0 = world::FloorDiv(view.gxMin, core::kChunkSize);
    const int cx1 = world::FloorDiv(view.gxMax, core::kChunkSize);
    const int cy0 = world::FloorDiv(view.gyMin, core::kChunkSize);
    const int cy1 = world::FloorDiv(view.gyMax, core::kChunkSize);

    index.ForEachInChunks(cx0, cy0, cx1, cy1, [&](const ecs::Entity e) {
        const auto* pos = registry.try_get<ecs::Position>(e);
        const auto* sprite = registry.try_get<ecs::SpriteRef>(e);
        if (pos == nullptr || sprite == nullptr || !view.Contains(pos->x, pos->y)) {
            return;
        }
        const std::vector<AssetProvider::Entry>* frames = assets.FramesByHash(sprite->sprite);
        if (frames == nullptr || frames->empty()) {
            return;
        }

        const auto* anim = registry.try_get<ecs::AnimState>(e);
        const auto* facing = registry.try_get<ecs::Facing>(e);
        const AssetProvider::Entry* selected = nullptr;
        if (anim != nullptr && facing != nullptr) {
            const AssetProvider::AnimationSet* set =
                assets.AnimationsByHash(sprite->sprite);
            const AssetProvider::AnimationClip* clip =
                set != nullptr ? set->Find(anim->clip) : nullptr;
            if (clip != nullptr && !clip->frames.empty()) {
                const std::size_t index = PickClipFrameIndex(clip, anim, facing);
                selected = &clip->frames[index];
            }
        }

        if (selected == nullptr) {
            const std::size_t frameIndex =
                PickLegacyFrameIndex(anim, facing, world.IdOf(e).id, frames->size());
            selected = &(*frames)[frameIndex];
        }
        if (selected->atlas == nullptr || selected->frame == nullptr) {
            return;
        }
        render::DrawItem item;
        FillEntityDrawItem(pos->x, pos->y, pos->z, sprite->tint, world.IdOf(e).id,
                           *selected->frame, selected->atlas->Texture(), sprite->layer,
                           &item);
        if (const auto* variant = registry.try_get<ecs::ZombieVariant>(e);
            variant != nullptr) {
            const float scale = std::clamp(variant->visualScale, 0.5f, 2.0f);
            if (scale != 1.0f) {
                const float centerX = item.rect.x + item.rect.w * 0.5f;
                const float bottom = item.rect.y + item.rect.h;
                item.rect.w *= scale;
                item.rect.h *= scale;
                item.rect.x = centerX - item.rect.w * 0.5f;
                item.rect.y = bottom - item.rect.h;
            }
        }
        items.push_back(item);
    });
}

}  // namespace zl::game
