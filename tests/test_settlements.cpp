#include <doctest/doctest.h>

#include <string>

#include "core/Config.h"
#include "game/Missions.h"
#include "game/Settlement.h"

using namespace zl;

namespace {

std::string SettlementJson(float guardX) {
    return std::string(R"({
  "format": "settlements-v1",
  "defs": [
    {
      "id": "mod_haven",
      "name": "模组聚落",
      "centerX": 70.5,
      "centerY": 24.5,
      "safeRadius": 10.5,
      "npcPoints": [
        { "id": "mod_merchant", "kind": "merchant", "x": 71.5, "y": 23.5, "wanderRadius": 1.0 },
        { "id": "mod_guard", "kind": "guard", "x": )") +
           std::to_string(guardX) +
           R"(, "y": 25.5, "wanderRadius": 2.0 },
        { "id": "mod_requester", "kind": "requester", "x": 68.5, "y": 26.5, "wanderRadius": 1.0 }
      ],
      "merchantStock": [1, 2, 3, 4, 5],
      "restockDays": 2,
      "missionPool": ["event_supply_drop"]
    }
  ]
})";
}

}  // namespace

TEST_CASE("M23-F4-T1 聚落数据包含安全区与三类 NPC") {
    core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    config.Set("settlements.file",
               std::string(ZL_SOURCE_DIR) + "/assets/settlements.json");
    game::SettlementConfig settlements;
    std::string error;
    REQUIRE(game::LoadSettlementConfig(config, &settlements, &error));
    CHECK(error.empty());
    CHECK(settlements.enabled);
    REQUIRE(settlements.defs.size() == 1u);

    const game::SettlementDef* village =
        settlements.Find("village_haven");
    REQUIRE(village != nullptr);
    CHECK(village->name == "村庄营地");
    CHECK(village->safeRadius == doctest::Approx(8.0f));
    CHECK(village->npcPoints.size() == 3u);
    CHECK(village->FindNpc(game::SettlementNpcKind::Merchant) != nullptr);
    CHECK(village->FindNpc(game::SettlementNpcKind::Guard) != nullptr);
    CHECK(village->FindNpc(game::SettlementNpcKind::Requester) != nullptr);
    CHECK(village->merchantStock[0] == 4);
    CHECK(village->merchantStock[4] == 1);
    CHECK(village->restockDays == 1);
    CHECK(village->defense.enabled);
    CHECK(village->defense.minDay == 10);
    CHECK(village->defense.nightStartHour == 22);
    CHECK(village->defense.nightEndHour == 5);
    CHECK(village->defense.guardBehavior ==
          game::SettlementGuardBehavior::Defend);
    CHECK(village->defense.baseRaiders == 2);
    CHECK(village->defense.maxRaiders == 4);
    CHECK(village->defense.guardHealth == doctest::Approx(120.0f));
    CHECK(village->defense.raiderHealth == doctest::Approx(50.0f));
    for (const game::SettlementNpcPoint& point : village->npcPoints) {
        CHECK(game::IsInsideSettlement(*village, point.x, point.y));
    }
    CHECK(game::PrimarySettlement(settlements) == village);
    CHECK(game::FindSettlementContaining(
              settlements, village->centerX, village->centerY) == village);
    CHECK(game::FindSettlementContaining(
              settlements, village->centerX + 100.0f,
              village->centerY + 100.0f) == nullptr);
    CHECK(game::SettlementNpcKindName(game::SettlementNpcKind::Merchant) ==
          std::string("merchant"));
    CHECK(game::SettlementGuardBehaviorName(
              game::SettlementGuardBehavior::Retreat) ==
          std::string("retreat"));

    game::MissionConfig missions;
    REQUIRE(game::LoadMissionConfig(config, &missions, &error));
    for (const std::string& id : village->missionPool) {
        CHECK(missions.Find(id) != nullptr);
    }
}

TEST_CASE("M23-F4-T1 聚落 mod 文件只追加新 id 且拒绝越界 NPC") {
    game::SettlementConfig settlements;
    settlements.enabled = true;
    game::SettlementMergeStats stats;
    std::string error;
    REQUIRE(game::MergeSettlementJson(SettlementJson(73.5f), "mod settlement",
                                      &settlements, &stats, &error));
    CHECK(error.empty());
    CHECK(stats.filesLoaded == 1);
    CHECK(stats.defsAdded == 1);
    REQUIRE(settlements.Find("mod_haven") != nullptr);
    CHECK(settlements.Find("mod_haven")->merchantStock[4] == 5);
    CHECK(settlements.Find("mod_haven")->restockDays == 2);

    stats = {};
    CHECK_FALSE(game::MergeSettlementJson(SettlementJson(90.0f),
                                          "bad placement", &settlements,
                                          &stats, &error));
    CHECK(error.find("安全区") != std::string::npos);
}

TEST_CASE("M23-F4-T1 非法聚落格式被拒绝") {
    game::SettlementConfig settlements;
    std::string error;
    CHECK_FALSE(game::MergeSettlementJson(
        R"({ "format": "settlements-v2", "defs": [] })", "bad format",
        &settlements, nullptr, &error));
    CHECK(error.find("settlements-v1") != std::string::npos);
}

TEST_CASE("M23-F4-T5 聚落夜袭参数校验") {
    game::SettlementConfig settlements;
    std::string error;
    CHECK_FALSE(game::MergeSettlementJson(
        R"({
  "format": "settlements-v1",
  "defs": [{
    "id": "bad_defense",
    "name": "坏防御",
    "centerX": 70.5,
    "centerY": 24.5,
    "safeRadius": 10.5,
    "npcPoints": [
      { "id": "m", "kind": "merchant", "x": 71.5, "y": 23.5, "wanderRadius": 1.0 },
      { "id": "g", "kind": "guard", "x": 73.5, "y": 25.5, "wanderRadius": 2.0 },
      { "id": "r", "kind": "requester", "x": 68.5, "y": 26.5, "wanderRadius": 1.0 }
    ],
    "merchantStock": [1, 2, 3, 4, 5],
    "restockDays": 2,
    "missionPool": ["event_supply_drop"],
    "defense": { "enabled": true, "nightStartHour": 22, "nightEndHour": 22 }
  }]
})",
        "bad defense", &settlements, nullptr, &error));
    CHECK(error.find("夜袭起止小时") != std::string::npos);
}
