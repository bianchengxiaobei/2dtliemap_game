#include "game/WorldEventDirector.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/Constants.h"

namespace zl::game {
namespace {

constexpr std::int64_t kMinutesPerDay = 1440;
constexpr int kEarliestActiveMinute = 8 * 60;

std::uint64_t HashText(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char value : text) {
        hash ^= static_cast<std::uint64_t>(value);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::uint64_t Mix(std::uint64_t value) {
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31;
    return value;
}

std::uint64_t AddHash(std::uint64_t hash, std::uint64_t value) {
    return Mix(hash ^ Mix(value + 0x9E3779B97F4A7C15ull));
}

std::uint64_t HashKey(std::uint32_t seed, int day, std::string_view a,
                      std::string_view b = {}) {
    std::uint64_t hash = 1469598103934665603ull;
    hash = AddHash(hash, seed);
    hash = AddHash(hash, static_cast<std::uint64_t>(std::max(0, day)));
    hash = AddHash(hash, HashText(a));
    hash = AddHash(hash, HashText(b));
    return hash;
}

std::string HexId(std::uint64_t value) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int i = 15; i >= 0; --i) {
        result[static_cast<std::size_t>(i)] = kHex[value & 0xFu];
        value >>= 4u;
    }
    return result;
}

void ClearError(std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
}

bool SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
    return false;
}

bool IsTerminal(WorldEventStatus status) {
    return status == WorldEventStatus::Resolved ||
           status == WorldEventStatus::Failed ||
           status == WorldEventStatus::Expired;
}

bool Near(float x, float y, const std::vector<glm::vec2>& focuses,
          float radius) {
    if (!std::isfinite(radius) || radius < 0.0f) {
        return false;
    }
    const float radiusSq = radius * radius;
    for (const glm::vec2& focus : focuses) {
        const float dx = x - focus.x;
        const float dy = y - focus.y;
        if (dx * dx + dy * dy <= radiusSq) {
            return true;
        }
    }
    return false;
}

std::int64_t DayForMinute(double minute) {
    return static_cast<std::int64_t>(std::floor(minute / 1440.0));
}

}  // namespace

const char* WorldEventStatusName(WorldEventStatus status) {
    switch (status) {
        case WorldEventStatus::Scheduled:
            return "已排期";
        case WorldEventStatus::Warning:
            return "预警";
        case WorldEventStatus::Active:
            return "进行中";
        case WorldEventStatus::Resolved:
            return "已解决";
        case WorldEventStatus::Failed:
            return "失败";
        case WorldEventStatus::Expired:
            return "已过期";
    }
    return "未知";
}

WorldEventDirector::WorldEventDirector(
    const WorldEventConfig& config, const world::WorldDefinition& definition,
    WorldEventDirectorConfig options)
    : m_config(config),
      m_definition(definition),
      m_options(options) {
    if (!std::isfinite(m_options.detailRadius) || m_options.detailRadius < 0.0f) {
        m_options.detailRadius = 32.0f;
    }
}

void WorldEventDirector::Reset() {
    m_events.clear();
    m_lastDefDay.clear();
    m_stats = {};
    m_now = 0;
    m_day = 0;
    m_scheduledDay = 0;
    m_instanceSerial = 0;
}

void WorldEventDirector::Update(double nowGameMinutes,
                                const std::vector<glm::vec2>& focuses) {
    if (!std::isfinite(nowGameMinutes) || nowGameMinutes < 0.0) {
        return;
    }
    const std::int64_t now =
        static_cast<std::int64_t>(std::floor(nowGameMinutes));
    const int day = static_cast<int>(DayForMinute(nowGameMinutes)) + 1;
    if (day < m_day) {
        Reset();
    }
    m_now = now;
    m_day = day;

    std::vector<glm::vec2> validFocuses;
    validFocuses.reserve(focuses.size());
    for (const glm::vec2& focus : focuses) {
        if (std::isfinite(focus.x) && std::isfinite(focus.y)) {
            validFocuses.push_back(focus);
        }
    }

    for (WorldEventInstance& event : m_events) {
        RefreshEvent(event, now, validFocuses);
    }

    if (m_scheduledDay < day) {
        const std::size_t before = m_events.size();
        EnsureDay(day);
        for (std::size_t i = before; i < m_events.size(); ++i) {
            RefreshEvent(m_events[i], now, validFocuses);
        }
    }
    PruneEvents(day);
    RefreshStats();
}

