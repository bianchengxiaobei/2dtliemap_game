#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "characters/components/CharacterComponents.h"
#include "characters/components/ProgressionComponents.h"
#include "core/Config.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/NetworkClient.h"
#include "game/SettlementReputation.h"
#include "game/StrategicState.h"
#include "game/WorldEventDirector.h"
#include "game/WorldEventRuntime.h"
#include "game/WorldSimulation.h"
#include "net/Command.h"
#include "net/WorldEventProtocol.h"
#include "net/WorldSound.h"
#include "physics/PhysicsWorld.h"
#include "save/EntityStore.h"
#include "save/SaveManager.h"
#include "save/WorldManifest.h"
#include "server/ServerHost.h"

using namespace zl;

namespace {

net::WorldEventSnapshotEntry MakeSnapshotEntry() {
    net::WorldEventSnapshotEntry event;
    event.id = "world_event_1_test_abc";
    event.defId = "test_event";
    event.name = "Test Event";
    event.kind = "custom";
    event.regionId = "core";
    event.x = 12.5f;
    event.y = 8.5f;
    event.status = 2;
    event.stageKind = 5;
    event.day = 1;
    event.stageIndex = 0;
    event.stageProgress = 0;
    event.stageTarget = 0;
    event.stageRadius = 4.0f;
    event.activeMinute = 480;
    event.deadlineMinute = 510;
    event.warningMinute = 480;
    event.scheduledMinute = 0;
    event.resolvedMinute = 0;
    return event;
}

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

std::filesystem::path TempDir(const char* name) {
    const auto stamp =
        Clock::now().time_since_epoch().count();
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zomboidlike_f2t5" /
        (std::string(name) + "-" + std::to_string(stamp));
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir, error);
    return dir;
}

void WriteText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    REQUIRE(file.is_open());
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    REQUIRE(static_cast<bool>(file));
}

void CreateEmptySave(const std::filesystem::path& saveRoot,
                     const std::string& mapId, std::string* saveId) {
    save::EntitySnapshot empty;
    save::SaveManager::Options options;
    options.root = saveRoot;
    options.gameVersion = save::kDefaultGameVersion;
    save::SaveManager manager(options);
    save::SaveManager::CreateOptions create;
    create.name = "M33-F2-T5";
    create.mapId = mapId;
    create.gameVersion = save::kDefaultGameVersion;
    create.entities = &empty;
    std::string error;
    REQUIRE_MESSAGE(manager.Create(create, saveId, &error), error);
}

std::filesystem::path MakeEventConfig(
    const std::filesystem::path& root, const std::filesystem::path& dir) {
    const std::filesystem::path eventFile = dir / "world_events.json";
    WriteText(eventFile, R"({
      "format": "world-events-v1",
      "defs": [{
        "id": "t5_choice",
        "name": "T5抉择",
        "kind": "custom",
        "minDay": 1,
        "maxDay": 100000,
        "warningMinutes": 0,
        "durationMinutes": 60,
        "region": "village_core",
        "stages": [{ "kind": "choice", "radius": 6 }],
        "choices": [
          { "id": "help", "label": "帮助", "reputation": 7, "rewardXp": 21, "pressure": 5 },
          { "id": "leave", "label": "离开", "reputation": -3, "rewardXp": 0, "pressure": 0 }
        ],
        "failureReputation": -5,
        "failurePressure": 5
      }]
    })");

    std::ifstream input(root / "config" / "game.json");
    REQUIRE(input.is_open());
    nlohmann::json config;
    input >> config;
    config["game"]["startHour"] = 0;
    config["worldEvents"] = {
        {"enabled", true},
        {"file", eventFile.generic_string()},
        {"randomSeed", 12345},
        {"detailRadius", 32.0},
    };
    const std::filesystem::path configFile = dir / "game.json";
    WriteText(configFile, config.dump(2));
    return configFile;
}

