#include "net/SessionProtocol.h"

#include <cstring>

namespace zl::net {
namespace {

std::uint16_t ReadU16Le(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0]) |
           (static_cast<std::uint16_t>(data[1]) << 8U);
}

std::uint32_t ReadU32Le(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

std::uint64_t ReadU64Le(const std::uint8_t* data) {
    return static_cast<std::uint64_t>(ReadU32Le(data)) |
           (static_cast<std::uint64_t>(ReadU32Le(data + 4)) << 32U);
}

void WriteU16Le(std::uint8_t* data, std::uint16_t value) {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8U);
}

void WriteU32Le(std::uint8_t* data, std::uint32_t value) {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8U);
    data[2] = static_cast<std::uint8_t>(value >> 16U);
    data[3] = static_cast<std::uint8_t>(value >> 24U);
}

void WriteU64Le(std::uint8_t* data, std::uint64_t value) {
    WriteU32Le(data, static_cast<std::uint32_t>(value));
    WriteU32Le(data + 4, static_cast<std::uint32_t>(value >> 32U));
}

bool ValidMessageType(SessionMessageType type) {
    switch (type) {
        case SessionMessageType::Hello:
        case SessionMessageType::Welcome:
        case SessionMessageType::Reject:
        case SessionMessageType::Disconnect:
        case SessionMessageType::Data:
        case SessionMessageType::Ping:
        case SessionMessageType::Pong:
            return true;
    }
    return false;
}

bool ValidReason(SessionDisconnectReason reason) {
    return static_cast<std::uint8_t>(reason) <=
           static_cast<std::uint8_t>(SessionDisconnectReason::BadRequest);
}

std::vector<std::uint8_t> EncodeTextPayload(SessionDisconnectReason reason,
                                            std::string_view text, std::string* error) {
    if (!ValidReason(reason)) {
        if (error != nullptr) {
            *error = "非法断开原因";
        }
        return {};
    }
    if (text.size() > kMaxSessionText) {
        if (error != nullptr) {
            *error = "断开原因文本超过 256 字节";
        }
        return {};
    }

    std::vector<std::uint8_t> payload(3 + text.size());
    payload[0] = static_cast<std::uint8_t>(reason);
    WriteU16Le(payload.data() + 1, static_cast<std::uint16_t>(text.size()));
    if (!text.empty()) {
        std::memcpy(payload.data() + 3, text.data(), text.size());
    }
    return payload;
}

bool DecodeTextPayload(std::span<const std::uint8_t> payload,
                       SessionDisconnectReason* reason, std::string* text,
                       std::string* error) {
    if (payload.size() < 3) {
        if (error != nullptr) {
            *error = "断开消息载荷过短";
        }
        return false;
    }
    const auto value = static_cast<SessionDisconnectReason>(payload[0]);
    if (!ValidReason(value)) {
        if (error != nullptr) {
            *error = "非法断开原因";
        }
        return false;
    }
    const std::size_t length = ReadU16Le(payload.data() + 1);
    if (length > kMaxSessionText || payload.size() != 3 + length) {
        if (error != nullptr) {
            *error = "断开消息文本长度非法";
        }
        return false;
    }
    if (reason != nullptr) {
        *reason = value;
    }
    if (text != nullptr) {
        text->assign(reinterpret_cast<const char*>(payload.data() + 3), length);
    }
    return true;
}

}  // namespace

std::string_view SessionMessageTypeName(SessionMessageType type) {
    switch (type) {
        case SessionMessageType::Hello:
            return "Hello";
        case SessionMessageType::Welcome:
            return "Welcome";
        case SessionMessageType::Reject:
            return "Reject";
        case SessionMessageType::Disconnect:
            return "Disconnect";
        case SessionMessageType::Data:
            return "Data";
        case SessionMessageType::Ping:
            return "Ping";
        case SessionMessageType::Pong:
            return "Pong";
    }
    return "Unknown";
}

