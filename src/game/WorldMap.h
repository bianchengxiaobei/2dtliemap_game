#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "game/StrategicMap.h"
#include "world/WorldDefinition.h"

namespace zl::world {
class WorldGraph;
}

namespace zl::game {

class RegionEventDirector;
class WorldEventDirector;
struct PoiConfig;

struct WorldMapPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct WorldMapRegion {
    std::string id;
    std::string name;
    std::string biome;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    bool discovered = false;
};

struct WorldMapRoad {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    bool bridge = false;
    bool vehiclePassable = true;
};

struct WorldMapWater {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct WorldMapLandmark {
    std::string id;
    std::string name;
    std::string kind;
    float x = 0.0f;
    float y = 0.0f;
    bool discovered = false;
};

struct WorldMapPoi {
    std::string id;
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    float radius = 0.0f;
    int threat = 0;
    bool discovered = false;
    bool completed = false;
    bool disabled = false;
};

struct WorldMapEventMarker {
    std::string id;
    std::string kind;
    std::string status;
    float x = 0.0f;
    float y = 0.0f;
    bool active = false;
};

// M33-F2-T4: interactive world event marker (warning/active only).
struct WorldMapWorldEventMarker {
    std::string id;
    std::string name;
    std::string kind;
    std::string status;
    std::string regionId;
    std::string regionName;
    float x = 0.0f;
    float y = 0.0f;
    bool active = false;
    bool warning = false;
};

struct WorldMapSettlement {
    std::string id;
    std::string name;
    std::string regionId;
    std::string reputationTier;
    std::string reputationName;
    std::string services;
    int reputation = 0;
    float x = 0.0f;
    float y = 0.0f;
    bool discovered = false;
    bool fallen = false;
    bool raidActive = false;
    bool defenseEnabled = false;
};

struct WorldMapObjective {
    bool valid = false;
    std::string title;
    float x = 0.0f;
    float y = 0.0f;
};

struct WorldMapRoute {
    bool valid = false;
    std::string label;
    std::string targetRegion;
    float distance = 0.0f;
    std::vector<WorldMapPoint> points;
};

// M32-T3: route cost shown on the map. The detour factor is intentionally
// separate from the graph distance so the UI can explain "road distance" vs
// "realistic travel". No fast travel is performed here.
struct WorldMapTravelConfig {
    float walkSpeed = 4.0f;
    float vehicleSpeed = 10.0f;
    float fuelPerSecond = 0.012f;
    float currentFuel = 0.0f;
    bool vehicleAvailable = false;
    float detourFactor = 1.2f;
    float gameHoursPerRealSecond = 0.4f;
};

struct WorldMapRouteCost {
    bool valid = false;
    float roadDistance = 0.0f;
    float travelDistance = 0.0f;
    float walkSeconds = 0.0f;
    float vehicleSeconds = 0.0f;
    float walkGameHours = 0.0f;
    float vehicleGameHours = 0.0f;
    float vehicleFuel = 0.0f;
    bool fuelEnough = false;
};

struct WorldMapObjectiveInput {
    bool valid = false;
    std::string title;
    float x = 0.0f;
    float y = 0.0f;
};

// M32-T8: player commands exposed by the strategic sidebar. Travel only sets
// the existing route; the remaining actions are sent to the server in network
// mode or executed by the local strategic simulation.
enum class WorldMapStrategicAction : std::uint8_t {
    Travel = 0,
    Support = 1,
    Donate = 2,
    ClearCore = 3,
    Repair = 4,
    Reclaim = 5,
};

const char* WorldMapStrategicActionLabel(WorldMapStrategicAction action);

struct WorldMapState {
    bool valid = false;
    std::string mapId;
    float width = 0.0f;
    float height = 0.0f;

    bool playerValid = false;
    float playerX = 0.0f;
    float playerY = 0.0f;
    std::string currentRegion;
    std::string currentRegionName;
    std::string nearestLandmark;
    std::string nearestLandmarkName;

    std::vector<WorldMapRegion> regions;
    std::vector<WorldMapRoad> roads;
    std::vector<WorldMapWater> waters;
    std::vector<WorldMapLandmark> landmarks;
    std::vector<WorldMapPoi> pois;
    std::vector<WorldMapSettlement> settlements;
    std::vector<WorldMapEventMarker> events;
    std::vector<WorldMapWorldEventMarker> worldEvents;
    WorldMapObjective objective;
    WorldMapRoute route;

    std::int64_t gameMinute = 0;
    WorldMapTravelConfig travel;
    WorldMapRouteCost routeCost;
    StrategicMapState strategic;

    int regionCount = 0;
    int discoveredRegionCount = 0;
    int poiCount = 0;
    int discoveredPoiCount = 0;
};

// Marks the region under the player and nearby POIs as discovered. Returns true
// when the persistent discovery state changes.
bool UpdateWorldMapDiscovery(const world::WorldGraph& graph,
                             const PoiConfig& poiConfig, float playerX,
                             float playerY, float poiRadius,
                             std::unordered_set<std::string>* regions,
                             std::unordered_set<std::string>* poiIds);

WorldMapState BuildWorldMapState(
    const world::WorldGraph& graph, const PoiConfig& poiConfig,
    float playerX, float playerY, bool playerValid,
    const std::unordered_set<std::string>& discoveredRegions,
    const std::unordered_set<std::string>& discoveredPois,
    const std::unordered_set<std::string>& completedPois,
    const RegionEventDirector* events, const WorldMapObjectiveInput& objective,
    const WorldMapRoute& route,
    const StrategicMapState* strategic = nullptr,
    const WorldMapTravelConfig& travel = {},
    const WorldEventDirector* worldEvents = nullptr);

WorldMapRoute BuildWorldMapRoute(const world::WorldGraph& graph,
                                 glm::vec2 start, glm::vec2 goal,
                                 const std::string& label, bool vehicleOnly);

WorldMapRouteCost EstimateWorldMapTravel(
    float roadDistance, const WorldMapTravelConfig& config);

const char* WorldMapRegionName(const std::string& id);
const char* WorldMapNodeName(const std::string& id);
const char* WorldMapBiomeName(const std::string& biome);
const char* WorldMapEventKindName(int kind);
const char* WorldMapEventStatusName(int status);

}  // namespace zl::game
