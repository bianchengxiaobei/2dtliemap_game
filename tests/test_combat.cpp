#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <type_traits>

#include "ai/components/AiComponents.h"
#include "ai/systems/AiSystem.h"
#include "ai/systems/PerceptionSystem.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/systems/CombatSystem.h"
#include "characters/systems/DeathSystem.h"
#include "characters/systems/MovementSystem.h"
#include "characters/systems/PainSystem.h"
#include "characters/systems/WoundSystem.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/PainStatus.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"

using namespace zl;

using zl::characters::CombatSystem;
using zl::characters::DeathSystem;
using zl::ecs::DamageEvent;
using zl::ecs::DamageType;
using zl::ecs::EcsWorld;
using zl::ecs::Health;
using zl::ecs::HitReaction;
using zl::ecs::Position;
using zl::ecs::SpriteRef;
using zl::ecs::Velocity;

namespace {

constexpr float kDt = 1.0f / 60.0f;

// 受击闪红 tint / 恢复值（与 CombatSystem.cpp 约定一致）
constexpr std::uint32_t kFlashTint = 0xFF6060FFu;
constexpr std::uint32_t kWhiteTint = 0xFFFFFFFFu;

}  // namespace

TEST_CASE("战斗组件：默认值与平凡可拷贝（M5-T6 定稿）") {
    const DamageEvent event;
    CHECK(event.sourceId == 0u);
    CHECK(event.type == DamageType::Blunt);
    CHECK(event.amount == 0.0f);
    CHECK(event.knockback == 0.0f);

    const ecs::AttackIntent attack;
    CHECK(attack.targetId == 0u);
    CHECK(attack.dirX == 1.0f);
    CHECK(attack.range == 1.0f);
    CHECK(attack.arcDegrees == 100.0f);
    CHECK(attack.staggerSeconds == 0.35f);
    CHECK_FALSE(attack.pending);

    const HitReaction hit;
    CHECK(hit.staggerLeft == 0.0f);
    CHECK(hit.vx == 0.0f);

    const ecs::Dying dying;
    CHECK(dying.remaining == 0.5f);

    static_assert(std::is_trivially_copyable_v<DamageEvent>);
    static_assert(std::is_trivially_copyable_v<ecs::AttackIntent>);
    static_assert(std::is_trivially_copyable_v<HitReaction>);
    static_assert(std::is_trivially_copyable_v<ecs::DamageResist>);
    static_assert(std::is_trivially_copyable_v<ecs::Dying>);
}

TEST_CASE("CombatSystem：扇形判定（锥内命中 / 锥外豁免 / 距离外豁免）+ pending 单次消费") {
    EcsWorld world;
    entt::registry& registry = world.Registry();

    // 攻击者：位置 (0,0)，朝 +x，扇形 90°，触及 1.5 格
    const auto attacker = world.Create();
    registry.emplace<Position>(attacker, Position{0.0f, 0.0f, 0});
    ecs::AttackIntent intent;
    intent.dirX = 1.0f;
    intent.dirY = 0.0f;
    intent.range = 1.5f;
    intent.arcDegrees = 90.0f;
    intent.damage = 10.0f;
    intent.knockback = 0.0f;
    intent.staggerSeconds = 0.2f;
    intent.pending = true;
    registry.emplace<ecs::AttackIntent>(attacker, intent);

    const auto addTarget = [&](float x, float y) {
        const auto e = world.Create();
        registry.emplace<Position>(e, Position{x, y, 0});
        registry.emplace<Health>(e, Health{100.0f, 100.0f});
        return e;
    };
    const auto inCone = addTarget(1.4f, 0.0f);   // 锥内 + 范围内 → 命中
    const auto side = addTarget(0.0f, 1.2f);     // 偏轴 90° → 锥外
    const auto diag = addTarget(1.2f, 1.2f);     // 偏轴 45°（锥内边界）但距离 1.70 → 距离外
    const auto beyond = addTarget(2.0f, 0.0f);   // 轴线上但超距

    CombatSystem combat;
    combat.Update(world, kDt);
    CHECK(registry.get<Health>(inCone).current == doctest::Approx(90.0f));
    CHECK(registry.get<Health>(side).current == doctest::Approx(100.0f));
    CHECK(registry.get<Health>(diag).current == doctest::Approx(100.0f));
    CHECK(registry.get<Health>(beyond).current == doctest::Approx(100.0f));
    CHECK(combat.GetStats().swings == 1u);
    CHECK(combat.GetStats().hits == 1u);

    // pending 已消费：同 tick 不重复，后续 tick 不重复
    combat.Update(world, kDt);
    CHECK(registry.get<Health>(inCone).current == doctest::Approx(90.0f));
    CHECK(combat.GetStats().swings == 1u);
    CHECK(combat.GetStats().hits == 1u);
}

TEST_CASE("CombatSystem：伤害管线（来源/类型解耦）+ 硬直/击退/闪红衰减与恢复 + 致死不重复结算") {
    EcsWorld world;
    entt::registry& registry = world.Registry();
    const auto e = world.Create();
    registry.emplace<Position>(e, Position{0.0f, 0.0f, 0});
    registry.emplace<Health>(e, Health{100.0f, 100.0f});
    registry.emplace<ecs::MoveIntent>(e, ecs::MoveIntent{});
    registry.emplace<SpriteRef>(e, SpriteRef{ecs::HashName("character.placeholder"), 0, kWhiteTint});

    CombatSystem combat;
    DamageEvent event;
    event.sourceId = 42;
    event.type = DamageType::Bite;
    event.amount = 30.0f;
    event.dirX = 1.0f;
    event.dirY = 0.0f;
    event.knockback = 3.0f;
    event.staggerSeconds = 0.4f;
    CHECK(combat.ApplyDamage(world, e, event));

    // Health 只认数值；来源/类型留在事件里（管线解耦）
    CHECK(registry.get<Health>(e).current == doctest::Approx(70.0f));
    const auto& hit = registry.get<HitReaction>(e);
    CHECK(hit.staggerLeft == doctest::Approx(0.4f));
    CHECK(hit.vx == doctest::Approx(3.0f));
    CHECK(hit.vy == doctest::Approx(0.0f));
    CHECK(hit.flashLeft > 0.0f);
    CHECK(registry.get<SpriteRef>(e).tint == kFlashTint);
    CHECK(combat.GetStats().damageDealt == doctest::Approx(30.0f));
    CHECK(combat.GetStats().kills == 0u);

    // 硬直接管移动：MoveIntent = 击退速度（先写后衰减）
    combat.Update(world, 0.1f);
    CHECK(registry.get<ecs::MoveIntent>(e).x == doctest::Approx(3.0f));
    CHECK(registry.get<HitReaction>(e).staggerLeft == doctest::Approx(0.3f));
    CHECK(registry.get<HitReaction>(e).vx == doctest::Approx(1.2f));  // 1 - 0.1×6 = 0.4 倍衰减
    combat.Update(world, 0.1f);
    CHECK(registry.get<ecs::MoveIntent>(e).x == doctest::Approx(1.2f));

    // 硬直结束（0.4s / 0.1s/tick = 4 tick；多跑 1 tick 兜底浮点）：不再接管移动/恢复 tint
    for (int i = 0; i < 3; ++i) {
        combat.Update(world, 0.1f);
    }
    CHECK(registry.get<HitReaction>(e).staggerLeft == doctest::Approx(0.0f));
    CHECK(registry.get<SpriteRef>(e).tint == kWhiteTint);  // 闪红（0.15s）已恢复
    registry.get<ecs::MoveIntent>(e).x = 9.0f;             // 哨兵：不再被接管
    combat.Update(world, 0.1f);
    CHECK(registry.get<ecs::MoveIntent>(e).x == doctest::Approx(9.0f));
    CHECK(combat.GetStats().staggered == 0);

    // 致死：HP 扣穿 → kills 计数；再次结算被拒绝（倒地目标不重复结算）
    DamageEvent lethal;
    lethal.sourceId = 7;
    lethal.type = DamageType::Debug;
    lethal.amount = 999.0f;
    CHECK(combat.ApplyDamage(world, e, lethal));
    CHECK(registry.get<Health>(e).current <= 0.0f);
    CHECK(combat.GetStats().kills == 1u);
    CHECK_FALSE(combat.ApplyDamage(world, e, lethal));
    CHECK(combat.GetStats().kills == 1u);
    CHECK(combat.GetStats().damageDealt == doctest::Approx(30.0f + 999.0f));
}

