#include <doctest/doctest.h>

#include <cmath>
#include <string>

#include "game/RangedSound.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/ItemDef.h"
#include "items/RangedWeapon.h"

using namespace zl;

namespace {

items::ItemDef MakePistol() {
    items::ItemDef def;
    def.id = "pistol";
    def.name = "手枪";
    def.type = items::ItemType::Weapon;
    def.maxStack = 1;
    items::RangedCombat ranged;
    ranged.damage = 18.0f;
    ranged.range = 18.0f;
    ranged.spreadDegrees = 3.0f;
    ranged.pellets = 1;
    ranged.cooldownSeconds = 0.45f;
    ranged.magazine = 8;
    ranged.reloadSeconds = 2.0f;
    ranged.ammoDefId = "ammo_9mm";
    ranged.noiseRadius = 28.0f;
    ranged.wearPerShot = 0.01f;
    ranged.projectileSpeed = 20.0f;
    ranged.falloffStart = 8.0f;
    ranged.falloffEnd = 18.0f;
    ranged.falloffScale = 0.55f;
    def.ranged = ranged;
    return def;
}

ecs::ItemInstance MakeWeapon(int loadedAmmo, float condition = 1.0f) {
    return ecs::ItemInstance{"pistol", 1, condition, loadedAmmo};
}

}  // namespace

TEST_CASE("M23-F1-T3：远程武器开火前置条件与冷却") {
    const items::ItemDef pistol = MakePistol();
    items::RangedWeaponRuntime runtime;
    std::string reason;

    CHECK(runtime.CanFire(pistol, MakeWeapon(3), &reason));
    CHECK(reason.empty());

    reason.clear();
    CHECK_FALSE(runtime.CanFire(pistol, MakeWeapon(0), &reason));
    CHECK(reason == "empty");

    reason.clear();
    CHECK_FALSE(runtime.CanFire(pistol, MakeWeapon(-1), &reason));
    CHECK(reason == "invalid weapon");

    reason.clear();
    CHECK_FALSE(runtime.CanFire(pistol, MakeWeapon(3, 0.0f), &reason));
    CHECK(reason == "weapon broken");

    runtime.reloading = true;
    reason.clear();
    CHECK_FALSE(runtime.CanFire(pistol, MakeWeapon(3), &reason));
    CHECK(reason == "reloading");

    runtime.reloading = false;
    runtime.cooldownLeft = 0.2f;
    reason.clear();
    CHECK_FALSE(runtime.CanFire(pistol, MakeWeapon(3), &reason));
    CHECK(reason == "cooldown");

    runtime.Tick(0.1f);
    CHECK(runtime.cooldownLeft == doctest::Approx(0.1f));
    CHECK_FALSE(runtime.CanFire(pistol, MakeWeapon(3)));
    runtime.Tick(0.1f);
    CHECK(runtime.cooldownLeft == doctest::Approx(0.0f));
    CHECK(runtime.CanFire(pistol, MakeWeapon(3)));
}

TEST_CASE("M23-F1-T3：换弹计时、装填量和中断规则") {
    const items::ItemDef pistol = MakePistol();
    items::RangedWeaponRuntime runtime;
    ecs::ItemInstance weapon = MakeWeapon(3);
    std::string reason;

    CHECK_FALSE(runtime.StartReload(pistol, MakeWeapon(8), 10, &reason));
    CHECK(reason == "magazine full");

    reason.clear();
    CHECK_FALSE(runtime.StartReload(pistol, weapon, 0, &reason));
    CHECK(reason == "no ammo");

    REQUIRE(runtime.StartReload(pistol, weapon, 10, &reason));
    CHECK(runtime.reloading);
    CHECK(runtime.reloadAmount == 5);
    CHECK(runtime.reloadLeft == doctest::Approx(2.0f));

    reason.clear();
    CHECK(runtime.TickReload(1.9f, &pistol, &weapon, &reason) ==
          items::RangedReloadEvent::None);
    CHECK(runtime.reloading);
    CHECK(runtime.TickReload(0.1f, &pistol, &weapon, &reason) ==
          items::RangedReloadEvent::Completed);
    CHECK_FALSE(runtime.reloading);
    CHECK(runtime.reloadAmount == 5);

    runtime.CancelReload();
    REQUIRE(runtime.StartReload(pistol, weapon, 2, &reason));
    CHECK(runtime.reloadAmount == 2);

    items::ItemDef shotgun = pistol;
    shotgun.id = "shotgun";
    reason.clear();
    CHECK(runtime.TickReload(0.5f, &shotgun, &weapon, &reason) ==
          items::RangedReloadEvent::Cancelled);
    CHECK(reason == "weapon changed");
    CHECK_FALSE(runtime.reloading);

    ecs::ItemInstance broken = MakeWeapon(0, 0.0f);
    CHECK_FALSE(runtime.StartReload(pistol, broken, 10, &reason));
    CHECK(reason == "weapon broken");
}

