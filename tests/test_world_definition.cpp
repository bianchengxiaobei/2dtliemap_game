#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "world/ChunkData.h"
#include "world/MapLoader.h"
#include "world/WorldDefinition.h"

using namespace zl;

namespace {

const std::filesystem::path kTempRoot = std::filesystem::temp_directory_path();

std::string BaseMapJson() {
    return R"({"format":"zomboidlike-map-v1","name":"test_world","bounds":{"chunkX0":0,"chunkY0":0,"chunksX":4,"chunksY":4},"terrain":{"defaultFloor":"tile.floor.grass"},"legend":{"g":"tile.floor.grass"}})";
}

std::string ValidWorldJson() {
    return R"({
        "format":"zomboidlike-world-v1",
        "mapId":"test_world",
        "seed":20261109,
        "generatorVersion":1,
        "bounds":{"chunkX0":0,"chunkY0":0,"chunksX":4,"chunksY":4},
        "regions":[
            {"id":"core","rect":{"chunkX":0,"chunkY":0,"chunksW":2,"chunksH":2},"biome":"village","poiIds":["clinic"]},
            {"id":"wilds","rect":{"chunkX":2,"chunkY":0,"chunksW":2,"chunksH":2},"biome":"forest"}
        ],
        "nodes":[
            {"id":"haven","kind":"settlement","x":10.5,"y":10.5},
            {"id":"junction","kind":"junction","x":25.5,"y":10.5},
            {"id":"lookout","kind":"landmark","x":30.0,"y":30.0,"isolated":true}
        ],
        "edges":[{"a":"haven","b":"junction","kind":"road","width":3.0}],
        "pois":[{"id":"clinic","template":"clinic_v1","x":12.0,"y":12.0,"radius":2.0,"threat":2,"lootProfile":"poi_clinic"}],
        "spawn":{"defaultZombieDensity":0.012,"biomes":{"forest":0.008,"village":0.02}}
    })";
}

std::string RunTag() {
    static const std::string tag = std::to_string(static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    return tag;
}

class TempWorld {
public:
    explicit TempWorld(const std::string& name)
        : root(kTempRoot / ("zl_world_definition_" + name + "_" + RunTag() + "_" +
                            std::to_string(s_sequence.fetch_add(1)))) {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root, ec);
    }

    ~TempWorld() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    void Write(const std::string& relative, const std::string& text) const {
        const std::filesystem::path path = root / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << text;
    }

    void WriteMap(const std::string& json = BaseMapJson()) const { Write("map.json", json); }

    void WriteTemplate(const std::string& id = "clinic_v1") const {
        Write("templates/" + id + ".json", R"({"format":"zomboidlike-poi-template-v1"})");
    }

    void WriteWorld(const std::string& json) const { Write("world.json", json); }

    void WriteWorld(const nlohmann::json& json) const { WriteWorld(json.dump(1)); }

    void WriteValidFixture() const {
        WriteMap();
        WriteTemplate();
        WriteWorld(ValidWorldJson());
    }

    std::string Path() const { return root.string(); }

private:
    inline static std::atomic<std::uint64_t> s_sequence{0};
    std::filesystem::path root;
};

}  // namespace

