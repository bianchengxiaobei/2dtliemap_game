#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "core/Config.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/Companion.h"
#include "game/CompanionSystem.h"
#include "game/Dialogue.h"
#include "items/InventoryOps.h"
#include "items/ItemRegistry.h"

using namespace zl;

namespace {

void LoadGameData(game::CompanionConfig* companions,
                  items::ItemRegistry* items) {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    core::Config config;
    REQUIRE(config.LoadFile(root / "config" / "game.json"));
    config.Set("companions.file",
               (root / "assets" / "companions.json").generic_string());
    REQUIRE(items->LoadFile(root / "assets" / "items.json"));
    std::string error;
    REQUIRE_MESSAGE(
        game::LoadCompanionConfig(config, companions, &error, items), error);
    REQUIRE(companions->enabled);
}

ecs::Entity AddOwner(ecs::EcsWorld& world, float x, float y) {
    const ecs::Entity entity = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(entity, ecs::Position{x, y, 0});
    registry.emplace<ecs::Velocity>(entity, ecs::Velocity{});
    registry.emplace<ecs::Health>(entity, ecs::Health{100.0f, 100.0f});
    registry.emplace<ecs::Facing>(entity, ecs::Facing{});
    registry.emplace<ecs::PlayerTag>(entity);
    registry.emplace<ecs::ActionState>(entity, ecs::ActionState{});
    registry.emplace<ecs::AttackIntent>(entity, ecs::AttackIntent{});
    registry.emplace<ecs::MoveIntent>(entity, ecs::MoveIntent{});
    return entity;
}

ecs::Entity AddCompanion(ecs::EcsWorld& world, ecs::Entity owner, float x,
                         float y, game::CompanionState state) {
    const ecs::Entity entity = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(entity, ecs::Position{x, y, 0});
    registry.emplace<ecs::Velocity>(entity, ecs::Velocity{});
    registry.emplace<ecs::Health>(entity, ecs::Health{95.0f, 95.0f});
    registry.emplace<ecs::Facing>(entity, ecs::Facing{});
    registry.emplace<ecs::AnimState>(entity, ecs::AnimState{});
    registry.emplace<ecs::ActionState>(entity, ecs::ActionState{});
    registry.emplace<ecs::MoveIntent>(entity, ecs::MoveIntent{});
    registry.emplace<ecs::PathRequest>(entity, ecs::PathRequest{});
    ecs::PathFollow follow;
    follow.speed = 3.6f;
    registry.emplace<ecs::PathFollow>(entity, follow);
    registry.emplace<ecs::FriendlyNpc>(entity);
    registry.emplace<ecs::NoCombatTarget>(entity);
    registry.emplace<game::Companion>(
        entity, game::Companion{"companion_medic_lin",
                                world.IdOf(owner).id, "village_haven"});
    registry.emplace<game::CompanionState>(entity, std::move(state));
    game::CompanionAiState ai;
    ai.anchorX = x;
    ai.anchorY = y;
    ai.homeX = x;
    ai.homeY = y;
    registry.emplace<game::CompanionAiState>(entity, ai);
    return entity;
}

ecs::Entity AddZombie(ecs::EcsWorld& world, float x, float y, float health) {
    const ecs::Entity entity = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(entity, ecs::Position{x, y, 0});
    registry.emplace<ecs::Health>(entity, ecs::Health{health, health});
    registry.emplace<ecs::AiState>(entity, ecs::AiState{});
    registry.emplace<ecs::PerceptionState>(entity, ecs::PerceptionState{});
    registry.emplace<ecs::MoveIntent>(entity, ecs::MoveIntent{});
    return entity;
}

game::CompanionState MakeState(int trust) {
    game::CompanionState state;
    state.loyalty = trust;
    state.inventory.capacityKg = 20.0f;
    state.equipment.slots[1] =
        ecs::ItemInstance{"jacket", 1, 1.0f, 0};
    return state;
}

bool HasOption(const std::vector<const game::DialogueOption*>& options,
               std::string_view text) {
    return std::any_of(options.begin(), options.end(),
                       [&](const game::DialogueOption* option) {
                           return option->text == text;
                       });
}

}  // namespace

