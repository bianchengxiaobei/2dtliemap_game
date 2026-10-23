#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "ecs/EcsWorld.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"
#include "items/LootTable.h"

namespace zl::world {
class World;
}

namespace zl::game {

// M14-T5：可搜刮容器实体。Container 存物品，LootContainer 存一次性 roll 状态；
// Position + SpriteRef 让现有 ECS 渲染收集直接画出木箱/储物柜。
ecs::Entity SpawnLootContainer(ecs::EcsWorld& world,
                               const items::LootTableRegistry& lootTables,
                               std::string_view tableId, float px, float py, int z,
                               std::string_view spriteLogical);

// 村庄出生点附近的两个演示容器；调用方需保证目标 chunk 已加载。
int SpawnDemoLootContainers(ecs::EcsWorld& world,
                            const items::LootTableRegistry& lootTables, world::World& map,
                            float baseX, float baseY, int z = 0);

int SpawnHouseLootContainers(ecs::EcsWorld& world,
                             const items::LootTableRegistry& lootTables, world::World& map,
                             int z = 0);

bool HasLootContainerNear(ecs::EcsWorld& world, std::string_view tableId, float px,
                          float py, int z, float maxRadius);

ecs::Entity FindNearestLootContainer(ecs::EcsWorld& world, float px, float py, int z,
                                     float maxRadius);

struct LootSearchResult {
    bool found = false;
    bool searchedNow = false;
    int itemStacks = 0;
    int totalItems = 0;
    std::string tableId;
    std::string error;
};

// 首次搜索按 loot.json + 容器 StableId 派生种子 roll；重复搜索只返回现有内容。
LootSearchResult SearchLootContainer(ecs::EcsWorld& world,
                                     const items::ItemRegistry& items,
                                     const items::LootTableRegistry& lootTables,
                                     ecs::Entity containerEntity);

struct LootTakeResult {
    bool found = false;
    int taken = 0;
    int remaining = 0;
    std::string defId;
};

// 从容器指定堆叠拿出最多 count 件并入玩家背包；部分拿取时源容器保留剩余。
// 失败（索引非法/背包已满）不修改源容器。
LootTakeResult TakeLootContainerItem(ecs::EcsWorld& world,
                                     const items::ItemRegistry& items,
                                     ecs::Entity containerEntity,
                                     ecs::Container& playerContainer, int index,
                                     int count = 1);

}  // namespace zl::game
