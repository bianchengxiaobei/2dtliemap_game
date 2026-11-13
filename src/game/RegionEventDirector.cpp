#include "game/RegionEventDirector.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "core/Constants.h"

namespace zl::game {
namespace {

constexpr std::size_t kMaxRegionEvents = 256;
constexpr std::size_t kMaxRegionEventIdBytes = 128;
constexpr std::size_t kMaxRegionIdBytes = 128;

std::uint64_t HashText(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char value : text) {
        hash ^= static_cast<std::uint64_t>(value);
        hash *= 1099511628211ull;
    }
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

bool Fail(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
    return false;
}

}  // namespace

const char* RegionEventKindName(RegionEventKind kind) {
    switch (kind) {
        case RegionEventKind::Horde:
            return "尸潮";
        case RegionEventKind::Caravan:
            return "商队/巡逻";
        case RegionEventKind::SupplyDrop:
            return "空投";
        case RegionEventKind::Outbreak:
            return "感染爆发";
    }
    return "未知事件";
}

const char* RegionEventStatusName(RegionEventStatus status) {
    switch (status) {
        case RegionEventStatus::Scheduled:
            return "待触发";
        case RegionEventStatus::Active:
            return "进行中";
        case RegionEventStatus::Frozen:
            return "已冻结";
        case RegionEventStatus::Resolved:
            return "已结束";
    }
    return "未知状态";
}

RegionEventDirector::RegionEventDirector(const world::WorldDefinition& definition,
                                         RegionEventConfig config)
    : m_definition(definition), m_config(config) {
    m_config.durationSeconds =
        std::max(1.0f, std::isfinite(m_config.durationSeconds)
                           ? m_config.durationSeconds
                           : 240.0f);
    m_config.contentRadius =
        std::max(0.0f, std::isfinite(m_config.contentRadius)
                           ? m_config.contentRadius
                           : 24.0f);
}

void RegionEventDirector::Reset() {
    m_events.clear();
    m_visibleRegions.clear();
    m_stats = {};
    m_day = 0;
}

void RegionEventDirector::AdvanceDay(int day) {
    const int safeDay = std::max(1, day);
    if (m_day == safeDay && !m_events.empty()) {
        return;
    }
    m_day = safeDay;
    m_events.clear();
    m_visibleRegions.clear();
    m_stats = {};
    m_events.reserve(m_definition.regions.size());
    for (const world::WorldRegion& region : m_definition.regions) {
        if (m_events.size() >= kMaxRegionEvents) {
            break;
        }
        m_events.push_back(BuildEvent(region, safeDay));
    }
}

bool RegionEventDirector::Restore(const std::vector<RegionEvent>& events,
                                  int day, std::string* error) {
    if (events.size() > kMaxRegionEvents) {
        return Fail(error, "区域事件数量超过上限");
    }
    const int safeDay = std::max(1, day);
    std::unordered_set<std::string> ids;
    for (const RegionEvent& event : events) {
        if (event.id.empty() || event.id.size() > kMaxRegionEventIdBytes ||
            event.regionId.empty() || event.regionId.size() > kMaxRegionIdBytes) {
            return Fail(error, "区域事件 id/regionId 非法");
        }
        if (!ids.insert(event.id).second) {
            return Fail(error, "区域事件 id 重复");
        }
        if (FindRegion(event.regionId) == nullptr) {
            return Fail(error, "区域事件引用未知区域: " + event.regionId);
        }
        if (static_cast<std::uint8_t>(event.kind) > 3 ||
            static_cast<std::uint8_t>(event.status) > 3) {
            return Fail(error, "区域事件类型/状态非法");
        }
        if (!std::isfinite(event.x) || !std::isfinite(event.y) ||
            !std::isfinite(event.remainingSeconds) ||
            event.remainingSeconds < 0.0f) {
            return Fail(error, "区域事件坐标/剩余时间非法");
        }
        if (event.day < 0 || event.targetCount < 0 || event.targetCount > 10000 ||
            event.spawnedCount < 0 || event.spawnedCount > 10000) {
            return Fail(error, "区域事件计数/日期非法");
        }
    }

    m_events = events;
    m_visibleRegions.clear();
    m_stats = {};
    m_day = safeDay;
    if (m_events.empty()) {
        AdvanceDay(safeDay);
        return true;
    }
    for (const RegionEvent& event : m_events) {
        switch (event.status) {
            case RegionEventStatus::Scheduled:
                ++m_stats.scheduled;
                break;
            case RegionEventStatus::Active:
                ++m_stats.active;
                break;
            case RegionEventStatus::Frozen:
                ++m_stats.frozen;
                break;
            case RegionEventStatus::Resolved:
                ++m_stats.resolved;
                break;
        }
    }
    return true;
}

void RegionEventDirector::Update(float dt, float focusX, float focusY,
                                 const PhaseQuery& phaseOf,
                                 const SpawnCallback& spawn) {
    std::vector<glm::vec2> focuses;
    if (std::isfinite(focusX) && std::isfinite(focusY)) {
        focuses.emplace_back(focusX, focusY);
    }
    UpdateFocuses(dt, focuses, phaseOf, spawn);
}

void RegionEventDirector::UpdateFocuses(float dt,
                                        const std::vector<glm::vec2>& focuses,
                                        const PhaseQuery& phaseOf,
                                        const SpawnCallback& spawn) {
    if (!m_config.enabled) {
        return;
    }
    if (!std::isfinite(dt) || dt < 0.0f) {
        dt = 0.0f;
    }
    const float radiusSq = m_config.contentRadius * m_config.contentRadius;
    m_visibleRegions.clear();
    m_stats = {};
    int spawned = 0;

    for (RegionEvent& event : m_events) {
        if (event.status == RegionEventStatus::Resolved) {
            continue;
        }

        const world::RegionPhase phase =
            phaseOf ? phaseOf(event.regionId) : world::RegionPhase::Frozen;
        if (phase == world::RegionPhase::Frozen) {
            if (event.status == RegionEventStatus::Active) {
                event.status = RegionEventStatus::Frozen;
            }
            continue;
        }
        m_visibleRegions.insert(event.regionId);
        if (phase != world::RegionPhase::Active) {
            if (event.status == RegionEventStatus::Active) {
                event.status = RegionEventStatus::Frozen;
            }
            continue;
        }

        bool near = false;
        for (const glm::vec2& focus : focuses) {
            const float dx = event.x - focus.x;
            const float dy = event.y - focus.y;
            if (dx * dx + dy * dy <= radiusSq) {
                near = true;
                break;
            }
        }
        if (!near) {
            if (event.status == RegionEventStatus::Active) {
                event.status = RegionEventStatus::Frozen;
            }
            continue;
        }

        if (event.status == RegionEventStatus::Scheduled ||
            event.status == RegionEventStatus::Frozen) {
            event.status = RegionEventStatus::Active;
        }
        if (!event.contentSpawned) {
            const int count = spawn ? spawn(event) : event.targetCount;
            if (count > 0) {
                event.contentSpawned = true;
                event.spawnedCount = count;
                spawned += count;
            }
        }
        if (!event.contentSpawned) {
            continue;
        }
        event.remainingSeconds =
            std::max(0.0f, event.remainingSeconds - dt);
        if (event.remainingSeconds <= 0.0f) {
            event.status = RegionEventStatus::Resolved;
        }
    }

    m_stats = {};
    for (const RegionEvent& event : m_events) {
        switch (event.status) {
            case RegionEventStatus::Scheduled:
                ++m_stats.scheduled;
                break;
            case RegionEventStatus::Active:
                ++m_stats.active;
                break;
            case RegionEventStatus::Frozen:
                ++m_stats.frozen;
                break;
            case RegionEventStatus::Resolved:
                ++m_stats.resolved;
                break;
        }
    }
    m_stats.spawned = spawned;
}

const RegionEvent* RegionEventDirector::NearestVisible(float x, float y,
                                                       float maxDistance) const {
    if (!std::isfinite(x) || !std::isfinite(y) || maxDistance < 0.0f) {
        return nullptr;
    }
    const float maxDistSq = maxDistance * maxDistance;
    const RegionEvent* best = nullptr;
    float bestDistSq = std::numeric_limits<float>::max();
    for (const RegionEvent& event : m_events) {
        if (event.status == RegionEventStatus::Resolved ||
            !m_visibleRegions.contains(event.regionId)) {
            continue;
        }
        const float dx = event.x - x;
        const float dy = event.y - y;
        const float distSq = dx * dx + dy * dy;
        if (distSq <= maxDistSq && distSq < bestDistSq) {
            bestDistSq = distSq;
            best = &event;
        }
    }
    return best;
}

const world::WorldRegion* RegionEventDirector::FindRegion(
    std::string_view id) const {
    for (const world::WorldRegion& region : m_definition.regions) {
        if (region.id == id) {
            return &region;
        }
    }
    return nullptr;
}

RegionEvent RegionEventDirector::BuildEvent(const world::WorldRegion& region,
                                            int day) const {
    RegionEvent event;
    event.regionId = region.id;
    event.kind = KindFor(m_definition, region.id, day);
    event.day = day;
    event.remainingSeconds = m_config.durationSeconds;
    event.targetCount = TargetCount(event.kind);
    event.id = "region_event_" + std::to_string(day) + "_" +
               std::to_string(static_cast<int>(event.kind)) + "_" +
               HexId(HashText(region.id));
    ResolveSite(region, &event.x, &event.y);
    return event;
}

void RegionEventDirector::ResolveSite(const world::WorldRegion& region,
                                      float* x, float* y) const {
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
    for (const std::string& poiId : region.poiIds) {
        const auto found = std::find_if(
            m_definition.pois.begin(), m_definition.pois.end(),
            [&poiId](const world::WorldPoi& poi) { return poi.id == poiId; });
        if (found != m_definition.pois.end()) {
            siteX = static_cast<float>(found->x);
            siteY = static_cast<float>(found->y);
            break;
        }
    }

    if (region.rect.chunksW > 0 && region.rect.chunksH > 0) {
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
        siteX = std::clamp(siteX, minX, maxX);
        siteY = std::clamp(siteY, minY, maxY);
    }

    const world::WorldBounds& bounds = m_definition.bounds;
    if (bounds.chunksX > 0 && bounds.chunksY > 0) {
        const float minX =
            static_cast<float>(bounds.chunkX0) * core::kChunkSize + 0.5f;
        const float maxX =
            static_cast<float>(bounds.chunkX0 + bounds.chunksX) *
                core::kChunkSize -
            0.5f;
        const float minY =
            static_cast<float>(bounds.chunkY0) * core::kChunkSize + 0.5f;
        const float maxY =
            static_cast<float>(bounds.chunkY0 + bounds.chunksY) *
                core::kChunkSize -
            0.5f;
        siteX = std::clamp(siteX, minX, maxX);
        siteY = std::clamp(siteY, minY, maxY);
    }
    *x = siteX;
    *y = siteY;
}

RegionEventKind RegionEventDirector::KindFor(
    const world::WorldDefinition& definition, std::string_view regionId,
    int day) {
    std::uint64_t hash = HashText(regionId);
    hash ^= static_cast<std::uint64_t>(std::max(0, day)) *
            0x9E3779B97F4A7C15ull;
    hash ^= static_cast<std::uint64_t>(definition.seed) *
            0xC2B2AE3D27D4EB4Full;
    hash ^= hash >> 30;
    hash *= 0xBF58476D1CE4E5B9ull;
    hash ^= hash >> 27;
    hash *= 0x94D049BB133111EBull;
    hash ^= hash >> 31;
    switch (hash % 4u) {
        case 0:
            return RegionEventKind::Horde;
        case 1:
            return RegionEventKind::Caravan;
        case 2:
            return RegionEventKind::SupplyDrop;
        default:
            return RegionEventKind::Outbreak;
    }
}

int RegionEventDirector::TargetCount(RegionEventKind kind) {
    switch (kind) {
        case RegionEventKind::Horde:
            return 8;
        case RegionEventKind::Caravan:
            return 2;
        case RegionEventKind::SupplyDrop:
            return 2;
        case RegionEventKind::Outbreak:
            return 6;
    }
    return 0;
}

}  // namespace zl::game
