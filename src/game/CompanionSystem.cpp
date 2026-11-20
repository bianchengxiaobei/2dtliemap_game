#include "game/CompanionSystem.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "ai/Passability.h"
#include "ai/components/AiComponents.h"
#include "characters/components/AnimalComponents.h"
#include "characters/components/CharacterComponents.h"
#include "core/Config.h"
#include "ecs/components/CommonComponents.h"
#include "game/CompanionInventory.h"
#include "game/HomesteadComponents.h"
#include "game/SettlementNpc.h"
#include "game/Traps.h"
#include "items/InventoryOps.h"
#include "items/ItemDef.h"
#include "items/ItemRegistry.h"

namespace zl::game {
namespace {

using ecs::EcsWorld;

float DistSq(float ax, float ay, float bx, float by) {
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

void SetFacing(ecs::Facing* facing, float dx, float dy) {
    if (facing == nullptr || (dx == 0.0f && dy == 0.0f)) {
        return;
    }
    if (std::abs(dx) >= std::abs(dy)) {
        facing->dir = dx >= 0.0f ? ecs::Direction::Right
                                 : ecs::Direction::Left;
    } else {
        facing->dir = dy >= 0.0f ? ecs::Direction::Down
                                 : ecs::Direction::Up;
    }
}

CompanionAttackProfile WeaponProfile(
    const CompanionState& state, const items::ItemRegistry* items,
    const CompanionSystem::Options& fallback) {
    CompanionAttackProfile profile;
    profile.damage = fallback.attackDamage;
    profile.range = fallback.attackRange;
    profile.cooldown = fallback.attackCooldown;
    if (items == nullptr) {
        return profile;
    }
    const auto& slot = state.equipment.slots[static_cast<std::size_t>(
        ecs::EquipSlot::PrimaryHand)];
    if (!slot.has_value()) {
        return profile;
    }
    const items::ItemDef* def = items->Find(slot->defId);
    if (def == nullptr || def->type != items::ItemType::Weapon) {
        return profile;
    }
    bool hasMelee = false;
    if (def->combat.damage.has_value()) {
        profile.damage = *def->combat.damage;
        hasMelee = true;
    }
    if (def->combat.range.has_value()) {
        profile.range = *def->combat.range;
    }
    if (def->combat.cooldownSeconds.has_value()) {
        profile.cooldown = *def->combat.cooldownSeconds;
        hasMelee = true;
    }
    if (def->combat.wearPerHit.has_value()) {
        profile.wear = *def->combat.wearPerHit;
    }
    if (hasMelee) {
        profile.damage *= fallback.attackDamageScale;
        profile.cooldown *= fallback.attackCooldownScale;
    }
    profile.damage = std::max(0.0f, profile.damage);
    profile.range = std::max(0.0f, profile.range);
    profile.cooldown = std::max(0.0f, profile.cooldown);
    profile.wear = std::clamp(profile.wear, 0.0f, 1.0f);
    return profile;
}

float ReadCompanionFloat(const core::Config& config, const char* key,
                         float fallback, float minimum, float maximum) {
    const double value = config.Get<double>(key, fallback);
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(static_cast<float>(value), minimum, maximum);
}

}  // namespace

CompanionAttackProfile CompanionAttackProfileFor(
    const CompanionState& state, const items::ItemRegistry* items,
    const CompanionSystem::Options& options) {
    return WeaponProfile(state, items, options);
}

CompanionSystem::Options LoadCompanionSystemOptions(
    const core::Config& config) {
    CompanionSystem::Options options;
    options.followDistance = ReadCompanionFloat(
        config, "companions.ai.followDistance", options.followDistance, 0.0f,
        20.0f);
    options.followMin = ReadCompanionFloat(
        config, "companions.ai.followMin", options.followMin, 0.0f, 20.0f);
    options.followMax = ReadCompanionFloat(
        config, "companions.ai.followMax", options.followMax, 0.0f, 20.0f);
    options.ownerEngageRadius = ReadCompanionFloat(
        config, "companions.ai.ownerEngageRadius", options.ownerEngageRadius,
        0.0f, 40.0f);
    options.ownerUrgentRadius = ReadCompanionFloat(
        config, "companions.ai.ownerUrgentRadius", options.ownerUrgentRadius,
        0.0f, 40.0f);
    options.leash = ReadCompanionFloat(
        config, "companions.ai.leash", options.leash, 0.0f, 100.0f);
    options.freezeRadius = ReadCompanionFloat(
        config, "companions.ai.freezeRadius", options.freezeRadius, 0.0f,
        200.0f);
    options.attackRange = ReadCompanionFloat(
        config, "companions.ai.attackRange", options.attackRange, 0.1f, 10.0f);
    options.attackDamage = ReadCompanionFloat(
        config, "companions.ai.attackDamage", options.attackDamage, 0.0f,
        1000.0f);
    options.attackCooldown = ReadCompanionFloat(
        config, "companions.ai.attackCooldown", options.attackCooldown, 0.05f,
        10.0f);
    options.attackDamageScale = ReadCompanionFloat(
        config, "companions.ai.attackDamageScale", options.attackDamageScale,
        0.05f, 2.0f);
    options.attackCooldownScale = ReadCompanionFloat(
        config, "companions.ai.attackCooldownScale",
        options.attackCooldownScale, 0.05f, 5.0f);
    options.attackKnockback = ReadCompanionFloat(
        config, "companions.ai.attackKnockback", options.attackKnockback, 0.0f,
        10.0f);
    options.attackStagger = ReadCompanionFloat(
        config, "companions.ai.attackStagger", options.attackStagger, 0.0f,
        10.0f);
    options.lowHealthFraction = ReadCompanionFloat(
        config, "companions.ai.lowHealthFraction", options.lowHealthFraction,
        0.05f, 0.95f);
    options.recoverHealthFraction = ReadCompanionFloat(
        config, "companions.ai.recoverHealthFraction",
        options.recoverHealthFraction, 0.05f, 1.0f);
    options.stayEngageRadius = ReadCompanionFloat(
        config, "companions.ai.stayEngageRadius", options.stayEngageRadius,
        0.0f, 40.0f);
    options.stayLeash = ReadCompanionFloat(
        config, "companions.ai.stayLeash", options.stayLeash, 0.0f, 40.0f);
    options.trapClearance = ReadCompanionFloat(
        config, "companions.ai.trapClearance", options.trapClearance, 0.0f,
        5.0f);
    options.pathRetrySeconds = ReadCompanionFloat(
        config, "companions.ai.pathRetrySeconds", options.pathRetrySeconds,
        0.05f, 5.0f);
    options.downedSeconds = ReadCompanionFloat(
        config, "companions.ai.downedSeconds", options.downedSeconds, 5.0f,
        600.0f);
    options.rescueHealthFraction = ReadCompanionFloat(
        config, "companions.ai.rescueHealthFraction",
        options.rescueHealthFraction, 0.05f, 1.0f);
    options.rescueRange = ReadCompanionFloat(
        config, "companions.ai.rescueRange", options.rescueRange, 0.5f, 10.0f);
    options.combatKillTrust = std::clamp(
        config.Get<int>("companions.ai.combatKillTrust",
                        options.combatKillTrust),
        0, 50);

    options.followMax = std::max(options.followMax, options.followMin);
    options.followDistance =
        std::clamp(options.followDistance, options.followMin,
                   options.followMax);
    options.ownerUrgentRadius =
        std::min(options.ownerUrgentRadius, options.ownerEngageRadius);
    options.leash = std::max(options.leash, options.attackRange);
    options.freezeRadius = std::max(options.freezeRadius, options.leash);
    options.recoverHealthFraction =
        std::max(options.recoverHealthFraction, options.lowHealthFraction);
    options.stayLeash = std::max(options.stayLeash, options.stayEngageRadius);
    return options;
}

bool CompanionNeedsRetreat(const ecs::Health& health, float threshold) {
    if (!std::isfinite(health.max) || health.max <= 0.0f ||
        !std::isfinite(health.current)) {
        return true;
    }
    return health.current <= health.max * std::clamp(threshold, 0.0f, 1.0f);
}

bool IsCompanionHostileTarget(const EcsWorld& world, ecs::Entity entity) {
    if (entity == EcsWorld::kNull || !world.Alive(entity)) {
        return false;
    }
    const entt::registry& registry = world.Registry();
    const auto* health = registry.try_get<ecs::Health>(entity);
    if (health == nullptr || health->current <= 0.0f) {
        return false;
    }
    if (registry.any_of<ecs::PlayerTag, ecs::FriendlyNpc, Companion,
                        SettlementNpc, ecs::Animal, ecs::NoCombatTarget>(
            entity)) {
        return false;
    }
    return registry.any_of<ecs::AiState, SettlementRaidZombie,
                           ecs::BaseSiegeZombie>(entity);
}

CompanionSystem::CompanionSystem(WalkableQuery walkable,
                                 DamageCallback damage, Options options)
    : m_walkable(std::move(walkable)),
      m_damage(std::move(damage)),
      m_options(options) {}

void CompanionSystem::EnterDowned(EcsWorld& world, ecs::Entity companion,
                                  CompanionState& state,
                                  CompanionAiState& ai) {
    state.downed = true;
    state.dead = false;
    state.downedRemaining = std::max(0.0f, m_options.downedSeconds);
    state.standby = false;
    state.command = CompanionCommand::Follow;
    ai.targetId = 0;
    ai.engaged = false;
    ai.autoRetreat = false;
    Stop(world, companion);
    if (auto* velocity = world.Registry().try_get<ecs::Velocity>(companion);
        velocity != nullptr) {
        *velocity = ecs::Velocity{};
    }
    if (auto* action = world.Registry().try_get<ecs::ActionState>(companion);
        action != nullptr) {
        action->action = ecs::ActorAction::Dead;
        action->elapsed = 0.0f;
        action->duration = state.downedRemaining;
    }
    ++m_stats.downed;
}

void CompanionSystem::KillCompanion(EcsWorld& world, ecs::Entity companion,
                                    CompanionState& state,
                                    CompanionAiState& ai) {
    if (state.dead) {
        return;
    }
    state.dead = true;
    state.downed = false;
    state.downedRemaining = 0.0f;
    state.standby = false;
    ai.targetId = 0;
    ai.engaged = false;
    ai.autoRetreat = false;
    if (m_companions != nullptr) {
        const auto* identity =
            world.Registry().try_get<Companion>(companion);
        const CompanionDef* def =
            identity != nullptr ? m_companions->Find(identity->defId)
                                : nullptr;
        if (def != nullptr) {
            (void)FailCompanionQuest(*def, &state, nullptr);
        }
    }
    Stop(world, companion);
    if (auto* velocity = world.Registry().try_get<ecs::Velocity>(companion);
        velocity != nullptr) {
        *velocity = ecs::Velocity{};
    }

    std::vector<ecs::ItemInstance> loot;
    for (auto& slot : state.equipment.slots) {
        if (slot.has_value()) {
            loot.push_back(std::move(*slot));
            slot.reset();
        }
    }
    loot.insert(loot.end(),
                std::make_move_iterator(state.inventory.items.begin()),
                std::make_move_iterator(state.inventory.items.end()));
    state.inventory.items.clear();

    ecs::Container corpseInventory;
    corpseInventory.capacityKg =
        std::max(20.0f, state.inventory.capacityKg + 10.0f);
    corpseInventory.items = std::move(loot);

    float x = 0.0f;
    float y = 0.0f;
    int z = 0;
    if (const auto* position = world.Registry().try_get<ecs::Position>(companion);
        position != nullptr) {
        x = position->x;
        y = position->y;
        z = position->z;
    }
    const std::uint64_t sourceId = world.IdOf(companion).id;
    ++m_stats.deaths;
    world.QueueCreate(
        [corpseInventory = std::move(corpseInventory), x, y, z,
         sourceId](EcsWorld& value) mutable {
            const ecs::Entity corpse = value.Create();
            if (corpse == EcsWorld::kNull) {
                return corpse;
            }
            entt::registry& registry = value.Registry();
            registry.emplace<ecs::Position>(corpse, ecs::Position{x, y, z});
            registry.emplace<ecs::SpriteRef>(
                corpse, ecs::SpriteRef{ecs::HashName("corpse.placeholder"), 0,
                                       0xFFFFFFFFu});
            registry.emplace<ecs::Corpse>(corpse, ecs::Corpse{sourceId});
            registry.emplace<ecs::Container>(corpse,
                                             std::move(corpseInventory));
            ecs::LootContainer lootState;
            lootState.tableId = "companion_corpse";
            lootState.searched = true;
            registry.emplace<ecs::LootContainer>(corpse, std::move(lootState));
            return corpse;
        });
}

bool CompanionSystem::TryRescue(EcsWorld& world, ecs::Entity companion,
                                ecs::Entity rescuer, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    auto fail = [error](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    if (companion == rescuer) {
        return fail("同伴无法自救");
    }
    if (!world.Alive(companion) || !world.Alive(rescuer)) {
        return fail("同伴或救援者不存在");
    }
    entt::registry& registry = world.Registry();
    CompanionState* state = registry.try_get<CompanionState>(companion);
    ecs::Health* health = registry.try_get<ecs::Health>(companion);
    const auto* companionPosition = registry.try_get<ecs::Position>(companion);
    const auto* rescuerPosition = registry.try_get<ecs::Position>(rescuer);
    if (state == nullptr || health == nullptr || companionPosition == nullptr ||
        rescuerPosition == nullptr) {
        return fail("同伴状态或位置无效");
    }
    if (state->dead) {
        return fail("同伴已死亡");
    }
    if (!state->downed) {
        return fail("同伴未倒地");
    }
    if (companionPosition->z != rescuerPosition->z) {
        return fail("救援者与同伴不在同一层");
    }
    if (!(health->max > 0.0f) || !std::isfinite(health->max)) {
        return fail("同伴生命上限无效");
    }
    if (const auto* rescuerHealth = registry.try_get<ecs::Health>(rescuer);
        rescuerHealth != nullptr && rescuerHealth->current <= 0.0f) {
        return fail("救援者已倒地");
    }
    const float rescueRange = std::max(0.0f, m_options.rescueRange);
    if (DistSq(companionPosition->x, companionPosition->y,
               rescuerPosition->x, rescuerPosition->y) >
        rescueRange * rescueRange) {
        return fail("救援距离过远");
    }

    state->downed = false;
    state->downedRemaining = 0.0f;
    state->command = CompanionCommand::Follow;
    state->standby = false;
    health->current = std::max(
        1.0f, health->max *
                  std::clamp(m_options.rescueHealthFraction, 0.0f, 1.0f));
    if (auto* action = registry.try_get<ecs::ActionState>(companion);
        action != nullptr) {
        action->action = ecs::ActorAction::Idle;
        action->elapsed = 0.0f;
        action->duration = 0.0f;
    }
    if (auto* ai = registry.try_get<CompanionAiState>(companion);
        ai != nullptr) {
        ai->targetId = 0;
        ai->engaged = false;
        ai->autoRetreat = false;
        ai->hasAnchor = false;
        ai->lastCommand = CompanionCommand::Follow;
        ai->repathTimer = 0.0f;
    }
    Stop(world, companion);
    return true;
}

void CompanionSystem::Update(EcsWorld& world, float dt) {
    m_stats.follow = 0;
    m_stats.stay = 0;
    m_stats.retreat = 0;
    m_stats.frozen = 0;
    m_stats.standby = 0;
    m_stats.engaged = 0;
    m_stats.downed = 0;
    m_stats.deaths = 0;
    if (!std::isfinite(dt) || dt < 0.0f) {
        dt = 0.0f;
    }

    entt::registry& registry = world.Registry();
    registry.view<Companion, CompanionState, ecs::Position, ecs::Health>()
        .each([&](const ecs::Entity entity, const Companion& identity,
                  CompanionState& state, const ecs::Position& position,
                  const ecs::Health& health) {
            CompanionAiState* ai =
                registry.try_get<CompanionAiState>(entity);
            if (ai == nullptr) {
                ai = &registry.emplace<CompanionAiState>(entity);
                ai->homeX = position.x;
                ai->homeY = position.y;
                ai->anchorX = position.x;
                ai->anchorY = position.y;
            }
            if (m_companions != nullptr) {
                if (const CompanionDef* def = m_companions->Find(identity.defId);
                    def != nullptr) {
                    RefreshCompanionQuestState(*def, &state);
                }
            }

            ai->attackCooldown =
                std::max(0.0f, ai->attackCooldown - dt);
            ai->repathTimer = std::max(0.0f, ai->repathTimer - dt);

            if (state.command != ai->lastCommand) {
                ai->lastCommand = state.command;
                ai->targetId = 0;
                ai->engaged = false;
                ai->autoRetreat = false;
                ai->hasAnchor = false;
                ai->repathTimer = 0.0f;
                Stop(world, entity);
                if (state.command == CompanionCommand::Stay) {
                    ai->anchorX = position.x;
                    ai->anchorY = position.y;
                    ai->hasAnchor = true;
                }
            }

            if (!registry.all_of<ecs::FriendlyNpc>(entity)) {
                registry.emplace<ecs::FriendlyNpc>(entity);
            }
            if (!registry.all_of<ecs::NoCombatTarget>(entity)) {
                registry.emplace<ecs::NoCombatTarget>(entity);
            }
            if (m_items != nullptr) {
                (void)RefreshCompanionDerivedStats(world, entity, state,
                                                   m_items);
            }

            if (state.dead) {
                ai->targetId = 0;
                ai->engaged = false;
                Stop(world, entity);
                return;
            }
            if (state.downed) {
                ++m_stats.downed;
                ai->targetId = 0;
                ai->engaged = false;
                Stop(world, entity);
                state.downedRemaining =
                    std::max(0.0f, state.downedRemaining - dt);
                if (state.downedRemaining <= 0.0f) {
                    KillCompanion(world, entity, state, *ai);
                }
                return;
            }
            if (health.current <= 0.0f) {
                EnterDowned(world, entity, state, *ai);
                return;
            }
            if (state.standby) {
                ++m_stats.standby;
                ai->targetId = 0;
                ai->engaged = false;
                Stop(world, entity);
                return;
            }

            const ecs::Entity owner = world.FindById(identity.ownerStableId);
            const auto* ownerPosition =
                owner != EcsWorld::kNull
                    ? registry.try_get<ecs::Position>(owner)
                    : nullptr;
            const auto* ownerHealth =
                owner != EcsWorld::kNull
                    ? registry.try_get<ecs::Health>(owner)
                    : nullptr;
            if (owner == EcsWorld::kNull || ownerPosition == nullptr ||
                ownerHealth == nullptr || ownerHealth->current <= 0.0f) {
                ai->targetId = 0;
                ai->engaged = false;
                Stop(world, entity);
                return;
            }

            if (position.z != ownerPosition->z) {
                ai->frozen = true;
                ++m_stats.frozen;
                ai->targetId = 0;
                ai->engaged = false;
                Stop(world, entity);
                return;
            }

            const float ownerDistanceSq =
                DistSq(position.x, position.y, ownerPosition->x,
                       ownerPosition->y);
            if (ownerDistanceSq >
                m_options.freezeRadius * m_options.freezeRadius) {
                ai->frozen = true;
                ++m_stats.frozen;
                ai->targetId = 0;
                ai->engaged = false;
                Stop(world, entity);
                return;
            }
            ai->frozen = false;

            if (TrapNear(world, position.x, position.y, 0.0f)) {
                int safeX = 0;
                int safeY = 0;
                ai->targetId = 0;
                ai->engaged = false;
                if (FindSafeGoal(world, position.x, position.y, &safeX,
                                 &safeY, m_options.trapClearance)) {
                    (void)RequestPath(
                        world, entity, *ai,
                        static_cast<float>(safeX) + 0.5f,
                        static_cast<float>(safeY) + 0.5f);
                } else {
                    Stop(world, entity);
                }
                return;
            }

            if (CompanionNeedsRetreat(health,
                                      m_options.lowHealthFraction)) {
                ai->autoRetreat = true;
            } else if (ai->autoRetreat &&
                       health.current >=
                           health.max * m_options.recoverHealthFraction) {
                ai->autoRetreat = false;
            }

            if (state.command == CompanionCommand::Retreat ||
                ai->autoRetreat) {
                ++m_stats.retreat;
                UpdateRetreat(world, entity, owner, state, *ai, position);
                return;
            }
            if (state.command == CompanionCommand::Stay) {
                ++m_stats.stay;
                UpdateStay(world, entity, state, *ai, position, dt);
                return;
            }

            ++m_stats.follow;
            UpdateFollow(world, entity, owner, state, *ai, position, dt);
        });
}

float CompanionSystem::OwnerFacingX(const EcsWorld& world,
                                    ecs::Entity owner) const {
    const auto* facing = world.Registry().try_get<ecs::Facing>(owner);
    if (facing == nullptr) {
        return 0.0f;
    }
    switch (facing->dir) {
        case ecs::Direction::Left:
            return -1.0f;
        case ecs::Direction::Right:
            return 1.0f;
        case ecs::Direction::Up:
        case ecs::Direction::Down:
            return 0.0f;
    }
    return 0.0f;
}

float CompanionSystem::OwnerFacingY(const EcsWorld& world,
                                    ecs::Entity owner) const {
    const auto* facing = world.Registry().try_get<ecs::Facing>(owner);
    if (facing == nullptr) {
        return 1.0f;
    }
    switch (facing->dir) {
        case ecs::Direction::Left:
        case ecs::Direction::Right:
            return 0.0f;
        case ecs::Direction::Up:
            return -1.0f;
        case ecs::Direction::Down:
            return 1.0f;
    }
    return 1.0f;
}

bool CompanionSystem::OwnerAttacking(const EcsWorld& world,
                                     ecs::Entity owner) const {
    const entt::registry& registry = world.Registry();
    if (const auto* action = registry.try_get<ecs::ActionState>(owner);
        action != nullptr) {
        if (action->action == ecs::ActorAction::Attacking) {
            return true;
        }
    }
    if (const auto* attack = registry.try_get<ecs::AttackIntent>(owner);
        attack != nullptr && attack->pending) {
        return true;
    }
    return false;
}

bool CompanionSystem::HasLineOfSight(EcsWorld&, float sx, float sy, float tx,
                                     float ty) const {
    if (!m_walkable) {
        return true;
    }
    const float dx = tx - sx;
    const float dy = ty - sy;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= 0.001f) {
        return true;
    }
    const int steps = std::max(2, static_cast<int>(std::ceil(distance / 0.35f)));
    for (int i = 1; i < steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const int gx = static_cast<int>(std::floor(sx + dx * t));
        const int gy = static_cast<int>(std::floor(sy + dy * t));
        if (!m_walkable(gx, gy)) {
            return false;
        }
    }
    return true;
}

bool CompanionSystem::TrapNear(EcsWorld& world, float x, float y,
                               float clearance) const {
    bool near = false;
    world.Registry().view<ecs::SpikeTrap, ecs::Position>().each(
        [&](const ecs::Entity, const ecs::SpikeTrap& trap,
            const ecs::Position& position) {
            if (near || !trap.armed || trap.uses <= 0 || position.z != 0) {
                return;
            }
            const float radius = trap.radius + std::max(0.0f, clearance);
            if (DistSq(x, y, position.x, position.y) <= radius * radius) {
                near = true;
            }
        });
    if (near) {
        return true;
    }
    world.Registry().view<ecs::PlacedTrap, ecs::Position>().each(
        [&](const ecs::Entity, const ecs::PlacedTrap& trap,
            const ecs::Position& position) {
            if (near || !trap.armed || trap.uses <= 0 || position.z != 0) {
                return;
            }
            const TrapDef* def =
                m_traps != nullptr ? m_traps->Find(trap.defId) : nullptr;
            const float trapRadius = def != nullptr ? def->radius : 1.0f;
            const float radius =
                trapRadius + std::max(0.0f, clearance);
            if (DistSq(x, y, position.x, position.y) <= radius * radius) {
                near = true;
            }
        });
    return near;
}

bool CompanionSystem::FindSafeGoal(EcsWorld& world, float x, float y, int* gx,
                                   int* gy, float clearance) const {
    if (gx == nullptr || gy == nullptr || !m_walkable) {
        return false;
    }
    int baseX = 0;
    int baseY = 0;
    if (!ai::FindNearestWalkable(
            static_cast<int>(std::floor(x)),
            static_cast<int>(std::floor(y)), 2, m_walkable, &baseX,
            &baseY)) {
        return false;
    }
    if (!TrapNear(world, static_cast<float>(baseX) + 0.5f,
                  static_cast<float>(baseY) + 0.5f, clearance)) {
        *gx = baseX;
        *gy = baseY;
        return true;
    }

    constexpr float kTwoPi = 6.28318530718f;
    for (float radius : {1.5f, 3.0f, 4.5f}) {
        for (int step = 0; step < 8; ++step) {
            const float angle =
                static_cast<float>(step) * kTwoPi / 8.0f;
            int candidateX = 0;
            int candidateY = 0;
            if (!ai::FindNearestWalkable(
                    static_cast<int>(std::floor(x + std::cos(angle) * radius)),
                    static_cast<int>(std::floor(y + std::sin(angle) * radius)),
                    1, m_walkable, &candidateX, &candidateY)) {
                continue;
            }
            if (!TrapNear(world, static_cast<float>(candidateX) + 0.5f,
                          static_cast<float>(candidateY) + 0.5f,
                          clearance)) {
                *gx = candidateX;
                *gy = candidateY;
                return true;
            }
        }
    }
    return false;
}

void CompanionSystem::Stop(EcsWorld& world, ecs::Entity entity) const {
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
        request->newRequest = false;
        request->queryId = 0;
    }
    if (auto* intent = registry.try_get<ecs::MoveIntent>(entity);
        intent != nullptr) {
        intent->x = 0.0f;
        intent->y = 0.0f;
    }
}

bool CompanionSystem::RequestPath(EcsWorld& world, ecs::Entity entity,
                                  CompanionAiState& ai, float x,
                                  float y) const {
    auto* request = world.Registry().try_get<ecs::PathRequest>(entity);
    auto* follow = world.Registry().try_get<ecs::PathFollow>(entity);
    if (request == nullptr || follow == nullptr) {
        return false;
    }
    int gx = 0;
    int gy = 0;
    if (!FindSafeGoal(world, x, y, &gx, &gy, m_options.trapClearance)) {
        return false;
    }
    const bool sameGoal = request->goalX == gx && request->goalY == gy;
    const bool busy = request->state == ecs::PathStatus::Pending ||
                      request->state == ecs::PathStatus::Following;
    if (sameGoal && busy) {
        return false;
    }
    if (ai.repathTimer > 0.0f && !sameGoal) {
        return false;
    }
    request->goalX = gx;
    request->goalY = gy;
    request->newRequest = true;
    ai.repathTimer = m_options.pathRetrySeconds;
    return true;
}

void CompanionSystem::FollowGoal(EcsWorld& world, ecs::Entity companion,
                                 ecs::Entity owner, float* x,
                                 float* y) const {
    const auto* position = world.Registry().try_get<ecs::Position>(owner);
    if (position == nullptr || x == nullptr || y == nullptr) {
        return;
    }
    const float fx = OwnerFacingX(world, owner);
    const float fy = OwnerFacingY(world, owner);
    const float side =
        (world.IdOf(companion).id & 1ull) != 0 ? 0.85f : -0.85f;
    *x = position->x - fx * m_options.followDistance + (-fy) * side;
    *y = position->y - fy * m_options.followDistance + fx * side;
}

ecs::Entity CompanionSystem::FindThreat(EcsWorld& world,
                                        ecs::Entity companion,
                                        float originX, float originY,
                                        float radius,
                                        float companionRadius) const {
    if (radius <= 0.0f) {
        return EcsWorld::kNull;
    }
    const float radiusSq = radius * radius;
    const float companionRadiusSq = companionRadius * companionRadius;
    ecs::Entity best = EcsWorld::kNull;
    float bestDistSq = std::numeric_limits<float>::max();
    entt::registry& registry = world.Registry();
    const auto* companionPosition =
        registry.try_get<ecs::Position>(companion);
    if (companionPosition == nullptr) {
        return EcsWorld::kNull;
    }

    const auto consider = [&](ecs::Entity entity) {
        if (!IsCompanionHostileTarget(world, entity)) {
            return;
        }
        const auto* position = registry.try_get<ecs::Position>(entity);
        if (position == nullptr || position->z != companionPosition->z) {
            return;
        }
        const float originDistSq =
            DistSq(originX, originY, position->x, position->y);
        if (originDistSq > radiusSq) {
            return;
        }
        if (DistSq(companionPosition->x, companionPosition->y, position->x,
                   position->y) > companionRadiusSq) {
            return;
        }
        if (originDistSq > bestDistSq) {
            return;
        }
        if (originDistSq == bestDistSq && best != EcsWorld::kNull) {
            const auto* bestPosition =
                registry.try_get<ecs::Position>(best);
            if (bestPosition != nullptr &&
                DistSq(companionPosition->x, companionPosition->y,
                       bestPosition->x, bestPosition->y) <=
                    DistSq(companionPosition->x, companionPosition->y,
                           position->x, position->y)) {
                return;
            }
        }
        bestDistSq = originDistSq;
        best = entity;
    };

    registry.view<ecs::AiState, ecs::Position, ecs::Health>().each(
        [&](const ecs::Entity entity, const ecs::AiState&,
            const ecs::Position&, const ecs::Health&) { consider(entity); });
    registry.view<SettlementRaidZombie, ecs::Position, ecs::Health>().each(
        [&](const ecs::Entity entity, const SettlementRaidZombie&,
            const ecs::Position&, const ecs::Health&) { consider(entity); });
    registry.view<ecs::BaseSiegeZombie, ecs::Position, ecs::Health>().each(
        [&](const ecs::Entity entity, const ecs::Position&,
            const ecs::Health&) { consider(entity); });
    return best;
}

bool CompanionSystem::TargetValid(EcsWorld& world, ecs::Entity target,
                                  float originX, float originY,
                                  float leash) const {
    if (!IsCompanionHostileTarget(world, target)) {
        return false;
    }
    const auto* position = world.Registry().try_get<ecs::Position>(target);
    if (position == nullptr) {
        return false;
    }
    return DistSq(originX, originY, position->x, position->y) <=
           leash * leash;
}

bool CompanionSystem::UpdateCombat(EcsWorld& world, ecs::Entity companion,
                                   CompanionState& state, CompanionAiState& ai,
                                   const ecs::Position& position,
                                   float originX, float originY, float leash,
                                   float) {
    ecs::Entity target = world.FindById(ai.targetId);
    if (!TargetValid(world, target, originX, originY, leash)) {
        ai.targetId = 0;
        ai.engaged = false;
        return false;
    }
    const auto* targetPosition =
        world.Registry().try_get<ecs::Position>(target);
    if (targetPosition == nullptr || targetPosition->z != position.z) {
        ai.targetId = 0;
        ai.engaged = false;
        return false;
    }
    ai.engaged = true;

    const CompanionAttackProfile profile =
        WeaponProfile(state, m_items, m_options);
    const float dx = targetPosition->x - position.x;
    const float dy = targetPosition->y - position.y;
    const float distanceSq = dx * dx + dy * dy;
    if (distanceSq <= profile.range * profile.range &&
        HasLineOfSight(world, position.x, position.y, targetPosition->x,
                       targetPosition->y)) {
        Stop(world, companion);
        if (auto* facing = world.Registry().try_get<ecs::Facing>(companion);
            facing != nullptr) {
            SetFacing(facing, dx, dy);
        }
        if (ai.attackCooldown <= 0.0f) {
            Attack(world, companion, state, target, position);
            ai.attackCooldown = profile.cooldown;
        }
        return true;
    }

    if (ai.repathTimer <= 0.0f) {
        (void)RequestPath(world, companion, ai, targetPosition->x,
                          targetPosition->y);
    }
    return true;
}

void CompanionSystem::Attack(EcsWorld& world, ecs::Entity companion,
                             CompanionState& state, ecs::Entity target,
                             const ecs::Position& position) {
    if (!IsCompanionHostileTarget(world, target)) {
        return;
    }
    entt::registry& registry = world.Registry();
    const CompanionAttackProfile profile =
        WeaponProfile(state, m_items, m_options);
    if (auto* action = registry.try_get<ecs::ActionState>(companion);
        action != nullptr) {
        action->action = ecs::ActorAction::Attacking;
        action->elapsed = 0.0f;
        action->duration = 0.28f;
    }
    if (m_damage) {
        (void)m_damage(companion, target, profile.damage,
                       ecs::DamageType::Blunt);
    } else {
        if (auto* health = registry.try_get<ecs::Health>(target);
            health != nullptr) {
            health->current = std::max(0.0f, health->current - profile.damage);
        }
    }
    if (const auto* targetHealth = registry.try_get<ecs::Health>(target);
        targetHealth != nullptr && targetHealth->current <= 0.0f) {
        ++m_stats.kills;
        AddCompanionTrust(&state, std::max(0, m_options.combatKillTrust));
        if (m_companions != nullptr) {
            const auto* identity =
                registry.try_get<Companion>(companion);
            const CompanionDef* def =
                identity != nullptr ? m_companions->Find(identity->defId)
                                    : nullptr;
            if (def != nullptr) {
                (void)RecordCompanionQuestEvent(
                    *def, &state, CompanionQuestObjectiveKind::Kill, "any", 1,
                    m_items, nullptr);
            }
        }
    }
    if (m_items != nullptr && profile.wear > 0.0f) {
        (void)items::WearEquippedWeapon(state.equipment, profile.wear);
    }
    if (const auto* targetPosition =
            registry.try_get<ecs::Position>(target);
        targetPosition != nullptr) {
        if (auto* facing = registry.try_get<ecs::Facing>(companion);
            facing != nullptr) {
            SetFacing(facing, targetPosition->x - position.x,
                      targetPosition->y - position.y);
        }
    }
    ++m_stats.attacks;
}

void CompanionSystem::UpdateFollow(EcsWorld& world, ecs::Entity companion,
                                   ecs::Entity owner, CompanionState& state,
                                   CompanionAiState& ai,
                                   const ecs::Position& position, float dt) {
    entt::registry& registry = world.Registry();
    const auto* ownerPosition = registry.try_get<ecs::Position>(owner);
    if (ownerPosition == nullptr) {
        Stop(world, companion);
        return;
    }

    bool ownerUnderAttack = false;
    const std::uint64_t ownerId = world.IdOf(owner).id;
    registry.view<ecs::AiState, ecs::Position, ecs::Health>().each(
        [&](const ecs::Entity entity, const ecs::AiState& ai,
            const ecs::Position&, const ecs::Health& health) {
            if (health.current > 0.0f && ai.targetId == ownerId &&
                IsCompanionHostileTarget(world, entity)) {
                ownerUnderAttack = true;
            }
        });

    float engageRadius = m_options.ownerUrgentRadius;
    if (ownerUnderAttack || OwnerAttacking(world, owner)) {
        engageRadius = m_options.ownerEngageRadius;
    }

    if (ai.targetId != 0 &&
        !TargetValid(world, world.FindById(ai.targetId), ownerPosition->x,
                     ownerPosition->y, m_options.leash)) {
        ai.targetId = 0;
        ai.engaged = false;
    }
    if (ai.targetId == 0) {
        const ecs::Entity target = FindThreat(
            world, companion, ownerPosition->x, ownerPosition->y,
            engageRadius, m_options.leash);
        if (target != EcsWorld::kNull) {
            ai.targetId = world.IdOf(target).id;
            ++m_stats.targetsAcquired;
        }
    }
    if (ai.targetId != 0) {
        ++m_stats.engaged;
        if (UpdateCombat(world, companion, state, ai, position,
                         ownerPosition->x, ownerPosition->y,
                         m_options.leash, dt)) {
            return;
        }
    }

    float goalX = ownerPosition->x;
    float goalY = ownerPosition->y;
    FollowGoal(world, companion, owner, &goalX, &goalY);
    const float ownerDistanceSq =
        DistSq(position.x, position.y, ownerPosition->x, ownerPosition->y);
    const float slotDistanceSq =
        DistSq(position.x, position.y, goalX, goalY);
    const bool closeEnough =
        ownerDistanceSq <= m_options.followMax * m_options.followMax &&
        slotDistanceSq <= 1.0f;
    if (closeEnough) {
        Stop(world, companion);
        return;
    }
    (void)RequestPath(world, companion, ai, goalX, goalY);
}

void CompanionSystem::UpdateStay(EcsWorld& world, ecs::Entity companion,
                                 CompanionState& state, CompanionAiState& ai,
                                 const ecs::Position& position, float dt) {
    if (!ai.hasAnchor) {
        ai.anchorX = position.x;
        ai.anchorY = position.y;
        ai.hasAnchor = true;
    }
    int anchorX = 0;
    int anchorY = 0;
    if (FindSafeGoal(world, ai.anchorX, ai.anchorY, &anchorX, &anchorY,
                     m_options.trapClearance)) {
        ai.anchorX = static_cast<float>(anchorX) + 0.5f;
        ai.anchorY = static_cast<float>(anchorY) + 0.5f;
    }

    if (ai.targetId != 0 &&
        !TargetValid(world, world.FindById(ai.targetId), ai.anchorX,
                     ai.anchorY, m_options.stayLeash)) {
        ai.targetId = 0;
        ai.engaged = false;
    }
    if (ai.targetId == 0) {
        const ecs::Entity target =
            FindThreat(world, companion, ai.anchorX, ai.anchorY,
                       m_options.stayEngageRadius, m_options.stayLeash);
        if (target != EcsWorld::kNull) {
            ai.targetId = world.IdOf(target).id;
            ++m_stats.targetsAcquired;
        }
    }
    if (ai.targetId != 0) {
        ++m_stats.engaged;
        if (UpdateCombat(world, companion, state, ai, position, ai.anchorX,
                         ai.anchorY, m_options.stayLeash, dt)) {
            return;
        }
    }

    if (DistSq(position.x, position.y, ai.anchorX, ai.anchorY) > 1.2f) {
        (void)RequestPath(world, companion, ai, ai.anchorX, ai.anchorY);
    } else {
        Stop(world, companion);
    }
}

void CompanionSystem::UpdateRetreat(EcsWorld& world, ecs::Entity companion,
                                    ecs::Entity owner, CompanionState&,
                                    CompanionAiState& ai,
                                    const ecs::Position& position) {
    ai.targetId = 0;
    ai.engaged = false;
    float goalX = ai.homeX;
    float goalY = ai.homeY;
    if (owner != EcsWorld::kNull) {
        if (const auto* ownerPosition =
                world.Registry().try_get<ecs::Position>(owner);
            ownerPosition != nullptr) {
            goalX = ownerPosition->x;
            goalY = ownerPosition->y;
        }
    }
    if (!std::isfinite(goalX) || !std::isfinite(goalY) ||
        DistSq(position.x, position.y, goalX, goalY) <=
            m_options.followMin * m_options.followMin) {
        Stop(world, companion);
        return;
    }
    (void)RequestPath(world, companion, ai, goalX, goalY);
}

}  // namespace zl::game
