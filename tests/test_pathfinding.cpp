#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

#include "ai/AStar.h"
#include "ai/Passability.h"
#include "ai/PathfindingService.h"
#include "ai/components/AiComponents.h"
#include "ai/systems/PathFollowSystem.h"
#include "characters/components/CharacterComponents.h"
#include "characters/systems/MovementSystem.h"
#include "core/TaskScheduler.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"
#include "world/WorldCoords.h"

using namespace zl;

namespace {

// 以谓词填充 chunk 矩形区域生成快照（未覆盖处 = 阻挡，来自 PathSnapshot 语义）
ai::PathSnapshot MakeSnapshot(int cx0, int cy0, int cx1, int cy1,
                              const std::function<bool(int, int)>& blocked) {
    ai::PathSnapshot snapshot(cx0, cy0, cx1, cy1);
    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            ai::ChunkPassability bits;
            for (int ly = 0; ly < core::kChunkSize; ++ly) {
                for (int lx = 0; lx < core::kChunkSize; ++lx) {
                    const int gx = cx * core::kChunkSize + lx;
                    const int gy = cy * core::kChunkSize + ly;
                    if (blocked(gx, gy)) {
                        bits.SetBlocked(lx, ly);
                    }
                }
            }
            snapshot.SetChunk(cx, cy, bits);
        }
    }
    return snapshot;
}

ai::PathSnapshot MakeOpenSnapshot(int chunksX, int chunksY) {
    return MakeSnapshot(0, 0, chunksX - 1, chunksY - 1, [](int, int) { return false; });
}

