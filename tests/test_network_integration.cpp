#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "ai/components/AiComponents.h"
#include "audio/SoundIds.h"
#include "characters/components/AnimalComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/systems/CombatSystem.h"
#include "characters/components/CombatComponents.h"
#include "characters/components/ProgressionComponents.h"
#include "core/Constants.h"
#include "core/Log.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/AnimalSystem.h"
#include "game/BaseSiegeDirector.h"
#include "game/BuildingRules.h"
#include "game/FireComponents.h"
#include "game/FireSystem.h"
#include "game/GroundItems.h"
#include "game/Homestead.h"
#include "game/NetworkClient.h"
#include "game/StructureComponents.h"
#include "game/VehicleComponents.h"
#include "game/VehicleSpike.h"
#include "game/WorldQueries.h"
#include "game/WorldSimulation.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "net/Command.h"
#include "net/WorldSound.h"
#include "physics/PhysicsWorld.h"
#include "save/ChunkDiff.h"
#include "server/ServerHost.h"
#include "ServerHostTestAccess.h"
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
bool PumpUntil(zl::server::ServerHost& host, zl::game::NetworkClient& first,
               zl::game::NetworkClient& second, int timeoutMs, Predicate predicate) {
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
bool PumpHostUntil(zl::server::ServerHost& host, int timeoutMs,
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

template <typename Predicate>
bool PumpUntil(zl::server::ServerHost& host, zl::game::NetworkClient& first,
               zl::game::NetworkClient& second,
               zl::game::NetworkClient& third, int timeoutMs,
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
        third.Poll();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

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

zl::net::Command MakeMove(std::uint64_t playerId, float x, float y) {
    zl::net::Command command;
    command.type = zl::net::CommandType::Move;
    command.playerStableId = playerId;
    command.payload.x = x;
    command.payload.y = y;
    return command;
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

zl::net::Command MakeVehicleCommand(zl::net::CommandType type,
                                    std::uint64_t playerId,
                                    std::uint64_t vehicleId) {
    zl::net::Command command;
    command.type = type;
    command.playerStableId = playerId;
    command.payload.entityId = vehicleId;
    return command;
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
            const zl::world::GridSquare* square =
                host.Simulation().World().GetSquare(wx, wy, 0);
            if (square == nullptr ||
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

bool FindOpenVehicleLane(zl::server::ServerHost& host, float centerX,
                         float centerY, int searchRadius, int* outCellX,
                         int* outCellY, float* outDirX, float* outDirY) {
    constexpr float kDirections[4][2] = {
        {1.0f, 0.0f}, {-1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, -1.0f}};
    const auto laneClear = [&](int cellX, int cellY, float dirX,
                               float dirY) {
        for (int step = 0; step <= 6; ++step) {
            const int wx =
                cellX + static_cast<int>(dirX) * step;
            const int wy =
                cellY + static_cast<int>(dirY) * step;
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    const int checkX = wx + ox;
                    const int checkY = wy + oy;
                    const zl::world::GridSquare* square =
                        host.Simulation().World().GetSquare(checkX, checkY, 0);
                    if (square == nullptr ||
                        zl::game::IsSquareBlocked(
                            host.Simulation().World(), checkX, checkY, 0)) {
                        return false;
                    }
                }
            }
        }
        return true;
    };
    const int baseX = static_cast<int>(std::floor(centerX));
    const int baseY = static_cast<int>(std::floor(centerY));
    for (int radius = 0; radius <= searchRadius; ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != radius) {
                    continue;
                }
                const int cellX = baseX + dx;
                const int cellY = baseY + dy;
                for (const auto& direction : kDirections) {
                    if (!laneClear(cellX, cellY, direction[0],
                                   direction[1])) {
                        continue;
                    }
                    *outCellX = cellX;
                    *outCellY = cellY;
                    *outDirX = direction[0];
                    *outDirY = direction[1];
                    return true;
                }
            }
        }
    }
    return false;
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

std::uint64_t SpawnCampfire(zl::server::ServerHost& host, int wx, int wy) {
    const zl::game::BuildableDef* campfireDef = nullptr;
    for (const zl::game::BuildableDef& def : zl::game::DefaultBuildables()) {
        if (def.id == "campfire") {
            campfireDef = &def;
            break;
        }
    }
    if (campfireDef == nullptr) {
        return 0;
    }
    zl::world::GridSquare* square =
        host.Simulation().World().GetSquare(wx, wy, 0);
    if (square == nullptr) {
        return 0;
    }
    zl::world::SquareObject object =
        zl::game::MakeBuiltObject(*campfireDef, false);
    square->AddObject(campfireDef->layer, std::move(object));
    return 1;
}

std::uint64_t SpawnBed(zl::server::ServerHost& host, float x, float y,
                       std::uint64_t ownerId) {
    const zl::game::BuildableDef* bedDef = nullptr;
    for (const zl::game::BuildableDef& def : zl::game::DefaultBuildables()) {
        if (def.id == "bed") {
            bedDef = &def;
            break;
        }
    }
    if (bedDef == nullptr) {
        return 0;
    }
    zl::world::GridSquare* square = host.Simulation().World().GetSquare(
        static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)), 0);
    if (square == nullptr) {
        return 0;
    }
    zl::world::SquareObject object = zl::game::MakeBuiltObject(*bedDef, false);
    object.ownerStableId = ownerId;
    square->AddObject(bedDef->layer, std::move(object));
    return 1;
}

}  // namespace

TEST_CASE("M13-T5 双客户端：移动/战斗广播、离开清理、重连恢复") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;  // 系统分配临时端口
    hostOptions.contentHash = 0x5A4C544E455431ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkEnabled());
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;
    clientOptions.localPredictionSpeed = 4.0f;

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
    CHECK(firstId != secondId);

    // 双方都拿到含对方玩家的首个/后续快照。
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        return first.Replication().Find(secondId) != nullptr &&
               second.Replication().Find(firstId) != nullptr;
    }));

    const zl::ecs::Entity serverFirst =
        host.Simulation().Ecs().FindById(firstId);
    const zl::ecs::Entity serverSecond =
        host.Simulation().Ecs().FindById(secondId);
    REQUIRE(static_cast<bool>(serverFirst != entt::null));
    REQUIRE(static_cast<bool>(serverSecond != entt::null));
    const float firstStartX =
        host.Simulation().Ecs().Registry().get<zl::ecs::Position>(serverFirst).x;

    // 客户端 A 提交移动；服务器模拟移动，B 的快照里看到 A 的新位置。
    float secondSeesFirstBefore = 0.0f;
    if (const auto* actor = second.Replication().Find(firstId);
        actor != nullptr && actor->hasPosition) {
        secondSeesFirstBefore = actor->position.x;
    }
    REQUIRE(first.SendCommand(MakeMove(firstId, 1.0f, 0.0f), &error));
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        const auto& registry = host.Simulation().Ecs().Registry();
        const auto& position = registry.get<zl::ecs::Position>(serverFirst);
        const auto* actor = second.Replication().Find(firstId);
        return position.x > firstStartX + 0.25f && actor != nullptr &&
               actor->hasPosition && actor->position.x > secondSeesFirstBefore + 0.1f;
    }));

    REQUIRE(first.SendCommand(MakeMove(firstId, 0.0f, 0.0f), &error));
    REQUIRE(PumpUntil(host, first, second, 1000, [&] {
        const auto& velocity =
            host.Simulation().Ecs().Registry().get<zl::ecs::Velocity>(serverFirst);
        return std::abs(velocity.x) < 0.01f && std::abs(velocity.y) < 0.01f;
    }));

    // M13-T10：服务器权威装备/动作经 ZLR1 复制到另一个客户端。
    auto& firstEquipment =
        host.Simulation().Ecs().Registry().get<zl::ecs::Equipment>(serverFirst);
    firstEquipment.slots[0] = zl::ecs::ItemInstance{"axe", 1, 0.75f};
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        const auto* actor = second.Replication().Find(firstId);
        return actor != nullptr && actor->hasEquipment &&
               actor->equipment[0].has_value() &&
               actor->equipment[0]->defId == "axe";
    }));

    // 服务器生成近身目标；A 攻击后服务器扣血，B 能收到 Health 变化。
    const auto& serverRegistry = host.Simulation().Ecs().Registry();
    const auto& firstPosition =
        serverRegistry.get<zl::ecs::Position>(serverFirst);
    const zl::ecs::Entity zombie = host.Simulation().SpawnZombieAt(
        firstPosition.x + 1.0f, firstPosition.y);
    REQUIRE(static_cast<bool>(zombie != entt::null));
    const std::uint64_t zombieId = host.Simulation().Ecs().IdOf(zombie).id;
    REQUIRE(zombieId != 0);
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        return second.Replication().Find(zombieId) != nullptr;
    }));

    zl::net::Command attack;
    attack.type = zl::net::CommandType::Attack;
    attack.playerStableId = firstId;
    attack.payload.entityId = zombieId;
    REQUIRE(first.SendCommand(attack, &error));
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        const auto& health =
            host.Simulation().Ecs().Registry().get<zl::ecs::Health>(zombie);
        const auto* actor = second.Replication().Find(zombieId);
        return health.current < health.max - 1.0f && actor != nullptr &&
               actor->hasHealth && actor->health.current < actor->health.max - 1.0f;
    }));

    // M19-F1：服务器权威命中经验通过 PlayerState 以只读镜像下发。
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        return first.HasPlayerState() && first.PlayerState().progressionXp > 0.0f;
    }));
    CHECK(first.PlayerState().skills[0].id == "melee");

    // M20-F1：服务器权威感染状态经 ZLP1 只读镜像到客户端。
    auto& serverInfection =
        host.Simulation().Ecs().Registry().get_or_emplace<zl::ecs::Infection>(
            serverFirst);
    serverInfection.stage = zl::ecs::InfectionStage::Fever;
    serverInfection.progress = 0.50f;
    serverInfection.timeToNextStage = 100.0f;
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        return first.HasPlayerState() &&
               first.PlayerState().infectionStage == 2;
    }));
    CHECK(first.PlayerState().infectionProgress >= 0.49f);
    CHECK(first.PlayerState().infectionTimeToNextStage > 0.0f);

    // M20-F2：服务器权威体温/潮湿经 ZLP1 v5 只读镜像。
    auto& serverClimate =
        host.Simulation().Ecs().Registry().get_or_emplace<zl::ecs::Climate>(
            serverFirst);
    serverClimate.bodyC = 34.5f;
    serverClimate.wetness = 0.80f;
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        return first.HasPlayerState() && first.PlayerState().climateBodyC < 35.5f &&
               first.PlayerState().climateWetness > 0.5f;
    }));

    // M19-F1：Perk 选择走服务器命令，客户端只读等待 PlayerState 更新。
    auto& serverProgression =
        host.Simulation().Ecs().Registry().get<zl::ecs::Progression>(serverFirst);
    serverProgression.level = 3;
    serverProgression.perkPoints = 1;
    zl::net::Command choosePerk;
    choosePerk.type = zl::net::CommandType::ChoosePerk;
    choosePerk.playerStableId = firstId;
    choosePerk.payload.index = 0;
    REQUIRE(first.SendCommand(choosePerk, &error));
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        return first.HasPlayerState() && first.PlayerState().perkPoints == 0 &&
               first.PlayerState().perks.size() == 1;
    }));

    REQUIRE(PumpUntil(host, first, second, 2000, [&] {
        const auto* actor = second.Replication().Find(firstId);
        return actor != nullptr && actor->hasAction &&
               (actor->action.action == zl::ecs::ActorAction::Attacking ||
                actor->action.action == zl::ecs::ActorAction::Hit);
    }));

    // B 主动离开：服务器销毁其权威实体，A 收到 removal。
    second.Close();
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        return host.SessionCount() == 1 && first.Replication().Find(secondId) == nullptr;
    }));

    // 新客户端重连：获得新 StableId，A 看到新玩家，新客户端看到 A。
    zl::game::NetworkClient reconnected(clientOptions);
    REQUIRE(reconnected.Connect(&error));
    REQUIRE(PumpUntil(host, first, reconnected, 6000, [&] {
        return reconnected.IsConnected() && reconnected.HasPlayer() &&
               host.SessionCount() == 2;
    }));
    const std::uint64_t reconnectedId = reconnected.PlayerStableId();
    REQUIRE(reconnectedId != 0);
    CHECK(reconnectedId != secondId);
    REQUIRE(PumpUntil(host, first, reconnected, 5000, [&] {
        return first.Replication().Find(reconnectedId) != nullptr &&
               reconnected.Replication().Find(firstId) != nullptr;
    }));

    reconnected.Close();
    first.Close();
    host.Shutdown();
    CHECK_FALSE(host.Running());
}

TEST_CASE("M13-T6 延迟命令、服务器重启与自动重连") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x544E4553543632ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);
    const std::uint16_t port = host.NetworkPort();

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = port;
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = true;
    clientOptions.reconnectDelayMs = 50;
    clientOptions.handshakeTimeoutMs = 300;
    clientOptions.heartbeatIntervalMs = 100;
    clientOptions.heartbeatTimeoutMs = 400;

    zl::game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.IsConnected() && client.HasPlayer();
    }));
    const std::uint64_t playerId = client.PlayerStableId();
    REQUIRE(playerId != 0);
    const zl::ecs::Entity player = host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));
    const float startX = host.Simulation().Ecs().Registry().get<zl::ecs::Position>(player).x;

    // 服务器延迟 150ms 才处理命令：命令应留在 KCP 队列并在恢复 tick 后生效。
    REQUIRE(client.SendCommand(MakeMove(playerId, 1.0f, 0.0f), &error));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    host.RunForTicks(12);
    CHECK(host.Simulation().Ecs().Registry().get<zl::ecs::Position>(player).x >
          startX + 0.02f);
    client.Poll();

    // 停掉服务器并在同一端口重启；客户端应自动重连并重新获得玩家分配。
    host.Shutdown();
    zl::server::ServerHost restarted;
    hostOptions.port = port;
    REQUIRE_MESSAGE(restarted.Init(hostOptions, &error), error);
    REQUIRE(PumpUntil(restarted, client, 8000, [&] {
        return client.IsConnected() && client.HasPlayer() &&
               restarted.SessionCount() == 1;
    }));
    CHECK(client.GetStats().connectAttempts >= 2);
    CHECK(client.GetStats().assignments >= 2);
    CHECK(client.PlayerStableId() != 0);

    restarted.Shutdown();
    client.Close();
}

TEST_CASE("X-F1 双客户端只读同步服务器玩家建筑格") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x58463153545243ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;

    zl::game::NetworkClient first(clientOptions);
    zl::game::NetworkClient second(clientOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        return first.IsConnected() && second.IsConnected() && first.HasPlayer() &&
               second.HasPlayer() && host.SessionCount() == 2;
    }));
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        return host.Simulation().Chunks().GetStats().loadedChunks > 0;
    }));

    zl::world::GridSquare* square =
        host.Simulation().World().GetSquare(70, 24, 0);
    REQUIRE(square != nullptr);
    zl::world::SquareObject built{"object.wall_h", 0, 0, true};
    built.hp = 100.0f;
    built.playerBuilt = true;
    square->AddObject(zl::world::SquareLayer::Wall, built);

    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return first.GetStats().chunkDiffsReceived > 0 &&
               second.GetStats().chunkDiffsReceived > 0;
    }));

    const auto containsWall = [](std::vector<zl::save::ChunkDiff> diffs) {
        for (const zl::save::ChunkDiff& diff : diffs) {
            for (const zl::save::ChunkSquareDiff& squareDiff : diff.squares) {
                for (const zl::world::SquareObject& object :
                     squareDiff.layers[static_cast<std::size_t>(
                         zl::world::SquareLayer::Wall)]) {
                    if (object.sprite == "object.wall_h" && object.playerBuilt &&
                        object.hp == doctest::Approx(100.0f)) {
                        return true;
                    }
                }
            }
        }
        return false;
    };
    CHECK(containsWall(first.TakeChunkDiffs()));
    CHECK(containsWall(second.TakeChunkDiffs()));

    first.Close();
    second.Close();
    host.Shutdown();
}

TEST_CASE("X-F1 阶段 2 服务器权威建造/维修/拆门/仓库") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x58463141555448ull;

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
        return client.IsConnected() && client.HasPlayer();
    }));
    const std::uint64_t playerId = client.PlayerStableId();
    REQUIRE(playerId != 0);
    const zl::ecs::Entity player =
        host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));
    REQUIRE(PumpUntil(host, client, 4000, [&] {
        return host.Simulation().Chunks().GetStats().loadedChunks > 0;
    }));

    auto& registry = host.Simulation().Ecs().Registry();
    auto& playerContainer = registry.get<zl::ecs::Container>(player);
    playerContainer.items.push_back(zl::ecs::ItemInstance{"wood", 30, 1.0f});

    const auto& wallDef = zl::game::DefaultBuildables()[1];
    const auto freeCell = [&](int* outX, int* outY) {
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
    zl::net::Command placeWall;
    placeWall.type = zl::net::CommandType::PlaceStructure;
    placeWall.playerStableId = playerId;
    placeWall.payload.x = static_cast<float>(wallX);
    placeWall.payload.y = static_cast<float>(wallY);
    placeWall.payload.index = 1;
    REQUIRE(client.SendCommand(placeWall, &error));
    REQUIRE(PumpUntil(host, client, 4000, [&] {
        return wallCountAt(wallX, wallY) == 1;
    }));
    CHECK(zl::items::CountOf(playerContainer, "wood") == 27);
    const zl::world::GridSquare* placedWallSquare =
        host.Simulation().World().GetSquare(wallX, wallY, 0);
    REQUIRE(placedWallSquare != nullptr);
    REQUIRE(!placedWallSquare->Layer(zl::world::SquareLayer::Wall).empty());
    const zl::world::SquareObject& placedWall =
        placedWallSquare->Layer(zl::world::SquareLayer::Wall).front();
    CHECK(placedWall.structureId ==
          zl::game::MakeStructureId(wallX, wallY, 0,
                                    static_cast<int>(wallDef.layer)));
    CHECK(placedWall.ownerStableId == playerId);
    bool clientSawWall = false;
    std::uint64_t clientWallId = 0;
    std::uint64_t clientWallOwner = 0;
    REQUIRE(PumpUntil(host, client, 4000, [&] {
        const auto diffs = client.TakeChunkDiffs();
        for (const zl::save::ChunkDiff& diff : diffs) {
            for (const zl::save::ChunkSquareDiff& squareDiff : diff.squares) {
                for (const zl::world::SquareObject& object :
                     squareDiff.layers[static_cast<std::size_t>(
                         zl::world::SquareLayer::Wall)]) {
                    if (object.playerBuilt && object.sprite == "object.wall_h") {
                        clientSawWall = true;
                        clientWallId = object.structureId;
                        clientWallOwner = object.ownerStableId;
                    }
                }
            }
        }
        return clientSawWall;
    }));
    CHECK(clientWallId == placedWall.structureId);
    CHECK(clientWallOwner == playerId);

    zl::game::NetworkClient second(clientOptions);
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, client, second, 6000, [&] {
        return second.IsConnected() && second.HasPlayer();
    }));
    const std::uint64_t secondId = second.PlayerStableId();
    REQUIRE(secondId != 0);
    REQUIRE(secondId != playerId);
    host.RunForTicks(40);
    client.Poll();
    second.Poll();
    const std::uint64_t ownershipBefore =
        host.GetNetworkStats().ownershipRejected;
    zl::net::Command otherDemolish;
    otherDemolish.type = zl::net::CommandType::DemolishStructure;
    otherDemolish.playerStableId = secondId;
    otherDemolish.payload.x = static_cast<float>(wallX);
    otherDemolish.payload.y = static_cast<float>(wallY);
    REQUIRE(second.SendCommand(otherDemolish, &error));
    REQUIRE(PumpUntil(host, client, second, 3000, [&] {
        return host.GetNetworkStats().ownershipRejected > ownershipBefore;
    }));
    CHECK(wallCountAt(wallX, wallY) == 1);

    zl::world::GridSquare* nonOwnerRepairSquare =
        host.Simulation().World().GetSquare(wallX, wallY, 0);
    REQUIRE(nonOwnerRepairSquare != nullptr);
    REQUIRE(!nonOwnerRepairSquare->Layer(zl::world::SquareLayer::Wall).empty());
    nonOwnerRepairSquare->Layer(zl::world::SquareLayer::Wall).front().hp =
        50.0f;
    const std::uint64_t repairOwnershipBefore =
        host.GetNetworkStats().ownershipRejected;
    zl::net::Command otherRepair;
    otherRepair.type = zl::net::CommandType::RepairStructure;
    otherRepair.playerStableId = secondId;
    otherRepair.payload.x = static_cast<float>(wallX);
    otherRepair.payload.y = static_cast<float>(wallY);
    REQUIRE(second.SendCommand(otherRepair, &error));
    REQUIRE(PumpUntil(host, client, second, 3000, [&] {
        return host.GetNetworkStats().ownershipRejected >
               repairOwnershipBefore;
    }));
    CHECK(nonOwnerRepairSquare->Layer(zl::world::SquareLayer::Wall).front().hp ==
          doctest::Approx(50.0f));
    CHECK(second.PlayerState().status.find("其他玩家") != std::string::npos);

    host.RunForTicks(40);
    client.Poll();
    const std::uint64_t rejectedBefore =
        host.GetNetworkStats().commandsRejected;
    zl::net::Command duplicate = placeWall;
    REQUIRE(client.SendCommand(duplicate, &error));
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        return host.GetNetworkStats().commandsRejected > rejectedBefore;
    }));
    CHECK(wallCountAt(wallX, wallY) == 1);

    host.RunForTicks(40);
    client.Poll();
    zl::world::GridSquare* wallSquare =
        host.Simulation().World().GetSquare(wallX, wallY, 0);
    REQUIRE(wallSquare != nullptr);
    REQUIRE(!wallSquare->Layer(zl::world::SquareLayer::Wall).empty());
    wallSquare->Layer(zl::world::SquareLayer::Wall).front().hp = 50.0f;
    zl::net::Command repair;
    repair.type = zl::net::CommandType::RepairStructure;
    repair.playerStableId = playerId;
    repair.payload.x = static_cast<float>(wallX);
    repair.payload.y = static_cast<float>(wallY);
    REQUIRE(client.SendCommand(repair, &error));
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        return wallSquare->Layer(zl::world::SquareLayer::Wall)
                   .front()
                   .hp > 50.0f;
    }));
    CHECK(zl::items::CountOf(playerContainer, "wood") == 26);

    host.RunForTicks(40);
    client.Poll();
    const std::uint64_t rangeBefore = host.GetNetworkStats().rangeRejected;
    zl::net::Command farPlace = placeWall;
    farPlace.payload.x = static_cast<float>(wallX + 100);
    REQUIRE(client.SendCommand(farPlace, &error));
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        return host.GetNetworkStats().rangeRejected > rangeBefore;
    }));
    CHECK(zl::items::CountOf(playerContainer, "wood") == 26);

    int doorX = 0;
    int doorY = 0;
    REQUIRE(freeCell(&doorX, &doorY));
    host.RunForTicks(40);
    client.Poll();
    zl::net::Command placeDoor;
    placeDoor.type = zl::net::CommandType::PlaceStructure;
    placeDoor.playerStableId = playerId;
    placeDoor.payload.x = static_cast<float>(doorX);
    placeDoor.payload.y = static_cast<float>(doorY);
    placeDoor.payload.index = 2;
    REQUIRE(client.SendCommand(placeDoor, &error));
    REQUIRE(PumpUntil(host, client, 4000, [&] {
        zl::world::GridSquare* square =
            host.Simulation().World().GetSquare(doorX, doorY, 0);
        if (square == nullptr) {
            return false;
        }
        for (const zl::world::SquareObject& object :
             square->Layer(zl::world::SquareLayer::Wall)) {
            if (object.playerBuilt && object.sprite == "object.door_closed_h") {
                return true;
            }
        }
        return false;
    }));

    host.RunForTicks(40);
    client.Poll();
    zl::net::Command toggle;
    toggle.type = zl::net::CommandType::ToggleDoor;
    toggle.playerStableId = playerId;
    toggle.payload.x = static_cast<float>(doorX);
    toggle.payload.y = static_cast<float>(doorY);
    REQUIRE(client.SendCommand(toggle, &error));
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        zl::world::GridSquare* square =
            host.Simulation().World().GetSquare(doorX, doorY, 0);
        if (square == nullptr) {
            return false;
        }
        for (const zl::world::SquareObject& object :
             square->Layer(zl::world::SquareLayer::Wall)) {
            if (object.playerBuilt && object.sprite == "object.door_open_h" &&
                !object.blocksMovement) {
                return true;
            }
        }
        return false;
    }));

    const auto doorOpenAtCell = [&] {
        const zl::world::GridSquare* square =
            host.Simulation().World().GetSquare(doorX, doorY, 0);
        if (square == nullptr) {
            return false;
        }
        for (const zl::world::SquareObject& object :
             square->Layer(zl::world::SquareLayer::Wall)) {
            if (object.playerBuilt && object.sprite == "object.door_open_h" &&
                !object.blocksMovement) {
                return true;
            }
        }
        return false;
    };
    REQUIRE(doorOpenAtCell());
    const zl::ecs::Entity secondPlayer =
        host.Simulation().Ecs().FindById(secondId);
    REQUIRE(static_cast<bool>(secondPlayer != entt::null));
    auto* secondPosition = registry.try_get<zl::ecs::Position>(secondPlayer);
    REQUIRE(secondPosition != nullptr);
    secondPosition->x = static_cast<float>(doorX) + 0.5f;
    secondPosition->y = static_cast<float>(doorY) + 0.5f;
    const std::uint64_t toggleOwnershipBefore =
        host.GetNetworkStats().ownershipRejected;
    zl::net::Command otherToggle = toggle;
    otherToggle.playerStableId = secondId;
    REQUIRE(second.SendCommand(otherToggle, &error));
    REQUIRE(PumpUntil(host, client, second, 3000, [&] {
        return host.GetNetworkStats().ownershipRejected >
               toggleOwnershipBefore;
    }));
    CHECK(doorOpenAtCell());
    second.Close();

    host.RunForTicks(40);
    client.Poll();
    zl::net::Command demolish;
    demolish.type = zl::net::CommandType::DemolishStructure;
    demolish.playerStableId = playerId;
    demolish.payload.x = static_cast<float>(wallX);
    demolish.payload.y = static_cast<float>(wallY);
    demolish.payload.flags = zl::net::kDemolishConfirmRequestFlag;
    REQUIRE(client.SendCommand(demolish, &error));
    REQUIRE(PumpUntil(host, client, 2000, [&] {
        return client.PlayerState().status.find("再次按 X 确认拆除") !=
               std::string::npos;
    }));
    CHECK(wallCountAt(wallX, wallY) == 1);
    CHECK(zl::items::CountOf(playerContainer, "wood") == 23);
    demolish.payload.flags = zl::net::kDemolishConfirmAcceptFlag;
    REQUIRE(client.SendCommand(demolish, &error));
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        return wallCountAt(wallX, wallY) == 0;
    }));
    CHECK(zl::items::CountOf(playerContainer, "wood") == 25);

    const zl::world::ChunkCoord wallChunk = zl::world::ChunkOf(wallX, wallY);
    const zl::world::SquareLocal wallLocal =
        zl::world::SquareLocalOf(wallX, wallY);
    bool clientSawWallRemoval = false;
    REQUIRE(PumpUntil(host, client, 4000, [&] {
        for (const zl::save::ChunkDiff& diff : client.TakeChunkDiffs()) {
            if (diff.chunkX != wallChunk.cx || diff.chunkY != wallChunk.cy) {
                continue;
            }
            for (const zl::save::ChunkSquareDiff& squareDiff : diff.squares) {
                if (squareDiff.lx != wallLocal.lx ||
                    squareDiff.ly != wallLocal.ly || squareDiff.z != 0) {
                    continue;
                }
                const auto& walls = squareDiff.layers[static_cast<std::size_t>(
                    zl::world::SquareLayer::Wall)];
                const bool hasWall = std::any_of(
                    walls.begin(), walls.end(), [](const zl::world::SquareObject& object) {
                        return object.playerBuilt && object.sprite == "object.wall_h";
                    });
                if (!hasWall) {
                    clientSawWallRemoval = true;
                }
            }
        }
        return clientSawWallRemoval;
    }));
    CHECK(clientSawWallRemoval);

    int storageX = 0;
    int storageY = 0;
    REQUIRE(freeCell(&storageX, &storageY));
    host.RunForTicks(40);
    client.Poll();
    zl::net::Command placeStorage;
    placeStorage.type = zl::net::CommandType::PlaceStructure;
    placeStorage.playerStableId = playerId;
    placeStorage.payload.x = static_cast<float>(storageX);
    placeStorage.payload.y = static_cast<float>(storageY);
    placeStorage.payload.index = 8;
    REQUIRE(client.SendCommand(placeStorage, &error));
    zl::ecs::Entity storage = entt::null;
    REQUIRE(PumpUntil(host, client, 4000, [&] {
        registry.view<zl::ecs::Position, zl::ecs::PlayerStorage>().each(
            [&](const zl::ecs::Entity entity,
                const zl::ecs::Position& position,
                const zl::ecs::PlayerStorage&) {
                if (static_cast<int>(std::floor(position.x)) == storageX &&
                    static_cast<int>(std::floor(position.y)) == storageY) {
                    storage = entity;
                }
            });
        return storage != entt::null;
    }));
    const std::uint64_t storageId = host.Simulation().Ecs().IdOf(storage).id;
    REQUIRE(storageId != 0);
    REQUIRE(PumpUntil(host, client, 4000, [&] {
        const auto* actor = client.Replication().Find(storageId);
        return actor != nullptr && actor->hasSprite;
    }));
    const auto* storageOwner =
        registry.try_get<zl::ecs::StructureOwner>(storage);
    REQUIRE(storageOwner != nullptr);
    CHECK(storageOwner->ownerStableId == playerId);
    const auto* storageActor = client.Replication().Find(storageId);
    REQUIRE(storageActor != nullptr);
    CHECK(storageActor->ownerStableId == playerId);
    auto* storageContainer = registry.try_get<zl::ecs::Container>(storage);
    REQUIRE(storageContainer != nullptr);
    storageContainer->items.push_back(
        zl::ecs::ItemInstance{"wood", 2, 1.0f});

    zl::game::NetworkClient intruder(clientOptions);
    REQUIRE(intruder.Connect(&error));
    REQUIRE(PumpUntil(host, client, intruder, 6000, [&] {
        return intruder.IsConnected() && intruder.HasPlayer();
    }));
    const std::uint64_t intruderId = intruder.PlayerStableId();
    REQUIRE(intruderId != 0);
    REQUIRE(intruderId != playerId);
    host.RunForTicks(40);
    client.Poll();
    intruder.Poll();
    REQUIRE(PumpUntil(host, client, intruder, 4000, [&] {
        const auto* actor = intruder.Replication().Find(storageId);
        return actor != nullptr && actor->hasSprite;
    }));
    const auto* intruderStorageActor = intruder.Replication().Find(storageId);
    REQUIRE(intruderStorageActor != nullptr);
    CHECK_FALSE(intruderStorageActor->hasStorage);
    CHECK(intruderStorageActor->ownerStableId == playerId);
    const std::uint64_t storageOwnershipBefore =
        host.GetNetworkStats().ownershipRejected;
    zl::net::Command otherDemolishStorage;
    otherDemolishStorage.type = zl::net::CommandType::DemolishStructure;
    otherDemolishStorage.playerStableId = intruderId;
    otherDemolishStorage.payload.x = static_cast<float>(storageX);
    otherDemolishStorage.payload.y = static_cast<float>(storageY);
    REQUIRE(intruder.SendCommand(otherDemolishStorage, &error));
    REQUIRE(PumpUntil(host, client, intruder, 3000, [&] {
        return host.GetNetworkStats().ownershipRejected >
               storageOwnershipBefore;
    }));
    CHECK(client.Replication().Find(storageId) != nullptr);

    const zl::ecs::Entity intruderPlayer =
        host.Simulation().Ecs().FindById(intruderId);
    REQUIRE(static_cast<bool>(intruderPlayer != entt::null));
    auto* intruderPosition =
        registry.try_get<zl::ecs::Position>(intruderPlayer);
    REQUIRE(intruderPosition != nullptr);
    intruderPosition->x = static_cast<float>(storageX) + 0.5f;
    intruderPosition->y = static_cast<float>(storageY) + 0.5f;
    const std::size_t storageItemsBefore = storageContainer->items.size();
    zl::net::Command otherTakeAll;
    otherTakeAll.type = zl::net::CommandType::InventoryAction;
    otherTakeAll.playerStableId = intruderId;
    otherTakeAll.payload.flags =
        static_cast<std::uint16_t>(zl::ui::InventoryActionType::TakeAll) |
        (1u << 8);
    otherTakeAll.payload.area =
        static_cast<std::uint8_t>(zl::ui::InventoryArea::Loot);
    otherTakeAll.payload.targetArea =
        static_cast<std::uint8_t>(zl::ui::InventoryArea::Backpack);
    otherTakeAll.payload.entityId = storageId;
    REQUIRE(intruder.SendCommand(otherTakeAll, &error));
    REQUIRE(PumpUntil(host, client, intruder, 3000, [&] {
        return intruder.PlayerState().status.find("没有可访问的仓库") !=
               std::string::npos;
    }));
    CHECK(storageContainer->items.size() == storageItemsBefore);
    CHECK(zl::items::CountOf(*storageContainer, "wood") == 2);

    auto* intruderContainer =
        registry.try_get<zl::ecs::Container>(intruderPlayer);
    REQUIRE(intruderContainer != nullptr);
    intruderContainer->items.push_back(
        zl::ecs::ItemInstance{"bandage", 1, 1.0f});
    zl::net::Command invalidUse;
    invalidUse.type = zl::net::CommandType::UseBag;
    invalidUse.playerStableId = intruderId;
    invalidUse.payload.index = 99;
    REQUIRE(intruder.SendCommand(invalidUse, &error));
    REQUIRE(PumpUntil(host, client, intruder, 3000, [&] {
        return intruder.PlayerState().status.find("背包槽不存在") !=
               std::string::npos;
    }));
    zl::net::Command otherStoreAll = otherTakeAll;
    otherStoreAll.payload.flags =
        static_cast<std::uint16_t>(zl::ui::InventoryActionType::StoreAll) |
        (1u << 8);
    REQUIRE(intruder.SendCommand(otherStoreAll, &error));
    REQUIRE(PumpUntil(host, client, intruder, 3000, [&] {
        return intruder.PlayerState().status.find("没有可访问的仓库") !=
               std::string::npos;
    }));
    CHECK(storageContainer->items.size() == storageItemsBefore);
    CHECK(zl::items::CountOf(*storageContainer, "wood") == 2);
    CHECK(zl::items::CountOf(*intruderContainer, "bandage") == 1);
    zl::net::Command otherTransfer = otherTakeAll;
    otherTransfer.payload.flags =
        static_cast<std::uint16_t>(zl::ui::InventoryActionType::Transfer) |
        (1u << 8);
    otherTransfer.payload.index = 0;
    otherTransfer.payload.targetIndex = 0;
    REQUIRE(intruder.SendCommand(otherTransfer, &error));
    REQUIRE(PumpUntil(host, client, intruder, 3000, [&] {
        return intruder.PlayerState().status.find(
                   "仓库距离太远或无权访问") != std::string::npos;
    }));
    CHECK(storageContainer->items.size() == storageItemsBefore);
    CHECK(zl::items::CountOf(*storageContainer, "wood") == 2);
    CHECK(zl::items::CountOf(*intruderContainer, "wood") == 0);
    intruder.Close();

    host.RunForTicks(40);
    client.Poll();
    zl::net::Command demolishStorage;
    demolishStorage.type = zl::net::CommandType::DemolishStructure;
    demolishStorage.playerStableId = playerId;
    demolishStorage.payload.x = static_cast<float>(storageX);
    demolishStorage.payload.y = static_cast<float>(storageY);
    REQUIRE(client.SendCommand(demolishStorage, &error));
    REQUIRE(PumpUntil(host, client, 4000, [&] {
        return client.Replication().Find(storageId) == nullptr;
    }));
    CHECK(zl::game::CountGroundItems(host.Simulation().Ecs()) >= 1);

    client.Close();
    host.Shutdown();
}

