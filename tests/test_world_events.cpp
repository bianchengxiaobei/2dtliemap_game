#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include <nlohmann/json.hpp>

#include "core/Config.h"
#include "game/WorldEvent.h"
#include "items/ItemRegistry.h"

using namespace zl;

namespace {

struct TempRoot {
    std::filesystem::path path;

    explicit TempRoot(const std::string& name)
        : path(std::filesystem::temp_directory_path() /
               "zl_world_events" / name) {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path, ec);
    }

    ~TempRoot() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

void WriteText(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << text;
}

std::string EventDocument(std::string_view defs) {
    return std::string(R"({"format":"world-events-v1","defs":[)") +
           std::string(defs) + "]}";
}

std::string CaravanEvent() {
    return R"({
      "id": "caravan_ambush",
      "name": "商队伏击",
      "kind": "caravan",
      "minDay": 3,
      "maxDay": 60,
      "weight": 1.5,
      "cooldownDays": 5,
      "warningMinutes": 120,
      "durationMinutes": 30,
      "region": "any",
      "stages": [
        { "kind": "reach", "radius": 8, "description": "赶到商队遇袭点" },
        { "kind": "kill", "target": "event_caravan_raider", "count": 3 }
      ],
      "choices": [
        { "id": "rescue", "label": "护送商队", "reputation": 15, "rewardXp": 80, "pressure": -12 },
        { "id": "rob", "label": "抢走货物", "reputation": -25, "rewardItem": "ammo_9mm", "rewardItemCount": 2, "pressure": 8 }
      ],
      "nextEvent": "caravan_aftermath",
      "failureReputation": -7,
      "failurePressure": 9
    })";
}

std::string AftermathEvent() {
    return R"({
      "id": "caravan_aftermath",
      "name": "商队余波",
      "stages": [{ "kind": "choice" }],
      "choices": [{ "id": "leave", "label": "离开" }]
    })";
}

}  // namespace

