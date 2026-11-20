#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "core/Config.h"
#include "game/Poi.h"

using namespace zl;

TEST_CASE("M19-F2 POI 任务规则与指引") {
    game::PoiDef poi;
    poi.id = "gas_station";
    poi.name = "废弃加油站";
    poi.threat = 2;
    poi.task.description = "清剿守军并搜索储物柜";
    poi.task.targetTable = "poi_gas_station";
    poi.task.rewardXp = 60.0f;

    CHECK_FALSE(game::ShouldCompletePoiTask(poi, false, 0));
    CHECK_FALSE(game::ShouldCompletePoiTask(poi, true, 2));
    CHECK(game::ShouldCompletePoiTask(poi, true, 0));

    CHECK(game::PoiThreatLabel(1) == std::string("低"));
    CHECK(game::PoiThreatLabel(2) == std::string("中"));
    CHECK(game::PoiThreatLabel(3) == std::string("高"));
    CHECK(game::PoiDirectionLabel(10.0f, 0.0f) == std::string("东"));
    CHECK(game::PoiDirectionLabel(-10.0f, -10.0f) == std::string("西北"));

    CHECK(game::PoiTaskText(poi, false, 2) == "清剿守军并搜索储物柜");
    CHECK(game::PoiTaskText(poi, true, 2).find("物资已搜") != std::string::npos);
    CHECK(game::PoiTaskText(poi, false, 0).find("区域已清") != std::string::npos);

    const std::string guide =
        game::PoiGuideText(poi, 10.0f, 0.0f, 10.0f, false, 2);
    CHECK(guide.find("废弃加油站") != std::string::npos);
    CHECK(guide.find("东 10格") != std::string::npos);
    CHECK(game::PoiGuideTone(poi, false, 2) == std::string("warn"));

    std::vector<std::string> completed;
    CHECK(game::MarkPoiCompleted(&completed, "gas_station"));
    CHECK_FALSE(game::MarkPoiCompleted(&completed, "gas_station"));
    CHECK(game::IsPoiCompleted(completed, "gas_station"));
    CHECK_FALSE(game::IsPoiCompleted(completed, "clinic"));
}

TEST_CASE("M19-F2 守军变体按配置选择") {
    game::PoiEnemyDef enemies;
    enemies.kind = game::PoiGuardKind::Mixed;
    CHECK(game::PoiGuardKindForSpawn(enemies, 0) == game::PoiGuardKind::Normal);
    CHECK(game::PoiGuardKindForSpawn(enemies, 1) == game::PoiGuardKind::Fast);
    CHECK(game::PoiGuardKindForSpawn(enemies, 2) == game::PoiGuardKind::Strong);

    enemies.kind = game::PoiGuardKind::Fast;
    CHECK(game::PoiGuardKindForSpawn(enemies, 2) == game::PoiGuardKind::Fast);

    enemies.kind = game::PoiGuardKind::Strong;
    enemies.count = 9;
    enemies.armoredCount = 2;
    CHECK(game::PoiGuardKindForSpawn(enemies, 0) ==
          game::PoiGuardKind::Armored);
    CHECK(game::PoiGuardKindForSpawn(enemies, 1) ==
          game::PoiGuardKind::Armored);
    CHECK(game::PoiGuardKindForSpawn(enemies, 2) ==
          game::PoiGuardKind::Strong);
    CHECK(game::PoiGuardKindName(game::PoiGuardKind::Armored) ==
          std::string("armored"));
}

TEST_CASE("M19-F2/F6-T1 内置 POI 配置可解析且每区有独立任务") {
    core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    game::PoiConfig pois;
    std::string error;
    REQUIRE(game::LoadPoiConfig(config, &pois, &error));
    CHECK(error.empty());
    CHECK(pois.enabled);
    REQUIRE(pois.defs.size() == 11u);

    const game::PoiDef* gas = pois.Find("gas_station");
    const game::PoiDef* clinic = pois.Find("clinic");
    const game::PoiDef* farm = pois.Find("farm_barn");
    const game::PoiDef* hospital = pois.Find("hospital");
    const game::PoiDef* military = pois.Find("military_checkpoint");
    REQUIRE(gas != nullptr);
    REQUIRE(clinic != nullptr);
    REQUIRE(farm != nullptr);
    REQUIRE(hospital != nullptr);
    REQUIRE(military != nullptr);
    CHECK(gas->threat == 1);
    CHECK(clinic->threat == 2);
    CHECK(farm->threat == 3);
    CHECK(hospital->threat == 2);
    CHECK(military->threat == 3);
    CHECK(gas->task.targetTable == "poi_gas_station");
    CHECK(clinic->task.targetTable == "poi_clinic_medicine");
    CHECK(farm->task.targetTable == "poi_farm_barn");
    CHECK(hospital->task.targetTable == "poi_hospital_pharmacy");
    CHECK(military->task.targetTable == "poi_military_armory");
    CHECK(gas->task.rewardItem == "ammo_9mm");
    CHECK(gas->task.rewardCount == 12);
    CHECK(farm->task.rewardItem == "ammo_shells");
    CHECK(farm->task.rewardCount == 6);
    CHECK(hospital->task.rewardItem == "antibiotics");
    CHECK(hospital->task.rewardCount == 2);
    CHECK(military->task.rewardItem == "ammo_9mm");
    CHECK(military->task.rewardCount == 18);
    CHECK(military->enemies.armoredCount == 2);
    CHECK(military->enemies.armoredCount < military->enemies.count);
    CHECK(hospital->enemies.armoredCount == 0);
    CHECK(gas->task.description.find("弹药") != std::string::npos);
    CHECK(farm->task.description.find("弹药") != std::string::npos);
    CHECK(hospital->task.description.find("医疗") != std::string::npos);
    CHECK(military->task.description.find("枪械") != std::string::npos);
    for (const game::PoiDef* poi : {gas, clinic, farm, hospital, military}) {
        CHECK(poi->containers.size() >= 2u);
        CHECK(poi->enemies.count > 0);
        CHECK(poi->task.rewardXp > 0.0f);
        bool targetFound = false;
        for (const game::PoiContainerDef& container : poi->containers) {
            targetFound = targetFound || container.tableId == poi->task.targetTable;
        }
        CHECK(targetFound);
    }
}

