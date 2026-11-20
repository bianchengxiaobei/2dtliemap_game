#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "characters/systems/StealthRules.h"
#include "net/SocialProtocol.h"
#include "ecs/EcsWorld.h"
#include "game/BaseSiege.h"
#include "game/BuildingRules.h"
#include "game/Companion.h"
#include "game/Fire.h"
#include "game/Forage.h"
#include "game/ForageComponents.h"
#include "game/Homestead.h"
#include "game/Missions.h"
#include "game/Poi.h"
#include "game/PoiComponents.h"
#include "game/ProgressionRules.h"
#include "game/Settlement.h"
#include "game/Sleeping.h"
#include "game/StrategicState.h"
#include "game/Throwing.h"
#include "game/Traps.h"
#include "game/VehicleMods.h"
#include "game/VehicleSpike.h"
#include "game/WorldEvent.h"
#include "items/LootTable.h"
#include "items/RangedWeapon.h"
#include "net/PlayerState.h"
#include "net/WorldSound.h"

namespace zl::core {
class Config;
class EventBus;
class GameTime;
class TaskScheduler;
}  // namespace zl::core

namespace zl::game {
class AnimalRegistry;
class CropRegistry;
class EntityReplicationProducer;
class ForageRegistry;
class PlayerInventoryService;
class WorldSimulation;
}  // namespace zl::game

namespace zl::items {
class ItemRegistry;
class RecipeRegistry;
}  // namespace zl::items

namespace zl::net {
struct Command;
struct StrategicRequest;
struct StrategicSnapshot;
struct WorldEventSnapshot;
class KcpSessionServer;
class ReplicationRegistry;
}  // namespace zl::net

namespace zl::save {
class ComponentRegistry;
class SaveManager;
class SaveService;
}  // namespace zl::save

namespace zl::world {
class Chunk;
}  // namespace zl::world

namespace zl::ecs {
struct ItemInstance;
}  // namespace zl::ecs

namespace zl::server {

struct PlayerProfile;
class PlayerProfileStore;

// M13-T3/T5：无窗口权威服务器主循环。
//
// 服务器只组装内容（地图/物品/配方/存档）与 WorldSimulation，不创建窗口、渲染、
// UI、输入或音频。固定 tick 内顺序：网络轮询 → chunk 流式加载 → 会话玩家 →
// 命令执行 → 事件/模拟 → flush → 各会话快照。
class ServerHost {
    friend struct ServerHostTestAccess;
public:
    struct Options {
        std::string configPath = "config/game.json";
        int argc = 0;
        const char* const* argv = nullptr;
        // Embedded/launcher overrides. Empty keeps the automatic selection.
        std::string mapDir;
        std::string contentFile;
        std::string saveRoot;
        std::string loadSaveId;
        bool spawnDemoPopulation = true;
        bool spawnLocalPlayer = true;
        // M23-F2b-T6：正式服务器入口开启动物权威生成；轻量单元测试可保持关闭。
        bool spawnAnimals = false;
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
        std::uint64_t structureDiffsSent = 0;
        std::uint64_t structureDiffFailures = 0;
        std::uint64_t structureChunksFiltered = 0;
        std::uint64_t playerStatesSent = 0;
        std::uint64_t playerStateFailures = 0;
        std::uint64_t socialStatesSent = 0;
        std::uint64_t chatMessagesReceived = 0;
        std::uint64_t chatMessagesRejected = 0;
        std::uint64_t gunshots = 0;
        std::uint64_t worldSoundsSent = 0;
        std::uint64_t strategicSnapshotsSent = 0;
        std::uint64_t strategicSnapshotFailures = 0;
        std::uint64_t strategicRequestsReceived = 0;
        std::uint64_t strategicRequestsRejected = 0;
        std::uint64_t strategicRequestsRateLimited = 0;
        std::uint64_t worldEventSnapshotsSent = 0;
        std::uint64_t worldEventSnapshotFailures = 0;
        std::uint64_t worldEventChoicesReceived = 0;
        std::uint64_t worldEventChoicesRejected = 0;
        std::uint64_t projectilesFired = 0;
        std::uint64_t projectilesHit = 0;
        std::uint64_t throwsStarted = 0;
        std::uint64_t throwsResolved = 0;
        std::uint64_t throwsRejected = 0;
        std::uint64_t throwsRecovered = 0;
        std::uint64_t throwsBroken = 0;
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

    double GameMinutes() const;
    void AdvanceGameMinutes(double minutes);

    game::WorldSimulation& Simulation();
    const game::WorldSimulation& Simulation() const;
    const core::Config& Config() const;

