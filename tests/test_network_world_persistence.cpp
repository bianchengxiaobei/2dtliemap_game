#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "characters/systems/CombatSystem.h"
#include "core/Constants.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/BuildingRules.h"
#include "game/Homestead.h"
#include "game/MissionComponents.h"
#include "game/PoiComponents.h"
#include "game/NetworkClient.h"
#include "game/PlayerStorage.h"
#include "game/StructureComponents.h"
#include "game/VehicleComponents.h"
#include "game/WorldQueries.h"
#include "game/WorldSimulation.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "net/Command.h"
#include "physics/PhysicsWorld.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"
#include "save/SaveManager.h"
#include "save/SaveTypes.h"
#include "server/ServerHost.h"
#include "ui/InventoryUi.h"
#include "world/ChunkManager.h"
#include "world/GridSquare.h"
#include "world/World.h"

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

std::filesystem::path TempDir(const char* name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zomboidlike_2b2b" / name;
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
    create.name = "X-F1 2b-2b";
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

int BagCount(const zl::net::PlayerState& state, const std::string& defId) {
    int count = 0;
    for (const zl::net::PlayerStateItem& item : state.bag) {
        if (item.defId == defId) {
            count += item.count;
        }
    }
    return count;
}

zl::net::Command StorageTakeAll(std::uint64_t playerId, std::uint64_t storageId) {
    zl::net::Command command;
    command.type = zl::net::CommandType::InventoryAction;
    command.playerStableId = playerId;
    command.payload.flags = static_cast<std::uint16_t>(
        zl::ui::InventoryActionType::TakeAll);
    command.payload.area = static_cast<std::uint8_t>(zl::ui::InventoryArea::Loot);
    command.payload.targetArea =
        static_cast<std::uint8_t>(zl::ui::InventoryArea::Backpack);
    command.payload.entityId = storageId;
    return command;
}

const zl::net::PlayerMissionProgress* FindClientMission(
    const zl::net::PlayerState& state, std::string_view id) {
    for (const zl::net::PlayerMissionProgress& mission : state.missions) {
        if (mission.id == id) {
            return &mission;
        }
    }
    return nullptr;
}

zl::ecs::MissionRunState* FindServerMission(zl::ecs::MissionLog& log,
                                            std::string_view id) {
    for (zl::ecs::MissionRunState& state : log.missions) {
        if (state.id == id) {
            return &state;
        }
    }
    return nullptr;
}

zl::ecs::Entity FindLootContainerByTable(entt::registry& registry,
                                         std::string_view tableId) {
    zl::ecs::Entity found = entt::null;
    registry.view<zl::ecs::LootContainer>().each(
        [&](const zl::ecs::Entity entity,
            const zl::ecs::LootContainer& loot) {
            if (found == entt::null && loot.tableId == tableId) {
                found = entity;
            }
        });
    return found;
}

int CountLootContainers(entt::registry& registry, std::string_view tableId) {
    int count = 0;
    registry.view<zl::ecs::LootContainer>().each(
        [&](const zl::ecs::Entity, const zl::ecs::LootContainer& loot) {
            if (loot.tableId == tableId) {
                ++count;
            }
        });
    return count;
}

int CountLivePoiGuards(entt::registry& registry, std::string_view poiId) {
    int count = 0;
    registry.view<zl::ecs::PoiGuard, zl::ecs::Health>().each(
        [&](const entt::entity entity, const zl::ecs::PoiGuard& guard,
            const zl::ecs::Health& health) {
            if (guard.poiId == poiId && health.current > 0.0f &&
                registry.valid(entity)) {
                ++count;
            }
        });
    return count;
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

zl::net::Command SearchLootCommand(std::uint64_t playerId,
                                  std::uint64_t containerId) {
    zl::net::Command command;
    command.type = zl::net::CommandType::SearchLootContainer;
    command.playerStableId = playerId;
    command.payload.entityId = containerId;
    return command;
}

zl::net::Command LootTransferCommand(std::uint64_t playerId,
                                    std::uint64_t containerId, int index) {
    zl::net::Command command;
    command.type = zl::net::CommandType::InventoryAction;
    command.playerStableId = playerId;
    command.payload.flags = static_cast<std::uint16_t>(
        zl::ui::InventoryActionType::Transfer);
    command.payload.area = static_cast<std::uint8_t>(zl::ui::InventoryArea::Loot);
    command.payload.targetArea =
        static_cast<std::uint8_t>(zl::ui::InventoryArea::Backpack);
    command.payload.index = index;
    command.payload.entityId = containerId;
    return command;
}

zl::net::Command MakeMove(std::uint64_t playerId, float x, float y) {
    zl::net::Command command;
    command.type = zl::net::CommandType::Move;
    command.playerStableId = playerId;
    command.payload.x = x;
    command.payload.y = y;
    return command;
}

zl::net::Command MakeVehicleCommand(zl::net::CommandType type,
                                    std::uint64_t playerId,
                                    std::uint64_t vehicleId) {
    zl::net::Command command;
    command.type = type;
    command.playerStableId = playerId;
    command.payload.entityId = vehicleId;
    return command;
}

void PickOpenDriveDirection(zl::server::ServerHost& host, float x, float y,
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
            if (host.Simulation().World().GetSquare(wx, wy, 0) == nullptr ||
                zl::game::IsSquareBlocked(host.Simulation().World(), wx, wy,
                                          0)) {
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
        (worldX - worldY) * static_cast<float>(zl::core::kTileW) * 0.5f;
    const float screenY =
        (worldX + worldY) * static_cast<float>(zl::core::kTileH) * 0.5f;
    const float length = std::sqrt(screenX * screenX + screenY * screenY);
    if (length > 1.0e-4f) {
        *outX = screenX / length;
        *outY = screenY / length;
    } else {
        *outX = 1.0f;
        *outY = 0.0f;
    }
}

}  // namespace

TEST_CASE("X-F1 2b-2b 服务端世界：chunk 重载与重启恢复结构/仓库/设施") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("world_persistence");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, &saveId);
    REQUIRE(!saveId.empty());

    std::vector<std::string> args{"ZomboidLikeTests",
                                  "--save.root=" + saveRoot.string()};
    std::vector<const char*> argv;
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }
    const std::uint64_t contentHash = 0x58463132423242ull;

    zl::server::ServerHost::Options options =
        MakeHostOptions(root, profilePath, contentHash, saveId);
    options.argc = static_cast<int>(argv.size());
    options.argv = argv.data();

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = contentHash;
    clientOptions.autoReconnect = false;
    clientOptions.playerIdentity = "2b2b-world";
    clientOptions.playerName = "World";

    zl::game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.IsConnected() && client.HasPlayer() &&
               client.HasPlayerState() &&
               host.Simulation().Chunks().GetStats().loadedChunks > 0;
    }));
    const std::uint64_t playerId = client.PlayerStableId();
    REQUIRE(playerId != 0);

    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity player = host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));
    const zl::ecs::Position playerStart =
        registry.get<zl::ecs::Position>(player);
    auto& playerBag = registry.get<zl::ecs::Container>(player);
    playerBag.items.push_back(zl::ecs::ItemInstance{"wood", 10, 1.0f});

    const auto& wallDef = zl::game::DefaultBuildables()[1];
    const auto freeCell = [&](int* outX, int* outY) {
        for (int radius = 1; radius <= 5; ++radius) {
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (std::abs(dx) != radius && std::abs(dy) != radius) {
                        continue;
                    }
                    const int wx =
                        static_cast<int>(std::floor(playerStart.x)) + dx;
                    const int wy =
                        static_cast<int>(std::floor(playerStart.y)) + dy;
                    zl::world::GridSquare* square =
                        host.Simulation().World().GetSquare(wx, wy, 0);
                    if (square == nullptr ||
                        !zl::game::IsBuildCellFreeFor(wallDef, *square)) {
                        continue;
                    }
                    bool occupied = false;
                    registry.view<zl::ecs::Position>().each(
                        [&](const zl::ecs::Position& candidate) {
                            if (!occupied && candidate.z == 0 &&
                                static_cast<int>(std::floor(candidate.x)) == wx &&
                                static_cast<int>(std::floor(candidate.y)) == wy) {
                                occupied = true;
                            }
                        });
                    if (!occupied) {
                        *outX = wx;
                        *outY = wy;
                        return true;
                    }
                }
            }
        }
        return false;
    };
    const auto wallCountAt = [&](int wx, int wy) {
        const zl::world::GridSquare* square =
            host.Simulation().World().GetSquare(wx, wy, 0);
        if (square == nullptr) {
            return 0;
        }
        int count = 0;
        for (const zl::world::SquareObject& object :
             square->Layer(zl::world::SquareLayer::Wall)) {
            if (object.playerBuilt && object.sprite == "object.wall_h") {
                ++count;
            }
        }
        return count;
    };

    int wallX = 0;
    int wallY = 0;
    REQUIRE(freeCell(&wallX, &wallY));
    zl::net::Command placeWall;
    placeWall.type = zl::net::CommandType::PlaceStructure;
    placeWall.playerStableId = playerId;
    placeWall.payload.x = static_cast<float>(wallX);
    placeWall.payload.y = static_cast<float>(wallY);
    placeWall.payload.index = 1;
    REQUIRE(client.SendCommand(placeWall, &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return wallCountAt(wallX, wallY) == 1;
    }));
    REQUIRE(zl::items::CountOf(playerBag, "wood") == 7);

    int storageX = 0;
    int storageY = 0;
    REQUIRE(freeCell(&storageX, &storageY));
    const zl::ecs::Entity storage = zl::game::SpawnPlayerStorage(
        host.Simulation().Ecs(), static_cast<float>(storageX) + 0.5f,
        static_cast<float>(storageY) + 0.5f, 0, "object.crate", 50.0f, 6);
    REQUIRE(static_cast<bool>(storage != entt::null));
    registry.emplace_or_replace<zl::ecs::StructureOwner>(
        storage, zl::ecs::StructureOwner{playerId});
    registry.get<zl::ecs::Container>(storage).items.push_back(
        zl::ecs::ItemInstance{"bandage", 3, 1.0f});
    const std::uint64_t storageId =
        host.Simulation().Ecs().IdOf(storage).id;
    REQUIRE(storageId != 0);

    int farmX = 0;
    int farmY = 0;
    REQUIRE(freeCell(&farmX, &farmY));
    const zl::ecs::Entity farm = zl::game::SpawnFarmPlot(
        host.Simulation().Ecs(), static_cast<float>(farmX) + 0.5f,
        static_cast<float>(farmY) + 0.5f, 0, zl::game::FarmConfig{});
    REQUIRE(static_cast<bool>(farm != entt::null));
    registry.emplace_or_replace<zl::ecs::StructureOwner>(
        farm, zl::ecs::StructureOwner{playerId});
    auto& plot = registry.get<zl::ecs::FarmPlot>(farm);
    plot.cropId = "cabbage";
    plot.growthMinutes = 30.0f;
    plot.waterMinutes = 120.0f;
    plot.stage = zl::ecs::CropStage::Growing;
    const std::uint64_t farmId = host.Simulation().Ecs().IdOf(farm).id;

    // 强制服务器 chunk 卸载/重载：卸载回调应把墙写入存档，重载后从 ZLCD 恢复。
    auto& playerPosition = registry.get<zl::ecs::Position>(player);
    const std::uint64_t unloadedBefore =
        host.Simulation().Chunks().GetStats().totalUnloaded;
    const zl::world::MapLoader::Meta& meta = host.Simulation().Chunks().Map();
    playerPosition.x = static_cast<float>(
                           (meta.chunkX0 + meta.chunksX - 1) * core::kChunkSize) +
                       5.5f;
    playerPosition.y = static_cast<float>(
                           (meta.chunkY0 + meta.chunksY - 1) * core::kChunkSize) +
                       5.5f;
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return host.Simulation().Chunks().GetStats().totalUnloaded >
               unloadedBefore;
    }));
    CHECK(host.Simulation().World().GetSquare(wallX, wallY, 0) == nullptr);

    playerPosition.x = playerStart.x;
    playerPosition.y = playerStart.y;
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return wallCountAt(wallX, wallY) == 1;
    }));

    client.Close();
    REQUIRE(PumpUntil(host, client, 4000, [&] { return host.SessionCount() == 0; }));
    host.Shutdown();
    CHECK(std::filesystem::exists(saveRoot / saveId / "entities.bin"));

    // 重启服务器：结构 diff、仓库内容/owner 与设施状态都应从同一存档恢复。
    zl::server::ServerHost restarted;
    zl::server::ServerHost::Options restartedOptions =
        MakeHostOptions(root, profilePath, contentHash, saveId);
    restartedOptions.argc = static_cast<int>(argv.size());
    restartedOptions.argv = argv.data();
    REQUIRE_MESSAGE(restarted.Init(restartedOptions, &error), error);
    REQUIRE(restarted.NetworkPort() != 0);

    zl::game::NetworkClient::Options restartedClientOptions = clientOptions;
    restartedClientOptions.port = restarted.NetworkPort();
    zl::game::NetworkClient restartedClient(restartedClientOptions);
    REQUIRE(restartedClient.Connect(&error));
    REQUIRE(PumpUntil(restarted, restartedClient, 6000, [&] {
        return restartedClient.IsConnected() && restartedClient.HasPlayer() &&
               restartedClient.HasPlayerState() &&
               restarted.Simulation().Chunks().GetStats().loadedChunks > 0;
    }));
    CHECK(restartedClient.PlayerStableId() == playerId);
    const auto restoredWallCountAt = [&](int wx, int wy) {
        const zl::world::GridSquare* square =
            restarted.Simulation().World().GetSquare(wx, wy, 0);
        if (square == nullptr) {
            return 0;
        }
        int count = 0;
        for (const zl::world::SquareObject& object :
             square->Layer(zl::world::SquareLayer::Wall)) {
            if (object.playerBuilt && object.sprite == "object.wall_h") {
                ++count;
            }
        }
        return count;
    };
    REQUIRE(PumpUntil(restarted, restartedClient, 5000, [&] {
        return restoredWallCountAt(wallX, wallY) == 1;
    }));
    const zl::world::GridSquare* restoredWallSquare =
        restarted.Simulation().World().GetSquare(wallX, wallY, 0);
    REQUIRE(restoredWallSquare != nullptr);
    const auto& restoredWalls =
        restoredWallSquare->Layer(zl::world::SquareLayer::Wall);
    REQUIRE(!restoredWalls.empty());
    CHECK(restoredWalls.front().ownerStableId == playerId);
    CHECK(restoredWalls.front().structureId ==
          zl::game::MakeStructureId(
              wallX, wallY, 0,
              static_cast<int>(zl::world::SquareLayer::Wall)));

    bool sawRestoredWallDiff = false;
    REQUIRE(PumpUntil(restarted, restartedClient, 5000, [&] {
        for (const auto& diff : restartedClient.TakeChunkDiffs()) {
            for (const auto& square : diff.squares) {
                for (const zl::world::SquareObject& object :
                     square.layers[static_cast<std::size_t>(
                         zl::world::SquareLayer::Wall)]) {
                    if (object.playerBuilt &&
                        object.sprite == "object.wall_h" &&
                        object.structureId ==
                            zl::game::MakeStructureId(
                                wallX, wallY, 0,
                                static_cast<int>(
                                    zl::world::SquareLayer::Wall))) {
                        sawRestoredWallDiff = true;
                    }
                }
            }
        }
        return sawRestoredWallDiff;
    }));

    auto& restartedRegistry = restarted.Simulation().Ecs().Registry();
    const zl::ecs::Entity restoredStorage =
        restarted.Simulation().Ecs().FindById(storageId);
    REQUIRE(static_cast<bool>(restoredStorage != entt::null));
    const auto* restoredOwner =
        restartedRegistry.try_get<zl::ecs::StructureOwner>(restoredStorage);
    REQUIRE(restoredOwner != nullptr);
    CHECK(restoredOwner->ownerStableId == playerId);
    const auto* restoredContents =
        restartedRegistry.try_get<zl::ecs::Container>(restoredStorage);
    REQUIRE(restoredContents != nullptr);
    CHECK(zl::items::CountOf(*restoredContents, "bandage") == 3);

    const zl::ecs::Entity restoredFarm =
        restarted.Simulation().Ecs().FindById(farmId);
    REQUIRE(static_cast<bool>(restoredFarm != entt::null));
    const auto* restoredFarmOwner =
        restartedRegistry.try_get<zl::ecs::StructureOwner>(restoredFarm);
    REQUIRE(restoredFarmOwner != nullptr);
    CHECK(restoredFarmOwner->ownerStableId == playerId);
    const auto* restoredPlot =
        restartedRegistry.try_get<zl::ecs::FarmPlot>(restoredFarm);
    REQUIRE(restoredPlot != nullptr);
    CHECK(restoredPlot->stage == zl::ecs::CropStage::Growing);
    CHECK(restoredPlot->growthMinutes >= 30.0f);
    CHECK(restoredPlot->waterMinutes > 100.0f);
    CHECK(restoredPlot->waterMinutes <= 120.0f);

    REQUIRE(PumpUntil(restarted, restartedClient, 5000, [&] {
        const auto* actor = restartedClient.Replication().Find(storageId);
        return actor != nullptr && actor->hasStorage &&
               zl::items::CountOf(actor->storageContainer, "bandage") == 3;
    }));
    REQUIRE(restartedClient.SendCommand(
        StorageTakeAll(playerId, storageId), &error));
    REQUIRE(PumpUntil(restarted, restartedClient, 5000, [&] {
        return BagCount(restartedClient.PlayerState(), "bandage") == 3;
    }));

    restartedClient.Close();
    restarted.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M21-F2b 联机电力：重连、chunk 重载与服务器重启保持供电设备") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("power_persistence");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, &saveId);
    REQUIRE(!saveId.empty());

    std::vector<std::string> args{"ZomboidLikeTests",
                                  "--save.root=" + saveRoot.string()};
    std::vector<const char*> argv;
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }
    const std::uint64_t contentHash = 0x4D323146324250ull;

    zl::server::ServerHost::Options options =
        MakeHostOptions(root, profilePath, contentHash, saveId);
    options.argc = static_cast<int>(argv.size());
    options.argv = argv.data();

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = contentHash;
    clientOptions.autoReconnect = false;
    clientOptions.playerIdentity = "f2b-power";
    clientOptions.playerName = "Power";

    zl::game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.IsConnected() && client.HasPlayer() &&
               client.HasPlayerState() &&
               host.Simulation().Chunks().GetStats().loadedChunks > 0;
    }));
    const std::uint64_t playerId = client.PlayerStableId();
    REQUIRE(playerId != 0);

    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity player = host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));
    const zl::ecs::Position playerStart =
        registry.get<zl::ecs::Position>(player);
    auto& playerBag = registry.get<zl::ecs::Container>(player);
    playerBag.items.push_back(zl::ecs::ItemInstance{"wood", 30, 1.0f});
    playerBag.items.push_back(zl::ecs::ItemInstance{"fuel_can", 1, 1.0f});

    const auto& defs = zl::game::DefaultBuildables();
    const auto findDef = [&](std::string_view id) -> const zl::game::BuildableDef* {
        for (const zl::game::BuildableDef& def : defs) {
            if (def.id == id) {
                return &def;
            }
        }
        return nullptr;
    };
    const zl::game::BuildableDef* generatorDef = findDef("generator");
    const zl::game::BuildableDef* floodlightDef = findDef("floodlight");
    const zl::game::BuildableDef* fenceDef = findDef("electric_fence");
    REQUIRE(generatorDef != nullptr);
    REQUIRE(floodlightDef != nullptr);
    REQUIRE(fenceDef != nullptr);

    const auto findFreeCell = [&](const zl::game::BuildableDef& def, int* outX,
                                  int* outY) {
        const auto* position = registry.try_get<zl::ecs::Position>(player);
        if (position == nullptr) {
            return false;
        }
        const int px = static_cast<int>(std::floor(position->x));
        const int py = static_cast<int>(std::floor(position->y));
        for (int radius = 1; radius <= 4; ++radius) {
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (std::abs(dx) != radius && std::abs(dy) != radius) {
                        continue;
                    }
                    const int wx = px + dx;
                    const int wy = py + dy;
                    zl::world::GridSquare* square =
                        host.Simulation().World().GetSquare(wx, wy, 0);
                    if (square == nullptr ||
                        !zl::game::IsBuildCellFreeFor(def, *square)) {
                        continue;
                    }
                    bool occupied = false;
                    registry.view<zl::ecs::Position>().each(
                        [&](const zl::ecs::Position& candidate) {
                            if (!occupied && candidate.z == 0 &&
                                static_cast<int>(std::floor(candidate.x)) == wx &&
                                static_cast<int>(std::floor(candidate.y)) == wy) {
                                occupied = true;
                            }
                        });
                    if (!occupied) {
                        *outX = wx;
                        *outY = wy;
                        return true;
                    }
                }
            }
        }
        return false;
    };
    const auto place = [&](const zl::game::BuildableDef& def, int* x, int* y) {
        REQUIRE(findFreeCell(def, x, y));
        zl::net::Command command;
        command.type = zl::net::CommandType::PlaceStructure;
        command.playerStableId = playerId;
        command.payload.x = static_cast<float>(*x);
        command.payload.y = static_cast<float>(*y);
        command.payload.index = static_cast<std::int32_t>(
            std::distance(defs.data(), &def));
        REQUIRE(client.SendCommand(command, &error));
        host.RunForTicks(40);
        client.Poll();
    };

    int generatorX = 0;
    int generatorY = 0;
    place(*generatorDef, &generatorX, &generatorY);
    zl::ecs::Entity generator = entt::null;
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        registry.view<zl::ecs::Generator, zl::ecs::Position>().each(
            [&](const zl::ecs::Entity entity, const zl::ecs::Generator&,
                const zl::ecs::Position& position) {
                if (static_cast<int>(std::floor(position.x)) == generatorX &&
                    static_cast<int>(std::floor(position.y)) == generatorY) {
                    generator = entity;
                }
            });
        return generator != entt::null;
    }));
    const std::uint64_t generatorId =
        host.Simulation().Ecs().IdOf(generator).id;

    int floodlightX = 0;
    int floodlightY = 0;
    place(*floodlightDef, &floodlightX, &floodlightY);
    zl::ecs::Entity floodlight = entt::null;
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        registry.view<zl::ecs::Floodlight, zl::ecs::Position>().each(
            [&](const zl::ecs::Entity entity, const zl::ecs::Floodlight&,
                const zl::ecs::Position& position) {
                if (static_cast<int>(std::floor(position.x)) == floodlightX &&
                    static_cast<int>(std::floor(position.y)) == floodlightY) {
                    floodlight = entity;
                }
            });
        return floodlight != entt::null;
    }));
    const std::uint64_t floodlightId =
        host.Simulation().Ecs().IdOf(floodlight).id;
    registry.get<zl::ecs::Health>(floodlight).current = 37.0f;

    int fenceX = 0;
    int fenceY = 0;
    place(*fenceDef, &fenceX, &fenceY);
    const auto fenceAt = [&](zl::server::ServerHost& target, int wx,
                             int wy) -> const zl::world::SquareObject* {
        const zl::world::GridSquare* square =
            target.Simulation().World().GetSquare(wx, wy, 0);
        if (square == nullptr) {
            return nullptr;
        }
        for (const zl::world::SquareLayer layer :
             {zl::world::SquareLayer::Wall, zl::world::SquareLayer::Furniture}) {
            for (const zl::world::SquareObject& object : square->Layer(layer)) {
                if (object.playerBuilt && object.hp > 0.0f &&
                    object.sprite == zl::game::kElectricFenceSprite) {
                    return &object;
                }
            }
        }
        return nullptr;
    };
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        return fenceAt(host, fenceX, fenceY) != nullptr;
    }));

    zl::net::Command startGenerator;
    startGenerator.type = zl::net::CommandType::InteractFacility;
    startGenerator.playerStableId = playerId;
    startGenerator.payload.entityId = generatorId;
    REQUIRE(client.SendCommand(startGenerator, &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return registry.get<zl::ecs::Generator>(generator).running &&
               client.PlayerState().power.load >= 2.45f &&
               !client.PlayerState().power.outage;
    }));
    const float fuelBeforeDisconnect =
        registry.get<zl::ecs::Generator>(generator).fuelMinutes;
    REQUIRE(fuelBeforeDisconnect > 200.0f);
    CHECK(registry.get<zl::ecs::Generator>(generator).load >= 1.49f);

    client.Close();
    REQUIRE(PumpUntil(host, client, 4000, [&] {
        return host.SessionCount() == 0;
    }));

    zl::game::NetworkClient reconnected(clientOptions);
    REQUIRE(reconnected.Connect(&error));
    REQUIRE(PumpUntil(host, reconnected, 6000, [&] {
        return reconnected.IsConnected() && reconnected.HasPlayer() &&
               reconnected.HasPlayerState();
    }));
    CHECK(reconnected.PlayerStableId() == playerId);
    REQUIRE(PumpUntil(host, reconnected, 5000, [&] {
        return reconnected.PlayerState().power.load >= 2.45f &&
               reconnected.Replication().Find(generatorId) != nullptr &&
               reconnected.Replication().Find(floodlightId) != nullptr;
    }));
    const auto* reconnectedGeneratorActor =
        reconnected.Replication().Find(generatorId);
    REQUIRE(reconnectedGeneratorActor != nullptr);
    REQUIRE(reconnectedGeneratorActor->hasGenerator);
    CHECK(reconnectedGeneratorActor->generator.running);
    const auto* reconnectedFloodlightActor =
        reconnected.Replication().Find(floodlightId);
    REQUIRE(reconnectedFloodlightActor != nullptr);
    REQUIRE(reconnectedFloodlightActor->hasFloodlight);
    CHECK(reconnectedFloodlightActor->floodlight.on);
    CHECK(reconnectedFloodlightActor->floodlight.powered);

    const zl::ecs::Entity reconnectedPlayer =
        host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(reconnectedPlayer != entt::null));
    auto& reconnectedPosition =
        registry.get<zl::ecs::Position>(reconnectedPlayer);
    const zl::ecs::Position home = reconnectedPosition;
    const std::uint64_t unloadedBefore =
        host.Simulation().Chunks().GetStats().totalUnloaded;
    const zl::world::MapLoader::Meta& meta =
        host.Simulation().Chunks().Map();
    const float farX = static_cast<float>(
                           (meta.chunkX0 + meta.chunksX - 1) *
                           core::kChunkSize) +
                       4.5f;
    const float farY = static_cast<float>(
                           (meta.chunkY0 + meta.chunksY - 1) *
                           core::kChunkSize) +
                       4.5f;
    if (std::abs(farX - home.x) > 1.0f || std::abs(farY - home.y) > 1.0f) {
        reconnectedPosition.x = farX;
        reconnectedPosition.y = farY;
        REQUIRE(PumpUntil(host, reconnected, 6000, [&] {
            return host.Simulation().Chunks().GetStats().totalUnloaded >
                       unloadedBefore &&
                   fenceAt(host, fenceX, fenceY) == nullptr;
        }));
        CHECK(fenceAt(host, fenceX, fenceY) == nullptr);

        reconnectedPosition.x = home.x;
        reconnectedPosition.y = home.y;
        REQUIRE(PumpUntil(host, reconnected, 6000, [&] {
            return fenceAt(host, fenceX, fenceY) != nullptr &&
                   reconnected.PlayerState().power.load >= 2.45f;
        }));
    }

    const zl::ecs::Entity reloadedGenerator =
        host.Simulation().Ecs().FindById(generatorId);
    const zl::ecs::Entity reloadedFloodlight =
        host.Simulation().Ecs().FindById(floodlightId);
    REQUIRE(static_cast<bool>(reloadedGenerator != entt::null));
    REQUIRE(static_cast<bool>(reloadedFloodlight != entt::null));
    REQUIRE(registry.get<zl::ecs::Generator>(reloadedGenerator).running);
    CHECK(registry.get<zl::ecs::Generator>(reloadedGenerator).fuelMinutes >
          0.0f);
    CHECK(registry.get<zl::ecs::Generator>(reloadedGenerator).fuelMinutes <=
          fuelBeforeDisconnect + 0.01f);
    CHECK(registry.get<zl::ecs::Floodlight>(reloadedFloodlight).on);
    CHECK(registry.get<zl::ecs::Floodlight>(reloadedFloodlight).powered);
    REQUIRE(registry.try_get<zl::ecs::Health>(reloadedFloodlight) != nullptr);
    CHECK(registry.get<zl::ecs::Health>(reloadedFloodlight).current ==
          doctest::Approx(37.0f));
    REQUIRE(fenceAt(host, fenceX, fenceY) != nullptr);
    REQUIRE(PumpUntil(host, reconnected, 5000, [&] {
        const auto* generatorActor =
            reconnected.Replication().Find(generatorId);
        const auto* floodlightActor =
            reconnected.Replication().Find(floodlightId);
        return generatorActor != nullptr && generatorActor->hasGenerator &&
               generatorActor->generator.running &&
               floodlightActor != nullptr && floodlightActor->hasFloodlight &&
               floodlightActor->floodlight.on &&
               floodlightActor->floodlight.powered &&
               floodlightActor->hasHealth &&
               floodlightActor->health.current == doctest::Approx(37.0f);
    }));

    const float fuelBeforeRestart =
        registry.get<zl::ecs::Generator>(reloadedGenerator).fuelMinutes;
    reconnected.Close();
    REQUIRE(PumpUntil(host, reconnected, 4000, [&] {
        return host.SessionCount() == 0;
    }));
    host.Shutdown();
    CHECK(std::filesystem::exists(saveRoot / saveId / "entities.bin"));

    zl::server::ServerHost restarted;
    zl::server::ServerHost::Options restartedOptions =
        MakeHostOptions(root, profilePath, contentHash, saveId);
    restartedOptions.argc = static_cast<int>(argv.size());
    restartedOptions.argv = argv.data();
    REQUIRE_MESSAGE(restarted.Init(restartedOptions, &error), error);
    REQUIRE(restarted.NetworkPort() != 0);

    zl::game::NetworkClient::Options restartedClientOptions = clientOptions;
    restartedClientOptions.port = restarted.NetworkPort();
    zl::game::NetworkClient restartedClient(restartedClientOptions);
    REQUIRE(restartedClient.Connect(&error));
    REQUIRE(PumpUntil(restarted, restartedClient, 6000, [&] {
        return restartedClient.IsConnected() && restartedClient.HasPlayer() &&
               restartedClient.HasPlayerState() &&
               restarted.Simulation().Chunks().GetStats().loadedChunks > 0;
    }));
    CHECK(restartedClient.PlayerStableId() == playerId);
    REQUIRE(PumpUntil(restarted, restartedClient, 6000, [&] {
        return restartedClient.PlayerState().power.load >= 2.45f;
    }));

    const zl::ecs::Entity restoredGenerator =
        restarted.Simulation().Ecs().FindById(generatorId);
    const zl::ecs::Entity restoredFloodlight =
        restarted.Simulation().Ecs().FindById(floodlightId);
    REQUIRE(static_cast<bool>(restoredGenerator != entt::null));
    REQUIRE(static_cast<bool>(restoredFloodlight != entt::null));
    const auto& restoredGeneratorState =
        restarted.Simulation().Ecs().Registry().get<zl::ecs::Generator>(
            restoredGenerator);
    CHECK(restoredGeneratorState.running);
    CHECK(restoredGeneratorState.fuelMinutes > 0.0f);
    CHECK(restoredGeneratorState.fuelMinutes <= fuelBeforeRestart + 0.01f);
    CHECK(restoredGeneratorState.load >= 1.49f);
    const auto& restoredFloodlightState =
        restarted.Simulation().Ecs().Registry().get<zl::ecs::Floodlight>(
            restoredFloodlight);
    CHECK(restoredFloodlightState.on);
    CHECK(restoredFloodlightState.powered);
    const auto* restoredLightHealth =
        restarted.Simulation().Ecs().Registry().try_get<zl::ecs::Health>(
            restoredFloodlight);
    REQUIRE(restoredLightHealth != nullptr);
    CHECK(restoredLightHealth->current == doctest::Approx(37.0f));
    const zl::world::SquareObject* restoredFence =
        fenceAt(restarted, fenceX, fenceY);
    REQUIRE(restoredFence != nullptr);
    CHECK(restoredFence->hp > 0.0f);
    CHECK(restoredFence->ownerStableId == playerId);
    REQUIRE(PumpUntil(restarted, restartedClient, 5000, [&] {
        const auto* generatorActor =
            restartedClient.Replication().Find(generatorId);
        const auto* floodlightActor =
            restartedClient.Replication().Find(floodlightId);
        return generatorActor != nullptr && generatorActor->hasGenerator &&
               generatorActor->generator.running &&
               floodlightActor != nullptr && floodlightActor->hasFloodlight &&
               floodlightActor->floodlight.on &&
               floodlightActor->floodlight.powered &&
               floodlightActor->hasHealth &&
               floodlightActor->health.current == doctest::Approx(37.0f);
    }));

    restartedClient.Close();
    restarted.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("X-F1 2b-2b 并发拆建/维修：同一格只允许一次成功") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.spawnDemoPopulation = false;
    options.spawnLocalPlayer = false;
    options.network = true;
    options.port = 0;
    options.contentHash = 0x58463132423243ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = options.contentHash;
    clientOptions.autoReconnect = false;

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

    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity firstPlayer =
        host.Simulation().Ecs().FindById(firstId);
    const zl::ecs::Entity secondPlayer =
        host.Simulation().Ecs().FindById(secondId);
    REQUIRE(static_cast<bool>(firstPlayer != entt::null));
    REQUIRE(static_cast<bool>(secondPlayer != entt::null));
    const auto& firstPosition =
        registry.get<zl::ecs::Position>(firstPlayer);
    auto& secondPosition = registry.get<zl::ecs::Position>(secondPlayer);
    secondPosition.x = firstPosition.x + 0.25f;
    secondPosition.y = firstPosition.y;

    auto& firstBag = registry.get<zl::ecs::Container>(firstPlayer);
    auto& secondBag = registry.get<zl::ecs::Container>(secondPlayer);
    firstBag.items.clear();
    secondBag.items.clear();
    firstBag.items.push_back(zl::ecs::ItemInstance{"wood", 10, 1.0f});
    secondBag.items.push_back(zl::ecs::ItemInstance{"wood", 10, 1.0f});

    const auto& wallDef = zl::game::DefaultBuildables()[1];
    const auto freeCell = [&](int* outX, int* outY) {
        const int px = static_cast<int>(std::floor(firstPosition.x));
        const int py = static_cast<int>(std::floor(firstPosition.y));
        for (int radius = 1; radius <= 4; ++radius) {
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (std::abs(dx) != radius && std::abs(dy) != radius) {
                        continue;
                    }
                    const int wx = px + dx;
                    const int wy = py + dy;
                    zl::world::GridSquare* square =
                        host.Simulation().World().GetSquare(wx, wy, 0);
                    if (square == nullptr ||
                        !zl::game::IsBuildCellFreeFor(wallDef, *square)) {
                        continue;
                    }
                    bool occupied = false;
                    registry.view<zl::ecs::Position>().each(
                        [&](const zl::ecs::Position& candidate) {
                            if (!occupied && candidate.z == 0 &&
                                static_cast<int>(std::floor(candidate.x)) == wx &&
                                static_cast<int>(std::floor(candidate.y)) == wy) {
                                occupied = true;
                            }
                        });
                    if (!occupied) {
                        *outX = wx;
                        *outY = wy;
                        return true;
                    }
                }
            }
        }
        return false;
    };
    const auto wallCountAt = [&](int wx, int wy) {
        const zl::world::GridSquare* square =
            host.Simulation().World().GetSquare(wx, wy, 0);
        if (square == nullptr) {
            return 0;
        }
        int count = 0;
        for (const zl::world::SquareObject& object :
             square->Layer(zl::world::SquareLayer::Wall)) {
            if (object.playerBuilt && object.sprite == "object.wall_h") {
                ++count;
            }
        }
        return count;
    };

    int wallX = 0;
    int wallY = 0;
    REQUIRE(freeCell(&wallX, &wallY));
    zl::net::Command firstPlace;
    firstPlace.type = zl::net::CommandType::PlaceStructure;
    firstPlace.playerStableId = firstId;
    firstPlace.payload.x = static_cast<float>(wallX);
    firstPlace.payload.y = static_cast<float>(wallY);
    firstPlace.payload.index = 1;
    zl::net::Command secondPlace = firstPlace;
    secondPlace.playerStableId = secondId;

    const std::uint64_t acceptedBefore =
        host.GetNetworkStats().commandsAccepted;
    const std::uint64_t rejectedBefore =
        host.GetNetworkStats().commandsRejected;
    const std::uint64_t receivedBefore =
        host.GetNetworkStats().commandsReceived;
    REQUIRE(first.SendCommand(firstPlace, &error));
    REQUIRE(second.SendCommand(secondPlace, &error));
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return host.GetNetworkStats().commandsReceived >= receivedBefore + 2;
    }));
    CHECK(wallCountAt(wallX, wallY) == 1);
    CHECK(host.GetNetworkStats().commandsAccepted == acceptedBefore + 1);
    CHECK(host.GetNetworkStats().commandsRejected > rejectedBefore);
    CHECK(zl::items::CountOf(firstBag, "wood") +
              zl::items::CountOf(secondBag, "wood") ==
          17);

    // 无主测试墙：两个玩家同一 tick 维修，只能有一个人扣木材成功。
    auto& wallObjects =
        host.Simulation().World()
            .GetSquare(wallX, wallY, 0)
            ->Layer(zl::world::SquareLayer::Wall);
    REQUIRE(!wallObjects.empty());
    wallObjects.front().ownerStableId = 0;
    wallObjects.front().hp = wallDef.maxHp - 10.0f;
    firstBag.items.push_back(zl::ecs::ItemInstance{"wood", 5, 1.0f});
    secondBag.items.push_back(zl::ecs::ItemInstance{"wood", 5, 1.0f});

    zl::net::Command firstRepair;
    firstRepair.type = zl::net::CommandType::RepairStructure;
    firstRepair.playerStableId = firstId;
    firstRepair.payload.x = static_cast<float>(wallX);
    firstRepair.payload.y = static_cast<float>(wallY);
    zl::net::Command secondRepair = firstRepair;
    secondRepair.playerStableId = secondId;

    host.RunForTicks(40);
    first.Poll();
    second.Poll();
    const std::uint64_t repairAcceptedBefore =
        host.GetNetworkStats().commandsAccepted;
    const std::uint64_t repairRejectedBefore =
        host.GetNetworkStats().commandsRejected;
    const std::uint64_t repairReceivedBefore =
        host.GetNetworkStats().commandsReceived;
    REQUIRE(first.SendCommand(firstRepair, &error));
    REQUIRE(second.SendCommand(secondRepair, &error));
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return host.GetNetworkStats().commandsReceived >=
               repairReceivedBefore + 2;
    }));
    CHECK(wallObjects.front().hp == doctest::Approx(wallDef.maxHp));
    CHECK(host.GetNetworkStats().commandsAccepted ==
          repairAcceptedBefore + 1);
    CHECK(host.GetNetworkStats().commandsRejected > repairRejectedBefore);
    CHECK(zl::items::CountOf(firstBag, "wood") +
              zl::items::CountOf(secondBag, "wood") ==
          26);

    // 并发拆除无主墙：只应移除一次并只返还一次木材。
    wallObjects.front().ownerStableId = 0;
    host.RunForTicks(40);
    first.Poll();
    second.Poll();
    const std::uint64_t demolishAcceptedBefore =
        host.GetNetworkStats().commandsAccepted;
    const std::uint64_t demolishRejectedBefore =
        host.GetNetworkStats().commandsRejected;
    const std::uint64_t demolishReceivedBefore =
        host.GetNetworkStats().commandsReceived;
    zl::net::Command firstDemolish;
    firstDemolish.type = zl::net::CommandType::DemolishStructure;
    firstDemolish.playerStableId = firstId;
    firstDemolish.payload.x = static_cast<float>(wallX);
    firstDemolish.payload.y = static_cast<float>(wallY);
    zl::net::Command secondDemolish = firstDemolish;
    secondDemolish.playerStableId = secondId;
    REQUIRE(first.SendCommand(firstDemolish, &error));
    REQUIRE(second.SendCommand(secondDemolish, &error));
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return host.GetNetworkStats().commandsReceived >=
               demolishReceivedBefore + 2;
    }));
    CHECK(wallCountAt(wallX, wallY) == 0);
    CHECK(host.GetNetworkStats().commandsAccepted ==
          demolishAcceptedBefore + 1);
    CHECK(host.GetNetworkStats().commandsRejected > demolishRejectedBefore);
    CHECK(zl::items::CountOf(firstBag, "wood") +
              zl::items::CountOf(secondBag, "wood") ==
          28);

    first.Close();
    second.Close();
    host.Shutdown();
}

