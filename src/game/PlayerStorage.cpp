#include "game/PlayerStorage.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "ecs/components/CommonComponents.h"
#include "items/InventoryOps.h"
#include "items/ItemDef.h"
#include "items/ItemRegistry.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "items";

StorageTransferResult TakeStorageItem(ecs::EcsWorld& world,
                                      const items::ItemRegistry& items,
                                      ecs::Entity storage,
                                      ecs::Container& playerContainer, int index,
                                      int count) {
    StorageTransferResult result;
    if (!world.Alive(storage) || index < 0 || count <= 0) {
        return result;
    }
    entt::registry& registry = world.Registry();
    auto* source = registry.try_get<ecs::Container>(storage);
    if (source == nullptr || static_cast<std::size_t>(index) >= source->items.size()) {
        return result;
    }
    result.found = true;
    const ecs::ItemInstance stack = source->items[static_cast<std::size_t>(index)];
    result.defId = stack.defId;
    const int requested = std::min(count, stack.count);
    ecs::ItemInstance attempt = stack;
    attempt.count = requested;

    ecs::Container probe = playerContainer;
    const int allowed = items::AddItem(probe, items, attempt);
    if (allowed <= 0) {
        return result;
    }
    ecs::ItemInstance removed;
    if (!items::TakeAt(*source, static_cast<std::size_t>(index), allowed, &removed)) {
        return result;
    }
    const int added = items::AddItem(playerContainer, items, removed);
    if (added < removed.count) {
        ecs::ItemInstance remainder = removed;
        remainder.count -= added;
        (void)items::AddItem(*source, items, remainder);
    }
    result.moved = added;
    result.stacksMoved = added > 0 ? 1 : 0;
    return result;
}

}  // namespace

StorageTransferResult TakePlayerStorageItem(ecs::EcsWorld& world,
                                            const items::ItemRegistry& items,
                                            ecs::Entity storage,
                                            ecs::Container& playerContainer, int index,
                                            int count) {
    return TakeStorageItem(world, items, storage, playerContainer, index, count);
}

ecs::Entity SpawnPlayerStorage(ecs::EcsWorld& world, float px, float py, int z,
                               std::string_view sprite, float capacityKg,
                               int maxStacks) {
    if (sprite.empty() || !std::isfinite(capacityKg) || capacityKg <= 0.0f) {
        return entt::null;
    }
    const ecs::Entity entity = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(entity, ecs::Position{px, py, z});
    registry.emplace<ecs::SpriteRef>(
        entity, ecs::SpriteRef{ecs::HashName(sprite), 0, 0xFFFFFFFFu});
    registry.emplace<ecs::Container>(
        entity, ecs::Container{{}, capacityKg});
    registry.emplace<ecs::PlayerStorage>(
        entity,
        ecs::PlayerStorage{std::clamp(maxStacks, 1, kMaxPlayerStorageStacks)});
    return entity;
}

bool IsPlayerStorage(const ecs::EcsWorld& world, ecs::Entity entity) {
    return entity != ecs::EcsWorld::kNull && world.Alive(entity) &&
           world.Registry().all_of<ecs::PlayerStorage>(entity);
}

ecs::Entity FindNearestPlayerStorage(ecs::EcsWorld& world, float px, float py, int z,
                                     float maxRadius) {
    entt::registry& registry = world.Registry();
    ecs::Entity best = entt::null;
    float bestDistSq = maxRadius * maxRadius;
    registry.view<ecs::Position, ecs::PlayerStorage, ecs::Container>().each(
        [&](const ecs::Entity entity, const ecs::Position& position,
            const ecs::PlayerStorage&, const ecs::Container&) {
            if (!world.Alive(entity) || position.z != z) {
                return;
            }
            const float dx = position.x - px;
            const float dy = position.y - py;
            const float distSq = dx * dx + dy * dy;
            if (distSq > bestDistSq) {
                return;
            }
            if (best == entt::null || distSq < bestDistSq) {
                best = entity;
                bestDistSq = distSq;
            }
        });
    return best;
}

