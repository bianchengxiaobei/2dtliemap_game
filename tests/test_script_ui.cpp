#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <RmlUi/Core.h>
#include <doctest/doctest.h>

#include "core/EventBus.h"
#include "core/GameEvents.h"
#include "game/GameContext.h"
#include "items/ItemRegistry.h"
#include "save/SaveTypes.h"
#include "script/LuaRuntime.h"
#include "script/ScriptApi.h"
#include "ui/ScriptUiHost.h"

using namespace zl;

namespace {

class ScriptUiNullRenderInterface final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>,
                                                Rml::Span<const int>) override {
        return 0;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f,
                        Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String&) override {
        dimensions = Rml::Vector2i(0, 0);
        return 0;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override {
        return 0;
    }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};

class ScriptUiNullSystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override { return 0.0; }
    bool LogMessage(Rml::Log::Type, const Rml::String&) override { return true; }
};

struct TempMod {
    std::filesystem::path root;

    TempMod() {
        root = std::filesystem::temp_directory_path() / "zl_script_ui_test";
        std::error_code error;
        std::filesystem::remove_all(root, error);
        std::filesystem::create_directories(root / "ui", error);
        std::ofstream out(root / "ui" / "panel.rml", std::ios::binary);
        out << R"rml(<rml>
<head><style>
body { width: 200px; height: 200px; }
#btn { position: absolute; left: 0; top: 0; width: 80px; height: 40px; }
#drag { position: absolute; left: 0; top: 60px; width: 80px; height: 40px;
        background-color: #333333; drag: clone; }
</style></head>
<body><div id="title">old</div><button id="btn">click</button>
<div id="drag">drag</div></body>
</rml>)rml";
    }

    ~TempMod() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

}  // namespace