void WorldEventDirector::Update(double nowGameMinutes, float focusX,
                                float focusY) {
    std::vector<glm::vec2> focuses;
    if (std::isfinite(focusX) && std::isfinite(focusY)) {
        focuses.emplace_back(focusX, focusY);
    }
    Update(nowGameMinutes, focuses);
}

bool WorldEventDirector::ReportStageProgress(std::string_view eventId,
                                             WorldEventStageKind kind,
                                             std::string_view target, int amount,
                                             std::string* error) {
    ClearError(error);
    if (amount <= 0) {
        return SetError(error, "world event stage progress must be positive");
    }
    WorldEventInstance* event = nullptr;
    for (WorldEventInstance& candidate : m_events) {
        if (candidate.id == eventId) {
            event = &candidate;
            break;
        }
    }
    if (event == nullptr) {
        return SetError(error, "world event instance not found");
    }
    if (event->status != WorldEventStatus::Active) {
        return SetError(error, "world event is not active");
    }
    if (event->stagesComplete) {
        return SetError(error, "world event stages are already complete");
    }
    if (event->stageKind != kind) {
        return SetError(error, "world event stage kind does not match");
    }
    if (kind == WorldEventStageKind::Choice) {
        return SetError(error, "world event choice stage requires Choose");
    }
    if (kind != WorldEventStageKind::Reach && !target.empty() &&
        !event->stageTargetId.empty() && target != event->stageTargetId) {
        return SetError(error, "world event stage target does not match");
    }
    if (!AdvanceStage(*event, amount)) {
        return SetError(error, "world event stage could not advance");
    }
    RefreshStats();
    return true;
}

bool WorldEventDirector::Choose(std::string_view eventId,
                                std::string_view choiceId, std::string* error) {
    ClearError(error);
    WorldEventInstance* event = nullptr;
    for (WorldEventInstance& candidate : m_events) {
        if (candidate.id == eventId) {
            event = &candidate;
            break;
        }
    }
    if (event == nullptr) {
        return SetError(error, "world event instance not found");
    }
    if (event->status != WorldEventStatus::Active) {
        return SetError(error, "world event is not active");
    }
    const WorldEventDef* def = FindDef(event->defId);
    if (def == nullptr) {
        return SetError(error, "world event definition is missing");
    }
    const auto choice = std::find_if(
        def->choices.begin(), def->choices.end(),
        [choiceId](const WorldEventChoiceDef& value) {
            return value.id == choiceId;
        });
    if (choice == def->choices.end()) {
        return SetError(error, "world event choice is invalid");
    }
    if (!event->stagesComplete) {
        const bool choiceStage =
            event->stageIndex + 1 == static_cast<int>(def->stages.size()) &&
            event->stageKind == WorldEventStageKind::Choice;
        if (!choiceStage) {
            return SetError(error, "world event stages are not complete");
        }
        event->stagesComplete = true;
        event->stageIndex = static_cast<int>(def->stages.size());
        event->stageProgress = 0;
        event->stageTarget = 0;
        event->stageTargetId.clear();
    }
    if (!Resolve(*event, choiceId, error)) {
        return false;
    }
    RefreshStats();
    return true;
}