TEST_CASE("M19-F4 护甲按伤害类型减伤、写入实际伤害并通知磨损") {
    EcsWorld world;
    entt::registry& registry = world.Registry();
    const auto target = world.Create();
    registry.emplace<Position>(target, Position{0.0f, 0.0f, 0});
    registry.emplace<Health>(target, Health{100.0f, 100.0f});
    ecs::DamageResist resist;
    resist.clawReduction = 0.5f;
    resist.biteReduction = 0.25f;
    resist.bluntReduction = 0.1f;
    resist.wearPerHit = 0.02f;
    resist.moveScale = 0.9f;
    registry.emplace<ecs::DamageResist>(target, resist);

    int wearCount = 0;
    float wearAmount = 0.0f;
    CombatSystem combat;
    combat.SetArmorWearCallback([&](ecs::Entity entity, float amount) {
        CHECK(entity == target);
        ++wearCount;
        wearAmount = amount;
    });

    DamageEvent event;
    event.sourceId = 7;
    event.type = DamageType::Bite;
    event.amount = 20.0f;
    REQUIRE(combat.ApplyDamage(world, target, event));
    CHECK(registry.get<Health>(target).current == doctest::Approx(85.0f));
    CHECK(registry.get<ecs::LastDamage>(target).amount == doctest::Approx(15.0f));
    CHECK(wearCount == 1);
    CHECK(wearAmount == doctest::Approx(0.02f));
    REQUIRE(registry.get<ecs::Wounds>(target).count == 1);
    CHECK(registry.get<ecs::Wounds>(target).entries[0].severity ==
          doctest::Approx(0.48f));

    event.type = DamageType::Claw;
    REQUIRE(combat.ApplyDamage(world, target, event));
    CHECK(registry.get<Health>(target).current == doctest::Approx(75.0f));
    CHECK(wearCount == 2);

    event.type = DamageType::Debug;
    event.amount = 10.0f;
    REQUIRE(combat.ApplyDamage(world, target, event));
    CHECK(registry.get<Health>(target).current == doctest::Approx(65.0f));
    CHECK(wearCount == 2);  // 环境/调试伤害不吃护甲

    event.type = DamageType::Blunt;
    event.amount = 20.0f;
    REQUIRE(combat.ApplyDamage(world, target, event));
    CHECK(registry.get<Health>(target).current == doctest::Approx(47.0f));
    CHECK(wearCount == 3);
}

TEST_CASE("M25-T4-V9 驾驶位对爪/咬获得车辆减伤，火焰/钝击不受影响") {
    EcsWorld world;
    entt::registry& registry = world.Registry();
    const auto driver = world.Create();
    registry.emplace<Position>(driver, Position{0.0f, 0.0f, 0});
    registry.emplace<Health>(driver, Health{100.0f, 100.0f});
    registry.emplace<ecs::VehicleOccupant>(driver,
                                           ecs::VehicleOccupant{0.75f});

    CombatSystem combat;
    DamageEvent event;
    event.sourceId = 9;
    event.type = DamageType::Claw;
    event.amount = 40.0f;
    REQUIRE(combat.ApplyDamage(world, driver, event));
    CHECK(registry.get<Health>(driver).current == doctest::Approx(90.0f));
    CHECK(registry.get<ecs::LastDamage>(driver).amount == doctest::Approx(10.0f));

    event.type = DamageType::Bite;
    REQUIRE(combat.ApplyDamage(world, driver, event));
    CHECK(registry.get<Health>(driver).current == doctest::Approx(80.0f));

    event.type = DamageType::Blunt;
    REQUIRE(combat.ApplyDamage(world, driver, event));
    CHECK(registry.get<Health>(driver).current == doctest::Approx(40.0f));

    event.type = DamageType::Fire;
    REQUIRE(combat.ApplyDamage(world, driver, event));
    CHECK(registry.get<Health>(driver).current == doctest::Approx(0.0f));
}

TEST_CASE("M23-F6-T3 军械守卫减伤并抗击退，火焰/调试伤害不受护甲影响") {
    EcsWorld world;
    entt::registry& registry = world.Registry();
    const auto guard = world.Create();
    registry.emplace<Position>(guard, Position{0.0f, 0.0f, 0});
    registry.emplace<Health>(guard, Health{100.0f, 100.0f});
    registry.emplace<ecs::ZombieVariant>(
        guard, ecs::ZombieVariant{ecs::ZombieVariantKind::Armored, 0.62f, 1.42f,
                                  1.22f});

    CombatSystem combat;
    DamageEvent event;
    event.sourceId = 7;
    event.type = DamageType::Claw;
    event.amount = 20.0f;
    event.dirX = 1.0f;
    event.knockback = 4.0f;
    REQUIRE(combat.ApplyDamage(world, guard, event));
    CHECK(registry.get<Health>(guard).current == doctest::Approx(87.0f));
    CHECK(registry.get<ecs::LastDamage>(guard).amount == doctest::Approx(13.0f));
    CHECK(registry.get<ecs::HitReaction>(guard).vx ==
          doctest::Approx(4.0f * ecs::kArmoredZombieKnockbackScale));

    event.type = DamageType::Bite;
    REQUIRE(combat.ApplyDamage(world, guard, event));
    CHECK(registry.get<Health>(guard).current == doctest::Approx(74.0f));

    event.type = DamageType::Fire;
    event.amount = 10.0f;
    event.knockback = 0.0f;
    REQUIRE(combat.ApplyDamage(world, guard, event));
    CHECK(registry.get<Health>(guard).current == doctest::Approx(64.0f));

    event.type = DamageType::Debug;
    REQUIRE(combat.ApplyDamage(world, guard, event));
    CHECK(registry.get<Health>(guard).current == doctest::Approx(54.0f));
}