TEST_CASE("M31-T1 world.json：合法契约加载、模板/区域/道路图校验与身份") {
    TempWorld temp("valid");
    temp.WriteMap();
    temp.WriteTemplate();
    nlohmann::json world = nlohmann::json::parse(ValidWorldJson());
    world["future"] = true;
    world["regions"][0]["future"] = 1;
    temp.WriteWorld(world);

    world::WorldDefinition definition;
    std::string error = "sentinel";
    const world::WorldDefinitionStatus status =
        world::LoadWorldDefinition(temp.Path(), &definition, &error);
    REQUIRE(status == world::WorldDefinitionStatus::Loaded);
    CHECK(error.empty());
    CHECK(definition.format == "zomboidlike-world-v1");
    CHECK(definition.mapId == "test_world");
    CHECK(definition.seed == 20261109);
    CHECK(definition.generatorVersion == 1);
    CHECK(definition.bounds.chunkX0 == 0);
    CHECK(definition.bounds.chunksX == 4);
    REQUIRE(definition.regions.size() == 2u);
    CHECK(definition.regions[0].id == "core");
    CHECK(definition.regions[0].rect.chunksW == 2);
    CHECK(definition.regions[0].poiIds == std::vector<std::string>{"clinic"});
    CHECK(definition.regions[1].id == "wilds");
    REQUIRE(definition.nodes.size() == 3u);
    CHECK(definition.nodes[2].isolated);
    REQUIRE(definition.edges.size() == 1u);
    CHECK(definition.edges[0].vehiclePassable);
    REQUIRE(definition.pois.size() == 1u);
    CHECK(definition.pois[0].templateId == "clinic_v1");
    CHECK(definition.pois[0].lootProfile == "poi_clinic");
    CHECK(definition.spawn.defaultZombieDensity == doctest::Approx(0.012));
    CHECK(definition.spawn.biomes.at("village") == doctest::Approx(0.02));

    std::uint64_t mapHash = 0;
    REQUIRE(world::ComputeMapHash(temp.Path(), &mapHash, &error));
    CHECK(error.empty());
    CHECK(mapHash != 0u);
    CHECK(definition.mapHash == mapHash);
    const std::string hashText = world::FormatMapHash(definition.mapHash);
    CHECK(hashText.size() == 16u);
    CHECK(hashText.find_first_not_of("0123456789abcdef") == std::string::npos);

    const world::WorldIdentity identity = world::MakeWorldIdentity(definition);
    CHECK(identity.mapId == definition.mapId);
    CHECK(identity.mapHash == definition.mapHash);
    CHECK(world::CheckWorldSaveCompatibility(definition, &identity, &error) ==
          world::WorldSaveCompatibility::Compatible);
    CHECK(error.empty());
    CHECK(world::CheckWorldSaveCompatibility(definition, nullptr, &error) ==
          world::WorldSaveCompatibility::LegacyNoManifest);
    CHECK(error.empty());
}

TEST_CASE("M31-T1 world.json：无 sidecar 的旧地图保持 Missing 且 MapLoader 零回归") {
    TempWorld temp("legacy");
    temp.WriteMap();

    world::WorldDefinition definition;
    std::string error = "sentinel";
    CHECK(world::LoadWorldDefinition(temp.Path(), &definition, &error) ==
          world::WorldDefinitionStatus::Missing);
    CHECK(error.empty());

    world::MapLoader::Meta meta;
    REQUIRE(world::MapLoader::LoadMeta(temp.Path(), &meta, &error));
    CHECK(meta.chunksX == 4);
    CHECK(meta.chunksY == 4);
    world::ChunkData chunk;
    REQUIRE(world::MapLoader::LoadChunk(meta, 1, 1, &chunk, &error));
    CHECK_FALSE(chunk.fromFile);
    CHECK(chunk.floors[0] == "tile.floor.grass");
}

