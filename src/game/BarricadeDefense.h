#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "ecs/EntityHash.h"
#include "ecs/components/CommonComponents.h"
#include "game/BuildingRules.h"
#include "game/HomesteadComponents.h"

namespace zl::game {

struct BarricadeAttack {
    std::uint64_t key = 0;
    int wx = 0;
    int wy = 0;
    float remainingHp = 0.0f;
    bool destroyed = false;
    bool warningStarted = false;  // M20-F3：破门者开始可打断的蓄力
};

inline bool IsZombieAggroForBarricade(ecs::AiStatus status) {
    return status == ecs::AiStatus::Chase || status == ecs::AiStatus::Attack ||
           status == ecs::AiStatus::SearchLastKnown;
}

// 返回本 tick 的栅栏受击结果；无目标/冷却未好/状态不满足返回 nullopt。
// 只改 HP、冷却和攻击动作；实体移除/碰撞重建由调用方处理。
inline std::optional<BarricadeAttack> TickZombieBarricadeAttack(
    entt::registry& registry, ecs::Entity entity, const ecs::AiState& ai,
    const ecs::Health& health, const ecs::Position& pos, float playerX,
    float playerY, std::unordered_map<std::uint64_t, BuiltBarricade>& barricades,
    std::unordered_map<std::uint32_t, float>& cooldowns, float damage,
    float cooldownSeconds, float attackRange = 1.5f,
    float playerRange = 10.0f, float breakerRange = 2.2f,
    float breakerDamage = 28.0f, float breakerCooldownSeconds = 1.2f,
    float breakerWindupSeconds = 0.9f) {
    if (health.current <= 0.0f || ai.status == ecs::AiStatus::Dead ||
        !IsZombieAggroForBarricade(ai.status)) {
        return std::nullopt;
    }
    const auto* hit = registry.try_get<ecs::HitReaction>(entity);
    if (hit != nullptr && hit->staggerLeft > 0.0f) {
        return std::nullopt;
    }

    auto* special = registry.try_get<ecs::ZombieSpecial>(entity);
    const bool breaker =
        special != nullptr &&
        special->kind == ecs::ZombieSpecialKind::Breaker;
    if (breaker && special->windup > 0.0f) {
        return std::nullopt;  // 蓄力由 ZombieSpecialSystem 递减
    }
    const float effectiveRange = breaker ? breakerRange : attackRange;
    std::uint64_t targetKey = 0;
    bool found = false;
    float bestDistance = effectiveRange * effectiveRange;
    const float playerRangeSq = playerRange * playerRange;
    for (const auto& [key, barricade] : barricades) {
        (void)barricade;
        const int wx = BuildCellX(key);
        const int wy = BuildCellY(key);
        const float px = static_cast<float>(wx) + 0.5f;
        const float py = static_cast<float>(wy) + 0.5f;
        const float playerDx = playerX - px;
        const float playerDy = playerY - py;
        if (playerDx * playerDx + playerDy * playerDy > playerRangeSq) {
            continue;
        }
        const float dx = pos.x - px;
        const float dy = pos.y - py;
        const float distance = dx * dx + dy * dy;
        if (distance <= bestDistance) {
            bestDistance = distance;
            targetKey = key;
            found = true;
        }
    }
    if (!found) {
        if (breaker && special != nullptr && special->windup <= 0.0f) {
            special->windupDuration = 0.0f;
        }
        return std::nullopt;
    }

    const std::uint32_t entityId =
        static_cast<std::uint32_t>(entt::to_integral(entity));
    float& cooldown = cooldowns[entityId];
    if (cooldown > 0.0f) {
        return std::nullopt;
    }

    auto target = barricades.find(targetKey);
    if (target == barricades.end()) {
        return std::nullopt;
    }

    BarricadeAttack result;
    result.key = targetKey;
    result.wx = BuildCellX(targetKey);
    result.wy = BuildCellY(targetKey);
    result.remainingHp = target->second.hp;

    if (breaker && breakerWindupSeconds > 0.0f &&
        special->windupDuration <= 0.0f) {
        special->windup = breakerWindupSeconds;
        special->windupDuration = breakerWindupSeconds;
        auto& action = registry.get_or_emplace<ecs::ActionState>(entity);
        action.action = ecs::ActorAction::Attacking;
        action.elapsed = 0.0f;
        action.duration = breakerWindupSeconds;
        result.warningStarted = true;
        return result;
    }

    cooldown = breaker ? breakerCooldownSeconds : cooldownSeconds;
    target->second.hp -= breaker ? breakerDamage : damage;
    if (breaker) {
        special->windupDuration = 0.0f;
    }
    auto& action = registry.get_or_emplace<ecs::ActionState>(entity);
    action.action = ecs::ActorAction::Attacking;
    action.elapsed = 0.0f;
    action.duration = 0.35f;

    result.remainingHp = target->second.hp;
    result.destroyed = target->second.hp <= 0.0f;
    return result;
}

struct FloodlightAttack {
    ecs::Entity zombie = entt::null;
    ecs::Entity light = entt::null;
    std::uint64_t zombieStableId = 0;
    float previousHp = 0.0f;
    float remainingHp = 0.0f;
    bool destroyed = false;
};

inline std::vector<FloodlightAttack> TickZombieFloodlightAttacks(
    entt::registry& registry,
    std::unordered_map<std::uint64_t, float>& cooldowns, float dt,
    float damage, float cooldownSeconds, float breakerDamage,
    float breakerCooldownSeconds, float attackRange = 1.4f,
    float breakerRange = 2.1f) {
    for (auto it = cooldowns.begin(); it != cooldowns.end();) {
        it->second = std::max(0.0f, it->second - std::max(0.0f, dt));
        if (it->second <= 0.0f) {
            it = cooldowns.erase(it);
        } else {
            ++it;
        }
    }

    std::vector<FloodlightAttack> attacks;
    registry.view<ecs::Floodlight, ecs::Position, ecs::Health>().each(
        [&](const ecs::Entity light, const ecs::Floodlight&,
            const ecs::Position& lightPos, ecs::Health& lightHealth) {
            if (lightHealth.current <= 0.0f) {
                return;
            }
            ecs::Entity best = entt::null;
            float bestDistance = 0.0f;
            bool bestBreaker = false;
            registry.view<ecs::BaseSiegeZombie, ecs::AiState, ecs::Health,
                          ecs::Position>()
                .each([&](const ecs::Entity zombie, const ecs::AiState& ai,
                          const ecs::Health& zombieHealth,
                          const ecs::Position& zombiePos) {
                    if (zombieHealth.current <= 0.0f ||
                        ai.status == ecs::AiStatus::Dead ||
                        !IsZombieAggroForBarricade(ai.status)) {
                        return;
                    }
                    if (const auto* hit =
                            registry.try_get<ecs::HitReaction>(zombie);
                        hit != nullptr && hit->staggerLeft > 0.0f) {
                        return;
                    }
                    const auto* special =
                        registry.try_get<ecs::ZombieSpecial>(zombie);
                    const bool breaker =
                        special != nullptr &&
                        special->kind == ecs::ZombieSpecialKind::Breaker;
                    if (breaker && special->windup > 0.0f) {
                        return;
                    }
                    const auto* stable =
                        registry.try_get<ecs::StableId>(zombie);
                    const std::uint64_t zombieId =
                        stable != nullptr && stable->id != 0
                            ? stable->id
                            : static_cast<std::uint64_t>(
                                  entt::to_integral(zombie));
                    if (cooldowns.contains(zombieId)) {
                        return;
                    }
                    if (zombiePos.z != lightPos.z) {
                        return;
                    }
                    const float range = breaker ? breakerRange : attackRange;
                    const float dx = zombiePos.x - lightPos.x;
                    const float dy = zombiePos.y - lightPos.y;
                    const float distance = dx * dx + dy * dy;
                    if (distance > range * range) {
                        return;
                    }
                    if (best == entt::null || distance < bestDistance) {
                        best = zombie;
                        bestDistance = distance;
                        bestBreaker = breaker;
                    }
                });
            if (best == entt::null) {
                return;
            }

            const auto* stable = registry.try_get<ecs::StableId>(best);
            const std::uint64_t zombieId =
                stable != nullptr && stable->id != 0
                    ? stable->id
                    : static_cast<std::uint64_t>(entt::to_integral(best));
            if (cooldowns.contains(zombieId)) {
                return;
            }
            const float amount = bestBreaker ? breakerDamage : damage;
            if (amount <= 0.0f) {
                return;
            }

            FloodlightAttack attack;
            attack.zombie = best;
            attack.light = light;
            attack.zombieStableId = zombieId;
            attack.previousHp = lightHealth.current;
            lightHealth.current = std::max(0.0f, lightHealth.current - amount);
            attack.remainingHp = lightHealth.current;
            attack.destroyed = lightHealth.current <= 0.0f;
            cooldowns[zombieId] =
                std::max(0.0f, bestBreaker ? breakerCooldownSeconds
                                           : cooldownSeconds);

            auto& action = registry.get_or_emplace<ecs::ActionState>(best);
            action.action = ecs::ActorAction::Attacking;
            action.elapsed = 0.0f;
            action.duration = 0.35f;
            attacks.push_back(attack);
        });
    return attacks;
}

}  // namespace zl::game