    bool NetworkEnabled() const { return m_networkEnabled; }
    std::uint16_t NetworkPort() const;
    std::uint64_t ContentHash() const { return m_options.contentHash; }
    std::size_t SessionCount() const { return m_sessions.size(); }
    std::size_t InFlightThrowCount() const { return m_serverThrows.size(); }
    NetworkStats GetNetworkStats() const { return m_networkStats; }
    // M25-T3/M26：服务器权威载具 StableId；主载具用于兼容旧测试/存档。
    std::uint64_t VehicleStableId() const {
        std::uint64_t id = 0;
        for (const ServerVehicleRuntime& vehicle : m_serverVehicles) {
            if (vehicle.stableId != 0 &&
                (id == 0 || vehicle.stableId < id)) {
                id = vehicle.stableId;
            }
        }
        return id;
    }
    std::vector<std::uint64_t> VehicleStableIds() const;

private:
    struct ServerSurvivalAction {
        std::uint8_t mode = net::kPlayerSurvivalModeNone;
        std::uint8_t phase = 0;
        std::string name;
        std::string zoneId;
        std::string tableId;
        std::string spotId;
        int x = 0;
        int y = 0;
        int z = 0;
        int waterX = 0;
        int waterY = 0;
        int waterZ = 0;
        float elapsed = 0.0f;
        float duration = 0.0f;
        float biteDuration = 0.0f;
        float biteElapsed = 0.0f;
        float catchChance = 0.0f;
        std::string reservationKey;
    };

    // M23-F2b-T6：服务器权威动物剥取；进度只由服务器推进，尸体按 StableId 校验。
    struct ServerAnimalHarvestAction {
        bool active = false;
        std::uint64_t corpseStableId = 0;
        std::string speciesId;
        std::string name;
        float x = 0.0f;
        float y = 0.0f;
        int z = 0;
        float elapsed = 0.0f;
        float duration = 0.0f;
        std::uint32_t startHitSerial = 0;
    };

