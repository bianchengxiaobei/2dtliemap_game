#include <doctest/doctest.h>

#include <algorithm>
#include <random>
#include <vector>

#include "core/Constants.h"
#include "ecs/EcsWorld.h"
#include "ecs/SpatialIndex.h"
#include "ecs/components/CommonComponents.h"
#include "game/RenderCollectSystem.h"
#include "render/DrawList.h"
#include "world/WorldCoords.h"

using zl::ecs::EcsWorld;
using zl::ecs::Position;
using zl::ecs::SpatialIndex;
using zl::ecs::SpriteRef;
using zl::ecs::Velocity;

namespace {

// 在 (x, y) 造一个带 Position 的实体
entt::entity SpawnAt(EcsWorld& world, float x, float y, int z = 0) {
    const entt::entity e = world.Create();
    world.Registry().emplace<Position>(e, Position{x, y, z});
    return e;
}

bool Contains(const std::vector<entt::entity>& list, entt::entity e) {
    return std::find(list.begin(), list.end(), e) != list.end();
}

}  // namespace

TEST_CASE("SpatialIndex：基础分桶与统计") {
    EcsWorld world;
    SpatialIndex index;
    SpawnAt(world, 1.5f, 2.5f);      // chunk (0,0)
    SpawnAt(world, 9.9f, 0.1f);      // chunk (0,0)
    SpawnAt(world, 10.0f, 0.0f);     // chunk (1,0)（边界：10.0 属下一 chunk）
    SpawnAt(world, -0.5f, -0.5f);    // chunk (-1,-1)（负数走 FloorDiv）
    SpawnAt(world, 10.0f, 10.0f, 3); // 楼层不参与分桶

    index.Update(world);
    CHECK(index.EntityCount() == 5u);
    CHECK(index.BucketSize(0, 0) == 2u);   // a, b
    CHECK(index.BucketSize(1, 0) == 1u);   // c
    CHECK(index.BucketSize(-1, -1) == 1u); // d
    CHECK(index.BucketSize(1, 1) == 1u);   // e（z 无关）

    const auto fresh = SpawnAt(world, 3.0f, 3.0f);  // Update 前不入索引
    CHECK(index.EntityCount() == 5u);
    index.Update(world);
    CHECK(index.EntityCount() == 6u);
    CHECK(Contains([&] {
              std::vector<entt::entity> out;
              index.ForEachInChunks(0, 0, 0, 0, [&](entt::entity x) { out.push_back(x); });
              return out;
          }(),
          fresh));
}

TEST_CASE("SpatialIndex：跨 chunk 移动换桶（含负方向）") {
    EcsWorld world;
    SpatialIndex index;
    const auto e = SpawnAt(world, 5.0f, 5.0f);
    index.Update(world);
    CHECK(index.BucketSize(0, 0) == 1u);

    world.Registry().get<Position>(e).x = 15.0f;  // → chunk (1,0)
    index.Update(world);
    CHECK(index.BucketSize(0, 0) == 0u);
    CHECK(index.BucketSize(1, 0) == 1u);
    CHECK(index.EntityCount() == 1u);

    world.Registry().get<Position>(e).x = -15.0f;  // → chunk (-2,0)
    index.Update(world);
    CHECK(index.BucketSize(1, 0) == 0u);
    CHECK(index.BucketSize(-2, 0) == 1u);

    // 同 chunk 内微移：桶不变
    world.Registry().get<Position>(e).x = -11.0f;
    index.Update(world);
    CHECK(index.BucketSize(-2, 0) == 1u);
}

TEST_CASE("SpatialIndex：销毁实体清出（含未 Flush 的入队销毁）") {
    EcsWorld world;
    SpatialIndex index;
    const auto a = SpawnAt(world, 5.0f, 5.0f);
    const auto b = SpawnAt(world, 6.0f, 6.0f);
    const auto c = SpawnAt(world, 7.0f, 7.0f);
    index.Update(world);
    CHECK(index.BucketSize(0, 0) == 3u);

    world.Destroy(c);  // 入队即视为不可用：索引应在下一次 Update 清出（不等 Flush）
    index.Update(world);
    CHECK(index.EntityCount() == 2u);
    CHECK(index.BucketSize(0, 0) == 2u);

    world.Destroy(a);
    world.Flush();
    index.Update(world);
    CHECK(index.EntityCount() == 1u);
    CHECK(index.BucketSize(0, 0) == 1u);
    CHECK(index.BucketCount() == 1u);

    world.Destroy(b);
    world.Flush();
    index.Update(world);
    CHECK(index.EntityCount() == 0u);
    CHECK(index.BucketCount() == 0u);  // 空桶被移除
}