TEST_CASE("M24-T2 六个新 POI 容器、威胁与奖励可验收") {
    core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    game::PoiConfig pois;
    std::string error;
    REQUIRE(game::LoadPoiConfig(config, &pois, &error));
    CHECK(error.empty());
    REQUIRE(pois.defs.size() == 11u);

    const game::PoiDef* supermarket = pois.Find("supermarket");
    const game::PoiDef* motel = pois.Find("motel");
    const game::PoiDef* police = pois.Find("police_station");
    const game::PoiDef* fire = pois.Find("fire_station");
    const game::PoiDef* church = pois.Find("church");
    const game::PoiDef* warehouse = pois.Find("warehouse");
    for (const game::PoiDef* poi : {supermarket, motel, police, fire, church,
                                    warehouse}) {
        REQUIRE(poi != nullptr);
        CHECK(poi->containers.size() >= 3u);
        CHECK(poi->enemies.count >= 3);
        bool targetFound = false;
        for (const game::PoiContainerDef& container : poi->containers) {
            targetFound = targetFound || container.tableId == poi->task.targetTable;
        }
        CHECK(targetFound);
    }
    CHECK(supermarket->containers.size() == 3u);
    CHECK(motel->containers.size() == 3u);
    CHECK(police->containers.size() == 3u);
    CHECK(fire->containers.size() == 3u);
    CHECK(church->containers.size() == 3u);
    CHECK(warehouse->containers.size() == 4u);
    bool relayShelf = false;
    for (const game::PoiContainerDef& container : warehouse->containers) {
        relayShelf = relayShelf || container.tableId == "poi_warehouse_relay";
    }
    CHECK(relayShelf);

    CHECK(supermarket->threat == 1);
    CHECK(motel->threat == 1);
    CHECK(police->threat == 2);
    CHECK(fire->threat == 2);
    CHECK(church->threat == 2);
    CHECK(warehouse->threat == 3);
    CHECK(police->enemies.armoredCount == 1);
    CHECK(fire->enemies.armoredCount == 1);
    CHECK(church->enemies.armoredCount == 1);
    CHECK(warehouse->enemies.kind == game::PoiGuardKind::Strong);
    CHECK(warehouse->enemies.armoredCount == 2);
    CHECK(supermarket->task.targetTable == "poi_supermarket_storage");
    CHECK(motel->task.targetTable == "poi_motel_storage");
    CHECK(police->task.targetTable == "poi_police_armory");
    CHECK(fire->task.targetTable == "poi_fire_gear");
    CHECK(church->task.targetTable == "poi_church_supplies");
    CHECK(warehouse->task.targetTable == "poi_warehouse_valuables");
    CHECK(warehouse->task.rewardXp > police->task.rewardXp);
    CHECK(supermarket->task.rewardXp < police->task.rewardXp);

    for (const game::PoiDef* poi : {supermarket, motel, police, fire, church,
                                    warehouse}) {
        const float villageDistance =
            std::hypot(poi->markerX - 70.0f, poi->markerY - 24.0f);
        CHECK(villageDistance <= pois.guideRange);
        const std::string guide = game::PoiGuideText(
            *poi, 10.0f, 0.0f, 10.0f, false, poi->enemies.count);
        CHECK(guide.find(poi->name) != std::string::npos);
        CHECK(guide.find("威胁") != std::string::npos);
        CHECK(guide.find("东 10格") != std::string::npos);
        CHECK(guide.find(poi->task.description) != std::string::npos);
        CHECK(game::PoiGuideTone(*poi, false, poi->enemies.count) ==
              std::string("warn"));
        CHECK(game::PoiGuideTone(*poi, true, 0) == std::string("ok"));
    }
    CHECK(supermarket->task.description.find("货架被搬空") != std::string::npos);
    CHECK(motel->task.description.find("登记簿最后一页") != std::string::npos);
    CHECK(police->task.description.find("案卷被人翻过") != std::string::npos);
    CHECK(fire->task.description.find("出警记录停在三天前") != std::string::npos);
    CHECK(church->task.description.find("未点完的蜡烛") != std::string::npos);
    CHECK(warehouse->task.description.find("中继零件已转移") != std::string::npos);
}

TEST_CASE("M23-F6-T5 F6 POI 标记唯一、任务条件与提示可验收") {
    core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    game::PoiConfig pois;
    std::string error;
    REQUIRE(game::LoadPoiConfig(config, &pois, &error));
    const game::PoiDef* hospital = pois.Find("hospital");
    const game::PoiDef* military = pois.Find("military_checkpoint");
    REQUIRE(hospital != nullptr);
    REQUIRE(military != nullptr);

    CHECK(hospital->markerX == 46.5f);
    CHECK(hospital->markerY == 84.5f);
    CHECK(military->markerX == 105.5f);
    CHECK(military->markerY == 21.5f);
    for (const game::PoiDef& poi : pois.defs) {
        CHECK(poi.markerX > 0.0f);
        CHECK(poi.markerY > 0.0f);
        for (const game::PoiDef& other : pois.defs) {
            if (poi.id == other.id) {
                continue;
            }
            const float dx = poi.markerX - other.markerX;
            const float dy = poi.markerY - other.markerY;
            CHECK(dx * dx + dy * dy > 25.0f);
        }
    }

    CHECK_FALSE(game::ShouldCompletePoiTask(*hospital, false, 0));
    CHECK_FALSE(game::ShouldCompletePoiTask(*hospital, true, 1));
    CHECK(game::ShouldCompletePoiTask(*hospital, true, 0));
    CHECK(game::ShouldCompletePoiTask(*military, true, 0));
    CHECK(game::PoiGuideTone(*hospital, false, 7) == std::string("warn"));
    CHECK(game::PoiGuideTone(*hospital, true, 0) == std::string("ok"));
    const std::string hospital_guide =
        game::PoiGuideText(*hospital, 0.0f, -10.0f, 10.0f, false, 7);
    CHECK(hospital_guide.find("医院") != std::string::npos);
    CHECK(hospital_guide.find("威胁中") != std::string::npos);
    const std::string military_guide =
        game::PoiGuideText(*military, 0.0f, -10.0f, 10.0f, false, 9);
    CHECK(military_guide.find("检查站") != std::string::npos);
    CHECK(military_guide.find("威胁高") != std::string::npos);
}

TEST_CASE("X-F2b-2b mod POI 文件只追加新 id") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "zl_mod_poi" / "pois.json";
    std::error_code ec;
    std::filesystem::remove_all(path.parent_path(), ec);
    std::filesystem::create_directories(path.parent_path(), ec);
    {
        std::ofstream output(path, std::ios::binary);
        output << R"({
          "format": "pois-v1",
          "defs": [
            {
              "id": "mod_poi",
              "name": "Mod POI",
              "threat": 2,
              "markerX": 40.5,
              "markerY": 30.5,
              "containers": [
                { "x": 40.5, "y": 30.5, "tableId": "mod_table", "sprite": "object.crate" }
              ],
              "enemies": { "count": 2, "kind": "fast", "radius": 5.0 },
              "task": {
                "description": "Search the cache",
                "targetTable": "mod_table",
                "rewardXp": 30.0,
                "rewardItem": "mod_item",
                "rewardCount": 2
              }
            },
            {
              "id": "base_poi",
              "name": "Duplicate",
              "threat": 1,
              "markerX": 0.0,
              "markerY": 0.0,
              "containers": [
                { "x": 0.0, "y": 0.0, "tableId": "base_table", "sprite": "object.crate" }
              ],
              "task": { "description": "Dup", "targetTable": "base_table" }
            }
          ]
        })";
    }

    game::PoiConfig pois;
    pois.enabled = true;
    pois.guideRange = 55.0f;
    game::PoiDef base;
    base.id = "base_poi";
    base.name = "Base";
    pois.defs.push_back(base);
    game::PoiMergeStats stats;
    std::string error;
    REQUIRE(game::MergePoiFile(path, &pois, &stats, &error));
    CHECK(error.empty());
    CHECK(pois.enabled);
    CHECK(pois.guideRange == doctest::Approx(55.0f));
    CHECK(pois.defs.size() == 2u);
    REQUIRE(pois.Find("mod_poi") != nullptr);
    CHECK(pois.Find("mod_poi")->containers.front().sprite == "object.crate");
    CHECK(stats.filesLoaded == 1);
    CHECK(stats.defsAdded == 1);
    CHECK(stats.defsSkipped == 1);

    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << R"({ "format": "pois-v0", "defs": [] })";
    }
    error.clear();
    CHECK_FALSE(game::MergePoiFile(path, &pois, &stats, &error));
    CHECK_FALSE(error.empty());
    CHECK(pois.defs.size() == 2u);
    std::filesystem::remove_all(path.parent_path(), ec);
}

