#pragma once

#include <string>
#include <vector>

#include "save/EntityStore.h"
#include "world/WorldDefinition.h"

namespace zl::ecs {
class EcsWorld;
}

namespace zl::save {

class ComponentRegistry;

struct EntityBucket {
    std::string id;
    EntitySnapshot entities;
};

struct EntityBucketSet {
    EntitySnapshot global;
    std::vector<EntityBucket> buckets;
};

bool CaptureRegionEntityBuckets(const ecs::EcsWorld& world,
                                const ComponentRegistry& registry,
                                const world::WorldDefinition& definition,
                                EntityBucketSet* out,
                                EntityFilter filter = {},
                                std::string* error = nullptr);

}  // namespace zl::save
