#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "ai/components/AiComponents.h"
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

#include "ServerHostTestAccess.h"

namespace {

using Clock = std::chrono::steady_clock;

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
        std::filesystem::temp_directory_path() / "zomboidlike_m34_t7" / name;
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir, error);
    return dir;
}

bool TeleportPlayer(zl::server::ServerHost& host, std::uint64_t stableId,
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

void SetPlayerHealth(zl::server::ServerHost& host, std::uint64_t stableId,
                     float value) {
    const zl::ecs::Entity player = host.Simulation().Ecs().FindById(stableId);
    REQUIRE(static_cast<bool>(player != entt::null));
    auto* health = host.Simulation().Ecs().Registry().try_get<zl::ecs::Health>(
        player);
    REQUIRE(health != nullptr);
    health->current = value;
    health->max = value;
}

void ReplaceBag(zl::server::ServerHost& host, std::uint64_t stableId,
                std::initializer_list<zl::ecs::ItemInstance> items) {
    const zl::ecs::Entity player = host.Simulation().Ecs().FindById(stableId);
    REQUIRE(static_cast<bool>(player != entt::null));
    auto& container =
        host.Simulation().Ecs().Registry().get<zl::ecs::Container>(player);
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

zl::ecs::Entity SpawnStaticTarget(zl::server::ServerHost& host,
                                  std::uint64_t stableId, float x, float y) {
    zl::ecs::Entity target =
        stableId != 0 ? host.Simulation().Ecs().CreateWithStableId(stableId)
                      : host.Simulation().Ecs().Create();
    REQUIRE(static_cast<bool>(target != entt::null));
    auto& registry = host.Simulation().Ecs().Registry();
    registry.emplace<zl::ecs::Position>(target, zl::ecs::Position{x, y, 0});
    registry.emplace<zl::ecs::Health>(target,
                                      zl::ecs::Health{10000.0f, 10000.0f});
    registry.emplace<zl::ecs::SpriteRef>(
        target, zl::ecs::SpriteRef{zl::ecs::HashName("character.zombie"), -1,
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

bool ClientSeesSoundFrom(zl::game::NetworkClient& client,
                         std::uint64_t playerId) {
    const std::vector<zl::net::WorldSoundEvent> sounds =
        client.TakeWorldSounds();
    for (const zl::net::WorldSoundEvent& sound : sounds) {
        if (sound.sourcePlayerStableId == playerId &&
            sound.type ==
                static_cast<std::uint8_t>(zl::core::SoundType::Impact)) {
            return true;
        }
    }
    return false;
}

struct ThrowFixture {
    std::uint64_t firstId = 0;
    std::uint64_t secondId = 0;
};

ThrowFixture ConnectPair(zl::server::ServerHost& host,
                         zl::game::NetworkClient& first,
                         zl::game::NetworkClient& second) {
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayer() && second.HasPlayer() &&
               first.HasPlayerState() && second.HasPlayerState() &&
               host.SessionCount() == 2;
    }));
    ThrowFixture fixture;
    fixture.firstId = first.PlayerStableId();
    fixture.secondId = second.PlayerStableId();
    REQUIRE(fixture.firstId != 0);
    REQUIRE(fixture.secondId != 0);
    REQUIRE(fixture.firstId != fixture.secondId);
    return fixture;
}

}  // namespace

TEST_CASE("M34-F2-T7 双客户端投掷夹具") {
    const char* enabled = std::getenv("ZL_M34_T7_FIXTURE");
    if (enabled == nullptr || *enabled == '\0') {
        return;
    }
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);
    const std::filesystem::path out = root / "build" / "m34_f2_t7_fixture";
    std::error_code cleanupError;
    std::filesystem::remove_all(out, cleanupError);
    std::filesystem::create_directories(out / "saves", cleanupError);
    REQUIRE_FALSE(cleanupError);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D33345437534658ull;
    hostOptions.enablePlayerProfiles = true;
    hostOptions.playerProfilesFile = (out / "players.bin").string();
    hostOptions.saveRoot = (out / "saves").string();

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options firstOptions;
    firstOptions.host = "127.0.0.1";
    firstOptions.port = host.NetworkPort();
    firstOptions.contentHash = hostOptions.contentHash;
    firstOptions.autoReconnect = false;
    firstOptions.playerIdentity = "m34t7a";
    firstOptions.playerName = "ThrowerA";
    zl::game::NetworkClient::Options secondOptions = firstOptions;
    secondOptions.playerIdentity = "m34t7b";
    secondOptions.playerName = "WatcherB";

    zl::game::NetworkClient first(firstOptions);
    zl::game::NetworkClient second(secondOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    const ThrowFixture ids = ConnectPair(host, first, second);

    REQUIRE(TeleportPlayer(host, ids.firstId, 56.5f, 15.5f));
    REQUIRE(TeleportPlayer(host, ids.secondId, 57.5f, 15.5f));
    SetPlayerHealth(host, ids.firstId, 100000.0f);
    SetPlayerHealth(host, ids.secondId, 100000.0f);
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        zl::world::World& world = host.Simulation().World();
        const zl::world::ChunkCoord targetChunk =
            zl::world::ChunkOf(56, 13);
        const zl::world::Chunk* chunk = zl::game::FindChunkInWorld(
            world, targetChunk.cx, targetChunk.cy);
        return chunk != nullptr &&
               chunk->State() == zl::world::ChunkState::Loaded;
    }));

    SpawnStaticTarget(host, 0x4D33345437544152ull, 56.5f, 13.5f);
    ReplaceBag(host, ids.firstId,
               {zl::ecs::ItemInstance{"forage_stone", 8, 1.0f},
                zl::ecs::ItemInstance{"makeshift_spear", 8, 1.0f},
                zl::ecs::ItemInstance{"molotov", 2, 1.0f}});
    host.RunForTicks(4);
    first.Poll();
    second.Poll();

    std::string saveId;
    std::string saveError;
    REQUIRE_MESSAGE(zl::server::ServerHostTestAccess::CreateSave(
                        host, "village_demo_m31", &saveId, &saveError),
                    saveError);
    REQUIRE_FALSE(saveId.empty());
    zl::server::ServerHostTestAccess::SetActiveSaveId(host, saveId);
    zl::server::ServerHostTestAccess::SaveProfiles(host, "fixture");
    const bool saved =
        zl::server::ServerHostTestAccess::SaveWorld(host, "fixture");
    INFO("fixture out=" << out.string() << " save=" << saveId);
    REQUIRE(saved);
    REQUIRE(std::filesystem::exists(out / "players.bin"));
    REQUIRE(std::filesystem::exists(out / "saves" / saveId));
    {
        std::ofstream marker(out / "save_id.txt", std::ios::binary);
        REQUIRE(marker.is_open());
        marker << saveId;
    }

    zl::server::ServerHostTestAccess::SetSaveWorldOnExit(host, false);
    first.Close();
    second.Close();
    host.Shutdown();
}

