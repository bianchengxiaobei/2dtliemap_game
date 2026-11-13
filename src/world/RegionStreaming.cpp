#include "world/RegionStreaming.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/Constants.h"
#include "world/WorldCoords.h"

namespace zl::world {
namespace {

bool IsFinite(float value) {
    return std::isfinite(value);
}

}  // namespace

RegionStreaming::RegionStreaming(WorldDefinition definition,
                                 StreamingConfig config)
    : m_definition(std::move(definition)), m_config(config) {
    m_config.loadRadius = std::max(0, m_config.loadRadius);
    m_config.residentRadius =
        std::max(m_config.loadRadius, m_config.residentRadius);
    m_config.maxLookaheadChunks = std::max(0, m_config.maxLookaheadChunks);
    m_config.lookaheadSeconds = std::max(0.0f, m_config.lookaheadSeconds);

    m_regions.reserve(m_definition.regions.size());
    for (const WorldRegion& region : m_definition.regions) {
        m_regions.push_back(RegionRuntime{region.id, region.rect,
                                          RegionPhase::Frozen});
    }
}

glm::vec2 RegionStreaming::ClampFocus(const WorldBounds& bounds, float gridX,
                                      float gridY, float margin) {
    if (!IsFinite(gridX) || !IsFinite(gridY) || bounds.chunksX <= 0 ||
        bounds.chunksY <= 0) {
        return glm::vec2(gridX, gridY);
    }
    const float safeMargin = std::max(0.0f, margin);
    const float minX =
        static_cast<float>(bounds.chunkX0) * core::kChunkSize + safeMargin;
    const float maxX =
        static_cast<float>(bounds.chunkX0 + bounds.chunksX) *
            core::kChunkSize -
        safeMargin;
    const float minY =
        static_cast<float>(bounds.chunkY0) * core::kChunkSize + safeMargin;
    const float maxY =
        static_cast<float>(bounds.chunkY0 + bounds.chunksY) *
            core::kChunkSize -
        safeMargin;
    const float centerX = (minX + maxX) * 0.5f;
    const float centerY = (minY + maxY) * 0.5f;
    return glm::vec2(
        maxX >= minX ? std::clamp(gridX, minX, maxX) : centerX,
        maxY >= minY ? std::clamp(gridY, minY, maxY) : centerY);
}

int RegionStreaming::ChunkDistance(int focusChunkX, int focusChunkY,
                                   const WorldRect& rect) {
    const int minX = rect.chunkX;
    const int minY = rect.chunkY;
    const int maxX = rect.chunkX + std::max(0, rect.chunksW) - 1;
    const int maxY = rect.chunkY + std::max(0, rect.chunksH) - 1;
    const int dx = focusChunkX < minX ? minX - focusChunkX
                                      : (focusChunkX > maxX ? focusChunkX - maxX : 0);
    const int dy = focusChunkY < minY ? minY - focusChunkY
                                      : (focusChunkY > maxY ? focusChunkY - maxY : 0);
    return std::max(dx, dy);
}

void RegionStreaming::SetFocus(float gridX, float gridY, float velocityX,
                               float velocityY) {
    if (!IsFinite(gridX) || !IsFinite(gridY)) {
        return;
    }
    m_focus = ClampFocus(m_definition.bounds, gridX, gridY);
    m_velocityX = IsFinite(velocityX) ? velocityX : 0.0f;
    m_velocityY = IsFinite(velocityY) ? velocityY : 0.0f;

    const ChunkCoord focus =
        ChunkOf(static_cast<int>(std::floor(m_focus.x)),
                static_cast<int>(std::floor(m_focus.y)));
    m_focusChunkX = focus.cx;
    m_focusChunkY = focus.cy;
    m_focuses.clear();
    m_focuses.push_back(m_focus);
    m_focusChunks.clear();
    m_focusChunks.emplace_back(m_focusChunkX, m_focusChunkY);
    m_stats.focusCount = 1;

    const ChunkOffset offset =
        ComputeLookahead(m_velocityX, m_velocityY, m_config);
    m_stats.lookaheadChunks = std::max(std::abs(offset.dx), std::abs(offset.dy));

    int predictedX = m_focusChunkX + offset.dx;
    int predictedY = m_focusChunkY + offset.dy;
    if (m_definition.bounds.chunksX > 0 && m_definition.bounds.chunksY > 0) {
        const int maxChunkX =
            m_definition.bounds.chunkX0 + m_definition.bounds.chunksX - 1;
        const int maxChunkY =
            m_definition.bounds.chunkY0 + m_definition.bounds.chunksY - 1;
        predictedX = std::clamp(predictedX, m_definition.bounds.chunkX0,
                                maxChunkX);
        predictedY = std::clamp(predictedY, m_definition.bounds.chunkY0,
                                maxChunkY);
    }
    m_predictedChunkX = predictedX;
    m_predictedChunkY = predictedY;
    m_predictedFocus = ClampFocus(
        m_definition.bounds,
        m_focus.x + static_cast<float>(offset.dx * core::kChunkSize),
        m_focus.y + static_cast<float>(offset.dy * core::kChunkSize));

    m_stats.focusChunkX = m_focusChunkX;
    m_stats.focusChunkY = m_focusChunkY;
    m_stats.predictedChunkX = m_predictedChunkX;
    m_stats.predictedChunkY = m_predictedChunkY;
    RecomputePhases();
}

void RegionStreaming::SetFocuses(const std::vector<glm::vec2>& focuses) {
    std::vector<glm::vec2> valid;
    valid.reserve(focuses.size());
    for (const glm::vec2& focus : focuses) {
        if (IsFinite(focus.x) && IsFinite(focus.y)) {
            valid.push_back(ClampFocus(m_definition.bounds, focus.x, focus.y));
        }
    }
    if (valid.empty()) {
        return;
    }

    m_focuses = std::move(valid);
    m_focusChunks.clear();
    m_focusChunks.reserve(m_focuses.size());
    for (const glm::vec2& focus : m_focuses) {
        const ChunkCoord chunk =
            ChunkOf(static_cast<int>(std::floor(focus.x)),
                    static_cast<int>(std::floor(focus.y)));
        m_focusChunks.emplace_back(chunk.cx, chunk.cy);
    }
    m_focus = m_focuses.front();
    m_focusChunkX = m_focusChunks.front().x;
    m_focusChunkY = m_focusChunks.front().y;
    m_predictedChunkX = m_focusChunkX;
    m_predictedChunkY = m_focusChunkY;
    m_predictedFocus = m_focus;
    m_velocityX = 0.0f;
    m_velocityY = 0.0f;
    m_stats.focusCount = static_cast<int>(m_focuses.size());
    m_stats.focusChunkX = m_focusChunkX;
    m_stats.focusChunkY = m_focusChunkY;
    m_stats.predictedChunkX = m_predictedChunkX;
    m_stats.predictedChunkY = m_predictedChunkY;
    m_stats.lookaheadChunks = 0;
    RecomputePhases();
}

void RegionStreaming::RecomputePhases() {
    m_stats.active = 0;
    m_stats.ambient = 0;
    m_stats.frozen = 0;
    m_activeRegionId.clear();

    for (RegionRuntime& region : m_regions) {
        const RegionPhase previous = region.phase;
        RegionPhase next = RegionPhase::Frozen;
        int nearest = 0;
        bool hasFocus = false;
        for (const glm::ivec2& focus : m_focusChunks) {
            const int distance = ChunkDistance(focus.x, focus.y, region.rect);
            if (!hasFocus || distance < nearest) {
                nearest = distance;
                hasFocus = true;
            }
        }
        if (hasFocus && nearest == 0) {
            next = RegionPhase::Active;
        } else if (hasFocus && nearest <= m_config.residentRadius) {
            next = RegionPhase::Ambient;
        }

        if (previous != RegionPhase::Frozen && next == RegionPhase::Frozen) {
            ++m_stats.freezeTransitions;
        } else if (previous == RegionPhase::Frozen &&
                   next != RegionPhase::Frozen) {
            ++m_stats.restoreTransitions;
        }
        region.phase = next;

        switch (next) {
            case RegionPhase::Active:
                ++m_stats.active;
                if (m_activeRegionId.empty()) {
                    m_activeRegionId = region.id;
                }
                break;
            case RegionPhase::Ambient:
                ++m_stats.ambient;
                break;
            case RegionPhase::Frozen:
                ++m_stats.frozen;
                break;
        }
    }
}

RegionPhase RegionStreaming::PhaseOf(const std::string& regionId) const {
    for (const RegionRuntime& region : m_regions) {
        if (region.id == regionId) {
            return region.phase;
        }
    }
    return RegionPhase::Frozen;
}

const std::string* RegionStreaming::RegionIdAtChunk(int chunkX,
                                                     int chunkY) const {
    for (const RegionRuntime& region : m_regions) {
        const int maxX = region.rect.chunkX + region.rect.chunksW;
        const int maxY = region.rect.chunkY + region.rect.chunksH;
        if (chunkX >= region.rect.chunkX && chunkX < maxX &&
            chunkY >= region.rect.chunkY && chunkY < maxY) {
            return &region.id;
        }
    }
    return nullptr;
}

}  // namespace zl::world
