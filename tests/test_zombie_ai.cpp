#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "ai/Passability.h"
#include "ai/PathfindingService.h"
#include "ai/components/AiComponents.h"
#include "ai/systems/AiSystem.h"
#include "ai/systems/PathFollowSystem.h"
#include "characters/components/CharacterComponents.h"
#include "characters/systems/MovementSystem.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"
#include "world/WorldCoords.h"

using namespace zl;

namespace {

constexpr float kDt = 1.0f / 60.0f;

// 无头全链：AiSystem → PathFollowSystem（同步寻路）→ MovementSystem。
// 测试世界 = 3×3 chunk 平地（可在构造后往 chunk(0,0) 加墙）。
struct AiRig {
    std::unordered_map<std::uint64_t, ai::ChunkPassability> pass;  // 测试用"位图缓存"
    ai::PathfindingService service{nullptr, ai::MapChunkBounds{0, 0, 3, 3}};  // 同步模式
    ecs::EcsWorld world;
    ai::AiSystem aiSystem;
    ai::PathFollowSystem followSystem;
    characters::MovementSystem movement;

    AiRig()
        : aiSystem([this](int wx, int wy) {
              if (wx < 0 || wy < 0 || wx >= 30 || wy >= 30) {
                  return false;  // 测试地图边界外 = 不可走
              }
              const world::ChunkCoord cc = world::ChunkOf(wx, wy);
              const world::SquareLocal local = world::SquareLocalOf(wx, wy);
              const auto it = pass.find(Key(cc.cx, cc.cy));
              if (it == pass.end()) {
                  return false;  // 未加载 = 不可走（与 PathSnapshot 语义一致）
              }
              return !it->second.IsBlocked(local.lx, local.ly);
          }),
          followSystem(&service) {
        for (int cy = 0; cy < 3; ++cy) {
            for (int cx = 0; cx < 3; ++cx) {
                world::Chunk chunk(cx, cy);  // 平地
                pass.emplace(Key(cx, cy), ai::BuildChunkPassability(chunk));
                service.OnChunkLoaded(chunk);
            }
        }
    }

    static std::uint64_t Key(int cx, int cy) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32) |
               static_cast<std::uint32_t>(cy);
    }

    ecs::Entity AddZombie(float x, float y) {
        entt::registry& r = world.Registry();
        const ecs::Entity e = world.Create();
        r.emplace<ecs::Position>(e, ecs::Position{x, y, 0});
        r.emplace<ecs::Velocity>(e, ecs::Velocity{});
        r.emplace<ecs::Collider>(e, ecs::Collider{});
        r.emplace<ecs::Health>(e, ecs::Health{100.0f, 100.0f});
        r.emplace<ecs::AiState>(e, ecs::AiState{});
        ecs::PerceptionState percep;
        percep.lastKnownX = x;  // 初始可疑点 = 出生位置（与演示一致）
        percep.lastKnownY = y;
        r.emplace<ecs::PerceptionState>(e, percep);
        r.emplace<ecs::MoveIntent>(e, ecs::MoveIntent{});
        r.emplace<ecs::PathRequest>(e, ecs::PathRequest{});
        r.emplace<ecs::PathFollow>(e, ecs::PathFollow{});
        return e;
    }

    ecs::Entity AddTarget(float x, float y) {
        const ecs::Entity e = world.Create();
        world.Registry().emplace<ecs::Position>(e, ecs::Position{x, y, 0});
        return e;
    }

    void Tick() {
        aiSystem.Update(world, kDt);
        followSystem.Update(world, kDt);
        movement.Update(world, kDt);
        world.Flush();
    }
    void Run(int ticks) {
        for (int i = 0; i < ticks; ++i) {
            Tick();
        }
    }

    ecs::Position& Pos(ecs::Entity e) { return world.Registry().get<ecs::Position>(e); }
    ecs::AiState& Ai(ecs::Entity e) { return world.Registry().get<ecs::AiState>(e); }
    ecs::PerceptionState& Percep(ecs::Entity e) {
        return world.Registry().get<ecs::PerceptionState>(e);
    }
    ecs::PathRequest& Request(ecs::Entity e) {
        return world.Registry().get<ecs::PathRequest>(e);
    }
    ecs::PathFollow& Follow(ecs::Entity e) { return world.Registry().get<ecs::PathFollow>(e); }
};

