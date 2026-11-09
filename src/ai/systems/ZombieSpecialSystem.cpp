#include "ai/systems/ZombieSpecialSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "core/Log.h"
#include "ecs/components/CommonComponents.h"

namespace zl::ai {
namespace {

struct PlayerTarget {
    bool valid = false;
    std::uint64_t id = 0;
    float x = 0.0f;
    float y = 0.0f;
};

float DistSq(float ax, float ay, float bx, float by) {
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

PlayerTarget FindAlivePlayer(const entt::registry& registry,
                             const ecs::EcsWorld& world) {
    PlayerTarget target;
    registry.view<ecs::PlayerTag, ecs::Position>().each(
        [&](const ecs::Entity entity, const ecs::Position& position) {
            if (target.valid) {
                return;
            }
            const auto* health = registry.try_get<ecs::Health>(entity);
            if (health != nullptr && health->current <= 0.0f) {
                return;
            }
            target.valid = true;
            target.id = world.IdOf(entity).id;
            target.x = position.x;
            target.y = position.y;
        });
    return target;
}

}  // namespace

void ZombieSpecialSystem::CancelWindup(ecs::EcsWorld& world,
                                       ecs::Entity entity) {
    entt::registry& registry = world.Registry();
    if (auto* special = registry.try_get<ecs::ZombieSpecial>(entity);
        special != nullptr) {
        special->windup = 0.0f;
        special->windupDuration = 0.0f;
    }
    if (auto* action = registry.try_get<ecs::ActionState>(entity);
        action != nullptr &&
        (action->action == ecs::ActorAction::Howling ||
         action->action == ecs::ActorAction::Attacking)) {
        action->action = ecs::ActorAction::Idle;
        action->elapsed = 0.0f;
        action->duration = 0.0f;
    }
}

void ZombieSpecialSystem::StartHowl(ecs::EcsWorld& world, ecs::Entity entity,
                                    float x, float y, float playerX,
                                    float playerY) {
    (void)playerX;
    (void)playerY;
    entt::registry& registry = world.Registry();
    const float duration = std::max(0.0f, m_params.howlWindupSeconds);
    auto* special = registry.try_get<ecs::ZombieSpecial>(entity);
    if (special == nullptr) {
        return;
    }
    special->windup = duration;
    special->windupDuration = duration;
    auto& action = registry.get_or_emplace<ecs::ActionState>(entity);
    action.action = ecs::ActorAction::Howling;
    action.elapsed = 0.0f;
    action.duration = duration;
    ++m_stats.windups;
    ZL_INFO("special", "嚎叫者蓄力 at ({:.1f}, {:.1f})，{:.1f}s",
            x, y, duration);
    if (duration <= 0.0f) {
        FinishHowl(world, entity, x, y, 0, 0.0f, 0.0f);
    }
}

void ZombieSpecialSystem::FinishHowl(ecs::EcsWorld& world,
                                     ecs::Entity entity, float x, float y,
                                     std::uint64_t playerId, float playerX,
                                     float playerY) {
    entt::registry& registry = world.Registry();
    auto* special = registry.try_get<ecs::ZombieSpecial>(entity);
    if (special == nullptr) {
        return;
    }
    special->windup = 0.0f;
    special->windupDuration = 0.0f;
    special->cooldown = std::max(0.0f, m_params.howlCooldownSeconds);
    ++special->serial;
    ++m_stats.howls;
    ZL_INFO("special", "嚎叫者释放嚎叫 at ({:.1f}, {:.1f})，半径 {:.1f}",
            x, y, m_params.howlRadius);

    if (m_soundSink) {
        core::SoundEvent sound;
        sound.x = x;
        sound.y = y;
        sound.radius = std::max(0.0f, m_params.howlRadius);
        sound.type = core::SoundType::ZombieGrowl;
        m_soundSink(sound);
    }
    AttractNearby(world, entity, playerId, playerX, playerY, x, y);
}

void ZombieSpecialSystem::AttractNearby(ecs::EcsWorld& world,
                                        ecs::Entity source,
                                        std::uint64_t playerId, float playerX,
                                        float playerY, float x, float y) {
    if (playerId == 0) {
        return;
    }
    const float radius = std::max(0.0f, m_params.howlRadius);
    if (!(radius > 0.0f)) {
        return;
    }
    const float radiusSq = radius * radius;
    entt::registry& registry = world.Registry();
    registry.view<ecs::AiState, ecs::Position, ecs::PerceptionState>().each(
        [&](const ecs::Entity entity, ecs::AiState& ai,
            const ecs::Position& position, ecs::PerceptionState& perception) {
            if (entity == source) {
                return;
            }
            const auto* health = registry.try_get<ecs::Health>(entity);
            if (health != nullptr && health->current <= 0.0f) {
                return;
            }
            if (DistSq(x, y, position.x, position.y) > radiusSq) {
                return;
            }
            perception.lastKnownX = playerX;
            perception.lastKnownY = playerY;
            perception.alertness = 1.0f;
            perception.sinceHeard = 0.0f;
            if (ai.status != ecs::AiStatus::Dead &&
                (ai.status == ecs::AiStatus::Idle ||
                 ai.status == ecs::AiStatus::Wander ||
                 ai.status == ecs::AiStatus::SearchLastKnown)) {
                ai.status = ecs::AiStatus::Chase;
                ai.targetId = playerId;
                ai.stateTime = 0.0f;
                ai.actionTimer = 0.0f;
            }
            ++m_stats.attracted;
        });
}

void ZombieSpecialSystem::Update(ecs::EcsWorld& world, float dt) {
    m_stats = Stats{};
    dt = std::max(0.0f, dt);
    if (dt <= 0.0f) {
        return;
    }
    entt::registry& registry = world.Registry();
    const PlayerTarget player = FindAlivePlayer(registry, world);

    registry.view<ecs::ZombieSpecial, ecs::AiState, ecs::Position>().each(
        [&](const ecs::Entity entity, ecs::ZombieSpecial& special,
            ecs::AiState& ai, const ecs::Position& position) {
            if (special.kind == ecs::ZombieSpecialKind::None) {
                return;
            }
            if (special.cooldown > 0.0f) {
                special.cooldown = std::max(0.0f, special.cooldown - dt);
            }

            const auto* health = registry.try_get<ecs::Health>(entity);
            if ((health != nullptr && health->current <= 0.0f) ||
                ai.status == ecs::AiStatus::Dead) {
                if (special.windup > 0.0f) {
                    CancelWindup(world, entity);
                }
                return;
            }

            const auto* hit = registry.try_get<ecs::HitReaction>(entity);
            if (hit != nullptr && hit->staggerLeft > 0.0f) {
                if (special.windup > 0.0f) {
                    CancelWindup(world, entity);
                    ++m_stats.interrupted;
                } else if (special.kind == ecs::ZombieSpecialKind::Breaker) {
                    // 已蓄力完成但尚未落下的一击也算被硬直打断。
                    special.windupDuration = 0.0f;
                }
                return;
            }

            const float previousWindup =
                std::max(0.0f, special.windup - dt);
            if (special.windup > 0.0f) {
                special.windup = previousWindup;
            }

            if (special.kind == ecs::ZombieSpecialKind::Breaker) {
                return;
            }

            ++m_stats.howlers;
            const float distanceSq =
                player.valid
                    ? DistSq(position.x, position.y, player.x, player.y)
                    : std::numeric_limits<float>::max();
            const float abortRangeSq =
                std::max(0.0f, m_params.howlAbortRange) *
                std::max(0.0f, m_params.howlAbortRange);

            if (special.windupDuration > 0.0f || special.windup > 0.0f) {
                if (!player.valid || distanceSq > abortRangeSq) {
                    CancelWindup(world, entity);
                    return;
                }
                if (special.windup <= 0.0f) {
                    FinishHowl(world, entity, position.x, position.y,
                               player.id, player.x, player.y);
                }
                return;
            }

            if (special.cooldown > 0.0f || !player.valid) {
                return;
            }
            const float rangeSq =
                std::max(0.0f, m_params.howlRange) *
                std::max(0.0f, m_params.howlRange);
            if (distanceSq > rangeSq) {
                return;
            }
            StartHowl(world, entity, position.x, position.y, player.x,
                      player.y);
        });
}

}  // namespace zl::ai
