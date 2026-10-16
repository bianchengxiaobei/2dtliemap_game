#include <doctest/doctest.h>

#include <string>

#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"

using namespace zl;

namespace {

const char* kItemsJson = R"({
  "format": "items-v1",
  "items": [
    { "id": "bandage", "name": "绷带", "icon": "i", "weight": 0.1, "maxStack": 5, "type": "consumable" },
    { "id": "rag", "name": "破布", "icon": "i", "weight": 0.1, "maxStack": 10, "type": "generic" },
    { "id": "axe", "name": "斧", "icon": "i", "weight": 3.0, "maxStack": 1, "type": "weapon" },
    { "id": "knife", "name": "刀", "icon": "i", "weight": 0.6, "maxStack": 1, "type": "weapon" },
    { "id": "jacket", "name": "夹克", "icon": "i", "weight": 1.2, "maxStack": 1, "type": "clothing" }
  ]
})";

items::ItemRegistry MakeRegistry() {
    items::ItemRegistry registry;
    std::string error;
    const bool ok = registry.LoadFromString(kItemsJson, "test", &error);
    CHECK(ok);
    CHECK(error.empty());
    return registry;
}

ecs::ItemInstance Make(const char* defId, int count) {
    return ecs::ItemInstance{defId, count, 1.0f};
}

}  // namespace

TEST_CASE("M6-T2：加入/堆叠合并规则") {
    const items::ItemRegistry registry = MakeRegistry();
    ecs::Container bag;
    bag.capacityKg = 100.0f;

    // 新物品：开一个堆叠
    CHECK(items::AddItem(bag, registry, Make("bandage", 3)) == 3);
    REQUIRE(bag.items.size() == 1);
    CHECK(bag.items[0].count == 3);
    CHECK(items::CountOf(bag, "bandage") == 3);

    // 合并进未满堆叠：不新增条目
    CHECK(items::AddItem(bag, registry, Make("bandage", 2)) == 2);
    CHECK(bag.items.size() == 1);
    CHECK(bag.items[0].count == 5);

    // 超过 maxStack(5)：新开第二条堆叠
    CHECK(items::AddItem(bag, registry, Make("bandage", 3)) == 3);
    CHECK(bag.items.size() == 2);
    CHECK(bag.items[0].count == 5);
    CHECK(bag.items[1].count == 3);

    // 单次加入 > maxStack：自动拆堆
    ecs::Container bag2;
    bag2.capacityKg = 100.0f;
    CHECK(items::AddItem(bag2, registry, Make("bandage", 8)) == 8);
    REQUIRE(bag2.items.size() == 2);
    CHECK(bag2.items[0].count == 5);
    CHECK(bag2.items[1].count == 3);

    // maxStack=1 的武器不合并：两次加入 = 两条条目
    ecs::Container bag3;
    bag3.capacityKg = 100.0f;
    CHECK(items::AddItem(bag3, registry, Make("axe", 1)) == 1);
    CHECK(items::AddItem(bag3, registry, Make("axe", 1)) == 1);
    CHECK(bag3.items.size() == 2);
}

TEST_CASE("M6-T2：容量约束与部分加入") {
    const items::ItemRegistry registry = MakeRegistry();

    // 容量不足一件：全部拒绝
    ecs::Container bag;
    bag.capacityKg = 2.0f;
    CHECK(items::AddItem(bag, registry, Make("axe", 1)) == 0);
    CHECK(bag.items.empty());

    // 部分加入：0.55kg 装 0.1kg 破布 → 5 件（0.5kg），剩余 5 件留在调用方
    ecs::Container bag2;
    bag2.capacityKg = 0.55f;
    const int added = items::AddItem(bag2, registry, Make("rag", 10));
    CHECK(added == 5);
    CHECK(items::CountOf(bag2, "rag") == 5);
    CHECK(items::TotalWeight(bag2, registry) == doctest::Approx(0.5f));

    // 再加 0.05kg 余量不足 1 件：拒绝
    CHECK(items::AddItem(bag2, registry, Make("rag", 1)) == 0);

    // 混合：先散件后重物，重物装不下不腾位置
    ecs::Container bag3;
    bag3.capacityKg = 3.5f;
    CHECK(items::AddItem(bag3, registry, Make("rag", 5)) == 5);   // 0.5
    CHECK(items::AddItem(bag3, registry, Make("axe", 1)) == 1);   // 3.5 满
    CHECK(items::AddItem(bag3, registry, Make("knife", 1)) == 0); // 0.6 放不下
}

TEST_CASE("M6-T2：未知定义与非法实例不生效") {
    const items::ItemRegistry registry = MakeRegistry();
    ecs::Container bag;
    bag.capacityKg = 100.0f;

    CHECK(items::AddItem(bag, registry, Make("ghost_item", 1)) == 0);
    CHECK(bag.items.empty());

    CHECK(items::AddItem(bag, registry, ecs::ItemInstance{}) == 0);          // 空 defId
    CHECK(items::AddItem(bag, registry, Make("bandage", 0)) == 0);           // count 0
    CHECK(items::AddItem(bag, registry, Make("bandage", -3)) == 0);          // 负数
    CHECK(bag.items.empty());
}

