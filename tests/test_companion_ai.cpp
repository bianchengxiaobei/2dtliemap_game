#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "ai/Passability.h"
#include "ai/PathfindingService.h"
#include "ai/components/AiComponents.h"
#include "ai/systems/PathFollowSystem.h"
#include "characters/components/CharacterComponents.h"
#include "characters/systems/MovementSystem.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/CompanionSystem.h"
#include "game/HomesteadComponents.h"
#include "game/Traps.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"
#include "world/WorldCoords.h"

using namespace zl;

namespace {

constexpr float kDt = 1.0f / 60.0f;

struct CompanionAiRig {
    ai::PathfindingService service{nullptr, ai::MapChunkBounds{0, 0, 3, 3}};
    ecs::EcsWorld world;
    std::vector<ecs::Entity> damageTargets;
    game::TrapDefsConfig traps;
    game::CompanionSystem system;
    ai::PathFollowSystem followSystem;
    characters::MovementSystem movement;

    explicit CompanionAiRig(
        const std::vector<std::pair<int, int>>& walls = {})
        : system(
              [this](int wx, int wy) { return service.IsPassable(wx, wy); },
              [this](ecs::Entity, ecs::Entity target, float damage,
                     ecs::DamageType) {
                  auto* health =
                      world.Registry().try_get<ecs::Health>(target);
                  if (health == nullptr || health->current <= 0.0f) {
                      return false;
                  }
                  health->current = std::max(0.0f, health->current - damage);
                  damageTargets.push_back(target);
                  return true;
              }),
          followSystem(&service) {
        game::TrapDef placedTrap;
        placedTrap.id = "bear_trap";
        placedTrap.name = "捕兽夹";
        placedTrap.buildingId = "bear_trap";
        placedTrap.radius = 2.0f;
        placedTrap.uses = 4;
        placedTrap.triggerMask = game::kTrapTriggerDefault;
        traps.enabled = true;
        traps.defs.push_back(std::move(placedTrap));
        system.SetTrapDefs(&traps);
        for (int cy = 0; cy < 3; ++cy) {
            for (int cx = 0; cx < 3; ++cx) {
                world::Chunk chunk(cx, cy);
                for (const auto& [gx, gy] : walls) {
                    const world::ChunkCoord coord = world::ChunkOf(gx, gy);
                    if (coord.cx != cx || coord.cy != cy) {
                        continue;
                    }
                    const world::SquareLocal local =
                        world::SquareLocalOf(gx, gy);
                    chunk.Square(local.lx, local.ly, 0)
                        .AddObject(world::SquareLayer::Wall,
                                   {"wall.brick", 0, 0, true});
                }
                service.OnChunkLoaded(chunk);
            }
        }
    }

    ecs::Entity AddPlayer(float x, float y,
                          ecs::Direction facing = ecs::Direction::Right) {
        const ecs::Entity entity = world.Create();
        entt::registry& registry = world.Registry();
        registry.emplace<ecs::Position>(entity, ecs::Position{x, y, 0});
        registry.emplace<ecs::Velocity>(entity, ecs::Velocity{});
        registry.emplace<ecs::Collider>(entity, ecs::Collider{});
        registry.emplace<ecs::Health>(entity, ecs::Health{100.0f, 100.0f});
        registry.emplace<ecs::PlayerTag>(entity);
        registry.emplace<ecs::Facing>(entity, ecs::Facing{facing});
        registry.emplace<ecs::ActionState>(entity, ecs::ActionState{});
        registry.emplace<ecs::AttackIntent>(entity, ecs::AttackIntent{});
        registry.emplace<ecs::MoveIntent>(entity, ecs::MoveIntent{});
        return entity;
    }

