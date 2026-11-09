#include "items/Repair.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "items/InventoryOps.h"

namespace zl::items {

RepairResult Repair(ecs::Container& container, const ItemRegistry& registry,
                    std::size_t index, std::string_view materialDefId,
                    int materialCount, float restore) {
    RepairResult result;
    result.materialDefId = std::string(materialDefId);
    result.materialCount = materialCount;
    if (index >= container.items.size()) {
        result.error = "物品不存在";
        return result;
    }

    const ecs::ItemInstance& entry = container.items[index];
    if (!entry.Valid()) {
        result.error = "物品实例无效";
        return result;
    }
    result.defId = entry.defId;
    result.before = entry.condition;
    const ItemDef* target = registry.Find(entry.defId);
    if (target == nullptr) {
        result.error = "物品定义不存在：" + entry.defId;
        return result;
    }
    if (target->type != ItemType::Weapon && target->type != ItemType::Clothing) {
        result.error = "该物品不可维修";
        return result;
    }
    if (target->maxStack != 1) {
        result.error = "可堆叠物品不可维修";
        return result;
    }
    if (!std::isfinite(entry.condition) || entry.condition <= 0.0f) {
        result.error = "工具已损坏，无法维修";
        return result;
    }
    if (entry.condition >= 1.0f) {
        result.error = "耐久已满，无需维修";
        return result;
    }
    if (!std::isfinite(restore) || restore <= 0.0f || restore > 1.0f) {
        result.error = "维修量无效";
        return result;
    }
    if (materialDefId.empty()) {
        result.error = "未配置维修材料";
        return result;
    }
    if (materialCount <= 0) {
        result.error = "维修材料数量无效";
        return result;
    }
    if (entry.defId == materialDefId) {
        result.error = "维修材料不能是工具本身";
        return result;
    }
    const ItemDef* material = registry.Find(materialDefId);
    if (material == nullptr) {
        result.error = "维修材料未注册：" + result.materialDefId;
        return result;
    }
    const int have = CountOf(container, materialDefId);
    if (have < materialCount) {
        result.error = "缺少维修材料：" + material->name + " ×" +
                       std::to_string(materialCount) + "（拥有 " +
                       std::to_string(have) + "）";
        return result;
    }

    // 在副本上完成耐久修改与材料扣除，避免 RemoveItem 失败时留下半完成状态。
    ecs::Container attempt = container;
    const float efficiency =
        std::clamp(target->repairEfficiency, 0.1f, 3.0f);
    const float repaired =
        std::min(1.0f, attempt.items[index].condition + restore * efficiency);
    attempt.items[index].condition = repaired;
    if (RemoveItem(attempt, materialDefId, materialCount) != materialCount) {
        result.error = "维修材料扣除失败";
        return result;
    }
    result.after = repaired;
    container = std::move(attempt);
    result.ok = true;
    return result;
}

}  // namespace zl::items
