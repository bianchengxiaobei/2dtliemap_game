#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include "ai/VirtualHorde.h"
#include "ai/components/AiComponents.h"
#include "ai/systems/AiSystem.h"
#include "ai/systems/BoidsSystem.h"
#include "ai/systems/LodSystem.h"
#include "ai/systems/PerceptionSystem.h"
#include "ai/systems/SpawnLodSystem.h"
#include "characters/components/CharacterComponents.h"
#include "characters/systems/CombatSystem.h"
#include "characters/systems/MovementSystem.h"
#include "ecs/EcsWorld.h"
#include "ecs/SpatialIndex.h"
#include "ecs/components/CommonComponents.h"

using namespace zl;

namespace {

constexpr float kDt = 1.0f / 60.0f;

struct BoidsRig {
    ecs::EcsWorld world;
    ai::BoidsSystem boids;

    ecs::Entity Spawn(float x, float y, float ix = 0.0f, float iy = 0.0f, float speed = 2.0f) {
        entt::registry& r = world.Registry();
        const ecs::Entity e = world.Create();
        r.emplace<ecs::Position>(e, ecs::Position{x, y, 0});
        r.emplace<ecs::Velocity>(e, ecs::Velocity{0.0f, 0.0f});
        r.emplace<ecs::Health>(e, ecs::Health{100.0f, 100.0f});
        r.emplace<ecs::AiState>(e, ecs::AiState{});
        r.emplace<ecs::MoveIntent>(e, ecs::MoveIntent{ix, iy});
        ecs::PathFollow follow;
        follow.speed = speed;
        r.emplace<ecs::PathFollow>(e, follow);
        return e;
    }

