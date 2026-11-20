#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/systems/CombatSystem.h"
#include "core/Config.h"
#include "core/SoundEvent.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/BuildingRules.h"
#include "game/Homestead.h"
#include "game/MissionComponents.h"
#include "game/NetworkClient.h"
#include "game/PlayerStorage.h"
#include "game/PoiComponents.h"
#include "game/StructureComponents.h"
#include "game/WorldSimulation.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "net/Command.h"
#include "net/WorldSound.h"
#include "physics/PhysicsWorld.h"
#include "server/ServerHost.h"
#include "ui/InventoryUi.h"
#include "world/ChunkManager.h"
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
bool PumpUntil(zl::server::ServerHost& host, zl::game::NetworkClient& client,
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
        client.Poll();
        second.Poll();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

template <typename Predicate>
bool PollClientUntil(zl::game::NetworkClient& client, int timeoutMs,
                     Predicate predicate) {
    const Clock::time_point deadline =
        Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        client.Poll();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

std::uint64_t SpawnItem(zl::game::WorldSimulation& simulation, const char* defId,
                        int count, float x, float y) {
    auto& world = simulation.Ecs();
    entt::registry& registry = world.Registry();
    const zl::ecs::Entity item = world.Create();
    registry.emplace<zl::ecs::Position>(item, zl::ecs::Position{x, y, 0});
    registry.emplace<zl::ecs::ItemInstance>(
        item, zl::ecs::ItemInstance{defId, count, 1.0f});
    registry.emplace<zl::ecs::SpriteRef>(
        item, zl::ecs::SpriteRef{zl::ecs::HashName("item.test"), -1, 0xFFFFFFFFu});
    return world.IdOf(item).id;
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

std::filesystem::path TempProfileDir(const char* name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zomboidlike_m23_t10" / name;
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir, error);
    return dir;
}

}  // namespace

TEST_CASE("M13-T7 联机背包：拾取/使用/丢弃/装备与状态同步") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4E4554494E5631ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;
    zl::game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.IsConnected() && client.HasPlayer() && client.HasPlayerState();
    }));
    const std::uint64_t playerId = client.PlayerStableId();
    REQUIRE(playerId != 0);

    const zl::ecs::Entity player = host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));
    const zl::ecs::Position spawnPosition =
        host.Simulation().Ecs().Registry().get<zl::ecs::Position>(player);
    const std::uint64_t bandageId =
        SpawnItem(host.Simulation(), "bandage", 2,
                  spawnPosition.x + 0.2f, spawnPosition.y);
    REQUIRE(bandageId != 0);

    zl::net::Command pickup;
    pickup.type = zl::net::CommandType::PickupEntity;
    pickup.playerStableId = playerId;
    pickup.payload.entityId = bandageId;
    REQUIRE(client.SendCommand(pickup, &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return BagCount(client.PlayerState(), "bandage") == 2;
    }));

    zl::net::Command use;
    use.type = zl::net::CommandType::UseBag;
    use.playerStableId = playerId;
    use.payload.index = 0;
    REQUIRE(client.SendCommand(use, &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return BagCount(client.PlayerState(), "bandage") == 1;
    }));

    zl::net::Command drop;
    drop.type = zl::net::CommandType::DropBag;
    drop.playerStableId = playerId;
    drop.payload.index = 0;
    REQUIRE(client.SendCommand(drop, &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        if (BagCount(client.PlayerState(), "bandage") != 0) {
            return false;
        }
        for (const auto& [stableId, actor] : client.Replication().Actors()) {
            static_cast<void>(stableId);
            if (actor.hasItem && actor.item.defId == "bandage") {
                return true;
            }
        }
        return false;
    }));

    const std::uint64_t axeId =
        SpawnItem(host.Simulation(), "axe", 1,
                  spawnPosition.x + 0.2f, spawnPosition.y);
    REQUIRE(axeId != 0);
    pickup.payload.entityId = axeId;
    REQUIRE(client.SendCommand(pickup, &error));
    REQUIRE_MESSAGE(PumpUntil(host, client, 5000, [&] {
        return BagCount(client.PlayerState(), "axe") == 1;
    }), client.PlayerState().status);

    zl::net::Command equip;
    equip.type = zl::net::CommandType::EquipBag;
    equip.playerStableId = playerId;
    equip.payload.index = 0;
    equip.payload.targetIndex = 0;
    REQUIRE(client.SendCommand(equip, &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return client.PlayerState().equipment[0].present &&
               client.PlayerState().equipment[0].item.defId == "axe";
    }));
    const auto& equipment =
        host.Simulation().Ecs().Registry().get<zl::ecs::Equipment>(player);
    REQUIRE(equipment.slots[0].has_value());
    CHECK(equipment.slots[0]->defId == "axe");

    auto& rangedEquipment = host.Simulation().Ecs().Registry().get<zl::ecs::Equipment>(
        player);
    rangedEquipment.slots[0] = zl::ecs::ItemInstance{"pistol", 1, 1.0f, 8};
    host.Simulation().Ecs().Registry().get<zl::ecs::Container>(player)
        .items.push_back(zl::ecs::ItemInstance{"ammo_9mm", 8, 1.0f});

    // 服务器权威制作：投入 2 块破布，服务器计时 0.8s 后把绷带写回权威背包。
    auto& serverBag = host.Simulation().Ecs().Registry().get<zl::ecs::Container>(
        player);
    serverBag.items.push_back(zl::ecs::ItemInstance{"rag", 1, 1.0f});
    serverBag.items.push_back(zl::ecs::ItemInstance{"rag", 1, 1.0f});
    zl::net::Command craft;
    craft.type = zl::net::CommandType::InventoryAction;
    craft.playerStableId = playerId;
    craft.payload.flags = static_cast<std::uint16_t>(
        zl::ui::InventoryActionType::Craft);
    craft.payload.area = static_cast<std::uint8_t>(zl::ui::InventoryArea::Backpack);
    craft.payload.targetArea =
        static_cast<std::uint8_t>(zl::ui::InventoryArea::Backpack);
    craft.payload.index = 0;  // field_bandage
    REQUIRE(client.SendCommand(craft, &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return client.PlayerState().craftingActive;
    }));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return !client.PlayerState().craftingActive &&
               BagCount(client.PlayerState(), "bandage") == 1;
    }));
    CHECK(host.GetNetworkStats().effectsSpawned > 0);

    zl::net::Command shot;
    shot.type = zl::net::CommandType::Gunshot;
    shot.playerStableId = playerId;
    const std::uint64_t shotsBefore = host.GetNetworkStats().gunshots;
    REQUIRE(client.SendCommand(shot, &error));
    host.RunForTicks(2);
    CHECK(host.GetNetworkStats().gunshots > shotsBefore);

    const auto& playerPosition =
        host.Simulation().Ecs().Registry().get<zl::ecs::Position>(player);
    const zl::ecs::Entity target =
        host.Simulation().SpawnZombieAt(playerPosition.x + 1.0f, playerPosition.y);
    REQUIRE(static_cast<bool>(target != entt::null));
    const float targetHealth =
        host.Simulation().Ecs().Registry().get<zl::ecs::Health>(target).current;
    zl::net::Command fire;
    fire.type = zl::net::CommandType::FireRanged;
    fire.playerStableId = playerId;
    fire.payload.x = 1.0f;
    fire.payload.y = 0.0f;
    const std::uint64_t firedBefore = host.GetNetworkStats().projectilesFired;
    bool sawImpact = false;
    REQUIRE(client.SendCommand(fire, &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        for (const auto& [stableId, actor] : client.Replication().Actors()) {
            static_cast<void>(stableId);
            if (actor.archetype == zl::game::kReplicationArchetypeEffect) {
                sawImpact = true;
            }
        }
        return host.GetNetworkStats().projectilesFired > firedBefore &&
               host.GetNetworkStats().projectilesHit > 0 &&
               host.Simulation()
                       .Ecs()
                       .Registry()
                       .get<zl::ecs::Health>(target)
                       .current < targetHealth;
    }));
    CHECK(sawImpact);

    const std::uint64_t cooldownBefore =
        host.GetNetworkStats().cooldownRejected;
    REQUIRE(client.SendCommand(fire, &error));
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        return host.GetNetworkStats().cooldownRejected > cooldownBefore;
    }));

    const zl::ecs::Entity visibleProjectile =
        host.Simulation().Ecs().Create();
    host.Simulation().Ecs().Registry().emplace<zl::ecs::Position>(
        visibleProjectile,
        zl::ecs::Position{playerPosition.x - 0.5f, playerPosition.y, 0});
    host.Simulation().Ecs().Registry().emplace<zl::ecs::Velocity>(
        visibleProjectile, zl::ecs::Velocity{});
    host.Simulation().Ecs().Registry().emplace<zl::ecs::SpriteRef>(
        visibleProjectile,
        zl::ecs::SpriteRef{zl::ecs::HashName("projectile.bullet"), 1,
                           0xFFFFC040u});
    host.Simulation().Ecs().Registry().emplace<zl::ecs::Projectile>(
        visibleProjectile, zl::ecs::Projectile{playerId});
    const std::uint64_t visibleProjectileId =
        host.Simulation().Ecs().IdOf(visibleProjectile).id;
    REQUIRE(PumpUntil(host, client, 4000, [&] {
        const auto* actor =
            client.Replication().Find(visibleProjectileId);
        return actor != nullptr &&
               actor->archetype ==
                   zl::game::kReplicationArchetypeProjectile;
    }));
    host.Simulation().DespawnEntity(visibleProjectile);
    host.RunForTicks(2);

    client.Close();
    host.Shutdown();
}

TEST_CASE("M23-F1-T6 联机远程武器：服务器权威装备/弹药/冷却/换弹") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x52414E47454431ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);
    CHECK_FALSE(host.Config().Get<bool>("combat.pvp", true));

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;
    zl::game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.IsConnected() && client.HasPlayer();
    }));
    const std::uint64_t playerId = client.PlayerStableId();
    REQUIRE(playerId != 0);
    const zl::ecs::Entity player = host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));
    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Position spawnPosition =
        registry.get<zl::ecs::Position>(player);

    zl::net::Command fire;
    fire.type = zl::net::CommandType::FireRanged;
    fire.playerStableId = playerId;
    fire.payload.x = 1.0f;
    fire.payload.y = 0.0f;

    const std::uint64_t rejectedBefore = host.GetNetworkStats().commandsRejected;
    REQUIRE(client.SendCommand(fire, &error));
    host.RunForTicks(3);
    CHECK(host.GetNetworkStats().commandsRejected > rejectedBefore);

    auto& equipment = registry.get<zl::ecs::Equipment>(player);
    equipment.slots[static_cast<std::size_t>(zl::ecs::EquipSlot::PrimaryHand)] =
        zl::ecs::ItemInstance{"pistol", 1, 1.0f, 8};
    auto& bag = registry.get<zl::ecs::Container>(player);
    bag.items.push_back(zl::ecs::ItemInstance{"ammo_9mm", 8, 1.0f});

    const zl::ecs::Entity target = host.Simulation().Ecs().Create();
    registry.emplace<zl::ecs::Position>(
        target,
        zl::ecs::Position{spawnPosition.x + 0.8f, spawnPosition.y, 0});
    registry.emplace<zl::ecs::Health>(target, zl::ecs::Health{100.0f, 100.0f});

    const zl::ecs::Entity playerTarget = host.Simulation().Ecs().Create();
    registry.emplace<zl::ecs::Position>(
        playerTarget,
        zl::ecs::Position{spawnPosition.x + 0.8f, spawnPosition.y, 0});
    registry.emplace<zl::ecs::PlayerTag>(playerTarget);
    registry.emplace<zl::ecs::Health>(playerTarget,
                                      zl::ecs::Health{100.0f, 100.0f});

    const std::uint64_t firedBefore = host.GetNetworkStats().projectilesFired;
    const std::uint64_t gunshotsBefore = host.GetNetworkStats().gunshots;
    REQUIRE(client.SendCommand(fire, &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto& weapon =
            *equipment.slots[static_cast<std::size_t>(
                zl::ecs::EquipSlot::PrimaryHand)];
        return weapon.loadedAmmo == 7 &&
               registry.get<zl::ecs::Health>(target).current < 100.0f;
    }));
    CHECK(host.GetNetworkStats().projectilesFired > firedBefore);
    CHECK(host.GetNetworkStats().gunshots > gunshotsBefore);
    CHECK(registry.get<zl::ecs::Health>(playerTarget).current == 100.0f);

    const std::uint64_t cooldownBefore = host.GetNetworkStats().cooldownRejected;
    REQUIRE(client.SendCommand(fire, &error));
    REQUIRE(PumpUntil(host, client, 2000, [&] {
        return host.GetNetworkStats().cooldownRejected > cooldownBefore;
    }));

    zl::net::Command reload;
    reload.type = zl::net::CommandType::ReloadRanged;
    reload.playerStableId = playerId;
    REQUIRE(client.SendCommand(reload, &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto& weapon =
            *equipment.slots[static_cast<std::size_t>(
                zl::ecs::EquipSlot::PrimaryHand)];
        return weapon.loadedAmmo == 8 &&
               zl::items::CountOf(bag, "ammo_9mm") == 7;
    }));

    client.Close();
    host.Shutdown();
}

