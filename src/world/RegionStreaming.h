#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "world/StreamingPolicy.h"
#include "world/WorldDefinition.h"

namespace zl::world {

// Region streaming phase is intentionally independent from chunk lifetime:
// chunks are owned by ChunkManager, while this state machine only owns the
// logical region summary. Dynamic entity persistence is a later milestone.
enum class RegionPhase : std::uint8_t {
    Frozen = 0,
    Ambient = 1,
    Active = 2,
};

struct RegionRuntime {
    std::string id;
    WorldRect rect;
    RegionPhase phase = RegionPhase::Frozen;
};

struct RegionStreamingStats {
    int active = 0;
    int ambient = 0;
    int frozen = 0;
    std::uint64_t freezeTransitions = 0;
    std::uint64_t restoreTransitions = 0;
    int lookaheadChunks = 0;
    int focusChunkX = 0;
    int focusChunkY = 0;
    int predictedChunkX = 0;
    int predictedChunkY = 0;
    int focusCount = 0;
};

// Player-centric region focus + look-ahead + freeze/restore state.
// - Active: the focus chunk is inside the region;
// - Ambient: the region is within residentRadius chunks of the focus;
// - Frozen: no player nearby; only the lightweight summary is retained.
// No dynamic entity data is saved in M31-T3.
class RegionStreaming {
public:
    explicit RegionStreaming(WorldDefinition definition,
                             StreamingConfig config = {});

    void SetFocus(float gridX, float gridY, float velocityX = 0.0f,
                  float velocityY = 0.0f);

    // M31-T8: server interest union. A region is Active when any player focus
    // chunk is inside it, Ambient when any focus is within residentRadius.
    // Look-ahead remains based on the primary (first) focus.
    void SetFocuses(const std::vector<glm::vec2>& focuses);

    const WorldDefinition& Definition() const { return m_definition; }
    const StreamingConfig& Config() const { return m_config; }
    const std::vector<RegionRuntime>& Regions() const { return m_regions; }
    const RegionStreamingStats& Stats() const { return m_stats; }

    glm::vec2 Focus() const { return m_focus; }
    glm::vec2 PredictedFocus() const { return m_predictedFocus; }
    int FocusChunkX() const { return m_focusChunkX; }
    int FocusChunkY() const { return m_focusChunkY; }
    int PredictedChunkX() const { return m_predictedChunkX; }
    int PredictedChunkY() const { return m_predictedChunkY; }
    int LookaheadChunks() const { return m_stats.lookaheadChunks; }

    RegionPhase PhaseOf(const std::string& regionId) const;
    const std::string* RegionIdAtChunk(int chunkX, int chunkY) const;
    const std::string& ActiveRegionId() const { return m_activeRegionId; }

    // Clamps a grid position inside the finite world, leaving a half-grid
    // margin so a point is never exactly on the outer edge.
    static glm::vec2 ClampFocus(const WorldBounds& bounds, float gridX,
                                float gridY, float margin = 0.5f);

private:
    void RecomputePhases();
    static int ChunkDistance(int focusChunkX, int focusChunkY,
                             const WorldRect& rect);

    WorldDefinition m_definition;
    StreamingConfig m_config;
    std::vector<RegionRuntime> m_regions;

    glm::vec2 m_focus{0.0f, 0.0f};
    glm::vec2 m_predictedFocus{0.0f, 0.0f};
    std::vector<glm::vec2> m_focuses;
    std::vector<glm::ivec2> m_focusChunks;
    float m_velocityX = 0.0f;
    float m_velocityY = 0.0f;
    int m_focusChunkX = 0;
    int m_focusChunkY = 0;
    int m_predictedChunkX = 0;
    int m_predictedChunkY = 0;
    std::string m_activeRegionId;

    RegionStreamingStats m_stats;
};

}  // namespace zl::world
