#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/Homestead.h"
#include "game/Missions.h"
#include "game/NetworkClient.h"
#include "game/SettlementNpc.h"
#include "game/SettlementReputation.h"
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
    CHECK(state->settlementId == "village_haven");

    registry.get<ecs::Container>(player).items.push_back(
        ecs::ItemInstance{"bandage", 2, 1.0f});
    REQUIRE(PumpUntil(host, client, 7000, [&] {
        const ecs::MissionRunState* run =
            game::FindMissionState(*log, "settlement_help");
        if (run == nullptr ||
            run->status != ecs::MissionRunStatus::Completed) {
            return false;
        }
        for (const net::PlayerSettlementReputation& entry :
             client.PlayerState().settlementReputation) {
            if (entry.id == "village_haven" && entry.score == 10) {
                return true;
            }
        }
        return false;
    }));
    CHECK(game::SettlementReputationScoreFor(world, player, "village_haven") ==
          10);

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
                static_cast<std::uint8_t>(ecs::MissionRunStatus::Completed)) {
            missionRestored = true;
        }
    }
    CHECK(missionRestored);
    bool reputationRestored = false;
    for (const net::PlayerSettlementReputation& entry :
         restoredClient.PlayerState().settlementReputation) {
        if (entry.id == "village_haven" && entry.score == 10) {
            reputationRestored = true;
        }
    }
    CHECK(reputationRestored);
    const ecs::Entity restoredPlayer =
        restoredHost.Simulation().Ecs().FindById(
            restoredClient.PlayerStableId());
    REQUIRE(restoredPlayer != ecs::EcsWorld::kNull);
    CHECK(game::SettlementReputationScoreFor(
              restoredHost.Simulation().Ecs(), restoredPlayer,
              "village_haven") == 10);

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
    CHECK(secondState->settlementId == "village_haven");
    CHECK(countRuns(*firstLog, "settlement_help") == 1u);

    const auto clientReputation = [](const game::NetworkClient& client,
                                     std::string_view id) {
        for (const net::PlayerSettlementReputation& entry :
             client.PlayerState().settlementReputation) {
            if (entry.id == id) {
                return entry.score;
            }
        }
        return 0;
    };
    registry.get<ecs::Container>(firstPlayer).items.push_back(
        ecs::ItemInstance{"bandage", 2, 1.0f});
    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        const auto* log = registry.try_get<ecs::MissionLog>(firstPlayer);
        const ecs::MissionRunState* run =
            log != nullptr ? game::FindMissionState(*log, "settlement_help")
                           : nullptr;
        return run != nullptr &&
               run->status == ecs::MissionRunStatus::Completed &&
               clientReputation(first, "village_haven") == 10;
    }));
    CHECK(clientReputation(second, "village_haven") == 0);
    CHECK(game::SettlementReputationScoreFor(host.Simulation().Ecs(),
                                             secondPlayer,
                                             "village_haven") == 0);

    registry.get<ecs::Container>(secondPlayer).items.push_back(
        ecs::ItemInstance{"bandage", 2, 1.0f});
    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        const auto* log = registry.try_get<ecs::MissionLog>(secondPlayer);
        const ecs::MissionRunState* run =
            log != nullptr ? game::FindMissionState(*log, "settlement_help")
                           : nullptr;
        return run != nullptr &&
               run->status == ecs::MissionRunStatus::Completed &&
               clientReputation(second, "village_haven") == 10;
    }));
    CHECK(clientReputation(first, "village_haven") == 10);

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

