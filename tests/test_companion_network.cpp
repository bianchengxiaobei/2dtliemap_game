#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "core/Config.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/Companion.h"
#include "game/CompanionSystem.h"
#include "game/MissionComponents.h"
#include "game/NetworkClient.h"
#include "game/SettlementNpc.h"
#include "game/SettlementReputation.h"
#include "game/WorldSimulation.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "net/Command.h"
#include "physics/PhysicsWorld.h"
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

void CreateEmptySave(const std::filesystem::path& saveRoot,
                     std::string* saveId) {
    save::EntitySnapshot empty;
    save::SaveManager::Options options;
    options.root = saveRoot;
    options.gameVersion = save::kDefaultGameVersion;
    save::SaveManager manager(options);
    save::SaveManager::CreateOptions create;
    create.name = "M33-F3-T2";
    create.mapId = "village_demo";
    create.gameVersion = save::kDefaultGameVersion;
    create.entities = &empty;
    std::string error;
    REQUIRE_MESSAGE(manager.Create(create, saveId, &error), error);
}

std::filesystem::path TempDir(const char* name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zomboidlike_f3t2" / name;
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir, error);
    return dir;
}

void SetReputation(server::ServerHost& host, ecs::Entity player,
                   std::string_view settlementId, int score) {
    entt::registry& registry = host.Simulation().Ecs().Registry();
    auto* reputation =
        registry.try_get<game::SettlementReputation>(player);
    if (reputation == nullptr) {
        reputation = &registry.emplace<game::SettlementReputation>(player);
    }
    reputation->settlements.clear();
    reputation->settlements.push_back(
        game::SettlementReputationEntry{std::string(settlementId), score});
}

void CompleteVillageMission(server::ServerHost& host, ecs::Entity player) {
    entt::registry& registry = host.Simulation().Ecs().Registry();
    auto* log = registry.try_get<ecs::MissionLog>(player);
    if (log == nullptr) {
        log = &registry.emplace<ecs::MissionLog>(player);
    }
    ecs::MissionRunState mission;
    mission.id = "settlement_help";
    mission.status = ecs::MissionRunStatus::Completed;
    mission.day = 1;
    mission.rewardClaimed = true;
    mission.settlementId = "village_haven";
    log->missions.push_back(mission);
}

void Teleport(server::ServerHost& host, ecs::Entity entity, float x, float y) {
    auto* position =
        host.Simulation().Ecs().Registry().try_get<ecs::Position>(entity);
    REQUIRE(position != nullptr);
    position->x = x;
    position->y = y;
    position->z = 0;
}

int CompanionCount(server::ServerHost& host) {
    int count = 0;
    host.Simulation().Ecs().Registry().view<game::Companion>().each(
        [&](const ecs::Entity, const game::Companion&) { ++count; });
    return count;
}

}  // namespace