TEST_CASE("M21-F1b 双客户端守夜：服务器方向/破口状态、Breaker 目标与每玩家奖励") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const char* args[] = {
        "ZomboidLikeTests",
        "--game.startHour=22",
        "--baseEvent.warningSeconds=0",
        "--baseEvent.waves=[1]",
        "--baseEvent.maxAlive=1",
        "--baseEvent.specialStartWave=0",
        "--baseEvent.specialCount=1",
        "--baseEvent.spawnRadiusMin=4",
        "--baseEvent.spawnRadiusMax=6",
    };
    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.argc = 9;
    hostOptions.argv = args;
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x5349454745463162ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

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
    wall.structureId =
        zl::game::MakeStructureId(72, 24, 0, 1);
    wallSquare->AddObject(zl::world::SquareLayer::Wall, wall);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;

    zl::game::NetworkClient first(clientOptions);
    zl::game::NetworkClient second(clientOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayerState() && second.HasPlayerState();
    }));

    REQUIRE(PumpUntil(host, first, second, 10000, [&] {
        const zl::net::PlayerSiegeState& a = first.PlayerState().siege;
        const zl::net::PlayerSiegeState& b = second.PlayerState().siege;
        return a.phase == 2 && b.phase == 2 && a.status == b.status &&
               !a.status.empty();
    }));
    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity anchor =
        host.Simulation().Ecs().FindById(first.PlayerStableId());
    REQUIRE(static_cast<bool>(anchor != entt::null));
    const auto& anchorPosition = registry.get<zl::ecs::Position>(anchor);
    const int expectedDirection = zl::game::BaseSiegeDirectionFromDelta(
        72.5f - anchorPosition.x, 24.5f - anchorPosition.y);

    const zl::net::PlayerSiegeState& firstSiege = first.PlayerState().siege;
    const zl::net::PlayerSiegeState& secondSiege = second.PlayerState().siege;
    CHECK(firstSiege.waveCount == 1);
    CHECK(firstSiege.waveIndex == 0);
    CHECK(firstSiege.primaryDirection == expectedDirection);
    CHECK(firstSiege.currentDirection == firstSiege.primaryDirection);
    CHECK(firstSiege.primaryDirection == secondSiege.primaryDirection);
    CHECK(firstSiege.currentDirection == secondSiege.currentDirection);
    CHECK(firstSiege.waveCount == secondSiege.waveCount);
    CHECK(firstSiege.waveIndex == secondSiege.waveIndex);
    CHECK(firstSiege.targetValid);
    CHECK(firstSiege.targetX == doctest::Approx(72.5f).epsilon(0.01));
    CHECK(firstSiege.targetY == doctest::Approx(24.5f).epsilon(0.01));
    CHECK_FALSE(firstSiege.breachValid);
    CHECK(firstSiege.status == secondSiege.status);
    CHECK(firstSiege.status.find(
              std::string("来袭 ") +
              zl::game::BaseSiegeDirectionName(expectedDirection)) !=
          std::string::npos);
    CHECK(firstSiege.status.find("守夜 第 1/1 波") != std::string::npos);

    zl::ecs::Entity breaker = entt::null;
    zl::ecs::SiegeAssaultTarget assault;
    registry.view<zl::ecs::BaseSiegeZombie, zl::ecs::ZombieSpecial,
                  zl::ecs::SiegeAssaultTarget>()
        .each([&](const zl::ecs::Entity entity,
                  const zl::ecs::ZombieSpecial& special,
                  const zl::ecs::SiegeAssaultTarget& target) {
            if (special.kind == zl::ecs::ZombieSpecialKind::Breaker) {
                breaker = entity;
                assault = target;
            }
        });
    REQUIRE(static_cast<bool>(breaker != entt::null));
    CHECK(assault.active);
    CHECK(std::hypot(assault.x - 72.5f, assault.y - 24.5f) <= 3.0f);

    const int firstFuelBefore = BagCount(first.PlayerState(), "fuel_can");
    const int secondFuelBefore = BagCount(second.PlayerState(), "fuel_can");
    const int firstRepairBefore = BagCount(first.PlayerState(), "repair_kit");
    const int secondRepairBefore = BagCount(second.PlayerState(), "repair_kit");
    const int firstMedicineBefore =
        BagCount(first.PlayerState(), "antibiotics");
    const int secondMedicineBefore =
        BagCount(second.PlayerState(), "antibiotics");

    std::vector<zl::ecs::Entity> eventZombies;
    registry.view<zl::ecs::BaseSiegeZombie>().each(
        [&](const zl::ecs::Entity entity) { eventZombies.push_back(entity); });
    REQUIRE_FALSE(eventZombies.empty());
    for (const zl::ecs::Entity zombie : eventZombies) {
        host.Simulation().DespawnEntity(zombie);
    }

    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        const zl::net::PlayerState& a = first.PlayerState();
        const zl::net::PlayerState& b = second.PlayerState();
        constexpr std::uint8_t finished =
            static_cast<std::uint8_t>(zl::game::BaseSiegePhase::Finished);
        return a.siege.phase == finished && b.siege.phase == finished &&
               BagCount(a, "fuel_can") == firstFuelBefore + 1 &&
               BagCount(b, "fuel_can") == secondFuelBefore + 1 &&
               BagCount(a, "repair_kit") == firstRepairBefore + 1 &&
               BagCount(b, "repair_kit") == secondRepairBefore + 1 &&
               BagCount(a, "antibiotics") == firstMedicineBefore + 1 &&
               BagCount(b, "antibiotics") == secondMedicineBefore + 1;
    }));
    CHECK(first.PlayerState().status.find("评级 S") != std::string::npos);
    CHECK(second.PlayerState().status.find("评级 S") != std::string::npos);
    CHECK(first.PlayerState().status.find("奖励") != std::string::npos);
    CHECK(second.PlayerState().status.find("奖励") != std::string::npos);
    CHECK(first.PlayerState().status == second.PlayerState().status);

    const int firstFuelAfter = BagCount(first.PlayerState(), "fuel_can");
    const int secondFuelAfter = BagCount(second.PlayerState(), "fuel_can");
    const std::uint64_t tickBefore = first.PlayerState().serverTick;
    host.RunForTicks(120);
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        return first.PlayerState().serverTick >= tickBefore + 30 &&
               second.PlayerState().serverTick >= tickBefore + 30;
    }));
    CHECK(BagCount(first.PlayerState(), "fuel_can") == firstFuelAfter);
    CHECK(BagCount(second.PlayerState(), "fuel_can") == secondFuelAfter);

    first.Close();
    second.Close();
    host.Shutdown();
}

TEST_CASE("M21-F2b 联机电力：服务器供电、设备复制与交互") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const char* args[] = {
        "ZomboidLikeTests",
        "--game.startHour=22",
        "--baseEvent.enabled=false",
    };
    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.argc = 3;
    hostOptions.argv = args;
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x504F574552463262ull;

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
        return client.IsConnected() && client.HasPlayer();
    }));
    const std::uint64_t playerId = client.PlayerStableId();
    REQUIRE(playerId != 0);
    const zl::ecs::Entity player =
        host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));

    auto& registry = host.Simulation().Ecs().Registry();
    auto& playerContainer = registry.get<zl::ecs::Container>(player);
    playerContainer.items.push_back(zl::ecs::ItemInstance{"wood", 30, 1.0f});
    playerContainer.items.push_back(
        zl::ecs::ItemInstance{"fuel_can", 1, 1.0f});

    const auto& defs = zl::game::DefaultBuildables();
    const auto findDef = [&](std::string_view id) -> const zl::game::BuildableDef* {
        for (const zl::game::BuildableDef& def : defs) {
            if (def.id == id) {
                return &def;
            }
        }
        return nullptr;
    };
    const auto findFreeCell = [&](const zl::game::BuildableDef& def, int* outX,
                                  int* outY) {
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

    const zl::game::BuildableDef* generatorDef = findDef("generator");
    const zl::game::BuildableDef* floodlightDef = findDef("floodlight");
    const zl::game::BuildableDef* fenceDef = findDef("electric_fence");
    REQUIRE(generatorDef != nullptr);
    REQUIRE(floodlightDef != nullptr);
    REQUIRE(fenceDef != nullptr);

    int generatorX = 0;
    int generatorY = 0;
    place(*generatorDef, &generatorX, &generatorY);
    zl::ecs::Entity generator = entt::null;
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        registry.view<zl::ecs::Generator, zl::ecs::Position>().each(
            [&](const zl::ecs::Entity entity,
                const zl::ecs::Generator&,
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
            [&](const zl::ecs::Entity entity,
                const zl::ecs::Floodlight&,
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

    int fenceX = 0;
    int fenceY = 0;
    place(*fenceDef, &fenceX, &fenceY);
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        const zl::world::GridSquare* square =
            host.Simulation().World().GetSquare(fenceX, fenceY, 0);
        if (square == nullptr) {
            return false;
        }
        for (const zl::world::SquareLayer layer :
             {zl::world::SquareLayer::Wall, zl::world::SquareLayer::Furniture}) {
            for (const zl::world::SquareObject& object :
                 square->Layer(layer)) {
                if (object.playerBuilt &&
                    object.sprite == zl::game::kElectricFenceSprite &&
                    object.hp > 0.0f) {
                    return true;
                }
            }
        }
        return false;
    }));

    zl::net::Command startGenerator;
    startGenerator.type = zl::net::CommandType::InteractFacility;
    startGenerator.playerStableId = playerId;
    startGenerator.payload.entityId = generatorId;
    REQUIRE(client.SendCommand(startGenerator, &error));
    host.RunForTicks(180);
    client.Poll();
    const auto& powerState = client.PlayerState().power;
    REQUIRE(registry.get<zl::ecs::Generator>(generator).running);
    REQUIRE(powerState.load >= 2.45f);
    REQUIRE_FALSE(powerState.outage);
    REQUIRE(powerState.fuelMinutes > 200.0f);
    const zl::ecs::Generator& runningGenerator =
        registry.get<zl::ecs::Generator>(generator);
    CHECK(runningGenerator.load >= 1.49f);
    CHECK(registry.get<zl::ecs::Floodlight>(floodlight).powered);
    CHECK(zl::items::CountOf(playerContainer, "fuel_can") == 0);

    const auto* generatorActor = client.Replication().Find(generatorId);
    REQUIRE(generatorActor != nullptr);
    REQUIRE(generatorActor->hasGenerator);
    CHECK(generatorActor->generator.running);
    CHECK(generatorActor->generator.load >= 1.49f);
    const auto* floodlightActor = client.Replication().Find(floodlightId);
    REQUIRE(floodlightActor != nullptr);
    REQUIRE(floodlightActor->hasFloodlight);
    CHECK(floodlightActor->floodlight.on);
    CHECK(floodlightActor->floodlight.powered);

    client.Poll();
    zl::net::Command toggleFloodlight;
    toggleFloodlight.type = zl::net::CommandType::InteractFacility;
    toggleFloodlight.playerStableId = playerId;
    toggleFloodlight.payload.entityId = floodlightId;
    REQUIRE(client.SendCommand(toggleFloodlight, &error));
    REQUIRE(PumpUntil(host, client, 4000, [&] {
        return !registry.get<zl::ecs::Floodlight>(floodlight).on &&
               client.PlayerState().power.load <= 2.05f;
    }));
    CHECK_FALSE(registry.get<zl::ecs::Floodlight>(floodlight).powered);
    const auto* toggledActor = client.Replication().Find(floodlightId);
    REQUIRE(toggledActor != nullptr);
    REQUIRE(toggledActor->hasFloodlight);
    CHECK_FALSE(toggledActor->floodlight.on);
    CHECK_FALSE(toggledActor->floodlight.powered);

    const int woodBeforeRepair = zl::items::CountOf(playerContainer, "wood");
    registry.get<zl::ecs::Health>(floodlight).current = 20.0f;
    REQUIRE(PumpUntil(host, client, 4000, [&] {
        const auto* actor = client.Replication().Find(floodlightId);
        return actor != nullptr && actor->hasHealth &&
               actor->health.current == doctest::Approx(20.0f);
    }));
    zl::net::Command repairFloodlight;
    repairFloodlight.type = zl::net::CommandType::RepairStructure;
    repairFloodlight.playerStableId = playerId;
    repairFloodlight.payload.x = static_cast<float>(floodlightX);
    repairFloodlight.payload.y = static_cast<float>(floodlightY);
    REQUIRE(client.SendCommand(repairFloodlight, &error));
    REQUIRE(PumpUntil(host, client, 4000, [&] {
        const auto* actor = client.Replication().Find(floodlightId);
        return registry.get<zl::ecs::Health>(floodlight).current ==
                   doctest::Approx(50.0f) &&
               actor != nullptr && actor->hasHealth &&
               actor->health.current == doctest::Approx(50.0f);
    }));
    CHECK(zl::items::CountOf(playerContainer, "wood") ==
          woodBeforeRepair - 1);

    client.Close();
    host.Shutdown();
}
TEST_CASE("M21-F2b 服务器电网：供电夜间事件僵尸电击，普通僵尸不触发") {
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
        "--baseEvent.spawnRadiusMin=2",
        "--baseEvent.spawnRadiusMax=3",
        "--baseEvent.minStructures=0",
    };
    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.argc = 9;
    hostOptions.argv = args;
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = true;
    hostOptions.network = false;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(PumpHostUntil(host, 8000, [&] {
        return host.Simulation().Player() != entt::null;
    }));
    host.RunForTicks(60);
    REQUIRE(host.Simulation().World().GetSquare(67, 23, 0) != nullptr);
    REQUIRE(host.Simulation().World().GetSquare(69, 21, 0) != nullptr);

    auto& registry = host.Simulation().Ecs().Registry();
    const auto& defs = zl::game::DefaultBuildables();
    const auto findDef = [&](std::string_view id) -> const zl::game::BuildableDef* {
        for (const zl::game::BuildableDef& def : defs) {
            if (def.id == id) {
                return &def;
            }
        }
        return nullptr;
    };
    const zl::game::BuildableDef* fenceDef = findDef("electric_fence");
    REQUIRE(fenceDef != nullptr);
    zl::world::GridSquare* fenceSquare =
        host.Simulation().World().GetSquare(67, 23, 0);
    zl::world::SquareObject fence =
        zl::game::MakeBuiltObject(*fenceDef, false);
    fence.structureId =
        zl::game::MakeStructureId(67, 23, 0,
                                  static_cast<int>(fenceDef->layer));
    fence.ownerStableId = 1;
    fenceSquare->AddObject(fenceDef->layer, std::move(fence));

    const zl::ecs::Entity generator = host.Simulation().Ecs().Create();
    registry.emplace<zl::ecs::Position>(generator,
                                        zl::ecs::Position{69.5f, 21.5f, 0});
    registry.emplace<zl::ecs::SpriteRef>(
        generator,
        zl::ecs::SpriteRef{zl::ecs::HashName("object.generator"), 0,
                           0xFFFFFFFFu});
    registry.emplace<zl::ecs::Generator>(
        generator, zl::ecs::Generator{720.0f, 8.0f, true, 0.0f});

    host.RunForTicks(60);
    REQUIRE(registry.get<zl::ecs::Generator>(generator).load >= 0.99f);

    const auto makeZombie = [&](bool siege, float x, float y) {
        const zl::ecs::Entity zombie = host.Simulation().Ecs().Create();
        registry.emplace<zl::ecs::Position>(zombie,
                                            zl::ecs::Position{x, y, 0});
        registry.emplace<zl::ecs::Health>(zombie,
                                          zl::ecs::Health{100.0f, 100.0f});
        registry.emplace<zl::ecs::AiState>(zombie, zl::ecs::AiState{});
        if (siege) {
            registry.emplace<zl::ecs::BaseSiegeZombie>(zombie);
        }
        return zombie;
    };
    const zl::ecs::Entity eventZombie = makeZombie(true, 68.5f, 23.5f);
    const zl::ecs::Entity normalZombie = makeZombie(false, 68.5f, 23.5f);

    host.RunForTicks(4);
    CHECK(registry.get<zl::ecs::Health>(eventZombie).current < 100.0f);
    CHECK(registry.get<zl::ecs::Health>(normalZombie).current ==
          doctest::Approx(100.0f));

    host.Shutdown();
}

TEST_CASE("M22-F1 双客户端：睡眠投票、服务器时间推进与状态同步") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D32324631534C50ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkEnabled());
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;
    zl::game::NetworkClient first(clientOptions);
    zl::game::NetworkClient second(clientOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayerState() && second.HasPlayerState() &&
               first.HasSocialState() && second.HasSocialState();
    }));

    const std::uint64_t firstId = first.PlayerStableId();
    const std::uint64_t secondId = second.PlayerStableId();
    REQUIRE(firstId != 0);
    REQUIRE(secondId != 0);
    CHECK(firstId != secondId);

    std::vector<zl::ecs::Entity> zombies;
    host.Simulation().Ecs().Registry().view<zl::ecs::AiState>().each(
        [&](const zl::ecs::Entity entity, const zl::ecs::AiState&) {
            zombies.push_back(entity);
        });
    for (const zl::ecs::Entity zombie : zombies) {
        host.Simulation().DespawnEntity(zombie);
    }
    host.RunForTicks(1);

    const zl::ecs::Entity firstPlayer =
        host.Simulation().Ecs().FindById(firstId);
    REQUIRE(static_cast<bool>(firstPlayer != entt::null));
    const auto& firstPosition =
        host.Simulation().Ecs().Registry().get<zl::ecs::Position>(firstPlayer);
    REQUIRE(SpawnBed(host, firstPosition.x + 0.8f, firstPosition.y,
                     firstId) != 0);

    const double beforeSleep = first.SocialState().gameMinutes;
    zl::net::Command request;
    request.type = zl::net::CommandType::SleepRequest;
    request.playerStableId = firstId;
    request.payload.x = 480.0f;
    REQUIRE(first.SendCommand(request, &error));
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseVote &&
               second.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseVote;
    }));
    CHECK(first.PlayerState().sleep.status.find("已同意") != std::string::npos);
    CHECK(second.PlayerState().sleep.status.find("Y 同意") != std::string::npos);

    zl::net::Command vote;
    vote.type = zl::net::CommandType::SleepVote;
    vote.playerStableId = secondId;
    vote.payload.flags = 0;
    REQUIRE(second.SendCommand(vote, &error));
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseNone &&
               second.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseNone;
    }));
    CHECK(first.PlayerState().status.find("拒绝") != std::string::npos);
    CHECK(first.SocialState().gameMinutes < beforeSleep + 5.0);

    REQUIRE(first.SendCommand(request, &error));
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseVote &&
               second.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseVote;
    }));
    vote.payload.flags = zl::net::kSleepVoteYesFlag;
    REQUIRE(second.SendCommand(vote, &error));
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseSleeping &&
               second.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseSleeping;
    }));

    const zl::ecs::Entity secondPlayer =
        host.Simulation().Ecs().FindById(secondId);
    REQUIRE(static_cast<bool>(secondPlayer != entt::null));
    const float secondXBefore =
        host.Simulation().Ecs().Registry().get<zl::ecs::Position>(secondPlayer).x;
    REQUIRE(second.SendCommand(MakeMove(secondId, 1.0f, 0.0f), &error));
    host.RunForTicks(8);
    CHECK(std::abs(
              host.Simulation().Ecs().Registry().get<zl::ecs::Position>(secondPlayer).x -
              secondXBefore) < 0.5f);

    REQUIRE(PumpUntil(host, first, second, 12000, [&] {
        return first.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseNone &&
               second.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseNone &&
               first.SocialState().gameMinutes >= beforeSleep + 479.0;
    }));
    CHECK(first.PlayerState().status.find("睡眠结束") != std::string::npos);
    CHECK(first.SocialState().gameMinutes >= beforeSleep + 479.0);

    const double beforeInterrupt = first.SocialState().gameMinutes;
    REQUIRE(first.SendCommand(request, &error));
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseVote &&
               second.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseVote;
    }));
    REQUIRE(second.SendCommand(vote, &error));
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseSleeping &&
               second.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseSleeping;
    }));
    auto& health =
        host.Simulation().Ecs().Registry().get<zl::ecs::Health>(firstPlayer);
    health.current -= 5.0f;
    host.RunForTicks(4);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseNone &&
               second.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseNone;
    }));
    CHECK(first.PlayerState().status.find("受到伤害") != std::string::npos);
    CHECK(first.SocialState().gameMinutes < beforeInterrupt + 479.0);

    const double beforeTimeout = first.SocialState().gameMinutes;
    REQUIRE(first.SendCommand(request, &error));
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseVote &&
               second.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseVote;
    }));
    host.Tick(31.0f);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseNone &&
               second.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseNone;
    }));
    CHECK(first.PlayerState().status.find("投票超时") != std::string::npos);
    CHECK(first.SocialState().gameMinutes < beforeTimeout + 5.0);

    REQUIRE(first.SendCommand(request, &error));
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseVote &&
               second.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseVote;
    }));
    zl::net::Command cancel;
    cancel.type = zl::net::CommandType::SleepCancel;
    cancel.playerStableId = secondId;
    REQUIRE(second.SendCommand(cancel, &error));
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseNone &&
               second.PlayerState().sleep.phase ==
                   zl::net::kPlayerSleepPhaseNone;
    }));
    CHECK(first.PlayerState().status.find("玩家取消") != std::string::npos);

    host.Shutdown();
}


