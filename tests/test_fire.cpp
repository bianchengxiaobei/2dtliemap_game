#include <chrono>
#include <filesystem>
#include <string>

#include <doctest/doctest.h>
#include <entt/entt.hpp>

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/systems/CombatSystem.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "items/InventoryOps.h"
#include "items/ItemRegistry.h"
#include "game/Fire.h"
#include "game/FireComponents.h"
#include "game/FireSystem.h"
#include "game/WorldQueries.h"
#include "save/ChunkDiff.h"
#include "world/GridSquare.h"

using namespace zl;

namespace {

const char* kFireJson = R"({
  "format": "fire-v1",
  "materials": [
    {
      "id": "wall",
      "name": "木墙",
      "sprites": ["object.wall_h", "object.wall_v"],
      "flammability": {
        "ignitionThreshold": 0.4,
        "burnSeconds": 90.0,
        "smokePerSecond": 1.2,
        "heatRadius": 2.5
      }
    },
    {
      "id": "tree",
      "name": "树木",
      "sprites": ["object.tree"],
      "flammability": {
        "ignitionThreshold": 0.5,
        "burnSeconds": 120.0,
        "smokePerSecond": 2.0,
        "heatRadius": 3.0
      }
    }
  ]
})";

const char* kSpreadJson = R"({
  "format": "fire-v1",
  "materials": [
    {
      "id": "floor",
      "name": "木地板",
      "sprites": ["tile.floor.wood"],
      "flammability": {
        "ignitionThreshold": 0.0,
        "burnSeconds": 45.0,
        "smokePerSecond": 0.6,
        "heatRadius": 1.0
      }
    },
    {
      "id": "wall",
      "name": "木墙",
      "sprites": ["object.wall_h"],
      "flammability": {
        "ignitionThreshold": 0.8,
        "burnSeconds": 90.0,
        "smokePerSecond": 1.2,
        "heatRadius": 2.5
      }
    }
  ]
})";

void LoadSquareChunk(world::World& world, int wx, int wy) {
    const world::ChunkCoord coord = world::ChunkOf(wx, wy);
    world::Chunk* chunk = game::FindChunkInWorld(world, coord.cx, coord.cy);
    REQUIRE(chunk != nullptr);
    if (chunk->State() == world::ChunkState::Unloaded) {
        chunk->SetState(world::ChunkState::Loading);
    }
    if (chunk->State() == world::ChunkState::Loading) {
        chunk->SetState(world::ChunkState::Loaded);
    }
    REQUIRE(chunk->State() == world::ChunkState::Loaded);
}

void SetWoodFloor(world::World& world, int wx, int wy) {
    world::GridSquare& square = world.GetOrCreateSquare(wx, wy, 0);
    square.SetFloor(world::SquareObject{"tile.floor.wood"});
}

void SetFurniture(world::World& world, int wx, int wy, const char* sprite,
                  bool playerBuilt = false) {
    world::GridSquare& square = world.GetOrCreateSquare(wx, wy, 0);
    world::SquareObject object{sprite};
    object.playerBuilt = playerBuilt;
    square.AddObject(world::SquareLayer::Furniture, std::move(object));
}

}  // namespace

TEST_CASE("M23-F3-T1 火焰材料数据契约") {
    game::FireRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(registry.LoadFromString(kFireJson, "test-fire", &error),
                    error);
    REQUIRE(registry.Size() == 2);

    const game::FireMaterialDef* wall = registry.Find("wall");
    REQUIRE(wall != nullptr);
    CHECK(wall->name == "木墙");
    REQUIRE(wall->sprites.size() == 2);
    CHECK(wall->sprites[0] == "object.wall_h");
    CHECK(wall->sprites[1] == "object.wall_v");
    CHECK(wall->flammability.ignitionThreshold == doctest::Approx(0.4f));
    CHECK(wall->flammability.burnSeconds == doctest::Approx(90.0f));
    CHECK(wall->flammability.smokePerSecond == doctest::Approx(1.2f));
    CHECK(wall->flammability.heatRadius == doctest::Approx(2.5f));

    CHECK(registry.FindBySprite("object.wall_v") == wall);
    const game::FireMaterialDef* tree = registry.FindBySprite("object.tree");
    REQUIRE(tree != nullptr);
    CHECK(tree->id == "tree");
    CHECK(registry.FindBySprite("object.tree") == tree);
    CHECK(registry.Find("missing") == nullptr);
    CHECK(registry.FindBySprite("missing") == nullptr);
}

TEST_CASE("M23-F3-T1 火焰材料拒绝重复与非法字段") {
    const char* duplicateSprite = R"({
      "format": "fire-v1",
      "materials": [
        { "id": "a", "name": "A", "sprites": ["object.tree"],
          "flammability": { "burnSeconds": 10.0 } },
        { "id": "b", "name": "B", "sprites": ["object.tree"],
          "flammability": { "burnSeconds": 20.0 } }
      ]
    })";
    const char* duplicateId = R"({
      "format": "fire-v1",
      "materials": [
        { "id": "a", "name": "A", "sprites": ["object.tree"],
          "flammability": { "burnSeconds": 10.0 } },
        { "id": "a", "name": "B", "sprites": ["object.bush"],
          "flammability": { "burnSeconds": 20.0 } }
      ]
    })";
    const char* missingFlammability = R"({
      "format": "fire-v1",
      "materials": [
        { "id": "a", "name": "A", "sprites": ["object.tree"] }
      ]
    })";
    const char* badBurn = R"({
      "format": "fire-v1",
      "materials": [
        { "id": "a", "name": "A", "sprites": ["object.tree"],
          "flammability": { "burnSeconds": 0.0 } }
      ]
    })";
    const char* unknownField = R"({
      "format": "fire-v1",
      "materials": [
        { "id": "a", "name": "A", "sprites": ["object.tree"],
          "flammability": { "burnSeconds": 10.0, "spread": 1.0 } }
      ]
    })";

    game::FireRegistry registry;
    std::string error;
    CHECK_FALSE(registry.LoadFromString(duplicateSprite, "duplicate-sprite",
                                        &error));
    CHECK(error.find("重复") != std::string::npos);
    CHECK_FALSE(registry.LoadFromString(duplicateId, "duplicate-id", &error));
    CHECK(error.find("重复") != std::string::npos);
    CHECK_FALSE(registry.LoadFromString(missingFlammability, "missing", &error));
    CHECK_FALSE(registry.LoadFromString(badBurn, "bad-burn", &error));
    CHECK_FALSE(registry.LoadFromString(unknownField, "unknown", &error));
    CHECK(registry.Size() == 0);
}

