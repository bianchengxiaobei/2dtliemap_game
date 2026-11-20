#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "characters/components/CharacterComponents.h"
#include "core/Config.h"
#include "ecs/EcsWorld.h"
#include "game/Companion.h"
#include "game/Dialogue.h"
#include "game/MissionComponents.h"
#include "game/Settlement.h"
#include "game/SettlementReputation.h"
#include "items/InventoryOps.h"
#include "items/ItemRegistry.h"

using namespace zl;

namespace {

struct TempRoot {
    std::filesystem::path path;

    explicit TempRoot(const std::string& name)
        : path(std::filesystem::temp_directory_path() / "zl_companions" /
               name) {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path, ec);
    }

    ~TempRoot() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

void WriteText(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << text;
}

std::string CompanionDocument(std::string_view defs) {
    return std::string(R"({"format":"companions-v1","defs":[)") +
           std::string(defs) + "]}";
}

game::CompanionState MakeState() {
    game::CompanionState state;
    state.loyalty = 55;
    state.command = game::CompanionCommand::Follow;
    state.inventory.capacityKg = 10.0f;
    state.inventory.items.push_back(
        ecs::ItemInstance{"bandage", 2, 1.0f, 0});
    state.equipment.slots[0] =
        ecs::ItemInstance{"kitchen_knife", 1, 1.0f, 0};
    state.equipment.slots[1] = ecs::ItemInstance{"jacket", 1, 1.0f, 0};
    return state;
}

}  // namespace

