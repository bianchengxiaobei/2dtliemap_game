#include "world/WorldGraph.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

#include "core/Constants.h"

namespace zl::world {
namespace {

constexpr float kInfinity = std::numeric_limits<float>::infinity();

float DistanceSq(glm::vec2 a, glm::vec2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

float Distance(glm::vec2 a, glm::vec2 b) {
    return std::sqrt(DistanceSq(a, b));
}

float DistanceSqToSegment(glm::vec2 point, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 ab = b - a;
    const float lengthSq = ab.x * ab.x + ab.y * ab.y;
    if (lengthSq <= 1.0e-8f) {
        return DistanceSq(point, a);
    }
    const glm::vec2 ap = point - a;
    const float t = std::clamp((ap.x * ab.x + ap.y * ab.y) / lengthSq, 0.0f, 1.0f);
    const glm::vec2 nearest = a + ab * t;
    return DistanceSq(point, nearest);
}

bool IsBridgeKind(std::string_view kind) {
    return kind == "bridge";
}

}  // namespace

const char* TerrainKindName(TerrainKind kind) {
    switch (kind) {
        case TerrainKind::Grass:
            return "grass";
        case TerrainKind::Dirt:
            return "dirt";
        case TerrainKind::Forest:
            return "forest";
        case TerrainKind::Paved:
            return "paved";
        case TerrainKind::Road:
            return "road";
        case TerrainKind::Bridge:
            return "bridge";
        case TerrainKind::Water:
            return "water";
        case TerrainKind::Blocked:
        default:
            return "blocked";
    }
}

WorldGraph::WorldGraph(WorldDefinition definition)
    : m_definition(std::move(definition)) {
    m_nodes.reserve(m_definition.nodes.size());
    for (const WorldNode& source : m_definition.nodes) {
        if (m_nodeIndex.contains(source.id)) {
            m_valid = false;
            return;
        }
        GraphNode node;
        node.id = source.id;
        node.kind = source.kind;
        node.position = glm::vec2(static_cast<float>(source.x), static_cast<float>(source.y));
        node.isolated = source.isolated;
        for (std::size_t i = 0; i < m_definition.regions.size(); ++i) {
            const WorldRegion& region = m_definition.regions[i];
            const float x0 = static_cast<float>(region.rect.chunkX * core::kChunkSize);
            const float y0 = static_cast<float>(region.rect.chunkY * core::kChunkSize);
            const float x1 =
                static_cast<float>((region.rect.chunkX + region.rect.chunksW) *
                                   core::kChunkSize);
            const float y1 =
                static_cast<float>((region.rect.chunkY + region.rect.chunksH) *
                                   core::kChunkSize);
            if (node.position.x >= x0 && node.position.x < x1 &&
                node.position.y >= y0 && node.position.y < y1) {
                node.regionIndex = static_cast<int>(i);
                break;
            }
        }
        m_nodeIndex.emplace(node.id, static_cast<int>(m_nodes.size()));
        m_nodes.push_back(std::move(node));
    }

    m_adjacency.resize(m_nodes.size());
    m_edges.reserve(m_definition.edges.size());
    for (const WorldEdge& source : m_definition.edges) {
        const auto a = m_nodeIndex.find(source.a);
        const auto b = m_nodeIndex.find(source.b);
        if (a == m_nodeIndex.end() || b == m_nodeIndex.end() || a->second == b->second ||
            !(source.width > 0.0) || !std::isfinite(source.width)) {
            m_valid = false;
            return;
        }
        GraphEdge edge;
        edge.a = a->second;
        edge.b = b->second;
        edge.kind = source.kind;
        edge.width = static_cast<float>(source.width);
        edge.vehiclePassable = source.vehiclePassable;
        edge.length = Distance(m_nodes[static_cast<std::size_t>(edge.a)].position,
                               m_nodes[static_cast<std::size_t>(edge.b)].position);
        const int index = static_cast<int>(m_edges.size());
        m_adjacency[static_cast<std::size_t>(edge.a)].push_back(index);
        m_adjacency[static_cast<std::size_t>(edge.b)].push_back(index);
        m_edges.push_back(std::move(edge));
    }

    m_valid = !m_nodes.empty();
}

const GraphNode* WorldGraph::Node(int index) const {
    if (index < 0 || index >= static_cast<int>(m_nodes.size())) {
        return nullptr;
    }
    return &m_nodes[static_cast<std::size_t>(index)];
}

const GraphEdge* WorldGraph::Edge(int index) const {
    if (index < 0 || index >= static_cast<int>(m_edges.size())) {
        return nullptr;
    }
    return &m_edges[static_cast<std::size_t>(index)];
}

const GraphNode* WorldGraph::FindNode(std::string_view id) const {
    const auto it = m_nodeIndex.find(std::string(id));
    if (it == m_nodeIndex.end()) {
        return nullptr;
    }
    return Node(it->second);
}

const WorldRegion* WorldGraph::RegionAt(float x, float y) const {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return nullptr;
    }
    for (const WorldRegion& region : m_definition.regions) {
        const float x0 = static_cast<float>(region.rect.chunkX * core::kChunkSize);
        const float y0 = static_cast<float>(region.rect.chunkY * core::kChunkSize);
        const float x1 =
            static_cast<float>((region.rect.chunkX + region.rect.chunksW) *
                               core::kChunkSize);
        const float y1 =
            static_cast<float>((region.rect.chunkY + region.rect.chunksH) *
                               core::kChunkSize);
        if (x >= x0 && x < x1 && y >= y0 && y < y1) {
            return &region;
        }
    }
    return nullptr;
}

const WorldRegion* WorldGraph::RegionForChunk(int chunkX, int chunkY) const {
    for (const WorldRegion& region : m_definition.regions) {
        if (chunkX >= region.rect.chunkX &&
            chunkX < region.rect.chunkX + region.rect.chunksW &&
            chunkY >= region.rect.chunkY &&
            chunkY < region.rect.chunkY + region.rect.chunksH) {
            return &region;
        }
    }
    return nullptr;
}

float WorldGraph::ZombieDensityAt(float x, float y) const {
    const WorldRegion* region = RegionAt(x, y);
    std::string_view biome;
    if (region != nullptr) {
        biome = region->biome;
    }
    if (!biome.empty()) {
        const auto it = m_definition.spawn.biomes.find(std::string(biome));
        if (it != m_definition.spawn.biomes.end()) {
            return std::max(0.0f, static_cast<float>(it->second));
        }
    }
    return std::max(0.0f,
                    static_cast<float>(m_definition.spawn.defaultZombieDensity));
}

float WorldGraph::DensityScaleAt(float x, float y) const {
    const float baseline =
        static_cast<float>(m_definition.spawn.defaultZombieDensity);
    if (!(baseline > 0.0f)) {
        return 1.0f;
    }
    return std::clamp(ZombieDensityAt(x, y) / baseline, 0.0f, 4.0f);
}

int WorldGraph::NearestNodeIndex(float x, float y) const {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return -1;
    }
    const glm::vec2 point(x, y);
    int best = -1;
    float bestDistanceSq = kInfinity;
    for (std::size_t i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i].isolated) {
            continue;
        }
        const float distanceSq = DistanceSq(point, m_nodes[i].position);
        if (distanceSq < bestDistanceSq) {
            bestDistanceSq = distanceSq;
            best = static_cast<int>(i);
        }
    }
    return best;
}