TEST_CASE("M33-F3-T2 服务器同伴招募、唯一性、解散与双玩家") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("recruit");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, &saveId);
    REQUIRE_FALSE(saveId.empty());
    const std::vector<std::string> args{
        "ZomboidLikeTests", "--save.root=" + saveRoot.string()};
    std::vector<const char*> argv;
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }

    server::ServerHost::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.spawnDemoPopulation = false;
    options.spawnLocalPlayer = false;
    options.network = true;
    options.port = 0;
    options.contentHash = 0x4633543252454352ull;
    options.enablePlayerProfiles = true;
    options.playerProfilesFile = profilePath;
    options.loadSaveId = saveId;
    options.argc = static_cast<int>(argv.size());
    options.argv = argv.data();

    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkEnabled());

    game::NetworkClient::Options firstOptions;
    firstOptions.host = "127.0.0.1";
    firstOptions.port = host.NetworkPort();
    firstOptions.contentHash = options.contentHash;
    firstOptions.autoReconnect = false;
    firstOptions.playerIdentity = "f3t2-first";
    firstOptions.playerName = "First";

    game::NetworkClient::Options secondOptions = firstOptions;
    secondOptions.playerIdentity = "f3t2-second";
    secondOptions.playerName = "Second";

    game::NetworkClient first(firstOptions);
    game::NetworkClient second(secondOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayer() && second.HasPlayer() &&
               first.HasPlayerState() && second.HasPlayerState();
    }));

    ecs::EcsWorld& world = host.Simulation().Ecs();
    entt::registry& registry = world.Registry();
    ecs::Entity requester = entt::null;
    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        registry.view<game::SettlementNpc>().each(
            [&](const ecs::Entity entity, const game::SettlementNpc& npc) {
                if (npc.kind == game::SettlementNpcKind::Requester &&
                    npc.settlementId == "village_haven") {
                    requester = entity;
                }
            });
        return requester != entt::null;
    }));
    const auto* requesterPosition =
        registry.try_get<ecs::Position>(requester);
    REQUIRE(requesterPosition != nullptr);
    const std::uint64_t requesterId = world.IdOf(requester).id;

    const ecs::Entity firstPlayer =
        world.FindById(first.PlayerStableId());
    const ecs::Entity secondPlayer =
        world.FindById(second.PlayerStableId());
    REQUIRE(static_cast<bool>(firstPlayer != entt::null));
    REQUIRE(static_cast<bool>(secondPlayer != entt::null));
    Teleport(host, firstPlayer, requesterPosition->x + 0.8f,
             requesterPosition->y);
    Teleport(host, secondPlayer, requesterPosition->x - 0.8f,
             requesterPosition->y);
    registry.get<ecs::Container>(firstPlayer).items.push_back(
        ecs::ItemInstance{"bandage", 4, 1.0f});
    registry.get<ecs::Container>(secondPlayer).items.push_back(
        ecs::ItemInstance{"bandage", 4, 1.0f});

    net::Command command;
    command.type = net::CommandType::RecruitCompanion;
    command.payload.entityId = requesterId;
    command.playerStableId = first.PlayerStableId();
    const std::uint64_t rejectedBefore =
        host.GetNetworkStats().commandsRejected;
    REQUIRE(first.SendCommand(command, &error));
    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        return host.GetNetworkStats().commandsRejected > rejectedBefore;
    }));
    CHECK(CompanionCount(host) == 0);
    CHECK(items::CountOf(registry.get<ecs::Container>(firstPlayer),
                         "bandage") == 4);

    SetReputation(host, firstPlayer, "village_haven", 30);
    CompleteVillageMission(host, firstPlayer);
    Teleport(host, firstPlayer, requesterPosition->x + 0.8f,
             requesterPosition->y);
    REQUIRE(first.SendCommand(command, &error));
    std::uint64_t firstCompanionId = 0;
    const bool firstRecruited = PumpUntil(host, first, second, 7000, [&] {
        const ecs::Entity owned =
            game::FindOwnedCompanion(world, first.PlayerStableId());
        if (owned == entt::null) {
            return false;
        }
        firstCompanionId = world.IdOf(owned).id;
        return firstCompanionId != 0;
    });
    if (!firstRecruited) {
        MESSAGE("first recruit status: ", first.PlayerState().status,
                " rejected=", host.GetNetworkStats().commandsRejected);
    }
    REQUIRE(firstRecruited);
    CHECK(CompanionCount(host) == 1);
    const ecs::Entity firstCompanion =
        world.FindById(firstCompanionId);
    REQUIRE(static_cast<bool>(firstCompanion != entt::null));
    const auto* firstIdentity =
        registry.try_get<game::Companion>(firstCompanion);
    const auto* firstState =
        registry.try_get<game::CompanionState>(firstCompanion);
    const auto* firstHealth =
        registry.try_get<ecs::Health>(firstCompanion);
    REQUIRE(firstIdentity != nullptr);
    REQUIRE(firstState != nullptr);
    REQUIRE(firstHealth != nullptr);
    CHECK(firstIdentity->defId == "companion_medic_lin");
    CHECK(firstIdentity->ownerStableId == first.PlayerStableId());
    CHECK(firstIdentity->homeSettlementId == "village_haven");
    CHECK_FALSE(firstState->standby);
    CHECK(firstHealth->max == doctest::Approx(95.0f));
    REQUIRE(firstState->equipment.slots[0].has_value());
    REQUIRE(firstState->equipment.slots[1].has_value());
    CHECK(firstState->equipment.slots[0]->defId == "kitchen_knife");
    CHECK(firstState->equipment.slots[1]->defId == "jacket");
    CHECK(items::CountOf(firstState->inventory, "bandage") == 2);
    CHECK(items::CountOf(firstState->inventory, "canned_soup") == 1);
    const auto* firstResist =
        registry.try_get<ecs::DamageResist>(firstCompanion);
    REQUIRE(firstResist != nullptr);
    CHECK(firstResist->bluntReduction == doctest::Approx(0.04f));
    CHECK(items::CountOf(registry.get<ecs::Container>(firstPlayer),
                         "bandage") == 2);

    const auto* firstPosition =
        registry.try_get<ecs::Position>(firstPlayer);
    REQUIRE(firstPosition != nullptr);
    REQUIRE(host.Simulation().TeleportCompanion(
        firstCompanion, firstPosition->x + 0.6f, firstPosition->y));
    const ecs::Entity zombie = host.Simulation().SpawnZombieAt(
        firstPosition->x + 3.0f, firstPosition->y);
    REQUIRE(static_cast<bool>(zombie != entt::null));
    const ecs::Entity placedZombie = world.FindById(world.IdOf(zombie).id);
    auto* zombiePosition =
        registry.try_get<ecs::Position>(placedZombie);
    const auto* companionPosition =
        registry.try_get<ecs::Position>(firstCompanion);
    REQUIRE(zombiePosition != nullptr);
    REQUIRE(companionPosition != nullptr);
    zombiePosition->x = companionPosition->x + 0.6f;
    zombiePosition->y = companionPosition->y;
    if (auto* rigid = registry.try_get<ecs::RigidBody>(placedZombie);
        rigid != nullptr && rigid->body != nullptr) {
        host.Simulation().Physics().TeleportBody(
            rigid->body, zombiePosition->x, zombiePosition->y, 0);
    }
    const std::uint64_t zombieId = world.IdOf(zombie).id;
    auto zombieHealthNow = [&]() {
        const ecs::Entity current = world.FindById(zombieId);
        if (current == entt::null) {
            return 0.0f;
        }
        const auto* health = registry.try_get<ecs::Health>(current);
        return health != nullptr ? health->current : 0.0f;
    };
    const float zombieHealthBefore = zombieHealthNow();
    REQUIRE(zombieHealthBefore > 0.0f);
    const bool companionDamagedZombie =
        PumpUntil(host, first, second, 15000, [&] {
            return zombieHealthNow() < zombieHealthBefore;
        });
    if (!companionDamagedZombie) {
        const ecs::Entity debugCompanion =
            world.FindById(firstCompanionId);
        const ecs::Entity debugZombie = world.FindById(zombieId);
        const auto* debugAi =
            debugCompanion != entt::null
                ? registry.try_get<game::CompanionAiState>(debugCompanion)
                : nullptr;
        const auto* debugCompanionPosition =
            debugCompanion != entt::null
                ? registry.try_get<ecs::Position>(debugCompanion)
                : nullptr;
        const auto* debugZombiePosition =
            debugZombie != entt::null
                ? registry.try_get<ecs::Position>(debugZombie)
                : nullptr;
        const auto* debugOwnerPosition =
            registry.try_get<ecs::Position>(firstPlayer);
        const auto* debugRequest =
            debugCompanion != entt::null
                ? registry.try_get<ecs::PathRequest>(debugCompanion)
                : nullptr;
        MESSAGE("zombie health ", zombieHealthNow(), " target ",
                debugAi != nullptr ? debugAi->targetId : 0ull, " companion (",
                debugCompanionPosition != nullptr
                    ? debugCompanionPosition->x
                    : -1.0f,
                ",",
                debugCompanionPosition != nullptr
                    ? debugCompanionPosition->y
                    : -1.0f,
                ") zombie (",
                debugZombiePosition != nullptr ? debugZombiePosition->x : -1.0f,
                ",",
                debugZombiePosition != nullptr ? debugZombiePosition->y : -1.0f,
                ") owner (",
                debugOwnerPosition != nullptr ? debugOwnerPosition->x : -1.0f,
                ",",
                debugOwnerPosition != nullptr ? debugOwnerPosition->y : -1.0f,
                ") path ",
                debugRequest != nullptr
                    ? static_cast<int>(debugRequest->state)
                    : -1,
                " goal ",
                debugRequest != nullptr ? debugRequest->goalX : -9999,
                ",",
                debugRequest != nullptr ? debugRequest->goalY : -9999);
    }
    REQUIRE(companionDamagedZombie);
    CHECK(zombieHealthNow() < zombieHealthBefore);
    if (const ecs::Entity current = world.FindById(zombieId);
        current != entt::null) {
        host.Simulation().DespawnEntity(current);
        host.RunForTicks(1);
    }

    const std::uint64_t duplicateRejectedBefore =
        host.GetNetworkStats().commandsRejected;
    REQUIRE(first.SendCommand(command, &error));
    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        return host.GetNetworkStats().commandsRejected >
               duplicateRejectedBefore;
    }));
    CHECK(CompanionCount(host) == 1);
    CHECK(world.IdOf(game::FindOwnedCompanion(
              world, first.PlayerStableId())).id == firstCompanionId);
    CHECK(items::CountOf(registry.get<ecs::Container>(firstPlayer),
                         "bandage") == 2);

    command.type = net::CommandType::DismissCompanion;
    Teleport(host, firstPlayer, requesterPosition->x + 0.8f,
             requesterPosition->y);
    REQUIRE(first.SendCommand(command, &error));
    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        const auto* state =
            registry.try_get<game::CompanionState>(firstCompanion);
        return state != nullptr && state->standby &&
               state->command == game::CompanionCommand::Stay;
    }));
    CHECK(CompanionCount(host) == 1);
    const auto* standbyPosition =
        registry.try_get<ecs::Position>(firstCompanion);
    REQUIRE(standbyPosition != nullptr);
    CHECK(std::abs(standbyPosition->x - requesterPosition->x) < 8.0f);
    CHECK(std::abs(standbyPosition->y - requesterPosition->y) < 8.0f);

    command.type = net::CommandType::RecruitCompanion;
    Teleport(host, firstPlayer, requesterPosition->x + 0.8f,
             requesterPosition->y);
    REQUIRE(first.SendCommand(command, &error));
    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        const auto* state =
            registry.try_get<game::CompanionState>(firstCompanion);
        return state != nullptr && !state->standby &&
               state->command == game::CompanionCommand::Follow;
    }));
    CHECK(world.IdOf(game::FindOwnedCompanion(
              world, first.PlayerStableId())).id == firstCompanionId);
    CHECK(CompanionCount(host) == 1);

    SetReputation(host, secondPlayer, "village_haven", 30);
    CompleteVillageMission(host, secondPlayer);
    command.playerStableId = second.PlayerStableId();
    Teleport(host, secondPlayer, requesterPosition->x - 0.8f,
             requesterPosition->y);
    REQUIRE(second.SendCommand(command, &error));
    std::uint64_t secondCompanionId = 0;
    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        const ecs::Entity owned =
            game::FindOwnedCompanion(world, second.PlayerStableId());
        if (owned == entt::null) {
            return false;
        }
        secondCompanionId = world.IdOf(owned).id;
        return secondCompanionId != 0;
    }));
    CHECK(CompanionCount(host) == 2);
    CHECK(secondCompanionId != firstCompanionId);
    const auto* secondIdentity =
        registry.try_get<game::Companion>(
            world.FindById(secondCompanionId));
    const auto* firstIdentityAgain =
        registry.try_get<game::Companion>(firstCompanion);
    REQUIRE(secondIdentity != nullptr);
    REQUIRE(firstIdentityAgain != nullptr);
    CHECK(secondIdentity->ownerStableId == second.PlayerStableId());
    CHECK(firstIdentityAgain->ownerStableId == first.PlayerStableId());
    CHECK(items::CountOf(registry.get<ecs::Container>(secondPlayer),
                         "bandage") == 2);

    second.Close();
    first.Close();
    host.Shutdown();
}

