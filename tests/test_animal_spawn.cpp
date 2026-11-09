#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "ai/components/AiComponents.h"
#include "characters/components/AnimalComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/systems/CombatSystem.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/AnimalSystem.h"
#include "game/Animals.h"
#include "game/Poi.h"
#include "game/WorldQueries.h"
#include "items/ItemRegistry.h"
#include "items/LootTable.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"
#include "world/Chunk.h"
#include "world/ChunkData.h"
#include "world/World.h"
#include "world/WorldCoords.h"

using namespace zl;

namespace {

const char* kAnimalSpawnJson = R"({
  "format": "animals-v1",
  "animals": [
    {
      "id": "deer",
      "name": "鹿",
      "kind": "passive",
      "sprite": "animal.deer",
      "maxHealth": 45.0,
      "wanderSpeed": 2.0,
      "fleeSpeed": 4.8,
      "alertRadius": 9.0,
      "fleeRadius": 8.0,
      "hearingRadius": 13.0,
      "fleeSeconds": 5.0,
      "damage": 0.0,
      "attackRange": 0.0,
      "attackCooldown": 0.0,
      "corpseSprite": "corpse.placeholder",
      "corpseSearchSeconds": 1.5,
      "lootTableId": "spawn_loot",
      "spawn": { "forest": 1.0, "maxPerChunk": 1 }
    },
    {
      "id": "rabbit",
      "name": "兔子",
      "kind": "passive",
      "sprite": "animal.rabbit",
      "maxHealth": 18.0,
      "wanderSpeed": 1.7,
      "fleeSpeed": 5.5,
      "alertRadius": 7.0,
      "fleeRadius": 6.5,
      "hearingRadius": 11.0,
      "fleeSeconds": 4.0,
      "damage": 0.0,
      "attackRange": 0.0,
      "attackCooldown": 0.0,
      "corpseSprite": "corpse.placeholder",
      "corpseSearchSeconds": 1.0,
      "lootTableId": "spawn_loot",
      "spawn": { "meadow": 1.0, "maxPerChunk": 1 }
    },
    {
      "id": "boar",
      "name": "野猪",
      "kind": "dangerous",
      "sprite": "animal.boar",
      "maxHealth": 85.0,
      "wanderSpeed": 2.2,
      "fleeSpeed": 3.4,
      "alertRadius": 8.0,
      "fleeRadius": 10.0,
      "hearingRadius": 11.0,
      "fleeSeconds": 3.5,
      "damage": 10.0,
      "attackRange": 1.5,
      "attackCooldown": 1.2,
      "corpseSprite": "corpse.placeholder",
      "corpseSearchSeconds": 2.0,
      "lootTableId": "spawn_loot",
      "spawn": {
        "poi": 1.0,
        "maxPerChunk": 1,
        "poiIds": ["farm_barn"]
      }
    }
  ]
})";

const char* kSpawnLoot = R"({
  "format": "loot-v1",
  "seed": 7,
  "tables": [
    { "id": "spawn_loot", "name": "测试掉落", "capacityKg": 1.0, "rolls": 0 }
  ]
})";

struct SpawnFixture {
    items::ItemRegistry items;
    items::LootTableRegistry loot;
    game::AnimalRegistry animals;

    bool Load() {
        std::string error;
        if (!loot.LoadFromString(kSpawnLoot, "spawn-loot", items, &error)) {
            return false;
        }
        return animals.LoadFromString(kAnimalSpawnJson, "spawn-animals",
                                      items, loot, &error);
    }
};

