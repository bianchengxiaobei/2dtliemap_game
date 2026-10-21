#include "game/PlayerInventoryService.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "characters/components/CharacterComponents.h"
#include "core/EventBus.h"
#include "core/GameEvents.h"
#include "game/GroundItems.h"
#include "game/WorldQueries.h"
#include "items/Crafting.h"
#include "items/InventoryOps.h"
#include "items/ItemRegistry.h"
#include "items/RecipeRegistry.h"
#include "items/Repair.h"

namespace zl::game {
namespace {

std::string ItemLabel(const items::ItemRegistry& registry, std::string_view defId) {
    const items::ItemDef* def = registry.Find(defId);
    return def != nullptr ? def->name : std::string(defId);
}

bool ValidSlot(int slot) {
    return slot >= 0 && slot < static_cast<int>(ecs::kEquipSlotCount);
}

}  // namespace

PlayerInventoryService::PlayerInventoryService(
    ecs::EcsWorld& world, const items::ItemRegistry& items,
    const items::RecipeRegistry& recipes, world::World* map, core::EventBus* events,
    InventoryServiceOptions options)
    : m_world(world),
      m_items(items),
      m_recipes(recipes),
      m_map(map),
      m_events(events),
      m_options(std::move(options)) {
    if (!(m_options.pickupRadius > 0.0f) || !std::isfinite(m_options.pickupRadius)) {
        m_options.pickupRadius = kDefaultPickupRadius;
    }
    if (!(m_options.inventoryRange > 0.0f) ||
        !std::isfinite(m_options.inventoryRange)) {
        m_options.inventoryRange = kDefaultInventoryRange;
    }
    if (!(m_options.stationRange > 0.0f) || !std::isfinite(m_options.stationRange)) {
        m_options.stationRange = kDefaultStationRange;
    }
    m_options.repairKitCount = std::max(1, m_options.repairKitCount);
    m_options.repairAmount = std::clamp(m_options.repairAmount, 0.01f, 1.0f);

    for (const items::ItemDef& def : m_items.All()) {
        for (const std::string& recipeId : def.unlocksRecipes) {
            m_lockedRecipes.insert(recipeId);
        }
    }
}

InventoryResult PlayerInventoryService::Fail(std::string message) const {
    return InventoryResult{false, std::move(message), InventoryTone::Fail};
}

InventoryResult PlayerInventoryService::Ok(std::string message,
                                           InventoryTone tone) const {
    return InventoryResult{true, std::move(message), tone};
}

bool PlayerInventoryService::RecipeKnown(const ecs::KnownRecipes* known,
                                         std::string_view recipeId) const {
    if (!m_lockedRecipes.contains(std::string(recipeId))) {
        return true;
    }
    if (known == nullptr) {
        return false;
    }
    return std::find(known->recipeIds.begin(), known->recipeIds.end(), recipeId) !=
           known->recipeIds.end();
}

bool PlayerInventoryService::StationNearby(std::string_view station, float x, float y,
                                           int z) const {
    if (station.empty()) {
        return true;
    }
    if (m_map == nullptr || !(m_options.stationRange > 0.0f)) {
        return false;
    }
    return HasStationNearby(*m_map, x, y, z, station, m_options.stationRange);
}

std::string PlayerInventoryService::StationLabel(std::string_view station) const {
    const auto it = m_options.stationLabels.find(std::string(station));
    return it != m_options.stationLabels.end() ? it->second : std::string(station);
}

InventoryResult PlayerInventoryService::PickupNearest(ecs::Entity player) {
    entt::registry& registry = m_world.Registry();
    auto* container = registry.try_get<ecs::Container>(player);
    const auto* position = registry.try_get<ecs::Position>(player);
    if (!m_world.Alive(player) || container == nullptr || position == nullptr) {
        return Fail("玩家背包不可用");
    }
    const PickupResult result =
        PickupNearestItem(m_world, m_items, *container, position->x, position->y,
                          position->z, m_options.pickupRadius);
    if (!result.found) {
        return Fail("附近没有可拾取物品");
    }
    if (result.picked <= 0) {
        return Fail("背包已满");
    }
    if (m_events != nullptr) {
        m_events->EmitQueued(core::kOnItemPickup,
                             core::ItemEvent{result.entityId, result.defId,
                                             result.picked});
    }
    const std::string name = ItemLabel(m_items, result.defId);
    if (result.groundRemaining > 0) {
        return Ok("拾取了 " + name + " ×" + std::to_string(result.picked) +
                  "（地上剩 " + std::to_string(result.groundRemaining) + "）");
    }
    return Ok("拾取了 " + name + " ×" + std::to_string(result.picked));
}

InventoryResult PlayerInventoryService::Pickup(ecs::Entity player,
                                               std::uint64_t stableId) {
    entt::registry& registry = m_world.Registry();
    auto* container = registry.try_get<ecs::Container>(player);
    const auto* playerPosition = registry.try_get<ecs::Position>(player);
    if (!m_world.Alive(player) || container == nullptr || playerPosition == nullptr) {
        return Fail("玩家背包不可用");
    }
    if (stableId == 0) {
        return Fail("地面物品不存在");
    }
    const ecs::Entity entity = m_world.FindById(stableId);
    const auto* itemPosition =
        entity != entt::null ? registry.try_get<ecs::Position>(entity) : nullptr;
    const auto* instance =
        entity != entt::null ? registry.try_get<ecs::ItemInstance>(entity) : nullptr;
    if (entity == entt::null || !m_world.Alive(entity) || itemPosition == nullptr ||
        instance == nullptr) {
        return Fail("地面物品不存在");
    }
    const float dx = itemPosition->x - playerPosition->x;
    const float dy = itemPosition->y - playerPosition->y;
    if (dx * dx + dy * dy > m_options.inventoryRange * m_options.inventoryRange) {
        return Fail("物品距离太远");
    }
    const PickupResult result = PickupGroundItem(m_world, m_items, *container, entity);
    if (!result.found) {
        return Fail("地面物品不存在");
    }
    if (result.picked <= 0) {
        return Fail("背包已满");
    }
    if (m_events != nullptr) {
        m_events->EmitQueued(core::kOnItemPickup,
                             core::ItemEvent{result.entityId, result.defId,
                                             result.picked});
    }
    const std::string name = ItemLabel(m_items, result.defId);
    if (result.groundRemaining > 0) {
        return Ok("拾取了 " + name + " ×" + std::to_string(result.picked) +
                  "（地上剩 " + std::to_string(result.groundRemaining) + "）");
    }
    return Ok("拾取了 " + name + " ×" + std::to_string(result.picked));
}

InventoryResult PlayerInventoryService::UseBag(ecs::Entity player, int index) {
    entt::registry& registry = m_world.Registry();
    auto* container = registry.try_get<ecs::Container>(player);
    auto* health = registry.try_get<ecs::Health>(player);
    if (!m_world.Alive(player) || container == nullptr || health == nullptr) {
        return Fail("玩家或背包不可用");
    }

    if (index < 0) {
        const items::UseResult use = items::UseFirstConsumable(*container, m_items);
        if (!use.applied) {
            return Fail("背包里没有可使用的消耗品");
        }
        auto* needs = registry.try_get<ecs::Needs>(player);
        const float healed = items::ApplyUseEffect(
            use.effect, health->max, &health->current,
            needs != nullptr ? &needs->hunger : nullptr,
            needs != nullptr ? &needs->thirst : nullptr,
            needs != nullptr ? &needs->fatigue : nullptr);
        const std::string name = ItemLabel(m_items, use.defId);
        if (use.effect.heal > 0.0f) {
            return Ok("使用了 " + name + "（+" +
                      std::to_string(static_cast<int>(std::lround(healed))) + " HP）");
        }
        return Ok("使用了 " + name);
    }

    if (static_cast<std::size_t>(index) >= container->items.size()) {
        return Fail("背包槽不存在");
    }
    const ecs::ItemInstance entry = container->items[static_cast<std::size_t>(index)];
    const items::ItemDef* def = m_items.Find(entry.defId);
    if (def == nullptr) {
        return Fail("物品定义不存在");
    }

    if (!def->unlocksRecipes.empty()) {
        auto* known = registry.try_get<ecs::KnownRecipes>(player);
        if (known == nullptr) {
            known = &registry.emplace<ecs::KnownRecipes>(player);
        }
        std::vector<std::string> learned;
        for (const std::string& recipeId : def->unlocksRecipes) {
            if (m_recipes.Find(recipeId) != nullptr &&
                !RecipeKnown(known, recipeId)) {
                learned.push_back(recipeId);
            }
        }
        if (learned.empty()) {
            return Ok("该配方已学会，无需再次使用", InventoryTone::Info);
        }
        ecs::ItemInstance removed;
        if (!items::TakeAt(*container, static_cast<std::size_t>(index), 1, &removed)) {
            return Fail("物品使用失败");
        }
        std::string labels;
        for (const std::string& recipeId : learned) {
            known->recipeIds.push_back(recipeId);
            const items::RecipeDef* recipe = m_recipes.Find(recipeId);
            if (!labels.empty()) {
                labels += "、";
            }
            labels += recipe != nullptr ? recipe->name : recipeId;
        }
        std::sort(known->recipeIds.begin(), known->recipeIds.end());
        known->recipeIds.erase(
            std::unique(known->recipeIds.begin(), known->recipeIds.end()),
            known->recipeIds.end());
        return Ok("学会了配方：" + labels);
    }

    const items::UseResult use =
        items::UseAt(*container, m_items, static_cast<std::size_t>(index));
    if (!use.applied) {
        return Fail("该物品不能使用");
    }
    auto* needs = registry.try_get<ecs::Needs>(player);
    const float healed = items::ApplyUseEffect(
        use.effect, health->max, &health->current,
        needs != nullptr ? &needs->hunger : nullptr,
        needs != nullptr ? &needs->thirst : nullptr,
        needs != nullptr ? &needs->fatigue : nullptr);
    const std::string name = ItemLabel(m_items, use.defId);
    if (use.effect.heal > 0.0f) {
        return Ok("使用了 " + name + "（+" +
                  std::to_string(static_cast<int>(std::lround(healed))) + " HP）");
    }
    return Ok("使用了 " + name);
}

InventoryResult PlayerInventoryService::EquipBag(ecs::Entity player, int index,
                                                 int slot) {
    if (index < 0) {
        return TogglePrimaryWeapon(player);
    }
    if (!ValidSlot(slot)) {
        return Fail("装备槽非法");
    }
    entt::registry& registry = m_world.Registry();
    auto* container = registry.try_get<ecs::Container>(player);
    auto* equipment = registry.try_get<ecs::Equipment>(player);
    if (!m_world.Alive(player) || container == nullptr || equipment == nullptr ||
        static_cast<std::size_t>(index) >= container->items.size()) {
        return Fail("背包槽不存在");
    }
    const ecs::ItemInstance entry = container->items[static_cast<std::size_t>(index)];
    const items::ItemDef* def = m_items.Find(entry.defId);
    const auto equipSlot = static_cast<ecs::EquipSlot>(slot);
    if (def == nullptr || !items::SlotAccepts(equipSlot, def->type)) {
        return Fail("该物品不能放入该装备槽");
    }
    ecs::ItemInstance one = entry;
    one.count = 1;
    ecs::ItemInstance removed;
    if (!items::TakeAt(*container, static_cast<std::size_t>(index), 1, &removed)) {
        return Fail("物品取出失败");
    }
    ecs::ItemInstance swapped;
    if (!items::Equip(*equipment, m_items, equipSlot, one, &swapped)) {
        ReturnOrDrop(player, removed);
        return Fail("装备失败");
    }
    if (swapped.Valid()) {
        ReturnOrDrop(player, swapped);
    }
    return Ok("装备了 " + ItemLabel(m_items, one.defId));
}

InventoryResult PlayerInventoryService::TogglePrimaryWeapon(ecs::Entity player) {
    entt::registry& registry = m_world.Registry();
    auto* container = registry.try_get<ecs::Container>(player);
    auto* equipment = registry.try_get<ecs::Equipment>(player);
    if (!m_world.Alive(player) || container == nullptr || equipment == nullptr) {
        return Fail("玩家背包不可用");
    }
    constexpr std::size_t kPrimary =
        static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand);
    if (equipment->slots[kPrimary].has_value()) {
        ecs::ItemInstance removed;
        if (!items::Unequip(*equipment, ecs::EquipSlot::PrimaryHand, &removed)) {
            return Fail("主手没有可卸下的物品");
        }
        const std::string name = ItemLabel(m_items, removed.defId);
        ReturnOrDrop(player, removed);
        return Ok("卸下了 " + name, InventoryTone::Info);
    }

