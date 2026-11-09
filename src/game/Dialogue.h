#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "game/Settlement.h"

namespace zl::ecs {
struct Container;
struct MissionLog;
}  // namespace zl::ecs

namespace zl::items {
class ItemRegistry;
}  // namespace zl::items

namespace zl::game {

struct MissionConfig;
struct MissionDef;

enum class DialogueConditionKind : std::uint8_t {
    Always = 0,
    MissionAvailable = 1,
    MissionActive = 2,
    MissionNone = 3,
    ItemAtLeast = 4,
};

struct DialogueCondition {
    DialogueConditionKind kind = DialogueConditionKind::Always;
    std::string id;
    int count = 1;
    bool negate = false;
};

enum class DialogueResultKind : std::uint8_t {
    None = 0,
    Goto = 1,
    Close = 2,
    OpenTrade = 3,
    AcceptSettlementMission = 4,
    FocusSettlementMission = 5,
};

struct DialogueResult {
    DialogueResultKind kind = DialogueResultKind::None;
    std::string target;
};

struct DialogueOption {
    std::string text;
    DialogueCondition condition;
    DialogueResult result;
};

struct DialogueNode {
    std::string id;
    std::string speaker;
    std::string text;
    std::vector<DialogueOption> options;
};

struct DialogueTree {
    SettlementNpcKind kind = SettlementNpcKind::Guard;
    std::string settlementId;
    std::string startNode;
    std::vector<DialogueNode> nodes;

    const DialogueNode* FindNode(std::string_view nodeId) const;
};

struct DialogueContext {
    const SettlementDef* settlement = nullptr;
    const MissionConfig* missions = nullptr;
    const ecs::MissionLog* missionLog = nullptr;
    const ecs::Container* inventory = nullptr;
    const items::ItemRegistry* items = nullptr;
    int day = 1;
};

bool EvaluateDialogueCondition(const DialogueCondition& condition,
                               const DialogueContext& context);
std::vector<const DialogueOption*> VisibleDialogueOptions(
    const DialogueNode& node, const DialogueContext& context);

const MissionDef* FindAvailableSettlementMission(
    const SettlementDef& settlement, const MissionConfig& missions,
    const ecs::MissionLog& missionLog, int day);
const MissionDef* FindActiveSettlementMission(
    const SettlementDef& settlement, const MissionConfig& missions,
    const ecs::MissionLog& missionLog);

const char* DialogueSpeakerLabel(SettlementNpcKind kind);
DialogueTree MakeSettlementDialogue(const SettlementDef& settlement,
                                    SettlementNpcKind kind);

}  // namespace zl::game
