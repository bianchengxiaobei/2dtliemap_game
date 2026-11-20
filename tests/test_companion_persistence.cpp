#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "core/Config.h"
#include "game/Companion.h"
#include "game/CompanionInventory.h"
#include "game/NetworkClient.h"
#include "game/WorldSimulation.h"
#include "items/InventoryOps.h"
#include "items/ItemRegistry.h"
#include "save/ComponentRegistry.h"
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
bool PumpUntil(server::ServerHost& host, game::NetworkClient& client,
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
bool PumpHostUntil(server::ServerHost& host, int timeoutMs,
                   Predicate predicate) {
    const Clock::time_point deadline =
        Clock::now() + std::chrono::milliseconds(timeoutMs);
    Clock::time_point nextTick = Clock::now();
    while (Clock::now() < deadline) {
        if (Clock::now() >= nextTick) {
            host.Tick(1.0f / 60.0f);
            nextTick += std::chrono::milliseconds(16);
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
        std::filesystem::temp_directory_path() / "zomboidlike_f3t6" / name;
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
    create.name = "M33-F3-T6";
    create.mapId = "village_demo";
    create.gameVersion = save::kDefaultGameVersion;
    create.entities = &empty;
    std::string error;
    REQUIRE_MESSAGE(manager.Create(create, saveId, &error), error);
}

server::ServerHost::Options MakeHostOptions(
    const std::filesystem::path& root, const std::filesystem::path& saveRoot,
    const std::string& profilePath, std::uint64_t contentHash,
    const std::string& saveId, float respawnSeconds = 60.0f) {
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
    options.respawnDelaySeconds = respawnSeconds;
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

bool LoadCompanionDefs(const std::filesystem::path& root,
                       game::CompanionConfig* out, std::string* error) {
    core::Config config;
    if (!config.LoadFile(root / "config" / "game.json")) {
        *error = "测试同伴配置加载失败";
        return false;
    }
    config.Set("companions.file",
               (root / "assets" / "companions.json").string());
    items::ItemRegistry items;
    if (!items.LoadFile(root / "assets" / "items.json")) {
        *error = "测试物品表加载失败";
        return false;
    }
    if (!game::LoadCompanionConfig(config, out, error, &items)) {
        return false;
    }
    if (out->Find("companion_medic_lin") == nullptr) {
        *error = "测试同伴定义不存在";
        return false;
    }
    return true;
}

int CompanionCount(server::ServerHost& host) {
    int count = 0;
    host.Simulation().Ecs().Registry().view<game::Companion>().each(
        [&](const ecs::Entity, const game::Companion&) { ++count; });
    return count;
}

ecs::Entity SpawnCompanion(server::ServerHost& host,
                           const game::CompanionDef& def,
                           std::uint64_t ownerStableId,
                           const ecs::Position& ownerPosition) {
    return host.Simulation().SpawnCompanionAt(
        def, ownerStableId, "village_haven", ownerPosition.x + 0.8f,
        ownerPosition.y);
}

game::CompanionState* StateAt(server::ServerHost& host,
                              std::uint64_t companionId) {
    const ecs::Entity entity =
        host.Simulation().Ecs().FindById(companionId);
    if (entity == entt::null) {
        return nullptr;
    }
    return host.Simulation().Ecs().Registry().try_get<game::CompanionState>(
        entity);
}

}  // namespace

TEST_CASE("M33-F3-T6 同伴断线待命、玩家死亡与重连不复制") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("disconnect");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, &saveId);
    REQUIRE_FALSE(saveId.empty());

    const std::uint64_t contentHash = 0x4D33335436504552ull;
    const server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId);
    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkEnabled());

    game::NetworkClient::Options firstOptions =
        MakeClientOptions(host, contentHash, "f3t6-first", "First");
    game::NetworkClient::Options secondOptions =
        MakeClientOptions(host, contentHash, "f3t6-second", "Second");
    game::NetworkClient first(firstOptions);
    game::NetworkClient second(secondOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayerState() && second.HasPlayerState() &&
               host.Simulation().Chunks().GetStats().loadedChunks > 0;
    }));

    const std::uint64_t firstOwner = first.PlayerStableId();
    const std::uint64_t secondOwner = second.PlayerStableId();
    REQUIRE(firstOwner != 0);
    REQUIRE(secondOwner != 0);
    REQUIRE(firstOwner != secondOwner);

    game::CompanionConfig companions;
    REQUIRE_MESSAGE(LoadCompanionDefs(root, &companions, &error), error);
    const game::CompanionDef* def =
        companions.Find("companion_medic_lin");
    REQUIRE(def != nullptr);

    entt::registry& registry = host.Simulation().Ecs().Registry();
    const ecs::Entity firstPlayer =
        host.Simulation().Ecs().FindById(firstOwner);
    const ecs::Entity secondPlayer =
        host.Simulation().Ecs().FindById(secondOwner);
    REQUIRE(static_cast<bool>(firstPlayer != entt::null));
    REQUIRE(static_cast<bool>(secondPlayer != entt::null));
    const auto* firstPlayerPosition =
        registry.try_get<ecs::Position>(firstPlayer);
    const auto* secondPlayerPosition =
        registry.try_get<ecs::Position>(secondPlayer);
    REQUIRE(firstPlayerPosition != nullptr);
    REQUIRE(secondPlayerPosition != nullptr);
    const ecs::Position firstStart = *firstPlayerPosition;
    const ecs::Position secondStart = *secondPlayerPosition;

    const ecs::Entity firstCompanion =
        SpawnCompanion(host, *def, firstOwner, firstStart);
    const ecs::Entity secondCompanion =
        SpawnCompanion(host, *def, secondOwner, secondStart);
    REQUIRE(static_cast<bool>(firstCompanion != entt::null));
    REQUIRE(static_cast<bool>(secondCompanion != entt::null));
    const std::uint64_t firstCompanionId =
        host.Simulation().Ecs().IdOf(firstCompanion).id;
    const std::uint64_t secondCompanionId =
        host.Simulation().Ecs().IdOf(secondCompanion).id;
    REQUIRE(firstCompanionId != 0);
    REQUIRE(secondCompanionId != 0);

    auto* firstState = StateAt(host, firstCompanionId);
    auto* secondState = StateAt(host, secondCompanionId);
    REQUIRE(firstState != nullptr);
    REQUIRE(secondState != nullptr);
    firstState->loyalty = 73;
    firstState->command = game::CompanionCommand::Follow;
    firstState->personalStatus = game::CompanionQuestStatus::Active;
    firstState->personalStage = 0;
    firstState->personalProgress = 1;
    firstState->equipment.slots[0] =
        ecs::ItemInstance{"kitchen_knife", 1, 0.5f, 0};
    firstState->equipment.slots[1] =
        ecs::ItemInstance{"jacket", 1, 0.75f, 0};
    secondState->loyalty = 44;
    secondState->command = game::CompanionCommand::Follow;

    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        const game::ReplicatedActor* firstActor =
            first.Replication().Find(firstCompanionId);
        const game::ReplicatedActor* secondActor =
            second.Replication().Find(secondCompanionId);
        return firstActor != nullptr && firstActor->hasCompanion &&
               firstActor->ownerStableId == firstOwner &&
               secondActor != nullptr && secondActor->hasCompanion &&
               secondActor->ownerStableId == secondOwner;
    }));
    const game::ReplicatedActor* firstActor =
        first.Replication().Find(firstCompanionId);
    const game::ReplicatedActor* secondActor =
        second.Replication().Find(secondCompanionId);
    REQUIRE(firstActor != nullptr);
    REQUIRE(secondActor != nullptr);
    CHECK(firstActor->companion.defId == def->id);
    CHECK(firstActor->companion.loyalty == 73);
    CHECK(firstActor->companion.questStatus ==
          game::CompanionQuestStatus::Active);
    CHECK(firstActor->companion.questProgress == 1);
    CHECK(secondActor->companion.loyalty == 44);

    auto* secondPlayerHealth = registry.try_get<ecs::Health>(secondPlayer);
    REQUIRE(secondPlayerHealth != nullptr);
    secondPlayerHealth->current = 0.0f;
    REQUIRE(PumpHostUntil(host, 4000, [&] {
        const auto* state = StateAt(host, secondCompanionId);
        return state != nullptr && state->standby;
    }));
    CHECK(CompanionCount(host) == 2);
    CHECK(StateAt(host, secondCompanionId)->standby);
    CHECK(StateAt(host, firstCompanionId)->standby == false);

    first.Close();
    REQUIRE(PumpHostUntil(host, 6000, [&] {
        const auto* state = StateAt(host, firstCompanionId);
        return state != nullptr && state->standby;
    }));
    CHECK(CompanionCount(host) == 2);
    CHECK(StateAt(host, firstCompanionId)->standby);
    CHECK(StateAt(host, secondCompanionId)->standby);
    const auto firstIdentity =
        registry.try_get<game::Companion>(firstCompanion);
    const auto secondIdentity =
        registry.try_get<game::Companion>(secondCompanion);
    REQUIRE(firstIdentity != nullptr);
    REQUIRE(secondIdentity != nullptr);
    CHECK(firstIdentity->ownerStableId == firstOwner);
    CHECK(secondIdentity->ownerStableId == secondOwner);

    game::NetworkClient reconnected(firstOptions);
    REQUIRE(reconnected.Connect(&error));
    REQUIRE(PumpUntil(host, second, reconnected, 8000, [&] {
        return reconnected.IsConnected() && reconnected.HasPlayer() &&
               reconnected.HasPlayerState();
    }));
    CHECK(reconnected.PlayerStableId() == firstOwner);
    CHECK(CompanionCount(host) == 2);
    const ecs::Entity owned =
        game::FindOwnedCompanion(host.Simulation().Ecs(), firstOwner);
    REQUIRE(static_cast<bool>(owned != entt::null));
    CHECK(host.Simulation().Ecs().IdOf(owned).id == firstCompanionId);
    CHECK(StateAt(host, firstCompanionId)->standby);
    CHECK(StateAt(host, secondCompanionId)->standby);
    CHECK(StateAt(host, firstCompanionId)->loyalty == 73);

    reconnected.Close();
    second.Close();
    host.Shutdown();
}