void MakeLoadedChunk(world::World& world, int chunkX, int chunkY) {
    const int originX = chunkX * core::kChunkSize;
    const int originY = chunkY * core::kChunkSize;
    for (int ly = 0; ly < core::kChunkSize; ++ly) {
        for (int lx = 0; lx < core::kChunkSize; ++lx) {
            world::GridSquare& square =
                world.GetOrCreateSquare(originX + lx, originY + ly, 0);
            square.SetFloor(world::SquareObject{"tile.floor.grass"});
        }
    }
    const world::ChunkCoord coord = world::ChunkOf(originX, originY);
    world::Cell* cell =
        world.GetCell(world::FloorDiv(coord.cx, core::kChunksPerCell),
                      world::FloorDiv(coord.cy, core::kChunksPerCell));
    REQUIRE(cell != nullptr);
    const world::ChunkLocal local = world::ChunkLocalOf(coord.cx, coord.cy);
    world::Chunk* chunk = cell->GetChunk(local.lcx, local.lcy);
    REQUIRE(chunk != nullptr);
    chunk->SetState(world::ChunkState::Loading);
    chunk->SetState(world::ChunkState::Loaded);
}

void AddTree(world::World& world, int wx, int wy) {
    world::SquareObject tree{"object.tree"};
    tree.blocksMovement = true;
    world.GetOrCreateSquare(wx, wy, 0).AddObject(
        world::SquareLayer::Furniture, tree);
}

void AddWall(world::World& world, int wx, int wy) {
    world::SquareObject wall{"object.wall"};
    wall.blocksMovement = true;
    world.GetOrCreateSquare(wx, wy, 0).AddObject(
        world::SquareLayer::Furniture, wall);
}

void MakeOpenArea(world::World& world, int chunksX) {
    for (int cx = 0; cx < chunksX; ++cx) {
        MakeLoadedChunk(world, cx, 0);
    }
}

ecs::Entity AddTestAnimal(ecs::EcsWorld& ecs, const std::string& species,
                          ecs::AnimalKind kind, float x, float y) {
    const ecs::Entity entity = ecs.Create();
    entt::registry& registry = ecs.Registry();
    registry.emplace<ecs::Position>(entity, ecs::Position{x, y, 0});
    registry.emplace<ecs::Health>(entity, ecs::Health{100.0f, 100.0f});
    registry.emplace<ecs::Animal>(entity, ecs::Animal{species, kind});
    registry.emplace<ecs::Flee>(entity, ecs::Flee{});
    registry.emplace<ecs::AnimalWander>(entity, ecs::AnimalWander{});
    registry.emplace<ecs::LodState>(entity, ecs::LodState{});
    return entity;
}

ecs::Entity AddTestPlayer(ecs::EcsWorld& ecs, float x, float y) {
    const ecs::Entity entity = ecs.Create();
    entt::registry& registry = ecs.Registry();
    registry.emplace<ecs::PlayerTag>(entity);
    registry.emplace<ecs::Position>(entity, ecs::Position{x, y, 0});
    registry.emplace<ecs::Health>(entity, ecs::Health{100.0f, 100.0f});
    return entity;
}

std::multiset<std::string> Species(const ecs::EcsWorld& ecs) {
    std::multiset<std::string> result;
    ecs.Registry().view<ecs::Animal>().each(
        [&](const ecs::Entity entity, const ecs::Animal& animal) {
            if (ecs.Alive(entity)) {
                result.insert(animal.speciesId);
            }
        });
    return result;
}

game::AnimalSystem::Params SpawnParams() {
    game::AnimalSystem::Params params;
    params.enabled = true;
    params.maxLive = 12;
    params.minPlayerDistance = 0.0f;
    params.poiRadius = 16.0f;
    params.seed = 20261030u;
    return params;
}

}  // namespace

