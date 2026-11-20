#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "ai/components/AiComponents.h"
#include "characters/components/AnimalComponents.h"
#include "characters/components/CharacterComponents.h"
#include "core/Config.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/BuildingRules.h"
#include "game/FireSystem.h"
#include "game/HomesteadComponents.h"
#include "game/SettlementNpc.h"
#include "game/StructureComponents.h"
#include "game/TrapSystem.h"
#include "game/Traps.h"
#include "game/WorldQueries.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"
#include "world/World.h"

using namespace zl;

namespace {

constexpr const char* kModTraps = R"({
  "format": "traps-v1",
  "defs": [
    {
      "id": "mod_snare",
      "name": "模组套索",
      "buildingId": "bear_trap",
      "damage": 9.0,
      "radius": 1.0,
      "cooldownSeconds": 0.0,
      "uses": 1,
      "staggerSeconds": 0.5,
      "noiseRadius": 0.0,
      "triggerMask": ["zombie"],
      "armSeconds": 0.0
    },
    {
      "id": "bad_damage",
      "name": "坏伤害",
      "buildingId": "bear_trap",
      "damage": 1000.0,
      "uses": 1
    },
    {
      "id": "bad_building",
      "name": "坏建造项",
      "buildingId": "missing_trap_building",
      "damage": 10.0,
      "uses": 1
    },
    {
      "id": "bear_trap",
      "name": "覆盖捕兽夹",
      "buildingId": "bear_trap",
      "damage": 1.0,
      "uses": 1
    }
  ]
})";

save::ComponentRegistry MakeSaveRegistry() {
    save::ComponentRegistry registry;
    std::string error;
    REQUIRE(save::RegisterBuiltinComponents(registry, &error));
    CHECK(error.empty());
    return registry;
}

}  // namespace

TEST_CASE("M34-F3-T1 traps-v1 加载三个内置陷阱并校验建造项") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    core::Config config;
    REQUIRE(config.LoadFile(root / "config" / "game.json"));

    const std::vector<game::BuildableDef> buildables =
        game::LoadBuildableDefs(config);
    REQUIRE(buildables.size() == 18u);
    CHECK(std::any_of(buildables.begin(), buildables.end(),
                      [](const game::BuildableDef& def) {
                          return def.id == "bear_trap" && def.woodCost == 3;
                      }));
    CHECK(std::any_of(buildables.begin(), buildables.end(),
                      [](const game::BuildableDef& def) {
                          return def.id == "tripwire_alarm" &&
                                 def.woodCost == 1;
                      }));
    CHECK(std::any_of(buildables.begin(), buildables.end(),
                      [](const game::BuildableDef& def) {
                          return def.id == "fuel_trap" && def.woodCost == 4;
                      }));

    game::TrapDefsConfig traps;
    std::string error;
    REQUIRE_MESSAGE(
        game::LoadTrapDefsConfig(config, buildables, &traps, &error), error);
    REQUIRE(traps.enabled);
    REQUIRE(traps.defs.size() == 3u);

    const game::TrapDef* bear = traps.Find("bear_trap");
    REQUIRE(bear != nullptr);
    CHECK(bear->buildingId == "bear_trap");
    CHECK(bear->damage == doctest::Approx(55.0f));
    CHECK(bear->radius == doctest::Approx(1.0f));
    CHECK(bear->cooldownSeconds == doctest::Approx(2.0f));
    CHECK(bear->uses == 4);
    CHECK(bear->staggerSeconds == doctest::Approx(2.0f));
    CHECK(bear->armSeconds == doctest::Approx(1.0f));
    CHECK(bear->triggerMask ==
          (game::kTrapTriggerZombie | game::kTrapTriggerAnimal));
    CHECK_FALSE(bear->fire.has_value());

    const game::TrapDef* tripwire = traps.Find("tripwire_alarm");
    REQUIRE(tripwire != nullptr);
    CHECK(tripwire->damage == doctest::Approx(0.0f));
    CHECK(tripwire->noiseRadius == doctest::Approx(18.0f));
    CHECK(tripwire->uses == 8);

    const game::TrapDef* fuel = traps.Find("fuel_trap");
    REQUIRE(fuel != nullptr);
    REQUIRE(fuel->fire.has_value());
    CHECK(fuel->fire->radius == doctest::Approx(1.5f));
    CHECK(fuel->fire->minutes == doctest::Approx(45.0f));
    CHECK(fuel->uses == 1);
}

