#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "game/VehicleSpike.h"
#include "world/Chunk.h"
#include "world/ChunkData.h"
#include "world/MapLoader.h"
#include "world/WorldCoords.h"
#include "world/WorldDefinition.h"
#include "world/WorldGraph.h"

using namespace zl;

namespace {

const std::string kM31Dir =
    std::string(ZL_SOURCE_DIR) + "/assets/maps/village_demo_m31";

std::uint64_t PackChunk(int cx, int cy) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(cy));
}

struct BridgeFixture {
    world::MapLoader::Meta meta;
    std::unordered_map<std::uint64_t, std::unique_ptr<world::Chunk>> chunks;

    bool Load() {
        std::string error;
        if (!world::MapLoader::LoadMeta(kM31Dir, &meta, &error)) {
            return false;
        }
        const auto load = [&](int cx, int cy) {
            world::ChunkData data;
            if (!world::MapLoader::LoadChunk(meta, cx, cy, &data, &error)) {
                return false;
            }
            auto chunk = std::make_unique<world::Chunk>(cx, cy);
            world::ApplyChunkData(data, *chunk);
            chunks.emplace(PackChunk(cx, cy), std::move(chunk));
            return true;
        };
        for (int cy = 11; cy <= 12; ++cy) {
            for (int cx = 19; cx <= 21; ++cx) {
                if (!load(cx, cy)) {
                    return false;
                }
            }
        }
        return load(20, 10);
    }

    const world::Chunk* Find(int wx, int wy) const {
        const world::ChunkCoord coord = world::ChunkOf(wx, wy);
        const auto it = chunks.find(PackChunk(coord.cx, coord.cy));
        if (it == chunks.end()) {
            return nullptr;
        }
        return it->second.get();
    }

    bool Blocked(int wx, int wy, int z) const {
        const world::Chunk* chunk = Find(wx, wy);
        if (chunk == nullptr) {
            return true;
        }
        const world::SquareLocal local = world::SquareLocalOf(wx, wy);
        const world::GridSquare& square = chunk->Square(local.lx, local.ly, z);
        for (int layer = 0; layer < world::kSquareLayerCount; ++layer) {
            for (const world::SquareObject& object :
                 square.Layer(static_cast<world::SquareLayer>(layer))) {
                if (object.blocksMovement) {
                    return true;
                }
            }
        }
        return false;
    }

    std::string_view Floor(int wx, int wy, int z) const {
        const world::Chunk* chunk = Find(wx, wy);
        if (chunk == nullptr) {
            return {};
        }
        const world::SquareLocal local = world::SquareLocalOf(wx, wy);
        const world::SquareObject* floor = chunk->Square(local.lx, local.ly, z).Floor();
        return floor != nullptr ? std::string_view(floor->sprite) : std::string_view{};
    }
};

}  // namespace

