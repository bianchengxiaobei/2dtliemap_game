#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "core/Constants.h"
#include "world/Chunk.h"
#include "world/ChunkData.h"
#include "world/MapLoader.h"

using namespace zl;

namespace {

const std::string kDemoDir = std::string(ZL_SOURCE_DIR) + "/assets/maps/village_demo";

void WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

}  // namespace

TEST_CASE("MapLoader::LoadMeta：village_demo 元数据（M3-T3）") {
    world::MapLoader::Meta meta;
    std::string error;
    REQUIRE(world::MapLoader::LoadMeta(kDemoDir, &meta, &error));
    CHECK(error.empty());
    CHECK(meta.name == "village_demo");
    CHECK(meta.defaultFloor == "tile.floor.grass");
    CHECK(meta.chunkX0 == 0);
    CHECK(meta.chunkY0 == 0);
    CHECK(meta.chunksX == 12);
    CHECK(meta.chunksY == 12);
    CHECK(meta.legend.size() == 3u);
    CHECK(meta.legend.at('g') == "tile.floor.grass");
    CHECK(meta.legend.at('d') == "tile.floor.dirt");
    CHECK(meta.legend.at('w') == "tile.floor.wood");

    CHECK(world::MapLoader::InBounds(meta, 0, 0));
    CHECK(world::MapLoader::InBounds(meta, 11, 11));
    CHECK_FALSE(world::MapLoader::InBounds(meta, 12, 0));
    CHECK_FALSE(world::MapLoader::InBounds(meta, 0, 12));
    CHECK_FALSE(world::MapLoader::InBounds(meta, -1, 0));
}

TEST_CASE("MapLoader：稀疏 chunk 文件缺失 = 默认地形数据") {
    world::MapLoader::Meta meta;
    std::string error;
    REQUIRE(world::MapLoader::LoadMeta(kDemoDir, &meta, &error));

    // chunk_0_1 在生成的 demo 地图里是纯默认内容（不落盘）
    world::ChunkData data;
    REQUIRE(world::MapLoader::LoadChunk(meta, 0, 1, &data, &error));
    CHECK_FALSE(data.fromFile);
    CHECK(data.chunkX == 0);
    CHECK(data.chunkY == 1);
    for (const auto& floor : data.floors) {
        CHECK(floor == "tile.floor.grass");
    }
    CHECK(data.objects.empty());
    CHECK(data.rooms.empty());
}

TEST_CASE("MapLoader：解析真实 chunk 文件（chunk_6_1 含房子）") {
    world::MapLoader::Meta meta;
    std::string error;
    REQUIRE(world::MapLoader::LoadMeta(kDemoDir, &meta, &error));

    world::ChunkData data;
    REQUIRE(world::MapLoader::LoadChunk(meta, 6, 1, &data, &error));
    CHECK(data.fromFile);

    // 木地板：房子内部 (5,8)（世界坐标 65,18）
    CHECK(data.floors[8 * core::kChunkSize + 5] == "tile.floor.wood");
    // 草地：房外 (0,0)
    CHECK(data.floors[0] == "tile.floor.grass");

    // 房间矩形（已按本 chunk 裁剪：h=3 跨到下一 chunk → 本文件 h=2）
    REQUIRE(data.rooms.size() == 1u);
    CHECK(data.rooms[0].localX == 5);
    CHECK(data.rooms[0].localY == 8);
    CHECK(data.rooms[0].w == 3);
    CHECK(data.rooms[0].h == 2);
    CHECK(data.rooms[0].id == 3);
    CHECK(data.rooms[0].name == "house_2");

    // 对象：9 面砖墙（房子西/北/东边）+ 1 处草丛
    CHECK(data.objects.size() == 10u);
    int wallCount = 0;
    bool foundNW = false;
    for (const auto& object : data.objects) {
        if (object.layer == world::SquareLayer::Wall) {
            ++wallCount;
            CHECK(object.sprite == "wall.brick");
            if (object.localX == 4 && object.localY == 7) {
                foundNW = true;
            }
        }
    }
    CHECK(wallCount == 9);
    CHECK(foundNW);
}