TEST_CASE("M33-F1-T3 服务器交易按商人 StableId 路由并拒绝错误 NPC 类型") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("routing");
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
    options.contentHash = 0x46315433524F5554ull;
    options.enablePlayerProfiles = true;
    options.playerProfilesFile = profilePath;
    options.loadSaveId = saveId;
    options.argc = static_cast<int>(argv.size());
    options.argv = argv.data();

    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);

    game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = options.contentHash;
    clientOptions.autoReconnect = false;
    clientOptions.playerIdentity = "f1t3-routing";
    clientOptions.playerName = "Routing";
    game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 7000, [&] {
        return client.IsConnected() && client.HasPlayer() &&
               client.HasPlayerState();
    }));

    ecs::EcsWorld& world = host.Simulation().Ecs();
    entt::registry& registry = world.Registry();
    ecs::Entity villageMerchant = ecs::EcsWorld::kNull;
    ecs::Entity requester = ecs::EcsWorld::kNull;
    REQUIRE(PumpUntil(host, client, 7000, [&] {
        registry.view<game::SettlementNpc>().each(
            [&](const ecs::Entity entity, const game::SettlementNpc& npc) {
                if (npc.settlementId != "village_haven") {
                    return;
                }
                if (npc.kind == game::SettlementNpcKind::Merchant) {
                    villageMerchant = entity;
                } else if (npc.kind == game::SettlementNpcKind::Requester) {
                    requester = entity;
                }
            });
        return villageMerchant != ecs::EcsWorld::kNull &&
               requester != ecs::EcsWorld::kNull;
    }));
    const auto* villagePosition =
        registry.try_get<ecs::Position>(villageMerchant);
    REQUIRE(villagePosition != nullptr);
    const auto* requesterPosition = registry.try_get<ecs::Position>(requester);
    REQUIRE(requesterPosition != nullptr);

    const ecs::Entity player = world.FindById(client.PlayerStableId());
    REQUIRE(player != ecs::EcsWorld::kNull);
    auto teleport = [&](float x, float y) {
        auto* position = registry.try_get<ecs::Position>(player);
        REQUIRE(position != nullptr);
        position->x = x;
        position->y = y;
        position->z = villagePosition->z;
        if (auto* body = registry.try_get<ecs::RigidBody>(player);
            body != nullptr && body->body != nullptr) {
            host.Simulation().Physics().TeleportBody(body->body, x, y,
                                                     position->z);
        }
    };
    teleport(villagePosition->x + 1.0f, villagePosition->y);

    const ecs::Entity northMerchant = game::SpawnMerchant(
        world, villagePosition->x + 1.5f, villagePosition->y, 0);
    REQUIRE(northMerchant != ecs::EcsWorld::kNull);
    game::ConfigureSettlementNpc(world, northMerchant, "north_fields",
                                  game::SettlementNpcKind::Merchant,
                                  villagePosition->x + 1.5f,
                                  villagePosition->y, 0.5f);
    auto* northStock =
        registry.try_get<ecs::MerchantStock>(northMerchant);
    auto* villageStock =
        registry.try_get<ecs::MerchantStock>(villageMerchant);
    REQUIRE(northStock != nullptr);
    REQUIRE(villageStock != nullptr);
    northStock->stock = {3, 3, 3, 3, 3};
    northStock->restockDay = 9999;
    villageStock->stock = {4, 2, 1, 2, 1};
    villageStock->restockDay = 9999;

    registry.get<ecs::Container>(player).items.push_back(
        ecs::ItemInstance{"wood", 6, 1.0f});
    const auto trade = [&](ecs::Entity target, int offerIndex = 0) {
        net::Command command;
        command.type = net::CommandType::TradeOffer;
        command.playerStableId = client.PlayerStableId();
        command.payload.entityId = world.IdOf(target).id;
        command.payload.index = offerIndex;
        REQUIRE(client.SendCommand(command, &error));
    };

    const auto reputationScore = [&](std::string_view id) {
        return game::SettlementReputationScoreFor(world, player, id, 0);
    };
    const auto networkReputation = [&](std::string_view id) {
        for (const net::PlayerSettlementReputation& entry :
             client.PlayerState().settlementReputation) {
            if (entry.id == id) {
                return entry.score;
            }
        }
        return 0;
    };

    trade(villageMerchant);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return villageStock->stock[0] == 3 && reputationScore("village_haven") == 1;
    }));
    CHECK(northStock->stock[0] == 3);
    CHECK(networkReputation("village_haven") == 1);
    trade(villageMerchant);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return villageStock->stock[0] == 2;
    }));
    CHECK(reputationScore("village_haven") == 1);
    CHECK(networkReputation("village_haven") == 1);
    trade(northMerchant);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return northStock->stock[0] == 2 &&
               reputationScore("north_fields") == 1;
    }));
    CHECK(villageStock->stock[0] == 2);
    CHECK(networkReputation("north_fields") == 1);

    auto& requesterStock =
        registry.emplace_or_replace<ecs::MerchantStock>(requester);
    requesterStock.stock = {4, 1, 1, 2, 1};
    requesterStock.restockDay = 9999;
    teleport(requesterPosition->x + 1.0f, requesterPosition->y);
    const std::uint64_t rejectedBefore =
        host.GetNetworkStats().commandsRejected;
    trade(requester);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return host.GetNetworkStats().commandsRejected > rejectedBefore &&
               client.HasPlayerState() &&
               client.PlayerState().status.find("不是商人") !=
                   std::string::npos;
    }));
    CHECK(requesterStock.stock[0] == 4);
    CHECK(villageStock->stock[0] == 2);
    CHECK(northStock->stock[0] == 2);
    CHECK(reputationScore("village_haven") == 1);
    CHECK(reputationScore("north_fields") == 1);

    teleport(villagePosition->x + 1.0f, villagePosition->y);
    const auto setReputation = [&](std::string_view id, int score) {
        auto& reputation =
            registry.get_or_emplace<game::SettlementReputation>(player);
        for (game::SettlementReputationEntry& entry :
             reputation.settlements) {
            if (entry.settlementId == id) {
                entry.score = score;
                return;
            }
        }
        reputation.settlements.push_back(
            game::SettlementReputationEntry{std::string(id), score});
    };
    setReputation("village_haven", 60);
    villageStock->stock[4] = 1;
    registry.get<ecs::Container>(player).items.push_back(
        ecs::ItemInstance{"wood", 8, 1.0f});
    trade(villageMerchant, 4);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return villageStock->stock[4] == 0 &&
               items::CountOf(registry.get<ecs::Container>(player), "wood") ==
                   2;
    }));
    CHECK(reputationScore("village_haven") == 60);

    setReputation("village_haven", -60);
    villageStock->stock[0] = 4;
    const std::uint64_t hostileRejectedBefore =
        host.GetNetworkStats().commandsRejected;
    trade(villageMerchant, 0);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return host.GetNetworkStats().commandsRejected >
                   hostileRejectedBefore &&
               client.HasPlayerState() &&
               client.PlayerState().status.find("敌视") !=
                   std::string::npos;
    }));
    CHECK(villageStock->stock[0] == 4);
    CHECK(items::CountOf(registry.get<ecs::Container>(player), "wood") == 2);

    client.Close();
    host.Shutdown();
}

