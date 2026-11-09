#include <doctest/doctest.h>

#include <cstddef>
#include <string>

#include "core/Constants.h"
#include "game/WorldQueries.h"
#include "world/Cell.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"
#include "world/World.h"
#include "world/WorldCoords.h"

using namespace zl;

// M3-T1：数据结构定稿的契约（尺寸、层序、负坐标语义）在编译期锁死
static_assert(core::kChunksPerCell == 30);
static_assert(core::kSquaresPerChunk == 100);
static_assert(core::kSquaresPerChunkAllZ == 800);
static_assert(static_cast<int>(world::SquareLayer::Floor) == 0);
static_assert(static_cast<int>(world::SquareLayer::Wall) == 1);
static_assert(static_cast<int>(world::SquareLayer::Furniture) == 2);
static_assert(static_cast<int>(world::SquareLayer::Item) == 3);
static_assert(static_cast<int>(world::SquareLayer::Mark) == 4);

TEST_CASE("FloorDiv/FloorMod：负数语义与恒等式（M3-T1）") {
    CHECK(world::FloorDiv(0, 10) == 0);
    CHECK(world::FloorDiv(9, 10) == 0);
    CHECK(world::FloorDiv(10, 10) == 1);
    CHECK(world::FloorDiv(-1, 10) == -1);   // 不是 C++ 的 -1/10 == 0
    CHECK(world::FloorDiv(-10, 10) == -1);
    CHECK(world::FloorDiv(-11, 10) == -2);

    CHECK(world::FloorMod(-1, 10) == 9);
    CHECK(world::FloorMod(-10, 10) == 0);
    CHECK(world::FloorMod(0, 10) == 0);

    // 恒等式 a == FloorDiv(a,b)*b + FloorMod(a,b)，且 FloorMod ∈ [0, b)
    for (int b : {10, 300}) {
        for (int a = -610; a <= 610; ++a) {
            const int q = world::FloorDiv(a, b);
            const int r = world::FloorMod(a, b);
            CHECK(a == q * b + r);
            CHECK(r >= 0);
            CHECK(r < b);
        }
    }
}

TEST_CASE("世界坐标分解：cell/chunk/局部往返（含负数）") {
    for (int wy = -60; wy <= 60; ++wy) {
        for (int wx = -60; wx <= 60; ++wx) {
            // 格级：chunk + 局部 → 还原
            const world::ChunkCoord chunk = world::ChunkOf(wx, wy);
            const world::SquareLocal local = world::SquareLocalOf(wx, wy);
            CHECK(chunk.cx * core::kChunkSize + local.lx == wx);
            CHECK(chunk.cy * core::kChunkSize + local.ly == wy);
            CHECK(local.lx >= 0);
            CHECK(local.lx < core::kChunkSize);

            // cell 级：cell + cell 内局部 chunk → 还原全局 chunk
            const world::CellCoord cell = world::CellOf(wx, wy);
            const world::ChunkLocal chunkLocal = world::ChunkLocalOf(chunk.cx, chunk.cy);
            CHECK(cell.cx * core::kChunksPerCell + chunkLocal.lcx == chunk.cx);
            CHECK(cell.cy * core::kChunksPerCell + chunkLocal.lcy == chunk.cy);
        }
    }

    // 边界样本（跨小节/跨 cell 的越界线）
    CHECK(world::ChunkOf(-1, -1).cx == -1);
    CHECK(world::ChunkOf(-10, -10).cx == -1);
    CHECK(world::ChunkOf(-11, -11).cx == -2);
    CHECK(world::CellOf(-1, -1).cx == -1);
    CHECK(world::CellOf(-300, -300).cx == -1);
    CHECK(world::CellOf(-301, -301).cx == -2);
    CHECK(world::CellOf(299, 299).cx == 0);
    CHECK(world::CellOf(300, 300).cx == 1);
}

