#pragma once

#include <cstdint>
#include <functional>
#include <random>
#include <vector>

#include "ai/VirtualHorde.h"
#include "ecs/EcsWorld.h"

namespace zl::ai {

// M5-T7：刷怪与虚拟化（docs/00 §6.1 固定链：… → Death → SpawnLod → Animation → RenderCollect）。
//
// 三种形态的同一批僵尸（人口守恒：live + virtual ≈ populationTarget）：
//  1) 密度目标（refill）：world 总量低于 populationTarget 时，按 refillPerSecond
//     把缺口补成"虚拟僵尸"——落在焦点周围 [refillRadius, refillRadius+band] 环带
//     （近距归并 mergeRadius 内的既有群落），由 VirtualHordeSystem 的漂移逐步带近；
//  2) 实体化（materialize）：群落中心进入 materializeRadius → 按 materializePerSecond
//     （全局速率）拆出实体入 registry；出生点 = 群落中心 ± spread（可走格就近吸附、
//     且离焦点 ≥ minSpawnDistance，保证在弹出的视野外）；实体上限 maxLive；
//  3) 虚拟化（virtualize）：实体超出 virtualizeRadius → 销毁实体、折回群落统计
//     （AddOrMerge 归并）。materializeRadius < virtualizeRadius 的环带形成回差，
//     防止边界抖动来回转换。
//
// 依赖注入（可无头测试）：walkable（(wx,wy) 是否可走）、spawnZombie（工厂：建实体，
// 含物理体等全部组件）、despawnZombie（销毁前钩子：外部资源如物理体回收）。
// 计时按帧累计（perSecond × dt），单 tick 操作数上限 maxOpsPerTick（倍速/卡顿保护）。
class SpawnLodSystem {
public:
    using WalkableQuery = std::function<bool(int, int)>;
    using SpawnFn = std::function<ecs::Entity(ecs::EcsWorld&, float, float)>;
    using DespawnFn = std::function<void(ecs::EcsWorld&, ecs::Entity)>;

    struct Params {
        int populationTarget = 240;          // 被追踪僵尸总数目标（实体 + 虚拟）
        int maxLive = 320;                   // 实体僵尸上限（性能保护）
        float materializeRadius = 32.0f;     // 群落中心进入该半径 → 开始实体化（格）
        float materializePerSecond = 16.0f;  // 实体化速率（只/秒，全局）
        float minSpawnDistance = 30.0f;      // 实体化出生点离焦点的最小距离（视野外）
        float virtualizeRadius = 48.0f;      // 实体超出该半径 → 折回虚拟统计（格）
        float virtualizePerSecond = 12.0f;   // 虚拟化速率（只/秒）
        float refillPerSecond = 2.0f;        // 人口补给速率（只/秒）
        float refillRadius = 40.0f;          // 补给环带内径（格）
        float refillBandWidth = 20.0f;       // 补给环带宽度（格）
        float mergeRadius = 15.0f;           // 归并半径（补入既有群落 / 实体化回收）
        int maxOpsPerTick = 8;               // 单 tick 实体化/虚拟化/补给各自上限
        std::uint32_t seed = 20261016u;      // 确定性随机（出生点/补给角）
    };

    struct Stats {
        int live = 0;            // 实体僵尸数（AiState + Health>0，本帧快照）
        int virtualZombies = 0;  // 虚拟僵尸总数（含本 tick 变化后的近似值）
        int hordes = 0;          // 群落数
        std::uint64_t spawned = 0;       // 累计补给生成（进虚拟层）
        std::uint64_t materialized = 0;  // 累计实体化
        std::uint64_t virtualized = 0;   // 累计虚拟化
    };

    static Params DefaultParams();
    SpawnLodSystem(WalkableQuery walkable, SpawnFn spawnZombie, DespawnFn despawnZombie,
                   VirtualHordeStore& hordes, Params params = DefaultParams())
        : m_walkable(std::move(walkable)),
          m_spawn(std::move(spawnZombie)),
          m_despawn(std::move(despawnZombie)),
          m_hordes(hordes),
          m_params(params),
          m_rng(params.seed) {}

    void SetFocus(float x, float y) {
        m_focusX = x;
        m_focusY = y;
    }

    // M31-T5: biome/region density gradient relative to the map default.
    // The base populationTarget remains the config value; this only scales the
    // target while the player is in a region with a different spawn density.
    void SetDensityScale(float scale);
    float DensityScale() const { return m_densityScale; }

    void Update(ecs::EcsWorld& world, float dt);
    const Stats& GetStats() const { return m_stats; }

private:
    struct Candidate {  // 虚拟化候选（view 遍历期间只收集；遍历后再销毁）
        ecs::Entity entity;
        float x = 0.0f;
        float y = 0.0f;
    };

    bool TryMaterializeOne(ecs::EcsWorld& world, std::uint64_t hordeId, float cx, float cy,
                           float spread);

    WalkableQuery m_walkable;
    SpawnFn m_spawn;
    DespawnFn m_despawn;
    VirtualHordeStore& m_hordes;
    Params m_params;
    std::mt19937 m_rng;

    float m_focusX = 0.0f;
    float m_focusY = 0.0f;
    float m_densityScale = 1.0f;
    float m_materializeAccum = 0.0f;
    float m_virtualizeAccum = 0.0f;
    float m_refillAccum = 0.0f;
    float m_logTimer = 0.0f;
    std::vector<Candidate> m_candidates;  // 每 tick 复用（避免分配）
    Stats m_stats;
};

inline SpawnLodSystem::Params SpawnLodSystem::DefaultParams() { return {}; }

}  // namespace zl::ai
