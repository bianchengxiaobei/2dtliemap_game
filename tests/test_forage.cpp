#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "game/ActionHints.h"
#include "game/FishingRuntime.h"
#include "game/Forage.h"
#include "game/ForageRuntime.h"
#include "game/Poi.h"
#include "game/Weather.h"
#include "items/ItemRegistry.h"
#include "items/LootTable.h"
#include "world/ChunkData.h"
#include "world/GridSquare.h"
#include "world/MapLoader.h"
#include "world/World.h"

using namespace zl;

namespace {

const char* kItems = R"({
  "format": "items-v1",
  "items": [
    { "id": "forage_stick", "name": "树枝", "icon": "item.wood", "weight": 0.2, "maxStack": 10, "type": "generic" },
    { "id": "fishing_rod", "name": "鱼竿", "icon": "item.wood", "weight": 1.0, "maxStack": 1, "type": "generic" },
    { "id": "fish_bait", "name": "鱼饵", "icon": "item.energy_bar", "weight": 0.02, "maxStack": 10, "type": "generic" },
    { "id": "small_fish", "name": "小鱼", "icon": "item.energy_bar", "weight": 0.3, "maxStack": 3, "type": "consumable" },
    { "id": "river_trash", "name": "河漂杂物", "icon": "item.rag", "weight": 0.2, "maxStack": 3, "type": "generic" }
  ]
})";

const char* kLoot = R"({
  "format": "loot-v1",
  "seed": 7,
  "tables": [
    { "id": "forage_forest", "name": "森林地表", "capacityKg": 8.0, "rolls": 0 },
    { "id": "forage_meadow", "name": "草地", "capacityKg": 8.0, "rolls": 0 },
    { "id": "fish_water", "name": "静水鱼获", "capacityKg": 6.0, "rolls": 0 },
    { "id": "fish_water_trash", "name": "静水杂物", "capacityKg": 6.0, "rolls": 0 }
  ]
})";

const char* kForage = R"({
  "format": "forage-v1",
  "seed": 99,
  "forageZones": [
    {
      "id": "forest",
      "name": "森林",
      "tableId": "forage_forest",
      "floors": ["tile.floor.grass"],
      "requiredNearbySprites": ["object.tree"],
      "nearbyRadius": 3.0,
      "density": 1.0,
      "spacing": 2,
      "maxNodes": 8,
      "respawnMinutes": 240,
      "searchSeconds": 3.0,
      "toolDefId": "forage_stick",
      "toolWear": 0.05,
      "survivalXp": 12
    },
    {
      "id": "meadow",
      "name": "草地",
      "tableId": "forage_meadow",
      "floors": ["tile.floor.grass"],
      "excludedNearbySprites": ["object.tree"],
      "nearbyRadius": 2.0,
      "density": 1.0,
      "spacing": 2,
      "maxNodes": 8,
      "respawnMinutes": 180
    }
  ],
  "fishingSpots": [
    {
      "id": "pond",
      "name": "池塘",
      "tableId": "fish_water",
      "trashTableId": "fish_water_trash",
      "waterFloors": ["tile.floor.water"],
      "bankRadius": 3,
      "density": 1.0,
      "spacing": 2,
      "maxSpots": 4,
      "respawnMinutes": 360,
      "waitSeconds": 5.0,
      "biteWindowSeconds": 2.0,
      "catchChance": 0.6,
      "survivalXp": 10,
      "rodDefId": "fishing_rod",
      "rodWear": 0.03,
      "baitDefId": "fish_bait",
      "baitCost": 1
    }
  ]
})";

struct Fixture {
    items::ItemRegistry items;
    items::LootTableRegistry loot;
    game::ForageRegistry forage;

    Fixture() {
        std::string error;
        REQUIRE(items.LoadFromString(kItems, "test-items", &error));
        REQUIRE(loot.LoadFromString(kLoot, "test-loot", items, &error));
        REQUIRE(forage.LoadFromString(kForage, "test-forage", loot, &error));
    }
};

