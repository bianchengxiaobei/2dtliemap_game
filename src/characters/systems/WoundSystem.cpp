#include "characters/systems/WoundSystem.h"

#include <algorithm>
#include <cmath>

#include <entt/entt.hpp>

#include "characters/components/CharacterComponents.h"
#include "core/Log.h"
#include "ecs/EcsWorld.h"

namespace zl::characters {
namespace {

constexpr float kMinWoundSeverity = 0.05f;

bool IsWoundSource(ecs::DamageType type) {
    return type == ecs::DamageType::Blunt || type == ecs::DamageType::Claw ||
           type == ecs::DamageType::Bite;
}

float NonNegative(float value) {
    return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
}

float SeverityScale(ecs::DamageType type, const WoundParams& params) {
    switch (type) {
        case ecs::DamageType::Claw:
            return NonNegative(params.clawSeverityScale);
        case ecs::DamageType::Bite:
            return NonNegative(params.biteSeverityScale);
        case ecs::DamageType::Blunt:
            return NonNegative(params.bluntSeverityScale);
        case ecs::DamageType::Debug:
        case ecs::DamageType::Fire:
            return 0.0f;
    }
    return 0.0f;
}

}  // namespace

void AddWound(ecs::Wounds& wounds, ecs::DamageType type, float damage,
              const WoundParams& params) {
    if (!IsWoundSource(type) || !std::isfinite(damage) || !(damage > 0.0f)) {
        return;
    }
    if (wounds.count >= ecs::kMaxWounds) {
        wounds.count = ecs::kMaxWounds;
    }

    ecs::WoundEntry entry;
    entry.source = type;
    const float severityScale = std::max(0.0f, params.severityPerDamage) *
                                SeverityScale(type, params);
    entry.severity = std::clamp(damage * severityScale, 0.02f, 1.0f);

    if (type == ecs::DamageType::Claw) {
        entry.bleedRate = NonNegative(damage) * NonNegative(params.clawBleedPerDamage);
        entry.bleedDuration = NonNegative(params.clawBleedSeconds);
    } else if (type == ecs::DamageType::Bite) {
        entry.bleedRate = NonNegative(damage) * NonNegative(params.biteBleedPerDamage);
        entry.bleedDuration = NonNegative(params.biteBleedSeconds);
    }
    entry.bleedRate =
        std::clamp(entry.bleedRate, 0.0f, NonNegative(params.maxBleedRate));
    entry.bleedRemaining = entry.bleedDuration;
    entry.bandaged = false;

    if (wounds.count < ecs::kMaxWounds) {
        wounds.entries[wounds.count] = entry;
        ++wounds.count;
        return;
    }

    std::size_t weakest = 0;
    for (std::size_t i = 1; i < ecs::kMaxWounds; ++i) {
        if (wounds.entries[i].severity < wounds.entries[weakest].severity) {
            weakest = i;
        }
    }
    if (entry.severity >= wounds.entries[weakest].severity) {
        wounds.entries[weakest] = entry;
    }
}

WoundTreatment TreatWounds(ecs::Wounds& wounds, float strength) {
    WoundTreatment result;
    if (!std::isfinite(strength) || strength <= 0.0f) {
        return result;
    }
    const float reduce = std::clamp(strength, 0.0f, 1.0f);
    const std::uint8_t count = std::min<std::uint8_t>(wounds.count, ecs::kMaxWounds);

    std::uint8_t write = 0;
    for (std::uint8_t i = 0; i < count; ++i) {
        const ecs::WoundEntry source = wounds.entries[i];
        ecs::WoundEntry entry = source;
        if (entry.bleedRemaining > 0.0f || entry.bleedRate > 0.0f) {
            result.stoppedBleeding = true;
        }
        entry.bleedRemaining = 0.0f;
        entry.bleedRate = 0.0f;
        entry.bandaged = true;
        const float before = entry.severity;
        entry.severity = std::clamp(entry.severity * (1.0f - reduce), 0.0f, 1.0f);
        ++result.wounds;
        if (entry.severity <= kMinWoundSeverity) {
            result.severityRemoved += before;
            continue;
        }
        result.severityRemoved += before - entry.severity;
        wounds.entries[write] = entry;
        ++write;
    }
    for (std::uint8_t i = write; i < ecs::kMaxWounds; ++i) {
        wounds.entries[i] = ecs::WoundEntry{};
    }
    wounds.count = write;
    return result;
}

int WoundCount(const ecs::Wounds& wounds) {
    return std::min<std::uint8_t>(wounds.count, ecs::kMaxWounds);
}

float WorstWoundSeverity(const ecs::Wounds& wounds) {
    float worst = 0.0f;
    const std::uint8_t count = std::min<std::uint8_t>(wounds.count, ecs::kMaxWounds);
    for (std::uint8_t i = 0; i < count; ++i) {
        worst = std::max(worst, std::clamp(wounds.entries[i].severity, 0.0f, 1.0f));
    }
    return worst;
}

float CurrentBleedRate(const ecs::Wounds& wounds) {
    float rate = 0.0f;
    const std::uint8_t count = std::min<std::uint8_t>(wounds.count, ecs::kMaxWounds);
    for (std::uint8_t i = 0; i < count; ++i) {
        const ecs::WoundEntry& entry = wounds.entries[i];
        if (entry.bandaged || entry.bleedRemaining <= 0.0f ||
            entry.bleedDuration <= 0.0f || entry.bleedRate <= 0.0f) {
            continue;
        }
        const float progress =
            std::clamp(entry.bleedRemaining / entry.bleedDuration, 0.0f, 1.0f);
        rate += entry.bleedRate * progress;
    }
    return rate;
}

void WoundSystem::Update(ecs::EcsWorld& world, float dt) {
    const float step = std::isfinite(dt) ? std::max(0.0f, dt) : 0.0f;
    if (step <= 0.0f) {
        return;
    }
    entt::registry& registry = world.Registry();
    registry.view<ecs::Wounds, ecs::Health>().each(
        [&](const entt::entity entity, ecs::Wounds& wounds, ecs::Health& health) {
            if (health.current <= 0.0f || !world.Alive(entity) ||
                registry.any_of<ecs::Dying>(entity)) {
                return;
            }
            const std::uint8_t count =
                std::min<std::uint8_t>(wounds.count, ecs::kMaxWounds);
            float totalDamage = 0.0f;
            for (std::uint8_t i = 0; i < count; ++i) {
                ecs::WoundEntry& entry = wounds.entries[i];
                if (entry.bandaged || entry.bleedRemaining <= 0.0f ||
                    entry.bleedDuration <= 0.0f || entry.bleedRate <= 0.0f) {
                    continue;
                }
                const float progress =
                    std::clamp(entry.bleedRemaining / entry.bleedDuration, 0.0f, 1.0f);
                totalDamage += entry.bleedRate * progress * step;
                entry.bleedRemaining = std::max(0.0f, entry.bleedRemaining - step);
            }
            if (!(totalDamage > 0.0f)) {
                return;
            }
            const float before = health.current;
            health.current = std::max(0.0f, health.current - totalDamage);
            ecs::LastDamage& last = registry.get_or_emplace<ecs::LastDamage>(entity);
            last.cause = ecs::DamageCause::Bleed;
            last.amount = totalDamage;
            if (before > 0.0f && health.current <= 0.0f &&
                registry.all_of<ecs::PlayerTag>(entity)) {
                ZL_INFO("characters", "玩家失血过多：稳定 ID {}",
                        world.IdOf(entity).id);
            }
        });
}

}  // namespace zl::characters
