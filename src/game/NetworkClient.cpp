#include "game/NetworkClient.h"

#include <chrono>
#include <utility>

#include "core/Log.h"
#include "net/CommandCodec.h"
#include "net/PlayerAssignment.h"
#include "net/PlayerIdentity.h"
#include "net/Replication.h"

namespace zl::game {
namespace {

std::uint64_t NowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::uint32_t ReadU32Le(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

}  // namespace

NetworkClient::NetworkClient(Options options)
    : m_options(std::move(options)), m_replication(m_options.replication) {
    std::string registryError;
    if (!RegisterEntityReplicationComponents(m_registry, &registryError)) {
        ZL_ERROR("net", "客户端复制组件注册失败：{}", registryError);
    }
    if (m_options.host.empty()) {
        m_options.host = "127.0.0.1";
    }
    if (m_options.port == 0) {
        m_options.port = 7777;
    }
    if (m_options.reconnectDelayMs == 0) {
        m_options.reconnectDelayMs = 1000;
    }
    if (m_options.handshakeTimeoutMs == 0) {
        m_options.handshakeTimeoutMs = 5000;
    }
    if (m_options.heartbeatIntervalMs == 0) {
        m_options.heartbeatIntervalMs = 5000;
    }
    if (m_options.heartbeatTimeoutMs == 0) {
        m_options.heartbeatTimeoutMs = 15000;
    }
    if (!(m_options.localPredictionSpeed > 0.0f)) {
        m_options.localPredictionSpeed = 4.0f;
    }
    if (!net::IsValidPlayerIdentity(m_options.playerIdentity)) {
        m_options.playerIdentity = net::GeneratePlayerIdentity();
    }
    if (m_options.playerName.size() > net::kMaxSessionName) {
        m_options.playerName.resize(net::kMaxSessionName);
    }
}

NetworkClient::~NetworkClient() = default;

void NetworkClient::ClearReplication() {
    m_replication.Reset();
    m_playerState = {};
    m_hasPlayerState = false;
    m_socialState = {};
    m_hasSocialState = false;
    m_playerStableId = 0;
}

void NetworkClient::ScheduleReconnect() {
    if (!m_options.autoReconnect || m_userClosed) {
        return;
    }
    m_nextReconnectMs = NowMs() + m_options.reconnectDelayMs;
}

bool NetworkClient::Connect(std::string* error) {
    m_userClosed = false;
    m_nextReconnectMs = 0;
    ClearReplication();
    m_commandSequence = 1;
    ++m_stats.connectAttempts;

    net::KcpSessionClientOptions sessionOptions;
    sessionOptions.contentHash = m_options.contentHash;
    sessionOptions.handshakeTimeoutMs = m_options.handshakeTimeoutMs;
    sessionOptions.heartbeatIntervalMs = m_options.heartbeatIntervalMs;
    sessionOptions.heartbeatTimeoutMs = m_options.heartbeatTimeoutMs;
    sessionOptions.playerIdentity = m_options.playerIdentity;
    sessionOptions.playerName = m_options.playerName;
    std::string localError;
    if (!m_session.Connect(m_options.host, m_options.port, sessionOptions, &localError)) {
        m_stats.lastError = localError;
        if (error != nullptr) {
            *error = localError;
        }
        m_wasDisconnected = true;
        ScheduleReconnect();
        return false;
    }
    m_wasDisconnected = false;
    ZL_INFO("net", "客户端连接中：{}:{}（内容 hash {:#x}，身份 {}）", m_options.host,
            m_options.port, m_options.contentHash, m_options.playerIdentity);
    return true;
}

void NetworkClient::Close() {
    m_userClosed = true;
    m_nextReconnectMs = 0;
    m_session.Close();
    ClearReplication();
    m_wasDisconnected = true;
}

void NetworkClient::Poll() {
    const net::KcpSessionClientState before = m_session.State();
    if (before == net::KcpSessionClientState::Connecting ||
        before == net::KcpSessionClientState::Connected) {
        m_session.Poll();
    }

    std::vector<std::uint8_t> message;
    while (m_session.Receive(&message)) {
        HandleMessage(message);
        if (m_session.State() != net::KcpSessionClientState::Connected) {
            break;
        }
    }

    const net::KcpSessionClientState after = m_session.State();
    if (after == net::KcpSessionClientState::Connected) {
        m_wasDisconnected = false;
        m_nextReconnectMs = 0;
        m_stats.lastError = m_session.LastError();
        return;
    }

    if (after != net::KcpSessionClientState::Disconnected) {
        m_stats.lastError = m_session.LastError();
        return;
    }

    if (!m_wasDisconnected) {
        ClearReplication();
        m_stats.lastError = m_session.LastError();
        ZL_WARN("net", "客户端会话断开：{}（{}）", net::SessionDisconnectReasonName(
                                                    m_session.LastReason()),
                m_session.LastError());
        ScheduleReconnect();
    }
    m_wasDisconnected = true;
    if (!m_userClosed && m_options.autoReconnect && m_nextReconnectMs != 0 &&
        NowMs() >= m_nextReconnectMs) {
        std::string error;
        if (!Connect(&error) && !error.empty()) {
            ZL_WARN("net", "客户端重连失败：{}", error);
        }
    }
}

void NetworkClient::HandleMessage(std::span<const std::uint8_t> message) {
    if (message.size() >= 4 &&
        ReadU32Le(message.data()) == net::kPlayerAssignmentMagic) {
        net::PlayerAssignment assignment;
        std::string error;
        if (!net::DecodePlayerAssignment(message, &assignment, &error)) {
            m_stats.lastError = error;
            ZL_WARN("net", "玩家分配消息无效：{}", error);
            m_session.Close();
            return;
        }
        if (assignment.sessionId != m_session.Id()) {
            m_stats.lastError = "玩家分配 sessionId 不匹配";
            ZL_WARN("net", "玩家分配 sessionId 不匹配（分配 {}，本地 {}）",
                    assignment.sessionId, m_session.Id());
            m_session.Close();
            return;
        }
        m_playerStableId = assignment.playerStableId;
        m_playerState = {};
        m_hasPlayerState = false;
        m_socialState = {};
        m_hasSocialState = false;
        ++m_stats.assignments;
        if (!m_replication.SetLocalPlayer(m_playerStableId,
                                          m_options.localPredictionSpeed)) {
            m_stats.lastError = "本地预测速度非法";
            m_session.Close();
            return;
        }
        ZL_INFO("net", "服务器分配玩家：stableId={}，session={}，tick={}",
                m_playerStableId, assignment.sessionId, assignment.serverTick);
        return;
    }

    if (message.size() >= 4 && ReadU32Le(message.data()) == net::kSocialStateMagic) {
        net::SocialState state;
        std::string error;
        if (!net::DecodeSocialState(message, &state, &error)) {
            m_stats.lastError = error;
            ZL_WARN("net", "社交状态消息无效：{}", error);
            m_session.Close();
            return;
        }
        if (m_hasSocialState && state.sequence < m_socialState.sequence) {
            return;
        }
        m_socialState = std::move(state);
        m_hasSocialState = true;
        ++m_stats.socialStatesReceived;
        return;
    }

    if (message.size() >= 4 && ReadU32Le(message.data()) == net::kPlayerStateMagic) {
        net::PlayerState state;
        std::string error;
        if (!net::DecodePlayerState(message, &state, &error)) {
            m_stats.lastError = error;
            ZL_WARN("net", "玩家状态消息无效：{}", error);
            m_session.Close();
            return;
        }
        if (m_playerStableId == 0 || state.playerStableId != m_playerStableId) {
            m_stats.lastError = "玩家状态 playerStableId 不匹配";
            ZL_WARN("net", "玩家状态 playerStableId 不匹配（状态 {}，本地 {}）",
                    state.playerStableId, m_playerStableId);
            m_session.Close();
            return;
        }
        m_playerState = std::move(state);
        m_hasPlayerState = true;
        ++m_stats.playerStatesReceived;
        return;
    }

    if (message.size() >= 4 && ReadU32Le(message.data()) == net::kReplicationMagic) {
        net::ReplicationSnapshot snapshot;
        std::string error;
        if (!net::DecodeReplicationSnapshot(message, m_registry, &snapshot, &error)) {
            m_stats.lastError = error;
            ZL_WARN("net", "复制快照解码失败：{}", error);
            m_session.Close();
            return;
        }
        if (!m_replication.Apply(snapshot, &error)) {
            m_stats.lastError = error;
            ZL_WARN("net", "复制快照应用失败：{}", error);
            m_session.Close();
            return;
        }
        ++m_stats.snapshotsReceived;
        return;
    }

    m_stats.lastError = "收到未知数据消息";
    ZL_WARN("net", "收到未知数据消息（{} 字节），已忽略", message.size());
}

bool NetworkClient::SendCommand(const net::Command& command, std::string* error) {
    auto fail = [&](const std::string& message) {
        ++m_stats.commandPacketsRejected;
        m_stats.lastError = message;
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (!IsConnected()) {
        return fail("会话未连接");
    }
    if (m_playerStableId == 0) {
        return fail("服务器尚未分配玩家");
    }
    if (command.playerStableId != 0 && command.playerStableId != m_playerStableId) {
        return fail("命令 playerStableId 与本地玩家不匹配");
    }

    net::Command packetCommand = command;
    packetCommand.playerStableId = m_playerStableId;
    net::CommandPacket packet;
    packet.sequence = m_commandSequence;
    packet.commands.push_back(std::move(packetCommand));
    std::string encodeError;
    const std::vector<std::uint8_t> bytes =
        net::EncodeCommandPacket(packet, &encodeError);
    if (bytes.empty()) {
        return fail(encodeError.empty() ? "命令包编码失败" : encodeError);
    }
    std::string sendError;
    if (!m_session.Send(bytes, &sendError)) {
        return fail(sendError.empty() ? "命令包发送失败" : sendError);
    }
    ++m_commandSequence;
    ++m_stats.commandPacketsSent;
    return true;
}

bool NetworkClient::SendChat(const std::string& text, std::string* error) {
    auto fail = [&](const std::string& message) {
        m_stats.lastError = message;
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (!IsConnected()) {
        return fail("会话未连接");
    }
    if (!net::IsValidChatText(text)) {
        return fail("聊天文本为空、过长或包含非法字符");
    }
    std::string encodeError;
    const std::vector<std::uint8_t> bytes = net::EncodeChatMessage(
        net::ChatMessage{text}, &encodeError);
    if (bytes.empty()) {
        return fail(encodeError.empty() ? "聊天消息编码失败" : encodeError);
    }
    std::string sendError;
    if (!m_session.Send(bytes, &sendError)) {
        return fail(sendError.empty() ? "聊天消息发送失败" : sendError);
    }
    ++m_stats.chatMessagesSent;
    return true;
}

bool NetworkClient::AdvanceLocalPrediction(float dt, float gridDirX, float gridDirY) {
    return m_replication.AdvanceLocalPrediction(dt, gridDirX, gridDirY);
}

bool NetworkClient::IsConnected() const {
    return m_session.IsConnected();
}

bool NetworkClient::HasPlayer() const {
    return m_playerStableId != 0;
}

net::KcpSessionClientState NetworkClient::State() const {
    return m_session.State();
}

net::SessionId NetworkClient::SessionId() const {
    return m_session.Id();
}

std::uint64_t NetworkClient::PlayerStableId() const {
    return m_playerStableId;
}

std::uint64_t NetworkClient::ServerTick() const {
    const std::uint64_t applied = m_replication.LastAppliedTick();
    return applied != 0 ? applied : m_session.ServerTick();
}

net::SessionDisconnectReason NetworkClient::LastReason() const {
    return m_session.LastReason();
}

const std::string& NetworkClient::LastError() const {
    return m_stats.lastError;
}

NetworkClient::Stats NetworkClient::GetStats() const {
    Stats stats = m_stats;
    stats.session = m_session.GetStats();
    return stats;
}

}  // namespace zl::game