bool WorldEventDirector::Fail(std::string_view eventId, std::string* error) {
    ClearError(error);
    WorldEventInstance* event = nullptr;
    for (WorldEventInstance& candidate : m_events) {
        if (candidate.id == eventId) {
            event = &candidate;
            break;
        }
    }
    if (event == nullptr) {
        return SetError(error, "world event instance not found");
    }
    if (IsTerminal(event->status)) {
        return SetError(error, "world event is already terminal");
    }
    event->status = WorldEventStatus::Failed;
    event->resolvedMinute = m_now;
    event->detail = false;
    RefreshStats();
    return true;
}

bool WorldEventDirector::Restore(const std::vector<WorldEventInstance>& events,
                                 int day, double nowGameMinutes,
                                 std::string* error) {
    ClearError(error);
    if (!std::isfinite(nowGameMinutes) || nowGameMinutes < 0.0) {
        return SetError(error, "world event restore time is invalid");
    }
    if (day < 1 || events.size() > kMaxWorldEventInstances) {
        return SetError(error, "world event restore bounds are invalid");
    }
    std::unordered_map<std::string, bool> ids;
    for (const WorldEventInstance& event : events) {
        if (!ids.emplace(event.id, true).second) {
            return SetError(error, "world event restore has duplicate id");
        }
        if (!ValidInstance(event, error)) {
            return false;
        }
    }

    m_events = events;
    m_now = static_cast<std::int64_t>(std::floor(nowGameMinutes));
    m_day = day;
    m_scheduledDay = day;
    m_instanceSerial = events.size();
    m_lastDefDay.clear();
    for (WorldEventInstance& event : m_events) {
        auto& last = m_lastDefDay[event.defId];
        last = std::max(last, event.day);
        event.detail = false;
    }
    m_stats = {};
    for (const WorldEventInstance& event : m_events) {
        if (!event.nextEventId.empty()) {
            ++m_stats.chained;
        }
    }
    RefreshStats();
    return true;
}

const WorldEventInstance* WorldEventDirector::Find(std::string_view id) const {
    for (const WorldEventInstance& event : m_events) {
        if (event.id == id) {
            return &event;
        }
    }
    return nullptr;
}

void WorldEventDirector::MarkContentSpawned(std::string_view eventId) {
    for (WorldEventInstance& event : m_events) {
        if (event.id == eventId) {
            event.contentSpawned = true;
            return;
        }
    }
}

const WorldEventInstance* WorldEventDirector::ActiveDetailed() const {
    for (const WorldEventInstance& event : m_events) {
        if (event.status == WorldEventStatus::Active && event.detail) {
            return &event;
        }
    }
    return nullptr;
}

const WorldEventDef* WorldEventDirector::FindDef(std::string_view id) const {
    return m_config.Find(id);
}

const world::WorldRegion* WorldEventDirector::FindRegion(
    std::string_view id) const {
    for (const world::WorldRegion& region : m_definition.regions) {
        if (region.id == id) {
            return &region;
        }
    }
    return nullptr;
}

void WorldEventDirector::EnsureDay(int day) {
    m_scheduledDay = day;
    if (!Enabled()) {
        return;
    }
    for (const WorldEventInstance& event : m_events) {
        if (!IsTerminal(event.status) && event.day <= day) {
            return;
        }
    }
    const WorldEventDef* def = SelectDefinition(day);
    if (def == nullptr) {
        return;
    }
    const world::WorldRegion* region = SelectRegion(*def, day);
    if (region == nullptr) {
        return;
    }
    WorldEventInstance event = BuildInstance(*def, *region, day, region->id);
    if (m_now >= event.activeMinute) {
        event.status = WorldEventStatus::Expired;
        event.resolvedMinute = event.activeMinute;
    } else if (m_now >= event.warningMinute) {
        event.status = WorldEventStatus::Warning;
    }
    m_lastDefDay[def->id] = day;
    m_events.push_back(std::move(event));
}

