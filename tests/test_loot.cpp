#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/LootContainers.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"
#include "items/LootTable.h"
#include "save/BinaryReader.h"
#include "save/BinaryWriter.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"

using namespace zl;

namespace {

const char* kItemsJson = R"({
  "format": "items-v1",
  "items": [
    { "id": "bandage", "name": "Bandage", "icon": "i", "weight": 0.1, "maxStack": 5, "type": "consumable" },
    { "id": "rag", "name": "Rag", "icon": "i", "weight": 0.1, "maxStack": 10, "type": "generic" },
    { "id": "repair_kit", "name": "Repair Kit", "icon": "i", "weight": 0.3, "maxStack": 3, "type": "generic" },
    { "id": "axe", "name": "Axe", "icon": "i", "weight": 3.0, "maxStack": 1, "type": "weapon" }
  ]
})";

const char* kLootJson = R"({
  "format": "loot-v1",
  "seed": 42,
  "tables": [
    {
      "id": "test_crate",
      "name": "Test Crate",
      "capacityKg": 10.0,
      "rolls": 4,
      "guaranteed": [ { "defId": "repair_kit", "count": 1 } ],
      "entries": [
        { "defId": "bandage", "weight": 1, "min": 1, "max": 2 },
        { "defId": "rag", "weight": 1, "min": 2, "max": 4 }
      ]
    },
    {
      "id": "empty",
      "name": "Empty",
      "rolls": 0
    },
    {
      "id": "weighted",
      "name": "Weighted",
      "rolls": 1,
      "entries": [
        { "defId": "axe", "weight": 9, "min": 1, "max": 1 },
        { "defId": "bandage", "weight": 1, "min": 1, "max": 1 }
      ]
    }
  ]
})";

struct Fixture {
    items::ItemRegistry items;
    items::LootTableRegistry loot;

    Fixture() {
        std::string error;
        REQUIRE(items.LoadFromString(kItemsJson, "test-items", &error));
        REQUIRE(loot.LoadFromString(kLootJson, "test-loot", items, &error));
    }
};

int CountStacks(const ecs::Container& container) {
    int count = 0;
    for (const ecs::ItemInstance& item : container.items) {
        count += item.count;
    }
    return count;
}

bool SameContents(const ecs::Container& left, const ecs::Container& right) {
    if (left.items.size() != right.items.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.items.size(); ++i) {
        if (left.items[i].defId != right.items[i].defId ||
            left.items[i].count != right.items[i].count ||
            left.items[i].condition != right.items[i].condition) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("M14-T5 loot.json 解析、校验与 deterministic roll") {
    Fixture fixture;
    CHECK(fixture.loot.Size() == 3u);
    CHECK(fixture.loot.Seed() == 42u);

    const items::LootTable* crate = fixture.loot.Find("test_crate");
    REQUIRE(crate != nullptr);
    CHECK(crate->name == "Test Crate");
    CHECK(crate->capacityKg == doctest::Approx(10.0f));
    CHECK(crate->rolls == 4);
    CHECK(crate->guaranteed.size() == 1u);
    CHECK(crate->entries.size() == 2u);

    ecs::Container first;
    ecs::Container second;
    const items::LootRollResult rolled =
        items::RollLootTable(*crate, fixture.items, first, 1234u);
    const items::LootRollResult rolledAgain =
        items::RollLootTable(*crate, fixture.items, second, 1234u);
    CHECK(rolled.itemsAdded > 0);
    CHECK(rolled.itemsAdded == rolledAgain.itemsAdded);
    CHECK(rolled.stacksAdded == rolledAgain.stacksAdded);
    CHECK(SameContents(first, second));
    CHECK(items::CountOf(first, "repair_kit") == 1);

    ecs::Container different;
    (void)items::RollLootTable(*crate, fixture.items, different, 9876u);
    CHECK(items::CountOf(different, "repair_kit") == 1);
    CHECK(items::CountOf(first, "bandage") + items::CountOf(first, "rag") > 0);

    const items::LootTable* empty = fixture.loot.Find("empty");
    REQUIRE(empty != nullptr);
    ecs::Container emptyContainer;
    const items::LootRollResult emptyRoll =
        items::RollLootTable(*empty, fixture.items, emptyContainer, 1u);
    CHECK(emptyRoll.itemsAdded == 0);
    CHECK(emptyContainer.items.empty());

    std::string error;
    CHECK_FALSE(fixture.loot.LoadFromString(
        R"({ "format": "loot-v1", "tables": [
          { "id": "bad", "name": "Bad", "rolls": 1,
            "entries": [ { "defId": "missing", "weight": 1, "min": 1, "max": 1 } ] }
        ] })",
        "bad-loot", fixture.items, &error));
    CHECK(error.find("missing") != std::string::npos);

    error.clear();
    CHECK_FALSE(fixture.loot.LoadFromString(
        R"({ "format": "loot-v1", "tables": [
          { "id": "duplicate", "name": "First", "rolls": 0 },
          { "id": "duplicate", "name": "Second", "rolls": 0 }
        ] })",
        "duplicate-loot", fixture.items, &error));
    CHECK(error.find("重复") != std::string::npos);

    error.clear();
    CHECK_FALSE(fixture.loot.LoadFromString(
        R"({ "format": "loot-v1", "tables": [
          { "id": "bad_rolls", "name": "Bad", "rolls": 1 }
        ] })",
        "bad-rolls", fixture.items, &error));
    CHECK(error.find("entries") != std::string::npos);

    error.clear();
    CHECK_FALSE(fixture.loot.LoadFromString(
        R"({ "format": "loot-v1", "tables": [
          { "id": "bad_count", "name": "Bad", "rolls": 0,
            "guaranteed": [ { "defId": "axe", "count": 2 } ] }
        ] })",
        "bad-count", fixture.items, &error));
    CHECK(error.find("数量") != std::string::npos);
}

