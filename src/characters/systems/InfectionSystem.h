#pragma once

#include <cstdint>

#include "characters/components/CombatComponents.h"

namespace zl::ecs {
class EcsWorld;
}

namespace zl::characters {

// M20-F1：感染规则。所有概率/阶段/治疗数值来自 config infection 段。
// 感染只挂在玩家上；僵尸爪击/撕咬命中后由 game 层回调按概率写入。
struct InfectionParams {
    bool enabled = true;
    float clawChance = 0.06f;
    float biteChance = 0.20f;
    float woundRiskBonus = 0.55f;
    float maxChance = 0.75f;

    float totalSeconds = 900.0f;
    float incubatingEnd = 0.30f;
    float feverEnd = 0.70f;
    float woundProgressionBonus = 0.80f;
    float feverFatiguePerSecond = 0.0015f;
    float severeFatiguePerSecond = 0.0030f;
    float severeHealthDrainPerSecond = 0.08f;
};

struct InfectionTreatment {
    bool applied = false;
    bool cured = false;
    float progressReduced = 0.0f;
};

// 未包扎伤口严重度总和（截断到 1）；用于暴露概率与病程加速。
float UnbandagedWoundSeverity(const ecs::Wounds& wounds);

// 爪/咬基础概率 + 既有未包扎伤口加成，最终截断到 maxChance。
float ExposureChance(ecs::DamageType type, const ecs::Wounds& wounds,
                     const InfectionParams& params = {});

// 当前病程速度（progress/秒）；slow 窗口会降低速度。
float InfectionProgressPerSecond(const ecs::Infection& infection,
                                 const ecs::Wounds* wounds,
                                 const InfectionParams& params = {});

// 到下一阶段/死亡的预计秒数；无进展时返回 0。
float InfectionSecondsToNextStage(const ecs::Infection& infection,
                                  const ecs::Wounds* wounds,
                                  const InfectionParams& params = {});

ecs::InfectionStage InfectionStageForProgress(float progress,
                                              const InfectionParams& params = {});

// 确定性感染判定：roll ∈ [0,1)，chance ∈ [0,1]。返回是否新感染。
bool TryInfect(ecs::Infection& infection, float chance, float roll,
               const ecs::Wounds* wounds = nullptr,
               const InfectionParams& params = {});

// 使用污染物品时的感染判定：同一 seed 可复现；risk=1 时 guaranteed。
// 概率仍受 InfectionParams.maxChance 约束。
bool TryInfectWithRisk(ecs::Infection& infection, float risk,
                       std::uint64_t seed, const ecs::Wounds* wounds = nullptr,
                       const InfectionParams& params = {});

// 抗生素/消毒/包扎：直接降低病程，并设置抑制窗口。进度归零时清除感染。
InfectionTreatment TreatInfection(ecs::Infection& infection, float cure,
                                  float slowStrength, float slowSeconds,
                                  const ecs::Wounds* wounds = nullptr,
                                  const InfectionParams& params = {});

// 根据 progress / 伤口重算阶段与 timeToNextStage。
void RefreshInfection(ecs::Infection& infection, const ecs::Wounds* wounds,
                      const InfectionParams& params = {});

// 固定链位置：PainSystem 之后、MovementSystem 之前。推进病程、发热疲劳、
// 重症掉血；死亡仍由 DeathSystem 统一处理。
class InfectionSystem {
public:
    explicit InfectionSystem(InfectionParams params = {});

    void Update(ecs::EcsWorld& world, float dt);

    const InfectionParams& Params() const { return m_params; }

private:
    InfectionParams m_params;
};

}  // namespace zl::characters
