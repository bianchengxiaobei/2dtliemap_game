#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "characters/components/CombatComponents.h"
#include "ecs/EcsWorld.h"
#include "game/Settlement.h"

namespace zl::game {

inline constexpr std::uint64_t kSettlementStateStableId =
    0x4D32334634535441ull;
inline constexpr std::size_t kMaxSettlementStates = 32;
inline constexpr std::size_t kMaxSettlementStateIdBytes = 64;

// M23-F4-T2：聚落 NPC 的运行时活动。Standing 覆盖商人/求助者驻点与守卫站岗，
// Patrol 只给守卫；威胁触发时非战斗 NPC 统一 Shelter（撤到安全区中心）。
// M23-F4-T5：夜袭时 Defend 守卫主动迎战，Retreat 守卫与平民一起 Shelter。
enum class NpcActivity : std::uint8_t {
    Standing = 0,
    Patrol = 1,
    Rest = 2,
    Shelter = 3,
    Defend = 4,
};

const char* NpcActivityName(NpcActivity activity);
std::uint32_t NpcActivityColor(NpcActivity activity);

// 聚落 NPC 的日程/寻路状态。home 是实际生成的可走点（不是数据点原始值），
// target 只在 hasTarget 时有效。运行时字段留空默认值即可。
struct SettlementNpc {
    std::string settlementId;
    SettlementNpcKind kind = SettlementNpcKind::Guard;
    float homeX = 0.0f;
    float homeY = 0.0f;
    float wanderRadius = 2.0f;
    NpcActivity activity = NpcActivity::Rest;
    float targetX = 0.0f;
    float targetY = 0.0f;
    float waitSeconds = 0.0f;
    float retrySeconds = 0.0f;
    float attackCooldown = 0.0f;
    std::uint32_t patrolSerial = 0;
    bool hasTarget = false;
    bool settled = false;
    bool frozen = false;
};

// M23-F4-T5：夜袭感染者标记。实体仍使用通用 Position/Sprite/Health 与寻路组件，
// 但没有 AiState，由 SettlementNpcSystem 统一驱动，避免普通游荡 AI 破坏守城节奏。
struct SettlementRaidZombie {
    std::string settlementId;
    int nightKey = 0;
    float targetX = 0.0f;
    float targetY = 0.0f;
    float attackCooldown = 0.0f;
    float retrySeconds = 0.0f;
    bool hasTarget = false;
};

// M23-F4-T6：聚落持久状态。挂在固定 StableId 的世界实体上，单机/服务器共用；
// 守卫实体本身仍是临时实体，读档后按 guardHealth 重建。
struct SettlementStateEntry {
    std::string id;
    bool fallen = false;
    float guardHealth = -1.0f;
    int lastNightKey = std::numeric_limits<int>::min();
};

struct SettlementState {
    std::vector<SettlementStateEntry> settlements;
};

// 给已有实体（例如商人）挂上日程组件与寻路/移动所需组件；保留已有 Position/Sprite。
ecs::Entity ConfigureSettlementNpc(ecs::EcsWorld& world, ecs::Entity entity,
                                   std::string_view settlementId,
                                   SettlementNpcKind kind, float x, float y,
                                   float wanderRadius,
                                   float maxHealth = 0.0f);

// 生成一个无物理刚体的聚落 NPC；位置由调用方保证可走。
ecs::Entity SpawnSettlementNpc(ecs::EcsWorld& world, std::string_view settlementId,
                               SettlementNpcKind kind, float x, float y,
                               float wanderRadius, float maxHealth = 0.0f);

class SettlementNpcSystem {
public:
    using WalkableQuery = std::function<bool(int, int)>;
    using DamageCallback =
        std::function<bool(ecs::Entity, ecs::Entity, float, ecs::DamageType)>;

    struct Stats {
        int total = 0;
        int standing = 0;
        int patrol = 0;
        int rest = 0;
        int shelter = 0;
        int defending = 0;
        int frozen = 0;
        int raiders = 0;
        int threatenedSettlements = 0;
        int fallenSettlements = 0;
    };

    struct DefenseStatus {
        bool fallen = false;
        bool raidActive = false;
        float guardHealth = 0.0f;
        float guardHealthMax = 0.0f;
    };

    SettlementNpcSystem(const SettlementConfig* settlements,
                        WalkableQuery walkable = {});

