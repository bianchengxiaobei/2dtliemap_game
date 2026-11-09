#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/Missions.h"
#include "game/NetworkClient.h"
#include "game/SettlementNpc.h"
#include "game/WorldSimulation.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "net/Command.h"
#include "physics/PhysicsWorld.h"
#include "save/EntityStore.h"
#include "save/SaveManager.h"
#include "save/SaveTypes.h"
#include "server/ServerHost.h"

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
bool PumpUntil(server::ServerHost& host, game::NetworkClient& client,
               int timeoutMs, Predicate predicate) {
    const Clock::time_point deadline =
        Clock::now() + std::chrono::milliseconds(timeoutMs);
    Clock::time_point nextTick = Clock::now();
    while (Clock::now() < deadline) {
        if (Clock::now() >= nextTick) {
            host.Tick(1.0f / 60.0f);
            nextTick += std::chrono::milliseconds(16);
        }
        client.Poll();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

template <typename Predicate>
bool PumpUntil(server::ServerHost& host, game::NetworkClient& first,
               game::NetworkClient& second, int timeoutMs,
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

void CreateEmptySave(const std::filesystem::path& saveRoot,
                     std::string* saveId) {
    save::EntitySnapshot empty;
    save::SaveManager::Options options;
    options.root = saveRoot;
    options.gameVersion = save::kDefaultGameVersion;
    save::SaveManager manager(options);
    save::SaveManager::CreateOptions create;
    create.name = "M23-F4-T6";
    create.mapId = "village_demo";
    create.gameVersion = save::kDefaultGameVersion;
    create.entities = &empty;
    std::string error;
    REQUIRE_MESSAGE(manager.Create(create, saveId, &error), error);
}

std::filesystem::path TempDir(const char* name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zomboidlike_f4t6" / name;
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir, error);
    return dir;
}

}  // namespace

TEST_CASE("M23-F4-T6 联机聚落：NPC 复制与服务器权威委托接取") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("mission_accept");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, &saveId);
    REQUIRE_FALSE(saveId.empty());
    const std::vector<std::string> args{
        "ZomboidLikeTests", "--save.root=" + saveRoot.string()};
    std::vector<const char*> argv;
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }

    server::ServerHost::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.spawnDemoPopulation = false;
    options.spawnLocalPlayer = false;
    options.network = true;
    options.port = 0;
    options.contentHash = 0x4634543653455454ull;
    options.enablePlayerProfiles = true;
    options.playerProfilesFile = profilePath;
    options.loadSaveId = saveId;
    options.argc = static_cast<int>(argv.size());
    options.argv = argv.data();

    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkEnabled());

    game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = options.contentHash;
    clientOptions.autoReconnect = false;
    clientOptions.playerIdentity = "f4t6-settlement";
    clientOptions.playerName = "Settlement";

    game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 7000, [&] {
        return client.IsConnected() && client.HasPlayer() &&
               client.HasPlayerState();
    }));

    ecs::EcsWorld& world = host.Simulation().Ecs();
    entt::registry& registry = world.Registry();
    ecs::Entity requester = entt::null;
    REQUIRE(PumpUntil(host, client, 7000, [&] {
        registry.view<game::SettlementNpc>().each(
            [&](const ecs::Entity entity, const game::SettlementNpc& npc) {
                if (npc.kind == game::SettlementNpcKind::Requester &&
                    npc.settlementId == "village_haven") {
                    requester = entity;
                }
            });
        return requester != ecs::EcsWorld::kNull;
    }));

    const ecs::Entity player =
        world.FindById(client.PlayerStableId());
    REQUIRE(player != ecs::EcsWorld::kNull);
    const auto* requesterPosition = registry.try_get<ecs::Position>(requester);
    auto* playerPosition = registry.try_get<ecs::Position>(player);
    REQUIRE(requesterPosition != nullptr);
    REQUIRE(playerPosition != nullptr);
    playerPosition->x = requesterPosition->x + 1.0f;
    playerPosition->y = requesterPosition->y;
    playerPosition->z = requesterPosition->z;
    registry.get_or_emplace<ecs::Velocity>(player) = ecs::Velocity{};
    if (const auto* rigid = registry.try_get<ecs::RigidBody>(player);
        rigid != nullptr && rigid->body != nullptr) {
        host.Simulation().Physics().TeleportBody(
            rigid->body, requesterPosition->x + 1.0f, requesterPosition->y,
            requesterPosition->z);
    }

    const std::uint64_t requesterId = world.IdOf(requester).id;
    REQUIRE(PumpUntil(host, client, 7000, [&] {
        const game::ReplicatedActor* actor =
            client.Replication().Find(requesterId);
        return actor != nullptr && actor->hasSettlementNpc;
    }));

    const game::ReplicatedActor* requesterActor =
        client.Replication().Find(requesterId);
    REQUIRE(requesterActor != nullptr);
    REQUIRE(requesterActor->hasSettlementNpc);
    CHECK(requesterActor->settlementNpc.kind ==
          game::SettlementNpcKind::Requester);
    CHECK(requesterActor->settlementNpc.settlementId == "village_haven");

    net::Command command;
    command.type = net::CommandType::AcceptSettlementMission;
    command.payload.entityId = requesterId;
    REQUIRE(client.SendCommand(command, &error));

    REQUIRE(PumpUntil(host, client, 7000, [&] {
        if (!client.HasPlayerState()) {
            return false;
        }
        for (const net::PlayerMissionProgress& mission :
             client.PlayerState().missions) {
            if (mission.id == "settlement_help" &&
                mission.status ==
                    static_cast<std::uint8_t>(
                        ecs::MissionRunStatus::Active)) {
                return true;
            }
        }
        return false;
    }));

    const auto* log = registry.try_get<ecs::MissionLog>(player);
    REQUIRE(log != nullptr);
    const ecs::MissionRunState* state =
        game::FindMissionState(*log, "settlement_help");
    REQUIRE(state != nullptr);
    CHECK(state->status == ecs::MissionRunStatus::Active);
    CHECK(state->rewardClaimed == false);

    bool foundSettlement = false;
    for (const net::PlayerSettlementProgress& settlement :
         client.PlayerState().settlements) {
        if (settlement.id == "village_haven") {
            foundSettlement = true;
            CHECK_FALSE(settlement.fallen);
            CHECK(settlement.guardHealthMax == doctest::Approx(120.0f));
        }
    }
    CHECK(foundSettlement);

    ecs::Entity guard = ecs::EcsWorld::kNull;
    registry.view<game::SettlementNpc, ecs::Health>().each(
        [&](const ecs::Entity entity, const game::SettlementNpc& npc,
            const ecs::Health&) {
            if (npc.kind == game::SettlementNpcKind::Guard &&
                npc.settlementId == "village_haven") {
                guard = entity;
            }
        });
    REQUIRE(guard != ecs::EcsWorld::kNull);
    registry.get<ecs::Health>(guard).current = 61.0f;
    ecs::Entity merchant = ecs::EcsWorld::kNull;
    registry.view<ecs::MerchantStock>().each(
        [&](const ecs::Entity entity, ecs::MerchantStock& stock) {
            merchant = entity;
            stock.stock = {0, 1, 1, 1, 1};
            stock.restockDay = 2;
        });
    REQUIRE(merchant != ecs::EcsWorld::kNull);

    host.Shutdown();
    client.Close();

    server::ServerHost restoredHost;
    REQUIRE_MESSAGE(restoredHost.Init(options, &error), error);
    clientOptions.port = restoredHost.NetworkPort();
    game::NetworkClient restoredClient(clientOptions);
    REQUIRE(restoredClient.Connect(&error));
    REQUIRE(PumpUntil(restoredHost, restoredClient, 7000, [&] {
        return restoredClient.IsConnected() && restoredClient.HasPlayer() &&
               restoredClient.HasPlayerState();
    }));
    REQUIRE(PumpUntil(restoredHost, restoredClient, 7000, [&] {
        bool foundGuard = false;
        restoredHost.Simulation().Ecs().Registry()
            .view<game::SettlementNpc, ecs::Health>()
            .each([&](const ecs::Entity, const game::SettlementNpc& npc,
                      const ecs::Health&) {
                if (npc.kind == game::SettlementNpcKind::Guard &&
                    npc.settlementId == "village_haven") {
                    foundGuard = true;
                }
            });
        return foundGuard;
    }));
    const ecs::Entity restoredGuardEntity =
        [&]() -> ecs::Entity {
        ecs::Entity found = ecs::EcsWorld::kNull;
        restoredHost.Simulation().Ecs().Registry()
            .view<game::SettlementNpc, ecs::Health>()
            .each([&](const ecs::Entity entity,
                      const game::SettlementNpc& npc, const ecs::Health&) {
                if (npc.kind == game::SettlementNpcKind::Guard &&
                    npc.settlementId == "village_haven") {
                    found = entity;
                }
            });
        return found;
    }();
    REQUIRE(restoredGuardEntity != ecs::EcsWorld::kNull);
    CHECK(restoredHost.Simulation()
              .Ecs()
              .Registry()
              .get<ecs::Health>(restoredGuardEntity)
              .current == doctest::Approx(61.0f));
    bool stockRestored = false;
    restoredHost.Simulation().Ecs().Registry().view<ecs::MerchantStock>().each(
        [&](const ecs::Entity, const ecs::MerchantStock& stock) {
            stockRestored = stock.stock[0] == 0 && stock.stock[1] == 1 &&
                            stock.stock[2] == 1 && stock.stock[3] == 1 &&
                            stock.stock[4] == 1;
        });
    CHECK(stockRestored);

    bool missionRestored = false;
    for (const net::PlayerMissionProgress& mission :
         restoredClient.PlayerState().missions) {
        if (mission.id == "settlement_help" &&
            mission.status ==
                static_cast<std::uint8_t>(ecs::MissionRunStatus::Active)) {
            missionRestored = true;
        }
    }
    CHECK(missionRestored);

    restoredClient.Close();
    restoredHost.Shutdown();
}