bool Contains(const std::vector<ai::GridCell>& cells, int x, int y) {
    for (const ai::GridCell& cell : cells) {
        if (cell.x == x && cell.y == y) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("M4-T6 passability：chunk 位图分层与 z 语义") {
    world::Chunk chunk(0, 0);
    chunk.Square(2, 3, 0).AddObject(world::SquareLayer::Wall, {"wall.brick", 0, 0, true});
    chunk.Square(5, 5, 0).AddObject(world::SquareLayer::Furniture, {"object.crate", 0, 0, true});
    chunk.Square(7, 7, 0).AddObject(world::SquareLayer::Floor, {"tile.floor.grass", 0, 0, false});
    chunk.Square(1, 1, 1).AddObject(world::SquareLayer::Wall, {"wall.brick", 0, 0, true});

    const ai::ChunkPassability bits = ai::BuildChunkPassability(chunk);
    CHECK(bits.IsBlocked(2, 3));   // 墙层
    CHECK(bits.IsBlocked(5, 5));   // 家具层同样阻挡
    CHECK_FALSE(bits.IsBlocked(7, 7));  // 地面不阻挡
    CHECK_FALSE(bits.IsBlocked(1, 1));  // z=1 不参与地面寻路
    CHECK_FALSE(bits.IsBlocked(0, 0));
    CHECK((bits.rows[3] & (1u << 2)) != 0u);  // 位序：行 ly、位 lx
}

TEST_CASE("M4-T6 snapshot：未填充 chunk / 区域外一律阻挡") {
    ai::PathSnapshot snapshot(0, 0, 1, 1);  // 20×20 格区域
    ai::ChunkPassability open;               // 全 0 = 全通
    snapshot.SetChunk(0, 0, open);

    CHECK_FALSE(snapshot.IsBlocked(5, 5));
    CHECK(snapshot.IsBlocked(15, 15));  // chunk (1,1) 未填充
    CHECK(snapshot.IsBlocked(-1, 5));   // 区域外（负坐标）
    CHECK(snapshot.IsBlocked(20, 5));
    CHECK(snapshot.IsBlocked(5, 20));
    CHECK(snapshot.InRegion(19, 19));
    CHECK_FALSE(snapshot.InRegion(20, 19));
}

TEST_CASE("M4-T6 A*：直线与对角（√2 代价）") {
    const ai::PathSnapshot snapshot = MakeOpenSnapshot(1, 1);  // 10×10
    SUBCASE("直线") {
        const ai::AStarResult result = ai::PlanPath(snapshot, 0, 0, 5, 0);
        REQUIRE(result.outcome == ai::AStarResult::Outcome::Found);
        CHECK(result.cells.size() == 5u);
        CHECK(result.cost == doctest::Approx(5.0f));
        CHECK(result.cells.front().x == 1);
        CHECK(result.cells.back().x == 5);
        for (const ai::GridCell& cell : result.cells) {
            CHECK(cell.y == 0);
        }
    }
    SUBCASE("对角 √2") {
        const ai::AStarResult result = ai::PlanPath(snapshot, 0, 0, 9, 9);
        REQUIRE(result.outcome == ai::AStarResult::Outcome::Found);
        CHECK(result.cells.size() == 9u);
        CHECK(result.cost == doctest::Approx(9.0f * 1.41421356f).epsilon(0.01));
        for (const ai::GridCell& cell : result.cells) {
            CHECK(cell.x == cell.y);
        }
    }
    SUBCASE("起点 = 终点") {
        const ai::AStarResult result = ai::PlanPath(snapshot, 4, 4, 4, 4);
        CHECK(result.outcome == ai::AStarResult::Outcome::Found);
        CHECK(result.cells.empty());
    }
}

TEST_CASE("M4-T6 A*：绕墙走缺口（路点全部可走）") {
    // x=5 一整列墙（chunk 0 内 y 0..9），仅 (5,3) 缺口
    const ai::PathSnapshot snapshot = MakeSnapshot(0, 0, 1, 1, [](int x, int y) {
        return x == 5 && y != 3;
    });
    const ai::AStarResult result = ai::PlanPath(snapshot, 2, 0, 9, 0);
    REQUIRE(result.outcome == ai::AStarResult::Outcome::Found);
    CHECK(Contains(result.cells, 5, 3));  // 必须穿过缺口
    for (const ai::GridCell& cell : result.cells) {
        CHECK_FALSE(snapshot.IsBlocked(cell.x, cell.y));
    }
}

TEST_CASE("M4-T6 A*：禁止穿角（对角需两个正交邻格可走）") {
    SUBCASE("双角堵死 → 不可达") {
        const ai::PathSnapshot snapshot = MakeSnapshot(0, 0, 0, 0, [](int x, int y) {
            return (x == 1 && y == 0) || (x == 0 && y == 1);
        });
        const ai::AStarResult result = ai::PlanPath(snapshot, 0, 0, 1, 1);
        CHECK(result.outcome == ai::AStarResult::Outcome::Unreachable);
    }
    SUBCASE("单角可绕") {
        const ai::PathSnapshot snapshot =
            MakeSnapshot(0, 0, 0, 0, [](int x, int y) { return x == 1 && y == 0; });
        const ai::AStarResult result = ai::PlanPath(snapshot, 0, 0, 1, 1);
        REQUIRE(result.outcome == ai::AStarResult::Outcome::Found);
        CHECK(Contains(result.cells, 0, 1));
    }
}

TEST_CASE("M4-T6 A*：目标围死 → Unreachable；越界/目标阻挡 语义") {
    SUBCASE("八邻围死") {
        const ai::PathSnapshot snapshot = MakeSnapshot(0, 0, 0, 0, [](int x, int y) {
            const int dx = x - 5;
            const int dy = y - 5;
            return std::abs(dx) <= 1 && std::abs(dy) <= 1 && !(dx == 0 && dy == 0);
        });
        const ai::AStarResult result = ai::PlanPath(snapshot, 0, 0, 5, 5);
        CHECK(result.outcome == ai::AStarResult::Outcome::Unreachable);
    }
    SUBCASE("区域外") {
        const ai::PathSnapshot snapshot = MakeOpenSnapshot(1, 1);
        CHECK(ai::PlanPath(snapshot, 0, 0, 10, 5).outcome ==
              ai::AStarResult::Outcome::Invalid);
        CHECK(ai::PlanPath(snapshot, -1, 0, 5, 5).outcome ==
              ai::AStarResult::Outcome::Invalid);
    }
    SUBCASE("目标格阻挡") {
        const ai::PathSnapshot snapshot =
            MakeSnapshot(0, 0, 0, 0, [](int x, int y) { return x == 5 && y == 5; });
        CHECK(ai::PlanPath(snapshot, 0, 0, 5, 5).outcome ==
              ai::AStarResult::Outcome::Unreachable);
    }
}

TEST_CASE("M4-T6 A*：展开预算截断") {
    const ai::PathSnapshot snapshot = MakeOpenSnapshot(8, 8);  // 80×80
    ai::AStarOptions options;
    options.maxExpansions = 50;
    const ai::AStarResult result = ai::PlanPath(snapshot, 0, 0, 79, 79, options);
    CHECK(result.outcome == ai::AStarResult::Outcome::BudgetExceeded);
    CHECK(result.expanded >= 50);
}

TEST_CASE("M4-T6 A*：1000+ 格蛇形路径（验收 <5ms）") {
    // 蛇形走廊：偶数行全通；奇数行仅一个连接口（左右交替）→ 路径必须来回折返
    // （70×70 格区域，路径 4000+ 格）
    const ai::PathSnapshot snapshot = MakeSnapshot(0, 0, 6, 6, [](int x, int y) {
        if (y % 2 == 0) {
            return false;  // 偶数行全通
        }
        const int lane = (y / 2) % 2;  // 奇数行连接口：左/右交替
        return x != (lane == 0 ? 69 : 0);
    });
    const ai::AStarResult result = ai::PlanPath(snapshot, 0, 0, 0, 62);
    REQUIRE(result.outcome == ai::AStarResult::Outcome::Found);
    CHECK(result.cells.size() >= 1000u);
    CHECK(result.ms < 5.0);
    MESSAGE("蛇形路径：" << result.cells.size() << " 格，展开 " << result.expanded << " 节点，"
                        << result.ms << " ms");
}

TEST_CASE("M4-T6 PathfindingService：chunk 位图缓存 + 同步模式 + 卸载失效") {
    ai::PathfindingService service(nullptr, ai::MapChunkBounds{0, 0, 2, 2});  // 同步模式
    CHECK(service.CachedChunks() == 0u);

    world::Chunk chunk(0, 0);
    chunk.Square(5, 5, 0).AddObject(world::SquareLayer::Wall, {"wall.brick", 0, 0, true});
    service.OnChunkLoaded(chunk);
    CHECK(service.CachedChunks() == 1u);

    ai::PathResult result;
    const std::uint64_t id = service.Submit(2, 5, 8, 5);
    CHECK(id == 1u);
    REQUIRE(service.TryTakeResult(&result));
    CHECK(result.queryId == id);
    REQUIRE(result.path.outcome == ai::AStarResult::Outcome::Found);
    for (const ai::GridCell& cell : result.path.cells) {
        const bool atWallCell = cell.x == 5 && cell.y == 5;
        CHECK_FALSE(atWallCell);  // 绕开 (5,5) 的单格墙
    }
    CHECK_FALSE(service.TryTakeResult(&result));  // 队列已空
    CHECK(service.GetStats().found == 1u);
    CHECK(service.GetStats().pending == 0);

    service.OnChunkUnloading(0, 0);
    CHECK(service.CachedChunks() == 0u);
    const std::uint64_t id2 = service.Submit(2, 5, 8, 5);
    REQUIRE(service.TryTakeResult(&result));
    CHECK(result.queryId == id2);
    CHECK(result.path.outcome == ai::AStarResult::Outcome::Unreachable);  // 位图已删 → 全阻挡
}

TEST_CASE("M4-T6 PathfindingService：越界即时失败（同步入队）") {
    ai::PathfindingService service(nullptr, ai::MapChunkBounds{0, 0, 2, 2});  // chunk 0..1
    const std::uint64_t id = service.Submit(2, 2, 25, 5);  // 终点在 chunk 2 → 越界
    ai::PathResult result;
    REQUIRE(service.TryTakeResult(&result));
    CHECK(result.queryId == id);
    CHECK(result.path.outcome == ai::AStarResult::Outcome::Invalid);
    CHECK(service.GetStats().invalid == 1u);
    CHECK(service.GetStats().pending == 0);
}

TEST_CASE("M4-T6 全链路：PathRequest → A* → 路点跟随（不穿墙、走缺口、到达）") {
    ai::PathfindingService service(nullptr, ai::MapChunkBounds{0, 0, 2, 2});
    world::Chunk chunk(0, 0);
    for (int ly = 0; ly < core::kChunkSize; ++ly) {
        if (ly == 3) {
            continue;  // 缺口 (5,3)
        }
        chunk.Square(5, ly, 0).AddObject(world::SquareLayer::Wall, {"wall.brick", 0, 0, true});
    }
    service.OnChunkLoaded(chunk);

    ecs::EcsWorld world;
    ai::PathFollowSystem pathFollow(&service);
    characters::MovementSystem movement;

    const ecs::Entity e = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(e, ecs::Position{0.5f, 0.5f, 0});
    registry.emplace<ecs::Velocity>(e, ecs::Velocity{});
    registry.emplace<ecs::MoveIntent>(e, ecs::MoveIntent{});
    registry.emplace<ecs::PathFollow>(e, ecs::PathFollow{});
    ecs::PathRequest request;
    request.goalX = 8;
    request.goalY = 6;
    request.newRequest = true;
    registry.emplace<ecs::PathRequest>(e, request);

    bool throughGap = false;
    bool enteredWall = false;
    bool arrived = false;
    constexpr float dt = 1.0f / 60.0f;
    for (int tick = 0; tick < 1800; ++tick) {
        pathFollow.Update(world, dt);
        movement.Update(world, dt);
        const auto& position = registry.get<ecs::Position>(e);
        const int gx = world::FloorMod(static_cast<int>(std::floor(position.x)), core::kChunkSize);
        const int gy = world::FloorMod(static_cast<int>(std::floor(position.y)), core::kChunkSize);
        if (gx == 5 && gy != 3) {
            enteredWall = true;
        }
        if (gx == 5 && gy == 3) {
            throughGap = true;
        }
        if (registry.get<ecs::PathRequest>(e).state == ecs::PathStatus::Arrived) {
            arrived = true;
            break;
        }
    }

    CHECK_FALSE(enteredWall);
    CHECK(throughGap);
    CHECK(arrived);
    CHECK(registry.get<ecs::PathFollow>(e).status == ecs::PathStatus::Arrived);
    const auto& finalPosition = registry.get<ecs::Position>(e);
    CHECK(finalPosition.x == doctest::Approx(8.5f).epsilon(0.6));
    CHECK(finalPosition.y == doctest::Approx(6.5f).epsilon(0.6));
    CHECK(registry.get<ecs::MoveIntent>(e).x == doctest::Approx(0.0f));
    CHECK(registry.get<ecs::MoveIntent>(e).y == doctest::Approx(0.0f));
}

TEST_CASE("M4-T6 PathFollowSystem：不可达 → Failed + 清路点 + 意图清零") {
    ai::PathfindingService service(nullptr, ai::MapChunkBounds{0, 0, 2, 2});
    world::Chunk chunk(0, 0);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            chunk.Square(5 + dx, 5 + dy, 0)
                .AddObject(world::SquareLayer::Wall, {"wall.brick", 0, 0, true});
        }
    }
    service.OnChunkLoaded(chunk);

    ecs::EcsWorld world;
    ai::PathFollowSystem pathFollow(&service);
    const ecs::Entity e = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(e, ecs::Position{0.5f, 0.5f, 0});
    registry.emplace<ecs::Velocity>(e, ecs::Velocity{});
    registry.emplace<ecs::MoveIntent>(e, ecs::MoveIntent{});
    registry.emplace<ecs::PathFollow>(e, ecs::PathFollow{});
    ecs::PathRequest request;
    request.goalX = 5;  // 被八邻围死
    request.goalY = 5;
    request.newRequest = true;
    registry.emplace<ecs::PathRequest>(e, request);

    pathFollow.Update(world, 1.0f / 60.0f);  // 提交
    CHECK(registry.get<ecs::PathRequest>(e).state == ecs::PathStatus::Pending);
    pathFollow.Update(world, 1.0f / 60.0f);  // 消费结果（同步模式：结果已在队列）
    CHECK(registry.get<ecs::PathRequest>(e).state == ecs::PathStatus::Failed);
    CHECK(registry.get<ecs::PathFollow>(e).status == ecs::PathStatus::Failed);
    CHECK(registry.get<ecs::PathFollow>(e).waypoints.empty());
    CHECK(registry.get<ecs::MoveIntent>(e).x == doctest::Approx(0.0f));
    CHECK(registry.get<ecs::MoveIntent>(e).y == doctest::Approx(0.0f));
}

