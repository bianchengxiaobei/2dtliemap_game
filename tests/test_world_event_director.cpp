#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "game/WorldEventDirector.h"
#include "world/WorldDefinition.h"

using namespace zl;

namespace {

world::WorldDefinition MakeDefinition() {
    world::WorldDefinition definition;
    definition.format = world::kWorldFormat;
    definition.mapId = "world_event_director_test";
    definition.seed = 20261113;
    definition.generatorVersion = 1;
    definition.bounds = world::WorldBounds{0, 0, 20, 20};
    definition.regions.push_back(world::WorldRegion{
        "core", world::WorldRect{0, 0, 4, 4}, "village", {"clinic"}});
    definition.regions.push_back(world::WorldRegion{
        "wilds", world::WorldRect{10, 10, 4, 4}, "forest", {"camp"}});
    definition.pois.push_back(
        world::WorldPoi{"clinic", "clinic_v1", 15.5, 15.5, 4.0, 1, "test"});
    definition.pois.push_back(
        world::WorldPoi{"camp", "camp_v1", 115.5, 115.5, 4.0, 1, "test"});
    return definition;
}

game::WorldEventChoiceDef Choice(std::string id, std::string label,
                                 int reputation, float xp) {
    game::WorldEventChoiceDef choice;
    choice.id = std::move(id);
    choice.label = std::move(label);
    choice.reputation = reputation;
    choice.rewardXp = xp;
    return choice;
}

game::WorldEventStageDef Stage(game::WorldEventStageKind kind,
                               std::string target, int count, float radius) {
    game::WorldEventStageDef stage;
    stage.kind = kind;
    stage.target = std::move(target);
    stage.count = count;
    stage.radius = radius;
    return stage;
}

game::WorldEventDef AlphaEvent() {
    game::WorldEventDef def;
    def.id = "caravan_ambush";
    def.name = "商队伏击";
    def.kind = "caravan";
    def.minDay = 1;
    def.maxDay = 10;
    def.weight = 1.0f;
    def.cooldownDays = 0;
    def.warningMinutes = 60;
    def.durationMinutes = 30;
    def.region = "any";
    def.stages.push_back(
        Stage(game::WorldEventStageKind::Reach, {}, 1, 6.0f));
    def.stages.push_back(
        Stage(game::WorldEventStageKind::Kill, "raider", 2, 6.0f));
    def.choices.push_back(Choice("rescue", "救援", 5, 10.0f));
    def.choices.push_back(Choice("leave", "离开", 0, 0.0f));
    def.nextEvent = "caravan_aftermath";
    return def;
}

game::WorldEventDef BetaEvent() {
    game::WorldEventDef def;
    def.id = "caravan_aftermath";
    def.name = "商队余波";
    def.kind = "caravan";
    def.minDay = 2;
    def.maxDay = 10;
    def.weight = 1.0f;
    def.cooldownDays = 0;
    def.warningMinutes = 30;
    def.durationMinutes = 20;
    def.region = "core";
    def.stages.push_back(
        Stage(game::WorldEventStageKind::Reach, {}, 1, 6.0f));
    def.choices.push_back(Choice("leave", "离开", 0, 0.0f));
    return def;
}

game::WorldEventConfig MakeConfig() {
    game::WorldEventConfig config;
    config.enabled = true;
    config.randomSeed = 424242u;
    config.defs.push_back(AlphaEvent());
    config.defs.push_back(BetaEvent());
    return config;
}

game::WorldEventDirector MakeDirector() {
    game::WorldEventDirectorConfig options;
    options.detailRadius = 8.0f;
    return game::WorldEventDirector(MakeConfig(), MakeDefinition(), options);
}

const game::WorldEventInstance* FindEvent(
    const game::WorldEventDirector& director, std::string_view id) {
    return director.Find(id);
}

std::vector<game::WorldEventInstance> EventsForDef(
    const game::WorldEventDirector& director, std::string_view defId) {
    std::vector<game::WorldEventInstance> result;
    for (const game::WorldEventInstance& event : director.Events()) {
        if (event.defId == defId) {
            result.push_back(event);
        }
    }
    return result;
}

}  // namespace