TEST_CASE("M23-F2a-T6 服务器权威觅食/钓鱼与重复/距离拒绝") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4632415436000001ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);

    zl::game::NetworkClient::Options options;
    options.host = "127.0.0.1";
    options.port = host.NetworkPort();
    options.contentHash = hostOptions.contentHash;
    options.autoReconnect = false;
    zl::game::NetworkClient first(options);
    zl::game::NetworkClient second(options);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        return first.HasPlayerState() && second.HasPlayerState();
    }));

    auto& registry = host.Simulation().Ecs().Registry();
    const auto teleport = [&](std::uint64_t stableId, float x, float y) {
        const zl::ecs::Entity entity =
            host.Simulation().Ecs().FindById(stableId);
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

    const std::uint64_t firstId = first.PlayerStableId();
    const std::uint64_t secondId = second.PlayerStableId();
    REQUIRE(firstId != 0);
    REQUIRE(secondId != 0);

    // 服务器附近节点列表由真实地图规划；先移动到已知资源区。
    teleport(firstId, 70.0f, 24.0f);
    teleport(secondId, 70.0f, 24.0f);
    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        return first.HasPlayerState() &&
               !first.PlayerState().forageNodes.empty();
    }));
    zl::net::PlayerForageNodeState node{};
    bool nodeStable = false;
    for (int attempt = 0; attempt < 8 && !nodeStable; ++attempt) {
        REQUIRE(PumpUntil(host, first, second, 6000, [&] {
            return first.HasPlayerState() &&
                   !first.PlayerState().forageNodes.empty();
        }));
        bool picked = false;
        for (const auto& candidate : first.PlayerState().forageNodes) {
            if (candidate.ready) {
                node = candidate;
                picked = true;
                break;
            }
        }
        REQUIRE(picked);
        teleport(firstId, static_cast<float>(node.x) + 0.5f,
                 static_cast<float>(node.y) + 0.5f);
        teleport(secondId, static_cast<float>(node.x) + 0.5f,
                 static_cast<float>(node.y) + 3.0f);
        nodeStable = PumpUntil(host, first, second, 3000, [&] {
            if (!first.HasPlayerState()) {
                return false;
            }
            for (const auto& nearby : first.PlayerState().forageNodes) {
                if (nearby.x == node.x && nearby.y == node.y &&
                    nearby.z == node.z) {
                    return nearby.ready;
                }
            }
            return false;
        });
    }
    INFO("forage node stable=" << nodeStable << " target="
         << node.x << "," << node.y << "," << node.z);
    REQUIRE(nodeStable);

    const zl::ecs::Entity firstEntity =
        host.Simulation().Ecs().FindById(firstId);
    const zl::ecs::Entity secondEntity =
        host.Simulation().Ecs().FindById(secondId);
    auto& firstBag = registry.get<zl::ecs::Container>(firstEntity);
    auto& secondBag = registry.get<zl::ecs::Container>(secondEntity);
    firstBag.items.push_back(
        zl::ecs::ItemInstance{"forage_stick", 1, 1.0f});
    secondBag.items.push_back(
        zl::ecs::ItemInstance{"forage_stick", 1, 1.0f});

    zl::net::Command start;
    start.playerStableId = firstId;
    start.type = zl::net::CommandType::ForageStart;
    start.payload.x = static_cast<float>(node.x) + 0.5f;
    start.payload.y = static_cast<float>(node.y) + 0.5f;
    start.payload.targetIndex = node.z;
    REQUIRE(first.SendCommand(start, &error));
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        return first.PlayerState().survival.mode ==
               zl::net::kPlayerSurvivalModeForage;
    }));
    CHECK(first.PlayerState().survival.progress >= 0.0f);

    start.playerStableId = secondId;
    REQUIRE(second.SendCommand(start, &error));
    const bool secondForageRejected = PumpUntil(host, first, second, 4000, [&] {
        const auto& state = second.PlayerState();
        return state.survival.mode == zl::net::kPlayerSurvivalModeNone &&
               (state.status.find("其他玩家") != std::string::npos ||
                state.status.find("耗尽") != std::string::npos ||
                state.status.find("觅食") != std::string::npos ||
                state.status.find("采集") != std::string::npos);
    });
    INFO("second forage status: " << second.PlayerState().status);
    REQUIRE(secondForageRejected);
    CHECK(second.PlayerState().survival.mode ==
          zl::net::kPlayerSurvivalModeNone);

    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        return first.PlayerState().survival.mode ==
                   zl::net::kPlayerSurvivalModeNone &&
               first.PlayerState().status.find("采集了") != std::string::npos;
    }));
    zl::net::Command repeat = start;
    repeat.playerStableId = firstId;
    REQUIRE(first.SendCommand(repeat, &error));
    const bool repeatRejected = PumpUntil(host, first, second, 3000, [&] {
        const auto& state = first.PlayerState();
        if (state.survival.mode != zl::net::kPlayerSurvivalModeNone) {
            return false;
        }
        return state.status.find("耗尽") != std::string::npos ||
               state.status.find("觅食目标不在服务器规划范围内") !=
                   std::string::npos;
    });
    INFO("forage repeat rejected=" << repeatRejected << " target="
         << node.x << "," << node.y << "," << node.z
         << " status=" << first.PlayerState().status);
    REQUIRE(repeatRejected);

    const std::uint64_t rangeBefore = host.GetNetworkStats().rangeRejected;
    zl::net::Command far = start;
    far.playerStableId = firstId;
    far.payload.x = static_cast<float>(node.x) + 20.5f;
    far.payload.y = static_cast<float>(node.y) + 20.5f;
    REQUIRE(first.SendCommand(far, &error));
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        return host.GetNetworkStats().rangeRejected > rangeBefore;
    }));
    CHECK(first.PlayerState().survival.mode ==
          zl::net::kPlayerSurvivalModeNone);

    // 钓鱼：服务器决定等待、咬钩和鱼获；客户端只读 survival 进度。
    teleport(firstId, 80.5f, 30.5f);
    firstBag.items.push_back(zl::ecs::ItemInstance{"fishing_rod", 1, 1.0f});
    firstBag.items.push_back(zl::ecs::ItemInstance{"fish_bait", 2, 1.0f});
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.HasPlayerState() &&
               !first.PlayerState().fishingSpots.empty();
    }));
    const zl::net::PlayerFishingSpotState fishing =
        first.PlayerState().fishingSpots.front();
    teleport(firstId, static_cast<float>(fishing.x) + 0.5f,
             static_cast<float>(fishing.y) + 0.5f);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.HasPlayerState() &&
               !first.PlayerState().fishingSpots.empty();
    }));

    const int baitBefore = BagCount(first.PlayerState(), "fish_bait");
    zl::net::Command fish;
    fish.playerStableId = firstId;
    fish.type = zl::net::CommandType::FishingStart;
    fish.payload.x = static_cast<float>(fishing.x) + 0.5f;
    fish.payload.y = static_cast<float>(fishing.y) + 0.5f;
    fish.payload.targetIndex = fishing.z;
    REQUIRE(first.SendCommand(fish, &error));
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        return first.PlayerState().survival.mode ==
               zl::net::kPlayerSurvivalModeFishing;
    }));
    CHECK(BagCount(first.PlayerState(), "fish_bait") < baitBefore);
    REQUIRE(PumpUntil(host, first, second, 30000, [&] {
        return first.PlayerState().survival.phase ==
               zl::net::kPlayerSurvivalPhaseBiting;
    }));

    zl::net::Command reel;
    reel.playerStableId = firstId;
    reel.type = zl::net::CommandType::FishingReel;
    REQUIRE(first.SendCommand(reel, &error));
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.PlayerState().survival.mode ==
                   zl::net::kPlayerSurvivalModeNone &&
               (first.PlayerState().status.find("钓到") !=
                    std::string::npos ||
                first.PlayerState().status.find("没钓到") !=
                    std::string::npos ||
                first.PlayerState().status.find("鱼跑了") !=
                    std::string::npos);
    }));

    host.Shutdown();
}

TEST_CASE("M23-F2b-T6 服务器权威动物生成、死亡复制与防重复剥取") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.spawnAnimals = true;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4632425436000001ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);

    zl::game::NetworkClient::Options options;
    options.host = "127.0.0.1";
    options.port = host.NetworkPort();
    options.contentHash = hostOptions.contentHash;
    options.autoReconnect = false;
    zl::game::NetworkClient first(options);
    zl::game::NetworkClient second(options);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        return first.HasPlayerState() && second.HasPlayerState();
    }));

    auto& registry = host.Simulation().Ecs().Registry();
    const auto teleport = [&](std::uint64_t stableId, float x, float y) {
        const zl::ecs::Entity entity =
            host.Simulation().Ecs().FindById(stableId);
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

    REQUIRE(host.Simulation().Animals() != nullptr);
    REQUIRE(PumpHostUntil(host, 8000, [&] {
        return host.Simulation().Animals()->GetStats().live > 0;
    }));
    CHECK(host.Simulation().Animals()->GetStats().live <= 36);

    const auto hasAnimalActor = [&](zl::game::NetworkClient& client) {
        for (const auto& [stableId, actor] :
             client.Replication().Actors()) {
            static_cast<void>(stableId);
            if (actor.hasAnimal) {
                return true;
            }
        }
        return false;
    };
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return hasAnimalActor(first) || hasAnimalActor(second);
    }));

    zl::ecs::Entity animal = entt::null;
    std::string species;
    registry.view<zl::ecs::Animal, zl::ecs::Health, zl::ecs::Position>().each(
        [&](const zl::ecs::Entity entity, const zl::ecs::Animal& value,
            const zl::ecs::Health& health, const zl::ecs::Position&) {
            if (animal == entt::null && health.current > 0.0f) {
                animal = entity;
                species = value.speciesId;
            }
        });
    REQUIRE_MESSAGE(static_cast<bool>(animal != entt::null),
                    "服务器没有可击杀的动物");

    // 客户端只发目标 StableId；服务器校验目标为动物并按权威近战结算伤害。
    const std::uint64_t animalId =
        host.Simulation().Ecs().IdOf(animal).id;
    const zl::ecs::Position animalPosition =
        registry.get<zl::ecs::Position>(animal);
    teleport(first.PlayerStableId(), animalPosition.x - 0.8f,
             animalPosition.y);
    const auto& animalHealth = registry.get<zl::ecs::Health>(animal);
    const float maxAnimalHealth = animalHealth.max;
    zl::net::Command attack;
    attack.type = zl::net::CommandType::Attack;
    attack.playerStableId = first.PlayerStableId();
    attack.payload.entityId = animalId;
    REQUIRE(first.SendCommand(attack, &error));
    bool animalDamaged = false;
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        const auto* health = registry.try_get<zl::ecs::Health>(animal);
        if (health == nullptr) {
            return animalDamaged;
        }
        animalDamaged = health->current < maxAnimalHealth;
        return animalDamaged;
    }));

    if (const auto* health = registry.try_get<zl::ecs::Health>(animal);
        health != nullptr && health->current > 0.0f) {
        zl::ecs::DamageEvent kill;
        kill.type = zl::ecs::DamageType::Blunt;
        kill.amount = 1000.0f;
        kill.sourceId = first.PlayerStableId();
        REQUIRE(host.Simulation().Combat()->ApplyDamage(
            host.Simulation().Ecs(), animal, kill));
    }

    zl::ecs::Entity corpse = entt::null;
    REQUIRE(PumpHostUntil(host, 4000, [&] {
        registry.view<zl::ecs::AnimalCorpse, zl::ecs::Position>().each(
            [&](const zl::ecs::Entity entity,
                const zl::ecs::AnimalCorpse& value,
                const zl::ecs::Position&) {
                if (corpse == entt::null && value.speciesId == species) {
                    corpse = entity;
                }
            });
        return corpse != entt::null;
    }));
    const std::uint64_t corpseId =
        host.Simulation().Ecs().IdOf(corpse).id;
    REQUIRE(corpseId != 0);

    const auto* corpsePosition =
        registry.try_get<zl::ecs::Position>(corpse);
    REQUIRE(corpsePosition != nullptr);
    const zl::ecs::Position corpsePoint = *corpsePosition;
    teleport(first.PlayerStableId(), corpsePoint.x + 20.0f,
             corpsePoint.y);

    zl::net::Command start;
    start.type = zl::net::CommandType::AnimalHarvestStart;
    start.payload.entityId = corpseId;
    start.playerStableId = first.PlayerStableId();
    const std::uint64_t rangeBefore = host.GetNetworkStats().rangeRejected;
    REQUIRE(first.SendCommand(start, &error));
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        return host.GetNetworkStats().rangeRejected > rangeBefore;
    }));
    CHECK_FALSE(first.PlayerState().animalHarvest.active);

    teleport(first.PlayerStableId(), corpsePoint.x, corpsePoint.y);
    teleport(second.PlayerStableId(), corpsePoint.x, corpsePoint.y);
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        const auto* actor = first.Replication().Find(corpseId);
        return actor != nullptr && actor->hasAnimalCorpse &&
               actor->animalCorpse.speciesId == species &&
               !actor->animalCorpse.harvested;
    }));

    const zl::ecs::Entity firstEntity =
        host.Simulation().Ecs().FindById(first.PlayerStableId());
    const zl::ecs::Entity secondEntity =
        host.Simulation().Ecs().FindById(second.PlayerStableId());
    REQUIRE(static_cast<bool>(firstEntity != entt::null));
    REQUIRE(static_cast<bool>(secondEntity != entt::null));
    auto& firstBag = registry.get<zl::ecs::Container>(firstEntity);
    auto& secondBag = registry.get<zl::ecs::Container>(secondEntity);
    firstBag.items.push_back(
        zl::ecs::ItemInstance{"kitchen_knife", 1, 1.0f});
    secondBag.items.push_back(
        zl::ecs::ItemInstance{"kitchen_knife", 1, 1.0f});

    start.playerStableId = first.PlayerStableId();
    REQUIRE(first.SendCommand(start, &error));
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        return first.PlayerState().animalHarvest.active &&
               first.PlayerState().animalHarvest.corpseStableId == corpseId;
    }));

    start.playerStableId = second.PlayerStableId();
    REQUIRE(second.SendCommand(start, &error));
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        return second.PlayerState().status.find("其他玩家") !=
                   std::string::npos ||
               second.PlayerState().status.find("已被") !=
                   std::string::npos ||
               second.PlayerState().status.find("已处理") !=
                   std::string::npos;
    }));
    CHECK_FALSE(second.PlayerState().animalHarvest.active);

    REQUIRE(PumpUntil(host, first, second, 7000, [&] {
        return !first.PlayerState().animalHarvest.active &&
               first.PlayerState().status.find("剥取了") !=
                   std::string::npos;
    }));
    const auto* harvested =
        registry.try_get<zl::ecs::AnimalCorpse>(corpse);
    REQUIRE(harvested != nullptr);
    CHECK(harvested->harvested);
    const auto* loot = registry.try_get<zl::ecs::LootContainer>(corpse);
    REQUIRE(loot != nullptr);
    CHECK(loot->searched);
    const auto* corpseItems =
        registry.try_get<zl::ecs::Container>(corpse);
    REQUIRE(corpseItems != nullptr);
    auto* liveFirstBag =
        registry.try_get<zl::ecs::Container>(firstEntity);
    REQUIRE(liveFirstBag != nullptr);
    int meat = 0;
    int hide = 0;
    int bone = 0;
    for (const auto& item : corpseItems->items) {
        if (item.defId == "raw_venison" || item.defId == "raw_rabbit_meat" ||
            item.defId == "raw_boar_meat" || item.defId == "raw_chicken" ||
            item.defId == "raw_goat_meat") {
            meat += item.count;
        } else if (item.defId == "animal_hide") {
            hide += item.count;
        } else if (item.defId == "bone") {
            bone += item.count;
        }
    }
    CHECK(meat + hide + bone > 0);
    bool knifeWorn = false;
    for (const auto& item : liveFirstBag->items) {
        if (item.defId == "kitchen_knife" && item.condition < 1.0f) {
            knifeWorn = true;
        }
    }
    CHECK(knifeWorn);

    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        const auto* actor = second.Replication().Find(corpseId);
        return actor != nullptr && actor->hasAnimalCorpse &&
               actor->animalCorpse.harvested && actor->hasLoot &&
               !actor->lootContainerItems.items.empty();
    }));

    start.playerStableId = first.PlayerStableId();
    REQUIRE(first.SendCommand(start, &error));
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        return first.PlayerState().status.find("已被") !=
                   std::string::npos ||
               first.PlayerState().status.find("已处理") !=
                   std::string::npos;
    }));

    host.Shutdown();
}

TEST_CASE("M23-F2a-T7 服务器觅食工具磨损、耗尽与时间刷新") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4632415437000001ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);

    zl::game::NetworkClient::Options options;
    options.host = "127.0.0.1";
    options.port = host.NetworkPort();
    options.contentHash = hostOptions.contentHash;
    options.autoReconnect = false;
    zl::game::NetworkClient client(options);
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
    const zl::net::PlayerForageNodeState* target = findMeadow();
    if (target == nullptr) {
        teleport(78.0f, 22.0f);
        REQUIRE(PumpUntil(host, client, 6000, [&] {
            return client.HasPlayerState() &&
                   !client.PlayerState().forageNodes.empty();
        }));
        target = findMeadow();
    }
    REQUIRE_MESSAGE(target != nullptr, "附近没有可采集的草地节点");
    const zl::net::PlayerForageNodeState node = *target;

    teleport(static_cast<float>(node.x) + 0.5f,
             static_cast<float>(node.y) + 0.5f);
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
    start.payload.x = static_cast<float>(node.x) + 0.5f;
    start.payload.y = static_cast<float>(node.y) + 0.5f;
    start.payload.targetIndex = node.z;
    REQUIRE(client.SendCommand(start, &error));
    REQUIRE(PumpUntil(host, client, 4000, [&] {
        return client.PlayerState().survival.mode ==
               zl::net::kPlayerSurvivalModeForage;
    }));
    REQUIRE(PumpUntil(host, client, 10000, [&] {
        return client.PlayerState().survival.mode ==
                   zl::net::kPlayerSurvivalModeNone &&
               client.PlayerState().status.find("采集了") != std::string::npos;
    }));
    const float wornCondition = [&]() {
        for (const auto& item : bag.items) {
            if (item.defId == "forage_stick") {
                return item.condition;
            }
        }
        return 1.0f;
    }();
    CHECK(wornCondition < 0.5f);
    CHECK(wornCondition > 0.0f);

    REQUIRE(PumpUntil(host, client, 5000, [&] {
        for (const auto& nearby : client.PlayerState().forageNodes) {
            if (nearby.x == node.x && nearby.y == node.y &&
                nearby.z == node.z) {
                return !nearby.ready;
            }
        }
        return false;
    }));

    host.AdvanceGameMinutes(200.0);
    host.RunForTicks(90);
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        for (const auto& nearby : client.PlayerState().forageNodes) {
            if (nearby.x == node.x && nearby.y == node.y &&
                nearby.z == node.z) {
                return nearby.ready;
            }
        }
        return false;
    }));

    host.Shutdown();
}

TEST_CASE("M23-F2a-T7 服务器钓鱼取消、过早收杆、错过咬钩与鱼竿磨损") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4632415437000002ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);

    zl::game::NetworkClient::Options options;
    options.host = "127.0.0.1";
    options.port = host.NetworkPort();
    options.contentHash = hostOptions.contentHash;
    options.autoReconnect = false;
    zl::game::NetworkClient client(options);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.HasPlayerState();
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

    teleport(80.5f, 30.5f);
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.HasPlayerState() && !client.PlayerState().fishingSpots.empty();
    }));
    const zl::net::PlayerFishingSpotState spot =
        client.PlayerState().fishingSpots.front();
    teleport(static_cast<float>(spot.x) + 0.5f,
             static_cast<float>(spot.y) + 0.5f);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return client.HasPlayerState() && !client.PlayerState().fishingSpots.empty();
    }));

    const zl::ecs::Entity player = host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));
    auto& bag = registry.get<zl::ecs::Container>(player);
    bag.items.push_back(zl::ecs::ItemInstance{"fishing_rod", 1, 0.5f});
    bag.items.push_back(zl::ecs::ItemInstance{"fish_bait", 4, 1.0f});

    zl::net::Command fish;
    fish.playerStableId = playerId;
    fish.type = zl::net::CommandType::FishingStart;
    fish.payload.x = static_cast<float>(spot.x) + 0.5f;
    fish.payload.y = static_cast<float>(spot.y) + 0.5f;
    fish.payload.targetIndex = spot.z;
    REQUIRE(client.SendCommand(fish, &error));
    REQUIRE(PumpUntil(host, client, 4000, [&] {
        return client.PlayerState().survival.mode ==
               zl::net::kPlayerSurvivalModeFishing;
    }));
    CHECK(zl::items::CountOf(bag, "fish_bait") == 3);

    zl::net::Command cancel;
    cancel.playerStableId = playerId;
    cancel.type = zl::net::CommandType::FishingCancel;
    REQUIRE(client.SendCommand(cancel, &error));
    REQUIRE(PumpUntil(host, client, 4000, [&] {
        return client.PlayerState().survival.mode ==
                   zl::net::kPlayerSurvivalModeNone &&
               client.PlayerState().status.find("取消钓鱼") != std::string::npos;
    }));
    CHECK(zl::items::CountOf(bag, "fish_bait") == 3);
    CHECK([&]() {
        for (const auto& item : bag.items) {
            if (item.defId == "fishing_rod") {
                return item.condition;
            }
        }
        return 0.0f;
    }() == doctest::Approx(0.5f));

    REQUIRE(client.SendCommand(fish, &error));
    REQUIRE(PumpUntil(host, client, 4000, [&] {
        return client.PlayerState().survival.mode ==
               zl::net::kPlayerSurvivalModeFishing;
    }));
    CHECK(zl::items::CountOf(bag, "fish_bait") == 2);

    zl::net::Command reel;
    reel.playerStableId = playerId;
    reel.type = zl::net::CommandType::FishingReel;
    REQUIRE(client.SendCommand(reel, &error));
    REQUIRE(PumpUntil(host, client, 4000, [&] {
        return client.PlayerState().status.find("还没有咬钩") !=
               std::string::npos;
    }));
    CHECK(client.PlayerState().survival.mode ==
          zl::net::kPlayerSurvivalModeFishing);

    REQUIRE(PumpUntil(host, client, 15000, [&] {
        return client.PlayerState().survival.phase ==
               zl::net::kPlayerSurvivalPhaseBiting;
    }));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.PlayerState().survival.mode ==
                   zl::net::kPlayerSurvivalModeNone &&
               client.PlayerState().status.find("错过收杆") !=
                   std::string::npos;
    }));
    CHECK([&]() {
        for (const auto& item : bag.items) {
            if (item.defId == "fishing_rod") {
                return item.condition;
            }
        }
        return 1.0f;
    }() < 0.5f);

    host.Shutdown();
}


TEST_CASE("M23-F3-T6 联机火焰：服务器权威点火、复制与灭火") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x5A4C544E455436ull;

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
        return client.IsConnected() && client.HasPlayer() &&
               host.SessionCount() == 1;
    }));

    const std::uint64_t playerId = client.PlayerStableId();
    REQUIRE(playerId != 0);
    const zl::ecs::Entity serverPlayer =
        host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(serverPlayer != entt::null));
    const zl::ecs::Position playerPosition =
        host.Simulation().Ecs().Registry().get<zl::ecs::Position>(serverPlayer);
    const int fireX = static_cast<int>(std::floor(playerPosition.x));
    const int fireY = static_cast<int>(std::floor(playerPosition.y));
    const int fireZ = playerPosition.z;
    REQUIRE(SpawnCampfire(host, fireX, fireY) != 0);

    zl::game::FireSystem* fire = host.Simulation().Fire();
    REQUIRE(fire != nullptr);
    CHECK(fire->FindAt(host.Simulation().Ecs(), fireX, fireY, fireZ) ==
          nullptr);

    zl::net::Command ignite;
    ignite.type = zl::net::CommandType::IgniteFire;
    ignite.playerStableId = playerId;
    ignite.payload.x = static_cast<float>(fireX);
    ignite.payload.y = static_cast<float>(fireY);
    ignite.payload.targetIndex = fireZ;

    // 客户端不能把火源伪造到目标格之外：服务器要求目标必须是附近篝火。
    zl::net::Command forged = ignite;
    forged.payload.x = static_cast<float>(fireX + 20);
    REQUIRE_MESSAGE(client.SendCommand(forged, &error), error);
    (void)PumpUntil(host, client, 300, [] { return false; });
    CHECK(fire->FindAt(host.Simulation().Ecs(), fireX + 20, fireY, fireZ) ==
          nullptr);

    REQUIRE_MESSAGE(client.SendCommand(ignite, &error), error);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const zl::ecs::FireSource* source =
            fire->FindAt(host.Simulation().Ecs(), fireX, fireY, fireZ);
        return source != nullptr && zl::game::FireIsActive(*source);
    }));
    const zl::ecs::FireSource* source =
        fire->FindAt(host.Simulation().Ecs(), fireX, fireY, fireZ);
    REQUIRE(source != nullptr);
    CHECK(zl::game::FireIsActive(*source));

    std::uint64_t fireStableId = 0;
    host.Simulation().Ecs().Registry().view<zl::ecs::FireAnchor>().each(
        [&](const entt::entity entity, const zl::ecs::FireAnchor& anchor) {
            if (anchor.x == fireX && anchor.y == fireY && anchor.z == fireZ) {
                fireStableId = host.Simulation().Ecs().IdOf(entity).id;
            }
        });
    REQUIRE(fireStableId != 0);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const zl::game::ReplicatedActor* actor =
            client.Replication().Find(fireStableId);
        return actor != nullptr && actor->hasFire;
    }));
    const zl::game::ReplicatedActor* replicatedFire =
        client.Replication().Find(fireStableId);
    REQUIRE(replicatedFire != nullptr);
    CHECK(replicatedFire->fireAnchor.x == fireX);
    CHECK(replicatedFire->fireAnchor.y == fireY);
    CHECK(replicatedFire->fireSource.materialId == "campfire");

    auto& serverRegistry = host.Simulation().Ecs().Registry();
    auto& container = serverRegistry.get<zl::ecs::Container>(serverPlayer);
    for (int i = 0; i < 12; ++i) {
        container.items.push_back(
            zl::ecs::ItemInstance{"fire_extinguisher", 1, 1.0f});
    }

    bool extinguished = false;
    for (int attempt = 0; attempt < 20 && !extinguished; ++attempt) {
        zl::net::Command extinguish;
        extinguish.type = zl::net::CommandType::ExtinguishFire;
        extinguish.playerStableId = playerId;
        extinguish.payload.x = static_cast<float>(fireX);
        extinguish.payload.y = static_cast<float>(fireY);
        extinguish.payload.targetIndex = fireZ;
        REQUIRE(client.SendCommand(extinguish, &error));
        extinguished = PumpUntil(host, client, 2000, [&] {
            const zl::ecs::FireSource* current =
                fire->FindAt(host.Simulation().Ecs(), fireX, fireY, fireZ);
            return current != nullptr &&
                   current->phase == zl::ecs::FirePhase::Extinguished &&
                   client.Replication().Find(fireStableId) != nullptr &&
                   client.Replication().Find(fireStableId)->hasFire &&
                   client.Replication().Find(fireStableId)->fireSource.phase ==
                       zl::ecs::FirePhase::Extinguished;
        });
    }
    CHECK(extinguished);
}





