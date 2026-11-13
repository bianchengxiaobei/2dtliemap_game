#include "characters/systems/CombatSystem.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include <entt/entt.hpp>

#include "ai/components/AiComponents.h"
#include "characters/components/AnimalComponents.h"
#include "characters/components/CharacterComponents.h"
#include "core/Log.h"
#include "ecs/components/CommonComponents.h"

namespace zl::characters {
namespace {

constexpr const char* kLogModule = "characters";

constexpr float kDegToRad = 0.017453292f;
constexpr float kDirectionEpsilon = 0.0001f;

// 击退速度衰减（每秒比例）：v *= max(0, 1 - dt × k)。k=6 → 半衰约 0.12s，
// 3 格/秒的击退总位移 ≈ v0/k = 0.5 格（半步，贴身推挤手感）。
constexpr float kKnockbackDampingPerSecond = 6.0f;

// 受击闪红：时长 + tint（0xRRGGBBAA；渲染侧 SpriteRef.tint 直接乘到精灵色）
constexpr float kFlashSeconds = 0.15f;
constexpr float kAttackPresentationSeconds = 0.28f;
constexpr float kHitPresentationSeconds = 0.22f;
constexpr std::uint32_t kHitFlashTint = 0xFF6060FFu;
constexpr std::uint32_t kTintRestore = 0xFFFFFFFFu;

float FiniteNonNegative(float value) {
    return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
}

void FacingVector(ecs::Direction dir, float& x, float& y) {
    // Facing 是屏幕 4 向；Attack/DamageEvent 使用世界格方向，需做等距逆变换。
    constexpr float kIso = 0.70710678f;
    switch (dir) {
        case ecs::Direction::Down:
            x = kIso;
            y = kIso;
            break;
        case ecs::Direction::Left:
            x = -kIso;
            y = kIso;
            break;
        case ecs::Direction::Right:
            x = kIso;
            y = -kIso;
            break;
        case ecs::Direction::Up:
            x = -kIso;
            y = -kIso;
            break;
    }
}

bool IsBlockableType(ecs::DamageType type) {
    return type == ecs::DamageType::Claw || type == ecs::DamageType::Bite ||
           type == ecs::DamageType::Blunt;
}

// attackerDir 是攻击者 → 目标；目标面向必须指向攻击者，才算正面格挡。
bool IsFrontFacing(const ecs::Facing* facing, float attackerDirX,
                   float attackerDirY, float arcDegrees) {
    if (facing == nullptr || !std::isfinite(attackerDirX) ||
        !std::isfinite(attackerDirY)) {
        return false;
    }
    float facingX = 0.0f;
    float facingY = 0.0f;
    FacingVector(facing->dir, facingX, facingY);
    const float toAttackerX = -attackerDirX;
    const float toAttackerY = -attackerDirY;
    const float len = std::sqrt(toAttackerX * toAttackerX +
                                toAttackerY * toAttackerY);
    if (len < kDirectionEpsilon) {
        return false;
    }
    const float dot =
        (facingX * toAttackerX + facingY * toAttackerY) / len;
    const float halfArc =
        std::clamp(arcDegrees, 0.0f, 360.0f) * 0.5f * kDegToRad;
    return dot >= std::cos(halfArc) - 0.0001f;
}

// 弹反/破防造成的无伤害硬直：不写伤口/感染，只冻结动作并取消攻击前摇。
void ApplyTimedStagger(ecs::EcsWorld& world, ecs::Entity entity,
                       float seconds) {
    if (!world.Alive(entity) || !std::isfinite(seconds) || seconds <= 0.0f) {
        return;
    }
    entt::registry& registry = world.Registry();
    if (const auto* health = registry.try_get<ecs::Health>(entity);
        health != nullptr && health->current <= 0.0f) {
        return;
    }
    ecs::HitReaction& hit = registry.get_or_emplace<ecs::HitReaction>(entity);
    hit.staggerLeft =
        std::max(FiniteNonNegative(hit.staggerLeft), seconds);
    ecs::ActionState& action =
        registry.get_or_emplace<ecs::ActionState>(entity);
    action.action = ecs::ActorAction::Hit;
    action.elapsed = 0.0f;
    action.duration = std::max(FiniteNonNegative(action.duration), seconds);
    if (auto* attack = registry.try_get<ecs::AttackIntent>(entity);
        attack != nullptr && attack->pending) {
        attack->pending = false;
        attack->windupLeft = 0.0f;
    }
}

}  // namespace

bool CombatSystem::TrySpendStamina(ecs::EcsWorld& world, ecs::Entity entity,
                                   float amount) {
    if (!world.Alive(entity) || !std::isfinite(amount) || amount < 0.0f) {
        return false;
    }
    if (amount <= 0.0f) {
        return true;
    }
    entt::registry& registry = world.Registry();
    auto* stamina = registry.try_get<ecs::CombatStamina>(entity);
    if (stamina == nullptr) {
        return false;
    }
    float current =
        std::isfinite(stamina->current) ? std::max(0.0f, stamina->current) : 0.0f;
    const float exhausted = std::isfinite(stamina->exhausted)
                                ? std::max(0.0f, stamina->exhausted)
                                : 0.0f;
    if (exhausted > 0.0f || current < amount) {
        stamina->current = current;
        stamina->exhausted = exhausted;
        return false;
    }
    current -= amount;
    stamina->current = current;
    stamina->regenDelay =
        std::max(std::isfinite(stamina->regenDelay)
                     ? std::max(0.0f, stamina->regenDelay)
                     : 0.0f,
                 m_rules.staminaRegenDelaySeconds);
    if (StaminaExhausted(current, m_rules)) {
        stamina->exhausted =
            std::max(exhausted, m_rules.staminaExhaustedSeconds);
    }
    return true;
}

bool CombatSystem::ApplyDamage(ecs::EcsWorld& world, ecs::Entity target,
                               const ecs::DamageEvent& incoming,
                               DamageResult* result) {
    if (result != nullptr) {
        *result = {};
    }
    if (!world.Alive(target)) {
        return false;
    }
    entt::registry& registry = world.Registry();
    auto* health = registry.try_get<ecs::Health>(target);
    if (health == nullptr || health->current <= 0.0f) {
        return false;  // 无生命 / 已倒地：不重复结算
    }

    // 统一清洗事件数值：NaN/负值不能污染 Health、击退或前摇判定。
    ecs::DamageEvent event = incoming;
    event.amount = FiniteNonNegative(incoming.amount);
    event.knockback = FiniteNonNegative(incoming.knockback);
    event.staggerSeconds = FiniteNonNegative(incoming.staggerSeconds);
    event.poiseDamage = FiniteNonNegative(incoming.poiseDamage);
    if (!std::isfinite(event.dirX) || !std::isfinite(event.dirY)) {
        event.dirX = 0.0f;
        event.dirY = 0.0f;
    }
    float dirLen =
        std::sqrt(event.dirX * event.dirX + event.dirY * event.dirY);
    if (dirLen > kDirectionEpsilon) {
        event.dirX /= dirLen;
        event.dirY /= dirLen;
    } else {
        event.dirX = 0.0f;
        event.dirY = 0.0f;
        dirLen = 0.0f;
    }

    ecs::CombatStance* stance = registry.try_get<ecs::CombatStance>(target);

    // 1) 闪避无敌优先于格挡与伤害。
    if (stance != nullptr) {
        const float invuln =
            std::isfinite(stance->dodgeInvulnLeft)
                ? std::max(0.0f, stance->dodgeInvulnLeft)
                : 0.0f;
        if (invuln > 0.0f) {
            if (result != nullptr) {
                result->dodged = true;
            }
            ++m_stats.dodges;
            if (m_blockFeedbackCallback) {
                const ecs::Entity attacker = world.FindById(event.sourceId);
                m_blockFeedbackCallback(BlockFeedback{
                    target, attacker, BlockOutcome::Dodged, event.type});
            }
            return false;
        }
    }

    // 2) 正面格挡/弹反：只处理 Claw/Bite/Blunt；Fire/Debug 不参与。
    bool guardBroken = false;
    if (stance != nullptr && stance->blocking && event.amount > 0.0f &&
        IsBlockableType(event.type)) {
        const float guardBreakLeft =
            std::isfinite(stance->guardBreakLeft)
                ? std::max(0.0f, stance->guardBreakLeft)
                : 0.0f;
        const auto* facing = registry.try_get<ecs::Facing>(target);
        if (guardBreakLeft <= 0.0f &&
            IsFrontFacing(facing, event.dirX, event.dirY,
                          BlockArcDegrees(m_rules))) {
            std::optional<float> blockReduction;
            if (std::isfinite(stance->blockReduction) &&
                stance->blockReduction >= 0.0f) {
                blockReduction = stance->blockReduction;
            }
            std::optional<float> blockStaminaCost;
            if (std::isfinite(stance->blockStaminaCost) &&
                stance->blockStaminaCost >= 0.0f) {
                blockStaminaCost = stance->blockStaminaCost;
            }

            const float parryCooldown =
                std::isfinite(stance->parryCooldown)
                    ? std::max(0.0f, stance->parryCooldown)
                    : 0.0f;
            if (ParryWindowActive(stance->blockElapsed, m_rules) &&
                parryCooldown <= 0.0f &&
                TrySpendStamina(world, target, ParryStaminaCost(m_rules))) {
                // 弹反：零伤害、零伤口/感染/击退，攻击者获得短硬直。
                stance->parryCooldown = ParryCooldownSeconds(m_rules);
                if (result != nullptr) {
                    result->parried = true;
                }
                ++m_stats.parries;
                const ecs::Entity attacker = world.FindById(event.sourceId);
                ApplyTimedStagger(world, attacker,
                                  ParryStaggerSeconds(m_rules));
                if (m_blockFeedbackCallback) {
                    m_blockFeedbackCallback(BlockFeedback{
                        target, attacker, BlockOutcome::Parried, event.type});
                }
                return false;
            }

            const float cost =
                BlockStaminaCost(0.0f, blockStaminaCost, m_rules);
            if (TrySpendStamina(world, target, cost)) {
                event.amount *= BlockDamageScale(blockReduction, m_rules);
                event.knockback *= BlockKnockbackScale(m_rules);
                if (result != nullptr) {
                    result->blocked = true;
                }
                ++m_stats.blocks;
                if (m_blockFeedbackCallback) {
                    const ecs::Entity attacker = world.FindById(event.sourceId);
                    m_blockFeedbackCallback(BlockFeedback{
                        target, attacker, BlockOutcome::Blocked, event.type});
                }
            } else {
                // 体力不足：不减免本次伤害，强制关格挡并造成破防硬直。
                guardBroken = true;
                stance->blocking = false;
                stance->blockElapsed = 0.0f;
                stance->guardBreakLeft = GuardBreakSeconds(m_rules);
                if (result != nullptr) {
                    result->guardBroken = true;
                }
                ++m_stats.guardBreaks;
                if (m_blockFeedbackCallback) {
                    const ecs::Entity attacker = world.FindById(event.sourceId);
                    m_blockFeedbackCallback(BlockFeedback{
                        target, attacker, BlockOutcome::GuardBroken, event.type});
                }
            }
        }
    }

    // 3) 护甲减伤（M19-F4）：按伤害类型读取装备派生的 DamageResist。
    // 减伤发生在扣血/写伤口之前；护甲磨损只通知上层，characters 不依赖 items。
    float damage = event.amount;
    float armorReduction = 0.0f;
    float reduction = 0.0f;
    const auto* resist = registry.try_get<ecs::DamageResist>(target);
    if (resist != nullptr && damage > 0.0f) {
        armorReduction =
            std::clamp(ecs::DamageResistFor(*resist, event.type), 0.0f, 0.95f);
        reduction = armorReduction;
    }
    if (const auto* variant = registry.try_get<ecs::ZombieVariant>(target);
        variant != nullptr && damage > 0.0f &&
        event.type != ecs::DamageType::Fire &&
        event.type != ecs::DamageType::Debug) {
        reduction =
            std::max(reduction, ecs::ZombieVariantDamageReduction(variant->kind));
    }
    // M25-T4-V9：驾驶位只对僵尸爪/咬获得车辆防护；火焰/调试/PvP 不吃此减伤。
    if (const auto* occupant = registry.try_get<ecs::VehicleOccupant>(target);
        occupant != nullptr && damage > 0.0f &&
        (event.type == ecs::DamageType::Claw ||
         event.type == ecs::DamageType::Bite)) {
        reduction = std::max(
            reduction, std::clamp(occupant->damageReduction, 0.0f, 0.95f));
    }
    if (reduction > 0.0f) {
        damage *= 1.0f - std::clamp(reduction, 0.0f, 0.95f);
        if (armorReduction > 0.0f && resist != nullptr &&
            resist->wearPerHit > 0.0f && m_armorWearCallback) {
            m_armorWearCallback(target,
                                std::clamp(resist->wearPerHit, 0.0f, 1.0f));
        }
    }

    // 4) 扣血（Health 只认数值；来源/类型在事件里，管线侧解耦）
    health->current -= damage;
    m_stats.damageDealt += damage;
    if (event.amount > 0.0f) {
        ecs::LastDamage& last = registry.get_or_emplace<ecs::LastDamage>(target);
        last.cause = ecs::DamageCauseFromType(event.type);
        last.amount = damage;
        last.sourceId = event.sourceId;
    }

    // 受击/格挡/闪避都会重置战斗体力回复延迟；格挡/弹反的消耗已由
    // TrySpendStamina 重置，这里只兜底普通伤害。
    if (damage > 0.0f) {
        if (auto* stamina = registry.try_get<ecs::CombatStamina>(target);
            stamina != nullptr) {
            stamina->regenDelay =
                std::max(std::isfinite(stamina->regenDelay)
                             ? std::max(0.0f, stamina->regenDelay)
                             : 0.0f,
                         m_rules.staminaRegenDelaySeconds);
        }
    }

    // M20-F1：爪/咬实际命中后先让上层按“既有未包扎伤口”判定暴露，
    // 再写入本次新伤口；这样首次命中的暴露概率只由基础概率 + 旧伤决定。
    if (damage > 0.0f && m_infectionExposureCallback &&
        (event.type == ecs::DamageType::Claw ||
         event.type == ecs::DamageType::Bite)) {
        m_infectionExposureCallback(target, event.type, damage);
    }

    // M15-T2：同一入口按类型写入伤口（Debug/非正伤害忽略）。
    if (damage > 0.0f &&
        (event.type == ecs::DamageType::Blunt || event.type == ecs::DamageType::Claw ||
         event.type == ecs::DamageType::Bite)) {
        ecs::Wounds& wounds = registry.get_or_emplace<ecs::Wounds>(target);
        AddWound(wounds, event.type, damage, m_woundParams);
        if (registry.all_of<ecs::PlayerTag>(target)) {
            (void)registry.get_or_emplace<ecs::Pain>(target);
        }
    }

    // 5) 受击反应：硬直取较大值；击退以本次方向为准；闪红刷新
    float staggerSeconds = event.staggerSeconds;
    if (const auto* variant = registry.try_get<ecs::ZombieVariant>(target);
        variant != nullptr) {
        staggerSeconds *= ecs::ZombieVariantStaggerScale(
            variant->kind, event.poiseDamage,
            m_rules.armoredStaggerPoiseThreshold);
    }
    ecs::HitReaction& hit = registry.get_or_emplace<ecs::HitReaction>(target);
    hit.staggerLeft =
        std::max(FiniteNonNegative(hit.staggerLeft), staggerSeconds);
    const bool wasFlashing = FiniteNonNegative(hit.flashLeft) > 0.0f;
    ++hit.hitSerial;
    hit.sourceType = event.type;
    if (dirLen > kDirectionEpsilon) {
        // 事件方向 = 攻击者 → 目标；受击提示方向取反 = 目标 → 攻击者。
        hit.sourceDirX = -event.dirX;
        hit.sourceDirY = -event.dirY;
    }
    float knockback = event.knockback;
    if (const auto* variant = registry.try_get<ecs::ZombieVariant>(target);
        variant != nullptr && knockback > 0.0f) {
        knockback *= ecs::ZombieVariantKnockbackScale(variant->kind);
    }
    if (knockback > 0.0f && dirLen > kDirectionEpsilon) {
        hit.vx = event.dirX * knockback;
        hit.vy = event.dirY * knockback;
        // 击退本 tick 立即生效（Combat 在 Movement 之前；硬直窗口的后续 tick 由 Update 接管）
        if (auto* intent = registry.try_get<ecs::MoveIntent>(target); intent != nullptr) {
            intent->x = hit.vx;
            intent->y = hit.vy;
        }
    }
    hit.flashLeft = std::max(FiniteNonNegative(hit.flashLeft), kFlashSeconds);
    ecs::ActionState& action =
        registry.get_or_emplace<ecs::ActionState>(target);
    if (health->current <= 0.0f) {
        action.action = ecs::ActorAction::Dead;
        action.elapsed = 0.0f;
        action.duration = 0.0f;
    } else {
        action.action = ecs::ActorAction::Hit;
        action.elapsed = 0.0f;
        action.duration = std::max(staggerSeconds, kHitPresentationSeconds);
    }

    // 6) 硬直条扣除与破防长硬直（只对存活且显式初始化 Poise 的目标）。
    bool poiseBroken = false;
    if (health->current > 0.0f && event.poiseDamage > 0.0f) {
        if (auto* poise = registry.try_get<ecs::Poise>(target);
            poise != nullptr && std::isfinite(poise->max) && poise->max > 0.0f) {
            float current = std::isfinite(poise->current)
                                ? std::clamp(poise->current, 0.0f, poise->max)
                                : 0.0f;
            const float previous = current;
            current = std::max(0.0f, current - event.poiseDamage);
            poise->current = current;
            poise->regenDelay = std::max(
                std::isfinite(poise->regenDelay)
                    ? std::max(0.0f, poise->regenDelay)
                    : 0.0f,
                PoiseRegenDelaySeconds(m_rules));
            if (previous > 0.0f && current <= 0.0f) {
                poiseBroken = true;
                ++m_stats.poiseBreaks;
            }
        }
    }
    if (guardBroken) {
        const float seconds = GuardBreakSeconds(m_rules);
        hit.staggerLeft = std::max(hit.staggerLeft, seconds);
        action.duration = std::max(action.duration, seconds);
    }
    if (poiseBroken) {
        const float seconds = PoiseBreakStaggerSeconds(m_rules);
        hit.staggerLeft = std::max(hit.staggerLeft, seconds);
        action.duration = std::max(action.duration, seconds);
    }

    if (auto* sprite = registry.try_get<ecs::SpriteRef>(target); sprite != nullptr) {
        // 变体 tint（fast 蓝/strong 红）在受击闪红结束后必须还原，而不是统一回白。
        if (!wasFlashing) {
            hit.restoreTint = sprite->tint;
        }
        sprite->tint = kHitFlashTint;
    }

    // 7) 致死：HP 归零 → DeathSystem（倒地窗口 → 尸体）；此处只记统计与日志
    const bool lethal = health->current <= 0.0f;
    const bool targetIsPlayer = registry.all_of<ecs::PlayerTag>(target);
    const bool targetIsFriendly = registry.all_of<ecs::FriendlyNpc>(target);
    if (lethal && !targetIsPlayer && !targetIsFriendly) {
        ++m_stats.kills;
        ZL_INFO(kLogModule, "击杀：目标稳定 ID {}（来源 ID {}，{} 点 {} 伤害，剩 {:.1f} HP）→ 倒地",
                world.IdOf(target).id, event.sourceId, damage,
                static_cast<int>(event.type), health->current);
    } else if (targetIsPlayer) {
        ZL_INFO(kLogModule, "玩家{}：{} 点（来源 ID {}，{} 类型），剩 {:.1f} HP",
                lethal ? "死亡" : "受到伤害", damage, event.sourceId,
                static_cast<int>(event.type), health->current);
    } else {
        ZL_DEBUG(kLogModule, "伤害：来源 {} → 目标 {}（{} 点），剩 {:.1f} HP",
                 event.sourceId, world.IdOf(target).id, damage, health->current);
    }
    if (result != nullptr) {
        result->applied = true;
        result->poiseBroken = poiseBroken;
    }
    return true;
}

void CombatSystem::Update(ecs::EcsWorld& world, float dt) {
    if (!std::isfinite(dt) || dt < 0.0f) {
        dt = 0.0f;
    }
    entt::registry& registry = world.Registry();
    m_stats.staggered = 0;  // 每帧快照（其余统计跨帧累计）
    m_weaponWears.clear();  // M15-T1：磨损请求每 tick 重新收集
    m_hits.clear();         // M19-F1：命中记录每 tick 重新收集

    // 1) 格挡移动减速先作用于本 tick 输入意图；随后受击硬直会覆盖为击退速度，
    //    避免把已格挡/未格挡的击退再次乘 0.65。
    registry.view<ecs::CombatStance>().each(
        [&](const entt::entity e, const ecs::CombatStance& stance) {
            if (!stance.blocking ||
                (std::isfinite(stance.dodgeLeft) && stance.dodgeLeft > 0.0f)) {
                return;
            }
            if (auto* intent = registry.try_get<ecs::MoveIntent>(e);
                intent != nullptr) {
                const float scale = BlockMoveScale(m_rules);
                intent->x *= scale;
                intent->y *= scale;
            }
        });

    // 2) 受击反应衰减：硬直中 → MoveIntent 被击退速度接管（覆盖本 tick 已写意图）
    const float decay = std::max(0.0f, 1.0f - dt * kKnockbackDampingPerSecond);
    registry.view<ecs::HitReaction>().each([&](const entt::entity e, ecs::HitReaction& hit) {
        if (!std::isfinite(hit.flashLeft) || hit.flashLeft < 0.0f) {
            hit.flashLeft = 0.0f;
        }
        if (!std::isfinite(hit.staggerLeft) || hit.staggerLeft < 0.0f) {
            hit.staggerLeft = 0.0f;
        }
        if (!std::isfinite(hit.vx) || !std::isfinite(hit.vy)) {
            hit.vx = 0.0f;
            hit.vy = 0.0f;
        }
        if (hit.flashLeft > 0.0f) {
            hit.flashLeft = std::max(0.0f, hit.flashLeft - dt);
            if (hit.flashLeft <= 0.0f) {
                if (auto* sprite = registry.try_get<ecs::SpriteRef>(e); sprite != nullptr) {
                    sprite->tint = hit.restoreTint;  // 闪红结束恢复（含变体 tint）
                    hit.restoreTint = kTintRestore;
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

    // 3) 姿态计时：blockElapsed 驱动弹反窗口；闪避/破防/冷却按 tick 推进。
    registry.view<ecs::CombatStance>().each(
        [&](const entt::entity e, ecs::CombatStance& stance) {
            const auto* health = registry.try_get<ecs::Health>(e);
            const bool incapacitated =
                registry.any_of<ecs::Dying>(e) ||
                (health != nullptr && health->current <= 0.0f);
            if (!std::isfinite(stance.blockElapsed) ||
                stance.blockElapsed < 0.0f) {
                stance.blockElapsed = 0.0f;
            }
            if (!std::isfinite(stance.parryCooldown) ||
                stance.parryCooldown < 0.0f) {
                stance.parryCooldown = 0.0f;
            }
            if (!std::isfinite(stance.guardBreakLeft) ||
                stance.guardBreakLeft < 0.0f) {
                stance.guardBreakLeft = 0.0f;
            }
            if (!std::isfinite(stance.dodgeLeft)) {
                stance.dodgeLeft = 0.0f;
            }
            if (!std::isfinite(stance.dodgeInvulnLeft) ||
                stance.dodgeInvulnLeft < 0.0f) {
                stance.dodgeInvulnLeft = 0.0f;
            }
            if (!std::isfinite(stance.dodgeCooldown) ||
                stance.dodgeCooldown < 0.0f) {
                stance.dodgeCooldown = 0.0f;
            }
            if (!std::isfinite(stance.dodgeDirX) ||
                !std::isfinite(stance.dodgeDirY)) {
                stance.dodgeDirX = 0.0f;
                stance.dodgeDirY = 0.0f;
            }

            if (incapacitated) {
                stance.blocking = false;
                stance.blockElapsed = 0.0f;
                stance.dodgeLeft = 0.0f;
                stance.dodgeInvulnLeft = 0.0f;
                stance.dodgeDirX = 0.0f;
                stance.dodgeDirY = 0.0f;
            } else if (stance.guardBreakLeft > 0.0f) {
                stance.blocking = false;
                stance.blockElapsed = 0.0f;
                stance.guardBreakLeft =
                    std::max(0.0f, stance.guardBreakLeft - dt);
            } else if (stance.blocking) {
                stance.blockElapsed += dt;
            } else {
                stance.blockElapsed = 0.0f;
            }

            if (stance.dodgeLeft > 0.0f) {
                stance.dodgeLeft = std::max(0.0f, stance.dodgeLeft - dt);
                if (stance.dodgeLeft <= 0.0f) {
                    stance.dodgeDirX = 0.0f;
                    stance.dodgeDirY = 0.0f;
                }
            }
            if (stance.dodgeInvulnLeft > 0.0f) {
                stance.dodgeInvulnLeft =
                    std::max(0.0f, stance.dodgeInvulnLeft - dt);
            }
            if (stance.parryCooldown > 0.0f) {
                stance.parryCooldown =
                    std::max(0.0f, stance.parryCooldown - dt);
            }
            if (stance.dodgeCooldown > 0.0f) {
                stance.dodgeCooldown =
                    std::max(0.0f, stance.dodgeCooldown - dt);
            }

            // M30-T4：闪避位移优先于输入；受击硬直仍由击退接管。
            const auto* reaction = registry.try_get<ecs::HitReaction>(e);
            const bool staggered =
                reaction != nullptr &&
                std::isfinite(reaction->staggerLeft) &&
                reaction->staggerLeft > 0.0f;
            if (stance.dodgeLeft > 0.0f && !staggered) {
                const float dirLen =
                    std::sqrt(stance.dodgeDirX * stance.dodgeDirX +
                              stance.dodgeDirY * stance.dodgeDirY);
                if (dirLen > kDirectionEpsilon) {
                    if (auto* intent =
                            registry.try_get<ecs::MoveIntent>(e);
                        intent != nullptr) {
                        const float speed = DodgeSpeed(m_rules);
                        intent->x = stance.dodgeDirX / dirLen * speed;
                        intent->y = stance.dodgeDirY / dirLen * speed;
                    }
                }
            }
        });

    // 4) 战斗体力：按疲劳缩放有效上限/回复，耗尽锁定期间不回复。
    registry.view<ecs::CombatStamina>().each(
        [&](const entt::entity e, ecs::CombatStamina& stamina) {
            if (!std::isfinite(stamina.max) || stamina.max <= 0.0f) {
                stamina.max = m_rules.staminaMax;
            }
            if (!std::isfinite(stamina.current) || stamina.current < 0.0f) {
                stamina.current = 0.0f;
            }
            if (!std::isfinite(stamina.regenDelay) ||
                stamina.regenDelay < 0.0f) {
                stamina.regenDelay = 0.0f;
            }
            if (!std::isfinite(stamina.exhausted) || stamina.exhausted < 0.0f) {
                stamina.exhausted = 0.0f;
            }
            if (registry.any_of<ecs::Dying>(e)) {
                return;
            }
            if (const auto* health = registry.try_get<ecs::Health>(e);
                health != nullptr && health->current <= 0.0f) {
                return;
            }

            float fatigue = 0.0f;
            if (const auto* needs = registry.try_get<ecs::Needs>(e);
                needs != nullptr && std::isfinite(needs->fatigue)) {
                fatigue = std::clamp(needs->fatigue, 0.0f, 1.0f);
            }
            CombatRuleConfig perEntity = m_rules;
            perEntity.staminaMax = stamina.max;
            const float effectiveMax =
                std::max(0.0f, EffectiveMaxStamina(fatigue, perEntity));
            stamina.current =
                std::clamp(stamina.current, 0.0f, effectiveMax);

            // 耗尽锁定与回复延迟并行计时；只有两者都归零后的剩余 dt 才回复，
            // 避免低帧率下“延迟结束即多回复一整帧”。
            const float regenLock =
                std::max(stamina.exhausted, stamina.regenDelay);
            const float regenSeconds = std::max(0.0f, dt - regenLock);
            if (stamina.exhausted > 0.0f) {
                stamina.exhausted =
                    std::max(0.0f, stamina.exhausted - dt);
            }
            if (stamina.regenDelay > 0.0f) {
                stamina.regenDelay =
                    std::max(0.0f, stamina.regenDelay - dt);
            }
            if (regenSeconds > 0.0f) {
                stamina.current = std::min(
                    effectiveMax,
                    stamina.current +
                        StaminaRegenPerSecond(fatigue, m_rules) * regenSeconds);
            }
        });

    // 5) 硬直条回复：受击后先等待 regenDelay，再按配置速率回复。
    registry.view<ecs::Poise>().each(
        [&](const entt::entity e, ecs::Poise& poise) {
            if (registry.any_of<ecs::Dying>(e)) {
                return;
            }
            if (const auto* health = registry.try_get<ecs::Health>(e);
                health != nullptr && health->current <= 0.0f) {
                return;
            }
            if (!std::isfinite(poise.max) || poise.max <= 0.0f) {
                poise.current = 0.0f;
                poise.regenDelay = 0.0f;
                return;
            }
            if (!std::isfinite(poise.current) || poise.current < 0.0f) {
                poise.current = 0.0f;
            }
            if (!std::isfinite(poise.regenDelay) || poise.regenDelay < 0.0f) {
                poise.regenDelay = 0.0f;
            }
            poise.current = std::clamp(poise.current, 0.0f, poise.max);
            const float regenDelay = poise.regenDelay;
            poise.regenDelay = std::max(0.0f, poise.regenDelay - dt);
            const float regenSeconds = std::max(0.0f, dt - regenDelay);
            if (regenSeconds > 0.0f && poise.current < poise.max) {
                poise.current = std::min(
                    poise.max,
                    poise.current +
                        PoiseRegenPerSecond(m_rules) * regenSeconds);
            }
        });

    // 6) 命中候选：本 tick 一次收集（可受伤 = 有 Health 且 HP>0），
    //    避免在每挥击循环里构造 view（M5-T5 性能教训）
    m_candidates.clear();
    registry.view<ecs::Health, ecs::Position>().each(
        [&](const entt::entity e, const ecs::Health& health, const ecs::Position& position) {
            if (health.current > 0.0f && std::isfinite(position.x) &&
                std::isfinite(position.y) && world.Alive(e) &&
                !registry.any_of<ecs::NoCombatTarget>(e)) {
                m_candidates.push_back(
                    Candidate{e, position.x, position.y,
                              registry.try_get<ecs::AiState>(e) != nullptr});
            }
        });

    // 7) 攻击前摇 → 扇形判定 → ApplyDamage（可多目标）
    registry.view<ecs::AttackIntent, ecs::Position>().each(
        [&](const entt::entity attacker, ecs::AttackIntent& attack,
            const ecs::Position& attackerPos) {
            if (!attack.pending) {
                return;  // continue
            }
            if (!world.Alive(attacker)) {
                attack.pending = false;
                attack.windupLeft = 0.0f;
                return;
            }
            // 倒地/死亡攻击者不挥击（冒烟抓出：倒地窗口内残留的挥击会"死人出刀"；
            // 僵尸侧由 AiSystem 的硬直/死亡冻结兜底，此处统一封口）
            if (registry.any_of<ecs::Dying>(attacker)) {
                attack.pending = false;
                attack.windupLeft = 0.0f;
                return;
            }
            if (const auto* actorHealth = registry.try_get<ecs::Health>(attacker);
                actorHealth != nullptr && actorHealth->current <= 0.0f) {
                attack.pending = false;
                attack.windupLeft = 0.0f;
                return;
            }
            if (!std::isfinite(attackerPos.x) || !std::isfinite(attackerPos.y)) {
                attack.pending = false;
                attack.windupLeft = 0.0f;
                return;
            }

            const float windupSeconds =
                std::isfinite(attack.windupSeconds)
                    ? std::max(0.0f, attack.windupSeconds)
                    : 0.0f;
            if (windupSeconds > 0.0f) {
                const auto* hit = registry.try_get<ecs::HitReaction>(attacker);
                const auto* stance = registry.try_get<ecs::CombatStance>(attacker);
                const bool staggered =
                    hit != nullptr && std::isfinite(hit->staggerLeft) &&
                    hit->staggerLeft > 0.0f;
                const bool stanceLocked =
                    stance != nullptr &&
                    ((std::isfinite(stance->guardBreakLeft) &&
                      stance->guardBreakLeft > 0.0f) ||
                     stance->blocking ||
                     (std::isfinite(stance->dodgeLeft) &&
                      stance->dodgeLeft > 0.0f));
                if (staggered || stanceLocked) {
                    // 前摇被硬直/格挡/闪避/破防打断：已扣体力不退，直接取消。
                    attack.pending = false;
                    attack.windupLeft = 0.0f;
                    return;
                }

                float windupLeft = std::isfinite(attack.windupLeft)
                                       ? std::max(0.0f, attack.windupLeft)
                                       : 0.0f;
                if (windupLeft <= 0.0f) {
                    windupLeft = windupSeconds;
                }
                ecs::ActionState& action =
                    registry.get_or_emplace<ecs::ActionState>(attacker);
                if (action.action != ecs::ActorAction::Attacking ||
                    action.duration < windupSeconds) {
                    action.action = ecs::ActorAction::Attacking;
                    action.elapsed = 0.0f;
                    action.duration = windupSeconds;
                }
                windupLeft = std::max(0.0f, windupLeft - dt);
                attack.windupLeft = windupLeft;
                if (windupLeft > 0.0f) {
                    return;  // 前摇未结束：本 tick 不判定
                }
            }

            attack.pending = false;  // 只消费一次
            attack.windupLeft = 0.0f;
            ++m_stats.swings;
            ecs::ActionState& action =
                registry.get_or_emplace<ecs::ActionState>(attacker);
            action.action = ecs::ActorAction::Attacking;
            action.elapsed = 0.0f;
            action.duration = kAttackPresentationSeconds;

            if (!std::isfinite(attack.dirX) || !std::isfinite(attack.dirY)) {
                return;  // 无方向：空挥
            }
            const float dirLen =
                std::sqrt(attack.dirX * attack.dirX + attack.dirY * attack.dirY);
            if (dirLen < kDirectionEpsilon) {
                return;  // 无方向：空挥
            }
            const float dirX = attack.dirX / dirLen;
            const float dirY = attack.dirY / dirLen;
            const float range = std::isfinite(attack.range)
                                    ? std::clamp(attack.range, 0.0f, 1000.0f)
                                    : 0.0f;
            const float arcDegrees =
                std::isfinite(attack.arcDegrees)
                    ? std::clamp(attack.arcDegrees, 0.0f, 360.0f)
                    : 0.0f;
            const float halfCos = std::cos(arcDegrees * 0.5f * kDegToRad);
            const float rangeSq = range * range;
            // M5-T6 阵营规则：AI 攻击者不误伤同类（僵尸 vs 僵尸豁免；阵营系统 M7+ 再抽象）
            const bool attackerIsAi = registry.try_get<ecs::AiState>(attacker) != nullptr;
            bool hitAny = false;

            for (const Candidate& candidate : m_candidates) {
                if (candidate.e == attacker ||
                    (attackerIsAi &&
                     (candidate.ai ||
                      registry.any_of<ecs::Animal>(candidate.e)))) {
                    continue;
                }
                const float ox = candidate.x - attackerPos.x;
                const float oy = candidate.y - attackerPos.y;
                const float distSq = ox * ox + oy * oy;
                if (!std::isfinite(distSq) || distSq > rangeSq) {
                    continue;  // 距离外
                }
                const float dist = std::sqrt(distSq);
                if (dist > kDirectionEpsilon &&
                    (ox * dirX + oy * dirY) / dist < halfCos) {
                    continue;  // 扇形外
                }
                float damageScale = 1.0f;
                if (const auto* needs = registry.try_get<ecs::Needs>(attacker);
                    needs != nullptr && std::isfinite(needs->damageScale)) {
                    damageScale = std::clamp(needs->damageScale, 0.05f, 1.0f);
                }
                ecs::DamageEvent event;
                event.sourceId = world.IdOf(attacker).id;
                event.type = attack.type;
                event.amount = FiniteNonNegative(attack.damage) * damageScale;
                event.dirX = dirX;
                event.dirY = dirY;
                event.knockback = FiniteNonNegative(attack.knockback);
                event.staggerSeconds =
                    FiniteNonNegative(attack.staggerSeconds);
                event.poiseDamage = FiniteNonNegative(attack.poiseDamage);
                DamageResult outcome;
                if (ApplyDamage(world, candidate.e, event, &outcome)) {
                    hitAny = true;
                    if (!outcome.blocked) {
                        // 被普通格挡的伤害不是“真实命中”，不进入 m_hits/经验。
                        ++m_stats.hits;
                        const auto* targetHealth =
                            registry.try_get<ecs::Health>(candidate.e);
                        const auto* lastDamage =
                            registry.try_get<ecs::LastDamage>(candidate.e);
                        const float dealt =
                            lastDamage != nullptr ? lastDamage->amount : 0.0f;
                        const bool lethal =
                            targetHealth != nullptr &&
                            targetHealth->current <= 0.0f;
                        m_hits.push_back(HitInfo{
                            attacker, candidate.e, dealt, lethal,
                            outcome.poiseBroken});
                        if (m_hitFeedbackCallback) {
                            m_hitFeedbackCallback(HitFeedback{
                                attacker, candidate.e, dealt, lethal,
                                outcome.poiseBroken});
                        }
                    }
                }
            }

            // M15-T1：一次挥击最多产生一次主手武器磨损请求（多目标命中不叠加）。
            if (hitAny && std::isfinite(attack.weaponWearPerHit) &&
                attack.weaponWearPerHit > 0.0f) {
                m_weaponWears.push_back(WeaponWearRequest{
                    attacker, std::min(attack.weaponWearPerHit, 1.0f)});
            }
        });
}

}  // namespace zl::characters
