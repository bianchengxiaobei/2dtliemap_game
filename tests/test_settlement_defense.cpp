#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/SettlementNpc.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"

using namespace zl;

namespace {

game::SettlementConfig MakeDefenseSettlement(
    game::SettlementGuardBehavior behavior =
        game::SettlementGuardBehavior::Defend) {
    game::SettlementConfig config;
    config.enabled = true;
    game::SettlementDef def;
    def.id = "raid_haven";
    def.name = "测试营地";
    def.centerX = 30.0f;
    def.centerY = 20.0f;
    def.safeRadius = 8.0f;
    def.defense.enabled = true;
    def.defense.activationRadius = 50.0f;
    def.defense.spawnRadius = 3.0f;
    def.defense.baseRaiders = 2;
    def.defense.raidersPerNight = 1;
    def.defense.maxRaiders = 2;
    def.defense.guardBehavior = behavior;
    def.defense.guardHealth = 120.0f;
    def.defense.guardDamage = 12.0f;
    def.defense.guardRange = 2.0f;
    def.defense.guardAttackSeconds = 0.01f;
    def.defense.raiderHealth = 20.0f;
    def.defense.raiderDamage = 0.0f;
    def.defense.raiderRange = 2.0f;
    def.defense.raiderAttackSeconds = 0.01f;
    config.defs.push_back(std::move(def));
    return config;
}

struct DefenseFixture {
    ecs::EcsWorld world;
    game::SettlementConfig config;
    ecs::Entity guard = ecs::EcsWorld::kNull;
    ecs::Entity merchant = ecs::EcsWorld::kNull;
    ecs::Entity requester = ecs::EcsWorld::kNull;
    game::SettlementNpcSystem system;

    DefenseFixture(game::SettlementConfig settlement)
        : config(std::move(settlement)),
          system(&config, [](int, int) { return true; }) {
        guard = game::SpawnSettlementNpc(
            world, "raid_haven", game::SettlementNpcKind::Guard, 30.5f, 20.5f,
            2.0f, config.defs[0].defense.guardHealth);
        merchant = game::SpawnSettlementNpc(
            world, "raid_haven", game::SettlementNpcKind::Merchant, 31.5f,
            20.5f, 1.5f);
        requester = game::SpawnSettlementNpc(
            world, "raid_haven", game::SettlementNpcKind::Requester, 29.5f,
            20.5f, 1.5f);
        system.SetFocus(30.0f, 20.0f);
    }

    ecs::Entity FirstRaider() {
        ecs::Entity entity = ecs::EcsWorld::kNull;
        world.Registry().view<game::SettlementRaidZombie>().each(
            [&](const ecs::Entity candidate, const game::SettlementRaidZombie&) {
                if (entity == ecs::EcsWorld::kNull) {
                    entity = candidate;
                }
            });
        return entity;
    }

    void MoveRaiderNextToGuard() {
        const ecs::Entity raider = FirstRaider();
        REQUIRE(raider != ecs::EcsWorld::kNull);
        auto& registry = world.Registry();
        auto* position = registry.try_get<ecs::Position>(raider);
        REQUIRE(position != nullptr);
        position->x = 30.5f;
        position->y = 21.4f;
        if (auto* follow = registry.try_get<ecs::PathFollow>(raider);
            follow != nullptr) {
            follow->status = ecs::PathStatus::None;
            follow->waypoints.clear();
        }
        if (auto* intent = registry.try_get<ecs::MoveIntent>(raider);
            intent != nullptr) {
            intent->x = 0.0f;
            intent->y = 0.0f;
        }
    }
};

}  // namespace

