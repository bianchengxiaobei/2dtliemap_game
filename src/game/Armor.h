#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "characters/components/CombatComponents.h"
#include "items/ItemComponents.h"
#include "items/ItemDef.h"
#include "items/ItemRegistry.h"

namespace zl::game {

struct PlayerArmor {
    bool hasArmor = false;
    bool broken = false;
    float condition = 1.0f;
    float insulation = 0.0f;
    float waterproof = 0.0f;
    std::string name;
    ecs::DamageResist resist;
};

inline PlayerArmor ResolvePlayerArmor(const ecs::Equipment& equipment,
                                      const items::ItemRegistry& registry) {
    PlayerArmor result;
    const auto& slot =
        equipment.slots[static_cast<std::size_t>(ecs::EquipSlot::Body)];
    if (!slot.has_value()) {
        return result;
    }
    const items::ItemDef* def = registry.Find(slot->defId);
    if (def == nullptr || def->type != items::ItemType::Clothing ||
        !def->armor.Any()) {
        return result;
    }
    result.hasArmor = true;
    result.name = def->name;
    result.condition =
        std::clamp(std::isfinite(slot->condition) ? slot->condition : 0.0f,
                   0.0f, 1.0f);
    result.broken = result.condition <= 0.0f;
    const items::ArmorProtection protection =
        items::ResolveArmor(def->armor, result.condition);
    result.resist.clawReduction = protection.clawReduction;
    result.resist.biteReduction = protection.biteReduction;
    result.resist.bluntReduction = protection.bluntReduction;
    result.resist.wearPerHit = protection.wearPerHit;
    result.resist.moveScale = protection.moveScale;
    result.insulation = protection.insulation;
    result.waterproof = protection.waterproof;
    return result;
}

inline std::string ArmorStatusText(const PlayerArmor& armor,
                                   std::string* tone = nullptr) {
    if (!armor.hasArmor) {
        if (tone != nullptr) {
            *tone = "info";
        }
        return {};
    }
    const int movePercent =
        static_cast<int>(std::lround(std::clamp(armor.resist.moveScale, 0.5f, 1.0f) *
                                     100.0f));
    if (armor.broken) {
        if (tone != nullptr) {
            *tone = "fail";
        }
        return "护甲已损坏（无减伤） · 移速 " + std::to_string(movePercent) + "%";
    }
    const int claw = static_cast<int>(
        std::lround(std::clamp(armor.resist.clawReduction, 0.0f, 0.95f) * 100.0f));
    const int bite = static_cast<int>(
        std::lround(std::clamp(armor.resist.biteReduction, 0.0f, 0.95f) * 100.0f));
    const int blunt = static_cast<int>(
        std::lround(std::clamp(armor.resist.bluntReduction, 0.0f, 0.95f) * 100.0f));
    if (tone != nullptr) {
        *tone = armor.resist.moveScale < 0.95f || armor.condition < 0.35f ? "warn"
                                                                           : "info";
    }
    return "护甲 爪 " + std::to_string(claw) + "% 咬 " + std::to_string(bite) +
           "% 钝 " + std::to_string(blunt) + "% · 移速 " +
           std::to_string(movePercent) + "%";
}

}  // namespace zl::game