TEST_CASE("M34-F3-T1 traps-v1 mod 追加入口、非法条目和重复 id") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    core::Config config;
    REQUIRE(config.LoadFile(root / "config" / "game.json"));
    const std::vector<game::BuildableDef> buildables =
        game::LoadBuildableDefs(config);

    game::TrapDefsConfig traps;
    traps.enabled = true;
    std::string error;
    REQUIRE_MESSAGE(
        game::LoadTrapDefsConfig(config, buildables, &traps, &error), error);
    const std::size_t baseCount = traps.defs.size();

    game::TrapMergeStats stats;
    REQUIRE_MESSAGE(game::MergeTrapJson(kModTraps, "mod_traps", &traps, &stats,
                                        buildables, &error),
                    error);
    CHECK(traps.defs.size() == baseCount + 1u);
    CHECK(stats.defsAdded == 1);
    CHECK(stats.defsSkipped == 3);

    const game::TrapDef* added = traps.Find("mod_snare");
    REQUIRE(added != nullptr);
    CHECK(added->damage == doctest::Approx(9.0f));
    CHECK(added->triggerMask == game::kTrapTriggerZombie);
    CHECK(traps.Find("bad_damage") == nullptr);
    CHECK(traps.Find("bad_building") == nullptr);
    CHECK(traps.Find("bear_trap")->damage == doctest::Approx(55.0f));
}

TEST_CASE("M34-F3-T1 PlacedTrap 存档往返与旧实体回退") {
    const save::ComponentRegistry components = MakeSaveRegistry();
    CHECK(components.Size() == 53u);

    ecs::EcsWorld source;
    const ecs::Entity trapEntity = source.CreateWithStableId(9101);
    source.Registry().emplace<ecs::Position>(
        trapEntity, ecs::Position{12.5f, 8.5f, 0});
    source.Registry().emplace<ecs::PlacedTrap>(
        trapEntity,
        ecs::PlacedTrap{"bear_trap", true, 1.25f, 3, 0x123456789ull});
    const ecs::Entity plainEntity = source.CreateWithStableId(9102);
    source.Registry().emplace<ecs::Position>(
        plainEntity, ecs::Position{13.5f, 8.5f, 0});

    std::string error;
    save::EntitySnapshot snapshot;
    REQUIRE(save::CaptureEntities(source, components, &snapshot, {}, &error));
    REQUIRE(snapshot.entities.size() == 2u);
    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(snapshot, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));

    ecs::EcsWorld target;
    for (const save::SavedEntity& saved : decoded.entities) {
        const ecs::Entity entity = target.CreateWithStableId(saved.stableId);
        REQUIRE(entity != ecs::EcsWorld::kNull);
        REQUIRE(save::ApplyEntity(saved, target, entity, components, &error));
    }
    const ecs::Entity loadedTrap = target.FindById(9101);
    REQUIRE(loadedTrap != ecs::EcsWorld::kNull);
    const auto* trap = target.Registry().try_get<ecs::PlacedTrap>(loadedTrap);
    REQUIRE(trap != nullptr);
    CHECK(trap->defId == "bear_trap");
    CHECK(trap->armed);
    CHECK(trap->cooldown == doctest::Approx(1.25f));
    CHECK(trap->uses == 3);
    CHECK(trap->owner == 0x123456789ull);

    const ecs::Entity loadedPlain = target.FindById(9102);
    REQUIRE(loadedPlain != ecs::EcsWorld::kNull);
    CHECK(target.Registry().try_get<ecs::PlacedTrap>(loadedPlain) == nullptr);
}

