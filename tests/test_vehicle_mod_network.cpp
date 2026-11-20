#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/EntityReplication.h"
#include "game/NetworkClient.h"
#include "game/VehicleComponents.h"
#include "game/VehicleMods.h"
#include "game/WorldSimulation.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "net/Replication.h"
#include "physics/PhysicsWorld.h"
#include "save/EntityStore.h"
#include "save/SaveManager.h"
#include "save/SaveTypes.h"
#include "server/ServerHost.h"
#include "world/ChunkManager.h"

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

template <typename Predicate>
bool PumpOne(server::ServerHost& host, game::NetworkClient& client,
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

std::filesystem::path TempDir(const char* name) {
    static std::atomic<std::uint64_t> counter{0};
    const std::uint64_t suffix =
        static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()) ^
        (counter.fetch_add(1) * 0x9E3779B97F4A7C15ull);
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zomboidlike_f4t5" /
        (std::string(name) + "-" + std::to_string(suffix));
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
    create.name = "M33-F4-T5";
    create.mapId = "village_demo";
    create.gameVersion = save::kDefaultGameVersion;
    create.entities = &empty;
    std::string error;
    REQUIRE_MESSAGE(manager.Create(create, saveId, &error), error);
}

server::ServerHost::Options MakeHostOptions(
    const std::filesystem::path& root, const std::filesystem::path& saveRoot,
    const std::string& profilePath, std::uint64_t contentHash,
    const std::string& saveId) {
    server::ServerHost::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.saveRoot = saveRoot.string();
    options.loadSaveId = saveId;
    options.spawnDemoPopulation = false;
    options.spawnLocalPlayer = false;
    options.network = true;
    options.port = 0;
    options.contentHash = contentHash;
    options.enablePlayerProfiles = true;
    options.playerProfilesFile = profilePath;
    return options;
}

game::NetworkClient::Options MakeClientOptions(
    server::ServerHost& host, std::uint64_t contentHash,
    const std::string& identity, const std::string& name) {
    game::NetworkClient::Options options;
    options.host = "127.0.0.1";
    options.port = host.NetworkPort();
    options.contentHash = contentHash;
    options.autoReconnect = false;
    options.playerIdentity = identity;
    options.playerName = name;
    return options;
}

void Teleport(server::ServerHost& host, ecs::Entity entity, float x,
              float y) {
    entt::registry& registry = host.Simulation().Ecs().Registry();
    auto* position = registry.try_get<ecs::Position>(entity);
    REQUIRE(position != nullptr);
    position->x = x;
    position->y = y;
    position->z = 0;
    if (auto* rigid = registry.try_get<ecs::RigidBody>(entity);
        rigid != nullptr && rigid->body != nullptr) {
        host.Simulation().Physics().TeleportBody(rigid->body, x, y, 0);
    }
}

void GiveItem(server::ServerHost& host, ecs::Entity entity,
              const std::string& itemId, int count) {
    auto* container =
        host.Simulation().Ecs().Registry().try_get<ecs::Container>(entity);
    REQUIRE(container != nullptr);
    container->items.push_back(ecs::ItemInstance{itemId, count, 1.0f});
}

int CountItem(server::ServerHost& host, ecs::Entity entity,
              const std::string& itemId) {
    const auto* container =
        host.Simulation().Ecs().Registry().try_get<ecs::Container>(entity);
    return container != nullptr ? items::CountOf(*container, itemId) : 0;
}

const ecs::VehicleMods* ModsAt(server::ServerHost& host,
                               std::uint64_t stableId) {
    const ecs::Entity entity =
        host.Simulation().Ecs().FindById(stableId);
    return entity != entt::null
               ? host.Simulation().Ecs().Registry().try_get<ecs::VehicleMods>(
                     entity)
               : nullptr;
}

void SendInstall(game::NetworkClient& client, std::uint64_t vehicleId,
                 int slot, int candidate, std::string* error) {
    net::Command command;
    command.type = net::CommandType::InstallVehicleMod;
    command.playerStableId = client.PlayerStableId();
    command.payload.entityId = vehicleId;
    command.payload.index = slot;
    command.payload.targetIndex = candidate;
    REQUIRE(client.SendCommand(command, error));
}

void SendRemove(game::NetworkClient& client, std::uint64_t vehicleId,
                int slot, std::string* error) {
    net::Command command;
    command.type = net::CommandType::RemoveVehicleMod;
    command.playerStableId = client.PlayerStableId();
    command.payload.entityId = vehicleId;
    command.payload.index = slot;
    command.payload.targetIndex = -1;
    REQUIRE(client.SendCommand(command, error));
}

}  // namespace