void SetFloor(world::World& world, int x, int y, std::string sprite) {
    world::SquareObject floor;
    floor.sprite = std::move(sprite);
    world.GetOrCreateSquare(x, y, 0).SetFloor(std::move(floor));
}

void AddTree(world::World& world, int x, int y) {
    world::SquareObject tree;
    tree.sprite = "object.tree";
    tree.blocksMovement = true;
    world.GetOrCreateSquare(x, y, 0).AddObject(world::SquareLayer::Furniture,
                                               std::move(tree));
}

}  // namespace

TEST_CASE("M23-F2a-T1 内置 forage 数据可加载") {
    items::ItemRegistry items;
    items::LootTableRegistry loot;
    game::ForageRegistry forage;
    const std::string root = ZL_SOURCE_DIR;
    REQUIRE(items.LoadFile(root + "/assets/items.json"));
    REQUIRE(loot.LoadFile(root + "/assets/loot.json", items));
    REQUIRE(forage.LoadFile(root + "/assets/forage.json", loot));
    CHECK(forage.ZoneCount() == 2u);
    CHECK(forage.SpotCount() == 1u);
    CHECK(forage.FindZone("forest_understory") != nullptr);
    CHECK(forage.FindZone("meadow_grass") != nullptr);
    CHECK(forage.FindSpot("still_water") != nullptr);
}

TEST_CASE("M23-F2a-T1 forage-v1 解析、校验与 mod 追加") {
    Fixture fixture;
    CHECK(fixture.forage.Seed() == 99u);
    REQUIRE(fixture.forage.ZoneCount() == 2u);
    REQUIRE(fixture.forage.SpotCount() == 1u);

    const game::ForageZoneDef* forest = fixture.forage.FindZone("forest");
    REQUIRE(forest != nullptr);
    CHECK(forest->tableId == "forage_forest");
    CHECK(forest->requiredNearbySprites.size() == 1u);
    CHECK(forest->maxNodes == 8);
    CHECK(forest->searchSeconds == doctest::Approx(3.0f));
    CHECK(forest->toolDefId == "forage_stick");
    CHECK(forest->toolWear == doctest::Approx(0.05f));
    CHECK(forest->survivalXp == doctest::Approx(12.0f));
    CHECK(fixture.forage.FindZone("missing") == nullptr);
    CHECK(fixture.forage.FindSpot("pond") != nullptr);

    std::string error;
    CHECK_FALSE(fixture.forage.LoadFromString(
        R"({ "format": "forage-v1", "forageZones": [
          { "id": "bad", "name": "坏表", "tableId": "missing_table",
            "floors": ["tile.floor.grass"] }
        ], "fishingSpots": [] })",
        "bad-forage", fixture.loot, &error));
    CHECK(error.find("missing_table") != std::string::npos);

    CHECK_FALSE(fixture.forage.LoadFromString(
        R"({ "format": "forage-v1", "forageZones": [
          { "id": "dup", "name": "A", "tableId": "forage_forest",
            "floors": ["tile.floor.grass"] },
          { "id": "dup", "name": "B", "tableId": "forage_forest",
            "floors": ["tile.floor.grass"] }
        ], "fishingSpots": [] })",
        "duplicate-forage", fixture.loot, &error));
    CHECK(error.find("id 重复") != std::string::npos);

    CHECK_FALSE(fixture.forage.LoadFromString(
        R"({ "format": "forage-v1", "forageZones": [
          { "id": "fast", "name": "太快", "tableId": "forage_forest",
            "floors": ["tile.floor.grass"], "searchSeconds": 0.1 }
        ], "fishingSpots": [] })",
        "bad-search-seconds", fixture.loot, &error));
    CHECK(error.find("searchSeconds") != std::string::npos);

    CHECK_FALSE(fixture.forage.LoadFromString(
        R"({ "format": "forage-v1", "forageZones": [
          { "id": "wear", "name": "坏磨损", "tableId": "forage_forest",
            "floors": ["tile.floor.grass"], "toolWear": 0.5 }
        ], "fishingSpots": [] })",
        "bad-tool-wear", fixture.loot, &error));
    CHECK(error.find("toolDefId") != std::string::npos);

    error.clear();
    REQUIRE(fixture.forage.MergeFromString(
        R"({ "format": "forage-v1", "forageZones": [
          { "id": "mod_forest", "name": "Mod 森林", "tableId": "forage_forest",
            "floors": ["tile.floor.grass"], "requiredNearbySprites": ["object.tree"] }
        ], "fishingSpots": [] })",
        "mod-forage", fixture.loot, &error));
    CHECK(fixture.forage.ZoneCount() == 3u);
    CHECK(fixture.forage.FindZone("mod_forest") != nullptr);

    CHECK_FALSE(fixture.forage.MergeFromString(
        R"({ "format": "forage-v1", "forageZones": [
          { "id": "forest", "name": "重复", "tableId": "forage_forest",
            "floors": ["tile.floor.grass"] }
        ], "fishingSpots": [] })",
        "duplicate-mod", fixture.loot, &error));
    CHECK(error.find("冲突") != std::string::npos);
}