TEST_CASE("M23-F4-T5 夜袭触发守卫迎战且平民躲避") {
    DefenseFixture fixture(MakeDefenseSettlement());
    fixture.system.Update(fixture.world, 0.016f, 22, 1);

    CHECK(fixture.system.RaidActive("raid_haven"));
    CHECK(fixture.system.GetStats().raiders == 2);
    CHECK(fixture.system.GetStats().defending == 1);
    CHECK(fixture.system.GetStats().shelter == 2);
    CHECK(fixture.system.NoticeSerial() > 0);
    CHECK(fixture.system.NoticeText().find("夜袭") != std::string::npos);
    CHECK(fixture.world.Registry()
              .get<game::SettlementNpc>(fixture.guard)
              .activity == game::NpcActivity::Defend);
    CHECK(fixture.world.Registry()
              .get<game::SettlementNpc>(fixture.merchant)
              .activity == game::NpcActivity::Shelter);
    CHECK(fixture.world.Registry()
              .get<game::SettlementNpc>(fixture.requester)
              .activity == game::NpcActivity::Shelter);
    CHECK_FALSE(fixture.world.Registry().all_of<ecs::NoCombatTarget>(
        fixture.guard));
}

TEST_CASE("M23-F4-T5 守卫击杀夜袭者后恢复非战斗状态") {
    game::SettlementConfig config = MakeDefenseSettlement();
    config.defs[0].defense.baseRaiders = 1;
    config.defs[0].defense.maxRaiders = 1;
    config.defs[0].defense.guardDamage = 12.0f;
    config.defs[0].defense.raiderHealth = 20.0f;
    DefenseFixture fixture(std::move(config));
    fixture.system.Update(fixture.world, 0.016f, 22, 1);
    REQUIRE(fixture.system.RaidActive("raid_haven"));
    fixture.MoveRaiderNextToGuard();

    for (int i = 0; i < 4; ++i) {
        fixture.system.Update(fixture.world, 0.02f, 22, 1);
    }
    CHECK_FALSE(fixture.system.RaidActive("raid_haven"));
    CHECK(fixture.system.GetStats().raiders == 0);
    CHECK(fixture.world.Registry().all_of<ecs::NoCombatTarget>(
        fixture.guard));
    CHECK(fixture.system.NoticeText().find("击退") != std::string::npos);
}

TEST_CASE("M23-F4-T5 守卫死亡后聚落失守并停止刷新守卫") {
    game::SettlementConfig config = MakeDefenseSettlement();
    config.defs[0].defense.baseRaiders = 1;
    config.defs[0].defense.maxRaiders = 1;
    config.defs[0].defense.guardHealth = 10.0f;
    config.defs[0].defense.guardDamage = 0.0f;
    config.defs[0].defense.raiderDamage = 20.0f;
    config.defs[0].defense.raiderRange = 8.0f;
    DefenseFixture fixture(std::move(config));
    fixture.system.Update(fixture.world, 0.016f, 22, 1);
    REQUIRE(fixture.system.RaidActive("raid_haven"));
    fixture.MoveRaiderNextToGuard();
    fixture.system.Update(fixture.world, 0.02f, 22, 1);
    CHECK(fixture.world.Registry().get<ecs::Health>(fixture.guard).current <=
          0.0f);

    fixture.system.Update(fixture.world, 0.02f, 22, 1);
    CHECK_FALSE(fixture.system.RaidActive("raid_haven"));
    CHECK(fixture.system.IsFallen("raid_haven"));
    CHECK(fixture.system.GetStats().fallenSettlements == 1);
    CHECK(fixture.system.NoticeTone() == "fail");
    CHECK(fixture.system.NoticeText().find("失守") != std::string::npos);

    fixture.system.Reset();
    CHECK_FALSE(fixture.system.IsFallen("raid_haven"));
    CHECK(fixture.system.GetStats().raiders == 0);
}

