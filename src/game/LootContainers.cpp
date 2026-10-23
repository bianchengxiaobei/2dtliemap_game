#include "game/LootContainers.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "ai/Passability.h"
#include "core/Log.h"
#include "ecs/components/CommonComponents.h"
#include "game/WorldQueries.h"
#include "items/InventoryOps.h"
#include "world/World.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "items";

struct DemoSpot {
    int dx;
    int dy;
    const char* tableId;
    const char* sprite;
};

constexpr DemoSpot kDemoSpots[] = {
    {2, 2, "village_crate", "object.crate"},
    {-2, 1, "village_locker", "object.locker"},
};

struct HouseSpot {
    float x;
    float y;
    const char* tableId;
    const char* sprite;
};

constexpr HouseSpot kHouseSpots[] = {
    {71.5f, 20.5f, "house_workshop", "object.locker"},
    {74.5f, 20.5f, "house_kitchen", "object.crate"},
    {71.5f, 23.5f, "village_crate", "object.crate"},
};

void CountContainer(const ecs::Container& container, int* stacks, int* total) {
    if (stacks != nullptr) {
        *stacks = static_cast<int>(container.items.size());
    }
    int sum = 0;
    for (const ecs::ItemInstance& item : container.items) {
        sum += item.count;
    }
    if (total != nullptr) {
        *total = sum;
    }
}

}  // namespace

ecs::Entity SpawnLootContainer(ecs::EcsWorld& world,
                               const items::LootTableRegistry& lootTables,
                               std::string_view tableId, float px, float py, int z,
                               std::string_view spriteLogical) {
    const items::LootTable* table = lootTables.Find(tableId);
    if (table == nullptr) {
        ZL_WARN(kLogModule, "SpawnLootContainer：未知战利品表 '{}'", tableId);
        return entt::null;
    }
    entt::registry& registry = world.Registry();
    const ecs::Entity entity = world.Create();
    registry.emplace<ecs::Position>(entity, ecs::Position{px, py, z});
    registry.emplace<ecs::Container>(entity,
                                     ecs::Container{{}, table->capacityKg});
    ecs::LootContainer loot;
    loot.tableId = std::string(tableId);
    registry.emplace<ecs::LootContainer>(entity, std::move(loot));
    registry.emplace<ecs::SpriteRef>(
        entity, ecs::SpriteRef{ecs::HashName(spriteLogical), 0, 0xFFFFFFFFu});
    return entity;
}

int SpawnDemoLootContainers(ecs::EcsWorld& world,
                            const items::LootTableRegistry& lootTables,
                            world::World& map, float baseX, float baseY, int z) {
    const int baseGridX = static_cast<int>(std::floor(baseX));
    const int baseGridY = static_cast<int>(std::floor(baseY));
    int spawned = 0;
    for (const DemoSpot& spot : kDemoSpots) {
        int wx = 0;
        int wy = 0;
        const bool found = ai::FindNearestWalkable(
            baseGridX + spot.dx, baseGridY + spot.dy, 6,
            [&](int x, int y) { return !IsSquareBlocked(map, x, y, z); }, &wx, &wy);
        if (!found) {
            ZL_WARN(kLogModule, "战利品容器落点 ({}, {}) 附近找不到可走格，跳过 {}",
                    baseGridX + spot.dx, baseGridY + spot.dy, spot.tableId);
            continue;
        }
        if (SpawnLootContainer(world, lootTables, spot.tableId,
                               static_cast<float>(wx) + 0.5f,
                               static_cast<float>(wy) + 0.5f, z, spot.sprite) !=
            entt::null) {
            ++spawned;
        }
    }
    return spawned;
}

bool HasLootContainerNear(ecs::EcsWorld& world, std::string_view tableId, float px,
                          float py, int z, float maxRadius) {
    entt::registry& registry = world.Registry();
    bool found = false;
    registry.view<ecs::Position, ecs::LootContainer>().each(
        [&](const ecs::Entity entity, const ecs::Position& position,
            const ecs::LootContainer& loot) {
            if (found || !world.Alive(entity) || position.z != z ||
                (!tableId.empty() && loot.tableId != tableId)) {
                return;
            }
            const float dx = position.x - px;
            const float dy = position.y - py;
            if (dx * dx + dy * dy <= maxRadius * maxRadius) {
                found = true;
            }
        });
    return found;
}

