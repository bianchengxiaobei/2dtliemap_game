#include "game/Climate.h"

#include <algorithm>
#include <cmath>

namespace zl::game {
namespace {

float Clamp01(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

float ClampNonNegative(float value) {
    return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
}

float ClampFinite(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

}  // namespace

ClimateParams SanitizeClimateParams(ClimateParams params) {
    params.rainWetnessPerSecond =
        ClampNonNegative(params.rainWetnessPerSecond);
    params.dryPerSecond = ClampNonNegative(params.dryPerSecond);
    params.fireDryPerSecond = ClampNonNegative(params.fireDryPerSecond);

    params.comfortC = std::clamp(ClampFinite(params.comfortC, 37.0f), 30.0f, 42.0f);
    params.coldStartC =
        std::clamp(ClampFinite(params.coldStartC, 18.0f), -20.0f, 35.0f);
    params.coldRate = std::clamp(ClampFinite(params.coldRate, 0.15f), 0.0f, 1.0f);
    params.bodyResponseSeconds =
        std::max(1.0f, ClampFinite(params.bodyResponseSeconds, 120.0f));
    params.indoorWarmthC =
        std::clamp(ClampFinite(params.indoorWarmthC, 14.0f), 0.0f, 40.0f);
    params.fireWarmthC =
        std::clamp(ClampFinite(params.fireWarmthC, 18.0f), 0.0f, 40.0f);
    params.insulationWarmthC =
        std::clamp(ClampFinite(params.insulationWarmthC, 8.0f), 0.0f, 30.0f);
    params.wetCoolingC =
        std::clamp(ClampFinite(params.wetCoolingC, 6.0f), 0.0f, 30.0f);
    params.indoorMinBodyC = std::clamp(
        ClampFinite(params.indoorMinBodyC, 36.2f), 30.0f, 42.0f);
    params.fireMinBodyC = std::clamp(
        ClampFinite(params.fireMinBodyC, 36.8f), 30.0f, 42.0f);

    params.coldWarnC = std::clamp(ClampFinite(params.coldWarnC, 36.0f), 30.0f, 42.0f);
    params.coldFatigueThresholdC = std::clamp(
        ClampFinite(params.coldFatigueThresholdC, 36.0f), 30.0f, 42.0f);
    params.coldFatiguePerSecond =
        ClampNonNegative(params.coldFatiguePerSecond);
    params.hypothermiaThresholdC = std::clamp(
        ClampFinite(params.hypothermiaThresholdC, 35.0f), 25.0f, 38.0f);
    params.hypothermiaDamagePerSecond =
        ClampNonNegative(params.hypothermiaDamagePerSecond);
    params.coldMovePenalty = Clamp01(params.coldMovePenalty);
    params.coldMoveMin = std::clamp(ClampFinite(params.coldMoveMin, 0.65f),
                                    0.05f, 1.0f);
    params.wetWarnLevel = Clamp01(params.wetWarnLevel);
    return params;
}

ClimateTick UpdateClimate(ecs::Climate& climate,
                          const ClimateEnvironment& environment,
                          const ClimateParams& params, float dt) {
    ClimateTick tick;
    if (!std::isfinite(dt) || dt <= 0.0f) {
        return tick;
    }
    const ClimateParams p = SanitizeClimateParams(params);

    climate.bodyC = std::clamp(ClampFinite(climate.bodyC, p.comfortC), 20.0f, 45.0f);
    climate.wetness = Clamp01(climate.wetness);
    const float insulation = Clamp01(environment.insulation);
    const float waterproof = Clamp01(environment.waterproof);
    const float rain = Clamp01(environment.rainIntensity);

    if (rain > 0.0f && !environment.indoor) {
        climate.wetness = Clamp01(
            climate.wetness +
            rain * (1.0f - waterproof) * p.rainWetnessPerSecond * dt);
    } else if (environment.nearFire) {
        climate.wetness =
            Clamp01(climate.wetness - p.fireDryPerSecond * dt);
    } else {
        climate.wetness = Clamp01(climate.wetness - p.dryPerSecond * dt);
    }

    float ambient = ClampFinite(environment.ambientC, 24.0f);
    if (environment.indoor) {
        ambient += p.indoorWarmthC;
    }
    if (environment.nearFire) {
        ambient += p.fireWarmthC;
    }
    const float effectiveInsulation =
        insulation * (1.0f - climate.wetness * 0.5f);
    ambient += effectiveInsulation * p.insulationWarmthC;
    ambient -= climate.wetness * p.wetCoolingC;
    climate.ambientC = ClampFinite(environment.ambientC, 24.0f);
    climate.effectiveC = ambient;
    climate.insulation = insulation;
    climate.waterproof = waterproof;
    climate.indoor = environment.indoor;
    climate.nearFire = environment.nearFire;

    float target =
        p.comfortC - std::max(0.0f, p.coldStartC - ambient) * p.coldRate;
    if (environment.indoor) {
        target = std::max(target, p.indoorMinBodyC);
    }
    if (environment.nearFire) {
        target = std::max(target, p.fireMinBodyC);
    }
    target = std::clamp(target, 18.0f, 42.0f);
    const float response =
        1.0f - std::exp(-dt / std::max(1.0f, p.bodyResponseSeconds));
    climate.bodyC = std::clamp(climate.bodyC + (target - climate.bodyC) * response,
                               20.0f, 45.0f);

    const float cold = std::max(0.0f, p.coldFatigueThresholdC - climate.bodyC);
    tick.fatigueDelta = cold * p.coldFatiguePerSecond * dt;
    const float hypothermia =
        std::max(0.0f, p.hypothermiaThresholdC - climate.bodyC);
    tick.healthDamage =
        hypothermia * p.hypothermiaDamagePerSecond * dt;
    tick.damaging = tick.healthDamage > 0.0001f;

    climate.moveScale =
        std::clamp(1.0f - cold * p.coldMovePenalty, p.coldMoveMin, 1.0f);
    const bool critical = climate.bodyC < p.hypothermiaThresholdC;
    const bool warn = climate.bodyC < p.coldWarnC ||
                      climate.wetness >= p.wetWarnLevel;
    climate.warning = critical ? 2 : (warn ? 1 : 0);
    return tick;
}

}  // namespace zl::game
