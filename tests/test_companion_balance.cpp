#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>

#include "characters/components/CharacterComponents.h"
#include "core/Config.h"
#include "game/Companion.h"
#include "game/CompanionSystem.h"
#include "game/Dialogue.h"
#include "items/ItemRegistry.h"

using namespace zl;

namespace {

struct BalanceData {
    core::Config config;
    items::ItemRegistry items;
    game::CompanionConfig companions;
};

bool LoadBalanceData(BalanceData* data, std::string* error) {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    if (!data->config.LoadFile(root / "config" / "game.json")) {
        *error = "failed to load game config";
        return false;
    }
    data->config.Set("companions.file",
                     (root / "assets" / "companions.json").generic_string());
    if (!data->items.LoadFile(root / "assets" / "items.json")) {
        *error = "failed to load items";
        return false;
    }
    return game::LoadCompanionConfig(data->config, &data->companions, error,
                                     &data->items);
}

game::CompanionState StateWithPrimary(const game::CompanionDef& def) {
    game::CompanionState state;
    state.loyalty = def.startingLoyalty;
    if (!def.equipment.primary.empty()) {
        state.equipment.slots[0] =
            ecs::ItemInstance{def.equipment.primary, 1, 1.0f, 0};
    }
    if (!def.equipment.body.empty()) {
        state.equipment.slots[1] =
            ecs::ItemInstance{def.equipment.body, 1, 1.0f, 0};
    }
    return state;
}

bool Contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("M33-F3-T8 内置同伴内容差异与招募条件") {
    BalanceData data;
    std::string error;
    REQUIRE_MESSAGE(LoadBalanceData(&data, &error), error);
    REQUIRE(data.companions.defs.size() == 2u);

    const game::CompanionDef* lin =
        data.companions.Find("companion_medic_lin");
    const game::CompanionDef* amo =
        data.companions.Find("companion_scout_amo");
    REQUIRE(lin != nullptr);
    REQUIRE(amo != nullptr);
    CHECK(lin->name == "林医生");
    CHECK(amo->name == "阿默");
    CHECK(lin->traits != amo->traits);
    CHECK(lin->equipment.primary != amo->equipment.primary);
    CHECK(lin->equipment.body != amo->equipment.body);
    CHECK(lin->dialogueProfile == "companion_lin");
    CHECK(amo->dialogueProfile == "companion_amo");
    CHECK(lin->dialogueProfile != amo->dialogueProfile);
    CHECK(lin->recruit.requiredMission != amo->recruit.requiredMission);
    CHECK(lin->recruit.requiredItem != amo->recruit.requiredItem);
    CHECK(lin->recruit.requiredItemCount != amo->recruit.requiredItemCount);

    REQUIRE(lin->personalQuest.size() == 2u);
    REQUIRE(amo->personalQuest.size() == 2u);
    CHECK(lin->personalQuest[0].objective.kind ==
          game::CompanionQuestObjectiveKind::Gift);
    CHECK(lin->personalQuest[1].objective.kind ==
          game::CompanionQuestObjectiveKind::Kill);
    CHECK(amo->personalQuest[0].objective.kind ==
          game::CompanionQuestObjectiveKind::Gift);
    CHECK(amo->personalQuest[1].objective.kind ==
          game::CompanionQuestObjectiveKind::Kill);
    CHECK(lin->personalQuest[0].objective.target !=
          amo->personalQuest[0].objective.target);
    CHECK(lin->personalQuest[0].rewardTrait !=
          amo->personalQuest[0].rewardTrait);
}

TEST_CASE("M33-F3-T8 同伴 DPS 为玩家的 30% 到 50%") {
    BalanceData data;
    std::string error;
    REQUIRE_MESSAGE(LoadBalanceData(&data, &error), error);
    const game::CompanionSystem::Options options =
        game::LoadCompanionSystemOptions(data.config);
    CHECK(options.attackDamageScale == doctest::Approx(0.38f));
    CHECK(options.attackCooldownScale == doctest::Approx(1.20f));
    CHECK(options.downedSeconds == doctest::Approx(60.0f));

    for (const game::CompanionDef& def : data.companions.defs) {
        REQUIRE_FALSE(def.equipment.primary.empty());
        const items::ItemDef* weapon =
            data.items.Find(def.equipment.primary);
        REQUIRE(weapon != nullptr);
        REQUIRE(weapon->combat.damage.has_value());
        REQUIRE(weapon->combat.cooldownSeconds.has_value());

        const game::CompanionState state = StateWithPrimary(def);
        const game::CompanionAttackProfile profile =
            game::CompanionAttackProfileFor(state, &data.items, options);
        const float playerDps = *weapon->combat.damage /
                                *weapon->combat.cooldownSeconds;
        const float companionDps = profile.damage / profile.cooldown;
        const float ratio = companionDps / playerDps;
        MESSAGE(def.id << " companion=" << companionDps
                       << " player=" << playerDps << " ratio=" << ratio);
        CHECK(ratio >= 0.30f);
        CHECK(ratio <= 0.50f);
    }

    const game::CompanionState unarmed;
    const game::CompanionAttackProfile unarmedProfile =
        game::CompanionAttackProfileFor(unarmed, &data.items, options);
    const float playerBareHandsDps =
        static_cast<float>(
            data.config.Get<double>("combat.playerDamage", 40.0)) /
        static_cast<float>(
            data.config.Get<double>("combat.playerCooldownSeconds", 0.5));
    const float unarmedRatio =
        (unarmedProfile.damage / unarmedProfile.cooldown) /
        playerBareHandsDps;
    CHECK(unarmedRatio >= 0.30f);
    CHECK(unarmedRatio <= 0.50f);
}

TEST_CASE("M33-F3-T8 同伴对话差异与受伤倒地状态") {
    BalanceData data;
    std::string error;
    REQUIRE_MESSAGE(LoadBalanceData(&data, &error), error);
    const game::CompanionDef* lin =
        data.companions.Find("companion_medic_lin");
    const game::CompanionDef* amo =
        data.companions.Find("companion_scout_amo");
    REQUIRE(lin != nullptr);
    REQUIRE(amo != nullptr);

    game::CompanionState state;
    state.loyalty = 55;
    ecs::Health health{95.0f, 95.0f};
    const game::DialogueTree linTree =
        game::MakeCompanionDialogue(*lin, state, health);
    const game::DialogueTree amoTree =
        game::MakeCompanionDialogue(*amo, state, health);
    const game::DialogueNode* linTalk = linTree.FindNode("talk");
    const game::DialogueNode* amoTalk = amoTree.FindNode("talk");
    REQUIRE(linTalk != nullptr);
    REQUIRE(amoTalk != nullptr);
    MESSAGE("lin talk=" << linTalk->text);
    MESSAGE("amo talk=" << amoTalk->text);
    CHECK(linTalk->text != amoTalk->text);
    CHECK(Contains(linTalk->text, "低头救人"));
    CHECK(Contains(amoTalk->text, "地图给你"));

    health.current = 20.0f;
    const game::DialogueTree linHurt =
        game::MakeCompanionDialogue(*lin, state, health);
    const game::DialogueTree amoHurt =
        game::MakeCompanionDialogue(*amo, state, health);
    REQUIRE(linHurt.FindNode("hurt") != nullptr);
    REQUIRE(amoHurt.FindNode("hurt") != nullptr);
    CHECK(Contains(linHurt.FindNode("hurt")->text, "皮外伤"));
    CHECK(Contains(amoHurt.FindNode("hurt")->text, "擦破点皮"));

    health.current = 0.0f;
    state.downed = true;
    const game::DialogueTree downed =
        game::MakeCompanionDialogue(*lin, state, health);
    REQUIRE(downed.FindNode("root") != nullptr);
    CHECK(Contains(downed.FindNode("root")->text, "按住伤口"));
    const auto options = game::VisibleDialogueOptions(
        *downed.FindNode("root"), game::DialogueContext{});
    CHECK(options.size() == 1u);
}
