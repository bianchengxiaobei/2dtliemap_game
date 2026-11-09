#include "items/RangedWeapon.h"

#include <algorithm>

namespace zl::items {
namespace {

void SetReason(std::string* reason, const char* text) {
    if (reason != nullptr) {
        *reason = text;
    }
}

bool IsUsableWeapon(const ItemDef& def, const ecs::ItemInstance& weapon) {
    return def.ranged.has_value() && weapon.defId == def.id && weapon.Valid();
}

std::uint32_t MixSpreadHash(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value;
}

}  // namespace

void RangedWeaponRuntime::Tick(float dt) {
    if (cooldownLeft > 0.0f && dt > 0.0f) {
        cooldownLeft = std::max(0.0f, cooldownLeft - dt);
    }
}

bool RangedWeaponRuntime::CanFire(const ItemDef& def,
                                  const ecs::ItemInstance& weapon,
                                  std::string* reason) const {
    if (!def.ranged.has_value()) {
        SetReason(reason, "not a ranged weapon");
        return false;
    }
    if (!IsUsableWeapon(def, weapon)) {
        SetReason(reason, "invalid weapon");
        return false;
    }
    if (weapon.condition <= 0.0f) {
        SetReason(reason, "weapon broken");
        return false;
    }
    if (weapon.loadedAmmo > def.ranged->magazine) {
        SetReason(reason, "invalid magazine count");
        return false;
    }
    if (reloading) {
        SetReason(reason, "reloading");
        return false;
    }
    if (cooldownLeft > 0.0f) {
        SetReason(reason, "cooldown");
        return false;
    }
    if (weapon.loadedAmmo <= 0) {
        SetReason(reason, "empty");
        return false;
    }
    return true;
}

bool RangedWeaponRuntime::StartReload(const ItemDef& def,
                                      const ecs::ItemInstance& weapon,
                                      int availableAmmo,
                                      std::string* reason) {
    if (!def.ranged.has_value()) {
        SetReason(reason, "not a ranged weapon");
        return false;
    }
    if (!IsUsableWeapon(def, weapon)) {
        SetReason(reason, "invalid weapon");
        return false;
    }
    if (weapon.condition <= 0.0f) {
        SetReason(reason, "weapon broken");
        return false;
    }
    if (reloading) {
        SetReason(reason, "already reloading");
        return false;
    }
    if (weapon.loadedAmmo < 0 || weapon.loadedAmmo > def.ranged->magazine) {
        SetReason(reason, "invalid magazine count");
        return false;
    }
    if (weapon.loadedAmmo >= def.ranged->magazine) {
        SetReason(reason, "magazine full");
        return false;
    }
    if (availableAmmo <= 0) {
        SetReason(reason, "no ammo");
        return false;
    }

    const int missing = def.ranged->magazine - weapon.loadedAmmo;
    reloading = true;
    reloadLeft = std::max(0.01f, def.ranged->reloadSeconds);
    reloadAmount = std::min(missing, availableAmmo);
    reloadWeaponId = def.id;
    return true;
}

RangedReloadEvent RangedWeaponRuntime::TickReload(
    float dt, const ItemDef* currentDef, const ecs::ItemInstance* currentWeapon,
    std::string* reason) {
    if (!reloading) {
        return RangedReloadEvent::None;
    }
    if (currentDef == nullptr || currentWeapon == nullptr ||
        !currentDef->ranged.has_value() ||
        currentDef->id != reloadWeaponId ||
        currentWeapon->defId != reloadWeaponId ||
        !currentWeapon->Valid()) {
        CancelReload(nullptr);
        SetReason(reason, "weapon changed");
        return RangedReloadEvent::Cancelled;
    }
    if (currentWeapon->condition <= 0.0f) {
        CancelReload(nullptr);
        SetReason(reason, "weapon broken");
        return RangedReloadEvent::Cancelled;
    }

    if (dt > 0.0f) {
        reloadLeft = std::max(0.0f, reloadLeft - dt);
    }
    if (reloadLeft > 1e-4f) {
        return RangedReloadEvent::None;
    }
    reloading = false;
    return RangedReloadEvent::Completed;
}

void RangedWeaponRuntime::CancelReload(std::string* reason) {
    if (reason != nullptr && reloading) {
        *reason = "reload cancelled";
    }
    reloading = false;
    reloadLeft = 0.0f;
    reloadAmount = 0;
    reloadWeaponId.clear();
}

float RangedDamageAtDistance(float damage, float distance, float falloffStart,
                             float falloffEnd, float falloffScale) {
    const float scale = std::clamp(falloffScale, 0.0f, 1.0f);
    const float d = std::max(0.0f, distance);
    if (d <= falloffStart) {
        return damage;
    }
    if (falloffEnd <= falloffStart) {
        return damage * scale;
    }
    if (d >= falloffEnd) {
        return damage * scale;
    }
    const float t = (d - falloffStart) / (falloffEnd - falloffStart);
    return damage * (1.0f + (scale - 1.0f) * t);
}

float RangedDamageAtDistance(const RangedCombat& ranged, float distance) {
    return RangedDamageAtDistance(ranged.damage, distance, ranged.falloffStart,
                                  ranged.falloffEnd, ranged.falloffScale);
}

float RangedSpreadOffsetDegrees(const RangedCombat& ranged,
                                std::uint32_t shotSerial,
                                std::uint32_t pelletIndex) {
    if (!(ranged.spreadDegrees > 0.0f)) {
        return 0.0f;
    }
    const std::uint32_t hash =
        MixSpreadHash(shotSerial * 0x9E3779B9u +
                      (pelletIndex + 1u) * 0x85EBCA6Bu);
    const float unit = static_cast<float>(hash & 0x00FFFFFFu) /
                       static_cast<float>(0x01000000u);
    return (unit - 0.5f) * ranged.spreadDegrees;
}

}  // namespace zl::items
