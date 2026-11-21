#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ai/components/AiComponents.h"
#include "characters/components/AnimalComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "ecs/EcsWorld.h"
#include "game/Companion.h"
#include "game/FireComponents.h"
#include "game/HomesteadComponents.h"
#include "game/SettlementNpc.h"
#include "game/VehicleComponents.h"
#include "items/ItemComponents.h"
#include "net/Replication.h"

namespace zl::game {

// M13-T4：ECS ↔ 复制协议的桥接层。
//
// net/Replication 保持 game-agnostic；本文件登记“哪些组件可复制、如何编码”，
// 并负责 AOI、全量/增量基线、客户端副本、位置插值与本地玩家轻量预测。
// 服务器发送视野半径内的角色与地面物品；尸体仍不复制。

inline constexpr std::uint32_t kReplicationArchetypePlayer = 1;
inline constexpr std::uint32_t kReplicationArchetypeCharacter = 2;
inline constexpr std::uint32_t kReplicationArchetypeItem = 3;
inline constexpr std::uint32_t kReplicationArchetypeEffect = 4;
inline constexpr std::uint32_t kReplicationArchetypeProjectile = 5;
inline constexpr std::uint32_t kReplicationArchetypeProp = 6;

inline constexpr std::uint32_t kReplicationPosition =
    net::MakeReplicationComponentId('P', 'O', 'S', 'N');
inline constexpr std::uint32_t kReplicationVelocity =
    net::MakeReplicationComponentId('V', 'E', 'L', 'O');
inline constexpr std::uint32_t kReplicationHealth =
    net::MakeReplicationComponentId('H', 'L', 'T', 'H');
inline constexpr std::uint32_t kReplicationSprite =
    net::MakeReplicationComponentId('S', 'P', 'R', 'T');
inline constexpr std::uint32_t kReplicationFacing =
    net::MakeReplicationComponentId('F', 'A', 'C', 'E');
inline constexpr std::uint32_t kReplicationAnim =
    net::MakeReplicationComponentId('A', 'N', 'I', 'M');
inline constexpr std::uint32_t kReplicationItem =
    net::MakeReplicationComponentId('I', 'T', 'E', 'M');
inline constexpr std::uint32_t kReplicationEquipment =
    net::MakeReplicationComponentId('E', 'Q', 'I', 'P');
inline constexpr std::uint32_t kReplicationAction =
    net::MakeReplicationComponentId('A', 'C', 'T', 'N');
inline constexpr std::uint32_t kReplicationCombatStance =
    net::MakeReplicationComponentId('C', 'B', 'S', 'T');
inline constexpr std::uint32_t kReplicationStealth =
    net::MakeReplicationComponentId('S', 'T', 'L', 'H');
inline constexpr std::uint32_t kReplicationVariant =
    net::MakeReplicationComponentId('Z', 'V', 'A', 'R');
inline constexpr std::uint32_t kReplicationSpecial =
    net::MakeReplicationComponentId('Z', 'S', 'P', 'C');
inline constexpr std::uint32_t kReplicationWounds =
    net::MakeReplicationComponentId('W', 'N', 'D', 'S');
inline constexpr std::uint32_t kReplicationPain =
    net::MakeReplicationComponentId('P', 'A', 'I', 'N');
inline constexpr std::uint32_t kReplicationInfection =
    net::MakeReplicationComponentId('I', 'N', 'F', 'C');
inline constexpr std::uint32_t kReplicationStorage =
    net::MakeReplicationComponentId('S', 'T', 'R', 'G');
inline constexpr std::uint32_t kReplicationVehicle =
    net::MakeReplicationComponentId('V', 'H', 'C', 'L');
inline constexpr std::uint32_t kReplicationFarmPlot =
    net::MakeReplicationComponentId('F', 'A', 'R', 'M');
inline constexpr std::uint32_t kReplicationGenerator =
    net::MakeReplicationComponentId('G', 'E', 'N', 'R');
inline constexpr std::uint32_t kReplicationColdStorage =
    net::MakeReplicationComponentId('C', 'O', 'L', 'D');
inline constexpr std::uint32_t kReplicationFloodlight =
    net::MakeReplicationComponentId('F', 'L', 'G', 'T');
inline constexpr std::uint32_t kReplicationTrap =
    net::MakeReplicationComponentId('T', 'R', 'A', 'P');
inline constexpr std::uint32_t kReplicationMerchant =
    net::MakeReplicationComponentId('M', 'R', 'C', 'H');
inline constexpr std::uint32_t kReplicationSettlementNpc =
    net::MakeReplicationComponentId('S', 'T', 'L', 'N');
inline constexpr std::uint32_t kReplicationLoot =
    net::MakeReplicationComponentId('L', 'O', 'O', 'T');
inline constexpr std::uint32_t kReplicationFire =
    net::MakeReplicationComponentId('F', 'I', 'R', 'E');
inline constexpr std::uint32_t kReplicationBurning =
    net::MakeReplicationComponentId('B', 'U', 'R', 'N');
inline constexpr std::uint32_t kReplicationAnimal =
    net::MakeReplicationComponentId('A', 'N', 'M', 'L');
inline constexpr std::uint32_t kReplicationAnimalCorpse =
    net::MakeReplicationComponentId('A', 'C', 'R', 'P');
inline constexpr std::uint32_t kReplicationCompanion =
    net::MakeReplicationComponentId('C', 'M', 'P', 'N');
inline constexpr std::uint32_t kReplicationVehicleMods =
    net::MakeReplicationComponentId('V', 'M', 'O', 'D');
inline constexpr std::uint32_t kReplicationPlacedTrap =
    net::MakeReplicationComponentId('T', 'R', 'A', '2');
inline constexpr std::size_t kMaxReplicationAnimalSpeciesBytes = 64;

bool RegisterEntityReplicationComponents(net::ReplicationRegistry& registry,
                                         std::string* error = nullptr);

// M33-F3-T6：同伴只读状态摘要。位置/生命/装备仍走通用复制组件；
// 这里补 defId、指令、待命/倒地/死亡和信任/个人委托进度。
struct CompanionReplicationState {
    std::string defId;
    CompanionCommand command = CompanionCommand::Follow;
    int loyalty = 50;
    bool standby = false;
    bool downed = false;
    bool dead = false;
    float downedRemaining = 0.0f;
    CompanionQuestStatus questStatus = CompanionQuestStatus::Locked;
    int questStage = 0;
    int questProgress = 0;
    std::uint32_t rewardMask = 0;
};

// M33-F4-T5：车辆改装只读摘要。客户端只按槽位显示服务器结果，
// 安装/拆除仍由服务器命令校验材料与槽位后修改权威组件。
struct VehicleModsReplicationState {
    std::array<std::string, ecs::kVehicleModSlotCount> defIds{};
};

struct ReplicatedActor {
    std::uint64_t stableId = 0;
    std::uint64_t ownerStableId = 0;
    std::uint32_t archetype = 0;
    bool player = false;