TEST_CASE("M23-F2a-T1 地形/树木/水面确定性生成节点") {
    Fixture fixture;
    world::World world;
    for (int y = 0; y <= 9; ++y) {
        for (int x = 0; x <= 9; ++x) {
            SetFloor(world, x, y, "tile.floor.grass");
        }
    }
    AddTree(world, 2, 2);
    AddTree(world, 8, 8);
    game::PoiConfig pois;

    const game::ForageBounds forestBounds{0, 0, 4, 4};
    const std::vector<game::ForageNode> first =
        game::PlanForageNodes(fixture.forage, world, pois, forestBounds);
    const std::vector<game::ForageNode> second =
        game::PlanForageNodes(fixture.forage, world, pois, forestBounds);
    REQUIRE(first.size() == second.size());
    REQUIRE_FALSE(first.empty());
    bool foundForest = false;
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].zoneId == second[i].zoneId);
        CHECK(first[i].x == second[i].x);
        CHECK(first[i].y == second[i].y);
        CHECK(first[i].x >= 0);
        CHECK(first[i].x <= 4);
        if (first[i].zoneId == "forest") {
            foundForest = true;
            CHECK(first[i].tableId == "forage_forest");
        } else {
            CHECK(first[i].zoneId == "meadow");
            CHECK(first[i].tableId == "forage_meadow");
        }
    }
    CHECK(foundForest);

    const std::vector<game::ForageNode> meadow =
        game::PlanForageNodes(fixture.forage, world, pois, {0, 0, 9, 9});
    bool foundMeadow = false;
    for (const game::ForageNode& node : meadow) {
        if (node.zoneId == "meadow") {
            foundMeadow = true;
            CHECK(node.tableId == "forage_meadow");
        }
        const bool atTree = node.x == 2 && node.y == 2;
        CHECK_FALSE(atTree);
    }
    CHECK(foundMeadow);

    SetFloor(world, 5, 5, "tile.floor.water");
    const std::vector<game::FishingSpot> spots =
        game::PlanFishingSpots(fixture.forage, world, pois, {4, 4, 6, 6});
    REQUIRE(spots.size() == 1u);
    CHECK(spots[0].spotId == "pond");
    CHECK(spots[0].tableId == "fish_water");
    CHECK(spots[0].waterX == 5);
    CHECK(spots[0].waterY == 5);
    const bool onWater = spots[0].x == 5 && spots[0].y == 5;
    CHECK_FALSE(onWater);
}

