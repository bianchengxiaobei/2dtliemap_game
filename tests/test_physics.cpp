#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <btBulletDynamicsCommon.h>

#include "characters/systems/MovementSystem.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "physics/PhysicsSyncSystem.h"
#include "physics/PhysicsWorld.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"
#include "world/MapLoader.h"

using namespace zl;

using zl::ecs::EcsWorld;
using zl::ecs::Position;
using zl::ecs::Velocity;
using zl::physics::PhysicsWorld;

namespace {

void FillChunk(world::Chunk& chunk,
               const std::vector<std::tuple<int, int, world::SquareLayer, const char*, bool>>&
                   entries) {
    for (const auto& [lx, ly, layer, sprite, solid] : entries) {
        chunk.Square(lx, ly, 0).AddObject(layer, {sprite, 0, 0, solid});
    }
}

// 直线推进：每 tick 把速度写进体（模拟 MovementSystem 的行为）
void RunSteps(PhysicsWorld& physics, btRigidBody* body, float vx, float vy, int steps) {
    for (int i = 0; i < steps; ++i) {
        body->activate();
        body->setLinearVelocity(btVector3(vx, vy, 0.0f));
        physics.Step(1.0f / 60.0f);
    }
}

float BodyX(const btRigidBody* body) {
    return static_cast<float>(body->getWorldTransform().getOrigin().x());
}
float BodyY(const btRigidBody* body) {
    return static_cast<float>(body->getWorldTransform().getOrigin().y());
}

void WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << text;
}

}  // namespace

TEST_CASE("PhysicsWorld：静态盒阻挡角色（停位精度）+ 禁旋转/锁 z 生效") {
    PhysicsWorld physics;
    world::Chunk chunk(0, 0);
    FillChunk(chunk, {{5, 5, world::SquareLayer::Wall, "wall.brick", true}});
    physics.BuildChunkStatics(0, 0, chunk);
    CHECK(physics.StaticBodyCount() == 1u);

    btRigidBody* body = physics.CreateCharacterBody(0.3f, 1.8f, 3.0f, 5.0f, 0);
    RunSteps(physics, body, 4.0f, 0.0f, 240);  // 4 秒向 +x 顶墙

    // 墙西边 = 5 - 0.5 = 4.5；半径 0.3 → 停在 ~4.2
    CHECK(BodyX(body) < 4.35f);
    CHECK(BodyX(body) > 4.0f);
    CHECK(BodyY(body) == doctest::Approx(5.0f).epsilon(0.05));
    CHECK(body->getWorldTransform().getOrigin().z() ==
          doctest::Approx(0.9f));  // 锁 z：中心恒在总高一半
    CHECK(body->getAngularVelocity().length() == doctest::Approx(0.0f));
    physics.DestroyBody(body);
    CHECK(physics.DynamicBodyCount() == 0u);
}

TEST_CASE("PhysicsWorld：CCD 高速不穿墙（90 格/s 顶墙）") {
    PhysicsWorld physics;
    world::Chunk chunk(0, 0);
    FillChunk(chunk, {{5, 5, world::SquareLayer::Wall, "wall.brick", true}});
    physics.BuildChunkStatics(0, 0, chunk);

    btRigidBody* body = physics.CreateCharacterBody(0.3f, 1.8f, 3.0f, 5.0f, 0);
    RunSteps(physics, body, 90.0f, 0.0f, 120);  // 每步 1.5 格（大于墙厚）：无 CCD 会穿过

    CHECK(BodyX(body) < 4.35f);  // 没穿到墙东侧（5.5+）
    CHECK(BodyX(body) > 3.5f);
}

TEST_CASE("PhysicsWorld：沿墙滑动（x 被挡、y 分量保留）") {
    PhysicsWorld physics;
    std::vector<std::tuple<int, int, world::SquareLayer, const char*, bool>> walls;
    for (int ly = 0; ly <= 8; ++ly) {
        walls.emplace_back(5, ly, world::SquareLayer::Wall, "wall.brick", true);
    }
    world::Chunk chunk(0, 0);
    FillChunk(chunk, walls);
    physics.BuildChunkStatics(0, 0, chunk);
    CHECK(physics.StaticBodyCount() == 9u);

    btRigidBody* body = physics.CreateCharacterBody(0.3f, 1.8f, 3.0f, 5.0f, 0);
    RunSteps(physics, body, 4.0f, 2.0f, 90);  // 斜向顶墙 1.5 秒

    CHECK(BodyX(body) < 4.35f);  // x 被墙挡住
    CHECK(BodyY(body) > 7.5f);   // y 沿墙滑走（5 + 2×1.5 = 8）
}