TEST_CASE("M23-F3-T8 双客户端火焰：复制状态一致与权威灭火") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x5A4C544E455438ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;

    zl::game::NetworkClient first(clientOptions);
    zl::game::NetworkClient second(clientOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayer() && second.HasPlayer() &&
               host.SessionCount() == 2;
    }));

    const std::uint64_t firstId = first.PlayerStableId();
    const std::uint64_t secondId = second.PlayerStableId();
    REQUIRE(firstId != 0);
    REQUIRE(secondId != 0);
    REQUIRE(firstId != secondId);

    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity firstPlayer =
        host.Simulation().Ecs().FindById(firstId);
    REQUIRE(static_cast<bool>(firstPlayer != entt::null));
    const zl::ecs::Position playerPosition =
        registry.get<zl::ecs::Position>(firstPlayer);
    const int fireX = static_cast<int>(std::floor(playerPosition.x));
    const int fireY = static_cast<int>(std::floor(playerPosition.y));
    const int fireZ = playerPosition.z;
    REQUIRE(SpawnCampfire(host, fireX, fireY) != 0);

    zl::game::FireSystem* fire = host.Simulation().Fire();
    REQUIRE(fire != nullptr);

    zl::net::Command ignite;
    ignite.type = zl::net::CommandType::IgniteFire;
    ignite.playerStableId = firstId;
    ignite.payload.x = static_cast<float>(fireX);
    ignite.payload.y = static_cast<float>(fireY);
    ignite.payload.targetIndex = fireZ;
    REQUIRE_MESSAGE(first.SendCommand(ignite, &error), error);

    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        const zl::ecs::FireSource* source =
            fire->FindAt(host.Simulation().Ecs(), fireX, fireY, fireZ);
        if (source == nullptr || !zl::game::FireIsActive(*source)) {
            return false;
        }
        const zl::game::ReplicatedActor* firstFire = nullptr;
        const zl::game::ReplicatedActor* secondFire = nullptr;
        registry.view<zl::ecs::FireAnchor>().each(
            [&](const entt::entity entity, const zl::ecs::FireAnchor& anchor) {
                if (anchor.x == fireX && anchor.y == fireY &&
                    anchor.z == fireZ) {
                    const std::uint64_t id =
                        host.Simulation().Ecs().IdOf(entity).id;
                    firstFire = first.Replication().Find(id);
                    secondFire = second.Replication().Find(id);
                }
            });
        return firstFire != nullptr && secondFire != nullptr &&
               firstFire->hasFire && secondFire->hasFire;
    }));

    std::uint64_t fireStableId = 0;
    registry.view<zl::ecs::FireAnchor>().each(
        [&](const entt::entity entity, const zl::ecs::FireAnchor& anchor) {
            if (anchor.x == fireX && anchor.y == fireY && anchor.z == fireZ) {
                fireStableId = host.Simulation().Ecs().IdOf(entity).id;
            }
        });
    REQUIRE(fireStableId != 0);

    const zl::game::ReplicatedActor* firstFire =
        first.Replication().Find(fireStableId);
    const zl::game::ReplicatedActor* secondFire =
        second.Replication().Find(fireStableId);
    REQUIRE(firstFire != nullptr);
    REQUIRE(secondFire != nullptr);
    REQUIRE(firstFire->hasFire);
    REQUIRE(secondFire->hasFire);
    CHECK(firstFire->fireAnchor.x == fireX);
    CHECK(firstFire->fireAnchor.y == fireY);
    CHECK(firstFire->fireSource.materialId == "campfire");
    CHECK(secondFire->fireAnchor.x == firstFire->fireAnchor.x);
    CHECK(secondFire->fireAnchor.y == firstFire->fireAnchor.y);
    CHECK(secondFire->fireSource.materialId == firstFire->fireSource.materialId);
    CHECK(secondFire->fireSource.phase == firstFire->fireSource.phase);

    zl::net::Command forged = ignite;
    forged.playerStableId = secondId;
    forged.payload.x = static_cast<float>(fireX + 20);
    REQUIRE_MESSAGE(second.SendCommand(forged, &error), error);
    (void)PumpUntil(host, first, second, 300, [] { return false; });
    CHECK(fire->FindAt(host.Simulation().Ecs(), fireX + 20, fireY, fireZ) ==
          nullptr);

    auto& container = registry.get<zl::ecs::Container>(firstPlayer);
    for (int i = 0; i < 12; ++i) {
        container.items.push_back(
            zl::ecs::ItemInstance{"fire_extinguisher", 1, 1.0f});
    }

    bool extinguished = false;
    for (int attempt = 0; attempt < 20 && !extinguished; ++attempt) {
        zl::net::Command extinguish;
        extinguish.type = zl::net::CommandType::ExtinguishFire;
        extinguish.playerStableId = firstId;
        extinguish.payload.x = static_cast<float>(fireX);
        extinguish.payload.y = static_cast<float>(fireY);
        extinguish.payload.targetIndex = fireZ;
        REQUIRE_MESSAGE(first.SendCommand(extinguish, &error), error);
        extinguished = PumpUntil(host, first, second, 2000, [&] {
            const zl::ecs::FireSource* current =
                fire->FindAt(host.Simulation().Ecs(), fireX, fireY, fireZ);
            const zl::game::ReplicatedActor* a =
                first.Replication().Find(fireStableId);
            const zl::game::ReplicatedActor* b =
                second.Replication().Find(fireStableId);
            return current != nullptr &&
                   current->phase == zl::ecs::FirePhase::Extinguished &&
                   a != nullptr && b != nullptr && a->hasFire && b->hasFire &&
                   a->fireSource.phase == zl::ecs::FirePhase::Extinguished &&
                   b->fireSource.phase == zl::ecs::FirePhase::Extinguished;
        });
    }
    CHECK(extinguished);
}

TEST_CASE("M25-T3-V3 服务器权威载具：上下车、驾驶、服务与非法命令") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D323554335633ull;

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
    REQUIRE(registry.all_of<zl::ecs::Position, zl::ecs::Vehicle,
                            zl::ecs::VehicleDriver, zl::ecs::Container,
                            zl::ecs::PlayerStorage>(vehicle));
    CHECK(registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == 0);

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
        const zl::net::PlayerVehicleState& state =
            client.PlayerState().vehicle;
        return state.valid && state.vehicleStableId == vehicleId &&
               state.driving;
    }));
    CHECK(registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == playerId);
    CHECK(registry.all_of<zl::ecs::VehicleOccupant>(player));
    CHECK(registry.get<zl::ecs::VehicleOccupant>(player).damageReduction ==
          doctest::Approx(0.85f));
    {
        const auto& playerPosition = registry.get<zl::ecs::Position>(player);
        CHECK(playerPosition.x == doctest::Approx(vehicleStart.x));
        CHECK(playerPosition.y == doctest::Approx(vehicleStart.y));
    }

    float worldDirX = 1.0f;
    float worldDirY = 0.0f;
    PickOpenDriveDirection(host, vehicleStart.x, vehicleStart.y, &worldDirX,
                           &worldDirY);
    float screenDirX = 1.0f;
    float screenDirY = 0.0f;
    WorldDirToScreen(worldDirX, worldDirY, &screenDirX, &screenDirY);

    const float fuelBeforeDrive =
        registry.get<zl::ecs::Vehicle>(vehicle).fuel;
    REQUIRE_MESSAGE(client.SendCommand(
                        MakeMove(playerId, screenDirX, screenDirY), &error),
                    error);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto& position = registry.get<zl::ecs::Position>(vehicle);
        const float dx = position.x - vehicleStart.x;
        const float dy = position.y - vehicleStart.y;
        return dx * dx + dy * dy > 0.20f * 0.20f;
    }));
    const zl::ecs::Position vehicleAfterDrive =
        registry.get<zl::ecs::Position>(vehicle);
    CHECK(registry.get<zl::ecs::Vehicle>(vehicle).fuel < fuelBeforeDrive);
    {
        const auto& playerPosition = registry.get<zl::ecs::Position>(player);
        CHECK(playerPosition.x == doctest::Approx(vehicleAfterDrive.x));
        CHECK(playerPosition.y == doctest::Approx(vehicleAfterDrive.y));
    }

    REQUIRE_MESSAGE(client.SendCommand(MakeMove(playerId, 0.0f, 0.0f), &error),
                    error);
    host.RunForTicks(2);
    REQUIRE_MESSAGE(client.SendCommand(
                        MakeVehicleCommand(zl::net::CommandType::ExitVehicle,
                                           playerId, 0),
                        &error),
                    error);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return client.PlayerState().vehicle.vehicleStableId == vehicleId &&
               !client.PlayerState().vehicle.driving;
    }));
    CHECK(registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == 0);
    CHECK_FALSE(registry.all_of<zl::ecs::VehicleOccupant>(player));

    const float fuelBeforeMissingItem =
        registry.get<zl::ecs::Vehicle>(vehicle).fuel;
    const std::uint64_t rejectedBeforeMissingItem =
        host.GetNetworkStats().commandsRejected;
    REQUIRE_MESSAGE(client.SendCommand(
                        MakeVehicleCommand(
                            zl::net::CommandType::ServiceVehicle, playerId,
                            vehicleId),
                        &error),
                    error);
    (void)PumpUntil(host, client, 500, [] { return false; });
    CHECK(registry.get<zl::ecs::Vehicle>(vehicle).fuel ==
          doctest::Approx(fuelBeforeMissingItem));
    CHECK(host.GetNetworkStats().commandsRejected >
          rejectedBeforeMissingItem);

    auto& bag = registry.get<zl::ecs::Container>(player);
    bag.items.push_back(zl::ecs::ItemInstance{"repair_kit", 2, 1.0f});
    bag.items.push_back(zl::ecs::ItemInstance{"fuel_can", 2, 1.0f});
    const float fuelBeforeService =
        registry.get<zl::ecs::Vehicle>(vehicle).fuel;
    const float conditionBeforeService =
        registry.get<zl::ecs::Vehicle>(vehicle).condition;
    const int repairCountBefore = zl::items::CountOf(bag, "repair_kit");
    const int fuelCountBefore = zl::items::CountOf(bag, "fuel_can");

    bool serviced = false;
    for (int attempt = 0; attempt < 2 && !serviced; ++attempt) {
        REQUIRE_MESSAGE(client.SendCommand(
                            MakeVehicleCommand(
                                zl::net::CommandType::ServiceVehicle, playerId,
                                vehicleId),
                            &error),
                        error);
        REQUIRE(PumpUntil(host, client, 4000, [&] {
            const auto& state = registry.get<zl::ecs::Vehicle>(vehicle);
            return state.fuel > fuelBeforeService + 1.0e-6f ||
                   state.condition > conditionBeforeService + 1.0e-6f;
        }));
        const auto& state = registry.get<zl::ecs::Vehicle>(vehicle);
        serviced = state.fuel > fuelBeforeService + 1.0e-6f;
    }
    CHECK(serviced);
    const bool consumedServiceItem =
        zl::items::CountOf(bag, "repair_kit") < repairCountBefore ||
        zl::items::CountOf(bag, "fuel_can") < fuelCountBefore;
    CHECK(consumedServiceItem);

    const std::uint64_t rejectedBefore =
        host.GetNetworkStats().commandsRejected;
    REQUIRE_MESSAGE(client.SendCommand(
                        MakeVehicleCommand(zl::net::CommandType::ExitVehicle,
                                           playerId, 0),
                        &error),
                    error);
    (void)PumpUntil(host, client, 500, [] { return false; });
    CHECK(host.GetNetworkStats().commandsRejected > rejectedBefore);
    CHECK_FALSE(client.PlayerState().vehicle.driving);

    REQUIRE_MESSAGE(client.SendCommand(
                        MakeVehicleCommand(zl::net::CommandType::EnterVehicle,
                                           playerId, vehicleId),
                        &error),
                    error);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return client.PlayerState().vehicle.driving;
    }));
    const zl::ecs::Position parkedAtDisconnect =
        registry.get<zl::ecs::Position>(vehicle);
    client.Close();
    CHECK(PumpHostUntil(host, 3000, [&] {
        return host.SessionCount() == 0;
    }));
    CHECK(registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == 0);
    {
        const auto& parked = registry.get<zl::ecs::Position>(vehicle);
        CHECK(parked.x == doctest::Approx(parkedAtDisconnect.x));
        CHECK(parked.y == doctest::Approx(parkedAtDisconnect.y));
    }

    host.Shutdown();
}

TEST_CASE("M25-T3-V4 双客户端载具复制：驾驶员、位置/速度、燃料与后备箱") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D323554335634ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;

    zl::game::NetworkClient first(clientOptions);
    zl::game::NetworkClient second(clientOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayerState() && second.HasPlayerState() &&
               host.SessionCount() == 2 && host.VehicleStableId() != 0;
    }));

    const std::uint64_t firstId = first.PlayerStableId();
    const std::uint64_t secondId = second.PlayerStableId();
    const std::uint64_t vehicleId = host.VehicleStableId();
    REQUIRE(firstId != 0);
    REQUIRE(secondId != 0);
    REQUIRE(vehicleId != 0);

    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity vehicle =
        host.Simulation().Ecs().FindById(vehicleId);
    REQUIRE(static_cast<bool>(vehicle != entt::null));
    REQUIRE(registry.all_of<zl::ecs::Position, zl::ecs::Velocity,
                            zl::ecs::Vehicle, zl::ecs::VehicleDriver,
                            zl::ecs::Container, zl::ecs::PlayerStorage>(
        vehicle));
    registry.get<zl::ecs::Container>(vehicle).items.push_back(
        zl::ecs::ItemInstance{"rag", 2, 1.0f});
    host.RunForTicks(1);

    REQUIRE(PumpUntil(host, first, second, 6000, [&] {
        const zl::game::ReplicatedActor* a = first.Replication().Find(vehicleId);
        const zl::game::ReplicatedActor* b = second.Replication().Find(vehicleId);
        return a != nullptr && b != nullptr && a->hasVehicle &&
               b->hasVehicle && a->hasStorage && b->hasStorage &&
               !a->storageContainer.items.empty() &&
               !b->storageContainer.items.empty();
    }));

    const zl::ecs::Position vehicleStart =
        registry.get<zl::ecs::Position>(vehicle);
    const float vehicleFuelBefore =
        registry.get<zl::ecs::Vehicle>(vehicle).fuel;
    REQUIRE(TeleportServerPlayer(host, firstId, vehicleStart.x - 1.0f,
                                 vehicleStart.y));
    zl::net::Command takeFromTrunk;
    takeFromTrunk.type = zl::net::CommandType::InventoryAction;
    takeFromTrunk.playerStableId = firstId;
    takeFromTrunk.payload.flags = static_cast<std::uint16_t>(
        zl::ui::InventoryActionType::Transfer) |
        static_cast<std::uint16_t>(2u << 8);
    takeFromTrunk.payload.area =
        static_cast<std::uint8_t>(zl::ui::InventoryArea::Loot);
    takeFromTrunk.payload.targetArea =
        static_cast<std::uint8_t>(zl::ui::InventoryArea::Backpack);
    takeFromTrunk.payload.index = 0;
    takeFromTrunk.payload.entityId = vehicleId;
    REQUIRE_MESSAGE(first.SendCommand(takeFromTrunk, &error), error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        const auto* actor = second.Replication().Find(vehicleId);
        return BagCount(first.PlayerState(), "rag") >= 2 &&
               actor != nullptr && actor->hasStorage &&
               actor->storageContainer.items.empty();
    }));
    REQUIRE_MESSAGE(
        first.SendCommand(MakeVehicleCommand(zl::net::CommandType::EnterVehicle,
                                             firstId, vehicleId),
                          &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        const auto* actor = second.Replication().Find(vehicleId);
        return first.PlayerState().vehicle.valid &&
               first.PlayerState().vehicle.driving &&
               actor != nullptr && actor->hasVehicle &&
               actor->vehicleDriverStableId == firstId;
    }));
    {
        const auto* actor = second.Replication().Find(vehicleId);
        REQUIRE(actor != nullptr);
        CHECK(actor->vehicle.fuel ==
              doctest::Approx(registry.get<zl::ecs::Vehicle>(vehicle).fuel));
        CHECK(actor->vehicle.condition ==
              doctest::Approx(
                  registry.get<zl::ecs::Vehicle>(vehicle).condition));
    }

    float worldDirX = 1.0f;
    float worldDirY = 0.0f;
    PickOpenDriveDirection(host, vehicleStart.x, vehicleStart.y, &worldDirX,
                           &worldDirY);
    float screenDirX = 1.0f;
    float screenDirY = 0.0f;
    WorldDirToScreen(worldDirX, worldDirY, &screenDirX, &screenDirY);
    REQUIRE_MESSAGE(first.SendCommand(
                        MakeMove(firstId, screenDirX, screenDirY), &error),
                    error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        const auto& position = registry.get<zl::ecs::Position>(vehicle);
        const float dx = position.x - vehicleStart.x;
        const float dy = position.y - vehicleStart.y;
        const auto* actor = second.Replication().Find(vehicleId);
        return dx * dx + dy * dy > 0.20f * 0.20f && actor != nullptr &&
               actor->hasPosition && actor->hasVelocity &&
               std::hypot(actor->velocity.x, actor->velocity.y) > 0.05f;
    }));
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        const auto& position = registry.get<zl::ecs::Position>(vehicle);
        const auto& state = registry.get<zl::ecs::Vehicle>(vehicle);
        const auto* actor = second.Replication().Find(vehicleId);
        return actor != nullptr && actor->hasPosition &&
               std::fabs(actor->position.x - position.x) < 0.05f &&
               std::fabs(actor->position.y - position.y) < 0.05f &&
               std::fabs(actor->vehicle.fuel - state.fuel) < 0.001f;
    }));
    CHECK(registry.get<zl::ecs::Vehicle>(vehicle).fuel < vehicleFuelBefore);

    REQUIRE_MESSAGE(first.SendCommand(MakeMove(firstId, 0.0f, 0.0f), &error),
                    error);
    host.RunForTicks(2);
    REQUIRE_MESSAGE(
        first.SendCommand(MakeVehicleCommand(zl::net::CommandType::ExitVehicle,
                                             firstId, vehicleId),
                          &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        const auto* actor = second.Replication().Find(vehicleId);
        return !first.PlayerState().vehicle.driving && actor != nullptr &&
               actor->hasVehicle && actor->vehicleDriverStableId == 0;
    }));
    CHECK(registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == 0);

    second.Close();
    first.Close();
    host.Shutdown();
}

TEST_CASE("M25-T3-V5 驾驶重连、座位接管与越权隔离") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path profilePath =
        std::filesystem::temp_directory_path() /
        ("zl_m25_t3_v5_" +
         std::to_string(static_cast<unsigned long long>(
             Clock::now().time_since_epoch().count())) +
         ".bin");
    std::filesystem::remove(profilePath);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D323554335635ull;
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
    firstOptions.playerIdentity = "m25-t3-v5-alpha";
    firstOptions.playerName = "Alpha";
    zl::game::NetworkClient::Options secondOptions = firstOptions;
    secondOptions.playerIdentity = "m25-t3-v5-bravo";
    secondOptions.playerName = "Bravo";

    zl::game::NetworkClient first(firstOptions);
    zl::game::NetworkClient second(secondOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayerState() && second.HasPlayerState() &&
               host.SessionCount() == 2 && host.VehicleStableId() != 0;
    }));

    const std::uint64_t firstId = first.PlayerStableId();
    const std::uint64_t secondId = second.PlayerStableId();
    const std::uint64_t vehicleId = host.VehicleStableId();
    REQUIRE(firstId != 0);
    REQUIRE(secondId != 0);
    REQUIRE(vehicleId != 0);
    REQUIRE(firstId != secondId);

    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity vehicle =
        host.Simulation().Ecs().FindById(vehicleId);
    REQUIRE(static_cast<bool>(vehicle != entt::null));
    registry.get<zl::ecs::Container>(vehicle).items.push_back(
        zl::ecs::ItemInstance{"rag", 2, 1.0f});
    const zl::ecs::Position vehicleStart =
        registry.get<zl::ecs::Position>(vehicle);
    REQUIRE(TeleportServerPlayer(host, firstId, vehicleStart.x - 1.0f,
                                 vehicleStart.y));
    REQUIRE(TeleportServerPlayer(host, secondId, vehicleStart.x + 1.0f,
                                 vehicleStart.y));

    REQUIRE_MESSAGE(
        first.SendCommand(MakeVehicleCommand(zl::net::CommandType::EnterVehicle,
                                             firstId, vehicleId),
                          &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.PlayerState().vehicle.driving &&
               registry.get<zl::ecs::VehicleDriver>(vehicle).stableId ==
                   firstId;
    }));

    const std::uint64_t rejectedBeforeOccupied =
        host.GetNetworkStats().commandsRejected;
    REQUIRE_MESSAGE(
        second.SendCommand(MakeVehicleCommand(zl::net::CommandType::EnterVehicle,
                                              secondId, vehicleId),
                           &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 1500, [&] {
        return host.GetNetworkStats().commandsRejected >
                   rejectedBeforeOccupied &&
               second.PlayerState().status.find("占用") != std::string::npos;
    }));
    CHECK_FALSE(second.PlayerState().vehicle.driving);
    CHECK(registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == firstId);

    const zl::ecs::Entity firstPlayer =
        host.Simulation().Ecs().FindById(firstId);
    REQUIRE(static_cast<bool>(firstPlayer != entt::null));
    auto& firstBag = registry.get<zl::ecs::Container>(firstPlayer);
    firstBag.items.push_back(zl::ecs::ItemInstance{"repair_kit", 1, 1.0f});
    firstBag.items.push_back(zl::ecs::ItemInstance{"fuel_can", 1, 1.0f});
    const int repairBefore = zl::items::CountOf(firstBag, "repair_kit");
    const int fuelBefore = zl::items::CountOf(firstBag, "fuel_can");
    const float conditionBeforeDriving =
        registry.get<zl::ecs::Vehicle>(vehicle).condition;
    const float fuelBeforeDriving = registry.get<zl::ecs::Vehicle>(vehicle).fuel;
    const std::uint64_t unsupportedBeforeService =
        host.GetNetworkStats().unsupportedCommands;
    REQUIRE_MESSAGE(
        first.SendCommand(MakeVehicleCommand(
                              zl::net::CommandType::ServiceVehicle, firstId,
                              vehicleId),
                          &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 1500, [&] {
        return host.GetNetworkStats().unsupportedCommands >
                   unsupportedBeforeService &&
               first.PlayerState().status.find("驾驶中") != std::string::npos;
    }));
    CHECK(registry.get<zl::ecs::Vehicle>(vehicle).condition ==
          doctest::Approx(conditionBeforeDriving));
    CHECK(registry.get<zl::ecs::Vehicle>(vehicle).fuel ==
          doctest::Approx(fuelBeforeDriving));
    CHECK(zl::items::CountOf(firstBag, "repair_kit") == repairBefore);
    CHECK(zl::items::CountOf(firstBag, "fuel_can") == fuelBefore);

    const std::uint64_t unsupportedBeforeAttack =
        host.GetNetworkStats().unsupportedCommands;
    zl::net::Command attack;
    attack.type = zl::net::CommandType::Attack;
    attack.playerStableId = firstId;
    REQUIRE_MESSAGE(first.SendCommand(attack, &error), error);
    REQUIRE(PumpUntil(host, first, second, 1500, [&] {
        return host.GetNetworkStats().unsupportedCommands >
               unsupportedBeforeAttack;
    }));
    CHECK(registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == firstId);

    float worldDirX = 1.0f;
    float worldDirY = 0.0f;
    PickOpenDriveDirection(host, vehicleStart.x, vehicleStart.y, &worldDirX,
                           &worldDirY);
    float screenDirX = 1.0f;
    float screenDirY = 0.0f;
    WorldDirToScreen(worldDirX, worldDirY, &screenDirX, &screenDirY);
    REQUIRE_MESSAGE(first.SendCommand(
                        MakeMove(firstId, screenDirX, screenDirY), &error),
                    error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        const auto& position = registry.get<zl::ecs::Position>(vehicle);
        const float dx = position.x - vehicleStart.x;
        const float dy = position.y - vehicleStart.y;
        return dx * dx + dy * dy > 0.20f * 0.20f;
    }));
    REQUIRE_MESSAGE(first.SendCommand(MakeMove(firstId, 0.0f, 0.0f), &error),
                    error);
    REQUIRE(PumpUntil(host, first, second, 4000, [&] {
        const auto& velocity = registry.get<zl::ecs::Velocity>(vehicle);
        return std::hypot(velocity.x, velocity.y) < 0.05f;
    }));

    const zl::ecs::Position vehicleSaved =
        registry.get<zl::ecs::Position>(vehicle);
    const zl::ecs::Vehicle vehicleStateSaved =
        registry.get<zl::ecs::Vehicle>(vehicle);
    CHECK(zl::items::CountOf(registry.get<zl::ecs::Container>(vehicle),
                             "rag") == 2);

    const zl::ecs::Entity secondPlayer =
        host.Simulation().Ecs().FindById(secondId);
    REQUIRE(static_cast<bool>(secondPlayer != entt::null));
    const zl::ecs::Position secondStart =
        registry.get<zl::ecs::Position>(secondPlayer);
    PickOpenDriveDirection(host, secondStart.x, secondStart.y, &worldDirX,
                           &worldDirY);
    WorldDirToScreen(worldDirX, worldDirY, &screenDirX, &screenDirY);
    REQUIRE_MESSAGE(second.SendCommand(
                        MakeMove(secondId, screenDirX, screenDirY), &error),
                    error);
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        const auto& position = registry.get<zl::ecs::Position>(secondPlayer);
        const float dx = position.x - secondStart.x;
        const float dy = position.y - secondStart.y;
        return dx * dx + dy * dy > 0.01f;
    }));
    REQUIRE_MESSAGE(second.SendCommand(MakeMove(secondId, 0.0f, 0.0f), &error),
                    error);
    {
        const auto& position = registry.get<zl::ecs::Position>(vehicle);
        CHECK(std::fabs(position.x - vehicleSaved.x) < 0.05f);
        CHECK(std::fabs(position.y - vehicleSaved.y) < 0.05f);
    }

    zl::game::NetworkClient reconnected(firstOptions);
    REQUIRE(reconnected.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, reconnected, 8000, [&] {
        return reconnected.IsConnected() && reconnected.HasPlayerState() &&
               !first.IsConnected() && host.SessionCount() == 2;
    }));
    CHECK(reconnected.PlayerStableId() == firstId);
    CHECK_FALSE(reconnected.PlayerState().vehicle.driving);
    CHECK(reconnected.PlayerState().vehicle.vehicleStableId == vehicleId);
    CHECK(registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == 0);
    CHECK(registry.get<zl::ecs::Position>(vehicle).x ==
          doctest::Approx(vehicleSaved.x).epsilon(0.01));
    CHECK(registry.get<zl::ecs::Position>(vehicle).y ==
          doctest::Approx(vehicleSaved.y).epsilon(0.01));
    CHECK(registry.get<zl::ecs::Vehicle>(vehicle).fuel ==
          doctest::Approx(vehicleStateSaved.fuel).epsilon(0.001));
    CHECK(registry.get<zl::ecs::Vehicle>(vehicle).condition ==
          doctest::Approx(vehicleStateSaved.condition).epsilon(0.001));
    CHECK(zl::items::CountOf(registry.get<zl::ecs::Container>(vehicle),
                             "rag") == 2);

    const zl::ecs::Entity restoredPlayer =
        host.Simulation().Ecs().FindById(firstId);
    REQUIRE(static_cast<bool>(restoredPlayer != entt::null));
    {
        const auto& position = registry.get<zl::ecs::Position>(restoredPlayer);
        const float distance = std::hypot(position.x - vehicleSaved.x,
                                          position.y - vehicleSaved.y);
        CHECK(distance > 0.65f);
    }

    REQUIRE(TeleportServerPlayer(host, firstId, vehicleSaved.x - 1.0f,
                                 vehicleSaved.y));
    REQUIRE_MESSAGE(reconnected.SendCommand(
                        MakeVehicleCommand(zl::net::CommandType::EnterVehicle,
                                           firstId, vehicleId),
                        &error),
                    error);
    REQUIRE(PumpUntil(host, second, reconnected, 5000, [&] {
        return reconnected.PlayerState().vehicle.driving &&
               registry.get<zl::ecs::VehicleDriver>(vehicle).stableId ==
                   firstId;
    }));
    REQUIRE_MESSAGE(reconnected.SendCommand(
                        MakeVehicleCommand(zl::net::CommandType::ExitVehicle,
                                           firstId, 0),
                        &error),
                    error);
    REQUIRE(PumpUntil(host, second, reconnected, 5000, [&] {
        return !reconnected.PlayerState().vehicle.driving &&
               registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == 0;
    }));

    const zl::ecs::Position vehicleForBravo =
        registry.get<zl::ecs::Position>(vehicle);
    REQUIRE(TeleportServerPlayer(host, secondId, vehicleForBravo.x - 1.0f,
                                 vehicleForBravo.y));
    REQUIRE_MESSAGE(
        second.SendCommand(MakeVehicleCommand(zl::net::CommandType::EnterVehicle,
                                              secondId, vehicleId),
                           &error),
        error);
    REQUIRE(PumpUntil(host, second, reconnected, 5000, [&] {
        return second.PlayerState().vehicle.driving &&
               registry.get<zl::ecs::VehicleDriver>(vehicle).stableId ==
                   secondId;
    }));

    first.Close();
    second.Close();
    reconnected.Close();
    host.Shutdown();
    std::filesystem::remove(profilePath);
}

