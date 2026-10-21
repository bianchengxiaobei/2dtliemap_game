#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace zl::net {

// M13-T5：服务器在握手后通过 Data 通道告知客户端本会话控制的实体。
// 不扩展 Welcome 线格式，保持 T2 会话协议 v1 兼容。
inline constexpr std::uint32_t kPlayerAssignmentMagic = 0x31414C5Au;  // "ZLA1"
inline constexpr std::uint16_t kPlayerAssignmentVersion = 1;
inline constexpr std::size_t kPlayerAssignmentWireSize = 32;

struct PlayerAssignment {
    std::uint64_t sessionId = 0;
    std::uint64_t playerStableId = 0;
    std::uint64_t serverTick = 0;
};

std::vector<std::uint8_t> EncodePlayerAssignment(const PlayerAssignment& assignment,
                                                 std::string* error = nullptr);
bool DecodePlayerAssignment(std::span<const std::uint8_t> bytes,
                            PlayerAssignment* assignment, std::string* error = nullptr);

}  // namespace zl::net