const GraphNode* WorldGraph::NearestNode(float x, float y) const {
    return Node(NearestNodeIndex(x, y));
}

int WorldGraph::NearestRoadEdge(float x, float y, float extraMargin) const {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(extraMargin)) {
        return -1;
    }
    const glm::vec2 point(x, y);
    int best = -1;
    float bestDistanceSq = kInfinity;
    for (std::size_t i = 0; i < m_edges.size(); ++i) {
        const GraphEdge& edge = m_edges[i];
        const GraphNode& a = m_nodes[static_cast<std::size_t>(edge.a)];
        const GraphNode& b = m_nodes[static_cast<std::size_t>(edge.b)];
        const float halfWidth = edge.width * 0.5f + 0.5f +
                                std::max(0.0f, extraMargin);
        const float distanceSq = DistanceSqToSegment(point, a.position, b.position);
        if (distanceSq <= halfWidth * halfWidth && distanceSq < bestDistanceSq) {
            bestDistanceSq = distanceSq;
            best = static_cast<int>(i);
        }
    }
    return best;
}

bool WorldGraph::OnRoad(float x, float y, float extraMargin) const {
    return NearestRoadEdge(x, y, extraMargin) >= 0;
}

bool WorldGraph::OnBridge(float x, float y, float extraMargin) const {
    const int index = NearestRoadEdge(x, y, extraMargin);
    const GraphEdge* edge = Edge(index);
    return edge != nullptr && IsBridgeKind(edge->kind);
}

