#include <doctest/doctest.h>

#include <cmath>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "ai/VirtualHorde.h"
#include "ai/components/AiComponents.h"
#include "ai/systems/SpawnLodSystem.h"
#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"

using namespace zl;

namespace {

constexpr float kDt = 1.0f / 60.0f;

float Dist(float ax, float ay, float bx, float by) {
    const float dx = ax - bx;
    const float dy = ay - by;
    return std::sqrt(dx * dx + dy * dy);
}

// 无头测试装置：无物理（despawn 为空钩子）；工厂记录调用位置并生成最小僵尸实体。
struct SpawnRig {
    ecs::EcsWorld world;
    ai::VirtualHordeStore hordes;
    std::function<bool(int, int)> walkable = [](int, int) { return true; };
    std::vector<std::pair<float, float>> spawnPositions;  // 工厂调用记录
    std::vector<ecs::Entity> spawnedEntities;
    ai::SpawnLodSystem::Params params;
    std::unique_ptr<ai::SpawnLodSystem> system;

    explicit SpawnRig(ai::SpawnLodSystem::Params p) : params(p) {
        system = std::make_unique<ai::SpawnLodSystem>(
            [this](int wx, int wy) { return walkable(wx, wy); },
            [this](ecs::EcsWorld& w, float px, float py) {
                spawnPositions.emplace_back(px, py);
                const ecs::Entity e = AddZombie(w, px, py);
                spawnedEntities.push_back(e);
                return e;
            },
            [](ecs::EcsWorld&, ecs::Entity) {},  // 测试无物理体
            hordes, params);
    }

    ecs::Entity AddZombie(ecs::EcsWorld& w, float x, float y) {
        entt::registry& r = w.Registry();
        const ecs::Entity e = w.Create();
        r.emplace<ecs::Position>(e, ecs::Position{x, y, 0});
        r.emplace<ecs::Health>(e, ecs::Health{100.0f, 100.0f});
        r.emplace<ecs::AiState>(e, ecs::AiState{});
        return e;
    }
    ecs::Entity AddZombie(float x, float y) { return AddZombie(world, x, y); }

    void Tick(float dt = kDt, float fx = 0.0f, float fy = 0.0f) {
        system->SetFocus(fx, fy);
        system->Update(world, dt);
        world.Flush();
    }
    void Run(int ticks, float dt = kDt, float fx = 0.0f, float fy = 0.0f) {
        for (int i = 0; i < ticks; ++i) {
            Tick(dt, fx, fy);
        }
    }
};

}  // namespace

TEST_CASE("VirtualHordeStore::AddOrMerge：近距归并 / 超距新建 / 空语义") {
    ai::VirtualHordeStore store;
    CHECK(store.AddOrMerge(10.0f, 10.0f, 5, 6.0f, 15.0f) == 1u);  // 新建
    CHECK(store.AddOrMerge(12.0f, 10.0f, 3, 6.0f, 15.0f) == 1u);  // 距 2 ≤ 15 → 归并
    CHECK(store.Hordes().size() == 1u);
    CHECK(store.Hordes()[0].count == 8);
    CHECK(store.AddOrMerge(100.0f, 100.0f, 2, 6.0f, 15.0f) == 2u);  // 超距 → 新建
    CHECK(store.Hordes().size() == 2u);
    CHECK(store.AddOrMerge(0.0f, 0.0f, 0, 6.0f, 15.0f) == 0u);  // count<=0：无操作
    CHECK(store.Hordes().size() == 2u);
}

