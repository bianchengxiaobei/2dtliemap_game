#include <doctest/doctest.h>

#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include "ecs/EcsWorld.h"
#include "ecs/SystemScheduler.h"
#include "ecs/components/CommonComponents.h"

using zl::ecs::Collider;
using zl::ecs::EcsWorld;
using zl::ecs::Position;
using zl::ecs::SpriteRef;
using zl::ecs::StableId;
using zl::ecs::Velocity;

namespace {

struct Observed {
    bool stillValid = false;  // registry.valid()（延迟回收：系统内仍 true）
    bool alive = false;       // EcsWorld::Alive()（语义已死：false）
};

}  // namespace

TEST_CASE("EcsWorld：创建附着 StableId、单调唯一、双向查找") {
    EcsWorld world;
    std::unordered_set<uint64_t> ids;
    for (int i = 0; i < 100; ++i) {
        const auto e = world.Create();
        REQUIRE(world.Alive(e));
        const StableId sid = world.IdOf(e);
        CHECK(sid.id != 0);
        CHECK(ids.insert(sid.id).second);  // 无重复
        CHECK(world.FindById(sid.id) == e);
        world.Registry().emplace<Position>(e, Position{static_cast<float>(i), 0.0f, 0});
    }
    CHECK(world.AliveCount() == 100u);
    CHECK(world.StableIdCount() == 100u);

    const auto extra = world.Create();
    CHECK(world.IdOf(extra).id == 101u);           // 从 1 开始单调递增
    CHECK(world.FindById(999999u) == EcsWorld::kNull);
    CHECK(world.IdOf(EcsWorld::kNull).id == 0u);
}

TEST_CASE("EcsWorld：Destroy 延迟生效，Flush 后回收且稳定 ID 失效") {
    EcsWorld world;
    const auto e = world.Create();
    const uint64_t id = world.IdOf(e).id;

    world.Destroy(e);
    CHECK_FALSE(world.Alive(e));            // 语义上立即失效
    CHECK(world.Registry().valid(e));       // 但内存未回收（延迟）
    CHECK(world.PendingDestroyCount() == 1u);
    CHECK(world.FindById(id) == EcsWorld::kNull);  // 旧 ID 查不到

    world.Flush();
    CHECK_FALSE(world.Registry().valid(e));  // 句柄已回收
    CHECK(world.PendingDestroyCount() == 0u);
    CHECK(world.StableIdCount() == 0u);
    CHECK(world.AliveCount() == 0u);
    CHECK(world.IdOf(e).id == 0u);
}

TEST_CASE("EcsWorld：遍历中销毁 + 延迟创建安全（生成回合制）") {
    EcsWorld world;
    for (int i = 0; i < 10; ++i) {
        const auto e = world.Create();
        world.Registry().emplace<Position>(e, Position{static_cast<float>(i), 0.0f, 0});
    }

    int visited = 0;
    world.Registry().view<Position>().each([&](const entt::entity e, Position& p) {
        (void)p;
        ++visited;
        world.Destroy(e);  // 遍历中销毁：入队，不动 storage
        world.QueueCreate([](EcsWorld& w) {
            const auto ne = w.Create();
            w.Registry().emplace<Position>(ne, Position{100.0f, 100.0f, 0});
            return ne;
        });
    });
    CHECK(visited == 10);
    CHECK(world.AliveCount() == 10u);  // 还没 Flush：老 10 + 新 0

    world.Flush();
    CHECK(world.AliveCount() == 10u);   // 老 10 回收、新 10 生成
    CHECK(world.StableIdCount() == 10u);
    int fresh = 0;
    world.Registry().view<Position>().each(
        [&](const entt::entity, const Position& p) { fresh += (p.x == 100.0f) ? 1 : 0; });
    CHECK(fresh == 10);
}

TEST_CASE("EcsWorld：QueueCreate 按注册顺序执行、可放弃创建") {
    EcsWorld world;
    std::vector<int> order;
    world.QueueCreate([&](EcsWorld& w) { order.push_back(1); return w.Create(); });
    world.QueueCreate([&](EcsWorld&) { order.push_back(2); return EcsWorld::kNull; });
    world.QueueCreate([&](EcsWorld& w) { order.push_back(3); return w.Create(); });

    CHECK(world.AliveCount() == 0u);
    world.Flush();
    CHECK(order == std::vector<int>{1, 2, 3});
    CHECK(world.AliveCount() == 2u);
    CHECK(world.StableIdCount() == 2u);
}

TEST_CASE("EcsWorld：无效句柄 / 重复 Destroy 不崩溃") {
    EcsWorld world;
    world.Destroy(EcsWorld::kNull);

    const auto e = world.Create();
    world.Destroy(e);
    world.Destroy(e);  // 重复
    CHECK(world.PendingDestroyCount() == 1u);
    world.Flush();

    world.Destroy(e);  // 已回收的句柄
    CHECK(world.PendingDestroyCount() == 0u);
    CHECK(world.AliveCount() == 0u);
}

TEST_CASE("EcsWorld::Update：系统内 Destroy 延迟回收，后续系统仍可读到") {
    EcsWorld world;
    const auto victim = world.Create();
    world.Registry().emplace<Position>(victim, Position{1.0f, 1.0f, 0});

    Observed during;
    Observed later;
    world.Systems().AddSystem("Killer", [&](EcsWorld& w, float) {
        w.Destroy(victim);
        during.stillValid = w.Registry().valid(victim);
        during.alive = w.Alive(victim);
    });
    world.Systems().AddSystem("Observer", [&](EcsWorld& w, float) {
        later.stillValid = w.Registry().valid(victim);  // 内存还在（本 tick 内安全）
        later.alive = w.Alive(victim);                  // 语义已死
    });

    world.Update(0.016f);

    CHECK(during.stillValid);
    CHECK_FALSE(during.alive);
    CHECK(later.stillValid);
    CHECK_FALSE(later.alive);
    CHECK_FALSE(world.Registry().valid(victim));  // Update 结束已统一回收
    CHECK(world.AliveCount() == 0u);
}