TEST_CASE("M23-F2a-T1 房间、阻挡与 POI 过滤") {
    Fixture fixture;
    world::World world;
    for (int y = 0; y <= 8; ++y) {
        for (int x = 0; x <= 8; ++x) {
            SetFloor(world, x, y, "tile.floor.grass");
        }
    }
    AddTree(world, 2, 2);
    world.GetOrCreateSquare(3, 3, 0).SetRoomId(4);
    world::SquareObject wall;
    wall.sprite = "wall.brick";
    wall.blocksMovement = true;
    world.GetOrCreateSquare(3, 2, 0).AddObject(world::SquareLayer::Wall,
                                               std::move(wall));

    game::PoiConfig pois;
    game::PoiDef poi;
    poi.id = "camp";
    poi.name = "营地";
    poi.markerX = 7.5f;
    poi.markerY = 7.5f;
    pois.defs.push_back(poi);

    const std::vector<game::ForageNode> nodes =
        game::PlanForageNodes(fixture.forage, world, pois, {0, 0, 8, 8});
    for (const game::ForageNode& node : nodes) {
        const bool inRoom = node.x == 3 && node.y == 3;
        const bool onWall = node.x == 3 && node.y == 2;
        const bool onTree = node.x == 2 && node.y == 2;
        CHECK_FALSE(inRoom);
        CHECK_FALSE(onWall);
        CHECK_FALSE(onTree);
    }

    game::ForageRegistry filtered;
    std::string error;
    REQUIRE(filtered.LoadFromString(
        R"({ "format": "forage-v1", "forageZones": [
          { "id": "camp_only", "name": "营地", "tableId": "forage_forest",
            "floors": ["tile.floor.grass"], "poiIds": ["camp"], "poiRadius": 2.0,
            "density": 1.0, "spacing": 0, "maxNodes": 64 }
        ], "fishingSpots": [] })",
        "poi-forage", fixture.loot, &error));
    const std::vector<game::ForageNode> campNodes =
        game::PlanForageNodes(filtered, world, pois, {0, 0, 8, 8});
    REQUIRE_FALSE(campNodes.empty());
    for (const game::ForageNode& node : campNodes) {
        CHECK(node.x >= 6);
        CHECK(node.y >= 6);
    }
}

TEST_CASE("M23-F2a-T2 搜索时长受技能与天气修正") {
    Fixture fixture;
    const game::ForageZoneDef* forest = fixture.forage.FindZone("forest");
    REQUIRE(forest != nullptr);

    game::WeatherState clear;
    CHECK(game::ForageSearchSeconds(*forest, clear, 0) ==
          doctest::Approx(3.0f));

    game::WeatherState rain;
    rain.kind = game::WeatherKind::Rain;
    rain.intensity = 1.0f;
    CHECK(game::ForageSearchSeconds(*forest, rain, 0) > 3.0f);

    game::WeatherState fog;
    fog.kind = game::WeatherKind::Fog;
    fog.intensity = 1.0f;
    CHECK(game::ForageSearchSeconds(*forest, fog, 0) > 3.0f);

    game::WeatherState cold;
    cold.kind = game::WeatherKind::ColdSnap;
    cold.intensity = 1.0f;
    CHECK(game::ForageSearchSeconds(*forest, cold, 0) >
          game::ForageSearchSeconds(*forest, rain, 0));

    game::WeatherState night = clear;
    night.night = true;
    CHECK(game::ForageSearchSeconds(*forest, night, 0) >
          game::ForageSearchSeconds(*forest, clear, 0));
    CHECK(game::ForageSearchSeconds(*forest, clear, 5) <
          game::ForageSearchSeconds(*forest, clear, 0));
}

TEST_CASE("M23-F2a-T2 节点耗尽、刷新与战利品种子") {
    game::ForageNode node{"forest", "forage_forest", 4, 5, 0};
    ecs::ForageState state;
    CHECK(game::ForageNodeReady(state, node, 100.0));

    game::ForageMarkHarvested(state, node, 240, 100.0);
    CHECK_FALSE(game::ForageNodeReady(state, node, 339.0));
    CHECK(game::ForageRespawnRemainingMinutes(state, node, 100.0) ==
          doctest::Approx(240.0));
    CHECK(game::ForageRespawnRemainingMinutes(state, node, 300.0) ==
          doctest::Approx(40.0));
    CHECK(game::ForageNodeReady(state, node, 340.0));

    game::ForageNode otherZone{"meadow", "forage_meadow", 4, 5, 0};
    CHECK(game::ForageNodeReady(state, otherZone, 100.0));

    game::ForagePrune(state, 340.0);
    CHECK(state.harvested.empty());
    CHECK(game::ForageLootSeed(7, node) == game::ForageLootSeed(7, node));
    CHECK(game::ForageLootSeed(7, node) != game::ForageLootSeed(7, otherZone));
}