TEST_CASE("M23-F1-T7 联机远程武器：弹匣/备弹/换弹进度复制与反作弊") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x52414E47454432ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;
    zl::game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.IsConnected() && client.HasPlayer() &&
               client.HasPlayerState();
    }));
    const std::uint64_t playerId = client.PlayerStableId();
    REQUIRE(playerId != 0);
    const zl::ecs::Entity player = host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));
    auto& registry = host.Simulation().Ecs().Registry();
    auto& equipment = registry.get<zl::ecs::Equipment>(player);
    auto& weaponSlot =
        equipment.slots[static_cast<std::size_t>(
            zl::ecs::EquipSlot::PrimaryHand)];
    weaponSlot = zl::ecs::ItemInstance{"pistol", 1, 1.0f, 1};
    auto& weapon = *weaponSlot;
    auto& bag = registry.get<zl::ecs::Container>(player);
    bag.items.push_back(zl::ecs::ItemInstance{"ammo_9mm", 8, 1.0f});

    zl::net::Command fire;
    fire.type = zl::net::CommandType::FireRanged;
    fire.playerStableId = playerId;
    fire.payload.x = 1.0f;
    fire.payload.y = 0.0f;

    const std::uint64_t firedBefore = host.GetNetworkStats().projectilesFired;
    bool sawProjectileOwner = false;
    REQUIRE(client.SendCommand(fire, &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        for (const auto& [stableId, actor] : client.Replication().Actors()) {
            static_cast<void>(stableId);
            if (actor.archetype == zl::game::kReplicationArchetypeProjectile &&
                actor.ownerStableId == playerId) {
                sawProjectileOwner = true;
            }
        }
        if (!client.HasPlayerState()) {
            return false;
        }
        const auto& state = client.PlayerState();
        return state.equipment[0].present &&
               state.equipment[0].loadedAmmo == 0 &&
               state.ranged.valid && state.ranged.loaded == 0 &&
               state.ranged.reserve == 8;
    }));
    CHECK(sawProjectileOwner);
    CHECK(host.GetNetworkStats().projectilesFired > firedBefore);

    const std::uint64_t cooldownBefore =
        host.GetNetworkStats().cooldownRejected;
    REQUIRE(client.SendCommand(fire, &error));
    REQUIRE(PumpUntil(host, client, 2000, [&] {
        return host.GetNetworkStats().cooldownRejected > cooldownBefore;
    }));

    zl::net::Command reload;
    reload.type = zl::net::CommandType::ReloadRanged;
    reload.playerStableId = playerId;
    REQUIRE(client.SendCommand(reload, &error));
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        if (!client.HasPlayerState()) {
            return false;
        }
        const auto& state = client.PlayerState();
        return state.ranged.reloading &&
               state.ranged.reloadElapsed > 0.0f &&
               state.ranged.reloadDuration == doctest::Approx(2.0f);
    }));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        if (!client.HasPlayerState()) {
            return false;
        }
        const auto& state = client.PlayerState();
        return weapon.loadedAmmo == 8 &&
               zl::items::CountOf(bag, "ammo_9mm") == 0 &&
               state.equipment[0].loadedAmmo == 8 &&
               state.ranged.loaded == 8 &&
               state.ranged.reserve == 0 && !state.ranged.reloading;
    }));

    zl::net::Command oldPayload = fire;
    oldPayload.type = zl::net::CommandType::FireProjectile;
    oldPayload.payload.x = 0.0f;
    oldPayload.payload.y = 0.0f;
    oldPayload.payload.entityId = 0;
    const std::uint64_t fallbackBefore =
        host.GetNetworkStats().projectilesFired;
    REQUIRE(client.SendCommand(oldPayload, &error));
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        return host.GetNetworkStats().projectilesFired > fallbackBefore;
    }));

    client.Close();
    host.Shutdown();
}