void WorldEventDirector::PruneEvents(int day) {
    constexpr int kHistoryDays = 7;
    m_events.erase(
        std::remove_if(m_events.begin(), m_events.end(),
                       [day](const WorldEventInstance& event) {
                           return IsTerminal(event.status) &&
                                  event.day < day - kHistoryDays;
                       }),
        m_events.end());
    while (m_events.size() > kMaxWorldEventInstances) {
        const auto terminal = std::find_if(
            m_events.begin(), m_events.end(),
            [](const WorldEventInstance& event) {
                return IsTerminal(event.status);
            });
        if (terminal == m_events.end()) {
            break;
        }
        m_events.erase(terminal);
    }
}

void WorldEventDirector::RefreshEvent(
    WorldEventInstance& event, std::int64_t now,
    const std::vector<glm::vec2>& focuses) {
    if (IsTerminal(event.status)) {
        event.detail = false;
        return;
    }
    if (now >= event.deadlineMinute) {
        Expire(event);
        return;
    }
    if (event.status == WorldEventStatus::Scheduled &&
        now >= event.warningMinute) {
        event.status = WorldEventStatus::Warning;
    }
    if (event.status == WorldEventStatus::Warning &&
        now >= event.activeMinute) {
        event.status = WorldEventStatus::Active;
    }
    if (event.status == WorldEventStatus::Warning ||
        event.status == WorldEventStatus::Active) {
        event.detail = Near(event.x, event.y, focuses, m_options.detailRadius);
    } else {
        event.detail = false;
    }
    if (event.status != WorldEventStatus::Active) {
        return;
    }
    if (!event.detail) {
        event.downgraded = true;
    }
    if (event.stagesComplete || event.stageKind != WorldEventStageKind::Reach) {
        return;
    }
    if (Near(event.x, event.y, focuses, event.stageRadius)) {
        AdvanceStage(event, 1);
    }
}

void WorldEventDirector::RefreshStats() {
    const int chained = m_stats.chained;
    m_stats = {};
    m_stats.chained = chained;
    for (const WorldEventInstance& event : m_events) {
        switch (event.status) {
            case WorldEventStatus::Scheduled:
                ++m_stats.scheduled;
                break;
            case WorldEventStatus::Warning:
                ++m_stats.warning;
                break;
            case WorldEventStatus::Active:
                ++m_stats.active;
                if (event.detail) {
                    ++m_stats.detailed;
                }
                break;
            case WorldEventStatus::Resolved:
                ++m_stats.resolved;
                break;
            case WorldEventStatus::Failed:
                ++m_stats.failed;
                break;
            case WorldEventStatus::Expired:
                ++m_stats.expired;
                break;
        }
    }
}

bool WorldEventDirector::AdvanceStage(WorldEventInstance& event, int amount) {
    if (amount <= 0 || event.stagesComplete || event.stageTarget <= 0) {
        return false;
    }
    event.stageProgress =
        std::min(event.stageTarget, event.stageProgress + amount);
    if (event.stageProgress < event.stageTarget) {
        return true;
    }
    const WorldEventDef* def = FindDef(event.defId);
    if (def == nullptr) {
        return false;
    }
    event.stageProgress = 0;
    ++event.stageIndex;
    SetStage(event, *def);
    return true;
}

bool WorldEventDirector::Resolve(WorldEventInstance& event,
                                 std::string_view choiceId,
                                 std::string* error) {
    const WorldEventDef* def = FindDef(event.defId);
    if (def == nullptr) {
        return SetError(error, "world event definition is missing");
    }
    event.status = WorldEventStatus::Resolved;
    event.choiceId = std::string(choiceId);
    event.resolvedMinute = m_now;
    event.detail = false;
    if (!def->nextEvent.empty()) {
        event.nextEventId = def->nextEvent;
        QueueChain(event);
    }
    return true;
}

