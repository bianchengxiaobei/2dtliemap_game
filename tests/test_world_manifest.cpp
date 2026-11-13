#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "characters/components/AnimalComponents.h"
#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/SettlementNpc.h"
#include "game/VehicleComponents.h"
#include "items/ItemComponents.h"
#include "save/ChunkDiff.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"
#include "save/RegionEntityBuckets.h"
#include "save/SaveManager.h"
#include "save/WorldManifest.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"
#include "world/WorldDefinition.h"

using namespace zl;

namespace {

struct TempRoot {
    std::filesystem::path path;

    explicit TempRoot(const std::string& name)
        : path(std::filesystem::temp_directory_path() /
               (name + "_" + std::to_string(static_cast<std::uint64_t>(
                                   std::chrono::steady_clock::now()
                                       .time_since_epoch()
                                       .count())) +
                "_" + std::to_string(s_counter.fetch_add(1)))) {
        std::filesystem::remove_all(path);
    }

    ~TempRoot() { std::filesystem::remove_all(path); }

    inline static std::atomic<std::uint64_t> s_counter{0};
};

save::ComponentRegistry MakeRegistry() {
    save::ComponentRegistry registry;
    std::string error;
    REQUIRE(save::RegisterBuiltinComponents(registry, &error));
    CHECK(error.empty());
    return registry;
}

world::WorldDefinition MakeDefinition() {
    world::WorldDefinition definition;
    definition.format = world::kWorldFormat;
    definition.mapId = "test_world";
    definition.seed = 20261110;
    definition.generatorVersion = 7;
    definition.mapHash = 0x1122334455667788ull;
    definition.bounds = world::WorldBounds{0, 0, 4, 4};
    definition.regions.push_back(
        world::WorldRegion{"core", world::WorldRect{0, 0, 2, 2}, "village", {}});
    definition.regions.push_back(
        world::WorldRegion{"wilds", world::WorldRect{2, 0, 2, 2}, "forest", {}});
    return definition;
}

save::WorldManifest MakeManifest(const world::WorldDefinition& definition) {
    save::WorldManifest manifest;
    manifest.identity = world::MakeWorldIdentity(definition);
    manifest.worldSaveVersion = save::kWorldSaveVersion;
    manifest.chunks.push_back(save::ManifestChunk{0, 0, true});
    manifest.chunks.push_back(save::ManifestChunk{1, 0, true});
    manifest.regions.push_back(save::ManifestRegion{"core", 2});
    manifest.regions.push_back(save::ManifestRegion{"wilds", 1});
    return manifest;
}

save::ChunkDiff MakeDirtDiff() {
    world::Chunk source(0, 0);
    source.Square(2, 2, 0).SetFloor({"tile.floor.wood", 0, 0});
    source.CaptureBaseline();
    source.Square(2, 2, 0).SetFloor({"tile.floor.dirt", 0, 0});
    save::ChunkDiff diff;
    std::string error;
    REQUIRE(save::CaptureChunkDiff(source, &diff, &error));
    CHECK(error.empty());
    return diff;
}

void WriteBytes(const std::filesystem::path& path,
                const std::vector<std::uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    REQUIRE(file.is_open());
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    REQUIRE(static_cast<bool>(file));
}

const save::SavedEntity* FindEntity(const save::EntitySnapshot& snapshot,
                                    std::uint64_t stableId) {
    for (const save::SavedEntity& entity : snapshot.entities) {
        if (entity.stableId == stableId) {
            return &entity;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("M31-T4 world_manifest 编解码、桶文件命名与输入校验") {
    const world::WorldDefinition definition = MakeDefinition();
    save::WorldManifest manifest = MakeManifest(definition);
    manifest.buckets.push_back(
        save::ManifestEntityBucket{"core", save::EntityBucketFile("core"), 3});
    manifest.buckets.push_back(save::ManifestEntityBucket{
        "wilds", save::EntityBucketFile("wilds"), 2});

    std::string error;
    const std::vector<std::uint8_t> bytes =
        save::EncodeWorldManifest(manifest, &error);
    REQUIRE_FALSE(bytes.empty());
    CHECK(error.empty());

    save::WorldManifest decoded;
    REQUIRE(save::DecodeWorldManifest(bytes, &decoded, &error));
    CHECK(error.empty());
    CHECK(decoded.identity.mapId == definition.mapId);
    CHECK(decoded.identity.seed == definition.seed);
    CHECK(decoded.identity.generatorVersion ==
          definition.generatorVersion);
    CHECK(decoded.identity.mapHash == definition.mapHash);
    REQUIRE(decoded.chunks.size() == 2u);
    CHECK(decoded.chunks[0].chunkX == 0);
    CHECK(decoded.chunks[1].chunkY == 0);
    REQUIRE(decoded.regions.size() == 2u);
    CHECK(decoded.regions[0].id == "core");
    CHECK(decoded.regions[0].phase == 2);
    REQUIRE(decoded.buckets.size() == 2u);
    CHECK(decoded.buckets[0].file == save::EntityBucketFile("core"));
    CHECK(save::IsSafeEntityBucketFile(decoded.buckets[0].file));
    CHECK_FALSE(save::IsSafeEntityBucketFile("../escape.bin"));
    CHECK_FALSE(save::IsSafeEntityBucketFile("entities_core.txt"));

    save::WorldManifest invalid = manifest;
    invalid.buckets[0].file = "../escape.bin";
    CHECK_FALSE(save::ValidateWorldManifest(invalid, &error));
    CHECK_FALSE(error.empty());

    save::WorldManifest duplicateChunk = manifest;
    duplicateChunk.chunks.push_back(duplicateChunk.chunks[0]);
    CHECK_FALSE(save::ValidateWorldManifest(duplicateChunk, &error));

    save::WorldManifest empty;
    CHECK_FALSE(save::DecodeWorldManifest({}, &empty, &error));
}

TEST_CASE("M31-T4 区域实体桶按区域拆分并保留全局玩家") {
    const save::ComponentRegistry components = MakeRegistry();
    const world::WorldDefinition definition = MakeDefinition();
    ecs::EcsWorld world;

    const ecs::Entity player = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::PlayerTag>(player);
    registry.emplace<ecs::Position>(player, ecs::Position{5.5f, 5.5f, 0});
    registry.emplace<ecs::Health>(player, ecs::Health{80.0f, 100.0f});
    const std::uint64_t playerId = world.IdOf(player).id;

    const ecs::Entity vehicle = world.Create();
    registry.emplace<ecs::Position>(vehicle, ecs::Position{15.5f, 15.5f, 0});
    registry.emplace<ecs::Vehicle>(vehicle, ecs::Vehicle{0.4f, 0.6f});
    const std::uint64_t vehicleId = world.IdOf(vehicle).id;

    const ecs::Entity corpse = world.Create();
    registry.emplace<ecs::Position>(corpse, ecs::Position{16.5f, 16.5f, 0});
    registry.emplace<ecs::Corpse>(corpse, ecs::Corpse{playerId});
    const std::uint64_t corpseId = world.IdOf(corpse).id;

    const ecs::Entity animal = world.Create();
    registry.emplace<ecs::Position>(animal, ecs::Position{25.5f, 6.5f, 0});
    registry.emplace<ecs::Animal>(animal,
                                  ecs::Animal{"deer", ecs::AnimalKind::Passive});
    const std::uint64_t animalId = world.IdOf(animal).id;

    const ecs::Entity item = world.Create();
    registry.emplace<ecs::Position>(item, ecs::Position{26.5f, 7.5f, 0});
    registry.emplace<ecs::ItemInstance>(item,
                                        ecs::ItemInstance{"axe", 1, 0.8f});
    const std::uint64_t itemId = world.IdOf(item).id;

    save::EntityBucketSet buckets;
    std::string error;
    REQUIRE(save::CaptureRegionEntityBuckets(world, components, definition,
                                              &buckets, {}, &error));
    CHECK(error.empty());
    REQUIRE(buckets.global.entities.size() == 1u);
    CHECK(buckets.global.entities[0].stableId == playerId);
    REQUIRE(buckets.buckets.size() == 2u);
    CHECK(buckets.buckets[0].id == "core");
    CHECK(buckets.buckets[0].entities.entities.size() == 2u);
    CHECK(buckets.buckets[1].id == "wilds");
    CHECK(buckets.buckets[1].entities.entities.size() == 2u);
    CHECK(FindEntity(buckets.buckets[0].entities, vehicleId) != nullptr);
    CHECK(FindEntity(buckets.buckets[0].entities, corpseId) != nullptr);
    CHECK(FindEntity(buckets.buckets[1].entities, animalId) != nullptr);
    CHECK(FindEntity(buckets.buckets[1].entities, itemId) != nullptr);
    CHECK(FindEntity(buckets.global, vehicleId) == nullptr);
}

TEST_CASE("M31-T4 SaveManager manifest/桶/墙体跨卸载重载") {
    TempRoot root("zl_m31_t4_roundtrip");
    const save::ComponentRegistry components = MakeRegistry();
    const world::WorldDefinition definition = MakeDefinition();
    ecs::EcsWorld world;
    entt::registry& registry = world.Registry();

    const ecs::Entity player = world.Create();
    registry.emplace<ecs::PlayerTag>(player);
    registry.emplace<ecs::Position>(player, ecs::Position{5.5f, 5.5f, 0});
    const std::uint64_t playerId = world.IdOf(player).id;

    const ecs::Entity vehicle = world.Create();
    registry.emplace<ecs::Position>(vehicle, ecs::Position{15.5f, 15.5f, 0});
    registry.emplace<ecs::Vehicle>(vehicle, ecs::Vehicle{0.35f, 0.72f});
    const std::uint64_t vehicleId = world.IdOf(vehicle).id;

    const ecs::Entity corpse = world.Create();
    registry.emplace<ecs::Position>(corpse, ecs::Position{16.5f, 16.5f, 0});
    registry.emplace<ecs::Corpse>(corpse, ecs::Corpse{playerId});
    const std::uint64_t corpseId = world.IdOf(corpse).id;

    const ecs::Entity item = world.Create();
    registry.emplace<ecs::Position>(item, ecs::Position{26.5f, 7.5f, 0});
    registry.emplace<ecs::ItemInstance>(item,
                                        ecs::ItemInstance{"axe", 2, 0.5f});
    const std::uint64_t itemId = world.IdOf(item).id;

    const ecs::Entity animal = world.Create();
    registry.emplace<ecs::Position>(animal, ecs::Position{27.5f, 8.5f, 0});
    registry.emplace<ecs::Animal>(animal,
                                  ecs::Animal{"boar", ecs::AnimalKind::Passive});
    const std::uint64_t animalId = world.IdOf(animal).id;

    const ecs::Entity settlement = world.Create();
    game::SettlementState settlementState;
    settlementState.settlements.push_back(
        game::SettlementStateEntry{"village_haven", false, 42.0f, 3});
    registry.emplace<game::SettlementState>(settlement,
                                            std::move(settlementState));
    const std::uint64_t settlementId = world.IdOf(settlement).id;

    save::EntityBucketSet buckets;
    std::string error;
    REQUIRE(save::CaptureRegionEntityBuckets(world, components, definition,
                                              &buckets, {}, &error));
    save::WorldManifest manifest = MakeManifest(definition);
    const save::ChunkDiff dirt = MakeDirtDiff();

    save::SaveManager manager({root.path, "m31-test"});
    REQUIRE(manager.EnsureRoot());
    save::SaveManager::CreateOptions options;
    options.name = "M31 T4";
    options.mapId = definition.mapId;
    options.gameVersion = "m31-test";
    options.entities = &buckets.global;
    options.manifest = &manifest;
    options.buckets = &buckets.buckets;
    options.chunks.push_back(dirt);
    options.players.push_back(save::SavePlayer{playerId, "Alice", true});
    std::string id;
    REQUIRE(manager.Create(options, &id, &error));
    CHECK(error.empty());

    CHECK(std::filesystem::exists(manager.WorldManifestPath(id)));
    CHECK(std::filesystem::exists(manager.WorldDir(id) /
                                  save::EntityBucketFile("core")));
    CHECK(std::filesystem::exists(manager.WorldDir(id) /
                                  save::EntityBucketFile("wilds")));

    save::WorldManifest loadedManifest;
    CHECK(manager.ReadWorldManifest(id, &loadedManifest, &error) ==
          save::SaveManager::LoadResult::Ok);
    CHECK(loadedManifest.identity.mapHash == definition.mapHash);
    REQUIRE(loadedManifest.chunks.size() == 1u);
    CHECK(loadedManifest.chunks[0].chunkX == 0);

    save::EntitySnapshot loaded;
    save::SaveManager::LoadReport report;
    REQUIRE(manager.Load(id, components, &loaded, nullptr, &error, &report) ==
            save::SaveManager::LoadResult::Ok);
    CHECK(error.empty());
    CHECK(report.usedWorldManifest);
    CHECK(report.entityBuckets == 2u);
    CHECK(loaded.entities.size() == 6u);
    CHECK(FindEntity(loaded, vehicleId) != nullptr);
    CHECK(FindEntity(loaded, corpseId) != nullptr);
    CHECK(FindEntity(loaded, itemId) != nullptr);
    CHECK(FindEntity(loaded, animalId) != nullptr);
    CHECK(FindEntity(loaded, settlementId) != nullptr);
    CHECK(FindEntity(loaded, playerId) != nullptr);

    ecs::EcsWorld restored;
    std::vector<ecs::Entity> restoredEntities;
    REQUIRE(save::LoadEntities(loaded, restored, components, &restoredEntities,
                               &error));
    entt::registry& restoredRegistry = restored.Registry();
    const ecs::Entity restoredVehicle = restored.FindById(vehicleId);
    const ecs::Entity restoredCorpse = restored.FindById(corpseId);
    const ecs::Entity restoredItem = restored.FindById(itemId);
    const ecs::Entity restoredAnimal = restored.FindById(animalId);
    const ecs::Entity restoredSettlement = restored.FindById(settlementId);
    REQUIRE(restoredVehicle != ecs::EcsWorld::kNull);
    REQUIRE(restoredCorpse != ecs::EcsWorld::kNull);
    REQUIRE(restoredItem != ecs::EcsWorld::kNull);
    REQUIRE(restoredAnimal != ecs::EcsWorld::kNull);
    REQUIRE(restoredSettlement != ecs::EcsWorld::kNull);
    const auto* vehicleState = restoredRegistry.try_get<ecs::Vehicle>(restoredVehicle);
    REQUIRE(vehicleState != nullptr);
    CHECK(vehicleState->condition == doctest::Approx(0.35f));
    CHECK(vehicleState->fuel == doctest::Approx(0.72f));
    const auto* corpseState = restoredRegistry.try_get<ecs::Corpse>(restoredCorpse);
    REQUIRE(corpseState != nullptr);
    CHECK(corpseState->sourceId == playerId);
    const auto* itemState =
        restoredRegistry.try_get<ecs::ItemInstance>(restoredItem);
    REQUIRE(itemState != nullptr);
    CHECK(itemState->defId == "axe");
    CHECK(itemState->count == 2);
    const auto* animalState =
        restoredRegistry.try_get<ecs::Animal>(restoredAnimal);
    REQUIRE(animalState != nullptr);
    CHECK(animalState->speciesId == "boar");
    const auto* restoredSettlementState =
        restoredRegistry.try_get<game::SettlementState>(restoredSettlement);
    REQUIRE(restoredSettlementState != nullptr);
    REQUIRE(restoredSettlementState->settlements.size() == 1u);
    CHECK(restoredSettlementState->settlements[0].id == "village_haven");
    CHECK(restoredSettlementState->settlements[0].guardHealth ==
          doctest::Approx(42.0f));

    save::SaveManager restarted({root.path, "m31-test"});
    save::EntitySnapshot restartedEntities;
    save::SaveManager::LoadReport restartedReport;
    REQUIRE(restarted.Load(id, components, &restartedEntities, nullptr, &error,
                           &restartedReport) ==
            save::SaveManager::LoadResult::Ok);
    CHECK(restartedReport.usedWorldManifest);
    CHECK(restartedEntities.entities.size() == 6u);

    world::Chunk target(0, 0);
    target.Square(2, 2, 0).SetFloor({"tile.floor.wood", 0, 0});
    target.CaptureBaseline();
    REQUIRE(manager.LoadChunk(id, target, &error) ==
            save::SaveManager::LoadResult::Ok);
    REQUIRE(target.Square(2, 2, 0).Floor() != nullptr);
    CHECK(target.Square(2, 2, 0).Floor()->sprite == "tile.floor.dirt");

    const std::vector<std::uint8_t> broken{'b', 'a', 'd'};
    WriteBytes(manager.WorldDir(id) / save::EntityBucketFile("core"), broken);
    CHECK(manager.Load(id, components, &loaded, nullptr, &error) ==
          save::SaveManager::LoadResult::Corrupt);
    CHECK_FALSE(error.empty());
}

TEST_CASE("M31-T4 旧档无 manifest 仍可读且地图身份不匹配可识别") {
    TempRoot root("zl_m31_t4_legacy");
    const save::ComponentRegistry components = MakeRegistry();
    ecs::EcsWorld world;
    const ecs::Entity player = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::PlayerTag>(player);
    registry.emplace<ecs::Position>(player, ecs::Position{5.5f, 5.5f, 0});
    save::EntitySnapshot snapshot;
    std::string error;
    REQUIRE(save::CaptureEntities(world, components, &snapshot, {}, &error));

    save::SaveManager manager({root.path, "legacy-test"});
    REQUIRE(manager.EnsureRoot());
    save::SaveManager::CreateOptions options;
    options.name = "legacy";
    options.mapId = "test_world";
    options.gameVersion = "legacy-test";
    options.entities = &snapshot;
    std::string id;
    REQUIRE(manager.Create(options, &id, &error));

    save::WorldManifest missing;
    CHECK(manager.ReadWorldManifest(id, &missing, &error) ==
          save::SaveManager::LoadResult::NotFound);
    save::EntitySnapshot loaded;
    save::SaveManager::LoadReport report;
    REQUIRE(manager.Load(id, components, &loaded, nullptr, &error, &report) ==
            save::SaveManager::LoadResult::Ok);
    CHECK_FALSE(report.usedWorldManifest);
    CHECK(loaded.entities.size() == 1u);

    const world::WorldDefinition definition = MakeDefinition();
    const world::WorldIdentity identity = world::MakeWorldIdentity(definition);
    CHECK(world::CheckWorldSaveCompatibility(definition, &identity, &error) ==
          world::WorldSaveCompatibility::Compatible);
    world::WorldIdentity changed = identity;
    changed.mapHash ^= 0x1u;
    CHECK(world::CheckWorldSaveCompatibility(definition, &changed, &error) ==
          world::WorldSaveCompatibility::Mismatch);
    CHECK_FALSE(error.empty());
}
