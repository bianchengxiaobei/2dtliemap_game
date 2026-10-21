#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>

#include "characters/components/CharacterComponents.h"
#include "ecs/components/CommonComponents.h"
#include "game/NetworkClient.h"
#include "game/WorldSimulation.h"
#include "items/ItemComponents.h"
#include "net/Command.h"
#include "server/ServerHost.h"
#include "ui/InventoryUi.h"

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
    const auto& position =
        host.Simulation().Ecs().Registry().get<zl::ecs::Position>(player);
    const std::uint64_t bandageId =
        SpawnItem(host.Simulation(), "bandage", 2, position.x + 0.2f, position.y);
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
        SpawnItem(host.Simulation(), "axe", 1, position.x + 0.2f, position.y);
    REQUIRE(axeId != 0);
    pickup.payload.entityId = axeId;
    REQUIRE(client.SendCommand(pickup, &error));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return BagCount(client.PlayerState(), "axe") == 1;
    }));

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
    const std::uint64_t targetId = host.Simulation().Ecs().IdOf(target).id;
    const float targetHealth =
        host.Simulation().Ecs().Registry().get<zl::ecs::Health>(target).current;
    zl::net::Command fire;
    fire.type = zl::net::CommandType::FireProjectile;
    fire.playerStableId = playerId;
    fire.payload.entityId = targetId;
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