TEST_CASE("M23-F1-T10 联机远程武器：双客户端弹药/投射物/PvP 伤害/重连") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);
    const std::filesystem::path profileDir = TempProfileDir("ranged");
    const std::string profilePath = (profileDir / "players.bin").string();

    const char* args[] = {"ZomboidLikeTests", "--combat.pvp=true"};
    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.argc = 2;
    hostOptions.argv = args;
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x52414E4745443130ull;
    hostOptions.enablePlayerProfiles = true;
    hostOptions.playerProfilesFile = profilePath;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);
    CHECK(host.Config().Get<bool>("combat.pvp", false));

    zl::game::NetworkClient::Options shooterOptions;
    shooterOptions.host = "127.0.0.1";
    shooterOptions.port = host.NetworkPort();
    shooterOptions.contentHash = hostOptions.contentHash;
    shooterOptions.autoReconnect = false;
    shooterOptions.playerIdentity = "m23-t10-shooter";
    shooterOptions.playerName = "Shooter";
    zl::game::NetworkClient::Options targetOptions = shooterOptions;
    targetOptions.playerIdentity = "m23-t10-target";
    targetOptions.playerName = "Target";

    zl::game::NetworkClient shooter(shooterOptions);
    zl::game::NetworkClient target(targetOptions);
    REQUIRE(shooter.Connect(&error));
    REQUIRE(target.Connect(&error));
    REQUIRE(PumpUntil(host, shooter, target, 6000, [&] {
        return shooter.IsConnected() && target.IsConnected() &&
               shooter.HasPlayer() && target.HasPlayer() &&
               shooter.HasPlayerState() && target.HasPlayerState() &&
               host.SessionCount() == 2;
    }));

    const std::uint64_t shooterId = shooter.PlayerStableId();
    const std::uint64_t targetId = target.PlayerStableId();
    REQUIRE(shooterId != 0);
    REQUIRE(targetId != 0);
    CHECK(shooterId != targetId);

    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity shooterEntity =
        host.Simulation().Ecs().FindById(shooterId);
    const zl::ecs::Entity targetEntity =
        host.Simulation().Ecs().FindById(targetId);
    REQUIRE(static_cast<bool>(shooterEntity != entt::null));
    REQUIRE(static_cast<bool>(targetEntity != entt::null));
    zl::ecs::Position shooterPosition =
        registry.get<zl::ecs::Position>(shooterEntity);
    zl::ecs::Position targetPosition =
        registry.get<zl::ecs::Position>(targetEntity);
    REQUIRE(targetPosition.z == shooterPosition.z);
    auto& targetRigidBody = registry.get<zl::ecs::RigidBody>(targetEntity);
    REQUIRE(targetRigidBody.body != nullptr);
    host.Simulation().Physics().TeleportBody(
        targetRigidBody.body, shooterPosition.x + 0.8f, shooterPosition.y,
        shooterPosition.z);
    if (auto* velocity = registry.try_get<zl::ecs::Velocity>(targetEntity);
        velocity != nullptr) {
        *velocity = {};
    }
    host.RunForTicks(2);
    shooterPosition = registry.get<zl::ecs::Position>(shooterEntity);
    targetPosition = registry.get<zl::ecs::Position>(targetEntity);
    REQUIRE(targetPosition.z == shooterPosition.z);
    REQUIRE(targetPosition.x - shooterPosition.x > 0.6f);

    auto& equipment = registry.get<zl::ecs::Equipment>(shooterEntity);
    auto& weapon = equipment.slots[static_cast<std::size_t>(
        zl::ecs::EquipSlot::PrimaryHand)];
    weapon = zl::ecs::ItemInstance{"pistol", 1, 1.0f, 8};
    auto& bag = registry.get<zl::ecs::Container>(shooterEntity);
    bag.items.push_back(zl::ecs::ItemInstance{"ammo_9mm", 8, 1.0f});

    zl::net::Command fire;
    fire.type = zl::net::CommandType::FireRanged;
    fire.playerStableId = shooterId;

    const auto aimAtTarget = [&] {
        const zl::ecs::Position currentShooter =
            registry.get<zl::ecs::Position>(shooterEntity);
        auto& targetBody = registry.get<zl::ecs::RigidBody>(targetEntity);
        REQUIRE(targetBody.body != nullptr);
        host.Simulation().Physics().TeleportBody(
            targetBody.body, currentShooter.x + 0.8f, currentShooter.y,
            currentShooter.z);
        if (auto* velocity = registry.try_get<zl::ecs::Velocity>(targetEntity);
            velocity != nullptr) {
            *velocity = {};
        }
        host.RunForTicks(2);
        shooterPosition = registry.get<zl::ecs::Position>(shooterEntity);
        targetPosition = registry.get<zl::ecs::Position>(targetEntity);
        REQUIRE(targetPosition.z == shooterPosition.z);
        REQUIRE(targetPosition.x - shooterPosition.x > 0.6f);
        fire.payload.x = targetPosition.x - shooterPosition.x;
        fire.payload.y = targetPosition.y - shooterPosition.y;
    };
    aimAtTarget();

    const std::uint64_t firedBefore = host.GetNetworkStats().projectilesFired;
    const std::uint64_t gunshotsBefore = host.GetNetworkStats().gunshots;

    REQUIRE(shooter.SendCommand(fire, &error));
    const bool fireReplicated = PumpUntil(host, shooter, target, 5000, [&] {
        const float targetHealth =
            registry.get<zl::ecs::Health>(targetEntity).current;
        return shooter.HasPlayerState() && target.HasPlayerState() &&
               shooter.PlayerState().equipment[0].loadedAmmo == 7 &&
               shooter.PlayerState().ranged.loaded == 7 &&
               shooter.PlayerState().ranged.reserve == 8 &&
               targetHealth < 100.0f &&
               target.PlayerState().health < 100.0f;
    });
    INFO("fire replicated=" << fireReplicated << " shooterState="
         << shooter.HasPlayerState() << " targetState="
         << target.HasPlayerState() << " serverTargetHp="
         << registry.get<zl::ecs::Health>(targetEntity).current);
    if (shooter.HasPlayerState()) {
        INFO("shooter loaded=" << shooter.PlayerState().ranged.loaded
             << " reserve=" << shooter.PlayerState().ranged.reserve
             << " equipLoaded=" << shooter.PlayerState().equipment[0].loadedAmmo);
    }
    if (target.HasPlayerState()) {
        INFO("target hp=" << target.PlayerState().health);
    }
    REQUIRE(fireReplicated);
    CHECK(host.GetNetworkStats().projectilesFired > firedBefore);
    CHECK(host.GetNetworkStats().gunshots > gunshotsBefore);
    CHECK(host.GetNetworkStats().projectilesHit > 0);
    CHECK(registry.get<zl::ecs::Health>(targetEntity).current <= 82.0f);
    CHECK(target.PlayerState().health <= 82.0f);

    // M23-F1-T11：服务器把枪声广播给双方，客户端统一播放并生成枪口效果。
    REQUIRE(PumpUntil(host, shooter, target, 2000, [&] {
        return target.GetStats().worldSoundsReceived > 0 &&
               shooter.GetStats().worldSoundsReceived > 0;
    }));
    const std::vector<zl::net::WorldSoundEvent> targetSounds =
        target.TakeWorldSounds();
    const std::vector<zl::net::WorldSoundEvent> shooterSounds =
        shooter.TakeWorldSounds();
    REQUIRE(!targetSounds.empty());
    REQUIRE(!shooterSounds.empty());
    CHECK(targetSounds.front().sourcePlayerStableId == shooterId);
    CHECK(shooterSounds.front().sourcePlayerStableId == shooterId);
    CHECK(targetSounds.front().type ==
          static_cast<std::uint8_t>(zl::core::SoundType::Gunshot));
    CHECK(targetSounds.front().clip == "gunshot");
    CHECK(targetSounds.front().x ==
          doctest::Approx(shooterPosition.x).epsilon(0.001));
    CHECK(targetSounds.front().y ==
          doctest::Approx(shooterPosition.y).epsilon(0.001));
    CHECK(targetSounds.front().z == shooterPosition.z);
    CHECK(targetSounds.front().radius > 0.0f);
    CHECK(host.GetNetworkStats().worldSoundsSent >= 2);

    const zl::ecs::Entity replicatedProjectile =
        host.Simulation().Ecs().Create();
    registry.emplace<zl::ecs::Position>(
        replicatedProjectile,
        zl::ecs::Position{shooterPosition.x + 0.5f, shooterPosition.y,
                          shooterPosition.z});
    registry.emplace<zl::ecs::Velocity>(replicatedProjectile,
                                       zl::ecs::Velocity{});
    registry.emplace<zl::ecs::SpriteRef>(
        replicatedProjectile,
        zl::ecs::SpriteRef{zl::ecs::HashName("projectile.bullet"), 1,
                           0xFFFFC040u});
    registry.emplace<zl::ecs::Projectile>(
        replicatedProjectile, zl::ecs::Projectile{shooterId});
    const std::uint64_t replicatedProjectileId =
        host.Simulation().Ecs().IdOf(replicatedProjectile).id;
    REQUIRE(PumpUntil(host, shooter, target, 4000, [&] {
        const auto* shooterActor =
            shooter.Replication().Find(replicatedProjectileId);
        const auto* targetActor =
            target.Replication().Find(replicatedProjectileId);
        return shooterActor != nullptr && targetActor != nullptr &&
               shooterActor->archetype ==
                   zl::game::kReplicationArchetypeProjectile &&
               targetActor->archetype ==
                   zl::game::kReplicationArchetypeProjectile;
    }));
    host.Simulation().DespawnEntity(replicatedProjectile);
    host.RunForTicks(2);
    shooter.Poll();
    target.Poll();

    host.RunForTicks(32);
    shooter.Poll();
    target.Poll();
    aimAtTarget();
    weapon->condition = 0.005f;
    const std::uint64_t brokenGunshots = host.GetNetworkStats().gunshots;
    const std::uint64_t brokenShots = host.GetNetworkStats().projectilesFired;
    REQUIRE(shooter.SendCommand(fire, &error));
    REQUIRE(PumpUntil(host, shooter, 5000, [&] {
        return shooter.HasPlayerState() &&
               shooter.PlayerState().equipment[0].loadedAmmo == 6 &&
               weapon->condition == doctest::Approx(0.0f);
    }));
    CHECK(registry.get<zl::ecs::Health>(targetEntity).current <= 64.0f);

    host.RunForTicks(32);
    shooter.Poll();
    target.Poll();
    const int loadedBefore = weapon->loadedAmmo;
    REQUIRE(shooter.SendCommand(fire, &error));
    REQUIRE(PumpUntil(host, shooter, 3000, [&] {
        return shooter.HasPlayerState() &&
               shooter.PlayerState().status.find("weapon broken") !=
                   std::string::npos;
    }));
    CHECK(weapon->loadedAmmo == loadedBefore);
    CHECK(host.GetNetworkStats().gunshots == brokenGunshots + 1);
    CHECK(host.GetNetworkStats().projectilesFired == brokenShots + 1);

    shooter.Close();
    REQUIRE(PumpUntil(host, target, 4000, [&] {
        return host.SessionCount() == 1 &&
               target.Replication().Find(shooterId) == nullptr;
    }));
    CHECK(std::filesystem::exists(profilePath));

    zl::game::NetworkClient reconnected(shooterOptions);
    REQUIRE(reconnected.Connect(&error));
    REQUIRE(PumpUntil(host, target, reconnected, 6000, [&] {
        return reconnected.IsConnected() && reconnected.HasPlayer() &&
               reconnected.HasPlayerState() &&
               reconnected.PlayerStableId() == shooterId;
    }));
    CHECK(reconnected.PlayerStableId() == shooterId);
    CHECK(reconnected.PlayerState().equipment[0].present);
    CHECK(reconnected.PlayerState().equipment[0].item.defId == "pistol");
    CHECK(reconnected.PlayerState().equipment[0].loadedAmmo == 6);
    CHECK(reconnected.PlayerState().equipment[0].item.condition ==
          doctest::Approx(0.0f));
    CHECK(reconnected.PlayerState().ranged.loaded == 6);
    CHECK(reconnected.PlayerState().ranged.reserve == 8);
    CHECK(BagCount(reconnected.PlayerState(), "ammo_9mm") == 8);
    CHECK(host.GetNetworkStats().profileRestores > 0);
    REQUIRE(PumpUntil(host, target, reconnected, 4000, [&] {
        return target.Replication().Find(shooterId) != nullptr;
    }));

    reconnected.Close();
    target.Close();
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(profileDir, cleanupError);
}