    ecs::Entity AddCompanion(ecs::Entity owner, float x, float y,
                             float health = 100.0f) {
        const ecs::Entity entity = world.Create();
        entt::registry& registry = world.Registry();
        registry.emplace<ecs::Position>(entity, ecs::Position{x, y, 0});
        registry.emplace<ecs::Velocity>(entity, ecs::Velocity{});
        registry.emplace<ecs::Health>(entity, ecs::Health{health, 100.0f});
        registry.emplace<ecs::Facing>(entity, ecs::Facing{});
        registry.emplace<ecs::AnimState>(entity, ecs::AnimState{});
        registry.emplace<ecs::ActionState>(entity, ecs::ActionState{});
        registry.emplace<ecs::MoveIntent>(entity, ecs::MoveIntent{});
        registry.emplace<ecs::PathRequest>(entity, ecs::PathRequest{});
        ecs::PathFollow follow;
        follow.speed = 3.6f;
        registry.emplace<ecs::PathFollow>(entity, follow);
        registry.emplace<ecs::FriendlyNpc>(entity);
        registry.emplace<ecs::NoCombatTarget>(entity);
        registry.emplace<game::Companion>(
            entity, game::Companion{"companion_medic_lin",
                                    world.IdOf(owner).id, "village_haven"});
        game::CompanionState state;
        state.inventory.capacityKg = 12.0f;
        registry.emplace<game::CompanionState>(entity, state);
        game::CompanionAiState ai;
        ai.anchorX = x;
        ai.anchorY = y;
        ai.homeX = x;
        ai.homeY = y;
        registry.emplace<game::CompanionAiState>(entity, ai);
        return entity;
    }

    ecs::Entity AddZombie(float x, float y) {
        const ecs::Entity entity = world.Create();
        entt::registry& registry = world.Registry();
        registry.emplace<ecs::Position>(entity, ecs::Position{x, y, 0});
        registry.emplace<ecs::Velocity>(entity, ecs::Velocity{});
        registry.emplace<ecs::Collider>(entity, ecs::Collider{});
        registry.emplace<ecs::Health>(entity, ecs::Health{100.0f, 100.0f});
        registry.emplace<ecs::AiState>(entity, ecs::AiState{});
        registry.emplace<ecs::PerceptionState>(entity,
                                               ecs::PerceptionState{});
        registry.emplace<ecs::MoveIntent>(entity, ecs::MoveIntent{});
        return entity;
    }

    ecs::Entity AddFriendly(float x, float y) {
        const ecs::Entity entity = world.Create();
        entt::registry& registry = world.Registry();
        registry.emplace<ecs::Position>(entity, ecs::Position{x, y, 0});
        registry.emplace<ecs::Health>(entity, ecs::Health{100.0f, 100.0f});
        registry.emplace<ecs::FriendlyNpc>(entity);
        return entity;
    }

    ecs::Entity AddTrap(float x, float y, float radius = 1.0f) {
        const ecs::Entity entity = world.Create();
        entt::registry& registry = world.Registry();
        registry.emplace<ecs::Position>(entity, ecs::Position{x, y, 0});
        ecs::SpikeTrap trap;
        trap.radius = radius;
        registry.emplace<ecs::SpikeTrap>(entity, trap);
        return entity;
    }

    ecs::Entity AddPlacedTrap(float x, float y) {
        const ecs::Entity entity = world.Create();
        entt::registry& registry = world.Registry();
        registry.emplace<ecs::Position>(entity, ecs::Position{x, y, 0});
        ecs::PlacedTrap trap;
        trap.defId = "bear_trap";
        trap.armed = true;
        trap.uses = 4;
        registry.emplace<ecs::PlacedTrap>(entity, trap);
        return entity;
    }

    void Tick() {
        system.Update(world, kDt);
        followSystem.Update(world, kDt);
        movement.Update(world, kDt);
        world.Flush();
    }

    void Run(int ticks) {
        for (int i = 0; i < ticks; ++i) {
            Tick();
        }
    }

    ecs::Position& Pos(ecs::Entity entity) {
        return world.Registry().get<ecs::Position>(entity);
    }
    game::CompanionAiState& Ai(ecs::Entity entity) {
        return world.Registry().get<game::CompanionAiState>(entity);
    }
    ecs::PathRequest& Request(ecs::Entity entity) {
        return world.Registry().get<ecs::PathRequest>(entity);
    }
    ecs::PathFollow& Follow(ecs::Entity entity) {
        return world.Registry().get<ecs::PathFollow>(entity);
    }
};

float Distance(float ax, float ay, float bx, float by) {
    return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
}

}  // namespace