TEST_CASE("M33-F3-T7 服务器同伴指令、赠礼、委托与救援") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("ui_commands");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, &saveId);
    REQUIRE_FALSE(saveId.empty());
    const std::vector<std::string> args{
        "ZomboidLikeTests", "--save.root=" + saveRoot.string()};
    std::vector<const char*> argv;
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }

    server::ServerHost::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.spawnDemoPopulation = false;
    options.spawnLocalPlayer = false;
    options.network = true;
    options.port = 0;
    options.contentHash = 0x4633543755494354ull;
    options.enablePlayerProfiles = true;
    options.playerProfilesFile = profilePath;
    options.loadSaveId = saveId;
    options.argc = static_cast<int>(argv.size());
    options.argv = argv.data();

    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkEnabled());

    game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = options.contentHash;
    clientOptions.autoReconnect = false;
    clientOptions.playerIdentity = "f3t7-ui";
    clientOptions.playerName = "T7";
    game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpOne(host, client, 7000, [&] {
        return client.IsConnected() && client.HasPlayer() &&
               client.HasPlayerState();
    }));

    items::ItemRegistry items;
    core::Config config;
    REQUIRE(config.LoadFile(root / "config" / "game.json"));
    config.Set("companions.file",
               (root / "assets" / "companions.json").generic_string());
    REQUIRE(items.LoadFile(root / "assets" / "items.json"));
    game::CompanionConfig companions;
    REQUIRE_MESSAGE(
        game::LoadCompanionConfig(config, &companions, &error, &items), error);
    const game::CompanionDef* lin =
        companions.Find("companion_medic_lin");
    REQUIRE(lin != nullptr);

    entt::registry& registry = host.Simulation().Ecs().Registry();
    const ecs::Entity player =
        host.Simulation().Ecs().FindById(client.PlayerStableId());
    REQUIRE(static_cast<bool>(player != entt::null));
    const auto* playerPosition = registry.try_get<ecs::Position>(player);
    REQUIRE(playerPosition != nullptr);
    const ecs::Entity spawned = host.Simulation().SpawnCompanionAt(
        *lin, client.PlayerStableId(), "village_haven",
        playerPosition->x + 0.8f, playerPosition->y);
    REQUIRE(static_cast<bool>(spawned != entt::null));
    const std::uint64_t companionId =
        host.Simulation().Ecs().IdOf(spawned).id;
    auto* state = registry.try_get<game::CompanionState>(spawned);
    auto* identity = registry.try_get<game::Companion>(spawned);
    auto* health = registry.try_get<ecs::Health>(spawned);
    REQUIRE(state != nullptr);
    REQUIRE(identity != nullptr);
    REQUIRE(health != nullptr);
    state->loyalty = 80;
    game::RefreshCompanionQuestState(*lin, state);
    REQUIRE(state->personalStatus == game::CompanionQuestStatus::Available);
    registry.get<ecs::Container>(player).items.push_back(
        ecs::ItemInstance{"bandage", 2, 1.0f});

    auto command = [&](net::CommandType type, std::uint16_t flags,
                       std::int32_t index) {
        net::Command value;
        value.type = type;
        value.playerStableId = client.PlayerStableId();
        value.payload.flags = flags;
        value.payload.index = index;
        REQUIRE(client.SendCommand(value, &error));
    };
    auto companionState = [&]() {
        const ecs::Entity current =
            host.Simulation().Ecs().FindById(companionId);
        return current != entt::null
                   ? registry.try_get<game::CompanionState>(current)
                   : nullptr;
    };

    command(net::CommandType::SetCompanionCommand, 1, -1);
    REQUIRE(PumpOne(host, client, 7000, [&] {
        const auto* current = companionState();
        return current != nullptr &&
               current->command == game::CompanionCommand::Stay;
    }));
    command(net::CommandType::SetCompanionCommand, 0, -1);
    REQUIRE(PumpOne(host, client, 7000, [&] {
        const auto* current = companionState();
        return current != nullptr &&
               current->command == game::CompanionCommand::Follow;
    }));

    REQUIRE(host.Simulation().TeleportCompanion(
        spawned, playerPosition->x + 0.6f, playerPosition->y));
    command(net::CommandType::GiftToCompanion, 0, 0);
    REQUIRE(PumpOne(host, client, 7000, [&] {
        const auto* current = companionState();
        return current != nullptr &&
               items::CountOf(current->inventory, "bandage") == 3;
    }));
    CHECK(companionState()->loyalty == 82);

    REQUIRE(host.Simulation().TeleportCompanion(
        spawned, playerPosition->x + 0.6f, playerPosition->y));
    command(net::CommandType::AcceptCompanionQuest, 0, -1);
    REQUIRE(PumpOne(host, client, 7000, [&] {
        const auto* current = companionState();
        return current != nullptr &&
               current->personalStatus ==
                   game::CompanionQuestStatus::Active;
    }));
    command(net::CommandType::AbandonCompanionQuest, 0, -1);
    REQUIRE(PumpOne(host, client, 7000, [&] {
        const auto* current = companionState();
        return current != nullptr &&
               current->personalStatus ==
                   game::CompanionQuestStatus::Failed;
    }));

    REQUIRE(host.Simulation().TeleportCompanion(
        spawned, playerPosition->x + 0.6f, playerPosition->y));
    state->downed = true;
    state->downedRemaining = 60.0f;
    state->standby = false;
    health->current = 0.0f;
    command(net::CommandType::RescueCompanion, 0, -1);
    REQUIRE(PumpOne(host, client, 7000, [&] {
        const ecs::Entity current =
            host.Simulation().Ecs().FindById(companionId);
        const auto* currentState =
            current != entt::null
                ? registry.try_get<game::CompanionState>(current)
                : nullptr;
        const auto* currentHealth =
            current != entt::null
                ? registry.try_get<ecs::Health>(current)
                : nullptr;
        return currentState != nullptr && currentHealth != nullptr &&
               !currentState->downed && currentHealth->current > 0.0f;
    }));

    client.Close();
    host.Shutdown();
}