    ecs::Position position;
    ecs::Position previousPosition;
    ecs::Velocity velocity;
    ecs::Health health;
    ecs::SpriteRef sprite;
    ecs::Facing facing;
    ecs::AnimState anim;
    ecs::ItemInstance item;
    std::array<std::optional<ecs::ItemInstance>, ecs::kEquipSlotCount> equipment;
    ecs::ActionState action;
    ecs::CombatStance combatStance;
    bool stealthCrouched = false;
    bool stealthMoving = false;
    ecs::ZombieVariant variant;
    ecs::ZombieSpecial special;
    ecs::Wounds wounds;
    ecs::Pain pain;
    ecs::Infection infection;
    ecs::PlayerStorage storage;
    ecs::Container storageContainer;
    ecs::Vehicle vehicle;
    std::uint64_t vehicleDriverStableId = 0;
    std::uint64_t vehiclePassengerStableId = 0;
    std::uint64_t vehicleSecondPassengerStableId = 0;
    ecs::FarmPlot farmPlot;
    ecs::Generator generator;
    ecs::ColdStorage coldStorage;
    ecs::Floodlight floodlight;
    ecs::SpikeTrap trap;
    ecs::PlacedTrap placedTrap;
    ecs::MerchantStock merchantStock;
    SettlementNpc settlementNpc;
    ecs::LootContainer lootContainer;
    ecs::Container lootContainerItems;
    ecs::FireSource fireSource;
    ecs::FireAnchor fireAnchor;
    ecs::BurningZombie burning;
    ecs::Animal animal;
    ecs::AnimalCorpse animalCorpse;
    CompanionReplicationState companion;
    VehicleModsReplicationState vehicleMods;

