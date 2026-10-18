#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "net/Command.h"

namespace zl::net {

// M12-T1/T2：命令包线格式 v1（小端、定长、无 padding）。
//
// 包 = magic/version/count/sequence + N × Command
// Command = type(u16)/reserved(u16)/player(u64)/tick(u64)/payload(44 bytes)
// 单包最多 64 条命令；解码严格校验长度、类型、StableId 与浮点有限性。
inline constexpr std::uint32_t kCommandPacketMagic = 0x31434C5Au;  // "ZLC1"
inline constexpr std::uint32_t kServerSnapshotMagic = 0x31534C5Au;  // "ZLS1"
inline constexpr std::uint16_t kNetProtocolVersion = 1;
inline constexpr std::uint16_t kMaxCommandsPerPacket = 64;
inline constexpr std::size_t kCommandPacketHeaderSize = 12;
inline constexpr std::size_t kCommandWireSize = 56;
inline constexpr std::size_t kMaxCommandPacketBytes = 4096;
inline constexpr std::size_t kServerSnapshotWireSize = 44;

struct CommandPacket {
    std::uint32_t sequence = 0;
    std::vector<Command> commands;
};

// 服务器回包（环回模式用于验证 Client 收包路径；M12 不做状态回滚/纠正）。
struct ServerSnapshot {
    std::uint64_t serverTick = 0;
    std::uint64_t processedCommands = 0;
    std::uint64_t playerStableId = 0;
    float x = 0.0f;
    float y = 0.0f;
    std::int32_t z = 0;
};

std::vector<std::uint8_t> EncodeCommandPacket(const CommandPacket& packet,
                                              std::string* error = nullptr);
bool DecodeCommandPacket(std::span<const std::uint8_t> bytes, CommandPacket* packet,
                         std::string* error = nullptr);

std::vector<std::uint8_t> EncodeServerSnapshot(const ServerSnapshot& snapshot,
                                               std::string* error = nullptr);
bool DecodeServerSnapshot(std::span<const std::uint8_t> bytes, ServerSnapshot* snapshot,
                          std::string* error = nullptr);

}  // namespace zl::net