TEST_CASE("M33-F3-T1 同伴数据契约、默认值与实际内容") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    core::Config config;
    REQUIRE(config.LoadFile(root / "config" / "game.json"));
    config.Set("companions.file",
               (root / "assets" / "companions.json").generic_string());
    items::ItemRegistry items;
    REQUIRE(items.LoadFile(root / "assets" / "items.json"));

    game::CompanionConfig companions;
    std::string error;
    REQUIRE_MESSAGE(
        game::LoadCompanionConfig(config, &companions, &error, &items), error);
    CHECK(companions.enabled);
    REQUIRE(companions.defs.size() == 2u);

    const game::CompanionDef* medic =
        companions.Find("companion_medic_lin");
    REQUIRE(medic != nullptr);
    CHECK(medic->name == "林医生");
    CHECK_FALSE(medic->background.empty());
    CHECK(medic->baseHealth == doctest::Approx(95.0f));
    CHECK(medic->startingLoyalty == 55);
    CHECK(medic->equipment.primary == "kitchen_knife");
    CHECK(medic->equipment.body == "jacket");
    CHECK(medic->inventoryCapacityKg == doctest::Approx(10.0f));
    REQUIRE(medic->inventory.size() == 2u);
    CHECK(medic->inventory[0].item == "bandage");
    CHECK(medic->inventory[0].count == 2);
    REQUIRE(medic->traits.size() == 2u);
    CHECK(medic->traits[0] == "field_medic");
    CHECK(medic->recruit.minReputationTier ==
          game::SettlementReputationTier::Friendly);
    CHECK(medic->recruit.requiredMission == "settlement_help");
    CHECK(medic->recruit.requiredItem == "bandage");
    CHECK(medic->recruit.requiredItemCount == 2);
    CHECK(medic->dialogueProfile == "companion_lin");

    const game::CompanionDef* scout =
        companions.Find("companion_scout_amo");
    REQUIRE(scout != nullptr);
    CHECK(scout->name == "阿默");
    CHECK(scout->baseHealth == doctest::Approx(115.0f));
    CHECK(scout->equipment.primary == "makeshift_spear");
    CHECK(scout->recruit.requiredMission == "south_harvest_help");

    game::Companion companion{"companion_medic_lin", 42, "village_haven"};
    game::CompanionState state = MakeState();
    ecs::Health health{95.0f, 95.0f};
    CHECK(game::CompanionCommandName(game::CompanionCommand::Follow) ==
          std::string("follow"));
    game::CompanionCommand parsed = game::CompanionCommand::Follow;
    CHECK(game::ParseCompanionCommand("retreat", &parsed));
    CHECK(parsed == game::CompanionCommand::Retreat);
    CHECK_FALSE(game::ParseCompanionCommand("rts", &parsed));
    CHECK(game::ValidateCompanionComponents(companion, state, health, &items,
                                             &error));

    game::CompanionState bad = state;
    bad.loyalty = 101;
    CHECK_FALSE(game::ValidateCompanionComponents(companion, bad, health,
                                                  &items, &error));
    bad = state;
    bad.downed = true;
    bad.dead = true;
    CHECK_FALSE(game::ValidateCompanionComponents(companion, bad, health,
                                                  &items, &error));
    bad = state;
    bad.inventory.items[0].defId = "missing_item";
    CHECK_FALSE(game::ValidateCompanionComponents(companion, bad, health,
                                                  &items, &error));
    bad = state;
    bad.downedRemaining = 1.0f;
    CHECK_FALSE(game::ValidateCompanionComponents(companion, bad, health,
                                                  &items, &error));
    CHECK_FALSE(game::ValidateCompanionComponents(
        game::Companion{"", 42, "village_haven"}, state, health, &items,
        &error));

    core::Config defaultConfig;
    const TempRoot temp("defaults");
    const std::filesystem::path defaultFile = temp.path / "companions.json";
    WriteText(defaultFile, R"({
      "format": "companions-v1",
      "defs": [{ "id": "default_companion", "name": "默认同伴" }]
    })");
    defaultConfig.Set("companions", nlohmann::json{
                                          {"enabled", true},
                                          {"file", defaultFile.generic_string()},
                                      });
    game::CompanionConfig defaults;
    error.clear();
    REQUIRE(game::LoadCompanionConfig(defaultConfig, &defaults, &error,
                                      &items));
    REQUIRE(defaults.defs.size() == 1u);
    const game::CompanionDef& def = defaults.defs.front();
    CHECK(def.baseHealth == doctest::Approx(100.0f));
    CHECK(def.inventoryCapacityKg == doctest::Approx(12.0f));
    CHECK(def.startingLoyalty == 50);
    CHECK(def.equipment.primary.empty());
    CHECK(def.equipment.body.empty());
    CHECK(def.inventory.empty());
    CHECK(def.traits.empty());
    CHECK(def.recruit.minReputationTier ==
          game::SettlementReputationTier::Friendly);
    CHECK(def.recruit.requiredItem.empty());
    CHECK(def.recruit.requiredItemCount == 0);
    CHECK(def.dialogueProfile == "default_companion");

    core::Config disabled;
    game::CompanionConfig empty;
    REQUIRE(game::LoadCompanionConfig(disabled, &empty, &error));
    CHECK_FALSE(empty.enabled);
    CHECK(empty.defs.empty());
}