TEST_CASE("M33-F1-T7 双客户端跨聚落交易任务声望与重启恢复") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("f1t7_cross");
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
    options.contentHash = 0x4631543754524145ull;
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
    firstOptions.playerIdentity = "f1t7-village";
    firstOptions.playerName = "Village";
    game::NetworkClient first(firstOptions);

    game::NetworkClient::Options secondOptions = firstOptions;
    secondOptions.playerIdentity = "f1t7-north";
    secondOptions.playerName = "North";
    game::NetworkClient second(secondOptions);

    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayer() && second.HasPlayer() &&
               first.HasPlayerState() && second.HasPlayerState();
    }));

    ecs::EcsWorld& world = host.Simulation().Ecs();
    entt::registry& registry = world.Registry();
    const auto findNpc = [&](std::string_view settlementId,
                             game::SettlementNpcKind kind) {
        ecs::Entity found = ecs::EcsWorld::kNull;
        registry.view<game::SettlementNpc>().each(
            [&](const ecs::Entity entity, const game::SettlementNpc& npc) {
                if (npc.settlementId == settlementId && npc.kind == kind) {
                    found = entity;
                }
            });
        return found;
    };
    ecs::Entity villageMerchant = ecs::EcsWorld::kNull;
    ecs::Entity villageRequester = ecs::EcsWorld::kNull;
    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        villageMerchant = findNpc("village_haven",
                                  game::SettlementNpcKind::Merchant);
        villageRequester = findNpc("village_haven",
                                   game::SettlementNpcKind::Requester);
        return villageMerchant != ecs::EcsWorld::kNull &&
               villageRequester != ecs::EcsWorld::kNull;
    }));

    const ecs::Entity firstPlayer = world.FindById(first.PlayerStableId());
    const ecs::Entity secondPlayer = world.FindById(second.PlayerStableId());
    REQUIRE(firstPlayer != ecs::EcsWorld::kNull);
    REQUIRE(secondPlayer != ecs::EcsWorld::kNull);
    auto teleport = [&](const ecs::Entity player, float x, float y,
                        int z) {
        auto* position = registry.try_get<ecs::Position>(player);
        REQUIRE(position != nullptr);
        position->x = x;
        position->y = y;
        position->z = z;
        if (auto* body = registry.try_get<ecs::RigidBody>(player);
            body != nullptr && body->body != nullptr) {
            host.Simulation().Physics().TeleportBody(body->body, x, y, z);
        }
    };

    teleport(secondPlayer, 150.0f, 64.0f, 0);
    ecs::Entity northMerchant = ecs::EcsWorld::kNull;
    ecs::Entity northRequester = ecs::EcsWorld::kNull;
    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        northMerchant = findNpc("north_fields",
                                game::SettlementNpcKind::Merchant);
        northRequester = findNpc("north_fields",
                                 game::SettlementNpcKind::Requester);
        return northMerchant != ecs::EcsWorld::kNull &&
               northRequester != ecs::EcsWorld::kNull;
    }));

    const auto* villageMerchantPosition =
        registry.try_get<ecs::Position>(villageMerchant);
    const auto* northMerchantPosition =
        registry.try_get<ecs::Position>(northMerchant);
    REQUIRE(villageMerchantPosition != nullptr);
    REQUIRE(northMerchantPosition != nullptr);
    teleport(firstPlayer, villageMerchantPosition->x + 1.0f,
             villageMerchantPosition->y, villageMerchantPosition->z);
    teleport(secondPlayer, northMerchantPosition->x + 1.0f,
             northMerchantPosition->y, northMerchantPosition->z);

    auto& firstBag = registry.get<ecs::Container>(firstPlayer);
    auto& secondBag = registry.get<ecs::Container>(secondPlayer);
    firstBag.items.push_back(ecs::ItemInstance{"wood", 4, 1.0f});
    secondBag.items.push_back(ecs::ItemInstance{"wood", 4, 1.0f});

    auto* villageStock = registry.try_get<ecs::MerchantStock>(villageMerchant);
    auto* northStock = registry.try_get<ecs::MerchantStock>(northMerchant);
    REQUIRE(villageStock != nullptr);
    REQUIRE(northStock != nullptr);
    villageStock->stock = {4, 2, 1, 2, 1};
    villageStock->restockDay = 9999;
    northStock->stock = {3, 1, 0, 2, 0};
    northStock->restockDay = 9999;

    const auto sendTrade = [&](game::NetworkClient& client,
                               game::NetworkClient& other,
                               ecs::Entity merchant,
                               ecs::MerchantStock* expectedStock,
                               int expectedRemaining) {
        net::Command command;
        command.type = net::CommandType::TradeOffer;
        command.playerStableId = client.PlayerStableId();
        command.payload.entityId = world.IdOf(merchant).id;
        command.payload.index = 1;
        REQUIRE(client.SendCommand(command, &error));
        REQUIRE(PumpUntil(host, client, other, 7000, [&] {
            return expectedStock->stock[1] == expectedRemaining;
        }));
    };
    const auto clientReputation = [](const game::NetworkClient& client,
                                     std::string_view id) {
        for (const net::PlayerSettlementReputation& entry :
             client.PlayerState().settlementReputation) {
            if (entry.id == id) {
                return entry.score;
            }
        }
        return 0;
    };

    villageStock->stock = {4, 2, 1, 2, 1};
    sendTrade(first, second, villageMerchant, villageStock, 1);
    northStock->stock = {3, 1, 0, 2, 0};
    sendTrade(second, first, northMerchant, northStock, 0);
    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        return clientReputation(first, "village_haven") == 1 &&
               clientReputation(second, "north_fields") == 1;
    }));
    CHECK(clientReputation(first, "north_fields") == 0);
    CHECK(clientReputation(second, "village_haven") == 0);
    CHECK(villageStock->stock[1] == 1);
    CHECK(northStock->stock[1] == 0);

    const auto* villageRequesterPosition =
        registry.try_get<ecs::Position>(villageRequester);
    const auto* northRequesterPosition =
        registry.try_get<ecs::Position>(northRequester);
    REQUIRE(villageRequesterPosition != nullptr);
    REQUIRE(northRequesterPosition != nullptr);
    teleport(firstPlayer, villageRequesterPosition->x + 0.8f,
             villageRequesterPosition->y, villageRequesterPosition->z);
    teleport(secondPlayer, northRequesterPosition->x + 0.8f,
             northRequesterPosition->y, northRequesterPosition->z);

    net::Command firstAccept;
    firstAccept.type = net::CommandType::AcceptSettlementMission;
    firstAccept.payload.entityId = world.IdOf(villageRequester).id;
    REQUIRE(first.SendCommand(firstAccept, &error));
    net::Command secondAccept = firstAccept;
    secondAccept.payload.entityId = world.IdOf(northRequester).id;
    REQUIRE(second.SendCommand(secondAccept, &error));
    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        const auto hasActive = [](const game::NetworkClient& client,
                                  std::string_view id) {
            for (const net::PlayerMissionProgress& mission :
                 client.PlayerState().missions) {
                if (mission.id == id &&
                    mission.status == static_cast<std::uint8_t>(
                                          ecs::MissionRunStatus::Active)) {
                    return true;
                }
            }
            return false;
        };
        return hasActive(first, "settlement_help") &&
               hasActive(second, "north_blood_drive");
    }));

    firstBag.items.push_back(ecs::ItemInstance{"bandage", 2, 1.0f});
    secondBag.items.push_back(ecs::ItemInstance{"bandage", 3, 1.0f});
    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        const auto completed = [](const game::NetworkClient& client,
                                  std::string_view id) {
            for (const net::PlayerMissionProgress& mission :
                 client.PlayerState().missions) {
                if (mission.id == id &&
                    mission.status == static_cast<std::uint8_t>(
                                          ecs::MissionRunStatus::Completed)) {
                    return true;
                }
            }
            return false;
        };
        return completed(first, "settlement_help") &&
               completed(second, "north_blood_drive") &&
               clientReputation(first, "village_haven") == 11 &&
               clientReputation(second, "north_fields") == 11;
    }));
    CHECK(clientReputation(first, "north_fields") == 0);
    CHECK(clientReputation(second, "village_haven") == 0);
    CHECK(game::SettlementReputationScoreFor(world, firstPlayer,
                                             "village_haven") == 11);
    CHECK(game::SettlementReputationScoreFor(world, secondPlayer,
                                             "north_fields") == 11);

    first.Close();
    second.Close();
    host.Shutdown();

    server::ServerHost restoredHost;
    REQUIRE_MESSAGE(restoredHost.Init(options, &error), error);
    firstOptions.port = restoredHost.NetworkPort();
    game::NetworkClient restoredFirst(firstOptions);
    secondOptions.port = restoredHost.NetworkPort();
    game::NetworkClient restoredSecond(secondOptions);
    REQUIRE(restoredFirst.Connect(&error));
    REQUIRE(restoredSecond.Connect(&error));
    REQUIRE(PumpUntil(restoredHost, restoredFirst, restoredSecond, 7000, [&] {
        return restoredFirst.IsConnected() && restoredSecond.IsConnected() &&
               restoredFirst.HasPlayer() && restoredSecond.HasPlayer() &&
               restoredFirst.HasPlayerState() &&
               restoredSecond.HasPlayerState();
    }));
    REQUIRE(PumpUntil(restoredHost, restoredFirst, restoredSecond, 7000, [&] {
        return clientReputation(restoredFirst, "village_haven") == 11 &&
               clientReputation(restoredSecond, "north_fields") == 11;
    }));
    CHECK(clientReputation(restoredFirst, "north_fields") == 0);
    CHECK(clientReputation(restoredSecond, "village_haven") == 0);
    const auto missionCompleted = [](const game::NetworkClient& client,
                                     std::string_view id) {
        for (const net::PlayerMissionProgress& mission :
             client.PlayerState().missions) {
            if (mission.id == id &&
                mission.status == static_cast<std::uint8_t>(
                                      ecs::MissionRunStatus::Completed)) {
                return true;
            }
        }
        return false;
    };
    CHECK(missionCompleted(restoredFirst, "settlement_help"));
    CHECK(missionCompleted(restoredSecond, "north_blood_drive"));

    ecs::Entity restoredVillageMerchant = ecs::EcsWorld::kNull;
    ecs::Entity restoredNorthMerchant = ecs::EcsWorld::kNull;
    const auto findRestoredNpc = [&](std::string_view settlementId,
                                     game::SettlementNpcKind kind) {
        ecs::Entity found = ecs::EcsWorld::kNull;
        restoredHost.Simulation().Ecs().Registry()
            .view<game::SettlementNpc>()
            .each([&](const ecs::Entity entity,
                      const game::SettlementNpc& npc) {
                if (npc.settlementId == settlementId && npc.kind == kind) {
                    found = entity;
                }
            });
        return found;
    };
    REQUIRE(PumpUntil(restoredHost, restoredFirst, restoredSecond, 7000, [&] {
        restoredVillageMerchant = findRestoredNpc(
            "village_haven", game::SettlementNpcKind::Merchant);
        restoredNorthMerchant = findRestoredNpc(
            "north_fields", game::SettlementNpcKind::Merchant);
        return restoredVillageMerchant != ecs::EcsWorld::kNull &&
               restoredNorthMerchant != ecs::EcsWorld::kNull;
    }));
    CHECK(restoredHost.Simulation()
              .Ecs()
              .Registry()
              .get<ecs::MerchantStock>(restoredVillageMerchant)
              .stock[1] == 1);
    CHECK(restoredHost.Simulation()
              .Ecs()
              .Registry()
              .get<ecs::MerchantStock>(restoredNorthMerchant)
              .stock[1] == 0);

    restoredFirst.Close();
    restoredSecond.Close();
    restoredHost.Shutdown();
}