    struct SessionPlayer {
        std::string identity;
        std::string playerName;
        std::uint64_t playerStableId = 0;
        // M25-T3：当前乘坐的服务器载具 StableId；0 = 步行。仅运行时，不存档。
        std::uint64_t drivingVehicleId = 0;
        bool spawned = false;
        bool dead = false;
        std::uint64_t respawnAtTick = 0;
        std::uint32_t lastCommandSequence = 0;
        std::uint32_t commandsThisTick = 0;
        std::uint64_t lastAttackTick = 0;
        std::uint64_t lastAttackCooldownTicks = 0;  // M15-T1：按当前武器冷却计算
        std::uint64_t lastChatTick = 0;
        std::uint64_t lastProjectileTick = 0;
        bool chatUsed = false;
        bool projectileUsed = false;
        bool attackUsed = false;
        // M23-F1-T6：服务器权威远程武器运行时（冷却/换弹），不进入存档。
        items::RangedWeaponRuntime rangedWeapon;
        std::uint32_t rangedShotSerial = 0;
        bool throwUsed = false;
        std::uint64_t lastThrowTick = 0;
        // M23-F2a-T6：服务器权威采集/钓鱼动作与附近节点缓存。
        ServerSurvivalAction survival;
        // M23-F2b-T6：服务器权威动物剥取动作。
        ServerAnimalHarvestAction animalHarvest;
        std::vector<net::PlayerForageNodeState> nearbyForage;
        std::vector<net::PlayerFishingSpotState> nearbyFishing;
        bool survivalScanned = false;
        bool assignmentSent = false;
        bool needsFullSnapshot = true;
        bool needsStrategicSnapshot = true;
        bool needsWorldEventSnapshot = true;
        std::uint32_t lastStrategicRequestSequence = 0;
        std::uint32_t strategicRequestsThisTick = 0;
        std::uint64_t lastStructureSyncTick = 0;
        std::uint64_t lastBuildTick = 0;
        bool buildUsed = false;
        bool demolishConfirmPending = false;
        bool demolishConfirmEntity = false;
        int demolishConfirmX = 0;
        int demolishConfirmY = 0;
        int demolishConfirmLayer = 0;
        std::uint64_t demolishConfirmToken = 0;
        std::uint64_t demolishConfirmExpiresTick = 0;
        bool stateDirty = true;
        std::uint64_t lastStateTick = 0;
        float stealthHealth = -1.0f;
        std::uint32_t statusSequence = 0;
        std::uint8_t statusTone = 0;
        std::string status;
        double lastProgressionMinute = -1.0;
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
    // M25-T3：服务器权威载具。座位只存在运行时，车辆实体/位置/燃料/后备箱随世界存档。
    struct ServerVehicleRuntime {
        ecs::Entity entity = entt::null;
        std::uint64_t stableId = 0;
        game::VehicleMotion motion;
        game::VehicleVisualDirection visualDirection =
            game::VehicleVisualDirection::South;
        std::uint64_t driverStableId = 0;
        std::uint64_t passengerStableId = 0;
        std::uint64_t secondPassengerStableId = 0;
        float inputX = 0.0f;
        float inputY = 0.0f;
        bool fuelWarned = false;
        float noiseCooldown = 0.0f;
        float ramWearCooldown = 0.0f;
        float hornCooldown = 0.0f;
        float zombieAttackCooldown = 0.0f;
        game::VehicleSurface surface = game::VehicleSurface::Paved;
        std::unordered_map<std::uint64_t, float> ramCooldowns;
        std::unordered_map<std::uint64_t, float> vehicleCollisionCooldowns;
    };
    ServerVehicleRuntime* FindServerVehicle(std::uint64_t stableId);
    const ServerVehicleRuntime* FindServerVehicle(
        std::uint64_t stableId) const;
    ServerVehicleRuntime* FindServerVehicleForSession(
        const SessionPlayer& session);
    ServerVehicleRuntime* FindNearestServerVehicle(
        float x, float y, int z, float range, float* outDistance = nullptr);
    void EnsureServerVehicles();
    void AdoptServerVehicle(ecs::Entity entity, bool restored = true);
    game::VehicleModStats ServerVehicleModStats(
        const ServerVehicleRuntime& vehicle) const;
    void SyncServerVehicleModStorage(ServerVehicleRuntime& vehicle);
    void TickServerVehicles(float dt);
    void TickServerVehicle(ServerVehicleRuntime& vehicle, float dt);
    bool ApplyServerVehicleRam(
        ServerVehicleRuntime& vehicle,
        const game::VehicleMotion* previousMotion,
        const game::VehicleModStats& modStats);
    bool ApplyServerVehicleVehicleCollisions(
        ServerVehicleRuntime& vehicle,
        const std::vector<std::uint64_t>& targets,
        const game::VehicleMotion& previousMotion,
        const game::VehicleModStats& modStats);
    bool ApplyServerVehicleAttacks(ServerVehicleRuntime& vehicle,
                                   SessionPlayer& session,
                                   const game::VehicleModStats& modStats);
    void EmitServerVehicleNoise(ServerVehicleRuntime& vehicle,
                                SessionPlayer& session);
    void SyncServerVehicleToEcs(ServerVehicleRuntime& vehicle);
    void SyncVehicleDriver(ServerVehicleRuntime& vehicle, ecs::Entity player);
    void SyncVehiclePassenger(ServerVehicleRuntime& vehicle,
                              ecs::Entity player);
    bool IsServerVehiclePassengerStableId(
        const ServerVehicleRuntime& vehicle, std::uint64_t stableId) const;
    std::uint64_t* FindFreeServerVehiclePassengerSeat(
        ServerVehicleRuntime& vehicle);
    void RemoveServerVehiclePassengerStableId(ServerVehicleRuntime& vehicle,
                                              std::uint64_t stableId);
    void SyncServerVehiclePassengers(ServerVehicleRuntime& vehicle);
    void PlaceServerPlayer(ecs::Entity player, float x, float y, int z);
    bool IsServerVehicleCellWalkable(int wx, int wy, int z) const;
    game::VehicleSurface ServerVehicleSurfaceAt(float x, float y, int z) const;
    bool FindServerVehicleExitCell(const ServerVehicleRuntime& vehicle,
                                   float* x, float* y) const;
    bool EnterServerVehicle(SessionPlayer& session, const net::Command& command,
                            std::string* error);
    bool EnterServerVehiclePassenger(SessionPlayer& session,
                                     const net::Command& command,
                                     std::string* error);
    bool TakeServerVehicleDriverSeat(SessionPlayer& session,
                                     std::string* error);
    bool GiveServerVehicleDriverSeat(SessionPlayer& session,
                                     std::string* error);
    bool ExitServerVehicle(SessionPlayer& session, std::string* error);
    bool ToggleServerVehicleHeadlights(SessionPlayer& session,
                                       std::string* error);
    bool HonkServerVehicleHorn(SessionPlayer& session, std::string* error);
    bool ServiceServerVehicle(SessionPlayer& session,
                              const net::Command& command, std::string* error);
    bool ModifyServerVehicleMod(SessionPlayer& session,
                                const net::Command& command, bool install,
                                std::string* error);
    void ReleaseServerVehicleSeat(SessionPlayer& session, bool placeAtExit,
                                  std::string_view reason);
    void PlacePlayerOutsideServerVehicle(ecs::Entity player);
    SessionPlayer* FindSessionByPlayerStableId(std::uint64_t stableId);
    // X-F1 2b-2b：服务器权威世界落盘（结构 diff + 实体/仓库存物；不含玩家实体）。
    bool SaveWorld(const char* reason);
    // M33-F3-T6：玩家断线/死亡时让归属同伴回所属聚落待命，保留 owner/StableId。
    void StandbyOwnedCompanion(std::uint64_t ownerStableId, const char* reason);
    // M31-T8：每会话兴趣窗口并集；无会话时退回默认出生点。
    std::vector<glm::vec2> CollectInterestFocuses() const;
    bool ProfilePosition(const PlayerProfile& profile, glm::vec2* out) const;
    void StreamServerInterest();
    void RestoreServerForageState();
    void StoreServerForageState();
    void StoreServerSettlementState();
    bool SetServerStealth(SessionPlayer& session, ecs::Entity player,
                          bool crouched, std::string* error);
    void ResetServerStealth(SessionPlayer& session, ecs::Entity player,
                            const char* reason);
    void UpdateServerStealth();
    void FillPlayerState(const SessionPlayer& session, net::PlayerState* state);
    void BroadcastSnapshots();
    // M32-T7：服务器权威战略快照；旧客户端忽略未知 magic。
    void BroadcastStrategicSnapshots();
    bool BuildStrategicSnapshot(net::StrategicSnapshot* snapshot,
                                std::string* error) const;
    bool HandleStrategicRequest(std::uint64_t sessionId,
                                SessionPlayer& session,
                                const net::StrategicRequest& request,
                                std::string* error);
    // M33-F2-T5: authoritative interactive world events.
    void BroadcastWorldEventSnapshots();
    bool BuildWorldEventSnapshot(net::WorldEventSnapshot* snapshot,
                                 std::string* error) const;
    void TrackServerWorldEventParticipants();
    void SettleTerminalServerWorldEvents();
    bool ChooseServerWorldEvent(SessionPlayer& session,
                                const net::Command& command,
                                std::string* error);
    void SettleServerWorldEventParticipants(std::string_view eventId);
    void AddStrategicContribution(std::uint64_t stableId,
                                  std::string_view regionId, int points);
    void UpdateStrategicContributionRewards();
    void RewardStrategicContributors(const game::StrategicRegionState& region);
    void RewardSiegeContributors(
        std::string_view regionId,
        const std::unordered_map<std::uint64_t, int>& contribution,
        int rewardPool);
    void RecordSiegeDefenseDamage(std::uint64_t playerStableId,
                                  ecs::Entity target, float damage);
    void ResetSiegeDefenseLedger();
    void CheckSiegeGuardKills();
    void DrainFireDamageCredits();
    void ProcessRegionalSiegeResult();
    // X-F1 阶段 1：把服务器已加载 chunk 中玩家修改过的格按单格 ZLCD 下发。
    void BroadcastStructureDiffs();
    // X-F1 阶段 2：服务器权威建造命令与增量结构同步。
    void MarkStructureSyncDirty();
    world::Chunk* FindServerLoadedChunk(int wx, int wy) const;
    void RebuildServerChunk(int chunkX, int chunkY);
    bool FindServerStructureAt(int wx, int wy, int* layer, std::size_t* index,
                               const game::BuildableDef** def) const;
    ecs::Entity FindServerBuildEntityAt(int wx, int wy,
                                        const game::BuildableDef** def) const;
    bool ServerCellOccupied(int wx, int wy) const;
    bool ServerBuildCooldown(SessionPlayer& session, std::string* error) const;
    bool ServerCanEditStructure(const SessionPlayer& session, std::uint64_t owner,
                                std::string* error);
    enum class DemolishConfirmDecision : std::uint8_t {
        Proceed,
        Pending,
        Reject,
    };
    DemolishConfirmDecision ServerConfirmDemolish(
        SessionPlayer& session, int wx, int wy, bool entityTarget, int layer,
        std::uint64_t token, std::uint16_t flags, const std::string& name,
        std::string* error);
    void MarkServerBuildAction(SessionPlayer& session);
    void MarkStructureTombstone(int wx, int wy, int z);
    void PruneStructureTombstones();
    bool RemoveServerWood(ecs::Entity player, int count);
    bool ReturnServerItem(ecs::Entity player, const ecs::ItemInstance& item,
                          float x, float y, int z);
    bool PlaceStructure(ecs::Entity player, SessionPlayer& session,
                        const net::Command& command, std::string* error);
    bool DemolishStructure(ecs::Entity player, SessionPlayer& session,
                           const net::Command& command, std::string* error);
    bool RepairStructure(ecs::Entity player, SessionPlayer& session,
                         const net::Command& command, std::string* error);
    bool ToggleDoor(ecs::Entity player, SessionPlayer& session,
                    const net::Command& command, std::string* error);
    bool InteractFacility(ecs::Entity player, SessionPlayer& session,
                          const net::Command& command, std::string* error);
    bool PurchaseMerchantOffer(ecs::Entity player, SessionPlayer& session,
                               const net::Command& command, std::string* error);
    bool AcceptSettlementMission(ecs::Entity player, SessionPlayer& session,
                                 const net::Command& command,
                                 std::string* error);
    bool RecruitCompanion(ecs::Entity player, SessionPlayer& session,
                          const net::Command& command, std::string* error);
    bool DismissCompanion(ecs::Entity player, SessionPlayer& session,
                          const net::Command& command, std::string* error);
    // M33-F3-T7：同伴指令/赠礼/个人委托/救援。
    bool SetCompanionCommandAction(ecs::Entity player, SessionPlayer& session,
                                   const net::Command& command,
                                   std::string* error);
    bool GiftToCompanionAction(ecs::Entity player, SessionPlayer& session,
                               const net::Command& command,
                               std::string* error);
    bool AcceptCompanionQuestAction(ecs::Entity player,
                                    SessionPlayer& session,
                                    const net::Command& command,
                                    std::string* error);
    bool AbandonCompanionQuestAction(ecs::Entity player,
                                     SessionPlayer& session,
                                     const net::Command& command,
                                     std::string* error);
    bool RescueCompanionAction(ecs::Entity player, SessionPlayer& session,
                               const net::Command& command,
                               std::string* error);
    void EnsureServerMerchant();
    void EnsureServerSettlementNpcs();
    void TickHomestead(float dt);
    void TickServerPower(float gameMinutes, float dt);
    // M22-F1：服务器权威联机睡眠与全队时间投票。
    bool ServerFindNearbyBed(ecs::Entity player, ecs::Entity* bed) const;
    bool ServerZombieNear(float x, float y, int z, float radius) const;
    bool StartServerSleepVote(SessionPlayer& session, bool untilDawn,
                              float durationMinutes, std::string* error);
    bool VoteServerSleep(SessionPlayer& session, bool agree,
                         std::string* error);
    void StartServerSleep();
    void TickServerSleep(float dt);
    void FinishServerSleep(game::SleepStopReason reason,
                           const std::string& detail);
    void HaltServerPlayerForSleep(ecs::Entity player);
    void MarkServerSleepDirty();
    std::string ServerSleepStatus(const SessionPlayer& session) const;
    std::string ServerSleepDurationLabel() const;
    void OnServerSessionRemovedForSleep(std::uint64_t playerStableId);
    void TickElectricFences();
    // X-F1 2b-6：服务器权威 BaseSiege；事件僵尸带 BaseSiegeZombie 标记。
    void InitializeServerBaseSiege();
    void TickServerBaseSiege(float dt);
    void TickServerFloodlightAttacks(float dt);
    ecs::Entity ServerSiegeAnchorPlayer();
    int CountServerPlayerStructuresNear(const game::BaseSiegePoint& point,
                                        float radius,
                                        bool barricadesOnly) const;
    std::vector<game::BaseSiegeStructure> ListServerPlayerStructuresNear(
        const game::BaseSiegePoint& point, float radius) const;
    bool IsServerSiegeSpawnCell(int wx, int wy) const;
    std::uint64_t SpawnServerSiegeZombie(int wx, int wy,
                                         ecs::ZombieSpecialKind special);
    void DespawnServerSiegeZombie(std::uint64_t stableId);
    void DriveServerSiegeZombies();
    void BroadcastServerNotice(const std::string& text,
                               const std::string& tone);
    void PublishSiegeReport(const std::string& text);
    void PushServerChat(const std::string& sender, std::string text);
    std::string GrantServerBaseSiegeRewards(
        const game::BaseSiegeResult& result);
    void TickTrapTriggers();
    void NotifyTrapTrigger(float x, float y, int z, int usesLeft);
    void BroadcastPlayerStates();
    void BroadcastSocialStates();
    void FillSocialState(net::SocialState* state) const;
    void HandleChatMessage(std::uint64_t sessionId, SessionPlayer& session,
                           const std::string& text);
    void TickCrafting(float dt);
    void TickProgression();
    void ApplyProgressionRuntime(ecs::Entity player, SessionPlayer* session);
    void MarkProgressionDirty(ecs::Entity player);
    void AwardProgressionXp(ecs::Entity player, ecs::SkillKind kind,
                            float baseAmount, SessionPlayer* session);
    void UpdateProjectiles(float dt);
    void UpdateTransientEffects(float dt);
    bool FireProjectile(SessionPlayer& session, const net::Command& command);
    bool FireRanged(SessionPlayer& session, const net::Command& command);
    bool ReloadRanged(SessionPlayer& session);
    struct ServerThrow {
        std::uint64_t ownerStableId = 0;
        std::string defId;
        std::string name;
        int z = 0;
        float targetX = 0.0f;
        float targetY = 0.0f;
        float directionX = 1.0f;
        float directionY = 0.0f;
        float remainingSeconds = 0.0f;
        float damage = 0.0f;
        float impactRadius = 0.0f;
        float noiseRadius = 0.0f;
        float fireRadius = 0.0f;
        float fireMinutes = 0.0f;
        float itemCondition = 1.0f;
        bool returnable = false;
        float breakChance = 0.0f;
        bool wallClipped = false;
    };
    bool ThrowItemAction(SessionPlayer& session, const net::Command& command,
                         std::string* error);
    void UpdateServerThrows(float dt);
    void SettleServerThrows(const char* reason, std::uint64_t ownerStableId = 0);
    void ResolveServerThrow(ServerThrow& pending);
    bool IgniteFireAction(SessionPlayer& session, const net::Command& command,
                          std::string* error);
    bool ExtinguishFireAction(SessionPlayer& session,
                              const net::Command& command,
                              std::string* error);
    void TickServerRangedWeapons(float dt);
    // M23-F2a-T6：服务器权威采集/钓鱼、节点刷新与距离/重复校验。
    void TickServerSurvival(float dt);
    // M23-F2b-T6：服务器权威剥取、进度复制、尸体预约与防重复结算。
    void TickServerAnimalHarvest(float dt);
    bool StartServerAnimalHarvest(SessionPlayer& session,
                                  const net::Command& command,
                                  std::string* error);
    void CompleteServerAnimalHarvest(SessionPlayer& session);
    void CancelServerAnimalHarvest(SessionPlayer& session,
                                   std::string_view reason,
                                   std::uint8_t tone);
    void ReleaseServerAnimalHarvest(SessionPlayer& session);
    void RefreshServerSurvival(SessionPlayer& session, ecs::Entity player);
    void ClearServerSurvival(SessionPlayer& session, std::string_view reason,
                             std::uint8_t tone);
    void ReleaseServerForageReservation(SessionPlayer& session);
    bool StartServerForage(SessionPlayer& session, const net::Command& command,
                           std::string* error);
    bool StartServerFishing(SessionPlayer& session, const net::Command& command,
                            std::string* error);
    bool ReelServerFishing(SessionPlayer& session, std::string* error);
    void CompleteServerForage(SessionPlayer& session);
    void CompleteServerFishing(SessionPlayer& session, bool caught);
    bool ServerHasForageTool(const ecs::Entity player,
                             const game::ForageZoneDef& zone) const;
    bool ServerWearForageTool(const ecs::Entity player,
                              const game::ForageZoneDef& zone);
    bool ServerHasFishingRod(const ecs::Entity player,
                             const game::FishingSpotDef& spot) const;
    int ServerFishingBaitCount(const ecs::Entity player,
                               const game::FishingSpotDef& spot) const;
    bool ServerConsumeFishingBait(const ecs::Entity player,
                                  const game::FishingSpotDef& spot);
    void ServerWearFishingRod(const ecs::Entity player,
                              const game::FishingSpotDef& spot);
    void SpawnImpactEffect(float x, float y, int z);
    void SpawnCraftEffect(std::uint64_t playerStableId);
    void SpawnDemoLoot();
    // M19-F2/M19-F3：服务器权威 POI 容器/守军与任务进度。
    void EnsureServerPoiContent();
    void EnsureServerLootContainers();
    bool FindServerPoiGuardCell(const game::PoiDef& poi, int index, int* wx,
                                int* wy) const;
    int CountServerPoiGuards(std::string_view poiId) const;
    bool ServerPoiTargetSearched(const game::PoiDef& poi) const;
    bool FindServerPoiStageEnemyCell(const game::PoiDef& poi,
                                     const game::PoiStageDef& stage, int index,
                                     float originX, float originY, int* wx,
                                     int* wy) const;
    ecs::Entity SpawnServerPoiStageEnemy(
        const game::PoiDef& poi, const game::PoiStageDef& stage,
        game::PoiStageEnemyRole role, game::PoiGuardKind kind, int index,
        float originX, float originY);
    void EnsureServerPoiStageEnemies(const game::PoiDef& poi,
                                     game::PoiProgress* progress);
    void UpdateServerPoiBossMechanics(const game::PoiDef& poi,
                                      const game::PoiProgress& progress,
                                      float dt);
    void ClearServerPoiStageEnemies(std::string_view poiId,
                                    std::string_view stageId = {});
    bool ServerPoiPlayerNearStart(const game::PoiDef& poi, float x,
                                  float y) const;
    void FailServerPoiOnWipe();
    ecs::PoiWorldProgress* ServerPoiWorldProgress();
    void TickServerPoiStages(float dt);
    bool InteractPoi(ecs::Entity player, SessionPlayer& session,
                     const net::Command& command, std::string* error);
    bool ServerPlayerHasPoiKeyItem(ecs::Entity player,
                                   const game::PoiKeyDef& key) const;
    bool ServerConsumePoiKeyItem(ecs::Entity player,
                                 const game::PoiKeyDef& key);
    void GrantServerPoiStageReward(SessionPlayer& session,
                                   const game::PoiDef& poi,
                                   const game::PoiStageDef& stage);
    void GrantServerPoiStageKeys(SessionPlayer& session,
                                 const game::PoiDef& poi,
                                 const game::PoiStageDef& stage);
    void ApplyServerPoiStageUpdate(const game::PoiDef& poi,
                                   game::PoiProgress& progress,
                                   const game::PoiStageUpdate& update);
    void TickServerPoiTasks();
    void TickServerObjectives();
    void RecordServerMissionEvent(SessionPlayer& session,
                                  game::MissionObjectiveKind kind,
                                  std::string_view target, int amount = 1);
    void TickServerMissions();
    void EnsureServerRandomEventContent(int day);
    void ActivateServerMission(SessionPlayer& session,
                               const game::MissionDef& def, int day,
                               std::string_view settlementId = {});
    void UpdateServerMissionAvailability(SessionPlayer& session, int day);
    void CompleteServerMission(SessionPlayer& session,
                               ecs::MissionRunState& state,
                               const game::MissionDef& def);
    bool GrantServerMissionReward(ecs::Entity player, const game::MissionDef& def,
                                  bool* itemDropped = nullptr);
    void OnServerLootSearched(SessionPlayer& session,
                              std::string_view tableId);
    void EmitGunshot(std::uint64_t playerStableId,
                     float noiseRadius = -1.0f);
    void BroadcastWorldSound(const net::WorldSoundEvent& sound);
    void SetSessionStatus(SessionPlayer& session, std::string message,
                          std::uint8_t tone);
    bool ExecuteSessionCommand(std::uint64_t sessionId, SessionPlayer& session,
                               const net::Command& command, std::string* error);
    void RemoveSession(std::uint64_t sessionId, std::uint8_t reason,
                       const std::string& message);
    void LogStatus();