TEST_CASE("M19-F4 护甲移速乘数叠加到玩家的 MovementSystem") {
    EcsWorld world;
    entt::registry& registry = world.Registry();
    const auto player = world.Create();
    registry.emplace<Position>(player, Position{0.0f, 0.0f, 0});
    registry.emplace<Velocity>(player, Velocity{});
    registry.emplace<ecs::MoveIntent>(player, ecs::MoveIntent{4.0f, 0.0f});
    registry.emplace<ecs::PlayerTag>(player);
    ecs::DamageResist resist;
    resist.moveScale = 0.5f;
    registry.emplace<ecs::DamageResist>(player, resist);

    zl::characters::MovementSystem movement;
    movement.Update(world, 0.5f);
    CHECK(registry.get<Velocity>(player).x == doctest::Approx(2.0f));
    CHECK(registry.get<Position>(player).x == doctest::Approx(1.0f));
}

TEST_CASE("AiSystem：攻击脉冲落下 AttackIntent（指向目标）；硬直中冻结状态机（不再挥击）") {
    EcsWorld world;
    entt::registry& registry = world.Registry();

    const auto target = world.Create();
    registry.emplace<Position>(target, Position{5.6f, 5.0f, 0});
    registry.emplace<Health>(target, Health{100.0f, 100.0f});
    const std::uint64_t targetId = world.IdOf(target).id;

    const auto zombie = world.Create();
    registry.emplace<Position>(zombie, Position{5.0f, 5.0f, 0});
    registry.emplace<Health>(zombie, Health{100.0f, 100.0f});
    ecs::AiState ai;
    ai.status = ecs::AiStatus::Attack;
    ai.targetId = targetId;
    registry.emplace<ecs::AiState>(zombie, ai);
    ecs::PerceptionState percep;
    percep.lastKnownX = 5.6f;
    percep.lastKnownY = 5.0f;
    registry.emplace<ecs::PerceptionState>(zombie, percep);
    registry.emplace<ecs::MoveIntent>(zombie, ecs::MoveIntent{});
    registry.emplace<ecs::PathRequest>(zombie, ecs::PathRequest{});
    registry.emplace<ecs::PathFollow>(zombie, ecs::PathFollow{});
    registry.emplace<ecs::AttackIntent>(zombie, ecs::AttackIntent{});

    ai::AiSystem::AttackParams params;
    params.normal.damage = 8.0f;
    params.normal.knockback = 2.0f;
    params.normal.staggerSeconds = 0.3f;
    params.normal.range = 1.3f;
    params.normal.arcDegrees = 120.0f;
    params.normal.windupSeconds = 0.1f;
    params.normal.type = DamageType::Bite;
    ai::AiSystem system([](int, int) { return true; }, params);

    system.Update(world, kDt);  // 进入前摇：不立即落伤害
    CHECK_FALSE(registry.get<ecs::AttackIntent>(zombie).pending);
    CHECK(registry.get<ecs::AiState>(zombie).attackWindupLeft > 0.0f);
    for (int i = 0; i < 8; ++i) {
        system.Update(world, kDt);
    }
    const auto& attack = registry.get<ecs::AttackIntent>(zombie);
    CHECK(attack.pending);
    CHECK(attack.targetId == targetId);
    CHECK(attack.dirX == doctest::Approx(1.0f));
    CHECK(attack.dirY == doctest::Approx(0.0f));
    CHECK(attack.damage == doctest::Approx(8.0f));
    CHECK(attack.type == DamageType::Bite);
    CHECK(registry.get<ecs::AiState>(zombie).attackPulses == 1u);

    // 硬直：冻结转移/计时/动作（清掉 pending 后跑 30 tick 不应再落脉冲，stateTime 不涨）
    registry.get<ecs::AttackIntent>(zombie).pending = false;
    const float stateTimeBefore = registry.get<ecs::AiState>(zombie).stateTime;
    registry.emplace<HitReaction>(zombie, HitReaction{1.0f, 0.0f, 0.0f, 0.0f});
    for (int i = 0; i < 30; ++i) {
        system.Update(world, kDt);
    }
    CHECK(registry.get<ecs::AiState>(zombie).attackPulses == 1u);
    CHECK(registry.get<ecs::AiState>(zombie).stateTime == doctest::Approx(stateTimeBefore));
    CHECK_FALSE(registry.get<ecs::AttackIntent>(zombie).pending);
}

