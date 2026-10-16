#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

#include "core/TaskScheduler.h"
#include "world/Chunk.h"
#include "world/ChunkManager.h"
#include "world/World.h"

using namespace zl;

namespace {

const std::string kDemoDir = std::string(ZL_SOURCE_DIR) + "/assets/maps/village_demo";

bool PumpUntil(world::ChunkManager& manager, float camX, float camY, const std::function<bool()>& done,
               int timeoutMs = 15000) {
    const auto begin = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 begin)
               .count() < timeoutMs) {
        manager.Update(camX, camY);  // 相机静止：继续按帧预算消费积压请求
        manager.ProcessResults();
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    manager.Update(camX, camY);
    manager.ProcessResults();
    return done();
}

void WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

}  // namespace

TEST_CASE("ChunkManager：相机进入 → 加载 7×7 窗口并挂接数据（M3-T4）") {
    core::TaskScheduler tasks(2);
    world::MapLoader::Meta meta;
    std::string error;
    REQUIRE(world::MapLoader::LoadMeta(kDemoDir, &meta, &error));

    world::World world;
    world::ChunkManager manager(world, meta, tasks, 3);

    // 相机落在 chunk (6,1)（房子所在 chunk）
    manager.Update(65.0f, 15.0f);
    // 请求按帧预算派发（kRequestBudget=4）：首帧只创建 4 个
    CHECK(manager.GetStats().loadingChunks == 4);
    CHECK(world.LoadedChunkCount() == 4);

    REQUIRE(PumpUntil(manager, 65.0f, 15.0f,
                      [&] { return manager.GetStats().loadedChunks == 49; }));
    CHECK(manager.GetStats().loadingChunks == 0);
    CHECK(manager.GetStats().totalLoaded == 49);
    CHECK(world.LoadedChunkCount() == 49);
    CHECK_FALSE(manager.HasPendingWork());

    // 抽查挂接结果：chunk (6,1) 的房子数据（与 test_maploader 相同的期望）
    world::Cell* cell = world.GetCell(0, 0);
    REQUIRE(cell != nullptr);
    world::Chunk* house = cell->GetChunk(6, 1);
    REQUIRE(house != nullptr);
    CHECK(house->State() == world::ChunkState::Loaded);
    CHECK(house->Square(5, 8, 0).RoomId() == 3);
    REQUIRE(house->Square(5, 8, 0).Floor() != nullptr);
    CHECK(house->Square(5, 8, 0).Floor()->sprite == "tile.floor.wood");
    CHECK(house->Square(4, 7, 0).Layer(world::SquareLayer::Wall).size() == 1u);

    // 地图范围外的 chunk（y<0）：纯稀疏默认（文件必不存在）
    const world::GridSquare* sparse = world.GetSquare(65, -15, 0);
    REQUIRE(sparse != nullptr);
    REQUIRE(sparse->Floor() != nullptr);
    CHECK(sparse->Floor()->sprite == "tile.floor.grass");
    CHECK(sparse->ObjectCount() == 1u);

    // 同一 chunk 内移动：差集不变，无新请求
    const auto before = manager.GetStats();
    manager.Update(66.0f, 18.9f);
    CHECK(manager.GetStats().totalLoaded == before.totalLoaded);
    CHECK(manager.GetStats().loadingChunks == 0);
}

TEST_CASE("ChunkManager：相机远跳 → 旧窗口卸载、新窗口加载，内存不增长") {
    core::TaskScheduler tasks(2);
    world::MapLoader::Meta meta;
    std::string error;
    REQUIRE(world::MapLoader::LoadMeta(kDemoDir, &meta, &error));

    world::World world;
    world::ChunkManager manager(world, meta, tasks, 3);

    manager.Update(55.0f, 55.0f);
    REQUIRE(PumpUntil(manager, 55.0f, 55.0f, [&] { return manager.GetStats().loadedChunks == 49; }));

    // 跳到地图范围外（chunk (50,50)，仍是默认地形世界）
    manager.Update(505.0f, 505.0f);
    CHECK(manager.GetStats().loadedChunks == 0);  // 旧窗口在 Update 内同步卸载
    CHECK(manager.GetStats().totalUnloaded >= 49);

    REQUIRE(PumpUntil(manager, 505.0f, 505.0f,
                      [&] { return manager.GetStats().loadedChunks == 49; }));
    CHECK(world.LoadedChunkCount() == 49);  // 驻留规模恒定
    CHECK(world.LoadedCellCount() == 1);    // chunk 47..53 全在 cell (1,1)
}

