#include "game/RenderCollectSystem.h"

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
        const std::uint64_t stableId = world.IdOf(e).id;
        const AssetProvider::Entry& entry =
            (*frames)[render::HashPick(stableId, 0x5EC7u) % frames->size()];
        if (entry.atlas == nullptr || entry.frame == nullptr) {
            return;
        }
        render::DrawItem item;
        FillEntityDrawItem(pos->x, pos->y, pos->z, sprite->tint, stableId, *entry.frame,
                           entry.atlas->Texture(), sprite->layer, &item);
        items.push_back(item);
    });
}

}  // namespace zl::game