TEST_CASE("X-F1 2b-2 联机仓库：复制内容、服务器存取与所有权") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x53544F52414745ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;

    zl::game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.IsConnected() && client.HasPlayer() &&
               client.HasPlayerState();
    }));
    const std::uint64_t playerId = client.PlayerStableId();
    REQUIRE(playerId != 0);

    const zl::ecs::Entity player =
        host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));
    auto& registry = host.Simulation().Ecs().Registry();
    const auto& position = registry.get<zl::ecs::Position>(player);
    const zl::ecs::Entity storage = zl::game::SpawnPlayerStorage(
        host.Simulation().Ecs(), position.x + 1.0f, position.y, position.z,
        "object.crate", 50.0f, 6);
    REQUIRE(static_cast<bool>(storage != entt::null));
    registry.emplace<zl::ecs::StructureOwner>(
        storage, zl::ecs::StructureOwner{playerId});
    const std::uint64_t storageId =
        host.Simulation().Ecs().IdOf(storage).id;
    REQUIRE(storageId != 0);
    auto& serverBag = registry.get<zl::ecs::Container>(player);
    serverBag.items.push_back(zl::ecs::ItemInstance{"bandage", 2, 1.0f});
    auto& serverStorage = registry.get<zl::ecs::Container>(storage);
    registry.emplace_or_replace<zl::ecs::FoodState>(
        player, zl::ecs::FoodState{0.4f, 12.0f, 0.12f, 45.0f});
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        if (!client.HasPlayerState()) {
            return false;
        }
        const auto& food = client.PlayerState().food;
        return food.valid && food.nutrition > 0.0f &&
               food.poisonRemaining > 0.0f &&
               food.poisonRemaining <= 12.0f &&
               food.poisonDamagePerSecond > 0.0f;
    }));

    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto* actor = client.Replication().Find(storageId);
        return actor != nullptr && actor->hasStorage &&
               actor->storageContainer.capacityKg == doctest::Approx(50.0f);
    }));

    const auto storageCommand = [&](zl::ui::InventoryActionType type) {
        zl::net::Command command;
        command.type = zl::net::CommandType::InventoryAction;
        command.playerStableId = playerId;
        command.payload.flags = static_cast<std::uint16_t>(type) |
                                (1u << 8);
        command.payload.area =
            static_cast<std::uint8_t>(zl::ui::InventoryArea::Loot);
        command.payload.targetArea =
            static_cast<std::uint8_t>(zl::ui::InventoryArea::Backpack);
        command.payload.entityId = storageId;
        return command;
    };

    REQUIRE(client.SendCommand(
        storageCommand(zl::ui::InventoryActionType::StoreAll), &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return BagCount(client.PlayerState(), "bandage") == 0 &&
               serverStorage.items.size() == 1 &&
               zl::items::CountOf(serverStorage, "bandage") == 2;
    }));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto* actor = client.Replication().Find(storageId);
        return actor != nullptr && actor->hasStorage &&
               actor->storageContainer.items.size() == 1 &&
               actor->storageContainer.items[0].defId == "bandage";
    }));

    REQUIRE(client.SendCommand(
        storageCommand(zl::ui::InventoryActionType::TakeAll), &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return BagCount(client.PlayerState(), "bandage") == 2 &&
               serverStorage.items.empty();
    }));

    const zl::ecs::Entity farm = zl::game::SpawnFarmPlot(
        host.Simulation().Ecs(), position.x, position.y + 1.5f, position.z,
        zl::game::FarmConfig{});
    const zl::ecs::Entity generator = zl::game::SpawnGenerator(
        host.Simulation().Ecs(), position.x + 1.0f, position.y + 1.0f,
        position.z, zl::game::PowerConfig{});
    const zl::ecs::Entity trap = zl::game::SpawnSpikeTrap(
        host.Simulation().Ecs(), position.x - 1.0f, position.y + 1.0f,
        position.z, zl::game::TrapConfig{});
    const zl::ecs::Entity floodlight = zl::game::SpawnFloodlight(
        host.Simulation().Ecs(), position.x - 1.0f, position.y - 1.0f,
        position.z, 80.0f);
    REQUIRE(static_cast<bool>(farm != entt::null));
    REQUIRE(static_cast<bool>(generator != entt::null));
    REQUIRE(static_cast<bool>(trap != entt::null));
    REQUIRE(static_cast<bool>(floodlight != entt::null));
    registry.emplace<zl::ecs::StructureOwner>(
        farm, zl::ecs::StructureOwner{playerId});
    registry.emplace<zl::ecs::StructureOwner>(
        generator, zl::ecs::StructureOwner{playerId});
    registry.emplace<zl::ecs::StructureOwner>(
        trap, zl::ecs::StructureOwner{playerId});
    registry.emplace<zl::ecs::StructureOwner>(
        floodlight, zl::ecs::StructureOwner{playerId});
    const std::uint64_t farmId = host.Simulation().Ecs().IdOf(farm).id;
    const std::uint64_t generatorId =
        host.Simulation().Ecs().IdOf(generator).id;
    const std::uint64_t trapId = host.Simulation().Ecs().IdOf(trap).id;
    const std::uint64_t floodlightId =
        host.Simulation().Ecs().IdOf(floodlight).id;
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto* farmActor = client.Replication().Find(farmId);
        const auto* generatorActor = client.Replication().Find(generatorId);
        const auto* trapActor = client.Replication().Find(trapId);
        return farmActor != nullptr && farmActor->hasFarmPlot &&
               generatorActor != nullptr && generatorActor->hasGenerator &&
               trapActor != nullptr && trapActor->hasTrap;
    }));

    const auto facilityCommand = [&](std::uint64_t entityId) {
        zl::net::Command command;
        command.type = zl::net::CommandType::InteractFacility;
        command.playerStableId = playerId;
        command.payload.entityId = entityId;
        return command;
    };

    serverBag.items.push_back(zl::ecs::ItemInstance{"cabbage_seeds", 1, 1.0f});
    REQUIRE(client.SendCommand(facilityCommand(farmId), &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return registry.get<zl::ecs::FarmPlot>(farm).stage ==
                   zl::ecs::CropStage::Growing &&
               zl::items::CountOf(serverBag, "cabbage_seeds") == 0;
    }));
    serverBag.items.push_back(zl::ecs::ItemInstance{"water_bottle", 1, 1.0f});
    REQUIRE(client.SendCommand(facilityCommand(farmId), &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return registry.get<zl::ecs::FarmPlot>(farm).waterMinutes > 0.0f &&
               zl::items::CountOf(serverBag, "water_bottle") == 0;
    }));
    auto& maturePlot = registry.get<zl::ecs::FarmPlot>(farm);
    maturePlot.stage = zl::ecs::CropStage::Mature;
    maturePlot.harvestYield = 2;
    REQUIRE(client.SendCommand(facilityCommand(farmId), &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return registry.get<zl::ecs::FarmPlot>(farm).stage ==
                   zl::ecs::CropStage::Empty &&
               zl::items::CountOf(serverBag, "fresh_cabbage") > 0;
    }));

    const zl::ecs::Entity farm2 = zl::game::SpawnFarmPlot(
        host.Simulation().Ecs(), position.x + 1.5f, position.y + 0.5f,
        position.z, zl::game::FarmConfig{});
    REQUIRE(static_cast<bool>(farm2 != entt::null));
    registry.emplace_or_replace<zl::ecs::StructureOwner>(
        farm2, zl::ecs::StructureOwner{playerId});
    const std::uint64_t farm2Id = host.Simulation().Ecs().IdOf(farm2).id;
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto* actor = client.Replication().Find(farm2Id);
        return actor != nullptr && actor->hasFarmPlot;
    }));
    const auto farmActionCommand = [&](std::uint16_t flags,
                                       std::int32_t index) {
        zl::net::Command command = facilityCommand(farm2Id);
        command.payload.flags = flags;
        command.payload.index = index;
        return command;
    };
    serverBag.items.push_back(
        zl::ecs::ItemInstance{"potato_seeds", 1, 1.0f});
    REQUIRE(client.SendCommand(
        farmActionCommand(zl::game::kFarmActionPlant, 1), &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return registry.get<zl::ecs::FarmPlot>(farm2).cropId == "potato" &&
               registry.get<zl::ecs::FarmPlot>(farm2).stage ==
                   zl::ecs::CropStage::Growing &&
               zl::items::CountOf(serverBag, "potato_seeds") == 0;
    }));
    serverBag.items.push_back(
        zl::ecs::ItemInstance{"fertilizer", 1, 1.0f});
    REQUIRE(client.SendCommand(
        farmActionCommand(zl::game::kFarmActionCare, -1), &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto& plot = registry.get<zl::ecs::FarmPlot>(farm2);
        return plot.fertilized && plot.harvestYield == 4 &&
               zl::items::CountOf(serverBag, "fertilizer") == 0;
    }));
    registry.get<zl::ecs::FarmPlot>(farm2).diseased = true;
    registry.get<zl::ecs::FarmPlot>(farm2).harvestYield = 2;
    serverBag.items.push_back(
        zl::ecs::ItemInstance{"fungicide", 1, 1.0f});
    REQUIRE(client.SendCommand(
        farmActionCommand(zl::game::kFarmActionCare, -1), &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto& plot = registry.get<zl::ecs::FarmPlot>(farm2);
        return !plot.diseased &&
               zl::items::CountOf(serverBag, "fungicide") == 0;
    }));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto* actor = client.Replication().Find(farm2Id);
        return actor != nullptr && actor->hasFarmPlot &&
               actor->farmPlot.fertilized && !actor->farmPlot.diseased;
    }));

    auto& growthPlot = registry.get<zl::ecs::FarmPlot>(farm2);
    growthPlot.growthMinutes = std::max(0.0f, growthPlot.growMinutes - 0.02f);
    growthPlot.waterMinutes = 5.0f;
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto* actor = client.Replication().Find(farm2Id);
        return growthPlot.stage == zl::ecs::CropStage::Mature &&
               actor != nullptr && actor->hasFarmPlot &&
               actor->farmPlot.stage == zl::ecs::CropStage::Mature;
    }));

    serverBag.items.push_back(zl::ecs::ItemInstance{"fuel_can", 1, 1.0f});
    REQUIRE(client.SendCommand(facilityCommand(generatorId), &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return registry.get<zl::ecs::Generator>(generator).running &&
               zl::items::CountOf(serverBag, "fuel_can") == 0;
    }));

    REQUIRE(client.SendCommand(facilityCommand(trapId), &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return !registry.get<zl::ecs::SpikeTrap>(trap).armed;
    }));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto* farmActor = client.Replication().Find(farmId);
        const auto* generatorActor = client.Replication().Find(generatorId);
        const auto* trapActor = client.Replication().Find(trapId);
        return farmActor != nullptr && farmActor->hasFarmPlot &&
               farmActor->farmPlot.stage == zl::ecs::CropStage::Empty &&
               generatorActor != nullptr && generatorActor->hasGenerator &&
               generatorActor->generator.running && trapActor != nullptr &&
               trapActor->hasTrap && !trapActor->trap.armed;
    }));

    zl::game::NetworkClient intruder(clientOptions);
    REQUIRE(intruder.Connect(&error));
    REQUIRE(PumpUntil(host, client, intruder, 6000, [&] {
        return intruder.IsConnected() && intruder.HasPlayer();
    }));
    const std::uint64_t intruderId = intruder.PlayerStableId();
    REQUIRE(intruderId != 0);
    REQUIRE(intruderId != playerId);
    const zl::ecs::Entity intruderPlayer =
        host.Simulation().Ecs().FindById(intruderId);
    REQUIRE(static_cast<bool>(intruderPlayer != entt::null));
    auto* intruderPosition =
        registry.try_get<zl::ecs::Position>(intruderPlayer);
    REQUIRE(intruderPosition != nullptr);
    const auto* farmPosition =
        registry.try_get<zl::ecs::Position>(farm);
    REQUIRE(farmPosition != nullptr);
    intruderPosition->x = farmPosition->x;
    intruderPosition->y = farmPosition->y;
    if (auto* rigid = registry.try_get<zl::ecs::RigidBody>(intruderPlayer);
        rigid != nullptr) {
        host.Simulation().Physics().TeleportBody(
            rigid->body, intruderPosition->x, intruderPosition->y, 0);
    }
    const auto intruderFacility = [&](std::uint64_t entityId) {
        zl::net::Command command = facilityCommand(entityId);
        command.playerStableId = intruderId;
        return command;
    };
    const auto expectFacilityRejected = [&](std::uint64_t entityId) {
        const std::uint64_t rejectedBefore =
            host.GetNetworkStats().ownershipRejected;
        REQUIRE(intruder.SendCommand(intruderFacility(entityId), &error));
        REQUIRE(PumpUntil(host, client, intruder, 3000, [&] {
            return host.GetNetworkStats().ownershipRejected > rejectedBefore;
        }));
    };
    const zl::ecs::CropStage farmStage =
        registry.get<zl::ecs::FarmPlot>(farm).stage;
    const bool generatorRunning =
        registry.get<zl::ecs::Generator>(generator).running;
    const bool trapArmed = registry.get<zl::ecs::SpikeTrap>(trap).armed;
    const bool floodlightOn =
        registry.get<zl::ecs::Floodlight>(floodlight).on;
    expectFacilityRejected(farmId);
    expectFacilityRejected(generatorId);
    expectFacilityRejected(trapId);
    const auto* floodlightPosition =
        registry.try_get<zl::ecs::Position>(floodlight);
    REQUIRE(floodlightPosition != nullptr);
    intruderPosition->x = floodlightPosition->x;
    intruderPosition->y = floodlightPosition->y;
    if (auto* rigid = registry.try_get<zl::ecs::RigidBody>(intruderPlayer);
        rigid != nullptr) {
        host.Simulation().Physics().TeleportBody(
            rigid->body, intruderPosition->x, intruderPosition->y, 0);
    }
    expectFacilityRejected(floodlightId);
    CHECK(registry.get<zl::ecs::FarmPlot>(farm).stage == farmStage);
    CHECK(registry.get<zl::ecs::Generator>(generator).running ==
          generatorRunning);
    CHECK(registry.get<zl::ecs::SpikeTrap>(trap).armed == trapArmed);
    CHECK(registry.get<zl::ecs::Floodlight>(floodlight).on ==
          floodlightOn);
    intruder.Close();

    client.Close();
    host.Shutdown();
}