TEST_CASE("Script UI host loads, updates, unloads and routes mouse events") {
    ScriptUiNullRenderInterface renderInterface;
    ScriptUiNullSystemInterface systemInterface;
    Rml::SetRenderInterface(&renderInterface);
    Rml::SetSystemInterface(&systemInterface);
    REQUIRE(Rml::Initialise());

    Rml::Context* context = Rml::CreateContext("script_ui_test", Rml::Vector2i(200, 200));
    REQUIRE(context != nullptr);

    TempMod mod;
    core::EventBus eventBus;
    std::vector<core::UiEvent> capturedEvents;
    eventBus.Subscribe(core::kOnUiEvent, [&capturedEvents](const core::Event& event) {
        if (const auto* ui = std::get_if<core::UiEvent>(&event.payload); ui != nullptr) {
            capturedEvents.push_back(*ui);
        }
    });

    zl::ui::ScriptUiHost host(*context);
    host.SetEventBus(&eventBus);
    REQUIRE(host.LoadDocument("test_mod", mod.root, "panel", "ui/panel.rml", true, false,
                              "test"));
    CHECK(host.IsLoaded("test_mod", "panel"));
    CHECK(host.IsVisible("test_mod", "panel"));
    CHECK(host.SetText("test_mod", "panel", "title", "<hello>"));
    CHECK(host.SetProperty("test_mod", "panel", "title", "color", "red"));
    Rml::Element* title = context->GetDocument(0) != nullptr
                              ? context->GetDocument(0)->GetElementById("title")
                              : nullptr;
    REQUIRE(title != nullptr);
    CHECK(title->GetInnerRML() == "&lt;hello&gt;");
    context->Update();
    const Rml::Property* color = title->GetProperty("color");
    REQUIRE(color != nullptr);
    CHECK_MESSAGE(color->ToString() == "#ff0000", color->ToString());

    context->Update();
    context->ProcessMouseMove(40, 20, 0);
    context->ProcessMouseButtonDown(0, 0);
    context->ProcessMouseButtonUp(0, 0);
    context->Update();
    host.EndFrame();

    REQUIRE_FALSE(capturedEvents.empty());
    const auto click = std::find_if(capturedEvents.begin(), capturedEvents.end(),
                                    [](const core::UiEvent& event) {
                                        return event.type == "click";
                                    });
    REQUIRE(click != capturedEvents.end());
    CHECK(click->modId == "test_mod");
    CHECK(click->documentId == "panel");
    CHECK(click->elementId == "btn");
    CHECK(host.InputConsumedThisFrame());

    capturedEvents.clear();
    host.BeginFrame();
    context->ProcessMouseMove(180, 180, 0);
    context->ProcessMouseButtonDown(0, 0);
    context->ProcessMouseButtonUp(0, 0);
    context->Update();
    host.EndFrame();
    const auto backgroundClick =
        std::find_if(capturedEvents.begin(), capturedEvents.end(),
                     [](const core::UiEvent& event) { return event.type == "click"; });
    REQUIRE(backgroundClick != capturedEvents.end());
    CHECK_FALSE(host.InputConsumedThisFrame());

    capturedEvents.clear();
    Rml::Element* button = context->GetDocument(0)->GetElementById("btn");
    REQUIRE(button != nullptr);
    REQUIRE(button->Focus());
    context->Update();
    context->ProcessKeyDown(Rml::Input::KI_A, 0);
    context->Update();
    host.EndFrame();
    const auto key = std::find_if(capturedEvents.begin(), capturedEvents.end(),
                                  [](const core::UiEvent& event) {
                                      return event.type == "keydown";
                                  });
    REQUIRE(key != capturedEvents.end());
    CHECK(key->elementId == "btn");
    CHECK(key->key != 0);

    capturedEvents.clear();
    context->ProcessMouseMove(40, 80, 0);
    context->ProcessMouseButtonDown(0, 0);
    context->Update();
    context->ProcessMouseMove(140, 80, 0);
    context->Update();
    context->ProcessMouseButtonUp(0, 0);
    context->Update();
    host.EndFrame();
    const auto dragStart = std::find_if(capturedEvents.begin(), capturedEvents.end(),
                                        [](const core::UiEvent& event) {
                                            return event.type == "dragstart";
                                        });
    REQUIRE(dragStart != capturedEvents.end());
    CHECK(dragStart->elementId == "drag");
    CHECK(dragStart->mouseX == doctest::Approx(40.0f));

    CHECK(host.SetModal("test_mod", "panel", true));
    CHECK(host.IsInputCaptured());
    CHECK(host.SetModal("test_mod", "panel", false));
    CHECK_FALSE(host.IsInputCaptured());
    CHECK(host.UnloadDocument("test_mod", "panel"));
    CHECK_FALSE(host.IsLoaded("test_mod", "panel"));

    CHECK_FALSE(host.LoadDocument("test_mod", mod.root, "bad", "../outside.rml", true,
                                  false, ""));
    CHECK_FALSE(host.LoadDocument("test_mod", mod.root, "bad", "/absolute.rml", true, false,
                                  ""));

    Rml::RemoveContext("script_ui_test");
    Rml::Shutdown();
}

TEST_CASE("Script UI host tracks owner refs and group visibility") {
    ScriptUiNullRenderInterface renderInterface;
    ScriptUiNullSystemInterface systemInterface;
    Rml::SetRenderInterface(&renderInterface);
    Rml::SetSystemInterface(&systemInterface);
    REQUIRE(Rml::Initialise());
    Rml::Context* context = Rml::CreateContext("script_ui_group", Rml::Vector2i(200, 200));
    REQUIRE(context != nullptr);

    TempMod mod;
    {
        zl::ui::ScriptUiHost host(*context);
        const auto oldToken = host.RegisterOwner("owner_a");
        const auto newToken = host.RegisterOwner("owner_a");
        CHECK(host.HasOwner("owner_a"));
        host.ReleaseOwner("owner_a", oldToken);
        CHECK(host.HasOwner("owner_a"));
        host.ReleaseOwner("owner_a", newToken);
        CHECK_FALSE(host.HasOwner("owner_a"));

        REQUIRE(host.LoadDocument("mod_g", mod.root, "panel", "ui/panel.rml", true, false,
                                  "group_g"));
        CHECK(host.IsVisible("mod_g", "panel"));
        host.SetGroupVisible("group_g", false);
        CHECK_FALSE(host.IsVisible("mod_g", "panel"));
        host.SetGroupVisible("group_g", true);
        CHECK(host.IsVisible("mod_g", "panel"));
    }

    Rml::RemoveContext("script_ui_group");
    Rml::Shutdown();
}

