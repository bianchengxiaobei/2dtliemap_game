#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "core/Config.h"
#include "core/Constants.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/Missions.h"
#include "game/NetworkClient.h"
#include "game/Poi.h"
#include "game/Settlement.h"
#include "game/WorldSimulation.h"
#include "items/ItemComponents.h"
#include "physics/PhysicsWorld.h"
#include "save/SaveManager.h"
#include "server/ServerHost.h"
#include "world/ChunkData.h"
#include "world/ChunkManager.h"
#include "world/MapLoader.h"
#include "world/WorldCoords.h"
#include "world/WorldDefinition.h"
#include "world/WorldGraph.h"
#include "world/WorldSelection.h"

using namespace zl;

namespace {

const std::string kM31Dir =
    std::string(ZL_SOURCE_DIR) + "/assets/maps/village_demo_m31";
const std::string kM31Content =
    std::string(ZL_SOURCE_DIR) + "/assets/maps/village_demo_m31_content.json";

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

int FindPoiProgress(const net::PlayerState& state, std::string_view id) {
    for (const net::PlayerPoiProgress& progress : state.poiProgress) {
        if (progress.id == id) {
            return progress.liveGuards;
        }
    }
    return -1;
}

std::filesystem::path TempDir(const char* name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zomboidlike_t10" / name;
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
    create.name = "M31-T10";
    create.mapId = "village_demo_m31";
    create.gameVersion = save::kDefaultGameVersion;
    create.entities = &empty;
    std::string error;
    REQUIRE_MESSAGE(manager.Create(create, saveId, &error), error);
}

server::ServerHost::Options HostOptions(const std::filesystem::path& root,
                                        const std::string& profile,
                                        std::uint64_t contentHash,
                                        const std::string& saveId) {
    server::ServerHost::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.mapDir = "assets/maps/village_demo_m31";
    options.contentFile = "assets/maps/village_demo_m31_content.json";
    options.spawnDemoPopulation = false;
    options.spawnLocalPlayer = false;
    options.network = true;
    options.port = 0;
    options.contentHash = contentHash;
    options.enablePlayerProfiles = true;
    options.playerProfilesFile = profile;
    options.loadSaveId = saveId;
    return options;
}

const game::PoiDef* FindPoi(const game::PoiConfig& config,
                            std::string_view id) {
    return config.Find(id);
}

const world::WorldPoi* FindWorldPoi(const world::WorldDefinition& definition,
                                    std::string_view id) {
    for (const world::WorldPoi& poi : definition.pois) {
        if (poi.id == id) {
            return &poi;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("M31-T10 默认 M31 与回退选择") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    ScopedWorkingDirectory cwd(root);

    core::Config config;
    REQUIRE(config.LoadFile((root / "config" / "game.json").string()));
    const world::WorldSelection single = world::ResolveWorldSelection(config, false);
    CHECK(single.m31);
    CHECK(single.mapDir.find("village_demo_m31") != std::string::npos);
    CHECK(single.contentFile.find("village_demo_m31_content") !=
          std::string::npos);

    const world::WorldSelection dedicated =
        world::ResolveWorldSelection(config, true);
    CHECK_FALSE(dedicated.m31);
    CHECK(dedicated.mapDir.find("village_demo") != std::string::npos);
    CHECK(dedicated.contentFile.empty());

    const std::vector<std::string> args{
        "ZomboidLikeTests", "--world.mapDir=assets/maps/village_demo_m31"};
    std::vector<const char*> argv;
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }
    config.ApplyCommandLine(static_cast<int>(argv.size()), argv.data());
    const world::WorldSelection explicitServer =
        world::ResolveWorldSelection(config, true);
    CHECK(explicitServer.m31);
    CHECK(explicitServer.mapDir.find("village_demo_m31") != std::string::npos);

    core::Config disabled;
    REQUIRE(disabled.LoadFile((root / "config" / "game.json").string()));
    disabled.Set("world.m31.enabled", false);
    const world::WorldSelection legacy =
        world::ResolveWorldSelection(disabled, false);
    CHECK_FALSE(legacy.m31);
    CHECK(legacy.contentFile.empty());
    CHECK(legacy.mapDir.find("village_demo") != std::string::npos);
}

TEST_CASE("M31-T10 垂直切片内容契约、道路可达与资源/危险区") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    ScopedWorkingDirectory cwd(root);

    core::Config content;
    REQUIRE(content.LoadFile(kM31Content));
    game::PoiConfig pois;
    std::string error;
    REQUIRE_MESSAGE(game::LoadPoiConfig(content, &pois, &error), error);
    REQUIRE(pois.enabled);
    CHECK(pois.defs.size() == 4u);

    game::MissionConfig missions;
    REQUIRE_MESSAGE(game::LoadMissionConfig(content, &missions, &error), error);
    REQUIRE(missions.enabled);
    CHECK(missions.defs.size() == 13u);
    const game::MissionDef* settlementHelp = missions.Find("settlement_help");
    const game::MissionDef* settlementEscort =
        missions.Find("settlement_escort");
    const game::MissionDef* settlementClear =
        missions.Find("settlement_clear");
    const game::MissionDef* settlementSupply =
        missions.Find("settlement_supply");
    REQUIRE(settlementHelp != nullptr);
    REQUIRE(settlementEscort != nullptr);
    REQUIRE(settlementClear != nullptr);
    REQUIRE(settlementSupply != nullptr);
    CHECK(settlementHelp->minSettlementReputation == -39);
    CHECK(settlementEscort->minSettlementReputation == 0);
    CHECK(settlementClear->minSettlementReputation == 20);
    CHECK(settlementSupply->minSettlementReputation == 30);
    CHECK(missions.Find("north_blood_drive") != nullptr);
    CHECK(missions.Find("south_harvest_help") != nullptr);
    CHECK(missions.Find("river_fish_supply") != nullptr);

    world::WorldDefinition definition;
    REQUIRE(world::LoadWorldDefinition(kM31Dir, &definition, &error) ==
            world::WorldDefinitionStatus::Loaded);
    CHECK(definition.mapId == "village_demo_m31");
    CHECK(definition.pois.size() == 3u);
    world::WorldGraph graph(definition);
    REQUIRE(graph.Valid());
    const world::GraphNode* village = graph.FindNode("village_haven");
    REQUIRE(village != nullptr);
    world::MapLoader::Meta mapMeta;
    REQUIRE_MESSAGE(world::MapLoader::LoadMeta(kM31Dir, &mapMeta, &error),
                    error);

    std::vector<std::string> mainChain;
    std::string previous;
    float routeTotal = 0.0f;
    for (const game::PoiDef& poi : pois.defs) {
        const world::WorldPoi* worldPoi = FindWorldPoi(definition, poi.id);
        if (poi.id == "river_hut") {
            CHECK(worldPoi == nullptr);  // resource/danger zone is content-only
        } else {
            REQUIRE_MESSAGE(worldPoi != nullptr, poi.id);
            CHECK(static_cast<float>(worldPoi->x) ==
                  doctest::Approx(poi.markerX).epsilon(1.0));
            CHECK(static_cast<float>(worldPoi->y) ==
                  doctest::Approx(poi.markerY).epsilon(1.0));
        }
        CHECK(!poi.name.empty());
        CHECK(poi.enemies.count > 0);
        REQUIRE(!poi.containers.empty());
        bool hasTarget = false;
        for (const game::PoiContainerDef& container : poi.containers) {
            hasTarget = hasTarget || container.tableId == poi.task.targetTable;
            const int wx = static_cast<int>(std::floor(container.x));
            const int wy = static_cast<int>(std::floor(container.y));
            CHECK(wx >= 0);
            CHECK(wy >= 0);
            CHECK(wx < 360);
            CHECK(wy < 360);
            const world::ChunkCoord coord = world::ChunkOf(wx, wy);
            world::ChunkData data;
            REQUIRE_MESSAGE(world::MapLoader::LoadChunk(mapMeta, coord.cx,
                                                        coord.cy, &data,
                                                        &error),
                            error);
            const int lx = wx - coord.cx * core::kChunkSize;
            const int ly = wy - coord.cy * core::kChunkSize;
            REQUIRE(lx >= 0);
            REQUIRE(lx < core::kChunkSize);
            REQUIRE(ly >= 0);
            REQUIRE(ly < core::kChunkSize);
            CHECK(data.floors[static_cast<std::size_t>(ly * core::kChunkSize + lx)] !=
                  "tile.floor.water");
        }
        CHECK(hasTarget);

        const world::RoadRoute route = graph.PlanRoute(
            village->position, glm::vec2(poi.markerX, poi.markerY), true);
        CHECK(route.outcome == world::RoadRoute::Outcome::Found);
        CHECK(route.totalDistance > 0.0f);
        routeTotal += route.totalDistance;
    }

    for (const game::MissionDef& def : missions.defs) {
        if (def.chain != "m31_slice") {
            continue;
        }
        mainChain.push_back(def.id);
        CHECK(def.order == static_cast<int>(mainChain.size()));
        CHECK(def.prerequisite == previous);
        previous = def.id;
        for (const game::MissionObjectiveDef& objective : def.objectives) {
            if (objective.kind == game::MissionObjectiveKind::Poi) {
                const game::PoiDef* target = FindPoi(pois, objective.target);
                REQUIRE_MESSAGE(target != nullptr, objective.target);
                CHECK(target->markerX > 0.0f);
                CHECK(target->markerY > 0.0f);
            } else if (objective.kind == game::MissionObjectiveKind::Reach) {
                CHECK(objective.x > 0.0f);
                CHECK(objective.y > 0.0f);
                CHECK(objective.radius > 0.0f);
            }
        }
    }
    CHECK(mainChain.size() == 5u);
    CHECK(mainChain.front() == "m31_clinic_supply");
    CHECK(mainChain.back() == "m31_return");
    CHECK(routeTotal > 500.0f);

    float tourDistance = 0.0f;
    glm::vec2 tourPoint = village->position;
    const char* tourOrder[] = {"clinic", "farmstead", "river_hut",
                               "checkpoint"};
    for (const char* id : tourOrder) {
        const game::PoiDef* stop = FindPoi(pois, id);
        REQUIRE(stop != nullptr);
        const world::RoadRoute leg =
            graph.PlanRoute(tourPoint, glm::vec2(stop->markerX, stop->markerY),
                            true);
        REQUIRE(leg.outcome == world::RoadRoute::Outcome::Found);
        tourDistance += leg.totalDistance;
        tourPoint = glm::vec2(stop->markerX, stop->markerY);
    }
    const world::RoadRoute home = graph.PlanRoute(tourPoint, village->position,
                                                   true);
    REQUIRE(home.outcome == world::RoadRoute::Outcome::Found);
    tourDistance += home.totalDistance;
    std::ostringstream tourText;
    tourText << "M31-T10 route tour: distance=" << tourDistance
             << " walkMinutes=" << (tourDistance / 4.0f / 60.0f)
             << " driveMinutes=" << (tourDistance / 10.0f / 60.0f);
    MESSAGE(tourText.str());
    CHECK(tourDistance > 800.0f);

    CHECK(FindPoi(pois, "clinic") != nullptr);
    CHECK(FindPoi(pois, "farmstead") != nullptr);
    CHECK(FindPoi(pois, "river_hut") != nullptr);
    CHECK(FindPoi(pois, "checkpoint") != nullptr);
    CHECK(FindPoi(pois, "river_hut")->task.description.find("资源/危险区") !=
          std::string::npos);
    CHECK(FindPoi(pois, "checkpoint")->threat == 3);
}

TEST_CASE("M31-T10 单机内容 + 双客户端诊所/村庄验收无阻塞") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("dual_client");
    const std::string profile = (dir / "players.bin").string();
    const std::uint64_t contentHash = 0x4D33315431304455ull;
    server::ServerHost::Options options =
        HostOptions(root, profile, contentHash, "");
    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);
    REQUIRE(host.Simulation().Graph() != nullptr);
    CHECK(host.Simulation().Graph()->Definition().mapId == "village_demo_m31");

    std::vector<std::unique_ptr<game::NetworkClient>> storage;
    std::vector<game::NetworkClient*> clients;
    for (int i = 0; i < 2; ++i) {
        storage.push_back(std::make_unique<game::NetworkClient>(
            ClientOptions(host.NetworkPort(), contentHash,
                          "t10-client-" + std::to_string(i))));
        clients.push_back(storage.back().get());
        REQUIRE(clients.back()->Connect(&error));
    }
    REQUIRE(PumpUntil(host, clients, 12000, [&] {
        for (game::NetworkClient* client : clients) {
            if (!client->IsConnected() || !client->HasPlayer() ||
                !client->HasPlayerState()) {
                return false;
            }
        }
        return host.SessionCount() == 2u;
    }));

    for (game::NetworkClient* client : clients) {
        CHECK(client->PlayerState().poiProgress.size() >= 4u);
        CHECK(FindPoiProgress(client->PlayerState(), "clinic") >= 0);
        CHECK(FindPoiProgress(client->PlayerState(), "river_hut") >= 0);
        CHECK(!client->PlayerState().missions.empty());
    }
    REQUIRE(TeleportServerPlayer(host, clients[0]->PlayerStableId(), 70.5f,
                                 26.5f));
    REQUIRE(TeleportServerPlayer(host, clients[1]->PlayerStableId(), 71.5f,
                                 26.5f));
    REQUIRE(PumpUntil(host, clients, 15000, [&] {
        return host.Simulation().Chunks().GetStats().loadedChunks > 0;
    }));
    REQUIRE(TeleportServerPlayer(host, clients[0]->PlayerStableId(), 150.5f,
                                 64.5f));
    REQUIRE(TeleportServerPlayer(host, clients[1]->PlayerStableId(), 149.5f,
                                 65.5f));
    REQUIRE(PumpUntil(host, clients, 20000, [&] {
        return FindPoiProgress(clients[0]->PlayerState(), "clinic") > 0 &&
               FindPoiProgress(clients[1]->PlayerState(), "clinic") > 0;
    }));
    CHECK(FindPoiProgress(clients[0]->PlayerState(), "clinic") > 0);
    CHECK(FindPoiProgress(clients[1]->PlayerState(), "clinic") > 0);
    int clinicContainers = 0;
    host.Simulation().Ecs().Registry().view<ecs::LootContainer>().each(
        [&](entt::entity, const ecs::LootContainer& loot) {
            if (loot.tableId == "poi_clinic_medicine" ||
                loot.tableId == "poi_clinic_supplies") {
                ++clinicContainers;
            }
        });
    CHECK(clinicContainers >= 1);
    CHECK(host.GetNetworkStats().snapshotFailures == 0);
    CHECK(host.GetNetworkStats().playerStateFailures == 0);

    for (game::NetworkClient* client : clients) {
        client->Close();
    }
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M33-F1-T6 四聚落点位可走且沿道路可达") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    ScopedWorkingDirectory cwd(root);

    core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    config.Set("settlements.file",
               std::string(ZL_SOURCE_DIR) + "/assets/settlements.json");
    game::SettlementConfig settlements;
    std::string error;
    REQUIRE_MESSAGE(game::LoadSettlementConfig(config, &settlements, &error),
                    error);
    REQUIRE(settlements.defs.size() == 4u);

    world::WorldDefinition definition;
    REQUIRE(world::LoadWorldDefinition(kM31Dir, &definition, &error) ==
            world::WorldDefinitionStatus::Loaded);
    world::WorldGraph graph(definition);
    REQUIRE(graph.Valid());
    const world::GraphNode* village = graph.FindNode("village_haven");
    REQUIRE(village != nullptr);
    world::MapLoader::Meta mapMeta;
    REQUIRE_MESSAGE(world::MapLoader::LoadMeta(kM31Dir, &mapMeta, &error),
                    error);

    const auto walkable = [&](float x, float y, std::string* reason) {
        const int wx = static_cast<int>(std::floor(x));
        const int wy = static_cast<int>(std::floor(y));
        if (wx < 0 || wy < 0 || wx >= 360 || wy >= 360) {
            *reason = "越界";
            return false;
        }
        const world::ChunkCoord coord = world::ChunkOf(wx, wy);
        world::ChunkData data;
        if (!world::MapLoader::LoadChunk(mapMeta, coord.cx, coord.cy, &data,
                                         reason)) {
            return false;
        }
        const int lx = wx - coord.cx * core::kChunkSize;
        const int ly = wy - coord.cy * core::kChunkSize;
        const std::string& floor =
            data.floors[static_cast<std::size_t>(ly * core::kChunkSize + lx)];
        if (floor == "tile.floor.water") {
            *reason = "水面";
            return false;
        }
        for (const world::ChunkObjectData& object : data.objects) {
            if (object.z == 0 && object.solid && object.localX == lx &&
                object.localY == ly) {
                *reason = "阻挡物 " + object.sprite;
                return false;
            }
        }
        return true;
    };

    CHECK(walkable(village->position.x, village->position.y, &error));
    CHECK(walkable(75.0f, 26.5f, &error));
    for (const game::SettlementDef& def : settlements.defs) {
        for (const game::SettlementNpcPoint& point : def.npcPoints) {
            std::string reason = "NPC 点位";
            const bool pointWalkable = walkable(point.x, point.y, &reason);
            const std::string pointMessage =
                def.id + " / " + point.id + "：" + reason;
            CHECK_MESSAGE(pointWalkable, pointMessage);
            CHECK(game::IsInsideSettlement(def, point.x, point.y));
        }
        const game::SettlementNpcPoint* anchor =
            def.FindNpc(game::SettlementNpcKind::Merchant);
        REQUIRE(anchor != nullptr);
        const world::RoadRoute route = graph.PlanRoute(
            village->position, glm::vec2(anchor->x, anchor->y), true);
        CHECK_MESSAGE(route.outcome == world::RoadRoute::Outcome::Found,
                      def.id);
        if (def.id != "village_haven") {
            CHECK(route.totalDistance > 0.0f);
        }
    }
}