TEST_CASE("M23-F2b-T2 按森林/草地/POI 生成且不重复种子") {
    SpawnFixture fixture;
    REQUIRE(fixture.Load());
    world::World world;
    ecs::EcsWorld ecs;
    MakeLoadedChunk(world, 0, 0);
    AddTree(world, 2, 2);
    game::AnimalSystem system(fixture.animals, world, ecs, nullptr,
                              SpawnParams());
    system.SetFocus(80.0f, 80.0f);
    system.OnChunkLoaded(0, 0);
    system.Update(0.016f);

    const std::multiset<std::string> species = Species(ecs);
    CHECK(species.count("deer") == 1u);
    CHECK(species.count("rabbit") == 1u);
    CHECK(species.count("boar") == 0u);
    CHECK(system.GetStats().spawned == 2u);

    system.OnChunkLoaded(0, 0);
    CHECK(Species(ecs).size() == 2u);

    game::PoiConfig pois;
    pois.enabled = true;
    game::PoiDef poi;
    poi.id = "farm_barn";
    poi.name = "农场仓";
    poi.markerX = 5.5f;
    poi.markerY = 5.5f;
    pois.defs.push_back(poi);
    world::World poiWorld;
    ecs::EcsWorld poiEcs;
    MakeLoadedChunk(poiWorld, 0, 0);
    AddTree(poiWorld, 2, 2);
    game::AnimalSystem poiSystem(fixture.animals, poiWorld, poiEcs, &pois,
                                 SpawnParams());
    poiSystem.SetFocus(80.0f, 80.0f);
    poiSystem.OnChunkLoaded(0, 0);
    poiSystem.Update(0.016f);
    const std::multiset<std::string> poiSpecies = Species(poiEcs);
    CHECK(poiSpecies.count("boar") == 1u);
}

TEST_CASE("M23-F2b-T2 动物生成上限与确定性") {
    SpawnFixture fixture;
    REQUIRE(fixture.Load());
    game::AnimalSystem::Params params = SpawnParams();
    params.maxLive = 1;

    world::World firstWorld;
    ecs::EcsWorld firstEcs;
    for (int cx = 0; cx < 3; ++cx) {
        MakeLoadedChunk(firstWorld, cx, 0);
        AddTree(firstWorld, cx * core::kChunkSize + 2, 2);
    }
    game::AnimalSystem first(fixture.animals, firstWorld, firstEcs, nullptr,
                             params);
    first.SetFocus(80.0f, 80.0f);
    for (int cx = 0; cx < 3; ++cx) {
        first.OnChunkLoaded(cx, 0);
    }
    first.Update(0.016f);
    CHECK(first.GetStats().live == 1);
    CHECK(Species(firstEcs).size() == 1u);

    world::World secondWorld;
    ecs::EcsWorld secondEcs;
    MakeLoadedChunk(secondWorld, 0, 0);
    AddTree(secondWorld, 2, 2);
    game::AnimalSystem second(fixture.animals, secondWorld, secondEcs, nullptr,
                              params);
    second.SetFocus(80.0f, 80.0f);
    second.OnChunkLoaded(0, 0);
    second.Update(0.016f);
    REQUIRE(Species(firstEcs).size() == 1u);
    REQUIRE(Species(secondEcs).size() == 1u);
    CHECK(Species(firstEcs) == Species(secondEcs));
}