TEST_CASE("M23-F4-T5 撤退策略与远离营地不触发夜袭") {
    DefenseFixture retreat(
        MakeDefenseSettlement(game::SettlementGuardBehavior::Retreat));
    retreat.system.Update(retreat.world, 0.016f, 22, 1);
    REQUIRE(retreat.system.RaidActive("raid_haven"));
    CHECK(retreat.world.Registry()
              .get<game::SettlementNpc>(retreat.guard)
              .activity == game::NpcActivity::Shelter);
    CHECK(retreat.system.GetStats().defending == 0);
    retreat.system.Update(retreat.world, 0.1f, 5, 2);
    CHECK_FALSE(retreat.system.RaidActive("raid_haven"));
    CHECK_FALSE(retreat.system.IsFallen("raid_haven"));

    DefenseFixture far(MakeDefenseSettlement());
    far.system.SetFocus(100.0f, 100.0f);
    far.system.Update(far.world, 0.016f, 22, 1);
    CHECK_FALSE(far.system.RaidActive("raid_haven"));
    CHECK(far.system.GetStats().raiders == 0);
}


TEST_CASE("M32-T4 外部战略围攻复用守卫与平民撤离") {
    DefenseFixture fixture(MakeDefenseSettlement());
    REQUIRE(fixture.system.BeginExternalDefense(fixture.world, "raid_haven"));
    CHECK(fixture.system.RaidActive("raid_haven"));
    CHECK(fixture.system.SpawnExternalWave(fixture.world, "raid_haven", 2,
                                           0) == 2);
    CHECK(fixture.system.LiveRaiders(fixture.world, "raid_haven") == 2);

    fixture.system.Update(fixture.world, 0.016f, 12, 1);
    CHECK(fixture.system.RaidActive("raid_haven"));
    CHECK(fixture.system.GetStats().defending == 1);
    CHECK(fixture.system.GetStats().shelter == 2);
    CHECK(fixture.world.Registry()
              .get<game::SettlementNpc>(fixture.requester)
              .activity == game::NpcActivity::Shelter);
    CHECK_FALSE(fixture.world.Registry().all_of<ecs::NoCombatTarget>(
        fixture.guard));

    fixture.MoveRaiderNextToGuard();
    for (int i = 0; i < 20; ++i) {
        fixture.system.Update(fixture.world, 0.02f, 12, 1);
    }
    CHECK(fixture.system.LiveRaiders(fixture.world, "raid_haven") < 2);

    fixture.system.EndExternalDefense(fixture.world, "raid_haven", false,
                                      "战略围攻击退");
    CHECK_FALSE(fixture.system.RaidActive("raid_haven"));
    CHECK(fixture.system.LiveRaiders(fixture.world, "raid_haven") == 0);
    CHECK(fixture.world.Registry().all_of<ecs::NoCombatTarget>(
        fixture.guard));
    CHECK(fixture.system.NoticeText().find("击退") != std::string::npos);
}

TEST_CASE("M32-T4 外部战略围攻失守标记聚落") {
    DefenseFixture fixture(MakeDefenseSettlement());
    REQUIRE(fixture.system.BeginExternalDefense(fixture.world, "raid_haven"));
    REQUIRE(fixture.system.SpawnExternalWave(fixture.world, "raid_haven", 1,
                                             1) == 1);
    auto* health =
        fixture.world.Registry().try_get<ecs::Health>(fixture.guard);
    REQUIRE(health != nullptr);
    health->current = 0.0f;
    fixture.system.EndExternalDefense(fixture.world, "raid_haven", true,
                                      "守卫倒下");
    CHECK(fixture.system.IsFallen("raid_haven"));
    CHECK_FALSE(fixture.system.RaidActive("raid_haven"));
    CHECK(fixture.system.LiveRaiders(fixture.world, "raid_haven") == 0);
    CHECK(fixture.system.NoticeTone() == "fail");
}

