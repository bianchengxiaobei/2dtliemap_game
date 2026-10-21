#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"
#include "items/ItemComponents.h"
#include "ui/InventoryUi.h"

namespace zl::core {
class EventBus;
}

namespace zl::items {
class ItemRegistry;
class RecipeRegistry;
struct RecipeDef;
}  // namespace zl::items

namespace zl::world {
class World;
}

namespace zl::game {

// M13-T7：服务器侧玩家背包/世界交互。
//
// 这里只读写权威 ECS + 内容注册表，不依赖窗口/UI/输入/音频；单机与服务器可共用。
// 制作任务状态保存在玩家 CraftingJob 组件，计时由固定 tick 推进。
inline constexpr float kDefaultPickupRadius = 1.6f;
inline constexpr float kDefaultInventoryRange = 3.0f;
inline constexpr float kDefaultStationRange = 2.5f;

enum class InventoryTone : std::uint8_t {
    Info = 0,
    Ok = 1,
    Fail = 2,
};

struct InventoryResult {
    bool ok = false;
    std::string message;
    InventoryTone tone = InventoryTone::Info;
};

struct InventoryServiceOptions {
    float pickupRadius = kDefaultPickupRadius;
    float inventoryRange = kDefaultInventoryRange;
    float stationRange = kDefaultStationRange;
    std::string repairKitDefId = "repair_kit";
    int repairKitCount = 1;
    float repairAmount = 0.5f;
    std::unordered_map<std::string, std::string> stationLabels;
};

class PlayerInventoryService {
public:
    PlayerInventoryService(ecs::EcsWorld& world, const items::ItemRegistry& items,
                           const items::RecipeRegistry& recipes, world::World* map,
                           core::EventBus* events, InventoryServiceOptions options = {});

    // 拾取最近/指定地面物品；指定实体仍受 inventoryRange 约束。
    InventoryResult PickupNearest(ecs::Entity player);
    InventoryResult Pickup(ecs::Entity player, std::uint64_t stableId);

    // index < 0：使用第一件可用消耗品；否则使用指定背包槽。
    InventoryResult UseBag(ecs::Entity player, int index);
    // index < 0：切换主手装备；否则把指定背包槽装到 slot。
    InventoryResult EquipBag(ecs::Entity player, int index, int slot);
    InventoryResult TogglePrimaryWeapon(ecs::Entity player);
    InventoryResult Unequip(ecs::Entity player, int slot);
    // index < 0：丢弃第一堆。
    InventoryResult DropBag(ecs::Entity player, int index);
    InventoryResult DropEquipment(ecs::Entity player, int slot);
    InventoryResult EquipLoot(ecs::Entity player, std::uint64_t stableId, int slot);
    InventoryResult RepairBag(ecs::Entity player, int index);

    // UI 拖拽/按钮动作的统一入口（Transfer/Use/Equip/Drop/Repair/Pickup/制作队列）。
    InventoryResult Dispatch(ecs::Entity player, const ui::InventoryAction& action);

    // 推进玩家制作计时；返回 true 表示任务状态变化（完成/中断/进入下一件）。
    // result 仅在产生需展示的结果时填充。
    bool TickCrafting(ecs::Entity player, float dt, InventoryResult* result);

private:
    InventoryResult Fail(std::string message) const;
    InventoryResult Ok(std::string message, InventoryTone tone = InventoryTone::Ok) const;

    bool RecipeKnown(const ecs::KnownRecipes* known, std::string_view recipeId) const;
    bool StationNearby(std::string_view station, float x, float y, int z) const;
    std::string StationLabel(std::string_view station) const;

    void ReturnOrDrop(ecs::Entity player, const ecs::ItemInstance& item);
    InventoryResult StartCraft(ecs::Entity player, const items::RecipeDef& recipe,
                               int requested, int* actual);
    InventoryResult StartNextQueued(ecs::Entity player, std::string prefix);
    void ClearCrafting(ecs::CraftingJob& job) const;

    ecs::EcsWorld& m_world;
    const items::ItemRegistry& m_items;
    const items::RecipeRegistry& m_recipes;
    world::World* m_map = nullptr;
    core::EventBus* m_events = nullptr;
    InventoryServiceOptions m_options;
    std::unordered_set<std::string> m_lockedRecipes;
};

}  // namespace zl::game
