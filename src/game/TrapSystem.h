#pragma once

#include <cstdint>
#include <vector>

#include "game/Traps.h"

namespace zl::game {

enum class TrapTargetKind : std::uint8_t {
    None = 0,
    Zombie = 1,
    Animal = 2,
};

struct TrapTargetCandidate {
    ecs::Entity entity = ecs::EcsWorld::kNull;
    float x = 0.0f;
    float y = 0.0f;
    int z = 0;
    TrapTargetKind kind = TrapTargetKind::None;
};

struct TrapFireCell {
    int cellX = 0;
    int cellY = 0;
};

const char* TrapTargetKindName(TrapTargetKind kind);
bool TrapCanActivate(const ecs::PlacedTrap& trap, const TrapDef& def);
bool TrapTargetAllowed(const TrapDef& def, TrapTargetKind kind);
TrapTargetKind ClassifyTrapTarget(const entt::registry& registry,
                                  ecs::Entity entity);
std::vector<TrapTargetCandidate> CollectTrapTargets(
    const ecs::EcsWorld& world);
ecs::Entity FindTrapTarget(const std::vector<TrapTargetCandidate>& candidates,
                           const ecs::Position& trapPosition,
                           const TrapDef& def,
                           TrapTargetKind* outKind = nullptr);
bool AdvancePlacedTrapCooldown(ecs::PlacedTrap& trap, float seconds);
bool ConsumePlacedTrapUse(ecs::PlacedTrap& trap, const TrapDef& def);
float TrapFireDurationSeconds(float minutes,
                              float gameMinutesPerRealSecond);
std::vector<TrapFireCell> TrapFireAreaCells(float x, float y, float radius);

}  // namespace zl::game
