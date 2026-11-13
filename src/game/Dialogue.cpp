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
                             int settlementReputation,
                             std::string_view id) {
    if (!HasSettlementService(settlement, SettlementService::Missions)) {
        return false;
    }
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
               MissionCanActivate(*def, log, day) &&
               MissionMeetsSettlementReputation(*def,
                                                settlementReputation);
    }
    for (const std::string& missionId : settlement.missionPool) {
        const MissionDef* def = missions.Find(missionId);
        if (def != nullptr && MissionCanActivate(*def, log, day) &&
            MissionMeetsSettlementReputation(*def, settlementReputation)) {
            return true;
        }
    }
    return false;
}

bool PoolHasActiveMission(const SettlementDef& settlement,
                          const ecs::MissionLog& log, std::string_view id);

bool PoolHasLockedMission(const SettlementDef& settlement,
                          const MissionConfig& missions,
                          const ecs::MissionLog& log, int day,
                          int settlementReputation) {
    if (!HasSettlementService(settlement, SettlementService::Missions) ||
        PoolHasAvailableMission(settlement, missions, log, day,
                                settlementReputation, {}) ||
        PoolHasActiveMission(settlement, log, {})) {
        return false;
    }
    for (const std::string& missionId : settlement.missionPool) {
        const MissionDef* def = missions.Find(missionId);
        if (def != nullptr && MissionCanActivate(*def, log, day) &&
            !MissionMeetsSettlementReputation(*def, settlementReputation)) {
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
                        *context.missionLog, context.day,
                        context.settlementReputation, condition.id);
                } else {
                    const ecs::MissionLog empty;
                    result = PoolHasAvailableMission(
                        *context.settlement, *context.missions, empty,
                        context.day, context.settlementReputation,
                        condition.id);
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
                            *context.missionLog, context.day,
                            context.settlementReputation, {}) &&
                        !PoolHasLockedMission(
                            *context.settlement, *context.missions,
                            *context.missionLog, context.day,
                            context.settlementReputation);
                } else {
                    const ecs::MissionLog empty;
                    result =
                        !PoolHasActiveMission(*context.settlement, empty,
                                              {}) &&
                        !PoolHasAvailableMission(
                            *context.settlement, *context.missions, empty,
                            context.day, context.settlementReputation, {}) &&
                        !PoolHasLockedMission(
                            *context.settlement, *context.missions, empty,
                            context.day, context.settlementReputation);
                }
            }
            break;
        case DialogueConditionKind::MissionLocked:
            if (context.settlement != nullptr &&
                context.missions != nullptr) {
                if (context.missionLog != nullptr) {
                    result = PoolHasLockedMission(
                        *context.settlement, *context.missions,
                        *context.missionLog, context.day,
                        context.settlementReputation);
                } else {
                    const ecs::MissionLog empty;
                    result = PoolHasLockedMission(
                        *context.settlement, *context.missions, empty,
                        context.day, context.settlementReputation);
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
    const ecs::MissionLog& missionLog, int day,
    int settlementReputation) {
    if (!HasSettlementService(settlement, SettlementService::Missions) ||
        FindActiveSettlementMission(settlement, missions, missionLog) !=
            nullptr) {
        return nullptr;
    }
    for (const std::string& missionId : settlement.missionPool) {
        const MissionDef* def = missions.Find(missionId);
        if (def != nullptr && MissionCanActivate(*def, missionLog, day) &&
            MissionMeetsSettlementReputation(*def,
                                             settlementReputation)) {
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

namespace {

std::string ProfileOpening(std::string_view profile, SettlementNpcKind kind) {
    if (profile == "village_haven") {
        switch (kind) {
            case SettlementNpcKind::Merchant:
                return "广场上有人朝你点头。";
            case SettlementNpcKind::Guard:
                return "守卫朝你抬了抬下巴。";
            case SettlementNpcKind::Requester:
                return "棚屋边围着几个低声说话的人。";
        }
    }
    if (profile == "north_fields") {
        switch (kind) {
            case SettlementNpcKind::Merchant:
                return "诊所里消毒水味很重。";
            case SettlementNpcKind::Guard:
                return "守卫靠在诊所门边，眼下发青。";
            case SettlementNpcKind::Requester:
                return "她压低声音，像怕惊动病房里的人。";
        }
    }
    if (profile == "south_farmland") {
        switch (kind) {
            case SettlementNpcKind::Merchant:
                return "谷仓门口堆着刚收的作物。";
            case SettlementNpcKind::Guard:
                return "守卫把草叉立在脚边。";
            case SettlementNpcKind::Requester:
                return "她手上还沾着泥。";
        }
    }
    if (profile == "river_camp") {
        switch (kind) {
            case SettlementNpcKind::Merchant:
                return "桥头的风带着河腥味。";
            case SettlementNpcKind::Guard:
                return "守卫听着桥下的水声。";
            case SettlementNpcKind::Requester:
                return "她望了一眼桥下的水。";
        }
    }
    return {};
}

std::string WithProfileOpening(std::string text, std::string_view profile,
                               SettlementNpcKind kind) {
    const std::string opening = ProfileOpening(profile, kind);
    if (opening.empty()) {
        return text;
    }
    return opening + "\n" + text;
}

SettlementReputationTier DialogueTier(
    const SettlementReputationConfig& reputation, int score) {
    if (!reputation.enabled) {
        return SettlementReputationTier::Neutral;
    }
    const SettlementReputationTierDef* tier =
        SettlementReputationTierForScore(reputation, score);
    return tier != nullptr ? tier->tier : SettlementReputationTier::Neutral;
}

std::string MerchantRootText(SettlementReputationTier tier, bool trade) {
    if (tier == SettlementReputationTier::Hostile) {
        return "我们不和敌视者做买卖。走吧，别在这里逗留。";
    }
    if (!trade) {
        return "我们这里不对外交易。缺的东西去别处找吧。";
    }
    switch (tier) {
        case SettlementReputationTier::Hostile:
            return "我们不和敌视者做买卖。走吧，别在这里逗留。";
        case SettlementReputationTier::Wary:
            return "想换东西？先让我看看你有没有诚意。";
        case SettlementReputationTier::Neutral:
            return "想换点东西？药品、弹药和图纸都还有货。";
        case SettlementReputationTier::Friendly:
            return "你来了。今天有些好货，给你留着。";
        case SettlementReputationTier::Trusted:
            return "老朋友，随便看。给你的价不会和别人一样。";
    }
    return {};
}

std::string GuardRootText(SettlementReputationTier tier, bool defense) {
    if (!defense) {
        return "这里没有常备守卫。别指望出事时有人来救你。";
    }
    switch (tier) {
        case SettlementReputationTier::Hostile:
            return "站住。别在营地惹事，我们盯着你。";
        case SettlementReputationTier::Wary:
            return "有事快说，别在围墙边乱晃。";
        case SettlementReputationTier::Neutral:
            return "营地内不准闹事。围墙外最近不太平。";
        case SettlementReputationTier::Friendly:
            return "是你啊。营地最近还算安稳，多谢你帮忙。";
        case SettlementReputationTier::Trusted:
            return "有你在，大家心里踏实。需要什么就说。";
    }
    return {};
}

std::string GuardStatusText(SettlementReputationTier tier, bool defense) {
    if (!defense) {
        return "这里不设常备防御。警报响了只能自己找地方躲。";
    }
    if (tier == SettlementReputationTier::Hostile) {
        return "暂时没有感染者靠近。别惹事，我们会盯着你。";
    }
    return "暂时没有感染者靠近。听到警报就往营地中心撤，别在围墙外逗留。";
}

std::string RequesterRootText(SettlementReputationTier tier, bool missions) {
    if (tier == SettlementReputationTier::Hostile) {
        return "你还有脸来？这里没有给你的委托。先离开营地。";
    }
    if (!missions) {
        return "这里不对外发布委托。去别的聚落问问吧。";
    }
    switch (tier) {
        case SettlementReputationTier::Hostile:
            return "你还有脸来？这里没有给你的委托。先离开营地。";
        case SettlementReputationTier::Wary:
            return "想找活干？先证明你靠得住。";
        case SettlementReputationTier::Neutral:
            return "你看起来能帮上忙。营地现在缺人手。";
        case SettlementReputationTier::Friendly:
            return "你来得正好。营地有些事正需要可靠的人。";
        case SettlementReputationTier::Trusted:
            return "你办事，我放心。挑一件吧。";
    }
    return {};
}

std::string RequesterWorkText(SettlementReputationTier tier) {
    switch (tier) {
        case SettlementReputationTier::Hostile:
            return "这里没有给你的委托。";
        case SettlementReputationTier::Wary:
            return "活倒是有，但你现在的名声还不够。先从简单的做起吧。";
        case SettlementReputationTier::Neutral:
            return "营地周边和医院那边都有麻烦。你要看看现在的委托吗？";
        case SettlementReputationTier::Friendly:
            return "有几件事想托付给你。看看哪个合适。";
        case SettlementReputationTier::Trusted:
            return "这些事交给你我放心。需要什么尽管说。";
    }
    return {};
}

std::string RequesterNoWorkText(SettlementReputationTier tier) {
    if (tier == SettlementReputationTier::Trusted) {
        return "暂时没有新的委托。先歇一会儿，有消息我第一时间找你。";
    }
    return "暂时没有新的委托。过一两天再来看看，医院和检查站那边总会需要人。";
}

DialogueTree BuildSettlementDialogue(
    const SettlementDef& settlement, SettlementNpcKind kind,
    const SettlementReputationConfig& reputation, int score) {
    DialogueTree tree;
    tree.kind = kind;
    tree.settlementId = settlement.id;
    tree.startNode = "root";
    const std::string speaker =
        std::string(DialogueSpeakerLabel(kind)) + " · " + settlement.name;
    const SettlementReputationTier tier = DialogueTier(reputation, score);
    const bool trade =
        SettlementCanUseService(settlement, reputation,
                                SettlementService::Trade, score);
    const bool missions =
        SettlementCanUseService(settlement, reputation,
                                SettlementService::Missions, score);
    const bool defense =
        SettlementCanUseService(settlement, reputation,
                                SettlementService::Defense, score);

    if (kind == SettlementNpcKind::Merchant) {
        std::vector<DialogueOption> options;
        if (trade) {
            options.push_back(
                MakeOption("看看你的货。", DialogueResultKind::OpenTrade));
        }
        options.push_back(MakeOption("最近有什么消息？",
                                     DialogueResultKind::Goto, "news"));
        options.push_back(MakeOption("再见。", DialogueResultKind::Close));
        tree.nodes.push_back(MakeNode(
            "root", speaker,
            WithProfileOpening(MerchantRootText(tier, trade),
                               settlement.dialogueProfile,
                               SettlementNpcKind::Merchant),
            std::move(options)));
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
            WithProfileOpening(GuardRootText(tier, defense),
                               settlement.dialogueProfile,
                               SettlementNpcKind::Guard),
            {
                MakeOption("附近安全吗？", DialogueResultKind::Goto,
                           "status"),
                MakeOption("再见。", DialogueResultKind::Close),
            }));
        tree.nodes.push_back(MakeNode(
            "status", speaker, GuardStatusText(tier, defense),
            {
                MakeOption("我知道了。", DialogueResultKind::Goto, "root"),
                MakeOption("再见。", DialogueResultKind::Close),
            }));
        return tree;
    }

    std::vector<DialogueOption> rootOptions;
    if (missions) {
        rootOptions.push_back(MakeOption("有什么委托？",
                                         DialogueResultKind::Goto, "work"));
    }
    rootOptions.push_back(
        MakeOption("我只是路过。", DialogueResultKind::Close));
    tree.nodes.push_back(MakeNode(
        "root", speaker,
        WithProfileOpening(RequesterRootText(tier, missions),
                           settlement.dialogueProfile,
                           SettlementNpcKind::Requester),
        std::move(rootOptions)));
    tree.nodes.push_back(MakeNode(
        "work", speaker,
        WithProfileOpening(RequesterWorkText(tier),
                           settlement.dialogueProfile,
                           SettlementNpcKind::Requester),
        {
            MakeOption("看看我现在的委托。",
                       DialogueResultKind::FocusSettlementMission, {},
                       MissionCondition(DialogueConditionKind::MissionActive)),
            MakeOption("接一件委托。",
                       DialogueResultKind::AcceptSettlementMission, {},
                       MissionCondition(
                           DialogueConditionKind::MissionAvailable)),
            MakeOption("声望不足？", DialogueResultKind::Goto, "low_rep",
                       MissionCondition(DialogueConditionKind::MissionLocked)),
            MakeOption("现在没有委托？", DialogueResultKind::Goto,
                       "no_work",
                       MissionCondition(DialogueConditionKind::MissionNone)),
            MakeOption("下次再说。", DialogueResultKind::Close),
        }));
    tree.nodes.push_back(MakeNode(
        "low_rep", speaker,
        "他们还不信任你。先通过交易或完成其他委托提高声望，再回来。",
        {
            MakeOption("知道了。", DialogueResultKind::Close),
        }));
    tree.nodes.push_back(MakeNode(
        "no_work", speaker, RequesterNoWorkText(tier),
        {
            MakeOption("好。", DialogueResultKind::Close),
        }));
    return tree;
}

}  // namespace

DialogueTree MakeSettlementDialogue(const SettlementDef& settlement,
                                    SettlementNpcKind kind) {
    return BuildSettlementDialogue(settlement, kind,
                                   SettlementReputationConfig{}, 0);
}

DialogueTree MakeSettlementDialogue(const SettlementDef& settlement,
                                    SettlementNpcKind kind,
                                    const SettlementReputationConfig& reputation,
                                    int score) {
    return BuildSettlementDialogue(settlement, kind, reputation, score);
}

}  // namespace zl::game
