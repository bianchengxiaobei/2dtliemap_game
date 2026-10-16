#include <doctest/doctest.h>

#include <string>

#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/SpatialIndex.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/GroundItems.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"

using namespace zl;

namespace {

const char* kItemsJson = R"({
  "format": "items-v1",
  "items": [
    { "id": "bandage", "name": "绷带", "icon": "item.bandage", "weight": 0.1, "maxStack": 5, "type": "consumable" },
    { "id": "rag", "name": "破布", "icon": "item.rag", "weight": 0.1, "maxStack": 10, "type": "generic" },
    { "id": "axe", "name": "斧", "icon": "item.axe", "weight": 3.0, "maxStack": 1, "type": "weapon" }
  ]
})";

items::ItemRegistry MakeRegistry() {
    items::ItemRegistry registry;
    std::string error;
    const bool ok = registry.LoadFromString(kItemsJson, "test", &error);
    CHECK(ok);
    return registry;
}

bool IsNull(ecs::Entity e) { return e == entt::null; }

bool IsValid(ecs::Entity e) { return e != entt::null; }

ecs::ItemInstance Make(const char* defId, int count) {
    return ecs::ItemInstance{defId, count, 1.0f};
}

}  // namespace

TEST_CASE("M6-T3：地面物品实体生成与空间索引归属") {
    const items::ItemRegistry registry = MakeRegistry();
    ecs::EcsWorld world;

    const ecs::Entity e =
        game::SpawnGroundItem(world, registry, Make("bandage", 2), 10.5f, 10.5f);
    REQUIRE(IsValid(e));

    entt::registry& reg = world.Registry();
    REQUIRE(reg.try_get<ecs::Position>(e) != nullptr);
    const auto* instance = reg.try_get<ecs::ItemInstance>(e);
    REQUIRE(instance != nullptr);
    CHECK(instance->defId == "bandage");
    CHECK(instance->count == 2);
    const auto* sprite = reg.try_get<ecs::SpriteRef>(e);
    REQUIRE(sprite != nullptr);
    CHECK(sprite->sprite == ecs::HashName("item.bandage"));
    CHECK(sprite->layer == -1);  // 贴地层：画在角色之下
    CHECK(reg.try_get<ecs::Collider>(e) == nullptr);   // 不阻挡
    CHECK(reg.try_get<ecs::RigidBody>(e) == nullptr);  // 不参与物理
    CHECK(game::CountGroundItems(world) == 1);

    // 空间索引按 chunk 桶管理（渲染收集只查视口覆盖的 chunk → 远处物品睡眠）
    ecs::SpatialIndex index;
    index.Update(world);
    int found = 0;
    index.ForEachInChunks(1, 1, 1, 1, [&](ecs::Entity other) {
        if (other == e) {
            ++found;
        }
    });
    CHECK(found == 1);
    found = 0;
    index.ForEachInChunks(5, 5, 6, 6, [&](ecs::Entity) { ++found; });
    CHECK(found == 0);

    // 非法输入拒绝
    CHECK(IsNull(game::SpawnGroundItem(world, registry, Make("ghost", 1), 10.5f, 10.5f)));
    CHECK(IsNull(game::SpawnGroundItem(world, registry, ecs::ItemInstance{}, 10.5f, 10.5f)));
    CHECK(game::CountGroundItems(world) == 1);
}

TEST_CASE("M6-T3：最近地面物品搜索") {
    const items::ItemRegistry registry = MakeRegistry();
    ecs::EcsWorld world;
    const ecs::Entity near = game::SpawnGroundItem(world, registry, Make("bandage", 1), 10.5f,
                                                   10.8f);
    game::SpawnGroundItem(world, registry, Make("rag", 1), 12.0f, 10.0f);
    game::SpawnGroundItem(world, registry, Make("axe", 1), 10.5f, 13.0f);
    // 楼上物品（z=1）不应被搜到
    game::SpawnGroundItem(world, registry, Make("bandage", 1), 10.5f, 10.5f, /*z=*/1);

    CHECK(game::FindNearestGroundItem(world, 10.5f, 10.9f, 0, 2.0f) == near);
    CHECK(IsNull(game::FindNearestGroundItem(world, 30.0f, 30.0f, 0, 2.0f)));
    CHECK(IsValid(game::FindNearestGroundItem(world, 10.5f, 10.5f, 1, 1.0f)));
}