TEST_CASE("M23-F2a-T2 觅食提示文案") {
    game::ActionHintState hint;
    hint.nearbyForage = "草地";
    hint.forageSeconds = 2.4f;
    std::string tone;
    const std::string ready = game::ActionHintText(hint, &tone);
    CHECK(ready.find("按 E 觅食：草地") != std::string::npos);
    CHECK(ready.find("2.4") != std::string::npos);
    CHECK(tone == "info");

    hint.forageMissingTool = true;
    hint.forageToolName = "树枝";
    const std::string missing = game::ActionHintText(hint, &tone);
    CHECK(missing.find("需要树枝") != std::string::npos);
    CHECK(tone == "warn");

    hint.forageMissingTool = false;
    hint.forageExhausted = true;
    hint.forageRespawnMinutes = 12.2;
    const std::string exhausted = game::ActionHintText(hint, &tone);
    CHECK(exhausted.find("已耗尽：草地") != std::string::npos);
    CHECK(exhausted.find("13 分钟") != std::string::npos);
    CHECK(tone == "warn");
}

TEST_CASE("M23-F2a-T3 钓鱼数据契约") {
    Fixture fixture;
    const game::FishingSpotDef* spot = fixture.forage.FindSpot("pond");
    REQUIRE(spot != nullptr);
    CHECK(spot->tableId == "fish_water");
    CHECK(spot->trashTableId == "fish_water_trash");
    CHECK(spot->waitSeconds == doctest::Approx(5.0f));
    CHECK(spot->biteWindowSeconds == doctest::Approx(2.0f));
    CHECK(spot->catchChance == doctest::Approx(0.6f));
    CHECK(spot->survivalXp == doctest::Approx(10.0f));
    CHECK(spot->rodDefId == "fishing_rod");
    CHECK(spot->rodWear == doctest::Approx(0.03f));
    CHECK(spot->baitDefId == "fish_bait");
    CHECK(spot->baitCost == 1);
}

TEST_CASE("M23-F2a-T3 钓鱼等待与成功率受技能/天气修正") {
    Fixture fixture;
    const game::FishingSpotDef* spot = fixture.forage.FindSpot("pond");
    REQUIRE(spot != nullptr);

    game::WeatherState clear;
    CHECK(game::FishingWaitSeconds(*spot, clear, 0) ==
          doctest::Approx(5.0f));
    CHECK(game::FishingWaitSeconds(*spot, clear, 5) <
          game::FishingWaitSeconds(*spot, clear, 0));

    game::WeatherState rain;
    rain.kind = game::WeatherKind::Rain;
    rain.intensity = 1.0f;
    CHECK(game::FishingWaitSeconds(*spot, rain, 0) > 5.0f);

    game::WeatherState fog;
    fog.kind = game::WeatherKind::Fog;
    fog.intensity = 1.0f;
    CHECK(game::FishingWaitSeconds(*spot, fog, 0) > 5.0f);

    game::WeatherState cold;
    cold.kind = game::WeatherKind::ColdSnap;
    cold.intensity = 1.0f;
    CHECK(game::FishingWaitSeconds(*spot, cold, 0) >
          game::FishingWaitSeconds(*spot, rain, 0));

    game::WeatherState night = clear;
    night.night = true;
    CHECK(game::FishingWaitSeconds(*spot, night, 0) >
          game::FishingWaitSeconds(*spot, clear, 0));

    CHECK(game::FishingCatchChance(*spot, clear, 5) >
          game::FishingCatchChance(*spot, clear, 0));
    CHECK(game::FishingCatchChance(*spot, rain, 0) >
          game::FishingCatchChance(*spot, clear, 0));
    CHECK(game::FishingCatchChance(*spot, cold, 0) <
          game::FishingCatchChance(*spot, clear, 0));
}

