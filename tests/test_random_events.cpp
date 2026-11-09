#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/RandomEvents.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"
#include "items/LootTable.h"

using namespace zl;

namespace {

const char* kItemsJson = R"({
  "format": "items-v1",
  "items": [
    { "id": "bandage", "name": "Bandage", "icon": "i", "weight": 0.1, "maxStack": 5, "type": "consumable" }
  ]
})";

const char* kLootJson = R"({
  "format": "loot-v1",
  "seed": 42,
  "tables": [
    { "id": "event_supply_crate", "name": "Supply", "capacityKg": 24.0, "rolls": 0 },
    { "id": "event_infected_cache", "name": "Cache", "capacityKg": 18.0, "rolls": 0 }
  ]
})";

struct EventFixture {
    ecs::EcsWorld world;
    items::ItemRegistry items;
    items::LootTableRegistry loot;
    int zombieSpawns = 0;
    game::RandomEventSpawnCallbacks callbacks;

    EventFixture() {
        std::string error;
        REQUIRE(items.LoadFromString(kItemsJson, "test-items", &error));
        REQUIRE(loot.LoadFromString(kLootJson, "test-loot", items, &error));
        callbacks.walkable = [](int, int) { return true; };
        callbacks.spawnZombie =
            [this](float x, float y, ecs::ZombieVariantKind,
                   std::uint64_t stableId) {
                const ecs::Entity entity =
                    world.CreateWithStableId(stableId);
                if (entity == entt::null) {
                    return entity;
                }
                world.Registry().emplace<ecs::Position>(
                    entity, ecs::Position{x, y, 0});
                world.Registry().emplace<ecs::Health>(
                    entity, ecs::Health{100.0f, 100.0f});
                ++zombieSpawns;
                return entity;
            };
    }

    game::RandomEventSpawnResult Ensure(std::string_view id, int day,
                                        int progress = 0) {
        game::RandomEventSpawnResult result;
        REQUIRE(game::EnsureRandomEventContent(
            world, loot, id, day, progress, 70.0f, 24.0f, callbacks, &result));
        return result;
    }

    int CountContainers(std::string_view tableId) const {
        int count = 0;
        world.Registry().view<ecs::LootContainer>().each(
            [&](const ecs::Entity, const ecs::LootContainer& loot) {
                if (loot.tableId == tableId) {
                    ++count;
                }
            });
        return count;
    }

    int CountActors(std::string_view eventId) const {
        int count = 0;
        world.Registry().view<ecs::RandomEventActor>().each(
            [&](const ecs::Entity, const ecs::RandomEventActor& actor) {
                if (actor.eventId == eventId) {
                    ++count;
                }
            });
        return count;
    }
};

}  // namespace

TEST_CASE("M24-T4 随机事件分类、站点与 StableId 确定性") {
    CHECK(game::RandomEventKindForMission("event_supply_drop") ==
          game::RandomEventKind::SupplyDrop);
    CHECK(game::RandomEventKindForMission("event_wandering_horde") ==
          game::RandomEventKind::WanderingHorde);
    CHECK(game::RandomEventKindForMission("event_infected_house") ==
          game::RandomEventKind::InfectedHouse);
    CHECK(game::RandomEventKindForMission("event_survivor_ambush") ==
          game::RandomEventKind::SurvivorAmbush);
    CHECK(game::RandomEventKindForMission("event_cold_snap") ==
          game::RandomEventKind::ColdSnap);
    CHECK(game::RandomEventKindForMission("radio_repair") ==
          game::RandomEventKind::None);
    CHECK(std::string(game::RandomEventLootName("event_supply_crate")) ==
          "空投补给箱");
    CHECK(std::string(game::RandomEventLootName("event_infected_cache")) ==
          "感染者藏匿箱");
    CHECK(game::RandomEventLootName("poi_supermarket_food") == nullptr);

    float x = 0.0f;
    float y = 0.0f;
    REQUIRE(game::RandomEventFallbackSite("event_supply_drop", 2, 70.0f, 24.0f,
                                          &x, &y));
    CHECK(x == doctest::Approx(62.5f));
    CHECK(y == doctest::Approx(29.5f));
    REQUIRE(game::RandomEventFallbackSite("event_wandering_horde", 3, 70.0f,
                                          24.0f, &x, &y));
    const float day3X = x;
    REQUIRE(game::RandomEventFallbackSite("event_wandering_horde", 4, 70.0f,
                                          24.0f, &x, &y));
    CHECK(x != doctest::Approx(day3X));
    REQUIRE(game::RandomEventFallbackSite("event_cold_snap", 2, 10.0f, 10.0f,
                                          &x, &y));
    CHECK(x == doctest::Approx(70.0f));
    CHECK(y == doctest::Approx(24.0f));

    const std::uint64_t first =
        game::RandomEventStableId("event_supply_drop", 3, 1);
    CHECK(first == game::RandomEventStableId("event_supply_drop", 3, 1));
    CHECK(first != game::RandomEventStableId("event_supply_drop", 4, 1));
    CHECK(first != game::RandomEventStableId("event_supply_drop", 3, 2));
    CHECK(first != game::RandomEventStableId("event_infected_house", 3, 1));
}

TEST_CASE("M24-T4 空投箱按 StableId 幂等生成并兼容旧进度") {
    EventFixture fixture;
    const game::RandomEventSpawnResult first =
        fixture.Ensure("event_supply_drop", 3);
    CHECK(first.content);
    CHECK(first.spawned == 2);
    CHECK(first.total == 2);
    CHECK(fixture.CountContainers("event_supply_crate") == 2);

    const game::RandomEventSpawnResult second =
        fixture.Ensure("event_supply_drop", 3);
    CHECK(second.spawned == 0);
    CHECK(second.total == 2);
    CHECK(fixture.CountContainers("event_supply_crate") == 2);

    EventFixture oldSave;
    const game::RandomEventSpawnResult skipped =
        oldSave.Ensure("event_supply_drop", 3, 1);
    CHECK(skipped.content);
    CHECK(skipped.spawned == 0);
    CHECK(skipped.total == 0);
    CHECK(oldSave.CountContainers("event_supply_crate") == 0);
}

TEST_CASE("M24-T4 尸潮/伏击/房屋生成专属敌人且不重复") {
    EventFixture fixture;
    const game::RandomEventSpawnResult horde =
        fixture.Ensure("event_wandering_horde", 3);
    CHECK(horde.spawned == 8);
    CHECK(fixture.zombieSpawns == 8);
    CHECK(fixture.CountActors("event_wandering_horde") == 8);
    const game::RandomEventSpawnResult hordeAgain =
        fixture.Ensure("event_wandering_horde", 3);
    CHECK(hordeAgain.spawned == 0);
    CHECK(hordeAgain.total == 8);
    CHECK(fixture.zombieSpawns == 8);

    const game::RandomEventSpawnResult ambush =
        fixture.Ensure("event_survivor_ambush", 4);
    CHECK(ambush.spawned == 2);
    CHECK(fixture.CountActors("event_survivor_ambush") == 2);

    const game::RandomEventSpawnResult house =
        fixture.Ensure("event_infected_house", 2);
    CHECK(house.spawned == 6);
    CHECK(fixture.CountActors("event_infected_house") == 5);
    CHECK(fixture.CountContainers("event_infected_cache") == 1);

    const game::RandomEventSpawnResult cold =
        fixture.Ensure("event_cold_snap", 2);
    CHECK_FALSE(cold.content);
    CHECK(cold.spawned == 0);
    CHECK(cold.siteX == doctest::Approx(70.0f));
    CHECK(cold.siteY == doctest::Approx(24.0f));
}