    void SetFocus(float x, float y);
    void SetDamageCallback(DamageCallback callback);
    // 读档/重开世界时清空夜袭运行时；已发出的提示序号保持单调。
    void Reset();
    // M23-F4-T6：把守卫生命/失守/当晚进度写入世界状态组件；读档后由
    // ApplyState 恢复。夜袭中的感染者不入存档，active 不跨档恢复。
    void CaptureState(ecs::EcsWorld& world, SettlementState* out) const;
    void ApplyState(const SettlementState& state);
    bool ReadStatus(ecs::EcsWorld& world, std::string_view id,
                    DefenseStatus* out) const;
    // 读档重建守卫时使用；没有存档值时返回 fallback。
    float GuardHealthForSpawn(std::string_view id, float fallback) const;
    void ApplyGuardHealth(ecs::EcsWorld& world, ecs::Entity guard,
                          std::string_view id) const;
    // day 仅用于夜袭“每晚一次”判定；默认 1 保持旧测试/调用兼容。
    void Update(ecs::EcsWorld& world, float dt, int hour, int day = 1);
    const Stats& GetStats() const { return m_stats; }

    bool DefenseEnabled(std::string_view id) const;
    bool RaidActive(std::string_view id) const;
    bool IsFallen(std::string_view id) const;
    std::uint32_t NoticeSerial() const { return m_noticeSerial; }
    const std::string& NoticeText() const { return m_noticeText; }
    const std::string& NoticeTone() const { return m_noticeTone; }

private:
    struct DefenseRuntime {
        bool active = false;
        bool fallen = false;
        int lastNightKey = std::numeric_limits<int>::min();
        int nightKey = 0;
        int raidersSpawned = 0;
        int raidersKilled = 0;
        float elapsed = 0.0f;
    };

    const SettlementDef* FindSettlement(std::string_view id) const;
    NpcActivity ScheduledActivity(SettlementNpcKind kind, int hour) const;
    void RequestPathAt(ecs::EcsWorld& world, ecs::Entity entity, float x,
                       float y) const;
    void RequestPath(ecs::EcsWorld& world, ecs::Entity entity,
                     SettlementNpc& npc) const;
    void Stop(ecs::EcsWorld& world, ecs::Entity entity) const;
    bool ChooseTarget(const SettlementDef& def, const SettlementNpc& npc,
                      float px, float py, float* x, float* y);
    bool ChoosePatrolTarget(const SettlementNpc& npc, float px, float py,
                            float* x, float* y);
    bool ChooseSafeTarget(const SettlementDef& def, float* x, float* y) const;
    float ThreatRemaining(std::string_view id) const;

    const DefenseRuntime* FindRuntime(std::string_view id) const;
    DefenseRuntime& RuntimeFor(std::string_view id);
    void UpdateDefense(ecs::EcsWorld& world, float dt, int hour, int day);
    void StartRaid(ecs::EcsWorld& world, const SettlementDef& def,
                   DefenseRuntime& runtime, int nightKey);
    void UpdateRaiders(ecs::EcsWorld& world, const SettlementDef& def,
                       DefenseRuntime& runtime, float dt);
    void SpawnRaider(ecs::EcsWorld& world, const SettlementDef& def,
                     int nightKey, int index, int count);
    void FinishRaid(ecs::EcsWorld& world, const SettlementDef& def,
                    DefenseRuntime& runtime, bool fallen,
                    std::string_view reason);
    void Attack(ecs::EcsWorld& world, ecs::Entity source, ecs::Entity target,
                float damage, ecs::DamageType type);
    ecs::Entity FindNearestGuard(ecs::EcsWorld& world, std::string_view id,
                                 float x, float y) const;
    ecs::Entity FindNearestRaider(ecs::EcsWorld& world, std::string_view id,
                                  float x, float y) const;
    int CountGuards(ecs::EcsWorld& world, std::string_view id,
                    bool aliveOnly) const;
    int CountRaiders(ecs::EcsWorld& world, std::string_view id) const;
    void PostNotice(std::string text, std::string tone);

    const SettlementConfig* m_settlements = nullptr;
    WalkableQuery m_walkable;
    DamageCallback m_damage;
    float m_focusX = 0.0f;
    float m_focusY = 0.0f;
    std::unordered_map<std::string, float> m_threatLinger;
    std::unordered_map<std::string, DefenseRuntime> m_defense;
    std::unordered_map<std::string, float> m_savedGuardHealth;
    std::string m_noticeText;
    std::string m_noticeTone = "info";
    std::uint32_t m_noticeSerial = 0;
    Stats m_stats;
};

}  // namespace zl::game
