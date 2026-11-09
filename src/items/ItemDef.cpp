#include "items/ItemDef.h"

#include <algorithm>
#include <cmath>

namespace zl::items {

const char* ItemTypeToString(ItemType type) {
    switch (type) {
        case ItemType::Generic:
            return "generic";
        case ItemType::Weapon:
            return "weapon";
        case ItemType::Consumable:
            return "consumable";
        case ItemType::Clothing:
            return "clothing";
        case ItemType::Container:
            return "container";
    }
    return "generic";
}

WeaponAttackParams ResolveWeaponAttack(const WeaponCombatFields& combat,
                                       const WeaponAttackParams& fallback) {
    WeaponAttackParams out = fallback;
    if (combat.damage.has_value()) {
        out.damage = *combat.damage;
    }
    if (combat.range.has_value()) {
        out.range = *combat.range;
    }
    if (combat.arcDegrees.has_value()) {
        out.arcDegrees = *combat.arcDegrees;
    }
    if (combat.cooldownSeconds.has_value()) {
        out.cooldownSeconds = *combat.cooldownSeconds;
    }
    if (combat.knockback.has_value()) {
        out.knockback = *combat.knockback;
    }
    if (combat.staggerSeconds.has_value()) {
        out.staggerSeconds = *combat.staggerSeconds;
    }
    if (combat.wearPerHit.has_value()) {
        out.wearPerHit = *combat.wearPerHit;
    }
    if (combat.staminaCost.has_value()) {
        out.staminaCost = *combat.staminaCost;
    }
    if (combat.windupSeconds.has_value()) {
        out.windupSeconds = *combat.windupSeconds;
    }
    if (combat.poiseDamage.has_value()) {
        out.poiseDamage = *combat.poiseDamage;
    }
    return out;
}

ArmorProtection ResolveArmor(const ArmorStats& armor, float condition) {
    ArmorProtection out;
    const float scale = std::clamp(std::isfinite(condition) ? condition : 0.0f, 0.0f, 1.0f);
    out.clawReduction = std::clamp(armor.clawReduction, 0.0f, 0.95f) * scale;
    out.biteReduction = std::clamp(armor.biteReduction, 0.0f, 0.95f) * scale;
    out.bluntReduction = std::clamp(armor.bluntReduction, 0.0f, 0.95f) * scale;
    out.wearPerHit = std::clamp(armor.wearPerHit, 0.0f, 1.0f);
    out.moveScale = std::clamp(armor.moveScale, 0.5f, 1.0f);
    out.insulation = std::clamp(armor.insulation, 0.0f, 1.0f);
    out.waterproof = std::clamp(armor.waterproof, 0.0f, 1.0f);
    return out;
}

bool ParseItemType(std::string_view text, ItemType* out) {
    if (out == nullptr) {
        return false;
    }
    if (text == "generic") {
        *out = ItemType::Generic;
        return true;
    }
    if (text == "weapon") {
        *out = ItemType::Weapon;
        return true;
    }
    if (text == "consumable") {
        *out = ItemType::Consumable;
        return true;
    }
    if (text == "clothing") {
        *out = ItemType::Clothing;
        return true;
    }
    if (text == "container") {
        *out = ItemType::Container;
        return true;
    }
    return false;
}

}  // namespace zl::items
