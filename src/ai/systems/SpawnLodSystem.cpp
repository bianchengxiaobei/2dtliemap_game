#include "ai/systems/SpawnLodSystem.h"

#include <algorithm>
#include <cmath>

#include "ai/Passability.h"
#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "core/Log.h"
#include "ecs/components/CommonComponents.h"

namespace zl::ai {
namespace {

constexpr float kTwoPi = 6.2831853f;
constexpr float kVirtualizeSpread = 6.0f;  // 折回/补给时新群落的默认散布（格）

float Accumulate(float& accum, float perSecond, float dt, int maxOps) {
    accum = std::clamp(accum + perSecond * dt, 0.0f, static_cast<float>(maxOps));
    const int ops = static_cast<int>(accum);
    accum -= static_cast<float>(ops);  // 只扣实际取走的（调用方未用完的预算即丢弃）
    return static_cast<float>(ops);
}

}  // namespace

void SpawnLodSystem::SetDensityScale(float scale) {
    m_densityScale = std::isfinite(scale) ? std::clamp(scale, 0.0f, 4.0f) : 1.0f;
}

void SpawnLodSystem::Update(ecs::EcsWorld& world, float dt) {
    m_stats.live = 0;

    // 1) 扫描：live 计数 + 虚拟化候选（视距外、非倒地/死亡 —— 倒地窗口归 DeathSystem 所有）
    m_candidates.clear();
    const float virtualizeSq = m_params.virtualizeRadius * m_params.virtualizeRadius;
    world.Registry().view<ecs::AiState, ecs::Position, ecs::Health>().each(
        [&](const ecs::Entity e, const ecs::AiState&, const ecs::Position& pos,
            const ecs::Health& health) {
            if (world.Registry().any_of<ecs::BaseSiegeZombie>(e)) {
                return;
            }
            if (world.Registry().any_of<ecs::PoiGuard>(e)) {
                return;
            }
            if (world.Registry().any_of<ecs::RandomEventActor>(e)) {
                return;
            }
            if (!world.Alive(e) || health.current <= 0.0f ||
                world.Registry().any_of<ecs::Dying>(e)) {
                return;
            }
            ++m_stats.live;
            const float dx = pos.x - m_focusX;
            const float dy = pos.y - m_focusY;
            if (dx * dx + dy * dy > virtualizeSq) {
                m_candidates.push_back(Candidate{e, pos.x, pos.y});
            }
        });

    const VirtualHordeStore::Stats hordeStats = m_hordes.GetStats(m_focusX, m_focusY);
    int virtualNow = hordeStats.zombies;  // 本 tick 内的虚拟总数（随转换实时修正）

    // 2) 虚拟化：视距外实体 → 销毁（物理体等经 despawn 钩子回收）→ 折回群落统计
    const int vOps = static_cast<int>(Accumulate(m_virtualizeAccum, m_params.virtualizePerSecond,
                                                 dt, m_params.maxOpsPerTick));
    const int vCount = std::min(vOps, static_cast<int>(m_candidates.size()));
    for (int i = 0; i < vCount; ++i) {
        const Candidate& c = m_candidates[i];
        m_despawn(world, c.entity);
        m_hordes.AddOrMerge(c.x, c.y, 1, kVirtualizeSpread, m_params.mergeRadius);
        world.Destroy(c.entity);
        --m_stats.live;
        ++virtualNow;
        ++m_stats.virtualized;
    }

    // 3) 实体化：materializeRadius 内的群落 → 按全局速率拆出实体（maxLive 封顶）
    const int mOps = static_cast<int>(Accumulate(m_materializeAccum, m_params.materializePerSecond,
                                                 dt, m_params.maxOpsPerTick));
    if (mOps > 0) {
        struct NearHorde {
            std::uint64_t id = 0;
            float cx = 0.0f;
            float cy = 0.0f;
            float spread = 6.0f;
            bool exhausted = false;  // 本 tick 内已扣空或找不到出生点
        };
        std::vector<NearHorde> near;
        const float materializeSq = m_params.materializeRadius * m_params.materializeRadius;
        for (const VirtualHorde& horde : m_hordes.Hordes()) {
            const float dx = horde.centerX - m_focusX;
            const float dy = horde.centerY - m_focusY;
            if (dx * dx + dy * dy <= materializeSq) {
                near.push_back(NearHorde{horde.id, horde.centerX, horde.centerY, horde.spread});
            }
        }
        int ops = mOps;
        while (ops > 0 && m_stats.live < m_params.maxLive && !near.empty()) {
            bool any = false;
            for (NearHorde& ref : near) {
                if (ops <= 0 || m_stats.live >= m_params.maxLive) {
                    break;
                }
                if (ref.exhausted) {
                    continue;
                }
                if (TryMaterializeOne(world, ref.id, ref.cx, ref.cy, ref.spread)) {
                    --ops;
                    ++m_stats.live;
                    --virtualNow;
                    ++m_stats.materialized;
                    any = true;
                } else {
                    ref.exhausted = true;  // 下 tick 再试（群落漂移/出生点重采样）
                }
            }
            if (!any) {
                break;
            }
        }
    }

    // 4) 密度目标：追踪总数（实体 + 虚拟）低于 target → 按速率把缺口补成虚拟僵尸
    //    （环带落在 refillRadius..+band，由 VirtualHordeSystem 的漂移逐步带近）
    const int effectiveTarget = static_cast<int>(std::lround(
        static_cast<double>(std::max(0, m_params.populationTarget)) *
        static_cast<double>(m_densityScale)));
    const int deficit = effectiveTarget - (m_stats.live + virtualNow);
    if (deficit > 0) {
        const int rOps = std::min(
            static_cast<int>(Accumulate(m_refillAccum, m_params.refillPerSecond, dt,
                                        m_params.maxOpsPerTick)),
            deficit);
        if (rOps > 0) {
            std::uniform_real_distribution<float> u01(0.0f, 1.0f);
            for (int i = 0; i < rOps; ++i) {
                const float angle = u01(m_rng) * kTwoPi;
                const float radius = m_params.refillRadius + u01(m_rng) * m_params.refillBandWidth;
                const float px = m_focusX + std::cos(angle) * radius;
                const float py = m_focusY + std::sin(angle) * radius;
                m_hordes.AddOrMerge(px, py, 1, kVirtualizeSpread, m_params.mergeRadius);
                ++virtualNow;
                ++m_stats.spawned;
            }
        }
    } else {
        m_refillAccum = 0.0f;  // 无缺口：清零，避免累积后突发补给
    }

    m_stats.virtualZombies = virtualNow;
    m_stats.hordes = static_cast<int>(m_hordes.Hordes().size());

    // 周期统计（5s：冒烟/压测的时间序列证据；DEBUG 级可关）
    m_logTimer += dt;
    if (m_logTimer >= 5.0f) {
        m_logTimer = 0.0f;
        ZL_DEBUG("ai",
                 "SpawnLod 统计：live={} virtual={} hordes={} | 累计 实体化={} 虚拟化={} 补给={}",
                 m_stats.live, m_stats.virtualZombies, m_stats.hordes,
                 static_cast<unsigned long long>(m_stats.materialized),
                 static_cast<unsigned long long>(m_stats.virtualized),
                 static_cast<unsigned long long>(m_stats.spawned));
    }
}

bool SpawnLodSystem::TryMaterializeOne(ecs::EcsWorld& world, std::uint64_t hordeId, float cx,
                                       float cy, float spread) {
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    constexpr int kAttempts = 4;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        // 出生点：群落中心 ± spread 圆盘（均匀采样）
        const float r = spread * std::sqrt(u01(m_rng));
        const float theta = u01(m_rng) * kTwoPi;
        float px = cx + std::cos(theta) * r;
        float py = cy + std::sin(theta) * r;
        // 推离焦点：保证出生在视野外（与 minSpawnDistance 一致；方向退化时取随机方向）
        float dx = px - m_focusX;
        float dy = py - m_focusY;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < m_params.minSpawnDistance) {
            if (dist < 0.001f) {
                const float a = u01(m_rng) * kTwoPi;
                dx = std::cos(a);
                dy = std::sin(a);
            } else {
                dx /= dist;
                dy /= dist;
            }
            px = m_focusX + dx * m_params.minSpawnDistance;
            py = m_focusY + dy * m_params.minSpawnDistance;
        }
        // 可走格吸附（附近 3 格；未加载/墙群 → 换一次采样）
        int wx = 0;
        int wy = 0;
        const int cellX = static_cast<int>(std::floor(px));
        const int cellY = static_cast<int>(std::floor(py));
        if (!FindNearestWalkable(cellX, cellY, 3, m_walkable, &wx, &wy)) {
            continue;
        }
        if (m_hordes.TakeZombies(hordeId, 1) <= 0) {
            return false;  // 群落已被扣空
        }
        m_spawn(world, static_cast<float>(wx) + 0.5f, static_cast<float>(wy) + 0.5f);
        return true;
    }
    return false;  // 本 tick 放弃（下一 tick 重采样）
}

}  // namespace zl::ai
