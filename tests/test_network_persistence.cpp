#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>

#include "characters/components/CharacterComponents.h"
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

std::filesystem::path TempProfileDir(const char* name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zomboidlike_t8_network" / name;
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir, error);
    return dir;
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

std::uint64_t SpawnItem(zl::game::WorldSimulation& simulation, const char* defId,
                        int count, float x, float y) {
    auto& world = simulation.Ecs();
    entt::registry& registry = world.Registry();
    const zl::ecs::Entity item = world.Create();
    registry.emplace<zl::ecs::Position>(item, zl::ecs::Position{x, y, 0});
    registry.emplace<zl::ecs::ItemInstance>(
        item, zl::ecs::ItemInstance{defId, count, 1.0f});
    registry.emplace<zl::ecs::SpriteRef>(
        item, zl::ecs::SpriteRef{zl::ecs::HashName("item.test"), -1, 0xFFFFFFFFu});
    return world.IdOf(item).id;
}

int BagCount(const zl::net::PlayerState& state, const std::string& defId) {
    int count = 0;
    for (const zl::net::PlayerStateItem& item : state.bag) {
        if (item.defId == defId) {
            count += item.count;
        }
    }
    return count;
}

zl::server::ServerHost::Options MakeHostOptions(const std::string& profilePath,
                                                std::uint64_t contentHash,
                                                float respawnSeconds) {
    zl::server::ServerHost::Options options;
    options.configPath = (std::filesystem::path{ZL_SOURCE_DIR} / "config" /
                          "game.json")
                             .string();
    options.spawnDemoPopulation = false;
    options.spawnLocalPlayer = false;
    options.network = true;
    options.port = 0;
    options.contentHash = contentHash;
    options.enablePlayerProfiles = true;
    options.playerProfilesFile = profilePath;
    options.respawnDelaySeconds = respawnSeconds;
    return options;
}

}  // namespace

