#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace zl::world {

inline constexpr const char* kWorldFormat = "zomboidlike-world-v1";

struct WorldBounds {
    int chunkX0 = 0;
    int chunkY0 = 0;
    int chunksX = 0;
    int chunksY = 0;
};

struct WorldRect {
    int chunkX = 0;
    int chunkY = 0;
    int chunksW = 0;
    int chunksH = 0;
};

struct WorldRegion {
    std::string id;
    WorldRect rect;
    std::string biome;
    std::vector<std::string> poiIds;
};

struct WorldNode {
    std::string id;
    std::string kind;
    double x = 0.0;
    double y = 0.0;
    bool isolated = false;
};

struct WorldEdge {
    std::string a;
    std::string b;
    std::string kind;
    double width = 0.0;
    bool vehiclePassable = true;
};

struct WorldPoi {
    std::string id;
    std::string templateId;
    double x = 0.0;
    double y = 0.0;
    double radius = 0.0;
    int threat = 0;
    std::string lootProfile;
};

struct WorldSpawn {
    double defaultZombieDensity = 0.0;
    std::unordered_map<std::string, double> biomes;
};

struct WorldDefinition {
    std::string format;
    std::string mapId;
    std::int64_t seed = 0;
    std::uint32_t generatorVersion = 0;
    WorldBounds bounds;
    std::vector<WorldRegion> regions;
    std::vector<WorldNode> nodes;
    std::vector<WorldEdge> edges;
    std::vector<WorldPoi> pois;
    WorldSpawn spawn;
    std::uint64_t mapHash = 0;
};

struct WorldIdentity {
    std::string mapId;
    std::int64_t seed = 0;
    std::uint32_t generatorVersion = 0;
    std::uint64_t mapHash = 0;
};

enum class WorldDefinitionStatus {
    Loaded,
    Missing,
    Invalid,
};

enum class WorldSaveCompatibility {
    Compatible,
    LegacyNoManifest,
    Mismatch,
};

WorldDefinitionStatus LoadWorldDefinition(const std::string& mapDir, WorldDefinition* out,
                                          std::string* error);

bool ComputeMapHash(const std::string& mapDir, std::uint64_t* hash, std::string* error);

std::string FormatMapHash(std::uint64_t hash);

WorldIdentity MakeWorldIdentity(const WorldDefinition& definition);

WorldSaveCompatibility CheckWorldSaveCompatibility(const WorldDefinition& definition,
                                                   const WorldIdentity* saved,
                                                   std::string* error);

}  // namespace zl::world
