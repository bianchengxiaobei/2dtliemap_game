#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "game/EntityReplication.h"
#include "net/Command.h"
#include "net/KcpSession.h"
#include "net/PlayerState.h"
#include "net/SocialProtocol.h"

namespace zl::game {

// M13-T5：游戏客户端会话门面。
//
// 只负责：KCP 会话握手/心跳、服务器分配本地玩家、命令编码发送、ZLR1 快照应用、
// ZLP1 owner-only 背包状态、断线重连。它不持有世界/ECS；渲染侧从
// EntityReplicationClient 的副本生成视觉实体。
class NetworkClient {
public:
    struct Options {
        std::string host = "127.0.0.1";
        std::uint16_t port = 7777;
        std::uint64_t contentHash = 0;
        // M13-T8：跨重连/重启稳定的玩家身份与显示名；identity 为空时自动生成。
        std::string playerIdentity;
        std::string playerName = "Player";
        bool autoReconnect = true;
        std::uint32_t reconnectDelayMs = 1000;
        std::uint32_t handshakeTimeoutMs = 5000;
        std::uint32_t heartbeatIntervalMs = 5000;
        std::uint32_t heartbeatTimeoutMs = 15000;
        float localPredictionSpeed = 4.0f;
        EntityReplicationClient::Options replication;
    };

    struct Stats {
        std::uint64_t connectAttempts = 0;
        std::uint64_t assignments = 0;
        std::uint64_t snapshotsReceived = 0;
        std::uint64_t playerStatesReceived = 0;
        std::uint64_t socialStatesReceived = 0;
        std::uint64_t chatMessagesSent = 0;
        std::uint64_t commandPacketsSent = 0;
        std::uint64_t commandPacketsRejected = 0;
        std::string lastError;
        net::KcpSessionClientStats session;
    };

    explicit NetworkClient(Options options = {});
    ~NetworkClient();

    NetworkClient(const NetworkClient&) = delete;
    NetworkClient& operator=(const NetworkClient&) = delete;

    bool Connect(std::string* error = nullptr);
    void Close();
    void Poll();

    bool SendCommand(const net::Command& command, std::string* error = nullptr);
    bool SendChat(const std::string& text, std::string* error = nullptr);
    bool AdvanceLocalPrediction(float dt, float gridDirX, float gridDirY);

    bool IsConnected() const;
    bool HasPlayer() const;
    net::KcpSessionClientState State() const;
    net::SessionId SessionId() const;
    const std::string& PlayerIdentity() const { return m_options.playerIdentity; }
    std::uint64_t PlayerStableId() const;
    std::uint64_t ServerTick() const;
    net::SessionDisconnectReason LastReason() const;
    const std::string& LastError() const;

    bool HasPlayerState() const { return m_hasPlayerState; }
    const net::PlayerState& PlayerState() const { return m_playerState; }

    bool HasSocialState() const { return m_hasSocialState; }
    const net::SocialState& SocialState() const { return m_socialState; }

    EntityReplicationClient& Replication() { return m_replication; }
    const EntityReplicationClient& Replication() const { return m_replication; }
    Stats GetStats() const;

private:
    void HandleMessage(std::span<const std::uint8_t> message);
    void ClearReplication();
    void ScheduleReconnect();

    Options m_options;
    net::ReplicationRegistry m_registry;
    net::KcpSessionClient m_session;
    EntityReplicationClient m_replication;
    net::PlayerState m_playerState;
    bool m_hasPlayerState = false;
    net::SocialState m_socialState;
    bool m_hasSocialState = false;
    std::uint64_t m_playerStableId = 0;
    std::uint32_t m_commandSequence = 1;
    std::uint64_t m_nextReconnectMs = 0;
    bool m_userClosed = false;
    bool m_wasDisconnected = true;
    Stats m_stats;
};

}  // namespace zl::game
