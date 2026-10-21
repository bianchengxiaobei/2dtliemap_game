#include <doctest/doctest.h>

#include <string>

#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/GroundItems.h"
#include "game/PlayerInventoryService.h"
#include "items/InventoryOps.h"
#include "items/ItemRegistry.h"
#include "items/RecipeRegistry.h"

using namespace zl;

namespace {

const char* kItemsJson = R"({
  "format": "items-v1",
  "items": [
    { "id": "bandage", "name": "绷带", "icon": "i", "weight": 0.1, "maxStack": 5, "type": "consumable", "use": { "heal": 15 } },
    { "id": "rag", "name": "破布", "icon": "i", "weight": 0.1, "maxStack": 10, "type": "generic" },
    { "id": "axe", "name": "斧", "icon": "i", "weight": 3.0, "maxStack": 1, "type": "weapon" },
    { "id": "jacket", "name": "夹克", "icon": "i", "weight": 1.2, "maxStack": 1, "type": "clothing" },
    { "id": "hammer", "name": "锤子", "icon": "i", "weight": 2.0, "maxStack": 1, "type": "weapon" },
    { "id": "repair_kit", "name": "维修包", "icon": "i", "weight": 0.3, "maxStack": 5, "type": "generic" }
  ]
})";

const char* kRecipesJson = R"({
  "format": "recipes-v3",
  "recipes": [
    {
      "id": "field_bandage",
      "name": "简易绷带",
      "output": { "item": "bandage", "count": 1 },
      "ingredients": [ { "item": "rag", "count": 2 } ],
      "time": 0.5
    }
  ]
})";

struct Fixture {
    items::ItemRegistry items;
    items::RecipeRegistry recipes;

    Fixture() {
        std::string error;
        REQUIRE(items.LoadFromString(kItemsJson, "test-items", &error));
        REQUIRE(recipes.LoadFromString(kRecipesJson, "test-recipes", items, &error));
    }
};

ecs::Entity MakePlayer(ecs::EcsWorld& world, float x = 10.0f, float y = 10.0f) {
    const ecs::Entity player = world.CreateWithStableId(1);
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(player, ecs::Position{x, y, 0});
    registry.emplace<ecs::Health>(player, ecs::Health{50.0f, 100.0f});
    registry.emplace<ecs::Needs>(player, ecs::Needs{});
    ecs::Container bag;
    bag.capacityKg = 100.0f;
    registry.emplace<ecs::Container>(player, std::move(bag));
    registry.emplace<ecs::Equipment>(player, ecs::Equipment{});
    registry.emplace<ecs::KnownRecipes>(player, ecs::KnownRecipes{});
    registry.emplace<ecs::CraftingJob>(player, ecs::CraftingJob{});
    return player;
}

}  // namespace

TEST_CASE("M13-T7 服务器背包动作：拾取/使用/装备/丢弃") {
    Fixture fixture;
    ecs::EcsWorld world;
    const ecs::Entity player = MakePlayer(world);
    game::PlayerInventoryService service(world, fixture.items, fixture.recipes,
                                         nullptr, nullptr);
    entt::registry& registry = world.Registry();
    auto& bag = registry.get<ecs::Container>(player);

    const ecs::Entity ground = game::SpawnGroundItem(
        world, fixture.items, ecs::ItemInstance{"bandage", 2, 1.0f}, 10.5f, 10.5f);
    REQUIRE(static_cast<bool>(ground != entt::null));

    const game::InventoryResult picked = service.PickupNearest(player);
    CHECK(picked.ok);
    CHECK(items::CountOf(bag, "bandage") == 2);

    const game::InventoryResult used = service.UseBag(player, 0);
    CHECK(used.ok);
    CHECK(registry.get<ecs::Health>(player).current == doctest::Approx(65.0f));
    CHECK(items::CountOf(bag, "bandage") == 1);

    const game::InventoryResult dropped = service.DropBag(player, 0);
    CHECK(dropped.ok);
    CHECK(items::CountOf(bag, "bandage") == 0);
    CHECK(game::CountGroundItems(world) == 1);

    const ecs::Entity axe = game::SpawnGroundItem(
        world, fixture.items, ecs::ItemInstance{"axe", 1, 1.0f}, 10.5f, 10.5f);
    REQUIRE(static_cast<bool>(axe != entt::null));
    REQUIRE(service.Pickup(player, world.IdOf(axe).id).ok);
    const game::InventoryResult equipped = service.EquipBag(player, 0, 0);
    CHECK(equipped.ok);
    REQUIRE(registry.get<ecs::Equipment>(player).slots[0].has_value());
    CHECK(registry.get<ecs::Equipment>(player).slots[0]->defId == "axe");

    const game::InventoryResult unequipped = service.Unequip(player, 0);
    CHECK(unequipped.ok);
    CHECK(items::CountOf(bag, "axe") == 1);

    const ecs::Entity far = game::SpawnGroundItem(
        world, fixture.items, ecs::ItemInstance{"rag", 1, 1.0f}, 30.0f, 30.0f);
    REQUIRE(static_cast<bool>(far != entt::null));
    CHECK_FALSE(service.Pickup(player, world.IdOf(far).id).ok);
}