TEST_CASE("MapLoader：ChunkFilePath 跨 cell/负数坐标目录（village_demo 全在 cell 0 测不到）") {
    world::MapLoader::Meta meta;
    std::string error;
    REQUIRE(world::MapLoader::LoadMeta(kDemoDir, &meta, &error));

    const auto EndsWith = [](const std::string& path, const std::filesystem::path& suffix) {
        const std::string tail = suffix.string();
        return path.size() >= tail.size() &&
               path.compare(path.size() - tail.size(), tail.size(), tail) == 0;
    };

    CHECK(EndsWith(world::MapLoader::ChunkFilePath(meta, 7, 1),
                   std::filesystem::path("chunks") / "c0_0" / "chunk_7_1.json"));
    // 跨 cell：chunk (52,50) → cell (1,1)（曾经的 bug：误写成 c30_30）
    CHECK(EndsWith(world::MapLoader::ChunkFilePath(meta, 52, 50),
                   std::filesystem::path("chunks") / "c1_1" / "chunk_52_50.json"));
    CHECK(EndsWith(world::MapLoader::ChunkFilePath(meta, 30, 0),
                   std::filesystem::path("chunks") / "c1_0" / "chunk_30_0.json"));
    // 负数：chunk (-1,-1) → cell (-1,-1)
    CHECK(EndsWith(world::MapLoader::ChunkFilePath(meta, -1, -1),
                   std::filesystem::path("chunks") / "c-1_-1" / "chunk_-1_-1.json"));
    CHECK(EndsWith(world::MapLoader::ChunkFilePath(meta, -31, -30),
                   std::filesystem::path("chunks") / "c-2_-1" / "chunk_-31_-30.json"));
}

TEST_CASE("MapLoader：cell 非 0 的 chunk 文件真实加载（回归：目录解析）") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zl_maploader_cell_test";
    std::filesystem::remove_all(dir);
    WriteTextFile(
        dir / "map.json",
        R"({"format":"zomboidlike-map-v1","name":"t","bounds":{"chunkX0":-100,"chunkY0":-100,"chunksX":500,"chunksY":500},"terrain":{"defaultFloor":"tile.floor.grass"},"legend":{"g":"tile.floor.grass"}})");
    // chunk (52,50) 放在 c1_1；chunk (-1,-2) 放在 c-1_-1
    WriteTextFile(dir / "chunks" / "c1_1" / "chunk_52_50.json",
                  R"({"cx":52,"cy":50,"objects":[{"x":8,"y":0,"layer":"furniture","sprite":"object.tree"}]})");
    WriteTextFile(dir / "chunks" / "c-1_-1" / "chunk_-1_-2.json",
                  R"({"cx":-1,"cy":-2,"objects":[{"x":0,"y":0,"layer":"wall","sprite":"wall.brick"}]})");

    world::MapLoader::Meta meta;
    std::string error;
    REQUIRE(world::MapLoader::LoadMeta(dir.string(), &meta, &error));

    world::ChunkData data;
    REQUIRE(world::MapLoader::LoadChunk(meta, 52, 50, &data, &error));
    CHECK(data.fromFile);
    REQUIRE(data.objects.size() == 1u);
    CHECK(data.objects[0].sprite == "object.tree");

    error.clear();
    REQUIRE(world::MapLoader::LoadChunk(meta, -1, -2, &data, &error));
    CHECK(data.fromFile);
    REQUIRE(data.objects.size() == 1u);
    CHECK(data.objects[0].sprite == "wall.brick");

    std::filesystem::remove_all(dir);
}

