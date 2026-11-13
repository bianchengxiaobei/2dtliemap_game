#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <string>

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/Homestead.h"
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

game::SettlementConfig MakePopulationSettlements() {
    game::SettlementConfig config;
    config.enabled = true;
    const std::array<const char*, 4> ids{"haven_a", "haven_b", "haven_c",
                                        "haven_d"};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        game::SettlementDef def;
        def.id = ids[i];
        def.name = std::string("测试聚落") + std::to_string(i + 1);
        def.centerX = 30.0f + static_cast<float>(i) * 30.0f;
        def.centerY = 20.0f;
        def.safeRadius = 6.0f;
        def.defense.enabled = true;
        def.defense.minDay = 1;
        def.defense.activationRadius = 10.0f;
        def.defense.baseRaiders = 1;
        def.defense.raidersPerNight = 1;
        def.defense.maxRaiders = 1;
        def.defense.guardHealth = 120.0f + static_cast<float>(i) * 10.0f;
        def.defense.guardDamage = 0.0f;
        def.defense.raiderDamage = 0.0f;
        def.merchantStock = {static_cast<int>(i) + 1, 2, 3, 4, 5};
        def.restockDays = static_cast<int>(i) + 1;
        def.npcPoints.push_back(
            game::SettlementNpcPoint{"merchant", game::SettlementNpcKind::Merchant,
                                     def.centerX + 1.0f, def.centerY, 1.0f});
        def.npcPoints.push_back(
            game::SettlementNpcPoint{"guard", game::SettlementNpcKind::Guard,
                                     def.centerX - 1.0f, def.centerY, 2.0f});
        def.npcPoints.push_back(
            game::SettlementNpcPoint{"requester",
                                     game::SettlementNpcKind::Requester,
                                     def.centerX, def.centerY + 1.0f, 1.5f});
        config.defs.push_back(std::move(def));
    }
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


TEST_CASE("M33-F1-T2 四个聚落分别生成商人守卫与求助者") {
    game::SettlementConfig config = MakePopulationSettlements();
    ecs::EcsWorld world;
    game::SettlementNpcSystem system(
        &config, [](int, int) { return true; });

    const game::SettlementNpcSystem::PopulationStats first =
        system.EnsurePopulation(world);
    CHECK(first.merchants == 4);
    CHECK(first.guards == 4);
    CHECK(first.requesters == 4);

    int total = 0;
    world.Registry().view<game::SettlementNpc>().each(
        [&](const ecs::Entity, const game::SettlementNpc&) { ++total; });
    CHECK(total == 12);

    for (const game::SettlementDef& def : config.defs) {
        bool foundMerchant = false;
        bool foundGuard = false;
        bool foundRequester = false;
        bool stockOk = false;
        world.Registry()
            .view<game::SettlementNpc>()
            .each([&](const ecs::Entity entity,
                      const game::SettlementNpc& npc) {
                if (npc.settlementId != def.id) {
                    return;
                }
                foundMerchant =
                    foundMerchant ||
                    npc.kind == game::SettlementNpcKind::Merchant;
                foundGuard = foundGuard ||
                             npc.kind == game::SettlementNpcKind::Guard;
                foundRequester =
                    foundRequester ||
                    npc.kind == game::SettlementNpcKind::Requester;
                if (npc.kind == game::SettlementNpcKind::Merchant) {
                    const auto* stock =
                        world.Registry().try_get<ecs::MerchantStock>(entity);
                    stockOk = stock != nullptr &&
                              stock->stock == def.merchantStock;
                }
            });
        CHECK(foundMerchant);
        CHECK(foundGuard);
        CHECK(foundRequester);
        CHECK(stockOk);
    }

    const game::SettlementNpcSystem::PopulationStats second =
        system.EnsurePopulation(world);
    CHECK(second.merchants == 0);
    CHECK(second.guards == 0);
    CHECK(second.requesters == 0);
    total = 0;
    world.Registry().view<game::SettlementNpc>().each(
        [&](const ecs::Entity, const game::SettlementNpc&) { ++total; });
    CHECK(total == 12);
}

TEST_CASE("M33-F1-T6 NPC 生成点不落到安全区外") {
    game::SettlementConfig config;
    config.enabled = true;
    game::SettlementDef def;
    def.id = "edge_camp";
    def.name = "边缘营地";
    def.centerX = 30.0f;
    def.centerY = 20.0f;
    def.safeRadius = 4.0f;
    def.npcPoints.push_back(game::SettlementNpcPoint{
        "edge_merchant", game::SettlementNpcKind::Merchant, 30.0f, 20.0f,
        0.0f});
    def.npcPoints.push_back(game::SettlementNpcPoint{
        "edge_requester", game::SettlementNpcKind::Requester, 31.0f, 20.0f,
        0.0f});
    config.defs.push_back(std::move(def));

    ecs::EcsWorld world;
    bool allowInside = false;
    game::SettlementNpcSystem system(
        &config, [&](int x, int y) {
            if (x >= 34 && x <= 36 && y >= 19 && y <= 21) {
                return true;
            }
            return allowInside && x >= 28 && x <= 33 && y >= 18 && y <= 22;
        });
    const auto blocked = system.EnsurePopulation(world);
    CHECK(blocked.merchants == 0);
    CHECK(blocked.requesters == 0);
    int total = 0;
    world.Registry().view<game::SettlementNpc>().each(
        [&](const ecs::Entity, const game::SettlementNpc&) { ++total; });
    CHECK(total == 0);

    allowInside = true;
    const auto spawned = system.EnsurePopulation(world);
    CHECK(spawned.merchants == 1);
    CHECK(spawned.requesters == 1);
    world.Registry().view<game::SettlementNpc, ecs::Position>().each(
        [&](const ecs::Entity, const game::SettlementNpc& npc,
            const ecs::Position& position) {
            CHECK(game::IsInsideSettlement(config.defs.front(), position.x,
                                           position.y));
            CHECK(npc.settlementId == "edge_camp");
        });
}