TEST_CASE("M5-T6 全链路：挥击命中 → 硬直击退 → 致死倒地（Dying）→ 尸体；倒地目标不再被命中") {
    EcsWorld world;
    entt::registry& registry = world.Registry();

    // 玩家式攻击者（无 AiState → 可伤害僵尸；自动瞄准方向 +x）
    const auto attacker = world.Create();
    registry.emplace<Position>(attacker, Position{0.0f, 0.0f, 0});
    ecs::AttackIntent attack;
    attack.dirX = 1.0f;
    attack.dirY = 0.0f;
    attack.range = 1.7f;
    attack.arcDegrees = 100.0f;
    attack.damage = 40.0f;
    attack.knockback = 3.0f;
    attack.staggerSeconds = 0.35f;
    registry.emplace<ecs::AttackIntent>(attacker, attack);

    // 僵尸：1 格距离、100 HP（3 刀 40 伤害致死）
    const auto zombie = world.Create();
    const std::uint64_t zombieId = world.IdOf(zombie).id;
    registry.emplace<Position>(zombie, Position{1.0f, 0.0f, 0});
    registry.emplace<Health>(zombie, Health{100.0f, 100.0f});
    registry.emplace<Velocity>(zombie, Velocity{});
    registry.emplace<ecs::MoveIntent>(zombie, ecs::MoveIntent{});
    registry.emplace<SpriteRef>(
        zombie, SpriteRef{ecs::HashName("character.placeholder"), 0, kWhiteTint});
    ecs::AiState ai;
    ai.status = ecs::AiStatus::Chase;
    registry.emplace<ecs::AiState>(zombie, ai);

    CombatSystem combat;
    DeathSystem death(nullptr, 0.5f);

    // 第 1 刀：命中 + 硬直 + 击退接管
    registry.get<ecs::AttackIntent>(attacker).pending = true;
    combat.Update(world, kDt);
    CHECK(registry.get<Health>(zombie).current == doctest::Approx(60.0f));
    CHECK(registry.get<HitReaction>(zombie).staggerLeft > 0.0f);
    CHECK(registry.get<ecs::MoveIntent>(zombie).x > 0.0f);  // 击退（+x 方向）
    CHECK(combat.GetStats().hits == 1u);

    // 第 2、3 刀：第 3 刀致死
    registry.get<ecs::AttackIntent>(attacker).pending = true;
    combat.Update(world, kDt);
    CHECK(registry.get<Health>(zombie).current == doctest::Approx(20.0f));
    registry.get<ecs::AttackIntent>(attacker).pending = true;
    combat.Update(world, kDt);
    CHECK(registry.get<Health>(zombie).current <= 0.0f);
    CHECK(combat.GetStats().hits == 3u);
    CHECK(combat.GetStats().kills == 1u);

    // 倒地窗口：第 1 次 DeathSystem → Dying（实体还在；速度冻结 + 播放 death 片段）
    death.Update(world, kDt);
    CHECK(registry.all_of<ecs::Dying>(zombie));
    CHECK(world.Alive(zombie));
    CHECK(registry.get<Velocity>(zombie).x == doctest::Approx(0.0f));
    CHECK(registry.get<SpriteRef>(zombie).sprite == ecs::HashName("character.placeholder"));

    // 倒地中不再被挥击命中（候选 = HP>0）
    registry.get<ecs::AttackIntent>(attacker).pending = true;
    combat.Update(world, kDt);
    CHECK(combat.GetStats().hits == 3u);
    CHECK(combat.GetStats().swings == 4u);  // 挥了但空

    // 推进倒地窗口（0.5s）→ 转尸体 + 回收
    for (int i = 0; i < 40; ++i) {
        death.Update(world, kDt);
    }
    CHECK_FALSE(world.Alive(zombie));  // 已入销毁队列
    world.Flush();
    CHECK_FALSE(registry.valid(zombie));
    CHECK(registry.view<ecs::Corpse>().size() == 1u);
    registry.view<ecs::Corpse>().each([&](const entt::entity e, const ecs::Corpse& corpse) {
        CHECK(corpse.sourceId == zombieId);
        const auto* pos = registry.try_get<Position>(e);
        REQUIRE(pos != nullptr);
        CHECK(pos->x == doctest::Approx(1.0f));
        CHECK(pos->y == doctest::Approx(0.0f));
    });
}

TEST_CASE("僵尸不误伤同类：AI 攻击者的扇形命中集合排除带 AiState 的候选（玩家正常受击）") {
    EcsWorld world;
    entt::registry& registry = world.Registry();

    const auto biter = world.Create();  // 有 AiState：攻击者
    registry.emplace<Position>(biter, Position{0.0f, 0.0f, 0});
    ecs::AiState biterAi;
    biterAi.status = ecs::AiStatus::Attack;
    registry.emplace<ecs::AiState>(biter, biterAi);
    ecs::AttackIntent intent;
    intent.dirX = 1.0f;
    intent.dirY = 0.0f;
    intent.range = 1.3f;
    intent.arcDegrees = 120.0f;
    intent.damage = 8.0f;
    intent.knockback = 2.0f;
    intent.staggerSeconds = 0.3f;
    intent.type = DamageType::Bite;
    intent.pending = true;
    registry.emplace<ecs::AttackIntent>(biter, intent);

    // 同类僵尸（锥内）：不应被咬
    const auto other = world.Create();
    registry.emplace<Position>(other, Position{0.9f, 0.0f, 0});
    registry.emplace<Health>(other, Health{100.0f, 100.0f});
    registry.emplace<ecs::AiState>(other, ecs::AiState{});
    // 玩家（锥内）：正常受击
    const auto player = world.Create();
    registry.emplace<Position>(player, Position{0.6f, 0.4f, 0});
    registry.emplace<Health>(player, Health{100.0f, 100.0f});
    registry.emplace<ecs::PlayerTag>(player);
    registry.emplace<ecs::MoveIntent>(player, ecs::MoveIntent{});

    CombatSystem combat;
    combat.Update(world, kDt);
    CHECK(registry.get<Health>(other).current == doctest::Approx(100.0f));  // 同类豁免
    CHECK(registry.get<Health>(player).current == doctest::Approx(92.0f));  // 被抓受伤
    CHECK(registry.get<HitReaction>(player).staggerLeft > 0.0f);
    CHECK(combat.GetStats().hits == 1u);
}

TEST_CASE("感知：倒地/死亡玩家不再是视觉目标（僵尸立即丢失追击）") {
    EcsWorld world;
    entt::registry& registry = world.Registry();

    const auto player = world.Create();
    registry.emplace<ecs::PlayerTag>(player);
    registry.emplace<Position>(player, Position{5.0f, 5.0f, 0});
    registry.emplace<Health>(player, Health{100.0f, 100.0f});

    const auto zombie = world.Create();
    registry.emplace<Position>(zombie, Position{6.0f, 5.0f, 0});
    registry.emplace<Health>(zombie, Health{100.0f, 100.0f});
    registry.emplace<ecs::AiState>(zombie, ecs::AiState{});
    ecs::PerceptionState percep;
    percep.lastKnownX = 6.0f;
    percep.lastKnownY = 5.0f;
    registry.emplace<ecs::PerceptionState>(zombie, percep);

    ai::PerceptionSystem perception([](int, int) { return true; });
    perception.Update(world, kDt);
    CHECK(registry.get<ecs::AiState>(zombie).targetId == world.IdOf(player).id);
    CHECK(perception.GetStats().seeing == 1);

    // 玩家倒地（HP<=0）→ 视觉目标集合剔除
    registry.get<Health>(player).current = 0.0f;
    perception.Update(world, kDt);
    CHECK(registry.get<ecs::AiState>(zombie).targetId == 0u);
    CHECK(perception.GetStats().seeing == 0);
}

