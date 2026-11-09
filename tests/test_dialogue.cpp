#include <doctest/doctest.h>

#include <string>
#include <vector>

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