    void Tick() { boids.Update(world); }
    ecs::MoveIntent& Intent(ecs::Entity e) { return world.Registry().get<ecs::MoveIntent>(e); }
};

float Len(const ecs::MoveIntent& v) {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

}  // namespace

TEST_CASE("Boids：分离——过近僵尸被推开（方向正确、确定性重叠方向）") {
    BoidsRig rig;
    const ecs::Entity a = rig.Spawn(5.5f, 5.5f);
    const ecs::Entity b = rig.Spawn(5.9f, 5.5f);  // 距 0.4 < 0.8（分离半径）

    rig.Tick();
    // a 在左 → 被推向左（x<0）；b 在右 → 被推向右（x>0）
    CHECK(rig.Intent(a).x < 0.0f);
    CHECK(rig.Intent(b).x > 0.0f);
    CHECK(rig.boids.GetStats().separated == 2);

    // 精确重叠：确定性方向（同 StableId 每次结果一致）且严格相背
    BoidsRig overlap;
    const ecs::Entity c = overlap.Spawn(7.5f, 7.5f);
    const ecs::Entity d = overlap.Spawn(7.5f, 7.5f);
    overlap.Tick();
    const float cx1 = overlap.Intent(c).x;
    const float cy1 = overlap.Intent(c).y;
    CHECK(Len(overlap.Intent(c)) > 0.0f);
    CHECK(Len(overlap.Intent(d)) > 0.0f);
    const float dot = overlap.Intent(c).x * overlap.Intent(d).x +
                      overlap.Intent(c).y * overlap.Intent(d).y;
    CHECK(dot < 0.0f);
    overlap.Tick();  // 再跑一 tick：方向仍一致（叉积 ≈ 0，不抖动）
    const float cross = cx1 * overlap.Intent(c).y - cy1 * overlap.Intent(c).x;
    CHECK(std::abs(cross) < 0.01f);
}

TEST_CASE("Boids：聚合/对齐仅作用于移动中实体（原地僵尸不被唤醒）") {
    // 站立的僵尸：附近有同伴但不在分离半径内 → 意图保持 0
    BoidsRig stand;
    const ecs::Entity s = stand.Spawn(5.5f, 5.5f);
    stand.Spawn(7.0f, 5.5f);  // 距 1.5（sep<1.5<neigh）→ 只触发聚合候选
    stand.Tick();
    CHECK(Len(stand.Intent(s)) == 0.0f);

    // 移动中的僵尸：右上 2.1 格有同伴 → 意图获得朝同伴的分量，速度不超上限
    BoidsRig mov;
    const ecs::Entity m = mov.Spawn(5.5f, 5.5f, 2.0f, 0.0f, /*speed=*/2.0f);
    mov.Spawn(7.0f, 7.0f);  // 在 neighborRadius 内（2.5），不在分离半径
    mov.Tick();
    const ecs::MoveIntent& mi = mov.Intent(m);
    CHECK(mi.x > 0.0f);            // 仍朝右
    CHECK(std::abs(mi.y) > 0.0f);  // 且被拉向右上同伴（聚合/对齐）
    CHECK(Len(mi) <= 2.0f + 0.001f);
}

TEST_CASE("Boids：意图速度截断到 PathFollow.speed") {
    BoidsRig rig;
    const ecs::Entity center = rig.Spawn(5.5f, 5.5f, 2.0f, 0.0f, /*speed=*/1.5f);
    // 四周塞满贴身邻居 → 分离向量很大
    rig.Spawn(5.5f, 5.5f);
    rig.Spawn(5.6f, 5.5f);
    rig.Spawn(5.4f, 5.5f);
    rig.Spawn(5.5f, 5.6f);
    rig.Spawn(5.5f, 5.4f);
    rig.Tick();
    CHECK(Len(rig.Intent(center)) <= 1.5f + 0.001f);
}

TEST_CASE("Boids：邻域以网格覆盖（分离半径外的大列表不误伤）") {
    BoidsRig rig;
    const ecs::Entity a = rig.Spawn(5.5f, 5.5f);
    rig.Spawn(5.5f, 9.5f);  // 距 4 格：分离/聚合均不触发
    rig.Tick();
    CHECK(Len(rig.Intent(a)) == 0.0f);
    CHECK(rig.boids.GetStats().separated == 0);
    CHECK(rig.boids.GetStats().zombies == 2);
}

TEST_CASE("LodSystem：三档距离划分 + 30Hz/10Hz 节奏 + dtScale") {
    ecs::EcsWorld world;
    ai::LodSystem lod;  // 默认 full=25 / mid=60
    lod.SetFocus(0.0f, 0.0f);
    entt::registry& r = world.Registry();
    const ecs::Entity full = world.Create();
    r.emplace<ecs::Position>(full, ecs::Position{10.0f, 0.0f, 0});
    r.emplace<ecs::LodState>(full, ecs::LodState{});
    const ecs::Entity mid = world.Create();
    r.emplace<ecs::Position>(mid, ecs::Position{40.0f, 0.0f, 0});
    r.emplace<ecs::LodState>(mid, ecs::LodState{});
    const ecs::Entity distant = world.Create();
    r.emplace<ecs::Position>(distant, ecs::Position{100.0f, 0.0f, 0});
    r.emplace<ecs::LodState>(distant, ecs::LodState{});

    // tick 0：三档全更新
    lod.Update(world);
    CHECK(r.get<ecs::LodState>(full).tier == 0);
    CHECK(r.get<ecs::LodState>(mid).tier == 1);
    CHECK(r.get<ecs::LodState>(distant).tier == 2);
    CHECK(lod.GetStats().fullCount == 1);
    CHECK(lod.GetStats().midCount == 1);
    CHECK(lod.GetStats().farCount == 1);
    CHECK(r.get<ecs::LodState>(mid).dtScale == doctest::Approx(2.0f));
    CHECK(r.get<ecs::LodState>(distant).dtScale == doctest::Approx(6.0f));

    // tick 1..6：记录 updateThisTick 节奏
    std::vector<bool> midPattern{true};
    std::vector<bool> farPattern{true};
    for (int i = 0; i < 6; ++i) {
        lod.Update(world);
        midPattern.push_back(r.get<ecs::LodState>(mid).updateThisTick);
        farPattern.push_back(r.get<ecs::LodState>(distant).updateThisTick);
        CHECK(r.get<ecs::LodState>(full).updateThisTick);
    }
    // mid：每 2 tick（偶数更新）；far：每 6 tick
    CHECK(midPattern[2] == true);
    CHECK(midPattern[3] == false);
    CHECK(midPattern[4] == true);
    CHECK(farPattern[5] == false);
    CHECK(farPattern[6] == true);  // 累计 6 次 Update 后的下一个 6 倍数

    // 焦点移动 → 档位即时重评
    lod.SetFocus(100.0f, 0.0f);
    lod.Update(world);
    CHECK(r.get<ecs::LodState>(distant).tier == 0);
    CHECK(r.get<ecs::LodState>(full).tier == 2);  // 距 90 > 60 → 10Hz 档
    CHECK(lod.GetStats().fullCount == 1);
}

TEST_CASE("LOD 门控：AiSystem 冻结转移但计时补齐（stateTime 与全速一致）") {
    ecs::EcsWorld world;
    ai::LodSystem lod;
    lod.SetFocus(0.0f, 0.0f);
    ai::AiSystem ai([](int, int) { return true; });

    entt::registry& r = world.Registry();
    // 两个僵尸：一个焦点附近（全速），一个远处（10Hz）——仅远者挂 LodState
    const ecs::Entity nearEntity = world.Create();
    r.emplace<ecs::Position>(nearEntity, ecs::Position{5.0f, 5.0f, 0});
    r.emplace<ecs::AiState>(nearEntity, ecs::AiState{});
    r.get<ecs::AiState>(nearEntity).idleSeconds = 100.0f;
    const ecs::Entity distant = world.Create();
    r.emplace<ecs::Position>(distant, ecs::Position{100.0f, 5.0f, 0});
    r.emplace<ecs::AiState>(distant, ecs::AiState{});
    r.get<ecs::AiState>(distant).idleSeconds = 100.0f;
    r.emplace<ecs::LodState>(distant, ecs::LodState{});

    constexpr int kTicks = 60;
    for (int i = 0; i < kTicks; ++i) {
        lod.Update(world);
        ai.Update(world, kDt);
    }
    // 远者的状态计时与全速一致（冻结 tick 以 dtScale 补齐）
    CHECK(r.get<ecs::AiState>(distant).stateTime ==
          doctest::Approx(r.get<ecs::AiState>(nearEntity).stateTime));
    CHECK(r.get<ecs::AiState>(distant).stateTime == doctest::Approx(kTicks * kDt).epsilon(0.05));
    // 状态分布计数不受门控影响（快照包含冻结实体）
    CHECK(ai.GetStats().idle == 2);
}

TEST_CASE("VirtualHordeStore：只存统计不入 registry + 聚合漂移 + 实体化扣减") {
    ecs::EcsWorld world;  // 伴随世界：任何时刻都不该出现实体
    ai::VirtualHordeStore store;
    const std::uint64_t h1 = store.AddHorde(100.0f, 100.0f, 40, /*spread=*/8.0f);
    const std::uint64_t h2 = store.AddHorde(300.0f, 100.0f, 160);
    CHECK(world.AliveCount() == 0u);  // 虚拟 = 纯统计，不入 registry

    auto stats = store.GetStats(100.0f, 100.0f);
    CHECK(stats.hordes == 2);
    CHECK(stats.zombies == 200);
    CHECK(stats.nearestDist == doctest::Approx(0.0f));

    // 聚合漂移：朝吸引点移动，到驻留距离停住
    store.Update(1.0f, 0.0f, 100.0f, /*stop=*/20.0f);
    stats = store.GetStats(0.0f, 100.0f);
    CHECK(stats.nearestDist == doctest::Approx(100.0f - 1.2f));  // speed=1.2 格/s
    for (int i = 0; i < 1000; ++i) {
        store.Update(0.1f, 0.0f, 100.0f, 20.0f);
    }
    stats = store.GetStats(0.0f, 100.0f);
    CHECK(stats.nearestDist == doctest::Approx(20.0f).epsilon(0.02));  // 停在驻留距离

    // 实体化扣减：部分扣 / 扣空移除
    CHECK(store.TakeZombies(h1, 15) == 15);
    CHECK(store.GetStats(0.0f, 0.0f).zombies == 185);
    CHECK(store.TakeZombies(h1, 100) == 25);  // 只剩 25
    CHECK(store.GetStats(0.0f, 0.0f).hordes == 1);
    CHECK(store.TakeZombies(h1, 1) == 0);  // 已移除
    CHECK(store.RemoveHorde(h2));
    CHECK(store.GetStats(0.0f, 0.0f).hordes == 0);
    CHECK(world.AliveCount() == 0u);
}

// M5 验收基准：500 个 registry 僵尸（含 LOD 门控）+ 200 虚拟僵尸（群落统计），
// 主线程模拟链全口径（空间索引 → LOD → 感知 → AI → 群体行为 → 战斗 → 虚拟群落漂移 →
// 移动 → 刷怪/虚拟化扫描），不含物理步进与寻路后台（docs/02 §6 预算口径："模拟 tick < 4ms"）。
// 稳态：僵尸全在 virtualizeRadius 内、群落全在 materializeRadius 外、总量超目标 →
// SpawnLod/Combat 每 tick 只做扫描与衰减（转换代价由 test_spawn_lod 单测覆盖）。
TEST_CASE("M5 基准：500 僵尸 + 200 虚拟化的模拟 tick 计时") {
    ecs::EcsWorld world;
    ecs::SpatialIndex index;
    ai::LodSystem lod;
    ai::PerceptionSystem perception([](int, int) { return true; });
    ai::AiSystem ai([](int, int) { return true; });
    ai::BoidsSystem boids;
    characters::CombatSystem combat;
    characters::MovementSystem movement;
    // 200 虚拟僵尸：4 群落 × 50，统计层漂移（声明先于 SpawnLodSystem —— 它持有引用）
    ai::VirtualHordeStore virtualHordes;
    for (int i = 0; i < 4; ++i) {
        virtualHordes.AddHorde(60.0f + static_cast<float>(i) * 10.0f, 60.0f, 50);
    }
    // 刷怪/虚拟化（M5-T7）：工厂走最小僵尸构成（无物理）；稳态下不发生转换
    ai::SpawnLodSystem spawnLod(
        [](int, int) { return true; },
        [&](ecs::EcsWorld& w, float px, float py) {
            const ecs::Entity e = w.Create();
            w.Registry().emplace<ecs::Position>(e, ecs::Position{px, py, 0});
            w.Registry().emplace<ecs::Health>(e, ecs::Health{100.0f, 100.0f});
            w.Registry().emplace<ecs::AiState>(e, ecs::AiState{});
            return e;
        },
        [](ecs::EcsWorld&, ecs::Entity) {}, virtualHordes);
    entt::registry& r = world.Registry();

    // 焦点/玩家：地图中心
    const ecs::Entity player = world.Create();
    r.emplace<ecs::Position>(player, ecs::Position{30.0f, 30.0f, 0});
    r.emplace<ecs::PlayerTag>(player);
    lod.SetFocus(30.0f, 30.0f);

    // 500 个僵尸：以玩家为中心撒开（半径 ~14 格，多档 LOD 混合），初始追击玩家
    std::mt19937 rng(20261016u);
    std::uniform_real_distribution<float> angleU(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> radiusU(1.0f, 14.0f);
    for (int i = 0; i < 500; ++i) {
        const float a = angleU(rng);
        const float rad = radiusU(rng);
        const float px = 30.0f + std::cos(a) * rad;
        const float py = 30.0f + std::sin(a) * rad;
        const ecs::Entity e = world.Create();
        r.emplace<ecs::Position>(e, ecs::Position{px, py, 0});
        r.emplace<ecs::Velocity>(e, ecs::Velocity{});
        r.emplace<ecs::Health>(e, ecs::Health{100.0f, 100.0f});
        r.emplace<ecs::AiState>(e, ecs::AiState{});
        ecs::PerceptionState percep;
        percep.lastKnownX = px;
        percep.lastKnownY = py;
        r.emplace<ecs::PerceptionState>(e, percep);
        r.emplace<ecs::MoveIntent>(e, ecs::MoveIntent{});
        r.emplace<ecs::PathRequest>(e, ecs::PathRequest{});
        ecs::PathFollow follow;
        follow.speed = 2.0f;
        r.emplace<ecs::PathFollow>(e, follow);
        r.emplace<ecs::LodState>(e, ecs::LodState{});
    }

    constexpr int kTicks = 120;
    double tIndex = 0.0;
    double tLod = 0.0;
    double tPerception = 0.0;
    double tAi = 0.0;
    double tBoids = 0.0;
    double tCombat = 0.0;
    double tMovement = 0.0;
    double tVirtual = 0.0;
    double tSpawn = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kTicks; ++i) {
        auto mark = [] { return std::chrono::steady_clock::now(); };
        auto acc = [&](double& slot, const std::chrono::steady_clock::time_point& a) {
            slot += std::chrono::duration<double, std::milli>(mark() - a).count();
        };
        auto a = mark();
        index.Update(world);
        acc(tIndex, a);
        a = mark();
        lod.Update(world);
        acc(tLod, a);
        a = mark();
        perception.Update(world, kDt);
        acc(tPerception, a);
        a = mark();
        ai.Update(world, kDt);
        acc(tAi, a);
        a = mark();
        boids.Update(world);
        acc(tBoids, a);
        a = mark();
        combat.Update(world, kDt);
        acc(tCombat, a);
        a = mark();
        virtualHordes.Update(kDt, 30.0f, 30.0f, 12.0f);
        acc(tVirtual, a);
        a = mark();
        movement.Update(world, kDt);
        acc(tMovement, a);
        a = mark();
        spawnLod.SetFocus(30.0f, 30.0f);
        spawnLod.Update(world, kDt);
        acc(tSpawn, a);
        world.Flush();
    }
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const double avgMs = elapsedMs / kTicks;
    const auto lodStats = lod.GetStats();
    const auto spawnStats = spawnLod.GetStats();
    const double per = 1.0 / kTicks;
    std::printf("[bench] M5 模拟 tick：500 僵尸 + 200 虚拟 avg=%.3f ms/tick"
                "（index=%.3f lod=%.3f perception=%.3f ai=%.3f boids=%.3f combat=%.3f"
                " virtual=%.3f movement=%.3f spawnlod=%.3f；"
                "LOD full=%d mid=%d far=%d updated=%d chase=%d；"
                "spawn live=%d virtualZ=%d 转换=%llu/%llu）\n",
                avgMs, tIndex * per, tLod * per, tPerception * per, tAi * per, tBoids * per,
                tCombat * per, tVirtual * per, tMovement * per, tSpawn * per, lodStats.fullCount,
                lodStats.midCount, lodStats.farCount, lodStats.updated, ai.GetStats().chase,
                spawnStats.live, spawnStats.virtualZombies,
                static_cast<unsigned long long>(spawnStats.materialized),
                static_cast<unsigned long long>(spawnStats.virtualized));
    CHECK(spawnStats.materialized == 0u);  // 稳态：无转换（群落都在 materializeRadius 外）
    CHECK(spawnStats.virtualized == 0u);   // 实体都在 virtualizeRadius 内
    CHECK(avgMs < 40.0);                   // 宽松哨兵（Debug 也远达不到）
#ifdef NDEBUG
    CHECK(avgMs < 4.0);  // docs/02 预算：模拟 tick < 4ms（Release）
#endif
}