TEST_CASE("M4-T6 PathFollowSystem：手动输入取消寻路（手动优先）") {
    ai::PathfindingService service(nullptr, ai::MapChunkBounds{0, 0, 2, 2});
    ecs::EcsWorld world;
    ai::PathFollowSystem pathFollow(&service);
    const ecs::Entity e = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(e, ecs::Position{1.5f, 1.5f, 0});
    registry.emplace<ecs::Velocity>(e, ecs::Velocity{});
    registry.emplace<ecs::MoveIntent>(e, ecs::MoveIntent{});
    registry.emplace<ecs::LocalControl>(e, ecs::LocalControl{});
    ecs::PathFollow follow;
    follow.waypoints.push_back(ecs::Waypoint{5.5f, 1.5f});
    follow.status = ecs::PathStatus::Following;
    registry.emplace<ecs::PathFollow>(e, follow);
    ecs::PathRequest request;
    request.goalX = 5;
    request.goalY = 1;
    request.state = ecs::PathStatus::Following;
    registry.emplace<ecs::PathRequest>(e, request);

    // 模拟 InputSystem 本 tick 已写输入意图（PathFollowSystem 在其后执行）
    registry.get<ecs::MoveIntent>(e) = ecs::MoveIntent{1.0f, 0.0f};
    pathFollow.Update(world, 1.0f / 60.0f);

    CHECK(registry.get<ecs::PathFollow>(e).status == ecs::PathStatus::None);
    CHECK(registry.get<ecs::PathFollow>(e).waypoints.empty());
    CHECK(registry.get<ecs::PathRequest>(e).state == ecs::PathStatus::None);
    CHECK(registry.get<ecs::MoveIntent>(e).x == doctest::Approx(1.0f));  // 输入意图原样保留
    CHECK(registry.get<ecs::MoveIntent>(e).y == doctest::Approx(0.0f));
}