TEST_CASE("M31-T1 world.json：缺字段、未知格式、坏 JSON、bounds 不一致均明确失败") {
    SUBCASE("缺 mapId") {
        TempWorld temp("missing_map_id");
        temp.WriteMap();
        nlohmann::json world = nlohmann::json::parse(ValidWorldJson());
        world.erase("mapId");
        temp.WriteWorld(world);
        world::WorldDefinition definition;
        std::string error;
        CHECK(world::LoadWorldDefinition(temp.Path(), &definition, &error) ==
              world::WorldDefinitionStatus::Invalid);
        CHECK(error.find("mapId") != std::string::npos);
    }

    SUBCASE("未知格式") {
        TempWorld temp("unknown_format");
        temp.WriteMap();
        nlohmann::json world = nlohmann::json::parse(ValidWorldJson());
        world["format"] = "zomboidlike-world-v2";
        temp.WriteWorld(world);
        world::WorldDefinition definition;
        std::string error;
        CHECK(world::LoadWorldDefinition(temp.Path(), &definition, &error) ==
              world::WorldDefinitionStatus::Invalid);
        CHECK(error.find("格式不受支持") != std::string::npos);
    }

    SUBCASE("坏 JSON") {
        TempWorld temp("bad_json");
        temp.WriteMap();
        temp.WriteWorld(std::string("{ not valid json"));
        world::WorldDefinition definition;
        std::string error;
        CHECK(world::LoadWorldDefinition(temp.Path(), &definition, &error) ==
              world::WorldDefinitionStatus::Invalid);
        CHECK(error.find("JSON 解析失败") != std::string::npos);
    }

    SUBCASE("bounds 与 map.json 不一致") {
        TempWorld temp("bounds_mismatch");
        temp.WriteMap();
        nlohmann::json world = nlohmann::json::parse(ValidWorldJson());
        world["bounds"]["chunksX"] = 5;
        temp.WriteWorld(world);
        world::WorldDefinition definition;
        std::string error;
        CHECK(world::LoadWorldDefinition(temp.Path(), &definition, &error) ==
              world::WorldDefinitionStatus::Invalid);
        CHECK(error.find("bounds 与 map.json 不一致") != std::string::npos);
    }
}

TEST_CASE("M31-T1 world.json：区域重叠、区域越界、POI 模板/越界/重复失败") {
    SUBCASE("区域重叠") {
        TempWorld temp("region_overlap");
        temp.WriteMap();
        nlohmann::json world = nlohmann::json::parse(ValidWorldJson());
        world["regions"][1]["rect"]["chunkX"] = 1;
        temp.WriteWorld(world);
        world::WorldDefinition definition;
        std::string error;
        CHECK(world::LoadWorldDefinition(temp.Path(), &definition, &error) ==
              world::WorldDefinitionStatus::Invalid);
        CHECK(error.find("重叠") != std::string::npos);
    }

    SUBCASE("区域越界") {
        TempWorld temp("region_bounds");
        temp.WriteMap();
        nlohmann::json world = nlohmann::json::parse(ValidWorldJson());
        world["regions"][1]["rect"]["chunkX"] = 3;
        world["regions"][1]["rect"]["chunksW"] = 2;
        temp.WriteWorld(world);
        world::WorldDefinition definition;
        std::string error;
        CHECK(world::LoadWorldDefinition(temp.Path(), &definition, &error) ==
              world::WorldDefinitionStatus::Invalid);
        CHECK(error.find("越界") != std::string::npos);
    }

    SUBCASE("模板不存在") {
        TempWorld temp("missing_template");
        temp.WriteMap();
        nlohmann::json world = nlohmann::json::parse(ValidWorldJson());
        world["pois"][0]["template"] = "missing_v1";
        temp.WriteWorld(world);
        world::WorldDefinition definition;
        std::string error;
        CHECK(world::LoadWorldDefinition(temp.Path(), &definition, &error) ==
              world::WorldDefinitionStatus::Invalid);
        CHECK(error.find("模板不存在") != std::string::npos);
    }

    SUBCASE("POI 越界") {
        TempWorld temp("poi_bounds");
        temp.WriteMap();
        temp.WriteTemplate();
        nlohmann::json world = nlohmann::json::parse(ValidWorldJson());
        world["pois"][0]["x"] = 39.0;
        temp.WriteWorld(world);
        world::WorldDefinition definition;
        std::string error;
        CHECK(world::LoadWorldDefinition(temp.Path(), &definition, &error) ==
              world::WorldDefinitionStatus::Invalid);
        CHECK(error.find("POI 越界") != std::string::npos);
    }

    SUBCASE("POI id 重复") {
        TempWorld temp("poi_duplicate");
        temp.WriteMap();
        temp.WriteTemplate();
        nlohmann::json world = nlohmann::json::parse(ValidWorldJson());
        const nlohmann::json duplicate = world["pois"][0];
        world["pois"].push_back(duplicate);
        temp.WriteWorld(world);
        world::WorldDefinition definition;
        std::string error;
        CHECK(world::LoadWorldDefinition(temp.Path(), &definition, &error) ==
              world::WorldDefinitionStatus::Invalid);
        CHECK(error.find("id 重复") != std::string::npos);
    }
}

