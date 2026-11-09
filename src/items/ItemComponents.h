#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace zl::ecs {

// M6-T2：物品实例——地面实体与容器条目共用的数据结构。
// 只引用定义 id（定义本体在 items::ItemRegistry），实例状态 = count/condition/loadedAmmo。
struct ItemInstance {
    std::string defId;       // 引用 ItemDef.id（空 = 无效实例）
    int count = 1;           // 堆叠数量（1 ~ ItemDef.maxStack）
    float condition = 1.0f;  // 状态/耐久 [0,1]（武器磨损接后续；拾取/丢弃原样携带）
    int loadedAmmo = 0;      // M23-F1-T2：枪械已装填弹药；0=空，非远程物品忽略

    bool Valid() const {
        return !defId.empty() && count > 0 && loadedAmmo >= 0;
    }
};

// M6-T2：背包/容器（玩家主背包、衣物口袋、尸体容器共用）。
// 条目内联存储（PZ InventoryItem 思路）：拾取 = 地面实体销毁 + AddItem；
// 丢弃 = RemoveItem + 地面实体生成。容量/堆叠规则（纯函数）在 items::InventoryOps。
struct Container {
    std::vector<ItemInstance> items;
    float capacityKg = 20.0f;  // 重量上限（kg）
};

// M14-T5：可搜刮容器。Container 负责物品/容量，本组件负责一次性 roll 与存档状态。
// tableId 指向 assets/loot.json；rollSeed 首搜时落定，保证固定种子可复现且读档后不再重 roll。
struct LootContainer {
    std::string tableId;
    std::uint64_t rollSeed = 0;
    bool searched = false;
};

struct PlayerStorage {
    int maxStacks = 6;
};

// M6-T2：装备槽（M6 最小集；扩展 = 加枚举值 + SlotAccepts 规则 + 渲染挂点）
enum class EquipSlot : std::uint8_t {
    PrimaryHand = 0,  // 主手（仅武器）
    Body = 1,         // 躯干（仅衣物）
};
inline constexpr std::size_t kEquipSlotCount = 2;

struct Equipment {
    std::array<std::optional<ItemInstance>, kEquipSlotCount> slots{};
};

}  // namespace zl::ecs