TEST_CASE("M13-T7 服务器背包动作：维修") {
    Fixture fixture;
    ecs::EcsWorld world;
    const ecs::Entity player = MakePlayer(world);
    game::PlayerInventoryService service(world, fixture.items, fixture.recipes,
                                         nullptr, nullptr);
    auto& bag = world.Registry().get<ecs::Container>(player);
    bag.items.push_back(ecs::ItemInstance{"axe", 1, 0.4f});
    bag.items.push_back(ecs::ItemInstance{"repair_kit", 1, 1.0f});

    const game::InventoryResult repaired = service.RepairBag(player, 0);
    CHECK(repaired.ok);
    CHECK(bag.items[0].condition == doctest::Approx(0.9f));
    CHECK(items::CountOf(bag, "repair_kit") == 0);
}

TEST_CASE("M13-T7 服务器制作：批量计时、队列与取消") {
    Fixture fixture;
    ecs::EcsWorld world;
    const ecs::Entity player = MakePlayer(world);
    game::PlayerInventoryService service(world, fixture.items, fixture.recipes,
                                         nullptr, nullptr);
    auto& registry = world.Registry();
    auto& bag = registry.get<ecs::Container>(player);
    const auto& job = registry.get<ecs::CraftingJob>(player);

    for (int i = 0; i < 8; ++i) {
        bag.items.push_back(ecs::ItemInstance{"rag", 1, 1.0f});
    }

    ui::InventoryAction craft;
    craft.type = ui::InventoryActionType::Craft;
    craft.fromIndex = 0;
    craft.count = 2;
    const game::InventoryResult started = service.Dispatch(player, craft);
    CHECK(started.ok);
    CHECK(job.active);
    CHECK(job.batchTotal == 2);

    game::InventoryResult result;
    CHECK(service.TickCrafting(player, 0.6f, &result));
    CHECK(job.active);
    CHECK(job.batchCompleted == 1);
    CHECK(items::CountOf(bag, "rag") == 6);
    CHECK(items::CountOf(bag, "bandage") == 1);

    CHECK(service.TickCrafting(player, 0.6f, &result));
    CHECK_FALSE(job.active);
    CHECK(items::CountOf(bag, "rag") == 4);
    CHECK(items::CountOf(bag, "bandage") == 2);

    // 第二件进入队列，第一件完成后自动开始队首。
    ui::InventoryAction one = craft;
    one.count = 1;
    REQUIRE(service.Dispatch(player, one).ok);
    REQUIRE(service.Dispatch(player, one).ok);
    CHECK(job.queue.size() == 1);
    CHECK(service.TickCrafting(player, 0.6f, &result));
    CHECK(job.active);
    CHECK(job.queue.empty());
    CHECK(service.TickCrafting(player, 0.6f, &result));
    CHECK_FALSE(job.active);
    CHECK(items::CountOf(bag, "rag") == 0);
    CHECK(items::CountOf(bag, "bandage") == 4);

    // 取消制作不扣材料。
    bag.items.push_back(ecs::ItemInstance{"rag", 2, 1.0f});
    REQUIRE(service.Dispatch(player, one).ok);
    ui::InventoryAction cancel;
    cancel.type = ui::InventoryActionType::CancelCraft;
    CHECK(service.Dispatch(player, cancel).ok);
    CHECK_FALSE(job.active);
    CHECK(items::CountOf(bag, "rag") == 2);
}
