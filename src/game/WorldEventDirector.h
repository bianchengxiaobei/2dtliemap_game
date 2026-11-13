#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "game/WorldEvent.h"
#include "world/WorldDefinition.h"

namespace zl::game {

inline constexpr std::size_t kMaxWorldEventInstances = 128;

enum class WorldEventStatus : std::uint8_t {
    Scheduled = 0,
    Warning = 1,
    Active = 2,
    Resolved = 3,
    Failed = 4,
    Expired = 5,
};

const char* WorldEventStatusName(WorldEventStatus status);

struct WorldEventInstance {
    std::string id;
    std::string defId;
    std::string name;
    std::string kind;
    std::string regionId;
    float x = 0.0f;
    float y = 0.0f;
    WorldEventStatus status = WorldEventStatus::Scheduled;
    int day = 1;
    int stageIndex = 0;
    int stageProgress = 0;
    int stageTarget = 0;
    float stageRadius = 0.0f;
    WorldEventStageKind stageKind = WorldEventStageKind::Reach;
    std::string stageTargetId;
    std::int64_t scheduledMinute = 0;
    std::int64_t warningMinute = 0;
    std::int64_t activeMinute = 0;
    std::int64_t deadlineMinute = 0;
    std::int64_t resolvedMinute = 0;
    int chainDepth = 0;
    bool detail = false;
    bool downgraded = false;
    bool stagesComplete = false;
    bool contentSpawned = false;
    std::string choiceId;
    std::string nextEventId;
};

struct WorldEventDirectorConfig {
    bool enabled = true;
    float detailRadius = 32.0f;
};

struct WorldEventDirectorStats {
    int scheduled = 0;
    int warning = 0;
    int active = 0;
    int detailed = 0;
    int resolved = 0;
    int failed = 0;
    int expired = 0;
    int chained = 0;
};

class WorldEventDirector {
public:
    WorldEventDirector(const WorldEventConfig& config,
                       const world::WorldDefinition& definition,
                       WorldEventDirectorConfig options = {});

    void Reset();
    void Update(double nowGameMinutes, const std::vector<glm::vec2>& focuses);
    void Update(double nowGameMinutes, float focusX, float focusY);

    bool ReportStageProgress(std::string_view eventId,
                             WorldEventStageKind kind,
                             std::string_view target, int amount,
                             std::string* error = nullptr);
    bool Choose(std::string_view eventId, std::string_view choiceId,
                std::string* error = nullptr);
    bool Fail(std::string_view eventId, std::string* error = nullptr);

    bool Restore(const std::vector<WorldEventInstance>& events, int day,
                 double nowGameMinutes, std::string* error = nullptr);
    void MarkContentSpawned(std::string_view eventId);
    std::vector<WorldEventInstance> Snapshot() const { return m_events; }

    const std::vector<WorldEventInstance>& Events() const { return m_events; }
    const WorldEventDirectorStats& Stats() const { return m_stats; }
    int Day() const { return m_day; }
    bool Enabled() const {
        return m_options.enabled && m_config.enabled &&
               !m_definition.regions.empty();
    }
    const WorldEventInstance* Find(std::string_view id) const;
    const WorldEventInstance* ActiveDetailed() const;

private:
    const WorldEventDef* FindDef(std::string_view id) const;
    const world::WorldRegion* FindRegion(std::string_view id) const;
    void EnsureDay(int day);
    void PruneEvents(int day);
    void RefreshEvent(WorldEventInstance& event, std::int64_t now,
                      const std::vector<glm::vec2>& focuses);
    void RefreshStats();
    bool AdvanceStage(WorldEventInstance& event, int amount);
    bool Resolve(WorldEventInstance& event, std::string_view choiceId,
                 std::string* error);
    void QueueChain(const WorldEventInstance& event);
    void Expire(WorldEventInstance& event);
    void SetStage(WorldEventInstance& event, const WorldEventDef& def);

    const WorldEventDef* SelectDefinition(int day) const;
    const world::WorldRegion* SelectRegion(const WorldEventDef& def, int day) const;
    glm::vec2 SelectSite(const world::WorldRegion& region,
                         const WorldEventDef& def, int day) const;
    int SelectActiveMinute(const WorldEventDef& def, int day,
                           std::string_view regionId) const;
    WorldEventInstance BuildInstance(const WorldEventDef& def,
                                     const world::WorldRegion& region,
                                     int day, std::string_view regionId);

    bool ValidInstance(const WorldEventInstance& event, std::string* error) const;

    WorldEventConfig m_config;
    world::WorldDefinition m_definition;
    WorldEventDirectorConfig m_options;
    std::vector<WorldEventInstance> m_events;
    std::unordered_map<std::string, int> m_lastDefDay;
    WorldEventDirectorStats m_stats;
    std::int64_t m_now = 0;
    int m_day = 0;
    int m_scheduledDay = 0;
    std::uint64_t m_instanceSerial = 0;
};

}  // namespace zl::game