TEST_CASE("M14-T5 容器首次搜索roll 一次、空容器与距离查") {
    Fixture fixture;
    ecs::EcsWorld world;
    const ecs::Entity crate = game::SpawnLootContainer(
        world, fixture.loot, "test_crate", 10.5f, 20.5f, 0, "object.crate");
    REQUIRE(crate != ecs::EcsWorld::kNull);
    REQUIRE(world.Registry().all_of<ecs::Position, ecs::Container,
                                   ecs::LootContainer, ecs::SpriteRef>(crate));

    const ecs::Entity nearest =
        game::FindNearestLootContainer(world, 10.0f, 20.0f, 0, 2.0f);
    CHECK(nearest == crate);
    CHECK(game::FindNearestLootContainer(world, 10.0f, 20.0f, 1, 2.0f) ==
          ecs::EcsWorld::kNull);

    const auto* before = world.Registry().try_get<ecs::Container>(crate);
    REQUIRE(before != nullptr);
    CHECK(before->items.empty());
    const game::LootSearchResult first =
        game::SearchLootContainer(world, fixture.items, fixture.loot, crate);
    CHECK(first.found);
    CHECK(first.searchedNow);
    CHECK(first.itemStacks > 0);
    CHECK(first.totalItems > 0);
    CHECK(first.tableId == "test_crate");
    CHECK(first.error.empty());

    const auto* searched = world.Registry().try_get<ecs::Container>(crate);
    REQUIRE(searched != nullptr);
    const std::vector<ecs::ItemInstance> snapshot = searched->items;
    const game::LootSearchResult second =
        game::SearchLootContainer(world, fixture.items, fixture.loot, crate);
    CHECK(second.found);
    CHECK_FALSE(second.searchedNow);
    CHECK(second.totalItems == first.totalItems);
    REQUIRE(searched->items.size() == snapshot.size());
    for (std::size_t i = 0; i < snapshot.size(); ++i) {
        CHECK(searched->items[i].defId == snapshot[i].defId);
        CHECK(searched->items[i].count == snapshot[i].count);
    }

    const ecs::Entity empty = game::SpawnLootContainer(
        world, fixture.loot, "empty", 30.0f, 30.0f, 0, "object.locker");
    REQUIRE(empty != ecs::EcsWorld::kNull);
    const game::LootSearchResult emptySearch =
        game::SearchLootContainer(world, fixture.items, fixture.loot, empty);
    CHECK(emptySearch.found);
    CHECK(emptySearch.searchedNow);
    CHECK(emptySearch.totalItems == 0);
    CHECK(world.Registry().get<ecs::LootContainer>(empty).searched);
}

TEST_CASE("M14-T5 容器拿取遵守背包容量且失败不改源容器") {
    Fixture fixture;
    ecs::EcsWorld world;
    const ecs::Entity crate = game::SpawnLootContainer(
        world, fixture.loot, "test_crate", 0.5f, 0.5f, 0, "object.crate");
    REQUIRE(crate != ecs::EcsWorld::kNull);
    REQUIRE(game::SearchLootContainer(world, fixture.items, fixture.loot, crate)
                .searchedNow);

    auto& source = world.Registry().get<ecs::Container>(crate);
    int repairIndex = -1;
    for (std::size_t i = 0; i < source.items.size(); ++i) {
        if (source.items[i].defId == "repair_kit") {
            repairIndex = static_cast<int>(i);
            break;
        }
    }
    REQUIRE(repairIndex >= 0);
    const int before = items::CountOf(source, "repair_kit");

    ecs::Container tiny{{}, 0.05f};
    const game::LootTakeResult full = game::TakeLootContainerItem(
        world, fixture.items, crate, tiny, repairIndex);
    CHECK(full.found);
    CHECK(full.taken == 0);
    CHECK(items::CountOf(source, "repair_kit") == before);

    ecs::Container bag{{}, 20.0f};
    const game::LootTakeResult taken = game::TakeLootContainerItem(
        world, fixture.items, crate, bag, repairIndex);
    CHECK(taken.found);
    CHECK(taken.taken == 1);
    CHECK(taken.defId == "repair_kit");
    CHECK(items::CountOf(source, "repair_kit") == before - 1);
    CHECK(items::CountOf(bag, "repair_kit") == 1);

    const game::LootTakeResult invalid =
        game::TakeLootContainerItem(world, fixture.items, crate, bag, 99);
    CHECK_FALSE(invalid.found);
    CHECK(invalid.taken == 0);

    ecs::Container fullBag{{}, 0.05f};
    int bandageIndex = -1;
    for (std::size_t i = 0; i < source.items.size(); ++i) {
        if (source.items[i].defId == "bandage") {
            bandageIndex = static_cast<int>(i);
            break;
        }
    }
    if (bandageIndex >= 0) {
        const int sourceBefore = items::CountOf(source, "bandage");
        const game::LootTakeResult partial = game::TakeLootContainerItem(
            world, fixture.items, crate, fullBag, bandageIndex, 2);
        CHECK(partial.taken <= 1);
        CHECK(items::CountOf(source, "bandage") == sourceBefore - partial.taken);
        CHECK(CountStacks(fullBag) == partial.taken);
    }
}

