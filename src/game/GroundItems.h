#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ecs/EcsWorld.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"

namespace zl::game {

// M6-T3：地面物品——带 ItemInstance + SpriteRef(icon) 的实体（无 Collider/RigidBody：
// 不阻挡、不参与物理）。经 SpatialIndex 按 chunk 桶管理：渲染收集只在相机视口覆盖的
// chunk 上查询（RenderCollectSystem）→ 远离相机的物品自然"睡眠"：不进渲染收集、
// 不产生 drawcall，也不需要每帧全量遍历。
// 拾取 = InventoryOps::AddItem + 地面实体数量扣减（减到 0 才销毁实体，支持部分拾取）。

// 生成地面物品实体；item 无效或注册表无此定义 → entt::null 并告警。
// SpriteRef 用 ItemDef.icon 的哈希、层 -1（同格时画在角色之下）。
ecs::Entity SpawnGroundItem(ecs::EcsWorld& world, const items::ItemRegistry& registry,
                            const ecs::ItemInstance& item, float px, float py, int z = 0);

// 地面物品实体数（调试/HUD）
int CountGroundItems(ecs::EcsWorld& world);

// 半径内最近的同层（z 相等）地面物品；无 → entt::null
ecs::Entity FindNearestGroundItem(ecs::EcsWorld& world, float px, float py, int z,
                                  float maxRadius);

struct GroundItemInfo {
    ecs::Entity entity = entt::null;
    ecs::ItemInstance item;
    float distanceSq = 0.0f;
};

// 半径内同层地面物品快照，按距离从近到远排序
std::vector<GroundItemInfo> GroundItemsInRadius(ecs::EcsWorld& world, float px, float py,
                                                int z, float maxRadius);

// 拾取结果（调用方拼 HUD 文案与日志；HUD 为 RmlUi，支持中文）
struct PickupResult {
    bool found = false;         // 半径内是否有地面物品
    int picked = 0;             // 实际入包数量（部分拾取时 < 地面原有数量）
    int groundRemaining = 0;    // 拾取后地面剩余数量（0 = 无剩余/实体已销毁）
    std::string defId;          // 物品定义 id（found=true 时有效）
    std::uint64_t entityId = 0; // 拾取目标稳定 ID（0 = 无）
};

// 把指定地面物品并入容器（规则/容量见 InventoryOps，可部分拾取）
PickupResult PickupGroundItem(ecs::EcsWorld& world, const items::ItemRegistry& registry,
                              ecs::Container& container, ecs::Entity target);

// 把半径内最近的地面物品并入容器（规则/容量见 InventoryOps，可部分拾取——
// 装不下的部分留在地面）。地面数量减到 0 时销毁实体。
PickupResult PickupNearestItem(ecs::EcsWorld& world, const items::ItemRegistry& registry,
                               ecs::Container& container, float px, float py, int z,
                               float maxRadius);

}  // namespace zl::game
