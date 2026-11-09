#pragma once

#include "characters/components/CombatComponents.h"

namespace zl::ecs {
class EcsWorld;
}

namespace zl::characters {

// M15-T2：伤口生成/流血规则。CombatSystem 用同一份参数把伤害映射为伤口，
// 运行时 WoundSystem 只按已写入的 bleedRate/remaining 推进失血。
struct WoundParams {
    float severityPerDamage = 0.02f;   // 每点伤害的严重度（再乘类型倍率）
    float clawSeverityScale = 1.2f;
    float biteSeverityScale = 1.6f;
    float bluntSeverityScale = 1.0f;
    float clawBleedPerDamage = 0.035f;
    float biteBleedPerDamage = 0.08f;
    float clawBleedSeconds = 18.0f;
    float biteBleedSeconds = 30.0f;
    float maxBleedRate = 8.0f;         // 单处伤口起始失血上限（HP/s）
};

struct WoundTreatment {
    int wounds = 0;             // 本次处理的伤口数
    float severityRemoved = 0.0f;
    bool stoppedBleeding = false;
};

// 把一次伤害写成伤口：Debug/非正伤害忽略；伤口满时替换最轻的一处。
// 参数越界会被就地钳制（配置来自外部文件）。
void AddWound(ecs::Wounds& wounds, ecs::DamageType type, float damage,
              const WoundParams& params = {});

// 绷带/破布：降低所有伤口严重度并立即止血；严重度 <= 0.05 的伤口移除。
WoundTreatment TreatWounds(ecs::Wounds& wounds, float strength);

int WoundCount(const ecs::Wounds& wounds);
float WorstWoundSeverity(const ecs::Wounds& wounds);
float CurrentBleedRate(const ecs::Wounds& wounds);

// 固定链位置：CombatSystem 之后、DeathSystem 之前。按 tick 扣失血并把
// 伤口剩余时间推进到 0；不写 HitReaction（避免持续闪红），死亡交给 DeathSystem。
class WoundSystem {
public:
    void Update(ecs::EcsWorld& world, float dt);
};

}  // namespace zl::characters