    Options m_options;
    std::string m_selectedMapDir;
    std::string m_selectedContentFile;
    std::unique_ptr<core::Config> m_config;
    std::unique_ptr<core::EventBus> m_events;
    std::unique_ptr<core::GameTime> m_gameTime;
    std::unique_ptr<core::TaskScheduler> m_tasks;
    std::unique_ptr<items::ItemRegistry> m_items;
    std::unique_ptr<items::RecipeRegistry> m_recipes;
    std::unique_ptr<items::LootTableRegistry> m_lootTables;
    std::unique_ptr<game::ForageRegistry> m_forage;
    std::unique_ptr<game::AnimalRegistry> m_animals;
    std::unique_ptr<game::CropRegistry> m_crops;
    std::unique_ptr<game::FireRegistry> m_fire;
    std::unique_ptr<game::PoiConfig> m_poiConfig;
    std::unique_ptr<game::MissionConfig> m_missionConfig;
    std::unique_ptr<game::SettlementConfig> m_settlements;
    std::unique_ptr<game::WorldEventConfig> m_worldEvents;
    std::unique_ptr<game::CompanionConfig> m_companions;
    std::unique_ptr<game::VehicleModsConfig> m_vehicleMods;
    std::unique_ptr<game::TrapDefsConfig> m_traps;
    std::unique_ptr<game::StrategicConfig> m_strategic;
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
    std::unordered_map<std::uint64_t,
                       std::unordered_map<std::string, int>>
        m_strategicContributions;
    std::unordered_map<std::string, game::StrategicOwner>
        m_lastStrategicOwners;
    std::string m_lastSiegeResultKey;
    std::string m_siegeDefenseRegion;
    std::unordered_map<std::uint64_t, float> m_siegeDefenseDamage;
    std::unordered_map<std::uint64_t,
                       std::unordered_map<std::uint64_t, float>>
        m_siegeTargetDamage;
    std::unordered_set<std::uint64_t> m_siegeGuardKills;
    std::uint32_t m_strategicSequence = 0;
    bool m_strategicDirty = true;
    std::uint32_t m_worldEventSequence = 0;
    bool m_worldEventDirty = true;
    float m_worldEventDetailRadius = 32.0f;
    std::unordered_map<std::string, std::unordered_set<std::uint64_t>>
        m_worldEventParticipants;
    std::unordered_set<std::string> m_worldEventTerminalHandled;
    bool m_networkEnabled = false;

