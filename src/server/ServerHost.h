#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "net/SocialProtocol.h"

namespace zl::core {
class Config;
class EventBus;
class GameTime;
class TaskScheduler;
}  // namespace zl::core

namespace zl::game {
class EntityReplicationProducer;
class PlayerInventoryService;
class WorldSimulation;
}  // namespace zl::game

namespace zl::items {
class ItemRegistry;
class RecipeRegistry;
}  // namespace zl::items

namespace zl::net {
struct Command;
struct PlayerState;
class KcpSessionServer;
class ReplicationRegistry;
}  // namespace zl::net

namespace zl::save {
class ComponentRegistry;
class SaveManager;
class SaveService;
}  // namespace zl::save

namespace zl::server {

struct PlayerProfile;
class PlayerProfileStore;

// M13-T3/T5：无窗口权威服务器主循环。
//
// 服务器只组装内容（地图/物品/配方/存档）与 WorldSimulation，不创建窗口、渲染、
// UI、输入或音频。固定 tick 内顺序：网络轮询 → chunk 流式加载 → 会话玩家 →
// 命令执行 → 事件/模拟 → flush → 各会话快照。
class ServerHost {
public:
    struct Options {
        std::string configPath = "config/game.json";
        int argc = 0;
        const char* const* argv = nullptr;
        std::string loadSaveId;
        bool spawnDemoPopulation = true;
        bool spawnLocalPlayer = true;
        float playerTargetX = 70.0f;
        float playerTargetY = 24.0f;
        int maxTicks = 0;  // 0 = 直到 RequestStop
        float tickRate = 0.0f;

        // M13-T8：持久玩家档案。独立服务器入口默认开启；测试/工具可显式关闭。
        bool enablePlayerProfiles = false;
        std::string playerProfilesFile;
        float respawnDelaySeconds = 0.0f;
        float profileSaveIntervalSeconds = 0.0f;

        // M13-T5：可选 UDP 会话服务器。port=0 表示让系统分配临时端口（测试用）。
        bool network = false;
        std::string bindHost = "127.0.0.1";
        bool bindHostOverride = false;
        bool portOverride = false;
        std::uint16_t port = 7777;
        std::size_t maxSessions = 8;
        std::uint64_t contentHash = 0;
    };

    // M13-T6：服务器侧防御性网络统计。命令计数在服务器 tick 内累计，
    // 便于区分正常流量、限频、越权、距离与冷却拒绝。
    struct NetworkStats {
        std::uint64_t dataPacketsReceived = 0;
        std::uint64_t malformedPackets = 0;
        std::uint64_t oversizedPackets = 0;
        std::uint64_t stalePackets = 0;
        std::uint64_t commandsReceived = 0;
        std::uint64_t commandsAccepted = 0;
        std::uint64_t commandsRejected = 0;
        std::uint64_t rateLimitedCommands = 0;
        std::uint64_t ownershipRejected = 0;
        std::uint64_t rangeRejected = 0;
        std::uint64_t cooldownRejected = 0;
        std::uint64_t unsupportedCommands = 0;
        std::uint64_t assignmentsSent = 0;
        std::uint64_t snapshotsSent = 0;
        std::uint64_t snapshotFailures = 0;
        std::uint64_t playerStatesSent = 0;
        std::uint64_t playerStateFailures = 0;
        std::uint64_t socialStatesSent = 0;
        std::uint64_t chatMessagesReceived = 0;
        std::uint64_t chatMessagesRejected = 0;
        std::uint64_t gunshots = 0;
        std::uint64_t projectilesFired = 0;
        std::uint64_t projectilesHit = 0;
        std::uint64_t effectsSpawned = 0;
        std::uint64_t profileRestores = 0;
        std::uint64_t profileSaves = 0;
        std::uint64_t playerDeaths = 0;
        std::uint64_t playerRespawns = 0;
    };

    ServerHost();
    ~ServerHost();

    ServerHost(const ServerHost&) = delete;
    ServerHost& operator=(const ServerHost&) = delete;

    // 独立进程入口：解析 --server（由 main 识别）与 --config，Ctrl+C 停止。
    static int Run(int argc, char** argv);

    bool Init(const Options& options, std::string* error = nullptr);
    void Shutdown();

    // 推进一个固定 tick（不 sleep）；供主循环与无网络测试复用。
    void Tick(float dt);
    // 连续推进 count 个 tick（不 sleep）；测试/快速 smoke 用。
    void RunForTicks(int count, float dt = 1.0f / 60.0f);

    void RequestStop();
    bool Running() const;
    uint64_t TickCount() const;

    game::WorldSimulation& Simulation();
    const game::WorldSimulation& Simulation() const;
    const core::Config& Config() const;

    bool NetworkEnabled() const { return m_networkEnabled; }
    std::uint16_t NetworkPort() const;
    std::size_t SessionCount() const { return m_sessions.size(); }
    NetworkStats GetNetworkStats() const { return m_networkStats; }

private:
    struct SessionPlayer {
        std::string identity;
        std::string playerName;
        std::uint64_t playerStableId = 0;
        bool spawned = false;
        bool dead = false;
        std::uint64_t respawnAtTick = 0;
        std::uint32_t lastCommandSequence = 0;
        std::uint32_t commandsThisTick = 0;
        std::uint64_t lastAttackTick = 0;
        std::uint64_t lastChatTick = 0;
        std::uint64_t lastProjectileTick = 0;
        bool chatUsed = false;
        bool projectileUsed = false;
        bool attackUsed = false;
        bool assignmentSent = false;
        bool needsFullSnapshot = true;
        bool stateDirty = true;
        std::uint64_t lastStateTick = 0;
        std::uint32_t statusSequence = 0;
        std::uint8_t statusTone = 0;
        std::string status;
        std::unique_ptr<game::EntityReplicationProducer> producer;
    };

