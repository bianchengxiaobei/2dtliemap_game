#include "characters/systems/CombatSystem.h"

#include <algorithm>
#include <cmath>

#include <entt/entt.hpp>

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "core/Log.h"
#include "ecs/components/CommonComponents.h"

namespace zl::characters {
namespace {

constexpr const char* kLogModule = "characters";

constexpr float kDegToRad = 0.017453292f;

// 击退速度衰减（每秒比例）：v *= max(0, 1 - dt × k)。k=6 → 半衰约 0.12s，
// 3 格/秒的击退总位移 ≈ v0/k = 0.5 格（半步，贴身推挤手感）。
constexpr float kKnockbackDampingPerSecond = 6.0f;

// 受击闪红：时长 + tint（0xRRGGBBAA；渲染侧 SpriteRef.tint 直接乘到精灵色）
constexpr float kFlashSeconds = 0.15f;
constexpr std::uint32_t kHitFlashTint = 0xFF6060FFu;
constexpr std::uint32_t kTintRestore = 0xFFFFFFFFu;

}  // namespace

bool CombatSystem::ApplyDamage(ecs::EcsWorld& world, ecs::Entity target,
                               const ecs::DamageEvent& event) {
    if (!world.Alive(target)) {
        return false;
    }
    entt::registry& registry = world.Registry();
    auto* health = registry.try_get<ecs::Health>(target);
    if (health == nullptr || health->current <= 0.0f) {
        return false;  // 无生命 / 已倒地：不重复结算
    }

    // 1) 扣血（Health 只认数值；来源/类型在事件里，管线侧解耦）
    health->current -= event.amount;
    m_stats.damageDealt += event.amount;

    // 2) 受击反应：硬直取较大值；击退以本次方向为准；闪红刷新
    ecs::HitReaction& hit = registry.get_or_emplace<ecs::HitReaction>(target);
    hit.staggerLeft = std::max(hit.staggerLeft, event.staggerSeconds);
    const float dirLen =
        std::sqrt(event.dirX * event.dirX + event.dirY * event.dirY);
    if (event.knockback > 0.0f && dirLen > 0.0001f) {
        hit.vx = event.dirX / dirLen * event.knockback;
        hit.vy = event.dirY / dirLen * event.knockback;
        // 击退本 tick 立即生效（Combat 在 Movement 之前；硬直窗口的后续 tick 由 Update 接管）
        if (auto* intent = registry.try_get<ecs::MoveIntent>(target); intent != nullptr) {
            intent->x = hit.vx;
            intent->y = hit.vy;
        }
    }
    hit.flashLeft = std::max(hit.flashLeft, kFlashSeconds);
    if (auto* sprite = registry.try_get<ecs::SpriteRef>(target); sprite != nullptr) {
        sprite->tint = kHitFlashTint;
    }

    // 3) 致死：HP 归零 → DeathSystem（倒地窗口 → 尸体）；此处只记统计与日志
    const bool lethal = health->current <= 0.0f;
    if (lethal) {
        ++m_stats.kills;
        ZL_INFO(kLogModule, "击杀：目标稳定 ID {}（来源 ID {}，{} 点 {} 伤害，剩 {:.1f} HP）→ 倒地",
                world.IdOf(target).id, event.sourceId, event.amount,
                static_cast<int>(event.type), health->current);
    } else if (registry.all_of<ecs::PlayerTag>(target)) {
        // 玩家被抓：INFO（冒烟关键信号）
        ZL_INFO(kLogModule, "玩家受到伤害：{} 点（来源 ID {}，{} 类型），剩 {:.1f} HP",
                event.amount, event.sourceId, static_cast<int>(event.type), health->current);
    } else {
        ZL_DEBUG(kLogModule, "伤害：来源 {} → 目标 {}（{} 点），剩 {:.1f} HP",
                 event.sourceId, world.IdOf(target).id, event.amount, health->current);
    }
    return true;
}

void CombatSystem::Update(ecs::EcsWorld& world, float dt) {
    entt::registry& registry = world.Registry();
    m_stats.staggered = 0;  // 每帧快照（其余统计跨帧累计）

    // 1) 受击反应衰减：硬直中 → MoveIntent 被击退速度接管（覆盖本 tick 已写意图）
    const float decay = std::max(0.0f, 1.0f - dt * kKnockbackDampingPerSecond);
    registry.view<ecs::HitReaction>().each([&](const entt::entity e, ecs::HitReaction& hit) {
        if (hit.flashLeft > 0.0f) {
            hit.flashLeft = std::max(0.0f, hit.flashLeft - dt);
            if (hit.flashLeft <= 0.0f) {
                if (auto* sprite = registry.try_get<ecs::SpriteRef>(e); sprite != nullptr) {
                    sprite->tint = kTintRestore;  // 闪红结束恢复
                }
            }
        }
        if (hit.staggerLeft <= 0.0f) {
            return;
        }
        ++m_stats.staggered;
        if (auto* intent = registry.try_get<ecs::MoveIntent>(e); intent != nullptr) {
            intent->x = hit.vx;
            intent->y = hit.vy;
        }
        hit.staggerLeft = std::max(0.0f, hit.staggerLeft - dt);
        hit.vx *= decay;
        hit.vy *= decay;
    });

    // 2) 命中候选：本 tick 一次收集（可受伤 = 有 Health 且 HP>0），
    //    避免在每挥击循环里构造 view（M5-T5 性能教训）
    m_candidates.clear();
    registry.view<ecs::Health, ecs::Position>().each(
        [&](const entt::entity e, const ecs::Health& health, const ecs::Position& position) {
            if (health.current > 0.0f && world.Alive(e)) {
                m_candidates.push_back(
                    Candidate{e, position.x, position.y,
                              registry.try_get<ecs::AiState>(e) != nullptr});
            }
        });

    // 3) 消费攻击意图：扇形判定 → ApplyDamage（可多目标）
    registry.view<ecs::AttackIntent, ecs::Position>().each(
        [&](const entt::entity attacker, ecs::AttackIntent& attack,
            const ecs::Position& attackerPos) {
            if (!attack.pending) {
                return;  // continue
            }
            attack.pending = false;  // 只消费一次
            ++m_stats.swings;

            const float dirLen =
                std::sqrt(attack.dirX * attack.dirX + attack.dirY * attack.dirY);
            if (dirLen < 0.0001f) {
                return;  // 无方向：空挥
            }
            const float dirX = attack.dirX / dirLen;
            const float dirY = attack.dirY / dirLen;
            const float halfCos = std::cos(attack.arcDegrees * 0.5f * kDegToRad);
            const float rangeSq = attack.range * attack.range;
            // M5-T6 阵营规则：AI 攻击者不误伤同类（僵尸 vs 僵尸豁免；阵营系统 M7+ 再抽象）
            const bool attackerIsAi = registry.try_get<ecs::AiState>(attacker) != nullptr;

            for (const Candidate& candidate : m_candidates) {
                if (candidate.e == attacker || (attackerIsAi && candidate.ai)) {
                    continue;
                }
                const float ox = candidate.x - attackerPos.x;
                const float oy = candidate.y - attackerPos.y;
                const float distSq = ox * ox + oy * oy;
                if (distSq > rangeSq) {
                    continue;  // 距离外
                }
                const float dist = std::sqrt(distSq);
                if (dist > 0.0001f && (ox * dirX + oy * dirY) / dist < halfCos) {
                    continue;  // 扇形外
                }
                ecs::DamageEvent event;
                event.sourceId = world.IdOf(attacker).id;
                event.type = attack.type;
                event.amount = attack.damage;
                event.dirX = dirX;
                event.dirY = dirY;
                event.knockback = attack.knockback;
                event.staggerSeconds = attack.staggerSeconds;
                if (ApplyDamage(world, candidate.e, event)) {
                    ++m_stats.hits;
                }
            }
        });
}

}  // namespace zl::characters
