#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"

namespace zl::items {

// 工具维修结果：成功时 before/after 为维修前后耐久，材料数量从容器扣除。
struct RepairResult {
    bool ok = false;
    std::string error;
    std::string defId;
    float before = 0.0f;
    float after = 0.0f;
    std::string materialDefId;
    int materialCount = 0;
};

// 维修指定槽位的武器/工具：要求不可堆叠、condition 在 (0,1)、restore 在 (0,1]，
// 并在同一容器内消耗 materialCount 个 materialDefId。先在副本上执行，成功后整体替换，
// 因此失败不会修改耐久或材料。工具损坏（condition<=0）后不可维修。
RepairResult Repair(ecs::Container& container, const ItemRegistry& registry,
                    std::size_t index, std::string_view materialDefId,
                    int materialCount, float restore);

}  // namespace zl::items