std::string_view SessionDisconnectReasonName(SessionDisconnectReason reason) {
    switch (reason) {
        case SessionDisconnectReason::None:
            return "None";
        case SessionDisconnectReason::ProtocolVersion:
            return "ProtocolVersion";
        case SessionDisconnectReason::ContentMismatch:
            return "ContentMismatch";
        case SessionDisconnectReason::ServerFull:
            return "ServerFull";
        case SessionDisconnectReason::HandshakeTimeout:
            return "HandshakeTimeout";
        case SessionDisconnectReason::SessionTimeout:
            return "SessionTimeout";
        case SessionDisconnectReason::ClientLeft:
            return "ClientLeft";
        case SessionDisconnectReason::ProtocolError:
            return "ProtocolError";
        case SessionDisconnectReason::ServerShutdown:
            return "ServerShutdown";
        case SessionDisconnectReason::BadRequest:
            return "BadRequest";
    }
    return "Unknown";
}

std::vector<std::uint8_t> EncodeSessionMessage(SessionMessageType type,
                                               std::span<const std::uint8_t> payload,
                                               std::string* error) {
    if (!ValidMessageType(type)) {
        if (error != nullptr) {
            *error = "非法会话消息类型";
        }
        return {};
    }
    if (payload.size() > kMaxSessionPayload) {
        if (error != nullptr) {
            *error = "会话消息超过 60KB 上限";
        }
        return {};
    }

    std::vector<std::uint8_t> frame(kSessionHeaderSize + payload.size());
    WriteU32Le(frame.data(), kSessionMagic);
    WriteU16Le(frame.data() + 4, kSessionProtocolVersion);
    frame[6] = static_cast<std::uint8_t>(type);
    frame[7] = 0;
    WriteU32Le(frame.data() + 8, static_cast<std::uint32_t>(payload.size()));
    if (!payload.empty()) {
        std::memcpy(frame.data() + kSessionHeaderSize, payload.data(), payload.size());
    }
    return frame;
}

bool DecodeSessionMessage(std::span<const std::uint8_t> frame, SessionMessage* message,
                          std::string* error, SessionDecodeError* decodeError) {
    if (decodeError != nullptr) {
        *decodeError = SessionDecodeError::None;
    }
    if (frame.size() < kSessionHeaderSize) {
        if (decodeError != nullptr) {
            *decodeError = SessionDecodeError::TooShort;
        }
        if (error != nullptr) {
            *error = "会话消息头过短";
        }
        return false;
    }
    if (ReadU32Le(frame.data()) != kSessionMagic) {
        if (decodeError != nullptr) {
            *decodeError = SessionDecodeError::BadMagic;
        }
        if (error != nullptr) {
            *error = "会话消息 magic 非法";
        }
        return false;
    }
    if (ReadU16Le(frame.data() + 4) != kSessionProtocolVersion) {
        if (decodeError != nullptr) {
            *decodeError = SessionDecodeError::BadVersion;
        }
        if (error != nullptr) {
            *error = "会话协议版本不匹配";
        }
        return false;
    }

    const auto type = static_cast<SessionMessageType>(frame[6]);
    if (!ValidMessageType(type)) {
        if (decodeError != nullptr) {
            *decodeError = SessionDecodeError::BadType;
        }
        if (error != nullptr) {
            *error = "会话消息类型非法";
        }
        return false;
    }

    const std::size_t payloadSize = ReadU32Le(frame.data() + 8);
    if (payloadSize > kMaxSessionPayload ||
        payloadSize != frame.size() - kSessionHeaderSize) {
        if (decodeError != nullptr) {
            *decodeError = SessionDecodeError::BadLength;
        }
        if (error != nullptr) {
            *error = "会话消息长度非法";
        }
        return false;
    }

    if (message == nullptr) {
        if (error != nullptr) {
            *error = "会话消息输出为空";
        }
        return false;
    }
    message->type = type;
    message->payload.assign(frame.begin() + static_cast<std::ptrdiff_t>(kSessionHeaderSize),
                            frame.end());
    return true;
}

