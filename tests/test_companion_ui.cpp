#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "characters/components/CharacterComponents.h"
#include "core/Config.h"
#include "game/Companion.h"
#include "game/CompanionUi.h"
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

game::CompanionState MakeState() {
    game::CompanionState state;
    state.loyalty = 55;
    state.command = game::CompanionCommand::Follow;
    state.inventory.capacityKg = 10.0f;
    state.inventory.items.push_back(ecs::ItemInstance{"bandage", 2, 1.0f});
    state.inventory.items.push_back(ecs::ItemInstance{"canned_soup", 1, 1.0f});
    state.equipment.slots[0] = ecs::ItemInstance{"kitchen_knife", 1, 1.0f};
    state.equipment.slots[1] = ecs::ItemInstance{"jacket", 1, 1.0f};
    return state;
}

}  // namespace

TEST_CASE("M33-F3-T7 同伴面板生命、信任、装备与背包") {
    game::CompanionConfig companions;
    items::ItemRegistry items;
    LoadGameData(&companions, &items);
    const game::CompanionDef* lin = companions.Find("companion_medic_lin");
    REQUIRE(lin != nullptr);

    game::CompanionState state = MakeState();
    game::RefreshCompanionQuestState(*lin, &state);
    ecs::Health health{20.0f, 95.0f};
    game::CompanionUiInput input;
    input.def = lin;
    input.state = &state;
    input.health = &health;
    input.items = &items;
    input.ownerDistance = 3.0f;

    const ui::CompanionUiState ui = game::BuildCompanionUi(input);
    REQUIRE(ui.valid);
    CHECK(ui.name == "林医生");
    CHECK(ui.health.find("20/95") != std::string::npos);
    CHECK(ui.trust.find("55") != std::string::npos);
    CHECK(ui.status == "跟随");
    CHECK(ui.equipment.find("厨刀") != std::string::npos);
    CHECK(ui.bag.find("绷带") != std::string::npos);
    CHECK(ui.bag.find("×2") != std::string::npos);
    CHECK(ui.quest.find("干净的绷带") != std::string::npos);
    CHECK(ui.lowHealth);
    CHECK(ui.canCommand);
    CHECK(ui.canGift);
    CHECK_FALSE(ui.downed);
    CHECK(ui.alert.find("低血") != std::string::npos);
    CHECK(ui.alertTone == "warn");
    CHECK(ui.hint.find("1 跟随") != std::string::npos);
}

TEST_CASE("M33-F3-T7 倒地、远离、卡住与待命提示") {
    game::CompanionConfig companions;
    items::ItemRegistry items;
    LoadGameData(&companions, &items);
    const game::CompanionDef* lin = companions.Find("companion_medic_lin");
    REQUIRE(lin != nullptr);

    game::CompanionState state = MakeState();
    game::RefreshCompanionQuestState(*lin, &state);
    ecs::Health health{0.0f, 95.0f};
    state.downed = true;
    state.downedRemaining = 23.0f;
    game::CompanionUiInput input;
    input.def = lin;
    input.state = &state;
    input.health = &health;
    input.items = &items;

    ui::CompanionUiState ui = game::BuildCompanionUi(input);
    REQUIRE(ui.valid);
    CHECK(ui.downed);
    CHECK(ui.canRescue);
    CHECK_FALSE(ui.canCommand);
    CHECK(ui.status.find("倒地") != std::string::npos);
    CHECK(ui.alert.find("23 秒") != std::string::npos);
    CHECK(ui.alert.find("E 救援") != std::string::npos);
    CHECK(ui.alertTone == "fail");

    state.downed = false;
    state.dead = false;
    state.standby = false;
    health.current = 80.0f;
    input.ownerDistance = 24.0f;
    ui = game::BuildCompanionUi(input);
    CHECK(ui.away);
    CHECK(ui.alert.find("远离") != std::string::npos);

    input.ownerDistance = 3.0f;
    input.stuck = true;
    ui = game::BuildCompanionUi(input);
    CHECK(ui.stuck);
    CHECK(ui.alert.find("路径受阻") != std::string::npos);

    input.stuck = false;
    state.standby = true;
    state.command = game::CompanionCommand::Stay;
    ui = game::BuildCompanionUi(input);
    CHECK(ui.standby);
    CHECK_FALSE(ui.canCommand);
    CHECK(ui.alert.find("聚落待命") != std::string::npos);

    state.standby = false;
    state.dead = true;
    health.current = 0.0f;
    ui = game::BuildCompanionUi(input);
    CHECK(ui.dead);
    CHECK(ui.alert.find("死亡") != std::string::npos);
    CHECK(ui.alertTone == "fail");
}