TEST_CASE("CombatSystem：倒地/死亡攻击者的挥击被忽略（冒烟抓出：死人出刀封口）") {
    EcsWorld world;
    entt::registry& registry = world.Registry();

    // 攻击者 A：已倒地（Dying）但残留 pending 挥击（玩家倒地窗口内按空格）
    const auto downed = world.Create();
    registry.emplace<Position>(downed, Position{0.0f, 0.0f, 0});
    registry.emplace<Health>(downed, Health{-5.0f, 100.0f});
    registry.emplace<ecs::Dying>(downed);
    ecs::AttackIntent intentA;
    intentA.dirX = 1.0f;
    intentA.range = 1.5f;
    intentA.damage = 40.0f;
    intentA.pending = true;
    registry.emplace<ecs::AttackIntent>(downed, intentA);

    // 攻击者 B：HP 归零但还没挂 Dying 组件（DeathSystem 之前的同 tick 残留）
    const auto zeroHp = world.Create();
    registry.emplace<Position>(zeroHp, Position{0.0f, 0.0f, 0});
    registry.emplace<Health>(zeroHp, Health{0.0f, 100.0f});
    ecs::AttackIntent intentB;
    intentB.dirX = 1.0f;
    intentB.range = 1.5f;
    intentB.damage = 40.0f;
    intentB.pending = true;
    registry.emplace<ecs::AttackIntent>(zeroHp, intentB);

    // 锥内目标：不应被"死人"打到
    const auto victim = world.Create();
    registry.emplace<Position>(victim, Position{1.0f, 0.0f, 0});
    registry.emplace<Health>(victim, Health{100.0f, 100.0f});

    CombatSystem combat;
    combat.Update(world, kDt);
    CHECK(registry.get<Health>(victim).current == doctest::Approx(100.0f));
    CHECK(combat.GetStats().swings == 0u);   // 未消费成挥击
    CHECK(combat.GetStats().hits == 0u);
    CHECK_FALSE(registry.get<ecs::AttackIntent>(downed).pending);  // pending 已清（不悬留）
    CHECK_FALSE(registry.get<ecs::AttackIntent>(zeroHp).pending);

    // 活着的攻击者同参数 → 正常结算（对照组）
    const auto alive = world.Create();
    registry.emplace<Position>(alive, Position{0.0f, 0.0f, 0});
    registry.emplace<Health>(alive, Health{100.0f, 100.0f});
    ecs::AttackIntent intentC;
    intentC.dirX = 1.0f;
    intentC.range = 1.5f;
    intentC.damage = 40.0f;
    intentC.pending = true;
    registry.emplace<ecs::AttackIntent>(alive, intentC);
    combat.Update(world, kDt);
    CHECK(registry.get<Health>(victim).current == doctest::Approx(60.0f));
    CHECK(combat.GetStats().swings == 1u);
    CHECK(combat.GetStats().hits == 1u);
}

TEST_CASE("M13-T10 ActorAction：攻击/受击/死亡表现状态") {
    ecs::EcsWorld world;
    entt::registry& registry = world.Registry();
    const ecs::Entity attacker = world.Create();
    registry.emplace<Position>(attacker, Position{0.0f, 0.0f, 0});
    registry.emplace<Health>(attacker, Health{100.0f, 100.0f});
    registry.emplace<ecs::ActionState>(attacker, ecs::ActionState{});
    ecs::AttackIntent intent;
    intent.dirX = 1.0f;
    intent.range = 2.0f;
    intent.damage = 30.0f;
    intent.pending = true;
    registry.emplace<ecs::AttackIntent>(attacker, intent);

    const ecs::Entity target = world.Create();
    registry.emplace<Position>(target, Position{1.0f, 0.0f, 0});
    registry.emplace<Health>(target, Health{100.0f, 100.0f});
    registry.emplace<ecs::ActionState>(target, ecs::ActionState{});

    CombatSystem combat;
    combat.Update(world, kDt);
    CHECK(registry.get<ecs::ActionState>(attacker).action ==
          ecs::ActorAction::Attacking);
    CHECK(registry.get<ecs::ActionState>(target).action == ecs::ActorAction::Hit);
    CHECK(registry.get<Health>(target).current == doctest::Approx(70.0f));

    ecs::DamageEvent lethal;
    lethal.sourceId = world.IdOf(attacker).id;
    lethal.amount = 100.0f;
    REQUIRE(combat.ApplyDamage(world, target, lethal));
    CHECK(registry.get<ecs::ActionState>(target).action == ecs::ActorAction::Dead);
}


TEST_CASE("M15-T1：命中消耗主手武器耐久（每次挥击一次，多目标不叠加；空挥不磨损）") {
    EcsWorld world;
    entt::registry& registry = world.Registry();
    const auto attacker = world.Create();
    registry.emplace<Position>(attacker, Position{0.0f, 0.0f, 0});
    ecs::Equipment equipment;
    equipment.slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)] =
        ecs::ItemInstance{"axe", 1, 0.9f};
    registry.emplace<ecs::Equipment>(attacker, equipment);
    ecs::AttackIntent intent;
    intent.dirX = 1.0f;
    intent.dirY = 0.0f;
    intent.range = 2.0f;
    intent.arcDegrees = 180.0f;
    intent.damage = 10.0f;
    intent.weaponWearPerHit = 0.03f;
    intent.pending = true;
    registry.emplace<ecs::AttackIntent>(attacker, intent);

    const auto first = world.Create();
    registry.emplace<Position>(first, Position{1.0f, 0.0f, 0});
    registry.emplace<Health>(first, Health{100.0f, 100.0f});
    const auto second = world.Create();
    registry.emplace<Position>(second, Position{0.5f, 0.5f, 0});
    registry.emplace<Health>(second, Health{100.0f, 100.0f});

    CombatSystem combat;
    combat.Update(world, kDt);
    CHECK(combat.GetStats().hits == 2u);
    REQUIRE(combat.GetWeaponWears().size() == 1u);
    CHECK(combat.GetWeaponWears()[0].amount == doctest::Approx(0.03f));
    auto* equipped = registry.try_get<ecs::Equipment>(attacker);
    REQUIRE(equipped != nullptr);
    CHECK_FALSE(items::WearEquippedWeapon(*equipped,
                                          combat.GetWeaponWears()[0].amount));
    CHECK(equipped->slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)]
              ->condition == doctest::Approx(0.87f));

    // 空挥：不产生磨损请求
    registry.get<ecs::AttackIntent>(attacker).pending = true;
    registry.get<Position>(first).x = 20.0f;
    registry.get<Position>(second).x = 20.0f;
    combat.Update(world, kDt);
    CHECK(combat.GetWeaponWears().empty());
}