TEST_CASE("X-F1 2b-3 联机陷阱：服务器权威伤害、冷却与复制") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x54524150314233ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;
    zl::game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.IsConnected() && client.HasPlayer() &&
               client.HasPlayerState();
    }));

    const std::uint64_t playerId = client.PlayerStableId();
    REQUIRE(playerId != 0);
    const zl::ecs::Entity player =
        host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));
    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Position playerPosition =
        registry.get<zl::ecs::Position>(player);

    const zl::ecs::Entity trap = zl::game::SpawnSpikeTrap(
        host.Simulation().Ecs(), playerPosition.x + 3.0f,
        playerPosition.y, playerPosition.z, zl::game::TrapConfig{});
    REQUIRE(static_cast<bool>(trap != entt::null));
    const zl::ecs::Position trapPosition =
        registry.get<zl::ecs::Position>(trap);
    auto& trapState = registry.get<zl::ecs::SpikeTrap>(trap);
    trapState.armed = false;
    trapState.uses = 3;
    trapState.damage = 45.0f;
    trapState.radius = 2.0f;
    trapState.cooldown = 0.0f;
    registry.emplace<zl::ecs::StructureOwner>(
        trap, zl::ecs::StructureOwner{playerId});
    const std::uint64_t trapId = host.Simulation().Ecs().IdOf(trap).id;
    REQUIRE(trapId != 0);

    const zl::ecs::Entity target = host.Simulation().Ecs().Create();
    REQUIRE(static_cast<bool>(target != entt::null));
    registry.emplace<zl::ecs::Position>(
        target, zl::ecs::Position{trapPosition.x, trapPosition.y,
                                  trapPosition.z});
    registry.emplace<zl::ecs::Health>(
        target, zl::ecs::Health{500.0f, 500.0f});
    registry.emplace<zl::ecs::SpriteRef>(
        target, zl::ecs::SpriteRef{zl::ecs::HashName("character.placeholder"),
                                   3, 0xFFFFFFFFu});
    zl::ecs::AiState ai;
    ai.idleSeconds = 1000.0f;
    registry.emplace<zl::ecs::AiState>(target, ai);
    registry.emplace<zl::ecs::BaseSiegeZombie>(target);
    const std::uint64_t targetId = host.Simulation().Ecs().IdOf(target).id;
    REQUIRE(targetId != 0);

    const zl::ecs::Entity ambient = host.Simulation().Ecs().Create();
    REQUIRE(static_cast<bool>(ambient != entt::null));
    registry.emplace<zl::ecs::Position>(
        ambient, zl::ecs::Position{trapPosition.x, trapPosition.y,
                                   trapPosition.z});
    registry.emplace<zl::ecs::Health>(
        ambient, zl::ecs::Health{500.0f, 500.0f});
    registry.emplace<zl::ecs::SpriteRef>(
        ambient, zl::ecs::SpriteRef{zl::ecs::HashName("character.placeholder"),
                                    3, 0xFFFFFFFFu});
    registry.emplace<zl::ecs::AiState>(ambient, ai);

    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto* trapActor = client.Replication().Find(trapId);
        const auto* targetActor = client.Replication().Find(targetId);
        return trapActor != nullptr && trapActor->hasTrap &&
               trapActor->trap.uses == 3 && !trapActor->trap.armed &&
               targetActor != nullptr && targetActor->hasHealth &&
               targetActor->health.current == doctest::Approx(500.0f);
    }));

    trapState.armed = true;
    host.Tick(1.0f / 60.0f);
    CHECK(registry.get<zl::ecs::SpikeTrap>(trap).uses == 2);
    CHECK(registry.get<zl::ecs::Health>(target).current ==
          doctest::Approx(455.0f));
    CHECK(registry.get<zl::ecs::Health>(ambient).current ==
          doctest::Approx(500.0f));
    CHECK(PollClientUntil(client, 3000, [&] {
        const auto* trapActor = client.Replication().Find(trapId);
        const auto* targetActor = client.Replication().Find(targetId);
        return trapActor != nullptr && trapActor->hasTrap &&
               trapActor->trap.uses == 2 && targetActor != nullptr &&
               targetActor->hasHealth && targetActor->health.current < 500.0f;
    }));

    const float healthAfterFirst =
        registry.get<zl::ecs::Health>(target).current;
    host.RunForTicks(10);
    CHECK(registry.get<zl::ecs::SpikeTrap>(trap).uses == 2);
    CHECK(registry.get<zl::ecs::Health>(target).current ==
          doctest::Approx(healthAfterFirst));
    CHECK(PollClientUntil(client, 3000, [&] {
        return client.PlayerState().status.find("陷阱") != std::string::npos;
    }));

    registry.get<zl::ecs::SpikeTrap>(trap).cooldown = 0.0f;
    host.Tick(1.0f / 60.0f);
    CHECK(registry.get<zl::ecs::SpikeTrap>(trap).uses == 1);
    CHECK(registry.get<zl::ecs::Health>(target).current ==
          doctest::Approx(410.0f));

    registry.get<zl::ecs::SpikeTrap>(trap).cooldown = 0.0f;
    host.Tick(1.0f / 60.0f);
    const auto& exhaustedTrap = registry.get<zl::ecs::SpikeTrap>(trap);
    CHECK(exhaustedTrap.uses == 0);
    CHECK_FALSE(exhaustedTrap.armed);
    CHECK(registry.get<zl::ecs::Health>(target).current ==
          doctest::Approx(365.0f));
    CHECK(registry.get<zl::ecs::Health>(ambient).current ==
          doctest::Approx(500.0f));

    host.RunForTicks(20);
    CHECK(registry.get<zl::ecs::Health>(target).current ==
          doctest::Approx(365.0f));
    CHECK(PollClientUntil(client, 3000, [&] {
        const auto* trapActor = client.Replication().Find(trapId);
        const auto* targetActor = client.Replication().Find(targetId);
        return trapActor != nullptr && trapActor->hasTrap &&
               trapActor->trap.uses == 0 && !trapActor->trap.armed &&
               targetActor != nullptr && targetActor->hasHealth &&
               targetActor->health.current < 410.0f;
    }));

    client.Close();
    host.Shutdown();
}

TEST_CASE("X-F1 2b-6 服务器守夜：事件僵尸与陷阱收窄") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const char* args[] = {
        "ZomboidLikeTests",
        "--game.startHour=22",
        "--baseEvent.warningSeconds=0",
        "--baseEvent.waves=[1]",
        "--baseEvent.maxAlive=1",
        "--baseEvent.specialCount=0",
        "--baseEvent.spawnRadiusMin=4",
        "--baseEvent.spawnRadiusMax=6",
    };
    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.argc = 8;
    hostOptions.argv = args;
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x53494547453236ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);
    CHECK(host.Config().Get<double>("baseEvent.warningSeconds", -1.0) ==
          doctest::Approx(0.0));

    zl::world::GridSquare* wallSquare = nullptr;
    for (int attempt = 0; attempt < 600; ++attempt) {
        host.RunForTicks(1);
        wallSquare = host.Simulation().World().GetSquare(72, 24, 0);
        if (wallSquare != nullptr &&
            !host.Simulation().Chunks().HasPendingWork()) {
            break;
        }
    }
    REQUIRE(wallSquare != nullptr);
    REQUIRE_FALSE(host.Simulation().Chunks().HasPendingWork());
    zl::world::SquareObject wall{"object.wall_h", 0, 0, true};
    wall.hp = 200.0f;
    wall.playerBuilt = true;
    wallSquare->AddObject(zl::world::SquareLayer::Wall, wall);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;
    zl::game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.IsConnected() && client.HasPlayer() &&
               client.HasPlayerState();
    }));

    const std::uint64_t playerId = client.PlayerStableId();
    REQUIRE(playerId != 0);
    auto& registry = host.Simulation().Ecs().Registry();

    const auto eventCount = [&] {
        int count = 0;
        registry.view<zl::ecs::BaseSiegeZombie, zl::ecs::Position>().each(
            [&](const zl::ecs::Entity, const zl::ecs::Position&) { ++count; });
        return count;
    };
    REQUIRE(PumpUntil(host, client, 8000, [&] {
        return eventCount() > 0;
    }));

    zl::ecs::Entity eventZombie = entt::null;
    zl::ecs::Position eventPosition;
    registry.view<zl::ecs::BaseSiegeZombie, zl::ecs::Position>().each(
        [&](const zl::ecs::Entity entity, const zl::ecs::Position& position) {
            if (eventZombie == entt::null) {
                eventZombie = entity;
                eventPosition = position;
            }
        });
    REQUIRE(static_cast<bool>(eventZombie != entt::null));
    const float eventHealth =
        registry.get<zl::ecs::Health>(eventZombie).current;

    const zl::ecs::Entity trap = zl::game::SpawnSpikeTrap(
        host.Simulation().Ecs(), eventPosition.x, eventPosition.y,
        eventPosition.z, zl::game::TrapConfig{});
    REQUIRE(static_cast<bool>(trap != entt::null));
    auto& trapState = registry.get<zl::ecs::SpikeTrap>(trap);
    trapState.armed = true;
    trapState.uses = 1;
    trapState.damage = 45.0f;
    trapState.radius = 3.0f;
    trapState.cooldown = 0.0f;
    registry.emplace<zl::ecs::StructureOwner>(
        trap, zl::ecs::StructureOwner{playerId});

    const zl::ecs::Entity ambient = host.Simulation().Ecs().Create();
    REQUIRE(static_cast<bool>(ambient != entt::null));
    registry.emplace<zl::ecs::Position>(ambient, eventPosition);
    registry.emplace<zl::ecs::Health>(
        ambient, zl::ecs::Health{500.0f, 500.0f});
    registry.emplace<zl::ecs::SpriteRef>(
        ambient, zl::ecs::SpriteRef{zl::ecs::HashName("character.placeholder"),
                                    3, 0xFFFFFFFFu});
    registry.emplace<zl::ecs::AiState>(ambient, zl::ecs::AiState{});

    host.Tick(1.0f / 60.0f);
    CHECK(registry.get<zl::ecs::SpikeTrap>(trap).uses == 0);
    CHECK(registry.get<zl::ecs::Health>(eventZombie).current < eventHealth);
    CHECK(registry.get<zl::ecs::Health>(ambient).current ==
          doctest::Approx(500.0f));

    client.Close();
    host.Shutdown();
}

TEST_CASE("X-F1 2b-4 联机商人：服务器权威交易、库存与图纸复制") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x54524144453234ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;
    zl::game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.IsConnected() && client.HasPlayer() &&
               client.HasPlayerState();
    }));

    const std::uint64_t playerId = client.PlayerStableId();
    REQUIRE(playerId != 0);
    const zl::ecs::Entity player =
        host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));
    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Position playerPosition =
        registry.get<zl::ecs::Position>(player);
    auto& serverBag = registry.get<zl::ecs::Container>(player);

    bool autoMerchantFound = false;
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        registry.view<zl::ecs::MerchantStock>().each(
            [&](const zl::ecs::Entity, const zl::ecs::MerchantStock&) {
                autoMerchantFound = true;
            });
        return autoMerchantFound;
    }));

    const zl::ecs::Entity merchant = zl::game::SpawnMerchant(
        host.Simulation().Ecs(), playerPosition.x - 1.0f, playerPosition.y,
        playerPosition.z);
    REQUIRE(static_cast<bool>(merchant != entt::null));
    auto& stock = registry.get<zl::ecs::MerchantStock>(merchant);
    stock.stock[0] = 2;
    stock.restockDay = 1000;
    const std::uint64_t merchantId =
        host.Simulation().Ecs().IdOf(merchant).id;
    REQUIRE(merchantId != 0);

    serverBag.items.push_back(zl::ecs::ItemInstance{"wood", 2, 1.0f});
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto* actor = client.Replication().Find(merchantId);
        return actor != nullptr && actor->hasMerchantStock &&
               actor->merchantStock.stock[0] == 2;
    }));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return BagCount(client.PlayerState(), "wood") == 2;
    }));

    const auto tradeCommand = [&](int index) {
        zl::net::Command command;
        command.type = zl::net::CommandType::TradeOffer;
        command.playerStableId = playerId;
        command.payload.entityId = merchantId;
        command.payload.index = index;
        return command;
    };

    REQUIRE(client.SendCommand(tradeCommand(0), &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return stock.stock[0] == 1 &&
               zl::items::CountOf(serverBag, "wood") == 0 &&
               zl::items::CountOf(serverBag, "bandage") == 2;
    }));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto* actor = client.Replication().Find(merchantId);
        return actor != nullptr && actor->hasMerchantStock &&
               actor->merchantStock.stock[0] == 1 &&
               BagCount(client.PlayerState(), "bandage") == 2;
    }));

    REQUIRE(client.SendCommand(tradeCommand(0), &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return client.PlayerState().status.find("物资不足") != std::string::npos;
    }));
    CHECK(stock.stock[0] == 1);
    CHECK(zl::items::CountOf(serverBag, "wood") == 0);

    serverBag.items.push_back(zl::ecs::ItemInstance{"wood", 3, 1.0f});
    REQUIRE(client.SendCommand(tradeCommand(2), &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto* known = registry.try_get<zl::ecs::KnownRecipes>(player);
        return stock.stock[2] == 0 &&
               zl::items::CountOf(serverBag, "wood") == 0 &&
               known != nullptr &&
               std::find(known->recipeIds.begin(), known->recipeIds.end(),
                         "campfire_beans") != known->recipeIds.end();
    }));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto& known = client.PlayerState().knownRecipes;
        return std::find(known.begin(), known.end(), "campfire_beans") !=
               known.end();
    }));

    serverBag.items.push_back(zl::ecs::ItemInstance{"wood", 10, 1.0f});
    REQUIRE(client.SendCommand(tradeCommand(4), &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return stock.stock[4] == 0 &&
               zl::items::CountOf(serverBag, "wood") == 2 &&
               zl::items::CountOf(serverBag, "pistol") == 1;
    }));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return BagCount(client.PlayerState(), "pistol") == 1;
    }));
    REQUIRE(client.SendCommand(tradeCommand(3), &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return stock.stock[3] == 1 &&
               zl::items::CountOf(serverBag, "wood") == 0 &&
               zl::items::CountOf(serverBag, "ammo_9mm") == 12;
    }));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return BagCount(client.PlayerState(), "ammo_9mm") == 12;
    }));

    int pistolIndex = -1;
    for (std::size_t i = 0; i < client.PlayerState().bag.size(); ++i) {
        if (client.PlayerState().bag[i].defId == "pistol") {
            pistolIndex = static_cast<int>(i);
            break;
        }
    }
    REQUIRE(pistolIndex >= 0);
    zl::net::Command equipPistol;
    equipPistol.type = zl::net::CommandType::EquipBag;
    equipPistol.playerStableId = playerId;
    equipPistol.payload.index = pistolIndex;
    equipPistol.payload.targetIndex = 0;
    REQUIRE(client.SendCommand(equipPistol, &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto& equipped = client.PlayerState().equipment[0];
        return equipped.present && equipped.item.defId == "pistol";
    }));

    zl::net::Command reloadPistol;
    reloadPistol.type = zl::net::CommandType::ReloadRanged;
    reloadPistol.playerStableId = playerId;
    REQUIRE(client.SendCommand(reloadPistol, &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto& equipped = client.PlayerState().equipment[0];
        return equipped.present && equipped.item.defId == "pistol" &&
               equipped.loadedAmmo == 8 &&
               BagCount(client.PlayerState(), "ammo_9mm") == 4;
    }));

    client.Close();
    host.Shutdown();
}