TEST_CASE("M33-F5-T1 多阶段 POI 数据契约与默认值") {
    core::Config config;
    config.Set("pois", nlohmann::json::parse(R"({
      "enabled": true,
      "guideRange": 80.0,
      "defs": [
        {
          "id": "staged_hospital",
          "name": "阶段医院",
          "threat": 2,
          "markerX": 40.5,
          "markerY": 50.5,
          "containers": [
            { "x": 41.5, "y": 51.5, "tableId": "poi_stage_loot", "sprite": "object.crate" },
            { "x": 42.5, "y": 52.5, "tableId": "poi_stage_key", "sprite": "object.locker" }
          ],
          "enemies": { "count": 4, "kind": "mixed", "radius": 7.0 },
          "stages": [
            {
              "id": "approach",
              "kind": "reach",
              "hint": "接近医院",
              "x": 40.5,
              "y": 50.5,
              "radius": 3.0
            },
            {
              "id": "find_key",
              "kind": "loot",
              "target": "poi_stage_key",
              "x": 42.5,
              "y": 52.5,
              "count": 1,
              "next": [ "open_gate" ],
              "reward": { "xp": 20.0, "item": "bandage", "count": 2 }
            },
            {
              "id": "open_gate",
              "kind": "interact",
              "target": "front_door",
              "x": 41.5,
              "y": 51.5,
              "requires": [ "front_key" ],
              "next": [ "choose_route" ]
            },
            {
              "id": "choose_route",
              "kind": "choice",
              "hint": "选择路线",
              "next": [ "ward", "morgue" ]
            },
            {
              "id": "ward",
              "kind": "kill",
              "target": "elite_ward",
              "count": 3,
              "next": [ "boss" ]
            },
            {
              "id": "morgue",
              "kind": "power",
              "target": "generator",
              "next": [ "boss" ]
            },
            {
              "id": "boss",
              "kind": "kill",
              "target": "boss_doctor",
              "count": 1
            }
          ],
          "entries": [
            {
              "id": "main_entrance",
              "label": "正门",
              "x": 40.5,
              "y": 50.5,
              "stage": "approach"
            },
            {
              "id": "back_entrance",
              "label": "后门",
              "x": 43.5,
              "y": 53.5,
              "stage": "morgue",
              "requires": "back_key"
            }
          ],
          "keys": [
            {
              "id": "front_key",
              "item": "keycard_front",
              "name": "前门钥匙卡",
              "source": "find_key"
            },
            {
              "id": "back_key",
              "item": "keycard_back",
              "name": "后门钥匙卡"
            }
          ],
          "branches": [
            {
              "id": "ward_route",
              "name": "病房路线",
              "from": "choose_route",
              "stages": [ "ward", "boss" ]
            },
            {
              "id": "morgue_route",
              "name": "太平间路线",
              "from": "choose_route",
              "stages": [ "morgue", "boss" ]
            }
          ],
          "boss": {
            "name": "院长",
            "variant": "armored",
            "stage": "boss",
            "mechanic": "summon",
            "count": 1
          },
          "narrative": [
            {
              "id": "note_intro",
              "stage": "approach",
              "text": "医院封锁记录",
              "item": "hospital_note"
            },
            {
              "id": "note_boss",
              "stage": "boss",
              "text": "院长日志"
            }
          ]
        }
      ]
    })"));

    game::PoiConfig pois;
    std::string error;
    REQUIRE(game::LoadPoiConfig(config, &pois, &error));
    CHECK(error.empty());
    REQUIRE(pois.defs.size() == 1u);
    const game::PoiDef* poi = pois.Find("staged_hospital");
    REQUIRE(poi != nullptr);
    CHECK(poi->task.targetTable.empty());
    REQUIRE(poi->stages.size() == 7u);

    CHECK(poi->stages[0].id == "approach");
    CHECK(poi->stages[0].kind == game::PoiStageKind::Reach);
    CHECK(poi->stages[0].hint == "接近医院");
    CHECK(poi->stages[0].x == doctest::Approx(40.5f));
    CHECK(poi->stages[0].y == doctest::Approx(50.5f));
    CHECK(poi->stages[0].radius == doctest::Approx(3.0f));
    CHECK(poi->stages[0].count == 1);
    CHECK(poi->stages[1].kind == game::PoiStageKind::Loot);
    CHECK(poi->stages[1].target == "poi_stage_key");
    CHECK(poi->stages[1].next == std::vector<std::string>{"open_gate"});
    CHECK(poi->stages[1].reward.xp == doctest::Approx(20.0f));
    CHECK(poi->stages[1].reward.item == "bandage");
    CHECK(poi->stages[1].reward.count == 2);
    CHECK(poi->stages[2].kind == game::PoiStageKind::Interact);
    CHECK(poi->stages[2].requiredKeys ==
          std::vector<std::string>{"front_key"});
    CHECK(poi->stages[3].kind == game::PoiStageKind::Choice);
    CHECK(poi->stages[3].next ==
          std::vector<std::string>({"ward", "morgue"}));
    CHECK(poi->stages[4].kind == game::PoiStageKind::Kill);
    CHECK(poi->stages[4].count == 3);
    CHECK(poi->stages[5].kind == game::PoiStageKind::Power);
    CHECK(poi->stages[6].id == "boss");
    CHECK(std::string(game::PoiStageKindName(game::PoiStageKind::Power)) ==
          "power");

    REQUIRE(poi->entries.size() == 2u);
    CHECK(poi->entries[0].label == "正门");
    CHECK(poi->entries[0].stage == "approach");
    CHECK(poi->entries[1].requiredKey == "back_key");
    REQUIRE(poi->keys.size() == 2u);
    CHECK(poi->keys[0].source == "find_key");
    CHECK(poi->keys[0].item == "keycard_front");
    CHECK(poi->keys[1].consumed);
    REQUIRE(poi->branches.size() == 2u);
    CHECK(poi->branches[0].from == "choose_route");
    CHECK(poi->branches[1].stages.size() == 2u);
    CHECK(poi->boss.enabled);
    CHECK(poi->boss.name == "院长");
    CHECK(poi->boss.variant == "armored");
    CHECK(poi->boss.stage == "boss");
    CHECK(poi->boss.mechanic == "summon");
    CHECK(poi->boss.count == 1);
    REQUIRE(poi->narrative.size() == 2u);
    CHECK(poi->narrative[0].id == "note_intro");
    CHECK(poi->narrative[0].item == "hospital_note");
    CHECK(poi->narrative[1].stage == "boss");
}

