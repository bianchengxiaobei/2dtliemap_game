#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "ecs/components/CommonComponents.h"
#include "game/NetworkClient.h"
#include "game/VehicleComponents.h"
#include "game/WorldSimulation.h"
#include "items/ItemComponents.h"
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

std::filesystem::path TempDir(const char* name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zomboidlike_m33_f4_t2" /
        name;
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
    create.name = "M33-F4-T2";
    create.mapId = "village_demo";
    create.gameVersion = save::kDefaultGameVersion;
    create.entities = &empty;
    std::string error;
    REQUIRE_MESSAGE(manager.Create(create, saveId, &error), error);
}

bool PumpUntil(server::ServerHost& host, game::NetworkClient& client,
               int timeoutMs, const std::function<bool()>& predicate) {
    using Clock = std::chrono::steady_clock;
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

bool ContainsId(const std::vector<std::uint64_t>& ids,
                std::uint64_t stableId) {
    return std::find(ids.begin(), ids.end(), stableId) != ids.end();
}

}  // namespace

TEST_CASE("M33-F4-T2 服务器重启恢复车辆改装") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("server_restart");
    const std::filesystem::path saveRoot = dir / "saves";
    std::string saveId;
    CreateEmptySave(saveRoot, &saveId);
    REQUIRE_FALSE(saveId.empty());

    const std::uint64_t contentHash = 0x4D333354324D4F44ull;
    server::ServerHost::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.saveRoot = saveRoot.string();
    options.loadSaveId = saveId;
    options.spawnDemoPopulation = false;
    options.spawnLocalPlayer = false;
    options.network = true;
    options.port = 0;
    options.contentHash = contentHash;

    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);

    game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = contentHash;
    clientOptions.autoReconnect = false;
    game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 8000, [&] {
        return client.IsConnected() && client.HasPlayer() &&
               !host.VehicleStableIds().empty();
    }));

    const std::vector<std::uint64_t> ids = host.VehicleStableIds();
    REQUIRE_FALSE(ids.empty());
    const std::uint64_t vehicleId = ids.front();
    const ecs::Entity vehicle =
        host.Simulation().Ecs().FindById(vehicleId);
    REQUIRE(static_cast<bool>(vehicle != entt::null));
    entt::registry& registry = host.Simulation().Ecs().Registry();
    auto* mods = registry.try_get<ecs::VehicleMods>(vehicle);
    if (mods == nullptr) {
        mods = &registry.emplace<ecs::VehicleMods>(vehicle);
    }
    mods->defIds[0] = "vehicle_armor_plate";
    mods->defIds[1] = "vehicle_storage_rack";
    mods->defIds[4] = "vehicle_engine_tune";

    client.Close();
    host.Shutdown();
    REQUIRE(std::filesystem::exists(saveRoot / saveId / "entities.bin"));

    server::ServerHost::Options restartedOptions = options;
    restartedOptions.network = false;
    restartedOptions.port = 0;
    server::ServerHost restarted;
    error.clear();
    REQUIRE_MESSAGE(restarted.Init(restartedOptions, &error), error);
    REQUIRE(ContainsId(restarted.VehicleStableIds(), vehicleId));

    const ecs::Entity restored =
        restarted.Simulation().Ecs().FindById(vehicleId);
    REQUIRE(static_cast<bool>(restored != entt::null));
    const auto* restoredMods =
        restarted.Simulation().Ecs().Registry().try_get<ecs::VehicleMods>(
            restored);
    REQUIRE(restoredMods != nullptr);
    CHECK(restoredMods->defIds[0] == "vehicle_armor_plate");
    CHECK(restoredMods->defIds[1] == "vehicle_storage_rack");
    CHECK(restoredMods->defIds[2].empty());
    CHECK(restoredMods->defIds[3].empty());
    CHECK(restoredMods->defIds[4] == "vehicle_engine_tune");
    const auto* restoredContainer =
        restarted.Simulation().Ecs().Registry().try_get<ecs::Container>(
            restored);
    REQUIRE(restoredContainer != nullptr);
    CHECK(restoredContainer->capacityKg == doctest::Approx(64.0f));
    restarted.Shutdown();
}
