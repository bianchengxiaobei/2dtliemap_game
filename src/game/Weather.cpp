#include "game/Weather.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace zl::game {
namespace {

struct WeatherProfile {
    WeatherKind kind = WeatherKind::Clear;
    float ambientC = 24.0f;
    float rainIntensity = 0.0f;
    float wetnessIntensity = 0.0f;
    float fogDensity = 0.0f;
    float sightScale = 1.0f;
    float moveScale = 1.0f;
};

float Clamp01(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

float ClampNonNegative(float value) {
    return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
}

std::uint64_t Mix(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30u)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27u)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31u);
}

float UnitFromHash(std::uint64_t value) {
    constexpr double kInv = 1.0 / 9007199254740992.0;  // 2^53
    return static_cast<float>(
        static_cast<double>(Mix(value) >> 11u) * kInv);
}

WeatherKind PickKind(const WeatherParams& params, std::uint64_t block) {
    const float clear = std::max(0.0f, params.clearWeight);
    const float rain = std::max(0.0f, params.rainWeight);
    const float fog = std::max(0.0f, params.fogWeight);
    const float cold = std::max(0.0f, params.coldSnapWeight);
    const float total = clear + rain + fog + cold;
    if (!(total > 0.0f)) {
        return WeatherKind::Clear;
    }
    const float roll = UnitFromHash(static_cast<std::uint64_t>(params.seed) ^
                                    Mix(block * 0x9E3779B97F4A7C15ull)) *
                       total;
    if (roll < clear) {
        return WeatherKind::Clear;
    }
    if (roll < clear + rain) {
        return WeatherKind::Rain;
    }
    if (roll < clear + rain + fog) {
        return WeatherKind::Fog;
    }
    return WeatherKind::ColdSnap;
}

WeatherProfile ProfileFor(const WeatherParams& params, WeatherKind kind) {
    WeatherProfile profile;
    profile.kind = kind;
    switch (kind) {
        case WeatherKind::Clear:
            profile.ambientC = params.clearTemperatureC;
            break;
        case WeatherKind::Rain:
            profile.ambientC = params.rainTemperatureC;
            profile.rainIntensity = 1.0f;
            profile.wetnessIntensity = Clamp01(params.rainWetness);
            profile.fogDensity = 0.12f;
            profile.sightScale = Clamp01(params.rainSightScale);
            profile.moveScale = Clamp01(params.rainMoveScale);
            break;
        case WeatherKind::Fog:
            profile.ambientC = params.fogTemperatureC;
            profile.wetnessIntensity = Clamp01(params.fogWetness);
            profile.fogDensity = 0.85f;
            profile.sightScale = Clamp01(params.fogSightScale);
            profile.moveScale = Clamp01(params.fogMoveScale);
            break;
        case WeatherKind::ColdSnap:
            profile.ambientC = params.coldSnapTemperatureC;
            profile.wetnessIntensity = Clamp01(params.coldSnapWetness);
            profile.fogDensity = 0.06f;
            profile.sightScale = Clamp01(params.coldSnapSightScale);
            profile.moveScale = Clamp01(params.coldSnapMoveScale);
            break;
    }
    return profile;
}

WeatherProfile BlockProfile(const WeatherParams& params, std::uint64_t index) {
    if (index == 0) {
        return ProfileFor(params, WeatherKind::Clear);
    }
    return ProfileFor(params, PickKind(params, index));
}

float SmoothStep(float value) {
    const float t = Clamp01(value);
    return t * t * (3.0f - 2.0f * t);
}

WeatherKind BlendKind(WeatherKind from, WeatherKind to, float blend) {
    return blend < 0.5f ? from : to;
}

WeatherParams Sanitize(WeatherParams params) {
    params.periodMinutes =
        std::max(30.0f, ClampNonNegative(params.periodMinutes));
    params.transitionMinutes = std::clamp(
        ClampNonNegative(params.transitionMinutes), 0.0f,
        std::min(params.periodMinutes, 120.0f));
    params.clearWeight = ClampNonNegative(params.clearWeight);
    params.rainWeight = ClampNonNegative(params.rainWeight);
    params.fogWeight = ClampNonNegative(params.fogWeight);
    params.coldSnapWeight = ClampNonNegative(params.coldSnapWeight);
    params.nightTemperatureDropC =
        ClampNonNegative(params.nightTemperatureDropC);
    params.clearTemperatureC = std::clamp(params.clearTemperatureC, -40.0f, 55.0f);
    params.rainTemperatureC = std::clamp(params.rainTemperatureC, -40.0f, 55.0f);
    params.fogTemperatureC = std::clamp(params.fogTemperatureC, -40.0f, 55.0f);
    params.coldSnapTemperatureC =
        std::clamp(params.coldSnapTemperatureC, -40.0f, 55.0f);
    params.rainSightScale = std::clamp(params.rainSightScale, 0.25f, 1.5f);
    params.fogSightScale = std::clamp(params.fogSightScale, 0.25f, 1.5f);
    params.coldSnapSightScale =
        std::clamp(params.coldSnapSightScale, 0.25f, 1.5f);
    params.rainMoveScale = std::clamp(params.rainMoveScale, 0.25f, 1.25f);
    params.fogMoveScale = std::clamp(params.fogMoveScale, 0.25f, 1.25f);
    params.coldSnapMoveScale =
        std::clamp(params.coldSnapMoveScale, 0.25f, 1.25f);
    params.rainWetness = Clamp01(params.rainWetness);
    params.fogWetness = Clamp01(params.fogWetness);
    params.coldSnapWetness = Clamp01(params.coldSnapWetness);
    return params;
}

}  // namespace

