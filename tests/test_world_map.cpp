#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <unordered_set>

#include "game/Poi.h"
#include "game/StrategicMap.h"
#include "game/StrategicState.h"
#include "game/RegionEventDirector.h"
#include "game/WorldMap.h"
#include "world/RegionStreaming.h"
#include "world/WorldDefinition.h"
#include "world/WorldGraph.h"

using namespace zl;

namespace {

const std::string kM31Dir =
    std::string(ZL_SOURCE_DIR) + "/assets/maps/village_demo_m31";

game::PoiConfig TestPoiConfig() {
    game::PoiConfig config;
    config.enabled = true;
    config.defs = {
        {"clinic", "诊所", 2, 150.0f, 64.0f, {}, {}, {}},
        {"farmstead", "农庄", 1, 110.0f, 210.0f, {}, {}, {}},
        {"checkpoint", "检查站", 3, 300.0f, 280.0f, {}, {}, {}},
    };
    return config;
}

}  // namespace

TEST_CASE("M31-T7 世界地图：区域/道路/河流/POI/事件/路线") {
    world::WorldDefinition definition;
    std::string error;
    REQUIRE(world::LoadWorldDefinition(kM31Dir, &definition, &error) ==
            world::WorldDefinitionStatus::Loaded);
    const world::WorldGraph graph(definition);
    REQUIRE(graph.Valid());

    const game::PoiConfig pois = TestPoiConfig();
    std::unordered_set<std::string> regions;
    std::unordered_set<std::string> discoveredPois;
    CHECK(game::UpdateWorldMapDiscovery(graph, pois, 70.0f, 24.0f, 40.0f,
                                        &regions, &discoveredPois));
    CHECK(regions.count("village_core") == 1u);
    CHECK(discoveredPois.empty());

    CHECK(game::UpdateWorldMapDiscovery(graph, pois, 150.0f, 64.0f, 10.0f,
                                        &regions, &discoveredPois));
    CHECK(regions.count("north_fields") == 1u);
    CHECK(discoveredPois.count("clinic") == 1u);
    CHECK_FALSE(game::UpdateWorldMapDiscovery(graph, pois, 150.0f, 64.0f, 10.0f,
                                              &regions, &discoveredPois));

    game::RegionEventDirector events(definition);
    events.AdvanceDay(1);
    events.Update(1.0f, 70.0f, 24.0f,
                  [](const std::string&) { return world::RegionPhase::Active; },
                  [](const game::RegionEvent&) { return 0; });

    std::unordered_set<std::string> completed{"farmstead"};
    game::WorldMapObjectiveInput objective;
    objective.valid = true;
    objective.title = "找到诊所";
    objective.x = 150.0f;
    objective.y = 64.0f;
    const game::WorldMapRoute route = game::BuildWorldMapRoute(
        graph, {70.5f, 24.5f}, {300.0f, 280.0f}, "检查站", true);

    const game::WorldMapState state = game::BuildWorldMapState(
        graph, pois, 70.0f, 24.0f, true, regions, discoveredPois, completed,
        &events, objective, route);
    REQUIRE(state.valid);
    CHECK(state.mapId == "village_demo_m31");
    CHECK(state.width == doctest::Approx(360.0f));
    CHECK(state.height == doctest::Approx(360.0f));
    CHECK(state.regions.size() == 10u);
    CHECK(state.regionCount == 10);
    CHECK(state.discoveredRegionCount == 2);
    CHECK(state.roads.size() == 10u);
    REQUIRE(state.waters.size() == 1u);
    CHECK(state.waters[0].width == doctest::Approx(20.0f));
    CHECK(state.landmarks.size() == 11u);
    CHECK(state.pois.size() == 3u);
    CHECK(state.discoveredPoiCount == 1);
    CHECK(state.currentRegion == "village_core");
    CHECK(std::string(state.currentRegionName) == "村庄核心");
    CHECK(state.nearestLandmark == "village_haven");
    CHECK(state.objective.valid);
    CHECK(state.objective.title == "找到诊所");
    CHECK(state.route.valid);
    CHECK(state.route.points.size() >= 5u);
    CHECK(state.route.distance > 300.0f);
    CHECK_FALSE(state.events.empty());
    bool activeEvent = false;
    for (const game::WorldMapEventMarker& event : state.events) {
        activeEvent = activeEvent || event.active;
    }
    CHECK(activeEvent);
    for (const game::WorldMapRegion& region : state.regions) {
        if (region.id == "north_fields") {
            CHECK(region.discovered);
        }
        if (region.id == "deep_south") {
            CHECK_FALSE(region.discovered);
        }
    }
    for (const game::WorldMapPoi& poi : state.pois) {
        if (poi.id == "farmstead") {
            CHECK_FALSE(poi.discovered);
            CHECK(poi.completed);
        }
    }
}