TEST_CASE("M33-F2-T2 世界事件：确定性排期、预警、同日去重与跨日推进") {
    game::WorldEventDirector first = MakeDirector();
    game::WorldEventDirector second = MakeDirector();
    first.Update(0.0, {});
    second.Update(0.0, {});

    REQUIRE(first.Events().size() == 1u);
    REQUIRE(second.Events().size() == 1u);
    const game::WorldEventInstance* event = &first.Events().front();
    const game::WorldEventInstance* same = &second.Events().front();
    CHECK(event->defId == "caravan_ambush");
    CHECK(event->status == game::WorldEventStatus::Scheduled);
    CHECK(event->scheduledMinute == 0);
    CHECK(event->warningMinute < event->activeMinute);
    CHECK(event->activeMinute < event->deadlineMinute);
    CHECK(event->id == same->id);
    CHECK(event->regionId == same->regionId);
    CHECK(event->x == doctest::Approx(same->x));
    CHECK(event->y == doctest::Approx(same->y));
    CHECK(event->warningMinute == same->warningMinute);
    CHECK(event->activeMinute == same->activeMinute);

    first.Update(1.0, {});
    CHECK(first.Events().size() == 1u);

    first.Update(static_cast<double>(event->warningMinute), {});
    CHECK(FindEvent(first, event->id)->status ==
          game::WorldEventStatus::Warning);
    CHECK(first.Stats().warning == 1);
    CHECK(first.Stats().active == 0);

    first.Update(static_cast<double>(event->activeMinute), {});
    const game::WorldEventInstance* active = FindEvent(first, event->id);
    REQUIRE(active != nullptr);
    CHECK(active->status == game::WorldEventStatus::Active);
    CHECK(active->stageIndex == 0);
    CHECK(active->stageKind == game::WorldEventStageKind::Reach);

    first.Update(static_cast<double>(active->deadlineMinute), {});
    CHECK(FindEvent(first, event->id)->status ==
          game::WorldEventStatus::Expired);

    game::WorldEventDirector nextDay = MakeDirector();
    nextDay.Update(0.0, {});
    REQUIRE(nextDay.Events().size() == 1u);
    nextDay.Update(1440.0, {});
    REQUIRE(nextDay.Events().size() == 2u);
    const game::WorldEventInstance* oldEvent = &nextDay.Events()[0];
    const game::WorldEventInstance* newEvent = &nextDay.Events()[1];
    CHECK(oldEvent->day == 1);
    CHECK(oldEvent->status == game::WorldEventStatus::Expired);
    CHECK(newEvent->day == 2);
    CHECK(newEvent->id != oldEvent->id);
    CHECK(newEvent->status == game::WorldEventStatus::Scheduled);
    CHECK(nextDay.Day() == 2);
    nextDay.Update(1441.0, {});
    CHECK(nextDay.Events().size() == 2u);
}

