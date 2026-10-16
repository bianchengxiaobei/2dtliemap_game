#include <doctest/doctest.h>

#include <cmath>

#include "characters/components/CharacterComponents.h"
#include "characters/systems/AnimationSystem.h"
#include "characters/systems/InputSystem.h"
#include "characters/systems/MovementSystem.h"
#include "core/Input.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/RenderCollectSystem.h"
#include "physics/PhysicsSyncSystem.h"
#include "physics/PhysicsWorld.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"

using namespace zl;

using zl::ecs::AnimState;
using zl::ecs::Direction;
using zl::ecs::EcsWorld;
using zl::ecs::Facing;
using zl::ecs::LocalControl;
using zl::ecs::MoveIntent;
using zl::ecs::PlayerTag;
using zl::ecs::Position;
using zl::ecs::Velocity;
using zl::characters::AnimationSystem;
using zl::characters::InputSystem;
using zl::characters::MovementSystem;

namespace {

// 世界格方向 → 屏幕方向（IsoMath::ToScreen 的线性部分，用于校验"等距矫正"）
glm::vec2 ScreenOfGrid(const glm::vec2& g) {
    const float halfW = static_cast<float>(core::kTileW) * 0.5f;
    const float halfH = static_cast<float>(core::kTileH) * 0.5f;
    return {(g.x - g.y) * halfW, (g.x + g.y) * halfH};
}

constexpr float kSqrtHalf = 0.70710678f;

}  // namespace

TEST_CASE("InputSystem：屏幕方向 → 世界格方向（等距矫正，8 向往返一致）") {
    const glm::vec2 screenDirs[] = {{0, -1},  {0, 1},   {-1, 0}, {1, 0},
                                    {1, -1},  {-1, -1}, {1, 1},  {-1, 1}};
    for (const glm::vec2& s : screenDirs) {
        const glm::vec2 unitS = glm::normalize(s);
        const glm::vec2 g = InputSystem::ScreenDirToGridDir(unitS);
        // 反解回屏幕：方向必须与输入一致（投影是线性的，方向不因归一化改变）
        const glm::vec2 back = glm::normalize(ScreenOfGrid(g));
        CHECK(back.x == doctest::Approx(unitS.x).epsilon(0.0005));
        CHECK(back.y == doctest::Approx(unitS.y).epsilon(0.0005));
    }

    // 具体值：W = 屏幕上 → 世界西北（-1,-1）/√2；D = 屏幕右 → 世界东北（1,-1)/√2
    const glm::vec2 w = glm::normalize(
        InputSystem::ScreenDirToGridDir(glm::vec2(0.0f, -1.0f)));
    CHECK(w.x == doctest::Approx(-kSqrtHalf).epsilon(0.0005));
    CHECK(w.y == doctest::Approx(-kSqrtHalf).epsilon(0.0005));
    const glm::vec2 d = glm::normalize(InputSystem::ScreenDirToGridDir(glm::vec2(1.0f, 0.0f)));
    CHECK(d.x == doctest::Approx(kSqrtHalf).epsilon(0.0005));
    CHECK(d.y == doctest::Approx(-kSqrtHalf).epsilon(0.0005));
}