    bool hasPosition = false;
    bool hasVelocity = false;
    bool hasHealth = false;
    bool hasSprite = false;
    bool hasFacing = false;
    bool hasAnim = false;
    bool hasItem = false;
    bool hasEquipment = false;
    bool hasAction = false;
    bool hasCombatStance = false;
    bool hasStealth = false;
    bool hasVariant = false;
    bool hasSpecial = false;
    bool hasWounds = false;
    bool hasPain = false;
    bool hasInfection = false;
    bool hasStorage = false;
    bool hasVehicle = false;
    bool hasFarmPlot = false;
    bool hasGenerator = false;
    bool hasColdStorage = false;
    bool hasFloodlight = false;
    bool hasTrap = false;
    bool hasPlacedTrap = false;
    bool hasMerchantStock = false;
    bool hasSettlementNpc = false;
    bool hasLoot = false;
    bool hasFire = false;
    bool hasBurning = false;
    bool hasAnimal = false;
    bool hasAnimalCorpse = false;
    bool hasCompanion = false;
    bool hasVehicleMods = false;
};

class EntityReplicationProducer {
public:
    struct Options {
        float aoiRadius = 48.0f;
        std::size_t maxEntities = 256;
        std::uint64_t viewerStableId = 0;
    };

    struct Stats {
        std::uint64_t fullSnapshots = 0;
        std::uint64_t deltaSnapshots = 0;
        std::uint64_t entitiesSent = 0;
        std::uint64_t entitiesRemoved = 0;
    };

    static Options DefaultOptions();
    EntityReplicationProducer(ecs::EcsWorld& world,
                              const net::ReplicationRegistry& registry,
                              Options options = DefaultOptions());

    // 全量快照重置该客户端的复制基线（首次连接/丢失增量基线时调用）。
    net::ReplicationSnapshot BuildFull(std::uint64_t serverTick, float focusX,
                                       float focusY, std::string* error = nullptr);

    // 与上次 BuildFull/BuildDelta 比较，只发送变化组件、新实体与离开 AOI/被销毁的实体。
    // 注意：构建成功即推进基线；若发送失败，T5 必须丢弃该客户端的 producer 并重新 BuildFull。
    bool BuildDelta(std::uint64_t serverTick, float focusX, float focusY,
                    net::ReplicationSnapshot* snapshot,
                    std::string* error = nullptr);

    void Reset();
    bool HasBaseline() const { return m_hasBaseline; }
    std::uint64_t LastBuiltTick() const { return m_lastBuiltTick; }
    std::size_t BaselineCount() const { return m_baseline.size(); }
    Stats GetStats() const { return m_stats; }

private:
    struct BaselineEntity {
        std::uint32_t archetype = 0;
        std::uint64_t ownerStableId = 0;
        std::vector<net::ReplicatedComponent> components;
    };

    bool Capture(float focusX, float focusY,
                 std::vector<net::ReplicatedEntity>* entities,
                 std::string* error) const;

    ecs::EcsWorld& m_world;
    const net::ReplicationRegistry& m_registry;
    Options m_options;
    std::unordered_map<std::uint64_t, BaselineEntity> m_baseline;
    std::uint64_t m_lastBuiltTick = 0;
    std::uint32_t m_sequence = 0;
    bool m_hasBaseline = false;
    Stats m_stats;
};

inline EntityReplicationProducer::Options EntityReplicationProducer::DefaultOptions() {
    return {};
}

class EntityReplicationClient {
public:
    struct Options {
        float localSnapDistance = 1.0f;
        float localCorrectionBlend = 0.25f;
    };

    struct Stats {
        std::uint64_t snapshotsApplied = 0;
        std::uint64_t fullSnapshots = 0;
        std::uint64_t deltaSnapshots = 0;
        std::uint64_t entitiesSpawned = 0;
        std::uint64_t entitiesRemoved = 0;
        std::uint64_t corrections = 0;
        std::uint64_t snaps = 0;
        float lastCorrectionDistance = 0.0f;
    };

    static Options DefaultOptions();
    explicit EntityReplicationClient(Options options = DefaultOptions());

    // Delta 的 baseTick 必须等于 LastAppliedTick()；不匹配时拒绝并标记需要全量快照。
    bool Apply(const net::ReplicationSnapshot& snapshot,
               std::string* error = nullptr);
    void Reset();

    bool SetLocalPlayer(std::uint64_t stableId, float maxSpeed);
    void ClearLocalPlayer();

    // 本地玩家轻量预测：只按输入方向积分，不做碰撞/战斗；服务器快照到达后校正。
    bool AdvanceLocalPrediction(float dt, float dirX, float dirY);
    void ReconcileLocalPlayer();