TEST_CASE("M33-F5-T1 非法阶段/钥匙/分支引用跳过且保留合法阶段") {
    core::Config config;
    config.Set("pois", nlohmann::json::parse(R"({
      "enabled": true,
      "defs": [
        {
          "id": "bad_staged",
          "name": "坏阶段",
          "threat": 1,
          "markerX": 10.5,
          "markerY": 10.5,
          "containers": [
            { "x": 10.5, "y": 10.5, "tableId": "stage_table", "sprite": "object.crate" }
          ],
          "enemies": { "count": 2 },
          "stages": [
            { "id": "reach_start", "kind": "reach", "x": 10.5, "y": 10.5, "radius": 2.0 },
            { "id": "reach_start", "kind": "kill", "target": "duplicate" },
            { "id": "bad_next", "kind": "kill", "target": "x", "next": [ "missing" ] },
            { "id": "bad_choice", "kind": "choice", "next": [ "reach_start" ] },
            { "id": "bad_kind", "kind": "teleport" },
            { "id": "bad_no_target", "kind": "loot", "next": [ "final" ] },
            {
              "id": "key_gate",
              "kind": "interact",
              "target": "gate",
              "requires": [ "missing_key" ],
              "next": [ "final" ]
            },
            {
              "id": "late_gate",
              "kind": "interact",
              "target": "gate",
              "requires": [ "late_key" ],
              "next": [ "final" ]
            },
            { "id": "final", "kind": "kill", "target": "boss" }
          ],
          "keys": [
            { "id": "late_key", "item": "late_item", "source": "final" },
            { "id": "bad_source", "item": "bad_item", "source": "missing" }
          ],
          "entries": [
            { "id": "bad_entry", "x": 1.0, "y": 1.0, "stage": "missing" },
            { "id": "good_entry", "x": 10.5, "y": 10.5, "stage": "reach_start" }
          ],
          "branches": [
            { "id": "bad_branch", "from": "missing", "stages": [ "final" ] },
            { "id": "good_branch", "from": "reach_start", "stages": [ "final" ] }
          ],
          "boss": {
            "name": "缺失 Boss",
            "variant": "strong",
            "stage": "missing",
            "mechanic": "summon"
          },
          "narrative": [
            { "id": "bad_note", "stage": "missing", "text": "bad" },
            { "id": "good_note", "stage": "reach_start", "text": "ok" }
          ]
        }
      ]
    })"));

    game::PoiConfig pois;
    std::string error;
    REQUIRE(game::LoadPoiConfig(config, &pois, &error));
    CHECK(error.empty());
    REQUIRE(pois.defs.size() == 1u);
    const game::PoiDef* poi = pois.Find("bad_staged");
    REQUIRE(poi != nullptr);
    CHECK(poi->task.targetTable.empty());
    REQUIRE(poi->stages.size() == 2u);
    CHECK(poi->stages[0].id == "reach_start");
    CHECK(poi->stages[1].id == "final");
    REQUIRE(poi->keys.size() == 1u);
    CHECK(poi->keys[0].id == "late_key");
    REQUIRE(poi->entries.size() == 1u);
    CHECK(poi->entries[0].id == "good_entry");
    REQUIRE(poi->branches.size() == 1u);
    CHECK(poi->branches[0].id == "good_branch");
    CHECK_FALSE(poi->boss.enabled);
    CHECK(poi->boss.name.empty());
    REQUIRE(poi->narrative.size() == 1u);
    CHECK(poi->narrative[0].id == "good_note");
}

TEST_CASE("M33-F5-T1 旧 pois-v1 无 stages 行为不变且 mod 可追加阶段") {
    core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    game::PoiConfig pois;
    std::string error;
    REQUIRE(game::LoadPoiConfig(config, &pois, &error));
    CHECK(error.empty());
    REQUIRE(pois.defs.size() == 11u);
    for (const game::PoiDef& def : pois.defs) {
        CHECK(def.stages.empty());
        CHECK(def.entries.empty());
        CHECK(def.keys.empty());
        CHECK(def.branches.empty());
        CHECK_FALSE(def.boss.enabled);
        CHECK(def.narrative.empty());
        CHECK_FALSE(def.task.targetTable.empty());
    }
    const game::PoiDef* gas = pois.Find("gas_station");
    REQUIRE(gas != nullptr);
    CHECK(gas->task.targetTable == "poi_gas_station");

    game::PoiMergeStats stats;
    REQUIRE(game::MergePoiJson(
        R"({
          "format": "pois-v1",
          "defs": [
            {
              "id": "mod_staged_poi",
              "name": "Mod Staged",
              "threat": 2,
              "markerX": 70.5,
              "markerY": 70.5,
              "containers": [
                { "x": 70.5, "y": 70.5, "tableId": "mod_stage_table", "sprite": "object.crate" }
              ],
              "enemies": { "count": 2 },
              "stages": [
                { "id": "start", "kind": "reach", "x": 70.5, "y": 70.5, "radius": 2.0 },
                { "id": "end", "kind": "kill", "target": "elite" }
              ],
              "boss": {
                "name": "Mod Boss",
                "variant": "strong",
                "mechanic": "charge"
              }
            }
          ]
        })",
        "mod-staged-poi", &pois, &stats, &error));
    CHECK(error.empty());
    CHECK(stats.filesLoaded == 1);
    CHECK(stats.defsAdded == 1);
    CHECK(stats.defsSkipped == 0);
    CHECK(pois.defs.size() == 12u);
    CHECK(pois.Find("gas_station")->stages.empty());
    const game::PoiDef* mod = pois.Find("mod_staged_poi");
    REQUIRE(mod != nullptr);
    REQUIRE(mod->stages.size() == 2u);
    CHECK(mod->stages[0].id == "start");
    CHECK(mod->stages[1].id == "end");
    CHECK(mod->boss.enabled);
    CHECK(mod->boss.mechanic == "charge");
}

