#include <doctest/doctest.h>

#include <string>

#include "game/Armor.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"

using namespace zl;

namespace {

const char* kItemsJson = R"({
  "format": "items-v1",
  "items": [
    { "id": "jacket", "name": "夹克", "icon": "i", "weight": 1.2, "maxStack": 1,
      "type": "clothing", "repairEfficiency": 1.2,
      "armor": { "claw": 0.4, "bite": 0.2, "blunt": 0.1, "wear": 0.02,
                 "moveScale": 0.9, "insulation": 0.4, "waterproof": 0.2 } },
    { "id": "plain", "name": "普通衣服", "icon": "i", "weight": 0.5, "maxStack": 1,
      "type": "clothing" },
    { "id": "axe", "name": "斧", "icon": "i", "weight": 3, "maxStack": 1,
      "type": "weapon" },
    { "id": "repair_kit", "name": "维修包", "icon": "i", "weight": 0.3,
      "maxStack": 5, "type": "generic" }
  ]
})";

items::ItemRegistry MakeRegistry() {
    items::ItemRegistry registry;
    std::string error;
    REQUIRE(registry.LoadFromString(kItemsJson, "test-armor", &error));
    REQUIRE(error.empty());
    return registry;
}

}  // namespace

TEST_CASE("M19-F4：装备护甲按耐久解析为有效减伤与移速") {
    const items::ItemRegistry registry = MakeRegistry();
    ecs::Equipment equipment;
    equipment.slots[static_cast<std::size_t>(ecs::EquipSlot::Body)] =
        ecs::ItemInstance{"jacket", 1, 0.5f};

    const game::PlayerArmor armor = game::ResolvePlayerArmor(equipment, registry);
    REQUIRE(armor.hasArmor);
    CHECK_FALSE(armor.broken);
    CHECK(armor.name == "夹克");
    CHECK(armor.condition == doctest::Approx(0.5f));
    CHECK(armor.resist.clawReduction == doctest::Approx(0.2f));
    CHECK(armor.resist.biteReduction == doctest::Approx(0.1f));
    CHECK(armor.resist.bluntReduction == doctest::Approx(0.05f));
    CHECK(armor.resist.wearPerHit == doctest::Approx(0.02f));
    CHECK(armor.resist.moveScale == doctest::Approx(0.9f));

    std::string tone;
    const std::string status = game::ArmorStatusText(armor, &tone);
    CHECK(status.find("爪 20%") != std::string::npos);
    CHECK(status.find("咬 10%") != std::string::npos);
    CHECK(status.find("钝 5%") != std::string::npos);
    CHECK(status.find("移速 90%") != std::string::npos);
    CHECK(tone == "warn");

    // 无护甲槽/普通衣物：HUD 不显示护甲行
    ecs::Equipment plain;
    plain.slots[static_cast<std::size_t>(ecs::EquipSlot::Body)] =
        ecs::ItemInstance{"plain", 1, 1.0f};
    const game::PlayerArmor none = game::ResolvePlayerArmor(plain, registry);
    CHECK_FALSE(none.hasArmor);
    CHECK(game::ArmorStatusText(none).empty());
}

TEST_CASE("M19-F4：护甲损坏后减伤归零、HUD 明确提示且不可再生效") {
    const items::ItemRegistry registry = MakeRegistry();
    ecs::Equipment equipment;
    equipment.slots[static_cast<std::size_t>(ecs::EquipSlot::Body)] =
        ecs::ItemInstance{"jacket", 1, 0.0f};

    const game::PlayerArmor broken = game::ResolvePlayerArmor(equipment, registry);
    REQUIRE(broken.hasArmor);
    CHECK(broken.broken);
    CHECK(broken.resist.clawReduction == doctest::Approx(0.0f));
    CHECK(broken.resist.biteReduction == doctest::Approx(0.0f));
    CHECK(broken.resist.bluntReduction == doctest::Approx(0.0f));

    std::string tone;
    const std::string status = game::ArmorStatusText(broken, &tone);
    CHECK(status.find("已损坏") != std::string::npos);
    CHECK(tone == "fail");
}

TEST_CASE("M19-F4：护甲磨损只作用于躯干槽并钳制耐久") {
    const items::ItemRegistry registry = MakeRegistry();
    ecs::Equipment equipment;
    equipment.slots[static_cast<std::size_t>(ecs::EquipSlot::Body)] =
        ecs::ItemInstance{"jacket", 1, 0.03f};
    equipment.slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)] =
        ecs::ItemInstance{"axe", 1, 0.8f};

    CHECK_FALSE(items::WearEquippedArmor(equipment, 0.02f));
    CHECK(equipment.slots[static_cast<std::size_t>(ecs::EquipSlot::Body)]
              ->condition == doctest::Approx(0.01f));
    CHECK(equipment.slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)]
              ->condition == doctest::Approx(0.8f));

    CHECK(items::WearEquippedArmor(equipment, 1.0f));
    CHECK(equipment.slots[static_cast<std::size_t>(ecs::EquipSlot::Body)]
              ->condition == doctest::Approx(0.0f));
    CHECK_FALSE(items::WearEquippedArmor(equipment, 0.02f));
}