TEST_CASE("M23-F3-T1 基础火焰数据覆盖木墙木地板与植被") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    game::FireRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(
        registry.LoadFile(root / "assets" / "fire.json", &error), error);
    REQUIRE(registry.Size() == 5);

    const auto requireSprite = [&](std::string_view sprite,
                                   std::string_view id) {
        const game::FireMaterialDef* material =
            registry.FindBySprite(sprite);
        REQUIRE(material != nullptr);
        CHECK(material->id == id);
        CHECK(material->flammability.burnSeconds > 0.0f);
        CHECK(material->flammability.heatRadius >= 0.0f);
        CHECK(material->flammability.smokePerSecond >= 0.0f);
    };
    requireSprite("object.campfire", "campfire");
    requireSprite("object.wall_h", "wood_wall");
    requireSprite("object.wall_v", "wood_wall");
    requireSprite("tile.floor.wood", "wood_floor");
    requireSprite("object.tree", "tree");
    requireSprite("decor.grass_tuft", "grass_tuft");

    const game::FireMaterialDef* wall = registry.Find("wood_wall");
    const game::FireMaterialDef* floor = registry.Find("wood_floor");
    const game::FireMaterialDef* tree = registry.Find("tree");
    REQUIRE(wall != nullptr);
    REQUIRE(floor != nullptr);
    REQUIRE(tree != nullptr);
    CHECK(wall->flammability.ignitionThreshold >
          floor->flammability.ignitionThreshold);
    CHECK(wall->flammability.burnSeconds > floor->flammability.burnSeconds);
    CHECK(tree->flammability.burnSeconds > wall->flammability.burnSeconds);
    REQUIRE(wall->charredDrops.size() == 1);
    CHECK(wall->charredDrops[0].itemId == "wood");
    CHECK(wall->charredDrops[0].count == 2);
    REQUIRE(tree->charredDrops.size() == 2);
    CHECK(tree->charredDrops[1].itemId == "forage_stick");
}

TEST_CASE("M23-F3-T1 格内可燃物按层查询") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    game::FireRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(
        registry.LoadFile(root / "assets" / "fire.json", &error), error);

    world::GridSquare square(0, 0, 0);
    square.SetFloor(world::SquareObject{"tile.floor.wood"});
    square.AddObject(world::SquareLayer::Wall,
                     world::SquareObject{"object.wall_h"});
    const game::FireMaterialDef* floor =
        game::FindFireMaterial(registry, square, world::SquareLayer::Floor);
    const game::FireMaterialDef* wall =
        game::FindFireMaterial(registry, square, world::SquareLayer::Wall);
    REQUIRE(floor != nullptr);
    REQUIRE(wall != nullptr);
    CHECK(floor->id == "wood_floor");
    CHECK(wall->id == "wood_wall");
    const game::FireMaterialDef* preferred =
        game::FindFireMaterial(registry, square);
    REQUIRE(preferred != nullptr);
    CHECK(preferred->id == "wood_wall");

    world::GridSquare treeSquare(1, 0, 0);
    treeSquare.SetFloor(world::SquareObject{"tile.floor.grass"});
    treeSquare.AddObject(world::SquareLayer::Furniture,
                         world::SquareObject{"object.tree"});
    const game::FireMaterialDef* tree =
        game::FindFireMaterial(registry, treeSquare);
    REQUIRE(tree != nullptr);
    CHECK(tree->id == "tree");

    world::GridSquare campfireSquare(2, 0, 0);
    campfireSquare.SetFloor(world::SquareObject{"tile.floor.grass"});
    campfireSquare.AddObject(world::SquareLayer::Furniture,
                             world::SquareObject{"object.campfire"});
    const game::FireMaterialDef* campfire =
        game::FindFireMaterial(registry, campfireSquare);
    REQUIRE(campfire != nullptr);
    CHECK(campfire->id == "campfire");
}

TEST_CASE("M23-F3-T1 火源组件字段") {
    ecs::FireSource source;
    source.materialId = "wood_wall";
    source.phase = ecs::FirePhase::Igniting;
    source.ignitionProgress = 0.25f;
    source.burnRemaining = 30.0f;
    source.burnDuration = 90.0f;
    source.smokePerSecond = 1.2f;
    source.heatRadius = 2.5f;

    entt::registry registry;
    const entt::entity entity = registry.create();
    registry.emplace<ecs::FireSource>(entity, source);
    const ecs::FireSource& stored = registry.get<ecs::FireSource>(entity);
    CHECK(stored.materialId == "wood_wall");
    CHECK(stored.phase == ecs::FirePhase::Igniting);
    CHECK(stored.ignitionProgress == doctest::Approx(0.25f));
    CHECK(stored.burnRemaining == doctest::Approx(30.0f));
    CHECK(stored.burnDuration == doctest::Approx(90.0f));
    CHECK(stored.smokePerSecond == doctest::Approx(1.2f));
    CHECK(stored.heatRadius == doctest::Approx(2.5f));
}


TEST_CASE("M23-F3-T2 点燃进度、燃烧与阴燃熄灭") {
    game::FireMaterialDef material;
    material.id = "test_wall";
    material.flammability.ignitionThreshold = 0.0f;
    material.flammability.burnSeconds = 3.0f;
    material.flammability.smokePerSecond = 1.0f;
    material.flammability.heatRadius = 2.0f;
    game::FireParams params;
    params.ignitionSeconds = 1.0f;
    params.smolderSeconds = 2.0f;

    ecs::FireSource source;
    CHECK(game::TryIgniteFire(source, material, 0.0f, 0.0f, params) ==
          game::FireIgnitionResult::Ignited);
    CHECK(source.phase == ecs::FirePhase::Igniting);
    CHECK(source.ignitionDuration == doctest::Approx(1.0f));
    CHECK(source.burnRemaining == doctest::Approx(3.0f));
    CHECK(source.heatRadius == doctest::Approx(2.0f));
    CHECK_FALSE(game::FireProvidesHeat(source));

    game::AdvanceFire(source, 0.5f, params);
    CHECK(source.phase == ecs::FirePhase::Igniting);
    game::AdvanceFire(source, 0.6f, params);
    CHECK(source.phase == ecs::FirePhase::Burning);
    CHECK(source.ignitionProgress == doctest::Approx(1.0f));
    CHECK(game::FireProvidesHeat(source));
    CHECK(game::FireSmokePerSecond(source) == doctest::Approx(1.0f));

    game::AdvanceFire(source, 1.0f, params);
    CHECK(source.burnRemaining == doctest::Approx(2.0f));
    game::AdvanceFire(source, 2.0f, params);
    CHECK(source.phase == ecs::FirePhase::Smoldering);
    CHECK(source.burnRemaining == doctest::Approx(0.0f));
    CHECK(game::FireProvidesHeat(source));

    game::AdvanceFire(source, 2.1f, params);
    CHECK(source.phase == ecs::FirePhase::Extinguished);
    CHECK_FALSE(game::FireProvidesHeat(source));
    CHECK(game::FireSmokePerSecond(source) == doctest::Approx(0.0f));
}

