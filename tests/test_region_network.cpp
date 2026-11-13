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
#include "core/Constants.h"
#include "core/TaskScheduler.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/NetworkClient.h"
#include "game/RegionEventDirector.h"
#include "game/StrategicState.h"
#include "game/WorldSimulation.h"
#include "physics/PhysicsWorld.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"
#include "save/SaveManager.h"
#include "save/SaveTypes.h"
#include "save/WorldManifest.h"
#include "server/ServerHost.h"
#include "world/Chunk.h"
#include "world/ChunkManager.h"
#include "world/GridSquare.h"
#include "world/RegionStreaming.h"
#include "world/World.h"
#include "world/WorldDefinition.h"

using namespace zl;

namespace {

const std::string kM31Dir =
    std::string(ZL_SOURCE_DIR) + "/assets/maps/village_demo_m31";

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
bool PumpUntil(world::ChunkManager& manager,
               const std::vector<world::ChunkManager::FocusPoint>& focuses,
               int timeoutMs, Predicate predicate) {
    const Clock::time_point deadline =
        Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        manager.UpdateFocuses(focuses);
        manager.ProcessResults();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    manager.UpdateFocuses(focuses);
    manager.ProcessResults();
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
bool PumpUntilMany(zl::server::ServerHost& host,
                   const std::vector<zl::game::NetworkClient*>& clients,
                   int timeoutMs, Predicate predicate) {
    const Clock::time_point deadline =
        Clock::now() + std::chrono::milliseconds(timeoutMs);
    Clock::time_point nextTick = Clock::now();
    while (Clock::now() < deadline) {
        if (Clock::now() >= nextTick) {
            host.Tick(1.0f / 60.0f);
            nextTick += std::chrono::milliseconds(16);
        }
        for (zl::game::NetworkClient* client : clients) {
            client->Poll();
        }
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

std::filesystem::path TempDir(const char* name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zomboidlike_t8" / name;
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir, error);
    return dir;
}

void CreateEmptySave(const std::filesystem::path& saveRoot,
                     const std::string& mapId, std::string* saveId) {
    save::EntitySnapshot empty;
    save::SaveManager::Options options;
    options.root = saveRoot;
    options.gameVersion = save::kDefaultGameVersion;
    save::SaveManager manager(options);
    save::SaveManager::CreateOptions create;
    create.name = "M31-T8";
    create.mapId = mapId;
    create.gameVersion = save::kDefaultGameVersion;
    create.entities = &empty;
    std::string error;
    REQUIRE_MESSAGE(manager.Create(create, saveId, &error), error);
}

zl::server::ServerHost::Options MakeHostOptions(
    const std::filesystem::path& root, const std::filesystem::path& saveRoot,
    const std::string& profilePath, std::uint64_t contentHash,
    const std::string& saveId, std::vector<std::string>* args,
    std::vector<const char*>* argv) {
    args->push_back("ZomboidLikeTests");
    args->push_back("--world.mapDir=assets/maps/village_demo_m31");
    args->push_back("--world.loadRadius=2");
    args->push_back("--save.root=" + saveRoot.string());
    if (!saveId.empty()) {
        args->push_back("--server.saveId=" + saveId);
    }
    argv->clear();
    for (const std::string& arg : *args) {
        argv->push_back(arg.c_str());
    }

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
    options.argc = static_cast<int>(argv->size());
    options.argv = argv->data();
    return options;
}

zl::game::NetworkClient::Options ClientOptions(
    std::uint16_t port, std::uint64_t contentHash,
    const std::string& identity) {
    zl::game::NetworkClient::Options options;
    options.host = "127.0.0.1";
    options.port = port;
    options.contentHash = contentHash;
    options.autoReconnect = false;
    options.playerIdentity = identity;
    options.playerName = identity;
    return options;
}

bool TeleportServerPlayer(zl::server::ServerHost& host,
                          std::uint64_t stableId, float x, float y) {
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

bool ContainsWall(const std::vector<save::ChunkDiff>& diffs) {
    for (const save::ChunkDiff& diff : diffs) {
        for (const save::ChunkSquareDiff& square : diff.squares) {
            for (const world::SquareObject& object :
                 square.layers[static_cast<std::size_t>(
                     world::SquareLayer::Wall)]) {
                if (object.playerBuilt && object.sprite == "object.wall_h" &&
                    object.hp == doctest::Approx(100.0f)) {
                    return true;
                }
            }
        }
    }
    return false;
}

int WallCountAt(zl::server::ServerHost& host, int wx, int wy) {
    const world::GridSquare* square =
        host.Simulation().World().GetSquare(wx, wy, 0);
    if (square == nullptr) {
        return 0;
    }
    int count = 0;
    for (const world::SquareObject& object :
         square->Layer(world::SquareLayer::Wall)) {
        if (object.playerBuilt && object.sprite == "object.wall_h") {
            ++count;
        }
    }
    return count;
}

void AddTestWall(zl::server::ServerHost& host, int wx, int wy) {
    world::GridSquare* square =
        host.Simulation().World().GetSquare(wx, wy, 0);
    REQUIRE(square != nullptr);
    world::SquareObject wall{"object.wall_h", 0, 0, true};
    wall.hp = 100.0f;
    wall.playerBuilt = true;
    square->AddObject(world::SquareLayer::Wall, wall);
}

}  // namespace

TEST_CASE("M31-T8 多焦点区域相位与区域事件可见性") {
    world::WorldDefinition definition;
    std::string error;
    REQUIRE(world::LoadWorldDefinition(kM31Dir, &definition, &error) ==
            world::WorldDefinitionStatus::Loaded);

    world::StreamingConfig config;
    config.loadRadius = 3;
    config.residentRadius = 5;
    world::RegionStreaming streaming(definition, config);
    streaming.SetFocuses({glm::vec2(70.0f, 24.0f),
                          glm::vec2(300.0f, 280.0f)});
    CHECK(streaming.Stats().focusCount == 2);
    CHECK(streaming.Stats().active == 2);
    CHECK(streaming.PhaseOf("village_core") == world::RegionPhase::Active);
    CHECK(streaming.PhaseOf("east_wilds") == world::RegionPhase::Active);
    CHECK(streaming.ActiveRegionId() == "village_core");

    game::RegionEventDirector events(definition);
    events.AdvanceDay(1);
    const auto phaseOf = [&streaming](const std::string& regionId) {
        return streaming.PhaseOf(regionId);
    };
    events.UpdateFocuses(1.0f, {glm::vec2(70.0f, 24.0f),
                               glm::vec2(300.0f, 280.0f)},
                         phaseOf, [](const game::RegionEvent&) { return 0; });
    CHECK(events.Stats().active >= 2);
    int active = 0;
    for (const game::RegionEvent& event : events.Events()) {
        if (event.status == game::RegionEventStatus::Active) {
            ++active;
        }
    }
    CHECK(active >= 2);

    streaming.SetFocuses({glm::vec2(70.0f, 24.0f)});
    events.UpdateFocuses(1.0f, {glm::vec2(70.0f, 24.0f)}, phaseOf,
                         [](const game::RegionEvent&) { return 0; });
    CHECK(streaming.PhaseOf("east_wilds") == world::RegionPhase::Frozen);
    CHECK(events.Stats().frozen >= 1);
    CHECK(events.Stats().active < active);
}

TEST_CASE("M31-T8 ChunkManager 多焦点窗口并集与单焦点收缩") {
    core::TaskScheduler tasks(2);
    world::MapLoader::Meta meta;
    std::string error;
    REQUIRE(world::MapLoader::LoadMeta(kM31Dir, &meta, &error));

    world::World world;
    world::ChunkManager manager(world, meta, tasks, 2, 8, 8, 4.0, 3, true);
    const std::vector<world::ChunkManager::FocusPoint> both{
        {55.0f, 55.0f}, {295.0f, 295.0f}};
    manager.UpdateFocuses(both);
    CHECK(manager.GetStats().focusCount == 2);
    REQUIRE(PumpUntil(manager, both, 15000, [&] {
        return manager.GetStats().loadedChunks == 50;
    }));
    CHECK(world.LoadedChunkCount() == 50);
    CHECK(manager.GetStats().residentChunks == 98);
    bool firstWindow = false;
    bool secondWindow = false;
    manager.ForEachLoadedChunk([&](const world::Chunk& chunk) {
        const bool west = chunk.ChunkX() >= 3 && chunk.ChunkX() <= 7 &&
                          chunk.ChunkY() >= 3 && chunk.ChunkY() <= 7;
        const bool east = chunk.ChunkX() >= 27 && chunk.ChunkX() <= 31 &&
                          chunk.ChunkY() >= 27 && chunk.ChunkY() <= 31;
        firstWindow = firstWindow || west;
        secondWindow = secondWindow || east;
        CHECK((west || east));
    });
    CHECK(firstWindow);
    CHECK(secondWindow);

    const std::vector<world::ChunkManager::FocusPoint> one{{55.0f, 55.0f}};
    manager.UpdateFocuses(one);
    REQUIRE(PumpUntil(manager, one, 15000, [&] {
        return manager.GetStats().loadedChunks == 25;
    }));
    CHECK(world.LoadedChunkCount() == 25);
    CHECK(manager.GetStats().residentChunks == 49);
    CHECK(manager.GetStats().totalUnloaded >= 25);
    manager.ForEachLoadedChunk([&](const world::Chunk& chunk) {
        CHECK(chunk.ChunkX() >= 3);
        CHECK(chunk.ChunkX() <= 7);
        CHECK(chunk.ChunkY() >= 3);
        CHECK(chunk.ChunkY() <= 7);
    });
}

TEST_CASE("M31-T8 服务器双区域：兴趣并集、实体 AOI 与结构 AOI") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("interest_union");
    const std::string profiles = (dir / "players.bin").string();
    std::vector<std::string> args;
    std::vector<const char*> argv;
    const std::uint64_t contentHash = 0x4D33315438494E54ull;
    zl::server::ServerHost::Options options =
        MakeHostOptions(root, dir / "saves", profiles, contentHash, "",
                        &args, &argv);

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient first(
        ClientOptions(host.NetworkPort(), contentHash, "t8-first"));
    zl::game::NetworkClient second(
        ClientOptions(host.NetworkPort(), contentHash, "t8-second"));
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayer() && second.HasPlayer() &&
               host.SessionCount() == 2;
    }));

    const std::uint64_t firstId = first.PlayerStableId();
    const std::uint64_t secondId = second.PlayerStableId();
    REQUIRE(firstId != 0);
    REQUIRE(secondId != 0);
    REQUIRE(TeleportServerPlayer(host, firstId, 55.5f, 55.5f));
    REQUIRE(TeleportServerPlayer(host, secondId, 295.5f, 295.5f));
    REQUIRE(PumpUntil(host, first, second, 15000, [&] {
        return host.Simulation().Chunks().GetStats().loadedChunks == 50 &&
               host.Simulation().Chunks().GetStats().focusCount == 2;
    }));
    CHECK(host.Simulation().Chunks().GetStats().loadedChunks == 50);

    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return first.Replication().Find(secondId) == nullptr &&
               second.Replication().Find(firstId) == nullptr;
    }));
    CHECK(first.Replication().Find(secondId) == nullptr);
    CHECK(second.Replication().Find(firstId) == nullptr);

    // Clear sync backlogs before adding a structure near the first player.
    (void)first.TakeChunkDiffs();
    (void)second.TakeChunkDiffs();
    AddTestWall(host, 55, 55);
    REQUIRE(PumpUntil(host, first, second, 10000, [&] {
        return ContainsWall(first.TakeChunkDiffs());
    }));
    CHECK_FALSE(ContainsWall(second.TakeChunkDiffs()));
    CHECK(host.GetNetworkStats().structureChunksFiltered > 0);

    first.Close();
    second.Close();
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M31-T8 四玩家分散区域压力：并集预算与无崩溃") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("four_players");
    const std::string profiles = (dir / "players.bin").string();
    std::vector<std::string> args;
    std::vector<const char*> argv;
    const std::uint64_t contentHash = 0x4D33315438503450ull;
    zl::server::ServerHost::Options options = MakeHostOptions(
        root, dir / "saves", profiles, contentHash, "", &args, &argv);

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    std::vector<std::unique_ptr<zl::game::NetworkClient>> storage;
    std::vector<zl::game::NetworkClient*> clients;
    for (int i = 0; i < 4; ++i) {
        storage.push_back(std::make_unique<zl::game::NetworkClient>(
            ClientOptions(host.NetworkPort(), contentHash,
                          "t8-four-" + std::to_string(i))));
        clients.push_back(storage.back().get());
        REQUIRE(clients.back()->Connect(&error));
    }
    REQUIRE(PumpUntilMany(host, clients, 10000, [&] {
        for (zl::game::NetworkClient* client : clients) {
            if (!client->IsConnected() || !client->HasPlayer()) {
                return false;
            }
        }
        return host.SessionCount() == 4;
    }));

    const float positions[4][2] = {
        {25.5f, 25.5f}, {75.5f, 75.5f},
        {275.5f, 275.5f}, {335.5f, 335.5f}};
    for (int i = 0; i < 4; ++i) {
        REQUIRE(TeleportServerPlayer(host, clients[i]->PlayerStableId(),
                                     positions[i][0], positions[i][1]));
    }
    REQUIRE(PumpUntilMany(host, clients, 15000, [&] {
        return host.Simulation().Chunks().GetStats().focusCount == 4 &&
               host.Simulation().Chunks().GetStats().loadedChunks == 100;
    }));
    CHECK(host.Simulation().Chunks().GetStats().focusCount == 4);
    CHECK(host.Simulation().Chunks().GetStats().loadedChunks == 100);
    CHECK(host.GetNetworkStats().snapshotFailures == 0);

    for (zl::game::NetworkClient* client : clients) {
        client->Close();
    }
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M31-T8 服务器 manifest/桶/事件持久化与重启恢复") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("manifest_restart");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE(!saveId.empty());

    std::vector<std::string> args;
    std::vector<const char*> argv;
    const std::uint64_t contentHash = 0x4D333154384D414Eull;
    zl::server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId,
                        &args, &argv);

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient client(
        ClientOptions(host.NetworkPort(), contentHash, "t8-manifest"));
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 8000, [&] {
        return client.IsConnected() && client.HasPlayer() &&
               client.HasPlayerState() &&
               host.Simulation().Chunks().GetStats().loadedChunks > 0;
    }));
    const std::uint64_t playerId = client.PlayerStableId();
    REQUIRE(playerId != 0);
    const ecs::Entity player = host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));
    const auto& position =
        host.Simulation().Ecs().Registry().get<ecs::Position>(player);
    const int wallX = static_cast<int>(std::floor(position.x));
    const int wallY = static_cast<int>(std::floor(position.y));

    game::StrategicState* strategic = host.Simulation().Strategic();
    REQUIRE(strategic != nullptr);
    REQUIRE(strategic->ApplyOwnerChange(
        "south_farmland", game::StrategicOwner::Contested,
        "m31-t8-strategic", 1234, {-1, -5, -2, 0}, &error));

    REQUIRE(PumpUntil(host, client, 8000, [&] {
        game::RegionEventDirector* events =
            host.Simulation().RegionEvents();
        if (events == nullptr) {
            return false;
        }
        for (const game::RegionEvent& event : events->Events()) {
            if (event.contentSpawned) {
                return true;
            }
        }
        return false;
    }));
    std::vector<std::string> eventIds;
    if (game::RegionEventDirector* events = host.Simulation().RegionEvents();
        events != nullptr) {
        for (const game::RegionEvent& event : events->Events()) {
            eventIds.push_back(event.id);
        }
    }
    REQUIRE(!eventIds.empty());

    AddTestWall(host, wallX, wallY);
    CHECK(WallCountAt(host, wallX, wallY) == 1);
    client.Close();
    host.Shutdown();

    save::SaveManager::Options saveOptions;
    saveOptions.root = saveRoot;
    save::SaveManager manager(saveOptions);
    save::WorldManifest manifest;
    const save::SaveManager::LoadResult read =
        manager.ReadWorldManifest(saveId, &manifest, &error);
    REQUIRE_MESSAGE(read == save::SaveManager::LoadResult::Ok, error);
    CHECK(manifest.identity.mapId == "village_demo_m31");
    CHECK(manifest.regions.size() == 10u);
    CHECK(!manifest.events.empty());
    CHECK(!manifest.buckets.empty());
    CHECK(!manifest.chunks.empty());
    CHECK(manifest.strategicRegions.size() == 5u);
    bool savedStrategic = false;
    for (const save::ManifestStrategicEvent& event : manifest.strategicEvents) {
        savedStrategic = savedStrategic || event.id == "m31-t8-strategic";
    }
    CHECK(savedStrategic);
    bool savedEvent = false;
    for (const save::ManifestRegionEvent& event : manifest.events) {
        savedEvent = savedEvent ||
                     std::find(eventIds.begin(), eventIds.end(), event.id) !=
                         eventIds.end();
    }
    CHECK(savedEvent);

    zl::server::ServerHost restarted;
    REQUIRE_MESSAGE(restarted.Init(options, &error), error);
    REQUIRE(restarted.NetworkPort() != 0);
    game::StrategicState* restoredStrategic =
        restarted.Simulation().Strategic();
    REQUIRE(restoredStrategic != nullptr);
    const game::StrategicRegionState* restoredFarm =
        restoredStrategic->FindRegion("south_farmland");
    REQUIRE(restoredFarm != nullptr);
    CHECK(restoredFarm->owner == game::StrategicOwner::Contested);
    CHECK(restoredFarm->lastEventMinute == 1234);
    CHECK(restoredStrategic->FindEvent("m31-t8-strategic") != nullptr);
    CHECK(restoredStrategic->resources.people == 11);
    if (game::RegionEventDirector* events =
            restarted.Simulation().RegionEvents();
        events != nullptr) {
        bool restored = false;
        for (const game::RegionEvent& event : events->Events()) {
            restored = restored ||
                       std::find(eventIds.begin(), eventIds.end(), event.id) !=
                           eventIds.end();
        }
        CHECK(restored);
    }

    zl::game::NetworkClient reconnected(ClientOptions(
        restarted.NetworkPort(), contentHash, "t8-manifest"));
    REQUIRE(reconnected.Connect(&error));
    REQUIRE(PumpUntil(restarted, reconnected, 8000, [&] {
        return reconnected.IsConnected() && reconnected.HasPlayer() &&
               WallCountAt(restarted, wallX, wallY) == 1;
    }));
    CHECK(WallCountAt(restarted, wallX, wallY) == 1);

    reconnected.Close();
    restarted.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M32-T2 服务器战略 tick 预警、离线宽限与自动结算") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("strategic_director");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE(!saveId.empty());

    std::vector<std::string> args;
    std::vector<const char*> argv;
    const std::uint64_t contentHash = 0x4D33325432444952ull;
    zl::server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId,
                        &args, &argv);
    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    game::StrategicState* strategic = host.Simulation().Strategic();
    REQUIRE(strategic != nullptr);
    game::StrategicRegionState* farm =
        strategic->FindRegion("south_farmland");
    REQUIRE(farm != nullptr);
    farm->pressure = 80.0f;

    host.AdvanceGameMinutes(30.0);
    host.Tick(1.0f / 60.0f);
    CHECK(strategic->FindEvent("warning.south_farmland.2910") != nullptr);
    CHECK(farm->siegePhase == game::StrategicSiegePhase::Warning);

    host.AdvanceGameMinutes(5760.0);
    host.Tick(1.0f / 60.0f);
    CHECK(strategic->FindEvent("siege.south_farmland.2910") != nullptr);
    CHECK(farm->owner == game::StrategicOwner::Infested);
    CHECK(farm->siegePhase == game::StrategicSiegePhase::None);

    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}
