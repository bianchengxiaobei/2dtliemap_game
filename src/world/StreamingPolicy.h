#pragma once

#include <algorithm>
#include <cmath>

#include "core/Constants.h"

namespace zl::world {

// Shared policy for player-centric streaming. Kept header-only so both the
// region state machine and ChunkManager use exactly the same look-ahead rule.
struct StreamingConfig {
    int loadRadius = core::kChunkLoadRadius;
    int residentRadius = core::kChunkLoadRadius + 2;
    int maxLookaheadChunks = 2;
    float lookaheadSeconds = 0.6f;
};

struct ChunkOffset {
    int dx = 0;
    int dy = 0;
};

inline ChunkOffset ComputeLookahead(float velocityX, float velocityY,
                                    const StreamingConfig& config) {
    const float speed = std::sqrt(velocityX * velocityX + velocityY * velocityY);
    const int maxLead = std::min(std::max(0, config.maxLookaheadChunks),
                                 std::max(0, config.loadRadius));
    if (!(speed > 0.05f) || maxLead == 0 || !(config.lookaheadSeconds > 0.0f)) {
        return {};
    }

    const float chunksPerSecond = speed / static_cast<float>(core::kChunkSize);
    int lead = static_cast<int>(
        std::ceil(chunksPerSecond * config.lookaheadSeconds));
    lead = std::clamp(lead, 1, maxLead);

    const float inverseSpeed = 1.0f / speed;
    ChunkOffset offset{
        static_cast<int>(std::lround(velocityX * inverseSpeed * lead)),
        static_cast<int>(std::lround(velocityY * inverseSpeed * lead)),
    };
    if (offset.dx == 0 && offset.dy == 0) {
        // Extremely small but non-zero movement still gets one cardinal lead.
        if (std::fabs(velocityX) >= std::fabs(velocityY)) {
            offset.dx = velocityX >= 0.0f ? 1 : -1;
        } else {
            offset.dy = velocityY >= 0.0f ? 1 : -1;
        }
    }
    return offset;
}

}  // namespace zl::world