    for (std::size_t i = 0; i < container->items.size(); ++i) {
        const ecs::ItemInstance entry = container->items[i];
        const items::ItemDef* def = m_items.Find(entry.defId);
        if (def == nullptr || def->type != items::ItemType::Weapon) {
            continue;
        }
        ecs::ItemInstance one = entry;
        one.count = 1;
        ecs::ItemInstance removed;
        if (!items::TakeAt(*container, i, 1, &removed)) {
            continue;
        }
        ecs::ItemInstance swapped;
        if (!items::Equip(*equipment, m_items, ecs::EquipSlot::PrimaryHand, one,
                          &swapped)) {
            ReturnOrDrop(player, removed);
            continue;
        }
        if (swapped.Valid()) {
            ReturnOrDrop(player, swapped);
        }
        return Ok("装备了 " + def->name);
    }
    return Fail("背包里没有可装备的武器");
}

InventoryResult PlayerInventoryService::Unequip(ecs::Entity player, int slot) {
    if (!ValidSlot(slot)) {
        return Fail("装备槽非法");
    }
    entt::registry& registry = m_world.Registry();
    auto* equipment = registry.try_get<ecs::Equipment>(player);
    if (!m_world.Alive(player) || equipment == nullptr) {
        return Fail("玩家装备不可用");
    }
    ecs::ItemInstance removed;
    if (!items::Unequip(*equipment, static_cast<ecs::EquipSlot>(slot), &removed)) {
        return Fail("装备槽为空");
    }
    const std::string name = ItemLabel(m_items, removed.defId);
    ReturnOrDrop(player, removed);
    return Ok("卸下了 " + name, InventoryTone::Info);
}