TEST_CASE("M33-F5-T2 阶段推进：到达/搜刮/钥匙/分支/奖励幂等") {
    game::PoiDef def;
    def.id = "staged_hospital";
    def.name = "多阶段医院";
    def.markerX = 10.0f;
    def.markerY = 10.0f;

    game::PoiStageDef approach;
    approach.id = "approach";
    approach.kind = game::PoiStageKind::Reach;
    approach.x = 10.0f;
    approach.y = 10.0f;
    approach.radius = 2.0f;
    approach.next = {"find_key"};

    game::PoiStageDef findKey;
    findKey.id = "find_key";
    findKey.kind = game::PoiStageKind::Loot;
    findKey.target = "key_item";
    findKey.count = 1;
    findKey.next = {"open_gate"};
    findKey.reward.xp = 20.0f;
    findKey.reward.item = "bandage";
    findKey.reward.count = 2;

    game::PoiStageDef openGate;
    openGate.id = "open_gate";
    openGate.kind = game::PoiStageKind::Interact;
    openGate.target = "door";
    openGate.requiredKeys = {"front"};
    openGate.next = {"choose_route"};

    game::PoiStageDef choose;
    choose.id = "choose_route";
    choose.kind = game::PoiStageKind::Choice;
    choose.next = {"ward", "morgue"};

    game::PoiStageDef ward;
    ward.id = "ward";
    ward.kind = game::PoiStageKind::Reach;
    ward.x = 20.0f;
    ward.y = 20.0f;

    game::PoiStageDef morgue;
    morgue.id = "morgue";
    morgue.kind = game::PoiStageKind::Kill;
    morgue.count = 2;

    def.stages = {approach, findKey, openGate, choose, ward, morgue};
    def.keys.push_back(game::PoiKeyDef{
        "front", "keycard_front", "前门钥匙", "find_key", true});
    game::PoiBranchDef wardRoute;
    wardRoute.id = "ward_route";
    wardRoute.from = "choose_route";
    wardRoute.stages = {"ward"};
    game::PoiBranchDef morgueRoute;
    morgueRoute.id = "morgue_route";
    morgueRoute.from = "choose_route";
    morgueRoute.stages = {"morgue"};
    def.branches = {wardRoute, morgueRoute};

    game::PoiProgress progress;
    CHECK(game::StartPoiProgress(def, &progress));
    CHECK(progress.currentStage == 0);
    CHECK(progress.attempts == 1);
    CHECK(game::CurrentPoiStage(def, progress)->id == "approach");

    game::PoiStageUpdate update = game::TickPoiStage(def, &progress, 50.0f, 50.0f);
    CHECK_FALSE(update.stageCompleted);
    CHECK(progress.currentStage == 0);

    update = game::TickPoiStage(def, &progress, 10.5f, 10.5f);
    REQUIRE(update.stageCompleted);
    CHECK(update.stageId == "approach");
    CHECK(update.nextStageId == "find_key");
    CHECK(progress.currentStage == 1);

    update = game::SignalPoiStage(def, &progress, game::PoiStageSignal::Loot, 1);
    REQUIRE(update.stageCompleted);
    CHECK(update.stageId == "find_key");
    CHECK(update.rewardAvailable);
    CHECK(update.reward.xp == doctest::Approx(20.0f));
    CHECK(progress.completedStages ==
          std::vector<std::string>({"approach", "find_key"}));
    CHECK(progress.acquiredKeys == std::vector<std::string>({"front"}));
    CHECK(progress.currentStage == 2);

    update = game::SignalPoiStage(def, &progress, game::PoiStageSignal::Interact);
    REQUIRE(update.stageCompleted);
    CHECK(update.stageId == "open_gate");
    CHECK(progress.currentStage == 3);

    update = game::ChoosePoiBranch(def, &progress, "missing_route");
    CHECK(update.awaitingChoice);
    CHECK(progress.awaitingChoice);
    CHECK(progress.currentStage == 3);

    update = game::ChoosePoiBranch(def, &progress, "morgue_route");
    REQUIRE(update.advanced);
    CHECK_FALSE(update.awaitingChoice);
    CHECK(progress.chosenBranch == "morgue_route");
    CHECK(progress.currentStage == 5);
    CHECK(update.nextStageId == "morgue");

    update = game::SignalPoiStage(def, &progress, game::PoiStageSignal::Kill, 1);
    CHECK_FALSE(update.stageCompleted);
    CHECK(progress.currentStage == 5);

    update = game::SignalPoiStage(def, &progress, game::PoiStageSignal::Kill, 1);
    REQUIRE(update.poiCompleted);
    CHECK(progress.completed);
    CHECK(progress.currentStage == -1);
    CHECK_FALSE(game::StartPoiProgress(def, &progress));

    std::vector<std::string> claims;
    CHECK(game::ClaimPoiStageReward(&claims, "staged_hospital", "find_key"));
    CHECK_FALSE(game::ClaimPoiStageReward(&claims, "staged_hospital", "find_key"));
    CHECK(claims.size() == 1u);
}

TEST_CASE("M33-F5-T2 失败重试保留已完成阶段") {
    game::PoiDef def;
    def.id = "retry_poi";
    def.name = "重试 POI";
    game::PoiStageDef first;
    first.id = "first";
    first.kind = game::PoiStageKind::Reach;
    first.x = 1.0f;
    first.y = 1.0f;
    first.radius = 1.0f;
    first.next = {"second"};
    game::PoiStageDef second;
    second.id = "second";
    second.kind = game::PoiStageKind::Kill;
    second.count = 2;
    def.stages = {first, second};

    game::PoiProgress progress;
    REQUIRE(game::StartPoiProgress(def, &progress));
    REQUIRE(game::TickPoiStage(def, &progress, 1.0f, 1.0f).stageCompleted);
    CHECK(progress.currentStage == 1);
    CHECK(game::FailPoiProgress(&progress));
    CHECK(progress.failed);
    CHECK(progress.failedStage == 1);
    CHECK_FALSE(game::TickPoiStage(def, &progress, 1.0f, 1.0f).stageCompleted);

    CHECK(game::RetryPoiProgress(def, &progress));
    CHECK_FALSE(progress.failed);
    CHECK(progress.attempts == 2);
    CHECK(progress.currentStage == 1);
    CHECK(progress.completedStages == std::vector<std::string>({"first"}));

    CHECK(game::SignalPoiStage(def, &progress, game::PoiStageSignal::Kill, 2)
              .poiCompleted);
    CHECK(progress.completed);
    CHECK_FALSE(game::FailPoiProgress(&progress));
}

TEST_CASE("M33-F5-T2 旧单段 POI 没有阶段时不启动阶段状态") {
    game::PoiDef def;
    def.id = "legacy";
    def.name = "旧 POI";
    def.task.targetTable = "legacy_table";
    def.task.rewardXp = 10.0f;
    game::PoiProgress progress;
    CHECK_FALSE(game::StartPoiProgress(def, &progress));
    CHECK_FALSE(game::TickPoiStage(def, &progress, 0.0f, 0.0f).stageCompleted);
    CHECK(progress.currentStage == -1);
}

