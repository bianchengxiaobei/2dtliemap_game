#include <doctest/doctest.h>

#include <chrono>
#include <functional>
#include <string>
#include <thread>

#include "core/TaskScheduler.h"
#include "world/Chunk.h"
#include "world/ChunkManager.h"
#include "world/RegionStreaming.h"
#include "world/World.h"
#include "world/WorldDefinition.h"

using namespace zl;

namespace {

const std::string kM31Dir =
    std::string(ZL_SOURCE_DIR) + "/assets/maps/village_demo_m31";

bool PumpUntil(world::ChunkManager& manager, float camX, float camY,
               const std::function<bool()>& done, int timeoutMs = 15000) {
    const auto begin = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - begin)
               .count() < timeoutMs) {
        manager.Update(camX, camY);
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

}  // namespace

TEST_CASE("M31-T3 RegionStreaming：预测加载、区域冻结/恢复与边界夹取") {
    world::WorldDefinition definition;
    std::string error;
    REQUIRE(world::LoadWorldDefinition(kM31Dir, &definition, &error) ==
            world::WorldDefinitionStatus::Loaded);

    world::StreamingConfig config;
    config.loadRadius = 3;
    config.residentRadius = 5;
    config.maxLookaheadChunks = 2;
    config.lookaheadSeconds = 0.6f;
    world::RegionStreaming streaming(definition, config);

    streaming.SetFocus(70.0f, 24.0f);
    CHECK(streaming.ActiveRegionId() == "village_core");
    CHECK(streaming.Stats().active == 1);
    CHECK(streaming.Stats().ambient >= 3);
    CHECK(streaming.LookaheadChunks() == 0);

    streaming.SetFocus(70.0f, 24.0f, 60.0f, 0.0f);
    CHECK(streaming.LookaheadChunks() == 2);
    CHECK(streaming.PredictedChunkX() == streaming.FocusChunkX() + 2);
    CHECK(streaming.PredictedChunkY() == streaming.FocusChunkY());

    streaming.SetFocus(300.0f, 280.0f);
    CHECK(streaming.ActiveRegionId() == "east_wilds");
    CHECK(streaming.PhaseOf("village_core") == world::RegionPhase::Frozen);
    CHECK(streaming.Stats().freezeTransitions > 0);

    streaming.SetFocus(70.0f, 24.0f);
    CHECK(streaming.ActiveRegionId() == "village_core");
    CHECK(streaming.PhaseOf("village_core") == world::RegionPhase::Active);
    CHECK(streaming.Stats().restoreTransitions > 0);

    const glm::vec2 clamped =
        world::RegionStreaming::ClampFocus(definition.bounds, -1000.0f, 9999.0f);
    CHECK(clamped.x == doctest::Approx(0.5f));
    CHECK(clamped.y == doctest::Approx(359.5f));

    streaming.SetFocus(359.0f, 359.0f, 60.0f, 0.0f);
    CHECK(streaming.FocusChunkX() == 35);
    CHECK(streaming.FocusChunkY() == 35);
    CHECK(streaming.PredictedChunkX() == 35);
    CHECK(streaming.PredictedChunkY() == 35);
}

TEST_CASE("M31-T3 ChunkManager：resident 双层窗口、预测、边界与卸载重载") {
    core::TaskScheduler tasks(2);
    world::MapLoader::Meta meta;
    std::string error;
    REQUIRE(world::MapLoader::LoadMeta(kM31Dir, &meta, &error));

    world::World world;
    // loadRadius=3, residentRadius=5, request=4, attach=8, attachMs=2, bounds on.
    world::ChunkManager manager(world, meta, tasks, 3, 4, 8, 2.0, 5, true);
    manager.SetPrediction(60.0f, 0.0f);
    manager.Update(55.0f, 55.0f);
    CHECK(manager.GetStats().lookaheadChunks == 2);
    CHECK(manager.GetStats().predictedChunkX == 7);
    CHECK(manager.GetStats().predictedChunkY == 5);
    CHECK(manager.GetStats().residentChunks == 121);
    CHECK(manager.IsResident(0, 0));
    CHECK(manager.IsResident(10, 10));
    CHECK_FALSE(manager.IsResident(11, 10));

    REQUIRE(PumpUntil(manager, 55.0f, 55.0f, [&] {
        return manager.GetStats().loadedChunks == 49;
    }));
    CHECK(world.LoadedChunkCount() == 49);
    CHECK(manager.GetStats().residentChunks == 121);
    CHECK(manager.GetStats().loadedChunks < manager.GetStats().residentChunks);

    // East edge: prediction is clamped to the last chunk and no out-of-map
    // chunk is requested. The valid 4x4 intersection remains loaded.
    manager.SetPrediction(0.0f, 0.0f);
    manager.Update(350.0f, 350.0f);
    REQUIRE(PumpUntil(manager, 350.0f, 350.0f, [&] {
        return manager.GetStats().loadedChunks == 16;
    }));
    CHECK(manager.GetStats().predictedChunkX == 35);
    CHECK(manager.GetStats().predictedChunkY == 35);
    CHECK(manager.GetStats().residentChunks == 36);
    manager.ForEachLoadedChunk([&](const world::Chunk& chunk) {
        CHECK(chunk.ChunkX() >= 32);
        CHECK(chunk.ChunkX() <= 35);
        CHECK(chunk.ChunkY() >= 32);
        CHECK(chunk.ChunkY() <= 35);
    });

    // Returning to the same region reloads a complete window without leaving
    // stale chunks in World.
    manager.Update(55.0f, 55.0f);
    REQUIRE(PumpUntil(manager, 55.0f, 55.0f, [&] {
        return manager.GetStats().loadedChunks == 49;
    }));
    CHECK(world.LoadedChunkCount() == 49);
    CHECK(manager.GetStats().totalUnloaded >= 16);
    CHECK(manager.GetStats().totalFailed == 0);
}