TEST_CASE("M33-F2-T2 世界事件：阶段、离开降级、超时与事件链") {
    game::WorldEventDirector director = MakeDirector();
    director.Update(0.0, {});
    REQUIRE(director.Events().size() == 1u);
    const std::string eventId = director.Events().front().id;
    const std::int64_t activeMinute = director.Events().front().activeMinute;
    const float siteX = director.Events().front().x;
    const float siteY = director.Events().front().y;

    director.Update(static_cast<double>(activeMinute), siteX, siteY);
    const game::WorldEventInstance* event = FindEvent(director, eventId);
    REQUIRE(event != nullptr);
    REQUIRE(event->status == game::WorldEventStatus::Active);
    CHECK(event->detail);
    CHECK(event->stageIndex == 1);
    CHECK(event->stageKind == game::WorldEventStageKind::Kill);
    CHECK(event->stageTargetId == "raider");
    CHECK(event->stageTarget == 2);

    std::string error;
    CHECK_FALSE(director.ReportStageProgress(
        eventId, game::WorldEventStageKind::Kill, "wrong", 1, &error));
    CHECK_FALSE(error.empty());
    CHECK(director.ReportStageProgress(
        eventId, game::WorldEventStageKind::Kill, "raider", 2, &error));
    CHECK(error.empty());
    event = FindEvent(director, eventId);
    REQUIRE(event != nullptr);
    CHECK(event->stagesComplete);
    CHECK(event->stageIndex == 2);
    CHECK_FALSE(director.Choose(eventId, "missing", &error));
    CHECK_FALSE(error.empty());
    REQUIRE(director.Choose(eventId, "rescue", &error));
    CHECK(error.empty());
    event = FindEvent(director, eventId);
    REQUIRE(event != nullptr);
    CHECK(event->status == game::WorldEventStatus::Resolved);
    CHECK(event->choiceId == "rescue");
    CHECK(event->nextEventId == "caravan_aftermath");
    CHECK(director.Stats().resolved == 1);
    CHECK(director.Stats().chained == 1);
    REQUIRE(director.Events().size() == 2u);
    const game::WorldEventInstance* chain = &director.Events()[1];
    CHECK(chain->defId == "caravan_aftermath");
    CHECK(chain->day == 2);
    CHECK(chain->chainDepth == 1);
    CHECK(chain->status == game::WorldEventStatus::Scheduled);

    game::WorldEventDirector leave = MakeDirector();
    leave.Update(0.0, {});
    const std::string leaveId = leave.Events().front().id;
    const std::int64_t leaveActive = leave.Events().front().activeMinute;
    const std::int64_t leaveDeadline = leave.Events().front().deadlineMinute;
    leave.Update(static_cast<double>(leaveActive), siteX, siteY);
    leave.Update(static_cast<double>(leaveActive + 1), -100.0f, -100.0f);
    const game::WorldEventInstance* left = FindEvent(leave, leaveId);
    REQUIRE(left != nullptr);
    CHECK(left->status == game::WorldEventStatus::Active);
    CHECK_FALSE(left->detail);
    CHECK(left->downgraded);
    CHECK(leave.ActiveDetailed() == nullptr);
    leave.Update(static_cast<double>(leaveActive + 2), siteX, siteY);
    CHECK(FindEvent(leave, leaveId)->detail);
    leave.Update(static_cast<double>(leaveDeadline), siteX, siteY);
    CHECK(FindEvent(leave, leaveId)->status ==
          game::WorldEventStatus::Expired);
    CHECK(leave.Stats().expired == 1);

    game::WorldEventDirector failed = MakeDirector();
    failed.Update(0.0, {});
    const std::string failedId = failed.Events().front().id;
    failed.Update(static_cast<double>(failed.Events().front().activeMinute),
                  failed.Events().front().x, failed.Events().front().y);
    REQUIRE(failed.Fail(failedId, &error));
    CHECK(error.empty());
    CHECK(FindEvent(failed, failedId)->status ==
          game::WorldEventStatus::Failed);
    CHECK(failed.Stats().failed == 1);
}