TEST_CASE("M23-F3-T2 潮湿降低点燃率并拒绝点燃") {
    game::FireMaterialDef grass;
    grass.id = "grass";
    grass.flammability.ignitionThreshold = 0.2f;
    game::FireMaterialDef tree;
    tree.id = "tree";
    tree.flammability.ignitionThreshold = 0.5f;
    game::FireParams params;
    params.ignitionSeconds = 1.0f;

    const float dryGrass =
        game::FireIgnitionChance(grass.flammability, 0.0f, params);
    const float wetGrass =
        game::FireIgnitionChance(grass.flammability, 0.8f, params);
    CHECK(dryGrass > wetGrass);
    CHECK(dryGrass == doctest::Approx(0.8f));
    CHECK(wetGrass == doctest::Approx(0.08f));
    CHECK(game::FireIgnitionChance(tree.flammability, 0.8f, params) <
          wetGrass);
    CHECK(game::FireIgnitionSeconds(tree.flammability, params) >
          game::FireIgnitionSeconds(grass.flammability, params));

    ecs::FireSource wet;
    const game::FireIgnitionResult wetResult =
        game::TryIgniteFire(wet, grass, 0.8f, 0.5f, params);
    CHECK(wetResult == game::FireIgnitionResult::TooWet);
    CHECK(wet.phase == ecs::FirePhase::Extinguished);
    CHECK_FALSE(game::FireIsActive(wet));

    ecs::FireSource dry;
    CHECK(game::TryIgniteFire(dry, grass, 0.0f, 0.5f, params) ==
          game::FireIgnitionResult::Ignited);
    CHECK(dry.phase == ecs::FirePhase::Igniting);
    CHECK(game::TryIgniteFire(dry, grass, 0.0f, 0.0f, params) ==
          game::FireIgnitionResult::AlreadyActive);
}

TEST_CASE("M23-F3-T2 火源按热半径升温且熄灭后立即失效") {
    game::FireRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(registry.LoadFromString(kFireJson, "heat-fire", &error),
                    error);
    game::FireSystem fire(registry, game::FireParams{});

    ecs::EcsWorld world;
    const ecs::Entity entity = world.Create();
    entt::registry& components = world.Registry();
    components.emplace<ecs::Position>(entity, ecs::Position{10.5f, 10.5f, 0});
    components.emplace<ecs::FireAnchor>(entity, ecs::FireAnchor{10, 10, 0});
    ecs::FireSource source;
    source.phase = ecs::FirePhase::Burning;
    source.heatRadius = 3.0f;
    source.burnDuration = 10.0f;
    source.burnRemaining = 10.0f;
    components.emplace<ecs::FireSource>(entity, source);

    CHECK(fire.HasActiveFireNear(world, 12.0f, 10.5f, 0));
    CHECK_FALSE(fire.HasActiveFireNear(world, 12.0f, 10.5f, 1));
    CHECK_FALSE(fire.HasActiveFireNear(world, 14.0f, 10.5f, 0));
    REQUIRE(fire.FindAt(world, 10, 10, 0) != nullptr);
    CHECK(fire.ExtinguishAt(world, 10, 10, 0));
    CHECK_FALSE(fire.HasActiveFireNear(world, 10.5f, 10.5f, 0));
    CHECK_FALSE(fire.ExtinguishAt(world, 10, 10, 0));
}


TEST_CASE("M23-F3-T2 篝火源创建、燃烧与主动熄灭") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    game::FireRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(
        registry.LoadFile(root / "assets" / "fire.json", &error), error);
    game::FireSystem fire(registry, game::FireParams{}, 7u);

    world::World world;
    world::GridSquare& square = world.GetOrCreateSquare(70, 18, 0);
    square.SetFloor(world::SquareObject{"tile.floor.grass"});
    square.AddObject(world::SquareLayer::Furniture,
                     world::SquareObject{"object.campfire"});
    const world::ChunkCoord chunkCoord = world::ChunkOf(70, 18);
    world::Chunk* chunk =
        game::FindChunkInWorld(world, chunkCoord.cx, chunkCoord.cy);
    REQUIRE(chunk != nullptr);
    chunk->SetState(world::ChunkState::Loading);
    chunk->SetState(world::ChunkState::Loaded);

    ecs::EcsWorld ecs;
    const game::FireIgnitionResult result =
        fire.IgniteAt(ecs, world, 70, 18, 0, 0.0f, &error);
    REQUIRE_MESSAGE(result == game::FireIgnitionResult::Ignited, error);
    const ecs::FireSource* source = fire.FindAt(ecs, 70, 18, 0);
    REQUIRE(source != nullptr);
    CHECK(source->materialId == "campfire");
    CHECK(source->phase == ecs::FirePhase::Igniting);
    CHECK(source->burnRemaining == doctest::Approx(600.0f));
    CHECK_FALSE(fire.HasActiveFireNear(ecs, 70.5f, 18.5f, 0));

    fire.Update(ecs, 2.0f);
    source = fire.FindAt(ecs, 70, 18, 0);
    REQUIRE(source != nullptr);
    CHECK(source->phase == ecs::FirePhase::Burning);
    CHECK(fire.HasActiveFireNear(ecs, 70.5f, 18.5f, 0));
    CHECK(fire.IgniteAt(ecs, world, 70, 18, 0, 0.0f, &error) ==
          game::FireIgnitionResult::AlreadyActive);
    CHECK(fire.ExtinguishAt(ecs, 70, 18, 0));
    CHECK_FALSE(fire.HasActiveFireNear(ecs, 70.5f, 18.5f, 0));
}