    std::string m_activeSaveId;
    bool m_activeSaveUseBackup = false;
    bool m_saveWorldOnExit = true;
    bool m_demoLootSpawned = false;
    bool m_demoLootContainersSpawned = false;
    bool m_houseLootContainersSpawned = false;
    bool m_worldLoadedFromSave = false;
    struct ServerPoiRuntime {
        bool containersSpawned = false;
        bool guardsSpawned = false;
    };
    std::unordered_map<std::string, ServerPoiRuntime> m_poiRuntime;
    std::unordered_map<std::string, float> m_serverPoiBossResetTimers;
    std::unordered_set<std::string> m_serverEventReady;
    std::unordered_set<std::string> m_serverColdSnapEvents;
    int m_serverEventDay = 0;
    float m_poiScanTimer = 0.0f;
    std::string m_saveWarning;
    std::atomic<bool> m_running{false};
    bool m_initialized = false;
    uint64_t m_ticks = 0;
    double m_logTimer = 0.0;
    float m_attackDamage = 40.0f;
    float m_attackRange = 1.7f;
    float m_attackArc = 100.0f;
    float m_attackCooldownSeconds = 0.5f;
    float m_attackKnockback = 3.0f;
    float m_attackStagger = 0.35f;
    float m_attackWear = 0.0f;
    characters::StealthRuleConfig m_stealthRules;
    bool m_pvpEnabled = false;
    float m_gunshotRadius = 30.0f;
    float m_projectileDamage = 24.0f;
    float m_projectileSpeed = 18.0f;
    float m_projectileLifetime = 2.5f;
    float m_maxProjectileDistance = 24.0f;
    game::ThrowingConfig m_throwingConfig;
    std::uint64_t m_throwCooldownTicks = 42;
    std::vector<ServerThrow> m_serverThrows;
    std::mt19937 m_throwRng{20261118u};
    std::uint32_t m_stateSendIntervalTicks = 6;
    std::uint32_t m_socialStateIntervalTicks = 30;
    std::uint32_t m_structureSyncIntervalTicks = 180;
    float m_structureInterestRadius = 48.0f;
    std::uint64_t m_chatCooldownTicks = 60;
    std::uint32_t m_socialSequence = 0;
    std::uint64_t m_lastSocialTick = 0;
    bool m_socialDirty = true;
    std::deque<net::SocialChatLine> m_chatLog;
    std::uint64_t m_respawnDelayTicks = 300;
    double m_profileSaveIntervalSeconds = 30.0;
    double m_profileSaveTimer = 0.0;
    bool m_profilesEnabled = false;
    game::ProgressionConfig m_progressionConfig;
    game::HomesteadConfig m_homesteadConfig;
    game::BaseSiege m_serverBaseSiege;
    std::uint64_t m_serverSiegeAnchorStableId = 0;
    game::SleepConfig m_serverSleepConfig;
    bool m_serverSleepEnabled = true;
    float m_serverSleepVoteSeconds = 30.0f;
    float m_serverSleepHungerPerDay = 1.4f;
    float m_serverSleepThirstPerDay = 1.8f;
    enum class ServerSleepPhase : std::uint8_t {
        None = 0,
        Vote,
        Sleeping,
    };
    struct ServerSleepRuntime {
        ServerSleepPhase phase = ServerSleepPhase::None;
        std::uint64_t initiator = 0;
        bool untilDawn = false;
        double startMinutes = 0.0;
        double durationMinutes = 0.0;
        double targetMinutes = 0.0;
        float voteSeconds = 0.0f;
        std::unordered_set<std::uint64_t> eligible;
        std::unordered_set<std::uint64_t> yes;
        std::unordered_map<std::uint64_t, float> lastHealth;
    };
    ServerSleepRuntime m_serverSleep;
    game::SleepSession m_serverSleepSession;
    struct ServerPowerSummary {
        float fuelMinutes = 0.0f;
        float load = 0.0f;
        bool lowFuel = false;
        bool outage = false;
    };
    ServerPowerSummary m_serverPowerSummary;
    std::unordered_set<std::uint64_t> m_serverPoweredFenceCells;
    std::unordered_set<std::uint64_t> m_serverElectricFenceCells;
    std::unordered_map<std::uint64_t, float> m_serverFenceCooldowns;
    float m_serverPowerScanTimer = 0.0f;
    float m_serverFenceNoticeCooldown = 0.0f;
    std::vector<game::BuildableDef> m_buildables;
    std::vector<game::BuildableDef> m_modBuildables;
    std::string m_buildWoodItem = "wood";
    float m_buildRange = 3.5f;
    float m_buildActionRange = 2.5f;
    float m_campfireRange = 2.5f;
    std::mt19937 m_fireRng{20261029u};
    int m_repairWoodCost = 1;
    float m_structureRepairAmount = 30.0f;
    float m_demolishRefundRatio = 0.5f;
    float m_buildZombieDamage = 12.0f;
    float m_buildZombieCooldown = 1.0f;
    float m_breakerDamage = 28.0f;
    float m_breakerCooldown = 1.2f;
    float m_breakerRange = 2.2f;
    std::unordered_map<std::uint64_t, float> m_serverFloodlightAttackCooldowns;
    float m_baseCarryKg = 20.0f;
    float m_baseWalkSpeed = 4.0f;
    double m_lastHomesteadMinute = -1.0;
    ecs::ForageState m_serverForage;
    ecs::Entity m_serverForageEntity = entt::null;
    std::unordered_map<std::string, std::uint64_t> m_forageReservations;
    std::unordered_map<std::uint64_t, std::uint64_t>
        m_animalHarvestReservations;
    float m_survivalScanTimer = 0.0f;
    float m_animalHarvestRange = 3.0f;
    float m_forageInteractRange = 2.2f;
    float m_fishingInteractRange = 2.2f;
    float m_foragePlanRadius = 10.0f;
    float m_fishingPlanRadius = 12.0f;
    std::vector<ServerVehicleRuntime> m_serverVehicles;
    std::size_t m_vehicleSpawnCount = 3;
    game::VehicleTuning m_vehicleTuning;
    std::string m_vehicleRepairItem = "repair_kit";
    std::string m_vehicleFuelItem = "fuel_can";

    // M13-T6：命令防御策略（按服务器 tick 配额，不依赖客户端 tick）。
    std::uint32_t m_maxCommandsPerTick = 8;
    float m_maxMoveToDistance = 64.0f;
    float m_maxAttackDistance = 3.0f;
    std::uint64_t m_attackCooldownTicks = 21;
    std::uint64_t m_projectileCooldownTicks = 30;
    std::uint64_t m_buildCooldownTicks = 30;
    struct StructureTombstone {
        int wx = 0;
        int wy = 0;
        int z = 0;
        std::uint64_t expiresTick = 0;
    };
    std::vector<StructureTombstone> m_structureTombstones;
    NetworkStats m_networkStats;
};

}  // namespace zl::server
