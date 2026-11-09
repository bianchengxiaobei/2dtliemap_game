#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <type_traits>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/systems/AnimationSystem.h"
#include "characters/systems/CombatSystem.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"

using namespace zl;

namespace {

using zl::characters::CombatSystem;
using zl::ecs::AttackIntent;
using zl::ecs::CombatStamina;
using zl::ecs::CombatStance;
using zl::ecs::DamageEvent;
using zl::ecs::DamageType;
using zl::ecs::EcsWorld;
using zl::ecs::Facing;
using zl::ecs::Health;
using zl::ecs::HitReaction;
using zl::ecs::Poise;
using zl::ecs::Position;

constexpr float kDt = 1.0f / 60.0f;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

ecs::Entity AddActor(EcsWorld& world, float x = 0.0f, float y = 0.0f,
                     float hp = 100.0f) {
    const ecs::Entity entity = world.Create();
    world.Registry().emplace<Position>(entity, Position{x, y, 0});
    world.Registry().emplace<Health>(entity, Health{hp, hp});
    return entity;
}

AttackIntent MakeIntent(float damage = 10.0f, float windup = 0.0f) {
    AttackIntent attack;
    attack.dirX = 1.0f;
    attack.dirY = 0.0f;
    attack.range = 1.5f;
    attack.arcDegrees = 120.0f;
    attack.damage = damage;
    attack.knockback = 4.0f;
    attack.staggerSeconds = 0.35f;
    attack.windupSeconds = windup;
    attack.windupLeft = windup;
    attack.pending = true;
    return attack;
}

DamageEvent MakeBlunt(float damage = 20.0f) {
    DamageEvent event;
    event.type = DamageType::Blunt;
    event.amount = damage;
    event.dirX = 1.0f;
    event.dirY = 0.0f;
    event.knockback = 4.0f;
    event.staggerSeconds = 0.3f;
    return event;
}

void AddGuard(EcsWorld& world, ecs::Entity entity, float stamina = 100.0f,
              float blockElapsed = 0.5f) {
    world.Registry().emplace<CombatStamina>(
        entity, CombatStamina{stamina, 100.0f, 0.0f, 0.0f});
    CombatStance stance;
    stance.blocking = true;
    stance.blockElapsed = blockElapsed;
    world.Registry().emplace<CombatStance>(entity, stance);
    world.Registry().emplace<Facing>(entity, Facing{ecs::Direction::Left});
}

}  // namespace

TEST_CASE("M30-T2：战斗组件默认值与平凡可拷贝") {
    const DamageEvent event;
    CHECK(event.poiseDamage == 0.0f);

    const AttackIntent attack;
    CHECK(attack.kind == ecs::AttackKind::Light);
    CHECK(attack.windupSeconds == 0.0f);
    CHECK(attack.windupLeft == 0.0f);
    CHECK(attack.poiseDamage == 0.0f);
    CHECK(attack.staminaCost == 0.0f);

    const CombatStamina stamina;
    CHECK(stamina.current == 100.0f);
    CHECK(stamina.max == 100.0f);
    CHECK(stamina.regenDelay == 0.0f);
    CHECK(stamina.exhausted == 0.0f);

    const CombatStance stance;
    CHECK_FALSE(stance.blocking);
    CHECK(stance.blockReduction == -1.0f);
    CHECK(stance.blockStaminaCost == -1.0f);
    CHECK(stance.dodgeLeft == 0.0f);

    const Poise poise;
    CHECK(poise.current == 0.0f);
    CHECK(poise.max == 0.0f);

    static_assert(std::is_trivially_copyable_v<AttackIntent>);
    static_assert(std::is_trivially_copyable_v<DamageEvent>);
    static_assert(std::is_trivially_copyable_v<CombatStamina>);
    static_assert(std::is_trivially_copyable_v<CombatStance>);
    static_assert(std::is_trivially_copyable_v<Poise>);
}