TEST_CASE("SpawnLodSystem：群落进 materializeRadius → 按速率实体化 + 扣减 + 出生点吸附") {
    ai::SpawnLodSystem::Params p;
    p.populationTarget = 0;        // 关补给
    p.materializePerSecond = 10.0f;
    p.minSpawnDistance = 0.0f;
    p.virtualizeRadius = 1000.0f;  // 关虚拟化
    SpawnRig rig(p);
    rig.hordes.AddHorde(10.0f, 10.0f, /*count=*/5, /*spread=*/2.0f);

    rig.Run(30);  // 0.5s × 10/s = 5 只（全量）
    CHECK(rig.spawnPositions.size() == 5u);
    CHECK(rig.system->GetStats().materialized == 5u);
    CHECK(rig.system->GetStats().live == 5);
    CHECK(rig.system->GetStats().virtualZombies == 0);
    CHECK(rig.hordes.Hordes().empty());  // 扣空自动移除
    CHECK(rig.world.AliveCount() == 5u);
    for (const auto& pos : rig.spawnPositions) {
        // 群落中心 ± spread（格中心 +0.5 吸附；边界 max 2.5）
        CHECK(std::abs(pos.first - 10.5f) <= 2.5f + 0.01f);
        CHECK(std::abs(pos.second - 10.5f) <= 2.5f + 0.01f);
        CHECK(pos.first == std::floor(pos.first) + 0.5f);  // 吸附到格中心
    }
    // 实体带 AI 全套组件（与工厂口径一致的"可被感知/AI/寻路驱动"）
    int zombieCount = 0;
    rig.world.Registry().view<ecs::AiState, ecs::Health, ecs::Position>().each(
        [&](const ecs::Entity, const ecs::AiState&, const ecs::Health&, const ecs::Position&) {
            ++zombieCount;
        });
    CHECK(zombieCount == 5);
}

TEST_CASE("SpawnLodSystem：实体化受 maxLive 与单 tick 上限双重封顶") {
    ai::SpawnLodSystem::Params p;
    p.populationTarget = 0;
    p.materializePerSecond = 10.0f;
    p.minSpawnDistance = 0.0f;
    p.virtualizeRadius = 1000.0f;
    p.maxLive = 3;
    SpawnRig rig(p);
    rig.hordes.AddHorde(10.0f, 10.0f, 10, 1.0f);
    rig.Run(60);  // 速率容量 10 只，maxLive=3 封顶
    CHECK(rig.system->GetStats().materialized == 3u);
    CHECK(rig.system->GetStats().live == 3);
    CHECK(rig.hordes.GetStats(0.0f, 0.0f).zombies == 7);  // 剩余仍在虚拟层

    // 单 tick 上限（maxOpsPerTick=8）：dt=1s 一 tick 至多 8 只
    ai::SpawnLodSystem::Params q;
    q.populationTarget = 0;
    q.materializePerSecond = 1000.0f;
    q.minSpawnDistance = 0.0f;
    q.virtualizeRadius = 1000.0f;
    SpawnRig rig2(q);
    rig2.hordes.AddHorde(10.0f, 10.0f, 20, 1.0f);
    rig2.Tick(1.0f);
    CHECK(rig2.spawnPositions.size() == 8u);
}

TEST_CASE("SpawnLodSystem：超出 virtualizeRadius 折回统计（归并）+ 回差带不抖动") {
    ai::SpawnLodSystem::Params p;
    p.populationTarget = 0;
    p.materializeRadius = 20.0f;
    p.materializePerSecond = 0.001f;  // 近似关（回差带内的群落不拆）
    p.virtualizeRadius = 40.0f;
    p.virtualizePerSecond = 100.0f;
    SpawnRig rig(p);
    const ecs::Entity far1 = rig.AddZombie(50.0f, 0.0f);  // 半径外 → 虚拟化
    const ecs::Entity far2 = rig.AddZombie(50.4f, 0.0f);  // 半径外（与 far1 近 → 归并同群落）
    const ecs::Entity band = rig.AddZombie(30.0f, 0.0f);  // 20~40 回差带 → 保持实体

    rig.Run(5);
    CHECK(!rig.world.Alive(far1));
    CHECK(!rig.world.Alive(far2));
    CHECK(rig.world.Alive(band));
    CHECK(rig.system->GetStats().virtualized == 2u);
    CHECK(rig.system->GetStats().live == 1);
    const ai::VirtualHordeStore::Stats stats = rig.hordes.GetStats(0.0f, 0.0f);
    CHECK(stats.zombies == 2);
    CHECK(stats.hordes == 1);                                 // mergeRadius 内归并
    CHECK(std::abs(stats.nearestDist - 50.2f) < 1.0f);        // 群落中心≈折回位置（守恒）
    // 继续跑：回差带内实体稳定，不再抖动
    rig.Run(30);
    CHECK(rig.world.Alive(band));
    CHECK(rig.system->GetStats().virtualized == 2u);
}