TEST_CASE("M23-F2b-T2 chunk 卸载保存并在重载时恢复") {
    SpawnFixture fixture;
    REQUIRE(fixture.Load());
    world::World world;
    ecs::EcsWorld ecs;
    MakeLoadedChunk(world, 0, 0);
    AddTree(world, 2, 2);
    game::AnimalSystem system(fixture.animals, world, ecs, nullptr,
                              SpawnParams());
    system.SetFocus(80.0f, 80.0f);
    system.OnChunkLoaded(0, 0);
    system.Update(0.016f);
    const std::multiset<std::string> before = Species(ecs);
    REQUIRE(before.size() == 2u);

    const ecs::Entity one = [&]() {
        ecs::Entity found = ecs::EcsWorld::kNull;
        ecs.Registry().view<ecs::Animal>().each(
            [&](const ecs::Entity entity, const ecs::Animal&) {
                if (found == ecs::EcsWorld::kNull) {
                    found = entity;
                }
            });
        return found;
    }();
    REQUIRE(one != ecs::EcsWorld::kNull);
    const auto* healthBefore = ecs.Registry().try_get<ecs::Health>(one);
    const auto* animalBefore = ecs.Registry().try_get<ecs::Animal>(one);
    REQUIRE(healthBefore != nullptr);
    REQUIRE(animalBefore != nullptr);
    const float health = healthBefore->current;
    const std::string speciesBefore = animalBefore->speciesId;

    system.OnChunkUnloaded(0, 0);
    ecs.Flush();
    system.Update(0.016f);
    CHECK(Species(ecs).empty());
    CHECK(system.GetStats().savedAnimals == 2);
    CHECK(system.GetStats().pendingChunks == 1);

    system.OnChunkLoaded(0, 0);
    ecs.Flush();
    system.Update(0.016f);
    CHECK(Species(ecs) == before);
    CHECK(system.GetStats().restored == 2u);
    bool healthRestored = false;
    ecs.Registry().view<ecs::Animal, ecs::Health>().each(
        [&](const ecs::Entity, const ecs::Animal& animal,
            const ecs::Health& value) {
            if (animal.speciesId == speciesBefore &&
                value.current == doctest::Approx(health)) {
                healthRestored = true;
            }
        });
    CHECK(healthRestored);
}

TEST_CASE("M23-F2b-T2 远离玩家的动物按 LodState 降频") {
    SpawnFixture fixture;
    REQUIRE(fixture.Load());
    world::World world;
    ecs::EcsWorld ecs;
    MakeLoadedChunk(world, 0, 0);
    AddTree(world, 2, 2);
    game::AnimalSystem system(fixture.animals, world, ecs, nullptr,
                              SpawnParams());
    system.SetFocus(80.0f, 80.0f);
    system.OnChunkLoaded(0, 0);
    system.Update(0.016f);
    REQUIRE(system.GetStats().live == 2);

    ecs.Registry().view<ecs::LodState>().each(
        [](const ecs::Entity, ecs::LodState& lod) {
            lod.updateThisTick = false;
        });
    system.Update(0.016f);
    CHECK(system.GetStats().skippedFar == 2u);
    CHECK(system.GetStats().updated == 0u);

    ecs.Registry().view<ecs::LodState>().each(
        [](const ecs::Entity, ecs::LodState& lod) {
            lod.updateThisTick = true;
        });
    system.Update(0.016f);
    CHECK(system.GetStats().updated == 2u);
}


TEST_CASE("M23-F2b-T3 视野发现玩家并逃跑，墙体遮挡不发现") {
    SpawnFixture fixture;
    REQUIRE(fixture.Load());

    world::World openWorld;
    ecs::EcsWorld openEcs;
    MakeOpenArea(openWorld, 2);
    game::AnimalSystem openSystem(fixture.animals, openWorld, openEcs, nullptr,
                                  SpawnParams());
    const ecs::Entity deer =
        AddTestAnimal(openEcs, "deer", ecs::AnimalKind::Passive, 10.5f, 5.5f);
    AddTestPlayer(openEcs, 19.5f, 5.5f);
    CHECK_FALSE(game::IsSquareBlocked(openWorld, 10, 5, 0));
    CHECK_FALSE(game::IsSquareBlocked(openWorld, 15, 5, 0));
    CHECK_FALSE(game::IsSquareBlocked(openWorld, 19, 5, 0));
    openSystem.Update(0.016f);
    const auto* openFlee = openEcs.Registry().try_get<ecs::Flee>(deer);
    REQUIRE(openFlee != nullptr);
    CHECK(openFlee->active);
    CHECK(openSystem.GetStats().fleeing == 1);

    world::World walledWorld;
    ecs::EcsWorld walledEcs;
    MakeOpenArea(walledWorld, 2);
    AddWall(walledWorld, 15, 5);
    game::AnimalSystem walledSystem(fixture.animals, walledWorld, walledEcs,
                                    nullptr, SpawnParams());
    const ecs::Entity walledDeer =
        AddTestAnimal(walledEcs, "deer", ecs::AnimalKind::Passive, 10.5f,
                      5.5f);
    AddTestPlayer(walledEcs, 19.5f, 5.5f);
    walledSystem.Update(0.016f);
    const auto* walledFlee = walledEcs.Registry().try_get<ecs::Flee>(walledDeer);
    REQUIRE(walledFlee != nullptr);
    CHECK_FALSE(walledFlee->active);
}