TEST_CASE("M23-F4-T6 聚落守卫生命与夜袭进度存档往返") {
    game::SettlementConfig config = MakeDefenseSettlement();
    ecs::EcsWorld source;
    const ecs::Entity guard = game::SpawnSettlementNpc(
        source, "raid_haven", game::SettlementNpcKind::Guard, 30.5f, 20.5f,
        2.0f, config.defs[0].defense.guardHealth);
    REQUIRE(guard != ecs::EcsWorld::kNull);
    game::SettlementNpcSystem system(&config,
                                     [](int, int) { return true; });
    system.SetFocus(30.0f, 20.0f);
    system.Update(source, 0.016f, 22, 1);
    REQUIRE(system.RaidActive("raid_haven"));
    source.Registry().get<ecs::Health>(guard).current = 73.0f;

    game::SettlementState captured;
    system.CaptureState(source, &captured);
    REQUIRE(captured.settlements.size() == 1u);
    CHECK(captured.settlements[0].id == "raid_haven");
    CHECK(captured.settlements[0].guardHealth == doctest::Approx(73.0f));
    CHECK_FALSE(captured.settlements[0].fallen);
    CHECK(captured.settlements[0].lastNightKey == 1);

    const ecs::Entity stateEntity =
        source.CreateWithStableId(game::kSettlementStateStableId);
    REQUIRE(stateEntity != ecs::EcsWorld::kNull);
    source.Registry().emplace<game::SettlementState>(stateEntity, captured);

    save::ComponentRegistry components;
    std::string error;
    REQUIRE(save::RegisterBuiltinComponents(components, &error));
    save::SavedEntity saved;
    REQUIRE(save::CaptureEntity(source, stateEntity, components, &saved,
                                &error));
    const save::EntitySnapshot snapshot = [&] {
        save::EntitySnapshot value;
        value.entities.push_back(saved);
        return value;
    }();
    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(snapshot, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));

    ecs::EcsWorld target;
    const ecs::Entity loaded =
        target.CreateWithStableId(game::kSettlementStateStableId);
    REQUIRE(loaded != ecs::EcsWorld::kNull);
    REQUIRE(save::ApplyEntity(decoded.entities[0], target, loaded, components,
                              &error));
    const auto* loadedState = target.Registry().try_get<game::SettlementState>(
        loaded);
    REQUIRE(loadedState != nullptr);
    REQUIRE(loadedState->settlements.size() == 1u);

    game::SettlementNpcSystem restored(&config,
                                       [](int, int) { return true; });
    restored.ApplyState(*loadedState);
    CHECK_FALSE(restored.IsFallen("raid_haven"));
    CHECK_FALSE(restored.RaidActive("raid_haven"));
    CHECK(restored.GuardHealthForSpawn("raid_haven", 120.0f) ==
          doctest::Approx(73.0f));
    const ecs::Entity restoredGuard = game::SpawnSettlementNpc(
        target, "raid_haven", game::SettlementNpcKind::Guard, 30.5f, 20.5f,
        2.0f, config.defs[0].defense.guardHealth);
    REQUIRE(restoredGuard != ecs::EcsWorld::kNull);
    restored.ApplyGuardHealth(target, restoredGuard, "raid_haven");
    CHECK(target.Registry().get<ecs::Health>(restoredGuard).current ==
          doctest::Approx(73.0f));
}

TEST_CASE("M23-F4-T6 失守状态读档后阻止守卫重建") {
    game::SettlementConfig config = MakeDefenseSettlement();
    config.defs[0].defense.baseRaiders = 1;
    config.defs[0].defense.maxRaiders = 1;
    config.defs[0].defense.guardHealth = 10.0f;
    config.defs[0].defense.guardDamage = 0.0f;
    config.defs[0].defense.raiderDamage = 20.0f;
    config.defs[0].defense.raiderRange = 8.0f;
    DefenseFixture fixture(std::move(config));
    fixture.system.Update(fixture.world, 0.016f, 22, 1);
    REQUIRE(fixture.system.RaidActive("raid_haven"));
    fixture.MoveRaiderNextToGuard();
    fixture.system.Update(fixture.world, 0.02f, 22, 1);
    fixture.system.Update(fixture.world, 0.02f, 22, 1);
    REQUIRE(fixture.system.IsFallen("raid_haven"));

    game::SettlementState captured;
    fixture.system.CaptureState(fixture.world, &captured);
    REQUIRE(captured.settlements.size() == 1u);
    CHECK(captured.settlements[0].fallen);
    CHECK(captured.settlements[0].guardHealth == doctest::Approx(0.0f));

    ecs::EcsWorld target;
    game::SettlementNpcSystem restored(&fixture.config,
                                       [](int, int) { return true; });
    restored.ApplyState(captured);
    CHECK(restored.IsFallen("raid_haven"));
    CHECK(restored.GuardHealthForSpawn("raid_haven", 120.0f) ==
          doctest::Approx(0.0f));
}