TEST_CASE("M33-F3-T5 信任边界与跟随战斗") {
    items::ItemRegistry items;
    game::CompanionConfig companions;
    LoadGameData(&companions, &items);
    const game::CompanionDef* lin = companions.Find("companion_medic_lin");
    REQUIRE(lin != nullptr);

    CHECK(game::CompanionTrustTierForScore(0) ==
          game::CompanionTrustTier::Wary);
    CHECK(game::CompanionTrustTierForScore(24) ==
          game::CompanionTrustTier::Wary);
    CHECK(game::CompanionTrustTierForScore(25) ==
          game::CompanionTrustTier::Steady);
    CHECK(game::CompanionTrustTierForScore(49) ==
          game::CompanionTrustTier::Steady);
    CHECK(game::CompanionTrustTierForScore(50) ==
          game::CompanionTrustTier::Trusted);
    CHECK(game::CompanionTrustTierForScore(74) ==
          game::CompanionTrustTier::Trusted);
    CHECK(game::CompanionTrustTierForScore(75) ==
          game::CompanionTrustTier::Bonded);
    CHECK(game::CompanionTrustTierForScore(100) ==
          game::CompanionTrustTier::Bonded);
    CHECK(std::string(game::CompanionTrustTierLabel(
              game::CompanionTrustTier::Bonded)) == "生死之交");

    game::CompanionState bounds = MakeState(95);
    CHECK(game::AddCompanionTrust(&bounds, 10) == 5);
    CHECK(bounds.loyalty == 100);
    CHECK(game::AddCompanionTrust(&bounds, 5) == 0);
    CHECK(game::AddCompanionTrust(&bounds, -200) == -100);
    CHECK(bounds.loyalty == 0);

    game::CompanionState questLocked = MakeState(44);
    game::RefreshCompanionQuestState(*lin, &questLocked);
    CHECK(questLocked.personalStatus == game::CompanionQuestStatus::Locked);
    questLocked.loyalty = 45;
    game::RefreshCompanionQuestState(*lin, &questLocked);
    CHECK(questLocked.personalStatus ==
          game::CompanionQuestStatus::Available);

    ecs::EcsWorld world;
    const ecs::Entity owner = AddOwner(world, 10.0f, 10.0f);
    const ecs::Entity zombie = AddZombie(world, 11.2f, 10.0f, 1.0f);
    game::CompanionState initial = MakeState(55);
    const ecs::Entity companion =
        AddCompanion(world, owner, 10.5f, 10.0f, std::move(initial));
    game::CompanionState& state =
        world.Registry().get<game::CompanionState>(companion);
    game::RefreshCompanionQuestState(*lin, &state);
    REQUIRE(game::StartCompanionQuest(*lin, &state));
    CHECK(state.personalStatus == game::CompanionQuestStatus::Active);
    ecs::Container donor;
    donor.capacityKg = 5.0f;
    donor.items.push_back(ecs::ItemInstance{"bandage", 2, 1.0f, 0});
    std::string giftError;
    REQUIRE(game::GiftToCompanion(world, companion, *lin, items, &donor, 0, 1,
                                  &giftError));
    CHECK(items::CountOf(donor, "bandage") == 1);
    CHECK(state.loyalty == 57);
    CHECK(state.personalProgress == 1);
    REQUIRE(game::GiftToCompanion(world, companion, *lin, items, &donor, 0, 1,
                                  &giftError));
    CHECK(items::CountOf(donor, "bandage") == 0);
    CHECK(items::CountOf(state.inventory, "bandage") == 2);
    CHECK(state.personalStage == 1);
    CHECK(state.personalStatus == game::CompanionQuestStatus::Available);
    CHECK(state.loyalty == 65);
    const int trustBefore = state.loyalty;
    REQUIRE(game::StartCompanionQuest(*lin, &state));

    game::CompanionSystem system(
        {}, [&world](ecs::Entity, ecs::Entity target, float damage,
                     ecs::DamageType) {
            auto* health = world.Registry().try_get<ecs::Health>(target);
            if (health == nullptr || health->current <= 0.0f) {
                return false;
            }
            health->current = std::max(0.0f, health->current - damage);
            return true;
        });
    system.SetItemRegistry(&items);
    system.SetCompanionConfig(&companions);
    system.Update(world, 1.0f / 60.0f);
    CHECK(system.GetStats().kills == 1);
    CHECK(state.loyalty == trustBefore + 2);
    CHECK(state.personalProgress == 1);
    CHECK(world.Registry().get<ecs::Health>(zombie).current == 0.0f);
}

