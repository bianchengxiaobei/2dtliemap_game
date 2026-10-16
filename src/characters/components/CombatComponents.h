#pragma once

#include <cstdint>

namespace zl::ecs {

// M5-T6：战斗领域组件（攻击意图 / 受击反应 / 伤害事件）。
// 组件 = 纯数据；行为在 characters::CombatSystem
// （固定链位置：Boids 之后、Movement 之前）。

// 伤害类型：伤害管线"来源/类型解耦"的类型侧（Health 不携带来源/类型）。
// M6 装备/护甲在 ApplyDamage 处按类型扩展减伤钩子。
enum class DamageType : std::uint8_t {
    Blunt = 0,  // 钝击（徒手/枪托）
    Bite = 1,   // 撕咬（僵尸）
    Debug = 2,  // 演示/调试（F6 等）
};

// 伤害事件（管线输入）：来源 = 稳定 ID（跨模块约定，绝不存 entt 句柄）；
// 击退/硬直是"表现参数"，由管线统一落到受害者的 HitReaction。
struct DamageEvent {
    std::uint64_t sourceId = 0;   // 攻击者稳定 ID（0 = 环境/未知）
    DamageType type = DamageType::Blunt;
    float amount = 0.0f;          // 伤害量（HP 扣减）
    float dirX = 0.0f;            // 击退方向（世界格空间；零向量 = 无击退）
    float dirY = 0.0f;
    float knockback = 0.0f;       // 击退初速（格/秒）
    float staggerSeconds = 0.0f;  // 硬直时长（秒）
};

// 攻击意图（挥击申请）：攻击者（AiSystem / 玩家输入）写 pending = true；
// CombatSystem 本 tick 消费：清 pending → 扇形判定（range + arcDegrees）→ 伤害管线。
// 判定不依赖 targetId（它是"想打谁"的提示，供未来弹道/日志用）。
struct AttackIntent {
    std::uint64_t targetId = 0;      // 期望目标稳定 ID（0 = 只按方向横扫）
    float dirX = 1.0f;               // 攻击方向（单位向量，世界格空间）
    float dirY = 0.0f;
    float range = 1.0f;              // 触及距离（格，中心距）
    float arcDegrees = 100.0f;       // 扇形总张角（以 dir 为轴）
    float damage = 10.0f;
    float knockback = 2.5f;          // 命中击退初速（格/秒）
    float staggerSeconds = 0.35f;    // 命中硬直时长（秒）
    DamageType type = DamageType::Blunt;
    bool pending = false;            // true = 本 tick 落下一次挥击
};

// 受击反应（CombatSystem 写/衰减）：
//  - staggerLeft > 0：硬直中——AiSystem 冻结转移/计时，MoveIntent 被击退速度接管；
//  - vx/vy：击退速度（指数衰减）；flashLeft：受击闪红剩余（渲染读 SpriteRef.tint）。
struct HitReaction {
    float staggerLeft = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float flashLeft = 0.0f;
};

}  // namespace zl::ecs
