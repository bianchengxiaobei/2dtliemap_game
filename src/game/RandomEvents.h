#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"

namespace zl::items {
class LootTableRegistry;
}

namespace zl::game {

// M24-T4：四条新随机事件 + 原空投的世界内容。事件任务仍走 missions-v1，
// 本模块只负责事件站点、临时敌人/容器与 StableId 去重。
enum class RandomEventKind : std::uint8_t {
    None = 0,
    SupplyDrop = 1,
    WanderingHorde = 2,
    InfectedHouse = 3,
    SurvivorAmbush = 4,
    ColdSnap = 5,
};

struct RandomEventSpawnCallbacks {
    std::function<bool(int, int)> walkable;
    std::function<ecs::Entity(float, float, ecs::ZombieVariantKind,
                              std::uint64_t)>
        spawnZombie;
};

struct RandomEventSpawnResult {
    bool content = false;
    float siteX = 0.0f;
    float siteY = 0.0f;
    int spawned = 0;
    int total = 0;
};

RandomEventKind RandomEventKindForMission(std::string_view missionId);
const char* RandomEventKindName(RandomEventKind kind);
bool IsRandomEventMission(std::string_view missionId);
const char* RandomEventLootName(std::string_view tableId);

// 固定/按天变化的事件站点。ambush 以玩家位置为锚点，其余忽略锚点。
void RandomEventSiteFor(RandomEventKind kind, int day, float anchorX,
                        float anchorY, float* x, float* y);
bool RandomEventFallbackSite(std::string_view missionId, int day, float anchorX,
                             float anchorY, float* x, float* y);

std::uint64_t RandomEventStableId(std::string_view missionId, int day,
                                  int index);

// progress > 0 时不再生成，只返回站点，兼容旧任务进度。
// 返回 true 表示所需内容已全部存在/生成；false 表示当前地图格不可用等，调用方可重试。
bool EnsureRandomEventContent(ecs::EcsWorld& world,
                              const items::LootTableRegistry& lootTables,
                              std::string_view missionId, int day, int progress,
                              float anchorX, float anchorY,
                              const RandomEventSpawnCallbacks& callbacks,
                              RandomEventSpawnResult* out);

}  // namespace zl::game
