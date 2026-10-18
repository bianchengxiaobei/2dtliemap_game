#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "save/ChunkDiff.h"
#include "save/ChunkDiffStore.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"

using namespace zl;

namespace {

void MakeBaseChunk(world::Chunk& chunk) {
    chunk.Square(1, 1, 0).SetFloor({"tile.floor.wood", 0, 0});
    chunk.Square(1, 1, 0).AddObject(world::SquareLayer::Wall,
                                    {"wall.brick", 0, 0, true});
    chunk.Square(2, 2, 0).SetFloor({"tile.floor.grass", 0, 0});
    chunk.Square(2, 2, 0).SetRoomId(7);
    chunk.Square(5, 5, 1).SetFloor({"tile.floor.wood", 0, 0});
    chunk.CaptureBaseline();
}

const save::ChunkSquareDiff* FindSquare(const save::ChunkDiff& diff, int lx, int ly,
                                        int z) {
    for (const save::ChunkSquareDiff& square : diff.squares) {
        if (square.lx == lx && square.ly == ly && square.z == z) {
            return &square;
        }
    }
    return nullptr;
}

bool MakeDiff(save::ChunkDiff* out) {
    world::Chunk chunk(0, 0);
    MakeBaseChunk(chunk);
    chunk.Square(1, 1, 0).ClearLayer(world::SquareLayer::Wall);
    chunk.Square(2, 2, 0).SetRoomId(8);
    chunk.Square(3, 3, 0).AddObject(world::SquareLayer::Item,
                                    {"item.axe", 2, -1, false});
    chunk.Square(3, 3, 0).AddObject(world::SquareLayer::Furniture,
                                    {"furn.crate", 0, 0, true});
    chunk.Square(5, 5, 1).SetFloor({"tile.floor.dirt", 0, 0});

    return save::CaptureChunkDiff(chunk, out);
}

}  // namespace

TEST_CASE("M10-T2 只捕获相对母地图发生变化的格") {
    world::Chunk chunk(0, 0);
    MakeBaseChunk(chunk);
    CHECK(chunk.HasBaseline());
    CHECK_FALSE(chunk.IsDirty());
    CHECK_FALSE(chunk.IsSquareModified(1, 1, 0));

    // const 访问不应产生 dirty。
    const world::Chunk& readOnly = chunk;
    (void)readOnly.Square(1, 1, 0);
    CHECK_FALSE(chunk.IsDirty());

    chunk.Square(1, 1, 0).ClearLayer(world::SquareLayer::Wall);
    chunk.Square(2, 2, 0).SetRoomId(8);
    chunk.Square(3, 3, 0).AddObject(world::SquareLayer::Item,
                                    {"item.axe", 2, -1, false});
    chunk.Square(3, 3, 0).AddObject(world::SquareLayer::Furniture,
                                    {"furn.crate", 0, 0, true});
    chunk.Square(5, 5, 1).SetFloor({"tile.floor.dirt", 0, 0});
    CHECK(chunk.IsDirty());
    CHECK(chunk.IsSquareModified(1, 1, 0));
    CHECK_FALSE(chunk.IsSquareModified(4, 4, 0));

    save::ChunkDiff diff;
    std::string error;
    REQUIRE(save::CaptureChunkDiff(chunk, &diff, &error));
    CHECK(error.empty());
    CHECK(diff.chunkX == 0);
    CHECK(diff.chunkY == 0);
    CHECK(diff.squares.size() == 4u);
    CHECK(FindSquare(diff, 1, 1, 0) != nullptr);
    CHECK(FindSquare(diff, 2, 2, 0) != nullptr);
    CHECK(FindSquare(diff, 3, 3, 0) != nullptr);
    CHECK(FindSquare(diff, 5, 5, 1) != nullptr);
    CHECK(FindSquare(diff, 4, 4, 0) == nullptr);

    const save::ChunkSquareDiff* itemSquare = FindSquare(diff, 3, 3, 0);
    REQUIRE(itemSquare != nullptr);
    CHECK(itemSquare->layers[static_cast<std::size_t>(world::SquareLayer::Item)]
              .size() == 1u);
    CHECK(itemSquare->layers[static_cast<std::size_t>(world::SquareLayer::Furniture)]
              .size() == 1u);
    CHECK(itemSquare->layers[static_cast<std::size_t>(world::SquareLayer::Furniture)][0]
              .blocksMovement);
}

