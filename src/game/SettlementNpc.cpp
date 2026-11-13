#include "game/SettlementNpc.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ai/Passability.h"
#include "ai/components/AiComponents.h"
#include "characters/components/AnimalComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/systems/AnimationSystem.h"
#include "core/Log.h"
#include "ecs/components/CommonComponents.h"
#include "game/Homestead.h"

namespace zl::game {
namespace {

constexpr float kSimulationRadius = 36.0f;
constexpr float kThreatPadding = 4.0f;
constexpr float kThreatLingerSeconds = 4.0f;
constexpr float kArriveRadius = 0.7f;
constexpr float kRetrySeconds = 1.5f;
constexpr float kPatrolWaitMin = 2.5f;
constexpr float kPatrolWaitMax = 5.5f;
constexpr float kTwoPi = 6.2831853f;
constexpr float kDefendRetargetSq = 0.75f * 0.75f;
constexpr float kDefendAttackCooldown = 0.35f;
constexpr const char* kNpcSprite = "character.placeholder";
constexpr const char* kRaiderSprite = "character.zombie";

std::uint32_t KindTint(SettlementNpcKind kind) {
    switch (kind) {
        case SettlementNpcKind::Merchant:
            return 0xFFD8A0FFu;
        case SettlementNpcKind::Guard:
            return 0xFF90C8FFu;
        case SettlementNpcKind::Requester:
            return 0xFFB0FFB0u;
    }
    return 0xFFFFFFFFu;
}

float Rand01(std::uint64_t stableId, std::uint64_t salt) {
    std::uint64_t h = stableId * 0x9E3779B97F4A7C15ull +
                      salt * 0xBF58476D1CE4E5B9ull + 0x94D049BB133111EBull;
    h ^= h >> 30;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 27;
    h *= 0x94D049BB133111EBull;
    h ^= h >> 31;
    return static_cast<float>(h >> 11) * (1.0f / 9007199254740992.0f);
}

float DistSq(float ax, float ay, float bx, float by) {
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

bool FinitePosition(float x, float y) {
    return std::isfinite(x) && std::isfinite(y);
}

bool InNightWindow(int hour, const SettlementDefenseConfig& defense) {
    const int clamped = std::clamp(hour, 0, 23);
    if (defense.nightStartHour < defense.nightEndHour) {
        return clamped >= defense.nightStartHour &&
               clamped < defense.nightEndHour;
    }
    return clamped >= defense.nightStartHour ||
           clamped < defense.nightEndHour;
}

int NightKeyFor(int day, int hour, const SettlementDefenseConfig& defense) {
    if (defense.nightStartHour < defense.nightEndHour) {
        return day;
    }
    return hour < defense.nightEndHour ? day - 1 : day;
}

void FaceTowards(entt::registry& registry, ecs::Entity entity, float dx,
                 float dy) {
    if (auto* facing = registry.try_get<ecs::Facing>(entity);
        facing != nullptr) {
        facing->dir = characters::AnimationSystem::FacingFromVelocity(dx, dy);
    }
}

}  // namespace

const char* NpcActivityName(NpcActivity activity) {
    switch (activity) {
        case NpcActivity::Standing:
            return "standing";
        case NpcActivity::Patrol:
            return "patrol";
        case NpcActivity::Rest:
            return "rest";
        case NpcActivity::Shelter:
            return "shelter";
        case NpcActivity::Defend:
            return "defend";
    }
    return "rest";
}

std::uint32_t NpcActivityColor(NpcActivity activity) {
    switch (activity) {
        case NpcActivity::Standing:
            return 0xFFB0D0FFu;
        case NpcActivity::Patrol:
            return 0xFFD060FFu;
        case NpcActivity::Rest:
            return 0xC080FFFFu;
        case NpcActivity::Shelter:
            return 0xFF5060FFu;
        case NpcActivity::Defend:
            return 0xFF3060FFu;
    }
    return 0xFFFFFFFFu;
}

ecs::Entity ConfigureSettlementNpc(ecs::EcsWorld& world, ecs::Entity entity,
                                   std::string_view settlementId,
                                   SettlementNpcKind kind, float x, float y,
                                   float wanderRadius, float maxHealth) {
    if (!world.Alive(entity) || !FinitePosition(x, y)) {
        return ecs::EcsWorld::kNull;
    }
    entt::registry& registry = world.Registry();
    if (auto* position = registry.try_get<ecs::Position>(entity);
        position != nullptr) {
        position->x = x;
        position->y = y;
        position->z = 0;
    } else {
        registry.emplace<ecs::Position>(entity, ecs::Position{x, y, 0});
    }
    if (registry.try_get<ecs::Velocity>(entity) == nullptr) {
        registry.emplace<ecs::Velocity>(entity, ecs::Velocity{});
    }
    if (auto* sprite = registry.try_get<ecs::SpriteRef>(entity);
        sprite != nullptr) {
        sprite->tint = KindTint(kind);
    } else {
        registry.emplace<ecs::SpriteRef>(
            entity, ecs::SpriteRef{ecs::HashName(kNpcSprite), 0,
                                   KindTint(kind)});
    }
    if (registry.try_get<ecs::Facing>(entity) == nullptr) {
        registry.emplace<ecs::Facing>(entity, ecs::Facing{});
    }
    if (registry.try_get<ecs::AnimState>(entity) == nullptr) {
        registry.emplace<ecs::AnimState>(entity, ecs::AnimState{});
    }
    if (registry.try_get<ecs::ActionState>(entity) == nullptr) {
        registry.emplace<ecs::ActionState>(entity, ecs::ActionState{});
    }
    if (registry.try_get<ecs::MoveIntent>(entity) == nullptr) {
        registry.emplace<ecs::MoveIntent>(entity, ecs::MoveIntent{});
    }
    if (registry.try_get<ecs::PathRequest>(entity) == nullptr) {
        registry.emplace<ecs::PathRequest>(entity, ecs::PathRequest{});
    }
    if (registry.try_get<ecs::PathFollow>(entity) == nullptr) {
        ecs::PathFollow follow;
        follow.speed = 2.4f;
        registry.emplace<ecs::PathFollow>(entity, follow);
    } else {
        registry.get<ecs::PathFollow>(entity).speed = 2.4f;
    }
    registry.emplace_or_replace<ecs::NoCombatTarget>(entity);
    registry.emplace_or_replace<ecs::FriendlyNpc>(entity);

    if (kind == SettlementNpcKind::Guard) {
        ecs::Health& health = registry.get_or_emplace<ecs::Health>(entity);
        const float hp = maxHealth > 0.0f ? maxHealth : 100.0f;
        health.max = hp;
        health.current = hp;
    }

    auto* npc = registry.try_get<SettlementNpc>(entity);
    if (npc == nullptr) {
        npc = &registry.emplace<SettlementNpc>(entity);
        npc->activity = NpcActivity::Rest;
    }
    npc->settlementId = std::string(settlementId);
    npc->kind = kind;
    npc->homeX = x;
    npc->homeY = y;
    npc->wanderRadius = std::clamp(wanderRadius, 0.0f, 16.0f);
    npc->targetX = x;
    npc->targetY = y;
    npc->attackCooldown = 0.0f;
    return entity;
}

ecs::Entity SpawnSettlementNpc(ecs::EcsWorld& world,
                               std::string_view settlementId,
                               SettlementNpcKind kind, float x, float y,
                               float wanderRadius, float maxHealth) {
    if (!FinitePosition(x, y)) {
        return ecs::EcsWorld::kNull;
    }
    const ecs::Entity entity = world.Create();
    if (entity == ecs::EcsWorld::kNull) {
        return ecs::EcsWorld::kNull;
    }
    return ConfigureSettlementNpc(world, entity, settlementId, kind, x, y,
                                  wanderRadius, maxHealth);
}

SettlementNpcSystem::SettlementNpcSystem(const SettlementConfig* settlements,
                                         WalkableQuery walkable)
    : m_settlements(settlements), m_walkable(std::move(walkable)) {}

SettlementNpcSystem::PopulationStats SettlementNpcSystem::EnsurePopulation(
    ecs::EcsWorld& world, bool spawnMerchants,
    const WalkableQuery& walkable) {
    PopulationStats stats;
    if (m_settlements == nullptr || !m_settlements->enabled ||
        m_settlements->defs.empty()) {
        return stats;
    }
    WalkableQuery canWalk = walkable ? walkable : m_walkable;
    if (!canWalk) {
        canWalk = [](int, int) { return true; };
    }
    entt::registry& registry = world.Registry();
    struct Counts {
        int merchants = 0;
        int guards = 0;
        int requesters = 0;
    };
    std::unordered_map<std::string, Counts> counts;
    registry.view<SettlementNpc>().each(
        [&](const ecs::Entity entity, const SettlementNpc& npc) {
            if (!world.Alive(entity)) {
                return;
            }
            Counts& count = counts[npc.settlementId];
            if (npc.kind == SettlementNpcKind::Merchant) {
                ++count.merchants;
            } else if (npc.kind == SettlementNpcKind::Guard) {
                ++count.guards;
            } else if (npc.kind == SettlementNpcKind::Requester) {
                ++count.requesters;
            }
        });

    std::vector<std::pair<ecs::Entity, ecs::Position>> legacyMerchants;
    registry.view<ecs::MerchantStock, ecs::Position>().each(
        [&](const ecs::Entity entity, const ecs::MerchantStock&,
            const ecs::Position& position) {
            if (!world.Alive(entity) ||
                registry.all_of<SettlementNpc>(entity)) {
                return;
            }
            legacyMerchants.emplace_back(entity, position);
        });
    for (const auto& [entity, position] : legacyMerchants) {
        const SettlementDef* nearest = nullptr;
        float nearestDistance = std::numeric_limits<float>::max();
        for (const SettlementDef& def : m_settlements->defs) {
            if (IsFallen(def.id) || counts[def.id].merchants > 0) {
                continue;
            }
            const float dx = position.x - def.centerX;
            const float dy = position.y - def.centerY;
            const float distance = dx * dx + dy * dy;
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = &def;
            }
        }
        if (nearest == nullptr) {
            continue;
        }
        const SettlementNpcPoint* point =
            FindSettlementNpc(*nearest, SettlementNpcKind::Merchant);
        ConfigureSettlementNpc(
            world, entity, nearest->id, SettlementNpcKind::Merchant,
            position.x, position.y,
            point != nullptr ? point->wanderRadius : 1.5f);
        ++counts[nearest->id].merchants;
    }