InventoryResult PlayerInventoryService::DropBag(ecs::Entity player, int index) {
    entt::registry& registry = m_world.Registry();
    auto* container = registry.try_get<ecs::Container>(player);
    const auto* position = registry.try_get<ecs::Position>(player);
    if (!m_world.Alive(player) || container == nullptr || position == nullptr) {
        return Fail("玩家背包不可用");
    }
    if (container->items.empty()) {
        return Fail("背包是空的");
    }
    if (index < 0) {
        index = 0;
    }
    if (static_cast<std::size_t>(index) >= container->items.size()) {
        return Fail("背包槽不存在");
    }
    const ecs::ItemInstance stack =
        container->items[static_cast<std::size_t>(index)];
    ecs::ItemInstance removed;
    if (!items::TakeAt(*container, static_cast<std::size_t>(index), stack.count,
                       &removed)) {
        return Fail("物品取出失败");
    }
    const ecs::Entity ground =
        SpawnGroundItem(m_world, m_items, removed, position->x, position->y,
                        position->z);
    if (ground == entt::null) {
        (void)items::AddItem(*container, m_items, removed);
        return Fail("无法丢弃物品：地面生成失败");
    }
    if (m_events != nullptr) {
        m_events->EmitQueued(core::kOnItemDrop,
                             core::ItemEvent{m_world.IdOf(ground).id, removed.defId,
                                             removed.count});
    }
    return Ok("丢弃了 " + ItemLabel(m_items, removed.defId) + " ×" +
              std::to_string(removed.count),
              InventoryTone::Info);
}