TEST_CASE("M33-F3-T5 个人委托前置、完成、失败与防重复奖励") {
    items::ItemRegistry items;
    game::CompanionConfig companions;
    LoadGameData(&companions, &items);
    const game::CompanionDef* lin = companions.Find("companion_medic_lin");
    REQUIRE(lin != nullptr);

    game::CompanionState state = MakeState(55);
    game::RefreshCompanionQuestState(*lin, &state);
    CHECK(state.personalStatus == game::CompanionQuestStatus::Available);

    std::string error;
    REQUIRE(game::StartCompanionQuest(*lin, &state, &error));
    REQUIRE(game::RecordCompanionQuestEvent(
        *lin, &state, game::CompanionQuestObjectiveKind::Gift, "bandage", 1,
        &items, &error));
    CHECK(state.personalProgress == 1);
    REQUIRE(game::FailCompanionQuest(*lin, &state, &error));
    CHECK(state.personalStatus == game::CompanionQuestStatus::Failed);
    CHECK(state.personalProgress == 0);
    CHECK(state.extraTraits.empty());
    CHECK(state.loyalty == 55);

    REQUIRE(game::StartCompanionQuest(*lin, &state, &error));
    REQUIRE(game::RecordCompanionQuestEvent(
        *lin, &state, game::CompanionQuestObjectiveKind::Gift, "bandage", 2,
        &items, &error));
    CHECK(state.personalStage == 1);
    CHECK(state.personalStatus == game::CompanionQuestStatus::Available);
    CHECK(state.personalRewardMask == 1u);
    CHECK(state.loyalty == 61);
    REQUIRE(std::find(state.extraTraits.begin(), state.extraTraits.end(),
                      "triage_drills") != state.extraTraits.end());
    REQUIRE(state.equipment.slots[1].has_value());
    CHECK(state.equipment.slots[1]->defId == "jacket");
    CHECK(items::CountOf(state.inventory, "first_aid_kit") == 1);

    const int trustAfterFirst = state.loyalty;
    const std::size_t traitsAfterFirst = state.extraTraits.size();
    REQUIRE(game::StartCompanionQuest(*lin, &state, &error));
    CHECK(state.personalStatus == game::CompanionQuestStatus::Active);
    REQUIRE(game::RecordCompanionQuestEvent(
        *lin, &state, game::CompanionQuestObjectiveKind::Kill, "any", 3,
        &items, &error));
    CHECK(state.personalStage == 2);
    CHECK(state.personalStatus == game::CompanionQuestStatus::Completed);
    CHECK(state.personalRewardMask == 3u);
    CHECK(state.loyalty == trustAfterFirst + 8);
    CHECK(state.extraTraits.size() == traitsAfterFirst + 1);
    CHECK(std::find(state.extraTraits.begin(), state.extraTraits.end(),
                    "steady_hands_ii") != state.extraTraits.end());
    REQUIRE(state.equipment.slots[1].has_value());
    CHECK(state.equipment.slots[1]->defId == "leather_jacket");
    CHECK(items::CountOf(state.inventory, "jacket") == 1);

    const int trustAfterSecond = state.loyalty;
    const std::size_t traitsAfterSecond = state.extraTraits.size();
    CHECK_FALSE(game::RecordCompanionQuestEvent(
        *lin, &state, game::CompanionQuestObjectiveKind::Kill, "any", 3,
        &items, &error));
    CHECK(state.loyalty == trustAfterSecond);
    CHECK(state.extraTraits.size() == traitsAfterSecond);
    CHECK_FALSE(game::StartCompanionQuest(*lin, &state, &error));
    CHECK(error.find("已完成") != std::string::npos);
}

