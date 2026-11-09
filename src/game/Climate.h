#pragma once

#include <cstdint>

#include "characters/components/CharacterComponents.h"

namespace zl::game {

// M20-F2：玩家体温/潮湿结算参数。所有数值由 config/game.json 的 climate 段注入。
struct ClimateParams {
    float rainWetnessPerSecond = 0.025f;
    float dryPerSecond = 0.012f;
    float fireDryPerSecond = 0.05f;

    float comfortC = 37.0f;
    float coldStartC = 18.0f;
    float coldRate = 0.15f;
    float bodyResponseSeconds = 120.0f;
    float indoorWarmthC = 14.0f;
    float fireWarmthC = 18.0f;
    float insulationWarmthC = 8.0f;
    float wetCoolingC = 6.0f;
    float indoorMinBodyC = 36.2f;
    float fireMinBodyC = 36.8f;

    float coldWarnC = 36.0f;
    float coldFatigueThresholdC = 36.0f;
    float coldFatiguePerSecond = 0.001f;
    float hypothermiaThresholdC = 35.0f;
    float hypothermiaDamagePerSecond = 0.15f;
    float coldMovePenalty = 0.12f;
    float coldMoveMin = 0.65f;
    float wetWarnLevel = 0.60f;
};

struct ClimateEnvironment {
    float ambientC = 24.0f;
    float rainIntensity = 0.0f;
    bool indoor = false;
    bool nearFire = false;
    float insulation = 0.0f;
    float waterproof = 0.0f;
};

struct ClimateTick {
    float fatigueDelta = 0.0f;
    float healthDamage = 0.0f;
    bool damaging = false;
};

// 纯函数结算：更新 climate 的体温/潮湿/移速，并返回本 tick 的疲劳与失温伤害。
// 载入存档后由 WorldSimulation 的 ClimateSystem 重新填充 ambient/effective 等派生字段；
// 只有 bodyC/wetness 进入存档。
ClimateTick UpdateClimate(ecs::Climate& climate, const ClimateEnvironment& environment,
                          const ClimateParams& params, float dt);

ClimateParams SanitizeClimateParams(ClimateParams params);

}  // namespace zl::game
