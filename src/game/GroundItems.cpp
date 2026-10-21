#include "game/GroundItems.h"

#include <algorithm>
#include <cmath>

#include "ai/Passability.h"
#include "core/Log.h"
#include "ecs/components/CommonComponents.h"
#include "game/WorldQueries.h"
#include "items/InventoryOps.h"
#include "world/World.h"

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

int SpawnDemoGroundItems(ecs::EcsWorld& world, const items::ItemRegistry& registry,
                         world::World& map, float baseX, float baseY, int z) {
    struct Spot {
        int dx;
        int dy;
        const char* defId;
        int count;
    };
    static constexpr Spot kSpots[] = {
        {0, 0, "bandage", 2},
        {1, 1, "axe", 1},
        {-3, -7, "canned_beans", 1},
        {4, -8, "kitchen_knife", 1},
        {-6, -3, "rag", 3},
        {7, -4, "tote_bag", 1},
        {-7, 4, "jacket", 1},
        {-5, 4, "hammer", 1},
        {5, 6, "rag", 2},
        {2, 1, "repair_kit", 2},
        {4, 2, "magazine_cooking", 1},
        {-2, 2, "water_bottle", 1},
        {3, 2, "energy_bar", 2},
    };
    const int baseGridX = static_cast<int>(std::floor(baseX));
    const int baseGridY = static_cast<int>(std::floor(baseY));
    int spawned = 0;
    for (const Spot& spot : kSpots) {
        int wx = 0;
        int wy = 0;
        const bool found = ::zl::ai::FindNearestWalkable(
            baseGridX + spot.dx, baseGridY + spot.dy, 6,
            [&](int x, int y) { return !IsSquareBlocked(map, x, y, z); }, &wx,
            &wy);
        if (!found) {
            ZL_WARN(kLogModule, "散落点 ({}, {}) 附近找不到可走格，跳过 {}",
                    baseGridX + spot.dx, baseGridY + spot.dy, spot.defId);
            continue;
        }
        const ecs::ItemInstance stack{spot.defId, spot.count, 1.0f};
        if (SpawnGroundItem(world, registry, stack,
                            static_cast<float>(wx) + 0.5f,
                            static_cast<float>(wy) + 0.5f, z) != entt::null) {
            ++spawned;
        }
    }
    return spawned;
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

std::vector<GroundItemInfo> GroundItemsInRadius(ecs::EcsWorld& world, float px, float py,
                                                int z, float maxRadius) {
    std::vector<GroundItemInfo> result;
    const float maxDistSq = maxRadius * maxRadius;
    world.Registry().view<ecs::Position, ecs::ItemInstance>().each(
        [&](const ecs::Entity e, const ecs::Position& pos, const ecs::ItemInstance& item) {
            if (!world.Alive(e) || pos.z != z) {
                return;
            }
            const float dx = pos.x - px;
            const float dy = pos.y - py;
            const float distSq = dx * dx + dy * dy;
            if (distSq > maxDistSq) {
                return;
            }
            result.push_back(GroundItemInfo{e, item, distSq});
        });
    std::sort(result.begin(), result.end(),
              [&world](const GroundItemInfo& a, const GroundItemInfo& b) {
                  if (a.distanceSq != b.distanceSq) {
                      return a.distanceSq < b.distanceSq;
                  }
                  return world.IdOf(a.entity).id < world.IdOf(b.entity).id;
              });
    return result;
}

PickupResult PickupGroundItem(ecs::EcsWorld& world, const items::ItemRegistry& registry,
                              ecs::Container& container, ecs::Entity target) {
    PickupResult result;
    if (!world.Alive(target)) {
        return result;
    }
    auto* instance = world.Registry().try_get<ecs::ItemInstance>(target);
    if (instance == nullptr) {
        return result;
    }
    result.found = true;
    result.entityId = world.IdOf(target).id;
    result.defId = instance->defId;
    result.groundRemaining = instance->count;

    const int added = items::AddItem(container, registry, *instance);
    if (added <= 0) {
        return result;
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

PickupResult PickupNearestItem(ecs::EcsWorld& world, const items::ItemRegistry& registry,
                               ecs::Container& container, float px, float py, int z,
                               float maxRadius) {
    const ecs::Entity target = NearestGroundItem(world, px, py, z, maxRadius, nullptr);
    if (target == entt::null) {
        return PickupResult{};
    }
    return PickupGroundItem(world, registry, container, target);
}

}  // namespace zl::game
