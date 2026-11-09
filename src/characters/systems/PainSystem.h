#pragma once

#include "characters/components/CombatComponents.h"

namespace zl::ecs {
class EcsWorld;
}

namespace zl::characters {

// M15-T4：疼痛参数。伤口严重度按类型/是否包扎折算后汇总，再乘 perSeverity；
// 最终 level 限制在 maxLevel，避免高伤时完全不可操作。
struct PainParams {
    float perSeverity = 0.65f;
    float bandagedScale = 0.5f;
    float maxLevel = 0.9f;
    float maxMoveSlow = 0.35f;
    float maxAttackCooldownIncrease = 0.5f;
};

float ComputeBasePain(const ecs::Wounds& wounds, const PainParams& params = {});

// 根据 baseLevel + 剩余止痛时间重算 level / moveScale / attackCooldownScale。
void RefreshPain(ecs::Pain& pain);

// 使用止痛药：强度取已有/新值的较大者，持续时间刷新；不叠加成更高强度。
bool ApplyPainRelief(ecs::Pain& pain, float strength, float seconds);

// 固定链位置：WoundSystem 之后、MovementSystem/DeathSystem 之前。
// 只维护疼痛数值与乘数，不直接改 Health/MoveIntent。
class PainSystem {
public:
    explicit PainSystem(PainParams params = {}) : m_params(params) {}

    void Update(ecs::EcsWorld& world, float dt);

private:
    PainParams m_params;
};

}  // namespace zl::characters