TEST_CASE("M33-F1-T2 旧存档商人迁移到最近聚落且保留库存") {
    game::SettlementConfig config = MakePopulationSettlements();
    ecs::EcsWorld world;
    const ecs::Entity legacy =
        game::SpawnMerchant(world, 60.5f, 20.5f, 0);
    REQUIRE(legacy != ecs::EcsWorld::kNull);
    auto* legacyStock =
        world.Registry().try_get<ecs::MerchantStock>(legacy);
    REQUIRE(legacyStock != nullptr);
    legacyStock->stock = {9, 9, 9, 9, 9};
    legacyStock->restockDay = 7;

    game::SettlementNpcSystem system(
        &config, [](int, int) { return true; });
    const game::SettlementNpcSystem::PopulationStats stats =
        system.EnsurePopulation(world);
    CHECK(stats.merchants == 3);

    const auto* npc =
        world.Registry().try_get<game::SettlementNpc>(legacy);
    REQUIRE(npc != nullptr);
    CHECK(npc->settlementId == "haven_b");
    CHECK(npc->kind == game::SettlementNpcKind::Merchant);
    CHECK(legacyStock->stock == std::array<int, 5>{9, 9, 9, 9, 9});
    CHECK(legacyStock->restockDay == 7);

    int merchants = 0;
    world.Registry().view<game::SettlementNpc>().each(
        [&](const ecs::Entity, const game::SettlementNpc& component) {
            merchants += component.kind == game::SettlementNpcKind::Merchant
                             ? 1
                             : 0;
        });
    CHECK(merchants == 4);
}

TEST_CASE("M33-F1-T2 聚落守卫生命与夜间进度按 id 捕获恢复") {
    game::SettlementConfig config = MakePopulationSettlements();
    ecs::EcsWorld world;
    game::SettlementNpcSystem system(
        &config, [](int, int) { return true; });
    REQUIRE(system.EnsurePopulation(world).merchants == 4);

    const auto findNpc = [&](std::string_view id,
                             game::SettlementNpcKind kind) {
        ecs::Entity found = ecs::EcsWorld::kNull;
        world.Registry().view<game::SettlementNpc>().each(
            [&](const ecs::Entity entity, const game::SettlementNpc& npc) {
                if (npc.settlementId == id && npc.kind == kind) {
                    found = entity;
                }
            });
        return found;
    };

    for (std::size_t i = 0; i < config.defs.size(); ++i) {
        const game::SettlementDef& def = config.defs[i];
        const ecs::Entity merchant =
            findNpc(def.id, game::SettlementNpcKind::Merchant);
        REQUIRE(merchant != ecs::EcsWorld::kNull);
        auto* stock = world.Registry().try_get<ecs::MerchantStock>(merchant);
        REQUIRE(stock != nullptr);
        stock->stock = {9 - static_cast<int>(i), 8, 7, 6, 5};

        const ecs::Entity guard =
            findNpc(def.id, game::SettlementNpcKind::Guard);
        REQUIRE(guard != ecs::EcsWorld::kNull);
        world.Registry().get<ecs::Health>(guard).current =
            70.0f + static_cast<float>(i) * 10.0f;
    }

    system.SetFocus(30.0f, 20.0f);
    system.Update(world, 0.016f, 22, 11);
    REQUIRE(system.RaidActive("haven_a"));

    game::SettlementState captured;
    system.CaptureState(world, &captured);
    REQUIRE(captured.settlements.size() == 4u);
    for (std::size_t i = 0; i < config.defs.size(); ++i) {
        const auto it = std::find_if(
            captured.settlements.begin(), captured.settlements.end(),
            [&](const game::SettlementStateEntry& entry) {
                return entry.id == config.defs[i].id;
            });
        REQUIRE(it != captured.settlements.end());
        CHECK(it->guardHealth ==
              doctest::Approx(70.0f + static_cast<float>(i) * 10.0f));
        CHECK_FALSE(it->fallen);
        if (i == 0) {
            CHECK(it->lastNightKey > 0);
        } else {
            CHECK(it->lastNightKey < 0);
        }
    }

    game::SettlementNpcSystem restored(
        &config, [](int, int) { return true; });
    restored.ApplyState(captured);
    for (std::size_t i = 0; i < config.defs.size(); ++i) {
        CHECK(restored.GuardHealthForSpawn(
                  config.defs[i].id, 120.0f) ==
              doctest::Approx(70.0f + static_cast<float>(i) * 10.0f));
    }
    ecs::EcsWorld emptyWorld;
    game::SettlementState recaptured;
    restored.CaptureState(emptyWorld, &recaptured);
    REQUIRE(recaptured.settlements.size() == 4u);
    const auto first = std::find_if(
        recaptured.settlements.begin(), recaptured.settlements.end(),
        [](const game::SettlementStateEntry& entry) {
            return entry.id == "haven_a";
        });
    REQUIRE(first != recaptured.settlements.end());
    CHECK(first->lastNightKey > 0);
}