TEST_CASE("M23-F4-T7 同一晚读档不会重复触发夜袭") {
    game::SettlementConfig config = MakeDefenseSettlement();
    config.defs[0].defense.baseRaiders = 1;
    config.defs[0].defense.maxRaiders = 1;
    DefenseFixture source(std::move(config));
    source.system.Update(source.world, 0.016f, 22, 10);
    REQUIRE(source.system.RaidActive("raid_haven"));
    game::SettlementState captured;
    source.system.CaptureState(source.world, &captured);
    REQUIRE(captured.settlements.size() == 1u);
    CHECK(captured.settlements[0].lastNightKey == 10);
    CHECK_FALSE(captured.settlements[0].fallen);

    ecs::EcsWorld restoredWorld;
    const ecs::Entity restoredGuard = game::SpawnSettlementNpc(
        restoredWorld, "raid_haven", game::SettlementNpcKind::Guard, 30.5f,
        20.5f, 2.0f, source.config.defs[0].defense.guardHealth);
    REQUIRE(restoredGuard != ecs::EcsWorld::kNull);
    game::SettlementNpcSystem restored(
        &source.config, [](int, int) { return true; });
    restored.SetFocus(30.0f, 20.0f);
    restored.ApplyState(captured);
    restored.Update(restoredWorld, 0.016f, 22, 10);
    CHECK_FALSE(restored.RaidActive("raid_haven"));
    CHECK(restored.GetStats().raiders == 0);

    restored.Update(restoredWorld, 0.016f, 22, 11);
    CHECK(restored.RaidActive("raid_haven"));
}

TEST_CASE("M32-T5 失守撤离、反攻波次与恢复聚落") {
    DefenseFixture fixture(MakeDefenseSettlement());
    fixture.system.MarkFallen(fixture.world, "raid_haven");
    CHECK(fixture.system.IsFallen("raid_haven"));
    CHECK_FALSE(fixture.system.BeginExternalDefense(fixture.world,
                                                    "raid_haven"));
    CHECK(fixture.world.Alive(fixture.merchant));
    CHECK_FALSE(fixture.world.Alive(fixture.guard));
    CHECK_FALSE(fixture.world.Alive(fixture.requester));
    CHECK(fixture.system.GuardHealthForSpawn("raid_haven", 77.0f) ==
          doctest::Approx(0.0f));

    REQUIRE(fixture.system.BeginExternalReclaim(fixture.world, "raid_haven"));
    CHECK(fixture.system.RaidActive("raid_haven"));
    CHECK(fixture.system.SpawnExternalWave(fixture.world, "raid_haven", 2,
                                           0) == 2);
    CHECK(fixture.system.LiveRaiders(fixture.world, "raid_haven") == 2);
    fixture.system.EndExternalReclaim(fixture.world, "raid_haven", true,
                                      "反攻波次击退");
    CHECK_FALSE(fixture.system.RaidActive("raid_haven"));
    CHECK(fixture.system.LiveRaiders(fixture.world, "raid_haven") == 0);
    CHECK(fixture.system.IsFallen("raid_haven"));
    CHECK(fixture.system.NoticeText().find("击退") != std::string::npos);

    fixture.system.RestoreSettlement(fixture.world, "raid_haven");
    CHECK_FALSE(fixture.system.IsFallen("raid_haven"));
    CHECK(fixture.system.GuardHealthForSpawn("raid_haven", 77.0f) ==
          doctest::Approx(77.0f));
    CHECK(fixture.system.NoticeTone() == "ok");
}

