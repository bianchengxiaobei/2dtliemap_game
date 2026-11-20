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

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "core/Config.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/BuildingRules.h"
#include "game/FireSystem.h"
#include "game/Homestead.h"
#include "game/NetworkClient.h"
#include "game/WorldSimulation.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "net/Command.h"
#include "physics/PhysicsWorld.h"
#include "server/ServerHost.h"
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

}  // namespace

TEST_CASE("M34-F3-T3 联机陷阱：捕兽夹伤害、绊线噪声与燃油火焰") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x5452415033ull;

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
    serverBag.items.push_back(zl::ecs::ItemInstance{"wood", 30, 1.0f});

    const auto& defs = zl::game::DefaultBuildables();
    const auto buildIndex = [&](std::string_view id) {
        for (std::size_t i = 0; i < defs.size(); ++i) {
            if (defs[i].id == id) {
                return static_cast<std::int32_t>(i);
            }
        }
        return static_cast<std::int32_t>(-1);
    };
    const std::int32_t bearIndex = buildIndex("bear_trap");
    const std::int32_t tripwireIndex = buildIndex("tripwire_alarm");
    const std::int32_t fuelIndex = buildIndex("fuel_trap");
    REQUIRE(bearIndex >= 0);
    REQUIRE(tripwireIndex >= 0);
    REQUIRE(fuelIndex >= 0);

    const auto findFreeCell = [&](const zl::game::BuildableDef& buildDef,
                                  int* outX, int* outY) {
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
                        !zl::game::IsBuildCellFreeFor(buildDef, *square)) {
                        continue;
                    }
                    bool occupied = false;
                    registry.view<zl::ecs::Position>().each(
                        [&](const zl::ecs::Position& candidate) {
                            if (!occupied && candidate.z == 0 &&
                                static_cast<int>(std::floor(candidate.x)) ==
                                    wx &&
                                static_cast<int>(std::floor(candidate.y)) ==
                                    wy) {
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
    const auto placeCommand = [&](std::int32_t index, int wx, int wy) {
        zl::net::Command command;
        command.type = zl::net::CommandType::PlaceStructure;
        command.playerStableId = playerId;
        command.payload.x = static_cast<float>(wx);
        command.payload.y = static_cast<float>(wy);
        command.payload.index = index;
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
    const auto placeTrap = [&](std::int32_t index, std::string_view defId,
                               const zl::game::BuildableDef& buildDef) {
        int wx = 0;
        int wy = 0;
        REQUIRE(findFreeCell(buildDef, &wx, &wy));
        REQUIRE(client.SendCommand(placeCommand(index, wx, wy), &error));
        zl::ecs::Entity trap = entt::null;
        REQUIRE(PumpUntil(host, client, 5000, [&] {
            trap = findTrapAt(wx, wy);
            return trap != entt::null &&
                   registry.get<zl::ecs::PlacedTrap>(trap).defId == defId;
        }));
        REQUIRE(PumpUntil(host, client, 5000, [&] {
            return registry.get<zl::ecs::PlacedTrap>(trap).armed;
        }));
        return std::pair<zl::ecs::Entity, std::pair<int, int>>{
            trap, {wx, wy}};
    };

    const auto bearPlacement =
        placeTrap(bearIndex, "bear_trap",
                  defs[static_cast<std::size_t>(bearIndex)]);
    const auto tripwirePlacement =
        placeTrap(tripwireIndex, "tripwire_alarm",
                  defs[static_cast<std::size_t>(tripwireIndex)]);
    const auto fuelPlacement =
        placeTrap(fuelIndex, "fuel_trap",
                  defs[static_cast<std::size_t>(fuelIndex)]);
    const zl::ecs::Entity bearTrap = bearPlacement.first;
    const int bearX = bearPlacement.second.first;
    const int bearY = bearPlacement.second.second;
    const zl::ecs::Entity tripwireTrap = tripwirePlacement.first;
    const int tripwireX = tripwirePlacement.second.first;
    const int tripwireY = tripwirePlacement.second.second;
    const zl::ecs::Entity fuelTrap = fuelPlacement.first;
    const int fuelX = fuelPlacement.second.first;
    const int fuelY = fuelPlacement.second.second;
    for (const zl::ecs::Entity trap : {bearTrap, tripwireTrap, fuelTrap}) {
        auto& state = registry.get<zl::ecs::PlacedTrap>(trap);
        state.armed = false;
        state.armRemaining = 0.0f;
    }
    const auto* playerPosition =
        registry.try_get<zl::ecs::Position>(player);
    REQUIRE(playerPosition != nullptr);
    REQUIRE(TeleportServerPlayer(host, playerId, playerPosition->x + 8.0f,
                                 playerPosition->y + 8.0f));
    host.RunForTicks(2);

    registry.get<zl::ecs::PlacedTrap>(bearTrap).armed = true;
    const zl::ecs::Entity bearZombie = host.Simulation().SpawnZombieAt(
        static_cast<float>(bearX) + 0.5f, static_cast<float>(bearY) + 0.5f,
        zl::ecs::ZombieVariantKind::Normal);
    REQUIRE(static_cast<bool>(bearZombie != entt::null));
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        const auto& trap = registry.get<zl::ecs::PlacedTrap>(bearTrap);
        const auto* health = registry.try_get<zl::ecs::Health>(bearZombie);
        return trap.uses == 3 && trap.cooldown > 0.0f && health != nullptr &&
               health->current <= 45.5f;
    }));
    const auto& bearState = registry.get<zl::ecs::PlacedTrap>(bearTrap);
    CHECK(bearState.armed);
    CHECK(bearState.uses == 3);
    CHECK(bearState.cooldown == doctest::Approx(2.0f));
    CHECK(registry.get<zl::ecs::Health>(bearZombie).current ==
          doctest::Approx(45.0f));
    CHECK(registry.get<zl::ecs::Health>(player).current ==
          doctest::Approx(100.0f));
    registry.get<zl::ecs::PlacedTrap>(bearTrap).armed = false;
    host.Simulation().DespawnEntity(bearZombie);
    host.RunForTicks(2);

    const zl::ecs::Entity tripwireZombie = host.Simulation().SpawnZombieAt(
        static_cast<float>(tripwireX) + 0.5f,
        static_cast<float>(tripwireY) + 0.5f,
        zl::ecs::ZombieVariantKind::Normal);
    REQUIRE(static_cast<bool>(tripwireZombie != entt::null));
    auto* tripwirePerception =
        registry.try_get<zl::ecs::PerceptionState>(tripwireZombie);
    REQUIRE(tripwirePerception != nullptr);
    tripwirePerception->sinceHeard = 5.0f;
    registry.get<zl::ecs::PlacedTrap>(tripwireTrap).armed = true;
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        const auto& trap = registry.get<zl::ecs::PlacedTrap>(tripwireTrap);
        const auto* perception =
            registry.try_get<zl::ecs::PerceptionState>(tripwireZombie);
        return trap.uses == 7 && trap.cooldown > 0.0f &&
               perception != nullptr && perception->sinceHeard < 0.01f;
    }));
    const auto& tripwireState =
        registry.get<zl::ecs::PlacedTrap>(tripwireTrap);
    CHECK(tripwireState.armed);
    CHECK(tripwireState.uses == 7);
    CHECK(tripwireState.cooldown > 0.9f);
    CHECK(tripwireState.cooldown <= 1.0f);
    CHECK(registry.get<zl::ecs::Health>(tripwireZombie).current ==
          doctest::Approx(100.0f));
    CHECK(registry.get<zl::ecs::Health>(player).current ==
          doctest::Approx(100.0f));
    registry.get<zl::ecs::PlacedTrap>(tripwireTrap).armed = false;
    host.Simulation().DespawnEntity(tripwireZombie);
    host.RunForTicks(2);

    registry.get<zl::ecs::PlacedTrap>(fuelTrap).armed = true;
    const zl::ecs::Entity fuelZombie = host.Simulation().SpawnZombieAt(
        static_cast<float>(fuelX) + 0.5f,
        static_cast<float>(fuelY) + 0.5f,
        zl::ecs::ZombieVariantKind::Normal);
    REQUIRE(static_cast<bool>(fuelZombie != entt::null));
    REQUIRE(PumpUntil(host, client, 4000, [&] {
        const auto& trap = registry.get<zl::ecs::PlacedTrap>(fuelTrap);
        const zl::ecs::FireSource* fire =
            host.Simulation().Fire()->FindAt(host.Simulation().Ecs(), fuelX,
                                             fuelY, 0);
        return trap.uses == 0 && !trap.armed && fire != nullptr &&
               fire->phase == zl::ecs::FirePhase::Burning;
    }));
    const zl::ecs::FireSource* fire = host.Simulation().Fire()->FindAt(
        host.Simulation().Ecs(), fuelX, fuelY, 0);
    REQUIRE(fire != nullptr);
    CHECK(fire->materialId == "trap_fuel");
    CHECK(fire->ownerStableId == playerId);
    CHECK(fire->burnDuration == doctest::Approx(112.5f));
    CHECK(fire->heatRadius == doctest::Approx(1.5f));
    std::size_t activeFireCells = 0;
    registry.view<zl::ecs::FireSource, zl::ecs::FireAnchor>().each(
        [&](const zl::ecs::Entity, const zl::ecs::FireSource& source,
            const zl::ecs::FireAnchor& anchor) {
            if (source.phase != zl::ecs::FirePhase::Extinguished &&
                anchor.z == 0 && anchor.x >= fuelX - 1 &&
                anchor.x <= fuelX + 1 && anchor.y >= fuelY - 1 &&
                anchor.y <= fuelY + 1) {
                ++activeFireCells;
            }
        });
    CHECK(activeFireCells == 9u);
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        return registry.any_of<zl::ecs::BurningZombie>(fuelZombie);
    }));
    CHECK(registry.get<zl::ecs::Health>(player).current ==
          doctest::Approx(100.0f));
    CHECK(zl::items::CountOf(serverBag, "wood") == 22);

    client.Close();
    host.Shutdown();
}