TEST_CASE("M33-F1-T2 守卫死亡与聚落失守不补生恢复后重建") {
    game::SettlementConfig config = MakePopulationSettlements();
    ecs::EcsWorld world;
    game::SettlementNpcSystem system(
        &config, [](int, int) { return true; });
    REQUIRE(system.EnsurePopulation(world).guards == 4);

    const auto countNpc = [&](std::string_view id,
                              game::SettlementNpcKind kind) {
        int count = 0;
        world.Registry().view<game::SettlementNpc>().each(
            [&](const ecs::Entity, const game::SettlementNpc& npc) {
                if (npc.settlementId == id && npc.kind == kind) {
                    ++count;
                }
            });
        return count;
    };
    const auto findNpc = [&](std::string_view id,
                             game::SettlementNpcKind kind) {
        ecs::Entity found = ecs::EcsWorld::kNull;
        world.Registry().view<game::SettlementNpc>().each(
            [&](const ecs::Entity entity, const game::SettlementNpc& npc) {
                if (npc.settlementId == id && npc.kind == kind) {
                    found = entity;
                }
            });
        return found;
    };

    const ecs::Entity lostGuard =
        findNpc("haven_a", game::SettlementNpcKind::Guard);
    REQUIRE(lostGuard != ecs::EcsWorld::kNull);
    world.Destroy(lostGuard);
    world.Flush();
    CHECK(countNpc("haven_a", game::SettlementNpcKind::Guard) == 0);

    const auto afterDeath = system.EnsurePopulation(world);
    CHECK(afterDeath.guards == 0);
    CHECK(countNpc("haven_a", game::SettlementNpcKind::Guard) == 0);

    system.MarkFallen(world, "haven_b");
    world.Flush();
    const auto afterFallen = system.EnsurePopulation(world);
    CHECK(afterFallen.guards == 0);
    CHECK(afterFallen.requesters == 0);
    CHECK(countNpc("haven_b", game::SettlementNpcKind::Guard) == 0);
    CHECK(countNpc("haven_b", game::SettlementNpcKind::Requester) == 0);
    CHECK(countNpc("haven_b", game::SettlementNpcKind::Merchant) == 1);

    system.RestoreSettlement(world, "haven_b");
    world.Flush();
    const auto afterRestore = system.EnsurePopulation(world);
    CHECK(afterRestore.guards == 1);
    CHECK(afterRestore.requesters == 1);
    CHECK(countNpc("haven_b", game::SettlementNpcKind::Guard) == 1);
    CHECK(countNpc("haven_b", game::SettlementNpcKind::Requester) == 1);
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

TEST_CASE("M33-F1-T5 Hostile 只拒绝服务，不让守卫主动追杀") {
    game::SettlementConfig config = MakeSettlement();
    config.defs[0].defense.enabled = true;
    config.defs[0].defense.minDay = 9999;
    config.defs[0].defense.activationRadius = 50.0f;
    config.defs[0].defense.guardDamage = 20.0f;
    config.defs[0].defense.guardRange = 3.0f;

    game::SettlementReputationConfig reputation;
    CHECK_FALSE(game::SettlementCanUseService(
        config.defs[0], reputation, game::SettlementService::Trade, -60));
    CHECK_FALSE(game::SettlementCanUseService(
        config.defs[0], reputation, game::SettlementService::Missions, -60));
    CHECK(game::SettlementCanUseService(
        config.defs[0], reputation, game::SettlementService::Defense, -60));

    ecs::EcsWorld world;
    const ecs::Entity guard = game::SpawnSettlementNpc(
        world, "test_haven", game::SettlementNpcKind::Guard, 30.0f, 20.0f,
        2.0f);
    const ecs::Entity player = world.Create();
    world.Registry().emplace<ecs::Position>(player, ecs::Position{31.0f, 20.0f, 0});
    world.Registry().emplace<ecs::Health>(player, ecs::Health{100.0f, 100.0f});

    game::SettlementNpcSystem system(&config);
    system.SetFocus(30.0f, 20.0f);
    for (int i = 0; i < 30; ++i) {
        system.Update(world, 0.1f, 23, 1);
    }
    CHECK(world.Registry().get<ecs::Health>(player).current == 100.0f);
    CHECK(world.Registry().get<game::SettlementNpc>(guard).activity !=
          game::NpcActivity::Defend);
}
