#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "characters/systems/CombatSystem.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/NetworkClient.h"
#include "game/PoiComponents.h"
#include "game/PoiStageCombat.h"
#include "game/WorldSimulation.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "physics/PhysicsWorld.h"
#include "save/EntityStore.h"
#include "save/SaveManager.h"
#include "server/ServerHost.h"
#include "world/ChunkManager.h"

using namespace zl;

namespace {

using Clock = std::chrono::steady_clock;

template <typename Predicate>
bool PumpUntil(zl::server::ServerHost& host, zl::game::NetworkClient& first,
               zl::game::NetworkClient& second, int timeoutMs,
               Predicate predicate) {
    const Clock::time_point deadline =
        Clock::now() + std::chrono::milliseconds(timeoutMs);
    Clock::time_point nextTick = Clock::now();
    while (Clock::now() < deadline) {
        if (Clock::now() >= nextTick) {
            host.Tick(1.0f / 60.0f);
            nextTick += std::chrono::milliseconds(16);
        }
        first.Poll();
        second.Poll();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

std::filesystem::path TempDir(const char* name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zomboidlike_m33_f5_t7" /
        name;
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
    create.name = "M33-F5-T7";
    create.mapId = "village_demo";
    create.gameVersion = save::kDefaultGameVersion;
    create.entities = &empty;
    std::string error;
    const bool ok = manager.Create(create, saveId, &error);
    REQUIRE_MESSAGE(ok, error);
}

zl::server::ServerHost::Options MakeHostOptions(
    const std::filesystem::path& root, const std::string& profilePath,
    std::uint64_t contentHash, const std::string& saveId) {
    zl::server::ServerHost::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.spawnDemoPopulation = false;
    options.spawnLocalPlayer = false;
    options.network = true;
    options.port = 0;
    options.contentHash = contentHash;
    options.enablePlayerProfiles = true;
    options.playerProfilesFile = profilePath;
    options.loadSaveId = saveId;
    return options;
}

const zl::game::PoiProgress* FindProgress(zl::server::ServerHost& host,
                                          std::string_view poiId) {
    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity worldEntity =
        host.Simulation().Ecs().FindById(zl::ecs::kPoiWorldProgressStableId);
    if (worldEntity == entt::null) {
        return nullptr;
    }
    const auto* world = registry.try_get<zl::ecs::PoiWorldProgress>(worldEntity);
    if (world == nullptr) {
        return nullptr;
    }
    return zl::game::FindPoiProgress(world->pois, poiId);
}

bool TeleportServerPlayer(zl::server::ServerHost& host, std::uint64_t stableId,
                          float x, float y) {
    const zl::ecs::Entity entity = host.Simulation().Ecs().FindById(stableId);
    if (entity == entt::null) {
        return false;
    }
    auto& registry = host.Simulation().Ecs().Registry();
    auto* position = registry.try_get<zl::ecs::Position>(entity);
    if (position == nullptr) {
        return false;
    }
    position->x = x;
    position->y = y;
    if (auto* rigid = registry.try_get<zl::ecs::RigidBody>(entity);
        rigid != nullptr && rigid->body != nullptr) {
        host.Simulation().Physics().TeleportBody(rigid->body, x, y,
                                                 position->z);
    }
    return true;
}

bool HasClientStage(zl::game::NetworkClient& client, std::string_view poiId,
                    int stageCount, int completedCount) {
    if (!client.HasPlayerState()) {
        return false;
    }
    for (const zl::net::PlayerPoiStageProgress& stage :
         client.PlayerState().poiStageProgress) {
        if (stage.id == poiId && stage.stageCount == stageCount &&
            stage.completedStageCount == completedCount) {
            return true;
        }
    }
    return false;
}

std::size_t ClaimCount(const entt::registry& registry, zl::ecs::Entity player,
                       std::string_view claim) {
    const auto* tasks = registry.try_get<zl::ecs::PoiTasks>(player);
    if (tasks == nullptr) {
        return 0;
    }
    return static_cast<std::size_t>(
        std::count(tasks->rewardClaims.begin(), tasks->rewardClaims.end(),
                   std::string(claim)));
}

int BagCount(const entt::registry& registry, zl::ecs::Entity player,
             const std::string& defId) {
    const auto* container = registry.try_get<zl::ecs::Container>(player);
    return container != nullptr ? zl::items::CountOf(*container, defId) : 0;
}

}  // namespace

TEST_CASE("M33-F5-T7 two clients share staged POI and restart") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    const std::filesystem::path oldCwd = std::filesystem::current_path();
    std::filesystem::current_path(root);
    struct CwdGuard {
        std::filesystem::path old;
        ~CwdGuard() { std::filesystem::current_path(old); }
    } cwdGuard{oldCwd};

    const std::filesystem::path dir = TempDir("shared_stage");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, &saveId);
    REQUIRE(!saveId.empty());

