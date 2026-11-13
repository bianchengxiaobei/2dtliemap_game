#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "world/RegionStreaming.h"
#include "world/WorldDefinition.h"

namespace zl::game {

enum class RegionEventKind : std::uint8_t {
    Horde = 0,
    Caravan = 1,
    SupplyDrop = 2,
    Outbreak = 3,
};

enum class RegionEventStatus : std::uint8_t {
    Scheduled = 0,
    Active = 1,
    Frozen = 2,
    Resolved = 3,
};

struct RegionEvent {
    std::string id;
    std::string regionId;
    RegionEventKind kind = RegionEventKind::Horde;
    RegionEventStatus status = RegionEventStatus::Scheduled;
    float x = 0.0f;
    float y = 0.0f;
    int day = 1;
    float remainingSeconds = 0.0f;
    int targetCount = 0;
    int spawnedCount = 0;
    bool contentSpawned = false;
};

struct RegionEventConfig {
    bool enabled = true;
    float durationSeconds = 240.0f;
    float contentRadius = 24.0f;
};

struct RegionEventStats {
    int scheduled = 0;
    int active = 0;
    int frozen = 0;
    int resolved = 0;
    int spawned = 0;
};

const char* RegionEventKindName(RegionEventKind kind);
const char* RegionEventStatusName(RegionEventStatus status);

class RegionEventDirector {
public:
    using PhaseQuery = std::function<world::RegionPhase(const std::string&)>;
    using SpawnCallback = std::function<int(const RegionEvent&)>;

    explicit RegionEventDirector(const world::WorldDefinition& definition,
                                 RegionEventConfig config = {});

    void Reset();
    void AdvanceDay(int day);
    bool Restore(const std::vector<RegionEvent>& events, int day,
                 std::string* error = nullptr);
    void Update(float dt, float focusX, float focusY,
                const PhaseQuery& phaseOf, const SpawnCallback& spawn);

    // M31-T8: evaluate an event against every player focus. An event is near
    // when any player is within contentRadius; visibility is still gated by the
    // union of active/ambient region phases.
    void UpdateFocuses(float dt, const std::vector<glm::vec2>& focuses,
                       const PhaseQuery& phaseOf,
                       const SpawnCallback& spawn);

    const std::vector<RegionEvent>& Events() const { return m_events; }
    std::vector<RegionEvent> Snapshot() const { return m_events; }
    const RegionEventStats& Stats() const { return m_stats; }
    int Day() const { return m_day; }
    const RegionEvent* NearestVisible(float x, float y,
                                      float maxDistance = 1.0e30f) const;

private:
    const world::WorldRegion* FindRegion(std::string_view id) const;
    RegionEvent BuildEvent(const world::WorldRegion& region, int day) const;
    void ResolveSite(const world::WorldRegion& region, float* x, float* y) const;
    static RegionEventKind KindFor(const world::WorldDefinition& definition,
                                   std::string_view regionId, int day);
    static int TargetCount(RegionEventKind kind);

    world::WorldDefinition m_definition;
    RegionEventConfig m_config;
    std::vector<RegionEvent> m_events;
    std::unordered_set<std::string> m_visibleRegions;
    RegionEventStats m_stats;
    int m_day = 0;
};

}  // namespace zl::game