TEST_CASE("M25-T4-V22/V23/V24/V25/V26 双客户端载具乘客位：上车、跟随、接管、让座、后备箱、快捷使用与下车") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D32355434563232ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;

    zl::game::NetworkClient first(clientOptions);
    zl::game::NetworkClient second(clientOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayerState() && second.HasPlayerState() &&
               host.SessionCount() == 2 && host.VehicleStableId() != 0;
    }));

    const std::uint64_t firstId = first.PlayerStableId();
    const std::uint64_t secondId = second.PlayerStableId();
    const std::uint64_t vehicleId = host.VehicleStableId();
    REQUIRE(firstId != 0);
    REQUIRE(secondId != 0);
    REQUIRE(vehicleId != 0);
    REQUIRE(firstId != secondId);

    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity vehicle =
        host.Simulation().Ecs().FindById(vehicleId);
    REQUIRE(static_cast<bool>(vehicle != entt::null));
    REQUIRE(registry.all_of<zl::ecs::Vehicle, zl::ecs::VehicleDriver>(
        vehicle));
    const zl::ecs::Position vehicleStart =
        registry.get<zl::ecs::Position>(vehicle);
    REQUIRE(TeleportServerPlayer(host, firstId, vehicleStart.x - 1.0f,
                                 vehicleStart.y));
    REQUIRE(TeleportServerPlayer(host, secondId, vehicleStart.x + 1.0f,
                                 vehicleStart.y));

    REQUIRE_MESSAGE(
        first.SendCommand(MakeVehicleCommand(zl::net::CommandType::EnterVehicle,
                                             firstId, vehicleId),
                          &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.PlayerState().vehicle.driving &&
               registry.get<zl::ecs::VehicleDriver>(vehicle).stableId ==
                   firstId;
    }));

    REQUIRE_MESSAGE(
        second.SendCommand(
            MakeVehicleCommand(zl::net::CommandType::EnterVehiclePassenger,
                               secondId, vehicleId),
            &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        const auto* a = first.Replication().Find(vehicleId);
        const auto* b = second.Replication().Find(vehicleId);
        return second.PlayerState().vehicle.passenger &&
               !second.PlayerState().vehicle.driving &&
               registry.all_of<zl::ecs::VehiclePassenger>(vehicle) &&
               registry.get<zl::ecs::VehiclePassenger>(vehicle).stableId ==
                   secondId &&
               a != nullptr && b != nullptr && a->hasVehicle &&
               b->hasVehicle &&
               a->vehicleDriverStableId == firstId &&
               b->vehicleDriverStableId == firstId &&
               a->vehiclePassengerStableId == secondId &&
               b->vehiclePassengerStableId == secondId;
    }));
    CHECK(first.PlayerState().vehicle.driving);
    CHECK_FALSE(first.PlayerState().vehicle.passenger);
    CHECK(registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == firstId);
    CHECK(registry.get<zl::ecs::VehiclePassenger>(vehicle).stableId ==
          secondId);
    const zl::ecs::Entity firstPlayer =
        host.Simulation().Ecs().FindById(firstId);
    const zl::ecs::Entity secondPlayer =
        host.Simulation().Ecs().FindById(secondId);
    REQUIRE(static_cast<bool>(firstPlayer != entt::null));
    REQUIRE(static_cast<bool>(secondPlayer != entt::null));
    CHECK(registry.all_of<zl::ecs::VehicleOccupant>(firstPlayer));
    CHECK(registry.all_of<zl::ecs::VehicleOccupant>(secondPlayer));
    CHECK(registry.get<zl::ecs::VehicleOccupant>(secondPlayer)
              .damageReduction == doctest::Approx(0.85f));
    {
        const auto& passengerPosition =
            registry.get<zl::ecs::Position>(secondPlayer);
        CHECK(passengerPosition.x == doctest::Approx(vehicleStart.x));
        CHECK(passengerPosition.y == doctest::Approx(vehicleStart.y));
    }

    {
        auto& trunk = registry.get<zl::ecs::Container>(vehicle);
        auto& passengerBag = registry.get<zl::ecs::Container>(secondPlayer);
        trunk.items.push_back(zl::ecs::ItemInstance{"bandage", 2, 1.0f});
        const int passengerBandages =
            zl::items::CountOf(passengerBag, "bandage");
        const int trunkBandages = zl::items::CountOf(trunk, "bandage");

        zl::net::Command takeAll;
        takeAll.type = zl::net::CommandType::InventoryAction;
        takeAll.playerStableId = secondId;
        takeAll.payload.flags = static_cast<std::uint16_t>(
            zl::ui::InventoryActionType::TakeAll);
        takeAll.payload.area =
            static_cast<std::uint8_t>(zl::ui::InventoryArea::Loot);
        takeAll.payload.targetArea =
            static_cast<std::uint8_t>(zl::ui::InventoryArea::Backpack);
        takeAll.payload.entityId = vehicleId;

        zl::net::Command driverTakeAll = takeAll;
        driverTakeAll.playerStableId = firstId;
        const std::uint64_t driverUnsupportedBefore =
            host.GetNetworkStats().unsupportedCommands;
        REQUIRE_MESSAGE(first.SendCommand(driverTakeAll, &error), error);
        REQUIRE(PumpUntil(host, first, second, 1500, [&] {
            return host.GetNetworkStats().unsupportedCommands >
                   driverUnsupportedBefore;
        }));
        CHECK(zl::items::CountOf(trunk, "bandage") == trunkBandages);
        CHECK(zl::items::CountOf(passengerBag, "bandage") ==
              passengerBandages);

        REQUIRE_MESSAGE(second.SendCommand(takeAll, &error), error);
        REQUIRE(PumpUntil(host, first, second, 5000, [&] {
            return zl::items::CountOf(trunk, "bandage") == 0 &&
                   zl::items::CountOf(passengerBag, "bandage") ==
                       passengerBandages + trunkBandages &&
                   second.PlayerState().status.find("取出") !=
                       std::string::npos;
        }));
    }

    {
        auto& passengerBag = registry.get<zl::ecs::Container>(secondPlayer);
        auto& passengerNeeds =
            registry.get_or_emplace<zl::ecs::Needs>(secondPlayer);
        passengerBag.items.insert(
            passengerBag.items.begin(),
            zl::ecs::ItemInstance{"canned_beans", 2, 1.0f});
        passengerNeeds.hunger = 1.0f;

        zl::net::Command driverUse;
        driverUse.type = zl::net::CommandType::UseBag;
        driverUse.playerStableId = firstId;
        driverUse.payload.index = -1;
        const std::uint64_t driverUseRejectedBefore =
            host.GetNetworkStats().unsupportedCommands;
        REQUIRE_MESSAGE(first.SendCommand(driverUse, &error), error);
        REQUIRE(PumpUntil(host, first, second, 1500, [&] {
            return host.GetNetworkStats().unsupportedCommands >
                   driverUseRejectedBefore;
        }));
        CHECK(zl::items::CountOf(passengerBag, "canned_beans") == 2);
        CHECK(passengerNeeds.hunger == doctest::Approx(1.0f));

        zl::net::Command passengerUse = driverUse;
        passengerUse.playerStableId = secondId;
        REQUIRE_MESSAGE(second.SendCommand(passengerUse, &error), error);
        REQUIRE(PumpUntil(host, first, second, 5000, [&] {
            return zl::items::CountOf(passengerBag, "canned_beans") == 1 &&
                   passengerNeeds.hunger < 1.0f &&
                   second.PlayerState().status.find("使用") !=
                       std::string::npos;
        }));
        CHECK(passengerNeeds.hunger < 1.0f);

        passengerBag.items.push_back(
            zl::ecs::ItemInstance{"kitchen_knife", 1, 1.0f});
        zl::net::Command passengerEquip;
        passengerEquip.type = zl::net::CommandType::EquipBag;
        passengerEquip.playerStableId = secondId;
        passengerEquip.payload.index = -1;
        REQUIRE_MESSAGE(second.SendCommand(passengerEquip, &error), error);
        REQUIRE(PumpUntil(host, first, second, 5000, [&] {
            auto& equipment = registry.get<zl::ecs::Equipment>(secondPlayer);
            const auto& primary =
                equipment.slots[static_cast<std::size_t>(
                    zl::ecs::EquipSlot::PrimaryHand)];
            return primary.has_value() &&
                   primary->defId == "kitchen_knife" &&
                   second.PlayerState().status.find("装备") !=
                       std::string::npos;
        }));
    }

    float worldDirX = 1.0f;
    float worldDirY = 0.0f;
    PickOpenDriveDirection(host, vehicleStart.x, vehicleStart.y, &worldDirX,
                           &worldDirY);
    float screenDirX = 1.0f;
    float screenDirY = 0.0f;
    WorldDirToScreen(worldDirX, worldDirY, &screenDirX, &screenDirY);
    REQUIRE_MESSAGE(first.SendCommand(
                        MakeMove(firstId, screenDirX, screenDirY), &error),
                    error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        const auto& position = registry.get<zl::ecs::Position>(vehicle);
        const float dx = position.x - vehicleStart.x;
        const float dy = position.y - vehicleStart.y;
        return dx * dx + dy * dy > 0.20f * 0.20f;
    }));
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        const auto& vehiclePosition =
            registry.get<zl::ecs::Position>(vehicle);
        const auto& passengerPosition =
            registry.get<zl::ecs::Position>(secondPlayer);
        return std::fabs(passengerPosition.x - vehiclePosition.x) < 0.05f &&
               std::fabs(passengerPosition.y - vehiclePosition.y) < 0.05f;
    }));

    REQUIRE_MESSAGE(first.SendCommand(MakeMove(firstId, 0.0f, 0.0f), &error),
                    error);
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        const auto* velocity = registry.try_get<zl::ecs::Velocity>(vehicle);
        return velocity != nullptr &&
               std::hypot(velocity->x, velocity->y) < 0.05f;
    }));
    const zl::ecs::Position parked = registry.get<zl::ecs::Position>(vehicle);
    const std::uint64_t unsupportedBefore =
        host.GetNetworkStats().unsupportedCommands;
    REQUIRE_MESSAGE(second.SendCommand(MakeMove(secondId, 1.0f, 0.0f), &error),
                    error);
    REQUIRE(PumpUntil(host, first, second, 1500, [&] {
        return host.GetNetworkStats().unsupportedCommands > unsupportedBefore;
    }));
    host.RunForTicks(4);
    CHECK(registry.get<zl::ecs::Position>(vehicle).x ==
          doctest::Approx(parked.x));
    CHECK(registry.get<zl::ecs::Position>(vehicle).y ==
          doctest::Approx(parked.y));
    CHECK(second.PlayerState().status.find("乘客") != std::string::npos);

    REQUIRE_MESSAGE(
        second.SendCommand(MakeVehicleCommand(zl::net::CommandType::ExitVehicle,
                                              secondId, 0),
                          &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return !second.PlayerState().vehicle.passenger &&
               !registry.all_of<zl::ecs::VehiclePassenger>(vehicle) &&
               first.PlayerState().vehicle.driving;
    }));

    REQUIRE(TeleportServerPlayer(host, secondId, parked.x + 1.0f,
                                 parked.y));
    REQUIRE_MESSAGE(
        second.SendCommand(
            MakeVehicleCommand(zl::net::CommandType::EnterVehiclePassenger,
                               secondId, vehicleId),
            &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return second.PlayerState().vehicle.passenger &&
               registry.get<zl::ecs::VehiclePassenger>(vehicle).stableId ==
                   secondId;
    }));

    REQUIRE_MESSAGE(
        first.SendCommand(MakeVehicleCommand(zl::net::CommandType::ExitVehicle,
                                             firstId, 0),
                          &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return !first.PlayerState().vehicle.driving &&
               registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == 0 &&
               second.PlayerState().vehicle.passenger &&
               registry.get<zl::ecs::VehiclePassenger>(vehicle).stableId ==
                   secondId;
    }));
    {
        const auto* velocity = registry.try_get<zl::ecs::Velocity>(vehicle);
        CHECK(velocity != nullptr);
        if (velocity != nullptr) {
            CHECK(velocity->x == doctest::Approx(0.0f));
            CHECK(velocity->y == doctest::Approx(0.0f));
        }
    }

    REQUIRE_MESSAGE(
        second.SendCommand(
            MakeVehicleCommand(zl::net::CommandType::TakeDriverSeat,
                               secondId, vehicleId),
            &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        const auto* a = first.Replication().Find(vehicleId);
        const auto* b = second.Replication().Find(vehicleId);
        return second.PlayerState().vehicle.driving &&
               !second.PlayerState().vehicle.passenger &&
               registry.get<zl::ecs::VehicleDriver>(vehicle).stableId ==
                   secondId &&
               !registry.all_of<zl::ecs::VehiclePassenger>(vehicle) &&
               a != nullptr && b != nullptr && a->hasVehicle &&
               b->hasVehicle &&
               a->vehicleDriverStableId == secondId &&
               b->vehicleDriverStableId == secondId &&
               a->vehiclePassengerStableId == 0 &&
               b->vehiclePassengerStableId == 0;
    }));
    CHECK(registry.all_of<zl::ecs::VehicleOccupant>(secondPlayer));
    CHECK(registry.get<zl::ecs::VehicleOccupant>(secondPlayer)
              .damageReduction == doctest::Approx(0.85f));
    CHECK(second.PlayerState().status.find("接管") != std::string::npos);

    const zl::ecs::Position takeoverStart =
        registry.get<zl::ecs::Position>(vehicle);
    REQUIRE_MESSAGE(second.SendCommand(
                        MakeMove(secondId, screenDirX, screenDirY), &error),
                    error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        const auto& position = registry.get<zl::ecs::Position>(vehicle);
        const float dx = position.x - takeoverStart.x;
        const float dy = position.y - takeoverStart.y;
        return dx * dx + dy * dy > 0.20f * 0.20f;
    }));
    REQUIRE_MESSAGE(second.SendCommand(MakeMove(secondId, 0.0f, 0.0f),
                                       &error),
                    error);
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        const auto* velocity = registry.try_get<zl::ecs::Velocity>(vehicle);
        return velocity != nullptr &&
               std::hypot(velocity->x, velocity->y) < 0.05f;
    }));

    {
        const auto& parkedAfterTakeover =
            registry.get<zl::ecs::Position>(vehicle);
        REQUIRE(TeleportServerPlayer(host, firstId,
                                     parkedAfterTakeover.x + 1.0f,
                                     parkedAfterTakeover.y));
    }
    REQUIRE_MESSAGE(
        first.SendCommand(
            MakeVehicleCommand(zl::net::CommandType::EnterVehiclePassenger,
                               firstId, vehicleId),
            &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.PlayerState().vehicle.passenger &&
               !first.PlayerState().vehicle.driving &&
               second.PlayerState().vehicle.driving &&
               registry.get<zl::ecs::VehicleDriver>(vehicle).stableId ==
                   secondId &&
               registry.get<zl::ecs::VehiclePassenger>(vehicle).stableId ==
                   firstId;
    }));

    REQUIRE_MESSAGE(
        second.SendCommand(MakeVehicleCommand(
                               zl::net::CommandType::GiveDriverSeat,
                               secondId, vehicleId),
                           &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        const auto* a = first.Replication().Find(vehicleId);
        const auto* b = second.Replication().Find(vehicleId);
        return first.PlayerState().vehicle.driving &&
               !first.PlayerState().vehicle.passenger &&
               second.PlayerState().vehicle.passenger &&
               !second.PlayerState().vehicle.driving &&
               registry.get<zl::ecs::VehicleDriver>(vehicle).stableId ==
                   firstId &&
               registry.get<zl::ecs::VehiclePassenger>(vehicle).stableId ==
                   secondId &&
               a != nullptr && b != nullptr && a->hasVehicle &&
               b->hasVehicle &&
               a->vehicleDriverStableId == firstId &&
               b->vehicleDriverStableId == firstId &&
               a->vehiclePassengerStableId == secondId &&
               b->vehiclePassengerStableId == secondId;
    }));
    CHECK(registry.all_of<zl::ecs::VehicleOccupant>(firstPlayer));
    CHECK(registry.all_of<zl::ecs::VehicleOccupant>(secondPlayer));
    CHECK(first.PlayerState().status.find("接管") != std::string::npos);
    CHECK(second.PlayerState().status.find("让出") != std::string::npos);
    {
        const auto* velocity = registry.try_get<zl::ecs::Velocity>(vehicle);
        CHECK(velocity != nullptr);
        if (velocity != nullptr) {
            CHECK(velocity->x == doctest::Approx(0.0f));
            CHECK(velocity->y == doctest::Approx(0.0f));
        }
    }

    const std::uint64_t unsupportedAfterHandoff =
        host.GetNetworkStats().unsupportedCommands;
    REQUIRE_MESSAGE(second.SendCommand(MakeMove(secondId, 1.0f, 0.0f),
                                       &error),
                    error);
    REQUIRE(PumpUntil(host, first, second, 1500, [&] {
        return host.GetNetworkStats().unsupportedCommands >
               unsupportedAfterHandoff;
    }));

    REQUIRE_MESSAGE(
        first.SendCommand(MakeVehicleCommand(zl::net::CommandType::ExitVehicle,
                                             firstId, 0),
                          &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return !first.PlayerState().vehicle.driving &&
               second.PlayerState().vehicle.passenger &&
               registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == 0 &&
               registry.get<zl::ecs::VehiclePassenger>(vehicle).stableId ==
                   secondId;
    }));

    first.Close();
    second.Close();
    host.Shutdown();
}

TEST_CASE("M25-T4-V27 三客户端载具双乘客位：占用、接管与让座") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D32355434563237ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;

    zl::game::NetworkClient first(clientOptions);
    zl::game::NetworkClient second(clientOptions);
    zl::game::NetworkClient third(clientOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(third.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, third, 8000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               third.IsConnected() && first.HasPlayerState() &&
               second.HasPlayerState() && third.HasPlayerState() &&
               host.SessionCount() == 3 && host.VehicleStableId() != 0;
    }));

    const std::uint64_t firstId = first.PlayerStableId();
    const std::uint64_t secondId = second.PlayerStableId();
    const std::uint64_t thirdId = third.PlayerStableId();
    const std::uint64_t vehicleId = host.VehicleStableId();
    REQUIRE(firstId != 0);
    REQUIRE(secondId != 0);
    REQUIRE(thirdId != 0);
    REQUIRE(vehicleId != 0);
    REQUIRE(firstId != secondId);
    REQUIRE(firstId != thirdId);
    REQUIRE(secondId != thirdId);

    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity vehicle =
        host.Simulation().Ecs().FindById(vehicleId);
    REQUIRE(static_cast<bool>(vehicle != entt::null));
    REQUIRE(registry.all_of<zl::ecs::Vehicle, zl::ecs::VehicleDriver>(
        vehicle));
    const zl::ecs::Position vehicleStart =
        registry.get<zl::ecs::Position>(vehicle);
    REQUIRE(TeleportServerPlayer(host, firstId, vehicleStart.x - 1.0f,
                                 vehicleStart.y));
    REQUIRE(TeleportServerPlayer(host, secondId, vehicleStart.x + 1.0f,
                                 vehicleStart.y));
    REQUIRE(TeleportServerPlayer(host, thirdId, vehicleStart.x,
                                 vehicleStart.y + 1.0f));

    REQUIRE_MESSAGE(
        first.SendCommand(MakeVehicleCommand(zl::net::CommandType::EnterVehicle,
                                             firstId, vehicleId),
                          &error),
        error);
    REQUIRE(PumpUntil(host, first, second, third, 5000, [&] {
        return first.PlayerState().vehicle.driving &&
               registry.get<zl::ecs::VehicleDriver>(vehicle).stableId ==
                   firstId;
    }));

    REQUIRE_MESSAGE(
        second.SendCommand(
            MakeVehicleCommand(zl::net::CommandType::EnterVehiclePassenger,
                               secondId, vehicleId),
            &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return second.PlayerState().vehicle.passenger &&
               registry.all_of<zl::ecs::VehiclePassenger>(vehicle) &&
               registry.get<zl::ecs::VehiclePassenger>(vehicle).stableId ==
                   secondId &&
               registry.get<zl::ecs::VehiclePassenger>(vehicle)
                       .secondStableId == 0;
    }));
    REQUIRE_MESSAGE(
        third.SendCommand(
            MakeVehicleCommand(zl::net::CommandType::EnterVehiclePassenger,
                               thirdId, vehicleId),
            &error),
        error);
    const bool passengersReady =
        PumpUntil(host, first, second, third, 10000, [&] {
            const auto* a = first.Replication().Find(vehicleId);
            const auto* b = second.Replication().Find(vehicleId);
            const auto* c = third.Replication().Find(vehicleId);
            return second.PlayerState().vehicle.passenger &&
                   third.PlayerState().vehicle.passenger &&
                   !second.PlayerState().vehicle.driving &&
                   !third.PlayerState().vehicle.driving &&
                   registry.all_of<zl::ecs::VehiclePassenger>(vehicle) &&
                   registry.get<zl::ecs::VehiclePassenger>(vehicle)
                           .stableId == secondId &&
                   registry.get<zl::ecs::VehiclePassenger>(vehicle)
                           .secondStableId == thirdId &&
                   a != nullptr && b != nullptr && c != nullptr &&
                   a->hasVehicle && b->hasVehicle && c->hasVehicle &&
                   a->vehicleDriverStableId == firstId &&
                   b->vehicleDriverStableId == firstId &&
                   c->vehicleDriverStableId == firstId &&
                   a->vehiclePassengerStableId == secondId &&
                   b->vehiclePassengerStableId == secondId &&
                   c->vehiclePassengerStableId == secondId &&
                   a->vehicleSecondPassengerStableId == thirdId &&
                   b->vehicleSecondPassengerStableId == thirdId &&
                   c->vehicleSecondPassengerStableId == thirdId;
        });
    const auto* debugA = first.Replication().Find(vehicleId);
    const auto* debugB = second.Replication().Find(vehicleId);
    const auto* debugC = third.Replication().Find(vehicleId);
    INFO("secondPassenger=" << second.PlayerState().vehicle.passenger
                             << " thirdPassenger="
                             << third.PlayerState().vehicle.passenger
                             << " component="
                             << registry.all_of<zl::ecs::VehiclePassenger>(
                                    vehicle));
    INFO("firstId=" << firstId << " secondId=" << secondId
                     << " thirdId=" << thirdId);
    INFO("aDriver=" << (debugA ? debugA->vehicleDriverStableId : 0)
                     << " aPassenger="
                     << (debugA ? debugA->vehiclePassengerStableId : 0)
                     << " aSecond="
                     << (debugA ? debugA->vehicleSecondPassengerStableId : 0));
    INFO("bDriver=" << (debugB ? debugB->vehicleDriverStableId : 0)
                     << " bPassenger="
                     << (debugB ? debugB->vehiclePassengerStableId : 0)
                     << " bSecond="
                     << (debugB ? debugB->vehicleSecondPassengerStableId : 0));
    INFO("cDriver=" << (debugC ? debugC->vehicleDriverStableId : 0)
                     << " cPassenger="
                     << (debugC ? debugC->vehiclePassengerStableId : 0)
                     << " cSecond="
                     << (debugC ? debugC->vehicleSecondPassengerStableId : 0));
    INFO("status second=" << second.PlayerState().status
                           << " third=" << third.PlayerState().status);
    REQUIRE(passengersReady);
    CHECK_FALSE(first.PlayerState().vehicle.passenger);
    CHECK(registry.get<zl::ecs::VehiclePassenger>(vehicle).stableId ==
          secondId);
    CHECK(registry.get<zl::ecs::VehiclePassenger>(vehicle).secondStableId ==
          thirdId);
    const zl::ecs::Entity firstPlayer =
        host.Simulation().Ecs().FindById(firstId);
    const zl::ecs::Entity secondPlayer =
        host.Simulation().Ecs().FindById(secondId);
    const zl::ecs::Entity thirdPlayer =
        host.Simulation().Ecs().FindById(thirdId);
    REQUIRE(static_cast<bool>(firstPlayer != entt::null));
    REQUIRE(static_cast<bool>(secondPlayer != entt::null));
    REQUIRE(static_cast<bool>(thirdPlayer != entt::null));
    CHECK(registry.all_of<zl::ecs::VehicleOccupant>(firstPlayer));
    CHECK(registry.all_of<zl::ecs::VehicleOccupant>(secondPlayer));
    CHECK(registry.all_of<zl::ecs::VehicleOccupant>(thirdPlayer));
    CHECK(registry.get<zl::ecs::VehicleOccupant>(thirdPlayer)
              .damageReduction == doctest::Approx(0.85f));
    for (const zl::ecs::Entity player : {secondPlayer, thirdPlayer}) {
        const auto& position = registry.get<zl::ecs::Position>(player);
        CHECK(position.x == doctest::Approx(vehicleStart.x));
        CHECK(position.y == doctest::Approx(vehicleStart.y));
    }

    REQUIRE_MESSAGE(second.SendCommand(
                        MakeVehicleCommand(
                            zl::net::CommandType::TakeDriverSeat,
                            secondId, vehicleId),
                        &error),
                    error);
    REQUIRE(PumpUntil(host, first, second, third, 1500, [&] {
        return second.PlayerState().status.find("驾驶位") !=
               std::string::npos;
    }));
    host.RunForTicks(3);
    CHECK(registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == firstId);
    CHECK(registry.get<zl::ecs::VehiclePassenger>(vehicle).stableId ==
          secondId);
    CHECK(registry.get<zl::ecs::VehiclePassenger>(vehicle).secondStableId ==
          thirdId);
    CHECK_FALSE(second.PlayerState().vehicle.driving);

    REQUIRE_MESSAGE(
        first.SendCommand(MakeVehicleCommand(zl::net::CommandType::ExitVehicle,
                                             firstId, 0),
                          &error),
        error);
    REQUIRE(PumpUntil(host, first, second, third, 5000, [&] {
        return !first.PlayerState().vehicle.driving &&
               second.PlayerState().vehicle.passenger &&
               third.PlayerState().vehicle.passenger &&
               registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == 0 &&
               registry.get<zl::ecs::VehiclePassenger>(vehicle).stableId ==
                   secondId &&
               registry.get<zl::ecs::VehiclePassenger>(vehicle)
                       .secondStableId == thirdId;
    }));

    REQUIRE_MESSAGE(
        third.SendCommand(
            MakeVehicleCommand(zl::net::CommandType::TakeDriverSeat,
                               thirdId, vehicleId),
            &error),
        error);
    REQUIRE(PumpUntil(host, first, second, third, 5000, [&] {
        const auto* a = first.Replication().Find(vehicleId);
        const auto* b = second.Replication().Find(vehicleId);
        const auto* c = third.Replication().Find(vehicleId);
        return third.PlayerState().vehicle.driving &&
               second.PlayerState().vehicle.passenger &&
               registry.get<zl::ecs::VehicleDriver>(vehicle).stableId ==
                   thirdId &&
               registry.get<zl::ecs::VehiclePassenger>(vehicle).stableId ==
                   secondId &&
               registry.get<zl::ecs::VehiclePassenger>(vehicle)
                       .secondStableId == 0 &&
               a != nullptr && b != nullptr && c != nullptr &&
               a->hasVehicle && b->hasVehicle && c->hasVehicle &&
               a->vehicleDriverStableId == thirdId &&
               b->vehicleDriverStableId == thirdId &&
               c->vehicleDriverStableId == thirdId &&
               a->vehiclePassengerStableId == secondId &&
               b->vehiclePassengerStableId == secondId &&
               c->vehiclePassengerStableId == secondId &&
               a->vehicleSecondPassengerStableId == 0 &&
               b->vehicleSecondPassengerStableId == 0 &&
               c->vehicleSecondPassengerStableId == 0;
    }));

    const zl::ecs::Position parked = registry.get<zl::ecs::Position>(vehicle);
    REQUIRE(TeleportServerPlayer(host, firstId, parked.x + 1.0f, parked.y));
    REQUIRE_MESSAGE(
        first.SendCommand(
            MakeVehicleCommand(zl::net::CommandType::EnterVehiclePassenger,
                               firstId, vehicleId),
            &error),
        error);
    REQUIRE(PumpUntil(host, first, second, third, 5000, [&] {
        return first.PlayerState().vehicle.passenger &&
               third.PlayerState().vehicle.driving &&
               registry.get<zl::ecs::VehiclePassenger>(vehicle).stableId ==
                   secondId &&
               registry.get<zl::ecs::VehiclePassenger>(vehicle)
                       .secondStableId == firstId;
    }));

    REQUIRE_MESSAGE(
        third.SendCommand(MakeVehicleCommand(
                              zl::net::CommandType::GiveDriverSeat,
                              thirdId, vehicleId),
                          &error),
        error);
    REQUIRE(PumpUntil(host, first, second, third, 5000, [&] {
        return second.PlayerState().vehicle.driving &&
               first.PlayerState().vehicle.passenger &&
               third.PlayerState().vehicle.passenger &&
               registry.get<zl::ecs::VehicleDriver>(vehicle).stableId ==
                   secondId &&
               registry.get<zl::ecs::VehiclePassenger>(vehicle).stableId ==
                   firstId &&
               registry.get<zl::ecs::VehiclePassenger>(vehicle)
                       .secondStableId == thirdId;
    }));

    REQUIRE_MESSAGE(
        second.SendCommand(MakeVehicleCommand(zl::net::CommandType::ExitVehicle,
                                              secondId, 0),
                          &error),
        error);
    REQUIRE(PumpUntil(host, first, second, third, 5000, [&] {
        return !second.PlayerState().vehicle.driving &&
               first.PlayerState().vehicle.passenger &&
               third.PlayerState().vehicle.passenger &&
               registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == 0 &&
               registry.get<zl::ecs::VehiclePassenger>(vehicle).stableId ==
                   firstId &&
               registry.get<zl::ecs::VehiclePassenger>(vehicle)
                       .secondStableId == thirdId;
    }));

    REQUIRE_MESSAGE(
        third.SendCommand(MakeVehicleCommand(zl::net::CommandType::ExitVehicle,
                                             thirdId, 0),
                          &error),
        error);
    REQUIRE(PumpUntil(host, first, second, third, 5000, [&] {
        return !third.PlayerState().vehicle.passenger &&
               first.PlayerState().vehicle.passenger &&
               registry.get<zl::ecs::VehiclePassenger>(vehicle).stableId ==
                   firstId &&
               registry.get<zl::ecs::VehiclePassenger>(vehicle)
                       .secondStableId == 0;
    }));

    REQUIRE_MESSAGE(
        first.SendCommand(MakeVehicleCommand(zl::net::CommandType::ExitVehicle,
                                             firstId, 0),
                          &error),
        error);
    REQUIRE(PumpUntil(host, first, second, third, 5000, [&] {
        return !first.PlayerState().vehicle.passenger &&
               !registry.all_of<zl::ecs::VehiclePassenger>(vehicle) &&
               registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == 0;
    }));

    first.Close();
    second.Close();
    third.Close();
    host.Shutdown();
}