TEST_CASE("M30-T2：前摇倒计时、单次命中与零前摇兼容") {
    EcsWorld world;
    const auto attacker = AddActor(world, 0.0f, 0.0f);
    const auto target = AddActor(world, 1.0f, 0.0f);
    world.Registry().emplace<AttackIntent>(attacker, MakeIntent(10.0f, 0.10f));

    CombatSystem combat;
    combat.Update(world, 0.04f);
    CHECK(world.Registry().get<Health>(target).current == doctest::Approx(100.0f));
    CHECK(world.Registry().get<AttackIntent>(attacker).pending);
    CHECK(world.Registry().get<AttackIntent>(attacker).windupLeft ==
          doctest::Approx(0.06f));
    CHECK(combat.GetStats().swings == 0u);

    combat.Update(world, 0.04f);
    CHECK(world.Registry().get<Health>(target).current == doctest::Approx(100.0f));
    CHECK(world.Registry().get<AttackIntent>(attacker).windupLeft ==
          doctest::Approx(0.02f));

    combat.Update(world, 0.03f);
    CHECK(world.Registry().get<Health>(target).current == doctest::Approx(90.0f));
    CHECK_FALSE(world.Registry().get<AttackIntent>(attacker).pending);
    CHECK(world.Registry().get<AttackIntent>(attacker).windupLeft == 0.0f);
    CHECK(combat.GetStats().swings == 1u);
    CHECK(combat.GetStats().hits == 1u);
    CHECK(combat.GetHits().size() == 1u);

    combat.Update(world, 0.1f);
    CHECK(world.Registry().get<Health>(target).current == doctest::Approx(90.0f));
    CHECK(combat.GetStats().swings == 1u);

    // windupSeconds=0 是旧 AI/测试行为：落下即命中。
    const auto legacyAttacker = AddActor(world, 0.0f, 3.0f);
    const auto legacyTarget = AddActor(world, 1.0f, 3.0f);
    world.Registry().emplace<AttackIntent>(legacyAttacker, MakeIntent(10.0f, 0.0f));
    combat.Update(world, kDt);
    CHECK(world.Registry().get<Health>(legacyTarget).current ==
          doctest::Approx(90.0f));
    CHECK(combat.GetStats().swings == 2u);
}

TEST_CASE("M30-T2：前摇被硬直/死亡打断") {
    EcsWorld world;
    const auto attacker = AddActor(world, 0.0f, 0.0f);
    const auto target = AddActor(world, 1.0f, 0.0f);
    world.Registry().emplace<AttackIntent>(attacker, MakeIntent(10.0f, 0.2f));
    HitReaction stagger;
    stagger.staggerLeft = 0.5f;
    world.Registry().emplace<HitReaction>(attacker, stagger);

    CombatSystem combat;
    combat.Update(world, 0.05f);
    CHECK(world.Registry().get<Health>(target).current == doctest::Approx(100.0f));
    CHECK_FALSE(world.Registry().get<AttackIntent>(attacker).pending);
    CHECK(world.Registry().get<AttackIntent>(attacker).windupLeft == 0.0f);
    CHECK(combat.GetStats().swings == 0u);

    const auto dyingAttacker = AddActor(world, 0.0f, 2.0f);
    const auto dyingTarget = AddActor(world, 1.0f, 2.0f);
    world.Registry().emplace<AttackIntent>(dyingAttacker, MakeIntent(10.0f, 0.2f));
    world.Registry().emplace<ecs::Dying>(dyingAttacker);
    combat.Update(world, kDt);
    CHECK(world.Registry().get<Health>(dyingTarget).current ==
          doctest::Approx(100.0f));
    CHECK_FALSE(world.Registry().get<AttackIntent>(dyingAttacker).pending);
}

