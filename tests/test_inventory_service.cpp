#include <doctest/doctest.h>

#include <string>
#include <variant>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/systems/PainSystem.h"
#include "characters/systems/WoundSystem.h"
#include "core/EventBus.h"
#include "core/GameEvents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/GroundItems.h"
#include "game/PlayerInventoryService.h"
#include "game/PlayerStorage.h"
#include "game/StructureComponents.h"
#include "items/InventoryOps.h"
#include "items/ItemRegistry.h"
#include "items/RecipeRegistry.h"

using namespace zl;

namespace {

const char* kItemsJson = R"({
  "format": "items-v1",
  "items": [
    { "id": "bandage", "name": "绷带", "icon": "i", "weight": 0.1, "maxStack": 5, "type": "consumable", "use": { "heal": 15, "bandage": 0.7 } },
    { "id": "rag", "name": "破布", "icon": "i", "weight": 0.1, "maxStack": 10, "type": "generic" },
    { "id": "axe", "name": "斧", "icon": "i", "weight": 3.0, "maxStack": 1, "type": "weapon" },
    { "id": "jacket", "name": "夹克", "icon": "i", "weight": 1.2, "maxStack": 1, "type": "clothing" },
    { "id": "hammer", "name": "锤子", "icon": "i", "weight": 2.0, "maxStack": 1, "type": "weapon" },
    { "id": "repair_kit", "name": "维修包", "icon": "i", "weight": 0.3, "maxStack": 5, "type": "generic" },
    { "id": "canned_beans", "name": "罐头豆子", "icon": "i", "weight": 0.8, "maxStack": 3, "type": "consumable", "use": { "hunger": 0.35 } },
    { "id": "tainted_food", "name": "可疑食物", "icon": "i", "weight": 0.3, "maxStack": 1, "type": "consumable", "use": { "hunger": 0.2, "nutrition": 0.4, "foodPoisonChance": 1.0 } },
    { "id": "water_bottle", "name": "瓶装水", "icon": "i", "weight": 0.5, "maxStack": 2, "type": "consumable", "use": { "thirst": 0.4 } },
    { "id": "painkillers", "name": "止痛药", "icon": "i", "weight": 0.05, "maxStack": 5, "type": "consumable", "use": { "painRelief": 0.65, "painReliefSeconds": 90 } },
    { "id": "antibiotics", "name": "抗生素", "icon": "i", "weight": 0.05, "maxStack": 5, "type": "consumable", "use": { "infectionCure": 0.35, "infectionSlow": 0.5, "infectionSlowSeconds": 60 } },
    { "id": "tainted_bandage", "name": "污染绷带", "icon": "i", "weight": 0.1, "maxStack": 5, "type": "consumable", "use": { "bandage": 0.35, "infectionRisk": 1.0 } }
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

TEST_CASE("M35-F2-T3 使用物品只在成功时派发 OnItemUsed") {
    Fixture fixture;
    ecs::EcsWorld world;
    const ecs::Entity player = MakePlayer(world);
    core::EventBus events;
    std::vector<core::ItemUsedEvent> used;
    const core::EventBus::SubscriptionId subscription = events.Subscribe(
        core::kOnItemUsed, [&](const core::Event& event) {
            if (const auto* payload =
                    std::get_if<core::ItemUsedEvent>(&event.payload);
                payload != nullptr) {
                used.push_back(*payload);
            }
        });
    game::PlayerInventoryService service(world, fixture.items, fixture.recipes,
                                         nullptr, &events);
    auto& bag = world.Registry().get<ecs::Container>(player);
    bag.items.push_back(ecs::ItemInstance{"bandage", 2, 1.0f});

    REQUIRE(service.UseBag(player, 0).ok);
    events.FlushQueued();
    REQUIRE(used.size() == 1u);
    CHECK(used[0].playerId == world.IdOf(player).id);
    CHECK(used[0].defId == "bandage");
    CHECK_FALSE(used[0].spoiled);
    CHECK(used[0].remaining == 1);

    CHECK_FALSE(service.UseBag(player, 99).ok);
    events.FlushQueued();
    CHECK(used.size() == 1u);

    REQUIRE(service.UseBag(player, 0).ok);
    events.FlushQueued();
    REQUIRE(used.size() == 2u);
    CHECK(used[1].remaining == 0);
    CHECK(bag.items.empty());
    events.Unsubscribe(subscription);
}

TEST_CASE("M14-T4 食物/饮水通过服务器背包动作恢复 Needs") {
    Fixture fixture;
    ecs::EcsWorld world;
    const ecs::Entity player = MakePlayer(world);
    game::PlayerInventoryService service(world, fixture.items, fixture.recipes,
                                         nullptr, nullptr);
    entt::registry& registry = world.Registry();
    auto& bag = registry.get<ecs::Container>(player);
    auto& needs = registry.get<ecs::Needs>(player);
    needs.hunger = 0.80f;
    needs.thirst = 0.80f;
    bag.items.push_back(ecs::ItemInstance{"canned_beans", 1, 1.0f});
    bag.items.push_back(ecs::ItemInstance{"water_bottle", 1, 1.0f});

    const game::InventoryResult ate = service.UseBag(player, 0);
    CHECK(ate.ok);
    CHECK(needs.hunger == doctest::Approx(0.45f));
    CHECK(needs.thirst == doctest::Approx(0.80f));

    const game::InventoryResult drank = service.UseBag(player, 0);
    CHECK(drank.ok);
    CHECK(needs.thirst == doctest::Approx(0.40f));
    CHECK(bag.items.empty());
}

TEST_CASE("F5-T3 服务器食用会写入营养与中毒状态") {
    Fixture fixture;
    ecs::EcsWorld world;
    const ecs::Entity player = MakePlayer(world);
    game::PlayerInventoryService service(world, fixture.items, fixture.recipes,
                                         nullptr, nullptr);
    entt::registry& registry = world.Registry();
    auto& bag = registry.get<ecs::Container>(player);
    bag.items.push_back(ecs::ItemInstance{"tainted_food", 1, 1.0f});

    const game::InventoryResult ate = service.UseBag(player, 0);
    REQUIRE(ate.ok);
    const auto* food = registry.try_get<ecs::FoodState>(player);
    REQUIRE(food != nullptr);
    CHECK(food->nutrition == doctest::Approx(0.4f));
    CHECK(food->poisonRemaining > 0.0f);
    CHECK(food->poisonDamagePerSecond > 0.0f);
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

TEST_CASE("M35-F2-T4 制作事件覆盖开始、完成、中断与批量") {
    Fixture fixture;
    ecs::EcsWorld world;
    const ecs::Entity player = MakePlayer(world);
    core::EventBus events;
    std::vector<core::CraftStartEvent> starts;
    std::vector<core::CraftFinishedEvent> finishes;
    const core::EventBus::SubscriptionId startSubscription = events.Subscribe(
        core::kOnCraftStart, [&](const core::Event& event) {
            if (const auto* payload =
                    std::get_if<core::CraftStartEvent>(&event.payload);
                payload != nullptr) {
                starts.push_back(*payload);
            }
        });
    const core::EventBus::SubscriptionId finishSubscription = events.Subscribe(
        core::kOnCraftFinished, [&](const core::Event& event) {
            if (const auto* payload =
                    std::get_if<core::CraftFinishedEvent>(&event.payload);
                payload != nullptr) {
                finishes.push_back(*payload);
            }
        });
    game::PlayerInventoryService service(world, fixture.items, fixture.recipes,
                                         nullptr, &events);
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
    ui::InventoryAction invalid = craft;
    invalid.fromIndex = 999;
    CHECK_FALSE(service.Dispatch(player, invalid).ok);
    events.FlushQueued();
    CHECK(starts.empty());
    CHECK(finishes.empty());

    REQUIRE(service.Dispatch(player, craft).ok);
    events.FlushQueued();
    REQUIRE(starts.size() == 1u);
    CHECK(starts[0].playerId == world.IdOf(player).id);
    CHECK(starts[0].recipeId == "field_bandage");
    CHECK(starts[0].outputDefId == "bandage");
    CHECK(starts[0].outputCount == 1);
    CHECK(starts[0].batchCount == 2);
    CHECK(finishes.empty());

    game::InventoryResult result;
    REQUIRE(service.TickCrafting(player, 0.6f, &result));
    events.FlushQueued();
    CHECK(starts.size() == 1u);
    CHECK(finishes.empty());
    CHECK(items::CountOf(bag, "bandage") == 1);

    const int remainingRags = items::CountOf(bag, "rag");
    REQUIRE(remainingRags > 0);
    items::RemoveItem(bag, "rag", remainingRags);
    REQUIRE(service.TickCrafting(player, 0.6f, &result));
    events.FlushQueued();
    REQUIRE(finishes.size() == 1u);
    CHECK(finishes[0].playerId == world.IdOf(player).id);
    CHECK(finishes[0].recipeId == "field_bandage");
    CHECK(finishes[0].outputDefId == "bandage");
    CHECK(finishes[0].outputCount == 1);
    CHECK(finishes[0].batchCompleted == 1);
    CHECK(finishes[0].failed);
    CHECK_FALSE(job.active);

    bag.items.push_back(ecs::ItemInstance{"rag", 4, 1.0f});
    REQUIRE(service.Dispatch(player, craft).ok);
    events.FlushQueued();
    REQUIRE(starts.size() == 2u);
    CHECK(starts[1].batchCount == 2);
    REQUIRE(service.TickCrafting(player, 0.6f, &result));
    events.FlushQueued();
    CHECK(finishes.size() == 1u);
    REQUIRE(service.TickCrafting(player, 0.6f, &result));
    events.FlushQueued();
    REQUIRE(finishes.size() == 2u);
    CHECK(finishes[1].batchCompleted == 2);
    CHECK_FALSE(finishes[1].failed);

    bag.items.push_back(ecs::ItemInstance{"rag", 2, 1.0f});
    ui::InventoryAction single = craft;
    single.count = 1;
    REQUIRE(service.Dispatch(player, single).ok);
    events.FlushQueued();
    REQUIRE(starts.size() == 3u);
    ui::InventoryAction cancel;
    cancel.type = ui::InventoryActionType::CancelCraft;
    REQUIRE(service.Dispatch(player, cancel).ok);
    events.FlushQueued();
    REQUIRE(finishes.size() == 3u);
    CHECK(finishes[2].batchCompleted == 0);
    CHECK(finishes[2].failed);
    events.Unsubscribe(startSubscription);
    events.Unsubscribe(finishSubscription);
}

TEST_CASE("M15-T2 服务器背包使用绷带止血并降低伤口") {
    Fixture fixture;
    ecs::EcsWorld world;
    const ecs::Entity player = MakePlayer(world);
    game::PlayerInventoryService service(world, fixture.items, fixture.recipes,
                                         nullptr, nullptr);
    entt::registry& registry = world.Registry();
    auto& bag = registry.get<ecs::Container>(player);
    bag.items.push_back(ecs::ItemInstance{"bandage", 1, 1.0f});
    ecs::Wounds wounds;
    wounds.count = 1;
    wounds.entries[0] =
        ecs::WoundEntry{ecs::DamageType::Bite, 0.4f, 1.2f, 12.5f, 30.0f, false};
    registry.emplace<ecs::Wounds>(player, wounds);

    const game::InventoryResult result = service.UseBag(player, 0);
    CHECK(result.ok);
    CHECK(result.message.find("止血") != std::string::npos);
    const auto& used = registry.get<ecs::Wounds>(player);
    CHECK(used.entries[0].bandaged);
    CHECK(used.entries[0].bleedRemaining == doctest::Approx(0.0f));
    CHECK(zl::characters::CurrentBleedRate(used) == doctest::Approx(0.0f));
    CHECK(used.entries[0].severity == doctest::Approx(0.12f));
    CHECK(registry.get<ecs::Health>(player).current == doctest::Approx(65.0f));
    const float before = registry.get<ecs::Health>(player).current;
    zl::characters::WoundSystem system;
    system.Update(world, 2.0f);
    CHECK(registry.get<ecs::Health>(player).current == doctest::Approx(before));
    CHECK(items::CountOf(bag, "bandage") == 0);
}

TEST_CASE("M15-T4 止痛药短时降低疼痛，重复使用刷新不叠加，无痛不消耗") {
    Fixture fixture;
    ecs::EcsWorld world;
    const ecs::Entity player = MakePlayer(world);
    game::PlayerInventoryService service(world, fixture.items, fixture.recipes,
                                         nullptr, nullptr);
    entt::registry& registry = world.Registry();
    auto& bag = registry.get<ecs::Container>(player);
    bag.items.push_back(ecs::ItemInstance{"painkillers", 2, 1.0f});
    ecs::Pain pain;
    pain.baseLevel = 0.5f;
    pain.level = 0.5f;
    registry.emplace<ecs::Pain>(player, pain);

    const game::InventoryResult first = service.UseBag(player, 0);
    CHECK(first.ok);
    CHECK(first.message.find("止痛") != std::string::npos);
    CHECK(items::CountOf(bag, "painkillers") == 1);
    auto& used = registry.get<ecs::Pain>(player);
    CHECK(used.reliefStrength == doctest::Approx(0.65f));
    CHECK(used.reliefRemaining == doctest::Approx(90.0f));
    CHECK(used.level == doctest::Approx(0.175f));

    const game::InventoryResult second = service.UseBag(player, 0);
    CHECK(second.ok);
    CHECK(items::CountOf(bag, "painkillers") == 0);
    CHECK(used.reliefStrength == doctest::Approx(0.65f));
    CHECK(used.reliefRemaining == doctest::Approx(90.0f));
    CHECK(used.level == doctest::Approx(0.175f));

    ecs::EcsWorld noPainWorld;
    const ecs::Entity noPain = MakePlayer(noPainWorld, 20.0f, 20.0f);
    game::PlayerInventoryService noPainService(noPainWorld, fixture.items,
                                               fixture.recipes, nullptr, nullptr);
    auto& noPainBag = noPainWorld.Registry().get<ecs::Container>(noPain);
    noPainBag.items.push_back(ecs::ItemInstance{"painkillers", 1, 1.0f});
    const game::InventoryResult invalid = noPainService.UseBag(noPain, 0);
    CHECK_FALSE(invalid.ok);
    CHECK(invalid.message.find("没有") != std::string::npos);
    CHECK(items::CountOf(noPainBag, "painkillers") == 1);
}

TEST_CASE("M20-F1 服务器背包使用抗生素降低感染，未感染时不消耗") {
    Fixture fixture;
    ecs::EcsWorld world;
    const ecs::Entity player = MakePlayer(world);
    game::PlayerInventoryService service(world, fixture.items, fixture.recipes,
                                         nullptr, nullptr);
    entt::registry& registry = world.Registry();
    auto& bag = registry.get<ecs::Container>(player);
    bag.items.push_back(ecs::ItemInstance{"antibiotics", 2, 1.0f});
    ecs::Infection infection;
    infection.stage = ecs::InfectionStage::Fever;
    infection.progress = 0.60f;
    infection.timeToNextStage = 100.0f;
    registry.emplace<ecs::Infection>(player, infection);

    const game::InventoryResult first = service.UseBag(player, 0);
    CHECK(first.ok);
    CHECK(first.message.find("感染") != std::string::npos);
    CHECK(items::CountOf(bag, "antibiotics") == 1);
    auto& treated = registry.get<ecs::Infection>(player);
    CHECK(treated.stage == ecs::InfectionStage::Incubating);
    CHECK(treated.progress == doctest::Approx(0.25f));
    CHECK(treated.slowStrength == doctest::Approx(0.5f));
    CHECK(treated.slowRemaining == doctest::Approx(60.0f));

    const game::InventoryResult second = service.UseBag(player, 0);
    CHECK(second.ok);
    CHECK(items::CountOf(bag, "antibiotics") == 0);
    CHECK(registry.get<ecs::Infection>(player).stage ==
          ecs::InfectionStage::None);

    ecs::EcsWorld cleanWorld;
    const ecs::Entity clean = MakePlayer(cleanWorld, 20.0f, 20.0f);
    game::PlayerInventoryService cleanService(cleanWorld, fixture.items,
                                              fixture.recipes, nullptr, nullptr);
    auto& cleanBag = cleanWorld.Registry().get<ecs::Container>(clean);
    cleanBag.items.push_back(ecs::ItemInstance{"antibiotics", 1, 1.0f});
    const game::InventoryResult invalid = cleanService.UseBag(clean, 0);
    CHECK_FALSE(invalid.ok);
    CHECK(invalid.message.find("没有感染") != std::string::npos);
    CHECK(items::CountOf(cleanBag, "antibiotics") == 1);
}

TEST_CASE("F6-T2 服务器使用污染绷带：止血同时触发伤口感染") {
    Fixture fixture;
    ecs::EcsWorld world;
    const ecs::Entity player = MakePlayer(world);
    game::PlayerInventoryService service(world, fixture.items, fixture.recipes,
                                         nullptr, nullptr);
    entt::registry& registry = world.Registry();
    auto& bag = registry.get<ecs::Container>(player);
    bag.items.push_back(ecs::ItemInstance{"tainted_bandage", 1, 1.0f});
    ecs::Wounds wounds;
    wounds.count = 1;
    wounds.entries[0] = ecs::WoundEntry{ecs::DamageType::Claw, 0.5f,
                                        0.0f, 0.0f, 0.0f, false};
    registry.emplace<ecs::Wounds>(player, wounds);

    const game::InventoryResult used = service.UseBag(player, 0);
    CHECK(used.ok);
    CHECK(used.message.find("感染") != std::string::npos);
    CHECK(items::CountOf(bag, "tainted_bandage") == 0);
    CHECK(registry.get<ecs::Wounds>(player).entries[0].bandaged);
    CHECK(registry.get<ecs::Infection>(player).stage ==
          ecs::InfectionStage::Incubating);
}

TEST_CASE("X-F1 2b-2 服务器仓库存取与所有权") {
    Fixture fixture;
    ecs::EcsWorld world;
    const ecs::Entity player = MakePlayer(world, 10.0f, 10.0f);
    game::PlayerInventoryService service(world, fixture.items, fixture.recipes,
                                         nullptr, nullptr);
    entt::registry& registry = world.Registry();
    auto& bag = registry.get<ecs::Container>(player);
    const ecs::Entity storage = game::SpawnPlayerStorage(
        world, 10.5f, 10.5f, 0, "object.crate", 50.0f, 6);
    REQUIRE(static_cast<bool>(storage != entt::null));
    const std::uint64_t storageId = world.IdOf(storage).id;
    registry.emplace<ecs::StructureOwner>(storage,
                                          ecs::StructureOwner{1});
    auto& storageContainer = registry.get<ecs::Container>(storage);

    bag.items.push_back(ecs::ItemInstance{"bandage", 2, 1.0f});
    ui::InventoryAction action;
    action.from = ui::InventoryArea::Loot;
    action.to = ui::InventoryArea::Backpack;
    action.fromEntityId = storageId;
    action.type = ui::InventoryActionType::StoreAll;
    const game::InventoryResult stored = service.Dispatch(player, action);
    CHECK(stored.ok);
    CHECK(bag.items.empty());
    CHECK(items::CountOf(storageContainer, "bandage") == 2);

    action.type = ui::InventoryActionType::TakeAll;
    const game::InventoryResult taken = service.Dispatch(player, action);
    CHECK(taken.ok);
    CHECK(items::CountOf(bag, "bandage") == 2);
    CHECK(storageContainer.items.empty());

    bag.items.clear();
    bag.items.push_back(ecs::ItemInstance{"rag", 2, 1.0f});
    ui::InventoryAction transfer;
    transfer.type = ui::InventoryActionType::Transfer;
    transfer.from = ui::InventoryArea::Backpack;
    transfer.to = ui::InventoryArea::Loot;
    transfer.fromIndex = 0;
    const game::InventoryResult storedOne = service.Dispatch(player, transfer);
    CHECK(storedOne.ok);
    CHECK(items::CountOf(storageContainer, "rag") == 2);
    CHECK(bag.items.empty());

    transfer.from = ui::InventoryArea::Loot;
    transfer.to = ui::InventoryArea::Backpack;
    transfer.fromEntityId = storageId;
    transfer.fromIndex = 0;
    const game::InventoryResult takenOne = service.Dispatch(player, transfer);
    CHECK(takenOne.ok);
    CHECK(items::CountOf(bag, "rag") == 1);
    CHECK(items::CountOf(storageContainer, "rag") == 1);

    registry.get<ecs::StructureOwner>(storage).ownerStableId = 99;
    const game::InventoryResult denied = service.Dispatch(player, action);
    CHECK_FALSE(denied.ok);
    CHECK(items::CountOf(bag, "rag") == 1);
    CHECK(items::CountOf(storageContainer, "rag") == 1);
}