TEST_CASE("M23-F3-T3 木地板与木墙的蔓延间隔和概率不同") {
    game::FireRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(registry.LoadFromString(kSpreadJson, "spread-fire", &error),
                    error);
    const game::FireMaterialDef* floor = registry.Find("floor");
    const game::FireMaterialDef* wall = registry.Find("wall");
    REQUIRE(floor != nullptr);
    REQUIRE(wall != nullptr);

    game::FireParams params;
    params.spreadIntervalSeconds = 6.0f;
    params.spreadChanceScale = 1.0f;
    CHECK(game::FireSpreadInterval(floor->flammability.burnSeconds, params) <
          game::FireSpreadInterval(wall->flammability.burnSeconds, params));
    CHECK(game::FireSpreadChance(floor->flammability, 0.0f, params) >
          game::FireSpreadChance(wall->flammability, 0.0f, params));
    CHECK(game::FireSpreadChance(floor->flammability, 0.8f, params) >
          game::FireSpreadChance(wall->flammability, 0.8f, params));
    CHECK(game::FireSpreadChance(floor->flammability, 0.0f, params) ==
          doctest::Approx(1.0f));
    CHECK(game::FireSpreadChance(wall->flammability, 0.0f, params) ==
          doctest::Approx(0.2f));

    game::FireParams clamped;
    clamped.spreadIntervalSeconds = -1.0f;
    clamped.spreadChanceScale = 2.0f;
    clamped.maxActiveSources = 0;
    clamped.maxSpreadsPerTick = 999;
    const game::FireParams sanitized = game::SanitizeFireParams(clamped);
    CHECK(sanitized.spreadIntervalSeconds == doctest::Approx(0.5f));
    CHECK(sanitized.spreadChanceScale == doctest::Approx(1.0f));
    CHECK(sanitized.maxActiveSources == 1);
    CHECK(sanitized.maxSpreadsPerTick == 64);
}

TEST_CASE("M23-F3-T3 按四邻接蔓延并受 tick 预算与火源上限约束") {
    game::FireRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(registry.LoadFromString(kSpreadJson, "spread-budget", &error),
                    error);
    game::FireParams params;
    params.ignitionSeconds = 0.1f;
    params.spreadIntervalSeconds = 1.0f;
    params.spreadChanceScale = 1.0f;
    params.maxActiveSources = 3;
    params.maxSpreadsPerTick = 1;
    game::FireSystem fire(registry, params, 19u);

    world::World world;
    SetWoodFloor(world, 10, 10);
    SetWoodFloor(world, 11, 10);
    SetWoodFloor(world, 12, 10);
    SetWoodFloor(world, 10, 11);
    SetWoodFloor(world, 10, 9);
    LoadSquareChunk(world, 10, 10);

    ecs::EcsWorld ecs;
    REQUIRE(fire.IgniteAt(ecs, world, 10, 10, 0, 0.0f, &error) ==
            game::FireIgnitionResult::Ignited);
    fire.Update(ecs, 0.2f, &world, 0.0f);
    CHECK(fire.ActiveSourceCount(ecs) == 1);
    ecs::FireSource* source = fire.FindAt(ecs, 10, 10, 0);
    REQUIRE(source != nullptr);
    REQUIRE(source->phase == ecs::FirePhase::Burning);
    source->spreadAccumulator =
        game::FireSpreadInterval(source->burnDuration, params) - 0.01f;

    fire.Update(ecs, 0.02f, &world, 0.0f);
    CHECK(fire.ActiveSourceCount(ecs) == 2);
    source = fire.FindAt(ecs, 10, 10, 0);
    REQUIRE(source != nullptr);
    source->spreadAccumulator =
        game::FireSpreadInterval(source->burnDuration, params) - 0.01f;
    fire.Update(ecs, 0.02f, &world, 0.0f);
    CHECK(fire.ActiveSourceCount(ecs) == 3);

    source = fire.FindAt(ecs, 10, 10, 0);
    REQUIRE(source != nullptr);
    source->spreadAccumulator =
        game::FireSpreadInterval(source->burnDuration, params) - 0.01f;
    fire.Update(ecs, 0.02f, &world, 0.0f);
    CHECK(fire.ActiveSourceCount(ecs) == 3);
}

TEST_CASE("M23-F3-T3 蔓延不跨越未加载 chunk") {
    game::FireRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(registry.LoadFromString(kSpreadJson, "spread-chunk", &error),
                    error);
    game::FireParams params;
    params.ignitionSeconds = 0.1f;
    params.spreadIntervalSeconds = 1.0f;
    params.spreadChanceScale = 1.0f;
    params.maxActiveSources = 10;
    params.maxSpreadsPerTick = 1;
    game::FireSystem fire(registry, params, 23u);

    world::World world;
    SetWoodFloor(world, 9, 10);
    world::GridSquare& target = world.GetOrCreateSquare(10, 10, 0);
    target.SetFloor(world::SquareObject{"tile.floor.wood"});
    LoadSquareChunk(world, 9, 10);

    ecs::EcsWorld ecs;
    REQUIRE(fire.IgniteAt(ecs, world, 9, 10, 0, 0.0f, &error) ==
            game::FireIgnitionResult::Ignited);
    fire.Update(ecs, 0.2f, &world, 0.0f);
    ecs::FireSource* source = fire.FindAt(ecs, 9, 10, 0);
    REQUIRE(source != nullptr);
    REQUIRE(source->phase == ecs::FirePhase::Burning);
    source->spreadAccumulator =
        game::FireSpreadInterval(source->burnDuration, params) - 0.01f;
    fire.Update(ecs, 0.02f, &world, 0.0f);
    CHECK(fire.ActiveSourceCount(ecs) == 1);
    CHECK(fire.FindAt(ecs, 10, 10, 0) == nullptr);

    LoadSquareChunk(world, 10, 10);
    source = fire.FindAt(ecs, 9, 10, 0);
    REQUIRE(source != nullptr);
    source->spreadAccumulator =
        game::FireSpreadInterval(source->burnDuration, params) - 0.01f;
    fire.Update(ecs, 0.02f, &world, 0.0f);
    CHECK(fire.ActiveSourceCount(ecs) == 2);
    REQUIRE(fire.FindAt(ecs, 10, 10, 0) != nullptr);
    CHECK(fire.FindAt(ecs, 10, 10, 0)->phase == ecs::FirePhase::Igniting);
}