TEST_CASE("SpatialIndex：范围查询（闭区间）") {
    EcsWorld world;
    SpatialIndex index;
    std::vector<entt::entity> all;
    std::mt19937 rng(7u);
    std::uniform_real_distribution<float> u(-64.0f, 63.0f);
    for (int i = 0; i < 200; ++i) {
        all.push_back(SpawnAt(world, u(rng), u(rng)));
    }
    index.Update(world);

    std::vector<entt::entity> got;
    index.ForEachInChunks(-1, -1, 1, 1, [&](entt::entity e) { got.push_back(e); });

    // 期望集合 = 手工过滤
    std::vector<entt::entity> expect;
    for (const entt::entity e : all) {
        const Position& p = world.Registry().get<Position>(e);
        const int cx = zl::world::FloorDiv(static_cast<int>(std::floor(p.x)), zl::core::kChunkSize);
        const int cy = zl::world::FloorDiv(static_cast<int>(std::floor(p.y)), zl::core::kChunkSize);
        if (cx >= -1 && cx <= 1 && cy >= -1 && cy <= 1) {
            expect.push_back(e);
        }
    }
    CHECK(got.size() == expect.size());
    for (const entt::entity e : expect) {
        CHECK(Contains(got, e));
    }
}

TEST_CASE("SpatialIndex：大规模（2 万实体）计数与全量遍历一致") {
    EcsWorld world;
    SpatialIndex index;
    std::mt19937 rng(20261016u);
    std::uniform_int_distribution<int> u(-500, 499);
    for (int i = 0; i < 20000; ++i) {
        SpawnAt(world, static_cast<float>(u(rng)), static_cast<float>(u(rng)));
    }
    index.Update(world);
    CHECK(index.EntityCount() == 20000u);

    std::size_t total = 0;
    index.ForEachInChunks(-50, -50, 49, 49, [&](entt::entity) { ++total; });
    CHECK(total == 20000u);
    CHECK(index.BucketCount() > 0u);
}

TEST_CASE("DrawList：全局画家序 DrawItemLess（stage→z→diag→gx→layer→tieBreaker）") {
    auto Make = [](int stage, int z, int diag, int gx, int layer, uint64_t tie) {
        zl::render::DrawItem it;
        it.stage = stage;
        it.z = z;
        it.diag = diag;
        it.gx = gx;
        it.layer = layer;
        it.tieBreaker = tie;
        return it;
    };

    std::vector<zl::render::DrawItem> items = {
        Make(1, 0, 10, 5, 5, 42),   // 实体（layer 5）
        Make(1, 0, 10, 5, 25, 7),   // 更高世界层
        Make(0, 0, 99, 99, 0, 0),   // 地面批最优先（stage 0）
        Make(1, 1, 0, 0, 0, 0),     // z 大的晚画
        Make(1, 0, 10, 5, 5, 41),   // 同格同层：tieBreaker 决胜
        Make(1, 0, 9, 4, 9, 0),     // diag 小的早画
    };
    std::sort(items.begin(), items.end(), zl::render::DrawItemLess);

    CHECK(items[0].stage == 0);        // 地面批最前
    CHECK(items[1].diag == 9);         // 然后 diag 9
    CHECK(items[2].tieBreaker == 41);  // diag 10, gx 5, layer 5：tieBreaker 决胜
    CHECK(items[3].tieBreaker == 42);
    CHECK(items[4].layer == 25);       // layer 25 在实体层之后
    CHECK(items[5].z == 1);            // z=1 最后
}

TEST_CASE("RenderCollectSystem::FillEntityDrawItem：锚点/分层/透明/决胜字段") {
    // 用 generate 出来的占位帧参数（character_placeholder: ox=19, oy=52, 27x60, 画布 64x128）
    zl::render::AtlasFrame frame;
    frame.fx = 64;
    frame.fy = 128;
    frame.ox = 19;
    frame.oy = 52;
    frame.w = 27;
    frame.h = 60;
    frame.u0 = 0.1f;
    frame.v0 = 0.2f;
    frame.u1 = 0.3f;
    frame.v1 = 0.4f;

    zl::render::DrawItem item;
    zl::game::RenderCollectSystem::FillEntityDrawItem(70.25f, 18.75f, 0, 0xFFFFFFFFu, 777u, frame,
                                                      BGFX_INVALID_HANDLE, 2, &item);

    // 绘制矩形 == IsoMath 脚底锚点公式（无额外偏移）
    const zl::render::SpriteRect expect = zl::render::SpriteDrawRect(70.25f, 18.75f, 0, 64, 128, 19,
                                                                     52, 27, 60);
    CHECK(item.rect.x == doctest::Approx(expect.x));
    CHECK(item.rect.y == doctest::Approx(expect.y));
    CHECK(item.rect.w == doctest::Approx(expect.w));
    CHECK(item.rect.h == doctest::Approx(expect.h));

    CHECK(item.stage == 1);  // 实体永不进地面批
    CHECK(item.z == 0);
    CHECK(item.diag == 70 + 18);
    CHECK(item.gx == 70);
    CHECK(item.layer == zl::game::RenderCollectSystem::kEntityLayerBase + 2);
    CHECK(item.layer > zl::world::kSquareLayerCount - 1);  // 排在同格世界层之后
    CHECK(item.tieBreaker == 777u);
    CHECK(item.roomId == -1);  // 实体默认不参与房间剖切
    CHECK(item.color == 0xFFFFFFFFu);  // z=0 原样

    // z≥1：整体压到 35% 不透明（与世界对象一致）
    zl::game::RenderCollectSystem::FillEntityDrawItem(1.0f, 2.0f, 1, 0xFFFFFFFFu, 1u, frame,
                                                      BGFX_INVALID_HANDLE, 0, &item);
    CHECK(item.color == 0x59FFFFFFu);

    // 负数亚格坐标：floor 取整（画家序键）
    zl::game::RenderCollectSystem::FillEntityDrawItem(-0.5f, -0.25f, 0, 0xFFFFFFFFu, 1u, frame,
                                                      BGFX_INVALID_HANDLE, 0, &item);
    CHECK(item.gx == -1);
    CHECK(item.diag == -1 + -1);
}

