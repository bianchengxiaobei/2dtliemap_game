#include "net/KcpSession.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <limits>
#include <unordered_map>
#include <utility>

#include "net/FrameCodec.h"

namespace zl::net {
namespace {

std::uint64_t NowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::uint32_t AtLeastOne(std::uint32_t value) {
    return std::max<std::uint32_t>(value, 1);
}

std::uint32_t HeartbeatTimeout(std::uint32_t intervalMs, std::uint32_t timeoutMs) {
    const std::uint64_t desired =
        std::min<std::uint64_t>(static_cast<std::uint64_t>(intervalMs) * 2U,
                                std::numeric_limits<std::uint32_t>::max());
    return std::max<std::uint32_t>(timeoutMs, static_cast<std::uint32_t>(desired));
}

KcpSessionClientOptions Normalize(KcpSessionClientOptions options) {
    options.handshakeTimeoutMs = AtLeastOne(options.handshakeTimeoutMs);
    options.heartbeatIntervalMs = AtLeastOne(options.heartbeatIntervalMs);
    options.heartbeatTimeoutMs =
        HeartbeatTimeout(options.heartbeatIntervalMs, options.heartbeatTimeoutMs);
    return options;
}

KcpSessionServerOptions Normalize(KcpSessionServerOptions options) {
    options.maxSessions = std::max<std::size_t>(options.maxSessions, 1);
    options.handshakeTimeoutMs = AtLeastOne(options.handshakeTimeoutMs);
    options.sessionTimeoutMs = AtLeastOne(options.sessionTimeoutMs);
    options.heartbeatMs = AtLeastOne(options.heartbeatMs);
    return options;
}

std::uint32_t RawTimeoutMs(const KcpSessionServerOptions& options) {
    const std::uint64_t longest =
        std::max<std::uint64_t>({options.handshakeTimeoutMs, options.sessionTimeoutMs,
                                 options.heartbeatMs});
    const std::uint64_t total =
        std::min<std::uint64_t>(longest + 5000U,
                                std::numeric_limits<std::uint32_t>::max());
    return static_cast<std::uint32_t>(total);
}

std::string ReasonText(SessionDisconnectReason reason, std::string_view fallback) {
    if (!fallback.empty()) {
        return std::string(fallback);
    }
    return std::string(SessionDisconnectReasonName(reason));
}

bool SendFramed(KcpClient& transport, std::span<const std::uint8_t> sessionFrame,
                std::string* error) {
    std::string encodeError;
    const std::vector<std::uint8_t> rawFrame = EncodeFrame(sessionFrame, &encodeError);
    if (rawFrame.empty()) {
        if (error != nullptr) {
            *error = encodeError.empty() ? "会话帧封装失败" : encodeError;
        }
        return false;
    }
    return transport.Send(rawFrame, error);
}

bool SendFramed(KcpServer& transport, KcpConnectionId id,
                std::span<const std::uint8_t> sessionFrame, std::string* error) {
    std::string encodeError;
    const std::vector<std::uint8_t> rawFrame = EncodeFrame(sessionFrame, &encodeError);
    if (rawFrame.empty()) {
        if (error != nullptr) {
            *error = encodeError.empty() ? "会话帧封装失败" : encodeError;
        }
        return false;
    }
    return transport.Send(id, rawFrame, error);
}

}  // namespace

struct KcpSessionClient::Impl {
    KcpClient transport;
    KcpSessionClientOptions options;
    KcpSessionClientState state = KcpSessionClientState::Disconnected;
    SessionId sessionId = 0;
    std::uint64_t serverTick = 0;
    std::string lastError;
    SessionDisconnectReason lastReason = SessionDisconnectReason::None;
    std::deque<std::vector<std::uint8_t>> inbox;
    KcpSessionClientStats stats;
    std::uint64_t handshakeDeadlineMs = 0;
    std::uint64_t lastPingMs = 0;
    std::uint64_t lastPongMs = 0;
    std::uint64_t nextPingNonce = 1;

    void ResetSessionState() {
        state = KcpSessionClientState::Disconnected;
        sessionId = 0;
        serverTick = 0;
        handshakeDeadlineMs = 0;
        inbox.clear();
    }

