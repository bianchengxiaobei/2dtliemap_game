#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "core/Config.h"
#include "core/SoundEvent.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/EntityReplication.h"
#include "game/NetworkClient.h"
#include "game/WorldQueries.h"
#include "game/WorldSimulation.h"
#include "items/ItemComponents.h"
#include "net/Command.h"
#include "net/WorldSound.h"
#include "physics/PhysicsWorld.h"
#include "server/ServerHost.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"
#include "world/World.h"

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

std::filesystem::path TempProfileDir(const char* name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zomboidlike_m34_t5" / name;
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir, error);
    return dir;
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
    host.RunForTicks(1);
    return true;
}

void SetServerPlayerHealth(zl::server::ServerHost& host,
                           std::uint64_t stableId, float value) {
    const zl::ecs::Entity player = host.Simulation().Ecs().FindById(stableId);
    REQUIRE(static_cast<bool>(player != entt::null));
    auto& registry = host.Simulation().Ecs().Registry();
    auto* health = registry.try_get<zl::ecs::Health>(player);
    REQUIRE(health != nullptr);
    health->current = value;
    health->max = value;
}

int ServerBagCount(zl::server::ServerHost& host, std::uint64_t stableId,
                   std::string_view defId) {
    const zl::ecs::Entity player = host.Simulation().Ecs().FindById(stableId);
    if (player == entt::null) {
        return -1;
    }
    const auto& registry = host.Simulation().Ecs().Registry();
    const auto* container = registry.try_get<zl::ecs::Container>(player);
    if (container == nullptr) {
        return -1;
    }
    int count = 0;
    for (const zl::ecs::ItemInstance& item : container->items) {
        if (item.defId == defId) {
            count += item.count;
        }
    }
    return count;
}

void ReplaceServerBag(zl::server::ServerHost& host, std::uint64_t stableId,
                      std::initializer_list<zl::ecs::ItemInstance> items) {
    const zl::ecs::Entity player = host.Simulation().Ecs().FindById(stableId);
    REQUIRE(static_cast<bool>(player != entt::null));
    auto& registry = host.Simulation().Ecs().Registry();
    auto& container = registry.get<zl::ecs::Container>(player);
    container.items.assign(items.begin(), items.end());
}

void SetWoodFloor(zl::server::ServerHost& host, int minX, int minY, int maxX,
                  int maxY) {
    zl::world::World& world = host.Simulation().World();
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const zl::world::ChunkCoord coord = zl::world::ChunkOf(x, y);
            zl::world::Chunk* chunk =
                zl::game::FindChunkInWorld(world, coord.cx, coord.cy);
            REQUIRE(chunk != nullptr);
            REQUIRE(chunk->State() == zl::world::ChunkState::Loaded);
            const zl::world::SquareLocal local =
                zl::world::SquareLocalOf(x, y);
            zl::world::GridSquare& square =
                chunk->Square(local.lx, local.ly, 0);
            square.ClearLayer(zl::world::SquareLayer::Furniture);
            square.ClearLayer(zl::world::SquareLayer::Wall);
            square.ClearLayer(zl::world::SquareLayer::Item);
            zl::world::SquareObject floor;
            floor.sprite = "tile.floor.wood";
            square.SetFloor(std::move(floor));
        }
    }
}


zl::ecs::Entity SpawnThrowTarget(zl::server::ServerHost& host, float x,
                                 float y) {
    zl::ecs::Entity target = host.Simulation().Ecs().Create();
    REQUIRE(static_cast<bool>(target != entt::null));
    auto& registry = host.Simulation().Ecs().Registry();
    registry.emplace<zl::ecs::Position>(target, zl::ecs::Position{x, y, 0});
    registry.emplace<zl::ecs::Health>(target, zl::ecs::Health{10000.0f, 10000.0f});
    registry.emplace<zl::ecs::SpriteRef>(
        target, zl::ecs::SpriteRef{zl::ecs::HashName("item.test"), -1,
                                   0xFFFFFFFFu});
    return target;
}

zl::net::Command MakeThrowCommand(std::uint64_t playerId, int index, float x,
                                  float y) {
    zl::net::Command command;
    command.type = zl::net::CommandType::ThrowItem;
    command.playerStableId = playerId;
    command.payload.index = index;
    command.payload.x = x;
    command.payload.y = y;
    return command;
}

