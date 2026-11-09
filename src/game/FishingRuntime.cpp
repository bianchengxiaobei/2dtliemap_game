#include "game/FishingRuntime.h"

#include <algorithm>
#include <cstdint>

namespace zl::game {
namespace {

std::uint64_t Mix(std::uint64_t hash, std::uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ull;
    return hash;
}

double UnitHash(std::uint64_t value) {
    return static_cast<double>(value >> 11U) * (1.0 / 9007199254740992.0);
}

}  // namespace

float FishingWaitSeconds(const FishingSpotDef& spot, const WeatherState& weather,
                         int survivalLevel) {
    float seconds = std::max(0.01f, spot.waitSeconds);
    seconds *= std::max(
        0.65f, 1.0f - 0.03f * static_cast<float>(std::max(0, survivalLevel)));

    float weatherScale = 1.0f;
    const float intensity = std::clamp(weather.intensity, 0.0f, 1.0f);
    switch (weather.kind) {
        case WeatherKind::Rain:
            weatherScale += 0.10f * intensity;
            break;
        case WeatherKind::Fog:
            weatherScale += 0.15f * intensity;
            break;
        case WeatherKind::ColdSnap:
            weatherScale += 0.20f * intensity;
            break;
        case WeatherKind::Clear:
            break;
    }
    if (weather.night) {
        weatherScale += 0.10f;
    }
    return std::clamp(seconds * weatherScale, 1.0f, 60.0f);
}

float FishingCatchChance(const FishingSpotDef& spot, const WeatherState& weather,
                         int survivalLevel) {
    float chance = std::clamp(spot.catchChance, 0.05f, 0.95f);
    chance +=
        std::min(0.20f, 0.03f * static_cast<float>(std::max(0, survivalLevel)));
    const float intensity = std::clamp(weather.intensity, 0.0f, 1.0f);
    switch (weather.kind) {
        case WeatherKind::Rain:
            chance += 0.05f * intensity;
            break;
        case WeatherKind::Fog:
            chance -= 0.03f * intensity;
            break;
        case WeatherKind::ColdSnap:
            chance -= 0.05f * intensity;
            break;
        case WeatherKind::Clear:
            break;
    }
    if (weather.night) {
        chance -= 0.04f;
    }
    return std::clamp(chance, 0.05f, 0.95f);
}

std::uint64_t FishingLootSeed(std::uint64_t baseSeed, const FishingSpot& spot,
                              bool success) {
    std::uint64_t hash = 1469598103934665603ull ^ baseSeed;
    for (const char c : spot.spotId) {
        hash = Mix(hash, static_cast<std::uint8_t>(c));
    }
    hash = Mix(hash, static_cast<std::uint32_t>(spot.x));
    hash = Mix(hash, static_cast<std::uint32_t>(spot.y));
    hash = Mix(hash, static_cast<std::uint32_t>(spot.z));
    hash = Mix(hash, static_cast<std::uint32_t>(spot.waterX));
    hash = Mix(hash, static_cast<std::uint32_t>(spot.waterY));
    hash = Mix(hash, static_cast<std::uint32_t>(spot.waterZ));
    hash = Mix(hash, success ? 1u : 0u);
    return hash;
}

bool FishingAttemptSucceeds(std::uint64_t baseSeed, const FishingSpot& spot,
                            float catchChance) {
    const double roll = UnitHash(FishingLootSeed(baseSeed, spot, false));
    return roll < static_cast<double>(std::clamp(catchChance, 0.0f, 1.0f));
}

}  // namespace zl::game
