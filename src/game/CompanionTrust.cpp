#include "game/Companion.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"
#include "items/InventoryOps.h"
#include "items/ItemDef.h"
#include "items/ItemRegistry.h"

namespace zl::game {
namespace {

bool SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
    return false;
}

bool ValidTraitId(std::string_view value) {
    return IsValidCompanionToken(value) &&
           value.size() <= kMaxCompanionTraitBytes;
}

bool IsTraitPresent(const CompanionState& state, std::string_view trait) {
    return std::find(state.extraTraits.begin(), state.extraTraits.end(),
                     trait) != state.extraTraits.end();
}

bool ObjectiveMatches(const CompanionQuestObjectiveDef& objective,
                      CompanionQuestObjectiveKind kind,
                      std::string_view target) {
    if (objective.kind != kind) {
        return false;
    }
    if (kind == CompanionQuestObjectiveKind::Kill) {
        return objective.target.empty() || objective.target == "any" ||
               objective.target == target;
    }
    return !objective.target.empty() && objective.target == target;
}

bool ApplyQuestItemReward(const CompanionQuestStageDef& stage,
                          CompanionState* state,
                          const items::ItemRegistry* items) {
    if (stage.rewardItem.empty() || stage.rewardItemCount <= 0 ||
        items == nullptr || items->Find(stage.rewardItem) == nullptr) {
        return true;
    }
    const ecs::ItemInstance reward{
        stage.rewardItem, std::max(1, stage.rewardItemCount), 1.0f, 0};
    ecs::Container inventory = state->inventory;
    ecs::Equipment equipment = state->equipment;
    const auto equipSlot = [&]() -> std::optional<ecs::EquipSlot> {
        if (stage.rewardEquipSlot == "primary") {
            return ecs::EquipSlot::PrimaryHand;
        }
        if (stage.rewardEquipSlot == "body") {
            return ecs::EquipSlot::Body;
        }
        return std::nullopt;
    }();

    if (equipSlot.has_value()) {
        ecs::ItemInstance equipped = reward;
        equipped.count = 1;
        ecs::ItemInstance swapped;
        if (items::Equip(equipment, *items, *equipSlot, equipped, &swapped)) {
            bool fits = true;
            if (swapped.Valid()) {
                fits = items::AddItem(inventory, *items, swapped) ==
                       swapped.count;
            }
            if (fits && reward.count > 1) {
                ecs::ItemInstance rest = reward;
                rest.count = reward.count - 1;
                fits = items::AddItem(inventory, *items, rest) == rest.count;
            }
            if (fits) {
                state->inventory = std::move(inventory);
                state->equipment = std::move(equipment);
                return true;
            }
        }
    }

    if (items::AddItem(inventory, *items, reward) == reward.count) {
        state->inventory = std::move(inventory);
        return true;
    }
    return false;
}

void CompleteQuestStage(const CompanionDef& def, CompanionState* state,
                        const items::ItemRegistry* items) {
    if (state == nullptr || state->personalStage < 0 ||
        static_cast<std::size_t>(state->personalStage) >=
            def.personalQuest.size()) {
        return;
    }
    const std::size_t stageIndex =
        static_cast<std::size_t>(state->personalStage);
    const CompanionQuestStageDef& stage = def.personalQuest[stageIndex];
    const std::uint32_t bit = 1u << stageIndex;
    if ((state->personalRewardMask & bit) != 0) {
        return;
    }
    state->personalRewardMask |= bit;
    AddCompanionTrust(state, stage.rewardTrust);
    if (!stage.rewardTrait.empty() && ValidTraitId(stage.rewardTrait) &&
        !IsTraitPresent(*state, stage.rewardTrait) &&
        state->extraTraits.size() < kMaxCompanionTraits) {
        state->extraTraits.push_back(stage.rewardTrait);
    }
    (void)ApplyQuestItemReward(stage, state, items);
    ++state->personalStage;
    state->personalProgress = 0;
    state->personalStatus = CompanionQuestStatus::Locked;
    RefreshCompanionQuestState(def, state);
}

}  // namespace

CompanionTrustTier CompanionTrustTierForScore(int score) {
    const int value = std::clamp(score, 0, 100);
    if (value < 25) {
        return CompanionTrustTier::Wary;
    }
    if (value < 50) {
        return CompanionTrustTier::Steady;
    }
    if (value < 75) {
        return CompanionTrustTier::Trusted;
    }
    return CompanionTrustTier::Bonded;
}

const char* CompanionTrustTierName(CompanionTrustTier tier) {
    switch (tier) {
        case CompanionTrustTier::Wary:
            return "wary";
        case CompanionTrustTier::Steady:
            return "steady";
        case CompanionTrustTier::Trusted:
            return "trusted";
        case CompanionTrustTier::Bonded:
            return "bonded";
    }
    return "wary";
}

const char* CompanionTrustTierLabel(CompanionTrustTier tier) {
    switch (tier) {
        case CompanionTrustTier::Wary:
            return "警惕";
        case CompanionTrustTier::Steady:
            return "平稳";
        case CompanionTrustTier::Trusted:
            return "信任";
        case CompanionTrustTier::Bonded:
            return "生死之交";
    }
    return "警惕";
}

int AddCompanionTrust(CompanionState* state, int delta) {
    if (state == nullptr) {
        return 0;
    }
    const int before = std::clamp(state->loyalty, 0, 100);
    state->loyalty = std::clamp(before + delta, 0, 100);
    return state->loyalty - before;
}