TEST_CASE("M19 联机 POI：服务器容器/守军、搜刮同步与任务完成") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.playerTargetX = 33.5f;
    hostOptions.playerTargetY = 20.5f;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x504F4931393031ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;
    zl::game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.IsConnected() && client.HasPlayer() && client.HasPlayerState();
    }));
    const std::uint64_t playerId = client.PlayerStableId();

    entt::registry& registry = host.Simulation().Ecs().Registry();
    zl::ecs::Entity container = entt::null;
    REQUIRE(PumpUntil(host, client, 8000, [&] {
        registry.view<zl::ecs::LootContainer>().each(
            [&](const zl::ecs::Entity entity, const zl::ecs::LootContainer& loot) {
                if (container == entt::null && loot.tableId == "poi_gas_station") {
                    container = entity;
                }
            });
        return container != entt::null;
    }));
    const std::uint64_t containerId =
        host.Simulation().Ecs().IdOf(container).id;
    REQUIRE(containerId != 0);
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        const auto* actor = client.Replication().Find(containerId);
        return actor != nullptr && actor->hasLoot &&
               actor->lootContainer.tableId == "poi_gas_station";
    }));

    zl::net::Command search;
    search.type = zl::net::CommandType::SearchLootContainer;
    search.playerStableId = playerId;
    search.payload.entityId = containerId;
    REQUIRE(client.SendCommand(search, &error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        for (const auto& poi : client.PlayerState().poiProgress) {
            if (poi.id == "gas_station" && poi.targetSearched) {
                return true;
            }
        }
        return false;
    }));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        const auto* actor = client.Replication().Find(containerId);
        return actor != nullptr && actor->hasLoot &&
               actor->lootContainer.searched &&
               !actor->lootContainerItems.items.empty();
    }));

    const auto bagCount = [&] {
        int total = 0;
        for (const auto& item : client.PlayerState().bag) {
            total += item.count;
        }
        return total;
    };
    const int bagBefore = bagCount();
    zl::net::Command take;
    take.type = zl::net::CommandType::InventoryAction;
    take.playerStableId = playerId;
    take.payload.area = static_cast<std::uint8_t>(zl::ui::InventoryArea::Loot);
    take.payload.targetArea =
        static_cast<std::uint8_t>(zl::ui::InventoryArea::Backpack);
    take.payload.index = 0;
    take.payload.targetIndex = 0;
    take.payload.entityId = containerId;
    take.payload.flags = static_cast<std::uint16_t>(
        zl::ui::InventoryActionType::Transfer);
    REQUIRE(client.SendCommand(take, &error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return bagCount() > bagBefore;
    }));

    const zl::ecs::Entity player =
        host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));
    auto& serverBag = registry.get<zl::ecs::Container>(player);
    for (int stack = 0; stack < 3; ++stack) {
        serverBag.items.push_back(zl::ecs::ItemInstance{"wood", 20, 1.0f});
    }
    const int repairKitsBefore = BagCount(client.PlayerState(), "repair_kit");

    int guards = 0;
    registry.view<zl::ecs::PoiGuard, zl::ecs::Health>().each(
        [&](const zl::ecs::Entity entity, const zl::ecs::PoiGuard& guard,
            const zl::ecs::Health& health) {
            if (guard.poiId == "gas_station" && health.current > 0.0f &&
                host.Simulation().Ecs().Alive(entity)) {
                ++guards;
                zl::ecs::DamageEvent event;
                event.amount = 1000.0f;
                event.type = zl::ecs::DamageType::Blunt;
                (void)host.Simulation().Combat()->ApplyDamage(
                    host.Simulation().Ecs(), entity, event);
            }
        });
    REQUIRE(guards > 0);
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        for (const auto& poi : client.PlayerState().poiProgress) {
            if (poi.id == "gas_station" && poi.completed) {
                return true;
            }
        }
        return false;
    }));
    CHECK_MESSAGE(client.PlayerState().status.find("背包已满") != std::string::npos,
                  client.PlayerState().status);
    CHECK(BagCount(client.PlayerState(), "repair_kit") == repairKitsBefore);

    client.Close();
    host.Shutdown();
}

TEST_CASE("M19 联机任务：房屋搜刮推进任务链并同步到客户端") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.playerTargetX = 73.0f;
    hostOptions.playerTargetY = 22.0f;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D49535331393031ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;
    zl::game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.IsConnected() && client.HasPlayer() && client.HasPlayerState();
    }));
    const std::uint64_t playerId = client.PlayerStableId();
    entt::registry& registry = host.Simulation().Ecs().Registry();
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.PlayerState().objective.valid &&
               client.PlayerState().objective.step == 0;
    }));

    zl::ecs::Entity container = entt::null;
    REQUIRE(PumpUntil(host, client, 8000, [&] {
        registry.view<zl::ecs::LootContainer>().each(
            [&](const zl::ecs::Entity entity, const zl::ecs::LootContainer& loot) {
                if (container == entt::null && loot.tableId == "house_workshop") {
                    container = entity;
                }
            });
        return container != entt::null;
    }));
    const std::uint64_t containerId =
        host.Simulation().Ecs().IdOf(container).id;

    zl::net::Command search;
    search.type = zl::net::CommandType::SearchLootContainer;
    search.playerStableId = playerId;
    search.payload.entityId = containerId;
    REQUIRE(client.SendCommand(search, &error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        for (const auto& mission : client.PlayerState().missions) {
            if (mission.id == "radio_signal" && mission.status == 1) {
                return true;
            }
        }
        return false;
    }));

    const zl::ecs::Entity player = host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));
    auto& bag = registry.get<zl::ecs::Container>(player);
    bag.items.push_back(zl::ecs::ItemInstance{"radio_part", 1, 1.0f});
    for (int stack = 0; stack < 3; ++stack) {
        bag.items.push_back(zl::ecs::ItemInstance{"wood", 20, 1.0f});
    }
    CHECK(zl::items::CountOf(bag, "repair_kit") == 0);
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.PlayerState().objective.valid &&
               client.PlayerState().objective.step == 1;
    }));

    zl::net::Command radio;
    radio.type = zl::net::CommandType::InteractRadio;
    radio.playerStableId = playerId;
    REQUIRE(client.SendCommand(radio, &error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        for (const auto& mission : client.PlayerState().missions) {
            if (mission.id == "radio_repair" && mission.status == 1) {
                return true;
            }
        }
        return false;
    }));

    CHECK_MESSAGE(client.PlayerState().status.find("背包已满") != std::string::npos,
                  client.PlayerState().status);
    CHECK(zl::items::CountOf(bag, "repair_kit") == 0);
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.PlayerState().objective.valid &&
               client.PlayerState().objective.step == 2;
    }));

    auto* playerPosition = registry.try_get<zl::ecs::Position>(player);
    REQUIRE(playerPosition != nullptr);
    if (auto* rigid = registry.try_get<zl::ecs::RigidBody>(player);
        rigid != nullptr) {
        host.Simulation().Physics().TeleportBody(rigid->body, 66.5f, 28.5f, 0);
    }
    playerPosition->x = 66.5f;
    playerPosition->y = 28.5f;
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.PlayerState().objective.valid &&
               client.PlayerState().objective.step == 3;
    }));
    const zl::ecs::Entity zombie = host.Simulation().SpawnZombieAt(
        playerPosition->x + 0.5f, playerPosition->y);
    REQUIRE(static_cast<bool>(zombie != entt::null));
    auto* zombieHealth = registry.try_get<zl::ecs::Health>(zombie);
    REQUIRE(zombieHealth != nullptr);
    zombieHealth->current = 1.0f;

    zl::net::Command attack;
    attack.type = zl::net::CommandType::Attack;
    attack.playerStableId = playerId;
    attack.payload.entityId = host.Simulation().Ecs().IdOf(zombie).id;
    REQUIRE(client.SendCommand(attack, &error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        for (const auto& mission : client.PlayerState().missions) {
            if (mission.id == "daily_patrol" && !mission.progress.empty() &&
                mission.progress[0] >= 1) {
                return true;
            }
        }
        return false;
    }));

    client.Close();
    host.Shutdown();
}