void WorldEventDirector::QueueChain(const WorldEventInstance& event) {
    if (event.nextEventId.empty() ||
        event.chainDepth >= static_cast<int>(kMaxWorldEventChainDepth)) {
        return;
    }
    const WorldEventDef* nextDef = FindDef(event.nextEventId);
    if (nextDef == nullptr) {
        return;
    }
    const int day = std::max(event.day + 1, nextDef->minDay);
    if (day > nextDef->maxDay) {
        return;
    }
    const world::WorldRegion* region = nullptr;
    if (nextDef->region == "any") {
        region = FindRegion(event.regionId);
    } else {
        region = FindRegion(nextDef->region);
    }
    if (region == nullptr) {
        return;
    }
    WorldEventInstance next =
        BuildInstance(*nextDef, *region, day, region->id);
    next.chainDepth = event.chainDepth + 1;
    m_lastDefDay[nextDef->id] = day;
    m_events.push_back(std::move(next));
    ++m_stats.chained;
}

void WorldEventDirector::Expire(WorldEventInstance& event) {
    event.status = WorldEventStatus::Expired;
    event.resolvedMinute = event.deadlineMinute;
    event.detail = false;
}

void WorldEventDirector::SetStage(WorldEventInstance& event,
                                  const WorldEventDef& def) {
    if (event.stageIndex < 0 ||
        event.stageIndex >= static_cast<int>(def.stages.size())) {
        event.stageIndex = static_cast<int>(def.stages.size());
        event.stageProgress = 0;
        event.stageTarget = 0;
        event.stageRadius = 0.0f;
        event.stageTargetId.clear();
        event.stagesComplete = true;
        return;
    }
    const WorldEventStageDef& stage = def.stages[static_cast<std::size_t>(
        event.stageIndex)];
    event.stageKind = stage.kind;
    event.stageTarget = stage.count;
    event.stageRadius = stage.radius;
    event.stageTargetId = stage.target;
    event.stageProgress = 0;
    event.stagesComplete = false;
}

const WorldEventDef* WorldEventDirector::SelectDefinition(int day) const {
    std::uint64_t total = 0;
    for (const WorldEventDef& def : m_config.defs) {
        if (day < def.minDay || day > def.maxDay) {
            continue;
        }
        const auto last = m_lastDefDay.find(def.id);
        if (last != m_lastDefDay.end() &&
            day - last->second < def.cooldownDays) {
            continue;
        }
        total += std::max<std::uint64_t>(
            1, static_cast<std::uint64_t>(std::llround(def.weight * 1000.0)));
    }
    if (total == 0) {
        return nullptr;
    }
    const std::uint64_t roll =
        HashKey(m_config.randomSeed, day, "definition") % total;
    std::uint64_t cursor = 0;
    for (const WorldEventDef& def : m_config.defs) {
        if (day < def.minDay || day > def.maxDay) {
            continue;
        }
        const auto last = m_lastDefDay.find(def.id);
        if (last != m_lastDefDay.end() &&
            day - last->second < def.cooldownDays) {
            continue;
        }
        cursor += std::max<std::uint64_t>(
            1, static_cast<std::uint64_t>(std::llround(def.weight * 1000.0)));
        if (roll < cursor) {
            return &def;
        }
    }
    return nullptr;
}

const world::WorldRegion* WorldEventDirector::SelectRegion(
    const WorldEventDef& def, int day) const {
    if (def.region != "any") {
        return FindRegion(def.region);
    }
    std::uint64_t total = 0;
    for (const world::WorldRegion& region : m_definition.regions) {
        const std::uint64_t area =
            static_cast<std::uint64_t>(std::max(1, region.rect.chunksW)) *
            static_cast<std::uint64_t>(std::max(1, region.rect.chunksH));
        total += area;
    }
    if (total == 0) {
        return nullptr;
    }
    const std::uint64_t roll =
        HashKey(m_config.randomSeed, day, def.id, "region") % total;
    std::uint64_t cursor = 0;
    for (const world::WorldRegion& region : m_definition.regions) {
        const std::uint64_t area =
            static_cast<std::uint64_t>(std::max(1, region.rect.chunksW)) *
            static_cast<std::uint64_t>(std::max(1, region.rect.chunksH));
        cursor += area;
        if (roll < cursor) {
            return &region;
        }
    }
    return nullptr;
}

