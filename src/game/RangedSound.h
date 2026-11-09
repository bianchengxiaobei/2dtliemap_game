#pragma once

#include <algorithm>

#include "core/SoundEvent.h"
#include "items/ItemDef.h"

namespace zl::game {

// M23-F1-T5：远程武器开火生成统一的世界声音。
// 声源位置由调用方给定；半径直接来自 ranged.noiseRadius，AI 听觉与音频系统共用。
inline core::SoundEvent MakeRangedGunshot(float x, float y,
                                          const items::RangedCombat& ranged) {
    core::SoundEvent shot;
    shot.x = x;
    shot.y = y;
    shot.radius = std::max(0.0f, ranged.noiseRadius);
    shot.type = core::SoundType::Gunshot;
    return shot;
}

}  // namespace zl::game
