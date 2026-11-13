#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "ecs/EcsWorld.h"
#include "game/SettlementReputation.h"
#include "game/StrategicDirector.h"
#include "game/StrategicState.h"
#include "game/WorldEventRuntime.h"

using namespace zl;

namespace {

game::WorldEventStageDef Stage(game::WorldEventStageKind kind,
                               std::string target, int count) {
    game::WorldEventStageDef stage;
    stage.kind = kind;
    stage.target = std::move(target);
    stage.count = count;
    return stage;
}

game::WorldEventDef MakeDef() {
    game::WorldEventDef def;
    def.id = "caravan_ambush";
    def.name = "商队伏击";
    def.kind = "caravan";
    def.minDay = 1;
    def.maxDay = 20;
    def.failureReputation = -7;
    def.failurePressure = 9;
    def.stages.push_back(
        Stage(game::WorldEventStageKind::Reach, {}, 1));
    def.stages.push_back(
        Stage(game::WorldEventStageKind::Kill, "fast_raider", 3));
    def.stages.push_back(
        Stage(game::WorldEventStageKind::Loot, "event_supply_crate", 1));
    game::WorldEventChoiceDef rescue;
    rescue.id = "rescue";
    rescue.label = "救援";
    rescue.reputation = 15;
    rescue.rewardXp = 80.0f;
    rescue.rewardItem = "ammo_9mm";
    rescue.rewardItemCount = 2;
    rescue.pressure = -12;
    game::WorldEventChoiceDef leave;
    leave.id = "leave";
    leave.label = "离开";
    leave.reputation = -3;
    leave.pressure = 4;
    def.choices.push_back(rescue);
    def.choices.push_back(leave);
    return def;
}

game::WorldEventInstance MakeEvent(std::string id,
                                   game::WorldEventStatus status) {
    game::WorldEventInstance event;
    event.id = std::move(id);
    event.defId = "caravan_ambush";
    event.name = "商队伏击";
    event.kind = "caravan";
    event.regionId = "core";
    event.x = 30.5f;
    event.y = 40.5f;
    event.day = 3;
    event.status = status;
    event.choiceId = "rescue";
    return event;
}

}  // namespace

TEST_CASE("M33-F2-T3 世界事件：生成计划、稳定 ID 与回调") {
    const game::WorldEventDef def = MakeDef();
    const game::WorldEventInstance event =
        MakeEvent("world_event_3_caravan_ambush_test", game::WorldEventStatus::Active);
    const game::WorldEventSpawnPlan plan =
        game::BuildWorldEventSpawnPlan(def, event);
    REQUIRE(plan.content);
    REQUIRE(plan.requests.size() == 4u);
    CHECK(plan.requests[0].kind == game::WorldEventSpawnKind::Zombie);
    CHECK(plan.requests[1].kind == game::WorldEventSpawnKind::Zombie);
    CHECK(plan.requests[2].kind == game::WorldEventSpawnKind::Zombie);
    CHECK(plan.requests[3].kind == game::WorldEventSpawnKind::Container);
    CHECK(plan.requests[0].stableId ==
          game::WorldEventStableId(event.id, 1));
    CHECK(plan.requests[3].stableId ==
          game::WorldEventStableId(event.id, 4));
    CHECK(plan.requests[0].stableId != plan.requests[3].stableId);

    std::vector<game::WorldEventSpawnRequest> calls;
    game::WorldEventSpawnCallbacks callbacks;
    callbacks.spawn = [&calls](const game::WorldEventSpawnRequest& request) {
        calls.push_back(request);
        return true;
    };
    game::WorldEventSpawnResult result;
    REQUIRE(game::EnsureWorldEventContent(plan, callbacks, &result));
    CHECK(result.ok);
    CHECK(result.content);
    CHECK(result.ready == 4);
    CHECK(result.total == 4);
    REQUIRE(calls.size() == 4u);
    CHECK(calls[0].stableId == plan.requests[0].stableId);
    CHECK(calls[3].target == "event_supply_crate");

    calls.clear();
    REQUIRE(game::EnsureWorldEventContent(plan, callbacks, &result));
    REQUIRE(calls.size() == 4u);
    CHECK(calls[0].stableId == plan.requests[0].stableId);
    CHECK(calls[3].stableId == plan.requests[3].stableId);

    game::WorldEventDef sparse;
    sparse.id = "supply_drop";
    sparse.name = "空投";
    sparse.kind = "supply_drop";
    sparse.stages.push_back(
        Stage(game::WorldEventStageKind::Reach, {}, 1));
    const game::WorldEventSpawnPlan empty =
        game::BuildWorldEventSpawnPlan(
            sparse, MakeEvent("event_drop", game::WorldEventStatus::Active));
    CHECK_FALSE(empty.content);
    CHECK(empty.requests.empty());

    game::WorldEventSpawnCallbacks rejected;
    rejected.spawn = [](const game::WorldEventSpawnRequest&) { return false; };
    CHECK_FALSE(game::EnsureWorldEventContent(plan, rejected, &result));
    CHECK_FALSE(result.ok);
    CHECK(result.ready == 0);
    CHECK(result.total == 4);
}