TEST_CASE("M31-T7 世界地图：无效地图与路线失败降级") {
    world::WorldDefinition definition;
    std::string error;
    REQUIRE(world::LoadWorldDefinition(kM31Dir, &definition, &error) ==
            world::WorldDefinitionStatus::Loaded);
    definition.nodes.clear();
    const world::WorldGraph invalid(std::move(definition));
    CHECK_FALSE(invalid.Valid());
    const game::WorldMapRoute route =
        game::BuildWorldMapRoute(invalid, {0.0f, 0.0f}, {1.0f, 1.0f}, "x", true);
    CHECK_FALSE(route.valid);
    CHECK(route.points.empty());
    CHECK(std::string(game::WorldMapRegionName("deep_south")) == "南部密林");
    CHECK(std::string(game::WorldMapNodeName("bridge_west")) == "西桥头");
}

TEST_CASE("M32-T5 沦陷区域道路与 POI 在地图上不可用") {
    world::WorldDefinition definition;
    std::string error;
    REQUIRE(world::LoadWorldDefinition(kM31Dir, &definition, &error) ==
            world::WorldDefinitionStatus::Loaded);
    const world::WorldGraph graph(definition);
    REQUIRE(graph.Valid());
    game::StrategicConfig config;
    REQUIRE(game::LoadStrategicConfig(
        std::string(ZL_SOURCE_DIR) + "/assets/strategic.json", &config,
        &error));
    game::StrategicState strategicState;
    REQUIRE(strategicState.Initialize(config, &error));
    strategicState.FindRegion("village_core")->owner =
        game::StrategicOwner::Infested;
    const game::StrategicMapState strategic =
        game::BuildStrategicMapState(config, strategicState, 100);
    const game::PoiConfig pois = TestPoiConfig();
    std::unordered_set<std::string> regions{"village_core", "north_fields",
                                            "deep_south"};
    std::unordered_set<std::string> discovered{"clinic", "farmstead"};
    std::unordered_set<std::string> completed;
    const game::WorldMapState state = game::BuildWorldMapState(
        graph, pois, 70.0f, 24.0f, true, regions, discovered, completed,
        nullptr, {}, {}, &strategic);

    bool blockedRoad = false;
    for (const game::WorldMapRoad& road : state.roads) {
        blockedRoad = blockedRoad || !road.vehiclePassable;
    }
    CHECK(blockedRoad);
    for (const game::WorldMapPoi& poi : state.pois) {
        if (poi.id == "farmstead") {
            CHECK_FALSE(poi.disabled);
        }
        if (poi.id == "checkpoint") {
            CHECK(poi.disabled);
        }
    }
    const auto disabled = std::find_if(
        state.strategic.regions.begin(), state.strategic.regions.end(),
        [](const game::StrategicMapRegion& region) {
            return region.owner == game::StrategicOwner::Infested;
        });
    CHECK(disabled != state.strategic.regions.end());
}