    const std::string poiId = "shared_stage_test";
    const std::filesystem::path contentPath = dir / "staged_poi.json";
    {
        std::ofstream out(contentPath, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << R"({
          "pois": {
            "enabled": true,
            "defs": [
              {
                "id": "shared_stage_test",
                "name": "Shared Stage",
                "threat": 2,
                "markerX": 70.5,
                "markerY": 24.5,
                "containers": [
                  { "x": 70.5, "y": 24.5, "tableId": "poi_motel_storage", "sprite": "object.crate" }
                ],
                "stages": [
                  {
                    "id": "approach",
                    "kind": "reach",
                    "x": 70.5,
                    "y": 24.5,
                    "radius": 3.0,
                    "next": [ "boss" ],
                    "reward": { "xp": 11.0, "item": "hospital_patient_log", "count": 1 }
                  },
                  {
                    "id": "boss",
                    "kind": "kill",
                    "x": 70.5,
                    "y": 24.5,
                    "radius": 3.0,
                    "count": 1,
                    "next": [ "sweep" ],
                    "reward": { "xp": 19.0, "item": "checkpoint_patrol_log", "count": 1 }
                  },
                  {
                    "id": "sweep",
                    "kind": "reach",
                    "x": 70.5,
                    "y": 24.5,
                    "radius": 4.0,
                    "reward": { "xp": 7.0, "item": "farm_supply_log", "count": 1 }
                  }
                ],
                "boss": {
                  "name": "Shared Boss",
                  "variant": "armored",
                  "stage": "boss",
                  "mechanic": "summon",
                  "count": 1
                }
              }
            ]
          }
        })";
    }

    std::vector<std::string> args{"ZomboidLikeTests",
                                  "--save.root=" + saveRoot.string(),
                                  "--pois.boss.healthScale=1.0",
                                  "--pois.boss.resetSeconds=30.0"};
    std::vector<const char*> argv;
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }
    const std::uint64_t contentHash = 0x4D33334635543744ull;

    zl::server::ServerHost host;
    std::string error;
    zl::server::ServerHost::Options options =
        MakeHostOptions(root, profilePath, contentHash, saveId);
    options.mapDir = (root / "assets" / "maps" / "village_demo").string();
    options.contentFile = contentPath.string();
    options.playerTargetX = 10.5f;
    options.playerTargetY = 37.5f;
    options.argc = static_cast<int>(argv.size());
    options.argv = argv.data();
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = contentHash;
    clientOptions.autoReconnect = false;
    clientOptions.playerIdentity = "m33-f5-t7-a";
    clientOptions.playerName = "StageA";
    zl::game::NetworkClient first(clientOptions);

    zl::game::NetworkClient::Options secondOptions = clientOptions;
    secondOptions.playerIdentity = "m33-f5-t7-b";
    secondOptions.playerName = "StageB";
    zl::game::NetworkClient second(secondOptions);

    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 10000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayerState() && second.HasPlayerState() &&
               host.Simulation().Chunks().GetStats().loadedChunks > 0;
    }));

    const std::uint64_t firstId = first.PlayerStableId();
    const std::uint64_t secondId = second.PlayerStableId();
    REQUIRE(firstId != 0);
    REQUIRE(secondId != 0);

    auto& registry = host.Simulation().Ecs().Registry();
    auto makeTough = [&](std::uint64_t id) {
        const zl::ecs::Entity player = host.Simulation().Ecs().FindById(id);
        REQUIRE(static_cast<bool>(player != entt::null));
        auto* health = registry.try_get<zl::ecs::Health>(player);
        REQUIRE(health != nullptr);
        health->max = 10000.0f;
        health->current = 10000.0f;
    };
    makeTough(firstId);
    makeTough(secondId);

    const zl::ecs::Entity firstPlayer =
        host.Simulation().Ecs().FindById(firstId);
    const zl::ecs::Entity secondPlayer =
        host.Simulation().Ecs().FindById(secondId);
    REQUIRE(BagCount(registry, firstPlayer, "hospital_patient_log") == 0);
    REQUIRE(BagCount(registry, secondPlayer, "hospital_patient_log") == 0);
    REQUIRE(TeleportServerPlayer(host, firstId, 70.5f, 24.5f));
    REQUIRE(TeleportServerPlayer(host, secondId, 70.5f, 24.5f));
    host.RunForTicks(1);

    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        const auto* progress = FindProgress(host, poiId);
        return progress != nullptr && progress->currentStage == 1 &&
               ClaimCount(registry, firstPlayer, "shared_stage_test.approach") ==
                   1 &&
               ClaimCount(registry, secondPlayer,
                          "shared_stage_test.approach") == 1;
    }));
    CHECK(BagCount(registry, firstPlayer, "hospital_patient_log") == 1);
    CHECK(BagCount(registry, secondPlayer, "hospital_patient_log") == 1);

    const auto findBoss = [&]() -> zl::ecs::Entity {
        zl::ecs::Entity boss = entt::null;
        registry.view<zl::ecs::PoiStageEnemy, zl::ecs::Health>().each(
            [&](const zl::ecs::Entity entity,
                const zl::ecs::PoiStageEnemy& enemy,
                const zl::ecs::Health& health) {
                if (boss == entt::null && enemy.poiId == poiId &&
                    enemy.stageId == "boss" &&
                    enemy.role == zl::game::PoiStageEnemyRole::Boss &&
                    health.current > 0.0f) {
                    boss = entity;
                }
            });
        return boss;
    };
    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        return findBoss() != entt::null;
    }));
    zl::ecs::Entity boss = findBoss();
    REQUIRE(static_cast<bool>(boss != entt::null));

    zl::ecs::DamageEvent lethal;
    lethal.amount = 100000.0f;
    lethal.type = zl::ecs::DamageType::Blunt;
    REQUIRE(host.Simulation().Combat()->ApplyDamage(
        host.Simulation().Ecs(), boss, lethal));

    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        const auto* progress = FindProgress(host, poiId);
        return progress != nullptr && progress->completed &&
               HasClientStage(first, poiId, 3, 3) &&
               HasClientStage(second, poiId, 3, 3);
    }));
    const auto* completed = FindProgress(host, poiId);
    REQUIRE(completed != nullptr);
    CHECK(completed->completedStages ==
          std::vector<std::string>({"approach", "boss", "sweep"}));
    CHECK(completed->attempts == 1);
    CHECK(ClaimCount(registry, firstPlayer, "shared_stage_test.approach") == 1);
    CHECK(ClaimCount(registry, secondPlayer, "shared_stage_test.approach") == 1);
    CHECK(ClaimCount(registry, firstPlayer, "shared_stage_test.boss") == 1);
    CHECK(ClaimCount(registry, secondPlayer, "shared_stage_test.boss") == 1);
    CHECK(ClaimCount(registry, firstPlayer, "shared_stage_test.sweep") == 1);
    CHECK(ClaimCount(registry, secondPlayer, "shared_stage_test.sweep") == 1);
    CHECK(BagCount(registry, firstPlayer, "hospital_patient_log") == 1);
    CHECK(BagCount(registry, secondPlayer, "hospital_patient_log") == 1);
    CHECK(BagCount(registry, firstPlayer, "checkpoint_patrol_log") == 1);
    CHECK(BagCount(registry, secondPlayer, "checkpoint_patrol_log") == 1);
    CHECK(BagCount(registry, firstPlayer, "farm_supply_log") == 1);
    CHECK(BagCount(registry, secondPlayer, "farm_supply_log") == 1);
    CHECK(zl::game::CountPoiStageTargets(registry, poiId, "boss") == 0);

    host.RunForTicks(30);
    CHECK(ClaimCount(registry, firstPlayer, "shared_stage_test.boss") == 1);
    CHECK(BagCount(registry, firstPlayer, "checkpoint_patrol_log") == 1);

    first.Close();
    second.Close();
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return host.SessionCount() == 0;
    }));
    host.Shutdown();

    zl::server::ServerHost restarted;
    zl::server::ServerHost::Options restartedOptions =
        MakeHostOptions(root, profilePath, contentHash, saveId);
    restartedOptions.mapDir = (root / "assets" / "maps" / "village_demo").string();
    restartedOptions.contentFile = contentPath.string();
    restartedOptions.playerTargetX = 10.5f;
    restartedOptions.playerTargetY = 37.5f;
    restartedOptions.argc = static_cast<int>(argv.size());
    restartedOptions.argv = argv.data();
    REQUIRE_MESSAGE(restarted.Init(restartedOptions, &error), error);
    REQUIRE(restarted.NetworkPort() != 0);

    zl::game::NetworkClient::Options restartedFirstOptions = clientOptions;
    restartedFirstOptions.port = restarted.NetworkPort();
    zl::game::NetworkClient restartedFirst(restartedFirstOptions);
    zl::game::NetworkClient::Options restartedSecondOptions = secondOptions;
    restartedSecondOptions.port = restarted.NetworkPort();
    zl::game::NetworkClient restartedSecond(restartedSecondOptions);

    REQUIRE(restartedFirst.Connect(&error));
    REQUIRE(restartedSecond.Connect(&error));
    REQUIRE(PumpUntil(restarted, restartedFirst, restartedSecond, 10000, [&] {
        const auto* progress = FindProgress(restarted, poiId);
        return restartedFirst.HasPlayerState() &&
               restartedSecond.HasPlayerState() && progress != nullptr &&
               progress->completed &&
               HasClientStage(restartedFirst, poiId, 3, 3) &&
               HasClientStage(restartedSecond, poiId, 3, 3);
    }));

    auto& restoredRegistry = restarted.Simulation().Ecs().Registry();
    const zl::ecs::Entity restoredFirst =
        restarted.Simulation().Ecs().FindById(firstId);
    const zl::ecs::Entity restoredSecond =
        restarted.Simulation().Ecs().FindById(secondId);
    REQUIRE(static_cast<bool>(restoredFirst != entt::null));
    REQUIRE(static_cast<bool>(restoredSecond != entt::null));
    CHECK(ClaimCount(restoredRegistry, restoredFirst,
                     "shared_stage_test.approach") == 1);
    CHECK(ClaimCount(restoredRegistry, restoredSecond,
                     "shared_stage_test.approach") == 1);
    CHECK(ClaimCount(restoredRegistry, restoredFirst,
                     "shared_stage_test.boss") == 1);
    CHECK(ClaimCount(restoredRegistry, restoredSecond,
                     "shared_stage_test.boss") == 1);
    CHECK(ClaimCount(restoredRegistry, restoredFirst,
                     "shared_stage_test.sweep") == 1);
    CHECK(ClaimCount(restoredRegistry, restoredSecond,
                     "shared_stage_test.sweep") == 1);
    CHECK(BagCount(restoredRegistry, restoredFirst, "hospital_patient_log") == 1);
    CHECK(BagCount(restoredRegistry, restoredSecond, "hospital_patient_log") == 1);
    CHECK(BagCount(restoredRegistry, restoredFirst, "checkpoint_patrol_log") == 1);
    CHECK(BagCount(restoredRegistry, restoredSecond, "checkpoint_patrol_log") == 1);
    CHECK(BagCount(restoredRegistry, restoredFirst, "farm_supply_log") == 1);
    CHECK(BagCount(restoredRegistry, restoredSecond, "farm_supply_log") == 1);
    CHECK(zl::game::CountPoiStageTargets(restoredRegistry, poiId, "boss") == 0);

    restarted.RunForTicks(60);
    CHECK(ClaimCount(restoredRegistry, restoredFirst,
                     "shared_stage_test.boss") == 1);
    CHECK(BagCount(restoredRegistry, restoredFirst,
                   "checkpoint_patrol_log") == 1);

    restartedFirst.Close();
    restartedSecond.Close();
    REQUIRE(PumpUntil(restarted, restartedFirst, restartedSecond, 5000, [&] {
        return restarted.SessionCount() == 0;
    }));
    restarted.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}
