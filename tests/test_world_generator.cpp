#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>
#include <string>

#include "core/Constants.h"
#include "world/ChunkData.h"
#include "world/MapLoader.h"
#include "world/WorldDefinition.h"

using namespace zl;

namespace {

const std::string kWorldDir = std::string(ZL_SOURCE_DIR) + "/assets/maps/village_demo_m31";

double Distance(double ax, double ay, double bx, double by) {
    return std::hypot(ax - bx, ay - by);
}

}  // namespace

TEST_CASE("M31-T2 世界契约：36×36、区域覆盖、道路图与 POI 模板") {
    world::MapLoader::Meta meta;
    std::string error;
    REQUIRE(world::MapLoader::LoadMeta(kWorldDir, &meta, &error));
    CHECK(meta.name == "village_demo_m31");
    CHECK(meta.chunksX == 36);
    CHECK(meta.chunksY == 36);

    world::WorldDefinition definition;
    REQUIRE(world::LoadWorldDefinition(kWorldDir, &definition, &error) ==
            world::WorldDefinitionStatus::Loaded);
    CHECK(error.empty());
    CHECK(definition.mapId == "village_demo_m31");
    CHECK(definition.seed == 20261109);
    CHECK(definition.generatorVersion == 1);
    CHECK(definition.bounds.chunksX == 36);
    CHECK(definition.bounds.chunksY == 36);
    CHECK(definition.regions.size() == 10u);
    CHECK(definition.nodes.size() == 11u);
    CHECK(definition.edges.size() == 10u);
    CHECK(definition.pois.size() == 3u);
    CHECK(definition.spawn.defaultZombieDensity == doctest::Approx(0.012));

    REQUIRE(definition.pois.size() == 3u);
    CHECK(definition.pois[0].id == "clinic");
    CHECK(definition.pois[0].templateId == "clinic_v1");
    CHECK(definition.pois[1].id == "farmstead");
    CHECK(definition.pois[1].templateId == "farmstead_v1");
    CHECK(definition.pois[2].id == "checkpoint");
    CHECK(definition.pois[2].templateId == "checkpoint_v1");
    for (const world::WorldPoi& poi : definition.pois) {
        const std::filesystem::path templatePath =
            std::filesystem::path(kWorldDir) / "templates" / (poi.templateId + ".json");
        CHECK(std::filesystem::exists(templatePath));
    }

    double minDistance = 1e9;
    for (std::size_t i = 0; i < definition.pois.size(); ++i) {
        for (std::size_t j = i + 1; j < definition.pois.size(); ++j) {
            minDistance = std::min(
                minDistance,
                Distance(definition.pois[i].x, definition.pois[i].y,
                         definition.pois[j].x, definition.pois[j].y));
        }
    }
    CHECK(minDistance >= 80.0);
    CHECK(definition.mapHash != 0u);
    CHECK(world::FormatMapHash(definition.mapHash) == "736948ac7902ed3e");
}

TEST_CASE("M31-T2 大世界加载：所有 chunk 可解析、边界闭合、模板房间存在") {
    world::MapLoader::Meta meta;
    std::string error;
    REQUIRE(world::MapLoader::LoadMeta(kWorldDir, &meta, &error));

    int filesLoaded = 0;
    int objectTotal = 0;
    int roomOccurrences = 0;
    bool allFloorsValid = true;
    bool borderWater = true;
    bool objectsInChunk = true;
    std::set<std::string> roomNames;
    for (int cy = 0; cy < meta.chunksY; ++cy) {
        for (int cx = 0; cx < meta.chunksX; ++cx) {
            world::ChunkData data;
            REQUIRE(world::MapLoader::LoadChunk(meta, cx, cy, &data, &error));
            if (data.fromFile) {
                ++filesLoaded;
            }
            objectTotal += static_cast<int>(data.objects.size());
            roomOccurrences += static_cast<int>(data.rooms.size());
            for (const world::ChunkObjectData& object : data.objects) {
                if (object.localX < 0 || object.localX >= core::kChunkSize ||
                    object.localY < 0 || object.localY >= core::kChunkSize) {
                    objectsInChunk = false;
                }
            }
            for (const world::RoomRectData& room : data.rooms) {
                roomNames.insert(room.name);
            }
            for (int ly = 0; ly < core::kChunkSize; ++ly) {
                for (int lx = 0; lx < core::kChunkSize; ++lx) {
                    const std::string& floor =
                        data.floors[static_cast<std::size_t>(ly) * core::kChunkSize + lx];
                    if (floor.empty()) {
                        allFloorsValid = false;
                    }
                    const int worldX = cx * core::kChunkSize + lx;
                    const int worldY = cy * core::kChunkSize + ly;
                    if ((worldX == 0 || worldX == 359 || worldY == 0 || worldY == 359) &&
                        floor != "tile.floor.water") {
                        borderWater = false;
                    }
                }
            }
        }
    }
    CHECK(filesLoaded == 1025);
    CHECK(objectTotal == 6381);
    CHECK(roomOccurrences > 0);
    CHECK(allFloorsValid);
    CHECK(borderWater);
    CHECK(objectsInChunk);
    CHECK(roomNames.count("poi_clinic") == 1u);
    CHECK(roomNames.count("poi_farmstead_house") == 1u);
    CHECK(roomNames.count("poi_farm_barn") == 1u);
    CHECK(roomNames.count("poi_checkpoint") == 1u);
    CHECK(roomNames.size() == 17u);
}