TEST_CASE("M33-F2-T2 世界事件：迟到加载不无提示激活，非法恢复保持旧状态") {
    game::WorldEventDirector late = MakeDirector();
    late.Update(0.0, {});
    REQUIRE(late.Events().size() == 1u);
    const game::WorldEventInstance* planned = &late.Events().front();
    game::WorldEventDirector lateLoad = MakeDirector();
    lateLoad.Update(static_cast<double>(planned->activeMinute), {});
    REQUIRE(lateLoad.Events().size() == 1u);
    const game::WorldEventInstance* after = &lateLoad.Events().front();
    CHECK(after->status == game::WorldEventStatus::Expired);
    CHECK(lateLoad.Stats().active == 0);

    game::WorldEventDirector source = MakeDirector();
    source.Update(0.0, {});
    const std::string sourceId = source.Events().front().id;
    source.Update(static_cast<double>(source.Events().front().activeMinute),
                  source.Events().front().x, source.Events().front().y);
    const std::vector<game::WorldEventInstance> snapshot = source.Snapshot();
    REQUIRE(source.Find(sourceId)->status == game::WorldEventStatus::Active);

    game::WorldEventDirector restored = MakeDirector();
    std::string error;
    REQUIRE(restored.Restore(
        snapshot, 1, static_cast<double>(snapshot.front().activeMinute),
        &error));
    CHECK(error.empty());
    REQUIRE(restored.Find(sourceId) != nullptr);
    CHECK(restored.Find(sourceId)->status == game::WorldEventStatus::Active);
    CHECK(restored.Day() == 1);

    std::vector<game::WorldEventInstance> invalid = snapshot;
    invalid.push_back(invalid.front());
    CHECK_FALSE(restored.Restore(invalid, 1, 0.0, &error));
    CHECK_FALSE(error.empty());
    CHECK(restored.Find(sourceId) != nullptr);

    invalid = snapshot;
    invalid.front().defId = "missing";
    CHECK_FALSE(restored.Restore(invalid, 1, 0.0, &error));
    invalid = snapshot;
    invalid.front().status = static_cast<game::WorldEventStatus>(9);
    CHECK_FALSE(restored.Restore(invalid, 1, 0.0, &error));
    invalid = snapshot;
    invalid.front().stageIndex = 99;
    CHECK_FALSE(restored.Restore(invalid, 1, 0.0, &error));
    invalid = snapshot;
    invalid.front().stageTarget = 99;
    CHECK_FALSE(restored.Restore(invalid, 1, 0.0, &error));
    invalid = snapshot;
    invalid.front().deadlineMinute = invalid.front().activeMinute;
    CHECK_FALSE(restored.Restore(invalid, 1, 0.0, &error));
    CHECK(restored.Find(sourceId) != nullptr);
}

TEST_CASE("M33-F2-T6 链后续事件不参与随机主排期") {
    game::WorldEventDirector director = MakeDirector();
    std::set<std::string> seen;
    for (int day = 1; day <= 10; ++day) {
        const double start = static_cast<double>(day - 1) * 1440.0;
        director.Update(start, {});
        for (const game::WorldEventInstance& event : director.Events()) {
            if (!seen.insert(event.id).second) {
                continue;
            }
            CHECK(event.day == day);
            CHECK(event.defId == "caravan_ambush");
        }
        director.Update(start + 1439.0, {});
    }
    CHECK(seen.size() == 10u);
    CHECK(EventsForDef(director, "caravan_aftermath").empty());
}

TEST_CASE("M33-F2-T2 世界事件：显式区域与冷却日") {
    game::WorldEventConfig config;
    config.enabled = true;
    config.randomSeed = 7u;
    game::WorldEventDef def = AlphaEvent();
    def.region = "wilds";
    def.cooldownDays = 2;
    def.warningMinutes = 0;
    config.defs.push_back(def);

    game::WorldEventDirector director(config, MakeDefinition());
    director.Update(0.0, {});
    REQUIRE(director.Events().size() == 1u);
    CHECK(director.Events().front().regionId == "wilds");
    CHECK(director.Events().front().x >= 100.5f);
    CHECK(director.Events().front().x <= 140.0f);
    CHECK(director.Events().front().y >= 100.5f);
    CHECK(director.Events().front().y <= 140.0f);

    director.Update(1440.0, {});
    CHECK(EventsForDef(director, "caravan_ambush").size() == 1u);
    director.Update(2880.0, {});
    CHECK(EventsForDef(director, "caravan_ambush").size() == 2u);
}

