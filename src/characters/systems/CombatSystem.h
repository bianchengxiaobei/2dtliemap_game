#pragma once

#include <cstdint>
#include <vector>

#include "characters/components/CombatComponents.h"
#include "ecs/EcsWorld.h"

namespace zl::characters {

// M5-T6：战斗系统。固定链位置（docs/00 §6.1）：Boids 之后、Movement 之前——
//  1) 衰减受击反应（硬直计时 / 击退速度 / 闪红恢复），硬直实体的 MoveIntent
//     被击退速度接管（覆盖 AI / 输入本 tick 写的意图）；
//  2) 消费 AttackIntent（pending = true）：扇形判定（range + arcDegrees）
//     → ApplyDamage 统一管线（可命中锥内多个目标）。
//
// 伤害管线 ApplyDamage（来源/类型解耦的单一入口；Update/演示/测试/未来脚本共用）：
//  Health 扣减 → HitReaction（硬直/击退/闪红）→ 致死由 DeathSystem 接手
//  （倒地窗口 → 尸体）。Stats 供 HUD / 测试。
class CombatSystem {
public:
    struct Stats {
        std::uint64_t swings = 0;   // 消费的挥击数（跨帧累计）
        std::uint64_t hits = 0;     // 命中次数（跨帧累计；含致死命中）
        std::uint64_t kills = 0;    // 击杀数（跨帧累计，HP 被打到 <=0）
        float damageDealt = 0.0f;   // 累计伤害（跨帧累计）
        int staggered = 0;          // 本帧硬直中的实体数（每帧快照）
    };

    // 统一伤害管线：命中有效性（有 Health 且 HP>0）→ 扣血 + 受击反应 + 统计。
    // 返回是否实际造成伤害（已倒地目标返回 false，不重复结算）。
    bool ApplyDamage(ecs::EcsWorld& world, ecs::Entity target, const ecs::DamageEvent& event);

    void Update(ecs::EcsWorld& world, float dt);
    const Stats& GetStats() const { return m_stats; }

private:
    struct Candidate {
        ecs::Entity e = ecs::EcsWorld::kNull;
        float x = 0.0f;
        float y = 0.0f;
        bool ai = false;  // 带 AiState（M5-T6 阵营规则：僵尸不误伤同类）
    };

    std::vector<Candidate> m_candidates;  // 命中候选缓存（每 tick 收集一次）
    Stats m_stats;
};

}  // namespace zl::characters
