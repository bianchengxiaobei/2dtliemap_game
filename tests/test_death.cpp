#include <doctest/doctest.h>

#include <algorithm>
#include <type_traits>

#include "characters/components/CharacterComponents.h"
#include "characters/systems/DeathSystem.h"
#include "characters/systems/MovementSystem.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "physics/PhysicsSyncSystem.h"
#include "physics/PhysicsWorld.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"

using namespace zl;

using zl::characters::DeathSystem;
using zl::ecs::Collider;
using zl::ecs::Corpse;
using zl::ecs::EcsWorld;
using zl::ecs::Health;
using zl::ecs::Needs;
using zl::ecs::Position;
using zl::ecs::RigidBody;
using zl::ecs::SpriteRef;
using zl::ecs::StableId;
using zl::ecs::Velocity;

TEST_CASE("角色组件集：Health/Needs/Corpse 默认值、平凡可拷贝、可共存一实体") {
    const Health health;
    CHECK(health.current == 100.0f);
    CHECK(health.max == 100.0f);
    const Needs needs;
    CHECK(needs.hunger == 0.0f);
    CHECK(needs.thirst == 0.0f);
    CHECK(needs.fatigue == 0.0f);
    const Corpse corpse;
    CHECK(corpse.sourceId == 0u);

    // 组件是平凡可拷贝的纯数据（EnTT storage 要求，同 M4-T1 约定）
    static_assert(std::is_trivially_copyable_v<Health>);
    static_assert(std::is_trivially_copyable_v<Needs>);
    static_assert(std::is_trivially_copyable_v<Corpse>);

    // 角色组件集可共存于一实体（M5-T1 定稿：控制/动画/生命/需求分离）
    EcsWorld world;
    const auto e = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<Position>(e, Position{});
    registry.emplace<ecs::PlayerTag>(e);
    registry.emplace<ecs::LocalControl>(e, ecs::LocalControl{});
    registry.emplace<ecs::MoveIntent>(e, ecs::MoveIntent{});
    registry.emplace<ecs::Facing>(e, ecs::Facing{});
    registry.emplace<ecs::AnimState>(e, ecs::AnimState{});
    registry.emplace<Health>(e, Health{});
    registry.emplace<Needs>(e, Needs{});
    CHECK(registry.all_of<ecs::PlayerTag, ecs::LocalControl, ecs::MoveIntent, ecs::Facing,
                          ecs::AnimState, Health, Needs>(e));
}

TEST_CASE("DeathSystem：HP<=0 → 物理体先销毁 + 延迟销毁 + 尸体实体（位置/精灵/来源 ID）") {
    physics::PhysicsWorld physics;
    EcsWorld world;
    entt::registry& registry = world.Registry();

    // 死者：完整角色实体（物理体 + 位置），HP 归零
    const auto dying = world.Create();
    const std::uint64_t dyingId = world.IdOf(dying).id;
    registry.emplace<Position>(dying, Position{12.5f, 7.25f, 0});
    registry.emplace<Velocity>(dying, Velocity{3.0f, 0.0f});
    registry.emplace<Collider>(dying, Collider{});
    registry.emplace<SpriteRef>(
        dying, SpriteRef{ecs::HashName("character.placeholder"), 0, 0xFFFFFFFFu});
    registry.emplace<Health>(dying, Health{0.0f, 100.0f});
    RigidBody rigid;
    rigid.body = physics.CreateCharacterBody(0.3f, 1.8f, 12.5f, 7.25f, 0);
    registry.emplace<RigidBody>(dying, rigid);

    // 健康实体：不应被碰
    const auto healthy = world.Create();
    registry.emplace<Position>(healthy, Position{1.0f, 1.0f, 0});
    registry.emplace<Health>(healthy, Health{55.0f, 100.0f});

    CHECK(physics.DynamicBodyCount() == 1u);
    DeathSystem death(&physics);
    death.Update(world);
    CHECK_FALSE(world.Alive(dying));          // 语义已死（延迟回收）
    CHECK(world.Registry().valid(dying));     // 本 tick 内存仍在
    CHECK(physics.DynamicBodyCount() == 0u);  // 物理体已回收（无泄漏）
    CHECK(world.AliveCount() == 2u);          // Flush 前：尸体尚未创建

    world.Flush();
    CHECK_FALSE(world.Registry().valid(dying));
    CHECK(world.Alive(healthy));
    CHECK(world.AliveCount() == 2u);  // healthy + 尸体

    int corpses = 0;
    registry.view<Corpse>().each([&](const entt::entity e, const Corpse& corpse) {
        ++corpses;
        CHECK(corpse.sourceId == dyingId);  // 溯源到死者稳定 ID
        const auto* pos = registry.try_get<Position>(e);
        REQUIRE(pos != nullptr);
        CHECK(pos->x == doctest::Approx(12.5f));
        CHECK(pos->y == doctest::Approx(7.25f));
        CHECK(pos->z == 0);
        const auto* sprite = registry.try_get<SpriteRef>(e);
        REQUIRE(sprite != nullptr);
        CHECK(sprite->sprite == ecs::HashName("corpse.placeholder"));
        CHECK_FALSE(registry.all_of<Collider>(e));   // 尸体不阻挡
        CHECK_FALSE(registry.all_of<RigidBody>(e));  // 尸体无物理体
        CHECK_FALSE(registry.all_of<Velocity>(e));   // 尸体不被 MovementSystem 积分
    });
    CHECK(corpses == 1);
}