TEST_CASE("M6-T3：拾取完整链路（入包 + 实体销毁）") {
    const items::ItemRegistry registry = MakeRegistry();
    ecs::EcsWorld world;
    game::SpawnGroundItem(world, registry, Make("bandage", 2), 10.5f, 10.8f);
    ecs::Container bag;
    bag.capacityKg = 100.0f;

    const game::PickupResult result =
        game::PickupNearestItem(world, registry, bag, 10.5f, 10.9f, 0, 1.6f);
    CHECK(result.found);
    CHECK(result.picked == 2);
    CHECK(result.groundRemaining == 0);
    CHECK(result.defId == "bandage");
    CHECK(items::CountOf(bag, "bandage") == 2);
    world.Flush();
    CHECK(game::CountGroundItems(world) == 0);  // 数量归零 → 实体销毁

    // 半径内无物品
    const game::PickupResult empty =
        game::PickupNearestItem(world, registry, bag, 50.0f, 50.0f, 0, 1.6f);
    CHECK_FALSE(empty.found);
    CHECK(empty.picked == 0);
}

TEST_CASE("M6-T3：部分拾取与背包满") {
    const items::ItemRegistry registry = MakeRegistry();
    ecs::EcsWorld world;
    const ecs::Entity ground =
        game::SpawnGroundItem(world, registry, Make("rag", 3), 10.5f, 10.8f);
    ecs::Container bag;
    bag.capacityKg = 0.15f;  // 只装得下 1 件破布（0.1kg）

    const game::PickupResult first =
        game::PickupNearestItem(world, registry, bag, 10.5f, 10.8f, 0, 1.6f);
    CHECK(first.found);
    CHECK(first.picked == 1);
    CHECK(first.groundRemaining == 2);
    CHECK(world.Alive(ground));  // 有剩余 → 实体保留
    const auto* instance = world.Registry().try_get<ecs::ItemInstance>(ground);
    REQUIRE(instance != nullptr);
    CHECK(instance->count == 2);

    // 背包已满：地面不变
    const game::PickupResult second =
        game::PickupNearestItem(world, registry, bag, 10.5f, 10.8f, 0, 1.6f);
    CHECK(second.found);
    CHECK(second.picked == 0);
    CHECK(second.groundRemaining == 2);
    CHECK(items::CountOf(bag, "rag") == 1);
}

TEST_CASE("M6-T3：未知定义的地面实例不污染背包") {
    const items::ItemRegistry registry = MakeRegistry();
    ecs::EcsWorld world;
    // 直接构造（工厂会拒绝未知定义）——模拟存档/脚本写入的坏数据
    entt::registry& reg = world.Registry();
    const ecs::Entity bad = world.Create();
    reg.emplace<ecs::Position>(bad, ecs::Position{10.5f, 10.5f, 0});
    reg.emplace<ecs::ItemInstance>(bad, Make("ghost_item", 1));

    ecs::Container bag;
    bag.capacityKg = 100.0f;
    const game::PickupResult result =
        game::PickupNearestItem(world, registry, bag, 10.5f, 10.5f, 0, 1.6f);
    CHECK(result.found);
    CHECK(result.picked == 0);
    CHECK(bag.items.empty());
    CHECK(world.Alive(bad));  // 坏数据留在地面，不被消灭
}

TEST_CASE("M6-T3：丢弃模拟（RemoveItem + SpawnGroundItem 往返一致）") {
    const items::ItemRegistry registry = MakeRegistry();
    ecs::Container bag;
    bag.capacityKg = 100.0f;
    CHECK(items::AddItem(bag, registry, Make("axe", 1)) == 1);
    CHECK(items::AddItem(bag, registry, Make("rag", 2)) == 2);

    // 丢弃：移出 2 件破布 → 生成地面实体
    CHECK(items::RemoveItem(bag, "rag", 2) == 2);
    ecs::EcsWorld world;
    const ecs::Entity dropped =
        game::SpawnGroundItem(world, registry, Make("rag", 2), 12.0f, 12.0f);
    REQUIRE(IsValid(dropped));
    CHECK(game::CountGroundItems(world) == 1);

    // 再拾回：数量与总重量一致
    const game::PickupResult back =
        game::PickupNearestItem(world, registry, bag, 12.0f, 12.0f, 0, 1.6f);
    CHECK(back.picked == 2);
    CHECK(items::CountOf(bag, "rag") == 2);
    CHECK(items::TotalWeight(bag, registry) == doctest::Approx(3.2f));  // 3.0 + 0.2
}