TEST_CASE("M34-F3-T2 放置陷阱实体、自动布防与手动切换") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    core::Config config;
    REQUIRE(config.LoadFile(root / "config" / "game.json"));
    const std::vector<game::BuildableDef> buildables =
        game::LoadBuildableDefs(config);

    game::TrapDefsConfig traps;
    std::string error;
    REQUIRE_MESSAGE(
        game::LoadTrapDefsConfig(config, buildables, &traps, &error), error);
    const game::TrapDef* bear = traps.FindByBuilding("bear_trap");
    REQUIRE(bear != nullptr);
    CHECK(traps.FindByBuilding("spike_trap") == nullptr);
    CHECK(traps.FindByBuilding("missing") == nullptr);

    const auto buildable = std::find_if(
        buildables.begin(), buildables.end(), [](const game::BuildableDef& def) {
            return def.id == "bear_trap";
        });
    REQUIRE(buildable != buildables.end());

    ecs::EcsWorld world;
    const ecs::Entity entity =
        game::SpawnPlacedTrap(world, 12.5f, 8.5f, 0, *bear, *buildable,
                              0x123456789ull);
    REQUIRE(entity != ecs::EcsWorld::kNull);
    auto& registry = world.Registry();
    REQUIRE(registry.all_of<ecs::Position>(entity));
    REQUIRE(registry.all_of<ecs::SpriteRef>(entity));
    REQUIRE(registry.all_of<ecs::PlacedTrap>(entity));
    REQUIRE(registry.all_of<ecs::StructureOwner>(entity));
    CHECK(registry.all_of<ecs::NoCombatTarget>(entity));

    const auto& position = registry.get<ecs::Position>(entity);
    CHECK(position.x == doctest::Approx(12.5f));
    CHECK(position.y == doctest::Approx(8.5f));
    CHECK(position.z == 0);
    CHECK(registry.get<ecs::SpriteRef>(entity).sprite ==
          ecs::HashName("object.trap"));
    CHECK(registry.get<ecs::StructureOwner>(entity).ownerStableId ==
          0x123456789ull);

    auto& placed = registry.get<ecs::PlacedTrap>(entity);
    CHECK(placed.defId == "bear_trap");
    CHECK_FALSE(placed.armed);
    CHECK(placed.armRemaining == doctest::Approx(1.0f));
    CHECK(placed.cooldown == doctest::Approx(0.0f));
    CHECK(placed.uses == 4);
    CHECK(placed.owner == 0x123456789ull);
    CHECK(game::TrapCanToggle(placed));

    CHECK_FALSE(game::AdvancePlacedTrapArming(placed, 0.4f));
    CHECK_FALSE(placed.armed);
    CHECK(placed.armRemaining == doctest::Approx(0.6f));
    CHECK_FALSE(game::AdvancePlacedTrapArming(placed, 0.4f));
    CHECK_FALSE(placed.armed);
    CHECK(game::AdvancePlacedTrapArming(placed, 0.2f));
    CHECK(placed.armed);
    CHECK(placed.armRemaining == doctest::Approx(0.0f));
    CHECK(placed.uses == 4);

    CHECK(game::SetPlacedTrapArmed(placed, false));
    CHECK_FALSE(placed.armed);
    CHECK(placed.armRemaining == doctest::Approx(0.0f));
    CHECK_FALSE(game::AdvancePlacedTrapArming(placed, 5.0f));
    CHECK_FALSE(placed.armed);
    CHECK(game::SetPlacedTrapArmed(placed, true));
    CHECK(placed.armed);
    CHECK(placed.uses == 4);

    placed.uses = 0;
    CHECK_FALSE(game::TrapCanToggle(placed));
    CHECK_FALSE(game::SetPlacedTrapArmed(placed, true));
    CHECK(game::SetPlacedTrapArmed(placed, false));
    CHECK_FALSE(placed.armed);
    CHECK_FALSE(game::AdvancePlacedTrapArming(placed, 1.0f));
}