TEST_CASE("M23-F3-T4 灭火物品选择、消耗与湿布/水") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    items::ItemRegistry items;
    std::string error;
    REQUIRE_MESSAGE(items.LoadFile(root / "assets" / "items.json", &error),
                    error);

    game::FireParams params;
    params.bareHandsChance = 0.15f;
    params.bareHandsSelfDamage = 2.0f;
    ecs::Container bag;
    bag.capacityKg = 50.0f;

    game::FireExtinguishPlan bare =
        game::PlanFireExtinguish(bag, items, params);
    CHECK(bare.bareHands);
    CHECK_FALSE(bare.usesItem);
    CHECK(bare.chance == doctest::Approx(0.15f));
    CHECK(bare.selfDamage == doctest::Approx(2.0f));
    CHECK(game::FireExtinguishSucceeds(0.5f, 0.49f));
    CHECK_FALSE(game::FireExtinguishSucceeds(0.5f, 0.50f));
    CHECK(game::FireExtinguishSucceeds(1.0f, 0.99f));

    REQUIRE(items::AddItem(
                bag, items,
                ecs::ItemInstance{"fire_extinguisher", 1, 0.8f}) == 1);
    game::FireExtinguishPlan extinguisher =
        game::PlanFireExtinguish(bag, items, params);
    CHECK(extinguisher.usesItem);
    CHECK_FALSE(extinguisher.bareHands);
    CHECK(extinguisher.name == "灭火器");
    CHECK(extinguisher.chance == doctest::Approx(0.855f));
    CHECK(extinguisher.wear == doctest::Approx(0.2f));
    REQUIRE(game::ConsumeFireExtinguishItem(bag, items, extinguisher));
    REQUIRE(bag.items.size() == 1);
    CHECK(bag.items[0].condition == doctest::Approx(0.6f));

    bag.items.clear();
    REQUIRE(items::AddItem(
                bag, items,
                ecs::ItemInstance{"water_bottle", 1, 1.0f}) == 1);
    game::FireExtinguishPlan water =
        game::PlanFireExtinguish(bag, items, params);
    CHECK(water.usesItem);
    CHECK(water.name == "瓶装净水（1L）");
    CHECK(water.replacementDefId == "empty_bottle");
    CHECK(water.chance == doctest::Approx(0.55f));
    REQUIRE(game::ConsumeFireExtinguishItem(bag, items, water));
    REQUIRE(bag.items.size() == 1);
    CHECK(bag.items[0].defId == "empty_bottle");

    bag.items.clear();
    REQUIRE(items::AddItem(
                bag, items,
                ecs::ItemInstance{"wet_rag", 1, 1.0f}) == 1);
    game::FireExtinguishPlan rag =
        game::PlanFireExtinguish(bag, items, params);
    CHECK(rag.usesItem);
    CHECK(rag.name == "湿布");
    CHECK(rag.chance == doctest::Approx(0.55f));
    REQUIRE(game::ConsumeFireExtinguishItem(bag, items, rag));
    REQUIRE(bag.items.size() == 1);
    CHECK(bag.items[0].condition == doctest::Approx(0.5f));
    REQUIRE(game::ConsumeFireExtinguishItem(bag, items, rag));
    CHECK(bag.items.empty());
}

TEST_CASE("M23-F3-T4 选择最近活跃火源供灭火交互使用") {
    game::FireRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(registry.LoadFromString(kFireJson, "extinguish-target", &error),
                    error);
    game::FireSystem fire(registry, game::FireParams{});
    ecs::EcsWorld ecs;
    const auto addSource = [&](int x, int y, ecs::FirePhase phase) {
        const ecs::Entity entity = ecs.Create();
        ecs.Registry().emplace<ecs::Position>(
            entity, ecs::Position{static_cast<float>(x) + 0.5f,
                                  static_cast<float>(y) + 0.5f, 0});
        ecs.Registry().emplace<ecs::FireAnchor>(
            entity, ecs::FireAnchor{x, y, 0});
        ecs::FireSource source;
        source.materialId = "wall";
        source.phase = phase;
        source.heatRadius = 1.0f;
        source.burnDuration = 10.0f;
        source.burnRemaining = 10.0f;
        ecs.Registry().emplace<ecs::FireSource>(entity, source);
    };
    addSource(5, 5, ecs::FirePhase::Burning);
    addSource(7, 5, ecs::FirePhase::Extinguished);
    game::FireTarget target;
    REQUIRE(fire.FindNearestActiveFire(ecs, 5.5f, 5.5f, 0, 3.0f, &target));
    CHECK(target.x == 5);
    CHECK(target.y == 5);
    REQUIRE(target.material != nullptr);
    CHECK(target.material->id == "wall");
    CHECK_FALSE(fire.FindNearestActiveFire(ecs, 6.0f, 5.5f, 0, 0.2f,
                                           &target));
}

TEST_CASE("M23-F3-T4 火焰对范围内生命实体造成持续伤害") {
    game::FireParams params;
    params.fireDamagePerSecond = 6.0f;
    params.fireDamageRadius = 1.0f;
    characters::CombatSystem combat;
    ecs::EcsWorld ecs;

    const ecs::Entity fireEntity = ecs.Create();
    ecs.Registry().emplace<ecs::Position>(
        fireEntity, ecs::Position{5.5f, 5.5f, 0});
    ecs::FireSource fire;
    fire.phase = ecs::FirePhase::Burning;
    ecs.Registry().emplace<ecs::FireSource>(fireEntity, fire);

    const ecs::Entity near = ecs.Create();
    ecs.Registry().emplace<ecs::Position>(near, ecs::Position{5.8f, 5.5f, 0});
    ecs.Registry().emplace<ecs::Health>(near, ecs::Health{100.0f, 100.0f});
    const ecs::Entity far = ecs.Create();
    ecs.Registry().emplace<ecs::Position>(far, ecs::Position{7.0f, 5.5f, 0});
    ecs.Registry().emplace<ecs::Health>(far, ecs::Health{100.0f, 100.0f});

    CHECK(game::ApplyFireDamage(ecs, combat, params, 1.0f) == 1);
    CHECK(ecs.Registry().get<ecs::Health>(near).current ==
          doctest::Approx(94.0f));
    CHECK(ecs.Registry().get<ecs::Health>(far).current ==
          doctest::Approx(100.0f));
    const auto* last = ecs.Registry().try_get<ecs::LastDamage>(near);
    REQUIRE(last != nullptr);
    CHECK(last->cause == ecs::DamageCause::Fire);

    ecs.Registry().get<ecs::FireSource>(fireEntity).phase =
        ecs::FirePhase::Smoldering;
    CHECK(game::ApplyFireDamage(ecs, combat, params, 1.0f) == 0);
    CHECK(ecs.Registry().get<ecs::Health>(near).current ==
          doctest::Approx(94.0f));
}


