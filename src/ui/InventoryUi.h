#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace zl::ui {

inline constexpr int kInventoryBagSlots = 25;
inline constexpr int kInventoryEquipmentSlots = 2;
inline constexpr int kInventoryLootSlots = 6;

enum class InventoryArea : std::uint8_t {
    Backpack = 0,
    Equipment = 1,
    Loot = 2,
};

enum class InventoryActionType : std::uint8_t {
    Transfer = 0,
    Use,
    Equip,
    Unequip,
    Drop,
    Pickup,
};

struct InventorySlot {
    bool filled = false;
    std::string label;
    int count = 0;
    bool usable = false;
    bool equippable = false;
    std::uint64_t entityId = 0;
};

struct InventoryState {
    bool playerValid = false;
    float weight = 0.0f;
    float capacity = 20.0f;
    std::array<InventorySlot, kInventoryBagSlots> bag{};
    std::array<InventorySlot, kInventoryEquipmentSlots> equipment{};
    std::array<InventorySlot, kInventoryLootSlots> loot{};
    int bagStacks = 0;
    int lootStacks = 0;
    std::string status;
    std::string statusTone = "info";
};

struct InventoryAction {
    InventoryActionType type = InventoryActionType::Transfer;
    InventoryArea from = InventoryArea::Backpack;
    int fromIndex = -1;
    std::uint64_t fromEntityId = 0;
    InventoryArea to = InventoryArea::Backpack;
    int toIndex = -1;
};

using InventoryActionHandler = std::function<void(const InventoryAction&)>;

}  // namespace zl::ui
