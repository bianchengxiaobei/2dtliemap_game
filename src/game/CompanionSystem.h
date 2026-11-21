#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "characters/components/CombatComponents.h"
#include "ecs/EcsWorld.h"
#include "game/Companion.h"

namespace zl::ecs {
struct Health;
}

namespace zl::core {
class Config;
}

namespace zl::items {
class ItemRegistry;
}

namespace zl::game {

struct TrapDefsConfig;

struct CompanionAiState {
    std::uint64_t targetId = 0;
    float anchorX = 0.0f;
    float anchorY = 0.0f;
    float homeX = 0.0f;
    float homeY = 0.0f;
    float attackCooldown = 0.0f;
    float repathTimer = 0.0f;
    bool hasAnchor = false;
    bool autoRetreat = false;
    bool frozen = false;
    bool engaged = false;
    CompanionCommand lastCommand = CompanionCommand::Follow;
};

bool IsCompanionHostileTarget(const ecs::EcsWorld& world, ecs::Entity entity);
bool CompanionNeedsRetreat(const ecs::Health& health, float threshold);

struct CompanionAttackProfile {
    float damage = 0.0f;
    float range = 0.0f;
    float cooldown = 0.0f;
    float wear = 0.0f;
};

class CompanionSystem {
public:
    using WalkableQuery = std::function<bool(int, int)>;
    using DamageCallback = std::function<bool(
        ecs::Entity source, ecs::Entity target, float damage,
        ecs::DamageType type)>;

    struct Options {
        float followDistance = 3.0f;
        float followMin = 1.8f;
        float followMax = 4.5f;
        float ownerEngageRadius = 6.0f;
        float ownerUrgentRadius = 3.5f;
        float leash = 12.0f;
        float freezeRadius = 40.0f;
        float attackRange = 1.35f;
        float attackDamage = 24.0f;
        float attackCooldown = 0.85f;
        float attackDamageScale = 0.38f;
        float attackCooldownScale = 1.20f;
        float attackKnockback = 1.1f;
        float attackStagger = 0.25f;
        float lowHealthFraction = 0.30f;
        float recoverHealthFraction = 0.50f;
        float stayEngageRadius = 4.0f;
        float stayLeash = 4.5f;
        float trapClearance = 0.8f;
        float pathRetrySeconds = 0.35f;
        float downedSeconds = 60.0f;
        float rescueHealthFraction = 0.35f;
        float rescueRange = 2.5f;
        int combatKillTrust = 2;
    };

    struct Stats {
        int follow = 0;
        int stay = 0;
        int retreat = 0;
        int frozen = 0;
        int standby = 0;
        int engaged = 0;
        int downed = 0;
        int deaths = 0;
        int kills = 0;
        std::uint64_t attacks = 0;
        std::uint64_t targetsAcquired = 0;
    };

    static Options DefaultOptions();
    explicit CompanionSystem(WalkableQuery walkable = {},
                             DamageCallback damage = {}, Options options = DefaultOptions());

    void Update(ecs::EcsWorld& world, float dt);
    void SetItemRegistry(const items::ItemRegistry* items) { m_items = items; }
    void SetCompanionConfig(const CompanionConfig* companions) {
        m_companions = companions;
    }
    void SetTrapDefs(const TrapDefsConfig* traps) { m_traps = traps; }

    // 救援倒地同伴：仅当同伴已倒地、救援者在范围内且未死亡时成功。
    bool TryRescue(ecs::EcsWorld& world, ecs::Entity companion,
                   ecs::Entity rescuer, std::string* error = nullptr);

    const Options& GetOptions() const { return m_options; }
    const Stats& GetStats() const { return m_stats; }

private:
    bool HasLineOfSight(ecs::EcsWorld& world, float sx, float sy, float tx,
                        float ty) const;
    bool TrapNear(ecs::EcsWorld& world, float x, float y, float clearance) const;
    bool FindSafeGoal(ecs::EcsWorld& world, float x, float y, int* gx,
                      int* gy, float clearance) const;
    void Stop(ecs::EcsWorld& world, ecs::Entity entity) const;
    bool RequestPath(ecs::EcsWorld& world, ecs::Entity entity,
                     CompanionAiState& ai, float x, float y) const;
    float OwnerFacingX(const ecs::EcsWorld& world, ecs::Entity owner) const;
    float OwnerFacingY(const ecs::EcsWorld& world, ecs::Entity owner) const;
    void FollowGoal(ecs::EcsWorld& world, ecs::Entity companion,
                    ecs::Entity owner, float* x, float* y) const;
    bool OwnerAttacking(const ecs::EcsWorld& world, ecs::Entity owner) const;
    ecs::Entity FindThreat(ecs::EcsWorld& world, ecs::Entity companion,
                           float originX, float originY, float radius,
                           float companionRadius) const;
    bool TargetValid(ecs::EcsWorld& world, ecs::Entity target,
                     float originX, float originY, float leash) const;
    void UpdateFollow(ecs::EcsWorld& world, ecs::Entity companion,
                      ecs::Entity owner, CompanionState& state,
                      CompanionAiState& ai, const ecs::Position& position,
                      float dt);
    void UpdateStay(ecs::EcsWorld& world, ecs::Entity companion,
                    CompanionState& state, CompanionAiState& ai,
                    const ecs::Position& position, float dt);
    void UpdateRetreat(ecs::EcsWorld& world, ecs::Entity companion,
                       ecs::Entity owner, CompanionState& state,
                       CompanionAiState& ai, const ecs::Position& position);
    bool UpdateCombat(ecs::EcsWorld& world, ecs::Entity companion,
                      CompanionState& state, CompanionAiState& ai,
                      const ecs::Position& position, float originX,
                      float originY, float leash, float dt);
    void Attack(ecs::EcsWorld& world, ecs::Entity companion,
                CompanionState& state, ecs::Entity target,
                const ecs::Position& position);
    void EnterDowned(ecs::EcsWorld& world, ecs::Entity companion,
                     CompanionState& state, CompanionAiState& ai);
    void KillCompanion(ecs::EcsWorld& world, ecs::Entity companion,
                       CompanionState& state, CompanionAiState& ai);

    WalkableQuery m_walkable;
    DamageCallback m_damage;
    const items::ItemRegistry* m_items = nullptr;
    const CompanionConfig* m_companions = nullptr;
    const TrapDefsConfig* m_traps = nullptr;
    Options m_options;
    Stats m_stats;
};

inline CompanionSystem::Options CompanionSystem::DefaultOptions() { return {}; }

CompanionSystem::Options LoadCompanionSystemOptions(const core::Config& config);
CompanionAttackProfile CompanionAttackProfileFor(
    const CompanionState& state, const items::ItemRegistry* items,
    const CompanionSystem::Options& options);

}  // namespace zl::game