TEST_CASE("M33-F3-T3 跟随距离、玩家背后落点与停留") {
    CompanionAiRig rig;
    const ecs::Entity player =
        rig.AddPlayer(10.5f, 10.5f, ecs::Direction::Right);
    const ecs::Entity companion = rig.AddCompanion(player, 20.5f, 10.5f);

    rig.Tick();
    CHECK(rig.Request(companion).goalX < 10);
    CHECK(rig.system.GetStats().follow == 1);

    rig.Run(480);
    const float followed = Distance(rig.Pos(companion).x,
                                    rig.Pos(companion).y,
                                    rig.Pos(player).x, rig.Pos(player).y);
    CHECK(followed >= 1.5f);
    CHECK(followed <= 4.5f);

    game::CompanionState& state =
        rig.world.Registry().get<game::CompanionState>(companion);
    state.command = game::CompanionCommand::Stay;
    rig.Tick();
    const float stayX = rig.Pos(companion).x;
    const float stayY = rig.Pos(companion).y;
    rig.Pos(player).x = 26.5f;
    rig.Run(240);
    CHECK(rig.Pos(companion).x == doctest::Approx(stayX).epsilon(0.2));
    CHECK(rig.Pos(companion).y == doctest::Approx(stayY).epsilon(0.2));
    CHECK(rig.Request(companion).state == ecs::PathStatus::None);
    CHECK(rig.system.GetStats().stay > 0);
}

TEST_CASE("M33-F3-T3 站住、撤退与低血自动撤退") {
    SUBCASE("停留后撤退回玩家") {
        CompanionAiRig rig;
        const ecs::Entity player = rig.AddPlayer(18.5f, 10.5f);
        const ecs::Entity companion = rig.AddCompanion(player, 18.5f, 10.5f);
        game::CompanionState& state =
            rig.world.Registry().get<game::CompanionState>(companion);
        state.command = game::CompanionCommand::Stay;
        rig.Tick();
        const float anchorX = rig.Pos(companion).x;
        rig.Pos(player).x = 28.5f;
        rig.Run(60);
        CHECK(rig.Pos(companion).x == doctest::Approx(anchorX).epsilon(0.2));

        state.command = game::CompanionCommand::Retreat;
        const float before = Distance(rig.Pos(companion).x,
                                      rig.Pos(companion).y,
                                      rig.Pos(player).x, rig.Pos(player).y);
        rig.Run(360);
        const float after = Distance(rig.Pos(companion).x,
                                     rig.Pos(companion).y,
                                     rig.Pos(player).x, rig.Pos(player).y);
        CHECK(after < before);
        CHECK(after <= 4.0f);
        CHECK(rig.system.GetStats().retreat > 0);
    }

    SUBCASE("低血自动撤退且不攻击") {
        CompanionAiRig rig;
        const ecs::Entity player = rig.AddPlayer(20.5f, 10.5f);
        const ecs::Entity companion =
            rig.AddCompanion(player, 8.5f, 10.5f, 10.0f);
        const ecs::Entity zombie = rig.AddZombie(19.5f, 10.5f);
        rig.Tick();
        CHECK(rig.Ai(companion).autoRetreat);
        CHECK(rig.Ai(companion).targetId == 0);
        CHECK(rig.system.GetStats().retreat == 1);
        CHECK(rig.system.GetStats().attacks == 0);
        CHECK(rig.Request(companion).goalX >= 18);
        CHECK(rig.world.Registry().get<ecs::Health>(zombie).current ==
              doctest::Approx(100.0f));
    }
}