TEST_CASE("GridSquare：分层对象数组") {
    world::GridSquare sq(3, -4, 1);
    CHECK(sq.X() == 3);
    CHECK(sq.Y() == -4);
    CHECK(sq.Z() == 1);
    CHECK(sq.RoomId() == -1);  // 默认室外
    CHECK(sq.IsEmpty());
    CHECK(sq.ObjectCount() == 0u);
    CHECK_FALSE(sq.HasFloor());
    CHECK(sq.Floor() == nullptr);

    // 地面便捷接口
    sq.SetFloor({"tile.floor.grass", 0, 0});
    CHECK(sq.HasFloor());
    CHECK(sq.ObjectCount() == 1u);
    REQUIRE(sq.Floor() != nullptr);
    CHECK(sq.Floor()->sprite == "tile.floor.grass");

    // 重复 SetFloor 为覆盖语义（chunk 重载场景）
    sq.SetFloor({"tile.floor.dirt", 1, -2});
    CHECK(sq.ObjectCount() == 1u);
    REQUIRE(sq.Floor() != nullptr);
    CHECK(sq.Floor()->sprite == "tile.floor.dirt");
    CHECK(sq.Floor()->offsetX == 1);
    CHECK(sq.Floor()->offsetY == -2);

    // 各层独立累加
    sq.AddObject(world::SquareLayer::Wall, {"wall.brick"});
    sq.AddObject(world::SquareLayer::Furniture, {"furn.chair"});
    sq.AddObject(world::SquareLayer::Furniture, {"furn.table", 5, 0});
    CHECK(sq.Layer(world::SquareLayer::Wall).size() == 1u);
    CHECK(sq.Layer(world::SquareLayer::Furniture).size() == 2u);
    CHECK(sq.Layer(world::SquareLayer::Item).empty());
    CHECK(sq.ObjectCount() == 4u);

    sq.SetRoomId(42);
    CHECK(sq.RoomId() == 42);

    // ClearLayer 只清一层；Clear 清全部并复位房间
    sq.ClearLayer(world::SquareLayer::Furniture);
    CHECK(sq.Layer(world::SquareLayer::Furniture).empty());
    CHECK(sq.ObjectCount() == 2u);
    sq.Clear();
    CHECK(sq.IsEmpty());
    CHECK(sq.RoomId() == -1);
    CHECK_FALSE(sq.HasFloor());
}

TEST_CASE("Chunk：坐标初始化与 [z][ly][lx] 格访问") {
    world::Chunk chunk(-3, 5);  // 覆盖世界格 x ∈ [-30,-21]，y ∈ [50,59]
    CHECK(chunk.NonEmptyZMask() == 0);
    chunk.MarkZUsed(2);
    CHECK((chunk.NonEmptyZMask() & (1u << 2)) != 0);
    chunk.Reset();
    CHECK(chunk.NonEmptyZMask() == 0);
    CHECK(chunk.ChunkX() == -3);
    CHECK(chunk.ChunkY() == 5);
    CHECK(world::Chunk::WorldOriginX(-3) == -30);
    CHECK(world::Chunk::WorldOriginY(5) == 50);

    // 全部 800 格的坐标都要正确（含负数世界坐标）
    for (int z = 0; z < core::kMaxZ; ++z) {
        for (int ly = 0; ly < core::kChunkSize; ++ly) {
            for (int lx = 0; lx < core::kChunkSize; ++lx) {
                const world::GridSquare& sq = chunk.Square(lx, ly, z);
                CHECK(sq.X() == -30 + lx);
                CHECK(sq.Y() == 50 + ly);
                CHECK(sq.Z() == z);
            }
        }
    }

    // 写一格，其余 z/相邻格不受影响
    chunk.Square(4, 6, 1).SetFloor({"tile.floor.wood"});
    CHECK(chunk.Square(4, 6, 1).HasFloor());
    CHECK_FALSE(chunk.Square(4, 7, 1).HasFloor());
    CHECK_FALSE(chunk.Square(4, 6, 0).HasFloor());
}

