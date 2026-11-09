#include "game/RandomEvents.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "ai/Passability.h"
#include "core/Log.h"
#include "ecs/components/CommonComponents.h"
#include "game/LootContainers.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "events";
constexpr std::uint64_t kEventStableIdBase = 0x4D32000000000000ull;
constexpr float kSupplyX = 62.5f;
constexpr float kSupplyY = 29.5f;
constexpr float kSupplyAltX = 77.5f;
constexpr float kSupplyAltY = 29.5f;
constexpr float kInfectedX = 66.5f;
constexpr float kInfectedY = 19.5f;
constexpr float kVillageX = 70.0f;
constexpr float kVillageY = 24.0f;

bool PlaceWalkable(const RandomEventSpawnCallbacks& callbacks, float px,
                   float py, int radius, float* x, float* y) {
    if (!callbacks.walkable || x == nullptr || y == nullptr ||
        !std::isfinite(px) || !std::isfinite(py)) {
        return false;
    }
    int wx = 0;
    int wy = 0;
    if (!ai::FindNearestWalkable(static_cast<int>(std::floor(px)),
                                 static_cast<int>(std::floor(py)), radius,
                                 callbacks.walkable, &wx, &wy)) {
        return false;
    }
    *x = static_cast<float>(wx) + 0.5f;
    *y = static_cast<float>(wy) + 0.5f;
    return true;
}

bool EnsureContainer(ecs::EcsWorld& world,
                     const items::LootTableRegistry& lootTables,
                     std::string_view missionId, int day, int index,
                     std::string_view tableId, float preferredX,
                     float preferredY,
                     const RandomEventSpawnCallbacks& callbacks,
                     RandomEventSpawnResult* out) {
    const std::uint64_t stableId =
        RandomEventStableId(missionId, day, index);
    if (world.FindById(stableId) != entt::null) {
        ++out->total;
        return true;
    }
    float x = 0.0f;
    float y = 0.0f;
    if (!PlaceWalkable(callbacks, preferredX, preferredY, 4, &x, &y)) {
        return false;
    }
    if (SpawnLootContainer(world, lootTables, tableId, x, y, 0,
                           "object.crate", stableId) == entt::null) {
        return false;
    }
    ++out->spawned;
    ++out->total;
    return true;
}

bool EnsureActor(ecs::EcsWorld& world, std::string_view missionId, int day,
                 int index, float preferredX, float preferredY,
                 ecs::ZombieVariantKind variant,
                 const RandomEventSpawnCallbacks& callbacks,
                 RandomEventSpawnResult* out) {
    const std::uint64_t stableId =
        RandomEventStableId(missionId, day, index);
    const ecs::Entity existing = world.FindById(stableId);
    if (existing != entt::null) {
        world.Registry().emplace_or_replace<ecs::RandomEventActor>(
            existing,
            ecs::RandomEventActor{std::string(missionId), index});
        ++out->total;
        return true;
    }
    if (!callbacks.spawnZombie) {
        return false;
    }
    float x = 0.0f;
    float y = 0.0f;
    if (!PlaceWalkable(callbacks, preferredX, preferredY, 4, &x, &y)) {
        return false;
    }
    const ecs::Entity actor =
        callbacks.spawnZombie(x, y, variant, stableId);
    if (actor == entt::null) {
        return false;
    }
    world.Registry().emplace_or_replace<ecs::RandomEventActor>(
        actor, ecs::RandomEventActor{std::string(missionId), index});
    ++out->spawned;
    ++out->total;
    return true;
}

std::uint64_t HashEventStableId(std::string_view missionId, int day,
                                int index) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const char c : missionId) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 1099511628211ull;
    }
    hash ^= static_cast<std::uint64_t>(std::max(0, day)) * 0x9E3779B97F4A7C15ull;
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(std::max(0, index)) * 0xC2B2AE3D27D4EB4Full;
    hash *= 1099511628211ull;
    return kEventStableIdBase | (hash & 0x0000FFFFFFFFFFFFull);
}

}  // namespace

RandomEventKind RandomEventKindForMission(std::string_view missionId) {
    if (missionId == "event_supply_drop") {
        return RandomEventKind::SupplyDrop;
    }
    if (missionId == "event_wandering_horde") {
        return RandomEventKind::WanderingHorde;
    }
    if (missionId == "event_infected_house") {
        return RandomEventKind::InfectedHouse;
    }
    if (missionId == "event_survivor_ambush") {
        return RandomEventKind::SurvivorAmbush;
    }
    if (missionId == "event_cold_snap") {
        return RandomEventKind::ColdSnap;
    }
    return RandomEventKind::None;
}

const char* RandomEventKindName(RandomEventKind kind) {
    switch (kind) {
        case RandomEventKind::SupplyDrop:
            return "空投回收";
        case RandomEventKind::WanderingHorde:
            return "流浪尸潮";
        case RandomEventKind::InfectedHouse:
            return "被感染房屋";
        case RandomEventKind::SurvivorAmbush:
            return "幸存者伏击";
        case RandomEventKind::ColdSnap:
            return "寒潮突变";
        case RandomEventKind::None:
        default:
            return "无";
    }
}

bool IsRandomEventMission(std::string_view missionId) {
    return RandomEventKindForMission(missionId) != RandomEventKind::None;
}

const char* RandomEventLootName(std::string_view tableId) {
    if (tableId == "event_supply_crate") {
        return "空投补给箱";
    }
    if (tableId == "event_infected_cache") {
        return "感染者藏匿箱";
    }
    return nullptr;
}

