#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/components/ProgressionComponents.h"
#include "core/Constants.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/HomesteadComponents.h"
#include "game/MissionComponents.h"
#include "game/NetworkClient.h"
#include "game/StructureComponents.h"
#include "game/SettlementNpc.h"
#include "game/StrategicMap.h"
#include "game/StrategicState.h"
#include "game/WorldQueries.h"
#include "game/WorldSimulation.h"
#include "net/StrategicProtocol.h"
#include "physics/PhysicsWorld.h"
#include "save/SaveManager.h"
#include "server/ServerHost.h"
#include "world/WorldGraph.h"

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

using Clock = std::chrono::steady_clock;

template <typename Predicate>
bool PumpUntil(server::ServerHost& host,
               const std::vector<game::NetworkClient*>& clients, int timeoutMs,
               Predicate predicate) {
    const Clock::time_point deadline =
        Clock::now() + std::chrono::milliseconds(timeoutMs);
    Clock::time_point nextTick = Clock::now();
    while (Clock::now() < deadline) {
        if (Clock::now() >= nextTick) {
            host.Tick(1.0f / 60.0f);
            nextTick += std::chrono::milliseconds(16);
        }
        for (game::NetworkClient* client : clients) {
            client->Poll();
        }
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

net::Command MakeMove(std::uint64_t playerId, float x, float y) {
    net::Command command;
    command.type = net::CommandType::Move;
    command.playerStableId = playerId;
    command.payload.x = x;
    command.payload.y = y;
    return command;
}

net::Command MakeVehicleCommand(net::CommandType type,
                                std::uint64_t playerId,
                                std::uint64_t vehicleId) {
    net::Command command;
    command.type = type;
    command.playerStableId = playerId;
    command.payload.entityId = vehicleId;
    return command;
}

void PickOpenDriveDirection(server::ServerHost& host, float x, float y,
                            float* outX, float* outY) {
    constexpr float kDirections[4][2] = {
        {1.0f, 0.0f}, {-1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, -1.0f}};
    for (const auto& direction : kDirections) {
        bool clear = true;
        for (int step = 1; step <= 4; ++step) {
            const int wx = static_cast<int>(std::floor(x)) +
                           static_cast<int>(direction[0]) * step;
            const int wy = static_cast<int>(std::floor(y)) +
                           static_cast<int>(direction[1]) * step;
            const world::GridSquare* square =
                host.Simulation().World().GetSquare(wx, wy, 0);
            if (square == nullptr ||
                game::IsSquareBlocked(host.Simulation().World(), wx, wy, 0)) {
                clear = false;
                break;
            }
        }
        if (clear) {
            *outX = direction[0];
            *outY = direction[1];
            return;
        }
    }
    *outX = 1.0f;
    *outY = 0.0f;
}

void WorldDirToScreen(float worldX, float worldY, float* outX, float* outY) {
    const float screenX =
        (worldX - worldY) * static_cast<float>(core::kTileW) * 0.5f;
    const float screenY =
        (worldX + worldY) * static_cast<float>(core::kTileH) * 0.5f;
    const float length = std::sqrt(screenX * screenX + screenY * screenY);
    if (length > 1.0e-4f) {
        *outX = screenX / length;
        *outY = screenY / length;
    } else {
        *outX = 1.0f;
        *outY = 0.0f;
    }
}

std::filesystem::path TempDir(const char* name) {
    static std::uint64_t sequence = 0;
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zomboidlike_t7" /
        (std::string(name) + "-" + std::to_string(stamp) + "-" +
         std::to_string(++sequence));
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    return dir;
}

bool HasSettlementGuard(game::WorldSimulation& simulation,
                        std::string_view settlementId) {
    bool found = false;
    simulation.Ecs().Registry().view<game::SettlementNpc>().each(
        [&](const ecs::Entity, const game::SettlementNpc& npc) {
            if (npc.settlementId == settlementId &&
                npc.kind == game::SettlementNpcKind::Guard) {
                found = true;
            }
        });
    return found;
}

ecs::Entity FindSettlementGuard(game::WorldSimulation& simulation,
                                std::string_view settlementId) {
    ecs::Entity found = entt::null;
    simulation.Ecs().Registry().view<game::SettlementNpc>().each(
        [&](const ecs::Entity entity, const game::SettlementNpc& npc) {
            if (npc.settlementId == settlementId &&
                npc.kind == game::SettlementNpcKind::Guard) {
                found = entity;
            }
        });
    return found;
}
void CreateEmptySave(const std::filesystem::path& saveRoot,
                     const std::string& mapId, std::string* saveId) {
    save::EntitySnapshot empty;
    save::SaveManager::Options options;
    options.root = saveRoot;
    options.gameVersion = save::kDefaultGameVersion;
    save::SaveManager manager(options);
    save::SaveManager::CreateOptions create;
    create.name = "M32-T7";
    create.mapId = mapId;
    create.gameVersion = save::kDefaultGameVersion;
    create.entities = &empty;
    std::string error;
    REQUIRE_MESSAGE(manager.Create(create, saveId, &error), error);
}

server::ServerHost::Options MakeHostOptions(
    const std::filesystem::path& root, const std::filesystem::path& saveRoot,
    const std::string& profilePath, std::uint64_t contentHash,
    const std::string& saveId, std::vector<std::string>* args,
    std::vector<const char*>* argv, int loadRadius = 2) {
    args->push_back("ZomboidLikeTests");
    args->push_back("--world.mapDir=assets/maps/village_demo_m31");
    args->push_back("--world.loadRadius=" + std::to_string(loadRadius));
    args->push_back("--save.root=" + saveRoot.string());
    if (!saveId.empty()) {
        args->push_back("--server.saveId=" + saveId);
    }
    argv->clear();
    for (const std::string& arg : *args) {
        argv->push_back(arg.c_str());
    }

    server::ServerHost::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.spawnDemoPopulation = false;
    options.spawnLocalPlayer = false;
    options.network = true;
    options.port = 0;
    options.contentHash = contentHash;
    options.enablePlayerProfiles = true;
    options.playerProfilesFile = profilePath;
    options.loadSaveId = saveId;
    options.argc = static_cast<int>(argv->size());
    options.argv = argv->data();
    return options;
}

game::NetworkClient::Options ClientOptions(std::uint16_t port,
                                           std::uint64_t contentHash,
                                           const std::string& identity) {
    game::NetworkClient::Options options;
    options.host = "127.0.0.1";
    options.port = port;
    options.contentHash = contentHash;
    options.autoReconnect = false;
    options.playerIdentity = identity;
    options.playerName = identity;
    return options;
}

bool TeleportServerPlayer(server::ServerHost& host, std::uint64_t stableId,
                          float x, float y) {
    const ecs::Entity entity = host.Simulation().Ecs().FindById(stableId);
    if (entity == entt::null) {
        return false;
    }
    auto& registry = host.Simulation().Ecs().Registry();
    auto* position = registry.try_get<ecs::Position>(entity);
    if (position == nullptr) {
        return false;
    }
    position->x = x;
    position->y = y;
    if (auto* rigid = registry.try_get<ecs::RigidBody>(entity);
        rigid != nullptr && rigid->body != nullptr) {
        host.Simulation().Physics().TeleportBody(rigid->body, x, y,
                                                 position->z);
    }
    host.RunForTicks(1);
    return true;
}

const net::StrategicRegionSnapshot* FindRegion(
    const net::StrategicSnapshot& snapshot, std::string_view id) {
    for (const net::StrategicRegionSnapshot& region : snapshot.regions) {
        if (region.id == id) {
            return &region;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("M32-T7 双玩家共享战略快照、区域校验与反攻请求") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("shared_strategy");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE(!saveId.empty());

    std::vector<std::string> args;
    std::vector<const char*> argv;
    const std::uint64_t contentHash = 0x4D33325437534852ull;
    server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId,
                        &args, &argv);
    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    game::NetworkClient first(
        ClientOptions(host.NetworkPort(), contentHash, "t7-first"));
    game::NetworkClient second(
        ClientOptions(host.NetworkPort(), contentHash, "t7-second"));
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    std::vector<game::NetworkClient*> clients{&first, &second};
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayer() && second.HasPlayer() &&
               host.SessionCount() == 2;
    }));

    REQUIRE(TeleportServerPlayer(host, first.PlayerStableId(), 75.5f, 26.5f));
    REQUIRE(TeleportServerPlayer(host, second.PlayerStableId(), 110.5f, 210.5f));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return first.HasStrategicState() && second.HasStrategicState() &&
               first.StrategicState().regions.size() == 5 &&
               second.StrategicState().regions.size() == 5;
    }));
    CHECK(first.StrategicState().enabled);
    CHECK(second.StrategicState().enabled);
    CHECK(first.StrategicState().supplies ==
          host.Simulation().Strategic()->resources.supplies);
    CHECK(second.StrategicState().supplies ==
          host.Simulation().Strategic()->resources.supplies);
    CHECK(FindRegion(first.StrategicState(), "village_core") != nullptr);
    CHECK(FindRegion(second.StrategicState(), "south_farmland") != nullptr);

    game::StrategicState* strategic = host.Simulation().Strategic();
    REQUIRE(strategic != nullptr);
    game::StrategicRegionState* village =
        strategic->FindRegion("village_core");
    REQUIRE(village != nullptr);
    const int suppliesBefore = strategic->resources.supplies;
    const float villageSupplyBefore = village->supply;

    REQUIRE(first.SendStrategicRequest(
        net::StrategicRequestAction::DonateSupply, "village_core", 10,
        &error));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return first.HasStrategicResult() &&
               first.LastStrategicResult().sequence == 1u &&
               first.LastStrategicResult().accepted;
    }));
    CHECK(strategic->resources.supplies == suppliesBefore - 10);
    CHECK(village->supply == villageSupplyBefore + 10.0f);
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        const net::StrategicRegionSnapshot* remote =
            FindRegion(second.StrategicState(), "village_core");
        return remote != nullptr && remote->supply >= villageSupplyBefore + 10.0f;
    }));

    const int suppliesBeforeMission = strategic->resources.supplies;
    REQUIRE(host.Simulation().GrantStrategicMissionReward(
        "settlement_escort", &error));
    const int suppliesAfterMission = strategic->resources.supplies;
    REQUIRE(host.Simulation().GrantStrategicMissionReward(
        "settlement_escort", &error));
    CHECK(suppliesAfterMission > suppliesBeforeMission);
    CHECK(strategic->resources.supplies == suppliesAfterMission);

    const int suppliesBeforeRejected = strategic->resources.supplies;
    REQUIRE(second.SendStrategicRequest(
        net::StrategicRequestAction::DonateSupply, "village_core", 5,
        &error));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return second.HasStrategicResult() &&
               second.LastStrategicResult().sequence == 1u;
    }));
    CHECK_FALSE(second.LastStrategicResult().accepted);
    CHECK(strategic->resources.supplies == suppliesBeforeRejected);

    game::StrategicRegionState* farm =
        strategic->FindRegion("south_farmland");
    REQUIRE(farm != nullptr);
    farm->owner = game::StrategicOwner::Infested;
    farm->recoveryStage = game::StrategicRecoveryStage::ClearCore;
    farm->recoveryReadyMinute = 0;
    farm->infectionCores = 1;
    farm->facilityDamage = 30;
    host.Simulation().SettlementNpcs()->MarkFallen(
        host.Simulation().Ecs(), "south_farmland");
    REQUIRE_FALSE(host.Simulation().RegionalSiegeStatus().active);

    REQUIRE(second.SendStrategicRequest(
        net::StrategicRequestAction::ClearCore, "south_farmland", 0, &error));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return second.HasStrategicResult() &&
               second.LastStrategicResult().sequence == 2u;
    }));
    CHECK(second.LastStrategicResult().accepted);
    CHECK(farm->infectionCores == 0);
    CHECK(farm->recoveryStage == game::StrategicRecoveryStage::Repair);

    REQUIRE(second.SendStrategicRequest(
        net::StrategicRequestAction::RepairFacility, "south_farmland", 0,
        &error));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return second.HasStrategicResult() &&
               second.LastStrategicResult().sequence == 3u;
    }));
    CHECK(second.LastStrategicResult().accepted);
    CHECK(farm->facilityDamage == 0);
    CHECK(farm->recoveryStage == game::StrategicRecoveryStage::Counterattack);

    REQUIRE(second.SendStrategicRequest(
        net::StrategicRequestAction::Reclaim, "south_farmland", 0, &error));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return second.HasStrategicResult() &&
               second.LastStrategicResult().sequence == 4u;
    }));
    CHECK(second.LastStrategicResult().accepted);
    CHECK(host.Simulation().RegionalSiegeStatus().active);
    CHECK(host.Simulation().RegionalSiegeStatus().regionId ==
          "south_farmland");

    first.Close();
    second.Close();
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M32-T12 战略贡献按比例拆分个人经验") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("contribution_reward");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE(!saveId.empty());

    std::vector<std::string> args;
    std::vector<const char*> argv;
    const std::uint64_t contentHash = 0x4D33325431325257ull;
    server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId,
                        &args, &argv);
    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    game::NetworkClient first(
        ClientOptions(host.NetworkPort(), contentHash, "t12-first"));
    game::NetworkClient second(
        ClientOptions(host.NetworkPort(), contentHash, "t12-second"));
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    std::vector<game::NetworkClient*> clients{&first, &second};
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayer() && second.HasPlayer() &&
               host.SessionCount() == 2;
    }));

    REQUIRE(TeleportServerPlayer(host, first.PlayerStableId(), 75.5f, 26.5f));
    REQUIRE(TeleportServerPlayer(host, second.PlayerStableId(), 75.5f,
                                 26.5f));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return first.HasStrategicState() && second.HasStrategicState();
    }));

    game::StrategicState* strategic = host.Simulation().Strategic();
    REQUIRE(strategic != nullptr);
    game::StrategicRegionState* village =
        strategic->FindRegion("village_core");
    REQUIRE(village != nullptr);

    village->owner = game::StrategicOwner::Infested;
    village->siegePhase = game::StrategicSiegePhase::None;
    village->recoveryStage = game::StrategicRecoveryStage::None;
    host.RunForTicks(1);
    village->owner = game::StrategicOwner::Contested;
    host.RunForTicks(1);
    REQUIRE(village->owner == game::StrategicOwner::Contested);

    REQUIRE(first.SendStrategicRequest(
        net::StrategicRequestAction::DonateSupply, "village_core", 20,
        &error));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return first.HasStrategicResult() &&
               first.LastStrategicResult().sequence == 1u &&
               first.LastStrategicResult().accepted;
    }));
    REQUIRE(second.SendStrategicRequest(
        net::StrategicRequestAction::DonateSupply, "village_core", 10,
        &error));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return second.HasStrategicResult() &&
               second.LastStrategicResult().sequence == 1u &&
               second.LastStrategicResult().accepted;
    }));

    const ecs::Entity firstEntity =
        host.Simulation().Ecs().FindById(first.PlayerStableId());
    const ecs::Entity secondEntity =
        host.Simulation().Ecs().FindById(second.PlayerStableId());
    REQUIRE(static_cast<bool>(firstEntity != entt::null));
    REQUIRE(static_cast<bool>(secondEntity != entt::null));
    auto* firstProgression =
        host.Simulation().Ecs().Registry().try_get<ecs::Progression>(
            firstEntity);
    auto* secondProgression =
        host.Simulation().Ecs().Registry().try_get<ecs::Progression>(
            secondEntity);
    REQUIRE(firstProgression != nullptr);
    REQUIRE(secondProgression != nullptr);
    const float firstBefore = firstProgression->xp;
    const float secondBefore = secondProgression->xp;

    village->owner = game::StrategicOwner::Human;
    host.RunForTicks(2);
    const float firstGain = firstProgression->xp - firstBefore;
    const float secondGain = secondProgression->xp - secondBefore;
    CHECK(firstGain == doctest::Approx(80.0f));
    CHECK(secondGain == doctest::Approx(40.0f));
    CHECK(firstGain > secondGain);

    const float firstStable = firstProgression->xp;
    const float secondStable = secondProgression->xp;
    host.RunForTicks(1);
    CHECK(firstProgression->xp == doctest::Approx(firstStable));
    CHECK(secondProgression->xp == doctest::Approx(secondStable));

    first.Close();
    second.Close();
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M32-T25 战略贡献账本跨服务器重启保留") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("contribution_restart");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE(!saveId.empty());

    const std::uint64_t contentHash = 0x4D33325432355257ull;
    {
        std::vector<std::string> args;
        std::vector<const char*> argv;
        server::ServerHost::Options options =
            MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId,
                            &args, &argv);
        server::ServerHost host;
        std::string error;
        REQUIRE_MESSAGE(host.Init(options, &error), error);
        game::NetworkClient client(
            ClientOptions(host.NetworkPort(), contentHash, "t25-first"));
        REQUIRE(client.Connect(&error));
        std::vector<game::NetworkClient*> clients{&client};
        REQUIRE(PumpUntil(host, clients, 10000, [&] {
            return client.IsConnected() && client.HasPlayer() &&
                   client.HasStrategicState();
        }));
        REQUIRE(TeleportServerPlayer(host, client.PlayerStableId(), 75.5f,
                                     26.5f));
        REQUIRE(client.SendStrategicRequest(
            net::StrategicRequestAction::DonateSupply, "village_core", 20,
            &error));
        REQUIRE(PumpUntil(host, clients, 10000, [&] {
            return client.HasStrategicResult() &&
                   client.LastStrategicResult().accepted;
        }));
        client.Close();
        host.Shutdown();
    }

    {
        std::vector<std::string> args;
        std::vector<const char*> argv;
        server::ServerHost::Options options =
            MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId,
                            &args, &argv);
        server::ServerHost host;
        std::string error;
        REQUIRE_MESSAGE(host.Init(options, &error), error);
        game::NetworkClient client(
            ClientOptions(host.NetworkPort(), contentHash, "t25-first"));
        REQUIRE(client.Connect(&error));
        std::vector<game::NetworkClient*> clients{&client};
        REQUIRE(PumpUntil(host, clients, 10000, [&] {
            return client.IsConnected() && client.HasPlayer() &&
                   client.HasStrategicState();
        }));
        game::StrategicState* strategic = host.Simulation().Strategic();
        REQUIRE(strategic != nullptr);
        game::StrategicRegionState* village =
            strategic->FindRegion("village_core");
        REQUIRE(village != nullptr);
        REQUIRE(strategic->contributions.size() == 1u);
        CHECK(strategic->contributions[0].stableId ==
              client.PlayerStableId());
        CHECK(strategic->contributions[0].regionId == "village_core");
        CHECK(strategic->contributions[0].points == 20);
        const ecs::Entity player =
            host.Simulation().Ecs().FindById(client.PlayerStableId());
        REQUIRE(static_cast<bool>(player != entt::null));
        auto* progression =
            host.Simulation().Ecs().Registry().try_get<ecs::Progression>(
                player);
        REQUIRE(progression != nullptr);
        const float before = progression->xp;

        village->owner = game::StrategicOwner::Infested;
        host.RunForTicks(1);
        village->owner = game::StrategicOwner::Contested;
        host.RunForTicks(1);
        village->owner = game::StrategicOwner::Human;
        host.RunForTicks(2);
        CHECK(progression->xp - before == doctest::Approx(120.0f));

        client.Close();
        host.Shutdown();
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}
TEST_CASE("M32-T13 战略任务完成计入区域贡献") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("mission_contribution");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE(!saveId.empty());

    std::vector<std::string> args;
    std::vector<const char*> argv;
    const std::uint64_t contentHash = 0x4D33325431335257ull;
    server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId,
                        &args, &argv);
    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    game::NetworkClient first(
        ClientOptions(host.NetworkPort(), contentHash, "t13-first"));
    game::NetworkClient second(
        ClientOptions(host.NetworkPort(), contentHash, "t13-second"));
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    std::vector<game::NetworkClient*> clients{&first, &second};
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayer() && second.HasPlayer() &&
               first.HasStrategicState() && second.HasStrategicState() &&
               host.SessionCount() == 2;
    }));

    REQUIRE(TeleportServerPlayer(host, first.PlayerStableId(), 75.5f, 26.5f));
    REQUIRE(TeleportServerPlayer(host, second.PlayerStableId(), 75.5f,
                                 26.5f));

    game::StrategicState* strategic = host.Simulation().Strategic();
    REQUIRE(strategic != nullptr);
    game::StrategicRegionState* north =
        strategic->FindRegion("north_fields");
    REQUIRE(north != nullptr);
    CHECK(north->owner == game::StrategicOwner::Contested);

    const ecs::Entity firstEntity =
        host.Simulation().Ecs().FindById(first.PlayerStableId());
    REQUIRE(static_cast<bool>(firstEntity != entt::null));
    auto& missionLog =
        host.Simulation().Ecs().Registry().get_or_emplace<ecs::MissionLog>(
            firstEntity);
    missionLog.missions.push_back(ecs::MissionRunState{
        "settlement_clear", {6}, ecs::MissionRunStatus::Active, 1, false});

    host.RunForTicks(2);
    bool missionCompleted = false;
    for (const ecs::MissionRunState& state : missionLog.missions) {
        if (state.id == "settlement_clear" &&
            state.status == ecs::MissionRunStatus::Completed) {
            missionCompleted = true;
        }
    }
    CHECK(missionCompleted);

    REQUIRE(TeleportServerPlayer(host, second.PlayerStableId(), 150.5f,
                                 64.5f));
    REQUIRE(second.SendStrategicRequest(
        net::StrategicRequestAction::DonateSupply, "north_fields", 25,
        &error));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return second.HasStrategicResult() &&
               second.LastStrategicResult().sequence == 1u &&
               second.LastStrategicResult().accepted;
    }));

    auto* firstProgression =
        host.Simulation().Ecs().Registry().try_get<ecs::Progression>(
            firstEntity);
    const ecs::Entity secondEntity =
        host.Simulation().Ecs().FindById(second.PlayerStableId());
    auto* secondProgression =
        host.Simulation().Ecs().Registry().try_get<ecs::Progression>(
            secondEntity);
    REQUIRE(firstProgression != nullptr);
    REQUIRE(secondProgression != nullptr);
    const float firstBefore = firstProgression->xp;
    const float secondBefore = secondProgression->xp;

    north->owner = game::StrategicOwner::Human;
    host.RunForTicks(2);
    CHECK(firstProgression->xp - firstBefore == doctest::Approx(60.0f));
    CHECK(secondProgression->xp - secondBefore == doctest::Approx(60.0f));

    const float firstStable = firstProgression->xp;
    const float secondStable = secondProgression->xp;
    host.RunForTicks(1);
    CHECK(firstProgression->xp == doctest::Approx(firstStable));
    CHECK(secondProgression->xp == doctest::Approx(secondStable));

    first.Close();
    second.Close();
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M32-T15 守城伤害按贡献分配奖励") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("siege_damage_reward");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE(!saveId.empty());

    std::vector<std::string> args;
    std::vector<const char*> argv;
    const std::uint64_t contentHash = 0x4D33325431355257ull;
    server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId,
                        &args, &argv, 6);
    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    game::NetworkClient first(
        ClientOptions(host.NetworkPort(), contentHash, "t15-first"));
    game::NetworkClient second(
        ClientOptions(host.NetworkPort(), contentHash, "t15-second"));
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    std::vector<game::NetworkClient*> clients{&first, &second};
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayer() && second.HasPlayer() &&
               first.HasStrategicState() && second.HasStrategicState() &&
               host.SessionCount() == 2;
    }));
    REQUIRE(TeleportServerPlayer(host, first.PlayerStableId(), 110.5f,
                                 210.5f));
    REQUIRE(TeleportServerPlayer(host, second.PlayerStableId(), 75.5f,
                                 26.5f));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return HasSettlementGuard(host.Simulation(), "south_farmland");
    }));
    host.RunForTicks(120);

    game::StrategicState* strategic = host.Simulation().Strategic();
    REQUIRE(strategic != nullptr);
    game::StrategicRegionState* farm =
        strategic->FindRegion("south_farmland");
    REQUIRE(farm != nullptr);
    farm->owner = game::StrategicOwner::Human;
    farm->siegePhase = game::StrategicSiegePhase::Warning;
    farm->nextAttackMinute = 0;
    farm->lastEventMinute = 0;
    farm->pressure = 90.0f;

    host.AdvanceGameMinutes(30.0);
    host.RunForTicks(1);
    REQUIRE(host.Simulation().RegionalSiegeStatus().active);
    CHECK(host.Simulation().RegionalSiegeStatus().regionId ==
          "south_farmland");

    const auto raiders = [&] {
        std::vector<ecs::Entity> result;
        host.Simulation()
            .Ecs()
            .Registry()
            .view<game::SettlementRaidZombie>()
            .each([&](const ecs::Entity entity,
                      const game::SettlementRaidZombie& raid) {
                if (raid.settlementId == "south_farmland" &&
                    host.Simulation().Ecs().Alive(entity)) {
                    result.push_back(entity);
                }
            });
        return result;
    };
    CHECK(host.Simulation().RegionalSiegeStatus().settlementId ==
          "south_farmland");
    CHECK(host.Simulation().SettlementNpcs()->RaidActive(
        "south_farmland"));
    host.Simulation().UpdateRegionalSiege(11.0f, false,
                                          "south_farmland");
    CHECK(host.Simulation().RegionalSiegeStatus().phase ==
          game::RegionalSiegePhase::Waves);
    CHECK(host.Simulation().RegionalSiegeStatus().wave >= 1);
    CHECK(host.Simulation().RegionalSiegeStatus().aliveEnemies > 0);
    std::vector<ecs::Entity> spawned = raiders();
    REQUIRE(!spawned.empty());
    const ecs::Entity target = spawned.front();
    for (std::size_t i = 1; i < spawned.size(); ++i) {
        host.Simulation().Ecs().Destroy(spawned[i]);
    }
    auto* targetHealth =
        host.Simulation().Ecs().Registry().try_get<ecs::Health>(target);
    REQUIRE(targetHealth != nullptr);
    targetHealth->max = 10000.0f;
    targetHealth->current = 10000.0f;
    const auto* targetPosition =
        host.Simulation().Ecs().Registry().try_get<ecs::Position>(target);
    REQUIRE(targetPosition != nullptr);
    REQUIRE(TeleportServerPlayer(host, first.PlayerStableId(),
                                 targetPosition->x - 1.5f,
                                 targetPosition->y));
    const ecs::Entity firstEntity =
        host.Simulation().Ecs().FindById(first.PlayerStableId());
    REQUIRE(static_cast<bool>(firstEntity != entt::null));
    auto& intent = host.Simulation()
                       .Ecs()
                       .Registry()
                       .get_or_emplace<ecs::AttackIntent>(firstEntity);
    intent.pending = true;
    intent.targetId = host.Simulation().Ecs().IdOf(target).id;
    intent.dirX = 1.0f;
    intent.dirY = 0.0f;
    intent.range = 5.0f;
    intent.arcDegrees = 90.0f;
    intent.damage = 500.0f;
    intent.knockback = 0.0f;
    intent.staggerSeconds = 0.0f;
    intent.staminaCost = 0.0f;
    const auto* playerPosition =
        host.Simulation().Ecs().Registry().try_get<ecs::Position>(firstEntity);
    REQUIRE(playerPosition != nullptr);
    const float attackDx = targetPosition->x - playerPosition->x;
    const float attackDy = targetPosition->y - playerPosition->y;
    CHECK(intent.pending);
    CHECK(playerPosition->x < targetPosition->x);
    CHECK(attackDx * attackDx + attackDy * attackDy <= 25.0f);
    CHECK(playerPosition->z == targetPosition->z);
    CHECK_FALSE(host.Simulation().Ecs().Registry().all_of<ecs::NoCombatTarget>(
        target));

    host.RunForTicks(1);
    CHECK_FALSE(intent.pending);
    CHECK_FALSE(host.Simulation().Ecs().Registry().all_of<ecs::NoCombatTarget>(
        target));
    const auto& hits = host.Simulation().Combat()->GetHits();
    CHECK(!hits.empty());
    bool hitTarget = false;
    float hitDamage = 0.0f;
    for (const auto& hit : hits) {
        if (hit.attacker == firstEntity && hit.target == target) {
            hitTarget = true;
            hitDamage = hit.damage;
        }
    }
    CHECK(hitTarget);
    CHECK(hitDamage > 0.0f);
    const ecs::Entity secondEntity =
        host.Simulation().Ecs().FindById(second.PlayerStableId());
    REQUIRE(static_cast<bool>(secondEntity != entt::null));
    auto* firstProgression =
        host.Simulation().Ecs().Registry().try_get<ecs::Progression>(
            firstEntity);
    auto* secondProgression =
        host.Simulation().Ecs().Registry().try_get<ecs::Progression>(
            secondEntity);
    REQUIRE(firstProgression != nullptr);
    REQUIRE(secondProgression != nullptr);
    const float firstBefore = firstProgression->xp;
    const float secondBefore = secondProgression->xp;

    for (const ecs::Entity raider : raiders()) {
        host.Simulation().Ecs().Destroy(raider);
    }
    for (int i = 0; i < 300; ++i) {
        for (const ecs::Entity raider : raiders()) {
            host.Simulation().Ecs().Destroy(raider);
        }
        host.Simulation().UpdateRegionalSiege(1.0f, false,
                                              "south_farmland");
        if (!host.Simulation().RegionalSiegeStatus().active) {
            break;
        }
    }
    REQUIRE_FALSE(host.Simulation().RegionalSiegeStatus().active);
    REQUIRE(host.Simulation().RegionalSiegeLastResult().held);
    CHECK(farm->owner == game::StrategicOwner::Human);

    host.RunForTicks(1);
    CHECK(firstProgression->xp - firstBefore == doctest::Approx(80.0f));
    CHECK(secondProgression->xp - secondBefore == doctest::Approx(0.0f));

    const float firstStable = firstProgression->xp;
    const float secondStable = secondProgression->xp;
    host.RunForTicks(1);
    CHECK(firstProgression->xp == doctest::Approx(firstStable));
    CHECK(secondProgression->xp == doctest::Approx(secondStable));

    first.Close();
    second.Close();
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M32-T16 远程守城伤害计入贡献") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("siege_ranged_reward");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE(!saveId.empty());

    std::vector<std::string> args;
    std::vector<const char*> argv;
    const std::uint64_t contentHash = 0x4D33325431365257ull;
    server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId,
                        &args, &argv, 6);
    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    game::NetworkClient first(
        ClientOptions(host.NetworkPort(), contentHash, "t16-first"));
    game::NetworkClient second(
        ClientOptions(host.NetworkPort(), contentHash, "t16-second"));
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    std::vector<game::NetworkClient*> clients{&first, &second};
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayer() && second.HasPlayer() &&
               first.HasStrategicState() && second.HasStrategicState() &&
               host.SessionCount() == 2;
    }));
    REQUIRE(TeleportServerPlayer(host, first.PlayerStableId(), 110.5f,
                                 210.5f));
    REQUIRE(TeleportServerPlayer(host, second.PlayerStableId(), 75.5f,
                                 26.5f));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return HasSettlementGuard(host.Simulation(), "south_farmland");
    }));
    host.RunForTicks(120);

    game::StrategicState* strategic = host.Simulation().Strategic();
    REQUIRE(strategic != nullptr);
    game::StrategicRegionState* farm =
        strategic->FindRegion("south_farmland");
    REQUIRE(farm != nullptr);
    farm->owner = game::StrategicOwner::Human;
    farm->siegePhase = game::StrategicSiegePhase::Warning;
    farm->nextAttackMinute = 0;
    farm->lastEventMinute = 0;
    farm->pressure = 90.0f;

    host.AdvanceGameMinutes(30.0);
    host.RunForTicks(1);
    REQUIRE(host.Simulation().RegionalSiegeStatus().active);
    CHECK(host.Simulation().RegionalSiegeStatus().regionId ==
          "south_farmland");
    const auto raiders = [&] {
        std::vector<ecs::Entity> result;
        host.Simulation()
            .Ecs()
            .Registry()
            .view<game::SettlementRaidZombie>()
            .each([&](const ecs::Entity entity,
                      const game::SettlementRaidZombie& raid) {
                if (raid.settlementId == "south_farmland" &&
                    host.Simulation().Ecs().Alive(entity)) {
                    result.push_back(entity);
                }
            });
        return result;
    };
    host.Simulation().UpdateRegionalSiege(11.0f, false, "south_farmland");
    REQUIRE(host.Simulation().RegionalSiegeStatus().phase ==
            game::RegionalSiegePhase::Waves);
    std::vector<ecs::Entity> spawned = raiders();
    REQUIRE(!spawned.empty());
    const ecs::Entity target = spawned.front();
    for (std::size_t i = 1; i < spawned.size(); ++i) {
        host.Simulation().Ecs().Destroy(spawned[i]);
    }
    auto* targetHealth =
        host.Simulation().Ecs().Registry().try_get<ecs::Health>(target);
    REQUIRE(targetHealth != nullptr);
    targetHealth->max = 10000.0f;
    targetHealth->current = 10000.0f;
    const auto* targetPosition =
        host.Simulation().Ecs().Registry().try_get<ecs::Position>(target);
    REQUIRE(targetPosition != nullptr);
    REQUIRE(TeleportServerPlayer(host, first.PlayerStableId(),
                                 targetPosition->x - 1.5f,
                                 targetPosition->y));
    const ecs::Entity firstEntity =
        host.Simulation().Ecs().FindById(first.PlayerStableId());
    const ecs::Entity secondEntity =
        host.Simulation().Ecs().FindById(second.PlayerStableId());
    REQUIRE(static_cast<bool>(firstEntity != entt::null));
    REQUIRE(static_cast<bool>(secondEntity != entt::null));

    const ecs::Entity projectile = host.Simulation().Ecs().Create();
    auto& registry = host.Simulation().Ecs().Registry();
    registry.emplace<ecs::Position>(
        projectile, ecs::Position{targetPosition->x, targetPosition->y,
                                  targetPosition->z});
    registry.emplace<ecs::Velocity>(projectile, ecs::Velocity{1.0f, 0.0f});
    ecs::Projectile shot;
    shot.ownerStableId = first.PlayerStableId();
    shot.lifetime = 1.0f;
    shot.radius = 0.5f;
    shot.damage = 500.0f;
    shot.knockback = 0.0f;
    shot.staggerSeconds = 0.0f;
    shot.startX = targetPosition->x;
    shot.startY = targetPosition->y;
    shot.maxDistance = 0.0f;
    registry.emplace<ecs::Projectile>(projectile, shot);

    const std::uint64_t hitsBefore = host.GetNetworkStats().projectilesHit;
    host.RunForTicks(1);
    CHECK(host.GetNetworkStats().projectilesHit > hitsBefore);
    auto* firstProgression =
        registry.try_get<ecs::Progression>(firstEntity);
    auto* secondProgression =
        registry.try_get<ecs::Progression>(secondEntity);
    REQUIRE(firstProgression != nullptr);
    REQUIRE(secondProgression != nullptr);
    const float firstBefore = firstProgression->xp;
    const float secondBefore = secondProgression->xp;

    for (const ecs::Entity raider : raiders()) {
        host.Simulation().Ecs().Destroy(raider);
    }
    for (int i = 0; i < 300; ++i) {
        for (const ecs::Entity raider : raiders()) {
            host.Simulation().Ecs().Destroy(raider);
        }
        host.Simulation().UpdateRegionalSiege(1.0f, false,
                                              "south_farmland");
        if (!host.Simulation().RegionalSiegeStatus().active) {
            break;
        }
    }
    REQUIRE_FALSE(host.Simulation().RegionalSiegeStatus().active);
    REQUIRE(host.Simulation().RegionalSiegeLastResult().held);

    host.RunForTicks(1);
    CHECK(firstProgression->xp - firstBefore == doctest::Approx(80.0f));
    CHECK(secondProgression->xp - secondBefore == doctest::Approx(0.0f));

    first.Close();
    second.Close();
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M32-T17 载具撞击守城伤害计入贡献") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("siege_vehicle_reward");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE(!saveId.empty());

    std::vector<std::string> args;
    std::vector<const char*> argv;
    const std::uint64_t contentHash = 0x4D33325431375257ull;
    server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId,
                        &args, &argv, 6);
    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    game::NetworkClient first(
        ClientOptions(host.NetworkPort(), contentHash, "t17-first"));
    REQUIRE(first.Connect(&error));
    std::vector<game::NetworkClient*> clients{&first};
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return first.IsConnected() && first.HasPlayer() &&
               first.HasStrategicState() && host.SessionCount() == 1;
    }));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return HasSettlementGuard(host.Simulation(), "village_haven");
    }));
    host.RunForTicks(120);

    game::StrategicState* strategic = host.Simulation().Strategic();
    REQUIRE(strategic != nullptr);
    game::StrategicRegionState* farm =
        strategic->FindRegion("village_core");
    REQUIRE(farm != nullptr);
    farm->owner = game::StrategicOwner::Human;
    farm->siegePhase = game::StrategicSiegePhase::Warning;
    farm->nextAttackMinute = 0;
    farm->lastEventMinute = 0;
    farm->pressure = 90.0f;

    host.AdvanceGameMinutes(30.0);
    host.RunForTicks(1);
    REQUIRE(host.Simulation().RegionalSiegeStatus().active);
    host.Simulation().UpdateRegionalSiege(11.0f, false, "village_core");
    REQUIRE(host.Simulation().RegionalSiegeStatus().phase ==
            game::RegionalSiegePhase::Waves);
    const auto raiders = [&] {
        std::vector<ecs::Entity> result;
        host.Simulation()
            .Ecs()
            .Registry()
            .view<game::SettlementRaidZombie>()
            .each([&](const ecs::Entity entity,
                      const game::SettlementRaidZombie& raid) {
                if (raid.settlementId == "village_haven" &&
                    host.Simulation().Ecs().Alive(entity)) {
                    result.push_back(entity);
                }
            });
        return result;
    };
    for (const ecs::Entity raider : raiders()) {
        host.Simulation().Ecs().Destroy(raider);
    }

    const std::uint64_t vehicleId = host.VehicleStableId();
    REQUIRE(vehicleId != 0);
    const ecs::Entity vehicleEntity =
        host.Simulation().Ecs().FindById(vehicleId);
    REQUIRE(static_cast<bool>(vehicleEntity != entt::null));
    auto& registry = host.Simulation().Ecs().Registry();
    const auto* vehiclePosition =
        registry.try_get<ecs::Position>(vehicleEntity);
    REQUIRE(vehiclePosition != nullptr);
    const ecs::Position vehicleStart = *vehiclePosition;
    REQUIRE(TeleportServerPlayer(host, first.PlayerStableId(),
                                 vehicleStart.x - 1.0f, vehicleStart.y));
    REQUIRE_MESSAGE(
        first.SendCommand(MakeVehicleCommand(
                              net::CommandType::EnterVehicle,
                              first.PlayerStableId(), vehicleId),
                          &error),
        error);
    REQUIRE(PumpUntil(host, clients, 5000, [&] {
        return first.PlayerState().vehicle.driving;
    }));

    float worldDirX = 1.0f;
    float worldDirY = 0.0f;
    PickOpenDriveDirection(host, vehicleStart.x, vehicleStart.y, &worldDirX,
                           &worldDirY);
    const float targetX = vehicleStart.x + worldDirX * 3.0f;
    const float targetY = vehicleStart.y + worldDirY * 3.0f;
    const ecs::Entity target =
        host.Simulation().SpawnZombieAt(targetX, targetY);
    REQUIRE(static_cast<bool>(target != entt::null));
    auto* targetPosition = registry.try_get<ecs::Position>(target);
    REQUIRE(targetPosition != nullptr);
    targetPosition->x = targetX;
    targetPosition->y = targetY;
    targetPosition->z = vehicleStart.z;
    auto* targetHealth = registry.try_get<ecs::Health>(target);
    REQUIRE(targetHealth != nullptr);
    targetHealth->max = 10000.0f;
    targetHealth->current = 10000.0f;
    registry.emplace<game::SettlementRaidZombie>(
        target,
        game::SettlementRaidZombie{"village_haven",
                                   game::ContestedFaction::Infected, 0,
                                   targetPosition->x, targetPosition->y, 0.0f,
                                   0.0f, false});

    float screenDirX = 1.0f;
    float screenDirY = 0.0f;
    WorldDirToScreen(worldDirX, worldDirY, &screenDirX, &screenDirY);
    const float healthBefore = targetHealth->current;
    REQUIRE_MESSAGE(first.SendCommand(MakeMove(first.PlayerStableId(),
                                               screenDirX, screenDirY),
                                      &error),
                    error);
    REQUIRE(PumpUntil(host, clients, 5000, [&] {
        return targetHealth->current < healthBefore;
    }));

    const ecs::Entity firstEntity =
        host.Simulation().Ecs().FindById(first.PlayerStableId());
    REQUIRE(static_cast<bool>(firstEntity != entt::null));
    auto* firstProgression =
        registry.try_get<ecs::Progression>(firstEntity);
    REQUIRE(firstProgression != nullptr);
    const float firstBefore = firstProgression->xp;

    for (const ecs::Entity raider : raiders()) {
        host.Simulation().Ecs().Destroy(raider);
    }
    for (int i = 0; i < 300; ++i) {
        for (const ecs::Entity raider : raiders()) {
            host.Simulation().Ecs().Destroy(raider);
        }
        host.Simulation().UpdateRegionalSiege(1.0f, false,
                                              "village_core");
        if (!host.Simulation().RegionalSiegeStatus().active) {
            break;
        }
    }
    REQUIRE_FALSE(host.Simulation().RegionalSiegeStatus().active);
    REQUIRE(host.Simulation().RegionalSiegeLastResult().held);

    host.RunForTicks(1);
    CHECK(firstProgression->xp - firstBefore == doctest::Approx(80.0f));

    first.Close();
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M32-T18 陷阱守城伤害计入贡献") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("siege_trap_reward");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE(!saveId.empty());

    std::vector<std::string> args;
    std::vector<const char*> argv;
    const std::uint64_t contentHash = 0x4D33325431385257ull;
    server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId,
                        &args, &argv, 6);
    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    game::NetworkClient first(
        ClientOptions(host.NetworkPort(), contentHash, "t18-first"));
    REQUIRE(first.Connect(&error));
    std::vector<game::NetworkClient*> clients{&first};
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return first.IsConnected() && first.HasPlayer() &&
               first.HasStrategicState() && host.SessionCount() == 1;
    }));
    REQUIRE(TeleportServerPlayer(host, first.PlayerStableId(), 110.5f,
                                 210.5f));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return HasSettlementGuard(host.Simulation(), "south_farmland");
    }));
    host.RunForTicks(120);

    game::StrategicState* strategic = host.Simulation().Strategic();
    REQUIRE(strategic != nullptr);
    game::StrategicRegionState* farm =
        strategic->FindRegion("south_farmland");
    REQUIRE(farm != nullptr);
    farm->owner = game::StrategicOwner::Human;
    farm->siegePhase = game::StrategicSiegePhase::Warning;
    farm->nextAttackMinute = 0;
    farm->lastEventMinute = 0;
    farm->pressure = 90.0f;

    host.AdvanceGameMinutes(30.0);
    host.RunForTicks(1);
    REQUIRE(host.Simulation().RegionalSiegeStatus().active);
    host.Simulation().UpdateRegionalSiege(11.0f, false, "south_farmland");
    REQUIRE(host.Simulation().RegionalSiegeStatus().phase ==
            game::RegionalSiegePhase::Waves);
    const auto raiders = [&] {
        std::vector<ecs::Entity> result;
        host.Simulation()
            .Ecs()
            .Registry()
            .view<game::SettlementRaidZombie>()
            .each([&](const ecs::Entity entity,
                      const game::SettlementRaidZombie& raid) {
                if (raid.settlementId == "south_farmland" &&
                    host.Simulation().Ecs().Alive(entity)) {
                    result.push_back(entity);
                }
            });
        return result;
    };
    for (const ecs::Entity raider : raiders()) {
        host.Simulation().Ecs().Destroy(raider);
    }

    const ecs::Entity firstEntity =
        host.Simulation().Ecs().FindById(first.PlayerStableId());
    REQUIRE(static_cast<bool>(firstEntity != entt::null));
    auto& registry = host.Simulation().Ecs().Registry();
    const auto* playerPosition =
        registry.try_get<ecs::Position>(firstEntity);
    REQUIRE(playerPosition != nullptr);
    const ecs::Entity target = host.Simulation().SpawnZombieAt(
        playerPosition->x + 1.0f, playerPosition->y);
    REQUIRE(static_cast<bool>(target != entt::null));
    auto* targetPosition = registry.try_get<ecs::Position>(target);
    auto* targetHealth = registry.try_get<ecs::Health>(target);
    REQUIRE(targetPosition != nullptr);
    REQUIRE(targetHealth != nullptr);
    targetPosition->x = playerPosition->x + 1.0f;
    targetPosition->y = playerPosition->y;
    targetPosition->z = playerPosition->z;
    targetHealth->max = 10000.0f;
    targetHealth->current = 10000.0f;
    registry.emplace<game::SettlementRaidZombie>(
        target,
        game::SettlementRaidZombie{"south_farmland",
                                   game::ContestedFaction::Infected, 0,
                                   targetPosition->x, targetPosition->y, 0.0f,
                                   0.0f, false});

    const ecs::Entity trap = host.Simulation().Ecs().Create();
    registry.emplace<ecs::Position>(trap, *targetPosition);
    ecs::SpikeTrap spike;
    spike.armed = true;
    spike.cooldown = 0.0f;
    spike.cooldownSeconds = 0.1f;
    spike.damage = 500.0f;
    spike.radius = 1.0f;
    spike.staggerSeconds = 0.0f;
    spike.uses = 1;
    registry.emplace<ecs::SpikeTrap>(trap, spike);
    registry.emplace<ecs::StructureOwner>(
        trap, ecs::StructureOwner{first.PlayerStableId()});

    const float healthBefore = targetHealth->current;
    host.RunForTicks(1);
    CHECK(registry.get<ecs::Health>(target).current < healthBefore);

    auto* firstProgression =
        registry.try_get<ecs::Progression>(firstEntity);
    REQUIRE(firstProgression != nullptr);
    const float firstBefore = firstProgression->xp;

    for (const ecs::Entity raider : raiders()) {
        host.Simulation().Ecs().Destroy(raider);
    }
    for (int i = 0; i < 300; ++i) {
        for (const ecs::Entity raider : raiders()) {
            host.Simulation().Ecs().Destroy(raider);
        }
        host.Simulation().UpdateRegionalSiege(1.0f, false,
                                              "south_farmland");
        if (!host.Simulation().RegionalSiegeStatus().active) {
            break;
        }
    }
    REQUIRE_FALSE(host.Simulation().RegionalSiegeStatus().active);
    REQUIRE(host.Simulation().RegionalSiegeLastResult().held);

    host.RunForTicks(1);
    CHECK(firstProgression->xp - firstBefore == doctest::Approx(80.0f));

    first.Close();
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M32-T24 守卫最后一击助攻") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("siege_guard_assist");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE(!saveId.empty());

    std::vector<std::string> args;
    std::vector<const char*> argv;
    const std::uint64_t contentHash = 0x4D33325432345257ull;
    server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId,
                        &args, &argv, 6);
    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    game::NetworkClient first(
        ClientOptions(host.NetworkPort(), contentHash, "t24-guard"));
    REQUIRE(first.Connect(&error));
    std::vector<game::NetworkClient*> clients{&first};
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return first.IsConnected() && first.HasPlayer() &&
               first.HasStrategicState() && host.SessionCount() == 1;
    }));
    REQUIRE(TeleportServerPlayer(host, first.PlayerStableId(), 110.5f,
                                 210.5f));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return HasSettlementGuard(host.Simulation(), "south_farmland");
    }));
    host.RunForTicks(120);

    game::StrategicState* strategic = host.Simulation().Strategic();
    REQUIRE(strategic != nullptr);
    game::StrategicRegionState* farm =
        strategic->FindRegion("south_farmland");
    REQUIRE(farm != nullptr);
    farm->owner = game::StrategicOwner::Human;
    farm->siegePhase = game::StrategicSiegePhase::Warning;
    farm->nextAttackMinute = 0;
    farm->lastEventMinute = 0;
    farm->pressure = 90.0f;

    host.AdvanceGameMinutes(30.0);
    host.RunForTicks(1);
    REQUIRE(host.Simulation().RegionalSiegeStatus().active);
    host.Simulation().UpdateRegionalSiege(11.0f, false, "south_farmland");
    REQUIRE(host.Simulation().RegionalSiegeStatus().phase ==
            game::RegionalSiegePhase::Waves);
    const auto raiders = [&] {
        std::vector<ecs::Entity> result;
        host.Simulation()
            .Ecs()
            .Registry()
            .view<game::SettlementRaidZombie>()
            .each([&](const ecs::Entity entity,
                      const game::SettlementRaidZombie& raid) {
                if (raid.settlementId == "south_farmland" &&
                    host.Simulation().Ecs().Alive(entity)) {
                    result.push_back(entity);
                }
            });
        return result;
    };
    for (const ecs::Entity raider : raiders()) {
        host.Simulation().Ecs().Destroy(raider);
    }

    const ecs::Entity firstEntity =
        host.Simulation().Ecs().FindById(first.PlayerStableId());
    REQUIRE(static_cast<bool>(firstEntity != entt::null));
    auto& registry = host.Simulation().Ecs().Registry();
    const auto* playerPosition =
        registry.try_get<ecs::Position>(firstEntity);
    REQUIRE(playerPosition != nullptr);
    const ecs::Entity target = host.Simulation().SpawnZombieAt(
        playerPosition->x + 1.0f, playerPosition->y);
    REQUIRE(static_cast<bool>(target != entt::null));
    auto* targetPosition = registry.try_get<ecs::Position>(target);
    auto* targetHealth = registry.try_get<ecs::Health>(target);
    REQUIRE(targetPosition != nullptr);
    REQUIRE(targetHealth != nullptr);
    targetPosition->x = playerPosition->x + 1.0f;
    targetPosition->y = playerPosition->y;
    targetPosition->z = playerPosition->z;
    targetHealth->max = 10000.0f;
    targetHealth->current = 10000.0f;
    registry.emplace<game::SettlementRaidZombie>(
        target,
        game::SettlementRaidZombie{"south_farmland",
                                   game::ContestedFaction::Infected, 0,
                                   targetPosition->x, targetPosition->y, 0.0f,
                                   0.0f, false});

    const ecs::Entity trap = host.Simulation().Ecs().Create();
    registry.emplace<ecs::Position>(trap, *targetPosition);
    ecs::SpikeTrap spike;
    spike.armed = true;
    spike.cooldown = 0.0f;
    spike.cooldownSeconds = 0.1f;
    spike.damage = 500.0f;
    spike.radius = 1.0f;
    spike.staggerSeconds = 0.0f;
    spike.uses = 1;
    registry.emplace<ecs::SpikeTrap>(trap, spike);
    registry.emplace<ecs::StructureOwner>(
        trap, ecs::StructureOwner{first.PlayerStableId()});
    const float healthBefore = targetHealth->current;
    host.RunForTicks(1);
    REQUIRE(registry.get<ecs::Health>(target).current < healthBefore);

    const ecs::Entity guard =
        FindSettlementGuard(host.Simulation(), "south_farmland");
    REQUIRE(static_cast<bool>(guard != entt::null));
    const auto* guardPosition = registry.try_get<ecs::Position>(guard);
    REQUIRE(guardPosition != nullptr);
    auto& targetPositionNow = registry.get<ecs::Position>(target);
    targetPositionNow.x = guardPosition->x + 0.5f;
    targetPositionNow.y = guardPosition->y;
    auto& raid = registry.get<game::SettlementRaidZombie>(target);
    raid.hasTarget = false;
    raid.retrySeconds = 0.0f;
    raid.attackCooldown = 0.0f;
    raid.targetX = guardPosition->x;
    raid.targetY = guardPosition->y;
    registry.get<ecs::Health>(target).current = 1.0f;
    host.RunForTicks(60);
    const auto* targetNow = registry.try_get<ecs::Position>(target);
    const auto* targetHealthNow = registry.try_get<ecs::Health>(target);
    const auto* guardNow = registry.try_get<ecs::Position>(guard);
    const auto* guardNpc = registry.try_get<game::SettlementNpc>(guard);
    INFO("target hp=", targetHealthNow != nullptr ? targetHealthNow->current
                                                   : -1.0f,
         " target=", targetNow != nullptr ? targetNow->x : -1.0f, ",",
         targetNow != nullptr ? targetNow->y : -1.0f, " guard=",
         guardNow != nullptr ? guardNow->x : -1.0f, ",",
         guardNow != nullptr ? guardNow->y : -1.0f, " activity=",
         guardNpc != nullptr ? static_cast<int>(guardNpc->activity) : -1);
    if (targetHealthNow != nullptr) {
        REQUIRE(targetHealthNow->current <= 0.0f);
    }

    auto* firstProgression =
        registry.try_get<ecs::Progression>(firstEntity);
    REQUIRE(firstProgression != nullptr);
    const float firstBefore = firstProgression->xp;

    for (const ecs::Entity raider : raiders()) {
        host.Simulation().Ecs().Destroy(raider);
    }
    for (int i = 0; i < 300; ++i) {
        for (const ecs::Entity raider : raiders()) {
            host.Simulation().Ecs().Destroy(raider);
        }
        host.Simulation().UpdateRegionalSiege(1.0f, false,
                                              "south_farmland");
        if (!host.Simulation().RegionalSiegeStatus().active) {
            break;
        }
    }
    REQUIRE_FALSE(host.Simulation().RegionalSiegeStatus().active);
    REQUIRE(host.Simulation().RegionalSiegeLastResult().held);

    host.RunForTicks(1);
    CHECK(firstProgression->xp - firstBefore == doctest::Approx(80.0f));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return first.HasPlayerState() &&
               first.PlayerState().status.find("守城战报") !=
                   std::string::npos;
    }));
    CHECK(first.PlayerState().status.find("贡献+51") !=
          std::string::npos);

    first.Close();
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M32-T23 火焰守城伤害计入贡献") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("siege_fire_reward");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE(!saveId.empty());

    std::vector<std::string> args;
    std::vector<const char*> argv;
    const std::uint64_t contentHash = 0x4D33325432335257ull;
    server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId,
                        &args, &argv, 6);
    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    game::NetworkClient first(
        ClientOptions(host.NetworkPort(), contentHash, "t23-first"));
    REQUIRE(first.Connect(&error));
    std::vector<game::NetworkClient*> clients{&first};
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return first.IsConnected() && first.HasPlayer() &&
               first.HasStrategicState() && host.SessionCount() == 1;
    }));
    REQUIRE(TeleportServerPlayer(host, first.PlayerStableId(), 110.5f,
                                 210.5f));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return HasSettlementGuard(host.Simulation(), "south_farmland");
    }));
    host.RunForTicks(120);

    game::StrategicState* strategic = host.Simulation().Strategic();
    REQUIRE(strategic != nullptr);
    game::StrategicRegionState* farm =
        strategic->FindRegion("south_farmland");
    REQUIRE(farm != nullptr);
    farm->owner = game::StrategicOwner::Human;
    farm->siegePhase = game::StrategicSiegePhase::Warning;
    farm->nextAttackMinute = 0;
    farm->lastEventMinute = 0;
    farm->pressure = 90.0f;

    host.AdvanceGameMinutes(30.0);
    host.RunForTicks(1);
    REQUIRE(host.Simulation().RegionalSiegeStatus().active);
    host.Simulation().UpdateRegionalSiege(11.0f, false, "south_farmland");
    REQUIRE(host.Simulation().RegionalSiegeStatus().phase ==
            game::RegionalSiegePhase::Waves);
    const auto raiders = [&] {
        std::vector<ecs::Entity> result;
        host.Simulation()
            .Ecs()
            .Registry()
            .view<game::SettlementRaidZombie>()
            .each([&](const ecs::Entity entity,
                      const game::SettlementRaidZombie& raid) {
                if (raid.settlementId == "south_farmland" &&
                    host.Simulation().Ecs().Alive(entity)) {
                    result.push_back(entity);
                }
            });
        return result;
    };
    for (const ecs::Entity raider : raiders()) {
        host.Simulation().Ecs().Destroy(raider);
    }

    const ecs::Entity firstEntity =
        host.Simulation().Ecs().FindById(first.PlayerStableId());
    REQUIRE(static_cast<bool>(firstEntity != entt::null));
    auto& registry = host.Simulation().Ecs().Registry();
    const auto* playerPosition =
        registry.try_get<ecs::Position>(firstEntity);
    REQUIRE(playerPosition != nullptr);
    const ecs::Entity target = host.Simulation().SpawnZombieAt(
        playerPosition->x + 1.0f, playerPosition->y);
    REQUIRE(static_cast<bool>(target != entt::null));
    auto* targetPosition = registry.try_get<ecs::Position>(target);
    auto* targetHealth = registry.try_get<ecs::Health>(target);
    REQUIRE(targetPosition != nullptr);
    REQUIRE(targetHealth != nullptr);
    targetPosition->x = playerPosition->x + 1.0f;
    targetPosition->y = playerPosition->y;
    targetPosition->z = playerPosition->z;
    targetHealth->max = 10000.0f;
    targetHealth->current = 10000.0f;
    registry.emplace<game::SettlementRaidZombie>(
        target,
        game::SettlementRaidZombie{"south_farmland",
                                   game::ContestedFaction::Infected, 0,
                                   targetPosition->x, targetPosition->y, 0.0f,
                                   0.0f, false});

    ecs::BurningZombie burning;
    burning.ownerStableId = first.PlayerStableId();
    burning.remaining = 10.0f;
    burning.damageAccumulator = 0.0f;
    registry.emplace<ecs::BurningZombie>(target, burning);

    const float healthBefore = targetHealth->current;
    host.RunForTicks(60);
    CHECK(registry.get<ecs::Health>(target).current < healthBefore);

    auto* firstProgression =
        registry.try_get<ecs::Progression>(firstEntity);
    REQUIRE(firstProgression != nullptr);
    const float firstBefore = firstProgression->xp;

    for (const ecs::Entity raider : raiders()) {
        host.Simulation().Ecs().Destroy(raider);
    }
    for (int i = 0; i < 300; ++i) {
        for (const ecs::Entity raider : raiders()) {
            host.Simulation().Ecs().Destroy(raider);
        }
        host.Simulation().UpdateRegionalSiege(1.0f, false,
                                              "south_farmland");
        if (!host.Simulation().RegionalSiegeStatus().active) {
            break;
        }
    }
    REQUIRE_FALSE(host.Simulation().RegionalSiegeStatus().active);
    REQUIRE(host.Simulation().RegionalSiegeLastResult().held);

    host.RunForTicks(1);
    CHECK(firstProgression->xp - firstBefore == doctest::Approx(80.0f));

    first.Close();
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M32-T19 守城贡献战报广播") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("siege_report_broadcast");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE(!saveId.empty());

    std::vector<std::string> args;
    std::vector<const char*> argv;
    const std::uint64_t contentHash = 0x4D33325431395257ull;
    server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId,
                        &args, &argv, 6);
    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    game::NetworkClient first(
        ClientOptions(host.NetworkPort(), contentHash, "t19-first"));
    REQUIRE(first.Connect(&error));
    std::vector<game::NetworkClient*> clients{&first};
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return first.IsConnected() && first.HasPlayer() &&
               first.HasStrategicState() && host.SessionCount() == 1;
    }));
    REQUIRE(TeleportServerPlayer(host, first.PlayerStableId(), 110.5f,
                                 210.5f));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return HasSettlementGuard(host.Simulation(), "south_farmland");
    }));
    host.RunForTicks(120);

    game::StrategicState* strategic = host.Simulation().Strategic();
    REQUIRE(strategic != nullptr);
    game::StrategicRegionState* farm =
        strategic->FindRegion("south_farmland");
    REQUIRE(farm != nullptr);
    farm->owner = game::StrategicOwner::Human;
    farm->siegePhase = game::StrategicSiegePhase::Warning;
    farm->nextAttackMinute = 0;
    farm->lastEventMinute = 0;
    farm->pressure = 90.0f;

    host.AdvanceGameMinutes(30.0);
    host.RunForTicks(1);
    REQUIRE(host.Simulation().RegionalSiegeStatus().active);
    host.Simulation().UpdateRegionalSiege(11.0f, false, "south_farmland");
    REQUIRE(host.Simulation().RegionalSiegeStatus().phase ==
            game::RegionalSiegePhase::Waves);
    const auto raiders = [&] {
        std::vector<ecs::Entity> result;
        host.Simulation()
            .Ecs()
            .Registry()
            .view<game::SettlementRaidZombie>()
            .each([&](const ecs::Entity entity,
                      const game::SettlementRaidZombie& raid) {
                if (raid.settlementId == "south_farmland" &&
                    host.Simulation().Ecs().Alive(entity)) {
                    result.push_back(entity);
                }
            });
        return result;
    };
    for (const ecs::Entity raider : raiders()) {
        host.Simulation().Ecs().Destroy(raider);
    }
    for (int i = 0; i < 300; ++i) {
        for (const ecs::Entity raider : raiders()) {
            host.Simulation().Ecs().Destroy(raider);
        }
        host.Simulation().UpdateRegionalSiege(1.0f, false,
                                              "south_farmland");
        if (!host.Simulation().RegionalSiegeStatus().active) {
            break;
        }
    }
    REQUIRE_FALSE(host.Simulation().RegionalSiegeStatus().active);
    REQUIRE(host.Simulation().RegionalSiegeLastResult().held);

    host.RunForTicks(1);
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return first.HasPlayerState() &&
               first.PlayerState().status.find("守城战报") !=
                   std::string::npos;
    }));
    const std::string report = first.PlayerState().status;
    CHECK(report.find("south_farmland") != std::string::npos);
    CHECK(report.find("1 人在场均分 80 经验") != std::string::npos);

    first.Close();
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M32-T21 守城战报进入聊天记录") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("siege_chat_report");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE(!saveId.empty());

    std::vector<std::string> args;
    std::vector<const char*> argv;
    const std::uint64_t contentHash = 0x4D33325432315257ull;
    server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId,
                        &args, &argv, 6);
    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    game::NetworkClient first(
        ClientOptions(host.NetworkPort(), contentHash, "t21-first"));
    REQUIRE(first.Connect(&error));
    std::vector<game::NetworkClient*> clients{&first};
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return first.IsConnected() && first.HasPlayer() &&
               first.HasStrategicState() && host.SessionCount() == 1;
    }));
    REQUIRE(TeleportServerPlayer(host, first.PlayerStableId(), 110.5f,
                                 210.5f));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return HasSettlementGuard(host.Simulation(), "south_farmland");
    }));
    host.RunForTicks(120);

    game::StrategicState* strategic = host.Simulation().Strategic();
    REQUIRE(strategic != nullptr);
    game::StrategicRegionState* farm =
        strategic->FindRegion("south_farmland");
    REQUIRE(farm != nullptr);
    farm->owner = game::StrategicOwner::Human;
    farm->siegePhase = game::StrategicSiegePhase::Warning;
    farm->nextAttackMinute = 0;
    farm->lastEventMinute = 0;
    farm->pressure = 90.0f;

    host.AdvanceGameMinutes(30.0);
    host.RunForTicks(1);
    REQUIRE(host.Simulation().RegionalSiegeStatus().active);
    host.Simulation().UpdateRegionalSiege(11.0f, false, "south_farmland");
    REQUIRE(host.Simulation().RegionalSiegeStatus().phase ==
            game::RegionalSiegePhase::Waves);
    const auto raiders = [&] {
        std::vector<ecs::Entity> result;
        host.Simulation()
            .Ecs()
            .Registry()
            .view<game::SettlementRaidZombie>()
            .each([&](const ecs::Entity entity,
                      const game::SettlementRaidZombie& raid) {
                if (raid.settlementId == "south_farmland" &&
                    host.Simulation().Ecs().Alive(entity)) {
                    result.push_back(entity);
                }
            });
        return result;
    };
    for (const ecs::Entity raider : raiders()) {
        host.Simulation().Ecs().Destroy(raider);
    }
    for (int i = 0; i < 300; ++i) {
        for (const ecs::Entity raider : raiders()) {
            host.Simulation().Ecs().Destroy(raider);
        }
        host.Simulation().UpdateRegionalSiege(1.0f, false,
                                              "south_farmland");
        if (!host.Simulation().RegionalSiegeStatus().active) {
            break;
        }
    }
    REQUIRE_FALSE(host.Simulation().RegionalSiegeStatus().active);
    REQUIRE(host.Simulation().RegionalSiegeLastResult().held);

    host.RunForTicks(1);
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return first.HasPlayerState() &&
               first.PlayerState().status.find("守城战报") !=
                   std::string::npos;
    }));
    const std::string report = first.PlayerState().status;
    CHECK(report.find("south_farmland") != std::string::npos);
    CHECK(report.find("1 人在场均分 80 经验") != std::string::npos);
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        if (!first.HasSocialState()) {
            return false;
        }
        for (const auto& line : first.SocialState().chat) {
            if (line.sender == "系统" &&
                line.text.find("守城战报") != std::string::npos) {
                return true;
            }
        }
        return false;
    }));
    bool found = false;
    for (const auto& line : first.SocialState().chat) {
        if (line.sender == "系统" &&
            line.text.find("1 人在场均分 80 经验") != std::string::npos) {
            found = true;
        }
    }
    CHECK(found);

    first.Close();
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M32-T7 服务器重启后客户端恢复同一战略状态") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("restart_strategy");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE(!saveId.empty());

    const std::uint64_t contentHash = 0x4D33325437525354ull;
    {
        std::vector<std::string> args;
        std::vector<const char*> argv;
        server::ServerHost::Options options =
            MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId,
                            &args, &argv);
        server::ServerHost host;
        std::string error;
        REQUIRE_MESSAGE(host.Init(options, &error), error);
        game::NetworkClient client(
            ClientOptions(host.NetworkPort(), contentHash, "t7-restart"));
        REQUIRE(client.Connect(&error));
        std::vector<game::NetworkClient*> clients{&client};
        REQUIRE(PumpUntil(host, clients, 10000, [&] {
            return client.HasPlayer() && client.HasStrategicState();
        }));

        game::StrategicState* strategic = host.Simulation().Strategic();
        REQUIRE(strategic != nullptr);
        game::StrategicRegionState* village =
            strategic->FindRegion("village_core");
        REQUIRE(village != nullptr);
        strategic->resources.supplies = 123;
        strategic->resources.ammo = 45;
        village->supply = 88.0f;
        village->fortification = 4;
        REQUIRE(strategic->ApplyResourceDelta(
            "village_core", game::StrategicResources{0, 1, 0, 0},
            "t7.restart.event", 0, &error));

        client.Close();
        game::NetworkClient reconnected(
            ClientOptions(host.NetworkPort(), contentHash, "t7-restart"));
        REQUIRE(reconnected.Connect(&error));
        std::vector<game::NetworkClient*> reconnectClients{&reconnected};
        REQUIRE(PumpUntil(host, reconnectClients, 10000, [&] {
            return reconnected.HasPlayer() && reconnected.HasStrategicState();
        }));
        CHECK(reconnected.StrategicState().supplies == 124);
        reconnected.Close();
        host.Shutdown();
    }

    {
        std::vector<std::string> args;
        std::vector<const char*> argv;
        server::ServerHost::Options options =
            MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId,
                            &args, &argv);
        server::ServerHost restarted;
        std::string error;
        REQUIRE_MESSAGE(restarted.Init(options, &error), error);
        game::NetworkClient client(ClientOptions(
            restarted.NetworkPort(), contentHash, "t7-restart"));
        REQUIRE(client.Connect(&error));
        std::vector<game::NetworkClient*> clients{&client};
        REQUIRE(PumpUntil(restarted, clients, 10000, [&] {
            return client.HasPlayer() && client.HasStrategicState();
        }));
        CHECK(client.StrategicState().supplies == 124);
        CHECK(client.StrategicState().ammo == 45);
        const net::StrategicRegionSnapshot* village =
            FindRegion(client.StrategicState(), "village_core");
        REQUIRE(village != nullptr);
        CHECK(village->supply == 88.0f);
        CHECK(village->fortification == 4);

        client.Close();
        restarted.Shutdown();
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M32-T7 四玩家分散与同区域压力：快照一致且无失败") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("four_strategy");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE(!saveId.empty());

    std::vector<std::string> args;
    std::vector<const char*> argv;
    const std::uint64_t contentHash = 0x4D33325437503450ull;
    server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId,
                        &args, &argv);
    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);

    std::vector<std::unique_ptr<game::NetworkClient>> storage;
    std::vector<game::NetworkClient*> clients;
    for (int i = 0; i < 4; ++i) {
        storage.push_back(std::make_unique<game::NetworkClient>(
            ClientOptions(host.NetworkPort(), contentHash,
                          "t7-four-" + std::to_string(i))));
        clients.push_back(storage.back().get());
        REQUIRE(clients.back()->Connect(&error));
    }
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        for (game::NetworkClient* client : clients) {
            if (!client->IsConnected() || !client->HasPlayer() ||
                !client->HasStrategicState()) {
                return false;
            }
        }
        return host.SessionCount() == 4;
    }));

    const float positions[4][2] = {
        {75.5f, 26.5f}, {75.5f, 26.5f},
        {110.5f, 210.5f}, {300.5f, 280.5f}};
    for (int i = 0; i < 4; ++i) {
        REQUIRE(TeleportServerPlayer(host, clients[static_cast<std::size_t>(i)]
                                               ->PlayerStableId(),
                                     positions[i][0], positions[i][1]));
    }
    REQUIRE(PumpUntil(host, clients, 15000, [&] {
        for (game::NetworkClient* client : clients) {
            if (client->StrategicState().regions.size() != 5 ||
                !client->StrategicState().enabled) {
                return false;
            }
        }
        return true;
    }));
    const int supplies = host.Simulation().Strategic()->resources.supplies;
    for (game::NetworkClient* client : clients) {
        CHECK(client->StrategicState().supplies == supplies);
        CHECK(client->StrategicState().regions.size() == 5);
    }
    CHECK(host.GetNetworkStats().strategicSnapshotFailures == 0);

    for (game::NetworkClient* client : clients) {
        client->Close();
    }
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M32-T7 战略协议编解码、版本拒绝与只读地图转换") {
    net::StrategicSnapshot snapshot;
    snapshot.sequence = 7;
    snapshot.serverTick = 900;
    snapshot.enabled = true;
    snapshot.gameMinute = 1440;
    snapshot.people = 10;
    snapshot.supplies = 120;
    snapshot.ammo = 30;
    snapshot.fuel = 2;

    net::StrategicRegionSnapshot farm;
    farm.id = "south_farmland";
    farm.owner = static_cast<std::uint8_t>(game::StrategicOwner::Human);
    farm.contestedFaction =
        static_cast<std::uint8_t>(game::ContestedFaction::Raiders);
    farm.siegePhase =
        static_cast<std::uint8_t>(game::StrategicSiegePhase::Warning);
    farm.pressure = 70.0f;
    farm.defense = 52.0f;
    farm.supply = 40.0f;
    farm.morale = 60.0f;
    farm.population = 4;
    farm.garrison = 2;
    farm.fortification = 1;
    farm.strategicValue = 3;
    farm.warningValid = true;
    farm.warningMinute = 100;
    farm.attackMinute = 2980;
    farm.reportedAttackMinute = 2960;
    farm.warningMinutes = 2880;
    farm.remainingMinutes = 1540;
    farm.warningIntelLevel = 2;
    farm.enemyStrength = 49;
    farm.reportedEnemyStrength = 52;
    farm.warningDefenseScore = 52;
    snapshot.regions.push_back(farm);

    net::StrategicEventSnapshot event;
    event.id = "warning.south_farmland.100";
    event.regionId = "south_farmland";
    event.kind = static_cast<std::uint8_t>(game::StrategicEventKind::Warning);
    event.ownerAfter = static_cast<std::uint8_t>(game::StrategicOwner::Human);
    event.gameMinute = 100;
    snapshot.events.push_back(event);

    std::string error;
    const std::vector<std::uint8_t> bytes =
        net::EncodeStrategicSnapshot(snapshot, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::StrategicSnapshot decoded;
    REQUIRE_MESSAGE(net::DecodeStrategicSnapshot(bytes, &decoded, &error),
                    error);
    CHECK(decoded.sequence == 7);
    CHECK(decoded.serverTick == 900);
    CHECK(decoded.enabled);
    CHECK(decoded.regions.size() == 1);
    CHECK(decoded.regions[0].id == "south_farmland");
    CHECK(decoded.regions[0].contestedFaction ==
          static_cast<std::uint8_t>(game::ContestedFaction::Raiders));
    CHECK(decoded.regions[0].warningValid);
    CHECK(decoded.events.size() == 1);

    game::StrategicMapState map =
        game::BuildStrategicMapStateFromSnapshot(decoded);
    REQUIRE(map.FindRegion("south_farmland") != nullptr);
    CHECK(map.regions[0].warning.reportedEnemyStrength == 52);
    CHECK(map.regions[0].contestedFaction ==
          game::ContestedFaction::Raiders);

    std::vector<std::uint8_t> badVersion = bytes;
    REQUIRE(badVersion.size() > 5);
    badVersion[4] = 3;
    net::StrategicSnapshot rejected;
    CHECK_FALSE(
        net::DecodeStrategicSnapshot(badVersion, &rejected, &error));

    net::StrategicRequest request;
    request.sequence = 3;
    request.action = net::StrategicRequestAction::DonateSupply;
    request.regionId = "village_core";
    request.amount = 10;
    const std::vector<std::uint8_t> requestBytes =
        net::EncodeStrategicRequest(request, &error);
    REQUIRE_MESSAGE(!requestBytes.empty(), error);
    net::StrategicRequest decodedRequest;
    REQUIRE_MESSAGE(
        net::DecodeStrategicRequest(requestBytes, &decodedRequest, &error),
        error);
    CHECK(decodedRequest.action ==
          net::StrategicRequestAction::DonateSupply);
    CHECK(decodedRequest.amount == 10);

    net::StrategicRequestResult result;
    result.sequence = 3;
    result.action = net::StrategicRequestAction::DonateSupply;
    result.regionId = "village_core";
    result.accepted = true;
    result.reason = "ok";
    const std::vector<std::uint8_t> resultBytes =
        net::EncodeStrategicResult(result, &error);
    REQUIRE_MESSAGE(!resultBytes.empty(), error);
    net::StrategicRequestResult decodedResult;
    REQUIRE_MESSAGE(
        net::DecodeStrategicResult(resultBytes, &decodedResult, &error),
        error);
    CHECK(decodedResult.accepted);
    CHECK(decodedResult.regionId == "village_core");
}