glm::vec2 WorldEventDirector::SelectSite(const world::WorldRegion& region,
                                         const WorldEventDef& def,
                                         int day) const {
    float siteX =
        (static_cast<float>(region.rect.chunkX) +
         static_cast<float>(region.rect.chunksW) * 0.5f) *
            static_cast<float>(core::kChunkSize) +
        0.5f;
    float siteY =
        (static_cast<float>(region.rect.chunkY) +
         static_cast<float>(region.rect.chunksH) * 0.5f) *
            static_cast<float>(core::kChunkSize) +
        0.5f;
    if (!region.poiIds.empty()) {
        const std::size_t index = static_cast<std::size_t>(
            HashKey(m_config.randomSeed, day, def.id, "site") %
            region.poiIds.size());
        const std::string& poiId = region.poiIds[index];
        const auto poi = std::find_if(
            m_definition.pois.begin(), m_definition.pois.end(),
            [&poiId](const world::WorldPoi& value) {
                return value.id == poiId;
            });
        if (poi != m_definition.pois.end()) {
            siteX = static_cast<float>(poi->x);
            siteY = static_cast<float>(poi->y);
        }
    }
    const float minX =
        static_cast<float>(region.rect.chunkX) * core::kChunkSize + 0.5f;
    const float maxX =
        static_cast<float>(region.rect.chunkX + region.rect.chunksW) *
            core::kChunkSize -
        0.5f;
    const float minY =
        static_cast<float>(region.rect.chunkY) * core::kChunkSize + 0.5f;
    const float maxY =
        static_cast<float>(region.rect.chunkY + region.rect.chunksH) *
            core::kChunkSize -
        0.5f;
    siteX = std::clamp(siteX, minX, std::max(minX, maxX));
    siteY = std::clamp(siteY, minY, std::max(minY, maxY));
    const world::WorldBounds& bounds = m_definition.bounds;
    if (bounds.chunksX > 0 && bounds.chunksY > 0) {
        const float worldMinX =
            static_cast<float>(bounds.chunkX0) * core::kChunkSize + 0.5f;
        const float worldMaxX =
            static_cast<float>(bounds.chunkX0 + bounds.chunksX) *
                core::kChunkSize -
            0.5f;
        const float worldMinY =
            static_cast<float>(bounds.chunkY0) * core::kChunkSize + 0.5f;
        const float worldMaxY =
            static_cast<float>(bounds.chunkY0 + bounds.chunksY) *
                core::kChunkSize -
            0.5f;
        siteX = std::clamp(siteX, worldMinX, std::max(worldMinX, worldMaxX));
        siteY = std::clamp(siteY, worldMinY, std::max(worldMinY, worldMaxY));
    }
    return glm::vec2(siteX, siteY);
}

int WorldEventDirector::SelectActiveMinute(const WorldEventDef& def, int day,
                                           std::string_view regionId) const {
    const int latest = std::max(1, 1440 - def.durationMinutes);
    const int earliest =
        std::min(std::max(kEarliestActiveMinute, def.warningMinutes), latest);
    const int span = std::max(0, latest - earliest);
    const std::uint64_t roll =
        HashKey(m_config.randomSeed, day, def.id, regionId) %
        static_cast<std::uint64_t>(span + 1);
    return earliest + static_cast<int>(roll);
}