TEST_CASE("ChunkManager：高速巡航不阻塞主线程（不等结果连续移动）") {
    core::TaskScheduler tasks(2);
    world::MapLoader::Meta meta;
    std::string error;
    REQUIRE(world::MapLoader::LoadMeta(kDemoDir, &meta, &error));

    world::World world;
    world::ChunkManager manager(world, meta, tasks, 2);  // 5×5 窗口

    double maxUpdateMs = 0.0;
    float lastX = 30.0f;
    for (int step = 0; step <= 60; ++step) {
        lastX = static_cast<float>(step) * 12.0f;
        const auto begin = std::chrono::steady_clock::now();
        manager.Update(lastX, 30.0f);
        const auto end = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(end - begin).count();
        maxUpdateMs = std::max(maxUpdateMs, ms);
    }
    // 主线程只做差集/派发/卸载，且按帧预算限量（不在 Update 里等待任何解析、不一次性建 25 个 chunk）
    CHECK(maxUpdateMs < 10.0);

    // 稳定到终点后：恰是最终窗口 25 个 chunk，其余全部取消/卸载
    REQUIRE(PumpUntil(manager, lastX, 30.0f, [&] {
        return manager.GetStats().loadedChunks == 25 && manager.GetStats().loadingChunks == 0;
    }));
    CHECK(world.LoadedChunkCount() == 25);
    CHECK(manager.GetStats().totalCancelled > 0);  // 快速移动必然丢掉一些解析结果
}

TEST_CASE("ChunkManager：坏 chunk 文件重试上限后放弃，不崩溃") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zl_chunkmanager_test";
    std::filesystem::remove_all(dir);
    WriteTextFile(
        dir / "map.json",
        R"({"format":"zomboidlike-map-v1","name":"t","bounds":{"chunkX0":0,"chunkY0":0,"chunksX":2,"chunksY":2},"terrain":{"defaultFloor":"tile.floor.grass"},"legend":{"g":"tile.floor.grass"}})");
    WriteTextFile(dir / "chunks" / "c0_0" / "chunk_0_0.json", "garbage {{{ not json");

    core::TaskScheduler tasks(2);
    world::MapLoader::Meta meta;
    std::string error;
    REQUIRE(world::MapLoader::LoadMeta(dir.string(), &meta, &error));

    world::World world;
    world::ChunkManager manager(world, meta, tasks, 0);  // 只关心 chunk (0,0)

    manager.Update(5.0f, 5.0f);
    REQUIRE(PumpUntil(manager, 5.0f, 5.0f, [&] {
        return manager.GetStats().totalFailed >= 3 && manager.GetStats().loadingChunks == 0;
    }));
    CHECK(manager.GetStats().totalFailed == 3);  // 初始 + 2 次重试
    CHECK(manager.GetStats().loadedChunks == 0);
    CHECK(world.LoadedChunkCount() == 0);

    // 同一位置再 Update（未跨 chunk）：已放弃，不重新请求
    manager.Update(8.0f, 8.0f);
    manager.ProcessResults();
    CHECK(manager.GetStats().totalFailed == 3);

    // 移动到相邻 chunk（文件缺失 = 稀疏默认）：正常加载，不受影响
    manager.Update(15.0f, 5.0f);
    REQUIRE(PumpUntil(manager, 15.0f, 5.0f, [&] { return manager.GetStats().loadedChunks == 1; }));
    CHECK(manager.GetStats().totalFailed == 3);

    std::filesystem::remove_all(dir);
}