TEST_CASE("M23-F3-T5 僵尸进入火焰后燃烧并按间隔持续受伤") {
    game::FireParams params;
    params.fireDamagePerSecond = 6.0f;
    params.fireDamageRadius = 1.0f;
    params.zombieBurnSeconds = 8.0f;
    params.zombieBurnDamagePerSecond = 3.0f;
    params.zombieBurnDamageIntervalSeconds = 0.5f;
    params.zombiePanicSeconds = 4.0f;
    params.zombiePanicSpeedScale = 1.25f;
    params.zombiePanicChaseRadius = 12.0f;
    characters::CombatSystem combat;
    ecs::EcsWorld ecs;

    const ecs::Entity fireEntity = ecs.Create();
    ecs.Registry().emplace<ecs::Position>(
        fireEntity, ecs::Position{5.5f, 5.5f, 0});
    ecs::FireSource fire;
    fire.phase = ecs::FirePhase::Burning;
    ecs.Registry().emplace<ecs::FireSource>(fireEntity, fire);

    const ecs::Entity zombie = ecs.Create();
    ecs.Registry().emplace<ecs::Position>(zombie,
                                          ecs::Position{5.8f, 5.5f, 0});
    ecs.Registry().emplace<ecs::Health>(zombie,
                                        ecs::Health{100.0f, 100.0f});
    ecs.Registry().emplace<ecs::ZombieVariant>(zombie);
    ecs.Registry().emplace<ecs::PerceptionState>(zombie,
                                                 ecs::PerceptionState{});

    const ecs::Entity player = ecs.Create();
    ecs.Registry().emplace<ecs::Position>(player,
                                          ecs::Position{5.8f, 5.5f, 0});
    ecs.Registry().emplace<ecs::Health>(player,
                                        ecs::Health{100.0f, 100.0f});
    ecs.Registry().emplace<ecs::PlayerTag>(player);

    REQUIRE(game::ApplyFireDamage(ecs, combat, params, 0.5f) == 2);
    REQUIRE(ecs.Registry().all_of<ecs::BurningZombie>(zombie));
    CHECK_FALSE(ecs.Registry().all_of<ecs::BurningZombie>(player));
    const auto* perception =
        ecs.Registry().try_get<ecs::PerceptionState>(zombie);
    REQUIRE(perception != nullptr);
    CHECK(perception->alertness == doctest::Approx(1.0f));
    CHECK(ecs.Registry().get<ecs::Health>(zombie).current ==
          doctest::Approx(97.0f));

    REQUIRE(game::ApplyBurningZombieDamage(ecs, combat, params, 0.5f) == 1);
    CHECK(ecs.Registry().get<ecs::Health>(zombie).current ==
          doctest::Approx(95.5f));
    const auto* burning = ecs.Registry().try_get<ecs::BurningZombie>(zombie);
    REQUIRE(burning != nullptr);
    CHECK(burning->remaining == doctest::Approx(7.5f));
    CHECK(burning->panicRemaining == doctest::Approx(3.5f));

    for (int i = 0; i < 15; ++i) {
        (void)game::ApplyBurningZombieDamage(ecs, combat, params, 0.5f);
    }
    CHECK(ecs.Registry().get<ecs::Health>(zombie).current > 0.0f);
    CHECK_FALSE(ecs.Registry().all_of<ecs::BurningZombie>(zombie));
}

TEST_CASE("M23-F3-T5 燃烧僵尸按间隔点燃脚下可燃格") {
    game::FireParams params;
    params.zombieSpreadIntervalSeconds = 0.5f;
    params.zombieSpreadChance = 1.0f;
    params.maxSpreadsPerTick = 1;
    game::FireRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(registry.LoadFromString(kSpreadJson, "zombie-spread", &error),
                    error);
    game::FireSystem fire(registry, params, 31u);

    ecs::EcsWorld ecs;
    world::World testWorld;
    SetWoodFloor(testWorld, 10, 10);
    LoadSquareChunk(testWorld, 10, 10);
    const ecs::Entity zombie = ecs.Create();
    ecs.Registry().emplace<ecs::Position>(zombie,
                                          ecs::Position{10.5f, 10.5f, 0});
    ecs::BurningZombie burning;
    burning.remaining = 5.0f;
    burning.spreadAccumulator = 0.49f;
    ecs.Registry().emplace<ecs::BurningZombie>(zombie, burning);

    CHECK(fire.SpreadFromBurningZombies(ecs, testWorld, 0.02f, 0.0f) == 1);
    REQUIRE(fire.FindAt(ecs, 10, 10, 0) != nullptr);
    CHECK(fire.FindAt(ecs, 10, 10, 0)->phase == ecs::FirePhase::Igniting);
    CHECK(fire.SpreadFromBurningZombies(ecs, testWorld, 0.5f, 0.0f) == 0);
    CHECK(fire.ActiveSourceCount(ecs) == 1);
}

TEST_CASE("M23-F3-T7 直接点火同样受活跃火源上限约束") {
    game::FireRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(registry.LoadFromString(kSpreadJson, "ignite-cap", &error),
                    error);
    game::FireParams params;
    params.ignitionSeconds = 0.05f;
    params.minIgnitionChance = 1.0f;
    params.maxActiveSources = 1;
    game::FireSystem fire(registry, params, 41u);

    world::World world;
    SetWoodFloor(world, 10, 10);
    SetWoodFloor(world, 11, 10);
    LoadSquareChunk(world, 10, 10);

    ecs::EcsWorld ecs;
    REQUIRE(fire.IgniteAt(ecs, world, 10, 10, 0, 0.0f, &error) ==
            game::FireIgnitionResult::Ignited);
    CHECK(fire.IgniteAt(ecs, world, 11, 10, 0, 0.0f, &error) ==
          game::FireIgnitionResult::AtCapacity);
    CHECK(error.find("上限") != std::string::npos);
    CHECK(fire.ActiveSourceCount(ecs) == 1);
}