InventoryResult PlayerInventoryService::DropEquipment(ecs::Entity player, int slot) {
    if (!ValidSlot(slot)) {
        return Fail("装备槽非法");
    }
    entt::registry& registry = m_world.Registry();
    auto* equipment = registry.try_get<ecs::Equipment>(player);
    const auto* position = registry.try_get<ecs::Position>(player);
    if (!m_world.Alive(player) || equipment == nullptr || position == nullptr) {
        return Fail("玩家装备不可用");
    }
    ecs::ItemInstance removed;
    if (!items::Unequip(*equipment, static_cast<ecs::EquipSlot>(slot), &removed)) {
        return Fail("装备槽为空");
    }
    const ecs::Entity ground =
        SpawnGroundItem(m_world, m_items, removed, position->x, position->y,
                        position->z);
    if (ground == entt::null) {
        ReturnOrDrop(player, removed);
        return Fail("无法丢弃物品：地面生成失败");
    }
    if (m_events != nullptr) {
        m_events->EmitQueued(core::kOnItemDrop,
                             core::ItemEvent{m_world.IdOf(ground).id, removed.defId,
                                             removed.count});
    }
    return Ok("丢弃了 " + ItemLabel(m_items, removed.defId), InventoryTone::Info);
}

InventoryResult PlayerInventoryService::EquipLoot(ecs::Entity player,
                                                   std::uint64_t stableId,
                                                   int slot) {
    if (!ValidSlot(slot)) {
        return Fail("装备槽非法");
    }
    entt::registry& registry = m_world.Registry();
    auto* equipment = registry.try_get<ecs::Equipment>(player);
    const auto* playerPosition = registry.try_get<ecs::Position>(player);
    if (!m_world.Alive(player) || equipment == nullptr || playerPosition == nullptr) {
        return Fail("玩家装备不可用");
    }
    const ecs::Entity entity = m_world.FindById(stableId);
    auto* instance =
        entity != entt::null ? registry.try_get<ecs::ItemInstance>(entity) : nullptr;
    const auto* itemPosition =
        entity != entt::null ? registry.try_get<ecs::Position>(entity) : nullptr;
    if (entity == entt::null || !m_world.Alive(entity) || instance == nullptr ||
        itemPosition == nullptr) {
        return Fail("地面物品不存在");
    }
    const float dx = itemPosition->x - playerPosition->x;
    const float dy = itemPosition->y - playerPosition->y;
    if (dx * dx + dy * dy > m_options.inventoryRange * m_options.inventoryRange) {
        return Fail("物品距离太远");
    }
    const items::ItemDef* def = m_items.Find(instance->defId);
    const auto equipSlot = static_cast<ecs::EquipSlot>(slot);
    if (def == nullptr || !items::SlotAccepts(equipSlot, def->type)) {
        return Fail("该物品不能放入该装备槽");
    }
    ecs::ItemInstance one = *instance;
    one.count = 1;
    ecs::ItemInstance swapped;
    if (!items::Equip(*equipment, m_items, equipSlot, one, &swapped)) {
        return Fail("装备失败");
    }
    const std::uint64_t entityId = m_world.IdOf(entity).id;
    instance->count -= 1;
    if (instance->count <= 0) {
        m_world.Destroy(entity);
    }
    if (m_events != nullptr) {
        m_events->EmitQueued(core::kOnItemPickup,
                             core::ItemEvent{entityId, one.defId, 1});
    }
    if (swapped.Valid()) {
        ReturnOrDrop(player, swapped);
    }
    return Ok("装备了 " + def->name);
}

