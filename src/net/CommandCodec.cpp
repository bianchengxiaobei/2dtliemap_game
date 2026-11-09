#include "net/CommandCodec.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace zl::net {
namespace {

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

void PutU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void PutU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu));
    }
}

void PutU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu));
    }
}

void PutF32(std::vector<std::uint8_t>& out, float value) {
    PutU32(out, std::bit_cast<std::uint32_t>(value));
}

void PutI32(std::vector<std::uint8_t>& out, std::int32_t value) {
    PutU32(out, static_cast<std::uint32_t>(value));
}

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : m_bytes(bytes) {}

    bool U16(std::uint16_t* value) {
        if (!Has(2)) {
            return false;
        }
        *value = static_cast<std::uint16_t>(m_bytes[m_offset]) |
                 (static_cast<std::uint16_t>(m_bytes[m_offset + 1]) << 8);
        m_offset += 2;
        return true;
    }

    bool U32(std::uint32_t* value) {
        if (!Has(4)) {
            return false;
        }
        std::uint32_t result = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            result |= static_cast<std::uint32_t>(m_bytes[m_offset + i]) << (i * 8);
        }
        *value = result;
        m_offset += 4;
        return true;
    }

    bool U64(std::uint64_t* value) {
        if (!Has(8)) {
            return false;
        }
        std::uint64_t result = 0;
        for (std::size_t i = 0; i < 8; ++i) {
            result |= static_cast<std::uint64_t>(m_bytes[m_offset + i]) << (i * 8);
        }
        *value = result;
        m_offset += 8;
        return true;
    }

    bool F32(float* value) {
        std::uint32_t raw = 0;
        if (!U32(&raw)) {
            return false;
        }
        *value = std::bit_cast<float>(raw);
        return true;
    }

    bool I32(std::int32_t* value) {
        std::uint32_t raw = 0;
        if (!U32(&raw)) {
            return false;
        }
        *value = static_cast<std::int32_t>(raw);
        return true;
    }

    bool Bytes(void* destination, std::size_t size) {
        if (!Has(size)) {
            return false;
        }
        std::memcpy(destination, m_bytes.data() + m_offset, size);
        m_offset += size;
        return true;
    }

    bool AtEnd() const { return m_offset == m_bytes.size(); }
    bool Has(std::size_t count) const { return count <= m_bytes.size() - m_offset; }
    std::size_t Offset() const { return m_offset; }

private:
    std::span<const std::uint8_t> m_bytes;
    std::size_t m_offset = 0;
};

void EncodeCommand(std::vector<std::uint8_t>& out, const Command& command) {
    PutU16(out, static_cast<std::uint16_t>(command.type));
    PutU16(out, 0);  // reserved
    PutU64(out, command.playerStableId);
    PutU64(out, command.tick);
    PutF32(out, command.payload.x);
    PutF32(out, command.payload.y);
    PutI32(out, command.payload.index);
    PutI32(out, command.payload.targetIndex);
    out.push_back(command.payload.area);
    out.push_back(command.payload.targetArea);
    PutU16(out, command.payload.flags);
    PutU64(out, command.payload.entityId);
    PutU64(out, command.payload.targetEntityId);
}

bool DecodeCommand(Reader& reader, Command* command, std::string* error) {
    std::uint16_t type = 0;
    std::uint16_t reserved = 0;
    if (!reader.U16(&type) || !reader.U16(&reserved)) {
        SetError(error, "命令头截断");
        return false;
    }
    if (reserved != 0) {
        SetError(error, "命令保留字段非 0");
        return false;
    }
    if (!IsValidCommandType(static_cast<CommandType>(type))) {
        SetError(error, "未知命令类型 " + std::to_string(type));
        return false;
    }
    Command decoded;
    decoded.type = static_cast<CommandType>(type);
    if (!reader.U64(&decoded.playerStableId) || !reader.U64(&decoded.tick) ||
        !reader.F32(&decoded.payload.x) || !reader.F32(&decoded.payload.y) ||
        !reader.I32(&decoded.payload.index) || !reader.I32(&decoded.payload.targetIndex) ||
        !reader.Bytes(&decoded.payload.area, 1) ||
        !reader.Bytes(&decoded.payload.targetArea, 1) ||
        !reader.U16(&decoded.payload.flags) || !reader.U64(&decoded.payload.entityId) ||
        !reader.U64(&decoded.payload.targetEntityId)) {
        SetError(error, "命令数据截断");
        return false;
    }
    if (decoded.playerStableId == 0) {
        SetError(error, "命令缺少 playerStableId");
        return false;
    }
    if (!std::isfinite(decoded.payload.x) || !std::isfinite(decoded.payload.y)) {
        SetError(error, "命令 payload 浮点非法（NaN/Inf）");
        return false;
    }
    *command = decoded;
    return true;
}

}  // namespace