TEST_CASE("M15-T2 伤害类型映射为伤口：claw/bite 流血、blunt 不流血、debug 忽略") {
    EcsWorld world;
    entt::registry& registry = world.Registry();
    const auto target = world.Create();
    registry.emplace<Position>(target, Position{0.0f, 0.0f, 0});
    registry.emplace<Health>(target, Health{100.0f, 100.0f});

    CombatSystem combat;
    DamageEvent event;
    event.sourceId = 7;
    event.amount = 10.0f;
    event.type = DamageType::Bite;
    REQUIRE(combat.ApplyDamage(world, target, event));
    auto& wounds = registry.get<ecs::Wounds>(target);
    REQUIRE(wounds.count == 1);
    CHECK(wounds.entries[0].source == DamageType::Bite);
    CHECK(wounds.entries[0].severity == doctest::Approx(0.32f));
    CHECK(wounds.entries[0].bleedRate == doctest::Approx(0.8f));
    CHECK(wounds.entries[0].bleedDuration == doctest::Approx(30.0f));
    CHECK(wounds.entries[0].bleedRemaining == doctest::Approx(30.0f));
    CHECK_FALSE(wounds.entries[0].bandaged);
    CHECK(zl::characters::CurrentBleedRate(wounds) == doctest::Approx(0.8f));

    event.type = DamageType::Claw;
    REQUIRE(combat.ApplyDamage(world, target, event));
    REQUIRE(wounds.count == 2);
    CHECK(wounds.entries[1].source == DamageType::Claw);
    CHECK(wounds.entries[1].severity == doctest::Approx(0.24f));
    CHECK(wounds.entries[1].bleedRate == doctest::Approx(0.35f));
    CHECK(wounds.entries[1].bleedDuration == doctest::Approx(18.0f));

    event.type = DamageType::Blunt;
    REQUIRE(combat.ApplyDamage(world, target, event));
    REQUIRE(wounds.count == 3);
    CHECK(wounds.entries[2].source == DamageType::Blunt);
    CHECK(wounds.entries[2].bleedRate == doctest::Approx(0.0f));
    CHECK(wounds.entries[2].bleedDuration == doctest::Approx(0.0f));

    event.type = DamageType::Debug;
    event.amount = 5.0f;
    REQUIRE(combat.ApplyDamage(world, target, event));
    CHECK(wounds.count == 3);
    CHECK(registry.get<Health>(target).current == doctest::Approx(65.0f));
    CHECK(zl::characters::WoundCount(wounds) == 3);
    CHECK(zl::characters::WorstWoundSeverity(wounds) == doctest::Approx(0.32f));
}

TEST_CASE("M15-T2 流血按 tick 衰减并停止，不触发受击闪红") {
    EcsWorld world;
    entt::registry& registry = world.Registry();
    const auto target = world.Create();
    registry.emplace<Position>(target, Position{0.0f, 0.0f, 0});
    registry.emplace<Health>(target, Health{100.0f, 100.0f});

    CombatSystem combat;
    DamageEvent event;
    event.sourceId = 7;
    event.amount = 10.0f;
    event.type = DamageType::Bite;
    REQUIRE(combat.ApplyDamage(world, target, event));
    REQUIRE(registry.get<Health>(target).current == doctest::Approx(90.0f));

    zl::characters::WoundSystem wounds;
    wounds.Update(world, 1.0f);
    const float first = registry.get<Health>(target).current;
    CHECK(first == doctest::Approx(89.2f));
    REQUIRE(registry.try_get<ecs::LastDamage>(target) != nullptr);
    CHECK(registry.get<ecs::LastDamage>(target).cause == ecs::DamageCause::Bleed);
    wounds.Update(world, 1.0f);
    const float second = registry.get<Health>(target).current;
    CHECK(second == doctest::Approx(88.4267f));
    CHECK((first - second) < (90.0f - first));

    for (int i = 0; i < 35; ++i) {
        wounds.Update(world, 1.0f);
    }
    const auto& entries = registry.get<ecs::Wounds>(target);
    CHECK(entries.entries[0].bleedRemaining == doctest::Approx(0.0f));
    CHECK(zl::characters::CurrentBleedRate(entries) == doctest::Approx(0.0f));
    const float stable = registry.get<Health>(target).current;
    CHECK(stable > 77.0f);
    CHECK(stable < 78.0f);
    wounds.Update(world, 1.0f);
    CHECK(registry.get<Health>(target).current == doctest::Approx(stable));
}

TEST_CASE("M15-T2 绷带/破布止血并降低伤口；伤口上限四层") {
    ecs::Wounds wounds;
    zl::characters::AddWound(wounds, DamageType::Bite, 20.0f, {});
    REQUIRE(wounds.count == 1);
    REQUIRE(wounds.entries[0].bleedRate > 0.0f);
    const auto treated = zl::characters::TreatWounds(wounds, 0.7f);
    CHECK(treated.wounds == 1);
    CHECK(treated.stoppedBleeding);
    CHECK(wounds.entries[0].bandaged);
    CHECK(wounds.entries[0].bleedRate == doctest::Approx(0.0f));
    CHECK(wounds.entries[0].severity == doctest::Approx(0.192f));

    for (int i = 0; i < 5; ++i) {
        zl::characters::AddWound(wounds, DamageType::Claw, 5.0f, {});
    }
    CHECK(wounds.count == ecs::kMaxWounds);
}