TEST_CASE("M13-T8 联机持久化：同一 identity 重连复用 StableId 与背包") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);
    const std::filesystem::path dir = TempProfileDir("reconnect");
    const std::string profilePath = (dir / "players.bin").string();
    const std::uint64_t contentHash = 0x5045525349535431ull;

    std::string error;
    zl::server::ServerHost host;
    REQUIRE_MESSAGE(host.Init(MakeHostOptions(profilePath, contentHash, 1.0f), &error),
                    error);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = contentHash;
    clientOptions.autoReconnect = false;
    clientOptions.playerIdentity = "persist-alice";
    clientOptions.playerName = "Alice";

    zl::game::NetworkClient first(clientOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(PumpUntil(host, first, 6000, [&] {
        return first.IsConnected() && first.HasPlayer() && first.HasPlayerState();
    }));
    const std::uint64_t stableId = first.PlayerStableId();
    REQUIRE(stableId != 0);
    const zl::ecs::Entity player = host.Simulation().Ecs().FindById(stableId);
    REQUIRE(static_cast<bool>(player != entt::null));
    const auto& position =
        host.Simulation().Ecs().Registry().get<zl::ecs::Position>(player);
    const std::uint64_t itemId =
        SpawnItem(host.Simulation(), "bandage", 2, position.x + 0.2f, position.y);
    zl::net::Command pickup;
    pickup.type = zl::net::CommandType::PickupEntity;
    pickup.playerStableId = stableId;
    pickup.payload.entityId = itemId;
    REQUIRE(first.SendCommand(pickup, &error));
    REQUIRE(PumpUntil(host, first, 5000, [&] {
        return BagCount(first.PlayerState(), "bandage") == 2;
    }));

    const std::uint64_t axeId =
        SpawnItem(host.Simulation(), "axe", 1, position.x + 0.2f, position.y);
    pickup.payload.entityId = axeId;
    REQUIRE(first.SendCommand(pickup, &error));
    REQUIRE(PumpUntil(host, first, 5000, [&] {
        return BagCount(first.PlayerState(), "axe") == 1;
    }));
    zl::net::Command equip;
    equip.type = zl::net::CommandType::EquipBag;
    equip.playerStableId = stableId;
    equip.payload.index = 1;
    equip.payload.targetIndex = 0;
    REQUIRE(first.SendCommand(equip, &error));
    REQUIRE(PumpUntil(host, first, 5000, [&] {
        return first.PlayerState().equipment[0].present &&
               first.PlayerState().equipment[0].item.defId == "axe";
    }));

    const float savedX = host.Simulation()
                             .Ecs()
                             .Registry()
                             .get<zl::ecs::Position>(player)
                             .x;

    first.Close();
    REQUIRE(PumpUntil(host, first, 4000, [&] { return host.SessionCount() == 0; }));
    CHECK(std::filesystem::exists(profilePath));

    zl::game::NetworkClient second(clientOptions);
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, second, 6000, [&] {
        return second.IsConnected() && second.HasPlayer() && second.HasPlayerState();
    }));
    CHECK(second.PlayerStableId() == stableId);
    CHECK(BagCount(second.PlayerState(), "bandage") == 2);
    CHECK(second.PlayerState().equipment[0].present);
    CHECK(second.PlayerState().equipment[0].item.defId == "axe");
    const zl::ecs::Entity restored = host.Simulation().Ecs().FindById(stableId);
    REQUIRE(static_cast<bool>(restored != entt::null));
    CHECK(host.Simulation().Ecs().Registry().get<zl::ecs::Position>(restored).x ==
          doctest::Approx(savedX).epsilon(0.01));

    second.Close();
    REQUIRE(PumpUntil(host, second, 4000, [&] { return host.SessionCount() == 0; }));
    host.Shutdown();

    // 服务器进程重启后仍从同一档案恢复身份与物品。
    zl::server::ServerHost restarted;
    REQUIRE_MESSAGE(
        restarted.Init(MakeHostOptions(profilePath, contentHash, 1.0f), &error), error);
    zl::game::NetworkClient::Options restartedOptions = clientOptions;
    restartedOptions.port = restarted.NetworkPort();
    zl::game::NetworkClient fourth(restartedOptions);
    REQUIRE(fourth.Connect(&error));
    REQUIRE(PumpUntil(restarted, fourth, 6000, [&] {
        return fourth.IsConnected() && fourth.HasPlayer() && fourth.HasPlayerState();
    }));
    CHECK(fourth.PlayerStableId() == stableId);
    CHECK(BagCount(fourth.PlayerState(), "bandage") == 2);
    CHECK(fourth.PlayerState().equipment[0].present);
    CHECK(fourth.PlayerState().equipment[0].item.defId == "axe");

    fourth.Close();
    restarted.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M13-T8 联机持久化：同身份新连接接管旧会话") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);
    const std::filesystem::path dir = TempProfileDir("takeover");
    const std::string profilePath = (dir / "players.bin").string();
    const std::uint64_t contentHash = 0x5045525349544B33ull;

    std::string error;
    zl::server::ServerHost host;
    REQUIRE_MESSAGE(host.Init(MakeHostOptions(profilePath, contentHash, 1.0f), &error),
                    error);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = contentHash;
    clientOptions.autoReconnect = false;
    clientOptions.playerIdentity = "persist-takeover";
    clientOptions.playerName = "Takeover";

    zl::game::NetworkClient first(clientOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(PumpUntil(host, first, 6000, [&] {
        return first.IsConnected() && first.HasPlayer() && first.HasPlayerState();
    }));
    const std::uint64_t stableId = first.PlayerStableId();

    // 旧会话尚未超时时，同身份的新连接应接管档案，而不是被拒绝或分配新 ID。
    zl::game::NetworkClient replacement(clientOptions);
    REQUIRE(replacement.Connect(&error));
    REQUIRE(PumpUntil(host, replacement, 6000, [&] {
        first.Poll();
        return replacement.IsConnected() && replacement.HasPlayer() &&
               replacement.HasPlayerState() && replacement.PlayerStableId() == stableId;
    }));
    CHECK(host.SessionCount() == 1);
    CHECK(replacement.PlayerStableId() == stableId);
    CHECK_FALSE(first.IsConnected());

    replacement.Close();
    first.Close();
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}
TEST_CASE("M13-T8 联机持久化：死亡后定时复活并保留 StableId/背包") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);
    const std::filesystem::path dir = TempProfileDir("death");
    const std::string profilePath = (dir / "players.bin").string();
    const std::uint64_t contentHash = 0x5045525349544832ull;

    std::string error;
    zl::server::ServerHost host;
    REQUIRE_MESSAGE(host.Init(MakeHostOptions(profilePath, contentHash, 2.0f), &error),
                    error);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = contentHash;
    clientOptions.autoReconnect = false;
    clientOptions.playerIdentity = "persist-dead";
    clientOptions.playerName = "Dead";

    zl::game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.IsConnected() && client.HasPlayer() && client.HasPlayerState();
    }));
    const std::uint64_t stableId = client.PlayerStableId();
    const zl::ecs::Entity player = host.Simulation().Ecs().FindById(stableId);
    REQUIRE(static_cast<bool>(player != entt::null));
    const auto& position =
        host.Simulation().Ecs().Registry().get<zl::ecs::Position>(player);
    const std::uint64_t itemId =
        SpawnItem(host.Simulation(), "bandage", 1, position.x + 0.2f, position.y);
    zl::net::Command pickup;
    pickup.type = zl::net::CommandType::PickupEntity;
    pickup.playerStableId = stableId;
    pickup.payload.entityId = itemId;
    REQUIRE(client.SendCommand(pickup, &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return BagCount(client.PlayerState(), "bandage") == 1;
    }));

    auto& health = host.Simulation().Ecs().Registry().get<zl::ecs::Health>(player);
    health.current = 0.0f;
    host.RunForTicks(2);
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        return client.HasPlayerState() && !client.PlayerState().alive;
    }));
    CHECK(client.PlayerState().health <= 0.0f);

    // 死亡状态下断开并重连：服务器仍复用同一 StableId，并继续剩余复活倒计时。
    client.Close();
    REQUIRE(PumpUntil(host, client, 3000, [&] { return host.SessionCount() == 0; }));
    zl::game::NetworkClient reconnected(clientOptions);
    REQUIRE(reconnected.Connect(&error));
    REQUIRE(PumpUntil(host, reconnected, 6000, [&] {
        return reconnected.IsConnected() && reconnected.HasPlayer() &&
               reconnected.HasPlayerState();
    }));
    CHECK(reconnected.PlayerStableId() == stableId);
    CHECK_FALSE(reconnected.PlayerState().alive);

    REQUIRE(PumpUntil(host, reconnected, 8000, [&] {
        if (!reconnected.HasPlayerState() || !reconnected.PlayerState().alive) {
            return false;
        }
        const zl::ecs::Entity respawned =
            host.Simulation().Ecs().FindById(reconnected.PlayerStableId());
        return static_cast<bool>(respawned != entt::null) &&
               BagCount(reconnected.PlayerState(), "bandage") == 1;
    }));
    CHECK(reconnected.PlayerStableId() == stableId);
    CHECK(reconnected.PlayerState().health > 0.0f);
    CHECK(reconnected.PlayerState().health ==
          doctest::Approx(reconnected.PlayerState().healthMax));
    CHECK(host.GetNetworkStats().playerDeaths >= 1);
    CHECK(host.GetNetworkStats().playerRespawns >= 1);

    reconnected.Close();
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}
