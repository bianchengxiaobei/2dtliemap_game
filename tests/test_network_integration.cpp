#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

#include "characters/components/CharacterComponents.h"
#include "core/Log.h"
#include "ecs/components/CommonComponents.h"
#include "game/NetworkClient.h"
#include "game/WorldSimulation.h"
#include "items/ItemComponents.h"
#include "net/Command.h"
#include "server/ServerHost.h"

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
bool PumpUntil(zl::server::ServerHost& host, zl::game::NetworkClient& first,
               zl::game::NetworkClient& second, int timeoutMs, Predicate predicate) {
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

template <typename Predicate>
bool PumpUntil(zl::server::ServerHost& host, zl::game::NetworkClient& client,
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

zl::net::Command MakeMove(std::uint64_t playerId, float x, float y) {
    zl::net::Command command;
    command.type = zl::net::CommandType::Move;
    command.playerStableId = playerId;
    command.payload.x = x;
    command.payload.y = y;
    return command;
}

}  // namespace

TEST_CASE("M13-T5 双客户端：移动/战斗广播、离开清理、重连恢复") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;  // 系统分配临时端口
    hostOptions.contentHash = 0x5A4C544E455431ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkEnabled());
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;
    clientOptions.localPredictionSpeed = 4.0f;

    zl::game::NetworkClient first(clientOptions);
    zl::game::NetworkClient second(clientOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));

    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        return first.IsConnected() && second.IsConnected() && first.HasPlayer() &&
               second.HasPlayer() && host.SessionCount() == 2;
    }));
    const std::uint64_t firstId = first.PlayerStableId();
    const std::uint64_t secondId = second.PlayerStableId();
    REQUIRE(firstId != 0);
    REQUIRE(secondId != 0);
    CHECK(firstId != secondId);

    // 双方都拿到含对方玩家的首个/后续快照。
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        return first.Replication().Find(secondId) != nullptr &&
               second.Replication().Find(firstId) != nullptr;
    }));

    const zl::ecs::Entity serverFirst =
        host.Simulation().Ecs().FindById(firstId);
    const zl::ecs::Entity serverSecond =
        host.Simulation().Ecs().FindById(secondId);
    REQUIRE(static_cast<bool>(serverFirst != entt::null));
    REQUIRE(static_cast<bool>(serverSecond != entt::null));
    const float firstStartX =
        host.Simulation().Ecs().Registry().get<zl::ecs::Position>(serverFirst).x;

    // 客户端 A 提交移动；服务器模拟移动，B 的快照里看到 A 的新位置。
    float secondSeesFirstBefore = 0.0f;
    if (const auto* actor = second.Replication().Find(firstId);
        actor != nullptr && actor->hasPosition) {
        secondSeesFirstBefore = actor->position.x;
    }
    REQUIRE(first.SendCommand(MakeMove(firstId, 1.0f, 0.0f), &error));
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        const auto& registry = host.Simulation().Ecs().Registry();
        const auto& position = registry.get<zl::ecs::Position>(serverFirst);
        const auto* actor = second.Replication().Find(firstId);
        return position.x > firstStartX + 0.25f && actor != nullptr &&
               actor->hasPosition && actor->position.x > secondSeesFirstBefore + 0.1f;
    }));

    REQUIRE(first.SendCommand(MakeMove(firstId, 0.0f, 0.0f), &error));
    REQUIRE(PumpUntil(host, first, second, 1000, [&] {
        const auto& velocity =
            host.Simulation().Ecs().Registry().get<zl::ecs::Velocity>(serverFirst);
        return std::abs(velocity.x) < 0.01f && std::abs(velocity.y) < 0.01f;
    }));

    // M13-T10：服务器权威装备/动作经 ZLR1 复制到另一个客户端。
    auto& firstEquipment =
        host.Simulation().Ecs().Registry().get<zl::ecs::Equipment>(serverFirst);
    firstEquipment.slots[0] = zl::ecs::ItemInstance{"axe", 1, 0.75f};
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        const auto* actor = second.Replication().Find(firstId);
        return actor != nullptr && actor->hasEquipment &&
               actor->equipment[0].has_value() &&
               actor->equipment[0]->defId == "axe";
    }));

    // 服务器生成近身目标；A 攻击后服务器扣血，B 能收到 Health 变化。
    const auto& serverRegistry = host.Simulation().Ecs().Registry();
    const auto& firstPosition =
        serverRegistry.get<zl::ecs::Position>(serverFirst);
    const zl::ecs::Entity zombie = host.Simulation().SpawnZombieAt(
        firstPosition.x + 1.0f, firstPosition.y);
    REQUIRE(static_cast<bool>(zombie != entt::null));
    const std::uint64_t zombieId = host.Simulation().Ecs().IdOf(zombie).id;
    REQUIRE(zombieId != 0);
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        return second.Replication().Find(zombieId) != nullptr;
    }));

    zl::net::Command attack;
    attack.type = zl::net::CommandType::Attack;
    attack.playerStableId = firstId;
    attack.payload.entityId = zombieId;
    REQUIRE(first.SendCommand(attack, &error));
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        const auto& health =
            host.Simulation().Ecs().Registry().get<zl::ecs::Health>(zombie);
        const auto* actor = second.Replication().Find(zombieId);
        return health.current < health.max - 1.0f && actor != nullptr &&
               actor->hasHealth && actor->health.current < actor->health.max - 1.0f;
    }));

    REQUIRE(PumpUntil(host, first, second, 2000, [&] {
        const auto* actor = second.Replication().Find(firstId);
        return actor != nullptr && actor->hasAction &&
               (actor->action.action == zl::ecs::ActorAction::Attacking ||
                actor->action.action == zl::ecs::ActorAction::Hit);
    }));

    // B 主动离开：服务器销毁其权威实体，A 收到 removal。
    second.Close();
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        return host.SessionCount() == 1 && first.Replication().Find(secondId) == nullptr;
    }));

    // 新客户端重连：获得新 StableId，A 看到新玩家，新客户端看到 A。
    zl::game::NetworkClient reconnected(clientOptions);
    REQUIRE(reconnected.Connect(&error));
    REQUIRE(PumpUntil(host, first, reconnected, 6000, [&] {
        return reconnected.IsConnected() && reconnected.HasPlayer() &&
               host.SessionCount() == 2;
    }));
    const std::uint64_t reconnectedId = reconnected.PlayerStableId();
    REQUIRE(reconnectedId != 0);
    CHECK(reconnectedId != secondId);
    REQUIRE(PumpUntil(host, first, reconnected, 5000, [&] {
        return first.Replication().Find(reconnectedId) != nullptr &&
               reconnected.Replication().Find(firstId) != nullptr;
    }));

    reconnected.Close();
    first.Close();
    host.Shutdown();
    CHECK_FALSE(host.Running());
}