TEST_CASE("SystemScheduler：按注册顺序执行、dt 原样传入、耗时统计") {
    EcsWorld world;
    std::vector<std::string> trace;
    float gotDt = 0.0f;
    world.Systems().AddSystem("InputSystem", [&](EcsWorld&, float dt) {
        trace.push_back("Input");
        gotDt = dt;
    });
    world.Systems().AddSystem("MovementSystem", [&](EcsWorld&, float) { trace.push_back("Move"); });
    world.Systems().AddSystem("RenderCollectSystem",
                              [&](EcsWorld&, float) { trace.push_back("Render"); });

    world.Update(0.016f);
    CHECK(trace == std::vector<std::string>{"Input", "Move", "Render"});
    CHECK(gotDt == doctest::Approx(0.016f));
    CHECK(world.Systems().Count() == 3u);
    for (const auto& entry : world.Systems().Entries()) {
        CHECK(entry.lastMs >= 0.0);
        CHECK(entry.avgMs >= 0.0);
    }
}

TEST_CASE("SystemScheduler：SetEnabled 跳过执行，顺序保持不变") {
    EcsWorld world;
    std::vector<std::string> trace;
    world.Systems().AddSystem("A", [&](EcsWorld&, float) { trace.push_back("A"); });
    world.Systems().AddSystem("B", [&](EcsWorld&, float) { trace.push_back("B"); });

    world.Update(0.016f);
    world.Systems().SetEnabled("A", false);
    CHECK_FALSE(world.Systems().IsEnabled("A"));
    world.Update(0.016f);
    world.Systems().SetEnabled("A", true);
    world.Update(0.016f);

    CHECK(trace == std::vector<std::string>{"A", "B", "B", "A", "B"});
    CHECK_FALSE(world.Systems().IsEnabled("不存在的系统"));
    world.Systems().SetEnabled("不存在的系统", false);  // 告警但不崩溃
}

TEST_CASE("公共组件：HashName 固定值、组件默认值与分类位") {
    // FNV-1a 32 固定值（与 Python 实现交叉验证过）
    CHECK(zl::ecs::HashName("") == 2166136261u);
    CHECK(zl::ecs::HashName("object.tree") == 2607164492u);
    CHECK(zl::ecs::HashName("tile.floor.grass") == 2555725025u);
    CHECK(zl::ecs::HashName("object.tree") != zl::ecs::HashName("object.bush"));

    const Position p;
    CHECK(p.x == 0.0f);
    CHECK(p.y == 0.0f);
    CHECK(p.z == 0);
    const Velocity v;
    CHECK(v.x == 0.0f);
    CHECK(v.y == 0.0f);
    const Collider c;
    CHECK(c.radius > 0.0f);
    CHECK(c.height > c.radius);
    CHECK(c.category == zl::ecs::kColliderCharacter);
    CHECK(c.mask == zl::ecs::kColliderAll);
    CHECK((zl::ecs::kColliderStatic & zl::ecs::kColliderAll) != 0u);  // 分类位独立
    CHECK((zl::ecs::kColliderCharacter & zl::ecs::kColliderStatic) == 0u);
    const SpriteRef s;
    CHECK(s.sprite == 0u);
    CHECK(s.tint == 0xFFFFFFFFu);
    const StableId sid;
    CHECK(sid.id == 0u);

    // 组件是平凡可拷贝的纯数据（EnTT storage 要求）
    static_assert(std::is_trivially_copyable_v<Position>);
    static_assert(std::is_trivially_copyable_v<Velocity>);
    static_assert(std::is_trivially_copyable_v<Collider>);
    static_assert(std::is_trivially_copyable_v<SpriteRef>);
    static_assert(std::is_trivially_copyable_v<StableId>);
}

TEST_CASE("压力：10000 实体三轮创建/销毁，storage 复用无泄漏（M4 验收）") {
    EcsWorld world;
    constexpr int kCount = 10000;
    std::size_t capacityAfterFirst = 0;

    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < kCount; ++i) {
            const auto e = world.Create();
            auto& reg = world.Registry();
            reg.emplace<Position>(e, Position{static_cast<float>(i % 1000), 
                                              static_cast<float>(i / 1000), 0});
            reg.emplace<Velocity>(e, Velocity{});
            reg.emplace<Collider>(e, Collider{});
            reg.emplace<SpriteRef>(e, SpriteRef{});
        }
        CHECK(world.AliveCount() == static_cast<std::size_t>(kCount));
        CHECK(world.StableIdCount() == static_cast<std::size_t>(kCount));

        world.Registry().view<Position>().each(
            [&](const entt::entity e, const Position&) { world.Destroy(e); });
        world.Flush();

        CHECK(world.AliveCount() == 0u);
        CHECK(world.StableIdCount() == 0u);
        CHECK(world.Registry().storage<Position>().size() == 0u);
        CHECK(world.Registry().storage<Velocity>().size() == 0u);
        CHECK(world.Registry().storage<Collider>().size() == 0u);
        CHECK(world.Registry().storage<SpriteRef>().size() == 0u);

        // 确认 EnTT storage 复用：第二轮起容量不再增长
        const std::size_t cap = world.Registry().storage<Position>().capacity();
        if (round == 0) {
            capacityAfterFirst = cap;
        } else {
            CHECK(cap == capacityAfterFirst);
        }
    }
}