TEST_CASE("M30-T2：普通格挡减伤、体力消耗与 m_hits 排除") {
    EcsWorld world;
    const auto attacker = AddActor(world, 0.0f, 0.0f);
    const auto defender = AddActor(world, 1.0f, 0.0f);
    AddGuard(world, defender);

    std::vector<CombatSystem::BlockFeedback> feedback;
    CombatSystem combat;
    combat.SetBlockFeedbackCallback(
        [&](const CombatSystem::BlockFeedback& value) { feedback.push_back(value); });

    world.Registry().emplace<AttackIntent>(attacker, MakeIntent(20.0f, 0.0f));
    combat.Update(world, kDt);

    CHECK(world.Registry().get<Health>(defender).current ==
          doctest::Approx(93.0f));
    CHECK(world.Registry().get<CombatStamina>(defender).current ==
          doctest::Approx(90.0f));
    CHECK(world.Registry().get<CombatStamina>(defender).regenDelay ==
          doctest::Approx(0.8f));
    CHECK(world.Registry().get<HitReaction>(defender).staggerLeft ==
          doctest::Approx(0.35f));
    CHECK(combat.GetStats().blocks == 1u);
    CHECK(combat.GetStats().hits == 0u);
    CHECK(combat.GetHits().empty());
    CHECK(combat.GetStats().damageDealt == doctest::Approx(7.0f));
    REQUIRE(feedback.size() == 1u);
    CHECK(feedback[0].outcome == CombatSystem::BlockOutcome::Blocked);
    CHECK(feedback[0].defender == defender);
    CHECK(feedback[0].attacker == attacker);

    combat.Update(world, 0.2f);
    CHECK(world.Registry().get<Health>(defender).current ==
          doctest::Approx(93.0f));
}

TEST_CASE("M30-T4：格挡击退保持朝向，恢复后随移动更新") {
    EcsWorld world;
    const auto defender = AddActor(world, 1.0f, 0.0f);
    AddGuard(world, defender);
    world.Registry().emplace<ecs::Velocity>(defender,
                                            ecs::Velocity{3.0f, 0.0f});
    world.Registry().emplace<ecs::AnimState>(defender);

    CombatSystem combat;
    CombatSystem::DamageResult result;
    CHECK(combat.ApplyDamage(world, defender, MakeBlunt(20.0f), &result));
    CHECK(result.blocked);

    characters::AnimationSystem animation;
    animation.Update(world, kDt);
    CHECK(world.Registry().get<Facing>(defender).dir ==
          ecs::Direction::Left);

    auto& reaction = world.Registry().get<HitReaction>(defender);
    reaction.staggerLeft = 0.0f;
    reaction.vx = 0.0f;
    reaction.vy = 0.0f;
    auto& velocity = world.Registry().get<ecs::Velocity>(defender);
    velocity.x = 0.0f;
    velocity.y = 4.0f;
    animation.Update(world, kDt);
    CHECK(world.Registry().get<Facing>(defender).dir ==
          ecs::Direction::Down);
}

TEST_CASE("M30-T2：背向/火焰不格挡，武器覆盖值生效") {
    EcsWorld world;
    CombatSystem combat;

    // 背向：不格挡、不扣体力。
    const auto back = AddActor(world, 1.0f, 0.0f);
    AddGuard(world, back);
    world.Registry().emplace_or_replace<Facing>(back, Facing{ecs::Direction::Right});
    DamageEvent backHit = MakeBlunt(20.0f);
    CombatSystem::DamageResult backResult;
    CHECK(combat.ApplyDamage(world, back, backHit, &backResult));
    CHECK(backResult.applied);
    CHECK_FALSE(backResult.blocked);
    CHECK(world.Registry().get<Health>(back).current == doctest::Approx(80.0f));
    CHECK(world.Registry().get<CombatStamina>(back).current ==
          doctest::Approx(100.0f));

    // Fire 不参与格挡。
    const auto fire = AddActor(world, 1.0f, 2.0f);
    AddGuard(world, fire);
    DamageEvent fireHit = MakeBlunt(20.0f);
    fireHit.type = DamageType::Fire;
    CombatSystem::DamageResult fireResult;
    CHECK(combat.ApplyDamage(world, fire, fireHit, &fireResult));
    CHECK(fireResult.applied);
    CHECK_FALSE(fireResult.blocked);
    CHECK(world.Registry().get<Health>(fire).current == doctest::Approx(80.0f));
    CHECK(world.Registry().get<CombatStamina>(fire).current ==
          doctest::Approx(100.0f));

    // 武器覆盖：65% 减伤、16 点体力消耗。
    const auto covered = AddActor(world, 1.0f, 4.0f);
    AddGuard(world, covered);
    auto& stance = world.Registry().get<CombatStance>(covered);
    stance.blockReduction = 0.65f;
    stance.blockStaminaCost = 16.0f;
    CombatSystem::DamageResult coverResult;
    CHECK(combat.ApplyDamage(world, covered, MakeBlunt(20.0f), &coverResult));
    CHECK(coverResult.blocked);
    CHECK(world.Registry().get<Health>(covered).current ==
          doctest::Approx(93.0f));
    CHECK(world.Registry().get<CombatStamina>(covered).current ==
          doctest::Approx(84.0f));
}