TEST_CASE("MapLoader：z=1 楼上数据往返（chunk_7_2 两层小楼，M3-T6）") {
    world::MapLoader::Meta meta;
    std::string error;
    REQUIRE(world::MapLoader::LoadMeta(kDemoDir, &meta, &error));

    world::ChunkData data;
    REQUIRE(world::MapLoader::LoadChunk(meta, 7, 2, &data, &error));
    CHECK(data.fromFile);

    int z1Walls = 0;
    int z1Floors = 0;
    for (const auto& object : data.objects) {
        if (object.z == 1 && object.layer == world::SquareLayer::Wall) {
            ++z1Walls;
        }
        if (object.z == 1 && object.layer == world::SquareLayer::Floor) {
            ++z1Floors;
            CHECK(object.sprite == "tile.floor.wood");
        }
    }
    CHECK(z1Walls == 14);
    CHECK(z1Floors == 16);

    // 楼上房间（z=1）
    bool foundUpperRoom = false;
    for (const auto& room : data.rooms) {
        if (room.z == 1) {
            foundUpperRoom = true;
            CHECK(room.name == "house_1_upper");
            CHECK(room.w == 4);
            CHECK(room.h == 4);
        }
    }
    CHECK(foundUpperRoom);

    // 挂接：z=1 的楼上地面/墙进 runtime Chunk，z=0 不受影响
    world::Chunk chunk(7, 2);
    world::ApplyChunkData(data, chunk);
    REQUIRE(chunk.Square(2, 1, 1).Floor() != nullptr);
    CHECK(chunk.Square(2, 1, 1).Floor()->sprite == "tile.floor.wood");
    CHECK(chunk.Square(2, 1, 1).RoomId() == 2);
    CHECK(chunk.Square(0, 0, 1).Layer(world::SquareLayer::Wall).size() == 1u);
    // z=0 同格：地面是 floors 网格的 default（该 chunk 无 override 的行也全为草/木）
    REQUIRE(chunk.Square(2, 1, 0).Floor() != nullptr);
    CHECK(chunk.Square(2, 1, 0).Floor()->sprite == "tile.floor.wood");  // 房内 z=0 也是木地板
}

TEST_CASE("ApplyChunkData：纯数据挂接进运行时 Chunk") {
    world::MapLoader::Meta meta;
    std::string error;
    REQUIRE(world::MapLoader::LoadMeta(kDemoDir, &meta, &error));
    world::ChunkData data;
    REQUIRE(world::MapLoader::LoadChunk(meta, 6, 1, &data, &error));

    world::Chunk chunk(6, 1);
    world::ApplyChunkData(data, chunk);

    const world::GridSquare& room = chunk.Square(5, 8, 0);
    REQUIRE(room.Floor() != nullptr);
    CHECK(room.Floor()->sprite == "tile.floor.wood");
    CHECK(room.RoomId() == 3);

    const world::GridSquare& wall = chunk.Square(4, 7, 0);
    REQUIRE(wall.Layer(world::SquareLayer::Wall).size() == 1u);
    CHECK(wall.Layer(world::SquareLayer::Wall)[0].sprite == "wall.brick");
    CHECK(wall.RoomId() == -1);  // 墙格不属于房间
    CHECK(wall.HasFloor());      // 墙下也有地面

    // 未在数据里的格：只有默认地面，无其它对象、无房间
    const world::GridSquare& plain = chunk.Square(0, 0, 0);
    REQUIRE(plain.Floor() != nullptr);
    CHECK(plain.Floor()->sprite == "tile.floor.grass");
    CHECK(plain.ObjectCount() == 1u);
    CHECK(plain.RoomId() == -1);
}

TEST_CASE("MapLoader：整张 village_demo 全量解析一致性") {
    world::MapLoader::Meta meta;
    std::string error;
    REQUIRE(world::MapLoader::LoadMeta(kDemoDir, &meta, &error));

    int filesLoaded = 0;
    int objectTotal = 0;
    int roomOccurrences = 0;
    for (int cy = 0; cy < 12; ++cy) {
        for (int cx = 0; cx < 12; ++cx) {
            world::ChunkData data;
            REQUIRE(world::MapLoader::LoadChunk(meta, cx, cy, &data, &error));
            CHECK(data.chunkX == cx);
            CHECK(data.chunkY == cy);
            if (data.fromFile) {
                ++filesLoaded;
            }
            objectTotal += static_cast<int>(data.objects.size());
            roomOccurrences += static_cast<int>(data.rooms.size());
        }
    }
    // 与生成器输出一致（确定性地图：131 个非默认 chunk / 398 对象（含 z=1 楼上）/ 15 房间出现）
    CHECK(filesLoaded == 131);
    CHECK(objectTotal == 398);
    CHECK(roomOccurrences == 15);
}