    void Reject(SessionDisconnectReason reason, std::string message) {
        state = KcpSessionClientState::Rejected;
        lastReason = reason;
        lastError = ReasonText(reason, message);
        ++stats.rejects;
        transport.Close();
    }

    void Fail(SessionDisconnectReason reason, std::string message) {
        state = KcpSessionClientState::Disconnected;
        lastReason = reason;
        lastError = ReasonText(reason, message);
        transport.Close();
    }

    bool SendMessage(SessionMessageType type, std::span<const std::uint8_t> payload,
                     std::string* error) {
        std::string encodeError;
        const std::vector<std::uint8_t> frame =
            EncodeSessionMessage(type, payload, &encodeError);
        if (frame.empty()) {
            if (error != nullptr) {
                *error = encodeError.empty() ? "会话消息编码失败" : encodeError;
            }
            return false;
        }
        if (!SendFramed(transport, frame, error)) {
            return false;
        }
        ++stats.messagesSent;
        return true;
    }

    void HandleFrame(std::span<const std::uint8_t> frame) {
        SessionMessage message;
        std::string decodeError;
        SessionDecodeError code = SessionDecodeError::None;
        if (!DecodeSessionMessage(frame, &message, &decodeError, &code)) {
            if (state == KcpSessionClientState::Connecting &&
                code == SessionDecodeError::BadVersion) {
                Reject(SessionDisconnectReason::ProtocolVersion, decodeError);
            } else {
                Fail(SessionDisconnectReason::ProtocolError, decodeError);
            }
            return;
        }
        ++stats.messagesReceived;

        if (state == KcpSessionClientState::Connecting) {
            if (message.type == SessionMessageType::Welcome) {
                SessionWelcome welcome;
                if (!DecodeSessionWelcome(message.payload, &welcome, &decodeError)) {
                    Fail(SessionDisconnectReason::ProtocolError, decodeError);
                    return;
                }
                if (welcome.contentHash != options.contentHash) {
                    Fail(SessionDisconnectReason::ContentMismatch, "服务器内容清单不一致");
                    return;
                }
                state = KcpSessionClientState::Connected;
                sessionId = welcome.sessionId;
                serverTick = welcome.serverTick;
                handshakeDeadlineMs = 0;
                lastPingMs = NowMs();
                lastPongMs = lastPingMs;
                return;
            }
            if (message.type == SessionMessageType::Reject) {
                SessionReject reject;
                if (!DecodeSessionReject(message.payload, &reject, &decodeError)) {
                    Fail(SessionDisconnectReason::ProtocolError, decodeError);
                    return;
                }
                Reject(reject.reason, reject.message);
                return;
            }
            Fail(SessionDisconnectReason::ProtocolError, "握手阶段收到意外消息");
            return;
        }

        if (state != KcpSessionClientState::Connected) {
            return;
        }

        switch (message.type) {
            case SessionMessageType::Data:
                inbox.emplace_back(std::move(message.payload));
                ++stats.dataReceived;
                break;
            case SessionMessageType::Ping: {
                std::uint64_t nonce = 0;
                if (!DecodeSessionPing(message.payload, &nonce, &decodeError)) {
                    Fail(SessionDisconnectReason::ProtocolError, decodeError);
                    return;
                }
                std::string error;
                std::vector<std::uint8_t> pong = EncodeSessionPong(nonce, &error);
                if (pong.empty()) {
                    Fail(SessionDisconnectReason::ProtocolError, error);
                    return;
                }
                if (!SendFramed(transport, pong, &error)) {
                    Fail(SessionDisconnectReason::ProtocolError, error);
                    return;
                }
                ++stats.messagesSent;
                lastPongMs = NowMs();
                break;
            }
            case SessionMessageType::Pong: {
                std::uint64_t nonce = 0;
                if (!DecodeSessionPong(message.payload, &nonce, &decodeError)) {
                    Fail(SessionDisconnectReason::ProtocolError, decodeError);
                    return;
                }
                lastPongMs = NowMs();
                break;
            }
            case SessionMessageType::Disconnect: {
                SessionDisconnect disconnect;
                if (!DecodeSessionDisconnect(message.payload, &disconnect, &decodeError)) {
                    Fail(SessionDisconnectReason::ProtocolError, decodeError);
                    return;
                }
                state = KcpSessionClientState::Disconnected;
                lastReason = disconnect.reason;
                lastError = ReasonText(disconnect.reason, disconnect.message);
                ++stats.disconnects;
                transport.Close();
                break;
            }
            default:
                Fail(SessionDisconnectReason::ProtocolError, "已连接阶段收到意外消息");
                break;
        }
    }
};

KcpSessionClient::KcpSessionClient() : m_impl(std::make_unique<Impl>()) {}
KcpSessionClient::~KcpSessionClient() {
    if (m_impl != nullptr) {
        Close();
    }
}

bool KcpSessionClient::Connect(std::string_view host, std::uint16_t port,
                               KcpSessionClientOptions options, std::string* error) {
    if (m_impl == nullptr) {
        if (error != nullptr) {
            *error = "会话客户端未初始化";
        }
        return false;
    }
    Close();
    m_impl->options = Normalize(options);
    m_impl->lastError.clear();
    m_impl->lastReason = SessionDisconnectReason::None;
    m_impl->stats = {};
    m_impl->state = KcpSessionClientState::Connecting;
    m_impl->handshakeDeadlineMs = NowMs() + m_impl->options.handshakeTimeoutMs;

    if (!m_impl->transport.Connect(host, port, error)) {
        m_impl->state = KcpSessionClientState::Disconnected;
        return false;
    }

    SessionHello hello;
    hello.clientNonce = static_cast<std::uint32_t>(NowMs());
    hello.contentHash = m_impl->options.contentHash;
    hello.playerIdentity = m_impl->options.playerIdentity;
    hello.playerName = m_impl->options.playerName;
    std::string encodeError;
    const std::vector<std::uint8_t> frame = EncodeSessionHello(hello, &encodeError);
    if (frame.empty() || !SendFramed(m_impl->transport, frame, error)) {
        if (error != nullptr && error->empty()) {
            *error = encodeError.empty() ? "发送 Hello 失败" : encodeError;
        }
        m_impl->transport.Close();
        m_impl->state = KcpSessionClientState::Disconnected;
        return false;
    }
    ++m_impl->stats.messagesSent;
    return true;
}

void KcpSessionClient::Close() {
    if (m_impl == nullptr) {
        return;
    }
    if (m_impl->state == KcpSessionClientState::Connected) {
        SessionDisconnect disconnect;
        disconnect.reason = SessionDisconnectReason::ClientLeft;
        disconnect.message = "client closed";
        std::string error;
        const std::vector<std::uint8_t> frame =
            EncodeSessionDisconnect(disconnect, &error);
        if (!frame.empty()) {
            SendFramed(m_impl->transport, frame, &error);
            ++m_impl->stats.messagesSent;
        }
    }
    m_impl->transport.Close();
    m_impl->ResetSessionState();
}

void KcpSessionClient::Poll() {
    if (m_impl == nullptr) {
        return;
    }
    if (m_impl->state != KcpSessionClientState::Connecting &&
        m_impl->state != KcpSessionClientState::Connected) {
        return;
    }

    m_impl->transport.Poll();
    if (!m_impl->transport.IsConnected()) {
        m_impl->Fail(SessionDisconnectReason::SessionTimeout, "KCP 连接已断开");
        return;
    }

    std::vector<std::uint8_t> frame;
    while (m_impl->transport.Receive(&frame)) {
        m_impl->HandleFrame(frame);
        if (m_impl->state != KcpSessionClientState::Connecting &&
            m_impl->state != KcpSessionClientState::Connected) {
            return;
        }
    }

    const std::uint64_t now = NowMs();
    if (m_impl->state == KcpSessionClientState::Connecting) {
        if (now >= m_impl->handshakeDeadlineMs) {
            m_impl->Fail(SessionDisconnectReason::HandshakeTimeout, "握手超时");
        }
        return;
    }

    if (now - m_impl->lastPongMs > m_impl->options.heartbeatTimeoutMs) {
        m_impl->Fail(SessionDisconnectReason::SessionTimeout, "服务器心跳超时");
        return;
    }

    if (now - m_impl->lastPingMs >= m_impl->options.heartbeatIntervalMs) {
        std::string error;
        const std::vector<std::uint8_t> ping =
            EncodeSessionPing(++m_impl->nextPingNonce, &error);
        if (ping.empty() || !SendFramed(m_impl->transport, ping, &error)) {
            m_impl->Fail(SessionDisconnectReason::ProtocolError,
                         error.empty() ? "发送心跳失败" : error);
            return;
        }
        ++m_impl->stats.messagesSent;
        ++m_impl->stats.heartbeatsSent;
        m_impl->lastPingMs = now;
    }
}

bool KcpSessionClient::Send(std::span<const std::uint8_t> data, std::string* error) {
    if (m_impl == nullptr || m_impl->state != KcpSessionClientState::Connected) {
        if (error != nullptr) {
            *error = "会话未连接";
        }
        return false;
    }
    std::string encodeError;
    const std::vector<std::uint8_t> frame =
        EncodeSessionMessage(SessionMessageType::Data, data, &encodeError);
    if (frame.empty()) {
        if (error != nullptr) {
            *error = encodeError.empty() ? "数据帧编码失败" : encodeError;
        }
        return false;
    }
    if (!SendFramed(m_impl->transport, frame, error)) {
        return false;
    }
    ++m_impl->stats.messagesSent;
    ++m_impl->stats.dataSent;
    return true;
}

bool KcpSessionClient::Receive(std::vector<std::uint8_t>* data) {
    if (m_impl == nullptr || data == nullptr || m_impl->inbox.empty()) {
        return false;
    }
    *data = std::move(m_impl->inbox.front());
    m_impl->inbox.pop_front();
    return true;
}

KcpSessionClientState KcpSessionClient::State() const {
    return m_impl != nullptr ? m_impl->state : KcpSessionClientState::Disconnected;
}

bool KcpSessionClient::IsConnected() const {
    return State() == KcpSessionClientState::Connected;
}

SessionId KcpSessionClient::Id() const {
    return m_impl != nullptr ? m_impl->sessionId : 0;
}

std::uint64_t KcpSessionClient::ServerTick() const {
    return m_impl != nullptr ? m_impl->serverTick : 0;
}

const std::string& KcpSessionClient::LastError() const {
    static const std::string empty;
    return m_impl != nullptr ? m_impl->lastError : empty;
}

SessionDisconnectReason KcpSessionClient::LastReason() const {
    return m_impl != nullptr ? m_impl->lastReason : SessionDisconnectReason::None;
}

KcpSessionClientStats KcpSessionClient::GetStats() const {
    if (m_impl == nullptr) {
        return {};
    }
    KcpSessionClientStats stats = m_impl->stats;
    stats.transport = m_impl->transport.GetStats();
    return stats;
}

struct KcpSessionServer::Impl {
    struct Pending {
        bool connected = false;
        SessionId sessionId = 0;
        std::uint64_t createdMs = 0;
        std::uint64_t lastSeenMs = 0;
        SessionHello hello;
    };

