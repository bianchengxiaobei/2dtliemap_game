#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "core/Config.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/BuildingRules.h"
#include "game/FireComponents.h"
#include "game/HomesteadComponents.h"
#include "game/Traps.h"
#include "game/WorldSimulation.h"
#include "save/BinaryWriter.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"
#include "save/SaveManager.h"
#include "save/SaveTypes.h"
#include "server/ServerHost.h"
#include "ServerHostTestAccess.h"

using namespace zl;

namespace {

class ScopedWorkingDirectory {
public:
    explicit ScopedWorkingDirectory(const std::filesystem::path& path)
        : m_old(std::filesystem::current_path()) {
        std::filesystem::current_path(path);
    }
    ~ScopedWorkingDirectory() {
        std::error_code error;
        std::filesystem::current_path(m_old, error);
    }

    ScopedWorkingDirectory(const ScopedWorkingDirectory&) = delete;
    ScopedWorkingDirectory& operator=(const ScopedWorkingDirectory&) = delete;

private:
    std::filesystem::path m_old;
};

save::ComponentRegistry MakeSaveRegistry() {
    save::ComponentRegistry registry;
    std::string error;
    REQUIRE(save::RegisterBuiltinComponents(registry, &error));
    CHECK(error.empty());
    return registry;
}

std::filesystem::path TempDir(const char* name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zomboidlike_f3t4" / name;
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir, error);
    return dir;
}

void CreateEmptySave(const std::filesystem::path& saveRoot,
                     std::string* saveId) {
    save::EntitySnapshot empty;
    save::SaveManager::Options options;
    options.root = saveRoot;
    options.gameVersion = save::kDefaultGameVersion;
    save::SaveManager manager(options);
    save::SaveManager::CreateOptions create;
    create.name = "M34-F3-T4";
    create.mapId = "village_demo";
    create.gameVersion = save::kDefaultGameVersion;
    create.entities = &empty;
    std::string error;
    REQUIRE_MESSAGE(manager.Create(create, saveId, &error), error);
}

server::ServerHost::Options MakeHostOptions(
    const std::filesystem::path& root, const std::filesystem::path& saveRoot,
    const std::string& saveId) {
    server::ServerHost::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.saveRoot = saveRoot.string();
    options.loadSaveId = saveId;
    options.spawnDemoPopulation = false;
    options.spawnLocalPlayer = false;
    options.spawnAnimals = false;
    options.network = false;
    options.port = 0;
    return options;
}

const game::BuildableDef* FindBuildable(
    const std::vector<game::BuildableDef>& buildables, std::string_view id) {
    const auto found =
        std::find_if(buildables.begin(), buildables.end(),
                     [id](const game::BuildableDef& def) {
                         return def.id == id;
                     });
    return found != buildables.end() ? &*found : nullptr;
}

std::vector<std::uint8_t> WriterBytes(const save::BinaryWriter& writer) {
    const auto bytes = writer.Data();
    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
}

}  // namespace