TEST_CASE("M23-F2b-T3 枪声触发逃跑并朝声源反方向移动") {
    SpawnFixture fixture;
    REQUIRE(fixture.Load());
    world::World world;
    ecs::EcsWorld ecs;
    MakeOpenArea(world, 2);
    game::AnimalSystem system(fixture.animals, world, ecs, nullptr,
                              SpawnParams());
    const ecs::Entity deer =
        AddTestAnimal(ecs, "deer", ecs::AnimalKind::Passive, 10.5f, 5.5f);
    core::SoundEvent shot;
    shot.x = 13.5f;
    shot.y = 5.5f;
    shot.radius = 28.0f;
    shot.type = core::SoundType::Gunshot;
    system.PushSound(shot);
    system.Update(0.016f);
    auto& registry = ecs.Registry();
    auto* flee = registry.try_get<ecs::Flee>(deer);
    REQUIRE(flee != nullptr);
    CHECK(flee->active);
    CHECK(flee->directionX < -0.5f);
    CHECK(system.GetStats().heard == 1u);

    flee->remaining = 0.05f;
    system.Update(0.1f);
    CHECK_FALSE(flee->active);
}

TEST_CASE("M23-F2b-T3 受击后逃跑，野猪反击玩家") {
    SpawnFixture fixture;
    REQUIRE(fixture.Load());
    world::World world;
    ecs::EcsWorld ecs;
    MakeOpenArea(world, 2);
    characters::CombatSystem combat;
    game::AnimalSystem system(fixture.animals, world, ecs, nullptr,
                              SpawnParams());
    system.SetCombat(&combat);

    const ecs::Entity deer =
        AddTestAnimal(ecs, "deer", ecs::AnimalKind::Passive, 10.5f, 5.5f);
    const ecs::Entity player = AddTestPlayer(ecs, 10.5f, 7.5f);
    ecs::DamageEvent hit;
    hit.sourceId = ecs.IdOf(player).id;
    hit.type = ecs::DamageType::Blunt;
    hit.amount = 8.0f;
    hit.dirX = 0.0f;
    hit.dirY = -1.0f;
    REQUIRE(combat.ApplyDamage(ecs, deer, hit));
    system.Update(0.016f);
    const auto* deerFlee = ecs.Registry().try_get<ecs::Flee>(deer);
    REQUIRE(deerFlee != nullptr);
    CHECK(deerFlee->active);
    CHECK(deerFlee->directionY < -0.5f);

    const ecs::Entity boar =
        AddTestAnimal(ecs, "boar", ecs::AnimalKind::Dangerous, 15.5f, 5.5f);
    const ecs::Entity boarPlayer = AddTestPlayer(ecs, 16.5f, 5.5f);
    system.Update(0.016f);
    const auto* boarWander = ecs.Registry().try_get<ecs::AnimalWander>(boar);
    REQUIRE(boarWander != nullptr);
    CHECK(boarWander->retaliating);
    CHECK(boarWander->attackCooldown > 0.0f);
    const auto* playerHealth =
        ecs.Registry().try_get<ecs::Health>(boarPlayer);
    REQUIRE(playerHealth != nullptr);
    CHECK(playerHealth->current < 100.0f);
    CHECK(system.GetStats().attacks == 1u);
}