TEST_CASE("M25-T3-V7 载具上下车距离、层级、占用、睡眠与死亡校验") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const std::filesystem::path profilePath =
        std::filesystem::temp_directory_path() /
        ("zl_m25_t3_v7_" +
         std::to_string(static_cast<unsigned long long>(
             Clock::now().time_since_epoch().count())) +
         ".bin");
    std::filesystem::remove(profilePath);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D323554335637ull;
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
    firstOptions.playerIdentity = "m25-t3-v7-alpha";
    firstOptions.playerName = "Alpha";
    zl::game::NetworkClient::Options secondOptions = firstOptions;
    secondOptions.playerIdentity = "m25-t3-v7-bravo";
    secondOptions.playerName = "Bravo";

    zl::game::NetworkClient first(firstOptions);
    zl::game::NetworkClient second(secondOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayerState() && second.HasPlayerState() &&
               host.SessionCount() == 2 && host.VehicleStableId() != 0;
    }));

    const std::uint64_t firstId = first.PlayerStableId();
    const std::uint64_t secondId = second.PlayerStableId();
    const std::uint64_t vehicleId = host.VehicleStableId();
    REQUIRE(firstId != 0);
    REQUIRE(secondId != 0);
    REQUIRE(vehicleId != 0);

    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity vehicle =
        host.Simulation().Ecs().FindById(vehicleId);
    const zl::ecs::Entity firstPlayer =
        host.Simulation().Ecs().FindById(firstId);
    const zl::ecs::Entity secondPlayer =
        host.Simulation().Ecs().FindById(secondId);
    REQUIRE(static_cast<bool>(vehicle != entt::null));
    REQUIRE(static_cast<bool>(firstPlayer != entt::null));
    REQUIRE(static_cast<bool>(secondPlayer != entt::null));
    const zl::ecs::Position vehicleStart =
        registry.get<zl::ecs::Position>(vehicle);

    REQUIRE(TeleportServerPlayer(host, firstId, vehicleStart.x + 10.0f,
                                 vehicleStart.y));
    const std::uint64_t rejectedBeforeDistance =
        host.GetNetworkStats().commandsRejected;
    REQUIRE_MESSAGE(
        first.SendCommand(MakeVehicleCommand(zl::net::CommandType::EnterVehicle,
                                             firstId, vehicleId), &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        return host.GetNetworkStats().commandsRejected >
                   rejectedBeforeDistance &&
               first.PlayerState().status.find("距离") != std::string::npos;
    }));
    CHECK_FALSE(first.PlayerState().vehicle.driving);
    CHECK(registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == 0);

    REQUIRE(TeleportServerPlayer(host, firstId, vehicleStart.x - 1.0f,
                                 vehicleStart.y));
    auto* firstPosition = registry.try_get<zl::ecs::Position>(firstPlayer);
    REQUIRE(firstPosition != nullptr);
    firstPosition->z = 1;
    const std::uint64_t rejectedBeforeLevel =
        host.GetNetworkStats().commandsRejected;
    REQUIRE_MESSAGE(
        first.SendCommand(MakeVehicleCommand(zl::net::CommandType::EnterVehicle,
                                             firstId, vehicleId), &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        return host.GetNetworkStats().commandsRejected >
                   rejectedBeforeLevel &&
               first.PlayerState().status.find("同一层") != std::string::npos;
    }));
    CHECK_FALSE(first.PlayerState().vehicle.driving);
    CHECK(registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == 0);
    firstPosition->z = 0;
    if (auto* rigid = registry.try_get<zl::ecs::RigidBody>(firstPlayer);
        rigid != nullptr && rigid->body != nullptr) {
        host.Simulation().Physics().TeleportBody(
            rigid->body, firstPosition->x, firstPosition->y, 0);
    }

    REQUIRE(TeleportServerPlayer(host, secondId, vehicleStart.x + 1.0f,
                                 vehicleStart.y));
    REQUIRE(SpawnBed(host, vehicleStart.x - 0.2f, vehicleStart.y, firstId) != 0);
    zl::net::Command sleepRequest;
    sleepRequest.type = zl::net::CommandType::SleepRequest;
    sleepRequest.playerStableId = firstId;
    sleepRequest.payload.x = 480.0f;
    REQUIRE_MESSAGE(first.SendCommand(sleepRequest, &error), error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.PlayerState().sleep.phase == zl::net::kPlayerSleepPhaseVote &&
               second.PlayerState().sleep.phase == zl::net::kPlayerSleepPhaseVote;
    }));
    const std::uint64_t rejectedBeforeSleep =
        host.GetNetworkStats().commandsRejected;
    REQUIRE_MESSAGE(
        second.SendCommand(MakeVehicleCommand(
                              zl::net::CommandType::EnterVehicle, secondId,
                              vehicleId), &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        return host.GetNetworkStats().commandsRejected > rejectedBeforeSleep;
    }));
    CAPTURE(second.PlayerState().status);
    REQUIRE(second.PlayerState().status.find("睡眠") != std::string::npos);
    CHECK_FALSE(second.PlayerState().vehicle.driving);
    CHECK(registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == 0);
    zl::net::Command sleepCancel;
    sleepCancel.type = zl::net::CommandType::SleepCancel;
    sleepCancel.playerStableId = secondId;
    REQUIRE_MESSAGE(second.SendCommand(sleepCancel, &error), error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.PlayerState().sleep.phase == zl::net::kPlayerSleepPhaseNone &&
               second.PlayerState().sleep.phase == zl::net::kPlayerSleepPhaseNone;
    }));

    REQUIRE_MESSAGE(
        first.SendCommand(MakeVehicleCommand(zl::net::CommandType::EnterVehicle,
                                             firstId, vehicleId), &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.PlayerState().vehicle.driving &&
               registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == firstId;
    }));
    const std::uint64_t rejectedBeforeOccupied =
        host.GetNetworkStats().commandsRejected;
    REQUIRE_MESSAGE(
        second.SendCommand(MakeVehicleCommand(zl::net::CommandType::EnterVehicle,
                                              secondId, vehicleId), &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        return host.GetNetworkStats().commandsRejected >
                   rejectedBeforeOccupied &&
               second.PlayerState().status.find("占用") != std::string::npos;
    }));
    CHECK_FALSE(second.PlayerState().vehicle.driving);
    CHECK(registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == firstId);

    auto* firstHealth = registry.try_get<zl::ecs::Health>(firstPlayer);
    REQUIRE(firstHealth != nullptr);
    firstHealth->current = 0.0f;
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return !first.PlayerState().alive &&
               registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == 0;
    }));
    CHECK_FALSE(first.PlayerState().vehicle.driving);
    const std::uint64_t rejectedBeforeDead =
        host.GetNetworkStats().commandsRejected;
    REQUIRE_MESSAGE(
        first.SendCommand(MakeVehicleCommand(zl::net::CommandType::EnterVehicle,
                                             firstId, vehicleId), &error),
        error);
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        return host.GetNetworkStats().commandsRejected > rejectedBeforeDead &&
               first.PlayerState().status.find("死亡") != std::string::npos;
    }));
    CHECK_FALSE(first.PlayerState().vehicle.driving);
    CHECK(registry.get<zl::ecs::VehicleDriver>(vehicle).stableId == 0);

    first.Close();
    second.Close();
    host.Shutdown();
    std::filesystem::remove(profilePath);
}

TEST_CASE("M25-T4 服务器载具撞击/噪音与车灯复制") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D32355434524D31ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options options;
    options.host = "127.0.0.1";
    options.port = host.NetworkPort();
    options.contentHash = hostOptions.contentHash;
    options.autoReconnect = false;
    zl::game::NetworkClient client(options);
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
    const zl::ecs::Entity vehicle =
        host.Simulation().Ecs().FindById(vehicleId);
    REQUIRE(static_cast<bool>(vehicle != entt::null));
    const zl::ecs::Position start =
        registry.get<zl::ecs::Position>(vehicle);

    REQUIRE(TeleportServerPlayer(host, playerId, start.x - 1.0f, start.y));
    REQUIRE_MESSAGE(
        client.SendCommand(MakeVehicleCommand(zl::net::CommandType::EnterVehicle,
                                              playerId, vehicleId),
                           &error),
        error);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return client.PlayerState().vehicle.driving;
    }));
    CHECK_FALSE(registry.get<zl::ecs::Vehicle>(vehicle).headlights);
    REQUIRE_MESSAGE(client.SendCommand(
                        MakeVehicleCommand(
                            zl::net::CommandType::ToggleHeadlights,
                            playerId, vehicleId),
                        &error),
                    error);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto* actor = client.Replication().Find(vehicleId);
        return registry.get<zl::ecs::Vehicle>(vehicle).headlights &&
               actor != nullptr && actor->hasVehicle &&
               actor->vehicle.headlights;
    }));

    const std::uint64_t hornSoundsBefore =
        host.GetNetworkStats().worldSoundsSent;
    REQUIRE_MESSAGE(client.SendCommand(
                        MakeVehicleCommand(zl::net::CommandType::HonkHorn,
                                           playerId, vehicleId),
                        &error),
                    error);
    bool gotHornSound = false;
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const std::vector<zl::net::WorldSoundEvent> batch =
            client.TakeWorldSounds();
        for (const zl::net::WorldSoundEvent& sound : batch) {
            if (sound.clip == zl::audio::kVehicleHorn) {
                gotHornSound = true;
            }
        }
        return gotHornSound;
    }));
    CHECK(gotHornSound);
    CHECK(host.GetNetworkStats().worldSoundsSent > hornSoundsBefore);

    const zl::ecs::Entity noiseTarget =
        host.Simulation().SpawnZombieAt(start.x + 4.0f, start.y + 4.0f);
    REQUIRE(static_cast<bool>(noiseTarget != entt::null));
    const std::uint64_t noiseTargetId =
        host.Simulation().Ecs().IdOf(noiseTarget).id;
    const std::uint64_t soundsBefore = host.GetNetworkStats().worldSoundsSent;

    float worldDirX = 1.0f;
    float worldDirY = 0.0f;
    PickOpenDriveDirection(host, start.x, start.y, &worldDirX, &worldDirY);
    float screenDirX = 1.0f;
    float screenDirY = 0.0f;
    WorldDirToScreen(worldDirX, worldDirY, &screenDirX, &screenDirY);
    REQUIRE_MESSAGE(client.SendCommand(
                        MakeMove(playerId, screenDirX, screenDirY), &error),
                    error);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto& velocity = registry.get<zl::ecs::Velocity>(vehicle);
        return std::hypot(velocity.x, velocity.y) > 2.5f;
    }));
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto* percep =
            registry.try_get<zl::ecs::PerceptionState>(noiseTarget);
        return host.GetNetworkStats().worldSoundsSent > soundsBefore &&
               percep != nullptr && percep->sinceHeard < 1.0f;
    }));
    CHECK(host.GetNetworkStats().worldSoundsSent > soundsBefore);
    CHECK(noiseTargetId != 0);

    const zl::ecs::Position moving =
        registry.get<zl::ecs::Position>(vehicle);
    const float conditionBeforeRam =
        registry.get<zl::ecs::Vehicle>(vehicle).condition;
    const zl::ecs::Entity ramTarget =
        host.Simulation().SpawnZombieAt(moving.x, moving.y);
    REQUIRE(static_cast<bool>(ramTarget != entt::null));
    const std::uint64_t ramTargetId =
        host.Simulation().Ecs().IdOf(ramTarget).id;
    const float ramHealthBefore =
        registry.get<zl::ecs::Health>(ramTarget).current;
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        const float health = registry.get<zl::ecs::Health>(ramTarget).current;
        const auto* actor = client.Replication().Find(ramTargetId);
        return ramHealthBefore > 0.0f && health < ramHealthBefore - 1.0f &&
               actor != nullptr && actor->hasHealth &&
               actor->health.current < ramHealthBefore - 1.0f &&
               registry.get<zl::ecs::Vehicle>(vehicle).condition <
                   conditionBeforeRam - 0.001f;
    }));
    CHECK(registry.get<zl::ecs::Vehicle>(vehicle).condition <
          conditionBeforeRam);
    std::vector<zl::net::WorldSoundEvent> ramSounds;
    CHECK(PumpUntil(host, client, 3000, [&] {
        std::vector<zl::net::WorldSoundEvent> batch =
            client.TakeWorldSounds();
        ramSounds.insert(ramSounds.end(), batch.begin(), batch.end());
        return std::any_of(ramSounds.begin(), ramSounds.end(),
                           [](const zl::net::WorldSoundEvent& sound) {
                               return sound.clip == zl::audio::kVehicleRam;
                           });
    }));

    const zl::ecs::Position animalAt =
        registry.get<zl::ecs::Position>(vehicle);
    const zl::ecs::Entity animal = host.Simulation().Ecs().Create();
    REQUIRE(static_cast<bool>(animal != entt::null));
    const std::uint64_t animalId =
        host.Simulation().Ecs().IdOf(animal).id;
    registry.emplace<zl::ecs::Position>(
        animal, zl::ecs::Position{animalAt.x, animalAt.y, animalAt.z});
    registry.emplace<zl::ecs::Health>(animal,
                                      zl::ecs::Health{100.0f, 100.0f});
    registry.emplace<zl::ecs::Animal>(
        animal, zl::ecs::Animal{"deer", zl::ecs::AnimalKind::Passive});
    registry.emplace<zl::ecs::Collider>(
        animal, zl::ecs::Collider{0.45f, 0.75f, zl::ecs::kColliderCharacter,
                                  zl::ecs::kColliderAll});
    const float animalHealthBefore =
        registry.get<zl::ecs::Health>(animal).current;
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        const float health = registry.get<zl::ecs::Health>(animal).current;
        const auto* actor = client.Replication().Find(animalId);
        return animalHealthBefore > 0.0f && health < animalHealthBefore - 1.0f &&
               actor != nullptr && actor->hasAnimal &&
               actor->hasHealth &&
               actor->health.current < animalHealthBefore - 1.0f;
    }));
    CHECK(registry.get<zl::ecs::Health>(animal).current <
          animalHealthBefore);
    const auto* animalActor = client.Replication().Find(animalId);
    REQUIRE(animalActor != nullptr);
    CHECK(animalActor->hasAnimal);
    CHECK(animalActor->hasHealth);
    CHECK(animalActor->health.current < animalHealthBefore - 1.0f);

    const zl::ecs::Entity driver =
        host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(driver != entt::null));
    REQUIRE(registry.all_of<zl::ecs::Progression>(driver));
    const std::size_t meleeIndex =
        zl::ecs::SkillIndex(zl::ecs::SkillKind::Melee);
    const float meleeXpBefore =
        registry.get<zl::ecs::Progression>(driver).skills[meleeIndex].xp;
    const zl::ecs::Position killAt =
        registry.get<zl::ecs::Position>(vehicle);
    const zl::ecs::Entity killTarget =
        host.Simulation().SpawnZombieAt(killAt.x, killAt.y);
    REQUIRE(static_cast<bool>(killTarget != entt::null));
    registry.get<zl::ecs::Health>(killTarget).current = 1.0f;
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        return registry.get<zl::ecs::Health>(killTarget).current <= 0.0f &&
               registry.get<zl::ecs::Progression>(driver)
                       .skills[meleeIndex]
                       .xp > meleeXpBefore;
    }));
    CHECK(registry.get<zl::ecs::Progression>(driver)
              .skills[meleeIndex]
              .xp > meleeXpBefore);

    REQUIRE_MESSAGE(client.SendCommand(MakeMove(playerId, 0.0f, 0.0f), &error),
                    error);
    client.Close();
    host.Shutdown();
}

TEST_CASE("M25-T4-V15/V20 服务器载具受僵尸/危险动物攻击扣车况") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.spawnAnimals = true;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D32355434563135ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options options;
    options.host = "127.0.0.1";
    options.port = host.NetworkPort();
    options.contentHash = hostOptions.contentHash;
    options.autoReconnect = false;
    zl::game::NetworkClient client(options);
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
    const zl::ecs::Entity vehicle =
        host.Simulation().Ecs().FindById(vehicleId);
    REQUIRE(static_cast<bool>(vehicle != entt::null));
    const zl::ecs::Position vehiclePosition =
        registry.get<zl::ecs::Position>(vehicle);

    REQUIRE(TeleportServerPlayer(host, playerId, vehiclePosition.x - 1.0f,
                                 vehiclePosition.y));
    REQUIRE_MESSAGE(
        client.SendCommand(MakeVehicleCommand(
                               zl::net::CommandType::EnterVehicle, playerId,
                               vehicleId),
                           &error),
        error);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return client.PlayerState().vehicle.driving;
    }));

    const float conditionBefore =
        registry.get<zl::ecs::Vehicle>(vehicle).condition;
    const zl::ecs::Entity attacker =
        host.Simulation().SpawnZombieAt(vehiclePosition.x + 0.9f,
                                        vehiclePosition.y);
    REQUIRE(static_cast<bool>(attacker != entt::null));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return registry.get<zl::ecs::Vehicle>(vehicle).condition <
               conditionBefore - 0.001f;
    }));
    CHECK(registry.get<zl::ecs::Vehicle>(vehicle).condition <
          conditionBefore);
    CHECK(client.PlayerState().vehicle.driving);
    bool receivedHitSound = false;
    REQUIRE(PumpUntil(host, client, 2000, [&] {
        for (const zl::net::WorldSoundEvent& sound : client.TakeWorldSounds()) {
            if (sound.clip == zl::audio::kVehicleHit) {
                receivedHitSound = true;
            }
        }
        return receivedHitSound;
    }));
    CHECK(receivedHitSound);

    host.Simulation().DespawnEntity(attacker);
    REQUIRE(PumpHostUntil(host, 2000, [&] {
        return !host.Simulation().Ecs().Alive(attacker);
    }));

    const float conditionBeforeAnimal =
        registry.get<zl::ecs::Vehicle>(vehicle).condition;
    const zl::ecs::Entity boar = host.Simulation().Ecs().Create();
    REQUIRE(static_cast<bool>(boar != entt::null));
    registry.emplace<zl::ecs::Position>(
        boar, zl::ecs::Position{vehiclePosition.x + 0.9f,
                                vehiclePosition.y, vehiclePosition.z});
    registry.emplace<zl::ecs::Health>(boar,
                                      zl::ecs::Health{200.0f, 200.0f});
    registry.emplace<zl::ecs::Animal>(
        boar, zl::ecs::Animal{"boar", zl::ecs::AnimalKind::Dangerous});
    registry.emplace<zl::ecs::Collider>(
        boar, zl::ecs::Collider{0.45f, 0.75f, zl::ecs::kColliderCharacter,
                                zl::ecs::kColliderAll});
    bool receivedAnimalHitSound = false;
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        for (const zl::net::WorldSoundEvent& sound : client.TakeWorldSounds()) {
            if (sound.clip == zl::audio::kVehicleHit) {
                receivedAnimalHitSound = true;
            }
        }
        return registry.get<zl::ecs::Vehicle>(vehicle).condition <
                   conditionBeforeAnimal - 0.001f &&
               receivedAnimalHitSound;
    }));
    CHECK(registry.get<zl::ecs::Vehicle>(vehicle).condition <
          conditionBeforeAnimal);
    CHECK(receivedAnimalHitSound);

    client.Close();
    host.Shutdown();
}

TEST_CASE("M25-T4-V21 服务器载具撞击玩家") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const char* args[] = {"ZomboidLikeTests", "--combat.pvp=true"};
    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.argc = 2;
    hostOptions.argv = args;
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D32355434563231ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options options;
    options.host = "127.0.0.1";
    options.port = host.NetworkPort();
    options.contentHash = hostOptions.contentHash;
    options.autoReconnect = false;

    zl::game::NetworkClient driver(options);
    REQUIRE(driver.Connect(&error));
    REQUIRE(PumpUntil(host, driver, 8000, [&] {
        return driver.IsConnected() && driver.HasPlayerState() &&
               host.VehicleStableId() != 0;
    }));

    zl::game::NetworkClient victim(options);
    REQUIRE(victim.Connect(&error));
    REQUIRE(PumpUntil(host, driver, victim, 8000, [&] {
        return driver.IsConnected() && victim.IsConnected() &&
               driver.HasPlayerState() && victim.HasPlayerState();
    }));

    const std::uint64_t driverId = driver.PlayerStableId();
    const std::uint64_t victimId = victim.PlayerStableId();
    const std::uint64_t vehicleId = host.VehicleStableId();
    REQUIRE(driverId != 0);
    REQUIRE(victimId != 0);
    REQUIRE(driverId != victimId);
    REQUIRE(vehicleId != 0);

    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity vehicle =
        host.Simulation().Ecs().FindById(vehicleId);
    const zl::ecs::Entity victimEntity =
        host.Simulation().Ecs().FindById(victimId);
    REQUIRE(static_cast<bool>(vehicle != entt::null));
    REQUIRE(static_cast<bool>(victimEntity != entt::null));
    const zl::ecs::Position start =
        registry.get<zl::ecs::Position>(vehicle);

    REQUIRE(TeleportServerPlayer(host, driverId, start.x - 1.0f, start.y));
    REQUIRE_MESSAGE(
        driver.SendCommand(MakeVehicleCommand(
                               zl::net::CommandType::EnterVehicle, driverId,
                               vehicleId),
                           &error),
        error);
    REQUIRE(PumpUntil(host, driver, victim, 5000, [&] {
        return driver.PlayerState().vehicle.driving;
    }));

    float worldDirX = 1.0f;
    float worldDirY = 0.0f;
    PickOpenDriveDirection(host, start.x, start.y, &worldDirX, &worldDirY);
    REQUIRE(TeleportServerPlayer(host, victimId, start.x + worldDirX * 1.4f,
                                 start.y + worldDirY * 1.4f));
    const float victimHealthBefore =
        registry.get<zl::ecs::Health>(victimEntity).current;
    const zl::ecs::Entity driverEntity =
        host.Simulation().Ecs().FindById(driverId);
    REQUIRE(static_cast<bool>(driverEntity != entt::null));
    const float driverHealthBefore =
        registry.get<zl::ecs::Health>(driverEntity).current;

    float screenDirX = 1.0f;
    float screenDirY = 0.0f;
    WorldDirToScreen(worldDirX, worldDirY, &screenDirX, &screenDirY);
    REQUIRE_MESSAGE(driver.SendCommand(
                        MakeMove(driverId, screenDirX, screenDirY), &error),
                    error);

    bool victimDamaged = false;
    REQUIRE(PumpUntil(host, driver, victim, 5000, [&] {
        const float health = registry.get<zl::ecs::Health>(victimEntity).current;
        const auto* actor = victim.Replication().Find(victimId);
        victimDamaged = health < victimHealthBefore - 1.0f && actor != nullptr &&
                        actor->hasHealth &&
                        actor->health.current < victimHealthBefore - 1.0f;
        return victimDamaged;
    }));
    CHECK(victimDamaged);
    const auto* victimActor = victim.Replication().Find(victimId);
    REQUIRE(victimActor != nullptr);
    CHECK(victimActor->hasHealth);
    CHECK(victimActor->health.current < victimHealthBefore - 1.0f);
    CHECK(registry.get<zl::ecs::Health>(driverEntity).current ==
          doctest::Approx(driverHealthBefore));
    CHECK(driver.PlayerState().vehicle.driving);

    REQUIRE_MESSAGE(driver.SendCommand(MakeMove(driverId, 0.0f, 0.0f), &error),
                    error);
    driver.Close();
    victim.Close();
    host.Shutdown();
}