TEST_CASE("M30-T2：弹反零伤害、攻击者硬直与冷却") {
    EcsWorld world;
    const auto attacker = AddActor(world, 0.0f, 0.0f);
    const auto defender = AddActor(world, 1.0f, 0.0f);
    AddGuard(world, defender, 100.0f, 0.05f);
    auto& attack = world.Registry().emplace<AttackIntent>(
        attacker, MakeIntent(20.0f, 0.2f));
    attack.pending = true;

    std::vector<CombatSystem::BlockFeedback> feedback;
    CombatSystem combat;
    combat.SetBlockFeedbackCallback(
        [&](const CombatSystem::BlockFeedback& value) { feedback.push_back(value); });

    DamageEvent hit = MakeBlunt(30.0f);
    hit.sourceId = world.IdOf(attacker).id;
    hit.poiseDamage = 20.0f;
    CombatSystem::DamageResult result;
    CHECK_FALSE(combat.ApplyDamage(world, defender, hit, &result));
    CHECK(result.parried);
    CHECK_FALSE(result.applied);
    CHECK(world.Registry().get<Health>(defender).current ==
          doctest::Approx(100.0f));
    CHECK_FALSE(world.Registry().all_of<ecs::Wounds>(defender));
    CHECK_FALSE(world.Registry().all_of<ecs::LastDamage>(defender));
    CHECK(world.Registry().get<CombatStamina>(defender).current ==
          doctest::Approx(94.0f));
    CHECK(world.Registry().get<CombatStance>(defender).parryCooldown ==
          doctest::Approx(0.7f));
    CHECK(world.Registry().get<HitReaction>(attacker).staggerLeft ==
          doctest::Approx(0.55f));
    CHECK(world.Registry().get<ecs::ActionState>(attacker).action ==
          ecs::ActorAction::Hit);
    CHECK_FALSE(world.Registry().get<AttackIntent>(attacker).pending);
    CHECK(combat.GetStats().parries == 1u);
    REQUIRE(feedback.size() == 1u);
    CHECK(feedback[0].outcome == CombatSystem::BlockOutcome::Parried);

    // 弹反冷却内同一格挡窗口退化为普通格挡。
    CombatSystem::DamageResult blocked;
    CHECK(combat.ApplyDamage(world, defender, MakeBlunt(20.0f), &blocked));
    CHECK(blocked.blocked);
    CHECK(world.Registry().get<Health>(defender).current ==
          doctest::Approx(93.0f));
    CHECK(world.Registry().get<CombatStamina>(defender).current ==
          doctest::Approx(84.0f));
}

TEST_CASE("M30-T2：格挡体力不足破防") {
    EcsWorld world;
    const auto defender = AddActor(world, 1.0f, 0.0f);
    AddGuard(world, defender, 5.0f);
    CombatSystem combat;

    std::vector<CombatSystem::BlockFeedback> feedback;
    combat.SetBlockFeedbackCallback(
        [&](const CombatSystem::BlockFeedback& value) { feedback.push_back(value); });

    DamageEvent hit = MakeBlunt(30.0f);
    CombatSystem::DamageResult result;
    CHECK(combat.ApplyDamage(world, defender, hit, &result));
    CHECK(result.applied);
    CHECK(result.guardBroken);
    CHECK_FALSE(result.blocked);
    CHECK(world.Registry().get<Health>(defender).current ==
          doctest::Approx(70.0f));
    CHECK(world.Registry().get<CombatStamina>(defender).current ==
          doctest::Approx(5.0f));
    const auto& stance = world.Registry().get<CombatStance>(defender);
    CHECK_FALSE(stance.blocking);
    CHECK(stance.guardBreakLeft == doctest::Approx(0.8f));
    CHECK(world.Registry().get<HitReaction>(defender).staggerLeft ==
          doctest::Approx(0.8f));
    CHECK(combat.GetStats().guardBreaks == 1u);
    REQUIRE(feedback.size() == 1u);
    CHECK(feedback[0].outcome == CombatSystem::BlockOutcome::GuardBroken);

    combat.Update(world, 0.2f);
    CHECK(world.Registry().get<CombatStance>(defender).guardBreakLeft ==
          doctest::Approx(0.6f));
    CHECK_FALSE(world.Registry().get<CombatStance>(defender).blocking);
}