TEST_CASE("M34-F2-T7 双客户端投掷：噪声、长矛伤害回收与燃烧瓶") {
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
    hostOptions.contentHash = 0x4D33345437524F57ull;
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
    firstOptions.playerIdentity = "m34t7-thrower";
    firstOptions.playerName = "Thrower";
    zl::game::NetworkClient::Options secondOptions = firstOptions;
    secondOptions.playerIdentity = "m34t7-watcher";
    secondOptions.playerName = "Watcher";

    zl::game::NetworkClient first(firstOptions);
    zl::game::NetworkClient second(secondOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    const ThrowFixture ids = ConnectPair(host, first, second);

    REQUIRE(TeleportPlayer(host, ids.firstId, 115.5f, 115.5f));
    REQUIRE(TeleportPlayer(host, ids.secondId, 114.5f, 115.5f));
    SetPlayerHealth(host, ids.firstId, 100000.0f);
    SetPlayerHealth(host, ids.secondId, 100000.0f);
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        zl::world::World& world = host.Simulation().World();
        const zl::world::ChunkCoord firstChunk = zl::world::ChunkOf(117, 115);
        const zl::world::ChunkCoord secondChunk = zl::world::ChunkOf(118, 116);
        const zl::world::ChunkCoord noiseChunk = zl::world::ChunkOf(119, 119);
        const zl::world::ChunkCoord listenerChunk =
            zl::world::ChunkOf(123, 123);
        const zl::world::Chunk* firstLoaded =
            zl::game::FindChunkInWorld(world, firstChunk.cx, firstChunk.cy);
        const zl::world::Chunk* secondLoaded =
            zl::game::FindChunkInWorld(world, secondChunk.cx, secondChunk.cy);
        const zl::world::Chunk* noiseLoaded =
            zl::game::FindChunkInWorld(world, noiseChunk.cx, noiseChunk.cy);
        const zl::world::Chunk* listenerLoaded =
            zl::game::FindChunkInWorld(world, listenerChunk.cx,
                                       listenerChunk.cy);
        return firstLoaded != nullptr &&
               firstLoaded->State() == zl::world::ChunkState::Loaded &&
               secondLoaded != nullptr &&
               secondLoaded->State() == zl::world::ChunkState::Loaded &&
               noiseLoaded != nullptr &&
               noiseLoaded->State() == zl::world::ChunkState::Loaded &&
               listenerLoaded != nullptr &&
               listenerLoaded->State() == zl::world::ChunkState::Loaded;
    }));
    SetWoodFloor(host, 116, 114, 119, 117);

    const zl::ecs::Entity target =
        SpawnStaticTarget(host, 0, 117.5f, 115.5f);
    const std::uint64_t targetId =
        host.Simulation().Ecs().IdOf(target).id;
    auto& registry = host.Simulation().Ecs().Registry();

    const zl::ecs::Entity listener =
        host.Simulation().SpawnZombieAt(123.5f, 123.5f);
    REQUIRE(static_cast<bool>(listener != entt::null));
    auto* listenerHealth = registry.try_get<zl::ecs::Health>(listener);
    REQUIRE(listenerHealth != nullptr);
    listenerHealth->current = 10000.0f;
    listenerHealth->max = 10000.0f;
    auto* listenerPerception =
        registry.try_get<zl::ecs::PerceptionState>(listener);
    REQUIRE(listenerPerception != nullptr);
    listenerPerception->lastKnownX = -1.0f;
    listenerPerception->lastKnownY = -1.0f;

    ReplaceBag(host, ids.firstId,
               {zl::ecs::ItemInstance{"forage_stone", 12, 1.0f},
                zl::ecs::ItemInstance{"makeshift_spear", 12, 1.0f},
                zl::ecs::ItemInstance{"molotov", 2, 1.0f}});
    host.RunForTicks(8);
    first.Poll();
    second.Poll();

    const std::uint64_t startedBefore = host.GetNetworkStats().throwsStarted;
    const zl::net::Command noiseCommand =
        MakeThrowCommand(ids.firstId, 0, 119.5f, 119.5f);
    REQUIRE(first.SendCommand(noiseCommand, &error));
    INFO("noise started=" << host.GetNetworkStats().throwsStarted
         << " rejected=" << host.GetNetworkStats().throwsRejected
         << " resolved=" << host.GetNetworkStats().throwsResolved);
    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        return host.GetNetworkStats().throwsStarted == startedBefore + 1 &&
               host.GetNetworkStats().throwsResolved >= startedBefore + 1;
    }));
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        return ClientSeesSoundFrom(second, ids.firstId);
    }));
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        return std::abs(listenerPerception->lastKnownX - 119.5f) < 0.1f &&
               std::abs(listenerPerception->lastKnownY - 119.5f) < 0.1f &&
               listenerPerception->alertness > 0.0f;
    }));

    host.RunForTicks(60);
    first.Poll();
    second.Poll();
    ReplaceBag(host, ids.firstId,
               {zl::ecs::ItemInstance{"makeshift_spear", 12, 1.0f}});
    const std::uint64_t recoveredBefore =
        host.GetNetworkStats().throwsRecovered;
    bool spearRecovered = false;
    for (int attempt = 0; attempt < 10 && !spearRecovered; ++attempt) {
        const std::uint64_t started = host.GetNetworkStats().throwsStarted;
        const zl::net::Command spearCommand =
            MakeThrowCommand(ids.firstId, 0, 117.5f, 115.5f);
        REQUIRE(first.SendCommand(spearCommand, &error));
        REQUIRE(PumpUntil(host, first, second, 6000, [&] {
            return host.GetNetworkStats().throwsStarted > started &&
                   host.GetNetworkStats().throwsResolved >=
                       started + 1;
        }));
        const auto* health = registry.try_get<zl::ecs::Health>(target);
        REQUIRE(health != nullptr);
        CHECK(health->current < 10000.0f);
        spearRecovered = host.GetNetworkStats().throwsRecovered >
                             recoveredBefore &&
                         ClientSeesGroundItem(second, "makeshift_spear");
        if (!spearRecovered) {
            host.RunForTicks(60);
            first.Poll();
            second.Poll();
        }
    }
    REQUIRE(spearRecovered);
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        const auto* actor = second.Replication().Find(targetId);
        return actor != nullptr && actor->hasHealth &&
               actor->health.current < 10000.0f;
    }));

    host.RunForTicks(60);
    first.Poll();
    second.Poll();
    ReplaceBag(host, ids.firstId,
               {zl::ecs::ItemInstance{"molotov", 1, 1.0f}});
    SetWoodFloor(host, 116, 114, 119, 117);
    const std::uint64_t fireStarted = host.GetNetworkStats().throwsStarted;
    const zl::net::Command fireCommand =
        MakeThrowCommand(ids.firstId, 0, 117.5f, 115.5f);
    REQUIRE(first.SendCommand(fireCommand, &error));
    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        return host.GetNetworkStats().throwsStarted == fireStarted + 1 &&
               host.GetNetworkStats().throwsResolved >= fireStarted + 1;
    }));
    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        return ClientSeesFire(second);
    }));

    first.Close();
    second.Close();
    host.Shutdown();
    std::error_code cleanup;
    std::filesystem::remove_all(profileDir, cleanup);
}
