#include "game/WorldMap.h"

#include <algorithm>
#include <cmath>
#include <string_view>

#include "core/Constants.h"
#include "game/Poi.h"
#include "game/RegionEventDirector.h"
#include "game/WorldEventDirector.h"
#include "world/WorldGraph.h"

namespace zl::game {
namespace {

float Distance(float ax, float ay, float bx, float by) {
    const float dx = ax - bx;
    const float dy = ay - by;
    return std::sqrt(dx * dx + dy * dy);
}

const world::WorldRegion* RegionForPoint(const world::WorldGraph& graph,
                                         float x, float y) {
    return graph.RegionAt(x, y);
}

bool InSet(const std::unordered_set<std::string>& set,
           const std::string& id) {
    return set.find(id) != set.end();
}

}  // namespace

bool UpdateWorldMapDiscovery(const world::WorldGraph& graph,
                             const PoiConfig& poiConfig, float playerX,
                             float playerY, float poiRadius,
                             std::unordered_set<std::string>* regions,
                             std::unordered_set<std::string>* poiIds) {
    if (regions == nullptr || poiIds == nullptr ||
        !std::isfinite(playerX) || !std::isfinite(playerY)) {
        return false;
    }
    bool changed = false;
    if (const world::WorldRegion* region =
            RegionForPoint(graph, playerX, playerY);
        region != nullptr) {
        changed = regions->insert(region->id).second || changed;
    }
    const float radius = std::max(0.0f, poiRadius);
    for (const PoiDef& poi : poiConfig.defs) {
        const float discoverRange = radius;
        if (Distance(playerX, playerY, poi.markerX, poi.markerY) >
            discoverRange) {
            continue;
        }
        changed = poiIds->insert(poi.id).second || changed;
    }
    return changed;
}

WorldMapState BuildWorldMapState(
    const world::WorldGraph& graph, const PoiConfig& poiConfig,
    float playerX, float playerY, bool playerValid,
    const std::unordered_set<std::string>& discoveredRegions,
    const std::unordered_set<std::string>& discoveredPois,
    const std::unordered_set<std::string>& completedPois,
    const RegionEventDirector* events, const WorldMapObjectiveInput& objective,
    const WorldMapRoute& route, const StrategicMapState* strategic,
    const WorldMapTravelConfig& travel,
    const WorldEventDirector* worldEvents,
    const std::vector<WorldMapPoiProgress>* poiProgress) {
    WorldMapState state;
    if (!graph.Valid()) {
        return state;
    }

    state.valid = true;
    state.travel = travel;
    if (strategic != nullptr) {
        state.strategic = *strategic;
        state.gameMinute = strategic->gameMinute;
    }
    const world::WorldDefinition& definition = graph.Definition();
    state.mapId = definition.mapId;
    state.width =
        static_cast<float>(definition.bounds.chunksX * core::kChunkSize);
    state.height =
        static_cast<float>(definition.bounds.chunksY * core::kChunkSize);
    state.playerValid = playerValid;
    state.playerX = playerX;
    state.playerY = playerY;

    for (const world::WorldRegion& region : definition.regions) {
        WorldMapRegion entry;
        entry.id = region.id;
        entry.name = WorldMapRegionName(region.id);
        entry.biome = WorldMapBiomeName(region.biome);
        entry.x = static_cast<float>(region.rect.chunkX * core::kChunkSize);
        entry.y = static_cast<float>(region.rect.chunkY * core::kChunkSize);
        entry.width =
            static_cast<float>(region.rect.chunksW * core::kChunkSize);
        entry.height =
            static_cast<float>(region.rect.chunksH * core::kChunkSize);
        entry.discovered = InSet(discoveredRegions, region.id);
        state.regions.push_back(std::move(entry));
        if (state.regions.back().discovered) {
            ++state.discoveredRegionCount;
        }
    }
    state.regionCount = static_cast<int>(state.regions.size());

    const auto strategicRegionAt =
        [&](float x, float y) -> const StrategicMapRegion* {
        const world::WorldRegion* region = graph.RegionAt(x, y);
        if (region == nullptr || !state.strategic.enabled) {
            return nullptr;
        }
        return state.strategic.FindRegion(region->id);
    };

    for (const world::GraphEdge& edge : graph.Edges()) {
        const world::GraphNode* a = graph.Node(edge.a);
        const world::GraphNode* b = graph.Node(edge.b);
        if (a == nullptr || b == nullptr) {
            continue;
        }
        WorldMapRoad road;
        road.x0 = a->position.x;
        road.y0 = a->position.y;
        road.x1 = b->position.x;
        road.y1 = b->position.y;
        road.bridge = edge.kind == "bridge";
        road.vehiclePassable = edge.vehiclePassable;
        const StrategicMapRegion* middleRegion = strategicRegionAt(
            (road.x0 + road.x1) * 0.5f, (road.y0 + road.y1) * 0.5f);
        if (middleRegion != nullptr &&
            middleRegion->owner == StrategicOwner::Infested) {
            road.vehiclePassable = false;
        }
        state.roads.push_back(road);
        if (road.bridge) {
            WorldMapWater water;
            if (std::abs(road.x1 - road.x0) >= std::abs(road.y1 - road.y0)) {
                water.x = std::min(road.x0, road.x1);
                water.y = 0.0f;
                water.width = std::abs(road.x1 - road.x0);
                water.height = state.height;
            } else {
                water.x = 0.0f;
                water.y = std::min(road.y0, road.y1);
                water.width = state.width;
                water.height = std::abs(road.y1 - road.y0);
            }
            state.waters.push_back(water);
        }
    }

    for (const world::GraphNode& node : graph.Nodes()) {
        WorldMapLandmark landmark;
        landmark.id = node.id;
        landmark.name = WorldMapNodeName(node.id);
        landmark.kind = node.kind;
        landmark.x = node.position.x;
        landmark.y = node.position.y;
        const world::WorldRegion* region = graph.RegionAt(node.position.x,
                                                          node.position.y);
        landmark.discovered =
            region != nullptr && InSet(discoveredRegions, region->id);
        state.landmarks.push_back(std::move(landmark));
    }

    if (const world::WorldRegion* region =
            RegionForPoint(graph, playerX, playerY);
        region != nullptr) {
        state.currentRegion = region->id;
        state.currentRegionName = WorldMapRegionName(region->id);
    } else {
        state.currentRegionName = "荒野";
    }
    if (const world::GraphNode* nearest =
            playerValid ? graph.NearestNode(playerX, playerY) : nullptr;
        nearest != nullptr) {
        state.nearestLandmark = nearest->id;
        state.nearestLandmarkName = WorldMapNodeName(nearest->id);
    }

    const auto appendPoi = [&](const std::string& id, const std::string& name,
                               float x, float y, float radius, int threat) {
        WorldMapPoi poi;
        poi.id = id;
        poi.name = name.empty() ? id : name;
        poi.x = x;
        poi.y = y;
        poi.radius = radius;
        poi.threat = threat;
        poi.discovered = InSet(discoveredPois, id);
        poi.completed = InSet(completedPois, id);
        if (poiProgress != nullptr) {
            const auto found = std::find_if(
                poiProgress->begin(), poiProgress->end(),
                [&](const WorldMapPoiProgress& item) {
                    return item.id == id;
                });
            if (found != poiProgress->end() && found->stageCount > 0) {
                poi.staged = true;
                poi.stageIndex = found->stageIndex;
                poi.stageCount = found->stageCount;
                poi.completedStageCount = found->completedStageCount;
                poi.stageFailed = found->failed;
                poi.stageAwaitingChoice = found->awaitingChoice;
                poi.stageX = found->hasStagePoint ? found->stageX : x;
                poi.stageY = found->hasStagePoint ? found->stageY : y;
                poi.stageLabel = found->stageLabel;
                if (found->completed) {
                    poi.completed = true;
                }
            }
        }
        const StrategicMapRegion* region = strategicRegionAt(x, y);
        poi.disabled =
            region != nullptr && region->owner == StrategicOwner::Infested;
        state.pois.push_back(std::move(poi));
    };
    if (!poiConfig.defs.empty()) {
        for (const PoiDef& poi : poiConfig.defs) {
            appendPoi(poi.id, poi.name, poi.markerX, poi.markerY, 0.0f,
                      poi.threat);
        }
    } else {
        for (const world::WorldPoi& poi : definition.pois) {
            appendPoi(poi.id, poi.id, static_cast<float>(poi.x),
                      static_cast<float>(poi.y),
                      static_cast<float>(poi.radius), poi.threat);
        }
    }
    state.poiCount = static_cast<int>(state.pois.size());
    for (const WorldMapPoi& poi : state.pois) {
        if (poi.discovered) {
            ++state.discoveredPoiCount;
        }
    }

    if (events != nullptr) {
        for (const RegionEvent& event : events->Events()) {
            const bool active = event.status == RegionEventStatus::Active ||
                                event.status == RegionEventStatus::Frozen;
            const bool known =
                InSet(discoveredRegions, event.regionId);
            if (!active && !known) {
                continue;
            }
            WorldMapEventMarker marker;
            marker.id = event.id;
            marker.kind = WorldMapEventKindName(
                static_cast<int>(event.kind));
            marker.status = WorldMapEventStatusName(
                static_cast<int>(event.status));
            marker.x = event.x;
            marker.y = event.y;
            marker.active = active;
            state.events.push_back(std::move(marker));
        }
    }

    if (worldEvents != nullptr) {
        for (const WorldEventInstance& event : worldEvents->Events()) {
            const bool active =
                event.status == WorldEventStatus::Active;
            const bool warning =
                event.status == WorldEventStatus::Warning;
            if (!active && !warning) {
                continue;
            }
            WorldMapWorldEventMarker marker;
            marker.id = event.id;
            marker.name = event.name;
            marker.kind = event.kind;
            marker.status = WorldEventStatusName(event.status);
            marker.regionId = event.regionId;
            marker.regionName = WorldMapRegionName(event.regionId);
            for (const WorldMapRegion& region : state.regions) {
                if (region.id == event.regionId && !region.name.empty()) {
                    marker.regionName = region.name;
                    break;
                }
            }
            marker.x = event.x;
            marker.y = event.y;
            marker.active = active;
            marker.warning = warning;
            state.worldEvents.push_back(std::move(marker));
        }
    }

    state.objective.valid = objective.valid;
    state.objective.title = objective.title;
    state.objective.x = objective.x;
    state.objective.y = objective.y;
    state.route = route;
    state.routeCost = EstimateWorldMapTravel(route.distance, travel);
    return state;
}

std::string WorldMapPoiLabel(const WorldMapPoi& poi) {
    if (!poi.discovered) {
        return "未知地点";
    }
    std::string label = poi.name;
    if (poi.disabled) {
        return label + "（沦陷）";
    }
    if (poi.completed) {
        return label + "（完成）";
    }
    if (poi.staged && poi.stageCount > 0) {
        label += "（";
        if (poi.stageFailed) {
            label += "阶段 " + std::to_string(poi.stageIndex + 1) + "/" +
                     std::to_string(poi.stageCount) + " · 失败可重试";
        } else {
            label += poi.stageLabel.empty()
                         ? "阶段 " + std::to_string(poi.stageIndex + 1) +
                               "/" + std::to_string(poi.stageCount)
                         : poi.stageLabel;
        }
        label += "）";
        return label;
    }
    return label;
}

WorldMapRoute BuildWorldMapRoute(const world::WorldGraph& graph,
                                 glm::vec2 start, glm::vec2 goal,
                                 const std::string& label, bool vehicleOnly) {
    WorldMapRoute result;
    result.label = label;
    if (!graph.Valid()) {
        return result;
    }
    const world::RoadRoute route = graph.PlanRoute(start, goal, vehicleOnly);
    if (route.outcome != world::RoadRoute::Outcome::Found ||
        route.points.empty()) {
        return result;
    }
    result.valid = true;
    result.distance = route.totalDistance;
    if (const world::WorldRegion* target =
            graph.RegionAt(goal.x, goal.y);
        target != nullptr) {
        result.targetRegion = target->id;
    }
    result.points.reserve(route.points.size());
    for (const glm::vec2& point : route.points) {
        result.points.push_back({point.x, point.y});
    }
    return result;
}

WorldMapRouteCost EstimateWorldMapTravel(
    float roadDistance, const WorldMapTravelConfig& config) {
    WorldMapRouteCost cost;
    const float detour =
        std::clamp(config.detourFactor, 1.0f, 3.0f);
    if (!std::isfinite(roadDistance) || roadDistance <= 0.0f ||
        !std::isfinite(config.walkSpeed) || config.walkSpeed <= 0.0f ||
        !std::isfinite(config.vehicleSpeed) || config.vehicleSpeed <= 0.0f ||
        !std::isfinite(config.fuelPerSecond) ||
        config.fuelPerSecond < 0.0f ||
        !std::isfinite(config.gameHoursPerRealSecond) ||
        config.gameHoursPerRealSecond <= 0.0f) {
        return cost;
    }

    cost.valid = true;
    cost.roadDistance = roadDistance;
    cost.travelDistance = roadDistance * detour;
    cost.walkSeconds = cost.travelDistance / config.walkSpeed;
    cost.vehicleSeconds = cost.travelDistance / config.vehicleSpeed;
    cost.walkGameHours =
        cost.walkSeconds * config.gameHoursPerRealSecond;
    cost.vehicleGameHours =
        cost.vehicleSeconds * config.gameHoursPerRealSecond;
    cost.vehicleFuel =
        cost.vehicleSeconds * config.fuelPerSecond;
    cost.fuelEnough =
        config.vehicleAvailable &&
        config.currentFuel + 0.0001f >= cost.vehicleFuel;
    return cost;
}

const char* WorldMapRegionName(const std::string& id) {
    if (id == "west_woods") {
        return "西部林地";
    }
    if (id == "village_core") {
        return "村庄核心";
    }
    if (id == "north_woods") {
        return "北部林地";
    }
    if (id == "riverlands") {
        return "河谷";
    }
    if (id == "east_wilds") {
        return "东部荒野";
    }
    if (id == "west_fields") {
        return "西部农田";
    }
    if (id == "north_fields") {
        return "北部农田";
    }
    if (id == "south_farmland") {
        return "南部农田";
    }
    if (id == "east_approach") {
        return "东部林道";
    }
    if (id == "deep_south") {
        return "南部密林";
    }
    return "未知区域";
}

const char* WorldMapStrategicActionLabel(WorldMapStrategicAction action) {
    switch (action) {
        case WorldMapStrategicAction::Travel:
            return "前往";
        case WorldMapStrategicAction::Support:
            return "支援";
        case WorldMapStrategicAction::Donate:
            return "捐献";
        case WorldMapStrategicAction::ClearCore:
            return "清理核心";
        case WorldMapStrategicAction::Repair:
            return "修复设施";
        case WorldMapStrategicAction::Reclaim:
            return "反攻";
    }
    return "行动";
}

const char* WorldMapNodeName(const std::string& id) {
    if (id == "village_haven") {
        return "村庄避风港";
    }
    if (id == "west_gate") {
        return "西侧路口";
    }
    if (id == "clinic_junction") {
        return "诊所路口";
    }
    if (id == "clinic") {
        return "诊所";
    }
    if (id == "river_junction") {
        return "河道路口";
    }
    if (id == "bridge_west") {
        return "西桥头";
    }
    if (id == "bridge_east") {
        return "东桥头";
    }
    if (id == "east_junction") {
        return "东部路口";
    }
    if (id == "checkpoint") {
        return "检查站";
    }
    if (id == "farm_junction") {
        return "农场路口";
    }
    if (id == "farmstead") {
        return "农庄";
    }
    return id.c_str();
}

const char* WorldMapBiomeName(const std::string& biome) {
    if (biome == "forest") {
        return "林地";
    }
    if (biome == "village") {
        return "村庄";
    }
    if (biome == "river") {
        return "河谷";
    }
    if (biome == "wilds") {
        return "荒野";
    }
    if (biome == "farmland") {
        return "农田";
    }
    return biome.c_str();
}

const char* WorldMapEventKindName(int kind) {
    switch (static_cast<RegionEventKind>(kind)) {
        case RegionEventKind::Horde:
            return "尸潮";
        case RegionEventKind::Caravan:
            return "商队";
        case RegionEventKind::SupplyDrop:
            return "空投";
        case RegionEventKind::Outbreak:
            return "感染爆发";
    }
    return "事件";
}

const char* WorldMapEventStatusName(int status) {
    switch (static_cast<RegionEventStatus>(status)) {
        case RegionEventStatus::Scheduled:
            return "预定";
        case RegionEventStatus::Active:
            return "进行中";
        case RegionEventStatus::Frozen:
            return "冻结";
        case RegionEventStatus::Resolved:
            return "已解决";
    }
    return "未知";
}

}  // namespace zl::game
