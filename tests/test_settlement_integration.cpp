#include <doctest/doctest.h>

#include <array>
#include <string>
#include <vector>

#include "core/Config.h"
#include "ecs/EcsWorld.h"
#include "game/Dialogue.h"
#include "game/Homestead.h"
#include "game/HomesteadComponents.h"
#include "game/MissionComponents.h"
#include "game/Missions.h"
#include "game/Settlement.h"
#include "game/SettlementReputation.h"

using namespace zl;

namespace {

bool HasDialogueResult(const std::vector<const game::DialogueOption*>& options,
                       game::DialogueResultKind kind) {
    for (const game::DialogueOption* option : options) {
        if (option != nullptr && option->result.kind == kind) {
            return true;
        }
    }
    return false;
}

bool HasDialogueGoto(const std::vector<const game::DialogueOption*>& options,
                     std::string_view target) {
    for (const game::DialogueOption* option : options) {
        if (option != nullptr &&
            option->result.kind == game::DialogueResultKind::Goto &&
            option->result.target == target) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("M23-F4-T7 内置聚落委托链对话接取、完成与不刷任务") {
    const std::string root{ZL_SOURCE_DIR};
    core::Config config;
    REQUIRE(config.LoadFile(root + "/config/game.json"));

    game::MissionConfig missions;
    std::string error;
    REQUIRE_MESSAGE(game::LoadMissionConfig(config, &missions, &error), error);
    config.Set("settlements.file", root + "/assets/settlements.json");
    game::SettlementConfig settlements;
    REQUIRE_MESSAGE(game::LoadSettlementConfig(config, &settlements, &error),
                    error);

    const game::SettlementDef* haven = settlements.Find("village_haven");
    REQUIRE(haven != nullptr);
    REQUIRE(haven->missionPool.size() == 4u);
    const game::DialogueTree requester = game::MakeSettlementDialogue(
        *haven, game::SettlementNpcKind::Requester);
    const game::DialogueNode* work = requester.FindNode("work");
    REQUIRE(work != nullptr);

    ecs::MissionLog log;
    game::DialogueContext context;
    context.settlement = haven;
    context.missions = &missions;
    context.missionLog = &log;
    context.day = 10;

    const std::vector<const char*> expected{
        "settlement_help", "settlement_escort", "settlement_clear",
        "settlement_supply"};
    for (const char* id : expected) {
        const game::MissionDef* available =
            game::FindAvailableSettlementMission(*haven, missions, log, 10);
        REQUIRE(available != nullptr);
        CHECK(available->id == id);
        CHECK(available->autoActivate == false);
        CHECK(available->timeLimitDays > 0);
        CHECK_FALSE(available->reward.label.empty());

        const auto availableOptions =
            game::VisibleDialogueOptions(*work, context);
        CHECK(HasDialogueResult(availableOptions,
                                game::DialogueResultKind::AcceptSettlementMission));
        CHECK_FALSE(HasDialogueResult(
            availableOptions, game::DialogueResultKind::FocusSettlementMission));

        ecs::MissionRunState run;
        run.id = available->id;
        run.progress.assign(available->objectives.size(), 0);
        run.status = ecs::MissionRunStatus::Active;
        run.day = 10;
        log.missions.push_back(run);

        CHECK(game::FindAvailableSettlementMission(*haven, missions, log, 10) ==
              nullptr);
        const game::MissionDef* active =
            game::FindActiveSettlementMission(*haven, missions, log);
        REQUIRE(active != nullptr);
        CHECK(active->id == id);
        const auto activeOptions =
            game::VisibleDialogueOptions(*work, context);
        CHECK_FALSE(HasDialogueResult(
            activeOptions, game::DialogueResultKind::AcceptSettlementMission));
        CHECK(HasDialogueResult(activeOptions,
                                game::DialogueResultKind::FocusSettlementMission));
        CHECK_FALSE(game::MissionCanActivate(*available, log, 10));

        ecs::MissionRunState* state = game::FindMissionState(&log, id);
        REQUIRE(state != nullptr);
        state->status = ecs::MissionRunStatus::Completed;
        state->rewardClaimed = true;
        CHECK_FALSE(game::MissionCanActivate(*available, log, 99));
    }

    CHECK(game::FindAvailableSettlementMission(*haven, missions, log, 99) ==
          nullptr);
    const auto finishedOptions = game::VisibleDialogueOptions(*work, context);
    CHECK_FALSE(HasDialogueResult(finishedOptions,
                                  game::DialogueResultKind::AcceptSettlementMission));
    CHECK_FALSE(HasDialogueResult(finishedOptions,
                                  game::DialogueResultKind::FocusSettlementMission));

    const game::MissionDef* help = missions.Find("settlement_help");
    REQUIRE(help != nullptr);
    const ecs::MissionRunState* helpState =
        game::FindMissionState(log, "settlement_help");
    REQUIRE(helpState != nullptr);
    CHECK(helpState->rewardClaimed);
    CHECK_FALSE(game::MissionCanActivate(*help, log, 99));
}

TEST_CASE("M33-F1-T3 聚落声望等级与服务摘要按配置解析") {
    game::SettlementReputationConfig reputation;
    const auto tier = [&](int score) {
        return game::SettlementReputationTierForScore(reputation, score);
    };
    REQUIRE(tier(-100) != nullptr);
    CHECK(tier(-100)->tier ==
          game::SettlementReputationTier::Hostile);
    CHECK(tier(-40)->tier == game::SettlementReputationTier::Hostile);
    CHECK(tier(-39)->tier == game::SettlementReputationTier::Wary);
    CHECK(tier(0)->tier == game::SettlementReputationTier::Neutral);
    CHECK(tier(19)->tier == game::SettlementReputationTier::Neutral);
    CHECK(tier(20)->tier == game::SettlementReputationTier::Friendly);
    CHECK(tier(59)->tier == game::SettlementReputationTier::Friendly);
    CHECK(tier(60)->tier == game::SettlementReputationTier::Trusted);
    CHECK(tier(100)->tier == game::SettlementReputationTier::Trusted);
    CHECK(tier(999)->tier == game::SettlementReputationTier::Trusted);
    CHECK(tier(-999)->tier == game::SettlementReputationTier::Hostile);
    CHECK(tier(0)->name == "中立");

    CHECK(game::SettlementServicesText(game::kAllSettlementServices) ==
          "交易/委托/防御/招募");
    CHECK(game::SettlementServicesText(game::kDefaultSettlementServices) ==
          "交易/委托/防御");
    CHECK(game::SettlementServicesText(game::kSettlementServiceTrade) ==
          "交易");
    CHECK(game::SettlementServicesText(0) == "无");

    game::SettlementConfig settlements;
    settlements.enabled = true;
    game::SettlementDef alpha;
    alpha.id = "alpha";
    alpha.centerX = 10.0f;
    alpha.centerY = 10.0f;
    alpha.safeRadius = 4.0f;
    game::SettlementDef beta;
    beta.id = "beta";
    beta.centerX = 30.0f;
    beta.centerY = 10.0f;
    beta.safeRadius = 4.0f;
    settlements.defs.push_back(alpha);
    settlements.defs.push_back(beta);
    REQUIRE(game::FindSettlementContaining(settlements, 11.0f, 10.0f) !=
            nullptr);
    CHECK(game::FindSettlementContaining(settlements, 11.0f, 10.0f)->id ==
          "alpha");
    CHECK(game::FindSettlementContaining(settlements, 29.0f, 10.0f)->id ==
          "beta");
    CHECK(game::FindSettlementContaining(settlements, 20.0f, 10.0f) ==
          nullptr);
}

TEST_CASE("M33-F1-T4 聚落声望增减、幂等键、每日交易与边界") {
    game::SettlementReputationConfig config;
    game::SettlementReputation reputation;
    CHECK(game::SettlementReputationScore(reputation, "village_haven", 25) ==
          25);

    auto change = game::ApplySettlementReputation(
        &reputation, config, "village_haven",
        game::SettlementReputationEvent::MissionComplete,
        "mission:village_haven:settlement_help:3:complete");
    CHECK(change.applied);
    CHECK(change.previous == 0);
    CHECK(change.current == 10);
    CHECK(change.delta == 10);
    CHECK(game::SettlementReputationScore(reputation, "village_haven") == 10);

    change = game::ApplySettlementReputation(
        &reputation, config, "village_haven",
        game::SettlementReputationEvent::MissionComplete,
        "mission:village_haven:settlement_help:3:complete");
    CHECK_FALSE(change.applied);
    CHECK(change.current == 10);
    CHECK(reputation.appliedKeys.size() == 1u);

    change = game::ApplySettlementReputation(
        &reputation, config, "village_haven",
        game::SettlementReputationEvent::MissionComplete,
        "mission:village_haven:settlement_supply:3:complete");
    CHECK(change.applied);
    CHECK(change.current == 20);

    change = game::ApplySettlementReputation(
        &reputation, config, "village_haven",
        game::SettlementReputationEvent::MissionAbandon,
        "mission:village_haven:settlement_clear:3:abandon");
    CHECK(change.current == 10);

    change = game::ApplySettlementReputation(
        &reputation, config, "village_haven",
        game::SettlementReputationEvent::MissionTimeout,
        "mission:village_haven:settlement_clear:4:fail");
    CHECK(change.current == 0);

    change = game::ApplySettlementReputation(
        &reputation, config, "village_haven",
        game::SettlementReputationEvent::Trade, {}, 0, 7);
    CHECK(change.applied);
    CHECK(change.current == 1);
    CHECK(game::SettlementReputationDailyTradeGain(reputation,
                                                   "village_haven", 7) == 1);
    change = game::ApplySettlementReputation(
        &reputation, config, "village_haven",
        game::SettlementReputationEvent::Trade, {}, 0, 7);
    CHECK_FALSE(change.applied);
    CHECK(change.limited);
    CHECK(change.current == 1);
    change = game::ApplySettlementReputation(
        &reputation, config, "village_haven",
        game::SettlementReputationEvent::Trade, {}, 0, 8);
    CHECK(change.applied);
    CHECK(change.current == 2);

    change = game::ApplySettlementReputation(
        &reputation, config, "village_haven",
        game::SettlementReputationEvent::EventComplete,
        "event:village_haven:supply_drop:8");
    CHECK(change.applied);
    CHECK(change.current == 12);

    game::SettlementReputation capped;
    for (int i = 0; i < 12; ++i) {
        change = game::ApplySettlementReputation(
            &capped, config, "north_fields",
            game::SettlementReputationEvent::MissionComplete,
            "mission:north_fields:m" + std::to_string(i) + ":complete");
    }
    CHECK(change.current == 100);
    change = game::ApplySettlementReputation(
        &capped, config, "north_fields",
        game::SettlementReputationEvent::MissionComplete,
        "mission:north_fields:overflow:complete");
    CHECK(change.applied);
    CHECK(change.current == 100);
    CHECK(change.delta == 0);
    for (int i = 0; i < 25; ++i) {
        change = game::ApplySettlementReputation(
            &capped, config, "north_fields",
            game::SettlementReputationEvent::MissionAbandon,
            "mission:north_fields:a" + std::to_string(i) + ":abandon");
    }
    CHECK(change.current == -100);

    change = game::ApplySettlementReputation(
        &capped, config, "INVALID",
        game::SettlementReputationEvent::MissionComplete, "invalid");
    CHECK_FALSE(change.applied);
    config.enabled = false;
    change = game::ApplySettlementReputation(
        &capped, config, "north_fields",
        game::SettlementReputationEvent::MissionComplete, "disabled");
    CHECK_FALSE(change.applied);

    config.enabled = true;
    ecs::EcsWorld world;
    const ecs::Entity player = world.Create();
    change = game::AwardSettlementReputation(
        world, player, config, "village_haven",
        game::SettlementReputationEvent::MissionComplete,
        "mission:village_haven:ecs:1:complete");
    CHECK(change.applied);
    CHECK(change.current == 10);
    REQUIRE(world.Registry().try_get<game::SettlementReputation>(player) !=
            nullptr);
    CHECK(game::SettlementReputationScoreFor(world, player, "village_haven",
                                             99) == 10);
    CHECK(game::SettlementReputationScoreFor(world, player, "north_fields",
                                             99) == 99);
}

TEST_CASE("M33-F1-T5 五档声望价格、任务门槛、对话与服务矩阵") {
    const std::string root{ZL_SOURCE_DIR};
    core::Config config;
    REQUIRE(config.LoadFile(root + "/config/game.json"));
    game::MissionConfig missions;
    std::string error;
    REQUIRE_MESSAGE(game::LoadMissionConfig(config, &missions, &error), error);
    config.Set("settlements.file", root + "/assets/settlements.json");
    game::SettlementConfig settlements;
    REQUIRE_MESSAGE(game::LoadSettlementConfig(config, &settlements, &error),
                    error);
    const game::SettlementDef* haven = settlements.Find("village_haven");
    const game::SettlementDef* clinic = settlements.Find("north_fields");
    REQUIRE(haven != nullptr);
    REQUIRE(clinic != nullptr);
    const game::SettlementReputationConfig& reputation = settlements.reputation;

    const std::array<int, 5> scores{-60, -20, 0, 20, 60};
    const std::array<int, 5> buyBandage{3, 3, 2, 2, 2};
    const std::array<int, 5> buyPistol{12, 10, 8, 7, 6};
    const std::array<int, 5> sellWood{1, 2, 2, 2, 3};
    for (std::size_t i = 0; i < scores.size(); ++i) {
        CHECK(game::SettlementBuyPrice(2, reputation, scores[i]) ==
              buyBandage[i]);
        CHECK(game::SettlementBuyPrice(8, reputation, scores[i]) ==
              buyPistol[i]);
        CHECK(game::SettlementSellPrice(2, reputation, scores[i]) ==
              sellWood[i]);
    }
    CHECK(game::SettlementAdjustedPrice(2, 1.25f) == 3);
    CHECK(game::SettlementAdjustedPrice(2, 0.8f) == 2);
    CHECK(game::SettlementAdjustedPrice(0, 1.0f) == 0);

    CHECK(game::SettlementCanUseService(
        *haven, reputation, game::SettlementService::Trade, 0));
    CHECK_FALSE(game::SettlementCanUseService(
        *haven, reputation, game::SettlementService::Trade, -40));
    CHECK(game::SettlementCanUseService(
        *haven, reputation, game::SettlementService::Defense, -40));
    CHECK_FALSE(game::SettlementCanUseService(
        *haven, reputation, game::SettlementService::Recruit, -40));
    CHECK_FALSE(game::SettlementCanUseService(
        *clinic, reputation, game::SettlementService::Recruit, 60));
    game::SettlementDef noMissions = *haven;
    noMissions.services &= ~game::kSettlementServiceMissions;
    CHECK_FALSE(game::SettlementCanUseService(
        noMissions, reputation, game::SettlementService::Missions, 60));
    game::SettlementDef noDefense = *haven;
    noDefense.services &= ~game::kSettlementServiceDefense;
    CHECK_FALSE(game::SettlementProvidesDefense(noDefense));
    CHECK(game::SettlementProvidesDefense(*haven));

    const game::MissionDef* help = missions.Find("settlement_help");
    const game::MissionDef* escort = missions.Find("settlement_escort");
    const game::MissionDef* clear = missions.Find("settlement_clear");
    const game::MissionDef* supply = missions.Find("settlement_supply");
    const game::MissionDef* refugee = missions.Find("refugee_relief");
    REQUIRE(help != nullptr);
    REQUIRE(escort != nullptr);
    REQUIRE(clear != nullptr);
    REQUIRE(supply != nullptr);
    REQUIRE(refugee != nullptr);
    CHECK(help->minSettlementReputation == -39);
    CHECK(escort->minSettlementReputation == 0);
    CHECK(clear->minSettlementReputation == 20);
    CHECK(supply->minSettlementReputation == 30);
    CHECK(refugee->minSettlementReputation == -100);
    CHECK_FALSE(game::MissionMeetsSettlementReputation(*help, -40));
    CHECK(game::MissionMeetsSettlementReputation(*help, -39));
    CHECK_FALSE(game::MissionMeetsSettlementReputation(*clear, 19));
    CHECK(game::MissionMeetsSettlementReputation(*clear, 20));

    ecs::MissionLog log;
    CHECK(game::FindAvailableSettlementMission(*haven, missions, log, 10,
                                               -60) == nullptr);
    const game::MissionDef* available =
        game::FindAvailableSettlementMission(*haven, missions, log, 10, -39);
    REQUIRE(available != nullptr);
    CHECK(available->id == "settlement_help");

    game::MissionDef locked = *help;
    locked.id = "locked_test";
    locked.name = "声望测试委托";
    locked.minSettlementReputation = 60;
    game::MissionConfig lockedMissions;
    lockedMissions.enabled = true;
    lockedMissions.defs.push_back(locked);
    game::SettlementDef lockedSettlement = *haven;
    lockedSettlement.missionPool = {"locked_test"};
    CHECK(game::FindAvailableSettlementMission(
              lockedSettlement, lockedMissions, log, 1, 59) == nullptr);
    REQUIRE(game::FindAvailableSettlementMission(
                lockedSettlement, lockedMissions, log, 1, 60) != nullptr);

    game::DialogueContext context;
    context.settlement = &lockedSettlement;
    context.missions = &lockedMissions;
    context.missionLog = &log;
    context.day = 1;
    context.settlementReputation = 59;
    const game::DialogueTree lockedTree = game::MakeSettlementDialogue(
        lockedSettlement, game::SettlementNpcKind::Requester, reputation, 59);
    const game::DialogueNode* lockedWork = lockedTree.FindNode("work");
    REQUIRE(lockedWork != nullptr);
    const auto lockedOptions =
        game::VisibleDialogueOptions(*lockedWork, context);
    CHECK_FALSE(HasDialogueResult(
        lockedOptions, game::DialogueResultKind::AcceptSettlementMission));
    CHECK(HasDialogueGoto(lockedOptions, "low_rep"));
    context.settlementReputation = 60;
    const auto unlockedOptions =
        game::VisibleDialogueOptions(*lockedWork, context);
    CHECK(HasDialogueResult(
        unlockedOptions, game::DialogueResultKind::AcceptSettlementMission));

    std::array<std::string, 5> attitudeRoots;
    for (std::size_t i = 0; i < scores.size(); ++i) {
        const game::DialogueTree tree = game::MakeSettlementDialogue(
            *haven, game::SettlementNpcKind::Requester, reputation,
            scores[i]);
        const game::DialogueNode* rootNode = tree.FindNode("root");
        REQUIRE(rootNode != nullptr);
        attitudeRoots[i] = rootNode->text;
    }
    for (std::size_t i = 0; i < attitudeRoots.size(); ++i) {
        for (std::size_t j = i + 1; j < attitudeRoots.size(); ++j) {
            CHECK(attitudeRoots[i] != attitudeRoots[j]);
        }
    }

    const game::DialogueTree hostile = game::MakeSettlementDialogue(
        *haven, game::SettlementNpcKind::Requester, reputation, -60);
    const game::DialogueNode* hostileRoot = hostile.FindNode("root");
    REQUIRE(hostileRoot != nullptr);
    CHECK(hostileRoot->text.find("没有给你的委托") != std::string::npos);
    CHECK_FALSE(HasDialogueGoto(
        game::VisibleDialogueOptions(*hostileRoot, context), "work"));

    game::SettlementDef noTrade = *haven;
    noTrade.services &= ~game::kSettlementServiceTrade;
    const game::DialogueTree noTradeTree = game::MakeSettlementDialogue(
        noTrade, game::SettlementNpcKind::Merchant, reputation, 0);
    const game::DialogueNode* noTradeRoot = noTradeTree.FindNode("root");
    REQUIRE(noTradeRoot != nullptr);
    CHECK_FALSE(HasDialogueResult(
        game::VisibleDialogueOptions(*noTradeRoot, context),
        game::DialogueResultKind::OpenTrade));

    const game::DialogueTree noMissionTree = game::MakeSettlementDialogue(
        noMissions, game::SettlementNpcKind::Requester, reputation, 0);
    const game::DialogueNode* noMissionRoot = noMissionTree.FindNode("root");
    REQUIRE(noMissionRoot != nullptr);
    CHECK(noMissionRoot->text.find("不对外发布委托") != std::string::npos);
    CHECK_FALSE(HasDialogueGoto(
        game::VisibleDialogueOptions(*noMissionRoot, context), "work"));

    const game::DialogueTree noDefenseTree = game::MakeSettlementDialogue(
        noDefense, game::SettlementNpcKind::Guard, reputation, 0);
    const game::DialogueNode* noDefenseRoot = noDefenseTree.FindNode("root");
    REQUIRE(noDefenseRoot != nullptr);
    CHECK(noDefenseRoot->text.find("没有常备守卫") != std::string::npos);
}

TEST_CASE("M23-F4-T7 聚落库存按配置初始化且非法数量被夹紧") {
    const std::string root{ZL_SOURCE_DIR};
    core::Config config;
    REQUIRE(config.LoadFile(root + "/config/game.json"));
    config.Set("settlements.file", root + "/assets/settlements.json");
    game::SettlementConfig settlements;
    std::string error;
    REQUIRE_MESSAGE(game::LoadSettlementConfig(config, &settlements, &error),
                    error);
    const game::SettlementDef* haven = settlements.Find("village_haven");
    REQUIRE(haven != nullptr);
    CHECK(haven->merchantStock ==
          std::array<int, 5>{4, 2, 1, 2, 1});

    ecs::MerchantStock stock;
    stock.stock = haven->merchantStock;
    stock.restockDay = 3;
    game::ClampMerchantStock(stock);
    CHECK(stock.stock == haven->merchantStock);
    CHECK(stock.restockDay == 3);

    stock.stock[0] = -5;
    stock.stock[2] = 200;
    game::ClampMerchantStock(stock);
    CHECK(stock.stock[0] == 0);
    CHECK(stock.stock[2] == 1);
    CHECK(stock.restockDay == 3);
}

TEST_CASE("M33-F1-T6 四聚落独立任务池、门槛、时限与重试") {
    const std::string root{ZL_SOURCE_DIR};
    core::Config config;
    REQUIRE(config.LoadFile(root + "/config/game.json"));
    config.Set("settlements.file", root + "/assets/settlements.json");
    game::SettlementConfig settlements;
    game::MissionConfig missions;
    std::string error;
    REQUIRE_MESSAGE(game::LoadSettlementConfig(config, &settlements, &error),
                    error);
    REQUIRE_MESSAGE(game::LoadMissionConfig(config, &missions, &error),
                    error);

    struct Expectation {
        const char* settlementId;
        const char* missionId;
        int minReputation;
        int timeLimitDays;
        std::size_t poolSize;
    };
    const std::array<Expectation, 4> expectations{{
        {"village_haven", "settlement_help", -39, 2, 4},
        {"north_fields", "north_blood_drive", -39, 2, 1},
        {"south_farmland", "south_harvest_help", 0, 3, 1},
        {"river_camp", "river_fish_supply", -39, 2, 1},
    }};
    for (const Expectation& expected : expectations) {
        const game::SettlementDef* settlement =
            settlements.Find(expected.settlementId);
        REQUIRE(settlement != nullptr);
        const game::MissionDef* mission = missions.Find(expected.missionId);
        REQUIRE(mission != nullptr);
        REQUIRE(settlement->missionPool.size() == expected.poolSize);
        CHECK(settlement->missionPool.front() == expected.missionId);
        CHECK(mission->minSettlementReputation == expected.minReputation);
        CHECK(mission->timeLimitDays == expected.timeLimitDays);
        CHECK(mission->retryDays == 1);
        CHECK(mission->source == game::MissionSource::Npc);
        CHECK_FALSE(mission->autoActivate);
        CHECK(mission->prerequisite.empty());

        CHECK(game::MissionMeetsSettlementReputation(
            *mission, expected.minReputation));
        CHECK_FALSE(game::MissionMeetsSettlementReputation(
            *mission, expected.minReputation - 1));

        ecs::MissionLog log;
        const game::MissionDef* available =
            game::FindAvailableSettlementMission(
                *settlement, missions, log, 10, expected.minReputation);
        REQUIRE(available != nullptr);
        CHECK(available->id == expected.missionId);
        CHECK(game::FindAvailableSettlementMission(
                  *settlement, missions, log, 10,
                  expected.minReputation - 1) == nullptr);
        CHECK(game::MissionCanActivate(*mission, log, 10));

        ecs::MissionRunState run;
        run.id = mission->id;
        run.progress.assign(mission->objectives.size(), 0);
        run.status = ecs::MissionRunStatus::Active;
        run.day = 10;
        log.missions.push_back(run);
        CHECK_FALSE(game::MissionCanActivate(*mission, log, 10));
        CHECK(game::MissionDeadlineDay(*mission, log.missions.back()) ==
              10 + expected.timeLimitDays);
        CHECK_FALSE(game::MissionIsExpired(
            *mission, log.missions.back(),
            10 + expected.timeLimitDays - 1));
        CHECK(game::MissionIsExpired(
            *mission, log.missions.back(),
            10 + expected.timeLimitDays));

        log.missions.back().status = ecs::MissionRunStatus::Failed;
        log.missions.back().day = 10;
        CHECK_FALSE(game::MissionCanActivate(*mission, log, 10));
        CHECK(game::MissionCanActivate(*mission, log, 11));
        log.missions.back().status = ecs::MissionRunStatus::Completed;
        log.missions.back().rewardClaimed = true;
        CHECK_FALSE(game::MissionCanActivate(*mission, log, 99));
    }
}

TEST_CASE("M33-F1-T6 新聚落任务完成增加声望、放弃降低并重新锁定") {
    const std::string root{ZL_SOURCE_DIR};
    core::Config config;
    REQUIRE(config.LoadFile(root + "/config/game.json"));
    config.Set("settlements.file", root + "/assets/settlements.json");
    game::SettlementConfig settlements;
    game::MissionConfig missions;
    std::string error;
    REQUIRE_MESSAGE(game::LoadSettlementConfig(config, &settlements, &error),
                    error);
    REQUIRE_MESSAGE(game::LoadMissionConfig(config, &missions, &error),
                    error);

    const game::MissionDef* riverMission =
        missions.Find("river_fish_supply");
    REQUIRE(riverMission != nullptr);
    game::SettlementReputation reputation;
    const game::SettlementReputationChange gain =
        game::ApplySettlementReputation(
            &reputation, settlements.reputation, "river_camp",
            game::SettlementReputationEvent::MissionComplete,
            "mission:river_camp:river_fish_supply:10:complete", 0, 10);
    CHECK(gain.applied);
    CHECK(gain.delta == 10);
    CHECK(gain.current == 10);
    CHECK(game::MissionMeetsSettlementReputation(*riverMission, gain.current));

    const game::MissionDef* farmMission =
        missions.Find("south_harvest_help");
    REQUIRE(farmMission != nullptr);
    const game::SettlementReputationChange loss =
        game::ApplySettlementReputation(
            &reputation, settlements.reputation, "south_farmland",
            game::SettlementReputationEvent::MissionAbandon,
            "mission:south_farmland:south_harvest_help:10:abandon", 0, 10);
    CHECK(loss.applied);
    CHECK(loss.delta == -10);
    CHECK(loss.current == -10);
    CHECK_FALSE(
        game::MissionMeetsSettlementReputation(*farmMission, loss.current));

    const game::SettlementDef* river = settlements.Find("river_camp");
    REQUIRE(river != nullptr);
    ecs::MissionLog log;
    CHECK(game::FindAvailableSettlementMission(
              *river, missions, log, 10, gain.current) != nullptr);
    CHECK(game::FindAvailableSettlementMission(
              *river, missions, log, 10, -40) == nullptr);
}