std::filesystem::path MakeActualEventConfig(
    const std::filesystem::path& root, const std::filesystem::path& dir,
    std::string_view definitionId, int durationMinutes,
    std::string_view region) {
    std::ifstream eventsInput(root / "assets" / "world_events.json");
    REQUIRE(eventsInput.is_open());
    nlohmann::json events;
    eventsInput >> events;
    nlohmann::json definition;
    for (const nlohmann::json& def : events["defs"]) {
        if (def.value("id", "") == definitionId) {
            definition = def;
            break;
        }
    }
    REQUIRE_FALSE(definition.is_null());
    definition["minDay"] = 1;
    definition["maxDay"] = 1;
    definition["warningMinutes"] = 0;
    definition["durationMinutes"] = durationMinutes;
    definition["region"] = region;
    nlohmann::json filtered;
    filtered["format"] = "world-events-v1";
    filtered["defs"] = nlohmann::json::array({definition});
    const std::filesystem::path eventFile = dir / "world_events.json";
    WriteText(eventFile, filtered.dump(2));

    std::ifstream configInput(root / "config" / "game.json");
    REQUIRE(configInput.is_open());
    nlohmann::json config;
    configInput >> config;
    config["game"]["startHour"] = 0;
    config["worldEvents"] = {
        {"enabled", true},
        {"file", eventFile.generic_string()},
        {"randomSeed", 20261113},
        {"detailRadius", 32.0},
    };
    const std::filesystem::path configFile = dir / "game.json";
    WriteText(configFile, config.dump(2));
    return configFile;
}

server::ServerHost::Options MakeHostOptions(
    const std::filesystem::path& root, const std::filesystem::path& saveRoot,
    const std::filesystem::path& configPath, const std::string& profilePath,
    std::uint64_t contentHash, const std::string& saveId,
    std::vector<std::string>* args, std::vector<const char*>* argv) {
    args->clear();
    args->push_back("ZomboidLikeTests");
    args->push_back("--world.mapDir=assets/maps/village_demo_m31");
    args->push_back("--world.loadRadius=6");
    args->push_back("--save.root=" + saveRoot.string());
    args->push_back("--server.saveId=" + saveId);
    argv->clear();
    for (const std::string& arg : *args) {
        argv->push_back(arg.c_str());
    }
    server::ServerHost::Options options;
    options.configPath = configPath.string();
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
    (void)root;
    return options;
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
    return true;
}