std::vector<std::uint8_t> EncodeCommandPacket(const CommandPacket& packet,
                                              std::string* error) {
    if (packet.commands.size() > kMaxCommandsPerPacket) {
        SetError(error, "命令包超过单包上限 " + std::to_string(kMaxCommandsPerPacket));
        return {};
    }
    const std::size_t total =
        kCommandPacketHeaderSize + packet.commands.size() * kCommandWireSize;
    if (total > kMaxCommandPacketBytes) {
        SetError(error, "命令包超过字节上限");
        return {};
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(total);
    PutU32(bytes, kCommandPacketMagic);
    PutU16(bytes, kNetProtocolVersion);
    PutU16(bytes, static_cast<std::uint16_t>(packet.commands.size()));
    PutU32(bytes, packet.sequence);
    for (const Command& command : packet.commands) {
        if (!IsValidCommandType(command.type)) {
            SetError(error, "命令类型非法: " + std::string(CommandTypeName(command.type)));
            return {};
        }
        if (command.playerStableId == 0) {
            SetError(error, "命令缺少 playerStableId");
            return {};
        }
        if (!std::isfinite(command.payload.x) || !std::isfinite(command.payload.y)) {
            SetError(error, "命令 payload 浮点非法（NaN/Inf）");
            return {};
        }
        EncodeCommand(bytes, command);
    }
    return bytes;
}

bool DecodeCommandPacket(std::span<const std::uint8_t> bytes, CommandPacket* packet,
                         std::string* error) {
    if (packet == nullptr) {
        SetError(error, "输出 CommandPacket 为空");
        return false;
    }
    if (bytes.size() < kCommandPacketHeaderSize) {
        SetError(error, "命令包头截断");
        return false;
    }
    Reader reader(bytes);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t count = 0;
    std::uint32_t sequence = 0;
    if (!reader.U32(&magic) || !reader.U16(&version) || !reader.U16(&count) ||
        !reader.U32(&sequence)) {
        SetError(error, "命令包头截断");
        return false;
    }
    if (magic != kCommandPacketMagic) {
        SetError(error, "命令包 magic 不匹配");
        return false;
    }
    if (version != kNetProtocolVersion) {
        SetError(error, "命令包协议版本不支持: " + std::to_string(version));
        return false;
    }
    if (count > kMaxCommandsPerPacket) {
        SetError(error, "命令数量超过上限 " + std::to_string(kMaxCommandsPerPacket));
        return false;
    }
    const std::size_t expected =
        kCommandPacketHeaderSize + static_cast<std::size_t>(count) * kCommandWireSize;
    if (bytes.size() != expected) {
        SetError(error, "命令包长度不匹配（期望 " + std::to_string(expected) + "，实际 " +
                            std::to_string(bytes.size()) + "）");
        return false;
    }

    CommandPacket decoded;
    decoded.sequence = sequence;
    decoded.commands.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        Command command;
        if (!DecodeCommand(reader, &command, error)) {
            return false;
        }
        decoded.commands.push_back(std::move(command));
    }
    if (!reader.AtEnd()) {
        SetError(error, "命令包尾部存在多余字节");
        return false;
    }
    *packet = std::move(decoded);
    return true;
}

std::vector<std::uint8_t> EncodeServerSnapshot(const ServerSnapshot& snapshot,
                                               std::string* error) {
    if (!std::isfinite(snapshot.x) || !std::isfinite(snapshot.y)) {
        SetError(error, "快照浮点非法（NaN/Inf）");
        return {};
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kServerSnapshotWireSize);
    PutU32(bytes, kServerSnapshotMagic);
    PutU16(bytes, kNetProtocolVersion);
    PutU16(bytes, 0);
    PutU64(bytes, snapshot.serverTick);
    PutU64(bytes, snapshot.processedCommands);
    PutU64(bytes, snapshot.playerStableId);
    PutF32(bytes, snapshot.x);
    PutF32(bytes, snapshot.y);
    PutI32(bytes, snapshot.z);
    return bytes;
}

bool DecodeServerSnapshot(std::span<const std::uint8_t> bytes, ServerSnapshot* snapshot,
                          std::string* error) {
    if (snapshot == nullptr) {
        SetError(error, "输出 ServerSnapshot 为空");
        return false;
    }
    if (bytes.size() != kServerSnapshotWireSize) {
        SetError(error, "服务器快照长度不匹配");
        return false;
    }
    Reader reader(bytes);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t reserved = 0;
    ServerSnapshot decoded;
    if (!reader.U32(&magic) || !reader.U16(&version) || !reader.U16(&reserved) ||
        !reader.U64(&decoded.serverTick) || !reader.U64(&decoded.processedCommands) ||
        !reader.U64(&decoded.playerStableId) || !reader.F32(&decoded.x) ||
        !reader.F32(&decoded.y) || !reader.I32(&decoded.z)) {
        SetError(error, "服务器快照截断");
        return false;
    }
    if (magic != kServerSnapshotMagic) {
        SetError(error, "服务器快照 magic 不匹配");
        return false;
    }
    if (version != kNetProtocolVersion) {
        SetError(error, "服务器快照协议版本不支持: " + std::to_string(version));
        return false;
    }
    if (reserved != 0) {
        SetError(error, "服务器快照保留字段非 0");
        return false;
    }
    if (!std::isfinite(decoded.x) || !std::isfinite(decoded.y)) {
        SetError(error, "服务器快照浮点非法（NaN/Inf）");
        return false;
    }
    *snapshot = decoded;
    return true;
}

}  // namespace zl::net
