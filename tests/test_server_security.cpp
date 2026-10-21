#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "ecs/components/CommonComponents.h"
#include "game/WorldSimulation.h"
#include "net/Command.h"
#include "net/CommandCodec.h"
#include "net/KcpSession.h"
#include "net/PlayerAssignment.h"
#include "server/ServerHost.h"

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

std::uint32_t ReadU32Le(const std::vector<std::uint8_t>& bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

template <typename Predicate>
bool PumpUntil(zl::server::ServerHost& host, zl::net::KcpSessionClient& raw,
               int timeoutMs, Predicate predicate) {
    const Clock::time_point deadline =
        Clock::now() + std::chrono::milliseconds(timeoutMs);
    Clock::time_point nextTick = Clock::now();
    while (Clock::now() < deadline) {
        if (Clock::now() >= nextTick) {
            host.Tick(1.0f / 60.0f);
            nextTick += std::chrono::milliseconds(16);
        }
        raw.Poll();
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

std::vector<std::uint8_t> EncodePacket(std::uint32_t sequence,
                                       const std::vector<zl::net::Command>& commands) {
    zl::net::CommandPacket packet;
    packet.sequence = sequence;
    packet.commands = commands;
    return zl::net::EncodeCommandPacket(packet);
}

}  // namespace

TEST_CASE("M13-T6 服务器拒绝越权/超距/限频/冷却与畸形包") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.spawnDemoPopulation = false;
    options.spawnLocalPlayer = false;
    options.network = true;
    options.port = 0;
    options.contentHash = 0x544E4553543631ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    REQUIRE(host.NetworkEnabled());
    REQUIRE(host.NetworkPort() != 0);

    zl::net::KcpSessionClientOptions sessionOptions;
    sessionOptions.contentHash = options.contentHash;
    sessionOptions.handshakeTimeoutMs = 1000;
    sessionOptions.heartbeatIntervalMs = 100;
    sessionOptions.heartbeatTimeoutMs = 500;
    zl::net::KcpSessionClient raw;
    REQUIRE(raw.Connect("127.0.0.1", host.NetworkPort(), sessionOptions, &error));

    std::uint64_t playerId = 0;
    std::vector<std::uint8_t> message;
    REQUIRE(PumpUntil(host, raw, 6000, [&] {
        while (raw.Receive(&message)) {
            if (message.size() >= 4 &&
                ReadU32Le(message) == zl::net::kPlayerAssignmentMagic) {
                zl::net::PlayerAssignment assignment;
                REQUIRE(zl::net::DecodePlayerAssignment(message, &assignment));
                playerId = assignment.playerStableId;
            }
        }
        return raw.IsConnected() && playerId != 0;
    }));
    REQUIRE(playerId != 0);
    REQUIRE(host.SessionCount() == 1);

    const zl::ecs::Entity player = host.Simulation().Ecs().FindById(playerId);
    REQUIRE(static_cast<bool>(player != entt::null));
    auto& registry = host.Simulation().Ecs().Registry();
    REQUIRE(registry.all_of<zl::ecs::Position>(player));

    std::uint32_t sequence = 1;
    const auto send = [&](const std::vector<zl::net::Command>& commands) {
        const std::vector<std::uint8_t> bytes = EncodePacket(sequence++, commands);
        REQUIRE_FALSE(bytes.empty());
        REQUIRE(raw.Send(bytes, &error));
    };

    // 越权：命令声称控制另一个 StableId。
    send({MakeMove(playerId + 1, 1.0f, 0.0f)});
    host.RunForTicks(1);
    CHECK(host.GetNetworkStats().ownershipRejected >= 1);

    // 协议层坏包与超大命令包都不能打崩或踢掉会话。
    const std::vector<std::uint8_t> malformed(zl::net::kCommandPacketHeaderSize, 0);
    REQUIRE(raw.Send(malformed, &error));
    host.RunForTicks(1);
    CHECK(host.GetNetworkStats().malformedPackets >= 1);

    const std::vector<std::uint8_t> oversized(
        zl::net::kMaxCommandPacketBytes + 1, 0x5A);
    REQUIRE(raw.Send(oversized, &error));
    host.RunForTicks(1);
    CHECK(host.GetNetworkStats().oversizedPackets >= 1);
    CHECK(raw.IsConnected());

    // 超距 MoveTo：服务器不把任意坐标交给寻路。
    const auto& playerPosition = registry.get<zl::ecs::Position>(player);
    zl::net::Command farMove;
    farMove.type = zl::net::CommandType::MoveTo;
    farMove.playerStableId = playerId;
    farMove.payload.x = playerPosition.x + 1000.0f;
    farMove.payload.y = playerPosition.y;
    send({farMove});
    host.RunForTicks(1);
    CHECK(host.GetNetworkStats().rangeRejected >= 1);

    // 单 tick 命令配额：合法包内 12 条 Move，最多执行 8 条。
    host.RunForTicks(1);  // 清空上一 tick 配额
    std::vector<zl::net::Command> flood;
    flood.reserve(12);
    for (int i = 0; i < 12; ++i) {
        flood.push_back(MakeMove(playerId, 1.0f, 0.0f));
    }
    send(flood);
    host.RunForTicks(1);
    CHECK(host.GetNetworkStats().rateLimitedCommands >= 4);

    host.RunForTicks(1);  // 新 tick，恢复攻击配额

    // 近身目标上连续两次 Attack：第二次被服务器冷却拒绝，目标只掉一次血。
    const auto& currentPosition = registry.get<zl::ecs::Position>(player);
    const zl::ecs::Entity zombie = host.Simulation().SpawnZombieAt(
        currentPosition.x + 0.8f, currentPosition.y);
    REQUIRE(static_cast<bool>(zombie != entt::null));
    const std::uint64_t zombieId = host.Simulation().Ecs().IdOf(zombie).id;
    REQUIRE(zombieId != 0);
    const float healthBefore = registry.get<zl::ecs::Health>(zombie).current;

    zl::net::Command attack;
    attack.type = zl::net::CommandType::Attack;
    attack.playerStableId = playerId;
    attack.payload.entityId = zombieId;
    send({attack, attack});
    host.RunForTicks(1);
    CHECK(host.GetNetworkStats().cooldownRejected >= 1);
    CHECK(registry.get<zl::ecs::Health>(zombie).current < healthBefore);

    // 经过这些恶意/非法输入后，会话仍然存活并继续接受合法命令。
    const std::uint64_t acceptedBefore = host.GetNetworkStats().commandsAccepted;
    send({MakeMove(playerId, 0.0f, 1.0f)});
    host.RunForTicks(1);
    CHECK(host.GetNetworkStats().commandsAccepted > acceptedBefore);
    CHECK(registry.get<zl::ecs::MoveInput>(player).y > 0.5f);
    CHECK(host.SessionCount() == 1);
    CHECK(raw.IsConnected());

    raw.Close();
    host.Shutdown();
    CHECK_FALSE(host.Running());
}
