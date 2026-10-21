#include "net/PlayerAssignment.h"

#include <utility>

namespace zl::net {
namespace {

void PutU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFu));
}

void PutU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFu));
    }
}

void PutU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFu));
    }
}

std::uint16_t ReadU16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(bytes[0]) |
           (static_cast<std::uint16_t>(bytes[1]) << 8U);
}

std::uint32_t ReadU32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::uint64_t ReadU64(const std::uint8_t* bytes) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(bytes[i]) << (i * 8U);
    }
    return value;
}

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

}  // namespace

std::vector<std::uint8_t> EncodePlayerAssignment(const PlayerAssignment& assignment,
                                                 std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (assignment.sessionId == 0) {
        SetError(error, "玩家分配消息缺少 sessionId");
        return {};
    }
    if (assignment.playerStableId == 0) {
        SetError(error, "玩家分配消息缺少 playerStableId");
        return {};
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kPlayerAssignmentWireSize);
    PutU32(bytes, kPlayerAssignmentMagic);
    PutU16(bytes, kPlayerAssignmentVersion);
    PutU16(bytes, 0);
    PutU64(bytes, assignment.sessionId);
    PutU64(bytes, assignment.playerStableId);
    PutU64(bytes, assignment.serverTick);
    return bytes;
}

bool DecodePlayerAssignment(std::span<const std::uint8_t> bytes,
                            PlayerAssignment* assignment, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (assignment == nullptr) {
        SetError(error, "玩家分配输出为空");
        return false;
    }
    if (bytes.size() != kPlayerAssignmentWireSize) {
        SetError(error, "玩家分配消息长度非法");
        return false;
    }
    if (ReadU32(bytes.data()) != kPlayerAssignmentMagic) {
        SetError(error, "玩家分配消息 magic 不匹配");
        return false;
    }
    if (ReadU16(bytes.data() + 4) != kPlayerAssignmentVersion) {
        SetError(error, "玩家分配消息协议版本不支持");
        return false;
    }
    if (ReadU16(bytes.data() + 6) != 0) {
        SetError(error, "玩家分配消息保留字段非 0");
        return false;
    }
    PlayerAssignment decoded;
    decoded.sessionId = ReadU64(bytes.data() + 8);
    decoded.playerStableId = ReadU64(bytes.data() + 16);
    decoded.serverTick = ReadU64(bytes.data() + 24);
    if (decoded.sessionId == 0 || decoded.playerStableId == 0) {
        SetError(error, "玩家分配消息字段非法");
        return false;
    }
    *assignment = decoded;
    return true;
}

}  // namespace zl::net
