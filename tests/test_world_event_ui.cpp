#include <string>
#include <unordered_set>

#include <doctest/doctest.h>

#include "game/Poi.h"
#include "game/WorldEventDirector.h"
#include "game/WorldMap.h"
#include "ui/WorldEventUi.h"
#include "world/WorldDefinition.h"
#include "world/WorldGraph.h"

using namespace zl;

namespace {

game::WorldEventChoiceDef Choice(std::string id, std::string label,
                                 int reputation, float xp) {
    game::WorldEventChoiceDef choice;
    choice.id = std::move(id);
    choice.label = std::move(label);
    choice.reputation = reputation;
    choice.rewardXp = xp;
    choice.rewardItem = "bandage";
    choice.rewardItemCount = 2;
    choice.pressure = -3;
    return choice;
}

game::WorldEventDef EventDef() {
    game::WorldEventDef def;
    def.id = "clinic_choice";
    def.name = "诊所抉择";
    def.kind = "choice";
    def.minDay = 1;
    def.maxDay = 10;
    def.warningMinutes = 0;
    def.durationMinutes = 1440;
    def.region = "north_fields";
    game::WorldEventStageDef stage;
    stage.kind = game::WorldEventStageKind::Choice;
    stage.count = 1;
    stage.radius = 4.0f;
    stage.description = "决定如何处置诊所";
    def.stages.push_back(stage);
    def.choices.push_back(Choice("help", "帮助幸存者", 5, 10.0f));
    def.choices.push_back(Choice("leave", "离开", 0, 0.0f));
    return def;
}

}  // namespace

TEST_CASE("M33-F2-T4 世界事件 UI：倒计时、阶段与选项文本") {
    CHECK(std::string(ui::WorldEventStageLabel(
                  game::WorldEventStageKind::Reach)) == "抵达");
    CHECK(std::string(ui::WorldEventStageLabel(
                  game::WorldEventStageKind::Choice)) == "抉择");
    CHECK(ui::WorldEventCountdownText(0) == "已到期");
    CHECK(ui::WorldEventCountdownText(45) == "45 分钟");
    CHECK(ui::WorldEventCountdownText(125) == "2 小时 5 分");

    const game::WorldEventDef def = EventDef();
    game::WorldEventInstance event;
    event.id = "evt";
    event.defId = def.id;
    event.name = def.name;
    event.x = 10.0f;
    event.y = 10.0f;
    event.status = game::WorldEventStatus::Active;
    event.stageIndex = 0;
    event.stageKind = game::WorldEventStageKind::Choice;
    event.stageRadius = 4.0f;
    event.stageTarget = 1;
    CHECK(ui::WorldEventCanChoose(def, event, 12.0f, 10.0f));
    CHECK_FALSE(ui::WorldEventCanChoose(def, event, 17.0f, 10.0f));
    event.status = game::WorldEventStatus::Warning;
    CHECK_FALSE(ui::WorldEventCanChoose(def, event, 10.0f, 10.0f));
    event.status = game::WorldEventStatus::Active;
    event.stageKind = game::WorldEventStageKind::Reach;
    CHECK_FALSE(ui::WorldEventCanChoose(def, event, 10.0f, 10.0f));

    const std::string line = ui::WorldEventChoiceLine(0, def.choices[0]);
    CHECK(line.rfind("1. 帮助幸存者", 0) == 0);
    CHECK(line.find("声望+5") != std::string::npos);
    CHECK(line.find("经验+10") != std::string::npos);
    CHECK(line.find("bandage×2") != std::string::npos);
    CHECK(line.find("压力-3") != std::string::npos);
}

TEST_CASE("M33-F2-T4 世界事件 UI：成功/失败结果可解释") {
    const game::WorldEventDef def = EventDef();
    game::WorldEventInstance event;
    event.defId = def.id;
    event.name = def.name;
    event.status = game::WorldEventStatus::Resolved;
    event.choiceId = "help";
    const std::string ok = ui::WorldEventResultLine(
        def, event, "帮助幸存者", 5, 10.0f, -3, true, "bandage", 2);
    CHECK(ok.find("事件完成：诊所抉择") == 0);
    CHECK(ok.find("帮助幸存者") != std::string::npos);
    CHECK(ok.find("声望+5") != std::string::npos);
    CHECK(ok.find("背包已满，掉落在地") != std::string::npos);
    CHECK(ok.find("区域压力-3") != std::string::npos);

    event.status = game::WorldEventStatus::Expired;
    event.choiceId.clear();
    const std::string expired = ui::WorldEventResultLine(
        def, event, "", -5, 0.0f, 5, false, "", 0);
    CHECK(expired.find("事件错过：诊所抉择") == 0);
    CHECK(expired.find("声望-5") != std::string::npos);
    CHECK(expired.find("区域压力+5") != std::string::npos);
}

TEST_CASE("M33-F2-T4 世界事件地图标记") {
    const std::string mapDir =
        std::string(ZL_SOURCE_DIR) + "/assets/maps/village_demo_m31";
    world::WorldDefinition definition;
    std::string error;
    REQUIRE(world::LoadWorldDefinition(mapDir, &definition, &error) ==
            world::WorldDefinitionStatus::Loaded);
    const world::WorldGraph graph(definition);
    REQUIRE(graph.Valid());

    game::WorldEventConfig config;
    config.enabled = true;
    config.randomSeed = 7u;
    config.defs.push_back(EventDef());
    game::WorldEventDirector director(config, definition);
    director.Update(0.0, {});
    director.Update(2.0, 150.0f, 64.0f);
    REQUIRE(director.Events().size() == 1u);
    REQUIRE(director.Events().front().status ==
            game::WorldEventStatus::Active);

    game::PoiConfig pois;
    std::unordered_set<std::string> discoveredRegions{"north_fields"};
    std::unordered_set<std::string> discoveredPois;
    std::unordered_set<std::string> completedPois;
    game::WorldMapObjectiveInput objective;
    game::WorldMapRoute route;
    const game::WorldMapState state = game::BuildWorldMapState(
        graph, pois, 150.0f, 64.0f, true, discoveredRegions,
        discoveredPois, completedPois, nullptr, objective, route, nullptr,
        {}, &director);

    REQUIRE(state.worldEvents.size() == 1u);
    const game::WorldMapWorldEventMarker& marker = state.worldEvents.front();
    CHECK(marker.id == director.Events().front().id);
    CHECK(marker.name == "诊所抉择");
    CHECK(marker.regionId == "north_fields");
    CHECK(marker.regionName == "北部农田");
    CHECK(marker.active);
    CHECK_FALSE(marker.warning);
    CHECK(marker.x == doctest::Approx(150.0f));
    CHECK(marker.y == doctest::Approx(64.0f));
    CHECK(state.events.empty());
}