    KcpSessionServerOptions options;
    KcpServer transport;
    std::unordered_map<KcpConnectionId, Pending> pending;
    std::unordered_map<SessionId, KcpConnectionId> sessions;
    std::deque<KcpSessionServerEvent> events;
    SessionId nextSessionId = 1;
    KcpSessionServerStats stats;

    explicit Impl(KcpSessionServerOptions input)
        : options(Normalize(input)),
          transport(options.maxSessions + 8, RawTimeoutMs(options)) {}

    bool SendMessage(KcpConnectionId rawId, SessionMessageType type,
                     std::span<const std::uint8_t> payload, std::string* error) {
        std::string encodeError;
        const std::vector<std::uint8_t> frame =
            EncodeSessionMessage(type, payload, &encodeError);
        if (frame.empty()) {
            if (error != nullptr) {
                *error = encodeError.empty() ? "会话消息编码失败" : encodeError;
            }
            return false;
        }
        if (!SendFramed(transport, rawId, frame, error)) {
            return false;
        }
        ++stats.messagesSent;
        return true;
    }

    void QueueDisconnected(SessionId id, SessionDisconnectReason reason,
                           std::string message) {
        KcpSessionServerEvent event;
        event.type = KcpSessionServerEventType::Disconnected;
        event.sessionId = id;
        event.reason = reason;
        event.message = std::move(message);
        events.push_back(std::move(event));
    }

