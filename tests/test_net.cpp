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
#include "net/WorldSound.h"

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
    packet.commands.push_back(MakeCommand(CommandType::ChoosePerk, 42, 7));
    packet.commands.back().payload.index = 2;
    packet.commands.push_back(MakeCommand(CommandType::PlaceStructure, 42, 8));
    packet.commands.back().payload.x = 70.0f;
    packet.commands.back().payload.y = 24.0f;
    packet.commands.back().payload.index = 1;
    packet.commands.back().payload.flags = 1;
    packet.commands.push_back(MakeCommand(CommandType::DemolishStructure, 42, 9));
    packet.commands.back().payload.x = 70.0f;
    packet.commands.back().payload.y = 24.0f;
    packet.commands.push_back(MakeCommand(CommandType::RepairStructure, 42, 10));
    packet.commands.back().payload.x = 70.0f;
    packet.commands.back().payload.y = 24.0f;
    packet.commands.push_back(MakeCommand(CommandType::ToggleDoor, 42, 11));
    packet.commands.back().payload.x = 70.0f;
    packet.commands.back().payload.y = 24.0f;
    packet.commands.push_back(MakeCommand(CommandType::InteractFacility, 42, 12));
    packet.commands.back().payload.entityId = 0x123456789ull;
    packet.commands.push_back(MakeCommand(CommandType::TradeOffer, 42, 13));
    packet.commands.back().payload.entityId = 0x987654321ull;
    packet.commands.back().payload.index = 3;
    packet.commands.push_back(MakeCommand(CommandType::EnterVehicle, 42, 14));
    packet.commands.back().payload.entityId = 0x1122334455667788ull;
    packet.commands.push_back(MakeCommand(CommandType::ExitVehicle, 42, 15));
    packet.commands.push_back(MakeCommand(CommandType::ServiceVehicle, 42, 16));
    packet.commands.back().payload.entityId = 0x8877665544332211ull;
    packet.commands.push_back(MakeCommand(CommandType::ToggleHeadlights, 42, 17));
    packet.commands.push_back(MakeCommand(CommandType::HonkHorn, 42, 18));
    packet.commands.push_back(MakeCommand(CommandType::TakeDriverSeat, 42, 19));
    packet.commands.push_back(MakeCommand(CommandType::EnterVehiclePassenger, 42, 20));
    packet.commands.back().payload.entityId = 0x4455667788990011ull;
    packet.commands.push_back(MakeCommand(CommandType::GiveDriverSeat, 42, 21));
    packet.commands.push_back(MakeCommand(CommandType::Block, 42, 22));
    packet.commands.back().payload.flags = kBlockDownFlag;
    packet.commands.push_back(MakeCommand(CommandType::Dodge, 42, 23));
    packet.commands.back().payload.x = 0.0f;
    packet.commands.back().payload.y = -1.0f;
    packet.commands.push_back(MakeCommand(CommandType::Attack, 42, 24));
    packet.commands.back().payload.entityId = 0x99AABBCCDDEEFF00ull;
    packet.commands.back().payload.flags = kAttackHeavyFlag;
    packet.commands.push_back(MakeCommand(CommandType::ThrowItem, 42, 25));
    packet.commands.back().payload.index = 2;
    packet.commands.back().payload.x = 36.5f;
    packet.commands.back().payload.y = 28.5f;

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
    CHECK(std::string(CommandTypeName(CommandType::EnterVehicle)) ==
          "EnterVehicle");
    CHECK(std::string(CommandTypeName(CommandType::ExitVehicle)) ==
          "ExitVehicle");
    CHECK(std::string(CommandTypeName(CommandType::ServiceVehicle)) ==
          "ServiceVehicle");
    CHECK(std::string(CommandTypeName(CommandType::ToggleHeadlights)) ==
          "ToggleHeadlights");
    CHECK(std::string(CommandTypeName(CommandType::HonkHorn)) ==
          "HonkHorn");
    CHECK(std::string(CommandTypeName(CommandType::TakeDriverSeat)) ==
          "TakeDriverSeat");
    CHECK(std::string(CommandTypeName(CommandType::EnterVehiclePassenger)) ==
          "EnterVehiclePassenger");
    CHECK(std::string(CommandTypeName(CommandType::GiveDriverSeat)) ==
          "GiveDriverSeat");
    CHECK(std::string(CommandTypeName(CommandType::Block)) == "Block");
    CHECK(std::string(CommandTypeName(CommandType::Dodge)) == "Dodge");
    CHECK(std::string(CommandTypeName(CommandType::RecruitCompanion)) ==
          "RecruitCompanion");
    CHECK(std::string(CommandTypeName(CommandType::DismissCompanion)) ==
          "DismissCompanion");
    CHECK(std::string(CommandTypeName(CommandType::SetCompanionCommand)) ==
          "SetCompanionCommand");
    CHECK(std::string(CommandTypeName(CommandType::GiftToCompanion)) ==
          "GiftToCompanion");
    CHECK(std::string(CommandTypeName(CommandType::AcceptCompanionQuest)) ==
          "AcceptCompanionQuest");
    CHECK(std::string(CommandTypeName(CommandType::AbandonCompanionQuest)) ==
          "AbandonCompanionQuest");
    CHECK(std::string(CommandTypeName(CommandType::RescueCompanion)) ==
          "RescueCompanion");
    CHECK(std::string(CommandTypeName(CommandType::InstallVehicleMod)) ==
          "InstallVehicleMod");
    CHECK(std::string(CommandTypeName(CommandType::RemoveVehicleMod)) ==
          "RemoveVehicleMod");
    CHECK(std::string(CommandTypeName(CommandType::PoiInteract)) ==
          "PoiInteract");
    CHECK(std::string(CommandTypeName(CommandType::ThrowItem)) == "ThrowItem");
    CHECK(IsValidCommandType(CommandType::EnterVehicle));
    CHECK(IsValidCommandType(CommandType::ExitVehicle));
    CHECK(IsValidCommandType(CommandType::ServiceVehicle));
    CHECK(IsValidCommandType(CommandType::ToggleHeadlights));
    CHECK(IsValidCommandType(CommandType::HonkHorn));
    CHECK(IsValidCommandType(CommandType::TakeDriverSeat));
    CHECK(IsValidCommandType(CommandType::EnterVehiclePassenger));
    CHECK(IsValidCommandType(CommandType::GiveDriverSeat));
    CHECK(IsValidCommandType(CommandType::Block));
    CHECK(IsValidCommandType(CommandType::Dodge));
    CHECK(IsValidCommandType(CommandType::ChooseWorldEvent));
    CHECK(IsValidCommandType(CommandType::RecruitCompanion));
    CHECK(IsValidCommandType(CommandType::DismissCompanion));
    CHECK(IsValidCommandType(CommandType::SetCompanionCommand));
    CHECK(IsValidCommandType(CommandType::GiftToCompanion));
    CHECK(IsValidCommandType(CommandType::AcceptCompanionQuest));
    CHECK(IsValidCommandType(CommandType::AbandonCompanionQuest));
    CHECK(IsValidCommandType(CommandType::RescueCompanion));
    CHECK(IsValidCommandType(CommandType::InstallVehicleMod));
    CHECK(IsValidCommandType(CommandType::RemoveVehicleMod));
    CHECK(IsValidCommandType(CommandType::PoiInteract));
    CHECK(IsValidCommandType(CommandType::ThrowItem));
    CHECK_FALSE(IsValidCommandType(static_cast<CommandType>(63)));
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

