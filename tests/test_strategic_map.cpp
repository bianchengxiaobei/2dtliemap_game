#include <doctest/doctest.h>

#include <string>
#include <unordered_set>

#include "game/Poi.h"
#include "game/StrategicDirector.h"
#include "game/StrategicMap.h"
#include "game/WorldMap.h"
#include "world/WorldDefinition.h"
#include "world/WorldGraph.h"

using namespace zl;

namespace {

const std::string kStrategicFile =
    std::string(ZL_SOURCE_DIR) + "/assets/strategic.json";
const std::string kM31Dir =
    std::string(ZL_SOURCE_DIR) + "/assets/maps/village_demo_m31";

game::StrategicConfig LoadTestConfig() {
    game::StrategicConfig config;
    std::string error;
    REQUIRE(game::LoadStrategicConfig(kStrategicFile, &config, &error));
    return config;
}

game::StrategicState LoadTestState(const game::StrategicConfig& config) {
    game::StrategicState state;
    std::string error;
    REQUIRE(state.Initialize(config, &error));
    return state;
}

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

TEST_CASE("M32-T3 战略快照：控制区、资源与预警倒计时") {
    const game::StrategicConfig config = LoadTestConfig();
    game::StrategicState state = LoadTestState(config);
    auto* farm = state.FindRegion("south_farmland");
    REQUIRE(farm != nullptr);
    farm->pressure = 80.0f;
    farm->siegePhase = game::StrategicSiegePhase::Warning;
    farm->nextAttackMinute = 1000;
    farm->intelLevel = 1;

    std::string error;
    REQUIRE(state.ApplyEvent("south_farmland", game::StrategicEventKind::Resource,
                             "m32-t3-resource-1", 100, farm->owner,
                             game::StrategicResources{0, -3, 0, 0}, &error));
    REQUIRE(state.ApplyEvent("south_farmland", game::StrategicEventKind::Resource,
                             "m32-t3-resource-2", 200, farm->owner,
                             game::StrategicResources{0, -4, 0, 0}, &error));

    const game::StrategicMapState map =
        game::BuildStrategicMapState(config, state, 400, 1);
    REQUIRE(map.enabled);
    CHECK(map.regions.size() == 5u);
    CHECK(map.resources.people == config.initialResources.people);
    const game::StrategicMapRegion* region =
        map.FindRegion("south_farmland");
    REQUIRE(region != nullptr);
    CHECK(region->owner == game::StrategicOwner::Human);
    CHECK(region->pressure == doctest::Approx(80.0f));
    CHECK(region->defense == doctest::Approx(35.0f));
    CHECK(region->supply == doctest::Approx(45.0f));
    CHECK(region->population == 4);
    CHECK(region->strategicValue == 3);
    CHECK(region->warning.valid);
    CHECK(region->warning.warningMinutes == 48 * 60);
    CHECK(region->warning.remainingMinutes == 600);
    CHECK(region->warning.attackMinute == 1000);
    CHECK(region->warning.enemyStrength == 58);
    CHECK(region->warning.reportedEnemyStrength >= 43);
    CHECK(region->warning.reportedEnemyStrength <= 73);
    CHECK(region->warning.reportedAttackMinute >= 1000 - 360);
    CHECK(region->warning.reportedAttackMinute <= 1000 + 360);
    CHECK(map.FindRegion("missing") == nullptr);

    REQUIRE(map.events.size() == 1u);
    CHECK(map.events[0].id == "m32-t3-resource-2");
    CHECK(map.events[0].delta.supplies == -4);

    CHECK(std::string(game::StrategicOwnerLabel(
              game::StrategicOwner::Infested)) == "沦陷");
    CHECK(std::string(game::StrategicSiegePhaseLabel(
              game::StrategicSiegePhase::Warning)) == "预警");
    CHECK(std::string(game::StrategicEventKindName(
              game::StrategicEventKind::Siege)) == "围攻");
    CHECK(std::string(game::StrategicIntelName(1)) == "低");
    CHECK(game::StrategicIntelErrorPercent(1) == 25);
    CHECK(game::StrategicIntelTimeErrorMinutes(1) == 360);
}

TEST_CASE("M32-T3 预警重建与战略 tick 报告一致") {
    const game::StrategicConfig config = LoadTestConfig();
    game::StrategicState state = LoadTestState(config);
    auto* farm = state.FindRegion("south_farmland");
    REQUIRE(farm != nullptr);
    farm->pressure = 80.0f;
    farm->siegePhase = game::StrategicSiegePhase::None;
    farm->nextAttackMinute = -1;
    farm->intelLevel = 1;

    game::StrategicTickInput input;
    input.gameMinute = 30;
    game::StrategicTickReport report;
    std::string error;
    REQUIRE(game::AdvanceStrategic(state, config, input, &report, &error));
    REQUIRE(report.warnings.size() == 1u);
    const game::StrategicWarningInfo& tickWarning = report.warnings[0];

    game::StrategicWarningInfo view;
    REQUIRE(game::DescribeStrategicWarning(*farm, config, 30, &view));
    CHECK(view.regionId == tickWarning.regionId);
    CHECK(view.attackMinute == tickWarning.attackMinute);
    CHECK(view.warningMinutes == tickWarning.warningMinutes);
    CHECK(view.remainingMinutes == tickWarning.warningMinutes);
    CHECK(view.enemyStrength == tickWarning.enemyStrength);
    CHECK(view.reportedEnemyStrength == tickWarning.reportedEnemyStrength);
    CHECK(view.reportedAttackMinute == tickWarning.reportedAttackMinute);

    farm->siegePhase = game::StrategicSiegePhase::None;
    CHECK_FALSE(game::DescribeStrategicWarning(*farm, config, 30, &view));
    CHECK_FALSE(game::DescribeStrategicWarning(*farm, config, 30, nullptr));
}

TEST_CASE("M32-T3 路线步行/载具 ETA 与油耗") {
    game::WorldMapTravelConfig travel;
    travel.walkSpeed = 4.0f;
    travel.vehicleSpeed = 10.0f;
    travel.fuelPerSecond = 0.012f;
    travel.detourFactor = 1.2f;
    travel.gameHoursPerRealSecond = 0.4f;
    travel.vehicleAvailable = true;
    travel.currentFuel = 0.8f;

    const game::WorldMapRouteCost cost =
        game::EstimateWorldMapTravel(225.0f, travel);
    REQUIRE(cost.valid);
    CHECK(cost.roadDistance == doctest::Approx(225.0f));
    CHECK(cost.travelDistance == doctest::Approx(270.0f));
    CHECK(cost.walkSeconds == doctest::Approx(67.5f));
    CHECK(cost.vehicleSeconds == doctest::Approx(27.0f));
    CHECK(cost.walkGameHours == doctest::Approx(27.0f));
    CHECK(cost.vehicleGameHours == doctest::Approx(10.8f));
    CHECK(cost.vehicleFuel == doctest::Approx(0.324f));
    CHECK(cost.fuelEnough);

    travel.currentFuel = 0.2f;
    const game::WorldMapRouteCost lowFuel =
        game::EstimateWorldMapTravel(225.0f, travel);
    CHECK(lowFuel.valid);
    CHECK_FALSE(lowFuel.fuelEnough);

    travel.vehicleSpeed = 0.0f;
    CHECK_FALSE(game::EstimateWorldMapTravel(225.0f, travel).valid);
    CHECK_FALSE(game::EstimateWorldMapTravel(0.0f, travel).valid);
}

TEST_CASE("M32-T3 世界地图叠加与目标区域路线") {
    world::WorldDefinition definition;
    std::string error;
    REQUIRE(world::LoadWorldDefinition(kM31Dir, &definition, &error) ==
            world::WorldDefinitionStatus::Loaded);
    const world::WorldGraph graph(definition);
    REQUIRE(graph.Valid());
    const game::PoiConfig pois = TestPoiConfig();

    const game::WorldMapRoute route = game::BuildWorldMapRoute(
        graph, {70.5f, 24.5f}, {110.0f, 210.0f}, "农庄", true);
    REQUIRE(route.valid);
    CHECK(route.targetRegion == "south_farmland");

    const game::StrategicConfig config = LoadTestConfig();
    game::StrategicState strategicState = LoadTestState(config);
    auto* farm = strategicState.FindRegion("south_farmland");
    REQUIRE(farm != nullptr);
    farm->pressure = 80.0f;
    farm->siegePhase = game::StrategicSiegePhase::Warning;
    farm->nextAttackMinute = 1000;
    const game::StrategicMapState strategic =
        game::BuildStrategicMapState(config, strategicState, 400);

    game::WorldMapTravelConfig travel;
    travel.vehicleAvailable = true;
    travel.currentFuel = 1.0f;
    const game::WorldMapState state = game::BuildWorldMapState(
        graph, pois, 70.5f, 24.5f, true, {"village_core"},
        {"farmstead"}, {}, nullptr, {}, route, &strategic, travel);
    REQUIRE(state.valid);
    CHECK(state.strategic.enabled);
    CHECK(state.strategic.regions.size() == 5u);
    CHECK(state.gameMinute == 400);
    REQUIRE(state.routeCost.valid);
    CHECK(state.routeCost.roadDistance == doctest::Approx(route.distance));
    CHECK(state.routeCost.vehicleFuel > 0.0f);
    CHECK(state.routeCost.fuelEnough);
    REQUIRE(state.strategic.FindRegion("south_farmland") != nullptr);
    CHECK(state.strategic.FindRegion("south_farmland")->warning.valid);
}
