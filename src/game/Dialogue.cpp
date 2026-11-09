#include "game/Dialogue.h"

#include <algorithm>

#include "game/MissionComponents.h"
#include "game/Missions.h"
#include "items/InventoryOps.h"
#include "items/ItemRegistry.h"

namespace zl::game {
namespace {

bool SettlementPoolContains(const SettlementDef& settlement,
                            std::string_view id) {
    return std::find(settlement.missionPool.begin(),
                     settlement.missionPool.end(),
                     id) != settlement.missionPool.end();
}

const ecs::MissionRunState* PoolMissionState(
    const SettlementDef& settlement, const ecs::MissionLog& log,
    std::string_view id) {
    if (!SettlementPoolContains(settlement, id)) {
        return nullptr;
    }
    return FindMissionState(log, id);
}

bool PoolHasAvailableMission(const SettlementDef& settlement,
                             const MissionConfig& missions,
                             const ecs::MissionLog& log, int day,
                             std::string_view id) {
    for (const std::string& missionId : settlement.missionPool) {
        const ecs::MissionRunState* state = FindMissionState(log, missionId);
        if (state != nullptr &&
            state->status == ecs::MissionRunStatus::Active) {
            return false;
        }
    }
    if (!id.empty()) {
        const MissionDef* def = missions.Find(id);
        return def != nullptr && SettlementPoolContains(settlement, id) &&
               MissionCanActivate(*def, log, day);
    }
    for (const std::string& missionId : settlement.missionPool) {
        const MissionDef* def = missions.Find(missionId);
        if (def != nullptr && MissionCanActivate(*def, log, day)) {
            return true;
        }
    }
    return false;
}

bool PoolHasActiveMission(const SettlementDef& settlement,
                          const ecs::MissionLog& log,
                          std::string_view id) {
    if (!id.empty()) {
        const ecs::MissionRunState* state =
            PoolMissionState(settlement, log, id);
        return state != nullptr &&
               state->status == ecs::MissionRunStatus::Active;
    }
    for (const std::string& missionId : settlement.missionPool) {
        const ecs::MissionRunState* state =
            FindMissionState(log, missionId);
        if (state != nullptr &&
            state->status == ecs::MissionRunStatus::Active) {
            return true;
        }
    }
    return false;
}

DialogueCondition AlwaysCondition() {
    return DialogueCondition{};
}

DialogueCondition MissionCondition(DialogueConditionKind kind,
                                   bool negate = false) {
    DialogueCondition condition;
    condition.kind = kind;
    condition.negate = negate;
    return condition;
}

DialogueOption MakeOption(std::string text, DialogueResultKind kind,
                          std::string target = {},
                          DialogueCondition condition = AlwaysCondition()) {
    DialogueOption option;
    option.text = std::move(text);
    option.condition = std::move(condition);
    option.result.kind = kind;
    option.result.target = std::move(target);
    return option;
}

DialogueNode MakeNode(std::string id, std::string speaker, std::string text,
                      std::vector<DialogueOption> options) {
    DialogueNode node;
    node.id = std::move(id);
    node.speaker = std::move(speaker);
    node.text = std::move(text);
    node.options = std::move(options);
    return node;
}

}  // namespace

const DialogueNode* DialogueTree::FindNode(std::string_view nodeId) const {
    for (const DialogueNode& node : nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

bool EvaluateDialogueCondition(const DialogueCondition& condition,
                               const DialogueContext& context) {
    bool result = false;
    switch (condition.kind) {
        case DialogueConditionKind::Always:
            result = true;
            break;
        case DialogueConditionKind::MissionAvailable:
            if (context.settlement != nullptr &&
                context.missions != nullptr) {
                if (context.missionLog != nullptr) {
                    result = PoolHasAvailableMission(
                        *context.settlement, *context.missions,
                        *context.missionLog, context.day, condition.id);
                } else {
                    const ecs::MissionLog empty;
                    result = PoolHasAvailableMission(
                        *context.settlement, *context.missions, empty,
                        context.day, condition.id);
                }
            }
            break;
        case DialogueConditionKind::MissionActive:
            if (context.settlement != nullptr &&
                context.missionLog != nullptr) {
                result = PoolHasActiveMission(
                    *context.settlement, *context.missionLog, condition.id);
            }
            break;
        case DialogueConditionKind::MissionNone:
            if (context.settlement != nullptr &&
                context.missions != nullptr) {
                if (context.missionLog != nullptr) {
                    result =
                        !PoolHasActiveMission(*context.settlement,
                                              *context.missionLog, {}) &&
                        !PoolHasAvailableMission(
                            *context.settlement, *context.missions,
                            *context.missionLog, context.day, {});
                } else {
                    const ecs::MissionLog empty;
                    result =
                        !PoolHasActiveMission(*context.settlement, empty,
                                              {}) &&
                        !PoolHasAvailableMission(
                            *context.settlement, *context.missions, empty,
                            context.day, {});
                }
            }
            break;
        case DialogueConditionKind::ItemAtLeast:
            if (context.inventory != nullptr && !condition.id.empty()) {
                result = items::CountOf(*context.inventory, condition.id) >=
                         std::max(1, condition.count);
            }
            break;
    }
    return condition.negate ? !result : result;
}

std::vector<const DialogueOption*> VisibleDialogueOptions(
    const DialogueNode& node, const DialogueContext& context) {
    std::vector<const DialogueOption*> visible;
    visible.reserve(node.options.size());
    for (const DialogueOption& option : node.options) {
        if (EvaluateDialogueCondition(option.condition, context)) {
            visible.push_back(&option);
        }
    }
    return visible;
}

const MissionDef* FindAvailableSettlementMission(
    const SettlementDef& settlement, const MissionConfig& missions,
    const ecs::MissionLog& missionLog, int day) {
    if (FindActiveSettlementMission(settlement, missions, missionLog) !=
        nullptr) {
        return nullptr;
    }
    for (const std::string& missionId : settlement.missionPool) {
        const MissionDef* def = missions.Find(missionId);
        if (def != nullptr && MissionCanActivate(*def, missionLog, day)) {
            return def;
        }
    }
    return nullptr;
}

const MissionDef* FindActiveSettlementMission(
    const SettlementDef& settlement, const MissionConfig& missions,
    const ecs::MissionLog& missionLog) {
    for (const std::string& missionId : settlement.missionPool) {
        const MissionDef* def = missions.Find(missionId);
        if (def == nullptr) {
            continue;
        }
        const ecs::MissionRunState* state =
            FindMissionState(missionLog, missionId);
        if (state != nullptr &&
            state->status == ecs::MissionRunStatus::Active) {
            return def;
        }
    }
    return nullptr;
}

const char* DialogueSpeakerLabel(SettlementNpcKind kind) {
    switch (kind) {
        case SettlementNpcKind::Merchant:
            return "商人";
        case SettlementNpcKind::Guard:
            return "守卫";
        case SettlementNpcKind::Requester:
            return "求助者";
    }
    return "幸存者";
}

DialogueTree MakeSettlementDialogue(const SettlementDef& settlement,
                                    SettlementNpcKind kind) {
    DialogueTree tree;
    tree.kind = kind;
    tree.settlementId = settlement.id;
    tree.startNode = "root";
    const std::string speaker =
        std::string(DialogueSpeakerLabel(kind)) + " · " + settlement.name;

    if (kind == SettlementNpcKind::Merchant) {
        tree.nodes.push_back(MakeNode(
            "root", speaker,
            "想换点东西？药品、弹药和图纸都还有货。",
            {
                MakeOption("看看你的货。", DialogueResultKind::OpenTrade),
                MakeOption("最近有什么消息？", DialogueResultKind::Goto,
                           "news"),
                MakeOption("再见。", DialogueResultKind::Close),
            }));
        tree.nodes.push_back(MakeNode(
            "news", speaker,
            "医院和检查站都不太平。缺人手，也缺物资。去问问营地里的求助者。",
            {
                MakeOption("我去问问。", DialogueResultKind::Close),
                MakeOption("知道了。", DialogueResultKind::Close),
            }));
        return tree;
    }

    if (kind == SettlementNpcKind::Guard) {
        tree.nodes.push_back(MakeNode(
            "root", speaker,
            "营地内不准闹事。围墙外最近不太平。",
            {
                MakeOption("附近安全吗？", DialogueResultKind::Goto,
                           "status"),
                MakeOption("再见。", DialogueResultKind::Close),
            }));
        tree.nodes.push_back(MakeNode(
            "status", speaker,
            "暂时没有感染者靠近。听到警报就往营地中心撤，别在围墙外逗留。",
            {
                MakeOption("我知道了。", DialogueResultKind::Goto, "root"),
                MakeOption("再见。", DialogueResultKind::Close),
            }));
        return tree;
    }

    tree.nodes.push_back(MakeNode(
        "root", speaker,
        "你看起来能帮上忙。营地现在缺人手。",
        {
            MakeOption("有什么委托？", DialogueResultKind::Goto, "work"),
            MakeOption("我只是路过。", DialogueResultKind::Close),
        }));
    tree.nodes.push_back(MakeNode(
        "work", speaker,
        "营地周边和医院那边都有麻烦。你要看看现在的委托吗？",
        {
            MakeOption("看看我现在的委托。",
                       DialogueResultKind::FocusSettlementMission, {},
                       MissionCondition(DialogueConditionKind::MissionActive)),
            MakeOption("接一件委托。",
                       DialogueResultKind::AcceptSettlementMission, {},
                       MissionCondition(
                           DialogueConditionKind::MissionAvailable)),
            MakeOption("现在没有委托？", DialogueResultKind::Goto,
                       "no_work",
                       MissionCondition(DialogueConditionKind::MissionNone)),
            MakeOption("下次再说。", DialogueResultKind::Close),
        }));
    tree.nodes.push_back(MakeNode(
        "no_work", speaker,
        "暂时没有新的委托。过一两天再来看看，医院和检查站那边总会需要人。",
        {
            MakeOption("好。", DialogueResultKind::Close),
        }));
    return tree;
}

}  // namespace zl::game