TEST_CASE("InputSystem：写 MoveIntent（速度缩放 + 屏幕语义 + 松键归零）") {
    EcsWorld world;
    const ecs::Entity player = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<PlayerTag>(player);
    registry.emplace<LocalControl>(player, LocalControl{4.0f});
    registry.emplace<MoveIntent>(player, MoveIntent{});

    // 非玩家实体（无 PlayerTag）：不应被写入
    const ecs::Entity npc = world.Create();
    registry.emplace<LocalControl>(npc, LocalControl{4.0f});
    registry.emplace<MoveIntent>(npc, MoveIntent{});

    core::Input input;
    input.Bind("MoveUp", 87);     // W
    input.Bind("MoveDown", 83);   // S
    input.Bind("MoveLeft", 65);   // A
    input.Bind("MoveRight", 68);  // D
    InputSystem system;

    // 无按键：意图归零
    system.Update(input, world);
    CHECK(registry.get<MoveIntent>(player).x == 0.0f);
    CHECK(registry.get<MoveIntent>(player).y == 0.0f);

    // 按 W：屏幕上方 → 世界 (-1,-1)/√2 × 速度
    input.SetKeyState(87, true);
    system.Update(input, world);
    {
        const MoveIntent& intent = registry.get<MoveIntent>(player);
        CHECK(intent.x == doctest::Approx(-4.0f * kSqrtHalf).epsilon(0.001));
        CHECK(intent.y == doctest::Approx(-4.0f * kSqrtHalf).epsilon(0.001));
        const float speed = std::sqrt(intent.x * intent.x + intent.y * intent.y);
        CHECK(speed == doctest::Approx(4.0f).epsilon(0.001));  // 世界速度恒定
    }
    // 非玩家未被动过
    CHECK(registry.get<MoveIntent>(npc).x == 0.0f);
    CHECK(registry.get<MoveIntent>(npc).y == 0.0f);

    // W+D（屏幕上右）：屏幕方向仍是 45°（等距矫正后世界方向不是轴对齐）
    input.SetKeyState(68, true);
    system.Update(input, world);
    {
        const MoveIntent& intent = registry.get<MoveIntent>(player);
        const glm::vec2 screen = ScreenOfGrid({intent.x, intent.y});
        CHECK(screen.x > 0.0f);
        CHECK(std::fabs(screen.x + screen.y) < 0.01f);  // 屏幕 45°（x + y == 0）
    }

    // 松键：归零
    input.SetKeyState(87, false);
    input.SetKeyState(68, false);
    system.Update(input, world);
    CHECK(registry.get<MoveIntent>(player).x == 0.0f);
    CHECK(registry.get<MoveIntent>(player).y == 0.0f);
}

TEST_CASE("MovementSystem：MoveIntent 覆盖 Velocity 并参与积分") {
    EcsWorld world;
    entt::registry& registry = world.Registry();

    const ecs::Entity mover = world.Create();
    registry.emplace<Position>(mover, Position{1.0f, 2.0f, 0});
    registry.emplace<Velocity>(mover, Velocity{9.0f, 9.0f});
    registry.emplace<MoveIntent>(mover, MoveIntent{3.0f, -1.5f});

    // 无意图实体：Velocity 原样积分
    const ecs::Entity stray = world.Create();
    registry.emplace<Position>(stray, Position{0.0f, 0.0f, 0});
    registry.emplace<Velocity>(stray, Velocity{2.0f, 0.0f});

    MovementSystem system;
    system.Update(world, 0.5f);

    CHECK(registry.get<Position>(mover).x == doctest::Approx(2.5f));   // 1 + 3×0.5
    CHECK(registry.get<Position>(mover).y == doctest::Approx(1.25f));  // 2 - 1.5×0.5
    CHECK(registry.get<Velocity>(mover).x == doctest::Approx(3.0f));   // 意图落到速度
    CHECK(registry.get<Position>(stray).x == doctest::Approx(1.0f));   // 2×0.5
}