TEST_CASE("MapLoader：错误路径（坏元数据/坏 chunk）") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zl_maploader_test";
    std::filesystem::remove_all(dir);

    // 坏 format
    WriteTextFile(dir / "bad_format" / "map.json",
                  R"({"format":"unknown-v9","name":"x","bounds":{"chunkX0":0,"chunkY0":0,"chunksX":1,"chunksY":1}})");
    world::MapLoader::Meta meta;
    std::string error;
    CHECK_FALSE(world::MapLoader::LoadMeta((dir / "bad_format").string(), &meta, &error));
    CHECK_FALSE(error.empty());

    // 缺 bounds
    WriteTextFile(dir / "no_bounds" / "map.json", R"({"format":"zomboidlike-map-v1","name":"x"})");
    error.clear();
    CHECK_FALSE(world::MapLoader::LoadMeta((dir / "no_bounds").string(), &meta, &error));
    CHECK_FALSE(error.empty());

    // 完全不是 JSON
    WriteTextFile(dir / "garbage" / "map.json", "not json at all {{{");
    error.clear();
    CHECK_FALSE(world::MapLoader::LoadMeta((dir / "garbage").string(), &meta, &error));
    CHECK_FALSE(error.empty());

    // 合法元数据，随后测 chunk 级错误
    WriteTextFile(
        dir / "ok" / "map.json",
        R"({"format":"zomboidlike-map-v1","name":"t","bounds":{"chunkX0":0,"chunkY0":0,"chunksX":2,"chunksY":2},"terrain":{"defaultFloor":"tile.floor.grass"},"legend":{"g":"tile.floor.grass"}})");
    error.clear();
    REQUIRE(world::MapLoader::LoadMeta((dir / "ok").string(), &meta, &error));

    // cx/cy 与路径不一致 → 硬错误
    WriteTextFile(dir / "ok" / "chunks" / "c0_0" / "chunk_1_1.json",
                  R"({"cx":9,"cy":9})");
    world::ChunkData data;
    CHECK_FALSE(world::MapLoader::LoadChunk(meta, 1, 1, &data, &error));
    CHECK_FALSE(error.empty());

    // floors 行长度错误 → 硬错误
    WriteTextFile(dir / "ok" / "chunks" / "c0_0" / "chunk_1_0.json",
                  R"({"cx":1,"cy":0,"floors":["ggg","ggg","ggg","ggg","ggg","ggg","ggg","ggg","ggg","ggg"]})");
    error.clear();
    CHECK_FALSE(world::MapLoader::LoadChunk(meta, 1, 0, &data, &error));
    CHECK_FALSE(error.empty());

    // 条目级问题：坐标越界对象/越界房间/未知层名/未知 floor 字符 → 跳过或取默认，整体成功
    WriteTextFile(
        dir / "ok" / "chunks" / "c0_0" / "chunk_0_0.json",
        R"({"cx":0,"cy":0,
            "floors":["xggggggggg","gggggggggg","gggggggggg","gggggggggg","gggggggggg","gggggggggg","gggggggggg","gggggggggg","gggggggggg","gggggggggg"],
            "objects":[
              {"x":99,"y":0,"layer":"wall","sprite":"wall.brick"},
              {"x":0,"y":0,"layer":"bogus","sprite":"wall.brick"},
              {"x":0,"y":0,"layer":"wall"},
              {"x":2,"y":3,"layer":"item","sprite":"item.axe"}
            ],
            "rooms":[{"x":8,"y":8,"w":5,"h":5,"id":1}]})");
    error.clear();
    REQUIRE(world::MapLoader::LoadChunk(meta, 0, 0, &data, &error));
    CHECK(data.fromFile);
    CHECK(data.floors[0] == "tile.floor.grass");  // 未知字符 'x' → 默认地面
    REQUIRE(data.objects.size() == 1u);           // 仅最后一条合法对象
    CHECK(data.objects[0].sprite == "item.axe");
    CHECK(data.objects[0].localX == 2);
    CHECK(data.objects[0].layer == world::SquareLayer::Item);
    CHECK(data.rooms.empty());  // 越界房间被跳过

    std::filesystem::remove_all(dir);
}