TEST_CASE("M23-F4-T7 双客户端聚落复制与重复委托拒绝") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("two_clients");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, &saveId);
    REQUIRE_FALSE(saveId.empty());
    const std::vector<std::string> args{
        "ZomboidLikeTests", "--save.root=" + saveRoot.string()};
    std::vector<const char*> argv;
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }

    server::ServerHost::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.spawnDemoPopulation = false;
    options.spawnLocalPlayer = false;
    options.network = true;
    options.port = 0;
    options.contentHash = 0x4634543754574F43ull;
    options.enablePlayerProfiles = true;
    options.playerProfilesFile = profilePath;
    options.loadSaveId = saveId;
    options.argc = static_cast<int>(argv.size());
    options.argv = argv.data();

    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);

    game::NetworkClient::Options firstOptions;
    firstOptions.host = "127.0.0.1";
    firstOptions.port = host.NetworkPort();
    firstOptions.contentHash = options.contentHash;
    firstOptions.autoReconnect = false;
    firstOptions.playerIdentity = "f4t7-first";
    firstOptions.playerName = "First";
    game::NetworkClient first(firstOptions);

    game::NetworkClient::Options secondOptions = firstOptions;
    secondOptions.playerIdentity = "f4t7-second";
    secondOptions.playerName = "Second";
    game::NetworkClient second(secondOptions);

    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayer() && second.HasPlayer() &&
               first.HasPlayerState() && second.HasPlayerState();
    }));

    entt::registry& registry = host.Simulation().Ecs().Registry();
    ecs::Entity requester = ecs::EcsWorld::kNull;
    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        registry.view<game::SettlementNpc>().each(
            [&](const ecs::Entity entity, const game::SettlementNpc& npc) {
                if (npc.kind == game::SettlementNpcKind::Requester &&
                    npc.settlementId == "village_haven") {
                    requester = entity;
                }
            });
        return requester != ecs::EcsWorld::kNull;
    }));

    const auto* requesterPosition =
        registry.try_get<ecs::Position>(requester);
    REQUIRE(requesterPosition != nullptr);
    auto teleport = [&](std::uint64_t stableId, float x, float y) {
        const ecs::Entity entity =
            host.Simulation().Ecs().FindById(stableId);
        REQUIRE(entity != ecs::EcsWorld::kNull);
        auto* position = registry.try_get<ecs::Position>(entity);
        REQUIRE(position != nullptr);
        position->x = x;
        position->y = y;
        position->z = requesterPosition->z;
        if (auto* body = registry.try_get<ecs::RigidBody>(entity);
            body != nullptr && body->body != nullptr) {
            host.Simulation().Physics().TeleportBody(body->body, x, y,
                                                     position->z);
        }
    };
    teleport(first.PlayerStableId(), requesterPosition->x + 1.0f,
             requesterPosition->y);
    teleport(second.PlayerStableId(), requesterPosition->x + 1.6f,
             requesterPosition->y);

    const std::uint64_t requesterId =
        host.Simulation().Ecs().IdOf(requester).id;
    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        const game::ReplicatedActor* firstActor =
            first.Replication().Find(requesterId);
        const game::ReplicatedActor* secondActor =
            second.Replication().Find(requesterId);
        return firstActor != nullptr && firstActor->hasSettlementNpc &&
               secondActor != nullptr && secondActor->hasSettlementNpc;
    }));
    CHECK(first.Replication().Find(requesterId)->settlementNpc.settlementId ==
          "village_haven");
    CHECK(second.Replication().Find(requesterId)->settlementNpc.kind ==
          game::SettlementNpcKind::Requester);

    const ecs::Entity firstPlayer =
        host.Simulation().Ecs().FindById(first.PlayerStableId());
    const ecs::Entity secondPlayer =
        host.Simulation().Ecs().FindById(second.PlayerStableId());
    REQUIRE(firstPlayer != ecs::EcsWorld::kNull);
    REQUIRE(secondPlayer != ecs::EcsWorld::kNull);

    net::Command command;
    command.type = net::CommandType::AcceptSettlementMission;
    command.payload.entityId = requesterId;
    const std::uint64_t rejectedBefore =
        host.GetNetworkStats().commandsRejected;
    REQUIRE(first.SendCommand(command, &error));
    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        for (const net::PlayerMissionProgress& mission :
             first.PlayerState().missions) {
            if (mission.id == "settlement_help" &&
                mission.status == static_cast<std::uint8_t>(
                                       ecs::MissionRunStatus::Active)) {
                return true;
            }
        }
        return false;
    }));

    REQUIRE(first.SendCommand(command, &error));
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return host.GetNetworkStats().commandsRejected > rejectedBefore;
    }));
    const auto* firstLog = registry.try_get<ecs::MissionLog>(firstPlayer);
    REQUIRE(firstLog != nullptr);
    const auto countRuns = [](const ecs::MissionLog& log,
                              std::string_view id) {
        std::size_t count = 0;
        for (const ecs::MissionRunState& run : log.missions) {
            count += run.id == id ? 1u : 0u;
        }
        return count;
    };
    CHECK(countRuns(*firstLog, "settlement_help") == 1u);

    REQUIRE(second.SendCommand(command, &error));
    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        for (const net::PlayerMissionProgress& mission :
             second.PlayerState().missions) {
            if (mission.id == "settlement_help" &&
                mission.status == static_cast<std::uint8_t>(
                                       ecs::MissionRunStatus::Active)) {
                return true;
            }
        }
        return false;
    }));
    const auto* secondLog = registry.try_get<ecs::MissionLog>(secondPlayer);
    REQUIRE(secondLog != nullptr);
    CHECK(countRuns(*secondLog, "settlement_help") == 1u);
    const ecs::MissionRunState* secondState =
        game::FindMissionState(*secondLog, "settlement_help");
    REQUIRE(secondState != nullptr);
    CHECK(secondState->status == ecs::MissionRunStatus::Active);
    CHECK(countRuns(*firstLog, "settlement_help") == 1u);

    for (const game::NetworkClient* client : {&first, &second}) {
        bool found = false;
        for (const net::PlayerSettlementProgress& settlement :
             client->PlayerState().settlements) {
            if (settlement.id == "village_haven") {
                found = true;
                CHECK_FALSE(settlement.fallen);
                CHECK(settlement.guardHealthMax == doctest::Approx(120.0f));
            }
        }
        CHECK(found);
    }

    ecs::Entity merchant = ecs::EcsWorld::kNull;
    registry.view<ecs::MerchantStock, game::SettlementNpc>().each(
        [&](const ecs::Entity entity, ecs::MerchantStock&,
            const game::SettlementNpc& npc) {
            if (npc.settlementId == "village_haven") {
                merchant = entity;
            }
        });
    REQUIRE(merchant != ecs::EcsWorld::kNull);
    const auto* merchantPosition = registry.try_get<ecs::Position>(merchant);
    REQUIRE(merchantPosition != nullptr);
    teleport(first.PlayerStableId(), merchantPosition->x + 1.0f,
             merchantPosition->y);
    auto& firstBag = registry.get<ecs::Container>(firstPlayer);
    firstBag.items.push_back(ecs::ItemInstance{"wood", 2, 1.0f});
    const int stockBefore = registry.get<ecs::MerchantStock>(merchant).stock[0];

    game::SettlementState fallen;
    game::SettlementStateEntry entry;
    entry.id = "village_haven";
    entry.fallen = true;
    entry.guardHealth = 0.0f;
    fallen.settlements.push_back(entry);
    host.Simulation().SettlementNpcs()->ApplyState(fallen);

    const std::uint64_t tradeRejectedBefore =
        host.GetNetworkStats().commandsRejected;
    net::Command trade;
    trade.type = net::CommandType::TradeOffer;
    trade.playerStableId = first.PlayerStableId();
    trade.payload.entityId = host.Simulation().Ecs().IdOf(merchant).id;
    trade.payload.index = 0;
    REQUIRE(first.SendCommand(trade, &error));
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return host.GetNetworkStats().commandsRejected > tradeRejectedBefore;
    }));
    CHECK(registry.get<ecs::MerchantStock>(merchant).stock[0] == stockBefore);
    CHECK(items::CountOf(firstBag, "wood") == 2);
    CHECK(first.PlayerState().status.find("无法交易") != std::string::npos);

    second.Close();
    first.Close();
    host.Shutdown();
}
