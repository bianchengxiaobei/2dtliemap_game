#include "characters/systems/InfectionSystem.h"

#include <algorithm>
#include <cmath>

#include <entt/entt.hpp>

#include "characters/components/CharacterComponents.h"
#include "core/Log.h"
#include "ecs/EcsWorld.h"

namespace zl::characters {
namespace {

float NonNegative(float value) {
    return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
}

float Clamp01(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

float ClampChance(float value, float maximum) {
    return std::clamp(Clamp01(value), 0.0f, Clamp01(maximum));
}

InfectionParams Sanitize(InfectionParams params) {
    params.clawChance = Clamp01(params.clawChance);
    params.biteChance = Clamp01(params.biteChance);
    params.woundRiskBonus = NonNegative(params.woundRiskBonus);
    params.maxChance = std::max(0.0f, std::min(1.0f, params.maxChance));
    params.totalSeconds = std::max(1.0f, NonNegative(params.totalSeconds));

    params.incubatingEnd = std::clamp(Clamp01(params.incubatingEnd), 0.01f, 0.90f);
    params.feverEnd = std::clamp(Clamp01(params.feverEnd),
                                 params.incubatingEnd + 0.01f, 0.99f);
    params.woundProgressionBonus = NonNegative(params.woundProgressionBonus);
    params.feverFatiguePerSecond = NonNegative(params.feverFatiguePerSecond);
    params.severeFatiguePerSecond = NonNegative(params.severeFatiguePerSecond);
    params.severeHealthDrainPerSecond =
        NonNegative(params.severeHealthDrainPerSecond);
    return params;
}

float StageThreshold(ecs::InfectionStage stage, const InfectionParams& params) {
    switch (stage) {
        case ecs::InfectionStage::Incubating:
            return params.incubatingEnd;
        case ecs::InfectionStage::Fever:
            return params.feverEnd;
        case ecs::InfectionStage::Severe:
            return 1.0f;
        case ecs::InfectionStage::None:
            return 0.0f;
    }
    return 0.0f;
}

const char* StageName(ecs::InfectionStage stage) {
    switch (stage) {
        case ecs::InfectionStage::Incubating:
            return "潜伏";
        case ecs::InfectionStage::Fever:
            return "发热/疲劳";
        case ecs::InfectionStage::Severe:
            return "重症";
        case ecs::InfectionStage::None:
            return "无";
    }
    return "无";
}

}  // namespace

float UnbandagedWoundSeverity(const ecs::Wounds& wounds) {
    const std::uint8_t count = std::min<std::uint8_t>(wounds.count, ecs::kMaxWounds);
    float severity = 0.0f;
    for (std::uint8_t i = 0; i < count; ++i) {
        const ecs::WoundEntry& entry = wounds.entries[i];
        if (!entry.bandaged) {
            severity += Clamp01(entry.severity);
        }
    }
    return Clamp01(severity);
}

float ExposureChance(ecs::DamageType type, const ecs::Wounds& wounds,
                     const InfectionParams& params) {
    const InfectionParams safe = Sanitize(params);
    float chance = 0.0f;
    if (type == ecs::DamageType::Claw) {
        chance = safe.clawChance;
    } else if (type == ecs::DamageType::Bite) {
        chance = safe.biteChance;
    } else {
        return 0.0f;
    }
    chance += UnbandagedWoundSeverity(wounds) * safe.woundRiskBonus;
    return ClampChance(chance, safe.maxChance);
}

float InfectionProgressPerSecond(const ecs::Infection& infection,
                                 const ecs::Wounds* wounds,
                                 const InfectionParams& params) {
    const InfectionParams safe = Sanitize(params);
    if (infection.stage == ecs::InfectionStage::None || !safe.enabled) {
        return 0.0f;
    }
    float factor = 1.0f;
    if (wounds != nullptr) {
        factor += UnbandagedWoundSeverity(*wounds) * safe.woundProgressionBonus;
    }
    if (infection.slowRemaining > 0.0f) {
        factor *= 1.0f - std::clamp(infection.slowStrength, 0.0f, 0.95f);
    }
    return std::max(0.0f, factor) / safe.totalSeconds;
}

float InfectionSecondsToNextStage(const ecs::Infection& infection,
                                  const ecs::Wounds* wounds,
                                  const InfectionParams& params) {
    const InfectionParams safe = Sanitize(params);
    if (infection.stage == ecs::InfectionStage::None) {
        return 0.0f;
    }
    const float rate = InfectionProgressPerSecond(infection, wounds, safe);
    if (!(rate > 0.0f)) {
        return 0.0f;
    }
    const float threshold = StageThreshold(infection.stage, safe);
    return std::max(0.0f, (threshold - Clamp01(infection.progress)) / rate);
}

ecs::InfectionStage InfectionStageForProgress(float progress,
                                              const InfectionParams& params) {
    const InfectionParams safe = Sanitize(params);
    const float value = Clamp01(progress);
    if (value < safe.incubatingEnd) {
        return ecs::InfectionStage::Incubating;
    }
    if (value < safe.feverEnd) {
        return ecs::InfectionStage::Fever;
    }
    return ecs::InfectionStage::Severe;
}

void RefreshInfection(ecs::Infection& infection, const ecs::Wounds* wounds,
                      const InfectionParams& params) {
    const InfectionParams safe = Sanitize(params);
    infection.progress = Clamp01(infection.progress);
    if (infection.stage == ecs::InfectionStage::None &&
        infection.progress <= 0.0001f) {
        infection.slowStrength = 0.0f;
        infection.slowRemaining = 0.0f;
        infection.slowDuration = 0.0f;
        infection.timeToNextStage = 0.0f;
        return;
    }
    infection.stage = InfectionStageForProgress(infection.progress, safe);
    infection.slowStrength = Clamp01(infection.slowStrength);
    infection.slowRemaining = std::max(0.0f, infection.slowRemaining);
    infection.slowDuration = std::max(0.0f, infection.slowDuration);
    if (infection.slowRemaining > infection.slowDuration) {
        infection.slowRemaining = infection.slowDuration;
    }
    if (infection.stage == ecs::InfectionStage::None) {
        infection.slowStrength = 0.0f;
        infection.slowRemaining = 0.0f;
        infection.slowDuration = 0.0f;
        infection.timeToNextStage = 0.0f;
        return;
    }
    infection.timeToNextStage =
        InfectionSecondsToNextStage(infection, wounds, safe);
}

bool TryInfectWithRisk(ecs::Infection& infection, float risk,
                       std::uint64_t seed, const ecs::Wounds* wounds,
                       const InfectionParams& params) {
    if (!(risk > 0.0f) || !std::isfinite(risk)) {
        return false;
    }
    if (risk >= 1.0f) {
        return TryInfect(infection, 1.0f, 0.0f, wounds, params);
    }
    std::uint64_t x = seed + 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30u)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27u)) * 0x94D049BB133111EBull;
    x ^= x >> 31u;
    const float roll = static_cast<float>(
        static_cast<double>(x >> 11u) * (1.0 / 9007199254740992.0));
    return TryInfect(infection, risk, roll, wounds, params);
}