TEST_CASE("M34-F3-T4 TRP2 v2 保存布防剩余时间并兼容 v1") {
    const save::ComponentRegistry components = MakeSaveRegistry();
    const save::ComponentCodec* codec =
        components.Find(save::kSaveComponentPlacedTrap);
    REQUIRE(codec != nullptr);
    CHECK(codec->version == 2);
    CHECK(codec->minimumVersion == 1);

    ecs::EcsWorld source;
    const ecs::Entity trapEntity = source.CreateWithStableId(9301);
    source.Registry().emplace<ecs::Position>(
        trapEntity, ecs::Position{12.5f, 8.5f, 0});
    source.Registry().emplace<ecs::PlacedTrap>(
        trapEntity,
        ecs::PlacedTrap{"tripwire_alarm", false, 0.5f, 5, 0x123456789ull,
                        0.6f});

    std::string error;
    save::EntitySnapshot snapshot;
    REQUIRE_MESSAGE(save::CaptureEntities(source, components, &snapshot, {},
                                          &error),
                    error);
    REQUIRE(snapshot.entities.size() == 1u);
    const save::SavedComponent* savedTrap = nullptr;
    for (const save::SavedComponent& component :
         snapshot.entities[0].components) {
        if (component.type == save::kSaveComponentPlacedTrap) {
            savedTrap = &component;
        }
    }
    REQUIRE(savedTrap != nullptr);
    CHECK(savedTrap->version == 2);

    ecs::EcsWorld target;
    std::vector<ecs::Entity> loaded;
    REQUIRE_MESSAGE(
        save::LoadEntities(snapshot, target, components, &loaded, &error),
        error);
    const ecs::Entity restored = target.FindById(9301);
    REQUIRE(restored != ecs::EcsWorld::kNull);
    auto* placed = target.Registry().try_get<ecs::PlacedTrap>(restored);
    REQUIRE(placed != nullptr);
    CHECK(placed->defId == "tripwire_alarm");
    CHECK_FALSE(placed->armed);
    CHECK(placed->cooldown == doctest::Approx(0.5f));
    CHECK(placed->uses == 5);
    CHECK(placed->owner == 0x123456789ull);
    CHECK(placed->armRemaining == doctest::Approx(0.6f));
    CHECK_FALSE(game::AdvancePlacedTrapArming(*placed, 0.5f));
    CHECK(game::AdvancePlacedTrapArming(*placed, 0.1f));
    CHECK(placed->armed);

    save::BinaryWriter legacyWriter;
    REQUIRE(legacyWriter.BeginSegment(save::kSaveComponentPlacedTrap, 1));
    REQUIRE(legacyWriter.WriteString("bear_trap", 64));
    REQUIRE(legacyWriter.Write(true));
    REQUIRE(legacyWriter.Write(1.25f));
    REQUIRE(legacyWriter.Write(static_cast<std::int32_t>(3)));
    REQUIRE(legacyWriter.Write(0x987654321ull));
    REQUIRE(legacyWriter.EndSegment());

    save::SavedEntity legacy;
    legacy.stableId = 9302;
    save::SavedComponent legacyTrap;
    legacyTrap.type = save::kSaveComponentPlacedTrap;
    legacyTrap.version = 1;
    legacyTrap.bytes = WriterBytes(legacyWriter);
    legacy.components.push_back(std::move(legacyTrap));

    const ecs::Entity legacyEntity = target.CreateWithStableId(9302);
    REQUIRE(legacyEntity != ecs::EcsWorld::kNull);
    REQUIRE_MESSAGE(
        save::ApplyEntity(legacy, target, legacyEntity, components, &error),
        error);
    const auto* legacyLoaded =
        target.Registry().try_get<ecs::PlacedTrap>(legacyEntity);
    REQUIRE(legacyLoaded != nullptr);
    CHECK(legacyLoaded->defId == "bear_trap");
    CHECK(legacyLoaded->armed);
    CHECK(legacyLoaded->cooldown == doctest::Approx(1.25f));
    CHECK(legacyLoaded->uses == 3);
    CHECK(legacyLoaded->owner == 0x987654321ull);
    CHECK(legacyLoaded->armRemaining == doctest::Approx(0.0f));
}