TEST_CASE("M33-F4-T5 ZLR1 旧车辆负载无 VMOD 按无改装回退") {
    net::ReplicationRegistry registry;
    REQUIRE(game::RegisterEntityReplicationComponents(registry));
    CHECK(registry.Size() == 33);
    REQUIRE(registry.Find(game::kReplicationVehicleMods) != nullptr);

    ecs::EcsWorld world;
    entt::registry& ecsRegistry = world.Registry();
    const ecs::Entity vehicle = world.CreateWithStableId(700);
    ecsRegistry.emplace<ecs::Position>(
        vehicle, ecs::Position{10.0f, 10.0f, 0});
    ecsRegistry.emplace<ecs::Vehicle>(vehicle, ecs::Vehicle{});

    game::EntityReplicationProducer producer(
        world, registry,
        game::EntityReplicationProducer::Options{16.0f, 16});
    game::EntityReplicationClient client;
    std::string error;

    const net::ReplicationSnapshot full =
        producer.BuildFull(1, 10.0f, 10.0f, &error);
    REQUIRE_MESSAGE(error.empty(), error);
    const std::vector<std::uint8_t> fullBytes =
        net::EncodeReplicationSnapshot(full, registry, &error);
    REQUIRE_MESSAGE(!fullBytes.empty(), error);
    net::ReplicationSnapshot decoded;
    REQUIRE(
        net::DecodeReplicationSnapshot(fullBytes, registry, &decoded, &error));
    REQUIRE_MESSAGE(client.Apply(decoded, &error), error);

    const game::ReplicatedActor* actor = client.Find(700);
    REQUIRE(actor != nullptr);
    CHECK(actor->hasVehicle);
    CHECK_FALSE(actor->hasVehicleMods);

    auto* mods = &ecsRegistry.emplace<ecs::VehicleMods>(vehicle);
    mods->defIds[0] = "vehicle_armor_plate";
    mods->defIds[1] = "vehicle_storage_rack";
    net::ReplicationSnapshot delta;
    REQUIRE(producer.BuildDelta(2, 10.0f, 10.0f, &delta, &error));
    REQUIRE_MESSAGE(error.empty(), error);
    actor = nullptr;
    const std::vector<std::uint8_t> deltaBytes =
        net::EncodeReplicationSnapshot(delta, registry, &error);
    REQUIRE_MESSAGE(!deltaBytes.empty(), error);
    REQUIRE(net::DecodeReplicationSnapshot(deltaBytes, registry, &decoded,
                                           &error));
    REQUIRE_MESSAGE(client.Apply(decoded, &error), error);
    actor = client.Find(700);
    REQUIRE(actor != nullptr);
    REQUIRE(actor->hasVehicleMods);
    CHECK(actor->vehicleMods.defIds[0] == "vehicle_armor_plate");
    CHECK(actor->vehicleMods.defIds[1] == "vehicle_storage_rack");
    CHECK(actor->vehicleMods.defIds[2].empty());
    CHECK(actor->vehicleMods.defIds[3].empty());
    CHECK(actor->vehicleMods.defIds[4].empty());

    mods->defIds = {};
    REQUIRE(producer.BuildDelta(3, 10.0f, 10.0f, &delta, &error));
    REQUIRE_MESSAGE(error.empty(), error);
    const std::vector<std::uint8_t> emptyBytes =
        net::EncodeReplicationSnapshot(delta, registry, &error);
    REQUIRE_MESSAGE(!emptyBytes.empty(), error);
    REQUIRE(net::DecodeReplicationSnapshot(emptyBytes, registry, &decoded,
                                           &error));
    REQUIRE_MESSAGE(client.Apply(decoded, &error), error);
    actor = client.Find(700);
    REQUIRE(actor != nullptr);
    REQUIRE(actor->hasVehicleMods);
    for (const std::string& defId : actor->vehicleMods.defIds) {
        CHECK(defId.empty());
    }
}

