#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <optional>
#include <unordered_map>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "ecs/EcsWorld.h"
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
inline constexpr std::uint32_t kReplicationVariant =
    net::MakeReplicationComponentId('Z', 'V', 'A', 'R');

bool RegisterEntityReplicationComponents(net::ReplicationRegistry& registry,
                                         std::string* error = nullptr);

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
    ecs::ZombieVariant variant;

    bool hasPosition = false;
    bool hasVelocity = false;
    bool hasHealth = false;
    bool hasSprite = false;
    bool hasFacing = false;
    bool hasAnim = false;
    bool hasItem = false;
    bool hasEquipment = false;
    bool hasAction = false;
    bool hasVariant = false;
};

class EntityReplicationProducer {
public:
    struct Options {
        float aoiRadius = 48.0f;
        std::size_t maxEntities = 256;
    };

    struct Stats {
        std::uint64_t fullSnapshots = 0;
        std::uint64_t deltaSnapshots = 0;
        std::uint64_t entitiesSent = 0;
        std::uint64_t entitiesRemoved = 0;
    };

    EntityReplicationProducer(ecs::EcsWorld& world,
                              const net::ReplicationRegistry& registry,
                              Options options = {});

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

    explicit EntityReplicationClient(Options options = {});

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
        ecs::ZombieVariant variant;
        bool hasPosition = false;
        bool hasVelocity = false;
        bool hasHealth = false;
        bool hasSprite = false;
        bool hasFacing = false;
        bool hasAnim = false;
        bool hasItem = false;
        bool hasEquipment = false;
        bool hasAction = false;
        bool hasVariant = false;
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

}  // namespace zl::game
