#pragma once

namespace zl::ecs {
class EcsWorld;
}

namespace zl::characters {

struct NeedsParams {
    float hungerPerDay = 1.4f;
    float thirstPerDay = 1.8f;
    float fatiguePerDay = 1.2f;
    float gameSecondsPerDay = 3600.0f;

    float hungerDamageThreshold = 0.70f;
    float hungerDamagePenalty = 0.35f;
    float fatigueDamageThreshold = 0.80f;
    float fatigueDamagePenalty = 0.20f;

    float thirstMoveThreshold = 0.65f;
    float thirstMovePenalty = 0.30f;
    float fatigueMoveThreshold = 0.70f;
    float fatigueMovePenalty = 0.30f;
    float minMoveScale = 0.35f;

    float hungerCritical = 0.90f;
    float thirstCritical = 0.90f;
    float starvationHpPerSecond = 0.35f;
    float dehydrationHpPerSecond = 0.60f;

    // F5-T3：营养降低饥饿增速与饥饿/脱水伤害（0 = 关闭）。
    float nutritionHungerRelief = 0.25f;
    float nutritionDamageRelief = 0.35f;
};

class NeedsSystem {
public:
    explicit NeedsSystem(NeedsParams params = {});

    void Update(ecs::EcsWorld& world, float dt);

    const NeedsParams& Params() const { return m_params; }

private:
    NeedsParams m_params;
};

}  // namespace zl::characters