TEST_CASE("M23-F2a-T7 服务器觅食耗尽状态跨存档恢复与刷新") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("forage_persistence");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, &saveId);
    REQUIRE(!saveId.empty());

    std::vector<std::string> args{"ZomboidLikeTests",
                                  "--save.root=" + saveRoot.string()};
    std::vector<const char*> argv;
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }
    const std::uint64_t contentHash = 0x4632415437000003ull;

    zl::server::ServerHost::Options options =
        MakeHostOptions(root, profilePath, contentHash, saveId);
    options.argc = static_cast<int>(argv.size());
    options.argv = argv.data();

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = contentHash;
    clientOptions.autoReconnect = false;
    clientOptions.playerIdentity = "t7-forage-persistence";
    clientOptions.playerName = "Forager";

    zl::game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.HasPlayerState() && !client.PlayerState().forageNodes.empty();
    }));

    auto& registry = host.Simulation().Ecs().Registry();
    const std::uint64_t playerId = client.PlayerStableId();
    REQUIRE(playerId != 0);
    const auto teleport = [&](float x, float y) {
        const zl::ecs::Entity entity =
            host.Simulation().Ecs().FindById(playerId);
        REQUIRE(static_cast<bool>(entity != entt::null));
        auto& position = registry.get<zl::ecs::Position>(entity);
        position.x = x;
        position.y = y;
        if (auto* body = registry.try_get<zl::ecs::RigidBody>(entity);
            body != nullptr && body->body != nullptr) {
            host.Simulation().Physics().TeleportBody(body->body, x, y,
                                                     position.z);
        }
        host.RunForTicks(1);
    };

    teleport(70.0f, 24.0f);
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.HasPlayerState() && !client.PlayerState().forageNodes.empty();
    }));
    const auto findMeadow = [&]() -> const zl::net::PlayerForageNodeState* {
        for (const auto& node : client.PlayerState().forageNodes) {
            if (node.zoneId == "meadow_grass" && node.ready) {
                return &node;
            }
        }
        return nullptr;
    };
    const zl::net::PlayerForageNodeState* found = findMeadow();
    if (found == nullptr) {
        teleport(78.0f, 22.0f);
        REQUIRE(PumpUntil(host, client, 6000, [&] {
            return client.HasPlayerState() &&
                   !client.PlayerState().forageNodes.empty();
        }));
        found = findMeadow();
    }
    REQUIRE_MESSAGE(found != nullptr, "附近没有可采集的草地节点");
    const zl::net::PlayerForageNodeState target = *found;

    teleport(static_cast<float>(target.x) + 0.5f,
             static_cast<float>(target.y) + 0.5f);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return client.HasPlayerState() && !client.PlayerState().forageNodes.empty();
    }));

    const zl::ecs::Entity player = host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));
    auto& bag = registry.get<zl::ecs::Container>(player);
    bag.items.push_back(zl::ecs::ItemInstance{"forage_stick", 1, 0.5f});

    zl::net::Command start;
    start.playerStableId = playerId;
    start.type = zl::net::CommandType::ForageStart;
    start.payload.x = static_cast<float>(target.x) + 0.5f;
    start.payload.y = static_cast<float>(target.y) + 0.5f;
    start.payload.targetIndex = target.z;
    REQUIRE(client.SendCommand(start, &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return client.PlayerState().survival.mode ==
               zl::net::kPlayerSurvivalModeForage;
    }));
    REQUIRE(PumpUntil(host, client, 10000, [&] {
        return client.PlayerState().survival.mode ==
                   zl::net::kPlayerSurvivalModeNone &&
               client.PlayerState().status.find("采集了") != std::string::npos;
    }));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        for (const auto& nearby : client.PlayerState().forageNodes) {
            if (nearby.x == target.x && nearby.y == target.y &&
                nearby.z == target.z) {
                return !nearby.ready;
            }
        }
        return false;
    }));
    CHECK(zl::items::CountOf(bag, "forage_stick") == 1);
    const double minutesBeforeSave = host.GameMinutes();
    CHECK(minutesBeforeSave > 0.0);

    client.Close();
    REQUIRE(PumpUntil(host, client, 4000, [&] { return host.SessionCount() == 0; }));
    host.Shutdown();

    zl::server::ServerHost restarted;
    REQUIRE_MESSAGE(restarted.Init(options, &error), error);
    CHECK(restarted.GameMinutes() >= minutesBeforeSave - 1.0);

    zl::game::NetworkClient::Options restartedClientOptions = clientOptions;
    restartedClientOptions.port = restarted.NetworkPort();
    zl::game::NetworkClient restartedClient(restartedClientOptions);
    REQUIRE(restartedClient.Connect(&error));
    REQUIRE(PumpUntil(restarted, restartedClient, 6000, [&] {
        return restartedClient.HasPlayerState() &&
               !restartedClient.PlayerState().forageNodes.empty();
    }));

    auto& restartedRegistry = restarted.Simulation().Ecs().Registry();
    const std::uint64_t restartedPlayerId = restartedClient.PlayerStableId();
    REQUIRE(restartedPlayerId == playerId);
    const zl::ecs::Entity restartedPlayer =
        restarted.Simulation().Ecs().FindById(restartedPlayerId);
    REQUIRE(static_cast<bool>(restartedPlayer != entt::null));
    const auto restartedTeleport = [&](float x, float y) {
        auto& position = restartedRegistry.get<zl::ecs::Position>(
            restartedPlayer);
        position.x = x;
        position.y = y;
        if (auto* body = restartedRegistry.try_get<zl::ecs::RigidBody>(
                restartedPlayer);
            body != nullptr && body->body != nullptr) {
            restarted.Simulation().Physics().TeleportBody(
                body->body, x, y, position.z);
        }
        restarted.RunForTicks(1);
    };
    restartedTeleport(static_cast<float>(target.x) + 0.5f,
                      static_cast<float>(target.y) + 0.5f);
    REQUIRE(PumpUntil(restarted, restartedClient, 6000, [&] {
        return restartedClient.HasPlayerState() &&
               !restartedClient.PlayerState().forageNodes.empty();
    }));
    bool restoredExhausted = false;
    for (const auto& nearby : restartedClient.PlayerState().forageNodes) {
        if (nearby.x == target.x && nearby.y == target.y &&
            nearby.z == target.z) {
            restoredExhausted = !nearby.ready;
            break;
        }
    }
    CHECK(restoredExhausted);
    const auto& restartedBag =
        restartedRegistry.get<zl::ecs::Container>(restartedPlayer);
    CHECK(zl::items::CountOf(restartedBag, "forage_stick") == 1);
    for (const auto& item : restartedBag.items) {
        if (item.defId == "forage_stick") {
            CHECK(item.condition < 0.5f);
            break;
        }
    }

    restarted.AdvanceGameMinutes(200.0);
    restarted.RunForTicks(90);
    REQUIRE(PumpUntil(restarted, restartedClient, 6000, [&] {
        for (const auto& nearby : restartedClient.PlayerState().forageNodes) {
            if (nearby.x == target.x && nearby.y == target.y &&
                nearby.z == target.z) {
                return nearby.ready;
            }
        }
        return false;
    }));

    restartedClient.Close();
    restarted.Shutdown();
}