TEST_CASE("M32-T6 守卫自主迎击普通感染者并保护平民") {
    game::SettlementConfig config = MakeDefenseSettlement();
    config.defs[0].defense.guardEngageRadius = 8.0f;
    DefenseFixture fixture(std::move(config));
    REQUIRE(fixture.system.BeginExternalDefense(fixture.world, "raid_haven"));
    ecs::Entity zombie = fixture.world.Create();
    auto& registry = fixture.world.Registry();
    registry.emplace<ecs::Position>(zombie, ecs::Position{31.5f, 20.5f, 0});
    registry.emplace<ecs::Health>(zombie, ecs::Health{20.0f, 20.0f});
    registry.emplace<ecs::AiState>(zombie, ecs::AiState{});

    fixture.system.Update(fixture.world, 0.016f, 12, 1);
    CHECK(fixture.system.GetStats().defending == 1);
    CHECK(fixture.system.GetStats().shelter == 2);
    CHECK(fixture.world.Registry()
              .get<game::SettlementNpc>(fixture.guard)
              .activity == game::NpcActivity::Defend);

    fixture.system.Update(fixture.world, 0.02f, 12, 1);
    fixture.system.Update(fixture.world, 0.02f, 12, 1);
    CHECK(fixture.world.Registry().get<ecs::Health>(zombie).current <= 0.0f);
}
TEST_CASE("M32-T11 战略掠夺者波次使用独立外观") {
    DefenseFixture raiders(MakeDefenseSettlement());
    REQUIRE(raiders.system.BeginExternalDefense(raiders.world, "raid_haven"));
    REQUIRE(raiders.system.SpawnExternalWave(
                raiders.world, "raid_haven", 1, 0,
                game::ContestedFaction::Raiders) == 1);
    const ecs::Entity raider = raiders.FirstRaider();
    REQUIRE(raider != ecs::EcsWorld::kNull);
    const auto* sprite =
        raiders.world.Registry().try_get<ecs::SpriteRef>(raider);
    const auto* raid =
        raiders.world.Registry().try_get<game::SettlementRaidZombie>(raider);
    REQUIRE(sprite != nullptr);
    REQUIRE(raid != nullptr);
    CHECK(sprite->sprite == ecs::HashName("character.placeholder"));
    CHECK(sprite->tint == 0xFFFF8060u);
    CHECK(raid->faction == game::ContestedFaction::Raiders);

    DefenseFixture infected(MakeDefenseSettlement());
    REQUIRE(infected.system.BeginExternalDefense(infected.world,
                                                  "raid_haven"));
    REQUIRE(infected.system.SpawnExternalWave(infected.world, "raid_haven",
                                               1, 0) == 1);
    const auto* infectedSprite = infected.world.Registry().try_get<ecs::SpriteRef>(
        infected.FirstRaider());
    REQUIRE(infectedSprite != nullptr);
    CHECK(infectedSprite->sprite == ecs::HashName("character.zombie"));
    CHECK(infectedSprite->tint == 0xFF8080FFu);
}

TEST_CASE("M33-F1-T5 防御服务未启用时不触发夜袭") {
    game::SettlementConfig config = MakeDefenseSettlement();
    config.defs[0].services &= ~game::kSettlementServiceDefense;
    DefenseFixture fixture(config);
    CHECK_FALSE(fixture.system.DefenseEnabled("raid_haven"));
    fixture.system.Update(fixture.world, 0.1f, 23, 1);
    CHECK(fixture.FirstRaider() == ecs::EcsWorld::kNull);
    CHECK(fixture.world.Registry()
              .get<game::SettlementNpc>(fixture.guard)
              .activity != game::NpcActivity::Defend);
}