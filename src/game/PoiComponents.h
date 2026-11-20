#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "game/Poi.h"

namespace zl::ecs {

// M19-F2：玩家已完成的 POI 任务 id。旧档缺少组件时按全部未完成处理。
// M33-F5-T2：追加玩家级阶段奖励幂等键。
struct PoiTasks {
    std::vector<std::string> completed;
    std::vector<std::string> rewardClaims;
};

inline constexpr std::uint64_t kPoiWorldProgressStableId =
    0xF333504F49575001ull;
inline constexpr std::size_t kMaxPoiWorldProgress = 64;

// M33-F5-T2：世界级多阶段 POI 进度，所有玩家共享。
struct PoiWorldProgress {
    std::vector<game::PoiProgress> pois;
};

// M33-F5-T4：阶段刷新的 Boss/精英/召唤物。Boss/精英计入 kill 目标，
// 召唤物只作为机制增援；实体同时带 PoiGuard，随存档保留。
struct PoiStageEnemy {
    std::string poiId;
    std::string stageId;
    game::PoiStageEnemyRole role = game::PoiStageEnemyRole::Elite;
};

// M33-F5-T4：Boss 能力计时/冲锋速度，运行时组件，不进入存档。
struct PoiBossRuntime {
    float abilityCooldown = 0.0f;
    float chargeRemaining = 0.0f;
    float baseSpeedScale = 1.0f;
    int summoned = 0;
};

}  // namespace zl::ecs
