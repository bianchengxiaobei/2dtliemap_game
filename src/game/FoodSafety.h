#pragma once

#include <cstdint>
#include <string_view>

#include "characters/components/CharacterComponents.h"
#include "items/ItemDef.h"

namespace zl::ecs {
class EcsWorld;
}

namespace zl::game {

// F5-T3：食物营养与中毒参数。营养只影响饥饿/中毒风险；中毒按真实秒数结算。
struct FoodSafetyParams {
    float spoiledPoisonChance = 0.85f;
    float poisonDurationSeconds = 45.0f;
    float poisonDamagePerSecond = 0.12f;
    float nutritionDecayPerDay = 0.5f;
    float nutritionPoisonReduction = 0.5f;
};

struct FoodConsumption {
    float poisonChance = 0.0f;
    bool poisoned = false;
    float nutritionGained = 0.0f;
};

bool IsFoodUse(const items::UseEffect& use);

float FoodPoisonChance(const ecs::FoodState& state,
                       const items::ItemDef& def, bool spoiled,
                       const FoodSafetyParams& params);

// 确定性中毒判定：同 seed/chance 结果可复现，便于服务器与测试。
bool FoodPoisonRoll(float chance, std::uint64_t seed);

std::uint64_t DeriveFoodPoisonSeed(std::uint64_t stableId,
                                   std::uint64_t serial,
                                   std::string_view defId);

// 食用后更新营养/中毒状态；返回值供提示与测试使用。
FoodConsumption ApplyFoodConsumption(ecs::FoodState& state,
                                     const items::ItemDef& def, bool spoiled,
                                     const FoodSafetyParams& params,
                                     std::uint64_t seed);

// 推进营养衰减与中毒计时；返回本步应扣的生命值。
float AdvanceFoodState(ecs::FoodState& state, float dtSeconds,
                       float gameMinutes, const FoodSafetyParams& params);

// 固定链系统入口：对带 FoodState 的实体结算中毒伤害。
void AdvanceFoodStates(ecs::EcsWorld& world, float dtSeconds,
                       float gameMinutes, const FoodSafetyParams& params);

}  // namespace zl::game