WeatherSystem::WeatherSystem(WeatherParams params)
    : m_params(Sanitize(params)) {}

WeatherState WeatherSystem::Sample(double totalGameMinutes, int hour) const {
    WeatherState state;
    if (!m_params.enabled || !std::isfinite(totalGameMinutes) ||
        totalGameMinutes < 0.0) {
        state.ambientC = m_params.clearTemperatureC;
        return state;
    }

    if (m_overrideUntil >= 0.0 && totalGameMinutes < m_overrideUntil) {
        const WeatherProfile profile = ProfileFor(m_params, m_overrideKind);
        state.kind = profile.kind;
        state.intensity = 1.0f;
        state.ambientC = profile.ambientC;
        state.rainIntensity = profile.rainIntensity;
        state.wetnessIntensity = profile.wetnessIntensity;
        state.fogDensity = profile.fogDensity;
        state.sightScale = profile.sightScale;
        state.moveScale = profile.moveScale;
        const int normalizedHour = std::clamp(hour, 0, 23);
        state.night = normalizedHour < 7 || normalizedHour >= 20;
        if (state.night) {
            state.ambientC -= m_params.nightTemperatureDropC;
        }
        state.ambientC = std::clamp(state.ambientC, -40.0f, 55.0f);
        state.rainIntensity = Clamp01(state.rainIntensity);
        state.wetnessIntensity = Clamp01(state.wetnessIntensity);
        state.fogDensity = Clamp01(state.fogDensity);
        state.sightScale = std::clamp(state.sightScale, 0.25f, 1.5f);
        state.moveScale = std::clamp(state.moveScale, 0.25f, 1.25f);
        return state;
    }

    const double period = static_cast<double>(m_params.periodMinutes);
    const std::uint64_t block =
        static_cast<std::uint64_t>(std::floor(totalGameMinutes / period));
    const double phase =
        totalGameMinutes - static_cast<double>(block) * period;
    const float blend =
        m_params.transitionMinutes > 0.0f
            ? SmoothStep(static_cast<float>(
                  phase / static_cast<double>(m_params.transitionMinutes)))
            : 1.0f;

    const WeatherProfile current = BlockProfile(m_params, block);
    const WeatherProfile previous =
        block > 0 ? BlockProfile(m_params, block - 1) : current;
    const float t = block > 0 ? blend : 1.0f;

    state.kind =
        block > 0 ? BlendKind(previous.kind, current.kind, t) : current.kind;
    state.intensity = t;
    state.ambientC = previous.ambientC + (current.ambientC - previous.ambientC) * t;
    state.rainIntensity =
        previous.rainIntensity +
        (current.rainIntensity - previous.rainIntensity) * t;
    state.wetnessIntensity =
        previous.wetnessIntensity +
        (current.wetnessIntensity - previous.wetnessIntensity) * t;
    state.fogDensity =
        previous.fogDensity + (current.fogDensity - previous.fogDensity) * t;
    state.sightScale =
        previous.sightScale + (current.sightScale - previous.sightScale) * t;
    state.moveScale =
        previous.moveScale + (current.moveScale - previous.moveScale) * t;

    const int normalizedHour = std::clamp(hour, 0, 23);
    state.night = normalizedHour < 7 || normalizedHour >= 20;
    if (state.night) {
        state.ambientC -= m_params.nightTemperatureDropC;
    }
    state.ambientC = std::clamp(state.ambientC, -40.0f, 55.0f);
    state.rainIntensity = Clamp01(state.rainIntensity);
    state.wetnessIntensity = Clamp01(state.wetnessIntensity);
    state.fogDensity = Clamp01(state.fogDensity);
    state.sightScale = std::clamp(state.sightScale, 0.25f, 1.5f);
    state.moveScale = std::clamp(state.moveScale, 0.25f, 1.25f);
    return state;
}

void WeatherSystem::SetOverride(WeatherKind kind,
                                double untilTotalGameMinutes) {
    m_overrideKind = kind;
    m_overrideUntil = std::isfinite(untilTotalGameMinutes)
                          ? untilTotalGameMinutes
                          : -1.0;
}

void WeatherSystem::ClearOverride() {
    m_overrideUntil = -1.0;
}

const char* WeatherSystem::KindName(WeatherKind kind) {
    switch (kind) {
        case WeatherKind::Clear: return "晴";
        case WeatherKind::Rain: return "雨";
        case WeatherKind::Fog: return "雾";
        case WeatherKind::ColdSnap: return "寒潮";
    }
    return "晴";
}

const char* WeatherSystem::KindClass(WeatherKind kind) {
    switch (kind) {
        case WeatherKind::Clear: return "clear";
        case WeatherKind::Rain: return "rain";
        case WeatherKind::Fog: return "fog";
        case WeatherKind::ColdSnap: return "cold";
    }
    return "clear";
}

}  // namespace zl::game