TEST_CASE("M31-T5 WorldGraph：区域、道路与长距离路线") {
    world::WorldDefinition definition;
    std::string error;
    REQUIRE(world::LoadWorldDefinition(kM31Dir, &definition, &error) ==
            world::WorldDefinitionStatus::Loaded);
    const world::WorldGraph graph(definition);

    REQUIRE(graph.Valid());
    CHECK(graph.Nodes().size() == 11u);
    CHECK(graph.Edges().size() == 10u);
    REQUIRE(graph.FindNode("village_haven") != nullptr);
    REQUIRE(graph.FindNode("checkpoint") != nullptr);
    CHECK(graph.FindNode("missing") == nullptr);

    CHECK(graph.RegionAt(70.0f, 24.0f)->id == "village_core");
    CHECK(graph.RegionAt(39.0f, 24.0f)->id == "west_woods");
    CHECK(graph.RegionAt(40.0f, 24.0f)->id == "village_core");
    CHECK(graph.RegionAt(400.0f, 400.0f) == nullptr);
    CHECK(graph.RegionForChunk(20, 10)->id == "riverlands");

    CHECK(graph.ZombieDensityAt(70.0f, 24.0f) ==
          doctest::Approx(0.020f));
    CHECK(graph.ZombieDensityAt(10.0f, 10.0f) ==
          doctest::Approx(0.008f));
    CHECK(graph.ZombieDensityAt(205.0f, 100.0f) ==
          doctest::Approx(0.006f));
    CHECK(graph.ZombieDensityAt(300.0f, 280.0f) ==
          doctest::Approx(0.014f));
    CHECK(graph.ZombieDensityAt(400.0f, 400.0f) ==
          doctest::Approx(0.012f));
    CHECK(graph.DensityScaleAt(70.0f, 24.0f) >
          graph.DensityScaleAt(10.0f, 10.0f));

    const glm::vec2 village{72.5f, 26.5f};
    const glm::vec2 checkpoint{300.0f, 280.0f};
    const world::RoadRoute route = graph.PlanRoute(village, checkpoint, true);
    REQUIRE(route.outcome == world::RoadRoute::Outcome::Found);
    CHECK(route.bridge);
    CHECK(route.points.front() == village);
    CHECK(route.points.back() == checkpoint);
    CHECK(route.totalDistance > route.roadDistance);
    REQUIRE(route.nodeIndices.size() >= 5u);
    CHECK(graph.Node(route.nodeIndices.front())->id == "village_haven");
    CHECK(graph.Node(route.nodeIndices.back())->id == "checkpoint");
    bool containsBridgeWest = false;
    bool containsBridgeEast = false;
    for (const int index : route.nodeIndices) {
        containsBridgeWest = containsBridgeWest ||
                             graph.Node(index)->id == "bridge_west";
        containsBridgeEast = containsBridgeEast ||
                             graph.Node(index)->id == "bridge_east";
    }
    CHECK(containsBridgeWest);
    CHECK(containsBridgeEast);

    CHECK(graph.OnRoad(70.5f, 24.5f));
    CHECK_FALSE(graph.OnRoad(75.0f, 35.0f));
    CHECK(graph.OnBridge(205.0f, 120.0f));
    CHECK_FALSE(graph.OnBridge(205.0f, 100.0f));
    CHECK(graph.RoadDistance(205.0f, 120.0f) == doctest::Approx(0.0f));

    const world::TerrainInfo water =
        graph.ClassifyTerrain(205.0f, 100.0f, true, "tile.floor.water");
    CHECK(water.kind == world::TerrainKind::Water);
    CHECK_FALSE(water.walkable);
    CHECK_FALSE(water.vehiclePassable);
    CHECK(water.speedScale == doctest::Approx(0.0f));

    const world::TerrainInfo bridge =
        graph.ClassifyTerrain(205.0f, 120.0f, false, "tile.floor.wood");
    CHECK(bridge.kind == world::TerrainKind::Bridge);
    CHECK(bridge.bridge);
    CHECK(bridge.onRoad);
    CHECK(bridge.walkable);
    CHECK(bridge.vehiclePassable);

    const world::TerrainInfo road =
        graph.ClassifyTerrain(100.0f, 24.5f, false, "tile.floor.dirt");
    CHECK(road.kind == world::TerrainKind::Road);
    CHECK(road.onRoad);
    CHECK(road.vehiclePassable);

    const world::TerrainInfo forest =
        graph.ClassifyTerrain(10.0f, 10.0f, false, "tile.floor.grass");
    CHECK(forest.kind == world::TerrainKind::Forest);
    CHECK(forest.walkable);
    CHECK(forest.vehiclePassable);
    CHECK(forest.speedScale < 1.0f);

    const world::TerrainInfo blocked =
        graph.ClassifyTerrain(10.0f, 10.0f, true, "tile.floor.grass");
    CHECK(blocked.kind == world::TerrainKind::Blocked);
    CHECK_FALSE(blocked.walkable);
    CHECK_FALSE(blocked.vehiclePassable);
}