TEST_CASE("M33-F2-T1 世界事件数据契约与默认值") {
    TempRoot root("contract");
    const std::filesystem::path eventFile = root.path / "world_events.json";
    WriteText(eventFile, EventDocument(CaravanEvent() + "," +
                                        AftermathEvent()));

    core::Config config;
    config.Set("worldEvents", nlohmann::json{
                                  {"enabled", true},
                                  {"file", eventFile.generic_string()},
                                  {"randomSeed", 12345},
                              });
    items::ItemRegistry items;
    REQUIRE(items.LoadFile(std::string(ZL_SOURCE_DIR) + "/assets/items.json"));

    game::WorldEventConfig events;
    std::string error;
    REQUIRE(game::LoadWorldEventConfig(config, &events, &error, &items));
    CHECK(error.empty());
    CHECK(events.enabled);
    CHECK(events.randomSeed == 12345u);
    REQUIRE(events.defs.size() == 2u);

    const game::WorldEventDef* caravan = events.Find("caravan_ambush");
    REQUIRE(caravan != nullptr);
    CHECK(caravan->name == "商队伏击");
    CHECK(caravan->kind == "caravan");
    CHECK(caravan->minDay == 3);
    CHECK(caravan->maxDay == 60);
    CHECK(caravan->weight == doctest::Approx(1.5f));
    CHECK(caravan->cooldownDays == 5);
    CHECK(caravan->warningMinutes == 120);
    CHECK(caravan->durationMinutes == 30);
    REQUIRE(caravan->stages.size() == 2u);
    CHECK(caravan->stages[0].kind == game::WorldEventStageKind::Reach);
    CHECK(caravan->stages[0].radius == doctest::Approx(8.0f));
    CHECK(caravan->stages[0].description == "赶到商队遇袭点");
    CHECK(caravan->stages[1].kind == game::WorldEventStageKind::Kill);
    CHECK(caravan->stages[1].target == "event_caravan_raider");
    CHECK(caravan->stages[1].count == 3);
    REQUIRE(caravan->choices.size() == 2u);
    CHECK(caravan->choices[0].id == "rescue");
    CHECK(caravan->choices[0].reputation == 15);
    CHECK(caravan->choices[0].rewardXp == doctest::Approx(80.0f));
    CHECK(caravan->choices[0].pressure == -12);
    CHECK(caravan->choices[1].rewardItem == "ammo_9mm");
    CHECK(caravan->choices[1].rewardItemCount == 2);
    CHECK(caravan->choices[1].pressure == 8);
    CHECK(caravan->failureReputation == -7);
    CHECK(caravan->failurePressure == 9);
    CHECK(caravan->nextEvent == "caravan_aftermath");
    CHECK(game::WorldEventHasNext(events, *caravan));

    const game::WorldEventDef* aftermath = events.Find("caravan_aftermath");
    REQUIRE(aftermath != nullptr);
    CHECK(aftermath->kind == "custom");
    CHECK(aftermath->region == "any");
    CHECK(aftermath->minDay == 1);
    CHECK(aftermath->maxDay == 100000);
    CHECK(aftermath->weight == doctest::Approx(1.0f));
    CHECK(aftermath->cooldownDays == 1);
    CHECK(aftermath->warningMinutes == 60);
    CHECK(aftermath->durationMinutes == 30);
    REQUIRE(aftermath->choices.size() == 1u);
    CHECK(aftermath->choices[0].label == "离开");
    CHECK(aftermath->choices[0].reputation == 0);
    CHECK(aftermath->choices[0].pressure == 0);
    CHECK(aftermath->choices[0].rewardItemCount == 1);
    CHECK(aftermath->failureReputation == -5);
    CHECK(aftermath->failurePressure == 5);
    CHECK(game::WorldEventStageKindName(
              game::WorldEventStageKind::Defend) == std::string("defend"));

    core::Config disabled;
    game::WorldEventConfig empty;
    REQUIRE(game::LoadWorldEventConfig(disabled, &empty, &error));
    CHECK_FALSE(empty.enabled);
    CHECK(empty.defs.empty());
}

