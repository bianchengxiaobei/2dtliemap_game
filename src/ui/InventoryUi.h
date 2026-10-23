#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

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
    Craft,
    CancelCraft,
    Repair,
    QueueMoveUp,
    QueueMoveDown,
    QueueRemove,
    QueueSetCount,
};

struct InventorySlot {
    bool filled = false;
    std::string label;
    int count = 0;
    bool usable = false;
    bool equippable = false;
    std::uint64_t entityId = 0;
    float condition = 1.0f;
    bool damaged = false;
    bool repairAvailable = false;
};

struct InventoryRecipeIngredient {
    std::string label;
    int required = 0;
    int have = 0;
};

struct InventoryRecipe {
    std::string id;
    std::string name;
    std::string outputDefId;
    std::string outputLabel;
    int outputCount = 0;
    bool known = true;
    bool canCraft = false;
    std::vector<InventoryRecipeIngredient> ingredients;
    std::vector<InventoryRecipeIngredient> tools;
    float estimatedSeconds = 0.0f;
    std::string station;
    std::string stationLabel;
    bool stationNearby = true;
};

struct InventoryQueuedCraft {
    std::string recipeId;
    std::string outputLabel;
    int outputCount = 0;
    int count = 1;
};

struct InventoryCrafting {
    bool active = false;
    std::string recipeId;
    std::string outputLabel;
    int outputCount = 0;
    float elapsed = 0.0f;
    float duration = 0.0f;
    float progress = 0.0f;
    float toolEfficiency = 1.0f;
    int batchCount = 1;
    int completedCount = 0;
    std::vector<InventoryQueuedCraft> queue;
};

struct InventoryState {
    bool open = false;
    bool playerValid = false;
    float weight = 0.0f;
    float capacity = 20.0f;
    std::array<InventorySlot, kInventoryBagSlots> bag{};
    std::array<InventorySlot, kInventoryEquipmentSlots> equipment{};
    std::array<InventorySlot, kInventoryLootSlots> loot{};
    std::vector<InventoryRecipe> recipes;
    InventoryCrafting crafting;
    int bagStacks = 0;
    int lootStacks = 0;
    std::string lootTitle = "附近地面";
    std::string status;
    std::string statusTone = "info";
};

inline bool operator==(const InventorySlot& a, const InventorySlot& b) {
    return a.filled == b.filled && a.label == b.label && a.count == b.count &&
           a.usable == b.usable && a.equippable == b.equippable &&
           a.entityId == b.entityId && a.condition == b.condition &&
           a.damaged == b.damaged && a.repairAvailable == b.repairAvailable;
}

inline bool operator==(const InventoryRecipeIngredient& a,
                       const InventoryRecipeIngredient& b) {
    return a.label == b.label && a.required == b.required && a.have == b.have;
}

inline bool operator==(const InventoryRecipe& a, const InventoryRecipe& b) {
    return a.id == b.id && a.name == b.name && a.outputDefId == b.outputDefId &&
           a.outputLabel == b.outputLabel && a.outputCount == b.outputCount &&
           a.known == b.known && a.canCraft == b.canCraft &&
           a.ingredients == b.ingredients &&
           a.tools == b.tools && a.estimatedSeconds == b.estimatedSeconds &&
           a.station == b.station && a.stationLabel == b.stationLabel &&
           a.stationNearby == b.stationNearby;
}

inline bool operator==(const InventoryQueuedCraft& a,
                       const InventoryQueuedCraft& b) {
    return a.recipeId == b.recipeId && a.outputLabel == b.outputLabel &&
           a.outputCount == b.outputCount && a.count == b.count;
}

inline bool operator==(const InventoryCrafting& a, const InventoryCrafting& b) {
    return a.active == b.active && a.recipeId == b.recipeId &&
           a.outputLabel == b.outputLabel && a.outputCount == b.outputCount &&
           a.elapsed == b.elapsed && a.duration == b.duration &&
           a.progress == b.progress && a.toolEfficiency == b.toolEfficiency &&
           a.batchCount == b.batchCount && a.completedCount == b.completedCount &&
           a.queue == b.queue;
}

inline bool operator==(const InventoryState& a, const InventoryState& b) {
    return a.open == b.open && a.playerValid == b.playerValid && a.weight == b.weight &&
           a.capacity == b.capacity && a.bag == b.bag && a.equipment == b.equipment &&
           a.loot == b.loot && a.recipes == b.recipes && a.crafting == b.crafting &&
           a.bagStacks == b.bagStacks && a.lootStacks == b.lootStacks &&
           a.lootTitle == b.lootTitle && a.status == b.status &&
           a.statusTone == b.statusTone;
}

struct InventoryAction {
    InventoryActionType type = InventoryActionType::Transfer;
    InventoryArea from = InventoryArea::Backpack;
    int fromIndex = -1;
    std::uint64_t fromEntityId = 0;
    InventoryArea to = InventoryArea::Backpack;
    int toIndex = -1;
    int count = 1;
};

using InventoryActionHandler = std::function<void(const InventoryAction&)>;

}  // namespace zl::ui