InventoryResult PlayerInventoryService::RepairBag(ecs::Entity player, int index) {
    entt::registry& registry = m_world.Registry();
    auto* container = registry.try_get<ecs::Container>(player);
    if (!m_world.Alive(player) || container == nullptr || index < 0) {
        return Fail("背包槽不存在");
    }
    const items::RepairResult result =
        items::Repair(*container, m_items, static_cast<std::size_t>(index),
                      m_options.repairKitDefId, m_options.repairKitCount,
                      m_options.repairAmount);
    if (!result.ok) {
        return Fail(result.error);
    }
    const int before = static_cast<int>(std::lround(result.before * 100.0f));
    const int after = static_cast<int>(std::lround(result.after * 100.0f));
    return Ok("维修成功：" + ItemLabel(m_items, result.defId) + " " +
              std::to_string(before) + "% → " + std::to_string(after) + "%");
}

void PlayerInventoryService::ReturnOrDrop(ecs::Entity player,
                                           const ecs::ItemInstance& item) {
    if (!item.Valid()) {
        return;
    }
    entt::registry& registry = m_world.Registry();
    auto* container = registry.try_get<ecs::Container>(player);
    const auto* position = registry.try_get<ecs::Position>(player);
    if (container != nullptr) {
        const int added = items::AddItem(*container, m_items, item);
        if (added >= item.count) {
            return;
        }
        ecs::ItemInstance remainder = item;
        remainder.count -= added;
        if (position != nullptr) {
            (void)SpawnGroundItem(m_world, m_items, remainder, position->x,
                                  position->y, position->z);
        }
        return;
    }
    if (position != nullptr) {
        (void)SpawnGroundItem(m_world, m_items, item, position->x, position->y,
                              position->z);
    }
}

InventoryResult PlayerInventoryService::StartCraft(ecs::Entity player,
                                                    const items::RecipeDef& recipe,
                                                    int requested, int* actual) {
    if (actual != nullptr) {
        *actual = 0;
    }
    entt::registry& registry = m_world.Registry();
    auto* container = registry.try_get<ecs::Container>(player);
    const auto* position = registry.try_get<ecs::Position>(player);
    auto* known = registry.try_get<ecs::KnownRecipes>(player);
    auto* job = registry.try_get<ecs::CraftingJob>(player);
    if (!m_world.Alive(player) || container == nullptr || job == nullptr) {
        return Fail("玩家或制作任务不可用");
    }
    if (!RecipeKnown(known, recipe.id)) {
        return Fail("尚未学会该配方：阅读配方杂志");
    }
    if (!recipe.station.empty() &&
        (position == nullptr ||
         !StationNearby(recipe.station, position->x, position->y, position->z))) {
        return Fail("需要附近" + StationLabel(recipe.station));
    }
    std::string reason;
    if (!items::CanCraft(*container, m_items, recipe, &reason)) {
        return Fail(reason.empty() ? "材料或背包空间不足" : reason);
    }
    const int requestedCount = std::clamp(requested, 1, 99);
    const int possible =
        items::MaxCraftCount(*container, m_items, recipe, requestedCount);
    if (possible <= 0) {
        return Fail("材料或背包空间不足");
    }
    const float efficiency = items::ToolEfficiency(*container, recipe);
    const float seconds = items::EffectiveCraftSeconds(recipe, efficiency);
    job->active = true;
    job->recipeId = recipe.id;
    job->elapsed = 0.0f;
    job->duration = seconds;
    job->toolEfficiency = efficiency;
    job->batchTotal = possible;
    job->batchCompleted = 0;
    if (actual != nullptr) {
        *actual = possible;
    }
    return Ok("开始制作：" + recipe.name);
}

InventoryResult PlayerInventoryService::StartNextQueued(ecs::Entity player,
                                                         std::string prefix) {
    entt::registry& registry = m_world.Registry();
    auto* job = registry.try_get<ecs::CraftingJob>(player);
    if (!m_world.Alive(player) || job == nullptr) {
        return Fail("玩家制作任务不可用");
    }
    if (job->queue.empty()) {
        return Fail("制作队列为空");
    }
    ecs::CraftQueueItem entry = std::move(job->queue.front());
    job->queue.erase(job->queue.begin());
    const items::RecipeDef* recipe = m_recipes.Find(entry.recipeId);
    if (recipe == nullptr) {
        ClearCrafting(*job);
        return Fail(prefix + "队列中断：配方不存在");
    }
    int actual = 0;
    const InventoryResult started = StartCraft(player, *recipe, entry.count, &actual);
    if (!started.ok) {
        ClearCrafting(*job);
        return Fail(prefix + "队列中断：" + started.message);
    }
    return Ok(prefix + "开始队列下一项：" + recipe->name + " ×" +
                  std::to_string(recipe->outputCount) + "（" +
                  std::to_string(actual) + " 件）",
              InventoryTone::Info);
}

