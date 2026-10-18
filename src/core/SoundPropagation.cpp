#include "core/SoundPropagation.h"

#include <algorithm>
#include <cmath>

namespace zl::core {
namespace {

constexpr float kSampleStep = 0.5f;

}  // namespace

int CountBlockingCells(float listenerX, float listenerY, float sourceX, float sourceY,
                       const SoundWalkableQuery& walkable) {
    if (!walkable) {
        return 0;
    }
    const float dx = sourceX - listenerX;
    const float dy = sourceY - listenerY;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= 0.001f) {
        return 0;
    }

    const int steps = std::max(2, static_cast<int>(std::ceil(distance / kSampleStep)));
    int walls = 0;
    int lastWx = 0;
    int lastWy = 0;
    bool hasLast = false;
    for (int i = 1; i < steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const int wx = static_cast<int>(std::floor(listenerX + dx * t));
        const int wy = static_cast<int>(std::floor(listenerY + dy * t));
        if (walkable(wx, wy)) {
            continue;
        }
        if (hasLast && wx == lastWx && wy == lastWy) {
            continue;
        }
        lastWx = wx;
        lastWy = wy;
        hasLast = true;
        ++walls;
    }
    return walls;
}

float WallAttenuationGain(int walls, float attenuationPerWall) {
    if (walls <= 0) {
        return 1.0f;
    }
    const float factor = std::clamp(attenuationPerWall, 0.0f, 1.0f);
    return std::pow(factor, static_cast<float>(walls));
}

float EffectiveSoundRadius(float baseRadius, int walls, float attenuationPerWall) {
    return std::max(0.0f, baseRadius) * WallAttenuationGain(walls, attenuationPerWall);
}

bool IsSoundAudible(float baseRadius, float distance, int walls, float attenuationPerWall) {
    if (baseRadius <= 0.0f || distance < 0.0f) {
        return false;
    }
    return distance <= EffectiveSoundRadius(baseRadius, walls, attenuationPerWall) + 0.0001f;
}

}  // namespace zl::core