int CountServerGroundItems(zl::server::ServerHost& host, std::string_view defId,
                           float x, float y, float radius) {
    const float radiusSq = radius * radius;
    const auto& registry = host.Simulation().Ecs().Registry();
    int count = 0;
    registry.view<zl::ecs::Position, zl::ecs::ItemInstance>().each(
        [&](const zl::ecs::Entity entity, const zl::ecs::Position& position,
            const zl::ecs::ItemInstance& item) {
            if (!host.Simulation().Ecs().Alive(entity) ||
                item.defId != defId || position.z != 0) {
                return;
            }
            const float dx = position.x - x;
            const float dy = position.y - y;
            if (dx * dx + dy * dy <= radiusSq) {
                ++count;
            }
        });
    return count;
}

bool ClientSeesGroundItem(zl::game::NetworkClient& client,
                          std::string_view defId) {
    for (const auto& [stableId, actor] : client.Replication().Actors()) {
        static_cast<void>(stableId);
        if (actor.hasItem && actor.item.defId == defId &&
            actor.archetype == zl::game::kReplicationArchetypeItem) {
            return true;
        }
    }
    return false;
}

bool ClientSeesFire(zl::game::NetworkClient& client) {
    for (const auto& [stableId, actor] : client.Replication().Actors()) {
        static_cast<void>(stableId);
        if (actor.hasFire) {
            return true;
        }
    }
    return false;
}