TEST_CASE("M30-T2：闪避无敌帧与硬直条不变") {
    EcsWorld world;
    const auto target = AddActor(world, 1.0f, 0.0f);
    CombatStance stance;
    stance.dodgeLeft = 0.28f;
    stance.dodgeInvulnLeft = 0.18f;
    stance.dodgeDirX = 1.0f;
    world.Registry().emplace<CombatStance>(target, stance);
    world.Registry().emplace<Poise>(target, Poise{20.0f, 20.0f, 0.0f});

    std::vector<CombatSystem::BlockFeedback> feedback;
    CombatSystem combat;
    combat.SetBlockFeedbackCallback(
        [&](const CombatSystem::BlockFeedback& value) { feedback.push_back(value); });

    DamageEvent hit = MakeBlunt(20.0f);
    hit.poiseDamage = 10.0f;
    CombatSystem::DamageResult result;
    CHECK_FALSE(combat.ApplyDamage(world, target, hit, &result));
    CHECK(result.dodged);
    CHECK(world.Registry().get<Health>(target).current == doctest::Approx(100.0f));
    CHECK(world.Registry().get<Poise>(target).current == doctest::Approx(20.0f));
    CHECK_FALSE(world.Registry().all_of<ecs::Wounds>(target));
    CHECK_FALSE(world.Registry().all_of<HitReaction>(target));
    CHECK(combat.GetStats().dodges == 1u);
    REQUIRE(feedback.size() == 1u);
    CHECK(feedback[0].outcome == CombatSystem::BlockOutcome::Dodged);

    combat.Update(world, 0.1f);
    CHECK(world.Registry().get<CombatStance>(target).dodgeInvulnLeft ==
          doctest::Approx(0.08f));
    CHECK_FALSE(combat.ApplyDamage(world, target, hit));

    combat.Update(world, 0.1f);
    CHECK(world.Registry().get<CombatStance>(target).dodgeInvulnLeft == 0.0f);
    CHECK(combat.ApplyDamage(world, target, hit));
    CHECK(world.Registry().get<Health>(target).current == doctest::Approx(80.0f));
    CHECK(world.Registry().get<Poise>(target).current == doctest::Approx(10.0f));
}

TEST_CASE("M30-T2：硬直条扣除、破防与延迟回复") {
    EcsWorld world;
    const auto target = AddActor(world, 1.0f, 0.0f);
    world.Registry().emplace<Poise>(target, Poise{10.0f, 20.0f, 0.0f});

    CombatSystem combat;
    DamageEvent hit = MakeBlunt(5.0f);
    hit.poiseDamage = 11.0f;
    CombatSystem::DamageResult result;
    CHECK(combat.ApplyDamage(world, target, hit, &result));
    CHECK(result.applied);
    CHECK(world.Registry().get<Health>(target).current == doctest::Approx(95.0f));
    CHECK(world.Registry().get<Poise>(target).current == 0.0f);
    CHECK(world.Registry().get<Poise>(target).regenDelay ==
          doctest::Approx(1.2f));
    CHECK(world.Registry().get<HitReaction>(target).staggerLeft ==
          doctest::Approx(0.55f));
    CHECK(world.Registry().get<ecs::ActionState>(target).duration ==
          doctest::Approx(0.55f));
    CHECK(combat.GetStats().poiseBreaks == 1u);

    combat.Update(world, 0.5f);
    CHECK(world.Registry().get<Poise>(target).regenDelay ==
          doctest::Approx(0.7f));
    CHECK(world.Registry().get<Poise>(target).current == 0.0f);

    combat.Update(world, 0.7f);
    CHECK(world.Registry().get<Poise>(target).regenDelay == doctest::Approx(0.0f));
    CHECK(world.Registry().get<Poise>(target).current == 0.0f);

    combat.Update(world, 0.1f);
    CHECK(world.Registry().get<Poise>(target).current == doctest::Approx(1.0f));
}