TEST_CASE("M14-T1 FadeOccludingWalls：只淡出玩家前方且屏幕相交的 z=0 墙") {
    auto Make = [](int diag, int stage, int z, int layer, std::uint32_t color,
                   zl::render::SpriteRect rect) {
        zl::render::DrawItem item;
        item.diag = diag;
        item.stage = stage;
        item.z = z;
        item.layer = layer;
        item.color = color;
        item.rect = rect;
        return item;
    };
    constexpr int kWall = static_cast<int>(zl::world::SquareLayer::Wall);
    constexpr int kFloor = static_cast<int>(zl::world::SquareLayer::Floor);
    const zl::render::SpriteRect player{0.0f, 0.0f, 20.0f, 40.0f};

    std::vector<zl::render::DrawItem> items = {
        Make(9, 1, 0, kWall, 0xFFFFFFFFu, {10.0f, 10.0f, 64.0f, 112.0f}),   // 玩家之前
        Make(10, 1, 0, 5, 0xFFFFFFFFu, player),                              // 玩家
        Make(11, 1, 0, kWall, 0xFFFFFFFFu, {10.0f, 10.0f, 64.0f, 112.0f}),  // 玩家之后遮挡
        Make(11, 1, 0, kFloor, 0xFFFFFFFFu, {10.0f, 10.0f, 64.0f, 112.0f}), // 非墙
        Make(11, 1, 0, kWall, 0x20FFFFFFu, {10.0f, 10.0f, 64.0f, 112.0f}),  // 已更透明
        Make(11, 1, 1, kWall, 0xFFFFFFFFu, {10.0f, 10.0f, 64.0f, 112.0f}),  // 楼上
        Make(11, 1, 0, kWall, 0xFFFFFFFFu, {100.0f, 100.0f, 64.0f, 112.0f}),  // 不相交
    };

    CHECK(zl::game::RenderCollectSystem::FadeOccludingWalls(player, 1, 0x59u, items) == 1u);
    CHECK(items[0].color == 0xFFFFFFFFu);
    CHECK(items[2].color == 0x59FFFFFFu);
    CHECK(items[3].color == 0xFFFFFFFFu);
    CHECK(items[4].color == 0x20FFFFFFu);
    CHECK(items[5].color == 0xFFFFFFFFu);
    CHECK(items[6].color == 0xFFFFFFFFu);
}

TEST_CASE("M14-T10 CutawayUpperLayersInRoom：只隐藏当前房间上方楼层") {
    auto Make = [](int z, int roomId, std::uint32_t color) {
        zl::render::DrawItem item;
        item.stage = z == 0 ? 0 : 1;
        item.z = z;
        item.roomId = roomId;
        item.color = color;
        return item;
    };
    std::vector<zl::render::DrawItem> items = {
        Make(1, 7, 0x59AABBCCu),  // 当前房间楼上：隐藏
        Make(1, 8, 0x59AABBCCu),  // 相邻房间楼上：保留
        Make(0, 7, 0xFFFFFFFFu),  // 地面层：保留
        Make(1, -1, 0x59AABBCCu), // 无房间信息：保留
    };

    CHECK(zl::game::RenderCollectSystem::CutawayUpperLayersInRoom(7, items) == 1u);
    CHECK(items[0].color == 0x00AABBCCu);
    CHECK(items[1].color == 0x59AABBCCu);
    CHECK(items[2].color == 0xFFFFFFFFu);
    CHECK(items[3].color == 0x59AABBCCu);
    CHECK(zl::game::RenderCollectSystem::CutawayUpperLayersInRoom(-1, items) == 0u);
}

TEST_CASE("RenderCollectSystem：ViewRect 包含性（含格边界余量）") {
    zl::game::RenderCollectSystem::ViewRect view;
    view.gxMin = 10;
    view.gyMin = 20;
    view.gxMax = 20;
    view.gyMax = 30;

    CHECK(view.Contains(10.0f, 20.0f));
    CHECK(view.Contains(20.99f, 30.99f));  // 上/右格内仍算可见（+1 余量）
    CHECK(view.Contains(10.0f, 31.0f));    // 边界上（=gyMax+1）仍算可见
    CHECK_FALSE(view.Contains(9.99f, 20.0f));
    CHECK_FALSE(view.Contains(10.0f, 32.0f));  // 超出 gyMax+1
    CHECK(view.Contains(21.0f, 20.0f));        // x 边界同样含 gxMax+1
    CHECK_FALSE(view.Contains(21.5f, 20.0f));
}