TEST_CASE("M15-T6 验收链路：武器磨损→撕咬流血→绷带止血→致死尸体") {
    items::ItemRegistry itemRegistry;
    std::string itemError;
    REQUIRE(itemRegistry.LoadFile(std::string(ZL_SOURCE_DIR) + "/assets/items.json",
                                  &itemError));

    EcsWorld world;
    entt::registry& registry = world.Registry();
    const auto player = world.Create();
    registry.emplace<Position>(player, Position{0.0f, 0.0f, 0});
    registry.emplace<Health>(player, Health{100.0f, 100.0f});
    ecs::Equipment equipment;
    REQUIRE(items::Equip(equipment, itemRegistry, ecs::EquipSlot::PrimaryHand,
                         ecs::ItemInstance{"axe", 1, 1.0f}, nullptr));
    registry.emplace<ecs::Equipment>(player, equipment);

    const auto zombie = world.Create();
    registry.emplace<Position>(zombie, Position{1.0f, 0.0f, 0});
    registry.emplace<Health>(zombie, Health{60.0f, 100.0f});

    ecs::AttackIntent attack;
    attack.dirX = 1.0f;
    attack.range = 2.0f;
    attack.arcDegrees = 180.0f;
    attack.damage = 65.0f;
    attack.weaponWearPerHit = 0.035f;
    attack.pending = true;
    registry.emplace<ecs::AttackIntent>(player, attack);

    CombatSystem combat;
    combat.Update(world, kDt);
    CHECK(registry.get<Health>(zombie).current <= 0.0f);
    CHECK(combat.GetStats().kills == 1u);
    REQUIRE(combat.GetWeaponWears().size() == 1u);
    auto& equipped = registry.get<ecs::Equipment>(player);
    CHECK_FALSE(items::WearEquippedWeapon(equipped,
                                          combat.GetWeaponWears()[0].amount));
    const auto& primary =
        equipped.slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)];
    REQUIRE(primary.has_value());
    CHECK(primary->condition == doctest::Approx(0.965f));

    ecs::DamageEvent bite;
    bite.sourceId = world.IdOf(zombie).id;
    bite.amount = 12.4f;
    bite.type = DamageType::Bite;
    REQUIRE(combat.ApplyDamage(world, player, bite));
    auto* health = registry.try_get<Health>(player);
    auto* wounds = registry.try_get<ecs::Wounds>(player);
    REQUIRE(health != nullptr);
    REQUIRE(wounds != nullptr);
    CHECK(health->current == doctest::Approx(87.6f));
    REQUIRE(zl::characters::CurrentBleedRate(*wounds) > 0.0f);

    zl::characters::WoundSystem woundSystem;
    woundSystem.Update(world, 1.0f);
    const float bleeding = health->current;
    CHECK(bleeding < 87.6f);

    ecs::Container bag;
    bag.capacityKg = 10.0f;
    REQUIRE(items::AddItem(bag, itemRegistry,
                           ecs::ItemInstance{"bandage", 1, 1.0f}) == 1);
    const items::UseResult bandage = items::UseAt(bag, itemRegistry, 0);
    REQUIRE(bandage.applied);
    CHECK(bandage.defId == "bandage");
    CHECK(bag.items.empty());
    items::ApplyUseEffect(bandage.effect, 100.0f, &health->current, nullptr, nullptr,
                          nullptr);
    const auto treatment =
        zl::characters::TreatWounds(*wounds, bandage.effect.bandage);
    CHECK(treatment.stoppedBleeding);
    CHECK(zl::characters::CurrentBleedRate(*wounds) == doctest::Approx(0.0f));
    const float bandagedHealth = health->current;
    woundSystem.Update(world, 2.0f);
    CHECK(health->current == doctest::Approx(bandagedHealth));

    ecs::DamageEvent lethal;
    lethal.sourceId = world.IdOf(zombie).id;
    lethal.amount = 200.0f;
    lethal.type = DamageType::Bite;
    REQUIRE(combat.ApplyDamage(world, player, lethal));
    CHECK(health->current <= 0.0f);
    const auto* lastDamage = registry.try_get<ecs::LastDamage>(player);
    REQUIRE(lastDamage != nullptr);
    CHECK(lastDamage->cause == ecs::DamageCause::Bite);

    DeathSystem death(nullptr);
    death.Update(world, kDt);
    CHECK(registry.all_of<ecs::Dying>(player));
    death.Update(world, DeathSystem::kDefaultFallSeconds);
    world.Flush();
    CHECK(registry.view<ecs::Corpse>().size() == 2u);
    CHECK_FALSE(registry.valid(player));
}

TEST_CASE("M15-T4 疼痛由伤口汇总，止痛短时降低惩罚且不叠加") {
    EcsWorld world;
    entt::registry& registry = world.Registry();
    const auto target = world.Create();
    registry.emplace<ecs::Wounds>(target, ecs::Wounds{});
    registry.emplace<ecs::Pain>(target, ecs::Pain{});

    ecs::Wounds wounds;
    wounds.entries[0] =
        ecs::WoundEntry{DamageType::Bite, 0.4f, 0.0f, 0.0f, 0.0f, false};
    wounds.entries[1] =
        ecs::WoundEntry{DamageType::Claw, 0.2f, 0.0f, 0.0f, 0.0f, true};
    wounds.count = 2;
    registry.get<ecs::Wounds>(target) = wounds;

    zl::characters::PainSystem painSystem;
    painSystem.Update(world, 0.0f);
    auto& pain = registry.get<ecs::Pain>(target);
    CHECK(pain.baseLevel == doctest::Approx(0.325f));
    CHECK(pain.level == doctest::Approx(0.325f));
    CHECK(pain.moveScale == doctest::Approx(0.88625f));
    CHECK(pain.attackCooldownScale == doctest::Approx(1.1625f));

    REQUIRE(zl::characters::ApplyPainRelief(pain, 0.65f, 10.0f));
    CHECK(pain.reliefStrength == doctest::Approx(0.65f));
    CHECK(pain.reliefRemaining == doctest::Approx(10.0f));
    CHECK(pain.level == doctest::Approx(0.11375f));

    REQUIRE(zl::characters::ApplyPainRelief(pain, 0.5f, 20.0f));
    CHECK(pain.reliefStrength == doctest::Approx(0.65f));
    CHECK(pain.reliefDuration == doctest::Approx(20.0f));
    CHECK(pain.reliefRemaining == doctest::Approx(20.0f));

    painSystem.Update(world, 10.0f);
    const float halfway = pain.level;
    painSystem.Update(world, 10.0f);
    CHECK(pain.level == doctest::Approx(0.325f));
    CHECK(pain.reliefStrength == doctest::Approx(0.0f));
    CHECK(pain.moveScale == doctest::Approx(0.88625f));
    CHECK(halfway > 0.11375f);
    CHECK(halfway < 0.325f);

    registry.get<ecs::Wounds>(target).count = 0;
    painSystem.Update(world, 0.0f);
    CHECK(pain.baseLevel == doctest::Approx(0.0f));
    CHECK(pain.level == doctest::Approx(0.0f));
    CHECK(pain.moveScale == doctest::Approx(1.0f));
    CHECK(pain.attackCooldownScale == doctest::Approx(1.0f));
}

TEST_CASE("M15-T4 疼痛 HUD 文案包含等级、惩罚与剩余止痛") {
    ecs::Pain pain;
    pain.level = 0.25f;
    pain.moveScale = 0.9125f;
    pain.attackCooldownScale = 1.125f;
    pain.reliefRemaining = 30.0f;
    std::string tone;
    const std::string text = zl::game::PainStatusText(pain, &tone);
    CHECK(text.find("疼痛：轻微 25%") != std::string::npos);
    CHECK(text.find("止痛 30s") != std::string::npos);

    pain.level = 0.75f;
    pain.moveScale = 0.7375f;
    pain.attackCooldownScale = 1.375f;
    pain.reliefRemaining = 0.0f;
    const std::string severe = zl::game::PainStatusText(pain, &tone);
    CHECK(severe.find("疼痛：严重 75%") != std::string::npos);
    CHECK(severe.find("止痛") == std::string::npos);
    CHECK(tone == "fail");
}