namespace {

bool ValidPlayerText(std::string_view text, std::size_t maxBytes, bool allowEmpty) {
    if ((!allowEmpty && text.empty()) || text.size() > maxBytes) {
        return false;
    }
    for (const unsigned char c : text) {
        if (c == 0 || c < 0x20 || c == 0x7F) {
            return false;
        }
    }
    return true;
}

bool ReadPlayerText(std::span<const std::uint8_t> payload, std::size_t* offset,
                    std::size_t maxBytes, bool allowEmpty, std::string* out,
                    std::string* error) {
    if (*offset + 2 > payload.size()) {
        if (error != nullptr) {
            *error = "Hello 文本长度字段截断";
        }
        return false;
    }
    const std::size_t size = ReadU16Le(payload.data() + *offset);
    *offset += 2;
    if (size > maxBytes || *offset + size > payload.size()) {
        if (error != nullptr) {
            *error = "Hello 文本长度非法";
        }
        return false;
    }
    const std::string_view text(
        reinterpret_cast<const char*>(payload.data() + *offset), size);
    if (!ValidPlayerText(text, maxBytes, allowEmpty)) {
        if (error != nullptr) {
            *error = "Hello 文本包含非法字符";
        }
        return false;
    }
    out->assign(text);
    *offset += size;
    return true;
}

}  // namespace

std::vector<std::uint8_t> EncodeSessionHello(const SessionHello& hello,
                                             std::string* error) {
    if (!ValidPlayerText(hello.playerIdentity, kMaxSessionIdentity, true)) {
        if (error != nullptr) {
            *error = "Hello playerIdentity 长度或字符非法";
        }
        return {};
    }
    if (!ValidPlayerText(hello.playerName, kMaxSessionName, true)) {
        if (error != nullptr) {
            *error = "Hello playerName 长度或字符非法";
        }
        return {};
    }

    std::vector<std::uint8_t> payload(12 + 2 + hello.playerIdentity.size() + 2 +
                                      hello.playerName.size());
    WriteU32Le(payload.data(), hello.clientNonce);
    WriteU64Le(payload.data() + 4, hello.contentHash);
    std::size_t offset = 12;
    WriteU16Le(payload.data() + offset,
               static_cast<std::uint16_t>(hello.playerIdentity.size()));
    offset += 2;
    if (!hello.playerIdentity.empty()) {
        std::memcpy(payload.data() + offset, hello.playerIdentity.data(),
                    hello.playerIdentity.size());
        offset += hello.playerIdentity.size();
    }
    WriteU16Le(payload.data() + offset,
               static_cast<std::uint16_t>(hello.playerName.size()));
    offset += 2;
    if (!hello.playerName.empty()) {
        std::memcpy(payload.data() + offset, hello.playerName.data(),
                    hello.playerName.size());
    }
    return EncodeSessionMessage(SessionMessageType::Hello, payload, error);
}

bool DecodeSessionHello(std::span<const std::uint8_t> payload, SessionHello* hello,
                        std::string* error) {
    if (payload.size() < 16 || hello == nullptr) {
        if (error != nullptr) {
            *error = "Hello 载荷长度非法";
        }
        return false;
    }
    SessionHello decoded;
    decoded.clientNonce = ReadU32Le(payload.data());
    decoded.contentHash = ReadU64Le(payload.data() + 4);
    std::size_t offset = 12;
    if (!ReadPlayerText(payload, &offset, kMaxSessionIdentity, true,
                        &decoded.playerIdentity, error) ||
        !ReadPlayerText(payload, &offset, kMaxSessionName, true, &decoded.playerName,
                        error) ||
        offset != payload.size()) {
        if (error != nullptr && error->empty()) {
            *error = "Hello 载荷存在尾随数据";
        }
        return false;
    }
    *hello = std::move(decoded);
    return true;
}