StorageTransferResult StorePlayerStorageItem(ecs::EcsWorld& world,
                                             const items::ItemRegistry& items,
                                             ecs::Entity storage,
                                             ecs::Container& playerContainer, int index,
                                             int count) {
    StorageTransferResult result;
    if (!world.Alive(storage) || index < 0 || count <= 0) {
        return result;
    }
    entt::registry& registry = world.Registry();
    auto* target = registry.try_get<ecs::Container>(storage);
    auto* meta = registry.try_get<ecs::PlayerStorage>(storage);
    if (target == nullptr || meta == nullptr ||
        static_cast<std::size_t>(index) >= playerContainer.items.size()) {
        return result;
    }
    result.found = true;
    const ecs::ItemInstance stack =
        playerContainer.items[static_cast<std::size_t>(index)];
    result.defId = stack.defId;
    const int requested = std::min(count, stack.count);
    ecs::ItemInstance attempt = stack;
    attempt.count = requested;

    ecs::Container probe = *target;
    const int allowed = items::AddItem(probe, items, attempt);
    if (allowed <= 0 ||
        probe.items.size() > static_cast<std::size_t>(meta->maxStacks)) {
        return result;
    }
    ecs::ItemInstance removed;
    if (!items::TakeAt(playerContainer, static_cast<std::size_t>(index), allowed,
                       &removed)) {
        return result;
    }
    const int added = items::AddItem(*target, items, removed);
    if (added < removed.count) {
        ecs::ItemInstance remainder = removed;
        remainder.count -= added;
        (void)items::AddItem(playerContainer, items, remainder);
    }
    result.moved = added;
    result.stacksMoved = added > 0 ? 1 : 0;
    return result;
}

StorageTransferResult StoreAllInPlayerStorage(ecs::EcsWorld& world,
                                              const items::ItemRegistry& items,
                                              ecs::Entity storage,
                                              ecs::Container& playerContainer) {
    StorageTransferResult total;
    if (!IsPlayerStorage(world, storage)) {
        return total;
    }
    total.found = true;
    while (!playerContainer.items.empty()) {
        bool progressed = false;
        for (std::size_t i = 0; i < playerContainer.items.size(); ++i) {
            const int before = playerContainer.items[i].count;
            const StorageTransferResult result = StorePlayerStorageItem(
                world, items, storage, playerContainer, static_cast<int>(i), before);
            if (result.moved <= 0) {
                continue;
            }
            total.moved += result.moved;
            total.stacksMoved += result.stacksMoved;
            progressed = true;
            if (result.moved < before) {
                return total;
            }
            break;
        }
        if (!progressed) {
            break;
        }
    }
    return total;
}

StorageTransferResult TakeAllFromPlayerStorage(ecs::EcsWorld& world,
                                               const items::ItemRegistry& items,
                                               ecs::Entity storage,
                                               ecs::Container& playerContainer) {
    StorageTransferResult total;
    if (!IsPlayerStorage(world, storage)) {
        return total;
    }
    total.found = true;
    while (true) {
        auto* source = world.Registry().try_get<ecs::Container>(storage);
        if (source == nullptr || source->items.empty()) {
            break;
        }
        bool progressed = false;
        for (std::size_t i = 0; i < source->items.size(); ++i) {
            const int before = source->items[i].count;
            const StorageTransferResult result = TakeStorageItem(
                world, items, storage, playerContainer, static_cast<int>(i), before);
            if (result.moved <= 0) {
                continue;
            }
            total.moved += result.moved;
            total.stacksMoved += result.stacksMoved;
            total.defId = result.defId;
            progressed = true;
            if (result.moved < before) {
                return total;
            }
            break;
        }
        if (!progressed) {
            break;
        }
    }
    return total;
}

std::vector<ecs::ItemInstance> DetachPlayerStorageContents(ecs::EcsWorld& world,
                                                           ecs::Entity storage) {
    if (!world.Alive(storage)) {
        return {};
    }
    auto* container = world.Registry().try_get<ecs::Container>(storage);
    if (container == nullptr) {
        return {};
    }
    std::vector<ecs::ItemInstance> contents = std::move(container->items);
    container->items.clear();
    return contents;
}

void SortPlayerStorage(ecs::EcsWorld& world, const items::ItemRegistry& items,
                       ecs::Entity storage) {
    if (!world.Alive(storage)) {
        return;
    }
    auto* container = world.Registry().try_get<ecs::Container>(storage);
    if (container == nullptr) {
        return;
    }
    const auto typeOf = [&items](const ecs::ItemInstance& item) {
        const items::ItemDef* def = items.Find(item.defId);
        return def != nullptr ? static_cast<int>(def->type) : 999;
    };
    std::stable_sort(
        container->items.begin(), container->items.end(),
        [&typeOf](const ecs::ItemInstance& left, const ecs::ItemInstance& right) {
            const int leftType = typeOf(left);
            const int rightType = typeOf(right);
            if (leftType != rightType) {
                return leftType < rightType;
            }
            if (left.defId != right.defId) {
                return left.defId < right.defId;
            }
            return left.condition > right.condition;
        });
}

}  // namespace zl::game