TEST_CASE("M31-T1 world.json：道路图断头/缺节点/自环失败，isolated 节点可保留") {
    SUBCASE("断头路") {
        TempWorld temp("graph_disconnected");
        temp.WriteMap();
        nlohmann::json world = nlohmann::json::parse(ValidWorldJson());
        world["edges"] = nlohmann::json::array();
        temp.WriteWorld(world);
        world::WorldDefinition definition;
        std::string error;
        CHECK(world::LoadWorldDefinition(temp.Path(), &definition, &error) ==
              world::WorldDefinitionStatus::Invalid);
        CHECK(error.find("不连通") != std::string::npos);
    }

    SUBCASE("缺节点") {
        TempWorld temp("graph_missing_node");
        temp.WriteMap();
        nlohmann::json world = nlohmann::json::parse(ValidWorldJson());
        world["edges"][0]["b"] = "missing_node";
        temp.WriteWorld(world);
        world::WorldDefinition definition;
        std::string error;
        CHECK(world::LoadWorldDefinition(temp.Path(), &definition, &error) ==
              world::WorldDefinitionStatus::Invalid);
        CHECK(error.find("不存在的节点") != std::string::npos);
    }

    SUBCASE("自环") {
        TempWorld temp("graph_self_loop");
        temp.WriteMap();
        nlohmann::json world = nlohmann::json::parse(ValidWorldJson());
        world["edges"][0]["b"] = "haven";
        temp.WriteWorld(world);
        world::WorldDefinition definition;
        std::string error;
        CHECK(world::LoadWorldDefinition(temp.Path(), &definition, &error) ==
              world::WorldDefinitionStatus::Invalid);
        CHECK(error.find("自环") != std::string::npos);
    }
}

TEST_CASE("M31-T1 world.json：mapHash 对地图内容敏感，身份不匹配会给出停止文案") {
    TempWorld temp("hash_identity");
    temp.WriteValidFixture();

    world::WorldDefinition definition;
    std::string error;
    REQUIRE(world::LoadWorldDefinition(temp.Path(), &definition, &error) ==
            world::WorldDefinitionStatus::Loaded);
    const world::WorldIdentity oldIdentity = world::MakeWorldIdentity(definition);

    temp.Write("templates/extra_v1.json", R"({"format":"zomboidlike-poi-template-v1"})");
    std::uint64_t changedHash = 0;
    REQUIRE(world::ComputeMapHash(temp.Path(), &changedHash, &error));
    CHECK(changedHash != definition.mapHash);

    world::WorldIdentity changedIdentity = oldIdentity;
    changedIdentity.mapHash = changedHash;
    CHECK(world::CheckWorldSaveCompatibility(definition, &changedIdentity, &error) ==
          world::WorldSaveCompatibility::Mismatch);
    CHECK(error.find("mapHash=") != std::string::npos);
    CHECK(error.find("已停止加载") != std::string::npos);

    changedIdentity = oldIdentity;
    changedIdentity.seed = 1;
    changedIdentity.generatorVersion = 2;
    changedIdentity.mapId = "other_world";
    CHECK(world::CheckWorldSaveCompatibility(definition, &changedIdentity, &error) ==
          world::WorldSaveCompatibility::Mismatch);
    CHECK(error.find("seed=") != std::string::npos);
    CHECK(error.find("generatorVersion=") != std::string::npos);
    CHECK(error.find("mapId=") != std::string::npos);
}