void PlayerInventoryService::ClearCrafting(ecs::CraftingJob& job) const {
    job.active = false;
    job.recipeId.clear();
    job.elapsed = 0.0f;
    job.duration = 0.0f;
    job.toolEfficiency = 1.0f;
    job.batchTotal = 0;
    job.batchCompleted = 0;
    job.queue.clear();
}

bool PlayerInventoryService::TickCrafting(ecs::Entity player, float dt,
                                           InventoryResult* result) {
    if (result != nullptr) {
        *result = InventoryResult{};
    }
    entt::registry& registry = m_world.Registry();
    auto* job = registry.try_get<ecs::CraftingJob>(player);
    if (!m_world.Alive(player) || job == nullptr) {
        return false;
    }
    if (!job->active) {
        if (job->queue.empty()) {
            return false;
        }
        const InventoryResult started = StartNextQueued(player, "制作队列恢复：");
        if (result != nullptr) {
            *result = started;
        }
        return true;
    }
    if (!(dt > 0.0f) || !std::isfinite(dt)) {
        return false;
    }
    job->elapsed = std::min(job->duration, job->elapsed + dt);
    if (job->elapsed + 1e-4f < job->duration) {
        return false;
    }

    const std::string recipeId = job->recipeId;
    const items::RecipeDef* recipe = m_recipes.Find(recipeId);
    items::ItemDef const* output = nullptr;
    if (recipe != nullptr) {
        output = m_items.Find(recipe->outputDefId);
    }
    const std::string label =
        output != nullptr ? output->name : (recipe != nullptr ? recipe->outputDefId
                                                              : recipeId);
    auto failCraft = [&](const std::string& reason) {
        const int completed = job->batchCompleted;
        const int total = std::max(job->batchTotal, 1);
        ClearCrafting(*job);
        if (result != nullptr) {
            *result = Fail(completed > 0
                               ? "批量制作中断：已完成 " + std::to_string(completed) +
                                     "/" + std::to_string(total) + "（" + reason + "）"
                               : "制作失败：" + reason);
        }
        return true;
    };

    auto* container = registry.try_get<ecs::Container>(player);
    const auto* position = registry.try_get<ecs::Position>(player);
    const auto* known = registry.try_get<ecs::KnownRecipes>(player);
    if (container == nullptr || recipe == nullptr) {
        return failCraft("配方或背包不可用");
    }
    if (!RecipeKnown(known, recipe->id)) {
        return failCraft("尚未学会配方");
    }
    if (!recipe->station.empty() &&
        (position == nullptr ||
         !StationNearby(recipe->station, position->x, position->y, position->z))) {
        return failCraft("已离开" + StationLabel(recipe->station));
    }
    const items::CraftResult crafted = items::Craft(*container, m_items, *recipe);
    if (!crafted.ok) {
        return failCraft(crafted.error);
    }

    const int total = std::max(job->batchTotal, 1);
    const int completed = ++job->batchCompleted;
    if (completed < total) {
        std::string reason;
        if (!RecipeKnown(known, recipe->id)) {
            reason = "尚未学会配方";
        } else if (!recipe->station.empty() &&
                   (position == nullptr ||
                    !StationNearby(recipe->station, position->x, position->y,
                                   position->z))) {
            reason = "已离开" + StationLabel(recipe->station);
        } else if (!items::CanCraft(*container, m_items, *recipe, &reason)) {
            // reason 已由 CanCraft 填充
        }
        if (!reason.empty()) {
            return failCraft(reason);
        }
        const float efficiency = items::ToolEfficiency(*container, *recipe);
        const float seconds = items::EffectiveCraftSeconds(*recipe, efficiency);
        job->elapsed = 0.0f;
        job->duration = seconds;
        job->toolEfficiency = efficiency;
        if (result != nullptr) {
            *result = Ok("制作中：" + label + "（" + std::to_string(completed + 1) +
                             "/" + std::to_string(total) + "）",
                         InventoryTone::Info);
        }
        return true;
    }

    const std::string completion =
        total > 1 ? "批量制作完成：" + label + " ×" +
                        std::to_string(crafted.outputCount * total) + "（共 " +
                        std::to_string(total) + " 件）"
                  : "制作成功：" + label + " ×" +
                        std::to_string(crafted.outputCount);
    job->active = false;
    job->recipeId.clear();
    job->elapsed = 0.0f;
    job->duration = 0.0f;
    job->toolEfficiency = 1.0f;
    job->batchTotal = 0;
    job->batchCompleted = 0;
    if (!job->queue.empty()) {
        const InventoryResult next = StartNextQueued(player, completion + "；");
        if (result != nullptr) {
            *result = next;
        }
        return true;
    }
    if (result != nullptr) {
        *result = Ok(completion);
    }
    return true;
}

