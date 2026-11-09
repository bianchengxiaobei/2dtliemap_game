#pragma once

#include <cstdint>
#include <string>

namespace zl::ecs {

enum class FirePhase : std::uint8_t {
    Igniting = 0,
    Burning = 1,
    Smoldering = 2,
    Extinguished = 3,
};

struct FireAnchor {
    int x = 0;
    int y = 0;
    int z = 0;
};

struct FireSource {
    std::string materialId;
    FirePhase phase = FirePhase::Extinguished;
    float ignitionProgress = 0.0f;
    float ignitionDuration = 0.0f;
    float burnRemaining = 0.0f;
    float burnDuration = 0.0f;
    float smokePerSecond = 0.0f;
    float heatRadius = 0.0f;
    float smolderElapsed = 0.0f;
    float spreadAccumulator = 0.0f;
};

}  // namespace zl::ecs