TEST_CASE("M23-F3-T7 雨天按湿度加速燃烧衰减") {
    game::FireParams params;
    params.rainBurnDecayScale = 1.0f;
    ecs::FireSource dry;
    dry.phase = ecs::FirePhase::Burning;
    dry.burnRemaining = 10.0f;
    ecs::FireSource wet = dry;

    game::AdvanceFire(dry, 1.0f, params, 0.0f);
    game::AdvanceFire(wet, 1.0f, params, 1.0f);
    CHECK(dry.burnRemaining == doctest::Approx(9.0f));
    CHECK(wet.burnRemaining == doctest::Approx(8.0f));

    game::AdvanceFire(wet, 10.0f, params, 1.0f);
    CHECK(wet.burnRemaining == doctest::Approx(0.0f));
    CHECK(wet.phase == ecs::FirePhase::Smoldering);

    game::FireParams clamped;
    clamped.rainBurnDecayScale = -1.0f;
    CHECK(game::SanitizeFireParams(clamped).rainBurnDecayScale ==
          doctest::Approx(0.0f));
    clamped.rainBurnDecayScale = 99.0f;
    CHECK(game::SanitizeFireParams(clamped).rainBurnDecayScale ==
          doctest::Approx(10.0f));
}

TEST_CASE("M23-F3-T7 chunk 卸载清理活跃火源但保留烧毁格") {
    game::FireRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(registry.LoadFromString(kSpreadJson, "unload-fire", &error),
                    error);
    game::FireParams params;
    params.ignitionSeconds = 0.05f;
    params.minIgnitionChance = 1.0f;
    game::FireSystem fire(registry, params, 43u);

    world::World world;
    for (int x : {1, 2, 10}) {
        SetWoodFloor(world, x, 1);
    }
    LoadSquareChunk(world, 1, 1);
    LoadSquareChunk(world, 10, 1);

    ecs::EcsWorld ecs;
    REQUIRE(fire.IgniteAt(ecs, world, 1, 1, 0, 0.0f, &error) ==
            game::FireIgnitionResult::Ignited);
    REQUIRE(fire.IgniteAt(ecs, world, 2, 1, 0, 0.0f, &error) ==
            game::FireIgnitionResult::Ignited);
    REQUIRE(fire.ExtinguishAt(ecs, 2, 1, 0));
    REQUIRE(fire.IgniteAt(ecs, world, 10, 1, 0, 0.0f, &error) ==
            game::FireIgnitionResult::Ignited);

    CHECK(fire.UnloadChunk(ecs, 0, 0) == 1);
    ecs.Flush();
    CHECK(fire.FindAt(ecs, 1, 1, 0) == nullptr);
    REQUIRE(fire.FindAt(ecs, 2, 1, 0) != nullptr);
    CHECK(fire.FindAt(ecs, 2, 1, 0)->phase == ecs::FirePhase::Extinguished);
    REQUIRE(fire.FindAt(ecs, 10, 1, 0) != nullptr);
    CHECK(fire.FindAt(ecs, 10, 1, 0)->phase == ecs::FirePhase::Igniting);

    world::World coldWorld;
    ecs::EcsWorld coldEcs;
    const ecs::Entity coldEntity = coldEcs.Create();
    coldEcs.Registry().emplace<ecs::Position>(
        coldEntity, ecs::Position{10.5f, 10.5f, 0});
    coldEcs.Registry().emplace<ecs::FireAnchor>(
        coldEntity, ecs::FireAnchor{10, 10, 0});
    ecs::FireSource coldSource;
    coldSource.phase = ecs::FirePhase::Burning;
    coldSource.burnRemaining = 10.0f;
    coldEcs.Registry().emplace<ecs::FireSource>(coldEntity, coldSource);
    fire.Update(coldEcs, 1.0f, &coldWorld, 0.0f);
    CHECK(coldEcs.Registry().get<ecs::FireSource>(coldEntity).burnRemaining ==
          doctest::Approx(10.0f));
}

TEST_CASE("M23-F3-T7 自然燃尽删除结构并按材料掉落") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    game::FireRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(registry.LoadFile(root / "assets" / "fire.json", &error),
                    error);

    game::FireParams params;
    params.ignitionSeconds = 0.05f;
    params.minIgnitionChance = 1.0f;
    params.smolderSeconds = 0.0f;
    game::FireSystem fire(registry, params, 47u);

    world::World world;
    SetFurniture(world, 10, 10, "object.wall_h", true);
    LoadSquareChunk(world, 10, 10);
    ecs::EcsWorld ecs;

    REQUIRE(fire.IgniteAt(ecs, world, 10, 10, 0, 0.0f, &error) ==
            game::FireIgnitionResult::Ignited);
    fire.Update(ecs, 0.1f, &world, 0.0f);
    ecs::FireSource* source = fire.FindAt(ecs, 10, 10, 0);
    REQUIRE(source != nullptr);
    REQUIRE(source->phase == ecs::FirePhase::Burning);
    source->burnRemaining = 0.0f;

    std::vector<game::FireBurnOut> burnOuts;
    fire.Update(ecs, 0.1f, &world, 0.0f, &burnOuts);
    CHECK(source->phase == ecs::FirePhase::Smoldering);
    CHECK(burnOuts.empty());
    fire.Update(ecs, 0.1f, &world, 0.0f, &burnOuts);
    REQUIRE(burnOuts.size() == 1);
    CHECK(burnOuts[0].materialId == "wood_wall");

    const game::FireBurnResolution resolution =
        fire.ResolveBurnedCell(world, burnOuts[0]);
    CHECK(resolution.structureRemoved);
    REQUIRE(resolution.drops.size() == 1);
    CHECK(resolution.drops[0].defId == "wood");
    CHECK(resolution.drops[0].count == 2);

    const world::ChunkCoord coord = world::ChunkOf(10, 10);
    const world::Chunk* chunk = game::FindChunkInWorld(world, coord.cx, coord.cy);
    REQUIRE(chunk != nullptr);
    const world::SquareLocal local = world::SquareLocalOf(10, 10);
    CHECK(chunk->Square(local.lx, local.ly, 0)
              .Layer(world::SquareLayer::Furniture)
              .empty());
}