bool ClientSeesWorldSoundFrom(zl::game::NetworkClient& client,
                              std::uint64_t playerId) {
    const std::vector<zl::net::WorldSoundEvent> sounds =
        client.TakeWorldSounds();
    for (const zl::net::WorldSoundEvent& sound : sounds) {
        if (sound.sourcePlayerStableId == playerId &&
            sound.type == static_cast<std::uint8_t>(zl::core::SoundType::Impact)) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("M34-F2-T5 双客户端投掷：服务器权威、复制、重复拒绝与重连") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);
    const std::filesystem::path profileDir = TempProfileDir("throw");
    const std::string profilePath = (profileDir / "players.bin").string();

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D33345448524F57ull;
    hostOptions.enablePlayerProfiles = true;
    hostOptions.playerProfilesFile = profilePath;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options firstOptions;
    firstOptions.host = "127.0.0.1";
    firstOptions.port = host.NetworkPort();
    firstOptions.contentHash = hostOptions.contentHash;
    firstOptions.autoReconnect = false;
    firstOptions.playerIdentity = "m34-t5-thrower";
    firstOptions.playerName = "Thrower";
    zl::game::NetworkClient::Options secondOptions = firstOptions;
    secondOptions.playerIdentity = "m34-t5-watcher";
    secondOptions.playerName = "Watcher";

    zl::game::NetworkClient first(firstOptions);
    zl::game::NetworkClient second(secondOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayer() && second.HasPlayer() &&
               first.HasPlayerState() && second.HasPlayerState() &&
               host.SessionCount() == 2;
    }));

    const std::uint64_t firstId = first.PlayerStableId();
    const std::uint64_t secondId = second.PlayerStableId();
    REQUIRE(firstId != 0);
    REQUIRE(secondId != 0);
    REQUIRE(firstId != secondId);

    REQUIRE(TeleportServerPlayer(host, firstId, 115.5f, 115.5f));
    REQUIRE(TeleportServerPlayer(host, secondId, 114.5f, 115.5f));
    SetServerPlayerHealth(host, firstId, 100000.0f);
    SetServerPlayerHealth(host, secondId, 100000.0f);
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        zl::world::World& world = host.Simulation().World();
        const zl::world::ChunkCoord firstChunk = zl::world::ChunkOf(117, 115);
        const zl::world::ChunkCoord secondChunk = zl::world::ChunkOf(118, 116);
        const zl::world::Chunk* firstLoaded =
            zl::game::FindChunkInWorld(world, firstChunk.cx, firstChunk.cy);
        const zl::world::Chunk* secondLoaded =
            zl::game::FindChunkInWorld(world, secondChunk.cx, secondChunk.cy);
        return firstLoaded != nullptr &&
               firstLoaded->State() == zl::world::ChunkState::Loaded &&
               secondLoaded != nullptr &&
               secondLoaded->State() == zl::world::ChunkState::Loaded;
    }));
    SetWoodFloor(host, 116, 114, 119, 117);

    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity target = SpawnThrowTarget(host, 117.5f, 115.5f);
    const std::uint64_t targetId =
        host.Simulation().Ecs().IdOf(target).id;
    REQUIRE(targetId != 0);

    ReplaceServerBag(host, firstId, {
        zl::ecs::ItemInstance{"forage_stone", 1, 1.0f},
        zl::ecs::ItemInstance{"forage_stone", 1, 1.0f},
        zl::ecs::ItemInstance{"forage_stone", 1, 1.0f},
        zl::ecs::ItemInstance{"forage_stone", 1, 1.0f},
        zl::ecs::ItemInstance{"forage_stone", 1, 1.0f},
        zl::ecs::ItemInstance{"forage_stone", 1, 1.0f},
        zl::ecs::ItemInstance{"forage_stone", 1, 1.0f},
        zl::ecs::ItemInstance{"forage_stone", 1, 1.0f},
    });
    host.RunForTicks(8);
    first.Poll();
    second.Poll();

    const std::uint64_t rejectedBefore = host.GetNetworkStats().commandsRejected;
    const std::uint64_t startedBefore = host.GetNetworkStats().throwsStarted;
    const zl::net::Command throwCommand =
        MakeThrowCommand(firstId, 0, 117.5f, 115.5f);
    REQUIRE(first.SendCommand(throwCommand, &error));
    REQUIRE(first.SendCommand(throwCommand, &error));
    const bool duplicateRejected = PumpUntil(host, first, second, 5000, [&] {
        return host.GetNetworkStats().throwsStarted == startedBefore + 1 &&
               host.GetNetworkStats().throwsRejected >= 1 &&
               ServerBagCount(host, firstId, "forage_stone") == 7;
    });
    const zl::ecs::Entity firstEntity =
        host.Simulation().Ecs().FindById(firstId);
    const auto* firstHealth = firstEntity != entt::null
                                  ? registry.try_get<zl::ecs::Health>(firstEntity)
                                  : nullptr;
    const auto* targetHealth =
        registry.try_get<zl::ecs::Health>(target);
    INFO("started=" << host.GetNetworkStats().throwsStarted
         << " rejected=" << host.GetNetworkStats().throwsRejected
         << " bag=" << ServerBagCount(host, firstId, "forage_stone")
         << " targetHp=" << (targetHealth != nullptr ? targetHealth->current : -1.0f)
         << " firstHp=" << (firstHealth != nullptr ? firstHealth->current : -1.0f)
         << " inFlight=" << host.InFlightThrowCount()
         << " status=" << (first.HasPlayerState() ? first.PlayerState().status
                                                   : std::string("none"))
         << " pos=" << (firstEntity != entt::null
                            ? std::to_string(registry.get<zl::ecs::Position>(firstEntity).x) +
                                  "," +
                                  std::to_string(registry.get<zl::ecs::Position>(firstEntity).y)
                            : std::string("none")));
    REQUIRE(duplicateRejected);
    CHECK(host.GetNetworkStats().commandsRejected > rejectedBefore);
    CHECK(host.GetNetworkStats().throwsStarted == startedBefore + 1);
    CHECK(host.InFlightThrowCount() <= 1);

    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        const auto* health = registry.try_get<zl::ecs::Health>(target);
        return host.GetNetworkStats().throwsResolved >= 1 &&
               health != nullptr && health->current < 10000.0f;
    }));
    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        const auto* actor = second.Replication().Find(targetId);
        return actor != nullptr && actor->hasHealth &&
               actor->health.current < 10000.0f;
    }));

    for (int i = 0; i < 6; ++i) {
        host.RunForTicks(60);
        first.Poll();
        second.Poll();
        const std::uint64_t before = host.GetNetworkStats().throwsStarted;
        REQUIRE(first.SendCommand(throwCommand, &error));
        REQUIRE(PumpUntil(host, first, second, 6000, [&] {
            return host.GetNetworkStats().throwsStarted > before;
        }));
        REQUIRE(PumpUntil(host, first, second, 6000, [&] {
            return host.GetNetworkStats().throwsResolved >=
                   host.GetNetworkStats().throwsStarted;
        }));
    }
    CHECK(ServerBagCount(host, firstId, "forage_stone") == 1);
    CHECK(host.GetNetworkStats().throwsRecovered +
              host.GetNetworkStats().throwsBroken ==
          7);
    CHECK(CountServerGroundItems(host, "forage_stone", 117.5f, 115.5f, 1.6f) >
          0);
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        return ClientSeesGroundItem(second, "forage_stone");
    }));
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        return ClientSeesWorldSoundFrom(second, firstId);
    }));

    ReplaceServerBag(host, firstId,
                     {zl::ecs::ItemInstance{"molotov", 1, 1.0f}});
    host.RunForTicks(60);
    SetWoodFloor(host, 116, 114, 119, 117);
    host.RunForTicks(2);
    first.Poll();
    second.Poll();
    const std::uint64_t fireStarted = host.GetNetworkStats().throwsStarted;
    const std::uint64_t fireResolved = host.GetNetworkStats().throwsResolved;
    const zl::net::Command fireCommand =
        MakeThrowCommand(firstId, 0, 117.5f, 115.5f);
    REQUIRE(first.SendCommand(fireCommand, &error));
    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        return host.GetNetworkStats().throwsStarted == fireStarted + 1 &&
               host.GetNetworkStats().throwsResolved == fireResolved + 1;
    }));
    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        return ClientSeesFire(second);
    }));
    CHECK(ServerBagCount(host, firstId, "molotov") == 0);

    ReplaceServerBag(host, firstId, {
        zl::ecs::ItemInstance{"forage_stone", 1, 1.0f},
        zl::ecs::ItemInstance{"forage_stone", 1, 1.0f},
    });
    host.RunForTicks(60);
    first.Poll();
    second.Poll();
    const std::uint64_t reconnectStarted =
        host.GetNetworkStats().throwsStarted;
    const zl::net::Command longThrow =
        MakeThrowCommand(firstId, 0, 117.5f, 115.5f);
    REQUIRE(first.SendCommand(longThrow, &error));
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        return host.GetNetworkStats().throwsStarted == reconnectStarted + 1;
    }));
    REQUIRE(host.InFlightThrowCount() >= 1);
    first.Close();
    REQUIRE(PumpUntil(host, second, 6000, [&] {
        return host.SessionCount() == 1;
    }));
    REQUIRE(PumpUntil(host, second, 6000, [&] {
        return host.GetNetworkStats().throwsResolved >= reconnectStarted + 1 &&
               host.InFlightThrowCount() == 0;
    }));
    zl::game::NetworkClient reconnected(firstOptions);
    REQUIRE(reconnected.Connect(&error));
    REQUIRE(PumpUntil(host, second, reconnected, 8000, [&] {
        return reconnected.IsConnected() && reconnected.HasPlayer() &&
               reconnected.HasPlayerState() &&
               reconnected.PlayerStableId() == firstId;
    }));
    CHECK(reconnected.PlayerState().playerStableId == firstId);
    int reconnectedStones = 0;
    for (const zl::net::PlayerStateItem& item :
         reconnected.PlayerState().bag) {
        if (item.defId == "forage_stone") {
            reconnectedStones += item.count;
        }
    }
    CHECK(reconnectedStones == 1);
    CHECK(host.InFlightThrowCount() == 0);
    CHECK(host.GetNetworkStats().throwsStarted ==
          reconnectStarted + 1);

    host.RunForTicks(60);
    second.Poll();
    reconnected.Poll();
    const std::uint64_t shutdownResolved =
        host.GetNetworkStats().throwsResolved;
    REQUIRE(reconnected.SendCommand(throwCommand, &error));
    REQUIRE(PumpUntil(host, second, reconnected, 4000, [&] {
        return host.InFlightThrowCount() >= 1;
    }));
    host.Shutdown();
    CHECK(host.InFlightThrowCount() == 0);
    CHECK(host.GetNetworkStats().throwsResolved >= shutdownResolved + 1);

    reconnected.Close();
    second.Close();
    std::error_code cleanupError;
    std::filesystem::remove_all(profileDir, cleanupError);
}
