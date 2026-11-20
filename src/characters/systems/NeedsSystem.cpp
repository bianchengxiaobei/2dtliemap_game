#include "characters/systems/NeedsSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/components/ProgressionComponents.h"
#include "core/Log.h"
#include "ecs/EcsWorld.h"

namespace zl::characters {
namespace {

float Clamp01(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

float ClampNonNegative(float value) {
    return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
}

float Severity(float value, float threshold) {
    if (value <= threshold) {
        return 0.0f;
    }
    if (threshold >= 1.0f) {
        return 1.0f;
    }
    return std::clamp((value - threshold) / (1.0f - threshold), 0.0f, 1.0f);
}

NeedsParams Sanitize(NeedsParams params) {
    params.hungerPerDay = ClampNonNegative(params.hungerPerDay);
    params.thirstPerDay = ClampNonNegative(params.thirstPerDay);
    params.fatiguePerDay = ClampNonNegative(params.fatiguePerDay);
    params.gameSecondsPerDay = std::max(1.0f, ClampNonNegative(params.gameSecondsPerDay));

    params.hungerDamageThreshold = Clamp01(params.hungerDamageThreshold);
    params.hungerDamagePenalty = Clamp01(params.hungerDamagePenalty);
    params.fatigueDamageThreshold = Clamp01(params.fatigueDamageThreshold);
    params.fatigueDamagePenalty = Clamp01(params.fatigueDamagePenalty);

    params.thirstMoveThreshold = Clamp01(params.thirstMoveThreshold);
    params.thirstMovePenalty = Clamp01(params.thirstMovePenalty);
    params.fatigueMoveThreshold = Clamp01(params.fatigueMoveThreshold);
    params.fatigueMovePenalty = Clamp01(params.fatigueMovePenalty);
    params.minMoveScale = std::clamp(Clamp01(params.minMoveScale), 0.05f, 1.0f);

    params.hungerCritical = Clamp01(params.hungerCritical);
    params.thirstCritical = Clamp01(params.thirstCritical);
    params.hungerCritical = std::max(params.hungerDamageThreshold, params.hungerCritical);
    params.thirstCritical = std::max(params.thirstMoveThreshold, params.thirstCritical);
    params.starvationHpPerSecond = ClampNonNegative(params.starvationHpPerSecond);
    params.dehydrationHpPerSecond = ClampNonNegative(params.dehydrationHpPerSecond);
    params.nutritionHungerRelief = Clamp01(params.nutritionHungerRelief);
    params.nutritionDamageRelief = Clamp01(params.nutritionDamageRelief);
    return params;
}

}  // namespace

NeedsSystem::NeedsSystem(NeedsParams params) : m_params(Sanitize(params)) {}

void NeedsSystem::Update(ecs::EcsWorld& world, float dt) {
    entt::registry& registry = world.Registry();
    const float step = std::max(0.0f, dt);
    const float share = step / m_params.gameSecondsPerDay;

    registry.view<ecs::PlayerTag, ecs::Needs>().each(
        [&](const ecs::Entity entity, ecs::Needs& needs) {
            auto* health = registry.try_get<ecs::Health>(entity);
            if (health == nullptr || health->current <= 0.0f ||
                registry.any_of<ecs::Dying>(entity)) {
                needs.moveScale = 1.0f;
                needs.damageScale = 1.0f;
                needs.healthDrainPerSecond = 0.0f;
                needs.warning = 0;
                return;
            }

            const ecs::Progression* progression =
                registry.try_get<ecs::Progression>(entity);
            const float hungerScale =
                progression != nullptr
                    ? std::clamp(progression->hungerDecayScale, 0.1f, 1.0f)
                    : 1.0f;
            const float thirstScale =
                progression != nullptr
                    ? std::clamp(progression->thirstDecayScale, 0.1f, 1.0f)
                    : 1.0f;
            const float fatigueScale =
                progression != nullptr
                    ? std::clamp(progression->fatigueDecayScale, 0.1f, 1.0f)
                    : 1.0f;
            const auto* food = registry.try_get<ecs::FoodState>(entity);
            const float nutrition =
                food != nullptr ? Clamp01(food->nutrition) : 0.0f;
            const float nutritionHungerScale =
                1.0f - m_params.nutritionHungerRelief * nutrition;

            needs.hunger =
                std::clamp(needs.hunger + m_params.hungerPerDay * share *
                                              hungerScale *
                                              nutritionHungerScale,
                           0.0f, 1.0f);
            needs.thirst =
                std::clamp(needs.thirst + m_params.thirstPerDay * share * thirstScale,
                           0.0f, 1.0f);
            needs.fatigue =
                std::clamp(needs.fatigue + m_params.fatiguePerDay * share * fatigueScale,
                           0.0f, 1.0f);

            const float hungerDamage =
                Severity(needs.hunger, m_params.hungerDamageThreshold);
            const float fatigueDamage =
                Severity(needs.fatigue, m_params.fatigueDamageThreshold);
            const float thirstMove = Severity(needs.thirst, m_params.thirstMoveThreshold);
            const float fatigueMove =
                Severity(needs.fatigue, m_params.fatigueMoveThreshold);

            needs.moveScale =
                std::clamp(1.0f - m_params.thirstMovePenalty * thirstMove -
                               m_params.fatigueMovePenalty * fatigueMove,
                           m_params.minMoveScale, 1.0f);
            needs.damageScale =
                std::clamp(1.0f - m_params.hungerDamagePenalty * hungerDamage -
                               m_params.fatigueDamagePenalty * fatigueDamage,
                           0.05f, 1.0f);

            const float starvation =
                Severity(needs.hunger, m_params.hungerCritical);
            const float dehydration =
                Severity(needs.thirst, m_params.thirstCritical);
            const float starvationRate = m_params.starvationHpPerSecond * starvation;
            const float dehydrationRate =
                m_params.dehydrationHpPerSecond * dehydration;
            needs.healthDrainPerSecond =
                (starvationRate + dehydrationRate) *
                (1.0f - m_params.nutritionDamageRelief * nutrition);

            const bool critical = needs.hunger >= m_params.hungerCritical ||
                                  needs.thirst >= m_params.thirstCritical;
            const bool warn = needs.moveScale < 0.999f ||
                              needs.damageScale < 0.999f ||
                              needs.healthDrainPerSecond > 0.0f;
            const std::uint8_t warning = critical ? 2 : (warn ? 1 : 0);
            if (warning != needs.warning) {
                ZL_INFO("needs",
                        "玩家需求状态 {}：饥饿 {:.2f}，口渴 {:.2f}，疲劳 {:.2f}，"
                        "移速 x{:.2f}，伤害 x{:.2f}",
                        static_cast<int>(warning), needs.hunger, needs.thirst,
                        needs.fatigue, needs.moveScale, needs.damageScale);
            }
            needs.warning = warning;

            if (needs.healthDrainPerSecond > 0.0f) {
                const bool wasAlive = health->current > 0.0f;
                const float drain = needs.healthDrainPerSecond * step;
                health->current = std::max(0.0f, health->current - drain);
                ecs::LastDamage& last =
                    registry.get_or_emplace<ecs::LastDamage>(entity);
                last.cause = dehydrationRate > starvationRate
                                 ? ecs::DamageCause::Dehydration
                                 : ecs::DamageCause::Starvation;
                last.amount = drain;
                last.sourceId = 0;  // 环境伤害：无击杀者
                if (wasAlive && health->current <= 0.0f) {
                    ZL_INFO("needs", "玩家因{}耗尽生命",
                            last.cause == ecs::DamageCause::Dehydration ? "脱水"
                                                                        : "饥饿");
                }
            }
        });
}

}  // namespace zl::characters
