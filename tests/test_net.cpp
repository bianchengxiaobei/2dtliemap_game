#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "net/Command.h"
#include "net/CommandCodec.h"
#include "net/CommandPipeline.h"
#include "net/Loopback.h"

using namespace zl::net;

namespace {

Command MakeCommand(CommandType type, std::uint64_t player = 42, std::uint64_t tick = 100) {
    Command command;
    command.type = type;
    command.playerStableId = player;
    command.tick = tick;
    return command;
}

Command MakeFullCommand() {
    Command command =
        MakeCommand(CommandType::InventoryAction, 9007199254740993ull, 123456789ull);
    command.payload.x = -1.25f;
    command.payload.y = 3.5f;
    command.payload.index = 7;
    command.payload.targetIndex = 1;
    command.payload.area = 2;
    command.payload.targetArea = 0;
    command.payload.flags = 5;
    command.payload.entityId = 0x1122334455667788ull;
    command.payload.targetEntityId = 0x8877665544332211ull;
    return command;
}

void CheckCommandEqual(const Command& actual, const Command& expected) {
    CHECK(actual.type == expected.type);
    CHECK(actual.playerStableId == expected.playerStableId);
    CHECK(actual.tick == expected.tick);
    CHECK(actual.payload.x == expected.payload.x);
    CHECK(actual.payload.y == expected.payload.y);
    CHECK(actual.payload.index == expected.payload.index);
    CHECK(actual.payload.targetIndex == expected.payload.targetIndex);
    CHECK(actual.payload.area == expected.payload.area);
    CHECK(actual.payload.targetArea == expected.payload.targetArea);
    CHECK(actual.payload.flags == expected.payload.flags);
    CHECK(actual.payload.entityId == expected.payload.entityId);
    CHECK(actual.payload.targetEntityId == expected.payload.targetEntityId);
}

}  // namespace

TEST_CASE("M12 CommandPacket：全字段往返 + 固定线格式") {
    CommandPacket packet;
    packet.sequence = 77;
    packet.commands.push_back(MakeFullCommand());
    packet.commands.push_back(MakeCommand(CommandType::Move, 42, 5));
    packet.commands.back().payload.x = 0.75f;
    packet.commands.back().payload.y = -0.25f;
    packet.commands.push_back(MakeCommand(CommandType::PickupEntity, 42, 6));
    packet.commands.back().payload.entityId = 0xAABBCCDDEEFF0011ull;

    std::string error;
    const std::vector<std::uint8_t> bytes = EncodeCommandPacket(packet, &error);
    REQUIRE(error.empty());
    CHECK(bytes.size() == kCommandPacketHeaderSize + packet.commands.size() * kCommandWireSize);

    CommandPacket decoded;
    REQUIRE(DecodeCommandPacket(bytes, &decoded, &error));
    CHECK(error.empty());
    CHECK(decoded.sequence == packet.sequence);
    REQUIRE(decoded.commands.size() == packet.commands.size());
    for (std::size_t i = 0; i < packet.commands.size(); ++i) {
        CheckCommandEqual(decoded.commands[i], packet.commands[i]);
    }
}

TEST_CASE("M12 CommandPacket：拒绝截断、magic/类型/StableId/NaN/数量/尾部非法") {
    CommandPacket packet;
    packet.sequence = 1;
    packet.commands.push_back(MakeCommand(CommandType::Move));
    std::string error;
    const std::vector<std::uint8_t> good = EncodeCommandPacket(packet, &error);
    REQUIRE(!good.empty());

    CommandPacket decoded;
    CHECK_FALSE(DecodeCommandPacket(std::span<const std::uint8_t>{}, &decoded, &error));
    CHECK_FALSE(DecodeCommandPacket(
        std::span<const std::uint8_t>(good.data(), good.size() - 1), &decoded, &error));

    std::vector<std::uint8_t> bad = good;
    bad[0] = 0;
    CHECK_FALSE(DecodeCommandPacket(bad, &decoded, &error));

    bad = good;
    bad[12] = 0;  // type = 0
    bad[13] = 0;
    CHECK_FALSE(DecodeCommandPacket(bad, &decoded, &error));

    bad = good;
    for (std::size_t i = 16; i < 24; ++i) {
        bad[i] = 0;  // playerStableId = 0
    }
    CHECK_FALSE(DecodeCommandPacket(bad, &decoded, &error));

    bad = good;
    // command 从 12 开始：type(2)+reserved(2)+player(8)+tick(8) => x 在 32
    bad[32] = 0x00;
    bad[33] = 0x00;
    bad[34] = 0xC0;
    bad[35] = 0x7F;  // NaN
    CHECK_FALSE(DecodeCommandPacket(bad, &decoded, &error));

    bad = good;
    bad[6] = static_cast<std::uint8_t>(kMaxCommandsPerPacket + 1);
    bad[7] = 0;
    CHECK_FALSE(DecodeCommandPacket(bad, &decoded, &error));

    bad = good;
    bad.push_back(0);
    CHECK_FALSE(DecodeCommandPacket(bad, &decoded, &error));
}