float WorldGraph::RoadDistance(float x, float y) const {
    const int index = NearestRoadEdge(x, y, 0.0f);
    if (index < 0) {
        return -1.0f;
    }
    const GraphEdge& edge = m_edges[static_cast<std::size_t>(index)];
    const GraphNode& a = m_nodes[static_cast<std::size_t>(edge.a)];
    const GraphNode& b = m_nodes[static_cast<std::size_t>(edge.b)];
    const float distance = std::sqrt(DistanceSqToSegment(
        glm::vec2(x, y), a.position, b.position));
    return std::max(0.0f, distance - (edge.width * 0.5f + 0.5f));
}

TerrainInfo WorldGraph::ClassifyTerrain(float x, float y, bool blocked,
                                        std::string_view floorSprite) const {
    TerrainInfo info;
    const WorldRegion* region = RegionAt(x, y);
    if (region != nullptr) {
        info.regionId = region->id;
        info.biome = region->biome;
    }
    info.zombieDensity = ZombieDensityAt(x, y);

    const bool water = floorSprite == "tile.floor.water";
    const int roadIndex = NearestRoadEdge(x, y, 0.0f);
    const GraphEdge* roadEdge = Edge(roadIndex);
    const bool onBridge = roadEdge != nullptr && IsBridgeKind(roadEdge->kind);
    info.onRoad = roadEdge != nullptr;
    info.bridge = onBridge;

    if (water) {
        info.kind = TerrainKind::Water;
        info.walkable = false;
        info.vehiclePassable = false;
        info.speedScale = 0.0f;
        return info;
    }

    if (blocked) {
        info.kind = TerrainKind::Blocked;
        info.walkable = false;
        info.vehiclePassable = false;
        info.speedScale = 0.0f;
        return info;
    }

    if (onBridge) {
        info.kind = TerrainKind::Bridge;
    } else if (roadEdge != nullptr) {
        info.kind = TerrainKind::Road;
    } else if (floorSprite == "tile.floor.dirt") {
        info.kind = TerrainKind::Dirt;
    } else if (floorSprite == "tile.floor.wood") {
        info.kind = TerrainKind::Paved;
    } else if (region != nullptr && region->biome == "forest") {
        info.kind = TerrainKind::Forest;
    } else {
        info.kind = TerrainKind::Grass;
    }

    info.walkable = true;
    info.vehiclePassable = roadEdge == nullptr || roadEdge->vehiclePassable;
    switch (info.kind) {
        case TerrainKind::Dirt:
            info.speedScale = 0.92f;
            break;
        case TerrainKind::Forest:
        case TerrainKind::Grass:
            info.speedScale = 0.62f;
            break;
        case TerrainKind::Road:
        case TerrainKind::Bridge:
        case TerrainKind::Paved:
        default:
            info.speedScale = 1.0f;
            break;
    }
    return info;
}

