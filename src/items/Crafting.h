#pragma once

#include <string>
#include <vector>

#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"
#include "items/RecipeDef.h"

namespace zl::items {

struct CraftResult {
    bool ok = false;
    std::string error;
    std::string outputDefId;
    int outputCount = 0;
    std::vector<std::string> brokenToolDefIds;
};

// 工具状态对制作效率的影响：无工具为 1.0；否则为 0.5 + 0.5 * 最低可用工具耐久。
// 缺少工具/工具全部损坏时返回 0。
float ToolEfficiency(const ecs::Container& container, const RecipeDef& recipe);

// 按效率换算实际耗时；efficiency<=0 返回 0。效率下限 0.5，实际耗时最多为基础耗时 2 倍。
float EffectiveCraftSeconds(const RecipeDef& recipe, float efficiency);

// 单次制作的计时状态。材料在计时完成时才原子扣除，工具按配方磨损，取消/中断不会丢失物品。
struct CraftingProcess {
    bool active = false;
    std::string recipeId;
    float duration = 0.0f;
    float elapsed = 0.0f;
    float toolEfficiency = 1.0f;

    void Start(std::string id, float seconds, float efficiency = 1.0f);
    void Cancel();
    bool Advance(float dt);  // 返回 true 表示本次推进完成计时
    float Progress() const;  // 0~1；未激活时为 0
};

// 检查材料与输出空间；不修改容器。reason 返回首个不满足项。
bool CanCraft(const ecs::Container& container, const ItemRegistry& registry,
              const RecipeDef& recipe, std::string* reason = nullptr);

// 原子制作：先在副本上扣除材料并加入产物，全部成功后一次性替换容器。
CraftResult Craft(ecs::Container& container, const ItemRegistry& registry,
                  const RecipeDef& recipe);

// 批量预检：在副本上连续制作，返回最多可连续完成的次数（含工具磨损/损坏与输出空间）。
// limit 为本次请求上限，必须 >0；不修改原容器。
int MaxCraftCount(const ecs::Container& container, const ItemRegistry& registry,
                  const RecipeDef& recipe, int limit);

}  // namespace zl::items