TEST_CASE("M30-T5：重甲轻击不硬直，重击破防并触发命中反馈") {
    EcsWorld world;
    const auto attacker = AddActor(world, 0.0f, 0.0f);
    const auto target = AddActor(world, 1.0f, 0.0f, 300.0f);
    world.Registry().emplace<ecs::ZombieVariant>(
        target,
        ecs::ZombieVariant{ecs::ZombieVariantKind::Armored, 0.62f, 1.42f,
                           1.22f});
    world.Registry().emplace<Poise>(target, Poise{55.0f, 55.0f, 0.0f});
    world.Registry().emplace<ecs::SpriteRef>(
        target, ecs::SpriteRef{1, 0, 0xFFFFFFFFu});

    CombatSystem combat;
    std::vector<CombatSystem::HitFeedback> feedback;
    combat.SetHitFeedbackCallback(
        [&](const CombatSystem::HitFeedback& value) {
            feedback.push_back(value);
        });

    AttackIntent light = MakeIntent(10.0f);
    light.poiseDamage = 11.0f;
    light.staggerSeconds = 0.45f;
    world.Registry().emplace<AttackIntent>(attacker, light);
    combat.Update(world, 0.0f);
    CHECK(world.Registry().get<Poise>(target).current == doctest::Approx(44.0f));
    CHECK(world.Registry().get<HitReaction>(target).staggerLeft ==
          doctest::Approx(0.0f));
    CHECK(world.Registry().get<ecs::ActionState>(target).duration ==
          doctest::Approx(0.22f));
    CHECK(world.Registry().get<ecs::SpriteRef>(target).tint != 0xFFFFFFFFu);
    REQUIRE(feedback.size() == 1u);
    CHECK_FALSE(feedback[0].poiseBroken);
    feedback.clear();

    // 重甲只对 <16 硬直条伤害免疫基础硬直；重击达到阈值后正常打断。
    world.Registry().get<Poise>(target).current = 10.0f;
    AttackIntent heavy = MakeIntent(10.0f);
    heavy.poiseDamage = 38.0f;
    heavy.staggerSeconds = 0.60f;
    world.Registry().emplace_or_replace<AttackIntent>(attacker, heavy);
    combat.Update(world, 0.0f);
    CHECK(world.Registry().get<Poise>(target).current == doctest::Approx(0.0f));
    CHECK(world.Registry().get<Poise>(target).regenDelay == doctest::Approx(1.2f));
    CHECK(world.Registry().get<HitReaction>(target).staggerLeft ==
          doctest::Approx(0.60f));
    CHECK(world.Registry().get<ecs::ActionState>(target).duration ==
          doctest::Approx(0.60f));
    CHECK(combat.GetStats().poiseBreaks == 1u);
    REQUIRE(combat.GetHits().size() == 1u);
    CHECK(combat.GetHits()[0].poiseBroken);
    REQUIRE(feedback.size() == 1u);
    CHECK(feedback[0].target == target);
    CHECK(feedback[0].damage == doctest::Approx(6.5f));
    CHECK(feedback[0].poiseBroken);
    CHECK_FALSE(feedback[0].lethal);

    // 硬直条持续为 0 时再次命中不重复计破防，也不刷新 0.55s 长硬直。
    world.Registry().get<HitReaction>(target).staggerLeft = 0.0f;
    DamageEvent tap = MakeBlunt(1.0f);
    tap.poiseDamage = 20.0f;
    tap.staggerSeconds = 0.1f;
    CombatSystem::DamageResult tapResult;
    CHECK(combat.ApplyDamage(world, target, tap, &tapResult));
    CHECK_FALSE(tapResult.poiseBroken);
    CHECK(combat.GetStats().poiseBreaks == 1u);
    CHECK(world.Registry().get<HitReaction>(target).staggerLeft ==
          doctest::Approx(0.1f));
}

