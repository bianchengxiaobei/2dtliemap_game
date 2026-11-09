#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "ecs/EcsWorld.h"
#include "items/ItemComponents.h"

namespace zl::items {
class ItemRegistry;
}

namespace zl::game {

inline constexpr int kMaxPlayerStorageStacks = 6;

ecs::Entity SpawnPlayerStorage(ecs::EcsWorld& world, float px, float py, int z,
                               std::string_view sprite, float capacityKg,
                               int maxStacks = kMaxPlayerStorageStacks);

bool IsPlayerStorage(const ecs::EcsWorld& world, ecs::Entity entity);

ecs::Entity FindNearestPlayerStorage(ecs::EcsWorld& world, float px, float py, int z,
                                     float maxRadius);

struct StorageTransferResult {
    bool found = false;
    int moved = 0;
    int stacksMoved = 0;
    std::string defId;
};

StorageTransferResult StorePlayerStorageItem(ecs::EcsWorld& world,
                                             const items::ItemRegistry& items,
                                             ecs::Entity storage,
                                             ecs::Container& playerContainer, int index,
                                             int count);

StorageTransferResult TakePlayerStorageItem(ecs::EcsWorld& world,
                                            const items::ItemRegistry& items,
                                            ecs::Entity storage,
                                            ecs::Container& playerContainer, int index,
                                            int count);

StorageTransferResult StoreAllInPlayerStorage(ecs::EcsWorld& world,
                                              const items::ItemRegistry& items,
                                              ecs::Entity storage,
                                              ecs::Container& playerContainer);

StorageTransferResult TakeAllFromPlayerStorage(ecs::EcsWorld& world,
                                               const items::ItemRegistry& items,
                                               ecs::Entity storage,
                                               ecs::Container& playerContainer);

std::vector<ecs::ItemInstance> DetachPlayerStorageContents(ecs::EcsWorld& world,
                                                           ecs::Entity storage);

void SortPlayerStorage(ecs::EcsWorld& world, const items::ItemRegistry& items,
                       ecs::Entity storage);

}  // namespace zl::game
