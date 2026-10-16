#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <type_traits>

#include "ai/components/AiComponents.h"
#include "ai/systems/AiSystem.h"
#include "ai/systems/PerceptionSystem.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/systems/CombatSystem.h"
#include "characters/systems/DeathSystem.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"

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
    params.damage = 8.0f;
    params.knockback = 2.0f;
    params.staggerSeconds = 0.3f;
    params.range = 1.3f;
    params.arcDegrees = 120.0f;
    params.type = DamageType::Bite;
    ai::AiSystem system([](int, int) { return true; }, params);

    system.Update(world, kDt);  // 进入攻击节拍：cooldown=0 → 立即落一次脉冲
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

    // 倒地窗口：第 1 次 DeathSystem → Dying（实体还在；速度冻结 + 精灵换尸体帧）
    death.Update(world, kDt);
    CHECK(registry.all_of<ecs::Dying>(zombie));
    CHECK(world.Alive(zombie));
    CHECK(registry.get<Velocity>(zombie).x == doctest::Approx(0.0f));
    CHECK(registry.get<SpriteRef>(zombie).sprite == ecs::HashName("corpse.placeholder"));

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
