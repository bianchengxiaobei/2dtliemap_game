#include <doctest/doctest.h>

#include <string>

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/SettlementNpc.h"

using namespace zl;

namespace {

game::SettlementConfig MakeSettlement() {
    game::SettlementConfig config;
    config.enabled = true;
    game::SettlementDef def;
    def.id = "test_haven";
    def.name = "测试营地";
    def.centerX = 30.0f;
    def.centerY = 20.0f;
    def.safeRadius = 8.0f;
    config.defs.push_back(std::move(def));
    return config;
}

ecs::Entity SpawnTestZombie(ecs::EcsWorld& world, float x, float y) {
    const ecs::Entity entity = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(entity, ecs::Position{x, y, 0});
    registry.emplace<ecs::Health>(entity, ecs::Health{100.0f, 100.0f});
    registry.emplace<ecs::AiState>(entity, ecs::AiState{});
    return entity;
}


}  // namespace

TEST_CASE("M23-F4-T2 NPC 日程按昼夜切换站岗巡逻休息") {
    ecs::EcsWorld world;
    const game::SettlementConfig config = MakeSettlement();
    const ecs::Entity guard = game::SpawnSettlementNpc(
        world, "test_haven", game::SettlementNpcKind::Guard, 30.0f, 20.0f,
        2.0f);
    const ecs::Entity merchant = game::SpawnSettlementNpc(
        world, "test_haven", game::SettlementNpcKind::Merchant, 31.0f, 20.0f,
        1.5f);
    const ecs::Entity requester = game::SpawnSettlementNpc(
        world, "test_haven", game::SettlementNpcKind::Requester, 29.0f, 20.0f,
        1.5f);
    REQUIRE(guard != ecs::EcsWorld::kNull);
    REQUIRE(merchant != ecs::EcsWorld::kNull);
    REQUIRE(requester != ecs::EcsWorld::kNull);

    game::SettlementNpcSystem system(&config);
    system.SetFocus(30.0f, 20.0f);
    system.Update(world, 0.016f, 10);
    const auto& registry = world.Registry();
    CHECK(registry.get<game::SettlementNpc>(guard).activity ==
          game::NpcActivity::Patrol);
    CHECK(registry.get<game::SettlementNpc>(merchant).activity ==
          game::NpcActivity::Standing);
    CHECK(registry.get<game::SettlementNpc>(requester).activity ==
          game::NpcActivity::Standing);
    CHECK(system.GetStats().total == 3);
    CHECK(system.GetStats().patrol == 1);
    CHECK(system.GetStats().standing == 2);

    system.Update(world, 0.016f, 19);
    CHECK(registry.get<game::SettlementNpc>(guard).activity ==
          game::NpcActivity::Standing);
    CHECK(registry.get<game::SettlementNpc>(merchant).activity ==
          game::NpcActivity::Standing);

    system.Update(world, 0.016f, 23);
    CHECK(registry.get<game::SettlementNpc>(guard).activity ==
          game::NpcActivity::Rest);
    CHECK(registry.get<game::SettlementNpc>(merchant).activity ==
          game::NpcActivity::Rest);
    CHECK(registry.get<game::SettlementNpc>(requester).activity ==
          game::NpcActivity::Rest);
    CHECK(std::string(game::NpcActivityName(game::NpcActivity::Shelter)) ==
          std::string("shelter"));
}

TEST_CASE("M23-F4-T2 聚落僵尸威胁触发躲避并在解除后恢复日程") {
    ecs::EcsWorld world;
    const game::SettlementConfig config = MakeSettlement();
    const ecs::Entity guard = game::SpawnSettlementNpc(
        world, "test_haven", game::SettlementNpcKind::Guard, 36.0f, 20.0f,
        2.0f);
    REQUIRE(guard != ecs::EcsWorld::kNull);

    game::SettlementNpcSystem system(&config);
    system.SetFocus(30.0f, 20.0f);
    system.Update(world, 0.016f, 10);
    CHECK(world.Registry().get<game::SettlementNpc>(guard).activity ==
          game::NpcActivity::Patrol);

    const ecs::Entity zombie = SpawnTestZombie(world, 30.0f, 20.0f);
    system.Update(world, 0.016f, 10);
    CHECK(world.Registry().get<game::SettlementNpc>(guard).activity ==
          game::NpcActivity::Shelter);
    CHECK(world.Registry().get<game::SettlementNpc>(guard).hasTarget);
    CHECK(world.Registry().get<game::SettlementNpc>(guard).targetX ==
          doctest::Approx(30.0f));
    CHECK(system.GetStats().shelter == 1);
    CHECK(system.GetStats().threatenedSettlements == 1);

    world.Destroy(zombie);
    world.Flush();
    for (int i = 0; i < 4; ++i) {
        system.Update(world, 1.0f, 10);
    }
    CHECK(world.Registry().get<game::SettlementNpc>(guard).activity ==
          game::NpcActivity::Patrol);
    CHECK(system.GetStats().threatenedSettlements == 0);
}