    void RejectRaw(KcpConnectionId rawId, SessionDisconnectReason reason,
                   std::string message) {
        SessionReject reject;
        reject.reason = reason;
        reject.message = std::move(message);
        std::string error;
        const std::vector<std::uint8_t> frame = EncodeSessionReject(reject, &error);
        if (!frame.empty()) {
            SendFramed(transport, rawId, frame, nullptr);
            ++stats.messagesSent;
        }
        transport.Disconnect(rawId);
        pending.erase(rawId);
        ++stats.rejected;
    }

    void DisconnectSession(SessionId id, SessionDisconnectReason reason,
                           std::string message, bool notify) {
        const auto found = sessions.find(id);
        if (found == sessions.end()) {
            return;
        }
        const KcpConnectionId rawId = found->second;
        SessionDisconnect disconnect;
        disconnect.reason = reason;
        disconnect.message = message;
        std::string error;
        const std::vector<std::uint8_t> frame = EncodeSessionDisconnect(disconnect, &error);
        if (!frame.empty()) {
            SendFramed(transport, rawId, frame, nullptr);
            ++stats.messagesSent;
        }
        transport.Disconnect(rawId);
        sessions.erase(id);
        pending.erase(rawId);
        if (notify) {
            QueueDisconnected(id, reason, std::move(message));
        }
    }