TEST_CASE("M23-F2b-T3 僵尸攻击不命中动物，玩家可命中动物") {
    ecs::EcsWorld ecs;
    characters::CombatSystem combat;
    const ecs::Entity zombie = ecs.Create();
    const ecs::Entity animal = AddTestAnimal(ecs, "rabbit",
                                             ecs::AnimalKind::Passive, 1.5f,
                                             0.5f);
    const ecs::Entity player = AddTestPlayer(ecs, 0.5f, 0.5f);
    entt::registry& registry = ecs.Registry();
    registry.emplace<ecs::Position>(zombie, ecs::Position{0.5f, 0.5f, 0});
    registry.emplace<ecs::Health>(zombie, ecs::Health{100.0f, 100.0f});
    registry.emplace<ecs::AiState>(zombie, ecs::AiState{});
    ecs::AttackIntent zombieAttack;
    zombieAttack.pending = true;
    zombieAttack.dirX = 1.0f;
    zombieAttack.dirY = 0.0f;
    zombieAttack.range = 3.0f;
    zombieAttack.arcDegrees = 180.0f;
    zombieAttack.damage = 10.0f;
    registry.emplace<ecs::AttackIntent>(zombie, zombieAttack);
    combat.Update(ecs, 0.016f);
    CHECK(registry.get<ecs::Health>(animal).current ==
          doctest::Approx(100.0f));

    ecs::AttackIntent playerAttack;
    playerAttack.pending = true;
    playerAttack.dirX = 1.0f;
    playerAttack.dirY = 0.0f;
    playerAttack.range = 3.0f;
    playerAttack.arcDegrees = 180.0f;
    playerAttack.damage = 10.0f;
    registry.emplace<ecs::AttackIntent>(player, playerAttack);
    combat.Update(ecs, 0.016f);
    CHECK(registry.get<ecs::Health>(animal).current ==
          doctest::Approx(90.0f));
}

TEST_CASE("M23-F2b-T7 未加载区块动物快照写入存档并恢复") {
    SpawnFixture fixture;
    REQUIRE(fixture.Load());

    world::World sourceWorld;
    ecs::EcsWorld sourceEcs;
    MakeLoadedChunk(sourceWorld, 0, 0);
    AddTree(sourceWorld, 2, 2);
    game::AnimalSystem source(fixture.animals, sourceWorld, sourceEcs, nullptr,
                              SpawnParams());
    source.SetFocus(80.0f, 80.0f);
    source.OnChunkLoaded(0, 0);
    source.Update(0.016f);
    REQUIRE(source.GetStats().live >= 2);

    ecs::Entity deer = ecs::EcsWorld::kNull;
    sourceEcs.Registry().view<ecs::Animal, ecs::Position>().each(
        [&](const ecs::Entity entity, const ecs::Animal& animal,
            const ecs::Position&) {
            if (deer == ecs::EcsWorld::kNull && animal.speciesId == "deer") {
                deer = entity;
            }
        });
    REQUIRE(deer != ecs::EcsWorld::kNull);
    auto& sourceRegistry = sourceEcs.Registry();
    sourceRegistry.get<ecs::Health>(deer).current = 7.5f;
    auto& flee = sourceRegistry.get<ecs::Flee>(deer);
    flee.active = true;
    flee.remaining = 4.25f;
    flee.directionX = 1.0f;
    flee.directionY = 0.0f;
    flee.threatX = 10.0f;
    flee.threatY = 10.0f;
    flee.alertness = 0.5f;

    source.OnChunkUnloaded(0, 0);
    sourceEcs.Flush();
    REQUIRE(source.GetStats().savedAnimals == 2);
    source.StoreState(sourceEcs);
    sourceEcs.Flush();

    save::ComponentRegistry components;
    std::string error;
    REQUIRE(save::RegisterBuiltinComponents(components, &error));
    save::EntitySnapshot snapshot;
    REQUIRE(save::CaptureEntities(sourceEcs, components, &snapshot, {}, &error));
    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(snapshot, &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));

    world::World targetWorld;
    MakeLoadedChunk(targetWorld, 0, 0);
    ecs::EcsWorld targetEcs;
    std::vector<ecs::Entity> loaded;
    REQUIRE(save::LoadEntities(decoded, targetEcs, components, &loaded,
                               &error));
    game::AnimalSystem target(fixture.animals, targetWorld, targetEcs, nullptr,
                              SpawnParams());
    target.LoadState(targetEcs);
    CHECK(target.GetStats().pendingChunks == 1);
    CHECK(target.GetStats().savedAnimals == 2);

    target.OnChunkLoaded(0, 0);
    targetEcs.Flush();
    ecs::Entity restoredDeer = ecs::EcsWorld::kNull;
    targetEcs.Registry().view<ecs::Animal, ecs::Health, ecs::Flee>().each(
        [&](const ecs::Entity entity, const ecs::Animal& animal,
            const ecs::Health& health, const ecs::Flee& value) {
            if (animal.speciesId == "deer") {
                restoredDeer = entity;
                CHECK(health.current == doctest::Approx(7.5f));
                CHECK(value.active);
                CHECK(value.remaining == doctest::Approx(4.25f));
                CHECK(value.alertness == doctest::Approx(0.5f));
            }
        });
    REQUIRE(restoredDeer != ecs::EcsWorld::kNull);

    target.OnChunkLoaded(0, 0);
    targetEcs.Flush();
    CHECK(target.GetStats().live == 2);
    CHECK(target.GetStats().spawned == 0u);
}