bool TryInfect(ecs::Infection& infection, float chance, float roll,
               const ecs::Wounds* wounds, const InfectionParams& params) {
    const InfectionParams safe = Sanitize(params);
    if (!safe.enabled || infection.stage != ecs::InfectionStage::None ||
        !std::isfinite(chance) || !std::isfinite(roll) || chance <= 0.0f ||
        roll < 0.0f || roll >= 1.0f) {
        return false;
    }
    if (roll >= ClampChance(chance, safe.maxChance)) {
        return false;
    }
    infection = {};
    infection.stage = ecs::InfectionStage::Incubating;
    infection.progress = 0.0f;
    RefreshInfection(infection, wounds, safe);
    return true;
}

InfectionTreatment TreatInfection(ecs::Infection& infection, float cure,
                                  float slowStrength, float slowSeconds,
                                  const ecs::Wounds* wounds,
                                  const InfectionParams& params) {
    InfectionTreatment result;
    if (infection.stage == ecs::InfectionStage::None) {
        return result;
    }
    const float progressBefore = Clamp01(infection.progress);
    if (!std::isfinite(cure) && !std::isfinite(slowStrength)) {
        return result;
    }
    const float reduction = std::clamp(
        std::isfinite(cure) ? cure : 0.0f, 0.0f, 1.0f);
    const float slow = std::clamp(
        std::isfinite(slowStrength) ? slowStrength : 0.0f, 0.0f, 1.0f);
    const float seconds = std::isfinite(slowSeconds)
                              ? std::clamp(slowSeconds, 0.0f, 3600.0f)
                              : 0.0f;
    if (reduction <= 0.0f && (slow <= 0.0f || seconds <= 0.0f)) {
        return result;
    }

    infection.progress = Clamp01(infection.progress - reduction);
    result.progressReduced = progressBefore - infection.progress;
    if (slow > 0.0f && seconds > 0.0f) {
        infection.slowStrength = std::max(infection.slowStrength, slow);
        infection.slowDuration = seconds;
        infection.slowRemaining = seconds;
    }
    result.applied = true;
    result.cured = infection.progress <= 0.0001f;
    if (result.cured) {
        infection = {};
        return result;
    }
    RefreshInfection(infection, wounds, params);
    return result;
}