TEST_CASE("Chunk：生命周期状态机") {
    using world::ChunkState;
    CHECK(world::IsChunkTransitionAllowed(ChunkState::Unloaded, ChunkState::Loading));
    CHECK(world::IsChunkTransitionAllowed(ChunkState::Loading, ChunkState::Loaded));
    CHECK(world::IsChunkTransitionAllowed(ChunkState::Loading, ChunkState::Unloaded));  // 解析失败回退
    CHECK(world::IsChunkTransitionAllowed(ChunkState::Loaded, ChunkState::Unloading));
    CHECK(world::IsChunkTransitionAllowed(ChunkState::Unloading, ChunkState::Unloaded));
    CHECK_FALSE(world::IsChunkTransitionAllowed(ChunkState::Unloaded, ChunkState::Loaded));
    CHECK_FALSE(world::IsChunkTransitionAllowed(ChunkState::Loaded, ChunkState::Loading));
    CHECK_FALSE(world::IsChunkTransitionAllowed(ChunkState::Unloading, ChunkState::Loaded));
    CHECK_FALSE(world::IsChunkTransitionAllowed(ChunkState::Unloaded, ChunkState::Unloaded));

    CHECK(std::string(world::ToString(ChunkState::Loaded)) == "Loaded");

    world::Chunk chunk(0, 0);
    CHECK(chunk.State() == ChunkState::Unloaded);
    chunk.SetState(ChunkState::Loading);
    CHECK(chunk.State() == ChunkState::Loading);
    chunk.SetState(ChunkState::Loaded);
    chunk.Square(1, 1, 0).SetFloor({"tile.floor.grass"});
    chunk.SetState(ChunkState::Unloading);
    chunk.SetState(ChunkState::Unloaded);
    CHECK(chunk.State() == ChunkState::Unloaded);
    CHECK_FALSE(chunk.Square(1, 1, 0).HasFloor());  // 卸载时数据已清空

    // Reset 清数据并强制回 Unloaded（任意状态可调用，为卸载复用路径）
    chunk.SetState(ChunkState::Loading);
    chunk.Square(2, 2, 0).SetFloor({"tile.floor.grass"});
    chunk.Reset();
    CHECK(chunk.State() == ChunkState::Unloaded);
    CHECK_FALSE(chunk.Square(2, 2, 0).HasFloor());
}

TEST_CASE("Cell：chunk 槽位按需创建/移除与坐标换算") {
    world::Cell cell(2, -1);  // 世界格 x ∈ [600,899]，y ∈ [-300,-1]
    CHECK(cell.CellX() == 2);
    CHECK(cell.CellY() == -1);
    CHECK(cell.LoadedChunkCount() == 0u);
    CHECK(cell.IsEmpty());
    CHECK(cell.GetChunk(0, 0) == nullptr);

    world::Chunk& chunk = cell.GetOrCreateChunk(3, 4);
    CHECK(&cell.GetOrCreateChunk(3, 4) == &chunk);  // 幂等
    CHECK(cell.LoadedChunkCount() == 1u);
    CHECK_FALSE(cell.IsEmpty());
    CHECK(chunk.ChunkX() == 2 * core::kChunksPerCell + 3);   // 全局 chunk 坐标 63
    CHECK(chunk.ChunkY() == -1 * core::kChunksPerCell + 4);  // 全局 chunk 坐标 -26

    // 世界格坐标直达：(635,-253) 落在 chunk(3,4) 的局部 (5,7)
    world::GridSquare* sq = cell.GetSquare(635, -253, 2);
    REQUIRE(sq != nullptr);
    CHECK(sq->X() == 635);
    CHECK(sq->Y() == -253);
    CHECK(sq->Z() == 2);

    // chunk(0,0) 未创建 → 直达访问返回 nullptr
    CHECK(cell.GetSquare(600, -300, 0) == nullptr);

    cell.RemoveChunk(3, 4);
    CHECK(cell.LoadedChunkCount() == 0u);
    CHECK(cell.GetChunk(3, 4) == nullptr);
    CHECK_FALSE(cell.RemoveChunk(3, 4));  // 已移除，再删返回 false

    // 遍历只访问存在的槽位
    cell.GetOrCreateChunk(0, 0);
    cell.GetOrCreateChunk(29, 29);
    int count = 0;
    cell.ForEachChunk([&count](world::Chunk&) { ++count; });
    CHECK(count == 2);
}

TEST_CASE("World：cell 稀疏网格与跨 chunk 访问") {
    world::World world;
    CHECK(world.LoadedCellCount() == 0u);
    CHECK(world.GetCell(0, 0) == nullptr);
    CHECK(world.GetSquare(0, 0, 0) == nullptr);

    // 正坐标：(305,305) → cell(1,1) / chunk(30,30)
    world::GridSquare& sq = world.GetOrCreateSquare(305, 305, 0);
    CHECK(sq.X() == 305);
    CHECK(sq.Y() == 305);
    CHECK(sq.Z() == 0);
    CHECK(world.LoadedCellCount() == 1u);
    CHECK(world.LoadedChunkCount() == 1u);

    // 负坐标：(-1,-301) → cell(-1,-2) / chunk(-1,-31) / 局部 (9,9)
    world::GridSquare& negSq = world.GetOrCreateSquare(-1, -301, 3);
    CHECK(negSq.X() == -1);
    CHECK(negSq.Y() == -301);
    CHECK(negSq.Z() == 3);
    CHECK(world.LoadedCellCount() == 2u);
    CHECK(world.LoadedChunkCount() == 2u);

    // 同 cell 再取其它格：不新增 cell/chunk
    world::GridSquare& sq2 = world.GetOrCreateSquare(305, 306, 0);
    CHECK(world.LoadedCellCount() == 2u);
    CHECK(world.LoadedChunkCount() == 2u);
    CHECK(&sq2 != &sq);
    CHECK(world.GetSquare(305, 306, 0) == &sq2);
    CHECK(world.GetSquare(305, 305, 0) == &sq);

    // 未创建链路查询返回 nullptr
    CHECK(world.GetSquare(9999, 9999, 0) == nullptr);

    // 移除 cell
    CHECK(world.RemoveCell(1, 1));
    CHECK(world.GetCell(1, 1) == nullptr);
    CHECK(world.LoadedCellCount() == 1u);
    CHECK_FALSE(world.RemoveCell(1, 1));

    world.Clear();
    CHECK(world.LoadedCellCount() == 0u);
}

