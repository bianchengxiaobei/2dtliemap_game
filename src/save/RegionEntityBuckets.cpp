#include "save/RegionEntityBuckets.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "core/Constants.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "save/ComponentRegistry.h"

namespace zl::save {
namespace {

bool Fail(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
    return false;
}

const world::WorldRegion* RegionAt(const world::WorldDefinition& definition,
                                   float x, float y) {
    for (const world::WorldRegion& region : definition.regions) {
        const double minX =
            static_cast<double>(region.rect.chunkX) * core::kChunkSize;
        const double minY =
            static_cast<double>(region.rect.chunkY) * core::kChunkSize;
        const double maxX =
            static_cast<double>(region.rect.chunkX + region.rect.chunksW) *
            core::kChunkSize;
        const double maxY =
            static_cast<double>(region.rect.chunkY + region.rect.chunksH) *
            core::kChunkSize;
        if (static_cast<double>(x) >= minX && static_cast<double>(x) < maxX &&
            static_cast<double>(y) >= minY && static_cast<double>(y) < maxY) {
            return &region;
        }
    }
    return nullptr;
}

void SortSnapshot(EntitySnapshot* snapshot) {
    std::sort(snapshot->entities.begin(), snapshot->entities.end(),
              [](const SavedEntity& left, const SavedEntity& right) {
                  return left.stableId < right.stableId;
              });
}

}  // namespace

bool CaptureRegionEntityBuckets(const ecs::EcsWorld& world,
                                const ComponentRegistry& registry,
                                const world::WorldDefinition& definition,
                                EntityBucketSet* out,
                                EntityFilter filter,
                                std::string* error) {
    if (out == nullptr) {
        return Fail(error, "实体区域桶输出为空");
    }

    EntityBucketSet captured;
    std::map<std::string, EntitySnapshot> buckets;
    std::string captureError;
    bool failed = false;
    world.Registry().view<ecs::StableId>().each(
        [&](const ecs::Entity entity, const ecs::StableId&) {
            if (failed || !world.Alive(entity)) {
                return;
            }
            if (filter && !filter(entity)) {
                return;
            }

            SavedEntity saved;
            if (!CaptureEntity(world, entity, registry, &saved, &captureError)) {
                failed = true;
                return;
            }

            const bool isPlayer =
                world.Registry().all_of<ecs::PlayerTag>(entity);
            const auto* position = world.Registry().try_get<ecs::Position>(entity);
            const world::WorldRegion* region =
                !isPlayer && position != nullptr
                    ? RegionAt(definition, position->x, position->y)
                    : nullptr;
            if (region == nullptr || region->id.empty()) {
                captured.global.entities.push_back(std::move(saved));
                return;
            }
            buckets[region->id].entities.push_back(std::move(saved));
        });
    if (failed) {
        return Fail(error, std::move(captureError));
    }

    SortSnapshot(&captured.global);
    captured.buckets.reserve(buckets.size());
    for (auto& [id, snapshot] : buckets) {
        if (snapshot.entities.empty()) {
            continue;
        }
        SortSnapshot(&snapshot);
        captured.buckets.push_back(EntityBucket{id, std::move(snapshot)});
    }

    if (!ValidateEntitySnapshot(captured.global, error)) {
        return false;
    }
    for (const EntityBucket& bucket : captured.buckets) {
        if (!ValidateEntitySnapshot(bucket.entities, error)) {
            return false;
        }
    }
    *out = std::move(captured);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

}  // namespace zl::save
