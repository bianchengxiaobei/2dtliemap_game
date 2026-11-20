#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "characters/components/CharacterComponents.h"
#include "game/FoodSafety.h"
#include "game/ProgressionRules.h"
#include "ecs/EcsWorld.h"
#include "items/ItemComponents.h"
#include "ui/InventoryUi.h"

namespace zl::core {
class EventBus;
}

namespace zl::items {
class ItemRegistry;
class RecipeRegistry;
class LootTableRegistry;
struct RecipeDef;
struct UseResult;
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
    float spoiledFoodDamage = 4.0f;
    FoodSafetyParams foodSafety;
    std::unordered_map<std::string, std::string> stationLabels;
    // M19-F1：服务器制作耗时按玩家制作技能/Perk 缩放；为空时保持基础耗时。
    const ProgressionConfig* progression = nullptr;
    // M19-F2：服务器权威 POI/容器搜刮与任务事件回调。
    const items::LootTableRegistry* lootTables = nullptr;
    std::function<void(ecs::Entity, std::string_view)> lootSearched;
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
    // M19-F2：服务器权威首次搜刮；lootTables 为空时拒绝。
    InventoryResult SearchLoot(ecs::Entity player, std::uint64_t stableId,
                               std::string* tableId = nullptr);

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
    float CraftSeconds(ecs::Entity player, const items::RecipeDef& recipe,
                       float efficiency) const;

    void ReturnOrDrop(ecs::Entity player, const ecs::ItemInstance& item);
    bool ApplyUseInfectionRisk(ecs::Entity player, std::string_view defId,
                               float risk);
    void EmitItemUsed(ecs::Entity player, std::string_view defId, bool spoiled,
                      int remaining);
    void EmitCraftStart(ecs::Entity player, const items::RecipeDef& recipe,
                        int batchCount);
    void EmitCraftFinished(ecs::Entity player, std::string_view recipeId,
                           std::string_view outputDefId, int outputCount,
                           int batchCompleted, bool failed);
    void ApplyFoodUse(ecs::Entity player, const items::ItemDef& def,
                      const items::UseResult& use);
    InventoryResult StartCraft(ecs::Entity player, const items::RecipeDef& recipe,
                               int requested, int* actual);
    InventoryResult StartNextQueued(ecs::Entity player, std::string prefix);
    void ClearCrafting(ecs::CraftingJob& job) const;

    ecs::Entity FindNearbyStorage(ecs::Entity player) const;
    bool StorageAccessible(ecs::Entity player, ecs::Entity storage) const;
    InventoryResult StoreItemInStorage(ecs::Entity player, ecs::Entity storage,
                                       int index, int count);
    InventoryResult TakeItemFromStorage(ecs::Entity player, ecs::Entity storage,
                                        int index, int count);
    InventoryResult StoreAllInStorage(ecs::Entity player, ecs::Entity storage);
    InventoryResult TakeAllFromStorage(ecs::Entity player, ecs::Entity storage);
    InventoryResult SortStorage(ecs::Entity player, ecs::Entity storage);
    InventoryResult TakeLootContainer(ecs::Entity player, ecs::Entity source,
                                      int index, int count);
    InventoryResult EquipLootContainer(ecs::Entity player, ecs::Entity source,
                                       int index, int slot);

    ecs::EcsWorld& m_world;
    const items::ItemRegistry& m_items;
    const items::RecipeRegistry& m_recipes;
    world::World* m_map = nullptr;
    core::EventBus* m_events = nullptr;
    InventoryServiceOptions m_options;
    std::unordered_set<std::string> m_lockedRecipes;
    std::uint64_t m_foodUseSerial = 0;
    std::uint64_t m_useRiskSerial = 0;
};

}  // namespace zl::game