TEST_CASE("PhysicsWorld：顶墙后反向可恢复（卡墙代理场景）") {
    PhysicsWorld physics;
    world::Chunk chunk(0, 0);
    FillChunk(chunk, {{5, 5, world::SquareLayer::Wall, "wall.brick", true}});
    physics.BuildChunkStatics(0, 0, chunk);

    btRigidBody* body = physics.CreateCharacterBody(0.3f, 1.8f, 3.0f, 5.0f, 0);
    RunSteps(physics, body, 4.0f, 0.0f, 120);  // 顶到墙上
    CHECK(BodyX(body) < 4.35f);

    RunSteps(physics, body, -4.0f, 0.0f, 60);  // 反向拉出
    CHECK(BodyX(body) < 3.5f);
}

TEST_CASE("PhysicsWorld：chunk 静态体批量生成/幂等/卸载") {
    PhysicsWorld physics;
    world::Chunk chunk(0, 0);
    FillChunk(chunk,
              {
                  {1, 1, world::SquareLayer::Wall, "wall.brick", true},
                  {2, 2, world::SquareLayer::Wall, "wall.brick", true},
                  {3, 3, world::SquareLayer::Furniture, "object.tree", true},
                  {4, 4, world::SquareLayer::Furniture, "decor.grass_tuft", false},
              });

    physics.BuildChunkStatics(0, 0, chunk);
    CHECK(physics.StaticBodyCount() == 3u);  // 草丛不生成
    CHECK(physics.ChunkStaticBodyCount(0, 0) == 3u);

    physics.BuildChunkStatics(0, 0, chunk);  // 幂等：不重复累积
    CHECK(physics.StaticBodyCount() == 3u);

    world::Chunk other(1, 0);
    FillChunk(other, {{0, 0, world::SquareLayer::Wall, "wall.brick", true}});
    physics.BuildChunkStatics(1, 0, other);
    CHECK(physics.StaticBodyCount() == 4u);
    CHECK(physics.ChunkStaticBodyCount(1, 0) == 1u);

    physics.RemoveChunkStatics(0, 0);
    CHECK(physics.StaticBodyCount() == 1u);
    CHECK(physics.ChunkStaticBodyCount(0, 0) == 0u);
    physics.RemoveChunkStatics(0, 0);  // 重复移除安全
    CHECK(physics.StaticBodyCount() == 1u);

    physics.ClearAll();
    CHECK(physics.StaticBodyCount() == 0u);
}

TEST_CASE("PhysicsWorld：调试线框开关与采集") {
    PhysicsWorld physics;
    world::Chunk chunk(0, 0);
    FillChunk(chunk, {{5, 5, world::SquareLayer::Wall, "wall.brick", true}});
    physics.BuildChunkStatics(0, 0, chunk);

    CHECK_FALSE(physics.DebugDrawEnabled());
    physics.Step(1.0f / 60.0f);
    CHECK(physics.DebugLines().empty());  // 未开启不采集

    physics.SetDebugDraw(true);
    physics.Step(1.0f / 60.0f);
    // 场景里只有一个静态盒 → 恰好 12 条棱（若为 0 说明 Bullet 没画静态体）
    CHECK(physics.DebugLines().size() == 12u);

    static_cast<void>(physics.CreateCharacterBody(0.3f, 1.8f, 3.0f, 5.0f, 0));
    physics.Step(1.0f / 60.0f);
    CHECK(physics.DebugLines().size() > 12u);  // 盒 + 胶囊

    physics.SetDebugDraw(false);
    CHECK(physics.DebugLines().empty());  // 关闭即清空
}

TEST_CASE("MovementSystem：无物理实体直接积分；有物理实体只写速度不动位置") {
    EcsWorld world;
    zl::characters::MovementSystem movement;

    const auto simple = world.Create();
    world.Registry().emplace<Position>(simple, Position{1.0f, 2.0f, 0});
    world.Registry().emplace<Velocity>(simple, Velocity{3.0f, -2.0f});
    movement.Update(world, 0.5f);
    CHECK(world.Registry().get<Position>(simple).x == doctest::Approx(2.5f));
    CHECK(world.Registry().get<Position>(simple).y == doctest::Approx(1.0f));

    PhysicsWorld physics;
    const auto physical = world.Create();
    world.Registry().emplace<Position>(physical, Position{10.0f, 10.0f, 0});
    world.Registry().emplace<Velocity>(physical, Velocity{1.0f, 1.0f});
    zl::ecs::RigidBody rigid;
    rigid.body = physics.CreateCharacterBody(0.3f, 1.8f, 10.0f, 10.0f, 0);
    world.Registry().emplace<zl::ecs::RigidBody>(physical, rigid);

    movement.Update(world, 0.5f);
    CHECK(world.Registry().get<Position>(physical).x == doctest::Approx(10.0f));  // 不直接积分
    const btVector3 linear = rigid.body->getLinearVelocity();
    CHECK(linear.x() == doctest::Approx(1.0f));
    CHECK(linear.y() == doctest::Approx(1.0f));
}

