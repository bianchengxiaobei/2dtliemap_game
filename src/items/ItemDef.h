#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace zl::items {

// 物品类型（M6-T1）：JSON "type" 字段的取值集合。
enum class ItemType : std::uint8_t {
    Generic = 0,  // 杂物（无特殊用途，如破布）
    Weapon,       // 武器（M6-T5 装备闭环）
    Consumable,   // 消耗品（吃/喝/包扎，M6-T5 使用闭环）
    Clothing,     // 衣物（穿戴）
    Container,    // 容器（背包；容量规则 M6-T2）
};

const char* ItemTypeToString(ItemType type);

// 解析类型名（JSON 中的小写字符串）；失败返回 false 且不改 *out
bool ParseItemType(std::string_view text, ItemType* out);

// 使用效果（M6-T5；JSON "use" 可选对象）：
//   heal：立即回复 HP（0~100）；hunger/thirst/fatigue：降低对应需求量
//   （0~1 归一化，正值 = 减少需求）。无 use 字段 → 全 0（不可使用）。
struct UseEffect {
    float heal = 0.0f;
    float hunger = 0.0f;
    float thirst = 0.0f;
    float fatigue = 0.0f;

    bool Any() const {
        return heal > 0.0f || hunger > 0.0f || thirst > 0.0f || fatigue > 0.0f;
    }
};

// 物品定义：不可变模板（数据来自 assets/items.json，M6-T1）。
// 实例的可变状态（耐久/剩余量等）在 ItemInstance 组件（M6-T2），
// 组件只引用定义 id，不复制 ItemDef。
struct ItemDef {
    std::string id;       // 唯一标识：小写字母/数字/下划线/点，1~64 字符
    std::string name;     // 显示名（可中文）
    std::string icon;     // 逻辑资源名（映射见 assets/asset_manifest.json）
    float weight = 0.0f;  // 单件重量（kg，0 ~ 1000）
    int maxStack = 1;     // 堆叠上限（1 ~ 9999）
    ItemType type = ItemType::Generic;
    UseEffect use;        // 使用效果（M6-T5；消耗品用）
};

}  // namespace zl::items
