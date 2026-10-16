#pragma once

#include <cstdint>
#include <functional>

#include "ai/components/AiComponents.h"
#include "characters/components/CombatComponents.h"
#include "ecs/EcsWorld.h"

namespace zl::ai {

// M5-T2：僵尸 AI 状态机（docs/00 §6.1 固定链：Input → Ai → PathFollow → Movement → …）。
//
// 设计（对齐 docs/01 M5-T2"转移条件集中可读，不再一个状态一个类"）：
//  - Update 内先算 per-entity facts（死亡 / 目标可见 / 路径忙闲 / 目标距离），
//    再进一个 switch —— 每个状态的出口条件一行一条、集中在一处；
//  - 状态副作用（要路 / 停步 / 计时重置）统一走 EnterState；
//  - 行为 = 写 PathRequest（PathFollowSystem 同 tick 提交）+ 读写 MoveIntent；
//    不直接调寻路服务、不碰物理 —— 只操作组件，可无头单测。
//
// 目标信息来源：AiState.targetId / lastKnown（感知系统 M5-T3 维护；
// M5-T2 阶段由 WorldDemoState 的临时感知（距离）填充，T3 落地后删除）。
class AiSystem {
public:
    using WalkableQuery = std::function<bool(int, int)>;  // (wx, wy) 是否可走

    // M5-T6：僵尸攻击参数（GameApp 从 config combat.zombie* 注入；
    // 攻击脉冲时写进 AttackIntent，CombatSystem 同 tick 消费）
    struct AttackParams {
        float damage = 8.0f;
        float knockback = 2.0f;         // 击退初速（格/秒）
        float staggerSeconds = 0.3f;    // 命中硬直（秒）
        float range = 1.3f;             // 扇形触及距离（格；攻击状态进入距离 1.0 + 余量）
        float arcDegrees = 120.0f;      // 扇形总张角
        ecs::DamageType type = ecs::DamageType::Bite;
    };

    struct Stats {
        int idle = 0;  // 以下状态分布 = 最近一帧快照
        int wander = 0;
        int chase = 0;
        int search = 0;
        int attack = 0;
        int dead = 0;
        std::uint64_t swings = 0;  // 累计攻击脉冲（跨帧累计）
    };

    explicit AiSystem(WalkableQuery walkable, AttackParams attack = {})
        : m_walkable(std::move(walkable)), m_attack(attack) {}

    void Update(ecs::EcsWorld& world, float dt);
    const Stats& GetStats() const { return m_stats; }

private:
    void EnterState(ecs::EcsWorld& world, ecs::Entity e, ecs::AiState& ai, ecs::AiStatus status,
                    float px, float py);
    bool RequestPathTo(ecs::EcsWorld& world, ecs::Entity e, float tx, float ty);

    WalkableQuery m_walkable;
    AttackParams m_attack;
    Stats m_stats;
    std::uint32_t m_tick = 0;  // 确定性随机盐（游荡/搜索选点每次不同）
};

}  // namespace zl::ai