TEST_CASE("M4-T7 passability：FindNearestWalkable 螺旋环序") {
    SUBCASE("自身可走直接返回") {
        int x = 0;
        int y = 0;
        CHECK(ai::FindNearestWalkable(7, 9, 4, [](int, int) { return true; }, &x, &y));
        CHECK(x == 7);
        CHECK(y == 9);
    }
    SUBCASE("中心九宫格阻挡 → 第二环首命中（确定性）") {
        const auto walkable = [](int gx, int gy) {
            return std::abs(gx - 5) > 1 || std::abs(gy - 5) > 1;
        };
        int x = 0;
        int y = 0;
        CHECK(ai::FindNearestWalkable(5, 5, 4, walkable, &x, &y));
        CHECK(x == 3);  // r=2 环按 dy、dx 升序 → 首个 (3,3)
        CHECK(y == 3);
    }
    SUBCASE("半径不够 → false") {
        int x = 0;
        int y = 0;
        CHECK_FALSE(ai::FindNearestWalkable(5, 5, 1, [](int, int) { return false; }, &x, &y));
    }
}

TEST_CASE("M4-T7 PathFollowSystem：卡住 → 自动重寻路（1.2s 窗口）") {
    ai::PathfindingService service(nullptr, ai::MapChunkBounds{0, 0, 2, 2});
    world::Chunk chunk(0, 0);  // 全空 = 全可走
    service.OnChunkLoaded(chunk);

    ecs::EcsWorld world;
    ai::PathFollowSystem pathFollow(&service);
    const ecs::Entity e = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(e, ecs::Position{1.5f, 1.5f, 0});
    registry.emplace<ecs::Velocity>(e, ecs::Velocity{});
    registry.emplace<ecs::MoveIntent>(e, ecs::MoveIntent{});
    registry.emplace<ecs::PathFollow>(e, ecs::PathFollow{});
    ecs::PathRequest request;
    request.goalX = 8;
    request.goalY = 8;
    request.newRequest = true;
    registry.emplace<ecs::PathRequest>(e, request);

    constexpr float dt = 1.0f / 60.0f;
    SUBCASE("原地不动 → 触发一次重寻路") {
        for (int tick = 0; tick < 100; ++tick) {  // 1.67s
            pathFollow.Update(world, dt);
        }
        CHECK(service.GetStats().submitted >= 2u);  // 初次提交 + 卡住重寻路
        CHECK(service.GetStats().submitted <= 3u);  // 但不该每 tick 刷查询
        CHECK(registry.get<ecs::PathFollow>(e).status == ecs::PathStatus::Following);
    }
    SUBCASE("持续前进 → 不触发") {
        for (int tick = 0; tick < 100; ++tick) {
            pathFollow.Update(world, dt);
            registry.get<ecs::Position>(e).x += 0.05f;  // 0.05 格/tick > 阈值
        }
        CHECK(service.GetStats().submitted == 1u);
    }
}

