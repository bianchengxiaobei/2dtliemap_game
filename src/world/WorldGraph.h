#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "world/WorldDefinition.h"

namespace zl::world {

// M31-T5: 由 world.json 的 nodes/edges/regions 派生的道路、区域与密度图。
// 道路图只负责长距离路线；起点/终点到最近道路节点的接地由调用方交给现有
// 局部 A*（PathfindingService），避免把游戏寻路逻辑塞进地图数据层。
enum class TerrainKind : std::uint8_t {
    Grass = 0,
    Dirt = 1,
    Forest = 2,
    Paved = 3,
    Road = 4,
    Bridge = 5,
    Water = 6,
    Blocked = 7,
};

const char* TerrainKindName(TerrainKind kind);

struct TerrainInfo {
    TerrainKind kind = TerrainKind::Grass;
    bool walkable = true;
    bool vehiclePassable = true;
    bool onRoad = false;
    bool bridge = false;
    float speedScale = 1.0f;
    std::string regionId;
    std::string biome;
    float zombieDensity = 0.0f;
};

struct GraphNode {
    std::string id;
    std::string kind;
    glm::vec2 position{0.0f, 0.0f};
    bool isolated = false;
    int regionIndex = -1;
};

struct GraphEdge {
    int a = -1;
    int b = -1;
    std::string kind;
    float width = 0.0f;
    bool vehiclePassable = true;
    float length = 0.0f;
};

struct RoadRoute {
    enum class Outcome : std::uint8_t {
        Found = 0,
        NoPath = 1,
        Invalid = 2,
        VehicleRestricted = 3,
    };

    Outcome outcome = Outcome::Invalid;
    std::vector<int> nodeIndices;
    std::vector<glm::vec2> points;
    float roadDistance = 0.0f;
    float totalDistance = 0.0f;
    float startAccess = 0.0f;
    float goalAccess = 0.0f;
    bool bridge = false;
};

class WorldGraph {
public:
    explicit WorldGraph(WorldDefinition definition);

    bool Valid() const { return m_valid; }
    const WorldDefinition& Definition() const { return m_definition; }
    const std::vector<GraphNode>& Nodes() const { return m_nodes; }
    const std::vector<GraphEdge>& Edges() const { return m_edges; }

    const GraphNode* Node(int index) const;
    const GraphEdge* Edge(int index) const;
    const GraphNode* FindNode(std::string_view id) const;

    const WorldRegion* RegionAt(float x, float y) const;
    const WorldRegion* RegionForChunk(int chunkX, int chunkY) const;

    // Biome density from world.json spawn.biomes; missing biome falls back to
    // spawn.defaultZombieDensity. Outside all regions also uses the default.
    float ZombieDensityAt(float x, float y) const;
    float DensityScaleAt(float x, float y) const;

    // Nearest routable node (isolated nodes never participate in routes).
    int NearestNodeIndex(float x, float y) const;
    const GraphNode* NearestNode(float x, float y) const;

    // Point-to-road queries. extraMargin expands the road half-width; one
    // extra half-grid covers the tile-center convention used by chunk data.
    int NearestRoadEdge(float x, float y, float extraMargin = 0.0f) const;
    bool OnRoad(float x, float y, float extraMargin = 0.0f) const;
    bool OnBridge(float x, float y, float extraMargin = 0.0f) const;
    float RoadDistance(float x, float y) const;

    // Combine a world tile's floor/blocksMovement with the road graph.
    // floorSprite may be empty when only the graph is available.
    TerrainInfo ClassifyTerrain(float x, float y, bool blocked,
                                std::string_view floorSprite = {}) const;

    // Long-distance only: snap both endpoints to the nearest road node and
    // route over nodes/edges. vehicleOnly filters vehicle=false edges.
    RoadRoute PlanRoute(glm::vec2 start, glm::vec2 goal, bool vehicleOnly = false,
                        float maxAccessDistance = -1.0f) const;

private:
    struct RouteNodes {
        RoadRoute::Outcome outcome = RoadRoute::Outcome::Invalid;
        std::vector<int> nodes;
        float distance = 0.0f;
        bool bridge = false;
    };

    RouteNodes RouteNodesBetween(int start, int goal, bool vehicleOnly) const;

    WorldDefinition m_definition;
    std::vector<GraphNode> m_nodes;
    std::vector<GraphEdge> m_edges;
    std::vector<std::vector<int>> m_adjacency;
    std::unordered_map<std::string, int> m_nodeIndex;
    bool m_valid = false;
};

using RoadGraph = WorldGraph;

}  // namespace zl::world