TEST_CASE("M30-T2：体力消耗、疲劳上限、耗尽锁定与 NaN") {
    EcsWorld world;
    const auto entity = AddActor(world, 0.0f, 0.0f);
    world.Registry().emplace<CombatStamina>(
        entity, CombatStamina{100.0f, 100.0f, 0.0f, 0.0f});

    CombatSystem combat;
    CHECK(combat.TrySpendStamina(world, entity, 20.0f));
    CHECK(world.Registry().get<CombatStamina>(entity).current ==
          doctest::Approx(80.0f));
    CHECK(world.Registry().get<CombatStamina>(entity).regenDelay ==
          doctest::Approx(0.8f));

    combat.Update(world, 0.5f);
    CHECK(world.Registry().get<CombatStamina>(entity).current ==
          doctest::Approx(80.0f));
    CHECK(world.Registry().get<CombatStamina>(entity).regenDelay ==
          doctest::Approx(0.3f));

    combat.Update(world, 0.4f);
    CHECK(world.Registry().get<CombatStamina>(entity).current ==
          doctest::Approx(81.8f));

    // 耗尽锁定：0.6 秒内拒绝再次消耗，锁定结束后才恢复。
    auto& stamina = world.Registry().get<CombatStamina>(entity);
    stamina.current = 7.0f;
    stamina.regenDelay = 0.0f;
    stamina.exhausted = 0.0f;
    CHECK(combat.TrySpendStamina(world, entity, 2.0f));
    CHECK(stamina.current == doctest::Approx(5.0f));
    CHECK(stamina.exhausted == doctest::Approx(0.6f));
    CHECK_FALSE(combat.TrySpendStamina(world, entity, 1.0f));
    combat.Update(world, 0.5f);
    CHECK(stamina.current == doctest::Approx(5.0f));
    CHECK(stamina.exhausted == doctest::Approx(0.1f));
    combat.Update(world, 0.2f);
    CHECK(stamina.exhausted == doctest::Approx(0.0f));
    CHECK(stamina.current == doctest::Approx(5.0f));
    combat.Update(world, 0.2f);
    CHECK(stamina.current > 5.0f);

    // 疲劳同时降低有效上限和回复速度。
    const auto tired = AddActor(world, 2.0f, 0.0f);
    world.Registry().emplace<CombatStamina>(
        tired, CombatStamina{100.0f, 100.0f, 0.0f, 0.0f});
    ecs::Needs needs;
    needs.fatigue = 0.5f;
    world.Registry().emplace<ecs::Needs>(tired, needs);
    combat.Update(world, 0.1f);
    CHECK(world.Registry().get<CombatStamina>(tired).current ==
          doctest::Approx(85.0f));

    // NaN 回退为有限值，不产生 NaN 体力。
    const auto broken = AddActor(world, 3.0f, 0.0f);
    world.Registry().emplace<CombatStamina>(
        broken, CombatStamina{kNaN, kNaN, kNaN, kNaN});
    combat.Update(world, 0.1f);
    CHECK(std::isfinite(world.Registry().get<CombatStamina>(broken).current));
    CHECK(world.Registry().get<CombatStamina>(broken).max ==
          doctest::Approx(100.0f));
    CHECK_FALSE(combat.TrySpendStamina(world, broken, kNaN));
}

TEST_CASE("M30-T2：NaN 攻击/伤害参数被清洗") {
    EcsWorld world;
    const auto attacker = AddActor(world, 0.0f, 0.0f);
    const auto target = AddActor(world, 1.0f, 0.0f);
    AttackIntent attack = MakeIntent(10.0f, 0.0f);
    attack.dirX = kNaN;
    attack.dirY = kNaN;
    attack.damage = kNaN;
    attack.range = kNaN;
    attack.arcDegrees = kNaN;
    attack.windupSeconds = kNaN;
    attack.windupLeft = kNaN;
    attack.pending = true;
    world.Registry().emplace<AttackIntent>(attacker, attack);

    CombatSystem combat;
    combat.Update(world, 0.1f);
    CHECK(world.Registry().get<Health>(target).current == doctest::Approx(100.0f));
    CHECK_FALSE(world.Registry().get<AttackIntent>(attacker).pending);
    CHECK(world.Registry().get<AttackIntent>(attacker).windupLeft == 0.0f);

    DamageEvent event = MakeBlunt(kNaN);
    event.dirX = kNaN;
    event.knockback = kNaN;
    event.staggerSeconds = kNaN;
    event.poiseDamage = kNaN;
    CombatSystem::DamageResult result;
    CHECK(combat.ApplyDamage(world, target, event, &result));
    CHECK(result.applied);
    CHECK(world.Registry().get<Health>(target).current == doctest::Approx(100.0f));
    CHECK(std::isfinite(world.Registry().get<HitReaction>(target).staggerLeft));
    CHECK(std::isfinite(world.Registry().get<HitReaction>(target).vx));
}