float Dist(float ax, float ay, float bx, float by) {
    const float dx = ax - bx;
    const float dy = ay - by;
    return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

TEST_CASE("AiSystem：Idle→Wander 自主循环（时长随机化 + 游荡目标可走）") {
    AiRig rig;
    const ecs::Entity z = rig.AddZombie(5.5f, 5.5f);
    rig.Ai(z).idleSeconds = 0.4f;

    rig.Run(21);  // 0.35s
    CHECK(rig.Ai(z).status == ecs::AiStatus::Idle);

    rig.Run(10);  // 累计 0.52s → 超过 idleSeconds
    CHECK(rig.Ai(z).status == ecs::AiStatus::Wander);
    const ecs::PathRequest& request = rig.Request(z);
    CHECK(request.state != ecs::PathStatus::None);  // 已提交寻路
    CHECK(request.goalX >= 0);
    CHECK(request.goalY >= 0);
    CHECK(request.goalX < 30);
    CHECK(request.goalY < 30);
    const float goalDist = Dist(static_cast<float>(request.goalX) + 0.5f,
                                static_cast<float>(request.goalY) + 0.5f, 5.5f, 5.5f);
    CHECK(goalDist >= 2.0f);   // 不会原地打转（半径下限 3，就近校正后仍应离出生点有距离）
    CHECK(goalDist <= 14.0f);  // 半径上限 10 + 校正余量

    // 模拟走完（引擎路径状态置 Arrived）→ 下一 tick 回 Idle，且 idleSeconds 重新随机
    rig.Follow(z).status = ecs::PathStatus::Arrived;
    rig.Request(z).state = ecs::PathStatus::Arrived;
    rig.Tick();
    CHECK(rig.Ai(z).status == ecs::AiStatus::Idle);
    CHECK(rig.Ai(z).idleSeconds >= 2.0f);
    CHECK(rig.Ai(z).idleSeconds <= 6.0f);

    // 再跑一轮循环：还会重新 Wander（周期行为稳定）
    rig.Run(6 * 60);
    const bool cycleOk = rig.Ai(z).status == ecs::AiStatus::Wander ||
                         rig.Ai(z).status == ecs::AiStatus::Idle;
    CHECK(cycleOk);
    CHECK(rig.Ai(z).status != ecs::AiStatus::Dead);
}

TEST_CASE("AiSystem：Chase→Attack→Search→Idle 转移链（含攻击节拍与迟滞）") {
    AiRig rig;
    const ecs::Entity z = rig.AddZombie(5.5f, 5.5f);
    const ecs::Entity target = rig.AddTarget(7.0f, 5.5f);
    const std::uint64_t targetId = rig.world.IdOf(target).id;
    rig.Ai(z).idleSeconds = 100.0f;  // 不主动 Wander，排除干扰

    // 感知到目标（距离 1.5 > 攻击距离 1.0）→ Chase，并请求走向目标
    // （真实感知由 PerceptionSystem 负责；本测试直接写感知输出，聚焦 AI 转移逻辑）
    rig.Ai(z).targetId = targetId;
    rig.Percep(z).lastKnownX = 7.0f;
    rig.Percep(z).lastKnownY = 5.5f;
    rig.Tick();
    CHECK(rig.Ai(z).status == ecs::AiStatus::Chase);
    CHECK(rig.Request(z).goalX == 7);  // 走向最后已知位置
    CHECK(rig.Request(z).goalY == 5);
    CHECK(rig.Request(z).state != ecs::PathStatus::None);

    // 拉近到攻击距离（0.4）→ Attack；意图清零、朝向面向目标
    rig.Pos(target).x = 5.9f;
    rig.Tick();
    CHECK(rig.Ai(z).status == ecs::AiStatus::Attack);
    CHECK(rig.world.Registry().get<ecs::MoveIntent>(z).x == 0.0f);
    CHECK(rig.world.Registry().get<ecs::MoveIntent>(z).y == 0.0f);

    // 攻击节拍：每 0.8s 一次脉冲
    rig.Run(55);  // 0.92s
    CHECK(rig.Ai(z).attackPulses >= 2);
    CHECK(rig.aiSystem.GetStats().swings >= 2);

    // 目标拉远（> keep 1.5）→ 回 Chase
    rig.Pos(target).x = 8.0f;  // 距离 2.5
    rig.Percep(z).lastKnownX = 8.0f;
    rig.Tick();
    CHECK(rig.Ai(z).status == ecs::AiStatus::Chase);

    // 目标消失（感知剥离）→ SearchLastKnown（去最后已知位置）
    rig.Ai(z).targetId = 0;
    rig.Tick();
    CHECK(rig.Ai(z).status == ecs::AiStatus::SearchLastKnown);
    CHECK(rig.Request(z).state != ecs::PathStatus::None);  // 已请求去最后已知位置

    // 搜索超时（6s）→ Idle
    rig.Run(6 * 60 + 5);
    CHECK(rig.Ai(z).status == ecs::AiStatus::Idle);
}

TEST_CASE("AiSystem：Chase 全链真走路（PathFollow+Movement 集成到攻击距离）") {
    AiRig rig;
    const ecs::Entity z = rig.AddZombie(5.5f, 5.5f);
    const ecs::Entity target = rig.AddTarget(14.5f, 5.5f);
    rig.Ai(z).targetId = rig.world.IdOf(target).id;
    rig.Percep(z).lastKnownX = 14.5f;
    rig.Percep(z).lastKnownY = 5.5f;
    rig.Ai(z).idleSeconds = 100.0f;

    rig.Run(30);  // 0.5s：应已进入 Chase 并在移动
    CHECK(rig.Ai(z).status == ecs::AiStatus::Chase);
    CHECK(rig.Pos(z).x > 5.5f);

    rig.Run(9 * 60);  // 再跑 9s（2 格/s 足够走完 9 格）
    const float dist = Dist(rig.Pos(z).x, rig.Pos(z).y, 14.5f, 5.5f);
    CHECK(dist <= 1.6f);  // 已贴到攻击距离（含轻微过冲）
    CHECK(rig.Ai(z).status == ecs::AiStatus::Attack);
    CHECK(rig.Pos(z).y == doctest::Approx(5.5f).epsilon(0.3));  // 直线走，不偏航
}

TEST_CASE("AiSystem：目标移动触发追击重定向（goal 跟随 lastKnown）") {
    AiRig rig;
    const ecs::Entity z = rig.AddZombie(5.5f, 5.5f);
    const ecs::Entity target = rig.AddTarget(12.5f, 5.5f);
    rig.Ai(z).targetId = rig.world.IdOf(target).id;
    rig.Percep(z).lastKnownX = 12.5f;
    rig.Percep(z).lastKnownY = 5.5f;
    rig.Ai(z).idleSeconds = 100.0f;

    rig.Run(10);
    CHECK(rig.Ai(z).status == ecs::AiStatus::Chase);
    CHECK(rig.Request(z).goalX == 12);  // 先追向 (12.5, 5.5) → 目标格 (12, 5)
    CHECK(rig.Request(z).goalY == 5);

    // 目标横移 4 格（> 容忍 1.2）→ 下一 tick 重寻路到新位置（感知同步刷新 lastKnown）
    rig.Pos(target).y = 9.5f;
    rig.Percep(z).lastKnownY = 9.5f;
    rig.Run(3);
    CHECK(rig.Request(z).goalY == 9);
    CHECK(rig.Request(z).goalX == 12);

    // 目标消失但 lastKnown 保留 → Search 巡点在最后已知位置附近
    rig.Ai(z).targetId = 0;
    rig.Tick();
    CHECK(rig.Ai(z).status == ecs::AiStatus::SearchLastKnown);
    CHECK(rig.Percep(z).lastKnownY == doctest::Approx(9.5f));
    rig.Run(30);
    const float searchGoalDist = Dist(static_cast<float>(rig.Request(z).goalX) + 0.5f,
                                      static_cast<float>(rig.Request(z).goalY) + 0.5f,
                                      rig.Percep(z).lastKnownX, rig.Percep(z).lastKnownY);
    CHECK(searchGoalDist <= 8.0f);  // 巡点半径 4 + 就近校正余量
}

TEST_CASE("AiSystem：Health 归零 → Dead 冻结（不再写意图/请求）") {
    AiRig rig;
    const ecs::Entity z = rig.AddZombie(5.5f, 5.5f);
    rig.Ai(z).idleSeconds = 100.0f;

    rig.world.Registry().get<ecs::Health>(z).current = 0.0f;
    rig.Tick();
    CHECK(rig.Ai(z).status == ecs::AiStatus::Dead);
    CHECK(rig.world.Registry().get<ecs::MoveIntent>(z).x == 0.0f);
    CHECK(rig.Request(z).state == ecs::PathStatus::None);

    rig.Request(z).newRequest = false;
    rig.Run(120);
    CHECK(rig.Ai(z).status == ecs::AiStatus::Dead);
    CHECK_FALSE(rig.Request(z).newRequest);  // 死了不再要路
    CHECK(rig.aiSystem.GetStats().dead == 1);
}
