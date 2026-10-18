#pragma once

#include <functional>

namespace zl::core {

// M11-T4：声音传播的唯一实现。AI 听觉（PerceptionSystem）与音频系统共用同一套
// 采样步长、阻挡格计数和隔墙衰减参数；禁止两边各写一份。
using SoundWalkableQuery = std::function<bool(int, int)>;

// 沿 "听者 → 声源" 连线每 0.5 格采样（不含两端），统计不同阻挡格数量。
// 查询为空或两端重合时返回 0（降级为无遮挡）。
int CountBlockingCells(float listenerX, float listenerY, float sourceX, float sourceY,
                       const SoundWalkableQuery& walkable);

// 每穿过 1 格阻挡的增益/可闻半径衰减系数（0..1）。
float WallAttenuationGain(int walls, float attenuationPerWall);

// 基础可闻半径经隔墙衰减后的有效半径。
float EffectiveSoundRadius(float baseRadius, int walls, float attenuationPerWall);

// 距离 + 隔墙衰减后的可闻判定（严格按有效半径）。
bool IsSoundAudible(float baseRadius, float distance, int walls, float attenuationPerWall);

}  // namespace zl::core