TEST_CASE("M12 ServerSnapshot：往返与非法长度") {
    ServerSnapshot snapshot;
    snapshot.serverTick = 123;
    snapshot.processedCommands = 456;
    snapshot.playerStableId = 789;
    snapshot.x = -12.5f;
    snapshot.y = 33.25f;
    snapshot.z = 1;

    std::string error;
    const std::vector<std::uint8_t> bytes = EncodeServerSnapshot(snapshot, &error);
    REQUIRE(error.empty());
    CHECK(bytes.size() == kServerSnapshotWireSize);

    ServerSnapshot decoded;
    REQUIRE(DecodeServerSnapshot(bytes, &decoded, &error));
    CHECK(decoded.serverTick == snapshot.serverTick);
    CHECK(decoded.processedCommands == snapshot.processedCommands);
    CHECK(decoded.playerStableId == snapshot.playerStableId);
    CHECK(decoded.x == snapshot.x);
    CHECK(decoded.y == snapshot.y);
    CHECK(decoded.z == snapshot.z);

    CHECK_FALSE(DecodeServerSnapshot(std::span<const std::uint8_t>(bytes.data(), 4),
                                     &decoded, &error));
}

TEST_CASE("M12 CommandPipeline：direct 立即执行，loopback 经编码/队列/服务器后一致") {
    std::vector<Command> directSeen;
    CommandPipeline direct;
    direct.SetHandler([&](const Command& command, std::string&) {
        directSeen.push_back(command);
        return true;
    });

    std::vector<Command> loopSeen;
    CommandPipeline::Options options;
    options.mode = TransportMode::Loopback;
    options.maxCommandsPerPacket = 8;
    CommandPipeline loop(options);
    loop.SetHandler([&](const Command& command, std::string&) {
        loopSeen.push_back(command);
        return true;
    });
    loop.SetSnapshotProvider([] {
        ServerSnapshot snapshot;
        snapshot.serverTick = 321;
        snapshot.playerStableId = 42;
        snapshot.x = 10.0f;
        snapshot.y = 20.0f;
        return snapshot;
    });

    std::vector<Command> commands;
    Command move = MakeCommand(CommandType::Move, 42, 11);
    move.payload.x = 0.5f;
    move.payload.y = -0.5f;
    commands.push_back(move);
    Command pickup = MakeCommand(CommandType::PickupEntity, 42, 12);
    pickup.payload.entityId = 0x1234567890ABCDEFull;
    commands.push_back(pickup);

    for (const Command& command : commands) {
        REQUIRE(direct.Submit(command));
        CHECK(loopSeen.empty());  // loopback 尚未 Flush
        REQUIRE(loop.Submit(command));
    }
    CHECK(directSeen.size() == 2);
    CHECK(loopSeen.empty());

    REQUIRE(loop.Flush());
    CHECK(loopSeen.empty());  // 已发包，但服务器要下一 tick 才 Pump
    loop.Pump();

    REQUIRE(loopSeen.size() == 2);
    for (std::size_t i = 0; i < commands.size(); ++i) {
        CheckCommandEqual(loopSeen[i], directSeen[i]);
    }
    CHECK(loop.HasSnapshot());
    CHECK(loop.LastSnapshot().serverTick == 321);
    CHECK(loop.LastSnapshot().playerStableId == 42);
    CHECK(loop.LastSnapshot().processedCommands == 2);

    const CommandPipeline::Stats directStats = direct.GetStats();
    const CommandPipeline::Stats loopStats = loop.GetStats();
    CHECK(directStats.commandsExecuted == 2);
    CHECK(directStats.packetsSent == 0);
    CHECK(loopStats.commandsExecuted == 2);
    CHECK(loopStats.packetsSent == 1);
    CHECK(loopStats.packetsReceived == 1);
    CHECK(loopStats.snapshotsReceived == 1);
    CHECK(loopStats.commandsRejected == 0);
}

TEST_CASE("M12 CommandPipeline：StableId 非法与处理拒绝计数") {
    CommandPipeline pipeline;
    int calls = 0;
    pipeline.SetHandler([&](const Command&, std::string& error) {
        ++calls;
        error = "rejected by test";
        return false;
    });

    Command invalid = MakeCommand(CommandType::Gunshot, 0);
    std::string error;
    CHECK_FALSE(pipeline.Submit(invalid, &error));
    CHECK(calls == 0);
    CHECK(pipeline.GetStats().commandsRejected == 0);

    Command rejected = MakeCommand(CommandType::Gunshot, 5);
    CHECK_FALSE(pipeline.Submit(rejected, &error));
    CHECK(calls == 1);
    CHECK(error == "rejected by test");
    CHECK(pipeline.GetStats().commandsRejected == 1);
}

TEST_CASE("M12 LoopbackTransport：字节队列与背压") {
    LoopbackTransport transport(1);
    LoopbackClient client(transport);
    std::vector<Command> commands{MakeCommand(CommandType::Gunshot)};
    CHECK(client.Send(commands));
    CHECK(transport.ToServerQueued() == 1);
    CHECK_FALSE(client.Send(commands));
    CHECK(client.GetStats().sendFailures == 1);

    std::vector<std::uint8_t> bytes;
    REQUIRE(transport.ReceiveOnServer(&bytes));
    CommandPacket packet;
    REQUIRE(DecodeCommandPacket(bytes, &packet));
    CHECK(packet.commands.size() == 1);
    CHECK(transport.ToServerQueued() == 0);
}