TEST_CASE("M13-T6 延迟命令、服务器重启与自动重连") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x544E4553543632ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);
    const std::uint16_t port = host.NetworkPort();

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = port;
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = true;
    clientOptions.reconnectDelayMs = 50;
    clientOptions.handshakeTimeoutMs = 300;
    clientOptions.heartbeatIntervalMs = 100;
    clientOptions.heartbeatTimeoutMs = 400;

    zl::game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.IsConnected() && client.HasPlayer();
    }));
    const std::uint64_t playerId = client.PlayerStableId();
    REQUIRE(playerId != 0);
    const zl::ecs::Entity player = host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));
    const float startX = host.Simulation().Ecs().Registry().get<zl::ecs::Position>(player).x;

    // 服务器延迟 150ms 才处理命令：命令应留在 KCP 队列并在恢复 tick 后生效。
    REQUIRE(client.SendCommand(MakeMove(playerId, 1.0f, 0.0f), &error));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    host.RunForTicks(12);
    CHECK(host.Simulation().Ecs().Registry().get<zl::ecs::Position>(player).x >
          startX + 0.02f);
    client.Poll();

    // 停掉服务器并在同一端口重启；客户端应自动重连并重新获得玩家分配。
    host.Shutdown();
    zl::server::ServerHost restarted;
    hostOptions.port = port;
    REQUIRE_MESSAGE(restarted.Init(hostOptions, &error), error);
    REQUIRE(PumpUntil(restarted, client, 8000, [&] {
        return client.IsConnected() && client.HasPlayer() &&
               restarted.SessionCount() == 1;
    }));
    CHECK(client.GetStats().connectAttempts >= 2);
    CHECK(client.GetStats().assignments >= 2);
    CHECK(client.PlayerStableId() != 0);

    restarted.Shutdown();
    client.Close();
}
