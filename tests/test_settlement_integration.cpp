#include <doctest/doctest.h>

#include <array>
#include <string>
#include <vector>

#include "core/Config.h"
#include "game/Dialogue.h"
#include "game/Homestead.h"
#include "game/HomesteadComponents.h"
#include "game/MissionComponents.h"
#include "game/Missions.h"
#include "game/Settlement.h"

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