    void HandleFrame(KcpConnectionId rawId, std::span<const std::uint8_t> frame) {
        const std::uint64_t now = NowMs();
        auto found = pending.find(rawId);
        if (found == pending.end()) {
            Pending p;
            p.createdMs = now;
            p.lastSeenMs = now;
            found = pending.emplace(rawId, p).first;
        }
        Pending& p = found->second;
        p.lastSeenMs = now;

        SessionMessage message;
        std::string decodeError;
        SessionDecodeError code = SessionDecodeError::None;
        if (!DecodeSessionMessage(frame, &message, &decodeError, &code)) {
            if (!p.connected) {
                RejectRaw(rawId, code == SessionDecodeError::BadVersion
                                      ? SessionDisconnectReason::ProtocolVersion
                                      : SessionDisconnectReason::BadRequest,
                          decodeError);
            } else {
                DisconnectSession(p.sessionId, SessionDisconnectReason::ProtocolError,
                                  decodeError, true);
            }
            return;
        }
        ++stats.messagesReceived;

        if (!p.connected) {
            if (message.type != SessionMessageType::Hello) {
                RejectRaw(rawId, SessionDisconnectReason::BadRequest,
                          "握手首条消息必须是 Hello");
                return;
            }
            SessionHello hello;
            if (!DecodeSessionHello(message.payload, &hello, &decodeError)) {
                RejectRaw(rawId, SessionDisconnectReason::BadRequest, decodeError);
                return;
            }
            if (hello.contentHash != options.contentHash) {
                RejectRaw(rawId, SessionDisconnectReason::ContentMismatch,
                          "内容清单 hash 不一致");
                return;
            }
            if (sessions.size() >= options.maxSessions) {
                RejectRaw(rawId, SessionDisconnectReason::ServerFull, "服务器已满");
                return;
            }

            const SessionId id = nextSessionId++;
            p.connected = true;
            p.sessionId = id;
            p.hello = hello;
            sessions.emplace(id, rawId);

            SessionWelcome welcome;
            welcome.sessionId = id;
            welcome.serverTick = 0;
            welcome.contentHash = options.contentHash;
            welcome.heartbeatMs = options.heartbeatMs;
            std::string sendError;
            if (!SendMessage(rawId, SessionMessageType::Welcome, welcomePayload(welcome),
                             &sendError)) {
                p.connected = false;
                sessions.erase(id);
                transport.Disconnect(rawId);
                pending.erase(rawId);
                ++stats.rejected;
                return;
            }
            ++stats.accepted;
            KcpSessionServerEvent event;
            event.type = KcpSessionServerEventType::Connected;
            event.sessionId = id;
            event.playerIdentity = p.hello.playerIdentity;
            event.playerName = p.hello.playerName;
            events.push_back(std::move(event));
            return;
        }

        switch (message.type) {
            case SessionMessageType::Data: {
                KcpSessionServerEvent event;
                event.type = KcpSessionServerEventType::Data;
                event.sessionId = p.sessionId;
                event.data = std::move(message.payload);
                events.push_back(std::move(event));
                ++stats.dataReceived;
                break;
            }
            case SessionMessageType::Ping: {
                std::uint64_t nonce = 0;
                if (!DecodeSessionPing(message.payload, &nonce, &decodeError)) {
                    DisconnectSession(p.sessionId, SessionDisconnectReason::ProtocolError,
                                      decodeError, true);
                    return;
                }
                const std::vector<std::uint8_t> pong = EncodeSessionPong(nonce, &decodeError);
                if (pong.empty() || !SendFramed(transport, rawId, pong, nullptr)) {
                    DisconnectSession(p.sessionId, SessionDisconnectReason::ProtocolError,
                                      "发送 Pong 失败", true);
                } else {
                    ++stats.messagesSent;
                }
                break;
            }
            case SessionMessageType::Pong: {
                std::uint64_t nonce = 0;
                if (!DecodeSessionPong(message.payload, &nonce, &decodeError)) {
                    DisconnectSession(p.sessionId, SessionDisconnectReason::ProtocolError,
                                      decodeError, true);
                }
                break;
            }
            case SessionMessageType::Disconnect: {
                SessionDisconnect disconnect;
                if (!DecodeSessionDisconnect(message.payload, &disconnect, &decodeError)) {
                    DisconnectSession(p.sessionId, SessionDisconnectReason::ProtocolError,
                                      decodeError, true);
                    return;
                }
                QueueDisconnected(p.sessionId, disconnect.reason, disconnect.message);
                transport.Disconnect(rawId);
                sessions.erase(p.sessionId);
                pending.erase(found);
                break;
            }
            default:
                DisconnectSession(p.sessionId, SessionDisconnectReason::ProtocolError,
                                  "已连接阶段收到意外消息", true);
                break;
        }
    }