    bool LoadContent(std::string* error);
    bool InitNetwork(std::string* error);
    bool InitPlayerProfiles(std::string* error);
    void PollNetwork();
    bool EnsureSessionPlayer(std::uint64_t sessionId, SessionPlayer& session);
    PlayerProfile* FindSessionProfile(SessionPlayer& session);
    bool SpawnOrRestoreSessionPlayer(std::uint64_t sessionId, SessionPlayer& session);
    bool RespawnSessionPlayer(SessionPlayer& session);
    void CaptureSessionProfile(SessionPlayer& session, bool markDead);
    void UpdatePlayerLifecycle();
    void SavePlayerProfiles(const char* reason);
    void FillPlayerState(const SessionPlayer& session, net::PlayerState* state);
    void BroadcastSnapshots();
    void BroadcastPlayerStates();
    void BroadcastSocialStates();
    void FillSocialState(net::SocialState* state) const;
    void HandleChatMessage(std::uint64_t sessionId, SessionPlayer& session,
                           const std::string& text);
    void TickCrafting(float dt);
    void UpdateProjectiles(float dt);
    void UpdateTransientEffects(float dt);
    bool FireProjectile(SessionPlayer& session, const net::Command& command);
    void SpawnImpactEffect(float x, float y, int z);
    void SpawnCraftEffect(std::uint64_t playerStableId);
    void SpawnDemoLoot();
    void EmitGunshot(std::uint64_t playerStableId);
    void SetSessionStatus(SessionPlayer& session, std::string message,
                          std::uint8_t tone);
    bool ExecuteSessionCommand(std::uint64_t sessionId, SessionPlayer& session,
                               const net::Command& command, std::string* error);
    void RemoveSession(std::uint64_t sessionId, std::uint8_t reason,
                       const std::string& message);
    void LogStatus();

    Options m_options;
    std::unique_ptr<core::Config> m_config;
    std::unique_ptr<core::EventBus> m_events;
    std::unique_ptr<core::GameTime> m_gameTime;
    std::unique_ptr<core::TaskScheduler> m_tasks;
    std::unique_ptr<items::ItemRegistry> m_items;
    std::unique_ptr<items::RecipeRegistry> m_recipes;
    std::unique_ptr<save::ComponentRegistry> m_saveComponents;
    std::unique_ptr<save::SaveManager> m_saveManager;
    std::unique_ptr<save::SaveService> m_saveService;
    std::unique_ptr<game::WorldSimulation> m_simulation;
    std::unique_ptr<game::PlayerInventoryService> m_inventory;
    std::unique_ptr<PlayerProfileStore> m_playerProfiles;

    // M13-T5：服务器会话与每会话复制基线。
    std::unique_ptr<net::KcpSessionServer> m_network;
    std::unique_ptr<net::ReplicationRegistry> m_replicationRegistry;
    std::unordered_map<std::uint64_t, SessionPlayer> m_sessions;
    bool m_networkEnabled = false;

    std::string m_activeSaveId;
    bool m_activeSaveUseBackup = false;
    bool m_demoLootSpawned = false;
    std::string m_saveWarning;
    std::atomic<bool> m_running{false};
    bool m_initialized = false;
    uint64_t m_ticks = 0;
    double m_logTimer = 0.0;
    float m_attackDamage = 40.0f;
    float m_attackRange = 1.7f;
    float m_attackKnockback = 3.0f;
    float m_attackStagger = 0.35f;
    float m_gunshotRadius = 30.0f;
    float m_projectileDamage = 24.0f;
    float m_projectileSpeed = 18.0f;
    float m_projectileLifetime = 2.5f;
    float m_maxProjectileDistance = 24.0f;
    std::uint32_t m_stateSendIntervalTicks = 6;
    std::uint32_t m_socialStateIntervalTicks = 30;
    std::uint64_t m_chatCooldownTicks = 60;
    std::uint32_t m_socialSequence = 0;
    std::uint64_t m_lastSocialTick = 0;
    bool m_socialDirty = true;
    std::deque<net::SocialChatLine> m_chatLog;
    std::uint64_t m_respawnDelayTicks = 300;
    double m_profileSaveIntervalSeconds = 30.0;
    double m_profileSaveTimer = 0.0;
    bool m_profilesEnabled = false;

    // M13-T6：命令防御策略（按服务器 tick 配额，不依赖客户端 tick）。
    std::uint32_t m_maxCommandsPerTick = 8;
    float m_maxMoveToDistance = 64.0f;
    float m_maxAttackDistance = 3.0f;
    std::uint64_t m_attackCooldownTicks = 21;
    std::uint64_t m_projectileCooldownTicks = 30;
    NetworkStats m_networkStats;
};

}  // namespace zl::server