TEST_CASE("M10-T2 chunk diff 编码往返并应用到母地图") {
    save::ChunkDiff diff;
    REQUIRE(MakeDiff(&diff));
    std::string error;
    REQUIRE(save::ValidateChunkDiff(diff, &error));
    CHECK(error.empty());

    const std::vector<std::uint8_t> bytes = save::EncodeChunkDiff(diff, &error);
    REQUIRE_FALSE(bytes.empty());
    CHECK(error.empty());

    save::ChunkDiff decoded;
    REQUIRE(save::DecodeChunkDiff(bytes, &decoded, &error));
    CHECK(error.empty());
    CHECK(decoded.chunkX == diff.chunkX);
    CHECK(decoded.chunkY == diff.chunkY);
    CHECK(decoded.squares.size() == diff.squares.size());

    world::Chunk target(0, 0);
    MakeBaseChunk(target);
    REQUIRE(save::ApplyChunkDiff(decoded, target, &error));
    CHECK(error.empty());
    CHECK(target.IsDirty());
    CHECK(target.IsSquareModified(1, 1, 0));

    // 墙被删除，但同格地面仍在
    CHECK(target.Square(1, 1, 0).Layer(world::SquareLayer::Wall).empty());
    REQUIRE(target.Square(1, 1, 0).Floor() != nullptr);
    CHECK(target.Square(1, 1, 0).Floor()->sprite == "tile.floor.wood");

    CHECK(target.Square(2, 2, 0).RoomId() == 8);
    REQUIRE(target.Square(3, 3, 0).Layer(world::SquareLayer::Item).size() == 1u);
    CHECK(target.Square(3, 3, 0).Layer(world::SquareLayer::Item)[0].sprite ==
          "item.axe");
    CHECK(target.Square(5, 5, 1).Floor() != nullptr);
    CHECK(target.Square(5, 5, 1).Floor()->sprite == "tile.floor.dirt");

    // 读档后的 diff 仍然相对母地图有效，下一次保存不会漏掉
    save::ChunkDiff recaptured;
    REQUIRE(save::CaptureChunkDiff(target, &recaptured));
    CHECK(recaptured.squares.size() == diff.squares.size());

    // 截断字节必须报错，不崩溃
    std::vector<std::uint8_t> truncated = bytes;
    truncated.pop_back();
    save::ChunkDiff broken;
    CHECK_FALSE(save::DecodeChunkDiff(truncated, &broken, &error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("M10-T2 改回母地图状态不产生 diff，Mark 不存档") {
    world::Chunk chunk(0, 0);
    MakeBaseChunk(chunk);
    chunk.Square(1, 1, 0).SetFloor({"tile.floor.dirt", 0, 0});
    chunk.Square(1, 1, 0).SetFloor({"tile.floor.wood", 0, 0});  // 改回原值

    save::ChunkDiff diff;
    REQUIRE(save::CaptureChunkDiff(chunk, &diff));
    CHECK(diff.squares.empty());

    chunk.Square(1, 1, 0).AddObject(world::SquareLayer::Mark, {"mark.blood"});
    REQUIRE(save::CaptureChunkDiff(chunk, &diff));
    CHECK(diff.squares.empty());  // Mark 是临时层，不参与 diff

    world::Chunk noBaseline(0, 0);
    std::string error;
    CHECK_FALSE(save::CaptureChunkDiff(noBaseline, &diff, &error));
    CHECK(error.find("基线") != std::string::npos);
}

TEST_CASE("M10-T2 ChunkDiffStore 只写改动 chunk 到 world/chunk_x_y.bin") {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "zl_chunk_diff_test";
    std::filesystem::remove_all(root);

    save::ChunkDiffStore store(root / "slot");
    save::ChunkDiff diff;
    REQUIRE(MakeDiff(&diff));
    std::string error;
    REQUIRE(store.Save(diff, &error));
    CHECK(error.empty());
    CHECK(store.Exists(0, 0));
    CHECK(store.ChunkPath(0, 0).filename() == "chunk_0_0.bin");
    CHECK(store.ChunkPath(0, 0).parent_path().filename() == "world");
    CHECK(std::filesystem::exists(root / "slot" / "world" / "chunk_0_0.bin"));

    save::ChunkDiff loaded;
    CHECK(store.Load(0, 0, &loaded, &error) ==
          save::ChunkDiffStore::LoadResult::Loaded);
    CHECK(error.empty());
    CHECK(loaded.squares.size() == diff.squares.size());

    world::Chunk target(0, 0);
    MakeBaseChunk(target);
    CHECK(store.LoadAndApply(0, 0, target, &error) ==
          save::ChunkDiffStore::LoadResult::Loaded);
    CHECK(error.empty());
    CHECK(target.Square(2, 2, 0).RoomId() == 8);
    CHECK(target.Square(3, 3, 0).Layer(world::SquareLayer::Item).size() == 1u);

    // 未改动 chunk 不落盘
    world::Chunk clean(1, 0);
    MakeBaseChunk(clean);
    REQUIRE(store.SaveChunk(clean, &error));
    CHECK_FALSE(store.Exists(1, 0));

    // 只是被非 const 访问（dirty=true）但内容未变，也不应写文件
    world::Chunk touched(2, 0);
    MakeBaseChunk(touched);
    (void)touched.Square(0, 0, 0);
    CHECK(touched.IsDirty());
    REQUIRE(store.SaveChunk(touched, &error));
    CHECK_FALSE(store.Exists(2, 0));

    // 有旧文件但改动已还原：保存会删除旧 diff
    world::Chunk reverted(0, 0);
    MakeBaseChunk(reverted);
    REQUIRE(store.SaveChunk(reverted, &error));
    CHECK_FALSE(store.Exists(0, 0));
    CHECK(error.empty());

    // 负坐标路径
    world::Chunk negative(-1, 2);
    MakeBaseChunk(negative);
    negative.Square(2, 2, 0).SetRoomId(9);
    REQUIRE(store.SaveChunk(negative, &error));
    CHECK(store.ChunkPath(-1, 2).filename() == "chunk_-1_2.bin");
    CHECK(store.Exists(-1, 2));

    // 文件缺失
    save::ChunkDiff missing;
    CHECK(store.Load(7, 7, &missing, &error) ==
          save::ChunkDiffStore::LoadResult::NotFound);

    // 损坏文件必须报错
    {
        std::ofstream bad(store.ChunkPath(3, 3), std::ios::binary | std::ios::trunc);
        bad << "not a chunk diff";
    }
    save::ChunkDiff broken;
    CHECK(store.Load(3, 3, &broken, &error) ==
          save::ChunkDiffStore::LoadResult::Error);
    CHECK_FALSE(error.empty());

    std::filesystem::remove_all(root);
}

TEST_CASE("M10-T2 chunk diff 坐标校验与非法内容拒绝") {
    save::ChunkDiff diff;
    REQUIRE(MakeDiff(&diff));
    world::Chunk wrongChunk(1, 1);
    std::string error;
    CHECK_FALSE(save::ApplyChunkDiff(diff, wrongChunk, &error));
    CHECK(error.find("坐标") != std::string::npos);

    save::ChunkSquareDiff square;
    square.lx = core::kChunkSize;  // 越界
    diff.squares.push_back(square);
    CHECK_FALSE(save::ValidateChunkDiff(diff, &error));
    CHECK(error.find("越界") != std::string::npos);

    save::ChunkDiff markDiff;
    markDiff.chunkX = 0;
    markDiff.chunkY = 0;
    save::ChunkSquareDiff mark;
    mark.layers[static_cast<std::size_t>(world::SquareLayer::Mark)].push_back(
        {"mark.blood"});
    markDiff.squares.push_back(mark);
    CHECK_FALSE(save::ValidateChunkDiff(markDiff, &error));
    CHECK(error.find("Mark") != std::string::npos);
}