const net::WorldEventSnapshotEntry* FindClientEvent(
    const game::NetworkClient& client, std::string_view id) {
    if (!client.HasWorldEventState()) {
        return nullptr;
    }
    for (const net::WorldEventSnapshotEntry& event :
         client.WorldEventState().events) {
        if (event.id == id) {
            return &event;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("M33-F2-T5 world event snapshot codec") {
    CHECK(net::kWorldEventSnapshotMagic != net::kWorldSoundMagic);
    CHECK(net::kWorldEventSnapshotMagic != net::kPlayerStateMagic);
    net::WorldEventSnapshot snapshot;
    snapshot.sequence = 7;
    snapshot.serverTick = 42;
    snapshot.enabled = true;
    snapshot.gameMinute = 485;
    snapshot.events.push_back(MakeSnapshotEntry());

    std::string error;
    const std::vector<std::uint8_t> bytes =
        net::EncodeWorldEventSnapshot(snapshot, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    CHECK(error.empty());

    net::WorldEventSnapshot decoded;
    REQUIRE(net::DecodeWorldEventSnapshot(bytes, &decoded, &error));
    CHECK(error.empty());
    CHECK(decoded.sequence == 7u);
    CHECK(decoded.serverTick == 42u);
    CHECK(decoded.enabled);
    CHECK(decoded.gameMinute == 485);
    REQUIRE(decoded.events.size() == 1u);
    const net::WorldEventSnapshotEntry& event = decoded.events[0];
    CHECK(event.id == "world_event_1_test_abc");
    CHECK(event.defId == "test_event");
    CHECK(event.name == "Test Event");
    CHECK(event.regionId == "core");
    CHECK(event.x == doctest::Approx(12.5f));
    CHECK(event.y == doctest::Approx(8.5f));
    CHECK(event.status == 2u);
    CHECK(event.stageKind == 5u);
    CHECK(event.activeMinute == 480);
    CHECK(event.deadlineMinute == 510);

    net::WorldEventSnapshot invalid = snapshot;
    invalid.events[0].status = 9;
    CHECK(net::EncodeWorldEventSnapshot(invalid, &error).empty());
    CHECK_FALSE(error.empty());

    invalid = snapshot;
    invalid.events[0].deadlineMinute = invalid.events[0].activeMinute;
    CHECK(net::EncodeWorldEventSnapshot(invalid, &error).empty());

    net::WorldEventSnapshot old;
    CHECK_FALSE(net::DecodeWorldEventSnapshot(bytes, nullptr, &error));
    std::vector<std::uint8_t> malformed = bytes;
    malformed.pop_back();
    CHECK_FALSE(net::DecodeWorldEventSnapshot(malformed, &old, &error));
}

TEST_CASE("M33-F2-T5 world manifest persists events and outcome keys") {
    save::WorldManifest manifest;
    manifest.version = save::kWorldManifestVersion;
    manifest.identity.mapId = "test_world";
    manifest.identity.seed = 12;
    manifest.identity.generatorVersion = 1;
    manifest.worldSaveVersion = save::kWorldSaveVersion;
    save::ManifestWorldEvent event;
    event.id = "world_event_1_test_abc";
    event.defId = "test_event";
    event.name = "Test Event";
    event.kind = "custom";
    event.regionId = "core";
    event.status = 2;
    event.stageKind = 5;
    event.activeMinute = 480;
    event.deadlineMinute = 510;
    event.warningMinute = 480;
    event.contentSpawned = true;
    manifest.worldEvents.push_back(event);
    manifest.worldEventOutcomes.push_back("worldevent:world_event_1_test_abc:42");

    std::string error;
    const std::vector<std::uint8_t> bytes =
        save::EncodeWorldManifest(manifest, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    save::WorldManifest decoded;
    REQUIRE(save::DecodeWorldManifest(bytes, &decoded, &error));
    CHECK(decoded.version == save::kWorldManifestVersion);
    REQUIRE(decoded.worldEvents.size() == 1u);
    CHECK(decoded.worldEvents[0].id == event.id);
    CHECK(decoded.worldEvents[0].activeMinute == 480);
    CHECK(decoded.worldEvents[0].contentSpawned);
    REQUIRE(decoded.worldEventOutcomes.size() == 1u);
    CHECK(decoded.worldEventOutcomes[0] ==
          "worldevent:world_event_1_test_abc:42");

    save::WorldManifest old = manifest;
    old.version = 5;
    CHECK_FALSE(save::ValidateWorldManifest(old, &error));
    CHECK_FALSE(error.empty());

    save::WorldManifest legacy;
    legacy.version = 5;
    legacy.identity.mapId = "legacy_world";
    legacy.identity.seed = 3;
    legacy.identity.generatorVersion = 1;
    legacy.worldSaveVersion = save::kWorldSaveVersion;
    const std::vector<std::uint8_t> legacyBytes =
        save::EncodeWorldManifest(legacy, &error);
    REQUIRE_MESSAGE(!legacyBytes.empty(), error);
    save::WorldManifest legacyDecoded;
    REQUIRE(save::DecodeWorldManifest(legacyBytes, &legacyDecoded, &error));
    CHECK(legacyDecoded.version == 5u);
    CHECK(legacyDecoded.worldEvents.empty());
    CHECK(legacyDecoded.worldEventOutcomes.empty());
}

TEST_CASE("M33-F2-T5 双客户端共享事件、服务器结算与重复选择拒绝") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("choice");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::filesystem::path configPath = MakeEventConfig(root, dir);
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE_FALSE(saveId.empty());

    const std::uint64_t contentHash = 0x4D33325435544556ull;
    std::vector<std::string> args;
    std::vector<const char*> argv;
    server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, configPath, profilePath, contentHash,
                        saveId, &args, &argv);
    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkPort() != 0);
    REQUIRE(host.Simulation().WorldEvents() != nullptr);

    game::NetworkClient first(
        ClientOptions(host.NetworkPort(), contentHash, "f2t5-first"));
    game::NetworkClient second(
        ClientOptions(host.NetworkPort(), contentHash, "f2t5-second"));
    std::vector<game::NetworkClient*> clients{&first, &second};
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayerState() && second.HasPlayerState() &&
               first.HasWorldEventState() && second.HasWorldEventState() &&
               host.SessionCount() == 2;
    }));

    const game::WorldEventDirector* director =
        host.Simulation().WorldEvents();
    REQUIRE(PumpUntil(host, clients, 5000, [&] {
        return director != nullptr && !director->Events().empty();
    }));
    const game::WorldEventInstance event = director->Events().front();
    const bool initiallyPending =
        event.status == game::WorldEventStatus::Scheduled ||
        event.status == game::WorldEventStatus::Warning;
    REQUIRE(initiallyPending);
    REQUIRE(event.activeMinute > host.GameMinutes());
    host.AdvanceGameMinutes(
        static_cast<double>(event.activeMinute) - host.GameMinutes() + 1.0);
    host.RunForTicks(2);
    const game::WorldEventInstance* active = director->Find(event.id);
    REQUIRE(active != nullptr);
    REQUIRE(active->status == game::WorldEventStatus::Active);

    REQUIRE(TeleportServerPlayer(host, first.PlayerStableId(), active->x,
                                 active->y));
    REQUIRE(TeleportServerPlayer(host, second.PlayerStableId(), active->x,
                                 active->y));
    host.RunForTicks(3);
    REQUIRE(PumpUntil(host, clients, 5000, [&] {
        const net::WorldEventSnapshotEntry* a =
            FindClientEvent(first, event.id);
        const net::WorldEventSnapshotEntry* b =
            FindClientEvent(second, event.id);
        return a != nullptr && b != nullptr && a->status == 2u &&
               b->status == 2u;
    }));

    ecs::Entity firstEntity =
        host.Simulation().Ecs().FindById(first.PlayerStableId());
    ecs::Entity secondEntity =
        host.Simulation().Ecs().FindById(second.PlayerStableId());
    REQUIRE(static_cast<bool>(firstEntity != entt::null));
    REQUIRE(static_cast<bool>(secondEntity != entt::null));
    auto* firstProgression =
        host.Simulation().Ecs().Registry().try_get<ecs::Progression>(
            firstEntity);
    auto* secondProgression =
        host.Simulation().Ecs().Registry().try_get<ecs::Progression>(
            secondEntity);
    REQUIRE(firstProgression != nullptr);
    REQUIRE(secondProgression != nullptr);
    const float firstXp = firstProgression->xp;
    const float secondXp = secondProgression->xp;

    net::Command choose;
    choose.type = net::CommandType::ChooseWorldEvent;
    choose.playerStableId = first.PlayerStableId();
    choose.payload.index = 0;
    REQUIRE_MESSAGE(first.SendCommand(choose, &error), error);
    REQUIRE(PumpUntil(host, clients, 5000, [&] {
        const net::WorldEventSnapshotEntry* a =
            FindClientEvent(first, event.id);
        const net::WorldEventSnapshotEntry* b =
            FindClientEvent(second, event.id);
        return a != nullptr && b != nullptr && a->status == 3u &&
               b->status == 3u && a->choiceId == "help" &&
               b->choiceId == "help";
    }));
    CHECK(firstProgression->xp > firstXp);
    CHECK(secondProgression->xp > secondXp);
    const game::WorldEventInstance* resolved = director->Find(event.id);
    REQUIRE(resolved != nullptr);
    CHECK(resolved->status == game::WorldEventStatus::Resolved);
    CHECK(resolved->choiceId == "help");

    const float firstAfterChoice = firstProgression->xp;
    const std::uint64_t rejectedBefore =
        host.GetNetworkStats().worldEventChoicesRejected;
    choose.payload.index = 1;
    REQUIRE_MESSAGE(first.SendCommand(choose, &error), error);
    REQUIRE(PumpUntil(host, clients, 3000, [&] {
        return host.GetNetworkStats().worldEventChoicesRejected >
               rejectedBefore;
    }));
    CHECK(firstProgression->xp == doctest::Approx(firstAfterChoice));

    first.Close();
    second.Close();
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M33-F2-T5 超时由服务器自动结算一次") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("timeout");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::filesystem::path configPath = MakeEventConfig(root, dir);
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE_FALSE(saveId.empty());

    const std::uint64_t contentHash = 0x4D33325435544F55ull;
    std::vector<std::string> args;
    std::vector<const char*> argv;
    server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, configPath, profilePath, contentHash,
                        saveId, &args, &argv);
    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);

    game::NetworkClient client(
        ClientOptions(host.NetworkPort(), contentHash, "f2t5-timeout"));
    std::vector<game::NetworkClient*> clients{&client};
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return client.HasPlayerState() && client.HasWorldEventState();
    }));

    const game::WorldEventDirector* director =
        host.Simulation().WorldEvents();
    REQUIRE(director != nullptr);
    REQUIRE(PumpUntil(host, clients, 5000, [&] {
        return !director->Events().empty();
    }));
    const game::WorldEventInstance event = director->Events().front();
    const std::string eventId = event.id;
    host.AdvanceGameMinutes(
        static_cast<double>(event.activeMinute) - host.GameMinutes() + 1.0);
    host.RunForTicks(2);
    const game::WorldEventInstance* active = director->Find(eventId);
    REQUIRE(active != nullptr);
    REQUIRE(active->status == game::WorldEventStatus::Active);
    REQUIRE(TeleportServerPlayer(host, client.PlayerStableId(), active->x,
                                 active->y));
    host.RunForTicks(3);
    REQUIRE(PumpUntil(host, clients, 5000, [&] {
        const net::WorldEventSnapshotEntry* saved =
            FindClientEvent(client, eventId);
        return saved != nullptr && saved->status == 2u;
    }));

    float pressureBefore = 0.0f;
    if (const game::StrategicState* strategic = host.Simulation().Strategic();
        strategic != nullptr) {
        if (const game::StrategicRegionState* region =
                strategic->FindRegion("village_core");
            region != nullptr) {
            pressureBefore = region->pressure;
        }
    }
    host.AdvanceGameMinutes(
        static_cast<double>(active->deadlineMinute) - host.GameMinutes() +
        2.0);
    host.RunForTicks(2);
    REQUIRE(PumpUntil(host, clients, 5000, [&] {
        const net::WorldEventSnapshotEntry* saved =
            FindClientEvent(client, eventId);
        return saved != nullptr && saved->status == 5u;
    }));
    const game::WorldEventInstance* expired = director->Find(eventId);
    REQUIRE(expired != nullptr);
    CHECK(expired->status == game::WorldEventStatus::Expired);
    if (host.Simulation().Strategic() != nullptr) {
        const game::StrategicRegionState* region =
            host.Simulation().Strategic()->FindRegion("village_core");
        if (region != nullptr) {
            CHECK(region->pressure > pressureBefore);
        }
    }

    client.Close();
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M33-F2-T5 服务器重启与客户端重连恢复事件") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("restart");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::filesystem::path configPath = MakeEventConfig(root, dir);
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE_FALSE(saveId.empty());

    const std::uint64_t contentHash = 0x4D33325435524553ull;
    std::vector<std::string> args;
    std::vector<const char*> argv;
    server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, configPath, profilePath, contentHash,
                        saveId, &args, &argv);
    std::string eventId;
    {
        server::ServerHost host;
        std::string error;
        REQUIRE_MESSAGE(host.Init(options, &error), error);
        game::NetworkClient client(
            ClientOptions(host.NetworkPort(), contentHash, "f2t5-restart"));
        std::vector<game::NetworkClient*> clients{&client};
        REQUIRE(client.Connect(&error));
        REQUIRE(PumpUntil(host, clients, 10000, [&] {
            return client.HasPlayerState() && client.HasWorldEventState();
        }));
        const game::WorldEventDirector* director =
            host.Simulation().WorldEvents();
        REQUIRE(director != nullptr);
        REQUIRE(PumpUntil(host, clients, 5000, [&] {
            return !director->Events().empty();
        }));
        const game::WorldEventInstance event = director->Events().front();
        eventId = event.id;
        host.AdvanceGameMinutes(
            static_cast<double>(event.activeMinute) - host.GameMinutes() +
            1.0);
        host.RunForTicks(2);
        const game::WorldEventInstance* active = director->Find(eventId);
        REQUIRE(active != nullptr);
        REQUIRE(active->status == game::WorldEventStatus::Active);
        REQUIRE(TeleportServerPlayer(host, client.PlayerStableId(),
                                     active->x, active->y));
        host.RunForTicks(3);
        REQUIRE(PumpUntil(host, clients, 5000, [&] {
            const net::WorldEventSnapshotEntry* saved =
                FindClientEvent(client, eventId);
            return saved != nullptr && saved->status == 2u;
        }));
        client.Close();
        host.Shutdown();
    }

    {
        server::ServerHost restored;
        std::string error;
        REQUIRE_MESSAGE(restored.Init(options, &error), error);
        game::NetworkClient reconnected(
            ClientOptions(restored.NetworkPort(), contentHash,
                          "f2t5-reconnected"));
        std::vector<game::NetworkClient*> clients{&reconnected};
        REQUIRE(reconnected.Connect(&error));
        REQUIRE(PumpUntil(restored, clients, 10000, [&] {
            return reconnected.HasPlayerState() &&
                   reconnected.HasWorldEventState();
        }));
        REQUIRE(PumpUntil(restored, clients, 5000, [&] {
            const net::WorldEventSnapshotEntry* saved =
                FindClientEvent(reconnected, eventId);
            return saved != nullptr && saved->status == 2u;
        }));
        reconnected.Close();

        game::NetworkClient again(
            ClientOptions(restored.NetworkPort(), contentHash,
                          "f2t5-reconnected-again"));
        std::vector<game::NetworkClient*> againClients{&again};
        REQUIRE(again.Connect(&error));
        REQUIRE(PumpUntil(restored, againClients, 10000, [&] {
            return again.HasPlayerState() && again.HasWorldEventState();
        }));
        REQUIRE(PumpUntil(restored, againClients, 5000, [&] {
            const net::WorldEventSnapshotEntry* saved =
                FindClientEvent(again, eventId);
            return saved != nullptr && saved->status == 2u;
        }));
        again.Close();
        restored.Shutdown();
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M33-F2-T7 双客户端实际内容事件 15 分钟窗口、结算与重启恢复") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    core::Config contentConfig;
    REQUIRE(contentConfig.LoadFile((root / "config" / "game.json").string()));
    game::WorldEventConfig content;
    std::string error;
    REQUIRE(game::LoadWorldEventConfig(contentConfig, &content, &error));
    CHECK(content.enabled);
    CHECK(content.defs.size() == 6u);
    REQUIRE(content.Find("world_survivor_distress") != nullptr);
    REQUIRE(content.Find("world_caravan_aftermath") != nullptr);

    const std::filesystem::path dir = TempDir("delivery");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::filesystem::path configPath =
        MakeActualEventConfig(root, dir, "world_survivor_distress", 720,
                              "north_fields");
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE_FALSE(saveId.empty());

    const std::uint64_t contentHash = 0x4D3332543744454Cull;
    std::vector<std::string> args;
    std::vector<const char*> argv;
    server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, configPath, profilePath, contentHash,
                        saveId, &args, &argv);

    std::string eventId;
    {
        server::ServerHost host;
        REQUIRE_MESSAGE(host.Init(options, &error), error);
        REQUIRE(host.NetworkPort() != 0);
        game::WorldEventDirector* director =
            host.Simulation().WorldEvents();
        REQUIRE(director != nullptr);

        game::NetworkClient first(
            ClientOptions(host.NetworkPort(), contentHash, "f2t7-a"));
        game::NetworkClient second(
            ClientOptions(host.NetworkPort(), contentHash, "f2t7-b"));
        std::vector<game::NetworkClient*> clients{&first, &second};
        REQUIRE(first.Connect(&error));
        REQUIRE(second.Connect(&error));
        REQUIRE(PumpUntil(host, clients, 10000, [&] {
            return first.IsConnected() && second.IsConnected() &&
                   first.HasPlayerState() && second.HasPlayerState() &&
                   first.HasWorldEventState() &&
                   second.HasWorldEventState() && host.SessionCount() == 2;
        }));
        REQUIRE(PumpUntil(host, clients, 5000, [&] {
            return !director->Events().empty();
        }));
        const game::WorldEventInstance event = director->Events().front();
        eventId = event.id;
        CHECK(event.defId == "world_survivor_distress");
        CHECK(event.regionId == "north_fields");
        CHECK(event.stageKind == game::WorldEventStageKind::Reach);
        if (host.GameMinutes() < event.activeMinute) {
            host.AdvanceGameMinutes(
                static_cast<double>(event.activeMinute) -
                host.GameMinutes() + 1.0);
        }
        host.RunForTicks(2);
        const game::WorldEventInstance* active = director->Find(eventId);
        REQUIRE(active != nullptr);
        REQUIRE(active->status == game::WorldEventStatus::Active);
        REQUIRE(TeleportServerPlayer(host, first.PlayerStableId(), active->x,
                                     active->y));
        REQUIRE(TeleportServerPlayer(host, second.PlayerStableId(), active->x,
                                     active->y));
        host.RunForTicks(3);
        REQUIRE(PumpUntil(host, clients, 5000, [&] {
            const net::WorldEventSnapshotEntry* a =
                FindClientEvent(first, eventId);
            const net::WorldEventSnapshotEntry* b =
                FindClientEvent(second, eventId);
            return a != nullptr && b != nullptr && a->status == 2u &&
                   b->status == 2u;
        }));
        int actors = 0;
        REQUIRE(PumpUntil(host, clients, 8000, [&] {
            actors = 0;
            host.Simulation().Ecs().Registry()
                .view<ecs::WorldEventActor>()
                .each([&](const ecs::WorldEventActor&) { ++actors; });
            return actors >= 4;
        }));

        host.AdvanceGameMinutes(15.0);
        host.RunForTicks(2);
        const game::WorldEventInstance* midway = director->Find(eventId);
        REQUIRE(midway != nullptr);
        CHECK(midway->status == game::WorldEventStatus::Active);
        CHECK(midway->stageProgress >= 0);

        REQUIRE(director->ReportStageProgress(
            eventId, game::WorldEventStageKind::Defend,
            "event_distress_zombie", 4, &error));
        host.RunForTicks(2);
        const game::WorldEventInstance* ready = director->Find(eventId);
        REQUIRE(ready != nullptr);
        CHECK(ready->stagesComplete);

        ecs::Entity firstEntity =
            host.Simulation().Ecs().FindById(first.PlayerStableId());
        ecs::Entity secondEntity =
            host.Simulation().Ecs().FindById(second.PlayerStableId());
        REQUIRE(static_cast<bool>(firstEntity != entt::null));
        REQUIRE(static_cast<bool>(secondEntity != entt::null));
        auto* firstProgression =
            host.Simulation().Ecs().Registry().try_get<ecs::Progression>(
                firstEntity);
        auto* secondProgression =
            host.Simulation().Ecs().Registry().try_get<ecs::Progression>(
                secondEntity);
        REQUIRE(firstProgression != nullptr);
        REQUIRE(secondProgression != nullptr);
        const float firstXp = firstProgression->xp;
        const float secondXp = secondProgression->xp;
        const int firstLevel = firstProgression->level;
        const int secondLevel = secondProgression->level;
        const game::StrategicRegionState* regionBefore =
            host.Simulation().Strategic() != nullptr
                ? host.Simulation().Strategic()->FindRegion("north_fields")
                : nullptr;
        const float pressureBefore =
            regionBefore != nullptr ? regionBefore->pressure : 0.0f;

        net::Command choose;
        choose.type = net::CommandType::ChooseWorldEvent;
        choose.playerStableId = first.PlayerStableId();
        choose.payload.index = 0;
        REQUIRE_MESSAGE(first.SendCommand(choose, &error), error);
        REQUIRE(PumpUntil(host, clients, 5000, [&] {
            const net::WorldEventSnapshotEntry* a =
                FindClientEvent(first, eventId);
            const net::WorldEventSnapshotEntry* b =
                FindClientEvent(second, eventId);
            return a != nullptr && b != nullptr && a->status == 3u &&
                   b->status == 3u && a->choiceId == "escort" &&
                   b->choiceId == "escort";
        }));
        CHECK((firstProgression->level > firstLevel ||
               firstProgression->xp > firstXp));
        CHECK((secondProgression->level > secondLevel ||
               secondProgression->xp > secondXp));
        CHECK(game::SettlementReputationScoreFor(
                  host.Simulation().Ecs(), firstEntity, "north_fields") == 15);
        CHECK(game::SettlementReputationScoreFor(
                  host.Simulation().Ecs(), secondEntity, "north_fields") ==
              15);
        if (host.Simulation().Strategic() != nullptr) {
            const game::StrategicRegionState* regionAfter =
                host.Simulation().Strategic()->FindRegion("north_fields");
            REQUIRE(regionAfter != nullptr);
            CHECK(regionAfter->pressure < pressureBefore);
        }

        first.Close();
        second.Close();
        host.Shutdown();
    }

    {
        server::ServerHost restored;
        REQUIRE_MESSAGE(restored.Init(options, &error), error);
        game::NetworkClient first(
            ClientOptions(restored.NetworkPort(), contentHash, "f2t7-a"));
        game::NetworkClient second(
            ClientOptions(restored.NetworkPort(), contentHash, "f2t7-b"));
        std::vector<game::NetworkClient*> clients{&first, &second};
        REQUIRE(first.Connect(&error));
        REQUIRE(second.Connect(&error));
        REQUIRE(PumpUntil(restored, clients, 10000, [&] {
            return first.HasPlayerState() && second.HasPlayerState() &&
                   first.HasWorldEventState() &&
                   second.HasWorldEventState();
        }));
        REQUIRE(PumpUntil(restored, clients, 5000, [&] {
            const net::WorldEventSnapshotEntry* a =
                FindClientEvent(first, eventId);
            const net::WorldEventSnapshotEntry* b =
                FindClientEvent(second, eventId);
            return a != nullptr && b != nullptr && a->status == 3u &&
                   b->status == 3u && a->choiceId == "escort" &&
                   b->choiceId == "escort";
        }));

        ecs::Entity firstEntity =
            restored.Simulation().Ecs().FindById(first.PlayerStableId());
        REQUIRE(static_cast<bool>(firstEntity != entt::null));
        CHECK(game::SettlementReputationScoreFor(
                  restored.Simulation().Ecs(), firstEntity, "north_fields") ==
              15);
        game::WorldEventSettlement outcome;
        REQUIRE(restored.Simulation().SettleWorldEvent(
            eventId, firstEntity, &outcome, &error));
        CHECK(outcome.alreadyApplied);
        CHECK(game::SettlementReputationScoreFor(
                  restored.Simulation().Ecs(), firstEntity, "north_fields") ==
              15);

        first.Close();
        second.Close();
        restored.Shutdown();
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}

