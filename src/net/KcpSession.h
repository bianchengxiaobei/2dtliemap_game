#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "net/Kcp.h"
#include "net/SessionProtocol.h"

namespace zl::net {

using SessionId = std::uint64_t;

enum class KcpSessionClientState : std::uint8_t {
    Disconnected = 0,
    Connecting,
    Connected,
    Rejected,
};

struct KcpSessionClientOptions {
    std::uint64_t contentHash = 0;
    std::uint32_t handshakeTimeoutMs = 5000;
    std::uint32_t heartbeatIntervalMs = 5000;
    std::uint32_t heartbeatTimeoutMs = 15000;
    // M13-T8：持久玩家身份；空值由上层决定是否生成。
    std::string playerIdentity;
    std::string playerName;
};

struct KcpSessionClientStats {
    std::uint64_t messagesSent = 0;
    std::uint64_t messagesReceived = 0;
    std::uint64_t dataSent = 0;
    std::uint64_t dataReceived = 0;
    std::uint64_t heartbeatsSent = 0;
    std::uint64_t rejects = 0;
    std::uint64_t disconnects = 0;
    KcpStats transport;
};

class KcpSessionClient {
public:
    KcpSessionClient();
    ~KcpSessionClient();

    KcpSessionClient(const KcpSessionClient&) = delete;
    KcpSessionClient& operator=(const KcpSessionClient&) = delete;

    bool Connect(std::string_view host, std::uint16_t port,
                 KcpSessionClientOptions options = {}, std::string* error = nullptr);
    void Close();
    void Poll();

    bool Send(std::span<const std::uint8_t> data, std::string* error = nullptr);
    bool Receive(std::vector<std::uint8_t>* data);

    KcpSessionClientState State() const;
    bool IsConnected() const;
    SessionId Id() const;
    std::uint64_t ServerTick() const;
    const std::string& LastError() const;
    SessionDisconnectReason LastReason() const;
    KcpSessionClientStats GetStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

struct KcpSessionServerOptions {
    std::uint64_t contentHash = 0;
    std::size_t maxSessions = 8;
    std::uint32_t handshakeTimeoutMs = 5000;
    std::uint32_t sessionTimeoutMs = 15000;
    std::uint32_t heartbeatMs = 5000;
};

enum class KcpSessionServerEventType : std::uint8_t {
    Connected = 0,
    Data,
    Disconnected,
};

struct KcpSessionServerEvent {
    KcpSessionServerEventType type = KcpSessionServerEventType::Connected;
    SessionId sessionId = 0;
    std::vector<std::uint8_t> data;
    SessionDisconnectReason reason = SessionDisconnectReason::None;
    std::string message;
    // Connected：Hello 中携带的持久玩家身份与显示名。
    std::string playerIdentity;
    std::string playerName;
};

struct KcpSessionServerStats {
    std::uint64_t accepted = 0;
    std::uint64_t rejected = 0;
    std::uint64_t timedOut = 0;
    std::uint64_t messagesSent = 0;
    std::uint64_t messagesReceived = 0;
    std::uint64_t dataSent = 0;
    std::uint64_t dataReceived = 0;
    KcpStats transport;
};

class KcpSessionServer {
public:
    explicit KcpSessionServer(KcpSessionServerOptions options = {});
    ~KcpSessionServer();

    KcpSessionServer(const KcpSessionServer&) = delete;
    KcpSessionServer& operator=(const KcpSessionServer&) = delete;

    bool Listen(std::string_view bindHost, std::uint16_t port,
                std::string* error = nullptr);
    void Close();
    bool IsListening() const;
    std::uint16_t Port() const;

    void Poll();
    bool PollEvent(KcpSessionServerEvent* event);

    bool Send(SessionId id, std::span<const std::uint8_t> data,
              std::string* error = nullptr);
    // 服务器主动断开（重复身份/策略拒绝）；会排队 Disconnected 事件。
    bool Disconnect(SessionId id, SessionDisconnectReason reason,
                    std::string_view message);
    std::vector<SessionId> Sessions() const;
    KcpSessionServerStats GetStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace zl::net