TEST_CASE("M31-T5 WorldGraph：载具限制路段与桥路线") {
    world::WorldDefinition definition;
    std::string error;
    REQUIRE(world::LoadWorldDefinition(kM31Dir, &definition, &error) ==
            world::WorldDefinitionStatus::Loaded);

    for (world::WorldEdge& edge : definition.edges) {
        if (edge.kind == "bridge") {
            edge.vehiclePassable = false;
        }
    }
    const world::WorldGraph restricted(definition);
    REQUIRE(restricted.Valid());
    const world::RoadRoute foot =
        restricted.PlanRoute({70.5f, 24.5f}, {300.0f, 280.0f}, false);
    const world::RoadRoute vehicle =
        restricted.PlanRoute({70.5f, 24.5f}, {300.0f, 280.0f}, true);
    CHECK(foot.outcome == world::RoadRoute::Outcome::Found);
    CHECK(foot.bridge);
    CHECK(vehicle.outcome == world::RoadRoute::Outcome::VehicleRestricted);

    const world::TerrainInfo bridge =
        restricted.ClassifyTerrain(205.0f, 120.0f, false, "tile.floor.wood");
    CHECK(bridge.kind == world::TerrainKind::Bridge);
    CHECK_FALSE(bridge.vehiclePassable);
}

TEST_CASE("M31-T5 可行走地形：水面阻挡、桥面可通行、载具跨桥") {
    BridgeFixture fixture;
    REQUIRE(fixture.Load());
    CHECK(fixture.Blocked(205, 100, 0));
    CHECK_FALSE(fixture.Blocked(205, 120, 0));
    CHECK(std::string(fixture.Floor(205, 100, 0)) == "tile.floor.water");
    CHECK(std::string(fixture.Floor(205, 120, 0)) == "tile.floor.wood");
    CHECK_FALSE(fixture.Blocked(196, 120, 0));
    CHECK_FALSE(fixture.Blocked(197, 120, 0));
    CHECK_FALSE(fixture.Blocked(196, 121, 0));
    CHECK_FALSE(fixture.Blocked(196, 119, 0));
    CHECK_FALSE(fixture.Blocked(195, 120, 0));
    CHECK(std::string(fixture.Floor(196, 120, 0)) == "tile.floor.wood");

    world::WorldDefinition definition;
    std::string error;
    REQUIRE(world::LoadWorldDefinition(kM31Dir, &definition, &error) ==
            world::WorldDefinitionStatus::Loaded);
    const world::WorldGraph graph(definition);
    const world::TerrainInfo water = graph.ClassifyTerrain(
        205.0f, 100.0f, fixture.Blocked(205, 100, 0), fixture.Floor(205, 100, 0));
    CHECK(water.kind == world::TerrainKind::Water);
    CHECK_FALSE(water.vehiclePassable);

    game::VehicleMotion vehicle;
    vehicle.x = 196.5f;
    vehicle.y = 120.5f;
    vehicle.z = 0;
    vehicle.heading = 0.0f;
    vehicle.fuel = 1.0f;
    vehicle.condition = 1.0f;
    const auto blocked = [&fixture](int x, int y, int z) {
        return fixture.Blocked(x, y, z);
    };
    CHECK_FALSE(game::VehicleBlocked(196.63f, 120.5f, 0, blocked));
    for (int i = 0; i < 360; ++i) {
        game::VehicleStep(&vehicle, 1.0f, 0.0f, 1.0f / 60.0f, blocked,
                          game::kVehicleDefaultTuning, game::VehicleSurface::Paved);
    }
    CHECK(vehicle.x > 214.0f);
    CHECK(vehicle.y == doctest::Approx(120.5f).epsilon(0.1));
    CHECK(vehicle.condition > 0.0f);
    CHECK(vehicle.fuel < 1.0f);
}