TEST_CASE("M33-F3-T1 同伴非法字段与 mod 追加") {
    items::ItemRegistry items;
    REQUIRE(items.LoadFile(std::string(ZL_SOURCE_DIR) + "/assets/items.json"));

    game::CompanionConfig companions;
    companions.enabled = true;
    game::CompanionMergeStats stats;
    std::string error;
    const std::string valid =
        R"({"id":"valid_a","name":"有效 A"})";
    const std::string validB =
        R"({"id":"valid_b","name":"有效 B","baseHealth":80,"traits":["scout"]})";
    const std::string badId =
        R"({"id":"BadId","name":"坏 id"})";
    const std::string badHealth =
        R"({"id":"bad_health","name":"坏生命","baseHealth":0})";
    const std::string badItem =
        R"({"id":"bad_item","name":"坏物品","inventory":[{"item":"missing_item"}]})";
    const std::string badTrait =
        R"({"id":"bad_trait","name":"坏特长","traits":["scout","scout"]})";
    const std::string badTier =
        R"({"id":"bad_tier","name":"坏声望","recruit":{"minReputationTier":"best"}})";
    const std::string badCost =
        R"({"id":"bad_cost","name":"坏消耗","recruit":{"requiredItemCount":1}})";
    const std::string duplicate =
        R"({"id":"valid_a","name":"重复"})";
    const std::string badSlot =
        R"({"id":"bad_slot","name":"坏槽位","equipment":{"hat":"jacket"}})";
    const std::string badStack =
        R"({"id":"bad_stack","name":"坏堆叠","inventory":[{"item":"bandage","count":0}]})";

    REQUIRE(game::MergeCompanionJson(
        CompanionDocument(valid + "," + validB + "," + badId + "," +
                          badHealth + "," + badItem + "," + badTrait + "," +
                          badTier + "," + badCost + "," + duplicate + "," +
                          badSlot + "," + badStack),
        "mixed", &companions, &stats, &error, &items));
    CHECK(companions.Find("valid_a") != nullptr);
    CHECK(companions.Find("valid_b") != nullptr);
    CHECK(companions.Find("bad_id") == nullptr);
    CHECK(companions.Find("bad_health") == nullptr);
    CHECK(companions.Find("bad_item") == nullptr);
    CHECK(companions.Find("bad_trait") == nullptr);
    CHECK(companions.Find("bad_tier") == nullptr);
    CHECK(companions.Find("bad_cost") == nullptr);
    CHECK(companions.Find("bad_slot") == nullptr);
    CHECK(companions.Find("bad_stack") == nullptr);
    CHECK(stats.defsAdded == 2);
    CHECK(stats.defsSkipped >= 9);

    const std::string mod =
        R"({"id":"mod_companion","name":"模组同伴","equipment":{"primary":"machete"},"inventory":[{"item":"bandage","count":1}],"traits":["cool_headed"],"recruit":{"minReputationTier":"neutral","requiredMission":"mod_mission","requiredItem":"bandage","requiredItemCount":1}})";
    REQUIRE(game::MergeCompanionJson(CompanionDocument(mod), "mod",
                                     &companions, &stats, &error, &items));
    const game::CompanionDef* added = companions.Find("mod_companion");
    REQUIRE(added != nullptr);
    CHECK(added->equipment.primary == "machete");
    CHECK(added->recruit.minReputationTier ==
          game::SettlementReputationTier::Neutral);
    CHECK(stats.filesLoaded == 2);
    CHECK(stats.defsAdded == 3);

    game::CompanionConfig badFormat;
    badFormat.enabled = true;
    CHECK_FALSE(game::MergeCompanionJson(
        R"({"format":"companions-v2","defs":[]})", "bad format", &badFormat,
        nullptr, &error, &items));
    CHECK(error.find("companions-v1") != std::string::npos);

    game::CompanionConfig emptyDoc;
    emptyDoc.enabled = true;
    CHECK_FALSE(game::MergeCompanionJson(CompanionDocument(""), "empty",
                                         &emptyDoc, nullptr, &error, &items));

    const TempRoot temp("missing");
    core::Config missingConfig;
    missingConfig.Set("companions", nlohmann::json{
                                         {"enabled", true},
                                         {"file", (temp.path / "missing.json")
                                                      .generic_string()},
                                     });
    game::CompanionConfig missing;
    CHECK_FALSE(game::LoadCompanionConfig(missingConfig, &missing, &error,
                                          &items));
}