const CompanionQuestStageDef* CurrentCompanionQuestStage(
    const CompanionDef& def, const CompanionState& state) {
    if (state.personalStage < 0 ||
        static_cast<std::size_t>(state.personalStage) >=
            def.personalQuest.size()) {
        return nullptr;
    }
    return &def.personalQuest[static_cast<std::size_t>(state.personalStage)];
}

CompanionQuestStatus CompanionQuestStatusFor(const CompanionDef& def,
                                             const CompanionState& state) {
    if (def.personalQuest.empty()) {
        return CompanionQuestStatus::Locked;
    }
    if (state.personalStage < 0 ||
        static_cast<std::size_t>(state.personalStage) >=
            def.personalQuest.size()) {
        return CompanionQuestStatus::Completed;
    }
    if (state.personalStatus == CompanionQuestStatus::Active ||
        state.personalStatus == CompanionQuestStatus::Failed) {
        return state.personalStatus;
    }
    const CompanionQuestStageDef* stage =
        CurrentCompanionQuestStage(def, state);
    if (stage != nullptr && state.loyalty >= stage->minTrust) {
        return CompanionQuestStatus::Available;
    }
    return CompanionQuestStatus::Locked;
}

void RefreshCompanionQuestState(const CompanionDef& def,
                                CompanionState* state) {
    if (state == nullptr || def.personalQuest.empty()) {
        return;
    }
    if (state->personalStatus == CompanionQuestStatus::Active ||
        state->personalStatus == CompanionQuestStatus::Failed) {
        return;
    }
    state->personalStatus = CompanionQuestStatusFor(def, *state);
}

bool StartCompanionQuest(const CompanionDef& def, CompanionState* state,
                         std::string* error) {
    if (state == nullptr) {
        return SetError(error, "同伴状态无效");
    }
    if (state->dead || state->downed) {
        return SetError(error, "倒地/死亡同伴无法接取个人委托");
    }
    RefreshCompanionQuestState(def, state);
    const CompanionQuestStatus status = CompanionQuestStatusFor(def, *state);
    if (status != CompanionQuestStatus::Available &&
        status != CompanionQuestStatus::Failed) {
        return SetError(error, status == CompanionQuestStatus::Completed
                                   ? "个人委托链已完成"
                                   : "信任不足或当前没有可接委托");
    }
    const CompanionQuestStageDef* stage =
        CurrentCompanionQuestStage(def, *state);
    if (stage == nullptr) {
        return SetError(error, "个人委托阶段无效");
    }
    static_cast<void>(stage);
    state->personalStatus = CompanionQuestStatus::Active;
    state->personalProgress = 0;
    return true;
}

bool FailCompanionQuest(const CompanionDef& def, CompanionState* state,
                        std::string* error) {
    if (state == nullptr) {
        return SetError(error, "同伴状态无效");
    }
    static_cast<void>(def);
    if (state->personalStatus != CompanionQuestStatus::Active) {
        return SetError(error, "当前没有进行中的个人委托");
    }
    state->personalStatus = CompanionQuestStatus::Failed;
    state->personalProgress = 0;
    return true;
}

bool RecordCompanionQuestEvent(const CompanionDef& def, CompanionState* state,
                               CompanionQuestObjectiveKind kind,
                               std::string_view target, int amount,
                               const items::ItemRegistry* items,
                               std::string* error) {
    if (state == nullptr || amount <= 0) {
        return false;
    }
    if (state->personalStatus != CompanionQuestStatus::Active) {
        return false;
    }
    const CompanionQuestStageDef* stage =
        CurrentCompanionQuestStage(def, *state);
    if (stage == nullptr ||
        !ObjectiveMatches(stage->objective, kind, target)) {
        return false;
    }
    state->personalProgress = std::min(
        std::max(0, stage->objective.count),
        state->personalProgress + amount);
    if (state->personalProgress < stage->objective.count) {
        return true;
    }
    CompleteQuestStage(def, state, items);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool GiftToCompanion(ecs::EcsWorld& world, ecs::Entity companion,
                     const CompanionDef& def, const items::ItemRegistry& items,
                     ecs::Container* donor, std::size_t index, int count,
                     std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (donor == nullptr || count <= 0) {
        return SetError(error, "赠送物品或数量无效");
    }
    if (!world.Alive(companion)) {
        return SetError(error, "同伴不存在");
    }
    entt::registry& registry = world.Registry();
    auto* state = registry.try_get<CompanionState>(companion);
    if (state == nullptr) {
        return SetError(error, "同伴状态无效");
    }
    if (state->dead || state->downed) {
        return SetError(error, "倒地/死亡同伴无法接受赠礼");
    }
    ecs::ItemInstance taken;
    if (!items::TakeAt(*donor, index, count, &taken) || taken.count <= 0) {
        return SetError(error, "玩家背包物品不足");
    }
    if (items.Find(taken.defId) == nullptr) {
        (void)items::AddItem(*donor, items, taken);
        return SetError(error, "赠送物品不存在");
    }
    ecs::Container inventory = state->inventory;
    if (items::AddItem(inventory, items, taken) != taken.count) {
        (void)items::AddItem(*donor, items, taken);
        return SetError(error, "同伴背包空间不足");
    }
    state->inventory = std::move(inventory);
    AddCompanionTrust(state, kCompanionGiftTrust);
    (void)RecordCompanionQuestEvent(
        def, state, CompanionQuestObjectiveKind::Gift, taken.defId, taken.count,
        &items, error);
    return true;
}

}  // namespace zl::game