TEST_CASE("M33-F4-T5 双客户端改装复制、拒绝重复与不刷物品") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::uint64_t contentHash = 0x463454354e455431ull;
    server::ServerHost::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.spawnDemoPopulation = false;
    options.spawnLocalPlayer = false;
    options.network = true;
    options.port = 0;
    options.contentHash = contentHash;

    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkEnabled());

    game::NetworkClient first(
        MakeClientOptions(host, contentHash, "f4t5-first", "First"));
    game::NetworkClient second(
        MakeClientOptions(host, contentHash, "f4t5-second", "Second"));
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayerState() && second.HasPlayerState() &&
               host.VehicleStableIds().size() >= 2 &&
               host.Simulation().Chunks().GetStats().loadedChunks > 0;
    }));

    const std::vector<std::uint64_t> vehicleIds = host.VehicleStableIds();
    REQUIRE(vehicleIds.size() >= 2);
    const std::uint64_t vehicleId = vehicleIds[0];
    const std::uint64_t otherVehicleId = vehicleIds[1];
    const ecs::Entity vehicle =
        host.Simulation().Ecs().FindById(vehicleId);
    REQUIRE(static_cast<bool>(vehicle != entt::null));
    const auto* vehiclePosition =
        host.Simulation().Ecs().Registry().try_get<ecs::Position>(vehicle);
    REQUIRE(vehiclePosition != nullptr);
    const ecs::Entity firstPlayer =
        host.Simulation().Ecs().FindById(first.PlayerStableId());
    const ecs::Entity secondPlayer =
        host.Simulation().Ecs().FindById(second.PlayerStableId());
    REQUIRE(static_cast<bool>(firstPlayer != entt::null));
    REQUIRE(static_cast<bool>(secondPlayer != entt::null));
    Teleport(host, firstPlayer, vehiclePosition->x + 0.8f,
             vehiclePosition->y);
    Teleport(host, secondPlayer, vehiclePosition->x - 0.8f,
             vehiclePosition->y);
    GiveItem(host, firstPlayer, "repair_kit", 3);
    GiveItem(host, firstPlayer, "wood", 10);

    SendInstall(first, vehicleId, 0, 0, &error);
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        const ecs::VehicleMods* mods = ModsAt(host, vehicleId);
        return mods != nullptr &&
               mods->defIds[0] == "vehicle_armor_plate";
    }));
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        const game::ReplicatedActor* firstActor =
            first.Replication().Find(vehicleId);
        const game::ReplicatedActor* secondActor =
            second.Replication().Find(vehicleId);
        return firstActor != nullptr && firstActor->hasVehicleMods &&
               firstActor->vehicleMods.defIds[0] ==
                   "vehicle_armor_plate" &&
               secondActor != nullptr && secondActor->hasVehicleMods &&
               secondActor->vehicleMods.defIds[0] ==
                   "vehicle_armor_plate";
    }));
    const ecs::VehicleMods* otherMods = ModsAt(host, otherVehicleId);
    const bool otherHasMods =
        otherMods != nullptr && game::VehicleModsHasAny(*otherMods);
    CHECK_FALSE(otherHasMods);
    CHECK(CountItem(host, firstPlayer, "repair_kit") == 1);
    CHECK(CountItem(host, firstPlayer, "wood") == 6);

    error.clear();
    const std::uint64_t duplicateRejectedBefore =
        host.GetNetworkStats().commandsRejected;
    SendInstall(first, vehicleId, 0, 0, &error);
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return host.GetNetworkStats().commandsRejected >
               duplicateRejectedBefore;
    }));
    const ecs::VehicleMods* mods = ModsAt(host, vehicleId);
    REQUIRE(mods != nullptr);
    CHECK(mods->defIds[0] == "vehicle_armor_plate");
    CHECK(CountItem(host, firstPlayer, "repair_kit") == 1);
    CHECK(CountItem(host, firstPlayer, "wood") == 6);

    error.clear();
    const std::uint64_t invalidRejectedBefore =
        host.GetNetworkStats().commandsRejected;
    SendInstall(first, vehicleId, 1, 99, &error);
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return host.GetNetworkStats().commandsRejected >
               invalidRejectedBefore;
    }));
    CHECK(ModsAt(host, vehicleId)->defIds[1].empty());
    CHECK(CountItem(host, firstPlayer, "repair_kit") == 1);
    CHECK(CountItem(host, firstPlayer, "wood") == 6);

    SendRemove(first, vehicleId, 0, &error);
    const bool removed = PumpUntil(host, first, second, 8000, [&] {
        const ecs::VehicleMods* current = ModsAt(host, vehicleId);
        return current != nullptr && current->defIds[0].empty();
    });
    if (!removed) {
        MESSAGE("remove status=", first.PlayerState().status,
                " rejected=", host.GetNetworkStats().commandsRejected);
    }
    REQUIRE(removed);
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        const game::ReplicatedActor* firstActor =
            first.Replication().Find(vehicleId);
        const game::ReplicatedActor* secondActor =
            second.Replication().Find(vehicleId);
        return firstActor != nullptr && firstActor->hasVehicleMods &&
               firstActor->vehicleMods.defIds[0].empty() &&
               secondActor != nullptr && secondActor->hasVehicleMods &&
               secondActor->vehicleMods.defIds[0].empty();
    }));
    CHECK(CountItem(host, firstPlayer, "repair_kit") == 2);
    CHECK(CountItem(host, firstPlayer, "wood") == 8);

    second.Close();
    first.Close();
    host.Shutdown();
}

