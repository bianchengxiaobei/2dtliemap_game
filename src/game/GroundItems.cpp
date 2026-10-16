#include "game/GroundItems.h"

#include "core/Log.h"
#include "ecs/components/CommonComponents.h"
#include "items/InventoryOps.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "items";
constexpr int kGroundItemLayer = -1;  // 同格时物品画在角色/僵尸之下

ecs::Entity NearestGroundItem(ecs::EcsWorld& world, float px, float py, int z,
                              float maxRadius, float* outDistSq) {
    entt::registry& registry = world.Registry();
    ecs::Entity best = entt::null;
    const float maxDistSq = maxRadius * maxRadius;
    float bestDistSq = maxDistSq;
    registry.view<ecs::Position, ecs::ItemInstance>().each(
        [&](const ecs::Entity e, const ecs::Position& pos, const ecs::ItemInstance&) {
            if (!world.Alive(e) || pos.z != z) {
                return;
            }
            const float dx = pos.x - px;
            const float dy = pos.y - py;
            const float distSq = dx * dx + dy * dy;
            if (distSq > maxDistSq) {
                return;  // continue：半径外
            }
            if (best == entt::null || distSq < bestDistSq) {
                best = e;
                bestDistSq = distSq;
            }
        });
    if (best == entt::null) {
        return entt::null;
    }
    if (outDistSq != nullptr) {
        *outDistSq = bestDistSq;
    }
    return best;
}

}  // namespace

ecs::Entity SpawnGroundItem(ecs::EcsWorld& world, const items::ItemRegistry& registry,
                            const ecs::ItemInstance& item, float px, float py, int z) {
    if (!item.Valid()) {
        ZL_WARN(kLogModule, "SpawnGroundItem：无效物品实例（忽略）");
        return entt::null;
    }
    const items::ItemDef* def = registry.Find(item.defId);
    if (def == nullptr) {
        ZL_WARN(kLogModule, "SpawnGroundItem：未知物品定义 '{}'（忽略）", item.defId);
        return entt::null;
    }
    entt::registry& reg = world.Registry();
    const ecs::Entity e = world.Create();
    reg.emplace<ecs::Position>(e, ecs::Position{px, py, z});
    reg.emplace<ecs::ItemInstance>(e, item);
    reg.emplace<ecs::SpriteRef>(
        e, ecs::SpriteRef{ecs::HashName(def->icon), kGroundItemLayer, 0xFFFFFFFFu});
    return e;
}

int CountGroundItems(ecs::EcsWorld& world) {
    int count = 0;
    world.Registry().view<ecs::ItemInstance>().each(
        [&](const ecs::Entity e, const ecs::ItemInstance&) {
            if (world.Alive(e)) {
                ++count;
            }
        });
    return count;
}

ecs::Entity FindNearestGroundItem(ecs::EcsWorld& world, float px, float py, int z,
                                  float maxRadius) {
    return NearestGroundItem(world, px, py, z, maxRadius, nullptr);
}

PickupResult PickupNearestItem(ecs::EcsWorld& world, const items::ItemRegistry& registry,
                               ecs::Container& container, float px, float py, int z,
                               float maxRadius) {
    PickupResult result;
    const ecs::Entity target = NearestGroundItem(world, px, py, z, maxRadius, nullptr);
    if (target == entt::null) {
        return result;
    }
    result.found = true;
    result.entityId = world.IdOf(target).id;

    auto* instance = world.Registry().try_get<ecs::ItemInstance>(target);
    if (instance == nullptr) {
        return result;  // 理论不出现（view 已过滤）
    }
    result.defId = instance->defId;
    result.groundRemaining = instance->count;

    const int added = items::AddItem(container, registry, *instance);
    if (added <= 0) {
        return result;  // 背包满/未知定义：地面不变
    }
    instance->count -= added;
    result.picked = added;
    result.groundRemaining = instance->count;
    if (instance->count <= 0) {
        world.Destroy(target);
        result.groundRemaining = 0;
    }
    return result;
}

}  // namespace zl::game