std::vector<std::uint8_t> EncodeSessionWelcome(const SessionWelcome& welcome,
                                               std::string* error) {
    std::vector<std::uint8_t> payload(28);
    WriteU64Le(payload.data(), welcome.sessionId);
    WriteU64Le(payload.data() + 8, welcome.serverTick);
    WriteU64Le(payload.data() + 16, welcome.contentHash);
    WriteU32Le(payload.data() + 24, welcome.heartbeatMs);
    return EncodeSessionMessage(SessionMessageType::Welcome, payload, error);
}

bool DecodeSessionWelcome(std::span<const std::uint8_t> payload, SessionWelcome* welcome,
                          std::string* error) {
    if (payload.size() != 28 || welcome == nullptr) {
        if (error != nullptr) {
            *error = "Welcome 载荷长度非法";
        }
        return false;
    }
    welcome->sessionId = ReadU64Le(payload.data());
    welcome->serverTick = ReadU64Le(payload.data() + 8);
    welcome->contentHash = ReadU64Le(payload.data() + 16);
    welcome->heartbeatMs = ReadU32Le(payload.data() + 24);
    if (welcome->sessionId == 0) {
        if (error != nullptr) {
            *error = "Welcome sessionId 不能为 0";
        }
        return false;
    }
    return true;
}

std::vector<std::uint8_t> EncodeSessionReject(const SessionReject& reject,
                                              std::string* error) {
    const std::vector<std::uint8_t> payload =
        EncodeTextPayload(reject.reason, reject.message, error);
    if (payload.empty() && !reject.message.empty()) {
        return {};
    }
    return EncodeSessionMessage(SessionMessageType::Reject, payload, error);
}

bool DecodeSessionReject(std::span<const std::uint8_t> payload, SessionReject* reject,
                         std::string* error) {
    if (reject == nullptr) {
        if (error != nullptr) {
            *error = "Reject 输出为空";
        }
        return false;
    }
    return DecodeTextPayload(payload, &reject->reason, &reject->message, error);
}

std::vector<std::uint8_t> EncodeSessionDisconnect(const SessionDisconnect& disconnect,
                                                  std::string* error) {
    const std::vector<std::uint8_t> payload =
        EncodeTextPayload(disconnect.reason, disconnect.message, error);
    if (payload.empty() && !disconnect.message.empty()) {
        return {};
    }
    return EncodeSessionMessage(SessionMessageType::Disconnect, payload, error);
}

bool DecodeSessionDisconnect(std::span<const std::uint8_t> payload,
                             SessionDisconnect* disconnect, std::string* error) {
    if (disconnect == nullptr) {
        if (error != nullptr) {
            *error = "Disconnect 输出为空";
        }
        return false;
    }
    return DecodeTextPayload(payload, &disconnect->reason, &disconnect->message, error);
}

std::vector<std::uint8_t> EncodeSessionPing(std::uint64_t nonce, std::string* error) {
    std::vector<std::uint8_t> payload(8);
    WriteU64Le(payload.data(), nonce);
    return EncodeSessionMessage(SessionMessageType::Ping, payload, error);
}

bool DecodeSessionPing(std::span<const std::uint8_t> payload, std::uint64_t* nonce,
                       std::string* error) {
    if (payload.size() != 8 || nonce == nullptr) {
        if (error != nullptr) {
            *error = "Ping 载荷长度非法";
        }
        return false;
    }
    *nonce = ReadU64Le(payload.data());
    return true;
}

std::vector<std::uint8_t> EncodeSessionPong(std::uint64_t nonce, std::string* error) {
    std::vector<std::uint8_t> payload(8);
    WriteU64Le(payload.data(), nonce);
    return EncodeSessionMessage(SessionMessageType::Pong, payload, error);
}

bool DecodeSessionPong(std::span<const std::uint8_t> payload, std::uint64_t* nonce,
                       std::string* error) {
    if (payload.size() != 8 || nonce == nullptr) {
        if (error != nullptr) {
            *error = "Pong 载荷长度非法";
        }
        return false;
    }
    *nonce = ReadU64Le(payload.data());
    return true;
}

}  // namespace zl::net