TEST_CASE("M24-T7 双客户端新 POI/任务/事件权威与重连不重复") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path profileDir = TempProfileDir("m24_t7_dual");
    const std::filesystem::path profilePath = profileDir / "players.bin";

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.playerTargetX = 10.5f;
    hostOptions.playerTargetY = 37.5f;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D323454374D5431ull;
    hostOptions.enablePlayerProfiles = true;
    hostOptions.playerProfilesFile = profilePath.string();

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options firstOptions;
    firstOptions.host = "127.0.0.1";
    firstOptions.port = host.NetworkPort();
    firstOptions.contentHash = hostOptions.contentHash;
    firstOptions.autoReconnect = false;
    firstOptions.playerIdentity = "m24-t7-a";
    firstOptions.playerName = "Alpha";

    zl::game::NetworkClient::Options secondOptions = firstOptions;
    secondOptions.playerIdentity = "m24-t7-b";
    secondOptions.playerName = "Beta";

    zl::game::NetworkClient first(firstOptions);
    zl::game::NetworkClient second(secondOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayerState() && second.HasPlayerState() &&
               first.PlayerStableId() != 0 &&
               second.PlayerStableId() != 0;
    }));
    const std::uint64_t firstId = first.PlayerStableId();
    const std::uint64_t secondId = second.PlayerStableId();
    REQUIRE(firstId != 0);
    REQUIRE(secondId != 0);
    REQUIRE(firstId != secondId);

    auto& registry = host.Simulation().Ecs().Registry();
    const auto serverPlayer = [&](std::uint64_t id) {
        return host.Simulation().Ecs().FindById(id);
    };
    for (const std::uint64_t id : {firstId, secondId}) {
        const zl::ecs::Entity player = serverPlayer(id);
        REQUIRE(static_cast<bool>(player != entt::null));
        auto* health = registry.try_get<zl::ecs::Health>(player);
        REQUIRE(health != nullptr);
        health->current = 10000.0f;
        health->max = 10000.0f;
    }
    REQUIRE(TeleportServerPlayer(host, firstId, 10.5f, 37.5f));
    REQUIRE(TeleportServerPlayer(host, secondId, 10.5f, 37.5f));

    zl::ecs::Entity motelContainer = entt::null;
    REQUIRE(PumpUntil(host, first, second, 10000, [&] {
        motelContainer =
            FindLootContainerByTable(registry, "poi_motel_storage");
        return motelContainer != entt::null;
    }));
    const std::uint64_t motelContainerId =
        host.Simulation().Ecs().IdOf(motelContainer).id;
    REQUIRE(motelContainerId != 0);
    REQUIRE(CountLootContainers(registry, "poi_motel_storage") == 1);
    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        return CountLivePoiGuards(registry, "motel") > 0;
    }));

    registry.view<zl::ecs::PoiGuard, zl::ecs::Health>().each(
        [&](const entt::entity entity, const zl::ecs::PoiGuard& guard,
            zl::ecs::Health& health) {
            if (guard.poiId == "motel" && health.current > 0.0f) {
                zl::ecs::DamageEvent event;
                event.amount = 1000.0f;
                event.type = zl::ecs::DamageType::Blunt;
                (void)host.Simulation().Combat()->ApplyDamage(
                    host.Simulation().Ecs(), entity, event);
            }
        });
    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        return CountLivePoiGuards(registry, "motel") == 0;
    }));

    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        const auto* a = first.Replication().Find(motelContainerId);
        const auto* b = second.Replication().Find(motelContainerId);
        return a != nullptr && b != nullptr && a->hasLoot && b->hasLoot;
    }));

    const int firstFruitBefore =
        BagCount(first.PlayerState(), "canned_fruit");
    const int secondFruitBefore =
        BagCount(second.PlayerState(), "canned_fruit");
    REQUIRE(first.SendCommand(SearchLootCommand(firstId, motelContainerId),
                              &error));
    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        const auto* actor = second.Replication().Find(motelContainerId);
        return actor != nullptr && actor->hasLoot &&
               actor->lootContainer.searched;
    }));
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        const auto* a = FindClientMission(first.PlayerState(), "motel_guest");
        const auto* b = FindClientMission(second.PlayerState(), "motel_guest");
        return a != nullptr && b != nullptr && a->status == 1 &&
               b->status == 1 && a->rewardClaimed && b->rewardClaimed;
    }));
    CHECK(BagCount(first.PlayerState(), "canned_fruit") ==
          firstFruitBefore + 2);
    CHECK(BagCount(second.PlayerState(), "canned_fruit") ==
          secondFruitBefore + 2);
    CHECK(CountLootContainers(registry, "poi_motel_storage") == 1);

    const auto* motelItems = registry.try_get<zl::ecs::Container>(
        motelContainer);
    REQUIRE(motelItems != nullptr);
    REQUIRE(!motelItems->items.empty());
    const std::size_t stacksBefore = motelItems->items.size();
    REQUIRE(first.SendCommand(
        LootTransferCommand(firstId, motelContainerId, 0), &error));
    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        const auto* actor = second.Replication().Find(motelContainerId);
        return actor != nullptr && actor->hasLoot &&
               actor->lootContainerItems.items.size() < stacksBefore;
    }));

    host.RunForTicks(30);
    CHECK(BagCount(first.PlayerState(), "canned_fruit") ==
          firstFruitBefore + 2);
    CHECK(BagCount(second.PlayerState(), "canned_fruit") ==
          secondFruitBefore + 2);
    CHECK(CountLootContainers(registry, "poi_motel_storage") == 1);

    REQUIRE(TeleportServerPlayer(host, firstId, 62.5f, 29.5f));
    REQUIRE(TeleportServerPlayer(host, secondId, 62.5f, 29.5f));
    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        return host.Simulation().World().GetSquare(62, 29, 0) != nullptr;
    }));

    const int day = 1;
    for (const std::uint64_t id : {firstId, secondId}) {
        const zl::ecs::Entity player = serverPlayer(id);
        REQUIRE(static_cast<bool>(player != entt::null));
        auto& log = registry.get_or_emplace<zl::ecs::MissionLog>(player);
        log.lastDay = day;
        log.randomDay = day;
        zl::ecs::MissionRunState* supply =
            FindServerMission(log, "event_supply_drop");
        if (supply == nullptr) {
            log.missions.push_back(zl::ecs::MissionRunState{});
            supply = &log.missions.back();
        }
        supply->id = "event_supply_drop";
        supply->progress.assign(2, 0);
        supply->status = zl::ecs::MissionRunStatus::Active;
        supply->day = day;
        supply->rewardClaimed = false;
    }
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return CountLootContainers(registry, "event_supply_crate") == 2;
    }));

    std::vector<std::pair<zl::ecs::Entity, zl::ecs::Position>> crates;
    registry.view<zl::ecs::LootContainer, zl::ecs::Position>().each(
        [&](const zl::ecs::Entity entity, const zl::ecs::LootContainer& loot,
            const zl::ecs::Position& position) {
            if (loot.tableId == "event_supply_crate") {
                crates.emplace_back(entity, position);
            }
        });
    REQUIRE(crates.size() == 2);
    std::sort(crates.begin(), crates.end(),
              [](const auto& lhs, const auto& rhs) {
                  return lhs.second.x < rhs.second.x;
              });
    const std::uint64_t supplyId1 = host.Simulation().Ecs().IdOf(
        crates[0].first).id;
    const std::uint64_t supplyId2 = host.Simulation().Ecs().IdOf(
        crates[1].first).id;
    const int firstBeansBefore = BagCount(first.PlayerState(), "canned_beans");

    REQUIRE(TeleportServerPlayer(host, firstId, crates[0].second.x,
                                 crates[0].second.y));
    REQUIRE(first.SendCommand(SearchLootCommand(firstId, supplyId1), &error));
    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        const auto* a = FindClientMission(first.PlayerState(),
                                          "event_supply_drop");
        const auto* b = FindClientMission(second.PlayerState(),
                                          "event_supply_drop");
        return a != nullptr && !a->progress.empty() &&
               a->progress[0] >= 1 && b != nullptr &&
               (b->progress.empty() || b->progress[0] == 0);
    }));
    CHECK(CountLootContainers(registry, "event_supply_crate") == 2);

    REQUIRE(TeleportServerPlayer(host, firstId, crates[1].second.x,
                                 crates[1].second.y));
    REQUIRE(first.SendCommand(SearchLootCommand(firstId, supplyId2), &error));
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        const auto* a = FindClientMission(first.PlayerState(),
                                          "event_supply_drop");
        const auto* b = FindClientMission(second.PlayerState(),
                                          "event_supply_drop");
        return a != nullptr && a->status == 1 && a->rewardClaimed &&
               b != nullptr && b->status == 0 &&
               (b->progress.empty() || b->progress[0] == 0);
    }));
    CHECK(BagCount(first.PlayerState(), "canned_beans") ==
          firstBeansBefore + 1);
    host.RunForTicks(30);
    CHECK(BagCount(first.PlayerState(), "canned_beans") ==
          firstBeansBefore + 1);
    CHECK(CountLootContainers(registry, "event_supply_crate") == 2);

    for (const std::uint64_t id : {firstId, secondId}) {
        const zl::ecs::Entity player = serverPlayer(id);
        REQUIRE(static_cast<bool>(player != entt::null));
        auto& log = registry.get_or_emplace<zl::ecs::MissionLog>(player);
        log.lastDay = day;
        log.randomDay = day;
        zl::ecs::MissionRunState* horde =
            FindServerMission(log, "event_wandering_horde");
        if (horde == nullptr) {
            log.missions.push_back(zl::ecs::MissionRunState{});
            horde = &log.missions.back();
        }
        horde->id = "event_wandering_horde";
        horde->progress.assign(1, 0);
        horde->status = zl::ecs::MissionRunStatus::Active;
        horde->day = day;
        horde->rewardClaimed = false;
    }
    REQUIRE(TeleportServerPlayer(host, firstId, 60.5f, 20.5f));
    REQUIRE(TeleportServerPlayer(host, secondId, 60.5f, 20.5f));
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        int actors = 0;
        registry.view<zl::ecs::RandomEventActor>().each(
            [&](const entt::entity entity,
                const zl::ecs::RandomEventActor& actor) {
                if (registry.valid(entity) &&
                    actor.eventId == "event_wandering_horde") {
                    ++actors;
                }
            });
        return actors == 8;
    }));

    zl::ecs::Entity hordeActor = entt::null;
    registry.view<zl::ecs::RandomEventActor>().each(
        [&](const entt::entity entity,
            const zl::ecs::RandomEventActor& actor) {
            if (hordeActor == entt::null && registry.valid(entity) &&
                actor.eventId == "event_wandering_horde") {
                hordeActor = entity;
            }
        });
    REQUIRE(static_cast<bool>(hordeActor != entt::null));
    auto* hordeHealth = registry.try_get<zl::ecs::Health>(hordeActor);
    auto* hordePosition = registry.try_get<zl::ecs::Position>(hordeActor);
    REQUIRE(hordeHealth != nullptr);
    REQUIRE(hordePosition != nullptr);
    hordeHealth->current = 1.0f;
    REQUIRE(TeleportServerPlayer(host, firstId, hordePosition->x,
                                 hordePosition->y));
    zl::net::Command hordeAttack;
    hordeAttack.type = zl::net::CommandType::Attack;
    hordeAttack.playerStableId = firstId;
    hordeAttack.payload.entityId =
        host.Simulation().Ecs().IdOf(hordeActor).id;
    REQUIRE(first.SendCommand(hordeAttack, &error));
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        const auto* a = FindClientMission(first.PlayerState(),
                                          "event_wandering_horde");
        const auto* b = FindClientMission(second.PlayerState(),
                                          "event_wandering_horde");
        return a != nullptr && !a->progress.empty() && a->progress[0] >= 1 &&
               b != nullptr &&
               (b->progress.empty() || b->progress[0] == 0);
    }));

    second.Close();
    REQUIRE(PumpUntil(host, first, 5000, [&] {
        return host.SessionCount() == 1;
    }));
    zl::game::NetworkClient secondReconnect(secondOptions);
    REQUIRE(secondReconnect.Connect(&error));
    REQUIRE(PumpUntil(host, first, secondReconnect, 8000, [&] {
        return secondReconnect.HasPlayerState() &&
               secondReconnect.PlayerStableId() == secondId;
    }));
    CHECK(CountLootContainers(registry, "event_supply_crate") == 2);
    const auto* reconnectedSupply = FindClientMission(
        secondReconnect.PlayerState(), "event_supply_drop");
    REQUIRE(reconnectedSupply != nullptr);
    CHECK(reconnectedSupply->status == 0);
    CHECK((reconnectedSupply->progress.empty() ||
           reconnectedSupply->progress[0] == 0));

    first.Close();
    REQUIRE(PumpUntil(host, secondReconnect, 5000, [&] {
        return host.SessionCount() == 1;
    }));
    zl::game::NetworkClient firstReconnect(firstOptions);
    REQUIRE(firstReconnect.Connect(&error));
    REQUIRE(PumpUntil(host, secondReconnect, firstReconnect, 8000, [&] {
        return firstReconnect.HasPlayerState() &&
               firstReconnect.PlayerStableId() == firstId;
    }));
    const auto* reconnectedCompleted = FindClientMission(
        firstReconnect.PlayerState(), "event_supply_drop");
    REQUIRE(reconnectedCompleted != nullptr);
    CHECK(reconnectedCompleted->status == 1);
    CHECK(reconnectedCompleted->rewardClaimed);
    CHECK(BagCount(firstReconnect.PlayerState(), "canned_beans") ==
          firstBeansBefore + 1);
    CHECK(CountLootContainers(registry, "event_supply_crate") == 2);

    firstReconnect.Close();
    secondReconnect.Close();
    host.Shutdown();
}