TEST_CASE("WorldQueries：工作台范围按对象逻辑名与已加载 chunk 查询") {
    world::World world;
    world::GridSquare& square = world.GetOrCreateSquare(72, 25, 0);
    const world::ChunkCoord cc = world::ChunkOf(72, 25);
    world::Chunk* chunk = game::FindChunkInWorld(world, cc.cx, cc.cy);
    REQUIRE(chunk != nullptr);
    chunk->SetState(world::ChunkState::Loading);
    chunk->SetState(world::ChunkState::Loaded);
    square.AddObject(world::SquareLayer::Furniture,
                     {"object.workbench", 0, 0, true});
    world::GridSquare& campSquare = world.GetOrCreateSquare(76, 25, 0);
    campSquare.AddObject(world::SquareLayer::Furniture,
                         {"object.campfire", 0, 0, true});
    world::GridSquare& waterSquare = world.GetOrCreateSquare(74, 25, 0);
    waterSquare.AddObject(world::SquareLayer::Furniture,
                          {"object.locker", 0, 0, false});

    CHECK(game::HasStationNearby(world, 74.5f, 25.5f, 0, "locker", 2.5f));
    CHECK(game::HasStationNearby(world, 74.5f, 25.5f, 0, "workbench", 2.5f));
    CHECK(game::HasStationNearby(world, 70.6f, 25.5f, 0, "workbench", 2.5f));
    CHECK(game::HasStationNearby(world, 70.6f, 25.5f, 0, "object.workbench", 2.5f));
    CHECK(game::HasStationNearby(world, 76.5f, 25.5f, 0, "campfire", 2.5f));
    int campfireX = -1;
    int campfireY = -1;
    CHECK(game::FindCampfireNearby(world, 74.5f, 25.5f, 0, 2.5f,
                                   &campfireX, &campfireY));
    CHECK(campfireX == 76);
    CHECK(campfireY == 25);
    CHECK(game::HasCampfireNearby(world, 74.5f, 25.5f, 0, 2.5f));
    CHECK_FALSE(game::HasCampfireNearby(world, 74.5f, 25.5f, 1, 2.5f));
    CHECK_FALSE(game::HasStationNearby(world, 76.5f, 25.5f, 0, "workbench", 2.5f));
    CHECK_FALSE(game::HasStationNearby(world, 80.0f, 25.0f, 0, "workbench", 2.5f));
    CHECK_FALSE(game::HasStationNearby(world, 72.5f, 25.5f, 0, "workbench", 0.0f));
    CHECK_FALSE(game::HasStationNearby(world, 72.5f, 25.5f, 0, "workbench", -1.0f));
    CHECK_FALSE(game::HasStationNearby(world, 72.5f, 25.5f, 0, "forge", 2.5f));
    CHECK_FALSE(game::HasStationNearby(world, 72.5f, 25.5f, 1, "workbench", 2.5f));
}

TEST_CASE("World：RemoveEmptyCells 回收空 cell") {
    world::World world;
    world.GetOrCreateCell(0, 0);
    world.GetOrCreateCell(5, 5);
    world.GetOrCreateSquare(3, 3, 0);  // 落在 cell(0,0)，使其非空
    CHECK(world.LoadedCellCount() == 2u);

    CHECK(world.RemoveEmptyCells() == 1u);  // 只有 cell(5,5) 可回收
    CHECK(world.LoadedCellCount() == 1u);
    CHECK(world.GetCell(0, 0) != nullptr);
    CHECK(world.GetCell(5, 5) == nullptr);
    CHECK(world.GetSquare(3, 3, 0) != nullptr);  // 数据仍在

    CHECK(world.RemoveEmptyCells() == 0u);
}
