#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace zl::net {

inline constexpr std::uint32_t kSessionMagic = 0x314D4C5Au;
inline constexpr std::uint16_t kSessionProtocolVersion = 2;
inline constexpr std::size_t kSessionHeaderSize = 12;
inline constexpr std::size_t kMaxSessionPayload = 60 * 1024;
inline constexpr std::size_t kMaxSessionText = 256;
inline constexpr std::size_t kMaxSessionIdentity = 64;
inline constexpr std::size_t kMaxSessionName = 48;

enum class SessionMessageType : std::uint8_t {
    Hello = 1,
    Welcome = 2,
    Reject = 3,
    Disconnect = 4,
    Data = 5,
    Ping = 6,
    Pong = 7,
};

enum class SessionDisconnectReason : std::uint8_t {
    None = 0,
    ProtocolVersion = 1,
    ContentMismatch = 2,
    ServerFull = 3,
    HandshakeTimeout = 4,
    SessionTimeout = 5,
    ClientLeft = 6,
    ProtocolError = 7,
    ServerShutdown = 8,
    BadRequest = 9,
};

enum class SessionDecodeError : std::uint8_t {
    None = 0,
    TooShort,
    BadMagic,
    BadVersion,
    BadType,
    BadLength,
};

struct SessionMessage {
    SessionMessageType type = SessionMessageType::Data;
    std::vector<std::uint8_t> payload;
};

struct SessionHello {
    std::uint32_t clientNonce = 0;
    std::uint64_t contentHash = 0;
    // M13-T8：跨连接/重启稳定的玩家身份。空 identity 仅兼容测试工具，
    // 正式客户端始终发送非空值；服务器会为空值退化为 session 局部身份。
    std::string playerIdentity;
    std::string playerName;
};

struct SessionWelcome {
    std::uint64_t sessionId = 0;
    std::uint64_t serverTick = 0;
    std::uint64_t contentHash = 0;
    std::uint32_t heartbeatMs = 0;
};

struct SessionReject {
    SessionDisconnectReason reason = SessionDisconnectReason::None;
    std::string message;
};

struct SessionDisconnect {
    SessionDisconnectReason reason = SessionDisconnectReason::None;
    std::string message;
};

std::string_view SessionMessageTypeName(SessionMessageType type);
std::string_view SessionDisconnectReasonName(SessionDisconnectReason reason);

std::vector<std::uint8_t> EncodeSessionMessage(SessionMessageType type,
                                               std::span<const std::uint8_t> payload,
                                               std::string* error = nullptr);
bool DecodeSessionMessage(std::span<const std::uint8_t> frame, SessionMessage* message,
                          std::string* error = nullptr,
                          SessionDecodeError* decodeError = nullptr);

std::vector<std::uint8_t> EncodeSessionHello(const SessionHello& hello,
                                             std::string* error = nullptr);
bool DecodeSessionHello(std::span<const std::uint8_t> payload, SessionHello* hello,
                        std::string* error = nullptr);

std::vector<std::uint8_t> EncodeSessionWelcome(const SessionWelcome& welcome,
                                               std::string* error = nullptr);
bool DecodeSessionWelcome(std::span<const std::uint8_t> payload, SessionWelcome* welcome,
                          std::string* error = nullptr);

std::vector<std::uint8_t> EncodeSessionReject(const SessionReject& reject,
                                              std::string* error = nullptr);
bool DecodeSessionReject(std::span<const std::uint8_t> payload, SessionReject* reject,
                         std::string* error = nullptr);

std::vector<std::uint8_t> EncodeSessionDisconnect(const SessionDisconnect& disconnect,
                                                  std::string* error = nullptr);
bool DecodeSessionDisconnect(std::span<const std::uint8_t> payload,
                             SessionDisconnect* disconnect, std::string* error = nullptr);

std::vector<std::uint8_t> EncodeSessionPing(std::uint64_t nonce,
                                            std::string* error = nullptr);
bool DecodeSessionPing(std::span<const std::uint8_t> payload, std::uint64_t* nonce,
                       std::string* error = nullptr);

std::vector<std::uint8_t> EncodeSessionPong(std::uint64_t nonce,
                                            std::string* error = nullptr);
bool DecodeSessionPong(std::span<const std::uint8_t> payload, std::uint64_t* nonce,
                       std::string* error = nullptr);

}  // namespace zl::net