    const auto spawnAt = [&](const SettlementDef& def, SettlementNpcKind kind,
                             float x, float y, float wanderRadius) {
        int wx = static_cast<int>(std::floor(x));
        int wy = static_cast<int>(std::floor(y));
        if (!ai::FindNearestWalkable(wx, wy, 6, canWalk, &wx, &wy)) {
            return ecs::EcsWorld::kNull;
        }
        const float spawnX = static_cast<float>(wx) + 0.5f;
        const float spawnY = static_cast<float>(wy) + 0.5f;
        if (!IsInsideSettlement(def, spawnX, spawnY)) {
            return ecs::EcsWorld::kNull;
        }
        if (kind == SettlementNpcKind::Merchant) {
            const ecs::Entity merchant =
                SpawnMerchant(world, spawnX, spawnY, 0);
            if (merchant == ecs::EcsWorld::kNull) {
                return ecs::EcsWorld::kNull;
            }
            ConfigureSettlementNpc(world, merchant, def.id, kind, spawnX,
                                   spawnY, wanderRadius);
            if (auto* stock =
                    registry.try_get<ecs::MerchantStock>(merchant);
                stock != nullptr) {
                stock->stock = def.merchantStock;
                stock->restockDay = 0;
            }
            ZL_INFO("settlement", "聚落商人已就位：{}（{}，格 {},{}）",
                    def.name, def.id, wx, wy);
            return merchant;
        }
        const ecs::Entity npc = SpawnSettlementNpc(
            world, def.id, kind, spawnX, spawnY, wanderRadius,
            kind == SettlementNpcKind::Guard ? def.defense.guardHealth
                                             : 0.0f);
        if (npc == ecs::EcsWorld::kNull) {
            return ecs::EcsWorld::kNull;
        }
        if (kind == SettlementNpcKind::Guard) {
            ApplyGuardHealth(world, npc, def.id);
        }
        ZL_INFO("settlement", "聚落 NPC 已就位：{}（{}，格 {},{}）",
                SettlementNpcKindName(kind), def.id, wx, wy);
        return npc;
    };