TEST_CASE("全链路：MovementSystem + PhysicsSyncSystem 顶墙 → Position 停止、速度回写归零") {
    PhysicsWorld physics;
    world::Chunk chunk(0, 0);
    FillChunk(chunk, {{5, 5, world::SquareLayer::Wall, "wall.brick", true}});
    physics.BuildChunkStatics(0, 0, chunk);

    EcsWorld world;
    const auto e = world.Create();
    world.Registry().emplace<Position>(e, Position{3.0f, 5.0f, 0});
    world.Registry().emplace<Velocity>(e, Velocity{4.0f, 0.0f});
    world.Registry().emplace<zl::ecs::Collider>(e, zl::ecs::Collider{});
    zl::ecs::RigidBody rigid;
    rigid.body = physics.CreateCharacterBody(0.3f, 1.8f, 3.0f, 5.0f, 0);
    world.Registry().emplace<zl::ecs::RigidBody>(e, rigid);

    zl::characters::MovementSystem movement;
    zl::physics::PhysicsSyncSystem sync(&physics);
    for (int i = 0; i < 240; ++i) {
        movement.Update(world, 1.0f / 60.0f);
        sync.Update(world, 1.0f / 60.0f);
    }

    const Position& position = world.Registry().get<Position>(e);
    CHECK(position.x < 4.35f);
    CHECK(position.x > 4.0f);
    CHECK(position.y == doctest::Approx(5.0f).epsilon(0.05));
    CHECK(position.z == 0);
    // 撞墙后实际速度回写：x 分量被物理归零
    CHECK(world.Registry().get<Velocity>(e).x < 1.0f);
}

TEST_CASE("预算冒烟：500 静态体 + 50 动态体步进（Debug 宽松上限）") {
    PhysicsWorld physics;
    // 5 个 chunk × 100 格全墙 = 500 静态体
    for (int cx = 0; cx < 5; ++cx) {
        std::vector<std::tuple<int, int, world::SquareLayer, const char*, bool>> walls;
        for (int ly = 0; ly < 10; ++ly) {
            for (int lx = 0; lx < 10; ++lx) {
                walls.emplace_back(lx, ly, world::SquareLayer::Wall, "wall.brick", true);
            }
        }
        world::Chunk chunk(cx, 0);
        FillChunk(chunk, walls);
        physics.BuildChunkStatics(cx, 0, chunk);
    }
    CHECK(physics.StaticBodyCount() == 500u);

    std::mt19937 rng(7u);
    std::uniform_real_distribution<float> u(-60.0f, 60.0f);
    std::vector<btRigidBody*> bodies;
    for (int i = 0; i < 50; ++i) {
        bodies.push_back(physics.CreateCharacterBody(0.3f, 1.8f, u(rng), u(rng), 0));
    }

    const auto begin = std::chrono::steady_clock::now();
    for (int tick = 0; tick < 60; ++tick) {
        for (auto* body : bodies) {
            body->activate();
            body->setLinearVelocity(btVector3(u(rng), u(rng), 0.0f));
        }
        physics.Step(1.0f / 60.0f);
    }
    const double totalMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin)
            .count();
    const double avgMs = totalMs / 60.0;
    CHECK(physics.AvgStepMs() > 0.0);
    CHECK(avgMs < 20.0);  // Debug 宽松上限（正式验收按 Release < 2ms 记录到 perf-baseline）
}

TEST_CASE("MapLoader：objects solid 字段解析（缺省 false / 非法跳过）") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zl_physics_solid_test";
    std::filesystem::remove_all(dir);
    WriteTextFile(dir / "map.json",
                  R"({"format":"zomboidlike-map-v1","name":"t","bounds":{"chunkX0":0,"chunkY0":0,"chunksX":10,"chunksY":10},"terrain":{"defaultFloor":"tile.floor.grass"},"legend":{"g":"tile.floor.grass"}})");
    WriteTextFile(
        dir / "chunks" / "c0_0" / "chunk_0_0.json",
        R"({"cx":0,"cy":0,"objects":[
            {"x":1,"y":1,"layer":"wall","sprite":"wall.brick","solid":true},
            {"x":2,"y":2,"layer":"furniture","sprite":"object.tree"},
            {"x":3,"y":3,"layer":"furniture","sprite":"object.tree","solid":false},
            {"x":4,"y":4,"layer":"furniture","sprite":"decor.grass_tuft","solid":"yes"}
        ]})");

    world::MapLoader::Meta meta;
    std::string error;
    REQUIRE(world::MapLoader::LoadMeta(dir.string(), &meta, &error));
    world::ChunkData data;
    REQUIRE(world::MapLoader::LoadChunk(meta, 0, 0, &data, &error));
    REQUIRE(data.objects.size() == 3u);  // solid 非布尔的条目被跳过
    CHECK(data.objects[0].solid);
    CHECK_FALSE(data.objects[1].solid);  // 缺省
    CHECK_FALSE(data.objects[2].solid);  // 显式 false
    std::filesystem::remove_all(dir);
}