TEST_CASE("M33-F2-T3 世界事件：选择/失败结果与幂等账本") {
    const game::WorldEventDef def = MakeDef();
    game::WorldEventInstance resolved =
        MakeEvent("event_rescue", game::WorldEventStatus::Resolved);
    game::WorldEventOutcome outcome;
    std::string error;
    REQUIRE(game::BuildWorldEventOutcome(def, resolved, &outcome, &error));
    CHECK(error.empty());
    CHECK(outcome.kind == game::WorldEventOutcomeKind::Resolved);
    CHECK(outcome.choiceId == "rescue");
    CHECK(outcome.reputation == 15);
    CHECK(outcome.xp == doctest::Approx(80.0f));
    CHECK(outcome.skill == "melee");
    CHECK(outcome.item == "ammo_9mm");
    CHECK(outcome.itemCount == 2);
    CHECK(outcome.pressure == -12);

    resolved.choiceId = "leave";
    REQUIRE(game::BuildWorldEventOutcome(def, resolved, &outcome, &error));
    CHECK(outcome.reputation == -3);
    CHECK(outcome.xp == 0.0f);
    CHECK(outcome.pressure == 4);

    for (const game::WorldEventStatus status :
         {game::WorldEventStatus::Failed, game::WorldEventStatus::Expired}) {
        const game::WorldEventInstance failed =
            MakeEvent("event_failed", status);
        REQUIRE(game::BuildWorldEventOutcome(def, failed, &outcome, &error));
        CHECK(outcome.kind ==
              (status == game::WorldEventStatus::Failed
                   ? game::WorldEventOutcomeKind::Failed
                   : game::WorldEventOutcomeKind::Expired));
        CHECK(outcome.reputation == -7);
        CHECK(outcome.pressure == 9);
        CHECK(outcome.xp == 0.0f);
        CHECK(outcome.item.empty());
        CHECK(outcome.itemCount == 0);
    }

    const game::WorldEventInstance active =
        MakeEvent("event_active", game::WorldEventStatus::Active);
    CHECK_FALSE(game::BuildWorldEventOutcome(def, active, &outcome, &error));
    CHECK_FALSE(error.empty());

    game::WorldEventOutcomeLedger ledger;
    CHECK(ledger.Claim("event_rescue", 11));
    CHECK_FALSE(ledger.Claim("event_rescue", 11));
    CHECK(ledger.Contains("event_rescue", 11));
    CHECK_FALSE(ledger.Contains("event_rescue", 12));
    CHECK(ledger.Claim("event_rescue", 12));
    CHECK(ledger.Claim("event_failed", 11));
    CHECK(ledger.Size() == 3u);
    const std::vector<std::string> keys = ledger.Keys();
    REQUIRE(keys.size() == 3u);
    game::WorldEventOutcomeLedger restored;
    REQUIRE(restored.Restore(keys));
    CHECK(restored.Contains("event_rescue", 11));
    CHECK(restored.Contains("event_rescue", 12));
    CHECK(restored.Contains("event_failed", 11));
    CHECK_FALSE(restored.Restore({"bad-key"}));
    CHECK(ledger.Keys() == keys);
    ledger.Reset();
    CHECK(ledger.Size() == 0u);
}

TEST_CASE("M33-F2-T3 世界事件：声望增量与玩家幂等") {
    game::SettlementReputationConfig config;
    game::SettlementReputation reputation;
    game::SettlementReputationChange change =
        game::ApplySettlementReputationDelta(
            &reputation, config, "village_haven", 15, "event_a:11");
    CHECK(change.applied);
    CHECK(change.delta == 15);
    CHECK(change.current == 15);
    CHECK(game::SettlementReputationScore(reputation, "village_haven") == 15);

    change = game::ApplySettlementReputationDelta(
        &reputation, config, "village_haven", 15, "event_a:11");
    CHECK_FALSE(change.applied);
    CHECK(change.current == 15);

    change = game::ApplySettlementReputationDelta(
        &reputation, config, "village_haven", -5, "event_b:11");
    CHECK(change.applied);
    CHECK(change.current == 10);

    ecs::EcsWorld world;
    const ecs::Entity player = world.Create();
    change = game::AwardSettlementReputationDelta(
        world, player, config, "village_haven", 20, "event_c:11");
    CHECK(change.applied);
    CHECK(change.current == 20);
    const game::SettlementReputation* stored =
        game::FindSettlementReputation(world, player);
    REQUIRE(stored != nullptr);
    CHECK(game::SettlementReputationScore(*stored, "village_haven") == 20);
}

TEST_CASE("M33-F2-T3 世界事件：区域压力变化与事件幂等") {
    game::StrategicConfig config;
    config.enabled = true;
    game::StrategicRegionConfig region;
    region.id = "core";
    region.initialOwner = game::StrategicOwner::Human;
    region.pressure = 20;
    config.regions.push_back(region);
    game::StrategicState state;
    std::string error;
    REQUIRE(state.Initialize(config, &error));
    CHECK(error.empty());

    game::StrategicWorldEventResult result;
    REQUIRE(game::ApplyStrategicWorldEventEffect(
        state, config, "core", 12, "event_a", 100, &result, &error));
    CHECK(error.empty());
    CHECK(result.applied);
    CHECK(result.pressureBefore == doctest::Approx(20.0f));
    CHECK(result.pressureAfter == doctest::Approx(32.0f));

    result = {};
    REQUIRE(game::ApplyStrategicWorldEventEffect(
        state, config, "core", 12, "event_a", 100, &result, &error));
    CHECK_FALSE(result.applied);
    CHECK(result.alreadyApplied);
    CHECK(result.pressureAfter == doctest::Approx(32.0f));

    result = {};
    REQUIRE(game::ApplyStrategicWorldEventEffect(
        state, config, "core", -5, "event_b", 120, &result, &error));
    CHECK(result.applied);
    CHECK(result.pressureAfter == doctest::Approx(27.0f));
    const game::StrategicRegionState* saved = state.FindRegion("core");
    REQUIRE(saved != nullptr);
    CHECK(saved->pressure == doctest::Approx(27.0f));
}
