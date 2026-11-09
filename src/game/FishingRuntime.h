#pragma once

#include <cstdint>

#include "game/Forage.h"
#include "game/Weather.h"

namespace zl::game {

// M23-F2a-T3：钓鱼等待/成功率与鱼获随机种子（纯规则，可无头单测）。
float FishingWaitSeconds(const FishingSpotDef& spot, const WeatherState& weather,
                         int survivalLevel);

float FishingCatchChance(const FishingSpotDef& spot, const WeatherState& weather,
                         int survivalLevel);

std::uint64_t FishingLootSeed(std::uint64_t baseSeed, const FishingSpot& spot,
                              bool success);

bool FishingAttemptSucceeds(std::uint64_t baseSeed, const FishingSpot& spot,
                            float catchChance);

}  // namespace zl::game