TEST_CASE("M23-F2a-T3 钓鱼种子、成败判定与水面校验") {
    game::FishingSpot spot{"pond", "fish_water", 4, 4, 0, 5, 5, 0};
    CHECK(game::FishingLootSeed(7, spot, true) ==
          game::FishingLootSeed(7, spot, true));
    CHECK(game::FishingLootSeed(7, spot, true) !=
          game::FishingLootSeed(7, spot, false));
    CHECK(game::FishingAttemptSucceeds(7, spot, 1.0f));
    CHECK_FALSE(game::FishingAttemptSucceeds(7, spot, 0.0f));

    game::ForageRegistry fixture;
    items::ItemRegistry items;
    items::LootTableRegistry loot;
    std::string error;
    REQUIRE(items.LoadFromString(kItems, "test-items", &error));
    REQUIRE(loot.LoadFromString(kLoot, "test-loot", items, &error));
    REQUIRE(fixture.LoadFromString(kForage, "test-forage", loot, &error));
    const game::FishingSpotDef* def = fixture.FindSpot("pond");
    REQUIRE(def != nullptr);

    world::World world;
    SetFloor(world, 5, 5, "tile.floor.water");
    CHECK(game::FishingSpotWaterValid(*def, world, spot));
    SetFloor(world, 5, 5, "tile.floor.grass");
    CHECK_FALSE(game::FishingSpotWaterValid(*def, world, spot));
}

TEST_CASE("M23-F2a-T3 钓鱼提示文案") {
    game::ActionHintState hint;
    hint.nearbyFishing = "静水岸边";
    hint.fishingSeconds = 5.4f;
    std::string tone;
    const std::string ready = game::ActionHintText(hint, &tone);
    CHECK(ready.find("按 E 钓鱼：静水岸边") != std::string::npos);
    CHECK(ready.find("5.4") != std::string::npos);
    CHECK(tone == "info");

    hint.fishingMissingRod = true;
    hint.fishingRodName = "鱼竿";
    const std::string rod = game::ActionHintText(hint, &tone);
    CHECK(rod.find("需要鱼竿") != std::string::npos);
    CHECK(tone == "warn");

    hint.fishingMissingRod = false;
    hint.fishingMissingBait = true;
    hint.fishingBaitName = "鱼饵";
    const std::string bait = game::ActionHintText(hint, &tone);
    CHECK(bait.find("需要鱼饵") != std::string::npos);
    CHECK(tone == "warn");
}

TEST_CASE("M23-F2a-T3 真实地图池塘可规划钓鱼点") {
    Fixture fixture;
    const std::string root = std::string(ZL_SOURCE_DIR);
    world::MapLoader::Meta meta;
    std::string error;
    REQUIRE(world::MapLoader::LoadMeta(root + "/assets/maps/village_demo", &meta,
                                       &error));
    world::World world;
    for (int cy = 3; cy <= 3; ++cy) {
        for (int cx = 7; cx <= 8; ++cx) {
            world::ChunkData data;
            REQUIRE(world::MapLoader::LoadChunk(meta, cx, cy, &data, &error));
            for (int ly = 0; ly < core::kChunkSize; ++ly) {
                for (int lx = 0; lx < core::kChunkSize; ++lx) {
                    const std::string& sprite =
                        data.floors[static_cast<std::size_t>(ly) *
                                        core::kChunkSize +
                                    lx];
                    if (sprite.empty()) {
                        continue;
                    }
                    world::SquareObject floor;
                    floor.sprite = sprite;
                    floor.blocksMovement = sprite == "tile.floor.water";
                    world.GetOrCreateSquare(cx * core::kChunkSize + lx,
                                            cy * core::kChunkSize + ly, 0)
                        .SetFloor(std::move(floor));
                }
            }
        }
    }
    game::PoiConfig pois;
    const std::vector<game::FishingSpot> spots =
        game::PlanFishingSpots(fixture.forage, world, pois, {78, 32, 82, 36});
    REQUIRE_FALSE(spots.empty());
    const game::FishingSpotDef* def = fixture.forage.FindSpot("pond");
    REQUIRE(def != nullptr);
    for (const game::FishingSpot& spot : spots) {
        const bool onWater = spot.x == spot.waterX && spot.y == spot.waterY;
        CHECK_FALSE(onWater);
        CHECK(game::FishingSpotWaterValid(*def, world, spot));
    }
}