TEST_CASE("M24-T7 新 POI/事件/任务跨服务器重启恢复且不重复") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("m24_t7_persistence");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, &saveId);
    REQUIRE(!saveId.empty());

    std::vector<std::string> args{"ZomboidLikeTests",
                                  "--save.root=" + saveRoot.string()};
    std::vector<const char*> argv;
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }
    const std::uint64_t contentHash = 0x4D32345437534156ull;

    zl::server::ServerHost::Options options =
        MakeHostOptions(root, profilePath, contentHash, saveId);
    options.playerTargetX = 10.5f;
    options.playerTargetY = 37.5f;
    options.argc = static_cast<int>(argv.size());
    options.argv = argv.data();

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = contentHash;
    clientOptions.autoReconnect = false;
    clientOptions.playerIdentity = "m24-t7-save";
    clientOptions.playerName = "Saver";

    zl::game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 8000, [&] {
        return client.IsConnected() && client.HasPlayerState() &&
               host.Simulation().Chunks().GetStats().loadedChunks > 0;
    }));
    const std::uint64_t playerId = client.PlayerStableId();
    REQUIRE(playerId != 0);
    auto& registry = host.Simulation().Ecs().Registry();
    zl::ecs::Entity player = host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));
    auto* health = registry.try_get<zl::ecs::Health>(player);
    REQUIRE(health != nullptr);
    health->current = 1000.0f;
    health->max = 1000.0f;
    REQUIRE(TeleportServerPlayer(host, playerId, 10.5f, 37.5f));

    zl::ecs::Entity motelContainer = entt::null;
    REQUIRE(PumpUntil(host, client, 10000, [&] {
        motelContainer =
            FindLootContainerByTable(registry, "poi_motel_storage");
        return motelContainer != entt::null;
    }));
    const std::uint64_t motelContainerId =
        host.Simulation().Ecs().IdOf(motelContainer).id;
    REQUIRE(CountLootContainers(registry, "poi_motel_storage") == 1);
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return CountLivePoiGuards(registry, "motel") > 0;
    }));
    registry.view<zl::ecs::PoiGuard, zl::ecs::Health>().each(
        [&](const entt::entity entity, const zl::ecs::PoiGuard& guard,
            zl::ecs::Health& guardHealth) {
            if (guard.poiId == "motel" && guardHealth.current > 0.0f) {
                zl::ecs::DamageEvent event;
                event.amount = 1000.0f;
                event.type = zl::ecs::DamageType::Blunt;
                (void)host.Simulation().Combat()->ApplyDamage(
                    host.Simulation().Ecs(), entity, event);
            }
        });
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return CountLivePoiGuards(registry, "motel") == 0;
    }));

    const int fruitBefore = BagCount(client.PlayerState(), "canned_fruit");
    REQUIRE(client.SendCommand(
        SearchLootCommand(playerId, motelContainerId), &error));
    REQUIRE(PumpUntil(host, client, 8000, [&] {
        const auto* mission =
            FindClientMission(client.PlayerState(), "motel_guest");
        return mission != nullptr && mission->status == 1 &&
               mission->rewardClaimed;
    }));
    const int expectedFruit = fruitBefore + 2;
    CHECK(BagCount(client.PlayerState(), "canned_fruit") == expectedFruit);

    auto* motelItems = registry.try_get<zl::ecs::Container>(motelContainer);
    REQUIRE(motelItems != nullptr);
    REQUIRE(!motelItems->items.empty());
    const std::size_t motelStacksBeforeTake = motelItems->items.size();
    REQUIRE(client.SendCommand(
        LootTransferCommand(playerId, motelContainerId, 0), &error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        const auto* actor = client.Replication().Find(motelContainerId);
        return actor != nullptr && actor->hasLoot &&
               actor->lootContainer.searched &&
               actor->lootContainerItems.items.size() <
                   motelStacksBeforeTake;
    }));
    const std::size_t motelItemsAfterTake = motelItems->items.size();
    CHECK(motelItemsAfterTake < motelStacksBeforeTake);
    CHECK(CountLootContainers(registry, "poi_motel_storage") == 1);

    REQUIRE(TeleportServerPlayer(host, playerId, 62.5f, 29.5f));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return host.Simulation().World().GetSquare(62, 29, 0) != nullptr;
    }));
    auto& log = registry.get_or_emplace<zl::ecs::MissionLog>(player);
    log.lastDay = 1;
    log.randomDay = 1;
    zl::ecs::MissionRunState* supply =
        FindServerMission(log, "event_supply_drop");
    if (supply == nullptr) {
        log.missions.push_back(zl::ecs::MissionRunState{});
        supply = &log.missions.back();
    }
    supply->id = "event_supply_drop";
    supply->progress.assign(2, 0);
    supply->status = zl::ecs::MissionRunStatus::Active;
    supply->day = 1;
    supply->rewardClaimed = false;
    REQUIRE(PumpUntil(host, client, 8000, [&] {
        return CountLootContainers(registry, "event_supply_crate") == 2;
    }));

    std::vector<zl::ecs::Entity> supplyCrates;
    registry.view<zl::ecs::LootContainer, zl::ecs::Position>().each(
        [&](const zl::ecs::Entity entity, const zl::ecs::LootContainer& loot,
            const zl::ecs::Position& position) {
            static_cast<void>(position);
            if (loot.tableId == "event_supply_crate") {
                supplyCrates.push_back(entity);
            }
        });
    REQUIRE(supplyCrates.size() == 2);
    std::sort(supplyCrates.begin(), supplyCrates.end(),
              [&](const zl::ecs::Entity& lhs, const zl::ecs::Entity& rhs) {
                  const auto* lhsPosition =
                      registry.try_get<zl::ecs::Position>(lhs);
                  const auto* rhsPosition =
                      registry.try_get<zl::ecs::Position>(rhs);
                  return lhsPosition != nullptr && rhsPosition != nullptr &&
                         lhsPosition->x < rhsPosition->x;
              });
    const std::uint64_t supplyId1 =
        host.Simulation().Ecs().IdOf(supplyCrates[0]).id;
    const auto* supplyPosition =
        registry.try_get<zl::ecs::Position>(supplyCrates[0]);
    REQUIRE(supplyPosition != nullptr);
    REQUIRE(TeleportServerPlayer(host, playerId, supplyPosition->x,
                                 supplyPosition->y));
    REQUIRE(client.SendCommand(SearchLootCommand(playerId, supplyId1),
                               &error));
    REQUIRE(PumpUntil(host, client, 8000, [&] {
        const auto* mission =
            FindClientMission(client.PlayerState(), "event_supply_drop");
        return mission != nullptr && !mission->progress.empty() &&
               mission->progress[0] == 1;
    }));
    CHECK(CountLootContainers(registry, "event_supply_crate") == 2);
    int searchedCrates = 0;
    for (const zl::ecs::Entity crate : supplyCrates) {
        const auto* loot = registry.try_get<zl::ecs::LootContainer>(crate);
        if (loot != nullptr && loot->searched) {
            ++searchedCrates;
        }
    }
    CHECK(searchedCrates == 1);

    client.Close();
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return host.SessionCount() == 0;
    }));
    host.Shutdown();
    CHECK(std::filesystem::exists(saveRoot / saveId / "entities.bin"));

    zl::server::ServerHost restarted;
    zl::server::ServerHost::Options restartedOptions =
        MakeHostOptions(root, profilePath, contentHash, saveId);
    restartedOptions.playerTargetX = 10.5f;
    restartedOptions.playerTargetY = 37.5f;
    restartedOptions.argc = static_cast<int>(argv.size());
    restartedOptions.argv = argv.data();
    REQUIRE_MESSAGE(restarted.Init(restartedOptions, &error), error);
    REQUIRE(restarted.NetworkPort() != 0);

    zl::game::NetworkClient::Options restartedOptionsClient = clientOptions;
    restartedOptionsClient.port = restarted.NetworkPort();
    zl::game::NetworkClient restartedClient(restartedOptionsClient);
    REQUIRE(restartedClient.Connect(&error));
    REQUIRE(PumpUntil(restarted, restartedClient, 8000, [&] {
        return restartedClient.HasPlayerState() &&
               restartedClient.PlayerStableId() == playerId;
    }));

    auto& restartedRegistry = restarted.Simulation().Ecs().Registry();
    zl::ecs::Entity restoredMotel = entt::null;
    REQUIRE(PumpUntil(restarted, restartedClient, 8000, [&] {
        restoredMotel =
            FindLootContainerByTable(restartedRegistry, "poi_motel_storage");
        return restoredMotel != entt::null &&
               CountLootContainers(restartedRegistry,
                                   "event_supply_crate") == 2;
    }));
    CHECK(CountLootContainers(restartedRegistry, "poi_motel_storage") == 1);
    const auto* restoredLoot =
        restartedRegistry.try_get<zl::ecs::LootContainer>(restoredMotel);
    const auto* restoredItems =
        restartedRegistry.try_get<zl::ecs::Container>(restoredMotel);
    REQUIRE(restoredLoot != nullptr);
    REQUIRE(restoredItems != nullptr);
    CHECK(restoredLoot->searched);
    CHECK(restoredItems->items.size() == motelItemsAfterTake);
    restarted.RunForTicks(20);
    CHECK(CountLivePoiGuards(restartedRegistry, "motel") == 0);

    const auto* restoredMission =
        FindClientMission(restartedClient.PlayerState(), "motel_guest");
    REQUIRE(restoredMission != nullptr);
    CHECK(restoredMission->status == 1);
    CHECK(restoredMission->rewardClaimed);
    CHECK(BagCount(restartedClient.PlayerState(), "canned_fruit") ==
          expectedFruit);

    CHECK(CountLootContainers(restartedRegistry, "event_supply_crate") == 2);
    int restoredSearched = 0;
    restartedRegistry.view<zl::ecs::LootContainer>().each(
        [&](const zl::ecs::Entity, const zl::ecs::LootContainer& loot) {
            if (loot.tableId == "event_supply_crate" && loot.searched) {
                ++restoredSearched;
            }
        });
    CHECK(restoredSearched == 1);
    const auto* restoredSupply =
        FindClientMission(restartedClient.PlayerState(), "event_supply_drop");
    REQUIRE(restoredSupply != nullptr);
    CHECK(restoredSupply->status == 0);
    REQUIRE(!restoredSupply->progress.empty());
    CHECK(restoredSupply->progress[0] == 1);

    restarted.RunForTicks(30);
    CHECK(BagCount(restartedClient.PlayerState(), "canned_fruit") ==
          expectedFruit);
    CHECK(CountLootContainers(restartedRegistry, "event_supply_crate") == 2);
    CHECK(CountLootContainers(restartedRegistry, "poi_motel_storage") == 1);

    restartedClient.Close();
    restarted.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M25-T3-V3 服务器载具世界存档：重启恢复且座位不持久化") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("m25_t3_v3_vehicle");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, &saveId);
    REQUIRE(!saveId.empty());

    std::vector<std::string> args{"ZomboidLikeTests",
                                  "--save.root=" + saveRoot.string()};
    std::vector<const char*> argv;
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }
    const std::uint64_t contentHash = 0x4D323554335650ull;

    zl::server::ServerHost::Options options =
        MakeHostOptions(root, profilePath, contentHash, saveId);
    options.argc = static_cast<int>(argv.size());
    options.argv = argv.data();

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = contentHash;
    clientOptions.autoReconnect = false;
    clientOptions.playerIdentity = "m25-t3-v3-vehicle";
    clientOptions.playerName = "Vehicle";

    zl::game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 8000, [&] {
        return client.IsConnected() && client.HasPlayerState() &&
               host.VehicleStableId() != 0;
    }));
    const std::uint64_t playerId = client.PlayerStableId();
    const std::uint64_t vehicleId = host.VehicleStableId();
    REQUIRE(playerId != 0);
    REQUIRE(vehicleId != 0);

    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity player = host.Simulation().Ecs().FindById(playerId);
    const zl::ecs::Entity vehicle =
        host.Simulation().Ecs().FindById(vehicleId);
    REQUIRE(static_cast<bool>(player != entt::null));
    REQUIRE(static_cast<bool>(vehicle != entt::null));
    const zl::ecs::Position vehicleStart =
        registry.get<zl::ecs::Position>(vehicle);
    REQUIRE(TeleportServerPlayer(host, playerId, vehicleStart.x - 1.0f,
                                 vehicleStart.y));

    REQUIRE_MESSAGE(client.SendCommand(
                        MakeVehicleCommand(zl::net::CommandType::EnterVehicle,
                                           playerId, vehicleId),
                        &error),
                    error);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return client.PlayerState().vehicle.driving;
    }));

    float worldDirX = 1.0f;
    float worldDirY = 0.0f;
    PickOpenDriveDirection(host, vehicleStart.x, vehicleStart.y, &worldDirX,
                           &worldDirY);
    float screenDirX = 1.0f;
    float screenDirY = 0.0f;
    WorldDirToScreen(worldDirX, worldDirY, &screenDirX, &screenDirY);
    REQUIRE_MESSAGE(client.SendCommand(
                        MakeMove(playerId, screenDirX, screenDirY), &error),
                    error);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto& position = registry.get<zl::ecs::Position>(vehicle);
        const float dx = position.x - vehicleStart.x;
        const float dy = position.y - vehicleStart.y;
        return dx * dx + dy * dy > 0.20f * 0.20f;
    }));

    const zl::ecs::Position vehicleSaved =
        registry.get<zl::ecs::Position>(vehicle);
    const zl::ecs::Vehicle vehicleStateSaved =
        registry.get<zl::ecs::Vehicle>(vehicle);
    auto& trunk = registry.get<zl::ecs::Container>(vehicle);
    trunk.items.push_back(zl::ecs::ItemInstance{"repair_kit", 1, 1.0f});

    client.Close();
    host.Shutdown();

    zl::server::ServerHost restarted;
    REQUIRE_MESSAGE(restarted.Init(options, &error), error);
    REQUIRE(restarted.NetworkPort() != 0);

    zl::game::NetworkClient::Options restartedClientOptions = clientOptions;
    restartedClientOptions.port = restarted.NetworkPort();
    zl::game::NetworkClient restartedClient(restartedClientOptions);
    REQUIRE(restartedClient.Connect(&error));
    REQUIRE(PumpUntil(restarted, restartedClient, 8000, [&] {
        return restartedClient.IsConnected() &&
               restartedClient.HasPlayerState() &&
               restarted.VehicleStableId() != 0;
    }));

    auto& restoredRegistry = restarted.Simulation().Ecs().Registry();
    int vehicleCount = 0;
    restoredRegistry.view<zl::ecs::Vehicle>().each(
        [&](const zl::ecs::Entity, const zl::ecs::Vehicle&) {
            ++vehicleCount;
        });
    CHECK(vehicleCount >= 1);
    CHECK(restarted.VehicleStableIds().size() >= 1);
    CHECK(restarted.VehicleStableId() == vehicleId);

    const zl::ecs::Entity restoredVehicle =
        restarted.Simulation().Ecs().FindById(vehicleId);
    REQUIRE(static_cast<bool>(restoredVehicle != entt::null));
    const auto* restoredPosition =
        restoredRegistry.try_get<zl::ecs::Position>(restoredVehicle);
    const auto* restoredState =
        restoredRegistry.try_get<zl::ecs::Vehicle>(restoredVehicle);
    const auto* restoredDriver =
        restoredRegistry.try_get<zl::ecs::VehicleDriver>(restoredVehicle);
    const auto* restoredTrunk =
        restoredRegistry.try_get<zl::ecs::Container>(restoredVehicle);
    REQUIRE(restoredPosition != nullptr);
    REQUIRE(restoredState != nullptr);
    REQUIRE(restoredDriver != nullptr);
    REQUIRE(restoredTrunk != nullptr);
    CHECK(restoredPosition->x ==
          doctest::Approx(vehicleSaved.x).epsilon(0.01));
    CHECK(restoredPosition->y ==
          doctest::Approx(vehicleSaved.y).epsilon(0.01));
    CHECK(restoredState->fuel ==
          doctest::Approx(vehicleStateSaved.fuel).epsilon(0.001));
    CHECK(restoredState->condition ==
          doctest::Approx(vehicleStateSaved.condition).epsilon(0.001));
    CHECK(restoredDriver->stableId == 0);
    CHECK(zl::items::CountOf(*restoredTrunk, "repair_kit") == 1);
    CHECK_FALSE(restartedClient.PlayerState().vehicle.driving);
    CHECK(restartedClient.PlayerState().vehicle.vehicleStableId == vehicleId);

    const zl::ecs::Entity restoredPlayer =
        restarted.Simulation().Ecs().FindById(
            restartedClient.PlayerStableId());
    REQUIRE(static_cast<bool>(restoredPlayer != entt::null));
    const auto* restoredPlayerPosition =
        restoredRegistry.try_get<zl::ecs::Position>(restoredPlayer);
    REQUIRE(restoredPlayerPosition != nullptr);
    const float distanceFromVehicle =
        std::hypot(restoredPlayerPosition->x - restoredPosition->x,
                   restoredPlayerPosition->y - restoredPosition->y);
    CHECK(distanceFromVehicle > 0.65f);

    restartedClient.Close();
    restarted.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}
