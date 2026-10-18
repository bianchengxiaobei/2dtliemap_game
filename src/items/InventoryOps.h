#pragma once

#include <cstddef>
#include <string_view>

#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"

namespace zl::items {

// M6-T2：容器/装备规则（纯函数，无 ECS/物理依赖，全部可无头单测）。
// 约定：不改动 ItemRegistry；未知 defId 视为无效操作（告警 + 不生效）。

// 往容器加入物品：先填已有未满堆叠（合并），再开新堆叠；受容量约束，允许部分加入。
// 返回实际加入数量（0 = 一件未加；< item.count = 部分加入，剩余由调用方处理——
// 例如拾取到一半时地面实体保留剩余数量）。合并保留原堆叠的 condition（M6 简化）。
int AddItem(ecs::Container& container, const ItemRegistry& registry,
            const ecs::ItemInstance& item);

// 从容器移除最多 count 件 defId 物品（按容器条目顺序）。返回实际移除数量
// （少于 count = 容器内不够）。
int RemoveItem(ecs::Container& container, std::string_view defId, int count);

// 从指定堆叠取出最多 count 件；out 写入被取出的实例（数量为实际取出数）。
bool TakeAt(ecs::Container& container, std::size_t index, int count,
            ecs::ItemInstance* out);

// 拖拽/排序：同定义合并到未满堆叠；不同定义交换；目标为空位（index >= size）移到末尾。
bool MoveStack(ecs::Container& container, const ItemRegistry& registry, std::size_t from,
               std::size_t to);

// 容器内 defId 的总数量（跨堆叠求和）
int CountOf(const ecs::Container& container, std::string_view defId);

// 容器当前总重量（kg）；未知 defId 记 0 并告警
float TotalWeight(const ecs::Container& container, const ItemRegistry& registry);

// 槽位是否接受该物品类型（M6：主手=武器，躯干=衣物）
bool SlotAccepts(ecs::EquipSlot slot, ItemType type);
const char* EquipSlotToString(ecs::EquipSlot slot);

// 装备到槽位：校验物品、槽位类型匹配；原槽内容（若有）移入 *outSwapped
// （outSwapped 非空且槽位空时写入无效实例，Valid()==false）。失败不改动任何状态。
bool Equip(ecs::Equipment& equipment, const ItemRegistry& registry, ecs::EquipSlot slot,
           const ecs::ItemInstance& item, ecs::ItemInstance* outSwapped);

// 卸下槽位：成功 true 并移出到 *outRemoved（可为 null）；槽位空返回 false
bool Unequip(ecs::Equipment& equipment, ecs::EquipSlot slot, ecs::ItemInstance* outRemoved);

// 使用结果（M6-T5）：调用方负责把 effect 落地到 Health/Needs（含钳制）与提示
struct UseResult {
    bool applied = false;  // false = 容器里没有可使用的物品（未消耗任何东西）
    std::string defId;     // 被消耗的物品定义 id
    UseEffect effect;      // 定义中的使用效果
    int remaining = 0;     // 该堆叠使用后剩余数量（0 = 条目已移除）
};

// 使用容器中第一个带使用效果（ItemDef.use.Any()）的物品：消耗 1 件
UseResult UseFirstConsumable(ecs::Container& container, const ItemRegistry& registry);

// 使用指定堆叠中的 1 件（无效果/索引非法时不消耗）
UseResult UseAt(ecs::Container& container, const ItemRegistry& registry, std::size_t index);

// 把使用效果落地到数值（纯函数，可单测）：HP 加到不超过 hpMax（返回实际回血量）；
// 需求三项钳到 [0,1]（指针可为 null = 该实体无需求组件）
float ApplyUseEffect(const UseEffect& effect, float hpMax, float* hpCurrent, float* hunger,
                     float* thirst, float* fatigue);

}  // namespace zl::items