TEST_CASE("M34-F3-T4 单机快照读档恢复陷阱状态") {
    const std::filesystem::path dir = TempDir("single");
    save::ComponentRegistry components = MakeSaveRegistry();
    ecs::EcsWorld source;
    const ecs::Entity trapEntity = source.CreateWithStableId(9311);
    source.Registry().emplace<ecs::Position>(
        trapEntity, ecs::Position{20.5f, 10.5f, 0});
    source.Registry().emplace<ecs::PlacedTrap>(
        trapEntity,
        ecs::PlacedTrap{"fuel_trap", false, 0.75f, 1, 0xCAFEull, 0.6f});

    std::string error;
    save::EntitySnapshot snapshot;
    REQUIRE_MESSAGE(save::CaptureEntities(source, components, &snapshot, {},
                                          &error),
                    error);

    save::SaveManager::Options options;
    options.root = dir / "saves";
    save::SaveManager manager(options);
    save::SaveManager::CreateOptions create;
    create.name = "M34-F3-T4-single";
    create.mapId = "village_demo";
    create.gameVersion = save::kDefaultGameVersion;
    create.entities = &snapshot;
    std::string saveId;
    REQUIRE_MESSAGE(manager.Create(create, &saveId, &error), error);

    auto* live = source.Registry().try_get<ecs::PlacedTrap>(trapEntity);
    REQUIRE(live != nullptr);
    live->armed = true;
    live->cooldown = 9.0f;
    live->uses = 0;
    live->armRemaining = 0.0f;

    save::EntitySnapshot loadedSnapshot;
    save::SaveMetadata metadata;
    REQUIRE_MESSAGE(
        manager.Load(saveId, components, &loadedSnapshot, &metadata, &error) ==
            save::SaveManager::LoadResult::Ok,
        error);
    ecs::EcsWorld target;
    std::vector<ecs::Entity> loaded;
    REQUIRE_MESSAGE(
        save::LoadEntities(loadedSnapshot, target, components, &loaded, &error),
        error);
    const ecs::Entity restored = target.FindById(9311);
    REQUIRE(restored != ecs::EcsWorld::kNull);
    auto* placed = target.Registry().try_get<ecs::PlacedTrap>(restored);
    REQUIRE(placed != nullptr);
    CHECK(placed->defId == "fuel_trap");
    CHECK_FALSE(placed->armed);
    CHECK(placed->cooldown == doctest::Approx(0.75f));
    CHECK(placed->uses == 1);
    CHECK(placed->owner == 0xCAFEull);
    CHECK(placed->armRemaining == doctest::Approx(0.6f));
    CHECK_FALSE(game::AdvancePlacedTrapArming(*placed, 0.5f));
    CHECK(game::AdvancePlacedTrapArming(*placed, 0.1f));
    CHECK(placed->armed);
}

TEST_CASE("M34-F3-T4 旧档无 PlacedTrap 且旧 TRAP v1 不被迁移") {
    save::ComponentRegistry components = MakeSaveRegistry();
    ecs::EcsWorld source;
    const ecs::Entity spikeEntity = source.CreateWithStableId(9321);
    source.Registry().emplace<ecs::Position>(
        spikeEntity, ecs::Position{30.5f, 12.5f, 0});
    ecs::SpikeTrap spike;
    spike.armed = false;
    spike.cooldown = 0.4f;
    spike.uses = 2;
    source.Registry().emplace<ecs::SpikeTrap>(spikeEntity, spike);

    const ecs::Entity fenceEntity = source.CreateWithStableId(9322);
    source.Registry().emplace<ecs::Position>(
        fenceEntity, ecs::Position{31.5f, 12.5f, 0});
    source.Registry().emplace<ecs::SpriteRef>(
        fenceEntity,
        ecs::SpriteRef{ecs::HashName("object.electric_fence"), 0, 0xFFFFFFFFu});

    std::string error;
    save::EntitySnapshot snapshot;
    REQUIRE_MESSAGE(save::CaptureEntities(source, components, &snapshot, {},
                                          &error),
                    error);
    for (const save::SavedEntity& entity : snapshot.entities) {
        for (const save::SavedComponent& component : entity.components) {
            CHECK(component.type != save::kSaveComponentPlacedTrap);
        }
    }

    ecs::EcsWorld target;
    std::vector<ecs::Entity> loaded;
    REQUIRE_MESSAGE(
        save::LoadEntities(snapshot, target, components, &loaded, &error),
        error);
    const ecs::Entity spikeLoaded = target.FindById(9321);
    REQUIRE(spikeLoaded != ecs::EcsWorld::kNull);
    const auto* loadedSpike =
        target.Registry().try_get<ecs::SpikeTrap>(spikeLoaded);
    REQUIRE(loadedSpike != nullptr);
    CHECK(loadedSpike->armed == spike.armed);
    CHECK(loadedSpike->cooldown == doctest::Approx(spike.cooldown));
    CHECK(loadedSpike->uses == spike.uses);
    CHECK(target.Registry().try_get<ecs::PlacedTrap>(spikeLoaded) == nullptr);
    const ecs::Entity fenceLoaded = target.FindById(9322);
    REQUIRE(fenceLoaded != ecs::EcsWorld::kNull);
    CHECK(target.Registry().try_get<ecs::PlacedTrap>(fenceLoaded) == nullptr);
}