    static std::vector<std::uint8_t> welcomePayload(const SessionWelcome& welcome) {
        std::string error;
        const std::vector<std::uint8_t> frame = EncodeSessionWelcome(welcome, &error);
        if (frame.size() < kSessionHeaderSize) {
            return {};
        }
        return std::vector<std::uint8_t>(frame.begin() + kSessionHeaderSize, frame.end());
    }

    void Poll() {
        if (!transport.IsListening()) {
            return;
        }
        transport.Poll();

        const std::uint64_t initialNow = NowMs();
        for (const KcpConnectionId id : transport.Connections()) {
            if (pending.find(id) == pending.end()) {
                Pending p;
                p.createdMs = initialNow;
                p.lastSeenMs = initialNow;
                pending.emplace(id, p);
            }
        }

        KcpConnectionId rawId = 0;
        std::vector<std::uint8_t> frame;
        while (transport.Receive(&rawId, &frame)) {
            HandleFrame(rawId, frame);
        }

        const std::uint64_t now = NowMs();
        std::vector<KcpConnectionId> handshakeExpired;
        std::vector<SessionId> sessionExpired;
        for (const auto& [id, p] : pending) {
            if (!p.connected && now - p.createdMs > options.handshakeTimeoutMs) {
                handshakeExpired.push_back(id);
            } else if (p.connected &&
                       now - p.lastSeenMs > options.sessionTimeoutMs) {
                sessionExpired.push_back(p.sessionId);
            }
        }
        for (const KcpConnectionId id : handshakeExpired) {
            RejectRaw(id, SessionDisconnectReason::HandshakeTimeout, "握手超时");
        }
        for (const SessionId id : sessionExpired) {
            ++stats.timedOut;
            DisconnectSession(id, SessionDisconnectReason::SessionTimeout,
                              "会话超时", true);
        }

        const std::vector<KcpConnectionId> rawConnections = transport.Connections();
        std::vector<KcpConnectionId> closed;
        for (const auto& [id, p] : pending) {
            if (std::find(rawConnections.begin(), rawConnections.end(), id) ==
                rawConnections.end()) {
                closed.push_back(id);
            }
        }
        for (const KcpConnectionId id : closed) {
            const auto found = pending.find(id);
            if (found == pending.end()) {
                continue;
            }
            if (found->second.connected) {
                sessions.erase(found->second.sessionId);
                QueueDisconnected(found->second.sessionId,
                                  SessionDisconnectReason::SessionTimeout,
                                  "传输连接已关闭");
            }
            pending.erase(id);
        }
    }