InventoryResult PlayerInventoryService::Dispatch(ecs::Entity player,
                                                  const ui::InventoryAction& action) {
    switch (action.type) {
        case ui::InventoryActionType::Transfer:
            if (action.from == ui::InventoryArea::Backpack &&
                action.to == ui::InventoryArea::Backpack) {
                entt::registry& registry = m_world.Registry();
                auto* container = registry.try_get<ecs::Container>(player);
                if (container == nullptr || action.fromIndex < 0 ||
                    action.toIndex < 0) {
                    return Fail("背包槽不存在");
                }
                if (!items::MoveStack(*container, m_items,
                                      static_cast<std::size_t>(action.fromIndex),
                                      static_cast<std::size_t>(action.toIndex))) {
                    return Fail("无法整理背包");
                }
                return Ok("已整理背包", InventoryTone::Info);
            }
            if (action.from == ui::InventoryArea::Backpack &&
                action.to == ui::InventoryArea::Equipment) {
                return EquipBag(player, action.fromIndex, action.toIndex);
            }
            if (action.from == ui::InventoryArea::Backpack &&
                action.to == ui::InventoryArea::Loot) {
                return DropBag(player, action.fromIndex);
            }
            if (action.from == ui::InventoryArea::Equipment &&
                action.to == ui::InventoryArea::Backpack) {
                return Unequip(player, action.fromIndex);
            }
            if (action.from == ui::InventoryArea::Equipment &&
                action.to == ui::InventoryArea::Loot) {
                return DropEquipment(player, action.fromIndex);
            }
            if (action.from == ui::InventoryArea::Loot &&
                action.to == ui::InventoryArea::Backpack) {
                return Pickup(player, action.fromEntityId);
            }
            if (action.from == ui::InventoryArea::Loot &&
                action.to == ui::InventoryArea::Equipment) {
                return EquipLoot(player, action.fromEntityId, action.toIndex);
            }
            return Fail("不支持的背包拖拽");
        case ui::InventoryActionType::Use:
            return UseBag(player, action.fromIndex);
        case ui::InventoryActionType::Equip: {
            entt::registry& registry = m_world.Registry();
            auto* container = registry.try_get<ecs::Container>(player);
            if (container == nullptr || action.fromIndex < 0 ||
                static_cast<std::size_t>(action.fromIndex) >= container->items.size()) {
                return Fail("背包槽不存在");
            }
            const ecs::ItemInstance& entry =
                container->items[static_cast<std::size_t>(action.fromIndex)];
            const items::ItemDef* def = m_items.Find(entry.defId);
            if (def == nullptr) {
                return Fail("物品定义不存在");
            }
            if (def->type == items::ItemType::Weapon) {
                return EquipBag(player, action.fromIndex,
                                static_cast<int>(ecs::EquipSlot::PrimaryHand));
            }
            if (def->type == items::ItemType::Clothing) {
                return EquipBag(player, action.fromIndex,
                                static_cast<int>(ecs::EquipSlot::Body));
            }
            return Fail("该物品不能装备");
        }
        case ui::InventoryActionType::Unequip:
            return Unequip(player, action.fromIndex);
        case ui::InventoryActionType::Drop:
            if (action.from == ui::InventoryArea::Backpack) {
                return DropBag(player, action.fromIndex);
            }
            if (action.from == ui::InventoryArea::Equipment) {
                return DropEquipment(player, action.fromIndex);
            }
            return Fail("只能丢弃背包或装备槽物品");
        case ui::InventoryActionType::Repair:
            if (action.from != ui::InventoryArea::Backpack) {
                return Fail("只能维修背包中的工具");
            }
            return RepairBag(player, action.fromIndex);
        case ui::InventoryActionType::Pickup:
            return Pickup(player, action.fromEntityId);
        case ui::InventoryActionType::Craft: {
            entt::registry& registry = m_world.Registry();
            auto* job = registry.try_get<ecs::CraftingJob>(player);
            auto* container = registry.try_get<ecs::Container>(player);
            const auto* position = registry.try_get<ecs::Position>(player);
            const auto* known = registry.try_get<ecs::KnownRecipes>(player);
            if (!m_world.Alive(player) || job == nullptr || container == nullptr) {
                return Fail("玩家制作任务不可用");
            }
            const items::RecipeDef* recipe =
                action.fromIndex >= 0
                    ? m_recipes.At(static_cast<std::size_t>(action.fromIndex))
                    : nullptr;
            if (recipe == nullptr) {
                return Fail("配方不存在");
            }
            if (!RecipeKnown(known, recipe->id)) {
                return Fail("尚未学会该配方：阅读配方杂志");
            }
            if (!recipe->station.empty() &&
                (position == nullptr ||
                 !StationNearby(recipe->station, position->x, position->y,
                                position->z))) {
                return Fail("需要附近" + StationLabel(recipe->station));
            }
            const int requested = std::clamp(action.count, 1, 99);
            if (job->active || !job->queue.empty()) {
                if (static_cast<int>(job->queue.size()) >=
                    ecs::kMaxCraftQueueEntries) {
                    return Fail("制作队列已满（最多 " +
                                std::to_string(ecs::kMaxCraftQueueEntries) + " 项）");
                }
                job->queue.push_back(ecs::CraftQueueItem{recipe->id, requested});
                if (!job->active) {
                    return StartNextQueued(player, "已加入制作队列；");
                }
                return Ok("已加入制作队列（第 " +
                              std::to_string(job->queue.size()) + " 项）：" +
                              recipe->name,
                          InventoryTone::Info);
            }
            int actual = 0;
            const InventoryResult started =
                StartCraft(player, *recipe, requested, &actual);
            if (!started.ok) {
                return started;
            }
            return Ok("开始制作：" + recipe->name + " ×" +
                          std::to_string(recipe->outputCount) + "（" +
                          std::to_string(actual) + " 件）",
                      InventoryTone::Info);
        }
        case ui::InventoryActionType::CancelCraft: {
            entt::registry& registry = m_world.Registry();
            auto* job = registry.try_get<ecs::CraftingJob>(player);
            if (!m_world.Alive(player) || job == nullptr ||
                (!job->active && job->queue.empty())) {
                return Fail("没有正在制作的项目");
            }
            const int completed = job->batchCompleted;
            const int total = job->batchTotal;
            const int queued = static_cast<int>(job->queue.size());
            ClearCrafting(*job);
            if (queued > 0) {
                return Ok("已取消制作队列（当前已完成 " + std::to_string(completed) +
                              "/" + std::to_string(total) + "，队列 " +
                              std::to_string(queued) + " 项已清空）",
                          InventoryTone::Info);
            }
            if (completed > 0) {
                return Ok("已取消批量制作（已完成 " + std::to_string(completed) +
                              "/" + std::to_string(total) + "）",
                          InventoryTone::Info);
            }
            return Ok("已取消制作（材料未消耗）", InventoryTone::Info);
        }
        case ui::InventoryActionType::QueueMoveUp:
        case ui::InventoryActionType::QueueMoveDown: {
            entt::registry& registry = m_world.Registry();
            auto* job = registry.try_get<ecs::CraftingJob>(player);
            if (!m_world.Alive(player) || job == nullptr) {
                return Fail("制作任务不可用");
            }
            if (action.fromIndex < 0 ||
                static_cast<std::size_t>(action.fromIndex) >= job->queue.size()) {
                return Fail("队列项不存在");
            }
            const int delta =
                action.type == ui::InventoryActionType::QueueMoveUp ? -1 : 1;
            const int target = action.fromIndex + delta;
            if (target < 0 || target >= static_cast<int>(job->queue.size())) {
                return Fail(delta < 0 ? "队列项已在队首" : "队列项已在队尾");
            }
            std::swap(job->queue[static_cast<std::size_t>(action.fromIndex)],
                      job->queue[static_cast<std::size_t>(target)]);
            return Ok(delta < 0 ? "已上移队列项" : "已下移队列项",
                      InventoryTone::Info);
        }
        case ui::InventoryActionType::QueueRemove: {
            entt::registry& registry = m_world.Registry();
            auto* job = registry.try_get<ecs::CraftingJob>(player);
            if (!m_world.Alive(player) || job == nullptr ||
                action.fromIndex < 0 ||
                static_cast<std::size_t>(action.fromIndex) >= job->queue.size()) {
                return Fail("队列项不存在");
            }
            job->queue.erase(job->queue.begin() + action.fromIndex);
            return Ok("已移除制作队列项", InventoryTone::Info);
        }
        case ui::InventoryActionType::QueueSetCount: {
            entt::registry& registry = m_world.Registry();
            auto* job = registry.try_get<ecs::CraftingJob>(player);
            if (!m_world.Alive(player) || job == nullptr ||
                action.fromIndex < 0 ||
                static_cast<std::size_t>(action.fromIndex) >= job->queue.size()) {
                return Fail("队列项不存在");
            }
            ecs::CraftQueueItem& entry =
                job->queue[static_cast<std::size_t>(action.fromIndex)];
            entry.count = std::clamp(action.count, 1, 99);
            return Ok("已调整队列项数量", InventoryTone::Info);
        }
    }
    return Fail("未知背包动作");
}

}  // namespace zl::game