TEST_CASE("M33-F3-T6 服务器重启恢复同伴 owner/信任/委托/装备") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path dir = TempDir("restart");
    const std::filesystem::path saveRoot = dir / "saves";
    const std::string profilePath = (dir / "players.bin").string();
    std::string saveId;
    CreateEmptySave(saveRoot, &saveId);
    REQUIRE_FALSE(saveId.empty());

    const std::uint64_t contentHash = 0x4D33335436525354ull;
    const server::ServerHost::Options options =
        MakeHostOptions(root, saveRoot, profilePath, contentHash, saveId);
    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);

    game::NetworkClient::Options clientOptions =
        MakeClientOptions(host, contentHash, "f3t6-restart", "Restart");
    game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 8000, [&] {
        return client.IsConnected() && client.HasPlayerState() &&
               host.Simulation().Chunks().GetStats().loadedChunks > 0;
    }));
    const std::uint64_t ownerStableId = client.PlayerStableId();
    REQUIRE(ownerStableId != 0);

    game::CompanionConfig companions;
    REQUIRE_MESSAGE(LoadCompanionDefs(root, &companions, &error), error);
    const game::CompanionDef* def =
        companions.Find("companion_medic_lin");
    REQUIRE(def != nullptr);
    entt::registry& registry = host.Simulation().Ecs().Registry();
    const ecs::Entity player =
        host.Simulation().Ecs().FindById(ownerStableId);
    REQUIRE(static_cast<bool>(player != entt::null));
    const auto* playerPosition = registry.try_get<ecs::Position>(player);
    REQUIRE(playerPosition != nullptr);
    const ecs::Entity companion =
        SpawnCompanion(host, *def, ownerStableId, *playerPosition);
    REQUIRE(static_cast<bool>(companion != entt::null));
    const std::uint64_t companionId =
        host.Simulation().Ecs().IdOf(companion).id;
    REQUIRE(companionId != 0);
    auto* state = StateAt(host, companionId);
    REQUIRE(state != nullptr);
    state->loyalty = 81;
    state->personalStatus = game::CompanionQuestStatus::Active;
    state->personalStage = 1;
    state->personalProgress = 2;
    state->personalRewardMask = 1;
    state->equipment.slots[0] =
        ecs::ItemInstance{"machete", 1, 0.4f, 0};
    state->equipment.slots[1] =
        ecs::ItemInstance{"padded_jacket", 1, 0.6f, 0};
    auto* health = registry.try_get<ecs::Health>(companion);
    REQUIRE(health != nullptr);
    health->current = 66.0f;

    REQUIRE(PumpUntil(host, client, 8000, [&] {
        const game::ReplicatedActor* actor =
            client.Replication().Find(companionId);
        return actor != nullptr && actor->hasCompanion &&
               actor->ownerStableId == ownerStableId &&
               actor->companion.loyalty == 81;
    }));
    const game::ReplicatedActor* before =
        client.Replication().Find(companionId);
    REQUIRE(before != nullptr);
    CHECK_FALSE(before->companion.standby);
    CHECK(before->companion.questStage == 1);
    CHECK(before->companion.questProgress == 2);

    host.Shutdown();
    client.Close();

    server::ServerHost restarted;
    REQUIRE_MESSAGE(restarted.Init(options, &error), error);
    CHECK(CompanionCount(restarted) == 1);
    const ecs::Entity restored =
        restarted.Simulation().Ecs().FindById(companionId);
    REQUIRE(static_cast<bool>(restored != entt::null));
    const auto* restoredIdentity =
        restarted.Simulation().Ecs().Registry().try_get<game::Companion>(
            restored);
    const auto* restoredState =
        restarted.Simulation().Ecs().Registry().try_get<game::CompanionState>(
            restored);
    const auto* restoredHealth =
        restarted.Simulation().Ecs().Registry().try_get<ecs::Health>(
            restored);
    REQUIRE(restoredIdentity != nullptr);
    REQUIRE(restoredState != nullptr);
    REQUIRE(restoredHealth != nullptr);
    CHECK(restoredIdentity->ownerStableId == ownerStableId);
    CHECK(restoredIdentity->homeSettlementId == "village_haven");
    CHECK(restoredState->loyalty == 81);
    CHECK(restoredState->personalStatus ==
          game::CompanionQuestStatus::Active);
    CHECK(restoredState->personalStage == 1);
    CHECK(restoredState->personalProgress == 2);
    CHECK(restoredState->personalRewardMask == 1u);
    CHECK_FALSE(restoredState->standby);
    CHECK(restoredHealth->current == doctest::Approx(66.0f));
    REQUIRE(restoredState->equipment.slots[0].has_value());
    REQUIRE(restoredState->equipment.slots[1].has_value());
    CHECK(restoredState->equipment.slots[0]->defId == "machete");
    CHECK(restoredState->equipment.slots[1]->defId == "padded_jacket");

    clientOptions.port = restarted.NetworkPort();
    game::NetworkClient rejoined(clientOptions);
    REQUIRE(rejoined.Connect(&error));
    REQUIRE(PumpUntil(restarted, rejoined, 8000, [&] {
        return rejoined.IsConnected() && rejoined.HasPlayerState();
    }));
    CHECK(rejoined.PlayerStableId() == ownerStableId);
    CHECK(CompanionCount(restarted) == 1);
    const ecs::Entity owned =
        game::FindOwnedCompanion(restarted.Simulation().Ecs(),
                                 ownerStableId);
    REQUIRE(static_cast<bool>(owned != entt::null));
    CHECK(restarted.Simulation().Ecs().IdOf(owned).id == companionId);
    REQUIRE(PumpUntil(restarted, rejoined, 8000, [&] {
        const game::ReplicatedActor* actor =
            rejoined.Replication().Find(companionId);
        return actor != nullptr && actor->hasCompanion &&
               actor->ownerStableId == ownerStableId &&
               actor->companion.loyalty == 81;
    }));
    const game::ReplicatedActor* after =
        rejoined.Replication().Find(companionId);
    REQUIRE(after != nullptr);
    CHECK(after->companion.questStatus ==
          game::CompanionQuestStatus::Active);
    CHECK(after->companion.questStage == 1);
    CHECK(after->companion.questProgress == 2);
    REQUIRE(after->hasEquipment);
    REQUIRE(after->equipment[0].has_value());
    CHECK(after->equipment[0]->defId == "machete");

    rejoined.Close();
    restarted.Shutdown();
}