WorldGraph::RouteNodes WorldGraph::RouteNodesBetween(int start, int goal,
                                                      bool vehicleOnly) const {
    RouteNodes result;
    if (!m_valid || Node(start) == nullptr || Node(goal) == nullptr) {
        return result;
    }

    const std::size_t count = m_nodes.size();
    std::vector<float> distance(count, kInfinity);
    std::vector<int> previous(count, -1);
    std::vector<std::uint8_t> closed(count, 0u);
    using QueueItem = std::pair<float, int>;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> open;

    distance[static_cast<std::size_t>(start)] = 0.0f;
    open.emplace(0.0f, start);
    while (!open.empty()) {
        const auto [currentDistance, current] = open.top();
        open.pop();
        if (closed[static_cast<std::size_t>(current)] != 0u) {
            continue;
        }
        closed[static_cast<std::size_t>(current)] = 1u;
        if (current == goal) {
            break;
        }
        for (const int edgeIndex : m_adjacency[static_cast<std::size_t>(current)]) {
            const GraphEdge& edge = m_edges[static_cast<std::size_t>(edgeIndex)];
            if (vehicleOnly && !edge.vehiclePassable) {
                continue;
            }
            const int next = edge.a == current ? edge.b : edge.a;
            const float nextDistance = currentDistance + edge.length;
            if (nextDistance >= distance[static_cast<std::size_t>(next)]) {
                continue;
            }
            distance[static_cast<std::size_t>(next)] = nextDistance;
            previous[static_cast<std::size_t>(next)] = current;
            open.emplace(nextDistance, next);
        }
    }

    if (!std::isfinite(distance[static_cast<std::size_t>(goal)])) {
        if (vehicleOnly) {
            RouteNodes unrestricted = RouteNodesBetween(start, goal, false);
            if (unrestricted.outcome == RoadRoute::Outcome::Found) {
                result.outcome = RoadRoute::Outcome::VehicleRestricted;
                return result;
            }
            result.outcome = RoadRoute::Outcome::NoPath;
        } else {
            result.outcome = RoadRoute::Outcome::NoPath;
        }
        return result;
    }

    std::vector<int> reverse;
    for (int current = goal; current != -1; current = previous[static_cast<std::size_t>(current)]) {
        reverse.push_back(current);
        if (current == start) {
            break;
        }
    }
    if (reverse.empty() || reverse.back() != start) {
        result.outcome = RoadRoute::Outcome::Invalid;
        return result;
    }
    std::reverse(reverse.begin(), reverse.end());
    result.nodes = std::move(reverse);
    result.distance = distance[static_cast<std::size_t>(goal)];
    for (std::size_t i = 1; i < result.nodes.size(); ++i) {
        const int a = result.nodes[i - 1];
        const int b = result.nodes[i];
        for (const int edgeIndex : m_adjacency[static_cast<std::size_t>(a)]) {
            const GraphEdge& edge = m_edges[static_cast<std::size_t>(edgeIndex)];
            if ((edge.a == a && edge.b == b) || (edge.a == b && edge.b == a)) {
                result.bridge = result.bridge || IsBridgeKind(edge.kind);
                break;
            }
        }
    }
    result.outcome = RoadRoute::Outcome::Found;
    return result;
}

RoadRoute WorldGraph::PlanRoute(glm::vec2 start, glm::vec2 goal, bool vehicleOnly,
                                float maxAccessDistance) const {
    RoadRoute result;
    if (!m_valid || !std::isfinite(start.x) || !std::isfinite(start.y) ||
        !std::isfinite(goal.x) || !std::isfinite(goal.y)) {
        return result;
    }

    const int startNode = NearestNodeIndex(start.x, start.y);
    const int goalNode = NearestNodeIndex(goal.x, goal.y);
    if (startNode < 0 || goalNode < 0) {
        return result;
    }
    const float startAccess =
        Distance(start, m_nodes[static_cast<std::size_t>(startNode)].position);
    const float goalAccess =
        Distance(goal, m_nodes[static_cast<std::size_t>(goalNode)].position);
    if (maxAccessDistance >= 0.0f &&
        (startAccess > maxAccessDistance || goalAccess > maxAccessDistance)) {
        return result;
    }

    RouteNodes nodes = RouteNodesBetween(startNode, goalNode, vehicleOnly);
    result.outcome = nodes.outcome;
    if (nodes.outcome != RoadRoute::Outcome::Found) {
        return result;
    }

    result.nodeIndices = std::move(nodes.nodes);
    result.roadDistance = nodes.distance;
    result.totalDistance = startAccess + nodes.distance + goalAccess;
    result.startAccess = startAccess;
    result.goalAccess = goalAccess;
    result.bridge = nodes.bridge;

    result.points.push_back(start);
    for (const int index : result.nodeIndices) {
        const glm::vec2 point = m_nodes[static_cast<std::size_t>(index)].position;
        if (result.points.empty() || DistanceSq(result.points.back(), point) > 1.0e-6f) {
            result.points.push_back(point);
        }
    }
    if (result.points.empty() || DistanceSq(result.points.back(), goal) > 1.0e-6f) {
        result.points.push_back(goal);
    }
    return result;
}

}  // namespace zl::world