TEST_CASE("M25-T4-V21 服务器载具撞击玩家受 PvP 开关限制") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D32355434563232ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options options;
    options.host = "127.0.0.1";
    options.port = host.NetworkPort();
    options.contentHash = hostOptions.contentHash;
    options.autoReconnect = false;

    zl::game::NetworkClient driver(options);
    REQUIRE(driver.Connect(&error));
    REQUIRE(PumpUntil(host, driver, 8000, [&] {
        return driver.IsConnected() && driver.HasPlayerState() &&
               host.VehicleStableId() != 0;
    }));

    zl::game::NetworkClient victim(options);
    REQUIRE(victim.Connect(&error));
    REQUIRE(PumpUntil(host, driver, victim, 8000, [&] {
        return driver.IsConnected() && victim.IsConnected() &&
               driver.HasPlayerState() && victim.HasPlayerState();
    }));

    const std::uint64_t driverId = driver.PlayerStableId();
    const std::uint64_t victimId = victim.PlayerStableId();
    const std::uint64_t vehicleId = host.VehicleStableId();
    REQUIRE(driverId != 0);
    REQUIRE(victimId != 0);
    REQUIRE(vehicleId != 0);

    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity vehicle =
        host.Simulation().Ecs().FindById(vehicleId);
    const zl::ecs::Entity victimEntity =
        host.Simulation().Ecs().FindById(victimId);
    REQUIRE(static_cast<bool>(vehicle != entt::null));
    REQUIRE(static_cast<bool>(victimEntity != entt::null));
    const zl::ecs::Position start =
        registry.get<zl::ecs::Position>(vehicle);

    REQUIRE(TeleportServerPlayer(host, driverId, start.x - 1.0f, start.y));
    REQUIRE_MESSAGE(
        driver.SendCommand(MakeVehicleCommand(
                               zl::net::CommandType::EnterVehicle, driverId,
                               vehicleId),
                           &error),
        error);
    REQUIRE(PumpUntil(host, driver, victim, 5000, [&] {
        return driver.PlayerState().vehicle.driving;
    }));

    float worldDirX = 1.0f;
    float worldDirY = 0.0f;
    PickOpenDriveDirection(host, start.x, start.y, &worldDirX, &worldDirY);
    REQUIRE(TeleportServerPlayer(host, victimId, start.x + worldDirX * 1.4f,
                                 start.y + worldDirY * 1.4f));
    const float victimHealthBefore =
        registry.get<zl::ecs::Health>(victimEntity).current;
    const zl::ecs::Entity driverEntity =
        host.Simulation().Ecs().FindById(driverId);
    REQUIRE(static_cast<bool>(driverEntity != entt::null));
    const float driverHealthBefore =
        registry.get<zl::ecs::Health>(driverEntity).current;

    float screenDirX = 1.0f;
    float screenDirY = 0.0f;
    WorldDirToScreen(worldDirX, worldDirY, &screenDirX, &screenDirY);
    REQUIRE_MESSAGE(driver.SendCommand(
                        MakeMove(driverId, screenDirX, screenDirY), &error),
                    error);

    REQUIRE(PumpUntil(host, driver, victim, 3000, [&] {
        const auto& velocity = registry.get<zl::ecs::Velocity>(vehicle);
        return std::hypot(velocity.x, velocity.y) > 2.5f;
    }));
    (void)PumpHostUntil(host, 300, [] { return false; });
    CHECK(registry.get<zl::ecs::Health>(victimEntity).current ==
          doctest::Approx(victimHealthBefore));
    CHECK(registry.get<zl::ecs::Health>(driverEntity).current ==
          doctest::Approx(driverHealthBefore));

    REQUIRE_MESSAGE(driver.SendCommand(MakeMove(driverId, 0.0f, 0.0f), &error),
                    error);
    driver.Close();
    victim.Close();
    host.Shutdown();
}

TEST_CASE("M26 多载具：两车独立驾驶与命令目标") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D32364D5631ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;

    zl::game::NetworkClient first(clientOptions);
    zl::game::NetworkClient second(clientOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayerState() && second.HasPlayerState() &&
               host.SessionCount() == 2 &&
               host.VehicleStableIds().size() >= 2;
    }));

    const std::uint64_t firstId = first.PlayerStableId();
    const std::uint64_t secondId = second.PlayerStableId();
    const std::vector<std::uint64_t> vehicleIds = host.VehicleStableIds();
    REQUIRE(firstId != 0);
    REQUIRE(secondId != 0);
    REQUIRE(vehicleIds.size() >= 2);
    CHECK(std::is_sorted(vehicleIds.begin(), vehicleIds.end()));
    CHECK(host.VehicleStableId() == vehicleIds.front());
    const std::uint64_t firstVehicleId = vehicleIds[0];
    const std::uint64_t secondVehicleId = vehicleIds[1];

    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity firstVehicle =
        host.Simulation().Ecs().FindById(firstVehicleId);
    const zl::ecs::Entity secondVehicle =
        host.Simulation().Ecs().FindById(secondVehicleId);
    REQUIRE(static_cast<bool>(firstVehicle != entt::null));
    REQUIRE(static_cast<bool>(secondVehicle != entt::null));
    REQUIRE(registry.all_of<zl::ecs::Position, zl::ecs::Vehicle>(
        firstVehicle));
    REQUIRE(registry.all_of<zl::ecs::Position, zl::ecs::Vehicle>(
        secondVehicle));

    const zl::ecs::Position firstStart =
        registry.get<zl::ecs::Position>(firstVehicle);
    const zl::ecs::Position secondStart =
        registry.get<zl::ecs::Position>(secondVehicle);
    REQUIRE(TeleportServerPlayer(host, firstId, firstStart.x - 1.0f,
                                 firstStart.y));
    REQUIRE(TeleportServerPlayer(host, secondId, secondStart.x - 1.0f,
                                 secondStart.y));

    REQUIRE_MESSAGE(first.SendCommand(
                        MakeVehicleCommand(zl::net::CommandType::EnterVehicle,
                                           firstId, secondVehicleId),
                        &error),
                    error);
    (void)PumpHostUntil(host, 300, [] { return false; });
    CHECK_FALSE(first.PlayerState().vehicle.driving);

    REQUIRE_MESSAGE(first.SendCommand(
                        MakeVehicleCommand(zl::net::CommandType::EnterVehicle,
                                           firstId, firstVehicleId),
                        &error),
                    error);
    REQUIRE_MESSAGE(second.SendCommand(
                        MakeVehicleCommand(zl::net::CommandType::EnterVehicle,
                                           secondId, secondVehicleId),
                        &error),
                    error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.PlayerState().vehicle.driving &&
               second.PlayerState().vehicle.driving &&
               first.PlayerState().vehicle.vehicleStableId == firstVehicleId &&
               second.PlayerState().vehicle.vehicleStableId == secondVehicleId &&
               registry.get<zl::ecs::VehicleDriver>(firstVehicle).stableId ==
                   firstId &&
               registry.get<zl::ecs::VehicleDriver>(secondVehicle).stableId ==
                   secondId;
    }));

    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        const zl::game::ReplicatedActor* firstSeen =
            first.Replication().Find(firstVehicleId);
        const zl::game::ReplicatedActor* secondSeen =
            second.Replication().Find(secondVehicleId);
        return firstSeen != nullptr && firstSeen->hasVehicle &&
               firstSeen->vehicleDriverStableId == firstId &&
               secondSeen != nullptr && secondSeen->hasVehicle &&
               secondSeen->vehicleDriverStableId == secondId &&
               first.Replication().Find(secondVehicleId) != nullptr &&
               second.Replication().Find(firstVehicleId) != nullptr;
    }));
    const zl::game::ReplicatedActor* firstCarOnFirst =
        first.Replication().Find(firstVehicleId);
    const zl::game::ReplicatedActor* secondCarOnFirst =
        first.Replication().Find(secondVehicleId);
    const zl::game::ReplicatedActor* firstCarOnSecond =
        second.Replication().Find(firstVehicleId);
    const zl::game::ReplicatedActor* secondCarOnSecond =
        second.Replication().Find(secondVehicleId);
    REQUIRE(firstCarOnFirst != nullptr);
    REQUIRE(secondCarOnFirst != nullptr);
    REQUIRE(firstCarOnSecond != nullptr);
    REQUIRE(secondCarOnSecond != nullptr);
    CHECK(firstCarOnFirst->vehicleDriverStableId == firstId);
    CHECK(secondCarOnFirst->vehicleDriverStableId == secondId);
    CHECK(firstCarOnSecond->vehicleDriverStableId == firstId);
    CHECK(secondCarOnSecond->vehicleDriverStableId == secondId);

    const zl::ecs::Position secondParked =
        registry.get<zl::ecs::Position>(secondVehicle);
    const float secondFuelBefore =
        registry.get<zl::ecs::Vehicle>(secondVehicle).fuel;

    float worldDirX = 1.0f;
    float worldDirY = 0.0f;
    PickOpenDriveDirection(host, firstStart.x, firstStart.y, &worldDirX,
                           &worldDirY);
    float screenDirX = 1.0f;
    float screenDirY = 0.0f;
    WorldDirToScreen(worldDirX, worldDirY, &screenDirX, &screenDirY);
    REQUIRE_MESSAGE(first.SendCommand(MakeMove(firstId, screenDirX, screenDirY),
                                      &error),
                    error);
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        const auto& position = registry.get<zl::ecs::Position>(firstVehicle);
        return std::hypot(position.x - firstStart.x,
                          position.y - firstStart.y) > 0.5f;
    }));
    REQUIRE_MESSAGE(first.SendCommand(MakeMove(firstId, 0.0f, 0.0f), &error),
                    error);
    (void)PumpHostUntil(host, 300, [] { return false; });

    const auto& secondAfter = registry.get<zl::ecs::Position>(secondVehicle);
    CHECK(secondAfter.x == doctest::Approx(secondParked.x).epsilon(0.01));
    CHECK(secondAfter.y == doctest::Approx(secondParked.y).epsilon(0.01));
    CHECK(registry.get<zl::ecs::Vehicle>(secondVehicle).fuel ==
          doctest::Approx(secondFuelBefore).epsilon(0.001));
    CHECK(registry.get<zl::ecs::Vehicle>(firstVehicle).fuel < 1.0f);

    REQUIRE_MESSAGE(first.SendCommand(
                        MakeVehicleCommand(zl::net::CommandType::ExitVehicle,
                                           firstId, firstVehicleId),
                        &error),
                    error);
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        return !first.PlayerState().vehicle.driving &&
               registry.get<zl::ecs::VehicleDriver>(firstVehicle).stableId ==
                   0 &&
               registry.get<zl::ecs::VehicleDriver>(secondVehicle).stableId ==
                   secondId;
    }));

    first.Close();
    second.Close();
    host.Shutdown();
}
TEST_CASE("M26-V2 服务器载具间碰撞：阻挡、双方车损与撞击音") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D32364D5632ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = host.NetworkPort();
    clientOptions.contentHash = hostOptions.contentHash;
    clientOptions.autoReconnect = false;

    zl::game::NetworkClient first(clientOptions);
    zl::game::NetworkClient second(clientOptions);
    zl::game::NetworkClient third(clientOptions);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayerState() && second.HasPlayerState() &&
               host.SessionCount() == 2 &&
               host.VehicleStableIds().size() >= 2;
    }));
    REQUIRE(third.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, third, 8000, [&] {
        return third.IsConnected() && third.HasPlayerState() &&
               host.SessionCount() == 3;
    }));

    const std::uint64_t firstId = first.PlayerStableId();
    const std::uint64_t secondId = second.PlayerStableId();
    const std::uint64_t thirdId = third.PlayerStableId();
    const std::vector<std::uint64_t> vehicleIds = host.VehicleStableIds();
    REQUIRE(firstId != 0);
    REQUIRE(secondId != 0);
    REQUIRE(thirdId != 0);

    REQUIRE(vehicleIds.size() >= 2);
    const std::uint64_t firstVehicleId = vehicleIds[0];
    const std::uint64_t secondVehicleId = vehicleIds[1];

    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity firstVehicle =
        host.Simulation().Ecs().FindById(firstVehicleId);
    const zl::ecs::Entity secondVehicle =
        host.Simulation().Ecs().FindById(secondVehicleId);
    REQUIRE(static_cast<bool>(firstVehicle != entt::null));
    REQUIRE(static_cast<bool>(secondVehicle != entt::null));
    zl::ecs::Position firstStart =
        registry.get<zl::ecs::Position>(firstVehicle);
    int laneCellX = 0;
    int laneCellY = 0;
    float worldDirX = 1.0f;
    float worldDirY = 0.0f;
    REQUIRE(FindOpenVehicleLane(host, firstStart.x, firstStart.y, 12,
                                &laneCellX, &laneCellY, &worldDirX,
                                &worldDirY));
    firstStart = {static_cast<float>(laneCellX) + 0.5f,
                  static_cast<float>(laneCellY) + 0.5f, firstStart.z};
    zl::ecs::Position secondStart{
        firstStart.x + worldDirX * 2.5f,
        firstStart.y + worldDirY * 2.5f, firstStart.z};
    REQUIRE(zl::server::ServerHostTestAccess::TeleportVehicle(
        host, firstVehicleId, firstStart.x, firstStart.y, firstStart.z));
    REQUIRE(zl::server::ServerHostTestAccess::TeleportVehicle(
        host, secondVehicleId, secondStart.x, secondStart.y,
        secondStart.z));
    const zl::ecs::Position secondRuntimeStart =
        zl::server::ServerHostTestAccess::VehiclePosition(host,
                                                          secondVehicleId);
    REQUIRE(secondRuntimeStart.x == doctest::Approx(secondStart.x));
    REQUIRE(secondRuntimeStart.y == doctest::Approx(secondStart.y));
    REQUIRE(TeleportServerPlayer(host, firstId, firstStart.x - 1.0f,
                                 firstStart.y));
    REQUIRE(TeleportServerPlayer(host, secondId, secondStart.x - 1.0f,
                                 secondStart.y));
    REQUIRE(TeleportServerPlayer(host, thirdId, secondStart.x + 1.0f,
                                 secondStart.y));

    REQUIRE_MESSAGE(first.SendCommand(
                        MakeVehicleCommand(zl::net::CommandType::EnterVehicle,
                                           firstId, firstVehicleId),
                        &error),
                    error);
    REQUIRE_MESSAGE(second.SendCommand(
                        MakeVehicleCommand(zl::net::CommandType::EnterVehicle,
                                           secondId, secondVehicleId),
                        &error),
                    error);
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return first.PlayerState().vehicle.driving &&
               second.PlayerState().vehicle.driving &&
               first.PlayerState().vehicle.vehicleStableId == firstVehicleId &&
               second.PlayerState().vehicle.vehicleStableId == secondVehicleId;
    }));
    REQUIRE_MESSAGE(third.SendCommand(
                        MakeVehicleCommand(
                            zl::net::CommandType::EnterVehiclePassenger,
                            thirdId, secondVehicleId),
                        &error),
                    error);
    REQUIRE(PumpUntil(host, first, second, third, 5000, [&] {
        return third.PlayerState().vehicle.passenger &&
               third.PlayerState().vehicle.vehicleStableId ==
                   secondVehicleId;
    }));

    const float firstConditionBefore =
        registry.get<zl::ecs::Vehicle>(firstVehicle).condition;
    const float secondConditionBefore =
        registry.get<zl::ecs::Vehicle>(secondVehicle).condition;
    const zl::ecs::Entity firstPlayerEntity =
        host.Simulation().Ecs().FindById(firstId);
    const zl::ecs::Entity secondPlayerEntity =
        host.Simulation().Ecs().FindById(secondId);
    const zl::ecs::Entity thirdPlayerEntity =
        host.Simulation().Ecs().FindById(thirdId);
    REQUIRE(static_cast<bool>(firstPlayerEntity != entt::null));
    REQUIRE(static_cast<bool>(secondPlayerEntity != entt::null));
    REQUIRE(static_cast<bool>(thirdPlayerEntity != entt::null));
    const float firstHealthBefore =
        registry.get<zl::ecs::Health>(firstPlayerEntity).current;
    const float secondHealthBefore =
        registry.get<zl::ecs::Health>(secondPlayerEntity).current;
    const float thirdHealthBefore =
        registry.get<zl::ecs::Health>(thirdPlayerEntity).current;
    const std::uint64_t soundsBefore = host.GetNetworkStats().worldSoundsSent;
    bool collisionSound = false;

    float screenDirX = 1.0f;
    float screenDirY = 0.0f;
    WorldDirToScreen(worldDirX, worldDirY, &screenDirX, &screenDirY);
    REQUIRE_MESSAGE(first.SendCommand(MakeMove(firstId, screenDirX, screenDirY),
                                      &error),
                    error);
    REQUIRE(PumpUntil(host, first, second, third, 5000, [&] {
        for (const zl::net::WorldSoundEvent& sound : first.TakeWorldSounds()) {
            if (sound.clip == zl::audio::kVehicleRam) {
                collisionSound = true;
            }
        }
        const auto& firstPosition =
            registry.get<zl::ecs::Position>(firstVehicle);
        const auto& secondPosition =
            registry.get<zl::ecs::Position>(secondVehicle);
        const float dx = firstPosition.x - secondPosition.x;
        const float dy = firstPosition.y - secondPosition.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        const auto& velocity = registry.get<zl::ecs::Velocity>(firstVehicle);
        return distance <= 2.5f && std::hypot(velocity.x, velocity.y) < 0.5f &&
               registry.get<zl::ecs::Vehicle>(firstVehicle).condition <
                   firstConditionBefore - 0.001f &&
               registry.get<zl::ecs::Vehicle>(secondVehicle).condition <
                   secondConditionBefore - 0.001f &&
               registry.get<zl::ecs::Health>(firstPlayerEntity).current <
                   firstHealthBefore - 0.001f &&
               registry.get<zl::ecs::Health>(secondPlayerEntity).current <
                   secondHealthBefore - 0.001f &&
               registry.get<zl::ecs::Health>(thirdPlayerEntity).current <
                   thirdHealthBefore - 0.001f;
    }));
    CHECK(collisionSound);
    CHECK(host.GetNetworkStats().worldSoundsSent > soundsBefore);

    const auto& firstPosition = registry.get<zl::ecs::Position>(firstVehicle);
    const auto& secondPosition = registry.get<zl::ecs::Position>(secondVehicle);
    const float dx = firstPosition.x - secondPosition.x;
    const float dy = firstPosition.y - secondPosition.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    const float firstRadius = zl::game::VehicleRadiusForType(
        registry.get<zl::ecs::Vehicle>(firstVehicle).type);
    const float secondRadius = zl::game::VehicleRadiusForType(
        registry.get<zl::ecs::Vehicle>(secondVehicle).type);
    CHECK(distance >= firstRadius + secondRadius - 0.02f);
    CHECK((firstPosition.x - secondPosition.x) * worldDirX +
              (firstPosition.y - secondPosition.y) * worldDirY <=
          0.05f);
    const float secondPushedDistance =
        std::hypot(secondPosition.x - secondRuntimeStart.x,
                   secondPosition.y - secondRuntimeStart.y);
    CHECK(secondPushedDistance > 0.05f);
    CHECK((secondPosition.x - secondRuntimeStart.x) * worldDirX +
              (secondPosition.y - secondRuntimeStart.y) * worldDirY >
          0.02f);
    CHECK(registry.get<zl::ecs::Vehicle>(firstVehicle).condition <
          firstConditionBefore);
    CHECK(registry.get<zl::ecs::Vehicle>(secondVehicle).condition <
          secondConditionBefore);
    CHECK(registry.get<zl::ecs::Health>(firstPlayerEntity).current <
          firstHealthBefore);
    CHECK(registry.get<zl::ecs::Health>(secondPlayerEntity).current <
          secondHealthBefore);
    CHECK(registry.get<zl::ecs::Health>(thirdPlayerEntity).current <
          thirdHealthBefore);
    CHECK(third.PlayerState().vehicle.passenger);
    CHECK(first.PlayerState().vehicle.driving);
    CHECK(second.PlayerState().vehicle.driving);

    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        const zl::game::ReplicatedActor* firstActor =
            first.Replication().Find(firstVehicleId);
        const zl::game::ReplicatedActor* secondActor =
            first.Replication().Find(secondVehicleId);
        return firstActor != nullptr && firstActor->hasVehicle &&
               firstActor->vehicle.condition < firstConditionBefore &&
               secondActor != nullptr && secondActor->hasVehicle &&
               secondActor->vehicle.condition < secondConditionBefore;
    }));
    CHECK(first.PlayerState().status.find("车辆碰撞") != std::string::npos);

    REQUIRE_MESSAGE(first.SendCommand(MakeMove(firstId, 0.0f, 0.0f), &error),
                    error);
    first.Close();
    second.Close();
    third.Close();
    host.Shutdown();
}

TEST_CASE("M26-V9 服务器载具碰撞：冲量继承与无人车滑行") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D32364D5639ull;

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
    REQUIRE(PumpUntil(host, client, 8000, [&] {
        return client.IsConnected() && client.HasPlayerState() &&
               host.VehicleStableIds().size() >= 2;
    }));

    const std::uint64_t playerId = client.PlayerStableId();
    const std::vector<std::uint64_t> vehicleIds = host.VehicleStableIds();
    REQUIRE(playerId != 0);
    REQUIRE(vehicleIds.size() >= 2);
    const std::uint64_t firstVehicleId = vehicleIds[0];
    const std::uint64_t secondVehicleId = vehicleIds[1];
    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity firstVehicle =
        host.Simulation().Ecs().FindById(firstVehicleId);
    const zl::ecs::Entity secondVehicle =
        host.Simulation().Ecs().FindById(secondVehicleId);
    REQUIRE(static_cast<bool>(firstVehicle != entt::null));
    REQUIRE(static_cast<bool>(secondVehicle != entt::null));

    const zl::ecs::Position start =
        registry.get<zl::ecs::Position>(firstVehicle);
    int laneCellX = 0;
    int laneCellY = 0;
    float worldDirX = 1.0f;
    float worldDirY = 0.0f;
    REQUIRE(FindOpenVehicleLane(host, start.x, start.y, 12, &laneCellX,
                                &laneCellY, &worldDirX, &worldDirY));
    const zl::ecs::Position firstStart{
        static_cast<float>(laneCellX) + 0.5f,
        static_cast<float>(laneCellY) + 0.5f, start.z};
    const zl::ecs::Position secondStart{
        firstStart.x + worldDirX * 2.5f,
        firstStart.y + worldDirY * 2.5f, firstStart.z};
    REQUIRE(zl::server::ServerHostTestAccess::TeleportVehicle(
        host, firstVehicleId, firstStart.x, firstStart.y, firstStart.z));
    REQUIRE(zl::server::ServerHostTestAccess::TeleportVehicle(
        host, secondVehicleId, secondStart.x, secondStart.y,
        secondStart.z));
    const zl::ecs::Position secondRuntimeStart =
        zl::server::ServerHostTestAccess::VehiclePosition(host,
                                                          secondVehicleId);
    REQUIRE(TeleportServerPlayer(host, playerId, firstStart.x - 1.0f,
                                 firstStart.y));
    REQUIRE_MESSAGE(
        client.SendCommand(MakeVehicleCommand(
                               zl::net::CommandType::EnterVehicle, playerId,
                               firstVehicleId),
                           &error),
        error);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return client.PlayerState().vehicle.driving &&
               client.PlayerState().vehicle.vehicleStableId ==
                   firstVehicleId;
    }));

    const float secondConditionBefore =
        registry.get<zl::ecs::Vehicle>(secondVehicle).condition;
    float screenDirX = 1.0f;
    float screenDirY = 0.0f;
    WorldDirToScreen(worldDirX, worldDirY, &screenDirX, &screenDirY);
    REQUIRE_MESSAGE(client.SendCommand(
                        MakeMove(playerId, screenDirX, screenDirY), &error),
                    error);

    bool sawImpulse = false;
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto& velocity =
            registry.get<zl::ecs::Velocity>(secondVehicle);
        sawImpulse = sawImpulse ||
                     (std::hypot(velocity.x, velocity.y) > 0.5f &&
                      registry.get<zl::ecs::Vehicle>(secondVehicle)
                              .condition <
                          secondConditionBefore - 0.001f);
        return sawImpulse;
    }));
    CHECK(sawImpulse);

    REQUIRE_MESSAGE(client.SendCommand(MakeMove(playerId, 0.0f, 0.0f),
                                        &error),
                    error);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto& velocity =
            registry.get<zl::ecs::Velocity>(secondVehicle);
        return std::hypot(velocity.x, velocity.y) <= 0.06f;
    }));
    const zl::ecs::Position secondEnd =
        zl::server::ServerHostTestAccess::VehiclePosition(host,
                                                          secondVehicleId);
    const float secondMoved =
        std::hypot(secondEnd.x - secondRuntimeStart.x,
                   secondEnd.y - secondRuntimeStart.y);
    CHECK(secondMoved > 0.4f);
    CHECK((secondEnd.x - secondRuntimeStart.x) * worldDirX +
              (secondEnd.y - secondRuntimeStart.y) * worldDirY >
          0.2f);
    CHECK(registry.get<zl::ecs::Vehicle>(secondVehicle).condition <
          secondConditionBefore);

    client.Close();
    host.Shutdown();
}