TEST_CASE("M33-F3-T6 单机读档恢复同伴信任/个人委托/特长/装备") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "assets" / "items.json"));
    items::ItemRegistry items;
    REQUIRE(items.LoadFile(root / "assets" / "items.json"));

    ecs::EcsWorld source;
    entt::registry& registry = source.Registry();
    const ecs::Entity companion = source.CreateWithStableId(700);
    registry.emplace<ecs::Position>(companion,
                                    ecs::Position{12.5f, 8.5f, 0});
    registry.emplace<ecs::Velocity>(companion, ecs::Velocity{});
    registry.emplace<ecs::SpriteRef>(
        companion, ecs::SpriteRef{ecs::HashName("character.placeholder"), 0,
                                  0xFF9AD8B0u});
    registry.emplace<ecs::Facing>(companion, ecs::Facing{});
    registry.emplace<ecs::AnimState>(companion, ecs::AnimState{});
    registry.emplace<ecs::Health>(companion, ecs::Health{66.0f, 95.0f});
    registry.emplace<game::Companion>(
        companion, game::Companion{"companion_medic_lin", 1234,
                                   "village_haven"});
    game::CompanionState state;
    state.loyalty = 81;
    state.command = game::CompanionCommand::Stay;
    state.standby = true;
    state.personalStatus = game::CompanionQuestStatus::Active;
    state.personalStage = 1;
    state.personalProgress = 2;
    state.personalRewardMask = 1;
    state.extraTraits.push_back("triage_drills");
    state.inventory.capacityKg = 12.0f;
    state.inventory.items.push_back(
        ecs::ItemInstance{"bandage", 2, 1.0f, 0});
    state.equipment.slots[0] =
        ecs::ItemInstance{"kitchen_knife", 1, 0.5f, 0};
    state.equipment.slots[1] =
        ecs::ItemInstance{"jacket", 1, 0.75f, 0};
    registry.emplace<game::CompanionState>(companion, std::move(state));

    save::ComponentRegistry components;
    std::string error;
    REQUIRE_MESSAGE(save::RegisterBuiltinComponents(components, &error),
                    error);
    save::EntitySnapshot snapshot;
    REQUIRE_MESSAGE(save::CaptureEntities(source, components, &snapshot, {},
                                          &error),
                    error);

    const std::filesystem::path dir = TempDir("single");
    save::SaveManager::Options options;
    options.root = dir / "saves";
    save::SaveManager manager(options);
    save::SaveManager::CreateOptions create;
    create.name = "M33-F3-T6";
    create.mapId = "village_demo";
    create.gameVersion = save::kDefaultGameVersion;
    create.entities = &snapshot;
    std::string saveId;
    REQUIRE_MESSAGE(manager.Create(create, &saveId, &error), error);

    ecs::EcsWorld target;
    save::EntitySnapshot loadedSnapshot;
    save::SaveMetadata metadata;
    REQUIRE(manager.Load(saveId, components, &loadedSnapshot, &metadata,
                         &error) == save::SaveManager::LoadResult::Ok);
    std::vector<ecs::Entity> loaded;
    REQUIRE_MESSAGE(
        save::LoadEntities(loadedSnapshot, target, components, &loaded,
                           &error),
        error);
    const ecs::Entity restored = target.FindById(700);
    REQUIRE(static_cast<bool>(restored != entt::null));
    entt::registry& restoredRegistry = target.Registry();
    const auto* identity =
        restoredRegistry.try_get<game::Companion>(restored);
    const auto* restoredState =
        restoredRegistry.try_get<game::CompanionState>(restored);
    const auto* health =
        restoredRegistry.try_get<ecs::Health>(restored);
    REQUIRE(identity != nullptr);
    REQUIRE(restoredState != nullptr);
    REQUIRE(health != nullptr);
    CHECK(identity->defId == "companion_medic_lin");
    CHECK(identity->ownerStableId == 1234u);
    CHECK(identity->homeSettlementId == "village_haven");
    CHECK(restoredState->loyalty == 81);
    CHECK(restoredState->command == game::CompanionCommand::Stay);
    CHECK(restoredState->standby);
    CHECK(restoredState->personalStatus ==
          game::CompanionQuestStatus::Active);
    CHECK(restoredState->personalStage == 1);
    CHECK(restoredState->personalProgress == 2);
    CHECK(restoredState->personalRewardMask == 1u);
    REQUIRE(restoredState->extraTraits.size() == 1u);
    CHECK(restoredState->extraTraits[0] == "triage_drills");
    CHECK(health->current == doctest::Approx(66.0f));
    CHECK(health->max == doctest::Approx(95.0f));
    CHECK(items::CountOf(restoredState->inventory, "bandage") == 2);
    REQUIRE(restoredState->equipment.slots[0].has_value());
    REQUIRE(restoredState->equipment.slots[1].has_value());
    CHECK(restoredState->equipment.slots[0]->defId == "kitchen_knife");
    CHECK(restoredState->equipment.slots[1]->defId == "jacket");
    REQUIRE(game::PrepareLoadedCompanion(target, restored, &items));
    CHECK(restoredRegistry.all_of<ecs::FriendlyNpc>(restored));
    CHECK(restoredRegistry.all_of<ecs::NoCombatTarget>(restored));
}