TEST_CASE("M4-T6 PathfindingService：后台线程异步查询（真实线程池）") {
    core::TaskScheduler tasks(2);
    ai::PathfindingService service(&tasks, ai::MapChunkBounds{0, 0, 2, 2});
    world::Chunk chunk(0, 0);
    for (int ly = 0; ly < core::kChunkSize; ++ly) {
        chunk.Square(5, ly, 0).AddObject(world::SquareLayer::Wall, {"wall.brick", 0, 0, true});
    }
    service.OnChunkLoaded(chunk);
    world::Chunk southChunk(0, 1);  // 南侧空 chunk：绕行通道（整列墙只能从 y≥10 绕过）
    service.OnChunkLoaded(southChunk);

    const std::uint64_t id = service.Submit(1, 5, 8, 5);  // 整列墙 → 需绕行
    ai::PathResult result;
    bool taken = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (service.TryTakeResult(&result)) {
            taken = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(taken);
    CHECK(result.queryId == id);
    CHECK(result.path.outcome == ai::AStarResult::Outcome::Found);
    bool southDetour = false;
    for (const ai::GridCell& cell : result.path.cells) {
        if (cell.y >= 10) {
            southDetour = true;
        }
    }
    CHECK(southDetour);
    CHECK(service.GetStats().found >= 1u);
    CHECK(service.GetStats().pending == 0);
}
