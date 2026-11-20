#include "game/CompanionInventory.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "ecs/components/CommonComponents.h"
#include "game/Armor.h"
#include "items/ItemDef.h"
#include "items/ItemRegistry.h"

namespace zl::game {
namespace {

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool ValidCount(int count) { return count > 0; }

CompanionState* FindWritableState(ecs::EcsWorld& world, ecs::Entity entity,
                                  std::string* error) {
    if (!world.Alive(entity)) {
        SetError(error, "同伴不存在");
        return nullptr;
    }
    CompanionState* state = world.Registry().try_get<CompanionState>(entity);
    if (state == nullptr) {
        SetError(error, "目标不是同伴");
    }
    return state;
}

bool FitInContainer(const ecs::Container& container,
                    const items::ItemRegistry& registry,
                    const ecs::ItemInstance& item) {
    if (!item.Valid()) {
        return false;
    }
    ecs::Container probe = container;
    return items::AddItem(probe, registry, item) == item.count;
}

}  // namespace

int AddItemToCompanion(ecs::EcsWorld& world, ecs::Entity companion,
                       const items::ItemRegistry& registry,
                       const ecs::ItemInstance& item) {
    CompanionState* state = FindWritableState(world, companion, nullptr);
    if (state == nullptr || state->dead) {
        return 0;
    }
    return items::AddItem(state->inventory, registry, item);
}

int RemoveItemFromCompanion(ecs::EcsWorld& world, ecs::Entity companion,
                            std::string_view defId, int count) {
    CompanionState* state = FindWritableState(world, companion, nullptr);
    if (state == nullptr || state->dead || !ValidCount(count)) {
        return 0;
    }
    return items::RemoveItem(state->inventory, defId, count);
}

int TransferItemToCompanion(ecs::Container& source, std::size_t sourceIndex,
                            ecs::EcsWorld& world, ecs::Entity companion,
                            const items::ItemRegistry& registry, int count) {
    CompanionState* state = FindWritableState(world, companion, nullptr);
    if (state == nullptr || state->dead || !ValidCount(count) ||
        sourceIndex >= source.items.size()) {
        return 0;
    }

    ecs::ItemInstance attempt = source.items[sourceIndex];
    attempt.count = std::min(count, attempt.count);
    if (!attempt.Valid()) {
        return 0;
    }
    ecs::Container probe = state->inventory;
    const int allowed = items::AddItem(probe, registry, attempt);
    if (allowed <= 0) {
        return 0;
    }

    ecs::ItemInstance removed;
    if (!items::TakeAt(source, sourceIndex, allowed, &removed)) {
        return 0;
    }
    const int added = items::AddItem(state->inventory, registry, removed);
    if (added != removed.count) {
        ecs::ItemInstance remainder = removed;
        remainder.count -= added;
        (void)items::AddItem(source, registry, remainder);
    }
    return added;
}

int TransferItemFromCompanion(ecs::EcsWorld& world, ecs::Entity companion,
                              const items::ItemRegistry& registry,
                              std::size_t sourceIndex, int count,
                              ecs::Container& destination) {
    CompanionState* state = FindWritableState(world, companion, nullptr);
    if (state == nullptr || state->dead || !ValidCount(count) ||
        sourceIndex >= state->inventory.items.size()) {
        return 0;
    }

    ecs::ItemInstance attempt = state->inventory.items[sourceIndex];
    attempt.count = std::min(count, attempt.count);
    if (!attempt.Valid()) {
        return 0;
    }
    ecs::Container probe = destination;
    const int allowed = items::AddItem(probe, registry, attempt);
    if (allowed <= 0) {
        return 0;
    }

    ecs::ItemInstance removed;
    if (!items::TakeAt(state->inventory, sourceIndex, allowed, &removed)) {
        return 0;
    }
    const int added = items::AddItem(destination, registry, removed);
    if (added != removed.count) {
        ecs::ItemInstance remainder = removed;
        remainder.count -= added;
        (void)items::AddItem(state->inventory, registry, remainder);
    }
    return added;
}

bool RefreshCompanionDerivedStats(ecs::EcsWorld& world,
                                  ecs::Entity companion,
                                  const CompanionState& state,
                                  const items::ItemRegistry* registry) {
    if (!world.Alive(companion)) {
        return false;
    }
    entt::registry& ecsRegistry = world.Registry();
    ecs::DamageResist resist;
    if (registry != nullptr) {
        const PlayerArmor armor =
            ResolvePlayerArmor(state.equipment, *registry);
        if (armor.hasArmor) {
            resist = armor.resist;
        }
    }
    if (auto* current = ecsRegistry.try_get<ecs::DamageResist>(companion);
        current != nullptr) {
        *current = resist;
    } else {
        ecsRegistry.emplace<ecs::DamageResist>(companion, resist);
    }
    return true;
}

bool PrepareLoadedCompanion(ecs::EcsWorld& world, ecs::Entity companion,
                            const items::ItemRegistry* registry) {
    if (!world.Alive(companion)) {
        return false;
    }
    entt::registry& ecsRegistry = world.Registry();
    if (ecsRegistry.try_get<Companion>(companion) == nullptr) {
        return false;
    }
    if (!ecsRegistry.all_of<ecs::FriendlyNpc>(companion)) {
        ecsRegistry.emplace<ecs::FriendlyNpc>(companion);
    }
    if (!ecsRegistry.all_of<ecs::NoCombatTarget>(companion)) {
        ecsRegistry.emplace<ecs::NoCombatTarget>(companion);
    }
    const CompanionState* state =
        ecsRegistry.try_get<CompanionState>(companion);
    return state != nullptr &&
           RefreshCompanionDerivedStats(world, companion, *state, registry);
}

bool EquipCompanionItem(ecs::EcsWorld& world, ecs::Entity companion,
                        const items::ItemRegistry& registry,
                        std::size_t inventoryIndex, ecs::EquipSlot slot,
                        std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    CompanionState* state = FindWritableState(world, companion, error);
    if (state == nullptr) {
        return false;
    }
    if (state->downed || state->dead) {
        SetError(error, "同伴无法装备物品");
        return false;
    }
    if (inventoryIndex >= state->inventory.items.size()) {
        SetError(error, "背包物品不存在");
        return false;
    }

    ecs::ItemInstance one = state->inventory.items[inventoryIndex];
    one.count = 1;
    ecs::Equipment nextEquipment = state->equipment;
    ecs::ItemInstance swapped;
    if (!items::Equip(nextEquipment, registry, slot, one, &swapped)) {
        SetError(error, "该物品不能装备到目标槽位");
        return false;
    }

    ecs::Container nextInventory = state->inventory;
    ecs::ItemInstance removed;
    if (!items::TakeAt(nextInventory, inventoryIndex, 1, &removed)) {
        SetError(error, "背包物品不存在");
        return false;
    }
    if (swapped.Valid() &&
        items::AddItem(nextInventory, registry, swapped) != swapped.count) {
        SetError(error, "背包空间不足，无法换下原装备");
        return false;
    }

    state->equipment = std::move(nextEquipment);
    state->inventory = std::move(nextInventory);
    (void)RefreshCompanionDerivedStats(world, companion, *state, &registry);
    return true;
}

bool UnequipCompanionItem(ecs::EcsWorld& world, ecs::Entity companion,
                          const items::ItemRegistry& registry,
                          ecs::EquipSlot slot, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    CompanionState* state = FindWritableState(world, companion, error);
    if (state == nullptr) {
        return false;
    }
    if (state->dead) {
        SetError(error, "同伴已死亡");
        return false;
    }

    ecs::Equipment nextEquipment = state->equipment;
    ecs::ItemInstance removed;
    if (!items::Unequip(nextEquipment, slot, &removed)) {
        SetError(error, "装备槽为空");
        return false;
    }
    ecs::Container nextInventory = state->inventory;
    if (!FitInContainer(nextInventory, registry, removed)) {
        SetError(error, "背包空间不足");
        return false;
    }

    state->equipment = std::move(nextEquipment);
    state->inventory = std::move(nextInventory);
    (void)items::AddItem(state->inventory, registry, removed);
    (void)RefreshCompanionDerivedStats(world, companion, *state, &registry);
    return true;
}

bool UseCompanionConsumable(ecs::EcsWorld& world, ecs::Entity companion,
                            const items::ItemRegistry& registry,
                            std::size_t inventoryIndex,
                            items::UseResult* result, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    CompanionState* state = FindWritableState(world, companion, error);
    if (state == nullptr) {
        return false;
    }
    if (state->downed || state->dead) {
        SetError(error, "倒地/死亡同伴无法自行使用物品");
        return false;
    }
    ecs::Health* health = world.Registry().try_get<ecs::Health>(companion);
    if (health == nullptr || health->current <= 0.0f) {
        SetError(error, "同伴没有可用生命值");
        return false;
    }

    const items::UseResult use =
        items::UseAt(state->inventory, registry, inventoryIndex);
    if (!use.applied) {
        SetError(error, "该物品不可使用");
        return false;
    }
    items::ApplyUseEffect(use.effect, health->max, &health->current, nullptr,
                          nullptr, nullptr);
    if (result != nullptr) {
        *result = use;
    }
    return true;
}

int ReloadCompanionWeapon(ecs::EcsWorld& world, ecs::Entity companion,
                          const items::ItemRegistry& registry,
                          std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    CompanionState* state = FindWritableState(world, companion, error);
    if (state == nullptr) {
        return 0;
    }
    if (state->downed || state->dead) {
        SetError(error, "倒地/死亡同伴无法装填弹药");
        return 0;
    }

    auto& slot = state->equipment.slots[static_cast<std::size_t>(
        ecs::EquipSlot::PrimaryHand)];
    if (!slot.has_value()) {
        SetError(error, "同伴没有主手武器");
        return 0;
    }
    const items::ItemDef* def = registry.Find(slot->defId);
    if (def == nullptr || !def->ranged.has_value()) {
        SetError(error, "主手武器不是远程武器");
        return 0;
    }
    const int magazine = std::max(0, def->ranged->magazine);
    const int loaded = std::clamp(slot->loadedAmmo, 0, magazine);
    const int missing = magazine - loaded;
    if (missing <= 0) {
        SetError(error, "弹匣已满");
        return 0;
    }
    if (def->ranged->ammoDefId.empty()) {
        SetError(error, "武器没有弹药定义");
        return 0;
    }

    const int available =
        items::CountOf(state->inventory, def->ranged->ammoDefId);
    const int take = std::min(missing, available);
    if (take <= 0) {
        SetError(error, "背包内没有匹配弹药");
        return 0;
    }
    if (items::RemoveItem(state->inventory, def->ranged->ammoDefId, take) !=
        take) {
        SetError(error, "弹药扣除失败");
        return 0;
    }
    slot->loadedAmmo = loaded + take;
    return take;
}

}  // namespace zl::game