TEST_CASE("M33-F2-T1 世界事件非法字段与坏引用被跳过") {
    game::WorldEventConfig events;
    events.enabled = true;
    game::WorldEventMergeStats stats;
    std::string error;

    const std::string valid =
        R"({"id":"valid_a","name":"有效 A","stages":[{"kind":"reach"}],"choices":[{"id":"go","label":"去"}],"nextEvent":"valid_b"})";
    const std::string validB =
        R"({"id":"valid_b","name":"有效 B","stages":[{"kind":"choice"}],"choices":[{"id":"go","label":"去"}]})";
    const std::string badRange =
        R"({"id":"bad_range","name":"坏范围","weight":0,"stages":[{"kind":"reach"}],"choices":[{"id":"go","label":"去"}]})";
    const std::string badStage =
        R"({"id":"bad_stage","name":"坏阶段","stages":[{"kind":"kill"}],"choices":[{"id":"go","label":"去"}]})";
    const std::string badRef =
        R"({"id":"bad_ref","name":"坏引用","stages":[{"kind":"reach"}],"choices":[{"id":"go","label":"去"}],"nextEvent":"missing_event"})";
    const std::string cycleA =
        R"({"id":"cycle_a","name":"环 A","stages":[{"kind":"reach"}],"choices":[{"id":"go","label":"去"}],"nextEvent":"cycle_b"})";
    const std::string cycleB =
        R"({"id":"cycle_b","name":"环 B","stages":[{"kind":"reach"}],"choices":[{"id":"go","label":"去"}],"nextEvent":"cycle_a"})";
    const std::string duplicate =
        R"({"id":"valid_a","name":"重复","stages":[{"kind":"reach"}],"choices":[{"id":"go","label":"去"}]})";
    const std::string badPressure =
        R"({"id":"bad_pressure","name":"坏压力","stages":[{"kind":"reach"}],"choices":[{"id":"go","label":"去","pressure":101}]})";

    const std::string doc = EventDocument(
        valid + "," + validB + "," + badRange + "," + badStage + "," +
        badRef + "," + cycleA + "," + cycleB + "," + duplicate + "," +
        badPressure);
    REQUIRE(game::MergeWorldEventJson(doc, "mixed", &events, &stats, &error));
    CHECK(events.defs.size() == 2u);
    CHECK(events.Find("valid_a") != nullptr);
    CHECK(events.Find("valid_b") != nullptr);
    CHECK(events.Find("bad_ref") == nullptr);
    CHECK(events.Find("bad_pressure") == nullptr);
    CHECK(events.Find("cycle_a") == nullptr);
    CHECK(events.Find("cycle_b") == nullptr);
    CHECK(stats.defsAdded == 2);
    CHECK(stats.defsSkipped >= 7);

    game::WorldEventConfig invalid;
    invalid.enabled = true;
    error.clear();
    CHECK_FALSE(game::MergeWorldEventJson(
        EventDocument(R"({"id":"bad","name":"坏","stages":[{"kind":"reach"}],"choices":[{"id":"go","label":"去"}],"minDay":0})"),
        "invalid", &invalid, nullptr, &error));
    CHECK(invalid.defs.empty());

    game::WorldEventConfig badFormat;
    badFormat.enabled = true;
    CHECK_FALSE(game::MergeWorldEventJson(
        R"({"format":"world-events-v2","defs":[]})", "bad format",
        &badFormat, nullptr, &error));
    CHECK(error.find("world-events-v1") != std::string::npos);

    TempRoot missingRoot("missing");
    core::Config missingFile;
    missingFile.Set(
        "worldEvents",
        nlohmann::json{{"enabled", true},
                       {"file", (missingRoot.path / "missing.json")
                                    .generic_string()}});
    game::WorldEventConfig missing;
    CHECK_FALSE(game::LoadWorldEventConfig(missingFile, &missing, &error));
}

TEST_CASE("M33-F2-T1 世界事件奖励引用与 mod 追加") {
    items::ItemRegistry items;
    REQUIRE(items.LoadFile(std::string(ZL_SOURCE_DIR) + "/assets/items.json"));

    game::WorldEventConfig events;
    events.enabled = true;
    game::WorldEventMergeStats stats;
    std::string error;
    const std::string base =
        R"({"id":"base_event","name":"基础事件","stages":[{"kind":"reach"}],"choices":[{"id":"go","label":"去"}]})";
    REQUIRE(game::MergeWorldEventJson(EventDocument(base), "base", &events,
                                      &stats, &error, &items));

    const std::string mod =
        R"({"id":"mod_event","name":"模组事件","stages":[{"kind":"choice"}],"choices":[{"id":"take","label":"拿","rewardItem":"ammo_9mm"}],"nextEvent":"base_event"})";
    REQUIRE(game::MergeWorldEventJson(EventDocument(mod), "mod", &events,
                                      &stats, &error, &items));
    CHECK(events.Find("mod_event") != nullptr);
    CHECK(stats.filesLoaded == 2);
    CHECK(stats.defsAdded == 2);

    game::WorldEventConfig badReward;
    badReward.enabled = true;
    error.clear();
    CHECK_FALSE(game::MergeWorldEventJson(
        EventDocument(R"({"id":"bad_reward","name":"坏奖励","stages":[{"kind":"reach"}],"choices":[{"id":"go","label":"拿","rewardItem":"missing_item"}]})"),
        "bad reward", &badReward, nullptr, &error, &items));
    CHECK(badReward.defs.empty());

    game::WorldEventConfig emptyDoc;
    emptyDoc.enabled = true;
    CHECK_FALSE(game::MergeWorldEventJson(
        EventDocument(""), "empty", &emptyDoc, nullptr, &error, &items));
}