TEST_CASE("M33-F5-T2 供电阶段与缺少钥匙阻塞") {
    game::PoiDef def;
    def.id = "power_poi";
    def.name = "供电 POI";
    game::PoiStageDef power;
    power.id = "power_on";
    power.kind = game::PoiStageKind::Power;
    power.target = "generator";
    power.requiredKeys = {"power_key"};
    def.stages = {power};
    def.keys.push_back(game::PoiKeyDef{
        "power_key", "keycard_power", "供电钥匙", "", false});

    game::PoiProgress progress;
    REQUIRE(game::StartPoiProgress(def, &progress));
    game::PoiStageUpdate update =
        game::SignalPoiStage(def, &progress, game::PoiStageSignal::Power);
    CHECK(update.blocked);
    CHECK_FALSE(progress.powered);
    CHECK(progress.currentStage == 0);

    progress.acquiredKeys.push_back("power_key");
    update = game::SignalPoiStage(def, &progress, game::PoiStageSignal::Power);
    REQUIRE(update.poiCompleted);
    CHECK(progress.powered);
    CHECK(progress.completed);
}

TEST_CASE("M33-F5-T3 钥匙物品、入口与可选路线数据") {
    game::PoiDef def;
    def.id = "staged_access";
    def.name = "门禁路线 POI";
    def.markerX = 30.0f;
    def.markerY = 40.0f;

    game::PoiStageDef find;
    find.id = "find";
    find.kind = game::PoiStageKind::Loot;
    find.target = "key_item";
    find.next = {"gate"};

    game::PoiStageDef gate;
    gate.id = "gate";
    gate.kind = game::PoiStageKind::Interact;
    gate.target = "front_gate";
    gate.requiredKeys = {"front"};
    gate.next = {"power"};

    game::PoiStageDef power;
    power.id = "power";
    power.kind = game::PoiStageKind::Power;
    power.target = "generator";
    power.next = {"route"};

    game::PoiStageDef route;
    route.id = "route";
    route.kind = game::PoiStageKind::Choice;
    route.next = {"vault", "backdoor"};

    game::PoiStageDef vault;
    vault.id = "vault";
    vault.kind = game::PoiStageKind::Reach;
    vault.x = 32.0f;
    vault.y = 40.0f;

    game::PoiStageDef backdoor;
    backdoor.id = "backdoor";
    backdoor.kind = game::PoiStageKind::Reach;
    backdoor.x = 28.0f;
    backdoor.y = 40.0f;

    def.stages = {find, gate, power, route, vault, backdoor};
    def.keys.push_back(game::PoiKeyDef{
        "front", "keycard_front", "前门钥匙", "find", true});
    def.entries.push_back(game::PoiEntryDef{
        "back", "后门", 28.0f, 40.0f, "backdoor", "front"});
    def.branches.push_back(game::PoiBranchDef{
        "vault_route", "金库路线", "route", {"vault"}});
    def.branches.push_back(game::PoiBranchDef{
        "back_route", "后门路线", "route", {"backdoor"}});

    const game::PoiKeyDef* key = game::FindPoiKey(def, "front");
    REQUIRE(key != nullptr);
    CHECK(game::PoiKeyDisplayName(*key) == "前门钥匙");
    const auto awards = game::PoiStageKeyAwards(def, "find");
    REQUIRE(awards.size() == 1u);
    CHECK(awards.front()->item == "keycard_front");
    CHECK(game::PoiStageKeyAwards(def, "gate").empty());

    const game::PoiEntryDef* entry = game::FindPoiEntry(def, "back");
    REQUIRE(entry != nullptr);
    CHECK(entry->stage == "backdoor");
    CHECK(entry->requiredKey == "front");

    game::PoiProgress progress;
    REQUIRE(game::StartPoiProgressAtStage(def, &progress, "route"));
    CHECK(progress.currentStage == 3);
    CHECK(game::CurrentPoiStage(def, progress)->id == "route");
    const auto branches = game::PoiAvailableBranches(def, progress);
    REQUIRE(branches.size() == 2u);
    CHECK(branches[0]->id == "vault_route");
    CHECK(branches[1]->id == "back_route");
    CHECK(game::PoiChoiceText(def, progress) ==
          "1 金库路线 / 2 后门路线");

    CHECK(game::PoiStageRequiredKeysText(def, progress, gate) ==
          "前门钥匙");
    CHECK(game::MarkPoiKeyAcquired(&progress.acquiredKeys, "front"));
    CHECK_FALSE(game::MarkPoiKeyAcquired(&progress.acquiredKeys, "front"));
    CHECK(game::PoiStageRequiredKeysText(def, progress, gate).empty());
    CHECK(game::PoiStageObjectiveText(def, progress, power) ==
          "启动发电机");
    CHECK(game::PoiStageObjectiveText(def, progress, gate) ==
          "操作 front_gate");
}

TEST_CASE("M33-F5-T4 Boss 生成计划、机制与失败重试") {
    game::PoiDef def;
    def.id = "staged_boss";
    def.name = "Boss 测试 POI";
    def.markerX = 10.0f;
    def.markerY = 10.0f;

    game::PoiStageDef approach;
    approach.id = "approach";
    approach.kind = game::PoiStageKind::Reach;
    approach.x = 10.0f;
    approach.y = 10.0f;
    approach.next = {"boss"};

    game::PoiStageDef boss;
    boss.id = "boss";
    boss.kind = game::PoiStageKind::Kill;
    boss.x = 10.0f;
    boss.y = 10.0f;
    boss.count = 3;

    game::PoiStageDef elite;
    elite.id = "elite";
    elite.kind = game::PoiStageKind::Kill;
    elite.target = "fast";
    elite.count = 2;

    def.stages = {approach, boss, elite};
    def.boss.name = "院长";
    def.boss.variant = "armored";
    def.boss.stage = "boss";
    def.boss.mechanic = "summon";
    def.boss.count = 1;
    def.boss.enabled = true;

    CHECK(game::PoiBossMechanicFor("summon") ==
          game::PoiBossMechanic::Summon);
    CHECK(game::PoiBossMechanicFor("charge") ==
          game::PoiBossMechanic::Charge);
    CHECK(game::PoiBossMechanicFor("fortify") ==
          game::PoiBossMechanic::Fortify);
    CHECK(std::string(game::PoiBossMechanicName(
              game::PoiBossMechanic::Fortify)) == "fortify");
    CHECK(std::string(game::PoiBossMechanicText(
              game::PoiBossMechanic::Summon)) == "召唤增援");
    REQUIRE(game::PoiBossStage(def) != nullptr);
    CHECK(game::PoiBossStage(def)->id == "boss");
    CHECK(game::PoiStageHasBoss(def, boss));
    CHECK_FALSE(game::PoiStageHasBoss(def, elite));

    const auto bossPlan = game::BuildPoiStageEnemyPlan(def, boss);
    REQUIRE(bossPlan.size() == 3u);
    CHECK(bossPlan[0].role == game::PoiStageEnemyRole::Boss);
    CHECK(bossPlan[0].kind == game::PoiGuardKind::Armored);
    CHECK(bossPlan[1].role == game::PoiStageEnemyRole::Elite);
    CHECK(bossPlan[2].role == game::PoiStageEnemyRole::Elite);

    const auto elitePlan = game::BuildPoiStageEnemyPlan(def, elite);
    REQUIRE(elitePlan.size() == 2u);
    CHECK(elitePlan[0].role == game::PoiStageEnemyRole::Elite);
    CHECK(elitePlan[0].kind == game::PoiGuardKind::Fast);
    CHECK(elitePlan[1].kind == game::PoiGuardKind::Fast);

    game::PoiProgress progress;
    REQUIRE(game::StartPoiProgressAtStage(def, &progress, "boss"));
    CHECK(progress.currentStage == 1);
    progress.stageKills = 1;
    const std::string objective =
        game::PoiStageObjectiveText(def, progress, boss);
    CHECK(objective.find("院长") != std::string::npos);
    CHECK(objective.find("召唤增援") != std::string::npos);
    CHECK(objective.find("1/3") != std::string::npos);

    progress.stageKills = 2;
    CHECK(game::FailPoiProgress(&progress));
    CHECK(progress.failed);
    CHECK(progress.failedStage == 1);
    CHECK(game::RetryPoiProgress(def, &progress));
    CHECK_FALSE(progress.failed);
    CHECK(progress.attempts == 2);
    CHECK(progress.currentStage == 1);
    CHECK(progress.stageKills == 0);

    std::vector<std::string> claims;
    CHECK(game::ClaimPoiStageReward(&claims, def.id, "boss"));
    CHECK_FALSE(game::ClaimPoiStageReward(&claims, def.id, "boss"));
    CHECK(claims.size() == 1u);
}