TEST_CASE("M33-F3-T3 战斗目标、不拉怪与友伤过滤") {
    SUBCASE("只攻击敌对目标，玩家和友方不掉血") {
        CompanionAiRig rig;
        const ecs::Entity player = rig.AddPlayer(12.5f, 10.5f);
        const ecs::Entity companion = rig.AddCompanion(player, 10.5f, 10.5f);
        const ecs::Entity zombie = rig.AddZombie(9.5f, 10.5f);
        const ecs::Entity friendly = rig.AddFriendly(10.0f, 9.5f);
        CHECK_FALSE(game::IsCompanionHostileTarget(rig.world, player));
        CHECK_FALSE(game::IsCompanionHostileTarget(rig.world, friendly));
        CHECK_FALSE(game::IsCompanionHostileTarget(rig.world, companion));
        CHECK(game::IsCompanionHostileTarget(rig.world, zombie));

        rig.Run(60);
        CHECK(rig.world.Registry().get<ecs::Health>(zombie).current < 100.0f);
        CHECK(rig.world.Registry().get<ecs::Health>(player).current ==
              doctest::Approx(100.0f));
        CHECK(rig.world.Registry().get<ecs::Health>(friendly).current ==
              doctest::Approx(100.0f));
        REQUIRE(!rig.damageTargets.empty());
        for (const ecs::Entity target : rig.damageTargets) {
            CHECK(target == zombie);
        }
    }

    SUBCASE("远处威胁不主动拉怪，玩家开战后才接战") {
        CompanionAiRig rig;
        const ecs::Entity player = rig.AddPlayer(20.5f, 10.5f);
        const ecs::Entity companion = rig.AddCompanion(player, 20.5f, 10.5f);
        const ecs::Entity zombie = rig.AddZombie(26.0f, 10.5f);
        rig.Run(60);
        CHECK(rig.Ai(companion).targetId == 0);
        CHECK(rig.system.GetStats().attacks == 0);

        rig.world.Registry().get<ecs::ActionState>(player).action =
            ecs::ActorAction::Attacking;
        rig.Tick();
        CHECK(rig.Ai(companion).targetId == rig.world.IdOf(zombie).id);
    }

    SUBCASE("多个威胁选择离玩家最近的") {
        CompanionAiRig rig;
        const ecs::Entity player = rig.AddPlayer(20.5f, 10.5f);
        const ecs::Entity companion = rig.AddCompanion(player, 18.5f, 10.5f);
        const ecs::Entity near = rig.AddZombie(21.5f, 10.5f);
        const ecs::Entity far = rig.AddZombie(24.0f, 10.5f);
        rig.Tick();
        CHECK(rig.Ai(companion).targetId == rig.world.IdOf(near).id);
        CHECK(rig.Ai(companion).targetId != rig.world.IdOf(far).id);
    }

    SUBCASE("目标超出 leash 后脱战") {
        CompanionAiRig rig;
        const ecs::Entity player = rig.AddPlayer(10.5f, 10.5f);
        const ecs::Entity companion = rig.AddCompanion(player, 10.5f, 10.5f);
        const ecs::Entity zombie = rig.AddZombie(12.5f, 10.5f);
        rig.Tick();
        REQUIRE(rig.Ai(companion).targetId == rig.world.IdOf(zombie).id);
        rig.Pos(zombie).x = 27.5f;
        rig.Tick();
        CHECK(rig.Ai(companion).targetId == 0);
    }

    SUBCASE("远距离冻结") {
        CompanionAiRig rig;
        const ecs::Entity player = rig.AddPlayer(5.5f, 5.5f);
        const ecs::Entity companion = rig.AddCompanion(player, 55.5f, 5.5f);
        rig.Tick();
        CHECK(rig.Ai(companion).frozen);
        CHECK(rig.system.GetStats().frozen == 1);
        CHECK(rig.Request(companion).state == ecs::PathStatus::None);
    }
}