void RandomEventSiteFor(RandomEventKind kind, int day, float anchorX,
                        float anchorY, float* x, float* y) {
    if (x == nullptr || y == nullptr) {
        return;
    }
    switch (kind) {
        case RandomEventKind::SupplyDrop:
            *x = kSupplyX;
            *y = kSupplyY;
            return;
        case RandomEventKind::WanderingHorde: {
            static constexpr float kHordeSites[3][2] = {
                {70.5f, 33.5f}, {60.5f, 20.5f}, {80.5f, 28.5f}};
            const int index = std::max(0, day) % 3;
            *x = kHordeSites[index][0];
            *y = kHordeSites[index][1];
            return;
        }
        case RandomEventKind::InfectedHouse:
            *x = kInfectedX;
            *y = kInfectedY;
            return;
        case RandomEventKind::SurvivorAmbush:
            *x = anchorX + 3.0f;
            *y = anchorY;
            return;
        case RandomEventKind::ColdSnap:
            *x = kVillageX;
            *y = kVillageY;
            return;
        case RandomEventKind::None:
        default:
            *x = anchorX;
            *y = anchorY;
            return;
    }
}

bool RandomEventFallbackSite(std::string_view missionId, int day, float anchorX,
                             float anchorY, float* x, float* y) {
    const RandomEventKind kind = RandomEventKindForMission(missionId);
    if (kind == RandomEventKind::None) {
        return false;
    }
    RandomEventSiteFor(kind, day, anchorX, anchorY, x, y);
    return true;
}

std::uint64_t RandomEventStableId(std::string_view missionId, int day,
                                  int index) {
    return HashEventStableId(missionId, day, index);
}

bool EnsureRandomEventContent(ecs::EcsWorld& world,
                              const items::LootTableRegistry& lootTables,
                              std::string_view missionId, int day, int progress,
                              float anchorX, float anchorY,
                              const RandomEventSpawnCallbacks& callbacks,
                              RandomEventSpawnResult* out) {
    if (out == nullptr) {
        return false;
    }
    *out = {};
    const RandomEventKind kind = RandomEventKindForMission(missionId);
    if (kind == RandomEventKind::None) {
        return false;
    }
    RandomEventSiteFor(kind, day, anchorX, anchorY, &out->siteX, &out->siteY);
    if (kind == RandomEventKind::ColdSnap) {
        out->content = false;
        return true;
    }
    out->content = true;
    if (progress > 0) {
        return true;
    }

    bool ok = true;
    switch (kind) {
        case RandomEventKind::SupplyDrop:
            ok = EnsureContainer(world, lootTables, missionId, day, 1,
                                 "event_supply_crate", kSupplyX, kSupplyY,
                                 callbacks, out) &&
                 EnsureContainer(world, lootTables, missionId, day, 2,
                                 "event_supply_crate", kSupplyAltX, kSupplyAltY,
                                 callbacks, out);
            break;
        case RandomEventKind::WanderingHorde: {
            constexpr ecs::ZombieVariantKind kHordeVariants[8] = {
                ecs::ZombieVariantKind::Normal, ecs::ZombieVariantKind::Normal,
                ecs::ZombieVariantKind::Fast,   ecs::ZombieVariantKind::Normal,
                ecs::ZombieVariantKind::Normal, ecs::ZombieVariantKind::Fast,
                ecs::ZombieVariantKind::Normal, ecs::ZombieVariantKind::Normal};
            for (int i = 0; i < 8; ++i) {
                const float angle =
                    static_cast<float>(i) * 0.7853982f;
                const float radius = 2.0f + static_cast<float>(i % 3) * 0.65f;
                ok = EnsureActor(world, missionId, day, i + 1,
                                 out->siteX + std::cos(angle) * radius,
                                 out->siteY + std::sin(angle) * radius,
                                 kHordeVariants[i], callbacks, out) &&
                     ok;
            }
            break;
        }
        case RandomEventKind::InfectedHouse:
            ok = EnsureContainer(world, lootTables, missionId, day, 1,
                                 "event_infected_cache", kInfectedX + 1.0f,
                                 kInfectedY - 1.0f, callbacks, out);
            for (int i = 0; i < 5; ++i) {
                static constexpr float kOffsets[5][2] = {
                    {-1.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 0.0f},
                    {-1.0f, 1.0f}, {1.0f, 1.0f}};
                ok = EnsureActor(world, missionId, day, i + 11,
                                 kInfectedX + kOffsets[i][0],
                                 kInfectedY + kOffsets[i][1],
                                 ecs::ZombieVariantKind::Normal, callbacks,
                                 out) &&
                     ok;
            }
            break;
        case RandomEventKind::SurvivorAmbush:
            for (int i = 0; i < 2; ++i) {
                const float side = i == 0 ? -1.0f : 1.0f;
                ok = EnsureActor(world, missionId, day, i + 1,
                                 out->siteX + side * 1.2f,
                                 out->siteY + side * 0.8f,
                                 ecs::ZombieVariantKind::Strong, callbacks,
                                 out) &&
                     ok;
            }
            break;
        case RandomEventKind::ColdSnap:
        case RandomEventKind::None:
        default:
            break;
    }
    if (out->spawned > 0) {
        ZL_INFO(kLogModule,
                "随机事件世界内容：{}，新增 {}，现有 {}，站点 ({:.1f},{:.1f})",
                RandomEventKindName(kind), out->spawned, out->total,
                static_cast<double>(out->siteX),
                static_cast<double>(out->siteY));
    }
    return ok;
}

}  // namespace zl::game