int SpawnHouseLootContainers(ecs::EcsWorld& world,
                             const items::LootTableRegistry& lootTables, world::World& map,
                             int z) {
    int spawned = 0;
    for (const HouseSpot& spot : kHouseSpots) {
        if (HasLootContainerNear(world, spot.tableId, spot.x, spot.y, z, 1.5f)) {
            continue;
        }
        int wx = 0;
        int wy = 0;
        const bool found = ai::FindNearestWalkable(
            static_cast<int>(std::floor(spot.x)), static_cast<int>(std::floor(spot.y)), 3,
            [&](int x, int y) { return !IsSquareBlocked(map, x, y, z); }, &wx, &wy);
        if (!found) {
            ZL_WARN(kLogModule, "房屋容器落点 ({:.1f}, {:.1f}) 附近找不到可走格，跳过 {}",
                    static_cast<double>(spot.x), static_cast<double>(spot.y), spot.tableId);
            continue;
        }
        const ecs::Entity entity =
            SpawnLootContainer(world, lootTables, spot.tableId, static_cast<float>(wx) + 0.5f,
                               static_cast<float>(wy) + 0.5f, z, spot.sprite);
        if (entity != entt::null) {
            ++spawned;
        }
    }
    return spawned;
}

ecs::Entity FindNearestLootContainer(ecs::EcsWorld& world, float px, float py, int z,
                                     float maxRadius) {
    entt::registry& registry = world.Registry();
    ecs::Entity best = entt::null;
    float bestDistSq = maxRadius * maxRadius;
    registry.view<ecs::Position, ecs::LootContainer, ecs::Container>().each(
        [&](const ecs::Entity entity, const ecs::Position& position,
            const ecs::LootContainer&, const ecs::Container&) {
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

LootSearchResult SearchLootContainer(ecs::EcsWorld& world,
                                     const items::ItemRegistry& items,
                                     const items::LootTableRegistry& lootTables,
                                     ecs::Entity containerEntity) {
    LootSearchResult result;
    if (!world.Alive(containerEntity)) {
        return result;
    }
    entt::registry& registry = world.Registry();
    auto* container = registry.try_get<ecs::Container>(containerEntity);
    auto* loot = registry.try_get<ecs::LootContainer>(containerEntity);
    if (container == nullptr || loot == nullptr) {
        return result;
    }
    result.found = true;
    result.tableId = loot->tableId;
    CountContainer(*container, &result.itemStacks, &result.totalItems);
    if (loot->searched) {
        return result;
    }

    const items::LootTable* table = lootTables.Find(loot->tableId);
    if (table == nullptr) {
        result.error = "战利品表不存在: " + loot->tableId;
        loot->searched = true;
        ZL_WARN(kLogModule, "搜索容器失败：{}（实体 StableId {}）", result.error,
                world.IdOf(containerEntity).id);
        return result;
    }

    if (loot->rollSeed == 0) {
        loot->rollSeed = items::DeriveLootSeed(lootTables.Seed(), loot->tableId,
                                               world.IdOf(containerEntity).id);
    }
    container->items.clear();
    container->capacityKg = table->capacityKg;
    (void)items::RollLootTable(*table, items, *container, loot->rollSeed);
    loot->searched = true;
    result.searchedNow = true;
    CountContainer(*container, &result.itemStacks, &result.totalItems);
    ZL_INFO(kLogModule,
            "搜索容器：表={}，实体={}，seed={}，结果 {} 堆/{} 件，容量 {:.1f}kg",
            loot->tableId, world.IdOf(containerEntity).id, loot->rollSeed,
            result.itemStacks, result.totalItems, container->capacityKg);
    return result;
}

LootTakeResult TakeLootContainerItem(ecs::EcsWorld& world,
                                     const items::ItemRegistry& items,
                                     ecs::Entity containerEntity,
                                     ecs::Container& playerContainer, int index,
                                     int count) {
    LootTakeResult result;
    if (!world.Alive(containerEntity) || index < 0 || count <= 0) {
        return result;
    }
    entt::registry& registry = world.Registry();
    auto* source = registry.try_get<ecs::Container>(containerEntity);
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
        const int restored = items::AddItem(*source, items, remainder);
        if (restored != remainder.count) {
            ZL_WARN(kLogModule, "容器拿取回滚不完整：{} 应回滚 {}，实际 {}",
                    remainder.defId, remainder.count, restored);
        }
    }
    result.taken = added;
    result.remaining = items::CountOf(*source, result.defId);
    return result;
}

}  // namespace zl::game