TEST_CASE("M34-F3-T3 陷阱目标掩码、最近目标与友军排除") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    core::Config config;
    REQUIRE(config.LoadFile(root / "config" / "game.json"));
    const std::vector<game::BuildableDef> buildables =
        game::LoadBuildableDefs(config);
    game::TrapDefsConfig traps;
    std::string error;
    REQUIRE_MESSAGE(
        game::LoadTrapDefsConfig(config, buildables, &traps, &error), error);
    const game::TrapDef* bear = traps.Find("bear_trap");
    REQUIRE(bear != nullptr);

    ecs::EcsWorld world;
    entt::registry& registry = world.Registry();
    const auto addTarget = [&](float x, float y, int z,
                               ecs::Health health) {
        const ecs::Entity entity = world.Create();
        registry.emplace<ecs::Position>(entity, ecs::Position{x, y, z});
        registry.emplace<ecs::Health>(entity, health);
        return entity;
    };
    const ecs::Entity zombie = addTarget(20.6f, 20.5f, 0, {100.0f, 100.0f});
    registry.emplace<ecs::AiState>(zombie, ecs::AiState{});
    registry.emplace<ecs::ZombieVariant>(zombie, ecs::ZombieVariant{});
    const ecs::Entity animal = addTarget(20.5f, 21.2f, 0, {30.0f, 30.0f});
    registry.emplace<ecs::Animal>(
        animal, ecs::Animal{"deer", ecs::AnimalKind::Passive});
    const ecs::Entity infected =
        addTarget(21.4f, 20.5f, 0, {100.0f, 100.0f});
    registry.emplace<game::SettlementRaidZombie>(
        infected,
        game::SettlementRaidZombie{"settlement", game::ContestedFaction::Infected,
                                   1, 0.0f, 0.0f, 0.0f, 0.0f, false});

    const ecs::Entity friendly =
        addTarget(20.5f, 20.6f, 0, {100.0f, 100.0f});
    registry.emplace<ecs::FriendlyNpc>(friendly);
    const ecs::Entity player = addTarget(20.4f, 20.5f, 0, {100.0f, 100.0f});
    registry.emplace<ecs::PlayerTag>(player);
    const ecs::Entity dead = addTarget(20.5f, 20.5f, 0, {0.0f, 100.0f});
    registry.emplace<ecs::ZombieVariant>(dead, ecs::ZombieVariant{});
    const ecs::Entity dying = addTarget(20.5f, 20.5f, 0, {100.0f, 100.0f});
    registry.emplace<ecs::ZombieVariant>(dying, ecs::ZombieVariant{});
    registry.emplace<ecs::Dying>(dying, ecs::Dying{1.0f});
    const ecs::Entity upper = addTarget(20.5f, 20.5f, 1, {100.0f, 100.0f});
    registry.emplace<ecs::ZombieVariant>(upper, ecs::ZombieVariant{});
    const ecs::Entity raider = addTarget(20.5f, 20.5f, 0, {100.0f, 100.0f});
    registry.emplace<game::SettlementRaidZombie>(
        raider,
        game::SettlementRaidZombie{"settlement", game::ContestedFaction::Raiders,
                                   1, 0.0f, 0.0f, 0.0f, 0.0f, false});

    const std::vector<game::TrapTargetCandidate> targets =
        game::CollectTrapTargets(world);
    REQUIRE(targets.size() == 4u);
    const ecs::Position trapPosition{20.5f, 20.5f, 0};

    game::TrapTargetKind kind = game::TrapTargetKind::None;
    CHECK(game::FindTrapTarget(targets, trapPosition, *bear, &kind) == zombie);
    CHECK(kind == game::TrapTargetKind::Zombie);

    game::TrapDef animalOnly = *bear;
    animalOnly.triggerMask = game::kTrapTriggerAnimal;
    CHECK(game::FindTrapTarget(targets, trapPosition, animalOnly, &kind) ==
          animal);
    CHECK(kind == game::TrapTargetKind::Animal);

    game::TrapDef zombieOnly = *bear;
    zombieOnly.triggerMask = game::kTrapTriggerZombie;
    CHECK(game::FindTrapTarget(targets, trapPosition, zombieOnly, &kind) ==
          zombie);

    game::TrapDef noMask = *bear;
    noMask.triggerMask = 0;
    CHECK(game::FindTrapTarget(targets, trapPosition, noMask) ==
          ecs::EcsWorld::kNull);

    game::TrapDef tooSmall = *bear;
    tooSmall.radius = 0.01f;
    CHECK(game::FindTrapTarget(targets, trapPosition, tooSmall) ==
          ecs::EcsWorld::kNull);

    const ecs::Position upperPosition{20.5f, 20.5f, 1};
    CHECK(game::FindTrapTarget(targets, upperPosition, *bear, &kind) == upper);
    CHECK(kind == game::TrapTargetKind::Zombie);
}

