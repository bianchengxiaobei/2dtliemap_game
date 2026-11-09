#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <doctest/doctest.h>

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