WorldEventInstance WorldEventDirector::BuildInstance(
    const WorldEventDef& def, const world::WorldRegion& region, int day,
    std::string_view regionId) {
    WorldEventInstance event;
    event.defId = def.id;
    event.name = def.name;
    event.kind = def.kind;
    event.regionId = std::string(regionId);
    const glm::vec2 site = SelectSite(region, def, day);
    event.x = site.x;
    event.y = site.y;
    event.day = day;
    event.chainDepth = 0;
    const std::int64_t dayStart =
        static_cast<std::int64_t>(day - 1) * kMinutesPerDay;
    const int activeDayMinute = SelectActiveMinute(def, day, regionId);
    event.scheduledMinute = dayStart;
    event.warningMinute =
        dayStart + std::max(0, activeDayMinute - def.warningMinutes);
    event.activeMinute = dayStart + activeDayMinute;
    event.deadlineMinute =
        event.activeMinute + static_cast<std::int64_t>(def.durationMinutes);
    event.status = WorldEventStatus::Scheduled;
    event.stageIndex = 0;
    SetStage(event, def);
    ++m_instanceSerial;
    event.id = "world_event_" + std::to_string(day) + "_" + def.id + "_" +
               HexId(HashKey(m_config.randomSeed, day, def.id, "instance") ^
                     Mix(m_instanceSerial));
    return event;
}

bool WorldEventDirector::ValidInstance(const WorldEventInstance& event,
                                       std::string* error) const {
    if (event.id.empty() || event.id.size() > 128) {
        return SetError(error, "world event id is invalid");
    }
    const WorldEventDef* def = FindDef(event.defId);
    if (def == nullptr) {
        return SetError(error, "world event definition is unknown");
    }
    if (event.regionId.empty() || FindRegion(event.regionId) == nullptr) {
        return SetError(error, "world event region is unknown");
    }
    if (static_cast<std::uint8_t>(event.status) >
        static_cast<std::uint8_t>(WorldEventStatus::Expired)) {
        return SetError(error, "world event status is invalid");
    }
    if (event.day < 1 || event.day > 100000 ||
        event.chainDepth < 0 ||
        event.chainDepth > static_cast<int>(kMaxWorldEventChainDepth)) {
        return SetError(error, "world event day/depth is invalid");
    }
    if (!std::isfinite(event.x) || !std::isfinite(event.y) ||
        event.stageIndex < 0 ||
        event.stageIndex > static_cast<int>(def->stages.size()) ||
        event.stageProgress < 0 || event.stageTarget < 0 ||
        event.stageProgress > event.stageTarget) {
        return SetError(error, "world event position/stage is invalid");
    }
    if (event.stagesComplete) {
        if (event.stageIndex != static_cast<int>(def->stages.size())) {
            return SetError(error, "world event complete stage index is invalid");
        }
    } else if (event.stageIndex >= static_cast<int>(def->stages.size())) {
        return SetError(error, "world event incomplete stage index is invalid");
    } else {
        const WorldEventStageDef& stage =
            def->stages[static_cast<std::size_t>(event.stageIndex)];
        if (event.stageKind != stage.kind ||
            event.stageTargetId != stage.target ||
            event.stageTarget != stage.count) {
            return SetError(error, "world event stage payload is invalid");
        }
    }
    if (event.scheduledMinute < 0 || event.warningMinute < 0 ||
        event.activeMinute < 0 || event.deadlineMinute <= event.activeMinute ||
        event.warningMinute < event.scheduledMinute ||
        event.activeMinute < event.warningMinute ||
        event.resolvedMinute < 0) {
        return SetError(error, "world event time is invalid");
    }
    if (event.day !=
        static_cast<int>(DayForMinute(
            static_cast<double>(event.scheduledMinute))) + 1) {
        return SetError(error, "world event schedule day is invalid");
    }
    if (event.status == WorldEventStatus::Resolved) {
        if (event.choiceId.empty() ||
            std::none_of(def->choices.begin(), def->choices.end(),
                         [&event](const WorldEventChoiceDef& choice) {
                             return choice.id == event.choiceId;
                         })) {
            return SetError(error, "world event resolved choice is invalid");
        }
    }
    return true;
}

}  // namespace zl::game