TEST_CASE("M26-V14 服务器载具低速接触：无车损推挤") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D32364D5634ull;

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
    REQUIRE(PumpUntil(host, client, 8000, [&] {
        return client.IsConnected() && client.HasPlayerState() &&
               host.VehicleStableIds().size() >= 2;
    }));

    const std::uint64_t playerId = client.PlayerStableId();
    const std::vector<std::uint64_t> vehicleIds = host.VehicleStableIds();
    REQUIRE(playerId != 0);
    REQUIRE(vehicleIds.size() >= 2);
    const std::uint64_t firstVehicleId = vehicleIds[0];
    const std::uint64_t secondVehicleId = vehicleIds[1];
    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity firstVehicle =
        host.Simulation().Ecs().FindById(firstVehicleId);
    const zl::ecs::Entity secondVehicle =
        host.Simulation().Ecs().FindById(secondVehicleId);
    REQUIRE(static_cast<bool>(firstVehicle != entt::null));
    REQUIRE(static_cast<bool>(secondVehicle != entt::null));

    const zl::ecs::Position start =
        registry.get<zl::ecs::Position>(firstVehicle);
    int laneCellX = 0;
    int laneCellY = 0;
    float worldDirX = 1.0f;
    float worldDirY = 0.0f;
    REQUIRE(FindOpenVehicleLane(host, start.x, start.y, 12, &laneCellX,
                                &laneCellY, &worldDirX, &worldDirY));
    const zl::ecs::Position firstStart{
        static_cast<float>(laneCellX) + 0.5f,
        static_cast<float>(laneCellY) + 0.5f, start.z};
    const float firstRadius = zl::game::VehicleRadiusForType(
        registry.get<zl::ecs::Vehicle>(firstVehicle).type);
    const float secondRadius = zl::game::VehicleRadiusForType(
        registry.get<zl::ecs::Vehicle>(secondVehicle).type);
    const zl::ecs::Position secondStart{
        firstStart.x + worldDirX * (firstRadius + secondRadius + 0.01f),
        firstStart.y + worldDirY * (firstRadius + secondRadius + 0.01f),
        firstStart.z};
    REQUIRE(zl::server::ServerHostTestAccess::TeleportVehicle(
        host, firstVehicleId, firstStart.x, firstStart.y, firstStart.z));
    REQUIRE(zl::server::ServerHostTestAccess::TeleportVehicle(
        host, secondVehicleId, secondStart.x, secondStart.y,
        secondStart.z));
    const zl::ecs::Position secondRuntimeStart =
        zl::server::ServerHostTestAccess::VehiclePosition(host,
                                                          secondVehicleId);
    REQUIRE(TeleportServerPlayer(host, playerId,
                                 firstStart.x - worldDirX * 1.0f,
                                 firstStart.y - worldDirY * 1.0f));
    REQUIRE_MESSAGE(
        client.SendCommand(MakeVehicleCommand(
                               zl::net::CommandType::EnterVehicle, playerId,
                               firstVehicleId),
                           &error),
        error);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return client.PlayerState().vehicle.driving &&
               client.PlayerState().vehicle.vehicleStableId ==
                   firstVehicleId;
    }));

    const float secondConditionBefore =
        registry.get<zl::ecs::Vehicle>(secondVehicle).condition;
    REQUIRE(zl::server::ServerHostTestAccess::SetVehicleVelocity(
        host, firstVehicleId, worldDirX, worldDirY));

    bool sawContact = false;
    REQUIRE(PumpUntil(host, client, 3000, [&] {
        const auto& velocity =
            registry.get<zl::ecs::Velocity>(secondVehicle);
        sawContact = sawContact || std::hypot(velocity.x, velocity.y) > 0.05f;
        return sawContact;
    }));
    CHECK(sawContact);
    const zl::ecs::Position secondEnd =
        zl::server::ServerHostTestAccess::VehiclePosition(host,
                                                          secondVehicleId);
    CHECK(std::hypot(secondEnd.x - secondRuntimeStart.x,
                     secondEnd.y - secondRuntimeStart.y) > 0.01f);
    CHECK(registry.get<zl::ecs::Vehicle>(secondVehicle).condition ==
          doctest::Approx(secondConditionBefore));

    client.Close();
    host.Shutdown();
}

TEST_CASE("M26-V11 服务器载具碰撞：分级质量与角速度") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D32364D5631ull;

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
    REQUIRE(PumpUntil(host, client, 8000, [&] {
        return client.IsConnected() && client.HasPlayerState() &&
               host.VehicleStableIds().size() >= 2;
    }));

    const std::uint64_t playerId = client.PlayerStableId();
    const std::vector<std::uint64_t> vehicleIds = host.VehicleStableIds();
    REQUIRE(playerId != 0);
    REQUIRE(vehicleIds.size() >= 2);
    const std::uint64_t firstVehicleId = vehicleIds[0];
    const std::uint64_t secondVehicleId = vehicleIds[1];
    CHECK(zl::game::VehicleMassForStableId(firstVehicleId) > 0.0f);
    CHECK(zl::game::VehicleMassForStableId(secondVehicleId) > 0.0f);

    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity firstVehicle =
        host.Simulation().Ecs().FindById(firstVehicleId);
    const zl::ecs::Entity secondVehicle =
        host.Simulation().Ecs().FindById(secondVehicleId);
    REQUIRE(static_cast<bool>(firstVehicle != entt::null));
    REQUIRE(static_cast<bool>(secondVehicle != entt::null));

    const zl::ecs::Position start =
        registry.get<zl::ecs::Position>(firstVehicle);
    int laneCellX = 0;
    int laneCellY = 0;
    float worldDirX = 1.0f;
    float worldDirY = 0.0f;
    REQUIRE(FindOpenVehicleLane(host, start.x, start.y, 12, &laneCellX,
                                &laneCellY, &worldDirX, &worldDirY));
    const zl::ecs::Position firstStart{
        static_cast<float>(laneCellX) + 0.5f,
        static_cast<float>(laneCellY) + 0.5f, start.z};
    const float sideX = -worldDirY;
    const float sideY = worldDirX;
    const zl::ecs::Position secondStart{
        firstStart.x + worldDirX * 2.5f + sideX * 0.30f,
        firstStart.y + worldDirY * 2.5f + sideY * 0.30f, firstStart.z};
    REQUIRE(zl::server::ServerHostTestAccess::TeleportVehicle(
        host, firstVehicleId, firstStart.x, firstStart.y, firstStart.z));
    REQUIRE(zl::server::ServerHostTestAccess::TeleportVehicle(
        host, secondVehicleId, secondStart.x, secondStart.y, secondStart.z));
    REQUIRE(TeleportServerPlayer(host, playerId, firstStart.x - 1.0f,
                                 firstStart.y));
    REQUIRE_MESSAGE(
        client.SendCommand(MakeVehicleCommand(
                               zl::net::CommandType::EnterVehicle, playerId,
                               firstVehicleId),
                           &error),
        error);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return client.PlayerState().vehicle.driving &&
               client.PlayerState().vehicle.vehicleStableId ==
                   firstVehicleId;
    }));

    const float secondConditionBefore =
        registry.get<zl::ecs::Vehicle>(secondVehicle).condition;
    float screenDirX = 1.0f;
    float screenDirY = 0.0f;
    WorldDirToScreen(worldDirX, worldDirY, &screenDirX, &screenDirY);
    REQUIRE_MESSAGE(client.SendCommand(
                        MakeMove(playerId, screenDirX, screenDirY), &error),
                    error);

    const float secondHeadingBefore =
        zl::server::ServerHostTestAccess::VehicleMotion(host, secondVehicleId)
            .heading;
    bool sawSpin = false;
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const zl::game::VehicleMotion motion =
            zl::server::ServerHostTestAccess::VehicleMotion(host,
                                                            secondVehicleId);
        const float headingDelta = std::fabs(std::remainder(
            motion.heading - secondHeadingBefore,
            6.283185307179586f));
        sawSpin = sawSpin || std::fabs(motion.angularVelocity) > 0.01f ||
                  headingDelta > 0.001f;
        return std::hypot(motion.vx, motion.vy) > 0.5f &&
               registry.get<zl::ecs::Vehicle>(secondVehicle).condition <
                   secondConditionBefore - 0.001f;
    }));
    CHECK(sawSpin);
    CHECK(registry.get<zl::ecs::Vehicle>(firstVehicle).condition < 1.0f);
    const zl::game::VehicleMotion firstMotion =
        zl::server::ServerHostTestAccess::VehicleMotion(host, firstVehicleId);
    const zl::game::VehicleMotion secondMotion =
        zl::server::ServerHostTestAccess::VehicleMotion(host,
                                                        secondVehicleId);
    CHECK(std::hypot(firstMotion.vx, firstMotion.vy) > 0.0f);
    CHECK(std::hypot(secondMotion.vx, secondMotion.vy) > 0.0f);

    REQUIRE_MESSAGE(client.SendCommand(MakeMove(playerId, 0.0f, 0.0f),
                                        &error),
                    error);
    client.Close();
    host.Shutdown();
}

TEST_CASE("M26-V7 服务器载具朝向：移动后复制并在停车后保持") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D32364D5637ull;

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
    REQUIRE(PumpUntil(host, client, 8000, [&] {
        return client.IsConnected() && client.HasPlayerState() &&
               host.VehicleStableId() != 0;
    }));

    const std::uint64_t playerId = client.PlayerStableId();
    const std::uint64_t vehicleId = host.VehicleStableId();
    REQUIRE(playerId != 0);
    REQUIRE(vehicleId != 0);
    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity vehicle =
        host.Simulation().Ecs().FindById(vehicleId);
    REQUIRE(static_cast<bool>(vehicle != entt::null));
    const zl::ecs::Position start =
        registry.get<zl::ecs::Position>(vehicle);

    int laneCellX = 0;
    int laneCellY = 0;
    float worldDirX = 1.0f;
    float worldDirY = 0.0f;
    REQUIRE(FindOpenVehicleLane(host, start.x, start.y, 12, &laneCellX,
                                &laneCellY, &worldDirX, &worldDirY));
    const zl::ecs::Position laneStart{
        static_cast<float>(laneCellX) + 0.5f,
        static_cast<float>(laneCellY) + 0.5f, start.z};
    REQUIRE(zl::server::ServerHostTestAccess::TeleportVehicle(
        host, vehicleId, laneStart.x, laneStart.y, laneStart.z));
    REQUIRE(TeleportServerPlayer(host, playerId, laneStart.x - 1.0f,
                                 laneStart.y));
    REQUIRE_MESSAGE(
        client.SendCommand(MakeVehicleCommand(
                               zl::net::CommandType::EnterVehicle, playerId,
                               vehicleId),
                           &error),
        error);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        return client.PlayerState().vehicle.driving &&
               client.PlayerState().vehicle.vehicleStableId == vehicleId;
    }));

    float screenDirX = 1.0f;
    float screenDirY = 0.0f;
    WorldDirToScreen(worldDirX, worldDirY, &screenDirX, &screenDirY);
    REQUIRE_MESSAGE(
        client.SendCommand(MakeMove(playerId, screenDirX, screenDirY), &error),
        error);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto& velocity = registry.get<zl::ecs::Velocity>(vehicle);
        if (std::hypot(velocity.x, velocity.y) <= 2.5f) {
            return false;
        }
        const auto direction =
            zl::server::ServerHostTestAccess::VehicleVisualDirection(
                host, vehicleId);
        const auto expected = zl::game::VehicleVisualDirectionFor(
            velocity.x, velocity.y, direction);
        const auto* actor = client.Replication().Find(vehicleId);
        return direction == expected &&
               registry.get<zl::ecs::SpriteRef>(vehicle).sprite ==
                   zl::ecs::HashName(
                       zl::game::VehicleVisualSprite(direction)) &&
               actor != nullptr && actor->hasSprite &&
               actor->sprite.sprite ==
                   zl::ecs::HashName(
                       zl::game::VehicleVisualSprite(direction));
    }));

    const zl::game::VehicleVisualDirection movingDirection =
        zl::server::ServerHostTestAccess::VehicleVisualDirection(host,
                                                                 vehicleId);
    const std::uint32_t movingSprite =
        registry.get<zl::ecs::SpriteRef>(vehicle).sprite;
    CHECK(registry.get<zl::ecs::SpriteRef>(vehicle).sprite ==
          zl::ecs::HashName(
              zl::game::VehicleVisualSprite(movingDirection)));
    if (const auto* actor = client.Replication().Find(vehicleId);
        actor != nullptr && actor->hasSprite) {
        CHECK(actor->sprite.sprite == movingSprite);
    } else {
        CHECK(false);
    }

    REQUIRE_MESSAGE(client.SendCommand(MakeMove(playerId, 0.0f, 0.0f), &error),
                    error);
    REQUIRE(PumpUntil(host, client, 5000, [&] {
        const auto& velocity = registry.get<zl::ecs::Velocity>(vehicle);
        const auto* actor = client.Replication().Find(vehicleId);
        return std::hypot(velocity.x, velocity.y) <= 0.05f &&
               zl::server::ServerHostTestAccess::VehicleVisualDirection(
                   host, vehicleId) == movingDirection &&
               registry.get<zl::ecs::SpriteRef>(vehicle).sprite ==
                   movingSprite &&
               actor != nullptr && actor->hasSprite &&
               actor->sprite.sprite == movingSprite;
    }));
    CHECK(zl::server::ServerHostTestAccess::VehicleVisualDirection(
              host, vehicleId) == movingDirection);
    CHECK(registry.get<zl::ecs::SpriteRef>(vehicle).sprite == movingSprite);

    client.Close();
    host.Shutdown();
}

TEST_CASE("M30-T6 双客户端：服务器权威重击/格挡/弹反/闪避与体力校验") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    const char* args[] = {"ZomboidLikeTests", "--combat.pvp=true"};
    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.argc = 2;
    hostOptions.argv = args;
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D333054365631ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options options;
    options.host = "127.0.0.1";
    options.port = host.NetworkPort();
    options.contentHash = hostOptions.contentHash;
    options.autoReconnect = false;

    zl::game::NetworkClient attacker(options);
    zl::game::NetworkClient defender(options);
    REQUIRE(attacker.Connect(&error));
    REQUIRE(defender.Connect(&error));
    REQUIRE(PumpUntil(host, attacker, defender, 8000, [&] {
        return attacker.HasPlayerState() && defender.HasPlayerState() &&
               attacker.PlayerState().combat.valid &&
               defender.PlayerState().combat.valid &&
               host.SessionCount() == 2;
    }));
    const std::uint64_t attackerId = attacker.PlayerStableId();
    const std::uint64_t defenderId = defender.PlayerStableId();
    REQUIRE(attackerId != 0);
    REQUIRE(defenderId != 0);
    REQUIRE(attackerId != defenderId);
    CHECK(defender.PlayerState().combat.stamina >
          defender.PlayerState().combat.staminaMax * 0.9f);

    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity attackerEntity =
        host.Simulation().Ecs().FindById(attackerId);
    const zl::ecs::Entity defenderEntity =
        host.Simulation().Ecs().FindById(defenderId);
    REQUIRE(static_cast<bool>(attackerEntity != entt::null));
    REQUIRE(static_cast<bool>(defenderEntity != entt::null));
    registry.get_or_emplace<zl::ecs::Equipment>(attackerEntity)
        .slots[static_cast<std::size_t>(
            zl::ecs::EquipSlot::PrimaryHand)] =
        zl::ecs::ItemInstance{"axe", 1, 1.0f};
    registry.get_or_emplace<zl::ecs::Equipment>(defenderEntity)
        .slots[static_cast<std::size_t>(
            zl::ecs::EquipSlot::PrimaryHand)] =
        zl::ecs::ItemInstance{"axe", 1, 1.0f};

    const auto& defenderStart =
        registry.get<zl::ecs::Position>(defenderEntity);
    REQUIRE(TeleportServerPlayer(host, attackerId, defenderStart.x - 0.8f,
                                 defenderStart.y));
    // 攻击者位于防守者西侧；Left 朝向覆盖来自西侧的伤害方向，确保正面格挡。
    registry.get_or_emplace<zl::ecs::Facing>(defenderEntity).dir =
        zl::ecs::Direction::Left;
    auto* defenderStance =
        registry.try_get<zl::ecs::CombatStance>(defenderEntity);
    auto* defenderStamina =
        registry.try_get<zl::ecs::CombatStamina>(defenderEntity);
    auto* attackerStamina =
        registry.try_get<zl::ecs::CombatStamina>(attackerEntity);
    REQUIRE(defenderStance != nullptr);
    REQUIRE(defenderStamina != nullptr);
    REQUIRE(attackerStamina != nullptr);

    zl::net::Command block;
    block.type = zl::net::CommandType::Block;
    block.playerStableId = defenderId;
    block.payload.flags = zl::net::kBlockDownFlag;
    REQUIRE(defender.SendCommand(block, &error));
    REQUIRE(PumpUntil(host, attacker, defender, 3000, [&] {
        return defenderStance->blocking &&
               defender.PlayerState().combat.stance ==
                   zl::net::kPlayerCombatStanceBlocking;
    }));
    REQUIRE(PumpUntil(host, attacker, defender, 3000, [&] {
        const auto* remote = attacker.Replication().Find(defenderId);
        return remote != nullptr && remote->hasCombatStance &&
               remote->combatStance.blocking;
    }));

    const auto* combat = host.Simulation().Combat();
    REQUIRE(combat != nullptr);
    const std::uint64_t blocksBefore = combat->GetStats().blocks;
    const float defenderHealthBefore =
        registry.get<zl::ecs::Health>(defenderEntity).current;
    const float defenderStaminaBefore = defenderStamina->current;
    const float attackerStaminaBefore = attackerStamina->current;
    defenderStance->blockElapsed = 0.30f;  // 离开弹反窗口，验证普通格挡
    registry.get_or_emplace<zl::ecs::Facing>(defenderEntity).dir =
        zl::ecs::Direction::Left;

    zl::net::Command heavy;
    heavy.type = zl::net::CommandType::Attack;
    heavy.playerStableId = attackerId;
    heavy.payload.entityId = defenderId;
    heavy.payload.flags = zl::net::kAttackHeavyFlag;
    REQUIRE(attacker.SendCommand(heavy, &error));
    REQUIRE(PumpUntil(host, attacker, defender, 4000, [&] {
        return combat->GetStats().blocks > blocksBefore &&
               registry.get<zl::ecs::Health>(defenderEntity).current <
                   defenderHealthBefore;
    }));
    CHECK(registry.get<zl::ecs::Health>(defenderEntity).current <
          defenderHealthBefore);
    CHECK(defenderStamina->current < defenderStaminaBefore);
    CHECK(attackerStamina->current <= attackerStaminaBefore - 27.0f);
    REQUIRE(PumpUntil(host, attacker, defender, 3000, [&] {
        return defender.PlayerState().combat.stamina <
               defenderStaminaBefore - 10.0f;
    }));

    // 弹反：A 轻击前摇 0.10s，B 重新进入 0.18s 弹反窗口后零伤害。
    defenderStance->blockElapsed = 0.0f;
    defenderStance->parryCooldown = 0.0f;
    const std::uint64_t parriesBefore = combat->GetStats().parries;
    const float defenderHealthBeforeParry =
        registry.get<zl::ecs::Health>(defenderEntity).current;
    const std::uint64_t parryWaitUntil = host.TickCount() + 60;
    REQUIRE(PumpHostUntil(host, 5000, [&] {
        return host.TickCount() >= parryWaitUntil;
    }));
    defenderStance->blockElapsed = 0.0f;
    defenderStance->parryCooldown = 0.0f;
    registry.get_or_emplace<zl::ecs::Facing>(defenderEntity).dir =
        zl::ecs::Direction::Left;
    CHECK(defenderStance->blocking);
    zl::net::Command light;
    light.type = zl::net::CommandType::Attack;
    light.playerStableId = attackerId;
    light.payload.entityId = defenderId;
    REQUIRE(attacker.SendCommand(light, &error));
    REQUIRE(PumpUntil(host, attacker, defender, 3000, [&] {
        return combat->GetStats().parries > parriesBefore;
    }));
    CHECK(registry.get<zl::ecs::Health>(defenderEntity).current ==
          doctest::Approx(defenderHealthBeforeParry));
    CHECK(registry.get<zl::ecs::HitReaction>(attackerEntity).staggerLeft >
          0.0f);

    // 松开格挡后闪避：A 用空手（无前摇）攻击，服务器判定无敌帧。
    zl::net::Command release;
    release.type = zl::net::CommandType::Block;
    release.playerStableId = defenderId;
    REQUIRE(defender.SendCommand(release, &error));
    REQUIRE(PumpUntil(host, attacker, defender, 3000, [&] {
        return !defenderStance->blocking;
    }));
    registry.get_or_emplace<zl::ecs::Equipment>(attackerEntity)
        .slots[static_cast<std::size_t>(
            zl::ecs::EquipSlot::PrimaryHand)]
        .reset();
    const std::uint64_t dodgeWaitUntil = host.TickCount() + 75;
    REQUIRE(PumpHostUntil(host, 6000, [&] {
        return host.TickCount() >= dodgeWaitUntil;
    }));
    const std::uint64_t dodgesBefore = combat->GetStats().dodges;
    const float defenderHealthBeforeDodge =
        registry.get<zl::ecs::Health>(defenderEntity).current;
    zl::net::Command dodge;
    dodge.type = zl::net::CommandType::Dodge;
    dodge.playerStableId = defenderId;
    dodge.payload.x = 1.0f;
    dodge.payload.y = 0.0f;
    REQUIRE(defender.SendCommand(dodge, &error));
    REQUIRE(PumpUntil(host, attacker, defender, 2000, [&] {
        return defenderStance->dodgeLeft > 0.0f;
    }));
    REQUIRE(PumpUntil(host, attacker, defender, 2000, [&] {
        const auto* remote = attacker.Replication().Find(defenderId);
        return remote != nullptr && remote->hasCombatStance &&
               remote->combatStance.dodgeLeft > 0.0f &&
               remote->combatStance.dodgeDirX > 0.5f;
    }));
    REQUIRE(attacker.SendCommand(light, &error));
    REQUIRE(PumpUntil(host, attacker, defender, 2000, [&] {
        return combat->GetStats().dodges > dodgesBefore;
    }));
    CHECK(registry.get<zl::ecs::Health>(defenderEntity).current ==
          doctest::Approx(defenderHealthBeforeDodge));

    // 体力不足：闪避命令被拒绝，且客户端无法上传体力覆盖服务器值。
    const std::uint64_t cooldownWaitUntil = host.TickCount() + 60;
    REQUIRE(PumpHostUntil(host, 5000, [&] {
        return host.TickCount() >= cooldownWaitUntil;
    }));
    defenderStance->dodgeCooldown = 0.0f;
    defenderStance->dodgeLeft = 0.0f;
    defenderStamina->current = 1.0f;
    defenderStamina->exhausted = 0.0f;
    defenderStamina->regenDelay = 100.0f;
    const float dodgeLeftBefore = defenderStance->dodgeLeft;
    REQUIRE(defender.SendCommand(dodge, &error));
    const std::uint64_t rejectWaitUntil = host.TickCount() + 20;
    REQUIRE(PumpHostUntil(host, 2000, [&] {
        return host.TickCount() >= rejectWaitUntil;
    }));
    CHECK(defenderStance->dodgeLeft == doctest::Approx(dodgeLeftBefore));
    CHECK(defenderStamina->current == doctest::Approx(1.0f));

    defender.Close();
    attacker.Close();
    host.Shutdown();
}


TEST_CASE("M34-F1-T5 双客户端潜行：SetStealth 权威、STLH、非法与倒地拒绝") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D33344631543531ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkPort() != 0);

    zl::game::NetworkClient::Options options;
    options.host = "127.0.0.1";
    options.port = host.NetworkPort();
    options.contentHash = hostOptions.contentHash;
    options.autoReconnect = false;

    zl::game::NetworkClient first(options);
    zl::game::NetworkClient second(options);
    REQUIRE(first.Connect(&error));
    REQUIRE(second.Connect(&error));
    REQUIRE(PumpUntil(host, first, second, 8000, [&] {
        return first.IsConnected() && second.IsConnected() &&
               first.HasPlayerState() && second.HasPlayerState() &&
               first.PlayerState().stealth.valid &&
               second.PlayerState().stealth.valid &&
               host.SessionCount() == 2;
    }));
    const std::uint64_t firstId = first.PlayerStableId();
    const std::uint64_t secondId = second.PlayerStableId();
    REQUIRE(firstId != 0);
    REQUIRE(secondId != 0);
    REQUIRE(firstId != secondId);

    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity firstEntity =
        host.Simulation().Ecs().FindById(firstId);
    REQUIRE(static_cast<bool>(firstEntity != entt::null));
    auto* stealth = registry.try_get<zl::ecs::StealthState>(firstEntity);
    REQUIRE(stealth != nullptr);

    zl::net::Command down;
    down.type = zl::net::CommandType::SetStealth;
    down.playerStableId = firstId;
    down.payload.flags = zl::net::kStealthCrouchFlag;
    REQUIRE(first.SendCommand(down, &error));
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        const auto* remote = second.Replication().Find(firstId);
        return stealth->crouched && first.PlayerState().stealth.crouched &&
               first.PlayerState().stealth.noiseRadius == doctest::Approx(1.125f) &&
               first.PlayerState().stealth.visibilityScale ==
                   doctest::Approx(0.65f) &&
               remote != nullptr && remote->hasStealth &&
               remote->stealthCrouched;
    }));
    CHECK(stealth->speedScale == doctest::Approx(0.55f));
    CHECK(stealth->noiseScale == doctest::Approx(0.45f));
    CHECK(stealth->visibilityScale == doctest::Approx(0.65f));

    const std::uint64_t rejectedBefore =
        host.GetNetworkStats().commandsRejected;
    zl::net::Command invalid = down;
    invalid.payload.flags = 0x02u;
    invalid.payload.x = 1.0f;
    REQUIRE(first.SendCommand(invalid, &error));
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        return host.GetNetworkStats().commandsRejected > rejectedBefore;
    }));
    CHECK(stealth->crouched);

    zl::net::Command up;
    up.type = zl::net::CommandType::SetStealth;
    up.playerStableId = firstId;
    up.payload.flags = 0;
    REQUIRE(first.SendCommand(up, &error));
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        const auto* remote = second.Replication().Find(firstId);
        return !stealth->crouched && !first.PlayerState().stealth.crouched &&
               remote != nullptr && remote->hasStealth &&
               !remote->stealthCrouched;
    }));
    CHECK(stealth->speedScale == doctest::Approx(1.0f));

    REQUIRE(first.SendCommand(down, &error));
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return stealth->crouched && first.PlayerState().stealth.crouched;
    }));
    registry.get<zl::ecs::Health>(firstEntity).current = 0.0f;
    REQUIRE(PumpUntil(host, first, second, 5000, [&] {
        return !stealth->crouched && !first.PlayerState().stealth.crouched;
    }));

    const std::uint64_t downedRejected =
        host.GetNetworkStats().commandsRejected;
    REQUIRE(first.SendCommand(down, &error));
    REQUIRE(PumpUntil(host, first, second, 3000, [&] {
        return host.GetNetworkStats().commandsRejected > downedRejected;
    }));
    CHECK_FALSE(stealth->crouched);
    CHECK_FALSE(first.PlayerState().stealth.crouched);

    first.Close();
    second.Close();
    host.Shutdown();
}


TEST_CASE("M34-F1-T5 服务器攻击写入背刺参数并退出潜行") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x4D33344631544154ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);

    zl::game::NetworkClient::Options options;
    options.host = "127.0.0.1";
    options.port = host.NetworkPort();
    options.contentHash = hostOptions.contentHash;
    options.autoReconnect = false;
    zl::game::NetworkClient client(options);
    REQUIRE(client.Connect(&error));
    REQUIRE(PumpUntil(host, client, 6000, [&] {
        return client.HasPlayerState() && client.PlayerState().stealth.valid;
    }));
    const std::uint64_t playerId = client.PlayerStableId();
    auto& registry = host.Simulation().Ecs().Registry();
    const zl::ecs::Entity player =
        host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));
    const auto& position = registry.get<zl::ecs::Position>(player);
    const zl::ecs::Entity zombie = host.Simulation().SpawnZombieAt(
        position.x + 0.8f, position.y + 0.8f);
    REQUIRE(static_cast<bool>(zombie != entt::null));
    registry.get_or_emplace<zl::ecs::AiState>(zombie).status =
        zl::ecs::AiStatus::Idle;
    registry.get_or_emplace<zl::ecs::PerceptionState>(zombie).alertness = 0.0f;
    registry.get_or_emplace<zl::ecs::Facing>(zombie).dir =
        zl::ecs::Direction::Down;

    zl::net::Command crouch;
    crouch.type = zl::net::CommandType::SetStealth;
    crouch.playerStableId = playerId;
    crouch.payload.flags = zl::net::kStealthCrouchFlag;
    REQUIRE(zl::server::ServerHostTestAccess::ExecuteCommand(host, playerId,
                                                             crouch));
    auto* stealth = registry.try_get<zl::ecs::StealthState>(player);
    REQUIRE(stealth != nullptr);
    CHECK(stealth->crouched);

    zl::net::Command attack;
    attack.type = zl::net::CommandType::Attack;
    attack.playerStableId = playerId;
    attack.payload.entityId = host.Simulation().Ecs().IdOf(zombie).id;
    REQUIRE(zl::server::ServerHostTestAccess::ExecuteCommand(host, playerId,
                                                             attack));
    const auto* intent = registry.try_get<zl::ecs::AttackIntent>(player);
    REQUIRE(intent != nullptr);
    CHECK(intent->pending);
    CHECK(intent->backstabMultiplier == doctest::Approx(1.6f));
    CHECK(intent->backstabHalfAngleDegrees == doctest::Approx(60.0f));
    CHECK_FALSE(stealth->crouched);

    client.Close();
    host.Shutdown();
}