    void Close() {
        for (const auto& [id, rawId] : sessions) {
            static_cast<void>(rawId);
            SessionDisconnect disconnect;
            disconnect.reason = SessionDisconnectReason::ServerShutdown;
            disconnect.message = "server closed";
            std::string error;
            const std::vector<std::uint8_t> frame =
                EncodeSessionDisconnect(disconnect, &error);
            if (!frame.empty()) {
                SendFramed(transport, rawId, frame, nullptr);
            }
            static_cast<void>(id);
        }
        transport.Close();
        pending.clear();
        sessions.clear();
        events.clear();
    }
};

KcpSessionServer::KcpSessionServer(KcpSessionServerOptions options)
    : m_impl(std::make_unique<Impl>(options)) {}

KcpSessionServer::~KcpSessionServer() {
    if (m_impl != nullptr) {
        Close();
    }
}

bool KcpSessionServer::Listen(std::string_view bindHost, std::uint16_t port,
                              std::string* error) {
    if (m_impl == nullptr) {
        if (error != nullptr) {
            *error = "会话服务器未初始化";
        }
        return false;
    }
    m_impl->Close();
    return m_impl->transport.Listen(bindHost, port, error);
}

void KcpSessionServer::Close() {
    if (m_impl != nullptr) {
        m_impl->Close();
    }
}

bool KcpSessionServer::IsListening() const {
    return m_impl != nullptr && m_impl->transport.IsListening();
}

std::uint16_t KcpSessionServer::Port() const {
    return m_impl != nullptr ? m_impl->transport.Port() : 0;
}

void KcpSessionServer::Poll() {
    if (m_impl != nullptr) {
        m_impl->Poll();
    }
}

bool KcpSessionServer::PollEvent(KcpSessionServerEvent* event) {
    if (m_impl == nullptr || event == nullptr || m_impl->events.empty()) {
        return false;
    }
    *event = std::move(m_impl->events.front());
    m_impl->events.pop_front();
    return true;
}

bool KcpSessionServer::Send(SessionId id, std::span<const std::uint8_t> data,
                            std::string* error) {
    if (m_impl == nullptr || !m_impl->transport.IsListening()) {
        if (error != nullptr) {
            *error = "会话服务器未监听";
        }
        return false;
    }
    const auto found = m_impl->sessions.find(id);
    if (found == m_impl->sessions.end()) {
        if (error != nullptr) {
            *error = "会话不存在";
        }
        return false;
    }
    std::string encodeError;
    const std::vector<std::uint8_t> frame =
        EncodeSessionMessage(SessionMessageType::Data, data, &encodeError);
    if (frame.empty() || !SendFramed(m_impl->transport, found->second, frame, error)) {
        if (error != nullptr && error->empty()) {
            *error = encodeError.empty() ? "数据帧编码失败" : encodeError;
        }
        return false;
    }
    ++m_impl->stats.messagesSent;
    ++m_impl->stats.dataSent;
    return true;
}

bool KcpSessionServer::Disconnect(SessionId id, SessionDisconnectReason reason,
                                  std::string_view message) {
    if (m_impl == nullptr || id == 0) {
        return false;
    }
    if (m_impl->sessions.find(id) == m_impl->sessions.end()) {
        return false;
    }
    m_impl->DisconnectSession(id, reason, std::string(message), true);
    return true;
}

std::vector<SessionId> KcpSessionServer::Sessions() const {
    std::vector<SessionId> result;
    if (m_impl == nullptr) {
        return result;
    }
    result.reserve(m_impl->sessions.size());
    for (const auto& [id, rawId] : m_impl->sessions) {
        static_cast<void>(rawId);
        result.push_back(id);
    }
    std::sort(result.begin(), result.end());
    return result;
}

KcpSessionServerStats KcpSessionServer::GetStats() const {
    if (m_impl == nullptr) {
        return {};
    }
    KcpSessionServerStats stats = m_impl->stats;
    stats.transport = m_impl->transport.GetStats();
    return stats;
}

}  // namespace zl::net