TEST_CASE("Lua UI binding opens and updates a mod document") {
    ScriptUiNullRenderInterface renderInterface;
    ScriptUiNullSystemInterface systemInterface;
    Rml::SetRenderInterface(&renderInterface);
    Rml::SetSystemInterface(&systemInterface);
    REQUIRE(Rml::Initialise());
    Rml::Context* context = Rml::CreateContext("script_ui_lua", Rml::Vector2i(200, 200));
    REQUIRE(context != nullptr);

    TempMod mod;
    std::filesystem::create_directories(mod.root / "scripts");
    {
        std::ofstream out(mod.root / "scripts" / "main.lua", std::ios::binary);
        out << "assert(UI.OpenDocument('panel', 'ui/panel.rml', false))\n"
               "assert(UI.SetText('panel', 'title', 'from lua'))\n"
               "assert(UI.SetProperty('panel', 'title', 'color', 'blue'))\n"
               "assert(Content.RegisterItems('{\"format\":\"items-v1\",\"items\":[{\"id\":\"lua_dynamic\",\"name\":\"Lua 动态\",\"icon\":\"i\",\"weight\":0.1,\"maxStack\":1,\"type\":\"generic\"}]}') == 1)\n"
               "assert(Items.Exists('lua_dynamic'))\n"
               "local hud = Hud.GetState()\n"
               "assert(hud.active == false)\n"
               "assert(#hud.modFields == 0)\n"
               "assert(Hud.RegisterField('status', '模组状态'))\n"
               "assert(Hud.RegisterField('status', '模组状态'))\n"
               "assert(Hud.SetField('status', '运行中', 'ok'))\n"
               "assert(Hud.SetField('missing', 'x', 'info') == false)\n"
               "assert(Hud.SetField('status', 'x', 'bad') == false)\n"
               "local fields = Hud.GetState().modFields\n"
               "assert(#fields == 1 and fields[1].modId == 'ui_lua')\n"
               "assert(fields[1].id == 'status' and fields[1].label == '模组状态')\n"
               "assert(fields[1].text == '运行中' and fields[1].tone == 'ok')\n"
               "assert(Hud.ClearField('status'))\n"
               "assert(Hud.GetState().modFields[1].text == '')\n"
               "assert(Hud.SetField('status', '运行中', 'ok'))\n"
                "assert(#hud.slots == 5)\n"
                "assert(type(hud.combatStatus) == 'string')\n"
                "assert(type(hud.combatTone) == 'string')\n"
                "assert(type(hud.stealthValid) == 'boolean')\n"
                "assert(type(hud.stealthCrouched) == 'boolean')\n"
                "assert(type(hud.stealthMoveSpeed) == 'number')\n"
                "assert(type(hud.stealthFootstepRadius) == 'number')\n"
                "assert(type(hud.stealthAwarenessValid) == 'boolean')\n"
                "assert(type(hud.stealthNearbyAlertCount) == 'number')\n"
                "assert(type(hud.stealthBackstabAvailable) == 'boolean')\n"
                "assert(type(hud.stealthStatus) == 'string')\n"
                "assert(type(hud.stealthTone) == 'string')\n"
                "assert(type(hud.stealthPrompt) == 'string')\n"
                "assert(type(hud.rangedValid) == 'boolean')\n"
                "assert(type(hud.rangedName) == 'string')\n"
                "assert(type(hud.rangedLoaded) == 'number')\n"
                "assert(type(hud.rangedMagazine) == 'number')\n"
                "assert(type(hud.rangedReserve) == 'number')\n"
                "assert(type(hud.rangedReloading) == 'boolean')\n"
                "assert(type(hud.rangedReloadProgress) == 'number')\n"
                "assert(type(hud.rangedReloadRemaining) == 'number')\n"
                "assert(type(hud.rangedStatus) == 'string')\n"
                "assert(type(hud.rangedTone) == 'string')\n"
                "assert(type(hud.forageValid) == 'boolean')\n"
                "assert(type(hud.forageName) == 'string')\n"
                "assert(type(hud.forageToolName) == 'string')\n"
                "assert(type(hud.forageMissingTool) == 'boolean')\n"
                "assert(type(hud.forageExhausted) == 'boolean')\n"
                "assert(type(hud.forageRespawnMinutes) == 'number')\n"
                "assert(type(hud.forageSeconds) == 'number')\n"
                "assert(type(hud.forageSearching) == 'boolean')\n"
                "assert(type(hud.forageProgress) == 'number')\n"
                "assert(type(hud.forageStatus) == 'string')\n"
                "assert(type(hud.forageTone) == 'string')\n"
                "assert(type(hud.fishingValid) == 'boolean')\n"
                "assert(type(hud.fishingName) == 'string')\n"
                "assert(type(hud.fishingRodName) == 'string')\n"
                "assert(type(hud.fishingBaitName) == 'string')\n"
                "assert(type(hud.fishingMissingRod) == 'boolean')\n"
                "assert(type(hud.fishingMissingBait) == 'boolean')\n"
                "assert(type(hud.fishingSeconds) == 'number')\n"
                "assert(type(hud.fishingWaiting) == 'boolean')\n"
                "assert(type(hud.fishingProgress) == 'number')\n"
                "assert(type(hud.fishingBiting) == 'boolean')\n"
                "assert(type(hud.fishingBiteRemaining) == 'number')\n"
                "assert(type(hud.fishingStatus) == 'string')\n"
                "assert(type(hud.fishingTone) == 'string')\n"
                "assert(type(hud.needsStatus) == 'string')\n"
               "assert(type(hud.needsTone) == 'string')\n"
               "assert(type(hud.woundStatus) == 'string')\n"
               "assert(type(hud.woundTone) == 'string')\n"
               "assert(type(hud.painStatus) == 'string')\n"
               "assert(type(hud.painTone) == 'string')\n"
               "assert(type(hud.armorStatus) == 'string')\n"
               "assert(type(hud.armorTone) == 'string')\n"
               "assert(type(hud.infectionStatus) == 'string')\n"
               "assert(type(hud.infectionTone) == 'string')\n"
               "assert(type(hud.weatherStatus) == 'string')\n"
               "assert(type(hud.weatherTone) == 'string')\n"
               "assert(type(hud.weatherClass) == 'string')\n"
               "assert(type(hud.threatStatus) == 'string')\n"
               "assert(type(hud.threatDirection) == 'string')\n"
               "assert(type(hud.threatRemaining) == 'number')\n"
               "assert(type(hud.threatDuration) == 'number')\n"
               "assert(type(hud.objective) == 'string')\n"
               "assert(type(hud.objectiveTone) == 'string')\n"
               "assert(type(hud.objectiveHint) == 'string')\n"
               "assert(type(hud.poiGuide) == 'string')\n"
               "assert(type(hud.poiGuideTone) == 'string')\n"
               "assert(type(hud.missionGuide) == 'string')\n"
               "assert(type(hud.missionTone) == 'string')\n"
               "assert(type(hud.eventGuide) == 'string')\n"
               "assert(type(hud.eventTone) == 'string')\n"
               "assert(type(hud.worldEventVisible) == 'boolean')\n"
               "assert(type(hud.worldEventCanChoose) == 'boolean')\n"
               "assert(type(hud.worldEventChoiceOpen) == 'boolean')\n"
               "assert(type(hud.worldEventName) == 'string')\n"
               "assert(type(hud.worldEventMeta) == 'string')\n"
               "assert(type(hud.worldEventStage) == 'string')\n"
               "assert(type(hud.worldEventHint) == 'string')\n"
               "assert(type(hud.worldEventTone) == 'string')\n"
               "assert(type(hud.worldEventResult) == 'string')\n"
               "assert(type(hud.worldEventResultTone) == 'string')\n"
               "assert(type(hud.worldEventOptionCount) == 'number')\n"
               "assert(type(hud.worldEventLogCount) == 'number')\n"
               "assert(#hud.worldEventOptions == 8)\n"
               "assert(#hud.worldEventLog == 6)\n"
               "assert(type(hud.regionGuide) == 'string')\n"
               "assert(type(hud.regionTone) == 'string')\n"
               "assert(type(hud.settlementGuide) == 'string')\n"
               "assert(type(hud.settlementTone) == 'string')\n"
               "assert(type(hud.strategicGuide) == 'string')\n"
               "assert(type(hud.strategicTone) == 'string')\n"
               "assert(type(hud.siegeStatus) == 'string')\n"
               "assert(type(hud.siegeTone) == 'string')\n"
               "assert(type(hud.siegeReportOpen) == 'boolean')\n"
               "assert(type(hud.siegeReportTitle) == 'string')\n"
               "assert(type(hud.siegeReportRating) == 'string')\n"
               "assert(type(hud.siegeReportTone) == 'string')\n"
               "assert(type(hud.siegeReportHint) == 'string')\n"
               "assert(type(hud.siegeReportLines) == 'table' and #hud.siegeReportLines == 8)\n"
               "assert(type(hud.baseActive) == 'boolean')\n"
               "assert(type(hud.baseStorageCount) == 'number')\n"
               "assert(type(hud.baseStorageWeight) == 'number')\n"
               "assert(type(hud.baseStorageCapacity) == 'number')\n"
               "assert(type(hud.baseBeds) == 'number')\n"
               "assert(type(hud.baseCampfires) == 'number')\n"
               "assert(type(hud.baseWorkbenches) == 'number')\n"
               "assert(type(hud.baseDefensesIntact) == 'number')\n"
               "assert(type(hud.baseDefensesDamaged) == 'number')\n"
               "assert(type(hud.baseStatus) == 'string')\n"
               "assert(type(hud.baseTone) == 'string')\n"
               "assert(type(hud.progressionValid) == 'boolean')\n"
               "assert(type(hud.progressionLevel) == 'number')\n"
               "assert(type(hud.progressionXp) == 'number')\n"
               "assert(type(hud.progressionXpNext) == 'number')\n"
               "assert(type(hud.progressionPerkPoints) == 'number')\n"
               "assert(type(hud.progressionSkills) == 'table' and #hud.progressionSkills == 4)\n"
               "assert(type(hud.progressionPerks) == 'string')\n"
               "assert(type(hud.progressionChoices) == 'table' and #hud.progressionChoices == 3)\n"
               "assert(type(hud.progressionChoiceCount) == 'number')\n"
               "assert(type(hud.perkPrompt) == 'string')\n"
               "assert(type(hud.characterOpen) == 'boolean')\n"
               "assert(type(hud.actionHint) == 'string')\n"
               "assert(type(hud.actionTone) == 'string')\n"
               "assert(Game.IsMenuOpen() == false)\n"
               "assert(Game.IsInventoryOpen() == false)\n"
               "local menu = Menu.GetState()\n"
               "assert(menu.volume == 42)\n"
               "assert(#menu.bindings == 1)\n"
               "assert(Menu.KeyName(34) == 'W')\n"
               "assert(Menu.Action('resume'))\n"
               "local result = Menu.ApplySettings({volume=55, musicVolume=60,"
               " sfxVolume=70, ambientVolume=80, bindings=menu.bindings})\n"
               "assert(result.ok)\n"
               "assert(Menu.Action('unknown') == false)\n"
               "local inv = Inventory.GetState()\n"
               "assert(inv.open)\n"
               "assert(#inv.bag == 25 and #inv.loot == 8)\n"
               "assert(type(inv.lootTitle) == 'string')\n"
               "assert(inv.bag[1].label == '绷带')\n"
               "assert(inv.bag[1].condition == 0.75)\n"
               "assert(inv.bag[1].damaged and inv.bag[1].repairAvailable)\n"
               "assert(#inv.recipes == 1 and inv.recipes[1].canCraft)\n"
               "assert(inv.recipes[1].estimatedSeconds == 1.5)\n"
               "assert(inv.recipes[1].known)\n"
               "assert(inv.recipes[1].station == 'workbench')\n"
               "assert(inv.recipes[1].stationLabel == '工作台')\n"
               "assert(inv.recipes[1].stationNearby)\n"
               "assert(inv.recipes[1].ingredients[1].label == '破布')\n"
               "assert(#inv.recipes[1].tools == 1 and inv.recipes[1].tools[1].label == '锤子')\n"
               "assert(inv.crafting.active and inv.crafting.recipeId == 'field_bandage')\n"
               "assert(inv.crafting.outputLabel == '绷带' and inv.crafting.outputCount == 1)\n"
               "assert(inv.crafting.elapsed == 0.75 and inv.crafting.duration == 1.5)\n"
               "assert(inv.crafting.progress == 0.5)\n"
               "assert(inv.crafting.toolEfficiency == 0.75)\n"
                "assert(inv.crafting.batchCount == 3 and inv.crafting.completedCount == 1)\n"
                "assert(#inv.crafting.queue == 1)\n"
                "assert(inv.crafting.queue[1].recipeId == 'makeshift_spear')\n"
                "assert(inv.crafting.queue[1].outputLabel == '简易长矛')\n"
                "assert(inv.crafting.queue[1].outputCount == 1 and inv.crafting.queue[1].count == 2)\n"
                "assert(Inventory.Action({type='transfer', from='bag', fromIndex=0, to='equipment', toIndex=0}))\n"
               "assert(Inventory.Action({type='craft', from='bag', fromIndex=0, to='bag', toIndex=-1, count=3}))\n"
                "assert(Inventory.Action({type='cancel_craft'}))\n"
                "assert(Inventory.Action({type='repair', from='bag', fromIndex=0}))\n"
                "assert(Inventory.Action({type='queue_up', from='bag', fromIndex=0}))\n"
                "assert(Inventory.Action({type='queue_down', from='bag', fromIndex=0}))\n"
                "assert(Inventory.Action({type='queue_remove', from='bag', fromIndex=0}))\n"
                "assert(Inventory.Action({type='queue_count', from='bag', fromIndex=0, count=7}))\n"
                "assert(Inventory.Action({type='store_all', from='loot'}))\n"
                "assert(Inventory.Action({type='take_all', from='loot'}))\n"
                "assert(Inventory.Action({type='sort_storage', from='loot'}))\n"
                 "assert(Inventory.Action({type='bogus'}) == false)\n"
                "Inventory.PlayClick()\n"
                "local saves = Save.List()\n"
                "assert(saves.ok and #saves.saves == 1)\n"
                "assert(saves.saves[1].id == 'lua-save')\n"
                "assert(saves.saves[1].name == 'Lua 存档')\n"
                "assert(Save.QuickSave())\n"
                "assert(Save.Load('lua-save').ok)\n"
                "assert(Save.Delete('lua-save').ok)\n"
                "assert(Save.Delete('missing').ok == false)\n";
    }

    {
        zl::ui::ScriptUiHost host(*context);
        zl::items::ItemRegistry items;
        std::string itemError;
        REQUIRE(items.LoadFromString(
            R"({"format":"items-v1","items":[{"id":"base_lua","name":"基础","icon":"i","weight":0.1,"maxStack":1,"type":"generic"}]})",
            "lua-items", &itemError));
        zl::game::GameContext contextData;
        contextData.items = &items;
        zl::ui::MenuState menuState;
        menuState.settings.volume = 42;
        menuState.settings.bindings.push_back(
            zl::ui::MenuKeyBinding{"MoveUp", "向上移动", "W"});
        contextData.getMenuState = [&menuState](zl::ui::MenuState& out) {
            out = menuState;
            return true;
        };
        contextData.applyMenuSettings = [](const zl::ui::MenuSettings& settings,
                                           std::string& error) {
            if (settings.volume != 55 || settings.bindings.size() != 1) {
                error = "bad settings";
                return false;
            }
            return true;
        };
        contextData.menuAction = [](zl::ui::MenuActionType) {};
        zl::ui::InventoryState inventoryState;
        inventoryState.open = true;
        inventoryState.playerValid = true;
        inventoryState.bag[0] =
            zl::ui::InventorySlot{true, "绷带", 2, true, false, 0};
        inventoryState.bag[0].condition = 0.75f;
        inventoryState.bag[0].damaged = true;
        inventoryState.bag[0].repairAvailable = true;
        inventoryState.recipes.push_back(zl::ui::InventoryRecipe{
            "field_bandage", "简易绷带", "bandage", "绷带", 1, true, true,
            {{"破布", 2, 2}}, {{"锤子", 1, 1}}});
        inventoryState.recipes.back().estimatedSeconds = 1.5f;
        inventoryState.recipes.back().station = "workbench";
        inventoryState.recipes.back().stationLabel = "工作台";
        inventoryState.recipes.back().stationNearby = true;
        inventoryState.crafting.active = true;
        inventoryState.crafting.recipeId = "field_bandage";
        inventoryState.crafting.outputLabel = "绷带";
        inventoryState.crafting.outputCount = 1;
        inventoryState.crafting.elapsed = 0.75f;
        inventoryState.crafting.duration = 1.5f;
        inventoryState.crafting.progress = 0.5f;
        inventoryState.crafting.toolEfficiency = 0.75f;
        inventoryState.crafting.batchCount = 3;
        inventoryState.crafting.completedCount = 1;
        inventoryState.crafting.queue.push_back(
            zl::ui::InventoryQueuedCraft{"makeshift_spear", "简易长矛", 1, 2});
        contextData.getInventoryState =
            [&inventoryState](zl::ui::InventoryState& out) {
                out = inventoryState;
                return true;
            };
        int inventoryActions = 0;
        bool transferActed = false;
        bool craftActed = false;
        bool cancelCraftActed = false;
        bool repairActed = false;
        bool queueUpActed = false;
        bool queueDownActed = false;
        bool queueRemoveActed = false;
        bool queueCountActed = false;
        bool storeAllActed = false;
        bool takeAllActed = false;
        bool sortStorageActed = false;
        int queueActionIndex = -1;
        int queueCountValue = 0;
        int craftBatchCount = 0;
        contextData.inventoryAction =
            [&](const zl::ui::InventoryAction& action) {
                ++inventoryActions;
                if (action.type == zl::ui::InventoryActionType::Transfer &&
                    action.fromIndex == 0 && action.toIndex == 0) {
                    transferActed = true;
                    return true;
                }
                if (action.type == zl::ui::InventoryActionType::Craft &&
                    action.fromIndex == 0) {
                    craftActed = true;
                    craftBatchCount = action.count;
                    return true;
                }
                if (action.type == zl::ui::InventoryActionType::CancelCraft) {
                    cancelCraftActed = true;
                    return true;
                }
                if (action.type == zl::ui::InventoryActionType::Repair &&
                    action.fromIndex == 0) {
                    repairActed = true;
                    return true;
                }
                if (action.type == zl::ui::InventoryActionType::QueueMoveUp &&
                    action.fromIndex == 0) {
                    queueUpActed = true;
                    queueActionIndex = action.fromIndex;
                    return true;
                }
                if (action.type == zl::ui::InventoryActionType::QueueMoveDown &&
                    action.fromIndex == 0) {
                    queueDownActed = true;
                    queueActionIndex = action.fromIndex;
                    return true;
                }
                if (action.type == zl::ui::InventoryActionType::QueueRemove &&
                    action.fromIndex == 0) {
                    queueRemoveActed = true;
                    queueActionIndex = action.fromIndex;
                    return true;
                }
                if (action.type == zl::ui::InventoryActionType::QueueSetCount &&
                    action.fromIndex == 0 && action.count == 7) {
                    queueCountActed = true;
                    queueActionIndex = action.fromIndex;
                    queueCountValue = action.count;
                    return true;
                }
                if (action.type == zl::ui::InventoryActionType::StoreAll) {
                    storeAllActed = true;
                    return true;
                }
                if (action.type == zl::ui::InventoryActionType::TakeAll) {
                    takeAllActed = true;
                    return true;
                }
                if (action.type == zl::ui::InventoryActionType::SortStorage) {
                    sortStorageActed = true;
                    return true;
                }
                return false;
            };
        int inventoryClicks = 0;
        contextData.playInventoryClick = [&inventoryClicks] { ++inventoryClicks; };
        std::vector<save::SaveMetadata> saveList(1);
        saveList[0].id = "lua-save";
        saveList[0].name = "Lua 存档";
        contextData.listSaves =
            [&saveList](std::vector<save::SaveMetadata>& out, std::string& error) {
                out = saveList;
                error.clear();
                return true;
            };
        std::string loadedSave;
        contextData.loadSave = [&loadedSave](const std::string& id, std::string& error) {
            loadedSave = id;
            error.clear();
            return true;
        };
        int quickSaves = 0;
        contextData.quickSave = [&quickSaves] {
            ++quickSaves;
            return true;
        };
        int deletedSaves = 0;
        contextData.deleteSave = [&deletedSaves](const std::string& id,
                                                  std::string& error) {
            if (id != "lua-save") {
                error = "不存在";
                return false;
            }
            ++deletedSaves;
            return true;
        };
        zl::script::ScriptApi api;
        api.Attach(&contextData);

        zl::script::LuaRuntime runtime;
        runtime.SetApi(&api);
        runtime.SetUiHost(&host);
        std::string error;
        REQUIRE(runtime.Initialize(error));

        zl::script::ModInfo info;
        info.id = "ui_lua";
        info.backend = "lua";
        info.rootPath = mod.root.string();
        info.mainScript = "scripts/main.lua";
        CAPTURE(error);
        REQUIRE(runtime.LoadMod(info, error));
        CHECK(host.IsLoaded("ui_lua", "panel"));
        Rml::Element* title = context->GetDocument(0) != nullptr
                                  ? context->GetDocument(0)->GetElementById("title")
                                  : nullptr;
        REQUIRE(title != nullptr);
        CHECK(title->GetInnerRML() == "from lua");
        CHECK(host.HasOwner("ui_lua"));
        REQUIRE(contextData.modHudFields.size() == 1);
        CHECK(contextData.modHudFields[0].modId == "ui_lua");
        CHECK(contextData.modHudFields[0].id == "status");
        CHECK(contextData.modHudFields[0].label == "模组状态");
        CHECK(contextData.modHudFields[0].text == "运行中");
        CHECK(contextData.modHudFields[0].tone == "ok");
        CHECK(contextData.dynamicContentCount == 1);
        CHECK(items.Find("lua_dynamic") != nullptr);
        CHECK(inventoryActions == 11);
        CHECK(transferActed);
        CHECK(craftActed);
        CHECK(craftBatchCount == 3);
        CHECK(cancelCraftActed);
        CHECK(repairActed);
        CHECK(queueUpActed);
        CHECK(queueDownActed);
        CHECK(queueRemoveActed);
        CHECK(queueCountActed);
        CHECK(storeAllActed);
        CHECK(takeAllActed);
        CHECK(sortStorageActed);
        CHECK(queueActionIndex == 0);
        CHECK(queueCountValue == 7);
        CHECK(inventoryClicks == 1);
        CHECK(quickSaves == 1);
        CHECK(loadedSave == "lua-save");
        CHECK(deletedSaves == 1);
        runtime.UnloadMod("ui_lua");
    }

    Rml::RemoveContext("script_ui_lua");
    Rml::Shutdown();
}
TEST_CASE("Menu script API exposes state, actions, key names and settings") {
    zl::game::GameContext context;
    zl::ui::MenuState source;
    source.open = true;
    source.pauseMode = true;
    source.canContinue = true;
    source.settings.volume = 42;
    source.settings.bindings.push_back(zl::ui::MenuKeyBinding{"MoveUp", "向上移动", "W"});
    context.getMenuState = [&source](zl::ui::MenuState& out) {
        out = source;
        return true;
    };
    int clicks = 0;
    context.playUiClick = [&clicks] { ++clicks; };
    zl::ui::MenuActionType routed = zl::ui::MenuActionType::NewGame;
    context.menuAction = [&routed](zl::ui::MenuActionType action) { routed = action; };
    bool applied = false;
    context.applyMenuSettings = [&applied](const zl::ui::MenuSettings& settings,
                                           std::string& error) {
        if (settings.volume != 55) {
            error = "volume";
            return false;
        }
        applied = true;
        return true;
    };

    zl::script::ScriptApi api;
    api.Attach(&context);

    zl::ui::MenuState state;
    REQUIRE(api.GetMenuState(state));
    CHECK(state.open);
    CHECK(state.pauseMode);
    CHECK(state.canContinue);
    CHECK(state.settings.volume == 42);
    REQUIRE(state.settings.bindings.size() == 1);
    CHECK(state.settings.bindings[0].key == "W");

    CHECK(api.MenuAction("resume"));
    CHECK(routed == zl::ui::MenuActionType::Resume);
    CHECK(api.MenuAction("multiplayer"));
    CHECK(routed == zl::ui::MenuActionType::Multiplayer);
    CHECK_FALSE(api.MenuAction("unknown"));

    CHECK(api.MenuKeyName(static_cast<int>(Rml::Input::KI_W)) == "W");
    CHECK(api.MenuKeyName(static_cast<int>(Rml::Input::KI_ESCAPE)) == "ESCAPE");
    CHECK(api.MenuKeyName(static_cast<int>(Rml::Input::KI_LSHIFT)).empty());

    api.PlayUiClick();
    CHECK(clicks == 1);

    zl::ui::MenuSettings settings;
    settings.volume = 55;
    std::string error;
    CHECK(api.ApplyMenuSettings(settings, error));
    CHECK(applied);
    settings.volume = 99;
    CHECK_FALSE(api.ApplyMenuSettings(settings, error));
    CHECK(error == "volume");
}