TEST_CASE("M33-F3-T5 同伴对话随信任、低血、倒地与委托状态变化") {
    items::ItemRegistry items;
    game::CompanionConfig companions;
    LoadGameData(&companions, &items);
    const game::CompanionDef* lin = companions.Find("companion_medic_lin");
    REQUIRE(lin != nullptr);

    game::CompanionState state = MakeState(55);
    game::RefreshCompanionQuestState(*lin, &state);
    ecs::Health health{95.0f, 95.0f};
    game::DialogueContext context;
    context.companionDef = lin;
    context.companionState = &state;
    context.companionHealth = &health;

    game::DialogueTree tree = game::MakeCompanionDialogue(*lin, state, health);
    REQUIRE(tree.FindNode("root") != nullptr);
    auto options = game::VisibleDialogueOptions(*tree.FindNode("root"), context);
    CHECK(tree.FindNode("root")->text.find("把身边的位置") !=
          std::string::npos);
    CHECK(HasOption(options, "你看起来有心事。"));
    CHECK(HasOption(options, "随便聊聊。"));
    CHECK_FALSE(HasOption(options, "你托付的事，怎么样了？"));

    state.loyalty = 10;
    tree = game::MakeCompanionDialogue(*lin, state, health);
    CHECK(tree.FindNode("root")->text.find("手上的动作") !=
          std::string::npos);
    state.loyalty = 80;
    tree = game::MakeCompanionDialogue(*lin, state, health);
    CHECK(tree.FindNode("root")->text.find("先看了看你的伤") !=
          std::string::npos);
    state.loyalty = 55;
    tree = game::MakeCompanionDialogue(*lin, state, health);

    state.personalStatus = game::CompanionQuestStatus::Active;
    tree = game::MakeCompanionDialogue(*lin, state, health);
    options = game::VisibleDialogueOptions(*tree.FindNode("root"), context);
    CHECK(HasOption(options, "你托付的事，怎么样了？"));
    CHECK_FALSE(HasOption(options, "你看起来有心事。"));
    const game::DialogueNode* questNode = tree.FindNode("quest");
    REQUIRE(questNode != nullptr);
    CHECK_FALSE(HasOption(
        game::VisibleDialogueOptions(*questNode, context), "我帮你。"));

    state.personalStatus = game::CompanionQuestStatus::Failed;
    tree = game::MakeCompanionDialogue(*lin, state, health);
    options = game::VisibleDialogueOptions(*tree.FindNode("root"), context);
    CHECK(HasOption(options, "上次的事，我想再试一次。"));
    questNode = tree.FindNode("quest");
    REQUIRE(questNode != nullptr);
    CHECK(HasOption(game::VisibleDialogueOptions(*questNode, context),
                    "我帮你。"));

    state.personalStage = 2;
    state.personalStatus = game::CompanionQuestStatus::Completed;
    tree = game::MakeCompanionDialogue(*lin, state, health);
    options = game::VisibleDialogueOptions(*tree.FindNode("root"), context);
    CHECK(HasOption(options, "你看起来轻松了些。"));

    state.personalStage = 0;
    state.personalStatus = game::CompanionQuestStatus::Locked;
    health.current = 20.0f;
    tree = game::MakeCompanionDialogue(*lin, state, health);
    options = game::VisibleDialogueOptions(*tree.FindNode("root"), context);
    CHECK(HasOption(options, "你受伤了？"));
    REQUIRE(tree.FindNode("hurt") != nullptr);
    CHECK(tree.FindNode("hurt")->text.find("皮外伤") != std::string::npos);

    health.current = 0.0f;
    state.downed = true;
    tree = game::MakeCompanionDialogue(*lin, state, health);
    CHECK(tree.FindNode("root")->text.find("按住伤口") != std::string::npos);
    options = game::VisibleDialogueOptions(*tree.FindNode("root"), context);
    CHECK_FALSE(HasOption(options, "随便聊聊。"));

    state.downed = false;
    state.dead = true;
    tree = game::MakeCompanionDialogue(*lin, state, health);
    CHECK(tree.FindNode("root")->text.find("没有了呼吸") !=
          std::string::npos);
}