TEST_CASE("M23-F4-T2 远程 NPC 冻结且返回焦点后恢复日程") {
    ecs::EcsWorld world;
    const game::SettlementConfig config = MakeSettlement();
    const ecs::Entity guard = game::SpawnSettlementNpc(
        world, "test_haven", game::SettlementNpcKind::Guard, 30.0f, 20.0f,
        2.0f);
    REQUIRE(guard != ecs::EcsWorld::kNull);
    world.Registry().get<ecs::MoveIntent>(guard).x = 3.0f;

    game::SettlementNpcSystem system(&config);
    system.SetFocus(100.0f, 100.0f);
    system.Update(world, 0.016f, 10);
    const auto& registry = world.Registry();
    CHECK(registry.get<game::SettlementNpc>(guard).frozen);
    CHECK(registry.get<game::SettlementNpc>(guard).hasTarget == false);
    CHECK(registry.get<ecs::MoveIntent>(guard).x == 0.0f);
    CHECK(system.GetStats().frozen == 1);

    system.SetFocus(30.0f, 20.0f);
    system.Update(world, 0.016f, 10);
    CHECK_FALSE(registry.get<game::SettlementNpc>(guard).frozen);
    CHECK(registry.get<game::SettlementNpc>(guard).activity ==
          game::NpcActivity::Patrol);
    CHECK(registry.get<game::SettlementNpc>(guard).hasTarget);
    CHECK(system.GetStats().frozen == 0);
}

TEST_CASE("M23-F4-T2 聚落 NPC 生成包含日程所需组件") {
    ecs::EcsWorld world;
    const ecs::Entity npc = game::SpawnSettlementNpc(
        world, "test_haven", game::SettlementNpcKind::Requester, 29.0f, 20.0f,
        1.5f);
    REQUIRE(npc != ecs::EcsWorld::kNull);
    const auto& registry = world.Registry();
    const auto* component = registry.try_get<game::SettlementNpc>(npc);
    REQUIRE(component != nullptr);
    CHECK(component->settlementId == "test_haven");
    CHECK(component->kind == game::SettlementNpcKind::Requester);
    CHECK(component->activity == game::NpcActivity::Rest);
    CHECK(registry.all_of<ecs::Position, ecs::Velocity, ecs::SpriteRef,
                          ecs::Facing, ecs::AnimState, ecs::MoveIntent,
                          ecs::PathRequest, ecs::PathFollow,
                          ecs::NoCombatTarget>(npc));
    CHECK_FALSE(registry.all_of<ecs::Health>(npc));
    CHECK_FALSE(registry.all_of<ecs::AiState>(npc));
    CHECK(registry.get<ecs::PathFollow>(npc).speed > 0.0f);
    CHECK(registry.get<ecs::SpriteRef>(npc).sprite ==
          ecs::HashName("character.placeholder"));
}


TEST_CASE("M23-F4-T7 聚落巡逻与躲避目标避开阻挡格") {
    ecs::EcsWorld patrolWorld;
    game::SettlementConfig patrolConfig = MakeSettlement();
    const ecs::Entity patrolGuard = game::SpawnSettlementNpc(
        patrolWorld, "test_haven", game::SettlementNpcKind::Guard, 30.5f,
        20.5f, 4.0f);
    REQUIRE(patrolGuard != ecs::EcsWorld::kNull);
    game::SettlementNpcSystem patrolSystem(
        &patrolConfig,
        [](int x, int y) { return !(x >= 31 && x <= 34 && y == 20); });
    patrolSystem.SetFocus(30.0f, 20.0f);
    for (int i = 0; i < 24; ++i) {
        patrolSystem.Update(patrolWorld, 0.016f, 10);
        auto& npc = patrolWorld.Registry().get<game::SettlementNpc>(patrolGuard);
        if (npc.hasTarget) {
            const int tx = static_cast<int>(std::floor(npc.targetX));
            const int ty = static_cast<int>(std::floor(npc.targetY));
            CHECK((tx < 31 || tx > 34 || ty != 20));
        }
        npc.hasTarget = false;
        npc.settled = true;
        npc.waitSeconds = 0.0f;
    }

    ecs::EcsWorld shelterWorld;
    game::SettlementConfig shelterConfig = MakeSettlement();
    shelterConfig.defs[0].centerX = 40.5f;
    shelterConfig.defs[0].centerY = 20.5f;
    const ecs::Entity shelterGuard = game::SpawnSettlementNpc(
        shelterWorld, "test_haven", game::SettlementNpcKind::Guard, 40.5f,
        20.5f, 2.0f);
    REQUIRE(shelterGuard != ecs::EcsWorld::kNull);
    game::SettlementNpcSystem shelterSystem(
        &shelterConfig, [](int x, int y) { return x == 43 && y == 20; });
    shelterSystem.SetFocus(40.0f, 20.0f);
    SpawnTestZombie(shelterWorld, 40.0f, 20.0f);
    shelterSystem.Update(shelterWorld, 0.016f, 10);

    const auto& shelterNpc =
        shelterWorld.Registry().get<game::SettlementNpc>(shelterGuard);
    CHECK(shelterNpc.activity == game::NpcActivity::Shelter);
    REQUIRE(shelterNpc.hasTarget);
    CHECK(shelterNpc.targetX == doctest::Approx(43.5f));
    CHECK(shelterNpc.targetY == doctest::Approx(20.5f));
    CHECK(std::abs(shelterNpc.targetX - 40.5f) > 0.01f);
}