TEST_CASE("M6-T2：移除与计数") {
    const items::ItemRegistry registry = MakeRegistry();
    ecs::Container bag;
    bag.capacityKg = 100.0f;
    CHECK(items::AddItem(bag, registry, Make("bandage", 5)) == 5);
    CHECK(items::AddItem(bag, registry, Make("bandage", 3)) == 3);
    CHECK(items::AddItem(bag, registry, Make("axe", 1)) == 1);

    // 部分移除：先耗尽第一条堆叠（5），再从第二条取 1 → 剩 2
    CHECK(items::RemoveItem(bag, "bandage", 6) == 6);
    CHECK(items::CountOf(bag, "bandage") == 2);
    REQUIRE(bag.items.size() == 2);  // bandage 剩 2 + axe
    CHECK(bag.items[0].count == 2);

    // 移除超过持有量：返回实际数量
    CHECK(items::RemoveItem(bag, "bandage", 10) == 2);
    CHECK(items::CountOf(bag, "bandage") == 0);
    CHECK(bag.items.size() == 1);

    // 不存在的物品 / 非法数量
    CHECK(items::RemoveItem(bag, "bandage", 1) == 0);
    CHECK(items::RemoveItem(bag, "axe", 0) == 0);
    CHECK(bag.items.size() == 1);

    // 重量随移除更新
    CHECK(items::TotalWeight(bag, registry) == doctest::Approx(3.0f));
}

TEST_CASE("M6-T2：装备槽规则与换装/卸下") {
    const items::ItemRegistry registry = MakeRegistry();
    ecs::Equipment equipment;

    // 类型-槽位匹配
    CHECK(items::SlotAccepts(ecs::EquipSlot::PrimaryHand, items::ItemType::Weapon));
    CHECK_FALSE(items::SlotAccepts(ecs::EquipSlot::PrimaryHand, items::ItemType::Clothing));
    CHECK(items::SlotAccepts(ecs::EquipSlot::Body, items::ItemType::Clothing));
    CHECK_FALSE(items::SlotAccepts(ecs::EquipSlot::Body, items::ItemType::Weapon));

    // 武器进主手
    CHECK(items::Equip(equipment, registry, ecs::EquipSlot::PrimaryHand, Make("axe", 1),
                       nullptr));
    REQUIRE(equipment.slots[0].has_value());
    CHECK(equipment.slots[0]->defId == "axe");

    // 换装：旧装备移出
    ecs::ItemInstance swapped;
    CHECK(items::Equip(equipment, registry, ecs::EquipSlot::PrimaryHand, Make("knife", 1),
                       &swapped));
    CHECK(swapped.Valid());
    CHECK(swapped.defId == "axe");
    CHECK(equipment.slots[0]->defId == "knife");

    // 类型不符被拒（且不破坏现有装备）
    CHECK_FALSE(items::Equip(equipment, registry, ecs::EquipSlot::PrimaryHand,
                             Make("jacket", 1), nullptr));
    CHECK_FALSE(items::Equip(equipment, registry, ecs::EquipSlot::Body, Make("axe", 1),
                             nullptr));
    CHECK_FALSE(items::Equip(equipment, registry, ecs::EquipSlot::Body,
                             Make("ghost_item", 1), nullptr));
    CHECK(equipment.slots[0]->defId == "knife");
    CHECK_FALSE(equipment.slots[1].has_value());

    // 衣物进躯干
    CHECK(items::Equip(equipment, registry, ecs::EquipSlot::Body, Make("jacket", 1), nullptr));
    REQUIRE(equipment.slots[1].has_value());
    CHECK(equipment.slots[1]->defId == "jacket");

    // 卸下
    ecs::ItemInstance removed;
    CHECK(items::Unequip(equipment, ecs::EquipSlot::Body, &removed));
    CHECK(removed.defId == "jacket");
    CHECK_FALSE(equipment.slots[1].has_value());
    CHECK_FALSE(items::Unequip(equipment, ecs::EquipSlot::Body, nullptr));  // 已空

    // 空槽位换装：outSwapped 写入无效实例
    ecs::ItemInstance swapped2;
    swapped2 = Make("dummy_placeholder", 1);  // 先填充，确认被覆盖
    CHECK(items::Equip(equipment, registry, ecs::EquipSlot::Body, Make("jacket", 1), &swapped2));
    CHECK_FALSE(swapped2.Valid());
}

TEST_CASE("M6-T2：槽位名称与总重量") {
    const items::ItemRegistry registry = MakeRegistry();
    ecs::Container bag;
    bag.capacityKg = 1.0f;
    CHECK(items::AddItem(bag, registry, Make("knife", 1)) == 1);   // 0.6
    CHECK(items::AddItem(bag, registry, Make("rag", 4)) == 4);     // 0.4 → 满
    CHECK(items::TotalWeight(bag, registry) == doctest::Approx(1.0f));
    CHECK(std::string(items::EquipSlotToString(ecs::EquipSlot::PrimaryHand)) == "primary");
    CHECK(std::string(items::EquipSlotToString(ecs::EquipSlot::Body)) == "body");
}
