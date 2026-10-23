#include "characters/systems/ActionPresentationSystem.h"

#include <entt/entt.hpp>

#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/systems/AnimationSystem.h"
#include "ecs/EcsWorld.h"

namespace zl::characters {

void ActionPresentationSystem::Update(ecs::EcsWorld& world, float dt) const {
    entt::registry& registry = world.Registry();
    registry.view<ecs::ActionState>().each(
        [&](const entt::entity entity, ecs::ActionState& action) {
            const auto* health = registry.try_get<ecs::Health>(entity);
            if ((health != nullptr && health->current <= 0.0f) ||
                registry.all_of<ecs::Dying>(entity)) {
                // M14-T3：死亡片段需要自己的计时；不要每 tick 把 Dead 归零。
                if (action.action != ecs::ActorAction::Dead) {
                    action.action = ecs::ActorAction::Dead;
                    action.elapsed = 0.0f;
                    action.duration = 0.0f;
                }
                return;
            }

            if (const auto* job = registry.try_get<ecs::CraftingJob>(entity);
                job != nullptr && job->active) {
                action.action = ecs::ActorAction::Crafting;
                action.elapsed = job->elapsed;
                action.duration = job->duration;
                return;
            }

            if ((action.action == ecs::ActorAction::Attacking ||
                 action.action == ecs::ActorAction::Hit) &&
                action.duration > 0.0f) {
                action.elapsed += dt;
                if (action.elapsed < action.duration) {
                    return;
                }
            }

            const auto* velocity = registry.try_get<ecs::Velocity>(entity);
            const float speed2 =
                velocity != nullptr
                    ? velocity->x * velocity->x + velocity->y * velocity->y
                    : 0.0f;
            action.action =
                speed2 >= AnimationSystem::kMoveThreshold *
                               AnimationSystem::kMoveThreshold
                    ? ecs::ActorAction::Moving
                    : ecs::ActorAction::Idle;
            action.elapsed = 0.0f;
            action.duration = 0.0f;
        });
}

}  // namespace zl::characters