TEST_CASE("M33-F3-T3 跨区域寻路、墙边界与卡住恢复") {
    std::vector<std::pair<int, int>> walls;
    for (int y = 0; y < 30; ++y) {
        if (y != 10) {
            walls.emplace_back(15, y);
        }
    }
    CompanionAiRig rig(walls);
    const ecs::Entity player = rig.AddPlayer(25.5f, 5.5f);
    const ecs::Entity companion = rig.AddCompanion(player, 5.5f, 5.5f);
    bool enteredWall = false;
    bool crossedGap = false;
    for (int tick = 0; tick < 1800; ++tick) {
        rig.Tick();
        const int gx = static_cast<int>(std::floor(rig.Pos(companion).x));
        const int gy = static_cast<int>(std::floor(rig.Pos(companion).y));
        if (gx == 15 && gy != 10) {
            enteredWall = true;
        }
        if (gx == 15 && gy == 10) {
            crossedGap = true;
        }
    }
    CHECK_FALSE(enteredWall);
    CHECK(crossedGap);
    CHECK(Distance(rig.Pos(companion).x, rig.Pos(companion).y,
                   rig.Pos(player).x, rig.Pos(player).y) <= 5.0f);

    rig.Pos(player).x = 28.5f;
    rig.Follow(companion).status = ecs::PathStatus::Failed;
    rig.Request(companion).state = ecs::PathStatus::Failed;
    rig.Ai(companion).repathTimer = 0.0f;
    rig.Tick();
    const bool recovered = rig.Request(companion).newRequest ||
                           rig.Request(companion).state ==
                               ecs::PathStatus::Pending ||
                           rig.Request(companion).state ==
                               ecs::PathStatus::Following;
    CHECK(recovered);
}

TEST_CASE("M33-F3-T3 跟随落点避开玩家陷阱") {
    CompanionAiRig rig;
    const ecs::Entity player =
        rig.AddPlayer(10.5f, 10.5f, ecs::Direction::Right);
    const ecs::Entity companion = rig.AddCompanion(player, 15.5f, 10.5f);
    const ecs::Entity trap = rig.AddTrap(7.5f, 10.5f, 2.0f);
    rig.Tick();
    const float goalX = static_cast<float>(rig.Request(companion).goalX) + 0.5f;
    const float goalY = static_cast<float>(rig.Request(companion).goalY) + 0.5f;
    const float trapX = rig.Pos(trap).x;
    const float trapY = rig.Pos(trap).y;
    const float goalDistance = Distance(goalX, goalY, trapX, trapY);
    const float safeRadius =
        rig.world.Registry().get<ecs::SpikeTrap>(trap).radius + 0.7f;
    CHECK(goalDistance > safeRadius);
}

TEST_CASE("M34-F3-T3 同伴跟随落点避开 PlacedTrap") {
    CompanionAiRig rig;
    const ecs::Entity player =
        rig.AddPlayer(10.5f, 10.5f, ecs::Direction::Right);
    const ecs::Entity companion = rig.AddCompanion(player, 15.5f, 10.5f);
    const ecs::Entity trap = rig.AddPlacedTrap(7.5f, 10.5f);
    rig.Tick();
    const float goalX = static_cast<float>(rig.Request(companion).goalX) + 0.5f;
    const float goalY = static_cast<float>(rig.Request(companion).goalY) + 0.5f;
    const auto& position = rig.Pos(trap);
    const float goalDistance =
        Distance(goalX, goalY, position.x, position.y);
    CHECK(goalDistance > 2.0f + 0.7f);
}

TEST_CASE("M34-F4-T1 同伴从武装 PlacedTrap 上移开") {
    CompanionAiRig rig;
    const ecs::Entity player =
        rig.AddPlayer(10.5f, 10.5f, ecs::Direction::Right);
    const ecs::Entity companion = rig.AddCompanion(player, 15.5f, 10.5f);
    const ecs::Entity trap = rig.AddPlacedTrap(15.5f, 10.5f);
    rig.Tick();
    const float goalX = static_cast<float>(rig.Request(companion).goalX) + 0.5f;
    const float goalY = static_cast<float>(rig.Request(companion).goalY) + 0.5f;
    const auto& position = rig.Pos(trap);
    CHECK(Distance(goalX, goalY, position.x, position.y) >
          2.0f + 0.7f);
}