TEST_CASE("M34-F3-T4 服务器 SaveWorld/重启恢复陷阱与燃油火焰") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("server");
    const std::filesystem::path saveRoot = dir / "saves";
    std::string saveId;
    CreateEmptySave(saveRoot, &saveId);
    REQUIRE_FALSE(saveId.empty());

    const server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, saveId);
    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);

    core::Config config;
    REQUIRE(config.LoadFile(root / "config" / "game.json"));
    const std::vector<game::BuildableDef> buildables =
        game::LoadBuildableDefs(config);
    const game::BuildableDef* bearBuild =
        FindBuildable(buildables, "bear_trap");
    const game::BuildableDef* tripwireBuild =
        FindBuildable(buildables, "tripwire_alarm");
    const game::BuildableDef* fuelBuild = FindBuildable(buildables, "fuel_trap");
    REQUIRE(bearBuild != nullptr);
    REQUIRE(tripwireBuild != nullptr);
    REQUIRE(fuelBuild != nullptr);

    game::TrapDefsConfig traps;
    REQUIRE_MESSAGE(
        game::LoadTrapDefsConfig(config, buildables, &traps, &error), error);
    const game::TrapDef* bear = traps.Find("bear_trap");
    const game::TrapDef* tripwire = traps.Find("tripwire_alarm");
    const game::TrapDef* fuel = traps.Find("fuel_trap");
    REQUIRE(bear != nullptr);
    REQUIRE(tripwire != nullptr);
    REQUIRE(fuel != nullptr);

    ecs::EcsWorld& ecs = host.Simulation().Ecs();
    entt::registry& registry = ecs.Registry();
    const std::uint64_t owner = 0x5151ull;
    const ecs::Entity bearEntity =
        game::SpawnPlacedTrap(ecs, 70.5f, 24.5f, 0, *bear, *bearBuild, owner);
    REQUIRE(bearEntity != ecs::EcsWorld::kNull);
    auto& bearState = registry.get<ecs::PlacedTrap>(bearEntity);
    bearState.armed = true;
    bearState.cooldown = 1.25f;
    bearState.uses = 3;
    bearState.armRemaining = 0.0f;

    const ecs::Entity tripwireEntity = game::SpawnPlacedTrap(
        ecs, 71.5f, 24.5f, 0, *tripwire, *tripwireBuild, owner);
    REQUIRE(tripwireEntity != ecs::EcsWorld::kNull);
    auto& tripwireState = registry.get<ecs::PlacedTrap>(tripwireEntity);
    tripwireState.armed = false;
    tripwireState.cooldown = 0.0f;
    tripwireState.uses = 5;
    tripwireState.armRemaining = 0.6f;

    const ecs::Entity fuelEntity = game::SpawnPlacedTrap(
        ecs, 72.5f, 24.5f, 0, *fuel, *fuelBuild, owner);
    REQUIRE(fuelEntity != ecs::EcsWorld::kNull);
    auto& fuelState = registry.get<ecs::PlacedTrap>(fuelEntity);
    fuelState.armed = true;
    fuelState.cooldown = 0.4f;
    fuelState.uses = 1;
    fuelState.armRemaining = 0.0f;

    const ecs::Entity fireEntity = ecs.Create();
    REQUIRE(fireEntity != ecs::EcsWorld::kNull);
    registry.emplace<ecs::Position>(fireEntity,
                                    ecs::Position{72.5f, 24.5f, 0});
    registry.emplace<ecs::FireAnchor>(fireEntity, ecs::FireAnchor{72, 24, 0});
    ecs::FireSource fire;
    fire.materialId = "trap_fuel";
    fire.ownerStableId = owner;
    fire.phase = ecs::FirePhase::Burning;
    fire.burnDuration = 112.5f;
    fire.burnRemaining = 112.5f;
    fire.smokePerSecond = 2.0f;
    fire.heatRadius = 1.5f;
    registry.emplace<ecs::FireSource>(fireEntity, fire);

    const std::uint64_t bearId = ecs.IdOf(bearEntity).id;
    const std::uint64_t tripwireId = ecs.IdOf(tripwireEntity).id;
    const std::uint64_t fuelId = ecs.IdOf(fuelEntity).id;
    const std::uint64_t fireId = ecs.IdOf(fireEntity).id;
    REQUIRE(bearId != 0);
    REQUIRE(tripwireId != 0);
    REQUIRE(fuelId != 0);
    REQUIRE(fireId != 0);

    REQUIRE_MESSAGE(server::ServerHostTestAccess::SaveWorld(host, "f3t4"),
                    "SaveWorld 失败");
    server::ServerHostTestAccess::SetSaveWorldOnExit(host, false);
    host.Shutdown();

    server::ServerHost restarted;
    REQUIRE_MESSAGE(restarted.Init(options, &error), error);
    server::ServerHostTestAccess::SetSaveWorldOnExit(restarted, false);
    ecs::EcsWorld& restoredEcs = restarted.Simulation().Ecs();
    entt::registry& restoredRegistry = restoredEcs.Registry();

    const ecs::Entity restoredBear = restoredEcs.FindById(bearId);
    REQUIRE(restoredBear != ecs::EcsWorld::kNull);
    const auto* restoredBearState =
        restoredRegistry.try_get<ecs::PlacedTrap>(restoredBear);
    REQUIRE(restoredBearState != nullptr);
    CHECK(restoredBearState->defId == "bear_trap");
    CHECK(restoredBearState->armed);
    CHECK(restoredBearState->cooldown == doctest::Approx(1.25f));
    CHECK(restoredBearState->uses == 3);
    CHECK(restoredBearState->owner == owner);
    CHECK(restoredBearState->armRemaining == doctest::Approx(0.0f));

    const ecs::Entity restoredTripwire = restoredEcs.FindById(tripwireId);
    REQUIRE(restoredTripwire != ecs::EcsWorld::kNull);
    auto* restoredTripwireState =
        restoredRegistry.try_get<ecs::PlacedTrap>(restoredTripwire);
    REQUIRE(restoredTripwireState != nullptr);
    CHECK(restoredTripwireState->defId == "tripwire_alarm");
    CHECK_FALSE(restoredTripwireState->armed);
    CHECK(restoredTripwireState->cooldown == doctest::Approx(0.0f));
    CHECK(restoredTripwireState->uses == 5);
    CHECK(restoredTripwireState->owner == owner);
    CHECK(restoredTripwireState->armRemaining == doctest::Approx(0.6f));
    CHECK_FALSE(
        game::AdvancePlacedTrapArming(*restoredTripwireState, 0.5f));
    CHECK(game::AdvancePlacedTrapArming(*restoredTripwireState, 0.1f));
    CHECK(restoredTripwireState->armed);

    const ecs::Entity restoredFuel = restoredEcs.FindById(fuelId);
    REQUIRE(restoredFuel != ecs::EcsWorld::kNull);
    const auto* restoredFuelState =
        restoredRegistry.try_get<ecs::PlacedTrap>(restoredFuel);
    REQUIRE(restoredFuelState != nullptr);
    CHECK(restoredFuelState->defId == "fuel_trap");
    CHECK(restoredFuelState->armed);
    CHECK(restoredFuelState->cooldown == doctest::Approx(0.4f));
    CHECK(restoredFuelState->uses == 1);
    CHECK(restoredFuelState->owner == owner);

    const ecs::Entity restoredFire = restoredEcs.FindById(fireId);
    REQUIRE(restoredFire != ecs::EcsWorld::kNull);
    const auto* restoredSource =
        restoredRegistry.try_get<ecs::FireSource>(restoredFire);
    REQUIRE(restoredSource != nullptr);
    CHECK(restoredSource->materialId == "trap_fuel");
    CHECK(restoredSource->phase == ecs::FirePhase::Burning);
    CHECK(restoredSource->burnDuration == doctest::Approx(112.5f));
    CHECK(restoredSource->burnRemaining == doctest::Approx(112.5f));

    CHECK(restoredRegistry.view<ecs::PlacedTrap>().size() == 3u);
    restarted.Shutdown();
}