    for (const SettlementDef& def : m_settlements->defs) {
        if (IsFallen(def.id)) {
            continue;
        }
        Counts& count = counts[def.id];
        if (!m_populationMerchants.contains(def.id)) {
            if (count.merchants > 0) {
                m_populationMerchants.insert(def.id);
            } else if (spawnMerchants) {
                const SettlementNpcPoint* point =
                    FindSettlementNpc(def, SettlementNpcKind::Merchant);
                const float merchantX =
                    point != nullptr ? point->x : def.centerX;
                const float merchantY =
                    point != nullptr ? point->y : def.centerY;
                if (spawnAt(def, SettlementNpcKind::Merchant, merchantX,
                            merchantY,
                            point != nullptr ? point->wanderRadius : 1.5f) !=
                    ecs::EcsWorld::kNull) {
                    ++count.merchants;
                    ++stats.merchants;
                    m_populationMerchants.insert(def.id);
                }
            }
        }

        const auto ensureKind = [&](SettlementNpcKind kind,
                                    std::unordered_set<std::string>& ensured,
                                    int& populated, int& spawned) {
            if (ensured.contains(def.id)) {
                return;
            }
            int target = 0;
            for (const SettlementNpcPoint& point : def.npcPoints) {
                target += point.kind == kind ? 1 : 0;
            }
            if (populated >= target) {
                ensured.insert(def.id);
                return;
            }
            for (const SettlementNpcPoint& point : def.npcPoints) {
                if (populated >= target) {
                    break;
                }
                if (point.kind != kind) {
                    continue;
                }
                if (spawnAt(def, kind, point.x, point.y,
                            point.wanderRadius) != ecs::EcsWorld::kNull) {
                    ++populated;
                    ++spawned;
                }
            }
            if (populated >= target) {
                ensured.insert(def.id);
            }
        };
        ensureKind(SettlementNpcKind::Guard, m_populationGuards, count.guards,
                   stats.guards);
        ensureKind(SettlementNpcKind::Requester, m_populationRequesters,
                   count.requesters, stats.requesters);
    }
    return stats;
}

void SettlementNpcSystem::SetFocus(float x, float y) {
    if (FinitePosition(x, y)) {
        m_focusX = x;
        m_focusY = y;
    }
}

void SettlementNpcSystem::SetDamageCallback(DamageCallback callback) {
    m_damage = std::move(callback);
}

void SettlementNpcSystem::Reset() {
    m_threatLinger.clear();
    m_defense.clear();
    m_savedGuardHealth.clear();
    m_populationMerchants.clear();
    m_populationGuards.clear();
    m_populationRequesters.clear();
    m_stats = {};
}

void SettlementNpcSystem::CaptureState(ecs::EcsWorld& world,
                                       SettlementState* out) const {
    if (out == nullptr) {
        return;
    }
    out->settlements.clear();
    if (m_settlements == nullptr) {
        return;
    }
    entt::registry& registry = world.Registry();
    for (const SettlementDef& def : m_settlements->defs) {
        if (out->settlements.size() >= kMaxSettlementStates) {
            break;
        }
        SettlementStateEntry entry;
        entry.id = def.id;
        const DefenseRuntime* runtime = FindRuntime(def.id);
        entry.fallen = runtime != nullptr && runtime->fallen;
        entry.lastNightKey =
            runtime != nullptr ? runtime->lastNightKey
                               : std::numeric_limits<int>::min();

        bool foundGuard = false;
        float guardHealth = -1.0f;
        registry.view<SettlementNpc, ecs::Health>().each(
            [&](const ecs::Entity, const SettlementNpc& npc,
                const ecs::Health& health) {
                if (!foundGuard && npc.settlementId == def.id &&
                    npc.kind == SettlementNpcKind::Guard) {
                    foundGuard = true;
                    guardHealth = std::max(0.0f, health.current);
                }
            });
        if (!foundGuard) {
            const auto it = m_savedGuardHealth.find(def.id);
            if (it != m_savedGuardHealth.end()) {
                guardHealth = it->second;
            }
        }
        if (entry.fallen) {
            guardHealth = 0.0f;
        } else if (foundGuard && guardHealth <= 0.0f) {
            entry.fallen = true;
        }
        entry.guardHealth = guardHealth;
        out->settlements.push_back(std::move(entry));
    }
}

void SettlementNpcSystem::ApplyState(const SettlementState& state) {
    m_threatLinger.clear();
    m_defense.clear();
    m_savedGuardHealth.clear();
    m_stats = {};
    if (m_settlements == nullptr) {
        return;
    }
    std::unordered_set<std::string> seen;
    for (const SettlementStateEntry& entry : state.settlements) {
        const SettlementDef* def = FindSettlement(entry.id);
        if (def == nullptr || !seen.insert(entry.id).second) {
            continue;
        }
        DefenseRuntime& runtime = RuntimeFor(entry.id);
        runtime.active = false;
        runtime.external = false;
        runtime.reclaim = false;
        runtime.fallen = entry.fallen ||
                         (entry.guardHealth >= 0.0f &&
                          entry.guardHealth <= 0.0f);
        runtime.lastNightKey = entry.lastNightKey;
        runtime.nightKey = 0;
        runtime.raidersSpawned = 0;
        runtime.raidersKilled = 0;
        runtime.elapsed = 0.0f;
        if (entry.guardHealth >= 0.0f) {
            m_savedGuardHealth[entry.id] = entry.guardHealth;
        }
    }
}

bool SettlementNpcSystem::ReadStatus(ecs::EcsWorld& world,
                                     std::string_view id,
                                     DefenseStatus* out) const {
    const SettlementDef* def = FindSettlement(id);
    if (def == nullptr || out == nullptr) {
        return false;
    }
    DefenseStatus status;
    status.guardHealthMax =
        def->defense.enabled ? def->defense.guardHealth : 0.0f;
    const DefenseRuntime* runtime = FindRuntime(id);
    status.fallen = runtime != nullptr && runtime->fallen;
    status.raidActive = runtime != nullptr && runtime->active;
    entt::registry& registry = world.Registry();
    registry.view<SettlementNpc, ecs::Health>().each(
        [&](const ecs::Entity, const SettlementNpc& npc,
            const ecs::Health& health) {
            if (npc.settlementId != id ||
                npc.kind != SettlementNpcKind::Guard) {
                return;
            }
            status.guardHealth = std::max(0.0f, health.current);
            status.guardHealthMax = std::max(0.0f, health.max);
        });
    if (status.fallen) {
        status.guardHealth = 0.0f;
    }
    *out = status;
    return true;
}

float SettlementNpcSystem::GuardHealthForSpawn(std::string_view id,
                                               float fallback) const {
    const auto it = m_savedGuardHealth.find(std::string(id));
    if (it == m_savedGuardHealth.end() || it->second < 0.0f) {
        return fallback;
    }
    return it->second;
}

void SettlementNpcSystem::ApplyGuardHealth(ecs::EcsWorld& world,
                                           ecs::Entity guard,
                                           std::string_view id) const {
    if (!world.Alive(guard)) {
        return;
    }
    const auto it = m_savedGuardHealth.find(std::string(id));
    if (it == m_savedGuardHealth.end() || it->second < 0.0f) {
        return;
    }
    entt::registry& registry = world.Registry();
    auto* health = registry.try_get<ecs::Health>(guard);
    if (health != nullptr) {
        health->current = std::clamp(it->second, 0.0f, health->max);
    }
}

const SettlementDef* SettlementNpcSystem::FindSettlement(
    std::string_view id) const {
    return m_settlements != nullptr ? m_settlements->Find(id) : nullptr;
}

const SettlementNpcSystem::DefenseRuntime* SettlementNpcSystem::FindRuntime(
    std::string_view id) const {
    const auto it = m_defense.find(std::string(id));
    return it != m_defense.end() ? &it->second : nullptr;
}

SettlementNpcSystem::DefenseRuntime& SettlementNpcSystem::RuntimeFor(
    std::string_view id) {
    return m_defense[std::string(id)];
}

bool SettlementNpcSystem::DefenseEnabled(std::string_view id) const {
    const SettlementDef* def = FindSettlement(id);
    return def != nullptr && SettlementProvidesDefense(*def);
}

bool SettlementNpcSystem::RaidActive(std::string_view id) const {
    const DefenseRuntime* runtime = FindRuntime(id);
    return runtime != nullptr && runtime->active;
}

bool SettlementNpcSystem::IsFallen(std::string_view id) const {
    const DefenseRuntime* runtime = FindRuntime(id);
    return runtime != nullptr && runtime->fallen;
}

bool SettlementNpcSystem::BeginExternalDefense(ecs::EcsWorld& world,
                                               std::string_view id) {
    const SettlementDef* def = FindSettlement(id);
    if (def == nullptr || !SettlementProvidesDefense(*def)) {
        return false;
    }
    DefenseRuntime& runtime = RuntimeFor(def->id);
    if (runtime.fallen) {
        return false;
    }
    if (runtime.active && runtime.external) {
        return true;
    }
    if (runtime.active) {
        FinishRaid(world, *def, runtime, false, "战略围攻接管防守");
    }
    runtime.active = true;
    runtime.external = true;
    runtime.reclaim = false;
    runtime.faction = ContestedFaction::Infected;
    runtime.nightKey = 0;
    runtime.raidersSpawned = 0;
    runtime.raidersKilled = 0;
    runtime.elapsed = 0.0f;
    entt::registry& registry = world.Registry();
    registry.view<SettlementNpc, ecs::Health>().each(
        [&](const ecs::Entity entity, const SettlementNpc& npc,
            const ecs::Health& health) {
            if (npc.settlementId != def->id ||
                npc.kind != SettlementNpcKind::Guard ||
                health.current <= 0.0f) {
                return;
            }
            registry.remove<ecs::NoCombatTarget>(entity);
            if (auto* component = registry.try_get<SettlementNpc>(entity);
                component != nullptr) {
                component->attackCooldown = 0.0f;
                component->hasTarget = false;
                component->settled = false;
            }
        });
    PostNotice(def->name + "遭到围攻：守卫迎战，平民撤离", "fail");
    return true;
}

int SettlementNpcSystem::SpawnExternalWave(ecs::EcsWorld& world,
                                           std::string_view id, int count,
                                           int waveIndex,
                                           ContestedFaction faction) {
    const SettlementDef* def = FindSettlement(id);
    DefenseRuntime* runtime = nullptr;
    if (def == nullptr || count <= 0) {
        return 0;
    }
    const auto found = m_defense.find(std::string(id));
    if (found == m_defense.end() || !found->second.active ||
        !found->second.external) {
        return 0;
    }
    runtime = &found->second;
    runtime->faction = faction;
    const int before = CountRaiders(world, def->id);
    const int safeCount = std::clamp(count, 0, 32);
    for (int i = 0; i < safeCount; ++i) {
        SpawnRaider(world, *def, std::max(1, waveIndex + 1), i, safeCount,
                    faction);
    }
    const int spawned = CountRaiders(world, def->id) - before;
    runtime->raidersSpawned += std::max(0, spawned);
    return std::max(0, spawned);
}

int SettlementNpcSystem::LiveGuards(ecs::EcsWorld& world,
                                    std::string_view id) const {
    return CountGuards(world, id, true);
}

int SettlementNpcSystem::LiveRaiders(ecs::EcsWorld& world,
                                     std::string_view id) const {
    return CountRaiders(world, id);
}

int SettlementNpcSystem::RaidersWithin(ecs::EcsWorld& world,
                                       std::string_view id, float x, float y,
                                       float radius) const {
    int count = 0;
    const float radiusSq = std::max(0.0f, radius) * std::max(0.0f, radius);
    world.Registry()
        .view<SettlementRaidZombie, ecs::Position, ecs::Health>()
        .each([&](const ecs::Entity entity, const SettlementRaidZombie& raid,
                  const ecs::Position& position, const ecs::Health& health) {
            if (raid.settlementId != id || health.current <= 0.0f ||
                !world.Alive(entity)) {
                return;
            }
            const float dx = position.x - x;
            const float dy = position.y - y;
            if (dx * dx + dy * dy <= radiusSq) {
                ++count;
            }
        });
    return count;
}

void SettlementNpcSystem::UpdateExternalDefense(ecs::EcsWorld& world,
                                                std::string_view id,
                                                float dt) {
    const SettlementDef* def = FindSettlement(id);
    if (def == nullptr || dt <= 0.0f) {
        return;
    }
    const auto it = m_defense.find(std::string(id));
    if (it == m_defense.end() || !it->second.active ||
        !it->second.external) {
        return;
    }
    UpdateRaiders(world, *def, it->second, dt);
}

void SettlementNpcSystem::EndExternalDefense(ecs::EcsWorld& world,
                                             std::string_view id, bool fallen,
                                             std::string_view reason) {
    const SettlementDef* def = FindSettlement(id);
    if (def == nullptr) {
        return;
    }
    const auto it = m_defense.find(std::string(id));
    if (it == m_defense.end() || !it->second.active) {
        return;
    }
    it->second.external = false;
    FinishRaid(world, *def, it->second, fallen, reason);
}

void SettlementNpcSystem::MarkFallen(ecs::EcsWorld& world,
                                     std::string_view id) {
    const SettlementDef* def = FindSettlement(id);
    if (def == nullptr || !def->defense.enabled) {
        return;
    }
    DefenseRuntime& runtime = RuntimeFor(def->id);
    const bool wasFallen = runtime.fallen;
    runtime.active = false;
    runtime.external = false;
    runtime.reclaim = false;
    runtime.fallen = true;
    runtime.nightKey = 0;
    m_savedGuardHealth[def->id] = 0.0f;
    m_threatLinger.erase(def->id);

    entt::registry& registry = world.Registry();
    std::vector<ecs::Entity> raiders;
    registry.view<SettlementRaidZombie>().each(
        [&](const ecs::Entity entity, const SettlementRaidZombie& raid) {
            if (raid.settlementId == def->id) {
                raiders.push_back(entity);
            }
        });
    for (const ecs::Entity raider : raiders) {
        world.Destroy(raider);
    }
    std::vector<ecs::Entity> civilians;
    registry.view<SettlementNpc>().each(
        [&](const ecs::Entity entity, const SettlementNpc& npc) {
            if (npc.settlementId == def->id &&
                npc.kind != SettlementNpcKind::Merchant) {
                civilians.push_back(entity);
            }
        });
    for (const ecs::Entity npc : civilians) {
        world.Destroy(npc);
    }
    if (!wasFallen) {
        PostNotice(def->name + "失守：平民撤离，交易与委托暂停", "fail");
    }
}

bool SettlementNpcSystem::BeginExternalReclaim(ecs::EcsWorld& world,
                                               std::string_view id) {
    const SettlementDef* def = FindSettlement(id);
    if (def == nullptr || !def->defense.enabled) {
        return false;
    }
    DefenseRuntime& runtime = RuntimeFor(def->id);
    if (!runtime.fallen) {
        return false;
    }
    if (runtime.active && runtime.external && runtime.reclaim) {
        return true;
    }
    if (runtime.active) {
        FinishRaid(world, *def, runtime, false, "反攻波次接管聚落");
    }
    runtime.active = true;
    runtime.external = true;
    runtime.reclaim = true;
    runtime.faction = ContestedFaction::Infected;
    runtime.nightKey = 0;
    runtime.raidersSpawned = 0;
    runtime.raidersKilled = 0;
    runtime.elapsed = 0.0f;
    PostNotice(def->name + "反攻：清理感染核心后守住波次", "warn");
    return true;
}

void SettlementNpcSystem::EndExternalReclaim(ecs::EcsWorld& world,
                                             std::string_view id, bool held,
                                             std::string_view reason) {
    const SettlementDef* def = FindSettlement(id);
    if (def == nullptr) {
        return;
    }
    const auto it = m_defense.find(std::string(id));
    if (it == m_defense.end() || !it->second.active ||
        !it->second.external) {
        return;
    }
    it->second.external = false;
    it->second.reclaim = false;
    FinishRaid(world, *def, it->second, !held, reason);
}

void SettlementNpcSystem::RestoreSettlement(ecs::EcsWorld& world,
                                            std::string_view id) {
    const SettlementDef* def = FindSettlement(id);
    if (def == nullptr || !def->defense.enabled) {
        return;
    }
    DefenseRuntime& runtime = RuntimeFor(def->id);
    if (!runtime.fallen) {
        return;
    }
    runtime.active = false;
    runtime.external = false;
    runtime.reclaim = false;
    runtime.fallen = false;
    runtime.nightKey = 0;
    runtime.raidersSpawned = 0;
    runtime.raidersKilled = 0;
    runtime.elapsed = 0.0f;
    m_savedGuardHealth.erase(def->id);
    m_threatLinger.erase(def->id);
    m_populationMerchants.erase(def->id);
    m_populationGuards.erase(def->id);
    m_populationRequesters.erase(def->id);

    entt::registry& registry = world.Registry();
    std::vector<ecs::Entity> stale;
    registry.view<SettlementNpc>().each(
        [&](const ecs::Entity entity, const SettlementNpc& npc) {
            if (npc.settlementId == def->id &&
                npc.kind != SettlementNpcKind::Merchant) {
                stale.push_back(entity);
            }
        });
    for (const ecs::Entity npc : stale) {
        world.Destroy(npc);
    }
    PostNotice(def->name + "恢复：守卫与平民正在返回", "ok");
}

void SettlementNpcSystem::PostNotice(std::string text, std::string tone) {
    m_noticeText = std::move(text);
    m_noticeTone = std::move(tone);
    ++m_noticeSerial;
    ZL_INFO("settlement", "{}", m_noticeText);
}

NpcActivity SettlementNpcSystem::ScheduledActivity(SettlementNpcKind kind,
                                                   int hour) const {
    const int clamped = std::clamp(hour, 0, 23);
    switch (kind) {
        case SettlementNpcKind::Guard:
            if (clamped >= 8 && clamped < 18) {
                return NpcActivity::Patrol;
            }
            if (clamped >= 18 && clamped < 22) {
                return NpcActivity::Standing;
            }
            return NpcActivity::Rest;
        case SettlementNpcKind::Merchant:
        case SettlementNpcKind::Requester:
            return clamped >= 8 && clamped < 20 ? NpcActivity::Standing
                                                : NpcActivity::Rest;
    }
    return NpcActivity::Rest;
}

float SettlementNpcSystem::ThreatRemaining(std::string_view id) const {
    const auto it = m_threatLinger.find(std::string(id));
    return it != m_threatLinger.end() ? it->second : 0.0f;
}

void SettlementNpcSystem::Stop(ecs::EcsWorld& world,
                               ecs::Entity entity) const {
    entt::registry& registry = world.Registry();
    if (auto* follow = registry.try_get<ecs::PathFollow>(entity);
        follow != nullptr) {
        follow->status = ecs::PathStatus::None;
        follow->waypoints.clear();
        follow->index = 0;
    }
    if (auto* request = registry.try_get<ecs::PathRequest>(entity);
        request != nullptr) {
        request->state = ecs::PathStatus::None;
        request->queryId = 0;
        request->newRequest = false;
    }
    if (auto* intent = registry.try_get<ecs::MoveIntent>(entity);
        intent != nullptr) {
        intent->x = 0.0f;
        intent->y = 0.0f;
    }
}

void SettlementNpcSystem::RequestPathAt(ecs::EcsWorld& world,
                                        ecs::Entity entity, float x,
                                        float y) const {
    entt::registry& registry = world.Registry();
    auto* request = registry.try_get<ecs::PathRequest>(entity);
    if (request == nullptr) {
        request = &registry.emplace<ecs::PathRequest>(entity);
    }
    request->goalX = static_cast<int>(std::floor(x));
    request->goalY = static_cast<int>(std::floor(y));
    request->newRequest = true;
    request->queryId = 0;
    request->state = ecs::PathStatus::None;
    if (auto* follow = registry.try_get<ecs::PathFollow>(entity);
        follow != nullptr) {
        follow->status = ecs::PathStatus::None;
        follow->waypoints.clear();
        follow->index = 0;
    }
    if (auto* intent = registry.try_get<ecs::MoveIntent>(entity);
        intent != nullptr) {
        intent->x = 0.0f;
        intent->y = 0.0f;
    }
}

void SettlementNpcSystem::RequestPath(ecs::EcsWorld& world,
                                      ecs::Entity entity,
                                      SettlementNpc& npc) const {
    RequestPathAt(world, entity, npc.targetX, npc.targetY);
}

bool SettlementNpcSystem::ChooseSafeTarget(const SettlementDef& def,
                                           float* x, float* y) const {
    if (x == nullptr || y == nullptr) {
        return false;
    }
    int wx = static_cast<int>(std::floor(def.centerX));
    int wy = static_cast<int>(std::floor(def.centerY));
    if (m_walkable &&
        ai::FindNearestWalkable(wx, wy, 4, m_walkable, &wx, &wy)) {
        *x = static_cast<float>(wx) + 0.5f;
        *y = static_cast<float>(wy) + 0.5f;
        return true;
    }
    *x = def.centerX;
    *y = def.centerY;
    return FinitePosition(*x, *y);
}

bool SettlementNpcSystem::ChoosePatrolTarget(const SettlementNpc& npc,
                                             float px, float py, float* x,
                                             float* y) {
    if (x == nullptr || y == nullptr) {
        return false;
    }
    const std::uint64_t stableId =
        static_cast<std::uint64_t>(npc.patrolSerial + 1u) * 1315423911ull +
        static_cast<std::uint64_t>(std::llround((npc.homeX + 100.0f) * 97.0f)) +
        static_cast<std::uint64_t>(std::llround((npc.homeY + 100.0f) * 193.0f));
    const float radius = std::max(0.75f, npc.wanderRadius);
    const float angle = Rand01(stableId, 11) * kTwoPi;
    const float distance = radius * std::sqrt(Rand01(stableId, 29));
    int wx = static_cast<int>(
        std::floor(npc.homeX + std::cos(angle) * distance));
    int wy = static_cast<int>(
        std::floor(npc.homeY + std::sin(angle) * distance));
    if (m_walkable &&
        ai::FindNearestWalkable(wx, wy, 2, m_walkable, &wx, &wy)) {
        *x = static_cast<float>(wx) + 0.5f;
        *y = static_cast<float>(wy) + 0.5f;
        return true;
    }
    *x = static_cast<float>(wx) + 0.5f;
    *y = static_cast<float>(wy) + 0.5f;
    if (!FinitePosition(*x, *y) || DistSq(*x, *y, px, py) < 0.01f) {
        *x = npc.homeX;
        *y = npc.homeY;
    }
    return FinitePosition(*x, *y);
}

bool SettlementNpcSystem::ChooseTarget(const SettlementDef& def,
                                       const SettlementNpc& npc, float px,
                                       float py, float* x, float* y) {
    if (npc.activity == NpcActivity::Shelter) {
        return ChooseSafeTarget(def, x, y);
    }
    if (npc.activity == NpcActivity::Defend) {
        *x = npc.homeX;
        *y = npc.homeY;
        return FinitePosition(*x, *y);
    }
    if (npc.activity == NpcActivity::Patrol) {
        return ChoosePatrolTarget(npc, px, py, x, y);
    }
    int wx = static_cast<int>(std::floor(npc.homeX));
    int wy = static_cast<int>(std::floor(npc.homeY));
    if (m_walkable &&
        ai::FindNearestWalkable(wx, wy, 2, m_walkable, &wx, &wy)) {
        *x = static_cast<float>(wx) + 0.5f;
        *y = static_cast<float>(wy) + 0.5f;
    } else {
        *x = npc.homeX;
        *y = npc.homeY;
    }
    return FinitePosition(*x, *y);
}

int SettlementNpcSystem::CountGuards(ecs::EcsWorld& world,
                                     std::string_view id,
                                     bool aliveOnly) const {
    int count = 0;
    entt::registry& registry = world.Registry();
    registry.view<SettlementNpc, ecs::Health>().each(
        [&](const ecs::Entity entity, const SettlementNpc& npc,
            const ecs::Health& health) {
            if (npc.settlementId != id ||
                npc.kind != SettlementNpcKind::Guard || !world.Alive(entity)) {
                return;
            }
            if (!aliveOnly || health.current > 0.0f) {
                ++count;
            }
        });
    return count;
}

int SettlementNpcSystem::CountRaiders(ecs::EcsWorld& world,
                                      std::string_view id) const {
    int count = 0;
    entt::registry& registry = world.Registry();
    registry.view<SettlementRaidZombie, ecs::Health>().each(
        [&](const ecs::Entity entity, const SettlementRaidZombie& raid,
            const ecs::Health& health) {
            if (raid.settlementId == id && health.current > 0.0f &&
                world.Alive(entity)) {
                ++count;
            }
        });
    return count;
}

ecs::Entity SettlementNpcSystem::FindNearestGuard(ecs::EcsWorld& world,
                                                  std::string_view id,
                                                  float x, float y) const {
    ecs::Entity best = ecs::EcsWorld::kNull;
    float bestDistSq = std::numeric_limits<float>::max();
    entt::registry& registry = world.Registry();
    registry.view<SettlementNpc, ecs::Position, ecs::Health>().each(
        [&](const ecs::Entity entity, const SettlementNpc& npc,
            const ecs::Position& position, const ecs::Health& health) {
            if (npc.settlementId != id ||
                npc.kind != SettlementNpcKind::Guard ||
                health.current <= 0.0f || !world.Alive(entity)) {
                return;
            }
            const float distanceSq =
                DistSq(x, y, position.x, position.y);
            if (distanceSq < bestDistSq) {
                bestDistSq = distanceSq;
                best = entity;
            }
        });
    return best;
}

ecs::Entity SettlementNpcSystem::FindNearestRaider(ecs::EcsWorld& world,
                                                   std::string_view id,
                                                   float x, float y) const {
    ecs::Entity best = ecs::EcsWorld::kNull;
    float bestDistSq = std::numeric_limits<float>::max();
    entt::registry& registry = world.Registry();
    registry.view<SettlementRaidZombie, ecs::Position, ecs::Health>().each(
        [&](const ecs::Entity entity, const SettlementRaidZombie& raid,
            const ecs::Position& position, const ecs::Health& health) {
            if (raid.settlementId != id || health.current <= 0.0f ||
                !world.Alive(entity)) {
                return;
            }
            const float distanceSq =
                DistSq(x, y, position.x, position.y);
            if (distanceSq < bestDistSq) {
                bestDistSq = distanceSq;
                best = entity;
            }
        });
    return best;
}

ecs::Entity SettlementNpcSystem::FindNearestThreat(ecs::EcsWorld& world,
                                                   std::string_view id,
                                                   float x, float y,
                                                   float radius) const {
    const ecs::Entity raid =
        FindNearestRaider(world, id, x, y);
    if (raid != ecs::EcsWorld::kNull) {
        return raid;
    }
    const float radiusSq = std::max(0.0f, radius) * std::max(0.0f, radius);
    ecs::Entity best = ecs::EcsWorld::kNull;
    float bestDistSq = std::numeric_limits<float>::max();
    entt::registry& registry = world.Registry();
    registry.view<ecs::AiState, ecs::Position, ecs::Health>().each(
        [&](const ecs::Entity entity, const ecs::AiState&,
            const ecs::Position& position, const ecs::Health& health) {
            if (health.current <= 0.0f || !world.Alive(entity) ||
                registry.any_of<SettlementNpc, ecs::FriendlyNpc, ecs::Animal>(
                    entity) ||
                registry.all_of<ecs::NoCombatTarget>(entity)) {
                return;
            }
            const float distanceSq = DistSq(x, y, position.x, position.y);
            if (distanceSq <= radiusSq && distanceSq < bestDistSq) {
                bestDistSq = distanceSq;
                best = entity;
            }
        });
    return best;
}

void SettlementNpcSystem::Attack(ecs::EcsWorld& world, ecs::Entity source,
                                 ecs::Entity target, float damage,
                                 ecs::DamageType type) {
    if (!world.Alive(source) || !world.Alive(target) || damage <= 0.0f) {
        return;
    }
    entt::registry& registry = world.Registry();
    const auto* targetHealth = registry.try_get<ecs::Health>(target);
    if (targetHealth == nullptr || targetHealth->current <= 0.0f) {
        return;
    }
    if (auto* action = registry.try_get<ecs::ActionState>(source);
        action != nullptr) {
        action->action = ecs::ActorAction::Attacking;
        action->elapsed = 0.0f;
        action->duration = 0.28f;
    }
    if (m_damage) {
        (void)m_damage(source, target, damage, type);
    } else {
        auto* health = registry.try_get<ecs::Health>(target);
        if (health != nullptr) {
            health->current -= damage;
        }
    }
    const auto* health = registry.try_get<ecs::Health>(target);
    if (health == nullptr || health->current > 0.0f) {
        return;
    }
    if (const auto* npc = registry.try_get<SettlementNpc>(target);
        npc != nullptr && npc->kind == SettlementNpcKind::Guard) {
        m_savedGuardHealth[npc->settlementId] = 0.0f;
    }
}

void SettlementNpcSystem::SpawnRaider(ecs::EcsWorld& world,
                                      const SettlementDef& def, int nightKey,
                                      int index, int count,
                                      ContestedFaction faction) {
    if (!m_walkable) {
        return;
    }
    const int safeCount = std::max(1, count);
    const std::uint64_t seed =
        static_cast<std::uint64_t>(nightKey) * 1315423911ull +
        static_cast<std::uint64_t>(std::max(0, index)) * 2654435761ull;
    const float baseAngle =
        (static_cast<float>(index) / static_cast<float>(safeCount)) * kTwoPi +
        Rand01(seed, 13) * 0.45f;
    const float radius = def.safeRadius + def.defense.spawnRadius +
                         static_cast<float>(index % 2) * 1.25f;
    int wx = static_cast<int>(
        std::floor(def.centerX + std::cos(baseAngle) * radius));
    int wy = static_cast<int>(
        std::floor(def.centerY + std::sin(baseAngle) * radius));
    if (!ai::FindNearestWalkable(wx, wy, 6, m_walkable, &wx, &wy)) {
        return;
    }
    const ecs::Entity entity = world.Create();
    if (entity == ecs::EcsWorld::kNull) {
        return;
    }
    entt::registry& registry = world.Registry();
    const bool raiders = faction == ContestedFaction::Raiders;
    registry.emplace<ecs::Position>(
        entity, ecs::Position{static_cast<float>(wx) + 0.5f,
                              static_cast<float>(wy) + 0.5f, 0});
    registry.emplace<ecs::Velocity>(entity, ecs::Velocity{});
    registry.emplace<ecs::SpriteRef>(
        entity, ecs::SpriteRef{ecs::HashName(raiders ? kNpcSprite
                                                     : kRaiderSprite),
                               0, raiders ? 0xFFFF8060u : 0xFF8080FFu});
    registry.emplace<ecs::Facing>(entity, ecs::Facing{});
    registry.emplace<ecs::AnimState>(entity, ecs::AnimState{});
    registry.emplace<ecs::ActionState>(entity, ecs::ActionState{});
    registry.emplace<ecs::Health>(
        entity, ecs::Health{def.defense.raiderHealth,
                            def.defense.raiderHealth});
    registry.emplace<ecs::MoveIntent>(entity, ecs::MoveIntent{});
    registry.emplace<ecs::PathRequest>(entity, ecs::PathRequest{});
    ecs::PathFollow follow;
    follow.speed = std::clamp(def.defense.raiderSpeed, 0.5f, 8.0f);
    registry.emplace<ecs::PathFollow>(entity, follow);
    SettlementRaidZombie raid;
    raid.settlementId = def.id;
    raid.faction = faction;
    raid.nightKey = nightKey;
    registry.emplace<SettlementRaidZombie>(entity, std::move(raid));
    ZL_INFO("settlement", "聚落{}生成：{}（波次 {}，格 {},{}）",
            ContestedFactionLabel(faction), def.id, nightKey, wx, wy);
}

void SettlementNpcSystem::StartRaid(ecs::EcsWorld& world,
                                    const SettlementDef& def,
                                    DefenseRuntime& runtime, int nightKey) {
    runtime.active = true;
    runtime.external = false;
    runtime.faction = ContestedFaction::Infected;
    runtime.lastNightKey = nightKey;
    runtime.nightKey = nightKey;
    runtime.raidersSpawned = 0;
    runtime.raidersKilled = 0;
    runtime.elapsed = 0.0f;
    entt::registry& registry = world.Registry();
    registry.view<SettlementNpc, ecs::Health>().each(
        [&](const ecs::Entity entity, const SettlementNpc& npc,
            const ecs::Health& health) {
            if (npc.settlementId != def.id ||
                npc.kind != SettlementNpcKind::Guard ||
                health.current <= 0.0f) {
                return;
            }
            registry.remove<ecs::NoCombatTarget>(entity);
            if (auto* component = registry.try_get<SettlementNpc>(entity);
                component != nullptr) {
                component->attackCooldown = 0.0f;
                component->hasTarget = false;
                component->settled = false;
            }
        });

    const int raidNumber =
        std::max(0, nightKey - def.defense.minDay);
    const int count = std::clamp(
        def.defense.baseRaiders +
            raidNumber * def.defense.raidersPerNight,
        0, def.defense.maxRaiders);
    for (int i = 0; i < count; ++i) {
        const int before = CountRaiders(world, def.id);
        SpawnRaider(world, def, nightKey, i, count,
                    ContestedFaction::Infected);
        if (CountRaiders(world, def.id) > before) {
            ++runtime.raidersSpawned;
        }
    }
    if (runtime.raidersSpawned == 0) {
        FinishRaid(world, def, runtime, false, "夜袭未能集结");
        return;
    }
    if (def.defense.guardBehavior == SettlementGuardBehavior::Retreat) {
        PostNotice(def.name + "夜袭：守卫与平民撤离，感染者聚集", "warn");
    } else {
        PostNotice(def.name + "夜袭：守卫迎战（" +
                       std::to_string(runtime.raidersSpawned) +
                       " 名感染者）",
                   "warn");
    }
}

void SettlementNpcSystem::UpdateRaiders(ecs::EcsWorld& world,
                                        const SettlementDef& def,
                                        DefenseRuntime& runtime, float dt) {
    runtime.elapsed += std::max(0.0f, dt);
    entt::registry& registry = world.Registry();
    const bool guardsFight =
        def.defense.guardBehavior == SettlementGuardBehavior::Defend;
    registry.view<SettlementRaidZombie, ecs::Position, ecs::Health>().each(
        [&](const ecs::Entity entity, SettlementRaidZombie& raid,
            const ecs::Position& position, const ecs::Health& health) {
            if (raid.settlementId != def.id || health.current <= 0.0f ||
                !world.Alive(entity)) {
                return;
            }
            raid.attackCooldown =
                std::max(0.0f, raid.attackCooldown - std::max(0.0f, dt));
            raid.retrySeconds =
                std::max(0.0f, raid.retrySeconds - std::max(0.0f, dt));

            const ecs::Entity guard =
                guardsFight ? FindNearestGuard(world, def.id, position.x,
                                               position.y)
                            : ecs::EcsWorld::kNull;
            float targetX = def.centerX;
            float targetY = def.centerY;
            if (guard != ecs::EcsWorld::kNull) {
                if (const auto* guardPosition =
                        registry.try_get<ecs::Position>(guard);
                    guardPosition != nullptr) {
                    const float dx = guardPosition->x - position.x;
                    const float dy = guardPosition->y - position.y;
                    if (dx * dx + dy * dy <=
                        def.defense.raiderRange * def.defense.raiderRange) {
                        Stop(world, entity);
                        FaceTowards(registry, entity, dx, dy);
                        if (raid.attackCooldown <= 0.0f) {
                            Attack(world, entity, guard,
                                   def.defense.raiderDamage,
                                   raid.faction == ContestedFaction::Raiders
                                       ? ecs::DamageType::Blunt
                                       : ecs::DamageType::Claw);
                            raid.attackCooldown =
                                def.defense.raiderAttackSeconds;
                        }
                        return;
                    }
                    const float offset = Rand01(world.IdOf(entity).id, 7) *
                                         kTwoPi;
                    targetX = guardPosition->x + std::cos(offset) * 0.55f;
                    targetY = guardPosition->y + std::sin(offset) * 0.55f;
                }
            }

            const float dx = targetX - position.x;
            const float dy = targetY - position.y;
            const bool atTarget = dx * dx + dy * dy <= 1.0f;
            if (atTarget) {
                Stop(world, entity);
                FaceTowards(registry, entity, dx, dy);
                return;
            }
            if (!raid.hasTarget ||
                DistSq(raid.targetX, raid.targetY, targetX, targetY) >
                    kDefendRetargetSq) {
                raid.targetX = targetX;
                raid.targetY = targetY;
                raid.hasTarget = true;
                RequestPathAt(world, entity, targetX, targetY);
                raid.retrySeconds = 1.0f;
            }

            auto* follow = registry.try_get<ecs::PathFollow>(entity);
            auto* request = registry.try_get<ecs::PathRequest>(entity);
            const bool failed =
                (follow != nullptr &&
                 follow->status == ecs::PathStatus::Failed) ||
                (request != nullptr &&
                 request->state == ecs::PathStatus::Failed);
            if (failed && raid.retrySeconds <= 0.0f) {
                const float length = std::sqrt(dx * dx + dy * dy);
                RequestPathAt(world, entity, targetX, targetY);
                raid.retrySeconds = 1.25f;
                if (auto* intent =
                        registry.try_get<ecs::MoveIntent>(entity);
                    intent != nullptr) {
                    const float speed =
                        std::clamp(def.defense.raiderSpeed, 0.5f, 8.0f);
                    if (length > 0.0001f) {
                        intent->x = dx / length * speed;
                        intent->y = dy / length * speed;
                    }
                }
            }
        });
}

void SettlementNpcSystem::FinishRaid(ecs::EcsWorld& world,
                                     const SettlementDef& def,
                                     DefenseRuntime& runtime, bool fallen,
                                     std::string_view reason) {
    if (!runtime.active && !fallen) {
        return;
    }
    const int alive = CountRaiders(world, def.id);
    runtime.raidersKilled +=
        std::max(0, runtime.raidersSpawned - alive);
    runtime.active = false;
    runtime.external = false;
    runtime.reclaim = false;
    entt::registry& registry = world.Registry();
    std::vector<ecs::Entity> raiders;
    registry.view<SettlementRaidZombie>().each(
        [&](const ecs::Entity entity, const SettlementRaidZombie& raid) {
            if (raid.settlementId == def.id) {
                raiders.push_back(entity);
            }
        });
    for (const ecs::Entity raider : raiders) {
        world.Destroy(raider);
    }
    registry.view<SettlementNpc, ecs::Health>().each(
        [&](const ecs::Entity entity, SettlementNpc& npc,
            const ecs::Health& health) {
            if (npc.settlementId != def.id ||
                npc.kind != SettlementNpcKind::Guard ||
                health.current <= 0.0f) {
                return;
            }
            registry.emplace_or_replace<ecs::NoCombatTarget>(entity);
            registry.remove<ecs::Wounds>(entity);
            registry.remove<ecs::HitReaction>(entity);
            npc.attackCooldown = 0.0f;
            npc.hasTarget = false;
            npc.settled = false;
        });
    if (fallen) {
        runtime.fallen = true;
        PostNotice(def.name + "失守：守卫倒下，交易与委托暂停", "fail");
        return;
    }
    if (runtime.raidersKilled > 0) {
        PostNotice(std::string(reason) + "：击退 " +
                       std::to_string(runtime.raidersKilled) + " 名" +
                       ContestedFactionLabel(runtime.faction),
                   "ok");
    } else {
        PostNotice(std::string(reason), "info");
    }
}

void SettlementNpcSystem::UpdateDefense(ecs::EcsWorld& world, float dt,
                                        int hour, int day) {
    if (m_settlements == nullptr || !m_settlements->enabled) {
        return;
    }
    for (const SettlementDef& def : m_settlements->defs) {
        if (!SettlementProvidesDefense(def)) {
            continue;
        }
        DefenseRuntime& runtime = RuntimeFor(def.id);
        const bool night =
            InNightWindow(hour, def.defense);
        if (runtime.active && runtime.external) {
            // M32-T4/T5：外部战略围攻/反攻只推进感染者和守卫近战；
            // 波次完成、玩家离开和胜负由区域围攻控制器决定。
            UpdateRaiders(world, def, runtime, dt);
            continue;
        }
        if (runtime.fallen) {
            continue;
        }
        if (runtime.active) {
            const int guards = CountGuards(world, def.id, true);
            if (guards <= 0) {
                FinishRaid(world, def, runtime, true, "守卫倒下");
                continue;
            }
            if (!night) {
                FinishRaid(world, def, runtime, false, "黎明到来，感染者退去");
                continue;
            }
            const float abortRadius =
                std::max(1.0f, def.defense.activationRadius) * 1.75f;
            if (DistSq(m_focusX, m_focusY, def.centerX, def.centerY) >
                abortRadius * abortRadius) {
                FinishRaid(world, def, runtime, false, "你离开了营地，夜袭中止");
                continue;
            }
            UpdateRaiders(world, def, runtime, dt);
            if (CountRaiders(world, def.id) == 0) {
                FinishRaid(world, def, runtime, false, "夜袭结束");
            }
            continue;
        }
        if (!night) {
            continue;
        }
        if (day < def.defense.minDay) {
            continue;
        }
        const int nightKey = NightKeyFor(day, hour, def.defense);
        if (runtime.lastNightKey == nightKey) {
            continue;
        }
        if (DistSq(m_focusX, m_focusY, def.centerX, def.centerY) >
            def.defense.activationRadius * def.defense.activationRadius) {
            continue;
        }
        if (CountGuards(world, def.id, true) <= 0) {
            continue;
        }
        StartRaid(world, def, runtime, nightKey);
    }
}
void SettlementNpcSystem::Update(ecs::EcsWorld& world, float dt, int hour,
                                 int day) {
    m_stats = {};
    if (m_settlements == nullptr || !m_settlements->enabled ||
        m_settlements->defs.empty()) {
        return;
    }
    entt::registry& registry = world.Registry();

    std::vector<std::pair<float, float>> zombies;
    registry.view<ecs::AiState, ecs::Position>().each(
        [&](const ecs::Entity entity, const ecs::AiState&,
            const ecs::Position& position) {
            const auto* health = registry.try_get<ecs::Health>(entity);
            if (health != nullptr && health->current <= 0.0f) {
                return;
            }
            zombies.emplace_back(position.x, position.y);
        });
    registry.view<SettlementRaidZombie, ecs::Position, ecs::Health>().each(
        [&](const ecs::Entity entity, const SettlementRaidZombie&,
            const ecs::Position& position, const ecs::Health& health) {
            if (health.current > 0.0f && world.Alive(entity)) {
                zombies.emplace_back(position.x, position.y);
            }
        });
    for (const SettlementDef& def : m_settlements->defs) {
        const float radius = def.safeRadius + kThreatPadding;
        const float radiusSq = radius * radius;
        bool threatened = false;
        for (const auto& [zx, zy] : zombies) {
            if (DistSq(zx, zy, def.centerX, def.centerY) <= radiusSq) {
                threatened = true;
                break;
            }
        }
        float& linger = m_threatLinger[def.id];
        if (threatened) {
            linger = kThreatLingerSeconds;
        } else {
            linger = std::max(0.0f, linger - std::max(0.0f, dt));
        }
        if (linger > 0.0f) {
            ++m_stats.threatenedSettlements;
        }
    }

    UpdateDefense(world, dt, hour, day);

    registry.view<SettlementNpc, ecs::Position>().each(
        [&](const ecs::Entity entity, SettlementNpc& npc,
            const ecs::Position& position) {
            ++m_stats.total;
            const SettlementDef* def = FindSettlement(npc.settlementId);
            if (def == nullptr) {
                Stop(world, entity);
                return;
            }
            const float dx = position.x - m_focusX;
            const float dy = position.y - m_focusY;
            if (dx * dx + dy * dy >
                kSimulationRadius * kSimulationRadius) {
                if (!npc.frozen) {
                    Stop(world, entity);
                    npc.frozen = true;
                }
                npc.hasTarget = false;
                npc.settled = false;
                ++m_stats.frozen;
                switch (npc.activity) {
                    case NpcActivity::Standing:
                        ++m_stats.standing;
                        break;
                    case NpcActivity::Patrol:
                        ++m_stats.patrol;
                        break;
                    case NpcActivity::Rest:
                        ++m_stats.rest;
                        break;
                    case NpcActivity::Shelter:
                        ++m_stats.shelter;
                        break;
                    case NpcActivity::Defend:
                        ++m_stats.defending;
                        break;
                }
                return;
            }
            if (npc.frozen) {
                npc.frozen = false;
                npc.hasTarget = false;
                npc.settled = false;
            }

            const DefenseRuntime* runtime = FindRuntime(def->id);
            const bool raidActive = runtime != nullptr && runtime->active;
            NpcActivity desired = ScheduledActivity(npc.kind, hour);
            if (raidActive) {
                if (npc.kind == SettlementNpcKind::Guard &&
                    def->defense.guardBehavior ==
                        SettlementGuardBehavior::Defend) {
                    desired = NpcActivity::Defend;
                } else {
                    desired = NpcActivity::Shelter;
                }
            } else if (ThreatRemaining(def->id) > 0.0f) {
                desired = NpcActivity::Shelter;
            }
            if (desired == NpcActivity::Defend &&
                npc.kind == SettlementNpcKind::Guard) {
                registry.remove<ecs::NoCombatTarget>(entity);
            }
            if (desired != npc.activity) {
                ZL_DEBUG("settlement",
                         "聚落 NPC 日程：{}（稳定 ID {}）{} -> {}",
                         SettlementNpcKindName(npc.kind),
                         world.IdOf(entity).id,
                         NpcActivityName(npc.activity),
                         NpcActivityName(desired));
                npc.activity = desired;
                npc.hasTarget = false;
                npc.settled = false;
                npc.waitSeconds = 0.0f;
                npc.retrySeconds = 0.0f;
                if (desired == NpcActivity::Patrol) {
                    npc.patrolSerial = 0;
                }
                Stop(world, entity);
            }
            npc.waitSeconds = std::max(0.0f, npc.waitSeconds - dt);
            npc.retrySeconds = std::max(0.0f, npc.retrySeconds - dt);
            npc.attackCooldown =
                std::max(0.0f, npc.attackCooldown - std::max(0.0f, dt));
            switch (npc.activity) {
                case NpcActivity::Standing:
                    ++m_stats.standing;
                    break;
                case NpcActivity::Patrol:
                    ++m_stats.patrol;
                    break;
                case NpcActivity::Rest:
                    ++m_stats.rest;
                    break;
                case NpcActivity::Shelter:
                    ++m_stats.shelter;
                    break;
                case NpcActivity::Defend:
                    ++m_stats.defending;
                    break;
            }

            if (const auto* health = registry.try_get<ecs::Health>(entity);
                npc.kind == SettlementNpcKind::Guard &&
                health != nullptr && health->current <= 0.0f) {
                m_savedGuardHealth[npc.settlementId] = 0.0f;
                Stop(world, entity);
                return;
            }

            if (npc.activity == NpcActivity::Defend) {
                const ecs::Entity raider = FindNearestThreat(
                    world, def->id, position.x, position.y,
                    def->safeRadius + std::max(0.0f, def->defense.guardEngageRadius));
                if (raider != ecs::EcsWorld::kNull) {
                    const auto* raiderPosition =
                        registry.try_get<ecs::Position>(raider);
                    if (raiderPosition != nullptr) {
                        const float rdx = raiderPosition->x - position.x;
                        const float rdy = raiderPosition->y - position.y;
                        const float range = def->defense.guardRange;
                        if (rdx * rdx + rdy * rdy <= range * range) {
                            Stop(world, entity);
                            FaceTowards(registry, entity, rdx, rdy);
                            if (npc.attackCooldown <= 0.0f) {
                                Attack(world, entity, raider,
                                       def->defense.guardDamage,
                                       ecs::DamageType::Blunt);
                                npc.attackCooldown =
                                    def->defense.guardAttackSeconds;
                            }
                            return;
                        }
                        if (npc.retrySeconds <= 0.0f) {
                            const float targetX =
                                std::floor(raiderPosition->x) + 0.5f;
                            const float targetY =
                                std::floor(raiderPosition->y) + 0.5f;
                            if (!npc.hasTarget ||
                                DistSq(npc.targetX, npc.targetY, targetX,
                                       targetY) > kDefendRetargetSq) {
                                npc.targetX = targetX;
                                npc.targetY = targetY;
                                npc.hasTarget = true;
                                RequestPath(world, entity, npc);
                            }
                            auto* follow =
                                registry.try_get<ecs::PathFollow>(entity);
                            auto* request =
                                registry.try_get<ecs::PathRequest>(entity);
                            const bool failed =
                                (follow != nullptr &&
                                 follow->status == ecs::PathStatus::Failed) ||
                                (request != nullptr &&
                                 request->state == ecs::PathStatus::Failed);
                            const bool arrived =
                                (follow != nullptr &&
                                 follow->status == ecs::PathStatus::Arrived) ||
                                (request != nullptr &&
                                 request->state == ecs::PathStatus::Arrived);
                            if (failed || arrived) {
                                Stop(world, entity);
                                npc.hasTarget = false;
                                npc.retrySeconds = 0.35f;
                            }
                            return;
                        }
                    }
                }
                float safeX = def->centerX;
                float safeY = def->centerY;
                (void)ChooseSafeTarget(*def, &safeX, &safeY);
                if (DistSq(position.x, position.y, safeX, safeY) >
                    kArriveRadius * kArriveRadius) {
                    if (!npc.hasTarget ||
                        DistSq(npc.targetX, npc.targetY, safeX, safeY) >
                            kDefendRetargetSq) {
                        npc.targetX = safeX;
                        npc.targetY = safeY;
                        npc.hasTarget = true;
                        RequestPath(world, entity, npc);
                    }
                } else {
                    Stop(world, entity);
                    npc.hasTarget = false;
                }
                return;
            }

            auto* follow = registry.try_get<ecs::PathFollow>(entity);
            auto* request = registry.try_get<ecs::PathRequest>(entity);
            if (npc.settled) {
                if (npc.activity == NpcActivity::Patrol) {
                    if (npc.waitSeconds > 0.0f) {
                        Stop(world, entity);
                        return;
                    }
                    npc.settled = false;
                    npc.patrolSerial = npc.patrolSerial + 1u;
                } else {
                    Stop(world, entity);
                    return;
                }
            }

            if (npc.hasTarget) {
                const bool arrived =
                    (follow != nullptr &&
                     follow->status == ecs::PathStatus::Arrived) ||
                    (request != nullptr &&
                     request->state == ecs::PathStatus::Arrived);
                if (arrived) {
                    Stop(world, entity);
                    npc.hasTarget = false;
                    npc.settled = true;
                    if (npc.activity == NpcActivity::Patrol) {
                        npc.waitSeconds =
                            kPatrolWaitMin +
                            Rand01(world.IdOf(entity).id,
                                   npc.patrolSerial + 71u) *
                                (kPatrolWaitMax - kPatrolWaitMin);
                    }
                    return;
                }
                const bool failed =
                    (follow != nullptr &&
                     follow->status == ecs::PathStatus::Failed) ||
                    (request != nullptr &&
                     request->state == ecs::PathStatus::Failed);
                if (failed) {
                    Stop(world, entity);
                    npc.hasTarget = false;
                    npc.retrySeconds = kRetrySeconds;
                    if (npc.activity == NpcActivity::Patrol) {
                        npc.patrolSerial = npc.patrolSerial + 1u;
                    }
                }
                return;
            }

            if (npc.waitSeconds > 0.0f || npc.retrySeconds > 0.0f) {
                Stop(world, entity);
                return;
            }
            float targetX = 0.0f;
            float targetY = 0.0f;
            if (!ChooseTarget(*def, npc, position.x, position.y, &targetX,
                              &targetY)) {
                npc.retrySeconds = kRetrySeconds;
                return;
            }
            npc.targetX = targetX;
            npc.targetY = targetY;
            npc.hasTarget = true;
            if (DistSq(position.x, position.y, targetX, targetY) <=
                kArriveRadius * kArriveRadius) {
                npc.hasTarget = false;
                npc.settled = true;
                if (npc.activity == NpcActivity::Patrol) {
                    npc.waitSeconds =
                        kPatrolWaitMin +
                        Rand01(world.IdOf(entity).id, npc.patrolSerial + 71u) *
                            (kPatrolWaitMax - kPatrolWaitMin);
                }
                Stop(world, entity);
                return;
            }
            RequestPath(world, entity, npc);
        });

    for (const SettlementDef& def : m_settlements->defs) {
        m_stats.raiders += CountRaiders(world, def.id);
        if (IsFallen(def.id)) {
            ++m_stats.fallenSettlements;
        }
    }
}

}  // namespace zl::game