TEST_CASE("M23-F1-T4：距离衰减与弹丸散布") {
    items::RangedCombat ranged;
    ranged.damage = 10.0f;
    ranged.falloffStart = 4.0f;
    ranged.falloffEnd = 10.0f;
    ranged.falloffScale = 0.5f;
    ranged.spreadDegrees = 12.0f;

    CHECK(items::RangedDamageAtDistance(ranged, 0.0f) ==
          doctest::Approx(10.0f));
    CHECK(items::RangedDamageAtDistance(ranged, 4.0f) ==
          doctest::Approx(10.0f));
    CHECK(items::RangedDamageAtDistance(ranged, 7.0f) ==
          doctest::Approx(7.5f));
    CHECK(items::RangedDamageAtDistance(ranged, 10.0f) ==
          doctest::Approx(5.0f));
    CHECK(items::RangedDamageAtDistance(ranged, 100.0f) ==
          doctest::Approx(5.0f));
    CHECK(items::RangedDamageAtDistance(10.0f, 4.0f, 4.0f, 4.0f, 0.25f) ==
          doctest::Approx(10.0f));
    CHECK(items::RangedDamageAtDistance(10.0f, 4.1f, 4.0f, 4.0f, 0.25f) ==
          doctest::Approx(2.5f));

    const float first = items::RangedSpreadOffsetDegrees(ranged, 7, 0);
    const float second = items::RangedSpreadOffsetDegrees(ranged, 7, 1);
    CHECK(std::abs(first) <= 6.0f);
    CHECK(std::abs(second) <= 6.0f);
    CHECK(first != second);

    ranged.spreadDegrees = 0.0f;
    CHECK(items::RangedSpreadOffsetDegrees(ranged, 7, 0) ==
          doctest::Approx(0.0f));
}

TEST_CASE("M23-F1-T5：远程枪声事件按 noiseRadius 生成") {
    items::RangedCombat ranged;
    ranged.noiseRadius = 28.0f;

    const core::SoundEvent shot = game::MakeRangedGunshot(3.5f, 4.5f, ranged);
    CHECK(shot.type == core::SoundType::Gunshot);
    CHECK(shot.radius == doctest::Approx(28.0f));
    CHECK(shot.x == doctest::Approx(3.5f));
    CHECK(shot.y == doctest::Approx(4.5f));

    ranged.noiseRadius = -5.0f;
    CHECK(game::MakeRangedGunshot(0.0f, 0.0f, ranged).radius == 0.0f);
}

TEST_CASE("M23-F1-T10 单机远程武器：逐发磨损与损坏拒绝") {
    items::ItemDef pistol = MakePistol();
    pistol.ranged->wearPerShot = 0.6f;
    items::RangedWeaponRuntime runtime;
    ecs::Equipment equipment;
    equipment.slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)] =
        MakeWeapon(2, 0.7f);
    auto& weapon =
        *equipment.slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)];

    std::string reason;
    CHECK(runtime.CanFire(pistol, weapon, &reason));
    --weapon.loadedAmmo;
    CHECK_FALSE(items::WearEquippedWeapon(equipment, pistol.ranged->wearPerShot));
    CHECK(weapon.condition == doctest::Approx(0.1f));

    CHECK(runtime.CanFire(pistol, weapon, &reason));
    --weapon.loadedAmmo;
    CHECK(items::WearEquippedWeapon(equipment, pistol.ranged->wearPerShot));
    CHECK(weapon.condition == doctest::Approx(0.0f));
    CHECK(weapon.loadedAmmo == 0);

    reason.clear();
    CHECK_FALSE(runtime.CanFire(pistol, weapon, &reason));
    CHECK(reason == "weapon broken");

    reason.clear();
    CHECK_FALSE(runtime.StartReload(pistol, weapon, 8, &reason));
    CHECK(reason == "weapon broken");
}

TEST_CASE("M23-F1-T3：换弹完成由调用方按剩余弹药结算") {
    const items::ItemDef pistol = MakePistol();
    items::RangedWeaponRuntime runtime;
    ecs::ItemInstance weapon = MakeWeapon(6);
    REQUIRE(runtime.StartReload(pistol, weapon, 5));
    CHECK(runtime.reloadAmount == 2);
    CHECK(runtime.TickReload(2.0f, &pistol, &weapon) ==
          items::RangedReloadEvent::Completed);

    const int requested = runtime.reloadAmount;
    runtime.CancelReload();
    CHECK(requested == 2);
    CHECK_FALSE(runtime.reloading);
    CHECK(runtime.reloadAmount == 0);
}