TEST_CASE("M33-F5-T6 M31 staged POI content and stage references") {
    core::Config config;
    const std::string path = std::string(ZL_SOURCE_DIR) +
                             "/assets/maps/village_demo_m31_content.json";
    REQUIRE(config.LoadFile(path));
    game::PoiConfig pois;
    std::string error;
    REQUIRE(game::LoadPoiConfig(config, &pois, &error));
    CHECK(error.empty());
    REQUIRE(pois.defs.size() == 4u);

    const game::PoiDef* clinic = pois.Find("clinic");
    const game::PoiDef* farmstead = pois.Find("farmstead");
    const game::PoiDef* riverHut = pois.Find("river_hut");
    const game::PoiDef* checkpoint = pois.Find("checkpoint");
    REQUIRE(clinic != nullptr);
    REQUIRE(farmstead != nullptr);
    REQUIRE(riverHut != nullptr);
    REQUIRE(checkpoint != nullptr);

    REQUIRE(clinic->stages.size() == 3u);
    CHECK(clinic->stages[0].id == "triage_choice");
    CHECK(clinic->stages[0].kind == game::PoiStageKind::Choice);
    CHECK(clinic->stages[0].next ==
          std::vector<std::string>{"ward", "pharmacy"});
    CHECK(clinic->branches.size() == 2u);
    CHECK(clinic->boss.enabled);
    CHECK(clinic->boss.stage == "ward");
    CHECK(clinic->boss.mechanic == "summon");
    CHECK(clinic->narrative.size() == 3u);
    const game::PoiStageDef* ward = game::FindPoiStage(*clinic, "ward");
    const game::PoiStageDef* pharmacy =
        game::FindPoiStage(*clinic, "pharmacy");
    REQUIRE(ward != nullptr);
    REQUIRE(pharmacy != nullptr);
    CHECK(ward->kind == game::PoiStageKind::Kill);
    CHECK(ward->count == 2);
    CHECK(ward->reward.item == "first_aid_kit");
    CHECK(pharmacy->kind == game::PoiStageKind::Loot);
    CHECK(pharmacy->target == "hospital_patient_log");

    game::PoiProgress progress;
    REQUIRE(game::StartPoiProgress(*clinic, &progress));
    const auto branches = game::PoiAvailableBranches(*clinic, progress);
    REQUIRE(branches.size() == 2u);
    game::PoiStageUpdate update = game::ChoosePoiBranch(
        *clinic, &progress, "pharmacy_route");
    CHECK(update.stageCompleted);
    CHECK(update.advanced);
    CHECK(progress.currentStage == 2);
    CHECK(progress.chosenBranch == "pharmacy_route");
    update = game::SignalPoiStage(*clinic, &progress,
                                  game::PoiStageSignal::Loot, 1);
    CHECK(update.stageCompleted);
    CHECK(update.poiCompleted);
    CHECK(progress.completed);

    REQUIRE(farmstead->stages.size() == 3u);
    CHECK(farmstead->boss.enabled);
    CHECK(farmstead->boss.stage == "farm_boss");
    CHECK(farmstead->boss.mechanic == "summon");
    const game::PoiStageDef* farmBoss =
        game::FindPoiStage(*farmstead, "farm_boss");
    REQUIRE(farmBoss != nullptr);
    CHECK(farmBoss->kind == game::PoiStageKind::Kill);
    game::PoiProgress farmProgress;
    REQUIRE(game::StartPoiProgress(*farmstead, &farmProgress));
    update = game::TickPoiStage(*farmstead, &farmProgress, 110.5f, 213.5f);
    CHECK(update.stageCompleted);
    CHECK(update.advanced);
    CHECK(farmProgress.currentStage == 1);
    update = game::SignalPoiStage(*farmstead, &farmProgress,
                                  game::PoiStageSignal::Loot, 1);
    CHECK(update.stageCompleted);
    CHECK(update.advanced);
    CHECK(farmProgress.currentStage == 2);
    const auto farmPlan = game::BuildPoiStageEnemyPlan(*farmstead, *farmBoss);
    REQUIRE(farmPlan.size() == 3u);
    CHECK(farmPlan[0].role == game::PoiStageEnemyRole::Boss);
    CHECK(farmPlan[1].role == game::PoiStageEnemyRole::Elite);
    CHECK(farmPlan[2].role == game::PoiStageEnemyRole::Elite);

    REQUIRE(checkpoint->stages.size() == 3u);
    REQUIRE(checkpoint->keys.size() == 1u);
    CHECK(checkpoint->keys[0].id == "checkpoint_key");
    CHECK(checkpoint->keys[0].item == "checkpoint_keycard");
    CHECK(checkpoint->keys[0].source == "patrol_cache");
    CHECK(checkpoint->keys[0].consumed);
    CHECK(checkpoint->boss.enabled);
    CHECK(checkpoint->boss.stage == "commander");
    CHECK(checkpoint->boss.mechanic == "fortify");
    const game::PoiStageDef* gate =
        game::FindPoiStage(*checkpoint, "armory_gate");
    const game::PoiStageDef* commander =
        game::FindPoiStage(*checkpoint, "commander");
    REQUIRE(gate != nullptr);
    REQUIRE(commander != nullptr);
    CHECK(gate->kind == game::PoiStageKind::Interact);
    CHECK(gate->requiredKeys == std::vector<std::string>{"checkpoint_key"});
    game::PoiProgress checkpointProgress;
    REQUIRE(game::StartPoiProgress(*checkpoint, &checkpointProgress));
    update = game::SignalPoiStage(*checkpoint, &checkpointProgress,
                                  game::PoiStageSignal::Loot, 1);
    CHECK(update.stageCompleted);
    CHECK(update.advanced);
    CHECK(checkpointProgress.acquiredKeys ==
          std::vector<std::string>{"checkpoint_key"});
    CHECK(game::PoiStageKeyAwards(*checkpoint, "patrol_cache").size() == 1u);
    update = game::SignalPoiStage(*checkpoint, &checkpointProgress,
                                  game::PoiStageSignal::Interact, 1);
    CHECK(update.stageCompleted);
    CHECK(update.advanced);
    CHECK(checkpointProgress.currentStage == 2);
    const auto bossPlan =
        game::BuildPoiStageEnemyPlan(*checkpoint, *commander);
    REQUIRE(bossPlan.size() == 3u);
    CHECK(bossPlan[0].role == game::PoiStageEnemyRole::Boss);
    CHECK(game::PoiBossMechanicFor(checkpoint->boss.mechanic) ==
          game::PoiBossMechanic::Fortify);

    CHECK(riverHut->stages.empty());
    CHECK_FALSE(riverHut->boss.enabled);
    CHECK(riverHut->task.targetTable == "poi_farm_tools");
}