    // 远程实体在上一/当前快照位置之间插值；本地玩家返回预测位置（忽略 alpha）。
    bool SamplePosition(std::uint64_t stableId, float alpha,
                        ecs::Position* position) const;
    bool LocalPlayerPosition(ecs::Position* position) const;

    const ReplicatedActor* Find(std::uint64_t stableId) const;
    const std::unordered_map<std::uint64_t, ReplicatedActor>& Actors() const {
        return m_actors;
    }
    std::uint64_t LastAppliedTick() const { return m_lastAppliedTick; }
    bool NeedsFullSnapshot() const { return m_needsFullSnapshot; }
    std::uint64_t LocalPlayerId() const { return m_localPlayerId; }
    Stats GetStats() const { return m_stats; }

private:
    struct ActorPatch {
        std::uint64_t stableId = 0;
        std::uint32_t archetype = 0;
        std::uint64_t ownerStableId = 0;
        ecs::Position position;
        ecs::Velocity velocity;
        ecs::Health health;
        ecs::SpriteRef sprite;
        ecs::Facing facing;
        ecs::AnimState anim;
        ecs::ItemInstance item;
        std::array<std::optional<ecs::ItemInstance>, ecs::kEquipSlotCount> equipment;
        ecs::ActionState action;
        ecs::CombatStance combatStance;
        bool stealthCrouched = false;
        bool stealthMoving = false;
        ecs::ZombieVariant variant;
        ecs::ZombieSpecial special;
        ecs::Wounds wounds;
        ecs::Pain pain;
        ecs::Infection infection;
        ecs::PlayerStorage storage;
        ecs::Container storageContainer;
        ecs::Vehicle vehicle;
        std::uint64_t vehicleDriverStableId = 0;
        std::uint64_t vehiclePassengerStableId = 0;
        std::uint64_t vehicleSecondPassengerStableId = 0;
        ecs::FarmPlot farmPlot;
        ecs::Generator generator;
        ecs::ColdStorage coldStorage;
        ecs::Floodlight floodlight;
        ecs::SpikeTrap trap;
        ecs::PlacedTrap placedTrap;
        ecs::MerchantStock merchantStock;
        SettlementNpc settlementNpc;
        ecs::LootContainer lootContainer;
        ecs::Container lootContainerItems;
        ecs::FireSource fireSource;
        ecs::FireAnchor fireAnchor;
        ecs::BurningZombie burning;
        ecs::Animal animal;
        ecs::AnimalCorpse animalCorpse;
        CompanionReplicationState companion;
        VehicleModsReplicationState vehicleMods;
        bool hasPosition = false;
        bool hasVelocity = false;
        bool hasHealth = false;
        bool hasSprite = false;
        bool hasFacing = false;
        bool hasAnim = false;
        bool hasItem = false;
        bool hasEquipment = false;
        bool hasAction = false;
        bool hasCombatStance = false;
        bool hasStealth = false;
        bool hasVariant = false;
        bool hasSpecial = false;
        bool hasWounds = false;
        bool hasPain = false;
        bool hasInfection = false;
        bool hasStorage = false;
        bool hasVehicle = false;
        bool hasFarmPlot = false;
        bool hasGenerator = false;
        bool hasColdStorage = false;
        bool hasFloodlight = false;
        bool hasTrap = false;
        bool hasPlacedTrap = false;
        bool hasMerchantStock = false;
        bool hasSettlementNpc = false;
        bool hasLoot = false;
        bool hasFire = false;
        bool hasBurning = false;
        bool hasAnimal = false;
        bool hasAnimalCorpse = false;
        bool hasCompanion = false;
        bool hasVehicleMods = false;
    };

    bool DecodePatch(const net::ReplicatedEntity& entity, ActorPatch* patch,
                     std::string* error) const;
    void ApplyPatch(const ActorPatch& patch);
    void ReconcileLocalPlayerFromActor();

    Options m_options;
    std::unordered_map<std::uint64_t, ReplicatedActor> m_actors;
    std::uint64_t m_lastAppliedTick = 0;
    std::uint64_t m_localPlayerId = 0;
    float m_localMaxSpeed = 0.0f;
    ecs::Position m_predictedPosition;
    bool m_hasPredictedPosition = false;
    bool m_needsFullSnapshot = false;
    Stats m_stats;
};

inline EntityReplicationClient::Options EntityReplicationClient::DefaultOptions() {
    return {};
}

}  // namespace zl::game
