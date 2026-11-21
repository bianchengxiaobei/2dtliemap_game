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

std::filesystem::path TempProfileDir(const char* name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zomboidlike_f3t5" / name;
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir, error);
    return dir;
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


TEST_CASE("M34-F3-T5 双客户端陷阱：权威、复制、拒绝与重连") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);
    const std::filesystem::path profileDir = TempProfileDir("dual");
    const std::string profilePath = (profileDir / "players.bin").string();

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D33345452353031ull;
    hostOptions.enablePlayerProfiles = true;
    hostOptions.playerProfilesFile = profilePath;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options firstOptions;
    firstOptions.host = "127.0.0.1";
    firstOptions.port = host.NetworkPort();
    firstOptions.contentHash = hostOptions.contentHash;
    firstOptions.autoReconnect = false;
    firstOptions.playerIdentity = "m34-t5-trap-owner";
    firstOptions.playerName = "TrapOwner";
    zl::game::NetworkClient::Options secondOptions = firstOptions;
    secondOptions.playerIdentity = "m34-t5-trap-watcher";
    secondOptions.playerName = "TrapWatcher";

    zl::game::NetworkClient first(firstOptions);
    zl::game::NetworkClient second(secondOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayer() && second.HasPlayer() &&
               first.HasPlayerState() && second.HasPlayerState() &&
               host.SessionCount() == 2;
    }));

    const std::uint64_t firstId = first.PlayerStableId();
    const std::uint64_t secondId = second.PlayerStableId();
    REQUIRE(firstId != 0);
    REQUIRE(secondId != 0);
    REQUIRE(firstId != secondId);

    zl::ecs::EcsWorld& ecs = host.Simulation().Ecs();
    entt::registry& registry = ecs.Registry();
    const zl::ecs::Entity firstPlayer = ecs.FindById(firstId);
    const zl::ecs::Entity secondPlayer = ecs.FindById(secondId);
    REQUIRE(static_cast<bool>(firstPlayer != entt::null));
    REQUIRE(static_cast<bool>(secondPlayer != entt::null));
    auto* firstPosition = registry.try_get<zl::ecs::Position>(firstPlayer);
    REQUIRE(firstPosition != nullptr);
    REQUIRE(TeleportServerPlayer(host, secondId, firstPosition->x + 1.0f,
                                 firstPosition->y + 1.0f));

    auto& serverBag = registry.get<zl::ecs::Container>(firstPlayer);
    serverBag.items.push_back(zl::ecs::ItemInstance{"wood", 10, 1.0f});

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
    REQUIRE(bearIndex >= 0);
    const zl::game::BuildableDef& bearBuild =
        defs[static_cast<std::size_t>(bearIndex)];

    const auto findFreeCell = [&](int* outX, int* outY) {
        const auto* position = registry.try_get<zl::ecs::Position>(firstPlayer);
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
                        !zl::game::IsBuildCellFreeFor(bearBuild, *square)) {
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
    const auto placeCommand = [&](int wx, int wy) {
        zl::net::Command command;
        command.type = zl::net::CommandType::PlaceStructure;
        command.playerStableId = firstId;
        command.payload.x = static_cast<float>(wx);
        command.payload.y = static_cast<float>(wy);
        command.payload.index = bearIndex;
        return command;
    };

    int trapX = 0;
    int trapY = 0;
    REQUIRE(findFreeCell(&trapX, &trapY));
    REQUIRE(first.SendCommand(placeCommand(trapX, trapY), &error));
    zl::ecs::Entity trap = entt::null;
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        trap = findTrapAt(trapX, trapY);
        return trap != entt::null &&
               registry.get<zl::ecs::PlacedTrap>(trap).armed;
    }));
    const std::uint64_t trapId = ecs.IdOf(trap).id;
    REQUIRE(trapId != 0);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        const auto* firstActor = first.Replication().Find(trapId);
        const auto* secondActor = second.Replication().Find(trapId);
        return firstActor != nullptr && secondActor != nullptr &&
               firstActor->hasPlacedTrap && secondActor->hasPlacedTrap &&
               firstActor->placedTrap.defId == "bear_trap" &&
               secondActor->placedTrap.defId == "bear_trap" &&
               firstActor->placedTrap.armed && secondActor->placedTrap.armed &&
               firstActor->placedTrap.cooldown == doctest::Approx(0.0f) &&
               secondActor->placedTrap.cooldown == doctest::Approx(0.0f) &&
               firstActor->placedTrap.uses == 4 &&
               secondActor->placedTrap.uses == 4 &&
               firstActor->placedTrap.owner == firstId &&
               secondActor->placedTrap.owner == firstId;
    }));

    REQUIRE(TeleportServerPlayer(host, secondId,
                                 static_cast<float>(trapX) + 0.5f,
                                 static_cast<float>(trapY) + 0.5f));
    const std::uint64_t ownershipRejectedBefore =
        host.GetNetworkStats().ownershipRejected;
    zl::net::Command intruderArm;
    intruderArm.type = zl::net::CommandType::InteractFacility;
    intruderArm.playerStableId = secondId;
    intruderArm.payload.entityId = trapId;
    REQUIRE(second.SendCommand(intruderArm, &error));
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        return host.GetNetworkStats().ownershipRejected >
               ownershipRejectedBefore;
    }));
    CHECK(registry.get<zl::ecs::PlacedTrap>(trap).armed);

    const std::uint64_t duplicateRejectedBefore =
        host.GetNetworkStats().commandsRejected;
    REQUIRE(first.SendCommand(placeCommand(trapX, trapY), &error));
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        return host.GetNetworkStats().commandsRejected >
               duplicateRejectedBefore;
    }));
    CHECK(findTrapAt(trapX, trapY) == trap);

    auto* firstPositionAfter = registry.try_get<zl::ecs::Position>(firstPlayer);
    auto* secondPositionAfter = registry.try_get<zl::ecs::Position>(secondPlayer);
    REQUIRE(firstPositionAfter != nullptr);
    REQUIRE(secondPositionAfter != nullptr);
    REQUIRE(TeleportServerPlayer(host, firstId,
                                 firstPositionAfter->x + 8.0f,
                                 firstPositionAfter->y + 8.0f));
    REQUIRE(TeleportServerPlayer(host, secondId,
                                 secondPositionAfter->x + 8.0f,
                                 secondPositionAfter->y + 8.0f));

    const zl::ecs::Entity firstZombie = host.Simulation().SpawnZombieAt(
        static_cast<float>(trapX) + 0.4f,
        static_cast<float>(trapY) + 0.3f,
        zl::ecs::ZombieVariantKind::Normal);
    const zl::ecs::Entity secondZombie = host.Simulation().SpawnZombieAt(
        static_cast<float>(trapX) + 0.4f,
        static_cast<float>(trapY) + 0.7f,
        zl::ecs::ZombieVariantKind::Normal);
    REQUIRE(static_cast<bool>(firstZombie != entt::null));
    REQUIRE(static_cast<bool>(secondZombie != entt::null));
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return registry.get<zl::ecs::PlacedTrap>(trap).uses == 3;
    }));
    const auto& triggeredState = registry.get<zl::ecs::PlacedTrap>(trap);
    CHECK(triggeredState.cooldown > 0.0f);
    const int firstHealth = static_cast<int>(
        std::lround(registry.get<zl::ecs::Health>(firstZombie).current));
    const int secondHealth = static_cast<int>(
        std::lround(registry.get<zl::ecs::Health>(secondZombie).current));
    CHECK((firstHealth == 45 || secondHealth == 45));
    CHECK((firstHealth == 100 || secondHealth == 100));
    registry.get<zl::ecs::PlacedTrap>(trap).armed = false;
    host.Simulation().DespawnEntity(firstZombie);
    host.Simulation().DespawnEntity(secondZombie);
    host.RunForTicks(2);
    first.Poll();
    second.Poll();

    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        const auto* firstActor = first.Replication().Find(trapId);
        const auto* secondActor = second.Replication().Find(trapId);
        return firstActor != nullptr && secondActor != nullptr &&
               firstActor->hasPlacedTrap && secondActor->hasPlacedTrap &&
               firstActor->placedTrap.uses == 3 &&
               secondActor->placedTrap.uses == 3 &&
               firstActor->placedTrap.cooldown > 0.0f &&
               secondActor->placedTrap.cooldown > 0.0f;
    }));

    first.Close();
    REQUIRE(PumpUntil(host, second, 8000, [&] {
        return host.SessionCount() == 1;
    }));
    zl::game::NetworkClient reconnected(firstOptions);
    REQUIRE(reconnected.Connect(&error));
    REQUIRE(PumpUntil(host, second, reconnected, 8000, [&] {
        return reconnected.IsConnected() && reconnected.HasPlayer() &&
               reconnected.HasPlayerState() &&
               reconnected.PlayerStableId() == firstId;
    }));
    REQUIRE(TeleportServerPlayer(host, firstId,
                                 static_cast<float>(trapX) + 0.5f,
                                 static_cast<float>(trapY) + 0.5f));
    REQUIRE(PumpUntil(host, second, reconnected, 5000, [&] {
        const auto* ownerActor = reconnected.Replication().Find(trapId);
        const auto* watcherActor = second.Replication().Find(trapId);
        return ownerActor != nullptr && watcherActor != nullptr &&
               ownerActor->hasPlacedTrap && watcherActor->hasPlacedTrap &&
               ownerActor->placedTrap.defId == "bear_trap" &&
               watcherActor->placedTrap.defId == "bear_trap" &&
               ownerActor->placedTrap.uses == 3 &&
               watcherActor->placedTrap.uses == 3 &&
               ownerActor->placedTrap.owner == firstId &&
               watcherActor->placedTrap.owner == firstId;
    }));
    CHECK(registry.get<zl::ecs::PlacedTrap>(trap).uses == 3);
    CHECK_FALSE(registry.get<zl::ecs::PlacedTrap>(trap).armed);

    zl::net::Command legacyDemolish;
    legacyDemolish.type = zl::net::CommandType::DemolishStructure;
    legacyDemolish.playerStableId = firstId;
    legacyDemolish.payload.x = static_cast<float>(trapX);
    legacyDemolish.payload.y = static_cast<float>(trapY);
    legacyDemolish.payload.flags = 0;
    REQUIRE(reconnected.SendCommand(legacyDemolish, &error));
    REQUIRE(PumpUntil(host, second, reconnected, 4000, [&] {
        return findTrapAt(trapX, trapY) == entt::null;
    }));

    reconnected.Close();
    second.Close();
    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(profileDir, cleanupError);
}