TEST_CASE("M32-T9 争夺层：河谷大桥在争夺时保持通行，沦陷后封锁") {
    world::WorldDefinition definition;
    std::string error;
    REQUIRE(world::LoadWorldDefinition(kM31Dir, &definition, &error) ==
            world::WorldDefinitionStatus::Loaded);
    const world::WorldGraph graph(definition);
    REQUIRE(graph.Valid());
    game::StrategicConfig config;
    REQUIRE(game::LoadStrategicConfig(
        std::string(ZL_SOURCE_DIR) + "/assets/strategic.json", &config,
        &error));
    REQUIRE(config.Find("riverlands") != nullptr);

    game::StrategicState strategicState;
    REQUIRE(strategicState.Initialize(config, &error));
    const game::PoiConfig pois = TestPoiConfig();
    std::unordered_set<std::string> regions;
    std::unordered_set<std::string> discoveredPois;
    std::unordered_set<std::string> completed;
    const auto buildMap = [&](const game::StrategicMapState& strategic) {
        return game::BuildWorldMapState(
            graph, pois, 70.0f, 24.0f, true, regions, discoveredPois,
            completed, nullptr, {}, {}, &strategic);
    };

    game::StrategicMapState contested = game::BuildStrategicMapState(
        config, strategicState, 100);
    game::WorldMapState map = buildMap(contested);
    auto bridge = std::find_if(map.roads.begin(), map.roads.end(),
                               [](const game::WorldMapRoad& road) {
                                   return road.bridge;
                               });
    REQUIRE(bridge != map.roads.end());
    CHECK(bridge->vehiclePassable);

    strategicState.FindRegion("riverlands")->owner =
        game::StrategicOwner::Infested;
    game::StrategicMapState infested = game::BuildStrategicMapState(
        config, strategicState, 200);
    map = buildMap(infested);
    bridge = std::find_if(map.roads.begin(), map.roads.end(),
                            [](const game::WorldMapRoad& road) {
                                return road.bridge;
                            });
    REQUIRE(bridge != map.roads.end());
    CHECK_FALSE(bridge->vehiclePassable);
}
TEST_CASE("M33-F5-T5 世界地图 POI 阶段标记与完成度") {
    world::WorldDefinition definition;
    std::string error;
    REQUIRE(world::LoadWorldDefinition(kM31Dir, &definition, &error) ==
            world::WorldDefinitionStatus::Loaded);
    const world::WorldGraph graph(definition);
    REQUIRE(graph.Valid());
    game::PoiConfig pois = TestPoiConfig();
    pois.defs[0].stages.resize(3);
    for (int i = 0; i < 3; ++i) {
        pois.defs[0].stages[static_cast<std::size_t>(i)].id =
            "stage" + std::to_string(i);
        pois.defs[0].stages[static_cast<std::size_t>(i)].kind =
            game::PoiStageKind::Reach;
        pois.defs[0].stages[static_cast<std::size_t>(i)].x =
            150.0f + static_cast<float>(i);
        pois.defs[0].stages[static_cast<std::size_t>(i)].y = 64.0f;
    }
    std::unordered_set<std::string> regions{"north_fields"};
    std::unordered_set<std::string> discovered{"clinic"};
    std::unordered_set<std::string> completed;
    game::WorldMapPoiProgress progress;
    progress.id = "clinic";
    progress.stageIndex = 1;
    progress.stageCount = 3;
    progress.completedStageCount = 1;
    progress.stageLabel = "阶段 2/3 · 已完成 1/3";
    progress.hasStagePoint = true;
    progress.stageX = 151.0f;
    progress.stageY = 64.0f;
    std::vector<game::WorldMapPoiProgress> progressList{progress};
    const game::WorldMapState state = game::BuildWorldMapState(
        graph, pois, 70.0f, 24.0f, true, regions, discovered, completed,
        nullptr, {}, {}, nullptr, {}, nullptr, &progressList);
    const auto clinic = std::find_if(
        state.pois.begin(), state.pois.end(),
        [](const game::WorldMapPoi& poi) { return poi.id == "clinic"; });
    REQUIRE(clinic != state.pois.end());
    CHECK(clinic->discovered);
    CHECK(clinic->staged);
    CHECK(clinic->stageIndex == 1);
    CHECK(clinic->stageCount == 3);
    CHECK(clinic->completedStageCount == 1);
    CHECK(clinic->stageX == doctest::Approx(151.0f));
    CHECK(game::WorldMapPoiLabel(*clinic) ==
          "诊所（阶段 2/3 · 已完成 1/3）");

    progressList[0].failed = true;
    const game::WorldMapState failed = game::BuildWorldMapState(
        graph, pois, 70.0f, 24.0f, true, regions, discovered, completed,
        nullptr, {}, {}, nullptr, {}, nullptr, &progressList);
    const auto failedClinic = std::find_if(
        failed.pois.begin(), failed.pois.end(),
        [](const game::WorldMapPoi& poi) { return poi.id == "clinic"; });
    REQUIRE(failedClinic != failed.pois.end());
    CHECK(failedClinic->stageFailed);
    CHECK(game::WorldMapPoiLabel(*failedClinic) ==
          "诊所（阶段 2/3 · 失败可重试）");
}
