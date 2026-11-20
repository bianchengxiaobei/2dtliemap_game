#pragma once

#include <string_view>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"
#include "game/PoiComponents.h"

namespace zl::game {

inline int CountPoiStageTargets(entt::registry& registry,
                                std::string_view poiId,
                                std::string_view stageId) {
    int count = 0;
    registry.view<ecs::PoiStageEnemy, ecs::Health>().each(
        [&](const ecs::Entity entity, const ecs::PoiStageEnemy& enemy,
            const ecs::Health& health) {
            if (enemy.poiId != poiId || enemy.stageId != stageId ||
                enemy.role == PoiStageEnemyRole::Summon ||
                health.current <= 0.0f) {
                return;
            }
            if (registry.valid(entity)) {
                ++count;
            }
        });
    return count;
}

inline int CountPoiStageSummons(entt::registry& registry,
                                std::string_view poiId,
                                std::string_view stageId) {
    int count = 0;
    registry.view<ecs::PoiStageEnemy, ecs::Health>().each(
        [&](const ecs::Entity entity, const ecs::PoiStageEnemy& enemy,
            const ecs::Health& health) {
            if (enemy.poiId != poiId || enemy.stageId != stageId ||
                enemy.role != PoiStageEnemyRole::Summon ||
                health.current <= 0.0f) {
                return;
            }
            if (registry.valid(entity)) {
                ++count;
            }
        });
    return count;
}

inline bool HasPoiStageEnemyRecords(entt::registry& registry,
                                    std::string_view poiId,
                                    std::string_view stageId) {
    bool found = false;
    registry.view<ecs::PoiStageEnemy>().each(
        [&](const ecs::PoiStageEnemy& enemy) {
            if (enemy.poiId == poiId && enemy.stageId == stageId) {
                found = true;
            }
        });
    return found;
}

inline std::vector<ecs::Entity> CollectPoiStageEnemies(
    entt::registry& registry, std::string_view poiId,
    std::string_view stageId) {
    std::vector<ecs::Entity> out;
    registry.view<ecs::PoiStageEnemy>().each(
        [&](const ecs::Entity entity, const ecs::PoiStageEnemy& enemy) {
            if (enemy.poiId == poiId &&
                (stageId.empty() || enemy.stageId == stageId) &&
                registry.valid(entity)) {
                out.push_back(entity);
            }
        });
    return out;
}

}  // namespace zl::game