TEST_CASE("M34-F3-T3 陷阱布防、冷却与次数消耗") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    core::Config config;
    REQUIRE(config.LoadFile(root / "config" / "game.json"));
    const std::vector<game::BuildableDef> buildables =
        game::LoadBuildableDefs(config);
    game::TrapDefsConfig traps;
    std::string error;
    REQUIRE_MESSAGE(
        game::LoadTrapDefsConfig(config, buildables, &traps, &error), error);
    const game::TrapDef* bear = traps.Find("bear_trap");
    REQUIRE(bear != nullptr);
    const game::TrapDef* fuel = traps.Find("fuel_trap");
    REQUIRE(fuel != nullptr);

    ecs::PlacedTrap placed;
    placed.defId = bear->id;
    placed.armed = true;
    placed.cooldown = 0.0f;
    placed.uses = bear->uses;
    placed.owner = 42;
    placed.armRemaining = 0.0f;
    CHECK(game::TrapCanActivate(placed, *bear));

    placed.armed = false;
    CHECK_FALSE(game::TrapCanActivate(placed, *bear));
    placed.armed = true;
    placed.armRemaining = 0.2f;
    CHECK_FALSE(game::TrapCanActivate(placed, *bear));
    placed.armRemaining = 0.0f;
    placed.cooldown = 0.5f;
    CHECK_FALSE(game::TrapCanActivate(placed, *bear));
    CHECK_FALSE(game::AdvancePlacedTrapCooldown(placed, 0.4f));
    CHECK(game::AdvancePlacedTrapCooldown(placed, 0.1f));
    CHECK(game::TrapCanActivate(placed, *bear));

    CHECK(game::ConsumePlacedTrapUse(placed, *bear));
    CHECK(placed.uses == 3);
    CHECK(placed.cooldown == doctest::Approx(2.0f));
    CHECK(placed.armed);
    CHECK_FALSE(game::TrapCanActivate(placed, *bear));
    CHECK_FALSE(game::AdvancePlacedTrapCooldown(placed, 1.9f));
    CHECK(game::AdvancePlacedTrapCooldown(placed, 0.1f));
    CHECK(game::TrapCanActivate(placed, *bear));

    CHECK(game::ConsumePlacedTrapUse(placed, *bear));
    CHECK(game::ConsumePlacedTrapUse(placed, *bear));
    CHECK(game::ConsumePlacedTrapUse(placed, *bear));
    CHECK(placed.uses == 0);
    CHECK_FALSE(placed.armed);
    CHECK_FALSE(game::TrapCanActivate(placed, *bear));
    CHECK_FALSE(game::ConsumePlacedTrapUse(placed, *bear));

    CHECK(game::TrapFireDurationSeconds(
              fuel->fire->minutes, 0.4f) == doctest::Approx(112.5f));
    CHECK(game::TrapFireAreaCells(10.5f, 8.5f, fuel->fire->radius).size() ==
          9u);
}

TEST_CASE("M34-F3-T3 燃油陷阱强制点燃无可燃物格") {
    game::FireRegistry registry;
    game::FireSystem fire(registry, game::FireParams{}, 71u);
    world::World world;
    world.GetOrCreateSquare(30, 30, 0);
    const world::ChunkCoord coord = world::ChunkOf(30, 30);
    world::Chunk* chunk = game::FindChunkInWorld(world, coord.cx, coord.cy);
    REQUIRE(chunk != nullptr);
    chunk->SetState(world::ChunkState::Loading);
    chunk->SetState(world::ChunkState::Loaded);

    ecs::EcsWorld ecs;
    REQUIRE(fire.IgniteForcedAt(ecs, world, 30, 30, 0, 112.5f, 1.5f, 42));
    const ecs::FireSource* source = fire.FindAt(ecs, 30, 30, 0);
    REQUIRE(source != nullptr);
    CHECK(source->materialId == "trap_fuel");
    CHECK(source->phase == ecs::FirePhase::Burning);
    CHECK(source->ownerStableId == 42u);
    CHECK(source->burnDuration == doctest::Approx(112.5f));
    CHECK(source->burnRemaining == doctest::Approx(112.5f));
    CHECK(source->heatRadius == doctest::Approx(1.5f));
    CHECK(fire.HasActiveFireNear(ecs, 30.5f, 30.5f, 0));
}