TEST_CASE("AnimationSystem：速度 → 4 向朝向（屏幕主轴）") {
    // WASD 意图方向（InputSystem 的输出）→ 4 向必须与按键屏幕语义一致
    CHECK(AnimationSystem::FacingFromVelocity(-kSqrtHalf, -kSqrtHalf) == Direction::Up);    // W
    CHECK(AnimationSystem::FacingFromVelocity(kSqrtHalf, -kSqrtHalf) == Direction::Right);  // D
    CHECK(AnimationSystem::FacingFromVelocity(-kSqrtHalf, kSqrtHalf) == Direction::Left);   // A
    CHECK(AnimationSystem::FacingFromVelocity(kSqrtHalf, kSqrtHalf) == Direction::Down);    // S
    // 世界格轴方向（AI/寻路沿格轴移动）：屏幕上是 45° 对角，|sx|==|sy| 平局 → 取上下轴（确定性）
    CHECK(AnimationSystem::FacingFromVelocity(0.0f, -1.0f) == Direction::Up);    // 格北
    CHECK(AnimationSystem::FacingFromVelocity(0.0f, 1.0f) == Direction::Down);   // 格南
    CHECK(AnimationSystem::FacingFromVelocity(-1.0f, 0.0f) == Direction::Up);    // 格西（平局）
    CHECK(AnimationSystem::FacingFromVelocity(1.0f, 0.0f) == Direction::Down);   // 格东（平局）
}

TEST_CASE("AnimationSystem：行走帧推进 + 站立复位（朝向保持）") {
    EcsWorld world;
    const ecs::Entity e = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<Velocity>(e, Velocity{1.0f, 0.0f});
    registry.emplace<Facing>(e, Facing{});
    AnimState anim;
    anim.fps = 8.0f;
    anim.framesPerDir = 2;
    registry.emplace<AnimState>(e, anim);

    AnimationSystem system;
    // 0.1s → t=0.1 → frame 0；再 0.1 → t=0.2 → frame 1；再 0.1 → t=0.3 → 2.4 → frame 0
    system.Update(world, 0.1f);
    CHECK(registry.get<AnimState>(e).frame == 0);
    system.Update(world, 0.1f);
    CHECK(registry.get<AnimState>(e).frame == 1);
    system.Update(world, 0.1f);
    CHECK(registry.get<AnimState>(e).frame == 0);
    // 速度 (1,0) = 格东（屏幕右下 45°）→ 平局取上下 → 朝下
    CHECK(registry.get<Facing>(e).dir == Direction::Down);

    // 静止：帧复位 0、计时清零；朝向保持最后值
    registry.get<Velocity>(e).x = 0.0f;
    registry.get<Velocity>(e).y = 0.0f;
    system.Update(world, 0.1f);
    CHECK(registry.get<AnimState>(e).frame == 0);
    CHECK(registry.get<AnimState>(e).time == 0.0f);
    CHECK(registry.get<Facing>(e).dir == Direction::Down);

    // framesPerDir=1：恒为第 0 帧，不除零
    registry.get<Velocity>(e).x = 1.0f;
    registry.get<AnimState>(e).framesPerDir = 1;
    for (int i = 0; i < 10; ++i) {
        system.Update(world, 0.1f);
    }
    CHECK(registry.get<AnimState>(e).frame == 0);
}

TEST_CASE("RenderCollectSystem::PickFrameIndex：动画实体按 dir*N+frame，静态实体哈希变体") {
    AnimState anim;
    anim.framesPerDir = 2;
    anim.frame = 1;
    Facing facing;

    facing.dir = Direction::Left;  // 1
    CHECK(zl::game::RenderCollectSystem::PickFrameIndex(&anim, &facing, 0, 8) == 3u);
    facing.dir = Direction::Up;  // 3
    CHECK(zl::game::RenderCollectSystem::PickFrameIndex(&anim, &facing, 0, 8) == 7u);

    // 无动画组件：HashPick 确定性
    CHECK(zl::game::RenderCollectSystem::PickFrameIndex(nullptr, nullptr, 12345u, 8) ==
          zl::render::HashPick(std::uint64_t{12345}, 0x5EC7u) % 8u);
    // 帧数保护：图集帧数少于约定时按模收敛
    facing.dir = Direction::Up;
    CHECK(zl::game::RenderCollectSystem::PickFrameIndex(&anim, &facing, 0, 4) == 3u);
    CHECK(zl::game::RenderCollectSystem::PickFrameIndex(&anim, &facing, 0, 1) == 0u);
    // framesPerDir=0：退化为哈希变体
    anim.framesPerDir = 0;
    CHECK(zl::game::RenderCollectSystem::PickFrameIndex(&anim, &facing, 12345u, 8) ==
          zl::render::HashPick(std::uint64_t{12345}, 0x5EC7u) % 8u);
}