TEST_CASE("M33-F5-T5 阶段 HUD、叙事、完成/失败与地图状态") {
    game::PoiDef def;
    def.id = "staged_hud";
    def.name = "阶段 HUD 医院";
    def.markerX = 40.0f;
    def.markerY = 50.0f;

    game::PoiStageDef approach;
    approach.id = "approach";
    approach.kind = game::PoiStageKind::Reach;
    approach.x = 40.0f;
    approach.y = 50.0f;
    approach.radius = 2.0f;
    approach.next = {"gate"};

    game::PoiStageDef gate;
    gate.id = "gate";
    gate.kind = game::PoiStageKind::Interact;
    gate.target = "front_gate";
    gate.x = 42.0f;
    gate.y = 50.0f;
    gate.requiredKeys = {"front"};
    gate.next = {"power"};

    game::PoiStageDef power;
    power.id = "power";
    power.kind = game::PoiStageKind::Power;
    power.target = "generator";
    power.x = 43.0f;
    power.y = 50.0f;
    power.next = {"route"};

    game::PoiStageDef route;
    route.id = "route";
    route.kind = game::PoiStageKind::Choice;
    route.x = 44.0f;
    route.y = 50.0f;
    route.next = {"boss", "backdoor"};

    game::PoiStageDef boss;
    boss.id = "boss";
    boss.kind = game::PoiStageKind::Kill;
    boss.x = 45.0f;
    boss.y = 50.0f;
    boss.count = 2;

    game::PoiStageDef backdoor;
    backdoor.id = "backdoor";
    backdoor.kind = game::PoiStageKind::Reach;
    backdoor.x = 38.0f;
    backdoor.y = 50.0f;

    def.stages = {approach, gate, power, route, boss, backdoor};
    def.keys.push_back(game::PoiKeyDef{
        "front", "keycard_front", "前门钥匙", "", false});
    def.branches.push_back(game::PoiBranchDef{
        "boss_route", "正门路线", "route", {"boss"}});
    def.branches.push_back(game::PoiBranchDef{
        "back_route", "后门路线", "route", {"backdoor"}});
    def.boss.name = "院长";
    def.boss.variant = "armored";
    def.boss.stage = "boss";
    def.boss.mechanic = "charge";
    def.boss.count = 1;
    def.boss.enabled = true;
    def.narrative.push_back(game::PoiNarrativeDef{
        "note_intro", "approach", "医院封锁记录", "hospital_note"});
    def.narrative.push_back(game::PoiNarrativeDef{
        "note_boss", "boss", "院长日志", ""});

    game::PoiProgress progress;
    REQUIRE(game::StartPoiProgress(def, &progress));
    CHECK(game::PoiStageProgressText(def, progress) == "阶段 1/6");
    CHECK(game::PoiStageHudObjective(def, progress) ==
          "前往目标地点");
    CHECK(game::PoiStageHudRequirements(def, progress) ==
          "无额外条件");
    CHECK(game::PoiStageNarrativeText(def, "approach") ==
          "医院封锁记录（文件：hospital_note）");
    CHECK(game::PoiStageMapStatusText(def, progress) == "阶段 1/6");

    float mapX = 0.0f;
    float mapY = 0.0f;
    REQUIRE(game::PoiStageMapPoint(def, progress, &mapX, &mapY));
    CHECK(mapX == doctest::Approx(40.0f));
    CHECK(mapY == doctest::Approx(50.0f));

    progress.currentStage = 1;
    CHECK(game::PoiStageHudRequirements(def, progress).find("前门钥匙") !=
          std::string::npos);
    progress.acquiredKeys.push_back("front");
    CHECK(game::PoiStageHudRequirements(def, progress).find("前门钥匙") ==
          std::string::npos);

    progress.currentStage = 2;
    CHECK(game::PoiStageHudRequirements(def, progress).find("供电") !=
          std::string::npos);
    progress.powered = true;
    CHECK(game::PoiStageHudRequirements(def, progress).find("已恢复") !=
          std::string::npos);

    progress.currentStage = 3;
    progress.awaitingChoice = true;
    CHECK(game::PoiStageProgressText(def, progress).find("待选路线") !=
          std::string::npos);
    CHECK(game::PoiStageHudRequirements(def, progress).find("路线：") !=
          std::string::npos);
    CHECK(game::PoiStageHudRequirements(def, progress).find("后门路线") !=
          std::string::npos);

    progress.currentStage = 4;
    progress.awaitingChoice = false;
    progress.stageKills = 1;
    CHECK(game::PoiStageHudObjective(def, progress).find("院长") !=
          std::string::npos);
    CHECK(game::PoiStageHudObjective(def, progress).find("1/2") !=
          std::string::npos);
    CHECK(game::PoiStageHudRequirements(def, progress).find("冲锋") !=
          std::string::npos);
    CHECK(game::PoiStageNarrativeText(def, "boss") == "院长日志");

    REQUIRE(game::FailPoiProgress(&progress));
    CHECK(game::PoiStageProgressText(def, progress).find("失败") !=
          std::string::npos);
    CHECK(game::PoiStageHudObjective(def, progress).find("重试") !=
          std::string::npos);
    const std::string failed =
        game::PoiStageResultText(def, progress, false);
    CHECK(failed.find("阶段 5/6") != std::string::npos);
    CHECK(failed.find("自动重试") != std::string::npos);
    CHECK(game::PoiStageResultTitle(def, false).find("失败") !=
          std::string::npos);

    progress.completed = true;
    progress.failed = false;
    progress.currentStage = -1;
    CHECK(game::PoiStageProgressText(def, progress) == "阶段 6/6 · 完成");
    CHECK(game::PoiStageHudObjective(def, progress).find("全部阶段完成") !=
          std::string::npos);
    CHECK(game::PoiStageMapStatusText(def, progress) == "已完成");
    CHECK(game::PoiStageResultText(def, progress, true).find("奖励已结算") !=
          std::string::npos);
}