TEST_CASE("M14-T5 仓库 loot.json 与物品定义一") {
    items::ItemRegistry itemRegistry;
    items::LootTableRegistry lootRegistry;
    std::string error;
    const std::string itemsPath = std::string(ZL_SOURCE_DIR) + "/assets/items.json";
    const std::string lootPath = std::string(ZL_SOURCE_DIR) + "/assets/loot.json";
    REQUIRE(itemRegistry.LoadFile(itemsPath, &error));
    REQUIRE(lootRegistry.LoadFile(lootPath, itemRegistry, &error));
    CHECK(lootRegistry.Seed() == 20261022u);
    CHECK(lootRegistry.Size() >= 3u);
    CHECK(lootRegistry.Find("village_crate") != nullptr);
    CHECK(lootRegistry.Find("village_locker") != nullptr);
    CHECK(lootRegistry.Find("house_kitchen") != nullptr);
    CHECK(lootRegistry.Find("house_workshop") != nullptr);
    CHECK(itemRegistry.Find("radio_part") != nullptr);
    for (const items::LootTable& table : lootRegistry.All()) {
        CHECK(table.rolls >= 0);
        CHECK(table.capacityKg > 0.0f);
        if (table.rolls > 0) {
            CHECK_FALSE(table.entries.empty());
        }
        for (const items::LootEntry& entry : table.entries) {
            CHECK(itemRegistry.Find(entry.defId) != nullptr);
            CHECK(entry.minCount >= 1);
            CHECK(entry.maxCount >= entry.minCount);
        }
    }
}

TEST_CASE("M14-T5 LootContainer 存档往") {
    save::ComponentRegistry components;
    std::string error;
    REQUIRE(save::RegisterBuiltinComponents(components, &error));
    CHECK(error.empty());
    CHECK(components.Size() == 16u);

    ecs::EcsWorld source;
    const ecs::Entity container = source.Create();
    entt::registry& sourceRegistry = source.Registry();
    sourceRegistry.emplace<ecs::Position>(container, ecs::Position{1.5f, 2.5f, 0});
    sourceRegistry.emplace<ecs::SpriteRef>(
        container, ecs::SpriteRef{ecs::HashName("object.crate"), 0, 0xFFFFFFFFu});
    sourceRegistry.emplace<ecs::Container>(
        container, ecs::Container{{ecs::ItemInstance{"bandage", 2, 0.75f}}, 12.0f});
    sourceRegistry.emplace<ecs::LootContainer>(
        container, ecs::LootContainer{"test_crate", 123456u, true});

    save::SavedEntity saved;
    REQUIRE(save::CaptureEntity(source, container, components, &saved, &error));
    CHECK(error.empty());
    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(save::EntitySnapshot{{saved}}, &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
    CHECK(error.empty());

    ecs::EcsWorld target;
    const ecs::Entity loaded =
        target.CreateWithStableId(decoded.entities[0].stableId);
    REQUIRE(loaded != ecs::EcsWorld::kNull);
    REQUIRE(save::ApplyEntity(decoded.entities[0], target, loaded, components,
                              &error));
    const auto* loot = target.Registry().try_get<ecs::LootContainer>(loaded);
    REQUIRE(loot != nullptr);
    CHECK(loot->tableId == "test_crate");
    CHECK(loot->rollSeed == 123456u);
    CHECK(loot->searched);
    const auto* bag = target.Registry().try_get<ecs::Container>(loaded);
    REQUIRE(bag != nullptr);
    CHECK(bag->capacityKg == doctest::Approx(12.0f));
    REQUIRE(bag->items.size() == 1u);
    CHECK(bag->items[0].defId == "bandage");
    CHECK(bag->items[0].count == 2);
    CHECK(bag->items[0].condition == doctest::Approx(0.75f));
}