TEST_CASE("DeathSystem：同 tick 多死者各自成尸；二次运行不重复；HP>0 不处理") {
    EcsWorld world;
    entt::registry& registry = world.Registry();
    DeathSystem death(nullptr);  // 无物理世界：仅验证实体流转

    const auto a = world.Create();
    registry.emplace<Position>(a, Position{2.0f, 2.0f, 0});
    registry.emplace<Health>(a, Health{0.0f, 100.0f});
    const auto b = world.Create();
    const std::uint64_t bId = world.IdOf(b).id;
    registry.emplace<Position>(b, Position{4.0f, 6.0f, 1});
    registry.emplace<Health>(b, Health{-1.0f, 100.0f});
    const auto c = world.Create();
    registry.emplace<Position>(c, Position{9.0f, 9.0f, 0});
    registry.emplace<Health>(c, Health{0.01f, 100.0f});  // 未死（>0）

    death.Update(world);
    world.Flush();
    CHECK(registry.view<Corpse>().size() == 2u);
    CHECK(world.Alive(c));
    CHECK(world.AliveCount() == 3u);  // c + 2 尸体

    // 二次运行：死者已回收、尸体无 Health → 不再产生新尸体
    death.Update(world);
    world.Flush();
    CHECK(registry.view<Corpse>().size() == 2u);
    CHECK(world.AliveCount() == 3u);

    // 稳定 ID 不复用：尸体的 ID 大于所有死者
    std::uint64_t maxCorpseId = 0;
    registry.view<Corpse>().each([&](const entt::entity e, const Corpse&) {
        maxCorpseId = std::max(maxCorpseId, world.IdOf(e).id);
    });
    CHECK(maxCorpseId > world.IdOf(c).id);

    // z 原样保留（楼上死亡 → 楼上尸体）
    bool sawUpper = false;
    registry.view<Corpse, Position>().each(
        [&](const entt::entity, const Corpse& corpse, const Position& pos) {
            if (corpse.sourceId == bId) {
                CHECK(pos.z == 1);
                sawUpper = true;
            }
        });
    CHECK(sawUpper);
}

TEST_CASE("DeathSystem：全链路——Health 归零的物理角色在 tick 链中被回收并留下尸体") {
    physics::PhysicsWorld physics;
    world::Chunk chunk(0, 0);
    chunk.Square(5, 5, 0).AddObject(world::SquareLayer::Wall, {"wall.brick", 0, 0, true});
    physics.BuildChunkStatics(0, 0, chunk);

    EcsWorld world;
    entt::registry& registry = world.Registry();
    const auto e = world.Create();
    registry.emplace<Position>(e, Position{3.0f, 5.0f, 0});
    registry.emplace<Velocity>(e, Velocity{4.0f, 0.0f});
    registry.emplace<Collider>(e, Collider{});
    registry.emplace<Health>(e, Health{100.0f, 100.0f});
    RigidBody rigid;
    rigid.body = physics.CreateCharacterBody(0.3f, 1.8f, 3.0f, 5.0f, 0);
    registry.emplace<RigidBody>(e, rigid);

    characters::MovementSystem movement;
    physics::PhysicsSyncSystem sync(&physics);
    DeathSystem death(&physics);

    constexpr float kDt = 1.0f / 60.0f;
    const auto runTick = [&] {
        movement.Update(world, kDt);
        sync.Update(world, kDt);
        death.Update(world);
        world.Flush();
    };

    for (int i = 0; i < 30; ++i) {  // 先正常跑 0.5s（物理接管位置）
        runTick();
    }
    CHECK(physics.DynamicBodyCount() == 1u);
    CHECK(registry.valid(e));
    const Position beforeDeath = registry.get<Position>(e);  // 物理回写后的实际位置

    registry.get<Health>(e).current = 0.0f;  // 致死一击
    runTick();

    CHECK_FALSE(registry.valid(e));          // tick 末已回收
    CHECK(physics.DynamicBodyCount() == 0u);  // 无物理体泄漏
    CHECK(registry.view<Corpse>().size() == 1u);
    bool matched = false;
    registry.view<Corpse, Position>().each(
        [&](const entt::entity, const Corpse&, const Position& pos) {
            CHECK(pos.x == doctest::Approx(beforeDeath.x));  // 尸体留在死亡瞬间的位置
            CHECK(pos.y == doctest::Approx(beforeDeath.y));
            matched = true;
        });
    CHECK(matched);
}
