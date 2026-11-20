#include "game/TrapSystem.h"

#include <algorithm>
#include <cmath>

#include "ai/components/AiComponents.h"
#include "characters/components/AnimalComponents.h"
#include "characters/components/CharacterComponents.h"
#include "ecs/components/CommonComponents.h"
#include "game/Companion.h"
#include "game/SettlementNpc.h"

namespace zl::game {
namespace {

constexpr float kDefaultGameMinutesPerRealSecond = 0.4f;
constexpr float kMaxTrapFireRadius = 3.0f;
constexpr float kMaxTrapFireSeconds = 1.0e7f;
constexpr float kReadyEpsilon = 0.0001f;

}  // namespace

const char* TrapTargetKindName(TrapTargetKind kind) {
    switch (kind) {
        case TrapTargetKind::Zombie:
            return "zombie";
        case TrapTargetKind::Animal:
            return "animal";
        case TrapTargetKind::None:
            return "none";
    }
    return "none";
}

bool TrapCanActivate(const ecs::PlacedTrap& trap, const TrapDef& def) {
    return !def.id.empty() && trap.armed && trap.uses > 0 &&
           std::isfinite(trap.cooldown) && trap.cooldown <= kReadyEpsilon &&
           std::isfinite(trap.armRemaining) &&
           trap.armRemaining <= kReadyEpsilon;
}

bool TrapTargetAllowed(const TrapDef& def, TrapTargetKind kind) {
    if (kind == TrapTargetKind::Zombie) {
        return (def.triggerMask & kTrapTriggerZombie) != 0;
    }
    if (kind == TrapTargetKind::Animal) {
        return (def.triggerMask & kTrapTriggerAnimal) != 0;
    }
    return false;
}

TrapTargetKind ClassifyTrapTarget(const entt::registry& registry,
                                  ecs::Entity entity) {
    if (entity == ecs::EcsWorld::kNull || !registry.valid(entity)) {
        return TrapTargetKind::None;
    }
    const auto* health = registry.try_get<ecs::Health>(entity);
    if (health == nullptr || !(health->current > 0.0f) ||
        registry.any_of<ecs::Dying>(entity)) {
        return TrapTargetKind::None;
    }
    if (const auto* ai = registry.try_get<ecs::AiState>(entity);
        ai != nullptr && ai->status == ecs::AiStatus::Dead) {
        return TrapTargetKind::None;
    }
    if (registry.any_of<ecs::PlayerTag, ecs::FriendlyNpc, Companion,
                        SettlementNpc, ecs::NoCombatTarget, ecs::AnimalCorpse>(
            entity)) {
        return TrapTargetKind::None;
    }
    if (registry.all_of<ecs::Animal>(entity)) {
        return TrapTargetKind::Animal;
    }
    if (const auto* raid = registry.try_get<SettlementRaidZombie>(entity);
        raid != nullptr) {
        return raid->faction == ContestedFaction::Infected
                   ? TrapTargetKind::Zombie
                   : TrapTargetKind::None;
    }
    if (registry.any_of<ecs::ZombieVariant, ecs::BaseSiegeZombie>(entity)) {
        return TrapTargetKind::Zombie;
    }
    return TrapTargetKind::None;
}

std::vector<TrapTargetCandidate> CollectTrapTargets(
    const ecs::EcsWorld& world) {
    std::vector<TrapTargetCandidate> candidates;
    const entt::registry& registry = world.Registry();
    registry.view<ecs::Health, ecs::Position>().each(
        [&](const ecs::Entity entity, const ecs::Health&,
            const ecs::Position& position) {
            const TrapTargetKind kind = ClassifyTrapTarget(registry, entity);
            if (kind == TrapTargetKind::None) {
                return;
            }
            candidates.push_back(TrapTargetCandidate{
                entity, position.x, position.y, position.z, kind});
        });
    return candidates;
}

ecs::Entity FindTrapTarget(
    const std::vector<TrapTargetCandidate>& candidates,
    const ecs::Position& trapPosition, const TrapDef& def,
    TrapTargetKind* outKind) {
    if (!std::isfinite(trapPosition.x) || !std::isfinite(trapPosition.y) ||
        !std::isfinite(def.radius) || def.radius <= 0.0f) {
        return ecs::EcsWorld::kNull;
    }
    ecs::Entity target = ecs::EcsWorld::kNull;
    TrapTargetKind bestKind = TrapTargetKind::None;
    float bestDistance = def.radius * def.radius;
    for (const TrapTargetCandidate& candidate : candidates) {
        if (candidate.z != trapPosition.z ||
            !TrapTargetAllowed(def, candidate.kind)) {
            continue;
        }
        const float dx = candidate.x - trapPosition.x;
        const float dy = candidate.y - trapPosition.y;
        const float distance = dx * dx + dy * dy;
        if (distance > bestDistance) {
            continue;
        }
        bestDistance = distance;
        target = candidate.entity;
        bestKind = candidate.kind;
    }
    if (outKind != nullptr) {
        *outKind = bestKind;
    }
    return target;
}

bool AdvancePlacedTrapCooldown(ecs::PlacedTrap& trap, float seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0f ||
        !std::isfinite(trap.cooldown) || trap.cooldown <= 0.0f) {
        return false;
    }
    trap.cooldown = std::max(0.0f, trap.cooldown - seconds);
    return trap.cooldown <= kReadyEpsilon;
}

bool ConsumePlacedTrapUse(ecs::PlacedTrap& trap, const TrapDef& def) {
    if (trap.uses <= 0) {
        return false;
    }
    --trap.uses;
    trap.cooldown = std::isfinite(def.cooldownSeconds)
                        ? std::max(0.0f, def.cooldownSeconds)
                        : 0.0f;
    if (trap.uses <= 0) {
        trap.uses = 0;
        trap.armed = false;
        trap.armRemaining = 0.0f;
    }
    return true;
}

float TrapFireDurationSeconds(float minutes,
                              float gameMinutesPerRealSecond) {
    if (!std::isfinite(minutes) || minutes <= 0.0f) {
        return 0.0f;
    }
    const float rate =
        std::isfinite(gameMinutesPerRealSecond) &&
                gameMinutesPerRealSecond > 0.0f
            ? gameMinutesPerRealSecond
            : kDefaultGameMinutesPerRealSecond;
    return std::clamp(minutes / rate, 0.0f, kMaxTrapFireSeconds);
}

std::vector<TrapFireCell> TrapFireAreaCells(float x, float y, float radius) {
    std::vector<TrapFireCell> cells;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius)) {
        return cells;
    }
    const int centerX = static_cast<int>(std::floor(x));
    const int centerY = static_cast<int>(std::floor(y));
    const float safeRadius =
        std::clamp(radius, 0.0f, kMaxTrapFireRadius);
    cells.push_back(TrapFireCell{centerX, centerY});
    const int extent = static_cast<int>(std::ceil(safeRadius));
    for (int ring = 1; ring <= extent; ++ring) {
        for (int dy = -ring; dy <= ring; ++dy) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != ring) {
                    continue;
                }
                const float distance =
                    std::sqrt(static_cast<float>(dx * dx + dy * dy));
                if (distance <= safeRadius + kReadyEpsilon) {
                    cells.push_back(
                        TrapFireCell{centerX + dx, centerY + dy});
                }
            }
        }
    }
    return cells;
}

}  // namespace zl::game