TEST_CASE("M34-F1-T5 SetStealth loopback 命令：flags 往返且无其他 payload") {
    CHECK(std::string(CommandTypeName(CommandType::SetStealth)) == "SetStealth");
    bool crouched = false;
    int handled = 0;
    CommandPipeline::Options options;
    options.mode = TransportMode::Loopback;
    CommandPipeline pipeline(options);
    pipeline.SetHandler([&](const Command& command, std::string&) {
        CHECK(command.type == CommandType::SetStealth);
        CHECK(command.payload.entityId == 0);
        CHECK(command.payload.targetEntityId == 0);
        CHECK(command.payload.index == -1);
        CHECK(command.payload.targetIndex == -1);
        CHECK(command.payload.x == 0.0f);
        CHECK(command.payload.y == 0.0f);
        crouched = (command.payload.flags & kStealthCrouchFlag) != 0;
        ++handled;
        return true;
    });

    Command down = MakeCommand(CommandType::SetStealth, 42, 1);
    down.payload.flags = kStealthCrouchFlag;
    REQUIRE(pipeline.Submit(down));
    CHECK(handled == 0);
    REQUIRE(pipeline.Flush());
    pipeline.Pump();
    CHECK(handled == 1);
    CHECK(crouched);

    Command up = MakeCommand(CommandType::SetStealth, 42, 2);
    up.payload.flags = 0;
    REQUIRE(pipeline.Submit(up));
    REQUIRE(pipeline.Flush());
    pipeline.Pump();
    CHECK(handled == 2);
    CHECK_FALSE(crouched);
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

TEST_CASE("M22-F1 CommandPacket：睡眠请求/投票/取消往返") {
    CommandPacket packet;
    packet.sequence = 88;
    Command request = MakeCommand(CommandType::SleepRequest, 42, 21);
    request.payload.x = 240.0f;
    request.payload.flags = kSleepUntilDawnFlag;
    packet.commands.push_back(request);
    Command vote = MakeCommand(CommandType::SleepVote, 42, 22);
    vote.payload.flags = kSleepVoteYesFlag;
    packet.commands.push_back(vote);
    packet.commands.push_back(MakeCommand(CommandType::SleepCancel, 42, 23));

    std::string error;
    const std::vector<std::uint8_t> bytes = EncodeCommandPacket(packet, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    CommandPacket decoded;
    REQUIRE(DecodeCommandPacket(bytes, &decoded, &error));
    REQUIRE(decoded.commands.size() == 3);
    CheckCommandEqual(decoded.commands[0], packet.commands[0]);
    CheckCommandEqual(decoded.commands[1], packet.commands[1]);
    CheckCommandEqual(decoded.commands[2], packet.commands[2]);
}

TEST_CASE("M23-F1-T11 WorldSound：往返 + 边界 + 截断/非法拒绝") {
    WorldSoundEvent sound;
    sound.sourcePlayerStableId = 9007199254740993ull;
    sound.x = 12.5f;
    sound.y = -3.25f;
    sound.z = 1;
    sound.radius = 28.0f;
    sound.type = 1;
    sound.clip = "gunshot";

    std::string error;
    const std::vector<std::uint8_t> bytes = EncodeWorldSound(sound, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    CHECK(bytes.size() == kWorldSoundHeaderSize + sound.clip.size());

    WorldSoundEvent decoded;
    REQUIRE(DecodeWorldSound(bytes, &decoded, &error));
    CHECK(decoded.sourcePlayerStableId == sound.sourcePlayerStableId);
    CHECK(decoded.x == doctest::Approx(sound.x));
    CHECK(decoded.y == doctest::Approx(sound.y));
    CHECK(decoded.z == sound.z);
    CHECK(decoded.radius == doctest::Approx(sound.radius));
    CHECK(decoded.type == sound.type);
    CHECK(decoded.clip == sound.clip);

    WorldSoundEvent invalid = sound;
    invalid.sourcePlayerStableId = 0;
    CHECK(EncodeWorldSound(invalid, &error).empty());
    invalid = sound;
    invalid.type = 7;
    CHECK(EncodeWorldSound(invalid, &error).empty());
    invalid = sound;
    invalid.radius = -1.0f;
    CHECK(EncodeWorldSound(invalid, &error).empty());
    invalid = sound;
    invalid.z = 9;
    CHECK(EncodeWorldSound(invalid, &error).empty());
    invalid = sound;
    invalid.clip = "bad/clip";
    CHECK(EncodeWorldSound(invalid, &error).empty());
    invalid = sound;
    invalid.clip.assign(kMaxWorldSoundClipBytes + 1, 'a');
    CHECK(EncodeWorldSound(invalid, &error).empty());

    CHECK_FALSE(DecodeWorldSound(std::span<const std::uint8_t>{}, &decoded,
                                 &error));
    std::vector<std::uint8_t> bad = bytes;
    bad[0] ^= 0xFFu;
    CHECK_FALSE(DecodeWorldSound(bad, &decoded, &error));
    bad = bytes;
    bad[4] = 2;
    CHECK_FALSE(DecodeWorldSound(bad, &decoded, &error));
    bad = bytes;
    bad[7] = 1;
    CHECK_FALSE(DecodeWorldSound(bad, &decoded, &error));
    bad = bytes;
    bad[6] = 7;
    CHECK_FALSE(DecodeWorldSound(bad, &decoded, &error));
    bad = bytes;
    for (std::size_t i = 8; i < 16; ++i) {
        bad[i] = 0;
    }
    CHECK_FALSE(DecodeWorldSound(bad, &decoded, &error));
    bad = bytes;
    bad.pop_back();
    CHECK_FALSE(DecodeWorldSound(bad, &decoded, &error));
    bad = bytes;
    bad.push_back(0);
    CHECK_FALSE(DecodeWorldSound(bad, &decoded, &error));
    bad = bytes;
    bad.back() = static_cast<std::uint8_t>('/');
    CHECK_FALSE(DecodeWorldSound(bad, &decoded, &error));
}
