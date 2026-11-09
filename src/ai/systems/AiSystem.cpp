#include "ai/systems/AiSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "ai/Passability.h"
#include "characters/components/CharacterComponents.h"
#include "characters/systems/AnimationSystem.h"
#include "core/Log.h"
#include "ecs/components/CommonComponents.h"

namespace zl::ai {
namespace {

using ecs::AiState;
using ecs::AiStatus;

// —— 状态机参数（集中调参；转移判定全文见 Update 的 switch）——
constexpr float kIdleMinSeconds = 2.0f;          // Idle 时长下限（进入时确定性随机）
constexpr float kIdleMaxSeconds = 6.0f;          // Idle 时长上限
constexpr float kWanderMinRadius = 3.0f;         // 游荡目标半径下限（格）
constexpr float kWanderMaxRadius = 10.0f;        // 游荡目标半径上限
constexpr int kWanderSnapRadius = 3;             // 游荡点就近可走校正半径
constexpr float kAttackRange = 1.0f;             // 进入攻击的距离（中心距，格）
constexpr float kAttackKeepRange = 1.5f;         // 脱出攻击的距离（迟滞，防状态抖动）
constexpr float kChaseRepathSeconds = 0.5f;      // 追击重寻路节流（失败重试/周期刷新）
constexpr float kChaseGoalTolerance = 1.2f;      // 目标移动超过该距离 → 立即重寻路（格）
constexpr float kSearchSeconds = 6.0f;           // 搜索超时 → 放弃回 Idle
constexpr float kSearchRetargetSeconds = 2.0f;   // 搜索巡点间隔
constexpr float kSearchRadius = 4.0f;            // 搜索巡点半径（格）
constexpr float kHearReactSeconds = 1.0f;        // 听见声音后 1s 内 → 去查看（Idle/Wander）

// 确定性伪随机 [0,1)（splitmix64 尾段；同 StableId + 盐 → 同结果，测试可复现）
float Rand01(std::uint64_t stableId, std::uint64_t salt) {
    std::uint64_t h = stableId * 0x9E3779B97F4A7C15ull + salt * 0xBF58476D1CE4E5B9ull +
                      0x94D049BB133111EBull;
    h ^= h >> 30;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 27;
    h *= 0x94D049BB133111EBull;
    h ^= h >> 31;
    return static_cast<float>(h >> 11) * (1.0f / 9007199254740992.0f);
}

float DistSq(float ax, float ay, float bx, float by) {
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

// 停步：取消路径（Following/Pending 一并处理）+ 意图清零。幂等，可每 tick 调用。
void CancelPathAndStop(ecs::EcsWorld& world, ecs::Entity e) {
    entt::registry& registry = world.Registry();
    if (auto* follow = registry.try_get<ecs::PathFollow>(e); follow != nullptr) {
        follow->status = ecs::PathStatus::None;
        follow->waypoints.clear();
        follow->index = 0;
    }
    if (auto* request = registry.try_get<ecs::PathRequest>(e); request != nullptr) {
        request->state = ecs::PathStatus::None;
        request->queryId = 0;
    }
    if (auto* intent = registry.try_get<ecs::MoveIntent>(e); intent != nullptr) {
        intent->x = 0.0f;
        intent->y = 0.0f;
    }
}

}  // namespace

const AiSystem::AttackProfile& AiSystem::AttackParams::ProfileFor(
    ecs::ZombieVariantKind kind) const {
    switch (kind) {
        case ecs::ZombieVariantKind::Fast:
            return fast;
        case ecs::ZombieVariantKind::Strong:
            return strong;
        case ecs::ZombieVariantKind::Armored:
            return armored;
        case ecs::ZombieVariantKind::Normal:
        default:
            return normal;
    }
}

void AiSystem::Update(ecs::EcsWorld& world, float dt) {
    ++m_tick;
    entt::registry& registry = world.Registry();
    // 每帧快照：状态分布清点；swings 跨帧累计（HUD 显示总数）
    m_stats.idle = 0;
    m_stats.wander = 0;
    m_stats.chase = 0;
    m_stats.search = 0;
    m_stats.attack = 0;
    m_stats.dead = 0;

    // M23-F3-T5：燃烧僵尸需要无视视野追逐最近玩家；每帧只收集一次。
    m_players.clear();
    registry.view<ecs::PlayerTag, ecs::Position>().each(
        [&](const entt::entity player, const ecs::Position& position) {
            const auto* health = registry.try_get<ecs::Health>(player);
            if (health != nullptr && health->current <= 0.0f) {
                return;
            }
            m_players.push_back(PlayerTarget{world.IdOf(player).id, position.x,
                                             position.y});
        });

    registry.view<AiState, ecs::Position>().each(
        [&](const entt::entity e, AiState& ai, const ecs::Position& position) {
            // 状态分布计数（快照：不受 LOD 门控影响，LOD 冻结 tick 按既有状态计入）
            switch (ai.status) {
                case AiStatus::Idle:
                    ++m_stats.idle;
                    break;
                case AiStatus::Wander:
                    ++m_stats.wander;
                    break;
                case AiStatus::Chase:
                    ++m_stats.chase;
                    break;
                case AiStatus::SearchLastKnown:
                    ++m_stats.search;
                    break;
                case AiStatus::Attack:
                    ++m_stats.attack;
                    break;
                case AiStatus::Dead:
                    ++m_stats.dead;
                    break;
            }

            // M5-T5：LOD 门控——非本 tick 档位 → 冻结转移与动作（计时下次按 dtScale 补齐）
            const auto* lod = registry.try_get<ecs::LodState>(e);
            if (lod != nullptr && !lod->updateThisTick) {
                return;
            }
            const float edt = lod != nullptr ? dt * lod->dtScale : dt;
            // M5-T6/M15-T3：受击硬直中 → 冻结状态机（转移/计时/动作全暂停）；
            // 正在蓄力的攻击被打断（不会硬直结束后立刻命中，必须重新前摇）。
            const auto* hit = registry.try_get<ecs::HitReaction>(e);
            if (hit != nullptr && hit->staggerLeft > 0.0f) {
                if (ai.attackWindupLeft > 0.0f) {
                    ai.attackWindupLeft = 0.0f;
                    ++m_stats.interrupted;
                }
                return;
            }

            // M20-F3：嚎叫蓄力时站定，给玩家可读的打断窗口；播报/引怪由
            // ZombieSpecialSystem 在同 tick 完成，AI 不重复发声音。
            const auto* special = registry.try_get<ecs::ZombieSpecial>(e);
            if (special != nullptr &&
                special->kind == ecs::ZombieSpecialKind::Howler &&
                special->windup > 0.0f) {
                CancelPathAndStop(world, e);
                return;
            }

            auto* health = registry.try_get<ecs::Health>(e);
            auto* request = registry.try_get<ecs::PathRequest>(e);
            auto* follow = registry.try_get<ecs::PathFollow>(e);
            auto* perception = registry.try_get<ecs::PerceptionState>(e);
            const auto* assault = registry.try_get<ecs::SiegeAssaultTarget>(e);
            const bool assaulting = assault != nullptr && assault->active;
            const auto* burning = registry.try_get<ecs::BurningZombie>(e);
            const bool panicking =
                burning != nullptr && burning->panicRemaining > 0.0f;
            if (panicking && !assaulting && perception != nullptr &&
                burning->panicChaseRadius > 0.0f) {
                const float radius = burning->panicChaseRadius;
                const float radiusSq = radius * radius;
                PlayerTarget best;
                float bestDistSq = radiusSq + 1.0f;
                for (const PlayerTarget& player : m_players) {
                    const float distanceSq =
                        DistSq(position.x, position.y, player.x, player.y);
                    if (distanceSq > radiusSq || distanceSq >= bestDistSq) {
                        continue;
                    }
                    best = player;
                    bestDistSq = distanceSq;
                }
                if (best.id != 0) {
                    ai.targetId = best.id;
                    perception->lastKnownX = best.x;
                    perception->lastKnownY = best.y;
                    perception->alertness = 1.0f;
                } else {
                    ai.targetId = 0;
                }
            }

            ai.stateTime += edt;
            ai.actionTimer = std::max(0.0f, ai.actionTimer - edt);

            // —— facts：每 tick 计算一次，转移判定只读 ——
            // lastKnown（LastKnownPos）由感知维护：看见 = 玩家位置；听见 = 声源位置
            const float lastKnownX = assaulting
                                         ? assault->x
                                         : (perception != nullptr
                                                ? perception->lastKnownX
                                                : position.x);
            const float lastKnownY = assaulting
                                         ? assault->y
                                         : (perception != nullptr
                                                ? perception->lastKnownY
                                                : position.y);
            const bool heardRecently =
                perception != nullptr && perception->sinceHeard < kHearReactSeconds;
            const bool dead = health != nullptr && health->current <= 0.0f;
            bool targetVisible = false;
            float targetX = 0.0f;
            float targetY = 0.0f;
            if (assaulting) {
                ai.targetId = 0;
            } else if (ai.targetId != 0) {
                const ecs::Entity target = world.FindById(ai.targetId);
                if (target != ecs::EcsWorld::kNull) {
                    if (const auto* tp = registry.try_get<ecs::Position>(target)) {
                        targetVisible = true;
                        targetX = tp->x;
                        targetY = tp->y;
                    }
                } else {
                    ai.targetId = 0;  // 目标实体已回收
                }
            }
            const bool pathBusy =
                (follow != nullptr && follow->status == ecs::PathStatus::Following) ||
                (request != nullptr && request->state == ecs::PathStatus::Pending);
            const float targetDistSq = DistSq(position.x, position.y, targetX, targetY);

            // —— 集中转移表：每行 = 一条出口条件 ——
            switch (ai.status) {
                case AiStatus::Idle:
                    if (dead) {
                        EnterState(world, e, ai, AiStatus::Dead, position.x, position.y);
                    } else if (targetVisible) {
                        EnterState(world, e, ai, AiStatus::Chase, position.x, position.y);
                    } else if (heardRecently) {
                        EnterState(world, e, ai, AiStatus::SearchLastKnown, position.x,
                                   position.y);  // 刚听见 → 去声源查看
                    } else if (ai.stateTime >= ai.idleSeconds) {
                        EnterState(world, e, ai, AiStatus::Wander, position.x, position.y);
                    }
                    break;
                case AiStatus::Wander:
                    if (dead) {
                        EnterState(world, e, ai, AiStatus::Dead, position.x, position.y);
                    } else if (targetVisible) {
                        EnterState(world, e, ai, AiStatus::Chase, position.x, position.y);
                    } else if (heardRecently) {
                        EnterState(world, e, ai, AiStatus::SearchLastKnown, position.x,
                                   position.y);  // 刚听见 → 中断游荡去查看
                    } else if (!pathBusy) {
                        EnterState(world, e, ai, AiStatus::Idle, position.x, position.y);
                    }
                    break;
                case AiStatus::Chase:
                    if (dead) {
                        EnterState(world, e, ai, AiStatus::Dead, position.x, position.y);
                    } else if (!targetVisible) {
                        EnterState(world, e, ai, AiStatus::SearchLastKnown, position.x,
                                   position.y);
                    } else if (targetDistSq <= kAttackRange * kAttackRange) {
                        EnterState(world, e, ai, AiStatus::Attack, position.x, position.y);
                    } else {
                        // 目标移动出容忍 → 立即重寻路（在途查询不重复提交）；
                        // 否则路径空闲时按 actionTimer 节流重试（失败/到达后兜底）
                        const bool goalStale =
                            request == nullptr ||
                            DistSq(static_cast<float>(request->goalX) + 0.5f,
                                   static_cast<float>(request->goalY) + 0.5f, lastKnownX,
                                   lastKnownY) >
                                kChaseGoalTolerance * kChaseGoalTolerance;
                        const bool pendingRepath =
                            request != nullptr && request->state == ecs::PathStatus::Pending;
                        if (request != nullptr && !pendingRepath &&
                            (goalStale || (!pathBusy && ai.actionTimer <= 0.0f))) {
                            RequestPathTo(world, e, lastKnownX, lastKnownY);
                            ai.actionTimer = kChaseRepathSeconds;
                        }
                    }
                    break;
                case AiStatus::SearchLastKnown:
                    if (dead) {
                        EnterState(world, e, ai, AiStatus::Dead, position.x, position.y);
                    } else if (targetVisible) {
                        EnterState(world, e, ai, AiStatus::Chase, position.x, position.y);
                    } else if (ai.stateTime >= kSearchSeconds) {
                        EnterState(world, e, ai, AiStatus::Idle, position.x, position.y);
                    } else if (!pathBusy && ai.actionTimer <= 0.0f) {
                        // 到点后再在最后已知位置周围挑巡点
                        const std::uint64_t id = world.IdOf(e).id;
                        const float angle = Rand01(id, m_tick) * 6.2831853f;
                        const float radius =
                            1.0f + Rand01(id, m_tick ^ 0x51633E2Du) * kSearchRadius;
                        RequestPathTo(world, e, lastKnownX + std::cos(angle) * radius,
                                      lastKnownY + std::sin(angle) * radius);
                        ai.actionTimer = kSearchRetargetSeconds;
                    }
                    break;
                case AiStatus::Attack:
                    if (dead) {
                        EnterState(world, e, ai, AiStatus::Dead, position.x, position.y);
                    } else if (!targetVisible) {
                        EnterState(world, e, ai, AiStatus::SearchLastKnown, position.x,
                                   position.y);
                    } else if (targetDistSq > kAttackKeepRange * kAttackKeepRange) {
                        EnterState(world, e, ai, AiStatus::Chase, position.x, position.y);
                    } else {
                        CancelPathAndStop(world, e);  // 贴身停步（幂等）
                        if (auto* facing = registry.try_get<ecs::Facing>(e); facing != nullptr) {
                            facing->dir = characters::AnimationSystem::FacingFromVelocity(
                                targetX - position.x, targetY - position.y);
                        }
                        const auto* variant = registry.try_get<ecs::ZombieVariant>(e);
                        const AttackProfile& profile = m_attack.ProfileFor(
                            variant != nullptr ? variant->kind
                                               : ecs::ZombieVariantKind::Normal);

                        bool strike = false;
                        if (ai.attackWindupLeft > 0.0f) {
                            ai.attackWindupLeft =
                                std::max(0.0f, ai.attackWindupLeft - edt);
                            strike = ai.attackWindupLeft <= 0.0f;
                        } else if (ai.attackCooldown > 0.0f) {
                            ai.attackCooldown = std::max(0.0f, ai.attackCooldown - edt);
                        } else {
                            // 新一击：先进入可读前摇，前摇结束才真正落地伤害。
                            ai.attackWindupLeft = std::max(0.0f, profile.windupSeconds);
                            ++m_stats.windups;
                            if (ai.attackWindupLeft <= 0.0f) {
                                strike = true;
                            } else {
                                ecs::ActionState& action =
                                    registry.get_or_emplace<ecs::ActionState>(e);
                                action.action = ecs::ActorAction::Attacking;
                                action.elapsed = 0.0f;
                                action.duration = ai.attackWindupLeft;
                            }
                        }

                        if (strike) {
                            ++ai.attackPulses;
                            ++m_stats.swings;
                            ai.attackCooldown =
                                std::max(0.0f, profile.cooldownSeconds);
                            // M5-T6：落下攻击意图（CombatSystem 同 tick 消费：
                            // 扇形判定 → 伤害管线 → 玩家被抓/僵尸受击）
                            if (auto* attack = registry.try_get<ecs::AttackIntent>(e);
                                attack != nullptr) {
                                attack->targetId = ai.targetId;
                                const float ddx = targetX - position.x;
                                const float ddy = targetY - position.y;
                                const float dlen = std::sqrt(ddx * ddx + ddy * ddy);
                                if (dlen > 0.0001f) {
                                    attack->dirX = ddx / dlen;
                                    attack->dirY = ddy / dlen;
                                }
                                attack->damage = profile.damage;
                                attack->knockback = profile.knockback;
                                attack->staggerSeconds = profile.staggerSeconds;
                                attack->range = profile.range;
                                attack->arcDegrees = profile.arcDegrees;
                                attack->type = profile.type;
                                attack->pending = true;
                            }
                        }
                    }
                    break;
                case AiStatus::Dead:
                    break;  // DeathSystem 负责转尸体；AI 不再动作
            }
        });
}

void AiSystem::EnterState(ecs::EcsWorld& world, ecs::Entity e, AiState& ai, AiStatus status,
                          float px, float py) {
    ai.status = status;
    ai.stateTime = 0.0f;
    ai.actionTimer = 0.0f;
    ai.attackWindupLeft = 0.0f;
    const std::uint64_t stableId = world.IdOf(e).id;
    switch (status) {
        case AiStatus::Idle:
            CancelPathAndStop(world, e);
            ai.idleSeconds =
                kIdleMinSeconds + Rand01(stableId, m_tick) * (kIdleMaxSeconds - kIdleMinSeconds);
            break;
        case AiStatus::Wander: {
            const float angle = Rand01(stableId, m_tick) * 6.2831853f;
            const float radius =
                kWanderMinRadius + Rand01(stableId, m_tick ^ 0x2545F491u) *
                                       (kWanderMaxRadius - kWanderMinRadius);
            if (!RequestPathTo(world, e, px + std::cos(angle) * radius,
                               py + std::sin(angle) * radius)) {
                EnterState(world, e, ai, AiStatus::Idle, px, py);  // 挑不到 → 回 Idle 重新计时
            }
            break;
        }
        case AiStatus::Chase: {
            const auto* percep = world.Registry().try_get<ecs::PerceptionState>(e);
            RequestPathTo(world, e, percep != nullptr ? percep->lastKnownX : px,
                          percep != nullptr ? percep->lastKnownY : py);
            ai.actionTimer = kChaseRepathSeconds;
            break;
        }
        case AiStatus::SearchLastKnown: {
            const auto* percep = world.Registry().try_get<ecs::PerceptionState>(e);
            RequestPathTo(world, e, percep != nullptr ? percep->lastKnownX : px,
                          percep != nullptr ? percep->lastKnownY : py);
            ai.actionTimer = kSearchRetargetSeconds;
            break;
        }
        case AiStatus::Attack:
            CancelPathAndStop(world, e);
            ai.attackCooldown = 0.0f;  // 贴身立刻落第一击
            break;
        case AiStatus::Dead:
            CancelPathAndStop(world, e);
            break;
    }
}

bool AiSystem::RequestPathTo(ecs::EcsWorld& world, ecs::Entity e, float tx, float ty) {
    auto* request = world.Registry().try_get<ecs::PathRequest>(e);
    if (request == nullptr || !m_walkable) {
        return false;
    }
    int gx = static_cast<int>(std::floor(tx));
    int gy = static_cast<int>(std::floor(ty));
    if (!m_walkable(gx, gy)) {
        int nearX = 0;
        int nearY = 0;
        if (!FindNearestWalkable(gx, gy, kWanderSnapRadius, m_walkable, &nearX, &nearY)) {
            return false;
        }
        gx = nearX;
        gy = nearY;
    }
    request->goalX = gx;
    request->goalY = gy;
    request->newRequest = true;
    return true;
}

}  // namespace zl::ai