TEST_CASE("M33-F3-T2 聚落招募资格、物资与唯一性") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    core::Config config;
    REQUIRE(config.LoadFile(root / "config" / "game.json"));
    config.Set("companions.file",
               (root / "assets" / "companions.json").generic_string());
    config.Set("settlements.file",
               (root / "assets" / "settlements.json").generic_string());
    items::ItemRegistry items;
    REQUIRE(items.LoadFile(root / "assets" / "items.json"));
    game::CompanionConfig companions;
    std::string error;
    REQUIRE_MESSAGE(
        game::LoadCompanionConfig(config, &companions, &error, &items), error);
    game::SettlementConfig settlements;
    REQUIRE_MESSAGE(
        game::LoadSettlementConfig(config, &settlements, &error), error);
    const game::SettlementDef* village =
        settlements.Find("village_haven");
    const game::SettlementDef* farm = settlements.Find("south_farmland");
    const game::SettlementDef* clinic = settlements.Find("north_fields");
    REQUIRE(village != nullptr);
    REQUIRE(farm != nullptr);
    REQUIRE(clinic != nullptr);

    CHECK(game::FindSettlementCompanion(companions, *village) != nullptr);
    CHECK(game::FindSettlementCompanion(companions, *village)->id ==
          "companion_medic_lin");
    CHECK(game::FindSettlementCompanion(companions, *farm)->id ==
          "companion_scout_amo");
    CHECK(game::FindSettlementCompanion(companions, *clinic) == nullptr);

    ecs::MissionLog log;
    ecs::MissionRunState mission;
    mission.id = "settlement_help";
    mission.status = ecs::MissionRunStatus::Completed;
    mission.day = 1;
    mission.rewardClaimed = true;
    mission.settlementId = "village_haven";
    log.missions.push_back(mission);

    ecs::Container inventory;
    inventory.items.push_back(ecs::ItemInstance{"bandage", 2, 1.0f});
    game::CompanionRecruitCheck check = game::EvaluateCompanionRecruitment(
        companions, *village, settlements.reputation, 20, &log, &inventory,
        false);
    CHECK(check.status == game::CompanionRecruitStatus::Ready);
    REQUIRE(check.def != nullptr);
    CHECK(check.def->id == "companion_medic_lin");
    CHECK(check.itemCount == 2);

    check = game::EvaluateCompanionRecruitment(
        companions, *village, settlements.reputation, 19, &log, &inventory,
        false);
    CHECK(check.status == game::CompanionRecruitStatus::ReputationTooLow);

    check = game::EvaluateCompanionRecruitment(
        companions, *village, settlements.reputation, 20, nullptr, &inventory,
        false);
    CHECK(check.status == game::CompanionRecruitStatus::MissionRequired);

    ecs::Container oneBandage;
    oneBandage.items.push_back(ecs::ItemInstance{"bandage", 1, 1.0f});
    check = game::EvaluateCompanionRecruitment(
        companions, *village, settlements.reputation, 20, &log, &oneBandage,
        false);
    CHECK(check.status == game::CompanionRecruitStatus::ItemRequired);
    CHECK(check.itemCount == 1);

    check = game::EvaluateCompanionRecruitment(
        companions, *village, settlements.reputation, 20, &log, &inventory,
        true);
    CHECK(check.status == game::CompanionRecruitStatus::AlreadyRecruited);

    check = game::EvaluateCompanionRecruitment(
        companions, *clinic, settlements.reputation, 100, &log, &inventory,
        false);
    CHECK(check.status ==
          game::CompanionRecruitStatus::ServiceUnavailable);

    ecs::Container cost = inventory;
    const game::CompanionDef* medic =
        companions.Find("companion_medic_lin");
    REQUIRE(medic != nullptr);
    CHECK(game::ConsumeCompanionRecruitItems(&cost, *medic));
    CHECK(items::CountOf(cost, "bandage") == 0);
    ecs::Container tooLittle = oneBandage;
    CHECK_FALSE(game::ConsumeCompanionRecruitItems(&tooLittle, *medic));
    CHECK(items::CountOf(tooLittle, "bandage") == 1);

    game::CompanionConfig noCost;
    noCost.enabled = true;
    game::CompanionMergeStats stats;
    REQUIRE(game::MergeCompanionJson(
        CompanionDocument(R"({"id":"free_helper","name":"免费同伴"})"),
        "free", &noCost, &stats, &error, &items));
    REQUIRE(noCost.Find("free_helper") != nullptr);
    CHECK(game::ConsumeCompanionRecruitItems(
        nullptr, *noCost.Find("free_helper")));
}