TEST_CASE("M33-F4-T5 服务器重启与客户端重连恢复改装") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("restart");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, &saveId);
    REQUIRE_FALSE(saveId.empty());
    const std::uint64_t contentHash = 0x4634543552455354ull;
    const server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId);

    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    game::NetworkClient client(
        MakeClientOptions(host, contentHash, "f4t5-restart", "Restart"));
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpOne(host, client, 8000, [&] {
        return client.IsConnected() && client.HasPlayerState() &&
               !host.VehicleStableIds().empty() &&
               host.Simulation().Chunks().GetStats().loadedChunks > 0;
    }));

    const std::uint64_t playerId = client.PlayerStableId();
    const std::uint64_t vehicleId = host.VehicleStableIds().front();
    const ecs::Entity player =
        host.Simulation().Ecs().FindById(playerId);
    const ecs::Entity vehicle =
        host.Simulation().Ecs().FindById(vehicleId);
    REQUIRE(static_cast<bool>(player != entt::null));
    REQUIRE(static_cast<bool>(vehicle != entt::null));
    const auto* vehiclePosition =
        host.Simulation().Ecs().Registry().try_get<ecs::Position>(vehicle);
    REQUIRE(vehiclePosition != nullptr);
    Teleport(host, player, vehiclePosition->x + 0.8f,
             vehiclePosition->y);
    GiveItem(host, player, "wood", 6);
    GiveItem(host, player, "repair_kit", 1);

    SendInstall(client, vehicleId, 1, 0, &error);
    REQUIRE(PumpOne(host, client, 8000, [&] {
        const ecs::VehicleMods* mods = ModsAt(host, vehicleId);
        return mods != nullptr &&
               mods->defIds[1] == "vehicle_storage_rack";
    }));
    REQUIRE(PumpOne(host, client, 8000, [&] {
        const game::ReplicatedActor* actor =
            client.Replication().Find(vehicleId);
        return actor != nullptr && actor->hasVehicleMods &&
               actor->vehicleMods.defIds[1] == "vehicle_storage_rack";
    }));

    client.Close();
    host.Shutdown();

    server::ServerHost restarted;
    error.clear();
    REQUIRE_MESSAGE(restarted.Init(options, &error), error);
    game::NetworkClient rejoined(
        MakeClientOptions(restarted, contentHash, "f4t5-restart",
                          "Restart"));
    REQUIRE(rejoined.Connect(&error));
    REQUIRE(PumpOne(restarted, rejoined, 8000, [&] {
        return rejoined.IsConnected() && rejoined.HasPlayerState() &&
               rejoined.PlayerStableId() == playerId;
    }));
    CHECK(static_cast<bool>(
        restarted.Simulation().Ecs().FindById(vehicleId) != entt::null));
    const ecs::VehicleMods* restoredMods = ModsAt(restarted, vehicleId);
    REQUIRE(restoredMods != nullptr);
    CHECK(restoredMods->defIds[1] == "vehicle_storage_rack");
    REQUIRE(PumpOne(restarted, rejoined, 8000, [&] {
        const game::ReplicatedActor* actor =
            rejoined.Replication().Find(vehicleId);
        return actor != nullptr && actor->hasVehicleMods &&
               actor->vehicleMods.defIds[1] == "vehicle_storage_rack";
    }));
    const ecs::Entity restoredVehicle =
        restarted.Simulation().Ecs().FindById(vehicleId);
    const auto* restoredContainer =
        restarted.Simulation().Ecs().Registry().try_get<ecs::Container>(
            restoredVehicle);
    REQUIRE(restoredContainer != nullptr);
    CHECK(restoredContainer->capacityKg == doctest::Approx(64.0f));

    SendRemove(rejoined, vehicleId, 1, &error);
    REQUIRE(PumpOne(restarted, rejoined, 8000, [&] {
        const ecs::VehicleMods* mods = ModsAt(restarted, vehicleId);
        return mods != nullptr && mods->defIds[1].empty();
    }));
    REQUIRE(PumpOne(restarted, rejoined, 8000, [&] {
        const game::ReplicatedActor* actor =
            rejoined.Replication().Find(vehicleId);
        return actor != nullptr && actor->hasVehicleMods &&
               actor->vehicleMods.defIds[1].empty();
    }));
    restoredContainer =
        restarted.Simulation().Ecs().Registry().try_get<ecs::Container>(
            restoredVehicle);
    REQUIRE(restoredContainer != nullptr);
    CHECK(restoredContainer->capacityKg == doctest::Approx(40.0f));

    rejoined.Close();
    restarted.Shutdown();
}
