#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

#include "core/Config.h"
#include "game/Dialogue.h"
#include "game/MissionComponents.h"
#include "game/Missions.h"
#include "items/ItemComponents.h"

using namespace zl;

namespace {

game::SettlementDef MakeSettlement() {
    game::SettlementDef settlement;
    settlement.id = "test_haven";
    settlement.name = "测试营地";
    settlement.missionPool = {"test_task"};
    return settlement;
}

game::MissionConfig MakeMissions() {
    game::MissionConfig config;
    config.enabled = true;
    game::MissionDef def;
    def.id = "test_task";
    def.name = "测试委托";
    def.kind = game::MissionKind::Once;
    def.minDay = 1;
    def.objectives.push_back(game::MissionObjectiveDef{});
    config.defs.push_back(std::move(def));
    return config;
}

const game::DialogueOption* FindOption(
    const game::DialogueNode& node, game::DialogueResultKind kind) {
    for (const game::DialogueOption& option : node.options) {
        if (option.result.kind == kind) {
            return &option;
        }
    }
    return nullptr;
}

bool HasVisibleOption(const std::vector<const game::DialogueOption*>& options,
                      std::string_view text) {
    for (const game::DialogueOption* option : options) {
        if (option != nullptr && option->text == text) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("M23-F4-T3 三类聚落 NPC 对话树入口完整") {
    const game::SettlementDef settlement = MakeSettlement();
    const game::DialogueTree merchant = game::MakeSettlementDialogue(
        settlement, game::SettlementNpcKind::Merchant);
    const game::DialogueTree guard = game::MakeSettlementDialogue(
        settlement, game::SettlementNpcKind::Guard);
    const game::DialogueTree requester = game::MakeSettlementDialogue(
        settlement, game::SettlementNpcKind::Requester);

    CHECK(merchant.startNode == "root");
    CHECK(merchant.settlementId == settlement.id);
    const game::DialogueNode* merchantRoot =
        merchant.FindNode(merchant.startNode);
    REQUIRE(merchantRoot != nullptr);
    CHECK(merchantRoot->speaker.find("商人") != std::string::npos);
    CHECK(FindOption(*merchantRoot, game::DialogueResultKind::OpenTrade) !=
          nullptr);
    CHECK(FindOption(*merchantRoot, game::DialogueResultKind::Goto) !=
          nullptr);

    const game::DialogueNode* guardRoot = guard.FindNode(guard.startNode);
    REQUIRE(guardRoot != nullptr);
    CHECK(guardRoot->speaker.find("守卫") != std::string::npos);
    CHECK(FindOption(*guardRoot, game::DialogueResultKind::Goto) != nullptr);

    const game::DialogueNode* requesterRoot =
        requester.FindNode(requester.startNode);
    REQUIRE(requesterRoot != nullptr);
    CHECK(requesterRoot->speaker.find("求助者") != std::string::npos);
    const game::DialogueNode* work = requester.FindNode("work");
    REQUIRE(work != nullptr);
    const game::DialogueOption* accept = FindOption(
        *work, game::DialogueResultKind::AcceptSettlementMission);
    const game::DialogueOption* focus = FindOption(
        *work, game::DialogueResultKind::FocusSettlementMission);
    REQUIRE(accept != nullptr);
    REQUIRE(focus != nullptr);
    CHECK(accept->condition.kind ==
          game::DialogueConditionKind::MissionAvailable);
    CHECK(focus->condition.kind ==
          game::DialogueConditionKind::MissionActive);

    for (const game::DialogueTree* tree : {&merchant, &guard, &requester}) {
        for (const game::DialogueNode& node : tree->nodes) {
            for (const game::DialogueOption& option : node.options) {
                if (option.result.kind == game::DialogueResultKind::Goto) {
                    CHECK(tree->FindNode(option.result.target) != nullptr);
                }
            }
        }
    }
}

TEST_CASE("M33-F1-T3 聚落对话与任务池按 NPC 归属隔离") {
    game::SettlementDef first;
    first.id = "haven_alpha";
    first.name = "甲营地";
    first.missionPool = {"alpha_task"};
    game::SettlementDef second;
    second.id = "haven_beta";
    second.name = "乙营地";
    second.missionPool = {"beta_task"};

    const game::DialogueTree firstTree = game::MakeSettlementDialogue(
        first, game::SettlementNpcKind::Requester);
    const game::DialogueTree secondTree = game::MakeSettlementDialogue(
        second, game::SettlementNpcKind::Requester);
    CHECK(firstTree.settlementId == "haven_alpha");
    CHECK(secondTree.settlementId == "haven_beta");
    const game::DialogueNode* firstRoot =
        firstTree.FindNode(firstTree.startNode);
    const game::DialogueNode* secondRoot =
        secondTree.FindNode(secondTree.startNode);
    REQUIRE(firstRoot != nullptr);
    REQUIRE(secondRoot != nullptr);
    CHECK(firstRoot->speaker.find("甲营地") != std::string::npos);
    CHECK(firstRoot->speaker.find("乙营地") == std::string::npos);
    CHECK(secondRoot->speaker.find("乙营地") != std::string::npos);
    CHECK(secondRoot->speaker.find("甲营地") == std::string::npos);

    game::MissionConfig missions;
    missions.enabled = true;
    for (const char* id : {"alpha_task", "beta_task"}) {
        game::MissionDef def;
        def.id = id;
        def.name = id;
        def.kind = game::MissionKind::Once;
        def.minDay = 1;
        def.objectives.push_back(game::MissionObjectiveDef{});
        missions.defs.push_back(std::move(def));
    }
    ecs::MissionLog log;
    const game::MissionDef* firstMission =
        game::FindAvailableSettlementMission(first, missions, log, 1);
    const game::MissionDef* secondMission =
        game::FindAvailableSettlementMission(second, missions, log, 1);
    REQUIRE(firstMission != nullptr);
    REQUIRE(secondMission != nullptr);
    CHECK(firstMission->id == "alpha_task");
    CHECK(secondMission->id == "beta_task");

    ecs::MissionRunState run;
    run.id = "alpha_task";
    run.progress = {0};
    run.status = ecs::MissionRunStatus::Active;
    log.missions.push_back(run);
    CHECK(game::FindAvailableSettlementMission(first, missions, log, 1) ==
          nullptr);
    CHECK(game::FindAvailableSettlementMission(second, missions, log, 1) !=
          nullptr);
    CHECK(game::FindAvailableSettlementMission(second, missions, log, 1)->id ==
          "beta_task");
}

TEST_CASE("M23-F4-T3 对话条件按任务状态和物品数量求值") {
    const game::SettlementDef settlement = MakeSettlement();
    const game::MissionConfig missions = MakeMissions();
    ecs::MissionLog log;
    game::DialogueContext context;
    context.settlement = &settlement;
    context.missions = &missions;
    context.missionLog = &log;
    context.day = 1;

    game::DialogueCondition available;
    available.kind = game::DialogueConditionKind::MissionAvailable;
    game::DialogueCondition active;
    active.kind = game::DialogueConditionKind::MissionActive;
    game::DialogueCondition none;
    none.kind = game::DialogueConditionKind::MissionNone;

    CHECK(game::EvaluateDialogueCondition(available, context));
    CHECK_FALSE(game::EvaluateDialogueCondition(active, context));
    CHECK_FALSE(game::EvaluateDialogueCondition(none, context));
    CHECK(game::FindAvailableSettlementMission(
              settlement, missions, log, 1) != nullptr);
    CHECK(game::FindActiveSettlementMission(settlement, missions, log) ==
          nullptr);

    ecs::MissionRunState run;
    run.id = "test_task";
    run.progress = {0};
    run.status = ecs::MissionRunStatus::Active;
    log.missions.push_back(run);
    CHECK(game::EvaluateDialogueCondition(active, context));
    CHECK_FALSE(game::EvaluateDialogueCondition(available, context));
    CHECK(game::FindActiveSettlementMission(settlement, missions, log) !=
          nullptr);

    log.missions[0].status = ecs::MissionRunStatus::Completed;
    CHECK_FALSE(game::EvaluateDialogueCondition(active, context));
    CHECK_FALSE(game::EvaluateDialogueCondition(available, context));
    CHECK(game::EvaluateDialogueCondition(none, context));

    ecs::Container container;
    container.items.push_back(ecs::ItemInstance{"bandage", 2, 1.0f});
    container.items.push_back(ecs::ItemInstance{"bandage", 1, 1.0f});
    context.inventory = &container;
    game::DialogueCondition item;
    item.kind = game::DialogueConditionKind::ItemAtLeast;
    item.id = "bandage";
    item.count = 3;
    CHECK(game::EvaluateDialogueCondition(item, context));
    item.count = 4;
    CHECK_FALSE(game::EvaluateDialogueCondition(item, context));
    item.negate = true;
    CHECK(game::EvaluateDialogueCondition(item, context));
}

TEST_CASE("M23-F4-T3 求助者选项按委托状态过滤") {
    const game::SettlementDef settlement = MakeSettlement();
    const game::MissionConfig missions = MakeMissions();
    const game::DialogueTree tree = game::MakeSettlementDialogue(
        settlement, game::SettlementNpcKind::Requester);
    const game::DialogueNode* work = tree.FindNode("work");
    REQUIRE(work != nullptr);

    ecs::MissionLog log;
    game::DialogueContext context;
    context.settlement = &settlement;
    context.missions = &missions;
    context.missionLog = &log;
    context.day = 1;

    ecs::MissionRunState run;
    run.id = "test_task";
    run.progress = {0};
    run.status = ecs::MissionRunStatus::Completed;
    log.missions.push_back(run);
    const auto noMission = game::VisibleDialogueOptions(*work, context);
    CHECK(noMission.size() == 2u);
    CHECK(HasVisibleOption(noMission, "现在没有委托？"));
    CHECK_FALSE(HasVisibleOption(noMission, "接一件委托。"));

    log.missions[0].status = ecs::MissionRunStatus::Active;
    const auto activeMission = game::VisibleDialogueOptions(*work, context);
    CHECK(activeMission.size() == 2u);
    CHECK(HasVisibleOption(activeMission, "看看我现在的委托。"));
    CHECK_FALSE(HasVisibleOption(activeMission, "现在没有委托？"));

    log.missions.clear();
    const auto availableMission =
        game::VisibleDialogueOptions(*work, context);
    CHECK(availableMission.size() == 2u);
    CHECK(HasVisibleOption(availableMission, "接一件委托。"));
    CHECK_FALSE(HasVisibleOption(availableMission, "看看我现在的委托。"));
}

TEST_CASE("M33-F1-T6 四个聚落按 dialogueProfile 使用不同口吻") {
    const std::string root{ZL_SOURCE_DIR};
    core::Config config;
    REQUIRE(config.LoadFile(root + "/config/game.json"));
    config.Set("settlements.file", root + "/assets/settlements.json");
    game::SettlementConfig settlements;
    std::string error;
    REQUIRE_MESSAGE(game::LoadSettlementConfig(config, &settlements, &error),
                    error);

    const auto rootText = [&](std::string_view id,
                              game::SettlementNpcKind kind) {
        const game::SettlementDef* settlement = settlements.Find(id);
        REQUIRE(settlement != nullptr);
        const game::DialogueTree tree =
            game::MakeSettlementDialogue(*settlement, kind);
        const game::DialogueNode* node = tree.FindNode("root");
        REQUIRE(node != nullptr);
        return node->text;
    };
    CHECK(rootText("village_haven", game::SettlementNpcKind::Merchant)
              .find("广场上") != std::string::npos);
    CHECK(rootText("north_fields", game::SettlementNpcKind::Merchant)
              .find("消毒水") != std::string::npos);
    CHECK(rootText("south_farmland", game::SettlementNpcKind::Merchant)
              .find("谷仓") != std::string::npos);
    CHECK(rootText("river_camp", game::SettlementNpcKind::Merchant)
              .find("桥头") != std::string::npos);
    CHECK(rootText("village_haven", game::SettlementNpcKind::Requester)
              .find("棚屋") != std::string::npos);
    CHECK(rootText("north_fields", game::SettlementNpcKind::Requester)
              .find("压低声音") != std::string::npos);
    CHECK(rootText("south_farmland", game::SettlementNpcKind::Requester)
              .find("泥") != std::string::npos);
    CHECK(rootText("river_camp", game::SettlementNpcKind::Requester)
              .find("桥下") != std::string::npos);

    game::SettlementDef custom;
    custom.id = "custom_camp";
    custom.name = "自定义营地";
    custom.dialogueProfile = "custom_profile";
    const game::DialogueTree customTree =
        game::MakeSettlementDialogue(custom, game::SettlementNpcKind::Merchant);
    const game::DialogueNode* customRoot = customTree.FindNode("root");
    REQUIRE(customRoot != nullptr);
    CHECK(customRoot->text.find("广场上") == std::string::npos);
    CHECK(customRoot->text.find("消毒水") == std::string::npos);
}