TEST_CASE("M31-T3：无 POI 区域卸载后重载与区域相位恢复") {
    world::WorldDefinition definition;
    std::string error;
    REQUIRE(world::LoadWorldDefinition(kM31Dir, &definition, &error) ==
            world::WorldDefinitionStatus::Loaded);
    world::StreamingConfig config;
    config.loadRadius = 2;
    config.residentRadius = 3;
    world::RegionStreaming streaming(definition, config);

    streaming.SetFocus(15.0f, 15.0f);
    const std::string* west = streaming.RegionIdAtChunk(1, 1);
    REQUIRE(west != nullptr);
    CHECK(*west == "west_woods");
    CHECK(streaming.ActiveRegionId() == "west_woods");

    core::TaskScheduler tasks(2);
    world::MapLoader::Meta meta;
    REQUIRE(world::MapLoader::LoadMeta(kM31Dir, &meta, &error));
    world::World world;
    world::ChunkManager manager(world, meta, tasks, 2, 4, 8, 2.0, 3, true);
    manager.Update(15.0f, 15.0f);
    REQUIRE(PumpUntil(manager, 15.0f, 15.0f, [&] {
        return manager.GetStats().loadedChunks == 16;
    }));

    manager.Update(315.0f, 315.0f);
    streaming.SetFocus(315.0f, 315.0f);
    REQUIRE(PumpUntil(manager, 315.0f, 315.0f, [&] {
        return manager.GetStats().loadedChunks == 25;
    }));
    CHECK(streaming.PhaseOf("west_woods") == world::RegionPhase::Frozen);
    CHECK(streaming.ActiveRegionId() == "east_wilds");

    manager.Update(15.0f, 15.0f);
    streaming.SetFocus(15.0f, 15.0f);
    REQUIRE(PumpUntil(manager, 15.0f, 15.0f, [&] {
        return manager.GetStats().loadedChunks == 16;
    }));
    CHECK(streaming.PhaseOf("west_woods") == world::RegionPhase::Active);
    CHECK(streaming.Stats().freezeTransitions > 0);
    CHECK(streaming.Stats().restoreTransitions > 0);
    CHECK(manager.GetStats().totalUnloaded >= 25);
    CHECK(manager.GetStats().totalFailed == 0);
}

TEST_CASE("M31-T3 ChunkManager：连续跨 10+ chunk 与快速移动取消积压") {
    core::TaskScheduler tasks(2);
    world::MapLoader::Meta meta;
    std::string error;
    REQUIRE(world::MapLoader::LoadMeta(kM31Dir, &meta, &error));

    world::World world;
    world::ChunkManager manager(world, meta, tasks, 2, 4, 8, 2.0, 4, true);

    // 13 consecutive chunk centers (3..15), processing each one like a
    // normal exploration step.
    for (int step = 0; step < 13; ++step) {
        const float x = 30.0f + static_cast<float>(step) * 10.0f;
        manager.Update(x, 55.0f);
        REQUIRE(PumpUntil(manager, x, 55.0f, [&] {
            return manager.GetStats().loadedChunks == 25 &&
                   manager.GetStats().loadingChunks == 0;
        }));
        CHECK(manager.GetStats().totalFailed == 0);
    }
    CHECK(world.LoadedChunkCount() == 25);
    CHECK(manager.GetStats().totalLoaded > 25);

    // Two-grid (2 chunk) jumps without waiting: completed jobs from stale
    // centers must cancel instead of attaching to the final window.
    for (int step = 0; step < 8; ++step) {
        manager.Update(150.0f + static_cast<float>(step) * 20.0f, 55.0f);
    }
    REQUIRE(PumpUntil(manager, 290.0f, 55.0f, [&] {
        return manager.GetStats().loadedChunks == 25 &&
               manager.GetStats().loadingChunks == 0;
    }));
    CHECK(world.LoadedChunkCount() == 25);
    CHECK(manager.GetStats().totalCancelled > 0);
    CHECK(manager.GetStats().totalFailed == 0);
}
