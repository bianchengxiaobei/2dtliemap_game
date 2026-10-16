#include "items/InventoryOps.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/Log.h"

namespace zl::items {
namespace {

constexpr const char* kLogModule = "items";

// 剩余重量还能容纳的件数（weight <= 0 视为无重量约束；epsilon 抵消浮点误差）
int CountByWeight(float weightKg, float freeKg) {
    if (weightKg <= 0.0f) {
        return std::numeric_limits<int>::max();
    }
    if (freeKg <= 0.0f) {
        return 0;
    }
    return static_cast<int>(std::floor(freeKg / weightKg + 1e-4f));
}

}  // namespace

int AddItem(ecs::Container& container, const ItemRegistry& registry,
            const ecs::ItemInstance& item) {
    if (!item.Valid()) {
        return 0;
    }
    const ItemDef* def = registry.Find(item.defId);
    if (def == nullptr) {
        ZL_WARN(kLogModule, "AddItem：未知物品定义 '{}'（忽略）", item.defId);
        return 0;
    }

    float freeKg = container.capacityKg - TotalWeight(container, registry);
    if (freeKg < 0.0f) {
        freeKg = 0.0f;
    }

    int added = 0;
    // 1) 填已有未满堆叠（保持原 condition）
    for (ecs::ItemInstance& entry : container.items) {
        if (added >= item.count) {
            break;
        }
        if (entry.defId != def->id || entry.count >= def->maxStack) {
            continue;
        }
        const int take =
            std::min({item.count - added, def->maxStack - entry.count,
                      CountByWeight(def->weight, freeKg)});
        if (take <= 0) {
            continue;
        }
        entry.count += take;
        added += take;
        freeKg -= static_cast<float>(take) * def->weight;
    }
    // 2) 剩余开新堆叠
    while (added < item.count) {
        const int take = std::min(item.count - added,
                                  std::min(def->maxStack, CountByWeight(def->weight, freeKg)));
        if (take <= 0) {
            break;
        }
        container.items.push_back(ecs::ItemInstance{def->id, take, item.condition});
        added += take;
        freeKg -= static_cast<float>(take) * def->weight;
    }
    return added;
}

int RemoveItem(ecs::Container& container, std::string_view defId, int count) {
    if (count <= 0) {
        return 0;
    }
    int removed = 0;
    for (auto it = container.items.begin(); it != container.items.end();) {
        if (it->defId != defId) {
            ++it;
            continue;
        }
        const int take = std::min(count - removed, it->count);
        it->count -= take;
        removed += take;
        if (it->count <= 0) {
            it = container.items.erase(it);
        } else {
            ++it;
        }
        if (removed >= count) {
            break;
        }
    }
    return removed;
}

int CountOf(const ecs::Container& container, std::string_view defId) {
    int total = 0;
    for (const ecs::ItemInstance& entry : container.items) {
        if (entry.defId == defId) {
            total += entry.count;
        }
    }
    return total;
}

float TotalWeight(const ecs::Container& container, const ItemRegistry& registry) {
    float total = 0.0f;
    for (const ecs::ItemInstance& entry : container.items) {
        const ItemDef* def = registry.Find(entry.defId);
        if (def == nullptr) {
            ZL_WARN(kLogModule, "TotalWeight：未知物品定义 '{}'（记 0kg）", entry.defId);
            continue;
        }
        total += static_cast<float>(entry.count) * def->weight;
    }
    return total;
}

bool SlotAccepts(ecs::EquipSlot slot, ItemType type) {
    switch (slot) {
        case ecs::EquipSlot::PrimaryHand:
            return type == ItemType::Weapon;
        case ecs::EquipSlot::Body:
            return type == ItemType::Clothing;
    }
    return false;
}

const char* EquipSlotToString(ecs::EquipSlot slot) {
    switch (slot) {
        case ecs::EquipSlot::PrimaryHand:
            return "primary";
        case ecs::EquipSlot::Body:
            return "body";
    }
    return "?";
}

bool Equip(ecs::Equipment& equipment, const ItemRegistry& registry, ecs::EquipSlot slot,
           const ecs::ItemInstance& item, ecs::ItemInstance* outSwapped) {
    const ItemDef* def = registry.Find(item.defId);
    if (def == nullptr || !item.Valid()) {
        ZL_WARN(kLogModule, "Equip：无效物品 '{}'（忽略）", item.defId);
        return false;
    }
    if (!SlotAccepts(slot, def->type)) {
        ZL_WARN(kLogModule, "Equip：物品 '{}'（类型 {}）不能装备到槽位 '{}'", def->id,
                ItemTypeToString(def->type), EquipSlotToString(slot));
        return false;
    }
    std::optional<ecs::ItemInstance>& slotRef =
        equipment.slots[static_cast<std::size_t>(slot)];
    if (outSwapped != nullptr) {
        *outSwapped = slotRef.has_value() ? std::move(*slotRef) : ecs::ItemInstance{};
    }
    slotRef = item;
    return true;
}

bool Unequip(ecs::Equipment& equipment, ecs::EquipSlot slot, ecs::ItemInstance* outRemoved) {
    std::optional<ecs::ItemInstance>& slotRef =
        equipment.slots[static_cast<std::size_t>(slot)];
    if (!slotRef.has_value()) {
        return false;
    }
    if (outRemoved != nullptr) {
        *outRemoved = std::move(*slotRef);
    }
    slotRef.reset();
    return true;
}

}  // namespace zl::items
