#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "ecs/EcsWorld.h"
#include "game/Companion.h"
#include "items/InventoryOps.h"

namespace zl::items {
class ItemRegistry;
}

namespace zl::game {

// M33-F3-T4：同伴背包/装备操作。所有物品移动都先试算目标容量，再提交，
// 避免“源已扣除、目标没放进去”的复制或丢失。服务器/单机共用这些规则。
int AddItemToCompanion(ecs::EcsWorld& world, ecs::Entity companion,
                       const items::ItemRegistry& registry,
                       const ecs::ItemInstance& item);
int RemoveItemFromCompanion(ecs::EcsWorld& world, ecs::Entity companion,
                            std::string_view defId, int count);
int TransferItemToCompanion(ecs::Container& source,
                            std::size_t sourceIndex, ecs::EcsWorld& world,
                            ecs::Entity companion,
                            const items::ItemRegistry& registry,
                            int count = 1);
int TransferItemFromCompanion(ecs::EcsWorld& world, ecs::Entity companion,
                              const items::ItemRegistry& registry,
                              std::size_t sourceIndex, int count,
                              ecs::Container& destination);

bool EquipCompanionItem(ecs::EcsWorld& world, ecs::Entity companion,
                        const items::ItemRegistry& registry,
                        std::size_t inventoryIndex, ecs::EquipSlot slot,
                        std::string* error = nullptr);
bool UnequipCompanionItem(ecs::EcsWorld& world, ecs::Entity companion,
                          const items::ItemRegistry& registry,
                          ecs::EquipSlot slot, std::string* error = nullptr);

// 从躯干装备重算 DamageResist；装备条件变化或读档后调用。
bool RefreshCompanionDerivedStats(ecs::EcsWorld& world, ecs::Entity companion,
                                  const CompanionState& state,
                                  const items::ItemRegistry* registry);

// 读档后补运行时标签/派生属性；identity/state 需已由存档组件恢复。
bool PrepareLoadedCompanion(ecs::EcsWorld& world, ecs::Entity companion,
                            const items::ItemRegistry* registry);

// 使用背包中药/食物。只落地 UseEffect 的即时数值，不执行制作或自动行为。
bool UseCompanionConsumable(ecs::EcsWorld& world, ecs::Entity companion,
                            const items::ItemRegistry& registry,
                            std::size_t inventoryIndex,
                            items::UseResult* result = nullptr,
                            std::string* error = nullptr);

// 从背包取弹药填装主手远程武器；返回实际装填数量，满弹/无弹药返回 0。
int ReloadCompanionWeapon(ecs::EcsWorld& world, ecs::Entity companion,
                          const items::ItemRegistry& registry,
                          std::string* error = nullptr);

}  // namespace zl::game