InfectionSystem::InfectionSystem(InfectionParams params)
    : m_params(Sanitize(params)) {}

void InfectionSystem::Update(ecs::EcsWorld& world, float dt) {
    if (!m_params.enabled) {
        return;
    }
    const float step = std::isfinite(dt) ? std::max(0.0f, dt) : 0.0f;
    if (step <= 0.0f) {
        return;
    }
    entt::registry& registry = world.Registry();
    registry.view<ecs::PlayerTag, ecs::Infection, ecs::Health>().each(
        [&](const ecs::Entity entity, ecs::Infection& infection,
            ecs::Health& health) {
            if (!world.Alive(entity) || health.current <= 0.0f ||
                registry.any_of<ecs::Dying>(entity) ||
                infection.stage == ecs::InfectionStage::None) {
                return;
            }

            const ecs::Wounds* wounds = registry.try_get<ecs::Wounds>(entity);
            if (infection.slowRemaining > 0.0f) {
                infection.slowRemaining =
                    std::max(0.0f, infection.slowRemaining - step);
                if (infection.slowRemaining <= 0.0f) {
                    infection.slowStrength = 0.0f;
                    infection.slowDuration = 0.0f;
                }
            }

            const ecs::InfectionStage before = infection.stage;
            const float rate = InfectionProgressPerSecond(infection, wounds,
                                                          m_params);
            infection.progress =
                Clamp01(infection.progress + rate * step);
            infection.stage = InfectionStageForProgress(infection.progress,
                                                        m_params);
            infection.timeToNextStage =
                InfectionSecondsToNextStage(infection, wounds, m_params);

            ecs::Needs* needs = registry.try_get<ecs::Needs>(entity);
            if (needs != nullptr) {
                const float fatigueRate =
                    infection.stage == ecs::InfectionStage::Severe
                        ? m_params.severeFatiguePerSecond
                    : infection.stage == ecs::InfectionStage::Fever
                        ? m_params.feverFatiguePerSecond
                        : 0.0f;
                needs->fatigue = Clamp01(
                    needs->fatigue + fatigueRate * step);
            }

            if (infection.stage == ecs::InfectionStage::Severe &&
                m_params.severeHealthDrainPerSecond > 0.0f) {
                const float damage = m_params.severeHealthDrainPerSecond * step;
                if (damage > 0.0f) {
                    health.current = std::max(0.0f, health.current - damage);
                    ecs::LastDamage& last =
                        registry.get_or_emplace<ecs::LastDamage>(entity);
                    last.cause = ecs::DamageCause::Infection;
                    last.amount = damage;
                }
            }

            if (before != infection.stage) {
                ZL_INFO("infection", "玩家感染阶段：{} → {}（稳定 ID {}，病程 {:.0f}%）",
                        StageName(before), StageName(infection.stage),
                        world.IdOf(entity).id,
                        static_cast<double>(infection.progress * 100.0f));
            }

            if (infection.progress >= 1.0f) {
                health.current = 0.0f;
                ecs::LastDamage& last =
                    registry.get_or_emplace<ecs::LastDamage>(entity);
                last.cause = ecs::DamageCause::Infection;
                last.amount = 1.0f;
                infection.timeToNextStage = 0.0f;
                ZL_INFO("infection", "玩家感染死亡：稳定 ID {}，病程 100%",
                        world.IdOf(entity).id);
            }
        });
}

}  // namespace zl::characters
