#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/systems/CombatSystem.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/Fire.h"
#include "game/FireComponents.h"
#include "game/FireSystem.h"
#include "game/Throwing.h"
#include "game/WorldQueries.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"
#include "world/World.h"

using namespace zl;

namespace {

const char* kThrowFireJson = R"({
  "format": "fire-v1",
  "materials": [
    {
      "id": "grass",
      "name": "草地",
      "sprites": ["decor.grass_tuft"],
      "flammability": {
        "ignitionThreshold": 0.0,
        "burnSeconds": 15.0,
        "smokePerSecond": 0.4,
        "heatRadius": 1.0
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

void SetGrassFloor(world::World& world, int wx, int wy) {
    world.GetOrCreateSquare(wx, wy, 0).SetFloor(
        world::SquareObject{"decor.grass_tuft"});
}

std::vector<float> IgniteThrowFireCells(game::FireSystem& fire,
                                        ecs::EcsWorld& ecs,
                                        world::World& world, int centerX,
                                        int centerY, float radius,
                                        float burnSeconds,
                                        float wetness = 0.0f) {
    std::vector<float> results;
    const std::vector<game::ThrowFireCell> cells =
        game::ThrowFireAreaCells(static_cast<float>(centerX) + 0.5f,
                                 static_cast<float>(centerY) + 0.5f, radius);
    for (const game::ThrowFireCell& cell : cells) {
        std::string error;
        const game::FireIgnitionResult result = fire.IgniteAt(
            ecs, world, cell.cellX, cell.cellY, 0, wetness, &error, 42);
        CHECK_MESSAGE(result == game::FireIgnitionResult::Ignited,
                      "cell ", cell.cellX, ",", cell.cellY, " result ",
                      static_cast<int>(result), " error ", error);
        results.push_back(static_cast<float>(result));
        if (result == game::FireIgnitionResult::Ignited &&
            burnSeconds > 0.0f) {
            ecs::FireSource* source =
                fire.FindAt(ecs, cell.cellX, cell.cellY, 0);
            REQUIRE(source != nullptr);
            source->burnDuration = burnSeconds;
            source->burnRemaining = burnSeconds;
        }
    }
    return results;
}

save::ComponentRegistry MakeRegistry() {
    save::ComponentRegistry registry;
    std::string error;
    REQUIRE(save::RegisterBuiltinComponents(registry, &error));
    CHECK(error.empty());
    return registry;
}

const save::SavedComponent* FindComponent(const save::SavedEntity& entity,
                                          std::uint32_t type) {
    for (const save::SavedComponent& component : entity.components) {
        if (component.type == type) {
            return &component;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("M34-F2-T4 投掷火焰范围、时长与无火源回退") {
    const std::vector<game::ThrowFireCell> cells =
        game::ThrowFireAreaCells(10.5f, 10.5f, 1.5f);
    REQUIRE(cells.size() == 9u);
    CHECK(cells.front().cellX == 10);
    CHECK(cells.front().cellY == 10);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        for (std::size_t j = i + 1; j < cells.size(); ++j) {
            const bool duplicate = cells[i].cellX == cells[j].cellX &&
                                   cells[i].cellY == cells[j].cellY;
            CHECK_FALSE(duplicate);
        }
    }
    CHECK(game::ThrowFireAreaCells(10.5f, 10.5f, 0.5f).size() == 1u);
    CHECK(game::ThrowFireAreaCells(std::nanf(""), 10.5f, 1.5f).empty());
    CHECK(game::ThrowFireBurnSeconds(45.0f, 0.4f) ==
          doctest::Approx(112.5f));
    CHECK(game::ThrowFireBurnSeconds(45.0f, 0.0f) ==
          doctest::Approx(112.5f));
    CHECK(game::ThrowFireBurnSeconds(240.0f, 0.4f) ==
          doctest::Approx(600.0f));
    CHECK(game::ThrowFireBurnSeconds(0.0f, 0.4f) == doctest::Approx(0.0f));
    CHECK(game::ThrowFireBurnSeconds(std::nanf(""), 0.4f) ==
          doctest::Approx(0.0f));

    game::FireRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(
        registry.LoadFromString(kThrowFireJson, "throw-fire", &error),
        error);
    game::FireSystem fire(registry, game::FireParams{}, 53u);
    world::World world;
    const std::vector<game::ThrowFireCell> worldCells =
        game::ThrowFireAreaCells(15.5f, 15.5f, 1.5f);
    for (const game::ThrowFireCell& cell : worldCells) {
        SetGrassFloor(world, cell.cellX, cell.cellY);
    }
    LoadSquareChunk(world, 15, 15);

    for (const game::ThrowFireCell& cell : worldCells) {
        const int wx = cell.cellX;
        const int wy = cell.cellY;
        const world::ChunkCoord chunkCoord = world::ChunkOf(wx, wy);
        const world::Chunk* chunk =
            game::FindChunkInWorld(world, chunkCoord.cx, chunkCoord.cy);
        REQUIRE_MESSAGE(chunk != nullptr,
                        "missing chunk at " << wx << "," << wy);
        const world::SquareLocal local = world::SquareLocalOf(wx, wy);
        REQUIRE_MESSAGE(
            game::FindFireMaterial(
                registry, chunk->Square(local.lx, local.ly, 0)) != nullptr,
            "missing material at " << wx << "," << wy);
    }

    ecs::EcsWorld ecs;
    const std::vector<float> results = IgniteThrowFireCells(
        fire, ecs, world, 15, 15, 1.5f, 112.5f);
    REQUIRE(results.size() == 9u);
    for (const float result : results) {
        CHECK(result ==
              static_cast<float>(game::FireIgnitionResult::Ignited));
    }
    CHECK(fire.ActiveSourceCount(ecs) == 9u);
    const ecs::FireSource* center = fire.FindAt(ecs, 15, 15, 0);
    REQUIRE(center != nullptr);
    CHECK(center->materialId == "grass");
    CHECK(center->ownerStableId == 42u);
    CHECK(center->phase == ecs::FirePhase::Igniting);
    CHECK(center->burnDuration == doctest::Approx(112.5f));
    CHECK(center->burnRemaining == doctest::Approx(112.5f));

    (void)world.GetOrCreateSquare(20, 20, 0);
    LoadSquareChunk(world, 20, 20);
    error.clear();
    CHECK(fire.IgniteAt(ecs, world, 20, 20, 0, 0.0f, &error, 42) ==
          game::FireIgnitionResult::InvalidMaterial);
    CHECK(error.find("可燃物") != std::string::npos);
    CHECK(fire.ActiveSourceCount(ecs) == 9u);

    SetGrassFloor(world, 30, 30);
    LoadSquareChunk(world, 30, 30);
    game::FireParams wetParams;
    wetParams.wetnessPenalty = 2.0f;
    game::FireSystem wetFire(registry, wetParams, 59u);
    ecs::EcsWorld wetEcs;
    error.clear();
    CHECK(wetFire.IgniteAt(wetEcs, world, 30, 30, 0, 1.0f, &error, 42) ==
          game::FireIgnitionResult::TooWet);
    CHECK(wetFire.ActiveSourceCount(wetEcs) == 0u);
}

TEST_CASE("M34-F2-T4 投掷火区伤害与蔓延沿用 M23") {
    game::FireRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(
        registry.LoadFromString(kThrowFireJson, "throw-fire-damage", &error),
        error);
    game::FireParams params;
    params.fireDamagePerSecond = 6.0f;
    params.fireDamageRadius = 1.0f;
    params.spreadIntervalSeconds = 1.0f;
    params.spreadChanceScale = 1.0f;
    params.maxSpreadsPerTick = 1;
    game::FireSystem fire(registry, params, 61u);

    world::World world;
    for (int y = 14; y <= 16; ++y) {
        for (int x = 14; x <= 16; ++x) {
            SetGrassFloor(world, x, y);
        }
    }
    SetGrassFloor(world, 17, 15);
    LoadSquareChunk(world, 15, 15);

    ecs::EcsWorld ecs;
    const std::vector<float> results = IgniteThrowFireCells(
        fire, ecs, world, 15, 15, 1.5f, 112.5f);
    REQUIRE(results.size() == 9u);
    fire.Update(ecs, 2.0f);

    characters::CombatSystem combat;
    const ecs::Entity near = ecs.Create();
    ecs.Registry().emplace<ecs::Position>(near, ecs::Position{15.5f, 15.5f, 0});
    ecs.Registry().emplace<ecs::Health>(near, ecs::Health{100.0f, 100.0f});
    const ecs::Entity far = ecs.Create();
    ecs.Registry().emplace<ecs::Position>(far, ecs::Position{20.5f, 20.5f, 0});
    ecs.Registry().emplace<ecs::Health>(far, ecs::Health{100.0f, 100.0f});
    const ecs::Entity zombie = ecs.Create();
    ecs.Registry().emplace<ecs::Position>(zombie,
                                          ecs::Position{15.8f, 15.5f, 0});
    ecs.Registry().emplace<ecs::Health>(zombie, ecs::Health{100.0f, 100.0f});
    ecs.Registry().emplace<ecs::ZombieVariant>(zombie);
    ecs.Registry().emplace<ecs::PerceptionState>(
        zombie, ecs::PerceptionState{});

    CHECK(game::ApplyFireDamage(ecs, combat, params, 1.0f) == 2u);
    CHECK(ecs.Registry().get<ecs::Health>(near).current ==
          doctest::Approx(94.0f));
    CHECK(ecs.Registry().get<ecs::Health>(far).current ==
          doctest::Approx(100.0f));
    CHECK(ecs.Registry().all_of<ecs::BurningZombie>(zombie));

    for (int y = 14; y <= 16; ++y) {
        for (int x = 14; x <= 16; ++x) {
            ecs::FireSource* source = fire.FindAt(ecs, x, y, 0);
            REQUIRE(source != nullptr);
            source->spreadAccumulator =
                game::FireSpreadInterval(source->burnDuration, params) - 0.01f;
        }
    }
    fire.Update(ecs, 0.02f, &world);
    CHECK(fire.FindAt(ecs, 17, 15, 0) != nullptr);
    CHECK(fire.ActiveSourceCount(ecs) == 10u);
}

TEST_CASE("M34-F2-T4 投掷火源进入实体快照并可恢复") {
    game::FireRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(
        registry.LoadFromString(kThrowFireJson, "throw-fire-save", &error),
        error);
    game::FireSystem fire(registry, game::FireParams{}, 67u);
    world::World world;
    SetGrassFloor(world, 40, 40);
    LoadSquareChunk(world, 40, 40);

    ecs::EcsWorld ecs;
    REQUIRE(fire.IgniteAt(ecs, world, 40, 40, 0, 0.0f, &error, 42) ==
            game::FireIgnitionResult::Ignited);
    ecs::FireSource* source = fire.FindAt(ecs, 40, 40, 0);
    REQUIRE(source != nullptr);
    source->burnDuration = 112.5f;
    source->burnRemaining = 112.5f;

    const save::ComponentRegistry components = MakeRegistry();
    save::EntitySnapshot snapshot;
    REQUIRE(save::CaptureEntities(ecs, components, &snapshot, {}, &error));
    REQUIRE(snapshot.entities.size() == 1u);
    const save::SavedEntity& saved = snapshot.entities.front();
    CHECK(FindComponent(saved, save::kSaveComponentFireAnchor) != nullptr);
    CHECK(FindComponent(saved, save::kSaveComponentFireSource) != nullptr);

    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(snapshot, &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
    ecs::EcsWorld target;
    std::vector<ecs::Entity> loaded;
    REQUIRE(save::LoadEntities(decoded, target, components, &loaded, &error));
    REQUIRE(loaded.size() == 1u);
    const ecs::FireAnchor* anchor =
        target.Registry().try_get<ecs::FireAnchor>(loaded.front());
    REQUIRE(anchor != nullptr);
    CHECK(anchor->x == 40);
    CHECK(anchor->y == 40);
    const ecs::FireSource* loadedSource =
        target.Registry().try_get<ecs::FireSource>(loaded.front());
    REQUIRE(loadedSource != nullptr);
    CHECK(loadedSource->materialId == "grass");
    CHECK(loadedSource->ownerStableId == 0u);
    CHECK(loadedSource->burnDuration == doctest::Approx(112.5f));
    CHECK(loadedSource->burnRemaining == doctest::Approx(112.5f));
}
