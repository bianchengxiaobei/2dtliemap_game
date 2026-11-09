#include "characters/systems/PainSystem.h"

#include <algorithm>
#include <cmath>

#include <entt/entt.hpp>

#include "ecs/EcsWorld.h"

namespace zl::characters {
namespace {

float NonNegative(float value) {
    return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
}

}  // namespace

float ComputeBasePain(const ecs::Wounds& wounds, const PainParams& params) {
    const std::uint8_t count = std::min<std::uint8_t>(wounds.count, ecs::kMaxWounds);
    const float bandagedScale = std::clamp(NonNegative(params.bandagedScale), 0.0f, 1.0f);
    float sum = 0.0f;
    for (std::uint8_t i = 0; i < count; ++i) {
        const ecs::WoundEntry& entry = wounds.entries[i];
        const float severity = std::clamp(NonNegative(entry.severity), 0.0f, 1.0f);
        sum += entry.bandaged ? severity * bandagedScale : severity;
    }
    const float maxLevel = std::clamp(params.maxLevel, 0.0f, 1.0f);
    return std::clamp(sum * NonNegative(params.perSeverity), 0.0f, maxLevel);
}

void RefreshPain(ecs::Pain& pain) {
    const float base = std::clamp(pain.baseLevel, 0.0f, 1.0f);
    const float strength = std::clamp(pain.reliefStrength, 0.0f, 1.0f);
    const float duration = NonNegative(pain.reliefDuration);
    const float remaining = std::clamp(pain.reliefRemaining, 0.0f, duration);
    const float reliefProgress = duration > 0.0f ? remaining / duration : 0.0f;
    pain.level = std::clamp(base * (1.0f - strength * reliefProgress), 0.0f, 1.0f);
    pain.moveScale = 1.0f - pain.level * std::clamp(pain.maxMoveSlow, 0.0f, 0.9f);
    pain.attackCooldownScale =
        1.0f + pain.level * std::clamp(pain.maxAttackCooldownIncrease, 0.0f, 2.0f);
}

bool ApplyPainRelief(ecs::Pain& pain, float strength, float seconds) {
    if (!std::isfinite(strength) || !(strength > 0.0f) || !std::isfinite(seconds) ||
        !(seconds > 0.0f)) {
        return false;
    }
    pain.reliefStrength =
        std::max(pain.reliefStrength, std::clamp(strength, 0.0f, 1.0f));
    pain.reliefDuration = std::clamp(seconds, 0.1f, 600.0f);
    pain.reliefRemaining = pain.reliefDuration;
    RefreshPain(pain);
    return true;
}

void PainSystem::Update(ecs::EcsWorld& world, float dt) {
    const float step = std::isfinite(dt) ? std::max(0.0f, dt) : 0.0f;
    entt::registry& registry = world.Registry();
    registry.view<ecs::Pain>().each([&](const ecs::Entity entity, ecs::Pain& pain) {
        if (!world.Alive(entity)) {
            return;
        }
        const auto* wounds = registry.try_get<ecs::Wounds>(entity);
        pain.baseLevel = wounds != nullptr ? ComputeBasePain(*wounds, m_params) : 0.0f;
        pain.maxMoveSlow = std::clamp(NonNegative(m_params.maxMoveSlow), 0.0f, 0.9f);
        pain.maxAttackCooldownIncrease =
            std::clamp(NonNegative(m_params.maxAttackCooldownIncrease), 0.0f, 2.0f);
        if (pain.reliefRemaining > 0.0f) {
            pain.reliefRemaining = std::max(0.0f, pain.reliefRemaining - step);
        }
        if (pain.reliefRemaining <= 0.0f || pain.reliefDuration <= 0.0f) {
            pain.reliefRemaining = 0.0f;
            pain.reliefDuration = 0.0f;
            pain.reliefStrength = 0.0f;
        }
        RefreshPain(pain);
    });
}

}  // namespace zl::characters
