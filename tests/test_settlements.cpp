#include <doctest/doctest.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

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

std::string SettlementMetaJson(
    std::string_view services = R"(["trade", "missions", "defense"])",
    int startingReputation = 0) {
    return std::string(R"({
  "format": "settlements-v1",
  "defs": [
    {
      "id": "mod_haven",
      "name": "模组聚落",
      "regionId": "mod_region",
      "services": )") +
           std::string(services) + R"(,
      "startingReputation": )" +
           std::to_string(startingReputation) + R"(,
      "dialogueProfile": "mod_profile",
      "centerX": 70.5,
      "centerY": 24.5,
      "safeRadius": 10.5,
      "npcPoints": [
        { "id": "mod_merchant", "kind": "merchant", "x": 71.5, "y": 23.5, "wanderRadius": 1.0 },
        { "id": "mod_guard", "kind": "guard", "x": 73.5, "y": 25.5, "wanderRadius": 2.0 },
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
    REQUIRE(settlements.defs.size() == 4u);

    const game::SettlementDef* village =
        settlements.Find("village_haven");
    REQUIRE(village != nullptr);
    CHECK(village->name == "村庄营地");
    CHECK(village->regionId == "village_core");
    CHECK(village->services == game::kAllSettlementServices);
    CHECK(village->startingReputation == 0);
    CHECK(village->dialogueProfile == "village_haven");
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
    const game::SettlementDef* farm =
        settlements.Find("south_farmland");
    REQUIRE(farm != nullptr);
    CHECK(farm->centerX == doctest::Approx(110.0f));
    CHECK(farm->centerY == doctest::Approx(210.0f));
    CHECK(farm->regionId == "south_farmland");
    CHECK(game::HasSettlementService(*farm, game::SettlementService::Recruit));
    CHECK(farm->defense.enabled);
    CHECK(farm->defense.minDay == 9999);
    CHECK(farm->FindNpc(game::SettlementNpcKind::Guard) != nullptr);
    CHECK(farm->FindNpc(game::SettlementNpcKind::Requester) != nullptr);
    const game::SettlementDef* clinic =
        settlements.Find("north_fields");
    REQUIRE(clinic != nullptr);
    CHECK(clinic->centerX == doctest::Approx(150.0f));
    CHECK(clinic->centerY == doctest::Approx(64.0f));
    CHECK(clinic->regionId == "north_fields");
    CHECK(clinic->merchantStock == std::array<int, 5>{3, 1, 0, 2, 0});
    CHECK(clinic->restockDays == 2);
    CHECK(clinic->missionPool ==
          std::vector<std::string>{"north_blood_drive"});
    CHECK_FALSE(
        game::HasSettlementService(*clinic, game::SettlementService::Recruit));
    CHECK(game::HasSettlementService(*clinic,
                                     game::SettlementService::Defense));
    CHECK(farm->merchantStock == std::array<int, 5>{4, 1, 1, 1, 0});
    CHECK(farm->restockDays == 2);
    CHECK(farm->missionPool ==
          std::vector<std::string>{"south_harvest_help"});

    const game::SettlementDef* river = settlements.Find("river_camp");
    REQUIRE(river != nullptr);
    CHECK(river->name == "河畔营地");
    CHECK(river->regionId == "riverlands");
    CHECK(river->centerX == doctest::Approx(216.0f));
    CHECK(river->centerY == doctest::Approx(120.0f));
    CHECK(river->safeRadius == doctest::Approx(4.5f));
    CHECK(river->services ==
          (game::kSettlementServiceTrade | game::kSettlementServiceMissions));
    CHECK(river->dialogueProfile == "river_camp");
    CHECK(river->npcPoints.size() == 2u);
    CHECK(river->FindNpc(game::SettlementNpcKind::Merchant) != nullptr);
    CHECK(river->FindNpc(game::SettlementNpcKind::Requester) != nullptr);
    CHECK(river->FindNpc(game::SettlementNpcKind::Guard) == nullptr);
    CHECK_FALSE(game::SettlementProvidesDefense(*river));
    CHECK(river->merchantStock == std::array<int, 5>{2, 2, 1, 2, 1});
    CHECK(river->restockDays == 1);
    CHECK(river->missionPool ==
          std::vector<std::string>{"river_fish_supply"});
    for (const game::SettlementDef& def : settlements.defs) {
        for (const game::SettlementNpcPoint& point : def.npcPoints) {
            CHECK(game::IsInsideSettlement(def, point.x, point.y));
        }
    }
    CHECK(clinic->services != village->services);
    CHECK(river->services != clinic->services);
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
    for (const game::SettlementDef& def : settlements.defs) {
        for (const std::string& id : def.missionPool) {
            const game::MissionDef* mission = missions.Find(id);
            REQUIRE(mission != nullptr);
            CHECK(mission->source == game::MissionSource::Npc);
            CHECK_FALSE(mission->autoActivate);
            CHECK(mission->timeLimitDays > 0);
            CHECK(mission->retryDays >= 1);
        }
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
    CHECK(settlements.Find("mod_haven")->regionId.empty());
    CHECK(settlements.Find("mod_haven")->services ==
          game::kDefaultSettlementServices);
    CHECK(settlements.Find("mod_haven")->startingReputation == 0);
    CHECK(settlements.Find("mod_haven")->dialogueProfile.empty());

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

TEST_CASE("M33-F1-T1 聚落元数据与服务默认值") {
    game::SettlementConfig settlements;
    settlements.enabled = true;
    std::string error;
    REQUIRE(game::MergeSettlementJson(SettlementMetaJson(), "meta",
                                      &settlements, nullptr, &error));
    const game::SettlementDef* mod = settlements.Find("mod_haven");
    REQUIRE(mod != nullptr);
    CHECK(mod->regionId == "mod_region");
    CHECK(mod->services == game::kDefaultSettlementServices);
    CHECK(mod->startingReputation == 0);
    CHECK(mod->dialogueProfile == "mod_profile");
    CHECK(game::HasSettlementService(*mod, game::SettlementService::Trade));
    CHECK(game::HasSettlementService(*mod, game::SettlementService::Missions));
    CHECK(game::HasSettlementService(*mod, game::SettlementService::Defense));
    CHECK_FALSE(game::HasSettlementService(*mod,
                                           game::SettlementService::Recruit));

    game::SettlementConfig withRecruit;
    withRecruit.enabled = true;
    REQUIRE(game::MergeSettlementJson(
        SettlementMetaJson(R"(["trade", "missions", "recruit"])", 25),
        "meta_recruit", &withRecruit, nullptr, &error));
    REQUIRE(withRecruit.Find("mod_haven") != nullptr);
    CHECK(withRecruit.Find("mod_haven")->startingReputation == 25);
    CHECK(withRecruit.Find("mod_haven")->services ==
          (game::kSettlementServiceTrade | game::kSettlementServiceMissions |
           game::kSettlementServiceRecruit));
    CHECK(game::SettlementServiceName(game::SettlementService::Missions) ==
          std::string("missions"));
    CHECK(game::SettlementReputationTierName(
              game::SettlementReputationTier::Trusted) ==
          std::string("trusted"));
}

TEST_CASE("M33-F1-T1 聚落元数据越界与重复 id 被拒绝") {
    std::string error;
    game::SettlementConfig settlements;
    settlements.enabled = true;
    CHECK_FALSE(game::MergeSettlementJson(
        SettlementMetaJson(R"(["trade", "bogus"])"), "bad service",
        &settlements, nullptr, &error));
    CHECK(error.find("未知聚落服务") != std::string::npos);

    error.clear();
    CHECK_FALSE(game::MergeSettlementJson(
        SettlementMetaJson(R"(["trade"])", 101), "bad reputation",
        &settlements, nullptr, &error));
    CHECK(error.find("startingReputation") != std::string::npos);
    CHECK(settlements.defs.empty());

    REQUIRE(game::MergeSettlementJson(SettlementJson(73.5f), "first",
                                      &settlements, nullptr, &error));
    game::SettlementMergeStats stats;
    CHECK_FALSE(game::MergeSettlementJson(SettlementMetaJson(), "duplicate",
                                          &settlements, &stats, &error));
    CHECK(stats.defsSkipped == 1);
    CHECK(error.find("重复") != std::string::npos);
}

TEST_CASE("M33-F1-T1 聚落声望配置校验与默认值") {
    core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    game::SettlementReputationConfig reputation;
    std::string error;
    REQUIRE(game::LoadSettlementReputationConfig(config, &reputation, &error));
    CHECK(error.empty());
    CHECK(reputation.enabled);
    REQUIRE(reputation.tiers.size() == 5u);
    CHECK(reputation.tiers[0].id == "hostile");
    CHECK(reputation.tiers[0].name == "敌对");
    CHECK(reputation.tiers[0].minScore == -100);
    CHECK(reputation.tiers[0].maxScore == -40);
    CHECK(reputation.tiers[4].id == "trusted");
    CHECK(reputation.tiers[4].minScore == 60);
    CHECK(reputation.tiers[4].maxScore == 100);
    CHECK(reputation.sources.missionComplete == 10);
    CHECK(reputation.sources.defenseComplete == 15);
    CHECK(reputation.sources.trade == 1);
    CHECK(reputation.sources.missionAbandon == -10);
    CHECK(reputation.sources.attackNpc == -25);
    CHECK(reputation.sources.theft == -25);
    CHECK(reputation.dailyTradeGainLimit == 1);
    REQUIRE(game::FindSettlementPriceMultiplier(
                reputation, game::SettlementReputationTier::Hostile) !=
            nullptr);
    CHECK(game::FindSettlementPriceMultiplier(
              reputation, game::SettlementReputationTier::Hostile)
              ->buy == doctest::Approx(1.5f));
    CHECK(game::FindSettlementPriceMultiplier(
              reputation, game::SettlementReputationTier::Trusted)
              ->sell == doctest::Approx(1.25f));

    core::Config defaults;
    game::SettlementReputationConfig defaultReputation;
    REQUIRE(game::LoadSettlementReputationConfig(defaults, &defaultReputation,
                                                 &error));
    CHECK(defaultReputation.tiers[2].minScore == 0);
    CHECK(defaultReputation.tiers[2].maxScore == 19);

    core::Config invalid;
    invalid.Set("settlements.reputation.dailyTradeGainLimit", -1);
    CHECK_FALSE(
        game::LoadSettlementReputationConfig(invalid, &reputation, &error));
    CHECK(error.find("dailyTradeGainLimit") != std::string::npos);

    invalid = core::Config();
    nlohmann::json tiers = nlohmann::json::parse(
        R"([
          { "id": "hostile", "minScore": -100, "maxScore": -40 },
          { "id": "wary", "minScore": -38, "maxScore": -1 },
          { "id": "neutral", "minScore": 0, "maxScore": 19 },
          { "id": "friendly", "minScore": 20, "maxScore": 59 },
          { "id": "trusted", "minScore": 60, "maxScore": 100 }
        ])");
    invalid.Set("settlements.reputation.tiers", tiers);
    CHECK_FALSE(
        game::LoadSettlementReputationConfig(invalid, &reputation, &error));
    CHECK(error.find("连续") != std::string::npos);

    invalid = core::Config();
    invalid.Set("settlements.reputation.priceMultipliers.hostile.buy", 9.0);
    CHECK_FALSE(
        game::LoadSettlementReputationConfig(invalid, &reputation, &error));
    CHECK(error.find("buy/sell") != std::string::npos);
}