TEST_CASE("M23-F3-T7 母地图地面不删除且篝火燃尽保留设施") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    game::FireRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(registry.LoadFile(root / "assets" / "fire.json", &error),
                    error);
    game::FireSystem fire(registry, game::FireParams{}, 49u);

    world::World world;
    SetWoodFloor(world, 10, 10);
    SetFurniture(world, 11, 10, "object.campfire");
    LoadSquareChunk(world, 10, 10);
    ecs::EcsWorld ecs;

    const game::FireBurnResolution floor =
        fire.ResolveBurnedCell(world, game::FireBurnOut{10, 10, 0,
                                                          "wood_floor"});
    CHECK_FALSE(floor.structureRemoved);
    CHECK(floor.drops.empty());
    CHECK(world.GetOrCreateSquare(10, 10, 0).HasFloor());

    world::GridSquare& playerFloor = world.GetOrCreateSquare(12, 10, 0);
    world::SquareObject floorObject{"tile.floor.wood"};
    floorObject.playerBuilt = true;
    floorObject.underlay = "tile.floor.grass";
    playerFloor.SetFloor(floorObject);
    const game::FireBurnResolution builtFloor =
        fire.ResolveBurnedCell(world, game::FireBurnOut{12, 10, 0,
                                                          "wood_floor"});
    CHECK(builtFloor.structureRemoved);
    REQUIRE(builtFloor.drops.size() == 1);
    CHECK(builtFloor.drops[0].defId == "wood");
    CHECK(builtFloor.drops[0].count == 1);
    REQUIRE(playerFloor.Floor() != nullptr);
    CHECK(playerFloor.Floor()->sprite == "tile.floor.grass");

    const game::FireBurnResolution campfire =
        fire.ResolveBurnedCell(world, game::FireBurnOut{11, 10, 0,
                                                          "campfire"});
    CHECK_FALSE(campfire.structureRemoved);
    CHECK(campfire.drops.empty());
    const world::ChunkCoord coord = world::ChunkOf(11, 10);
    const world::Chunk* chunk = game::FindChunkInWorld(world, coord.cx, coord.cy);
    REQUIRE(chunk != nullptr);
    const world::SquareLocal local = world::SquareLocalOf(11, 10);
    CHECK_FALSE(chunk->Square(local.lx, local.ly, 0)
                    .Layer(world::SquareLayer::Furniture)
                    .empty());
}





TEST_CASE("M23-F3-T8 100 火源压力：上限内点燃/推进/熄灭") {
    game::FireRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(registry.LoadFromString(kSpreadJson, "fire-stress", &error),
                    error);
    game::FireParams params;
    params.ignitionSeconds = 0.05f;
    params.smolderSeconds = 10.0f;
    params.maxActiveSources = 100;
    params.maxSpreadsPerTick = 0;
    game::FireSystem fire(registry, params, 101u);

    world::World world;
    for (int i = 0; i < 100; ++i) {
        const int x = 20 + i % 10;
        const int y = 20 + i / 10;
        SetWoodFloor(world, x, y);
        LoadSquareChunk(world, x, y);
    }
    ecs::EcsWorld ecs;

    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) {
        const int x = 20 + i % 10;
        const int y = 20 + i / 10;
        REQUIRE_MESSAGE(
            fire.IgniteAt(ecs, world, x, y, 0, 0.0f, &error) ==
                game::FireIgnitionResult::Ignited,
            error);
    }
    fire.Update(ecs, 0.1f, &world, 0.0f);
    for (int frame = 0; frame < 100; ++frame) {
        fire.Update(ecs, 0.05f, &world, 0.0f);
    }
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started)
            .count();

    CHECK(fire.ActiveSourceCount(ecs) == 100);
    for (int i = 0; i < 100; ++i) {
        const ecs::FireSource* source =
            fire.FindAt(ecs, 20 + i % 10, 20 + i / 10, 0);
        REQUIRE(source != nullptr);
        CHECK(source->phase == ecs::FirePhase::Burning);
        CHECK(source->burnRemaining > 0.0f);
    }
    CHECK(elapsedMs < 2000.0);
    MESSAGE("100 火源压力：点燃+100 帧推进 " << elapsedMs << " ms");

    for (int i = 0; i < 100; ++i) {
        CHECK(fire.ExtinguishAt(ecs, 20 + i % 10, 20 + i / 10, 0));
    }
    CHECK(fire.ActiveSourceCount(ecs) == 0);
}

TEST_CASE("M23-F3-T8 燃尽墙体经 ChunkDiff 重载后不复活") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    game::FireRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(registry.LoadFile(root / "assets" / "fire.json", &error),
                    error);
    game::FireSystem fire(registry, game::FireParams{}, 53u);

    world::World world;
    SetFurniture(world, 10, 10, "object.wall_h", true);
    LoadSquareChunk(world, 10, 10);
    const world::ChunkCoord coord = world::ChunkOf(10, 10);
    world::Chunk* chunk = game::FindChunkInWorld(world, coord.cx, coord.cy);
    REQUIRE(chunk != nullptr);
    chunk->CaptureBaseline();

    const game::FireBurnResolution resolution =
        fire.ResolveBurnedCell(world, game::FireBurnOut{10, 10, 0,
                                                          "wood_wall"});
    REQUIRE(resolution.structureRemoved);
    save::ChunkDiff diff;
    REQUIRE(save::CaptureChunkDiff(*chunk, &diff, &error));
    const world::SquareLocal local = world::SquareLocalOf(10, 10);
    const save::ChunkSquareDiff* square = nullptr;
    for (const save::ChunkSquareDiff& candidate : diff.squares) {
        if (candidate.lx == local.lx && candidate.ly == local.ly &&
            candidate.z == 0) {
            square = &candidate;
        }
    }
    REQUIRE(square != nullptr);
    CHECK(square->layers[static_cast<std::size_t>(
              world::SquareLayer::Furniture)]
              .empty());

    world::World reloaded;
    SetFurniture(reloaded, 10, 10, "object.wall_h", true);
    LoadSquareChunk(reloaded, 10, 10);
    world::Chunk* reloadedChunk =
        game::FindChunkInWorld(reloaded, coord.cx, coord.cy);
    REQUIRE(reloadedChunk != nullptr);
    reloadedChunk->CaptureBaseline();
    REQUIRE(save::ApplyChunkDiff(diff, *reloadedChunk, &error));
    const world::SquareLocal reloadedLocal = world::SquareLocalOf(10, 10);
    CHECK(reloadedChunk
              ->Square(reloadedLocal.lx, reloadedLocal.ly, 0)
              .Layer(world::SquareLayer::Furniture)
              .empty());
}
