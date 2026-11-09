#pragma once

#include <cstdint>

#include "game/Forage.h"
#include "game/ForageComponents.h"
#include "game/Weather.h"

namespace zl::game {

float ForageSearchSeconds(const ForageZoneDef& zone, const WeatherState& weather,
                          int survivalLevel);

bool ForageNodeReady(const ecs::ForageState& state, const ForageNode& node,
                     double nowMinutes);

double ForageRespawnRemainingMinutes(const ecs::ForageState& state,
                                     const ForageNode& node,
                                     double nowMinutes);

void ForageMarkHarvested(ecs::ForageState& state, const ForageNode& node,
                         int respawnMinutes, double nowMinutes);

void ForagePrune(ecs::ForageState& state, double nowMinutes);

std::uint64_t ForageLootSeed(std::uint64_t baseSeed, const ForageNode& node);

}  // namespace zl::game