TEST_CASE("SpawnLodSystem：倒地窗口 / 已死亡实体不参与虚拟化（归 DeathSystem 收尾）") {
    ai::SpawnLodSystem::Params p;
    p.populationTarget = 0;
    p.materializeRadius = 10.0f;
    p.virtualizeRadius = 40.0f;
    p.virtualizePerSecond = 100.0f;
    SpawnRig rig(p);
    const ecs::Entity dying = rig.AddZombie(60.0f, 0.0f);
    rig.world.Registry().emplace<ecs::Dying>(dying);
    const ecs::Entity dead = rig.AddZombie(61.0f, 0.0f);
    rig.world.Registry().get<ecs::Health>(dead).current = 0.0f;
    const ecs::Entity normal = rig.AddZombie(62.0f, 0.0f);

    rig.Run(3);
    CHECK(rig.world.Alive(dying));   // 倒地窗口的实体不越权回收
    CHECK(rig.world.Alive(dead));    // HP=0 但仍由 DeathSystem 走流程
    CHECK(!rig.world.Alive(normal));  // 正常实体照常虚拟化
    CHECK(rig.system->GetStats().virtualized == 1u);
}

TEST_CASE("SpawnLodSystem：人口低于目标 → 补给进虚拟层（环形放置/到达目标即停）") {
    ai::SpawnLodSystem::Params p;
    p.populationTarget = 20;
    p.refillPerSecond = 240.0f;   // 快：4 只/tick
    p.refillRadius = 60.0f;
    p.refillBandWidth = 5.0f;
    p.materializeRadius = 10.0f;  // 群落远在 60 格，不实体化
    p.virtualizeRadius = 48.0f;
    SpawnRig rig(p);

    rig.Run(30);  // 缺口 20 只即停
    CHECK(rig.system->GetStats().spawned == 20u);
    CHECK(rig.system->GetStats().virtualZombies == 20);
    const ai::VirtualHordeStore::Stats stats = rig.hordes.GetStats(0.0f, 0.0f);
    CHECK(stats.zombies == 20);
    for (const ai::VirtualHorde& horde : rig.hordes.Hordes()) {
        const float d = Dist(horde.centerX, horde.centerY, 0.0f, 0.0f);
        CHECK(d >= 60.0f - 0.01f);  // 全部落在 [refillRadius, +band] 环带（虚拟化半径外）
        CHECK(d <= 65.0f + 0.01f);
    }
    // 到达目标后不再补给
    rig.Run(60);
    CHECK(rig.system->GetStats().spawned == 20u);
    CHECK(rig.hordes.GetStats(0.0f, 0.0f).zombies == 20);
}

TEST_CASE("SpawnLodSystem：守恒——实体化/虚拟化只做形态转换（live + virtual 不变）") {
    ai::SpawnLodSystem::Params p;
    p.populationTarget = 0;
    p.materializeRadius = 30.0f;
    p.materializePerSecond = 60.0f;  // 1 只/tick
    p.minSpawnDistance = 0.0f;
    p.virtualizeRadius = 60.0f;
    p.virtualizePerSecond = 60.0f;
    SpawnRig rig(p);
    rig.hordes.AddHorde(20.0f, 0.0f, 10, 1.0f);

    rig.Run(12);  // 全部实体化
    CHECK(rig.system->GetStats().live == 10);
    CHECK(rig.system->GetStats().virtualZombies == 0);
    CHECK(rig.system->GetStats().live + rig.system->GetStats().virtualZombies == 10);

    // 焦点移远 → 全部虚拟化，总量守恒
    rig.Run(60, kDt, 200.0f, 200.0f);
    CHECK(rig.system->GetStats().live == 0);
    CHECK(rig.system->GetStats().virtualZombies == 10);
    CHECK(rig.world.AliveCount() == 0u);
}