TEST_CASE("M30-T4：格挡减速与闪避覆盖 MoveIntent") {
    EcsWorld world;
    const auto entity = AddActor(world, 0.0f, 0.0f);
    world.Registry().emplace<ecs::MoveIntent>(entity,
                                              ecs::MoveIntent{4.0f, 0.0f});
    CombatStance stance;
    stance.blocking = true;
    world.Registry().emplace<CombatStance>(entity, stance);

    CombatSystem combat;
    combat.Update(world, 1.0f / 60.0f);
    const auto& blockIntent = world.Registry().get<ecs::MoveIntent>(entity);
    CHECK(blockIntent.x == doctest::Approx(4.0f * characters::BlockMoveScale(
                                                  combat.GetCombatRules())));
    CHECK(blockIntent.y == doctest::Approx(0.0f));
    CHECK(world.Registry().get<CombatStance>(entity).blockElapsed ==
          doctest::Approx(1.0f / 60.0f));

    // 格挡减速不能再次缩放受击击退：硬直接管后应保留击退速度。
    auto& blockIntentMutable = world.Registry().get<ecs::MoveIntent>(entity);
    blockIntentMutable.x = 4.0f;
    blockIntentMutable.y = 0.0f;
    ecs::HitReaction reaction;
    reaction.staggerLeft = 0.5f;
    reaction.vx = 3.0f;
    world.Registry().emplace<ecs::HitReaction>(entity, reaction);
    combat.Update(world, 0.01f);
    CHECK(world.Registry().get<ecs::MoveIntent>(entity).x ==
          doctest::Approx(3.0f));
    world.Registry().remove<ecs::HitReaction>(entity);

    auto& dodging = world.Registry().get<CombatStance>(entity);
    dodging.blocking = false;
    dodging.blockElapsed = 0.0f;
    dodging.dodgeLeft = characters::DodgeDurationSeconds(combat.GetCombatRules());
    dodging.dodgeInvulnLeft = characters::DodgeInvulnSeconds(combat.GetCombatRules());
    dodging.dodgeDirX = 0.6f;
    dodging.dodgeDirY = 0.8f;
    auto& dodgeIntent = world.Registry().get<ecs::MoveIntent>(entity);
    dodgeIntent.x = 0.0f;
    dodgeIntent.y = 0.0f;

    combat.Update(world, 0.01f);
    const float speed = characters::DodgeSpeed(combat.GetCombatRules());
    CHECK(world.Registry().get<ecs::MoveIntent>(entity).x ==
          doctest::Approx(0.6f * speed));
    CHECK(world.Registry().get<ecs::MoveIntent>(entity).y ==
          doctest::Approx(0.8f * speed));
    CHECK(world.Registry().get<CombatStance>(entity).dodgeLeft ==
          doctest::Approx(characters::DodgeDurationSeconds(
                              combat.GetCombatRules()) -
                          0.01f));

    // 闪避后半段受击：硬直/击退接管，不被闪避位移覆盖。
    auto& lateDodge = world.Registry().get<CombatStance>(entity);
    lateDodge.dodgeLeft = 0.1f;
    world.Registry().get<ecs::MoveIntent>(entity).x = 0.0f;
    world.Registry().get<ecs::MoveIntent>(entity).y = 0.0f;
    ecs::HitReaction lateHit;
    lateHit.staggerLeft = 0.4f;
    lateHit.vx = 2.0f;
    world.Registry().emplace<ecs::HitReaction>(entity, lateHit);
    combat.Update(world, 0.01f);
    CHECK(world.Registry().get<ecs::MoveIntent>(entity).x ==
          doctest::Approx(2.0f));
}