TEST_CASE("M33-F2-T7 链后续事件可单独排期并复制到客户端") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("chain_schedule");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::filesystem::path configPath = MakeActualEventConfig(
        root, dir, "world_caravan_aftermath", 1430, "village_core");
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, "village_demo_m31", &saveId);
    REQUIRE_FALSE(saveId.empty());

    const std::uint64_t contentHash = 0x4D33325437434841ull;
    std::vector<std::string> args;
    std::vector<const char*> argv;
    server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, configPath, profilePath, contentHash,
                        saveId, &args, &argv);
    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    game::NetworkClient client(
        ClientOptions(host.NetworkPort(), contentHash, "f2t7-chain"));
    std::vector<game::NetworkClient*> clients{&client};
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, clients, 10000, [&] {
        return client.HasPlayerState() && client.HasWorldEventState();
    }));
    game::WorldEventDirector* director = host.Simulation().WorldEvents();
    REQUIRE(director != nullptr);
    REQUIRE(PumpUntil(host, clients, 3000, [&] {
        return !director->Events().empty();
    }));
    const game::WorldEventInstance event = director->Events().front();
    CHECK(event.defId == "world_caravan_aftermath");
    host.AdvanceGameMinutes(10.0);
    host.RunForTicks(2);
    const game::WorldEventInstance* active = director->Find(event.id);
    REQUIRE(active != nullptr);
    CHECK(active->status == game::WorldEventStatus::Active);
    REQUIRE(PumpUntil(host, clients, 3000, [&] {
        const net::WorldEventSnapshotEntry* saved =
            FindClientEvent(client, event.id);
        return saved != nullptr && saved->status == 2u;
    }));
    client.Close();
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}