TEST_CASE("M15-T4 疼痛移速乘数由 MovementSystem 应用到玩家速度") {
    EcsWorld world;
    entt::registry& registry = world.Registry();
    const auto player = world.Create();
    registry.emplace<Position>(player, Position{0.0f, 0.0f, 0});
    registry.emplace<Velocity>(player, Velocity{});
    registry.emplace<ecs::MoveIntent>(player, ecs::MoveIntent{4.0f, 0.0f});
    registry.emplace<ecs::PlayerTag>(player);
    registry.emplace<ecs::Needs>(player, ecs::Needs{});
    ecs::Pain pain;
    pain.moveScale = 0.5f;
    registry.emplace<ecs::Pain>(player, pain);

    zl::characters::MovementSystem movement;
    movement.Update(world, 0.5f);
    CHECK(registry.get<Velocity>(player).x == doctest::Approx(2.0f));
    CHECK(registry.get<Position>(player).x == doctest::Approx(1.0f));
}

TEST_CASE("M15-T5 致死命中后死因保持直接伤害，不被同 tick 流血覆盖") {
    EcsWorld world;
    entt::registry& registry = world.Registry();
    const auto target = world.Create();
    registry.emplace<Position>(target, Position{0.0f, 0.0f, 0});
    registry.emplace<Health>(target, Health{5.0f, 100.0f});

    CombatSystem combat;
    DamageEvent event;
    event.sourceId = 7;
    event.amount = 10.0f;
    event.type = DamageType::Bite;
    REQUIRE(combat.ApplyDamage(world, target, event));
    zl::characters::WoundSystem wounds;
    wounds.Update(world, 1.0f);
    REQUIRE(registry.try_get<ecs::LastDamage>(target) != nullptr);
    CHECK(registry.get<ecs::LastDamage>(target).cause == ecs::DamageCause::Bite);
}

TEST_CASE("M15-T3 受击反馈：来源方向/类型/serial，闪红结束后还原变体 tint") {
    EcsWorld world;
    entt::registry& registry = world.Registry();
    const auto attacker = world.Create();
    registry.emplace<Position>(attacker, Position{1.0f, 1.0f, 0});
    const auto victim = world.Create();
    registry.emplace<Position>(victim, Position{1.0f, 2.0f, 0});
    registry.emplace<Health>(victim, Health{100.0f, 100.0f});
    constexpr std::uint32_t kStrongTint = 0xFFFF8070u;
    registry.emplace<SpriteRef>(
        victim, SpriteRef{ecs::HashName("character.zombie"), 0, kStrongTint});

    CombatSystem combat;
    ecs::DamageEvent event;
    event.sourceId = world.IdOf(attacker).id;
    event.type = DamageType::Claw;
    event.amount = 5.0f;
    event.dirX = 0.0f;
    event.dirY = 1.0f;
    REQUIRE(combat.ApplyDamage(world, victim, event));

    const auto& hit = registry.get<HitReaction>(victim);
    CHECK(hit.hitSerial == 1u);
    CHECK(hit.sourceType == DamageType::Claw);
    CHECK(hit.sourceDirX == doctest::Approx(0.0f));
    CHECK(hit.sourceDirY == doctest::Approx(-1.0f));
    CHECK(registry.get<SpriteRef>(victim).tint != kStrongTint);

    for (int i = 0; i < 12; ++i) {
        combat.Update(world, kDt);
    }
    CHECK(registry.get<SpriteRef>(victim).tint == kStrongTint);
}

TEST_CASE("M15-T3 变体攻击参数：fast 爪击 / strong 撕咬且冷却/前摇可区分") {
    ai::AiSystem::AttackParams params;
    CHECK(params.ProfileFor(ecs::ZombieVariantKind::Normal).type ==
          DamageType::Claw);
    CHECK(params.ProfileFor(ecs::ZombieVariantKind::Fast).type ==
          DamageType::Claw);
    CHECK(params.ProfileFor(ecs::ZombieVariantKind::Strong).type ==
          DamageType::Bite);
    CHECK(params.ProfileFor(ecs::ZombieVariantKind::Armored).type ==
          DamageType::Bite);
    CHECK(params.ProfileFor(ecs::ZombieVariantKind::Fast).windupSeconds <
          params.ProfileFor(ecs::ZombieVariantKind::Strong).windupSeconds);
    CHECK(params.ProfileFor(ecs::ZombieVariantKind::Strong).damage >
          params.ProfileFor(ecs::ZombieVariantKind::Fast).damage);
    CHECK(params.ProfileFor(ecs::ZombieVariantKind::Armored).damage >
          params.ProfileFor(ecs::ZombieVariantKind::Strong).damage);
}

TEST_CASE("M15-T3 前摇中断：硬直期间清空蓄力，结束后必须重新完成完整前摇") {
    EcsWorld world;
    entt::registry& registry = world.Registry();
    const auto target = world.Create();
    registry.emplace<Position>(target, Position{5.6f, 5.0f, 0});
    registry.emplace<Health>(target, Health{100.0f, 100.0f});
    const std::uint64_t targetId = world.IdOf(target).id;

    const auto zombie = world.Create();
    registry.emplace<Position>(zombie, Position{5.0f, 5.0f, 0});
    registry.emplace<Health>(zombie, Health{100.0f, 100.0f});
    ecs::AiState ai;
    ai.status = ecs::AiStatus::Attack;
    ai.targetId = targetId;
    registry.emplace<ecs::AiState>(zombie, ai);
    ecs::PerceptionState percep;
    percep.lastKnownX = 5.6f;
    percep.lastKnownY = 5.0f;
    registry.emplace<ecs::PerceptionState>(zombie, percep);
    registry.emplace<ecs::MoveIntent>(zombie, ecs::MoveIntent{});
    registry.emplace<ecs::AttackIntent>(zombie, ecs::AttackIntent{});

    ai::AiSystem::AttackParams params;
    params.normal.windupSeconds = 0.5f;
    ai::AiSystem system([](int, int) { return true; }, params);
    system.Update(world, kDt);
    CHECK(registry.get<ecs::AiState>(zombie).attackWindupLeft > 0.0f);
    CHECK_FALSE(registry.get<ecs::AttackIntent>(zombie).pending);

    registry.emplace<HitReaction>(zombie, HitReaction{1.0f, 0.0f, 0.0f, 0.0f});
    system.Update(world, kDt);
    CHECK(registry.get<ecs::AiState>(zombie).attackWindupLeft ==
          doctest::Approx(0.0f));
    CHECK(system.GetStats().interrupted == 1u);
    CHECK_FALSE(registry.get<ecs::AttackIntent>(zombie).pending);

    for (int i = 0; i < 10; ++i) {
        system.Update(world, kDt);
    }
    registry.remove<HitReaction>(zombie);
    system.Update(world, kDt);
    CHECK(registry.get<ecs::AiState>(zombie).attackWindupLeft > 0.0f);
    CHECK_FALSE(registry.get<ecs::AttackIntent>(zombie).pending);
}
