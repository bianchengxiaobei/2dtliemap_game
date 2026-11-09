#include "game/ForageRuntime.h"

#include <algorithm>
#include <cmath>

namespace zl::game {
namespace {

bool SameNode(const ecs::ForageHarvest& harvest, const ForageNode& node) {
    return harvest.zoneId == node.zoneId && harvest.x == node.x &&
           harvest.y == node.y && harvest.z == node.z;
}

std::uint64_t Mix(std::uint64_t hash, std::uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ull;
    return hash;
}

}  // namespace

float ForageSearchSeconds(const ForageZoneDef& zone, const WeatherState& weather,
                          int survivalLevel) {
    float seconds = std::max(0.01f, zone.searchSeconds);
    seconds *= std::max(0.55f, 1.0f - 0.035f * static_cast<float>(std::max(0, survivalLevel)));

    float weatherScale = 1.0f;
    const float intensity = std::clamp(weather.intensity, 0.0f, 1.0f);
    switch (weather.kind) {
        case WeatherKind::Rain:
            weatherScale += 0.20f * intensity;
            break;
        case WeatherKind::Fog:
            weatherScale += 0.15f * intensity;
            break;
        case WeatherKind::ColdSnap:
            weatherScale += 0.25f * intensity;
            break;
        case WeatherKind::Clear:
            break;
    }
    if (weather.night) {
        weatherScale += 0.08f;
    }
    return std::clamp(seconds * weatherScale, 0.5f, 30.0f);
}

bool ForageNodeReady(const ecs::ForageState& state, const ForageNode& node,
                     double nowMinutes) {
    for (const ecs::ForageHarvest& harvest : state.harvested) {
        if (SameNode(harvest, node)) {
            return nowMinutes >= harvest.availableAtMinutes;
        }
    }
    return true;
}

double ForageRespawnRemainingMinutes(const ecs::ForageState& state,
                                     const ForageNode& node,
                                     double nowMinutes) {
    for (const ecs::ForageHarvest& harvest : state.harvested) {
        if (SameNode(harvest, node)) {
            return std::max(0.0, harvest.availableAtMinutes - nowMinutes);
        }
    }
    return 0.0;
}

void ForageMarkHarvested(ecs::ForageState& state, const ForageNode& node,
                         int respawnMinutes, double nowMinutes) {
    state.harvested.erase(
        std::remove_if(state.harvested.begin(), state.harvested.end(),
                       [&](const ecs::ForageHarvest& harvest) {
                           return SameNode(harvest, node);
                       }),
        state.harvested.end());
    if (respawnMinutes <= 0) {
        return;
    }
    while (state.harvested.size() >= ecs::kMaxForageHarvests) {
        auto oldest = std::min_element(
            state.harvested.begin(), state.harvested.end(),
            [](const ecs::ForageHarvest& left, const ecs::ForageHarvest& right) {
                return left.availableAtMinutes < right.availableAtMinutes;
            });
        if (oldest == state.harvested.end()) {
            break;
        }
        state.harvested.erase(oldest);
    }
    state.harvested.push_back(ecs::ForageHarvest{
        node.zoneId, node.x, node.y, node.z,
        nowMinutes + static_cast<double>(std::max(0, respawnMinutes))});
}

void ForagePrune(ecs::ForageState& state, double nowMinutes) {
    state.harvested.erase(
        std::remove_if(state.harvested.begin(), state.harvested.end(),
                       [&](const ecs::ForageHarvest& harvest) {
                           return harvest.availableAtMinutes <= nowMinutes;
                       }),
        state.harvested.end());
}

std::uint64_t ForageLootSeed(std::uint64_t baseSeed, const ForageNode& node) {
    std::uint64_t hash = 1469598103934665603ull ^ baseSeed;
    for (const char c : node.zoneId) {
        hash = Mix(hash, static_cast<std::uint8_t>(c));
    }
    hash = Mix(hash, static_cast<std::uint32_t>(node.x));
    hash = Mix(hash, static_cast<std::uint32_t>(node.y));
    hash = Mix(hash, static_cast<std::uint32_t>(node.z));
    return hash;
}

}  // namespace zl::game