TEST_CASE("player_chain 全链路：输入 → MoveIntent → 物理 → 同步 → 动画（顶墙滑动 + 松键站立）") {
    physics::PhysicsWorld physics;
    world::Chunk chunk(0, 0);
    for (int ly = 4; ly <= 9; ++ly) {  // 纵向墙列（chunk 局部 0..9）：玩家沿墙滑动不会绕过
        chunk.Square(5, ly, 0).AddObject(world::SquareLayer::Wall,
                                         {"wall.brick", 0, 0, true});
    }
    physics.BuildChunkStatics(0, 0, chunk);

    EcsWorld world;
    entt::registry& registry = world.Registry();
    const ecs::Entity player = world.Create();
    registry.emplace<Position>(player, Position{3.0f, 5.0f, 0});
    registry.emplace<Velocity>(player, Velocity{});
    registry.emplace<ecs::RigidBody>(
        player, ecs::RigidBody{physics.CreateCharacterBody(0.3f, 1.8f, 3.0f, 5.0f, 0)});
    registry.emplace<PlayerTag>(player);
    registry.emplace<LocalControl>(player, LocalControl{4.0f});
    registry.emplace<MoveIntent>(player, MoveIntent{});
    registry.emplace<Facing>(player, Facing{});
    registry.emplace<AnimState>(player, AnimState{});

    core::Input input;
    input.Bind("MoveUp", 87);
    input.Bind("MoveDown", 83);
    input.Bind("MoveLeft", 65);
    input.Bind("MoveRight", 68);
    InputSystem inputSystem;
    MovementSystem movementSystem;
    physics::PhysicsSyncSystem syncSystem(&physics);
    AnimationSystem animSystem;

    constexpr float kDt = 1.0f / 60.0f;
    const auto runTick = [&] {
        inputSystem.Update(input, world);
        movementSystem.Update(world, kDt);
        syncSystem.Update(world, kDt);
        animSystem.Update(world, kDt);
    };

    // 按 S+D（屏幕下右 → 世界主 +x 带 +y）：x 被墙列挡住（4.5 - 半径 0.3），沿墙滑向 +y
    input.SetKeyState(83, true);
    input.SetKeyState(68, true);
    bool sawFrame1 = false;
    for (int i = 0; i < 200; ++i) {  // 3.33s：滑到墙列末端（y≈9.2 < 9.8 接触区）前结束
        runTick();
        if (registry.get<AnimState>(player).frame == 1) {
            sawFrame1 = true;  // 行走帧确实在交替
        }
    }
    const Position& pos = registry.get<Position>(player);
    CHECK(pos.x < 4.35f);  // 没穿墙（墙在 x=5，西边 4.5）
    CHECK(pos.x > 3.9f);
    CHECK(pos.y > 6.0f);  // 沿墙滑走（5 + 1.26×3.3s ≈ 9.2）
    CHECK(sawFrame1);

    // 松键：速度立即归零（意图→速度覆盖），动画复位站立帧，位置不再变
    input.SetKeyState(83, false);
    input.SetKeyState(68, false);
    for (int i = 0; i < 10; ++i) {
        runTick();
    }
    const Position stopped = registry.get<Position>(player);
    CHECK(registry.get<Velocity>(player).x == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(registry.get<AnimState>(player).frame == 0);
    for (int i = 0; i < 10; ++i) {
        runTick();
    }
    CHECK(registry.get<Position>(player).x == doctest::Approx(stopped.x).epsilon(0.001));
    CHECK(registry.get<Position>(player).y == doctest::Approx(stopped.y).epsilon(0.001));

    physics.DestroyBody(registry.get<ecs::RigidBody>(player).body);
}
