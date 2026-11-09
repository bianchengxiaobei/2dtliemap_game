#include "game/FoodSafety.h"

#include <algorithm>
#include <cmath>

#include "characters/components/CombatComponents.h"
#include "ecs/EcsWorld.h"

namespace zl::game {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void MixBytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= kFnvPrime;
    }
}

float Clamp01(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

float ClampNonNegative(float value) {
    return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
}

}  // namespace

bool IsFoodUse(const items::UseEffect& use) {
    return use.hunger > 0.0f || use.thirst > 0.0f || use.nutrition > 0.0f ||
           use.foodPoisonChance > 0.0f;
}

float FoodPoisonChance(const ecs::FoodState& state,
                       const items::ItemDef& def, bool spoiled,
                       const FoodSafetyParams& params) {
    float chance = Clamp01(def.use.foodPoisonChance);
    if (spoiled) {
        chance = std::max(chance, Clamp01(params.spoiledPoisonChance));
    }
    const float nutrition =
        Clamp01(state.nutrition) * Clamp01(params.nutritionPoisonReduction);
    return Clamp01(chance * (1.0f - nutrition));
}

bool FoodPoisonRoll(float chance, std::uint64_t seed) {
    if (!std::isfinite(chance) || chance <= 0.0f) {
        return false;
    }
    if (chance >= 1.0f) {
        return true;
    }
    std::uint64_t x = seed + 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30u)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27u)) * 0x94D049BB133111EBull;
    x ^= x >> 31u;
    const double unit =
        static_cast<double>(x >> 11u) * (1.0 / 9007199254740992.0);
    return unit < static_cast<double>(chance);
}

std::uint64_t DeriveFoodPoisonSeed(std::uint64_t stableId,
                                   std::uint64_t serial,
                                   std::string_view defId) {
    std::uint64_t hash = kFnvOffset;
    MixBytes(hash, &stableId, sizeof(stableId));
    MixBytes(hash, &serial, sizeof(serial));
    MixBytes(hash, defId.data(), defId.size());
    return hash;
}

FoodConsumption ApplyFoodConsumption(ecs::FoodState& state,
                                     const items::ItemDef& def, bool spoiled,
                                     const FoodSafetyParams& params,
                                     std::uint64_t seed) {
    FoodConsumption result;
    if (!IsFoodUse(def.use)) {
        return result;
    }
    state.nutrition = Clamp01(state.nutrition);
    state.poisonRemaining = ClampNonNegative(state.poisonRemaining);
    state.poisonDamagePerSecond =
        ClampNonNegative(state.poisonDamagePerSecond);

    result.poisonChance = FoodPoisonChance(state, def, spoiled, params);
    result.nutritionGained = Clamp01(def.use.nutrition);
    if (result.poisonChance > 0.0f &&
        FoodPoisonRoll(result.poisonChance, seed)) {
        result.poisoned = true;
        const float duration =
            ClampNonNegative(params.poisonDurationSeconds);
        const float dps = ClampNonNegative(params.poisonDamagePerSecond);
        state.poisonRemaining = std::max(state.poisonRemaining, duration);
        state.poisonDamagePerSecond =
            std::max(state.poisonDamagePerSecond, dps);
        state.poisonTotal = std::max(state.poisonTotal, state.poisonRemaining);
    }
    state.nutrition = Clamp01(state.nutrition + result.nutritionGained);
    return result;
}

float AdvanceFoodState(ecs::FoodState& state, float dtSeconds,
                       float gameMinutes, const FoodSafetyParams& params) {
    if (std::isfinite(gameMinutes) && gameMinutes > 0.0f) {
        const float decay =
            ClampNonNegative(params.nutritionDecayPerDay) *
            gameMinutes / 1440.0f;
        state.nutrition = Clamp01(state.nutrition - decay);
    }
    if (!(dtSeconds > 0.0f) || !std::isfinite(dtSeconds) ||
        !(state.poisonRemaining > 0.0f)) {
        return 0.0f;
    }
    const float step = std::min(dtSeconds, state.poisonRemaining);
    const float damage =
        step * ClampNonNegative(state.poisonDamagePerSecond);
    state.poisonRemaining =
        std::max(0.0f, state.poisonRemaining - step);
    if (state.poisonRemaining <= 1e-4f) {
        state.poisonRemaining = 0.0f;
        state.poisonDamagePerSecond = 0.0f;
        state.poisonTotal = 0.0f;
    }
    return damage;
}

void AdvanceFoodStates(ecs::EcsWorld& world, float dtSeconds,
                       float gameMinutes, const FoodSafetyParams& params) {
    if (!(dtSeconds > 0.0f) && !(gameMinutes > 0.0f)) {
        return;
    }
    entt::registry& registry = world.Registry();
    registry.view<ecs::FoodState, ecs::Health>().each(
        [&](const ecs::Entity entity, ecs::FoodState& food,
            ecs::Health& health) {
            if (health.current <= 0.0f) {
                return;
            }
            const float damage =
                AdvanceFoodState(food, dtSeconds, gameMinutes, params);
            if (damage > 0.0f) {
                health.current = std::max(0.0f, health.current - damage);
                ecs::LastDamage& last =
                    registry.get_or_emplace<ecs::LastDamage>(entity);
                last.cause = ecs::DamageCause::Poison;
                last.amount = damage;
            }
        });
}

}  // namespace zl::game