TEST_CASE("M34-F3-T2 联机陷阱：权威放置、布防/解除、拆除与拒绝") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x5452415032ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;

    zl::game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.IsConnected() && client.HasPlayer() &&
               client.HasPlayerState();
    }));
    const std::uint64_t playerId = client.PlayerStableId();
    REQUIRE(playerId != 0);

    const zl::ecs::Entity player =
        host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));
    auto& registry = host.Simulation().Ecs().Registry();
    auto& serverBag = registry.get<zl::ecs::Container>(player);
    serverBag.items.push_back(zl::ecs::ItemInstance{"wood", 20, 1.0f});

    const auto& defs = zl::game::DefaultBuildables();
    const zl::game::BuildableDef* bearDef = nullptr;
    for (const zl::game::BuildableDef& def : defs) {
        if (def.id == "bear_trap") {
            bearDef = &def;
            break;
        }
    }
    REQUIRE(bearDef != nullptr);
    const std::int32_t bearIndex = static_cast<std::int32_t>(
        std::distance(defs.data(), bearDef));

    const auto findFreeCell = [&](int* outX, int* outY) {
        const auto* position = registry.try_get<zl::ecs::Position>(player);
        if (position == nullptr) {
            return false;
        }
        const int px = static_cast<int>(std::floor(position->x));
        const int py = static_cast<int>(std::floor(position->y));
        for (int radius = 1; radius <= 3; ++radius) {
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (std::abs(dx) != radius && std::abs(dy) != radius) {
                        continue;
                    }
                    if (std::hypot(static_cast<float>(dx),
                                   static_cast<float>(dy)) > 3.0f) {
                        continue;
                    }
                    const int wx = px + dx;
                    const int wy = py + dy;
                    zl::world::GridSquare* square =
                        host.Simulation().World().GetSquare(wx, wy, 0);
                    if (square == nullptr ||
                        !zl::game::IsBuildCellFreeFor(*bearDef, *square)) {
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
    const auto placeCommand = [&](int wx, int wy) {
        zl::net::Command command;
        command.type = zl::net::CommandType::PlaceStructure;
        command.playerStableId = playerId;
        command.payload.x = static_cast<float>(wx);
        command.payload.y = static_cast<float>(wy);
        command.payload.index = bearIndex;
        return command;
    };
    const auto findTrapAt = [&](int wx, int wy) {
        zl::ecs::Entity found = entt::null;
        registry.view<zl::ecs::PlacedTrap, zl::ecs::Position>().each(
            [&](const zl::ecs::Entity entity,
                const zl::ecs::PlacedTrap&,
                const zl::ecs::Position& position) {
                if (found == entt::null && position.z == 0 &&
                    static_cast<int>(std::floor(position.x)) == wx &&
                    static_cast<int>(std::floor(position.y)) == wy) {
                    found = entity;
                }
            });
        return found;
    };

    const auto* playerPosition =
        registry.try_get<zl::ecs::Position>(player);
    REQUIRE(playerPosition != nullptr);

    const std::uint64_t rangeBefore = host.GetNetworkStats().rangeRejected;
    REQUIRE(client.SendCommand(
        placeCommand(static_cast<int>(std::floor(playerPosition->x)) + 40,
                     static_cast<int>(std::floor(playerPosition->y))),
        &error));
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        return host.GetNetworkStats().rangeRejected > rangeBefore;
    }));

    int woodlessX = 0;
    int woodlessY = 0;
    REQUIRE(findFreeCell(&woodlessX, &woodlessY));
    const int woodCount = zl::items::CountOf(serverBag, "wood");
    REQUIRE(woodCount >= 3);
    REQUIRE(zl::items::RemoveItem(serverBag, "wood", woodCount) == woodCount);
    const std::uint64_t woodlessRejectedBefore =
        host.GetNetworkStats().commandsRejected;
    REQUIRE(client.SendCommand(placeCommand(woodlessX, woodlessY), &error));
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        return host.GetNetworkStats().commandsRejected >
               woodlessRejectedBefore;
    }));
    CHECK_FALSE(static_cast<bool>(findTrapAt(woodlessX, woodlessY) != entt::null));
    serverBag.items.push_back(zl::ecs::ItemInstance{"wood", 20, 1.0f});

    int trapX = 0;
    int trapY = 0;
    REQUIRE(findFreeCell(&trapX, &trapY));
    REQUIRE(client.SendCommand(placeCommand(trapX, trapY), &error));
    zl::ecs::Entity trap = entt::null;
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        trap = findTrapAt(trapX, trapY);
        return trap != entt::null;
    }));
    const std::uint64_t trapId = host.Simulation().Ecs().IdOf(trap).id;
    REQUIRE(trapId != 0);
    const auto& placed = registry.get<zl::ecs::PlacedTrap>(trap);
    CHECK(placed.defId == "bear_trap");
    CHECK(placed.owner == playerId);
    CHECK(placed.uses == 4);
    CHECK(placed.cooldown == doctest::Approx(0.0f));
    CHECK((placed.armed || placed.armRemaining > 0.0f));
    CHECK(registry.get<zl::ecs::StructureOwner>(trap).ownerStableId ==
          playerId);
    CHECK(zl::items::CountOf(serverBag, "wood") == 17);

    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto* actor = client.Replication().Find(trapId);
        return actor != nullptr && actor->hasPlacedTrap &&
               actor->placedTrap.defId == "bear_trap" &&
               actor->placedTrap.owner == playerId;
    }));
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        return registry.get<zl::ecs::PlacedTrap>(trap).armed;
    }));

    const std::uint64_t occupiedRejectedBefore =
        host.GetNetworkStats().commandsRejected;
    REQUIRE(client.SendCommand(placeCommand(trapX, trapY), &error));
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        return host.GetNetworkStats().commandsRejected >
               occupiedRejectedBefore;
    }));
    CHECK(findTrapAt(trapX, trapY) == trap);

    const auto facilityCommand = [&](std::uint64_t entityId,
                                     std::uint64_t stableId) {
        zl::net::Command command;
        command.type = zl::net::CommandType::InteractFacility;
        command.playerStableId = stableId;
        command.payload.entityId = entityId;
        return command;
    };
    REQUIRE(client.SendCommand(facilityCommand(trapId, playerId), &error));
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        const auto& state = registry.get<zl::ecs::PlacedTrap>(trap);
        return !state.armed && state.uses == 4 &&
               state.armRemaining == doctest::Approx(0.0f);
    }));
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        const auto* actor = client.Replication().Find(trapId);
        return actor != nullptr && actor->hasPlacedTrap &&
               !actor->placedTrap.armed && actor->placedTrap.uses == 4;
    }));
    REQUIRE(client.SendCommand(facilityCommand(trapId, playerId), &error));
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        const auto& state = registry.get<zl::ecs::PlacedTrap>(trap);
        return state.armed && state.uses == 4;
    }));

    zl::game::NetworkClient intruder(clientOptions);
    REQUIRE(intruder.Connect(&error));
    REQUIRE(PumpUntil(host, client, intruder, 6000, [&] {
        return intruder.IsConnected() && intruder.HasPlayer();
    }));
    const std::uint64_t intruderId = intruder.PlayerStableId();
    REQUIRE(intruderId != 0);
    REQUIRE(intruderId != playerId);
    REQUIRE(TeleportServerPlayer(host, intruderId,
                                 static_cast<float>(trapX) + 0.5f,
                                 static_cast<float>(trapY) + 0.5f));
    const std::uint64_t ownerRejectedBefore =
        host.GetNetworkStats().ownershipRejected;
    REQUIRE(intruder.SendCommand(facilityCommand(trapId, intruderId), &error));
    REQUIRE(PumpUntil(host, client, intruder, 3000, [&] {
        return host.GetNetworkStats().ownershipRejected >
               ownerRejectedBefore;
    }));
    CHECK(registry.get<zl::ecs::PlacedTrap>(trap).armed);

    zl::net::Command intruderDemolish;
    intruderDemolish.type = zl::net::CommandType::DemolishStructure;
    intruderDemolish.playerStableId = intruderId;
    intruderDemolish.payload.x = static_cast<float>(trapX);
    intruderDemolish.payload.y = static_cast<float>(trapY);
    intruderDemolish.payload.flags = zl::net::kDemolishConfirmRequestFlag;
    const std::uint64_t intruderDemolishRejectedBefore =
        host.GetNetworkStats().ownershipRejected;
    REQUIRE(intruder.SendCommand(intruderDemolish, &error));
    REQUIRE(PumpUntil(host, client, intruder, 3000, [&] {
        return host.GetNetworkStats().ownershipRejected >
               intruderDemolishRejectedBefore;
    }));
    CHECK(host.Simulation().Ecs().Alive(trap));
    intruder.Close();
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return host.SessionCount() == 1;
    }));

    host.RunForTicks(40);
    client.Poll();
    const int woodBeforeDemolish = zl::items::CountOf(serverBag, "wood");
    zl::net::Command demolish;
    demolish.type = zl::net::CommandType::DemolishStructure;
    demolish.playerStableId = playerId;
    demolish.payload.x = static_cast<float>(trapX);
    demolish.payload.y = static_cast<float>(trapY);
    demolish.payload.flags = zl::net::kDemolishConfirmRequestFlag;
    REQUIRE(client.SendCommand(demolish, &error));
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        return client.PlayerState().status.find("再次按 X 确认拆除") !=
               std::string::npos;
    }));
    CHECK(host.Simulation().Ecs().Alive(trap));
    demolish.payload.flags = zl::net::kDemolishConfirmAcceptFlag;
    REQUIRE(client.SendCommand(demolish, &error));
    REQUIRE(PumpUntil(host, client, 4000, [&] {
        return !host.Simulation().Ecs().Alive(trap);
    }));
    CHECK(zl::items::CountOf(serverBag, "wood") == woodBeforeDemolish + 2);
    CHECK(BagCount(client.PlayerState(), "bear_trap") == 0);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return client.Replication().Find(trapId) == nullptr;
    }));

    client.Close();
    host.Shutdown();
}