TEST_CASE("M31-T10 返回村庄存档/重载身份一致") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("save_reload");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profile = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, &saveId);
    REQUIRE(!saveId.empty());

    const std::uint64_t contentHash = 0x4D33315431305341ull;
    server::ServerHost::Options options =
        HostOptions(root, profile, contentHash, saveId);
    options.saveRoot = saveRoot.string();
    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    game::NetworkClient client(ClientOptions(host.NetworkPort(), contentHash,
                                             "t10-save"));
    REQUIRE(client.Connect(&error));
    std::vector<game::NetworkClient*> clients{&client};
    REQUIRE(PumpUntil(host, clients, 12000, [&] {
        return client.IsConnected() && client.HasPlayer() &&
               client.HasPlayerState() &&
               host.Simulation().Chunks().GetStats().loadedChunks > 0;
    }));
    REQUIRE(TeleportServerPlayer(host, client.PlayerStableId(), 75.0f, 26.5f));
    host.RunForTicks(2);
    client.Close();
    host.Shutdown();

    save::SaveManager::Options saveOptions;
    saveOptions.root = saveRoot;
    save::SaveManager manager(saveOptions);
    save::WorldManifest manifest;
    REQUIRE_MESSAGE(manager.ReadWorldManifest(saveId, &manifest, &error) ==
                        save::SaveManager::LoadResult::Ok,
                    error);
    CHECK(manifest.identity.mapId == "village_demo_m31");
    CHECK(manifest.regions.size() == 10u);

    server::ServerHost restarted;
    server::ServerHost::Options restartedOptions =
        HostOptions(root, profile, contentHash, saveId);
    restartedOptions.saveRoot = saveRoot.string();
    REQUIRE_MESSAGE(restarted.Init(restartedOptions, &error), error);
    game::NetworkClient client2(ClientOptions(restarted.NetworkPort(),
                                              contentHash, "t10-save"));
    REQUIRE(client2.Connect(&error));
    std::vector<game::NetworkClient*> clients2{&client2};
    REQUIRE(PumpUntil(restarted, clients2, 12000, [&] {
        return client2.IsConnected() && client2.HasPlayer() &&
               client2.HasPlayerState() &&
               restarted.Simulation().Chunks().GetStats().loadedChunks > 0;
    }));
    CHECK(restarted.Simulation().Graph()->Definition().mapId ==
          "village_demo_m31");
    CHECK(restarted.GetNetworkStats().snapshotFailures == 0);
    client2.Close();
    restarted.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}