TEST_CASE("M23-F2b-T7 动物上限、墙体规避与更新预算") {
    SpawnFixture fixture;
    REQUIRE(fixture.Load());

    world::World world;
    ecs::EcsWorld ecs;
    game::AnimalSystem::Params params = SpawnParams();
    params.maxLive = 36;
    game::AnimalSystem system(fixture.animals, world, ecs, nullptr, params);
    system.SetFocus(80.0f, 80.0f);
    for (int cx = 0; cx < 20; ++cx) {
        MakeLoadedChunk(world, cx, 0);
        AddTree(world, cx * core::kChunkSize + 2, 2);
        system.OnChunkLoaded(cx, 0);
    }
    system.Update(0.016f);
    CHECK(system.GetStats().live > 0);
    CHECK(system.GetStats().live <= 36);
    ecs.Registry().view<ecs::Animal, ecs::Position>().each(
        [&](const ecs::Entity, const ecs::Animal&, const ecs::Position& position) {
            CHECK(std::isfinite(position.x));
            CHECK(std::isfinite(position.y));
            CHECK(position.z == 0);
            CHECK_FALSE(game::IsSquareBlocked(
                world, static_cast<int>(std::floor(position.x)),
                static_cast<int>(std::floor(position.y)), 0));
        });

    world::World wallWorld;
    ecs::EcsWorld wallEcs;
    MakeOpenArea(wallWorld, 1);
    AddWall(wallWorld, 11, 5);
    game::AnimalSystem wallSystem(fixture.animals, wallWorld, wallEcs, nullptr,
                                  SpawnParams());
    const ecs::Entity deer =
        AddTestAnimal(wallEcs, "deer", ecs::AnimalKind::Passive, 10.5f, 5.5f);
    auto& wander = wallEcs.Registry().get<ecs::AnimalWander>(deer);
    wander.hasGoal = true;
    wander.goalX = 13.5f;
    wander.goalY = 5.5f;
    wallSystem.Update(0.5f);
    const auto& wallPosition = wallEcs.Registry().get<ecs::Position>(deer);
    CHECK(wallPosition.x < 11.0f);
    CHECK(game::IsSquareBlocked(wallWorld, 11, 5, 0));

    constexpr int kTicks = 240;
    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < kTicks; ++i) {
        system.Update(0.016f);
    }
    const double avgMs =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started)
            .count() /
        static_cast<double>(kTicks);
    MESSAGE("36 动物更新：" << avgMs << " ms/tick，live="
                            << system.GetStats().live);
    CHECK(avgMs < 35.0);
#ifdef NDEBUG
    CHECK(avgMs < 4.0);
#endif
}