TEST_CASE("M33-F3-T2 对话入口、待命与归队条件") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    core::Config config;
    REQUIRE(config.LoadFile(root / "config" / "game.json"));
    config.Set("companions.file",
               (root / "assets" / "companions.json").generic_string());
    config.Set("settlements.file",
               (root / "assets" / "settlements.json").generic_string());
    items::ItemRegistry items;
    REQUIRE(items.LoadFile(root / "assets" / "items.json"));
    game::CompanionConfig companions;
    game::SettlementConfig settlements;
    std::string error;
    REQUIRE_MESSAGE(
        game::LoadCompanionConfig(config, &companions, &error, &items), error);
    REQUIRE_MESSAGE(
        game::LoadSettlementConfig(config, &settlements, &error), error);
    const game::SettlementDef* village =
        settlements.Find("village_haven");
    const game::SettlementDef* farm = settlements.Find("south_farmland");
    REQUIRE(village != nullptr);
    REQUIRE(farm != nullptr);

    const game::DialogueTree tree = game::MakeSettlementDialogue(
        *village, game::SettlementNpcKind::Requester,
        settlements.reputation, 20);
    const game::DialogueNode* rootNode = tree.FindNode("root");
    REQUIRE(rootNode != nullptr);

    game::DialogueContext context;
    context.settlement = village;
    context.companions = &companions;
    context.settlementReputation = 20;
    const auto hasResult = [](const std::vector<const game::DialogueOption*>&
                                  options,
                              game::DialogueResultKind kind,
                              std::string_view text) {
        return std::any_of(
            options.begin(), options.end(),
            [&](const game::DialogueOption* option) {
                return option->result.kind == kind && option->text == text;
            });
    };

    auto options = game::VisibleDialogueOptions(*rootNode, context);
    CHECK(hasResult(options, game::DialogueResultKind::RecruitCompanion,
                    "招募一名同伴。"));

    context.hasCompanion = true;
    context.companionStandby = false;
    context.companionHomeSettlementId = "village_haven";
    options = game::VisibleDialogueOptions(*rootNode, context);
    CHECK_FALSE(hasResult(options,
                          game::DialogueResultKind::RecruitCompanion,
                          "招募一名同伴。"));
    CHECK(hasResult(options,
                    game::DialogueResultKind::DismissCompanion,
                    "让同伴留在聚落待命。"));

    context.companionStandby = true;
    options = game::VisibleDialogueOptions(*rootNode, context);
    CHECK(hasResult(options, game::DialogueResultKind::RecruitCompanion,
                    "让同伴归队。"));
    CHECK_FALSE(hasResult(options,
                          game::DialogueResultKind::DismissCompanion,
                          "让同伴留在聚落待命。"));

    context.companionStandby = false;
    context.companionHomeSettlementId = "south_farmland";
    options = game::VisibleDialogueOptions(*rootNode, context);
    CHECK_FALSE(hasResult(options,
                          game::DialogueResultKind::RecruitCompanion,
                          "让同伴归队。"));
    CHECK_FALSE(hasResult(options,
                          game::DialogueResultKind::DismissCompanion,
                          "让同伴留在聚落待命。"));
    CHECK(farm->id == "south_farmland");
}

TEST_CASE("M33-F3-T2 同伴归属查询与待命状态") {
    ecs::EcsWorld world;
    const ecs::Entity owned = world.Create();
    const ecs::Entity other = world.Create();
    REQUIRE(owned != ecs::EcsWorld::kNull);
    REQUIRE(other != ecs::EcsWorld::kNull);
    world.Registry().emplace<game::Companion>(
        owned, game::Companion{"companion_medic_lin", 42, "village_haven"});
    game::CompanionState state;
    state.standby = true;
    world.Registry().emplace<game::CompanionState>(owned, state);
    world.Registry().emplace<game::Companion>(
        other, game::Companion{"companion_scout_amo", 99, "south_farmland"});
    world.Registry().emplace<game::CompanionState>(other);

    CHECK(game::FindOwnedCompanion(world, 42) == owned);
    CHECK(game::FindOwnedCompanion(world, 99) == other);
    CHECK(game::FindOwnedCompanion(world, 7) == ecs::EcsWorld::kNull);
    CHECK(game::FindOwnedCompanion(world, 0) == ecs::EcsWorld::kNull);
    CHECK(world.Registry().get<game::CompanionState>(owned).standby);
}
