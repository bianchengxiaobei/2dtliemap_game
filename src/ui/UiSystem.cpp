#include "ui/UiSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <GLFW/glfw3.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/StringUtilities.h>
#include <bgfx/bgfx.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "core/EventBus.h"
#include "core/GameEvents.h"
#include "core/Input.h"
#include "core/Log.h"
#include "platform/Window.h"
#include "render/Renderer.h"
#include "ui/InventoryPanel.h"
#include "ui/MenuPanel.h"
#include "ui/MultiplayerPanel.h"
#include "ui/SocialPanel.h"
#include "ui/VehicleModsPanel.h"
#include "ui/WorldMapUi.h"
#include "ui/RmlBgfxRenderInterface.h"
#include "ui/RmlSystemInterface.h"
#include "ui/ScriptUiHost.h"
#include "ui/UiInput.h"

namespace zl::ui {
namespace {

constexpr uint32_t kFontAtlasPageBudget = 12;
constexpr uint64_t kFontAtlasByteBudget = 48ull * 1024 * 1024;
constexpr const char* kCoreUiOwner = "core_ui";
constexpr const char* kCoreHudDocument = "hud";
constexpr const char* kCoreMenuDocument = "menu";
constexpr const char* kCoreInventoryDocument = "inventory";
constexpr const char* kHudGroup = "hud";

class FunctionListener final : public Rml::EventListener {
public:
    explicit FunctionListener(std::function<void()> action) : m_action(std::move(action)) {}

    void ProcessEvent(Rml::Event& event) override {
        (void)event;
        if (m_action) {
            m_action();
        }
    }

private:
    std::function<void()> m_action;
};

}  // namespace

UiSystem::UiSystem() = default;

UiSystem::~UiSystem() { Shutdown(); }

void UiSystem::SetEventBus(core::EventBus* events) { m_events = events; }

bool UiSystem::Init(platform::Window& window, int fbWidth, int fbHeight) {
    if (m_initialized) {
        return true;
    }
    m_window = &window;
    m_width = fbWidth;
    m_height = fbHeight;

    m_renderer = std::make_unique<RmlBgfxRenderInterface>();
    if (!m_renderer->Init(static_cast<bgfx::ViewId>(render::ViewId::UI))) {
        ZL_ERROR("ui", "RmlUi 渲染后端初始化失败");
        m_renderer.reset();
        return false;
    }
    m_renderer->SetViewport(m_width, m_height);
    m_system = std::make_unique<RmlSystemInterface>();

    Rml::SetSystemInterface(m_system.get());
    Rml::SetRenderInterface(m_renderer.get());
    if (!Rml::Initialise()) {
        ZL_ERROR("ui", "Rml::Initialise 失败");
        m_system.reset();
        m_renderer->Shutdown();
        m_renderer.reset();
        return false;
    }

    if (Rml::LoadFontFace("assets/fonts/NotoSansSC-Regular.otf", "zl_ui",
                          Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal)) {
        ZL_INFO("ui", "UI 字体已加载：NotoSansSC-Regular（zl_ui 400）");
    } else {
        ZL_WARN("ui", "UI 字体加载失败：assets/fonts/NotoSansSC-Regular.otf（中文将无法显示）");
    }
    if (Rml::LoadFontFace("assets/fonts/NotoSansSC-Bold.ttf", "zl_ui",
                          Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Bold)) {
        ZL_INFO("ui", "UI 字体已加载：NotoSansSC-Bold（zl_ui 700）");
    } else {
        ZL_WARN("ui", "UI 粗体加载失败：assets/fonts/NotoSansSC-Bold.ttf（粗体将回退到常规字重）");
    }

    m_context = Rml::CreateContext("main", Rml::Vector2i(m_width, m_height));
    if (m_context == nullptr) {
        ZL_ERROR("ui", "RmlUi 上下文创建失败");
        Rml::Shutdown();
        m_system.reset();
        m_renderer->Shutdown();
        m_renderer.reset();
        return false;
    }

    m_scriptUi = std::make_unique<ScriptUiHost>(*m_context);

    window.SetCharCallback([this](unsigned int codepoint) { HandleChar(codepoint); });
    m_context->Update();

    m_initialized = true;
    ZL_INFO("ui", "RmlUi {} 初始化完成：{}x{}（核心 UI 按需懒加载，脚本 UI Host 就绪）",
            Rml::GetVersion().c_str(), m_width, m_height);
    return true;
}

bool UiSystem::EnsureDebugDocument() {
    if (m_document != nullptr) {
        return true;
    }
    if (m_debugLoadAttempted) {
        return false;
    }
    m_debugLoadAttempted = true;
    m_document = m_context->LoadDocument("assets/ui/debug.rml");
    if (m_document == nullptr) {
        ZL_WARN("ui", "调试面板加载失败：assets/ui/debug.rml");
        return false;
    }
    m_document->Hide();
    m_visible = false;
    m_counterElement = m_document->GetElementById("count");
    m_disableSampleElement = m_document->GetElementById("disabled-sample");
    m_volumeElement = m_document->GetElementById("volume");
    m_volumeValueElement = m_document->GetElementById("volume-value");
    m_atlasStatsElement = m_document->GetElementById("atlas-stats");

    AddClickHandler("inc", [this] {
        ++m_counter;
        if (m_counterElement != nullptr) {
            m_counterElement->SetInnerRML(std::to_string(m_counter));
        }
    });
    AddClickHandler("dec", [this] {
        m_counter = std::max(0, m_counter - 1);
        if (m_counterElement != nullptr) {
            m_counterElement->SetInnerRML(std::to_string(m_counter));
        }
    });
    AddEventHandler("toggle-disable", "click", [this] {
        if (m_disableSampleElement == nullptr) {
            return;
        }
        if (m_disableSampleElement->HasAttribute("disabled")) {
            m_disableSampleElement->RemoveAttribute("disabled");
            m_disableSampleElement->SetInnerRML("禁用示例按钮");
        } else {
            m_disableSampleElement->SetAttribute("disabled", Rml::String("disabled"));
            m_disableSampleElement->SetInnerRML("启用示例按钮");
        }
    });
    AddEventHandler("volume", "change", [this] {
        if (m_volumeElement == nullptr || m_volumeValueElement == nullptr) {
            return;
        }
        auto* input = static_cast<Rml::ElementFormControlInput*>(m_volumeElement);
        const double value = std::strtod(input->GetValue().c_str(), nullptr);
        m_volumeValueElement->SetInnerRML(
            std::to_string(static_cast<int>(std::lround(value))));
    });
    ZL_INFO("ui", "核心 UI 按需加载：调试面板");
    return true;
}

bool UiSystem::EnsureHudDocument() {
    if (m_hudDocument != nullptr) {
        return true;
    }
    if (m_hudLoadAttempted) {
        return false;
    }
    m_hudLoadAttempted = true;
    m_hudDocument = m_context->LoadDocument("assets/ui/hud.rml");
    if (m_hudDocument == nullptr) {
        ZL_WARN("ui", "HUD 加载失败：assets/ui/hud.rml");
        return false;
    }
    m_hudDocument->Hide();
    m_nativeHudNeedsFullRefresh = true;
    m_hudDayElement = m_hudDocument->GetElementById("hud-day");
    m_hudTimeElement = m_hudDocument->GetElementById("hud-time");
    m_hudSpeedElement = m_hudDocument->GetElementById("hud-speed");
    m_hpFillElement = m_hudDocument->GetElementById("hp-fill");
    m_hpTextElement = m_hudDocument->GetElementById("hp-text");
    m_staminaFillElement = m_hudDocument->GetElementById("stamina-fill");
    m_staminaTextElement = m_hudDocument->GetElementById("stamina-text");
    m_combatStatusElement = m_hudDocument->GetElementById("combat-status");
    m_stealthStatusElement = m_hudDocument->GetElementById("stealth-status");
    m_rangedStatusElement = m_hudDocument->GetElementById("ranged-status");
    m_throwStatusElement = m_hudDocument->GetElementById("throw-status");
    m_forageStatusElement = m_hudDocument->GetElementById("forage-status");
    m_fishingStatusElement = m_hudDocument->GetElementById("fishing-status");
    m_needsStatusElement = m_hudDocument->GetElementById("needs-status");
    m_woundStatusElement = m_hudDocument->GetElementById("wound-status");
    m_painStatusElement = m_hudDocument->GetElementById("pain-status");
    m_armorStatusElement = m_hudDocument->GetElementById("armor-status");
    m_infectionStatusElement =
        m_hudDocument->GetElementById("infection-status");
    m_weatherStatusElement = m_hudDocument->GetElementById("weather-status");
    m_weatherOverlayElement =
        m_hudDocument->GetElementById("weather-overlay");
    m_threatStatusElement = m_hudDocument->GetElementById("threat-status");
    m_threatEdges[0] = m_hudDocument->GetElementById("threat-left");
    m_threatEdges[1] = m_hudDocument->GetElementById("threat-right");
    m_threatEdges[2] = m_hudDocument->GetElementById("threat-top");
    m_threatEdges[3] = m_hudDocument->GetElementById("threat-bottom");
    m_objectivePanelElement = m_hudDocument->GetElementById("hud-objective");
    m_objectiveElement = m_hudDocument->GetElementById("objective-text");
    m_objectiveHintElement = m_hudDocument->GetElementById("objective-hint");
    m_poiGuideElement = m_hudDocument->GetElementById("poi-guide");
    m_poiStagePanelElement =
        m_hudDocument->GetElementById("poi-stage-panel");
    m_poiStageTitleElement =
        m_hudDocument->GetElementById("poi-stage-title");
    m_poiStageObjectiveElement =
        m_hudDocument->GetElementById("poi-stage-objective");
    m_poiStageMetaElement =
        m_hudDocument->GetElementById("poi-stage-meta");
    m_poiStageNarrativeElement =
        m_hudDocument->GetElementById("poi-stage-narrative");
    m_poiResultPanelElement =
        m_hudDocument->GetElementById("poi-result-panel");
    m_poiResultTitleElement =
        m_hudDocument->GetElementById("poi-result-title");
    m_poiResultBodyElement =
        m_hudDocument->GetElementById("poi-result-body");
    m_missionGuideElement = m_hudDocument->GetElementById("mission-guide");
    m_eventGuideElement = m_hudDocument->GetElementById("event-guide");
    m_worldEventPanelElement =
        m_hudDocument->GetElementById("world-event-panel");
    m_worldEventNameElement =
        m_hudDocument->GetElementById("world-event-title");
    m_worldEventMetaElement =
        m_hudDocument->GetElementById("world-event-meta");
    m_worldEventStageElement =
        m_hudDocument->GetElementById("world-event-stage");
    m_worldEventHintElement =
        m_hudDocument->GetElementById("world-event-hint");
    m_worldEventResultElement =
        m_hudDocument->GetElementById("world-event-result");
    m_worldEventOptionsElement =
        m_hudDocument->GetElementById("world-event-options");
    m_worldEventLogElement =
        m_hudDocument->GetElementById("world-event-log");
    for (int i = 0; i < 8; ++i) {
        m_worldEventOptionElements[static_cast<std::size_t>(i)] =
            m_hudDocument->GetElementById("world-event-option-" +
                                          std::to_string(i));
    }
    for (int i = 0; i < 6; ++i) {
        m_worldEventLogLines[static_cast<std::size_t>(i)] =
            m_hudDocument->GetElementById("world-event-log-" +
                                          std::to_string(i));
    }
    m_regionGuideElement = m_hudDocument->GetElementById("region-guide");
    m_settlementGuideElement =
        m_hudDocument->GetElementById("settlement-guide");
    m_strategicGuideElement =
        m_hudDocument->GetElementById("strategic-guide");
    m_siegeStatusElement = m_hudDocument->GetElementById("siege-status");
    m_baseStatusElement = m_hudDocument->GetElementById("base-status");
    m_modFieldsElement = m_hudDocument->GetElementById("mod-fields");
    for (int i = 0; i < 8; ++i) {
        const std::string suffix = std::to_string(i);
        m_modFieldRows[static_cast<std::size_t>(i)] =
            m_hudDocument->GetElementById("mod-field-" + suffix);
        m_modFieldLabels[static_cast<std::size_t>(i)] =
            m_hudDocument->GetElementById("mod-field-label-" + suffix);
        m_modFieldValues[static_cast<std::size_t>(i)] =
            m_hudDocument->GetElementById("mod-field-value-" + suffix);
    }
    m_siegeReportElement = m_hudDocument->GetElementById("siege-report");
    m_siegeReportTitleElement =
        m_hudDocument->GetElementById("siege-report-title");
    m_siegeReportRatingElement =
        m_hudDocument->GetElementById("siege-report-rating");
    m_siegeReportHintElement =
        m_hudDocument->GetElementById("siege-report-hint");
    for (int i = 0; i < 8; ++i) {
        m_siegeReportLineElements[static_cast<std::size_t>(i)] =
            m_hudDocument->GetElementById("siege-report-line-" +
                                          std::to_string(i));
    }
    m_levelPanelElement = m_hudDocument->GetElementById("hud-level");
    m_levelTextElement = m_hudDocument->GetElementById("level-text");
    m_levelPerkElement = m_hudDocument->GetElementById("level-perk");
    m_levelFillElement = m_hudDocument->GetElementById("level-fill");
    m_levelXpElement = m_hudDocument->GetElementById("level-xp");
    m_perkPromptElement = m_hudDocument->GetElementById("perk-prompt");
    m_companionPanelElement =
        m_hudDocument->GetElementById("companion-panel");
    m_companionNameElement =
        m_hudDocument->GetElementById("companion-name");
    m_companionHealthElement =
        m_hudDocument->GetElementById("companion-health");
    m_companionHealthFillElement =
        m_hudDocument->GetElementById("companion-health-fill");
    m_companionStatusElement =
        m_hudDocument->GetElementById("companion-state");
    m_companionTrustElement =
        m_hudDocument->GetElementById("companion-trust");
    m_companionEquipmentElement =
        m_hudDocument->GetElementById("companion-equipment");
    m_companionBagElement =
        m_hudDocument->GetElementById("companion-bag");
    m_companionQuestElement =
        m_hudDocument->GetElementById("companion-quest");
    m_companionHintElement =
        m_hudDocument->GetElementById("companion-hint");
    m_companionAlertElement =
        m_hudDocument->GetElementById("companion-status");
    m_characterPanelElement = m_hudDocument->GetElementById("character-panel");
    m_characterLevelElement = m_hudDocument->GetElementById("character-level");
    m_characterXpElement = m_hudDocument->GetElementById("character-xp");
    m_characterXpFillElement = m_hudDocument->GetElementById("character-xp-fill");
    m_characterPerksElement = m_hudDocument->GetElementById("character-perks");
    for (int i = 0; i < 4; ++i) {
        m_characterSkillElements[static_cast<std::size_t>(i)] =
            m_hudDocument->GetElementById("character-skill-" + std::to_string(i));
    }
    for (int i = 0; i < 3; ++i) {
        m_perkChoiceElements[static_cast<std::size_t>(i)] =
            m_hudDocument->GetElementById("perk-choice-" + std::to_string(i));
    }
    m_dialoguePanelElement =
        m_hudDocument->GetElementById("dialogue-panel");
    m_dialogueSpeakerElement =
        m_hudDocument->GetElementById("dialogue-speaker");
    m_dialogueTextElement =
        m_hudDocument->GetElementById("dialogue-text");
    for (int i = 0; i < 5; ++i) {
        m_dialogueOptionElements[static_cast<std::size_t>(i)] =
            m_hudDocument->GetElementById("dialogue-option-" +
                                          std::to_string(i));
    }
    m_dialogueHintElement =
        m_hudDocument->GetElementById("dialogue-hint");
    m_actionHintElement = m_hudDocument->GetElementById("action-hint");
    m_stealthPromptElement =
        m_hudDocument->GetElementById("stealth-prompt");
    m_noticeElement = m_hudDocument->GetElementById("notice");
    for (int i = 0; i < 5; ++i) {
        const std::string suffix = std::to_string(i);
        m_hotbarSlots[i] = m_hudDocument->GetElementById("slot-" + suffix);
        m_hotbarNames[i] = m_hudDocument->GetElementById("slot-name-" + suffix);
        m_hotbarCounts[i] = m_hudDocument->GetElementById("slot-count-" + suffix);
        m_hotbarThrows[i] = m_hudDocument->GetElementById("slot-throw-" + suffix);
    }
    AddClickHandler(m_hudDocument, "hud-bag", [this] { ToggleInventory(); });
    ZL_INFO("ui", "核心 UI 按需加载：HUD");
    return true;
}

bool UiSystem::EnsureWorldMapPanel() {
    if (m_worldMap != nullptr) {
        return true;
    }
    if (m_worldMapLoadAttempted || m_context == nullptr) {
        return false;
    }
    m_worldMapLoadAttempted = true;
    auto panel = std::make_unique<WorldMapPanel>();
    if (!panel->Init(*m_context, "assets/ui/worldmap.rml")) {
        return false;
    }
    m_worldMap = std::move(panel);
    m_worldMap->SetCloseHandler(m_worldMapCloseHandler);
    m_worldMap->SetSelectHandler(m_worldMapSelectHandler);
    m_worldMap->SetStrategicActionHandler(
        m_worldMapStrategicActionHandler);
    ZL_INFO("ui", "核心 UI 按需加载：世界地图");
    return true;
}

bool UiSystem::EnsureVehicleModsPanel() {
    if (m_vehicleMods != nullptr) {
        return true;
    }
    if (m_vehicleModsLoadAttempted || m_context == nullptr) {
        return false;
    }
    m_vehicleModsLoadAttempted = true;
    auto panel = std::make_unique<VehicleModsPanel>();
    if (!panel->Init(*m_context, "assets/ui/vehiclemods.rml")) {
        return false;
    }
    m_vehicleMods = std::move(panel);
    m_vehicleMods->SetActionHandler(m_vehicleModsActionHandler);
    ZL_INFO("ui", "核心 UI 按需加载：车辆改装");
    return true;
}

bool UiSystem::ScriptHudAvailable() const {
    return m_scriptUi != nullptr &&
           (m_scriptUi->HasOwner(kCoreUiOwner) ||
            m_scriptUi->IsLoaded(kCoreUiOwner, kCoreHudDocument));
}

bool UiSystem::ScriptMenuAvailable() const {
    return m_scriptUi != nullptr && m_scriptUi->HasOwner(kCoreUiOwner);
}

bool UiSystem::ScriptInventoryAvailable() const {
    return m_scriptUi != nullptr && m_scriptUi->HasOwner(kCoreUiOwner);
}

bool UiSystem::ScriptInventoryVisible() const {
    return m_scriptUi != nullptr &&
           m_scriptUi->IsVisible(kCoreUiOwner, kCoreInventoryDocument);
}

bool UiSystem::ShowNativeInventoryFallback() {
    if (!EnsureInventoryPanel()) {
        return false;
    }
    InventoryState state = m_pendingInventoryState;
    state.open = true;
    m_inventory->SetState(state);
    m_inventory->Show();
    SyncInventoryChrome(true);
    m_inventoryWasOpen = true;
    return true;
}

bool UiSystem::ShowNativeMenuFallback(bool pauseMode, bool canContinue) {
    if (!EnsureMenuPanel()) {
        return false;
    }
    if (pauseMode) {
        m_menu->ShowPauseMenu();
    } else {
        m_menu->ShowMainMenu(canContinue);
    }
    if (!m_menu->IsOpen()) {
        return false;
    }
    SyncMenuChrome(true);
    m_menuWasOpen = true;
    return true;
}

void UiSystem::NotifyMenuChanged() {
    if (m_events == nullptr) {
        return;
    }
    core::MenuChangedEvent event;
    event.open = m_scriptMenuOpen || (m_menu != nullptr && m_menu->IsOpen());
    event.pauseMode = m_scriptMenuPauseMode;
    event.canContinue = m_scriptMenuCanContinue;
    m_events->Emit(core::kOnMenuChanged, event);
}

void UiSystem::NotifyInventoryChanged() {
    if (m_events == nullptr) {
        return;
    }
    core::InventoryChangedEvent event;
    event.open = IsInventoryOpen();
    m_events->Emit(core::kOnInventoryChanged, event);
}

bool UiSystem::EnsureInventoryPanel() {
    if (m_inventory != nullptr) {
        return true;
    }
    if (m_inventoryLoadAttempted) {
        return false;
    }
    m_inventoryLoadAttempted = true;
    auto panel = std::make_unique<InventoryPanel>();
    if (!panel->Init(*m_context, "assets/ui/inventory.rml")) {
        ZL_WARN("ui", "背包界面不可用：assets/ui/inventory.rml");
        return false;
    }
    m_inventory = std::move(panel);
    if (m_pendingInventoryActionHandler) {
        m_inventory->SetActionHandler(m_pendingInventoryActionHandler);
    }
    if (m_inventorySoundHandler) {
        m_inventory->SetSoundHandler(m_inventorySoundHandler);
    }
    if (m_hasPendingInventoryState) {
        m_inventory->SetState(m_pendingInventoryState);
    }
    ZL_INFO("ui", "核心 UI 按需加载：背包");
    return true;
}

bool UiSystem::EnsureMultiplayerPanel() {
    if (m_multiplayer != nullptr) {
        return true;
    }
    if (m_multiplayerLoadAttempted) {
        return false;
    }
    m_multiplayerLoadAttempted = true;
    auto panel = std::make_unique<MultiplayerPanel>();
    if (!panel->Init(*m_context, "assets/ui/multiplayer.rml")) {
        ZL_WARN("ui", "联机菜单不可用：assets/ui/multiplayer.rml");
        return false;
    }
    m_multiplayer = std::move(panel);
    if (m_pendingMultiplayerActionHandler) {
        m_multiplayer->SetActionHandler(m_pendingMultiplayerActionHandler);
    }
    ZL_INFO("ui", "核心 UI 按需加载：联机菜单");
    return true;
}

bool UiSystem::EnsureSocialPanel() {
    if (m_social != nullptr) {
        return true;
    }
    if (m_socialLoadAttempted) {
        return false;
    }
    m_socialLoadAttempted = true;
    auto panel = std::make_unique<SocialPanel>();
    if (!panel->Init(*m_context, "assets/ui/social.rml")) {
        ZL_WARN("ui", "聊天/玩家列表不可用：assets/ui/social.rml");
        return false;
    }
    m_social = std::move(panel);
    if (m_socialChatHandler) {
        m_social->SetSendHandler(m_socialChatHandler);
    }
    ZL_INFO("ui", "核心 UI 按需加载：聊天/玩家列表");
    return true;
}
bool UiSystem::EnsureMenuPanel() {
    if (m_menu != nullptr) {
        return true;
    }
    if (m_menuLoadAttempted) {
        return false;
    }
    m_menuLoadAttempted = true;
    auto menu = std::make_unique<MenuPanel>();
    if (!menu->Init(*m_context, "assets/ui/menu.rml")) {
        ZL_WARN("ui", "主菜单/暂停菜单不可用：assets/ui/menu.rml");
        return false;
    }
    m_menu = std::move(menu);
    ApplyMenuSettings();
    ZL_INFO("ui", "核心 UI 按需加载：主菜单/暂停菜单");
    return true;
}

void UiSystem::ApplyMenuSettings() {
    if (m_menu == nullptr) {
        return;
    }
    m_menu->SetSettings(m_pendingMenuSettings);
    if (m_pendingMenuSettingsHandler) {
        m_menu->SetSettingsHandler(m_pendingMenuSettingsHandler);
    }
    if (m_pendingMenuActionHandler) {
        m_menu->SetActionHandler(m_pendingMenuActionHandler);
    }
    if (m_menuSoundHandler) {
        m_menu->SetSoundHandler(m_menuSoundHandler);
    }
}

void UiSystem::Shutdown() {
    if (!m_initialized) {
        return;
    }
    if (m_window != nullptr) {
        m_window->SetCharCallback(nullptr);
        m_window = nullptr;
    }
    for (ListenerBinding& binding : m_listeners) {
        if (binding.element != nullptr) {
            binding.element->RemoveEventListener(binding.event, binding.listener.get());
        }
    }
    m_listeners.clear();
    if (m_inventory != nullptr) {
        m_inventory->Shutdown();
        m_inventory.reset();
    }
    if (m_menu != nullptr) {
        m_menu->Shutdown();
        m_menu.reset();
    }
    if (m_multiplayer != nullptr) {
        m_multiplayer->Shutdown();
        m_multiplayer.reset();
    }
    if (m_social != nullptr) {
        m_social->Shutdown();
        m_social.reset();
    }
    if (m_worldMap != nullptr) {
        m_worldMap->Shutdown();
        m_worldMap.reset();
    }
    m_worldMapLoadAttempted = false;
    m_worldMapCloseHandler = nullptr;
    m_worldMapSelectHandler = nullptr;
    m_worldMapStrategicActionHandler = nullptr;
    m_counterElement = nullptr;
    m_disableSampleElement = nullptr;
    m_volumeElement = nullptr;
    m_volumeValueElement = nullptr;
    m_atlasStatsElement = nullptr;
    m_hudDayElement = nullptr;
    m_hudTimeElement = nullptr;
    m_hudSpeedElement = nullptr;
    m_hpFillElement = nullptr;
    m_hpTextElement = nullptr;
    m_staminaFillElement = nullptr;
    m_staminaTextElement = nullptr;
    m_combatStatusElement = nullptr;
    m_stealthStatusElement = nullptr;
    m_rangedStatusElement = nullptr;
    m_throwStatusElement = nullptr;
    m_forageStatusElement = nullptr;
    m_fishingStatusElement = nullptr;
    m_needsStatusElement = nullptr;
    m_woundStatusElement = nullptr;
    m_painStatusElement = nullptr;
    m_armorStatusElement = nullptr;
    m_infectionStatusElement = nullptr;
    m_weatherStatusElement = nullptr;
    m_weatherOverlayElement = nullptr;
    m_threatStatusElement = nullptr;
    m_threatEdges.fill(nullptr);
    m_objectivePanelElement = nullptr;
    m_objectiveElement = nullptr;
    m_objectiveHintElement = nullptr;
    m_poiGuideElement = nullptr;
    m_poiStagePanelElement = nullptr;
    m_poiStageTitleElement = nullptr;
    m_poiStageObjectiveElement = nullptr;
    m_poiStageMetaElement = nullptr;
    m_poiStageNarrativeElement = nullptr;
    m_poiResultPanelElement = nullptr;
    m_poiResultTitleElement = nullptr;
    m_poiResultBodyElement = nullptr;
    m_missionGuideElement = nullptr;
    m_eventGuideElement = nullptr;
    m_worldEventPanelElement = nullptr;
    m_worldEventNameElement = nullptr;
    m_worldEventMetaElement = nullptr;
    m_worldEventStageElement = nullptr;
    m_worldEventHintElement = nullptr;
    m_worldEventResultElement = nullptr;
    m_worldEventOptionsElement = nullptr;
    m_worldEventLogElement = nullptr;
    m_worldEventOptionElements.fill(nullptr);
    m_worldEventLogLines.fill(nullptr);
    m_regionGuideElement = nullptr;
    m_settlementGuideElement = nullptr;
    m_strategicGuideElement = nullptr;
    m_siegeStatusElement = nullptr;
    m_baseStatusElement = nullptr;
    m_modFieldsElement = nullptr;
    m_modFieldRows.fill(nullptr);
    m_modFieldLabels.fill(nullptr);
    m_modFieldValues.fill(nullptr);
    m_siegeReportElement = nullptr;
    m_siegeReportTitleElement = nullptr;
    m_siegeReportRatingElement = nullptr;
    m_siegeReportHintElement = nullptr;
    m_siegeReportLineElements.fill(nullptr);
    m_levelPanelElement = nullptr;
    m_levelTextElement = nullptr;
    m_levelPerkElement = nullptr;
    m_levelFillElement = nullptr;
    m_levelXpElement = nullptr;
    m_perkPromptElement = nullptr;
    m_companionPanelElement = nullptr;
    m_companionNameElement = nullptr;
    m_companionHealthElement = nullptr;
    m_companionHealthFillElement = nullptr;
    m_companionStatusElement = nullptr;
    m_companionTrustElement = nullptr;
    m_companionEquipmentElement = nullptr;
    m_companionBagElement = nullptr;
    m_companionQuestElement = nullptr;
    m_companionHintElement = nullptr;
    m_companionAlertElement = nullptr;
    m_characterPanelElement = nullptr;
    m_characterLevelElement = nullptr;
    m_characterXpElement = nullptr;
    m_characterXpFillElement = nullptr;
    m_characterPerksElement = nullptr;
    m_characterSkillElements.fill(nullptr);
    m_perkChoiceElements.fill(nullptr);
    m_dialoguePanelElement = nullptr;
    m_dialogueSpeakerElement = nullptr;
    m_dialogueTextElement = nullptr;
    m_dialogueOptionElements.fill(nullptr);
    m_dialogueHintElement = nullptr;
    m_actionHintElement = nullptr;
    m_stealthPromptElement = nullptr;
    m_noticeElement = nullptr;
    m_hotbarSlots.fill(nullptr);
    m_hotbarNames.fill(nullptr);
    m_hotbarCounts.fill(nullptr);
    m_hotbarThrows.fill(nullptr);
    if (m_scriptUi != nullptr) {
        m_scriptUi->UnloadAll();
        m_scriptUi.reset();
    }
    m_hudDocument = nullptr;
    m_document = nullptr;
    m_hudCacheValid = false;
    m_noticeVisible = false;
    m_consumeGameplayThisFrame = false;
    m_inventoryWasOpen = false;
    m_menuWasOpen = false;
    m_multiplayerWasOpen = false;
    m_socialState = {};
    m_pendingMultiplayerActionHandler = nullptr;
    m_socialChatHandler = nullptr;
    m_scriptMenuOpen = false;
    m_scriptMenuPauseMode = false;
    m_scriptMenuCanContinue = false;
    m_scriptInventoryOpen = false;
    m_events = nullptr;
    if (m_context != nullptr) {
        Rml::RemoveContext("main");
        m_context = nullptr;
    }
    Rml::Shutdown();
    if (m_renderer != nullptr) {
        m_renderer->Shutdown();
        m_renderer.reset();
    }
    m_system.reset();
    m_pendingChars.clear();
    m_initialized = false;
}

void UiSystem::Resize(int fbWidth, int fbHeight) {
    m_width = std::max(fbWidth, 1);
    m_height = std::max(fbHeight, 1);
    if (m_context != nullptr) {
        m_context->SetDimensions(Rml::Vector2i(m_width, m_height));
    }
    if (m_renderer != nullptr) {
        m_renderer->SetViewport(m_width, m_height);
    }
}

void UiSystem::Update(core::Input& input) {
    if (!m_initialized) {
        return;
    }
    if (m_scriptUi != nullptr) {
        m_scriptUi->BeginFrame();
    }
    if (m_scriptMenuOpen && !ScriptMenuAvailable()) {
        const bool pauseMode = m_scriptMenuPauseMode;
        const bool canContinue = m_scriptMenuCanContinue;
        m_scriptMenuOpen = false;
        m_scriptMenuPauseMode = false;
        ZL_WARN("ui", "core_ui 菜单 owner 已失效，回退 native 菜单");
        ShowNativeMenuFallback(pauseMode, canContinue);
    }
    if (m_scriptInventoryOpen && !ScriptInventoryAvailable()) {
        m_scriptInventoryOpen = false;
        ZL_WARN("ui", "core_ui 背包 owner 已失效，回退 native 背包");
        ShowNativeInventoryFallback();
    }
    const bool menuOpenAtStart = IsMenuOpen();
    const bool multiplayerOpenAtStart = IsMultiplayerOpen();
    if (m_social != nullptr) {
        m_social->SetSuppressed(menuOpenAtStart || IsInventoryOpen() ||
                                multiplayerOpenAtStart);
    }
    if (input.IsKeyPressed(GLFW_KEY_F11) && !menuOpenAtStart &&
        !multiplayerOpenAtStart && !IsInventoryOpen() &&
        !IsWorldMapOpen() && !IsVehicleModsOpen()) {
        ToggleVisible();
    }

    // 内置 HUD 的 native 版本只作为 fallback：core_ui 脚本存在时由脚本负责；
    // 第一个 tick 前脚本可能还没加载 HUD 文档，因此这里延迟到 owner 已确定后。
    if (m_hudCacheValid && !ScriptHudAvailable()) {
        if (EnsureHudDocument()) {
            if (m_nativeHudNeedsFullRefresh) {
                ApplyHudState(m_hudCache, true);
            }
            if (IsMenuOpen() || IsInventoryOpen() || IsMultiplayerOpen() ||
                IsWorldMapOpen() || IsVehicleModsOpen()) {
                m_hudDocument->Hide();
            } else {
                m_hudDocument->Show();
            }
        }
    } else if (m_hudDocument != nullptr) {
        m_hudDocument->Hide();
    }

    bool escapeHandled = false;
    int consumedKey = -1;
    if (multiplayerOpenAtStart) {
        if (input.IsKeyPressed(GLFW_KEY_ESCAPE)) {
            m_multiplayer->TriggerBack();
            consumedKey = GLFW_KEY_ESCAPE;
            m_consumeGameplayThisFrame = true;
            escapeHandled = true;
        }
    } else if (menuOpenAtStart && m_menu != nullptr) {
        if (input.IsKeyPressed(GLFW_KEY_ESCAPE)) {
            m_menu->OnEscape();
            consumedKey = GLFW_KEY_ESCAPE;
            m_consumeGameplayThisFrame = true;
            escapeHandled = true;
        }
    } else if (IsVehicleModsOpen()) {
        if (input.IsKeyPressed(GLFW_KEY_ESCAPE)) {
            if (m_vehicleModsActionHandler) {
                m_vehicleModsActionHandler(
                    {VehicleModPanelActionType::Close, -1});
            }
            consumedKey = GLFW_KEY_ESCAPE;
            m_consumeGameplayThisFrame = true;
            escapeHandled = true;
        }
    } else if (m_social != nullptr && m_social->IsChatOpen()) {
        if (input.IsKeyPressed(GLFW_KEY_ESCAPE)) {
            m_social->CloseChat();
            consumedKey = GLFW_KEY_ESCAPE;
            m_consumeGameplayThisFrame = true;
            escapeHandled = true;
        }
    } else {
        const bool inventoryOpen = IsInventoryOpen();
        const bool typing = HasInputFocus();
        if (input.IsKeyPressed(GLFW_KEY_I) && (!typing || inventoryOpen) &&
            !IsVehicleModsOpen()) {
            ToggleInventory();
        }
        if (inventoryOpen && input.IsKeyPressed(GLFW_KEY_ESCAPE)) {
            CloseInventory();
            consumedKey = GLFW_KEY_ESCAPE;
            m_consumeGameplayThisFrame = true;
        }
    }

    if (!menuOpenAtStart && !multiplayerOpenAtStart && !IsInventoryOpen() &&
        m_social != nullptr && m_social->IsChatOpen()) {
        const bool enter = input.IsKeyPressed(GLFW_KEY_ENTER);
        const bool keypadEnter = input.IsKeyPressed(GLFW_KEY_KP_ENTER);
        if (enter || keypadEnter) {
            m_social->SubmitChat();
            consumedKey = enter ? GLFW_KEY_ENTER : GLFW_KEY_KP_ENTER;
            m_consumeGameplayThisFrame = true;
        }
    } else if (!menuOpenAtStart && !multiplayerOpenAtStart &&
               !IsInventoryOpen() && IsSocialVisible()) {
        if (input.IsKeyPressed(GLFW_KEY_Y)) {
            if (m_social != nullptr) {
                m_social->OpenChat();
            }
            consumedKey = GLFW_KEY_Y;
            m_consumeGameplayThisFrame = true;
        }
    }

    // 设置页采集按键：在 RmlUi 之前吃掉这枚物理按键，避免它同时触发焦点/文本输入。
    int capturedKey = -1;
    if (menuOpenAtStart && m_menu != nullptr && m_menu->IsCapturingKey() &&
        !escapeHandled) {
        for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
            if (input.IsKeyPressed(key)) {
                m_menu->CaptureKey(key);
                capturedKey = key;
                m_pendingChars.clear();
                m_consumeGameplayThisFrame = true;
                break;
            }
        }
    }

    if (!m_visible && m_hudDocument == nullptr && !IsMenuOpen() &&
        !IsInventoryOpen() && !IsMultiplayerOpen() && !IsSocialVisible() &&
        (m_scriptUi == nullptr || !m_scriptUi->HasVisibleDocuments())) {
        m_pendingChars.clear();
        input.SetGameplayBlocked(false);
        if (m_scriptUi != nullptr) {
            m_scriptUi->EndFrame();
        }
        return;
    }

    const int modifiers = ModifiersFromInput(input);
    const int windowWidth = m_window != nullptr ? std::max(m_window->Width(), 1) : m_width;
    const int windowHeight = m_window != nullptr ? std::max(m_window->Height(), 1) : m_height;
    const float scaleX = static_cast<float>(m_width) / static_cast<float>(windowWidth);
    const float scaleY = static_cast<float>(m_height) / static_cast<float>(windowHeight);
    const int mouseX = static_cast<int>(std::lround(input.MouseX() * scaleX));
    const int mouseY = static_cast<int>(std::lround(input.MouseY() * scaleY));
    if (mouseX != m_mouseX || mouseY != m_mouseY || !m_mouseMoved) {
        m_mouseX = mouseX;
        m_mouseY = mouseY;
        m_mouseMoved = true;
        m_context->ProcessMouseMove(m_mouseX, m_mouseY, modifiers);
    }

    for (int button = 0; button < 8; ++button) {
        if (input.IsMouseButtonPressed(button)) {
            m_context->ProcessMouseButtonDown(button, modifiers);
        }
        if (input.IsMouseButtonReleased(button)) {
            m_context->ProcessMouseButtonUp(button, modifiers);
        }
    }

    const float wheel = input.ScrollDeltaY();
    if (wheel != 0.0f) {
        m_context->ProcessMouseWheel(-wheel, modifiers);
    }

    for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
        if (key == capturedKey || key == consumedKey) {
            continue;
        }
        const Rml::Input::KeyIdentifier identifier = ToRmlKey(key);
        if (identifier == Rml::Input::KI_UNKNOWN) {
            continue;
        }
        if (input.IsKeyPressed(key)) {
            m_context->ProcessKeyDown(identifier, modifiers);
            if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
                m_context->ProcessTextInput('\n');
            }
        }
        if (input.IsKeyReleased(key)) {
            m_context->ProcessKeyUp(identifier, modifiers);
        }
    }

    while (!m_pendingChars.empty()) {
        m_context->ProcessTextInput(static_cast<Rml::Character>(m_pendingChars.front()));
        m_pendingChars.pop_front();
    }

    m_context->Update();
    const bool menuOpen = IsMenuOpen();
    const bool inventoryOpen = IsInventoryOpen();
    const bool multiplayerOpen = IsMultiplayerOpen();
    if (m_social != nullptr) {
        m_social->SetSuppressed(menuOpen || inventoryOpen || multiplayerOpen);
    }
    if (multiplayerOpen != m_multiplayerWasOpen) {
        SyncMenuChrome(multiplayerOpen);
        m_multiplayerWasOpen = multiplayerOpen;
    }
    if (menuOpen != m_menuWasOpen) {
        SyncMenuChrome(menuOpen);
        m_menuWasOpen = menuOpen;
    }
    if (inventoryOpen != m_inventoryWasOpen) {
        SyncInventoryChrome(inventoryOpen);
        m_inventoryWasOpen = inventoryOpen;
    }
    if (m_scriptUi != nullptr) {
        m_scriptUi->EndFrame();
        if (IsVehicleModsOpen()) {
            m_scriptUi->SetGroupVisible(kHudGroup, false);
        }
    }
    const bool focusBlocked = HasInputFocus();
    const bool scriptBlocked = m_scriptUi != nullptr && m_scriptUi->IsInputCaptured();
    const bool scriptConsumed = m_scriptUi != nullptr && m_scriptUi->InputConsumedThisFrame();
    input.SetGameplayBlocked(menuOpen || inventoryOpen || multiplayerOpen ||
                             IsVehicleModsOpen() || focusBlocked ||
                             scriptBlocked || scriptConsumed ||
                             m_consumeGameplayThisFrame);
    m_consumeGameplayThisFrame = false;
}

void UiSystem::Render() {
    if (!m_initialized || m_context == nullptr) {
        return;
    }
    if (!m_visible && m_hudDocument == nullptr &&
        !IsInventoryOpen() && !IsMenuOpen() && !IsMultiplayerOpen() &&
        !IsWorldMapOpen() && !IsVehicleModsOpen() && !IsSocialVisible() &&
        (m_scriptUi == nullptr || !m_scriptUi->HasVisibleDocuments())) {
        return;
    }
    const glm::mat4 view(1.0f);
    const glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(m_width),
                                            static_cast<float>(m_height), 0.0f, -1.0f, 1.0f);
    if ((IsWorldMapOpen() || IsVehicleModsOpen()) &&
        m_scriptUi != nullptr) {
        m_scriptUi->SetGroupVisible(kHudGroup, false);
    }
    bgfx::setViewTransform(static_cast<bgfx::ViewId>(render::ViewId::UI),
                           glm::value_ptr(view), glm::value_ptr(projection));
    // 状态/脚本在 UiSystem::Update 之后修改文本或加载文档；渲染前再布局一次，
    // 否则新增文本要等下一帧 Update 才进入几何（暂停/菜单下尤其明显）。
    m_context->Update();
    m_context->Render();
    UpdateFontAtlasStats();
}

void UiSystem::UpdateFontAtlasStats() {
    if (m_renderer == nullptr || m_atlasStatsElement == nullptr) {
        return;
    }
    const auto stats = m_renderer->GetFontAtlasStats();
    if (stats.pages == m_lastAtlasPages && stats.bytes == m_lastAtlasBytes) {
        return;
    }
    const bool first = m_lastAtlasPages == 0xffffffffu;
    m_lastAtlasPages = stats.pages;
    m_lastAtlasBytes = stats.bytes;
    const bool overBudget =
        stats.pages > kFontAtlasPageBudget || stats.bytes > kFontAtlasByteBudget;
    const double megabytes = static_cast<double>(stats.bytes) / (1024.0 * 1024.0);
    char text[128];
    std::snprintf(text, sizeof(text), "%u 页 · %.1f MB · 预算 %u 页", stats.pages, megabytes,
                  kFontAtlasPageBudget);
    m_atlasStatsElement->SetInnerRML(text);
    m_atlasStatsElement->SetClass("over-budget", overBudget);
    if (first) {
        ZL_INFO("ui", "字体图集：{} 页 / {:.1f} MB（预算 {} 页 / {} MB，CJK 字形按需加载）",
                stats.pages, megabytes, kFontAtlasPageBudget,
                kFontAtlasByteBudget / (1024 * 1024));
    }
    if (overBudget && !m_atlasBudgetWarned) {
        m_atlasBudgetWarned = true;
        ZL_WARN("ui", "字体图集超预算：{} 页 / {:.1f} MB（预算 {} 页 / {} MB）；"
                      "检查字号数量与 CJK 字符覆盖",
                stats.pages, megabytes, kFontAtlasPageBudget,
                kFontAtlasByteBudget / (1024 * 1024));
    }
}

void UiSystem::SetHudState(const HudState& state) {
    const bool first = !m_hudCacheValid;
    if (m_hudDocument != nullptr && !ScriptHudAvailable()) {
        ApplyHudState(state, first);
    }
    m_hudCache = state;
    m_hudCacheValid = true;
}

void UiSystem::SetWorldMapState(const game::WorldMapState& state, bool open) {
    if (!open) {
        if (m_worldMap != nullptr) {
            m_worldMap->SetState(state, false);
        }
        if (m_scriptUi != nullptr) {
            m_scriptUi->SetGroupVisible("hud", true);
        }
        return;
    }
    if (!EnsureWorldMapPanel()) {
        return;
    }
    m_worldMap->SetCloseHandler(m_worldMapCloseHandler);
    m_worldMap->SetSelectHandler(m_worldMapSelectHandler);
    m_worldMap->SetStrategicActionHandler(
        m_worldMapStrategicActionHandler);
    m_worldMap->SetState(state, true);
    if (m_hudDocument != nullptr) {
        m_hudDocument->Hide();
    }
    if (m_scriptUi != nullptr) {
        m_scriptUi->SetGroupVisible("hud", false);
    }
}

bool UiSystem::IsWorldMapOpen() const {
    return m_worldMap != nullptr && m_worldMap->IsOpen();
}

void UiSystem::SetWorldMapCloseHandler(std::function<void()> handler) {
    m_worldMapCloseHandler = std::move(handler);
    if (m_worldMap != nullptr) {
        m_worldMap->SetCloseHandler(m_worldMapCloseHandler);
    }
}

void UiSystem::SetWorldMapSelectHandler(
    std::function<void(const std::string&)> handler) {
    m_worldMapSelectHandler = std::move(handler);
    if (m_worldMap != nullptr) {
        m_worldMap->SetSelectHandler(m_worldMapSelectHandler);
    }
}

void UiSystem::SetWorldMapStrategicActionHandler(
    std::function<void(const std::string&,
                       game::WorldMapStrategicAction)> handler) {
    m_worldMapStrategicActionHandler = std::move(handler);
    if (m_worldMap != nullptr) {
        m_worldMap->SetStrategicActionHandler(
            m_worldMapStrategicActionHandler);
    }
}

void UiSystem::SetVehicleModsState(const VehicleModsUiState& state,
                                   bool open) {
    m_vehicleModsState = state;
    m_vehicleModsState.open = open;
    if (!open) {
        if (m_vehicleMods != nullptr) {
            m_vehicleMods->SetState(m_vehicleModsState);
        }
        if (m_scriptUi != nullptr) {
            m_scriptUi->SetGroupVisible(kHudGroup, true);
        }
        return;
    }
    if (!EnsureVehicleModsPanel()) {
        return;
    }
    m_vehicleMods->SetActionHandler(m_vehicleModsActionHandler);
    m_vehicleMods->SetState(m_vehicleModsState);
    if (m_hudDocument != nullptr) {
        m_hudDocument->Hide();
    }
    if (m_scriptUi != nullptr) {
        m_scriptUi->SetGroupVisible(kHudGroup, false);
    }
}

void UiSystem::SetVehicleModsActionHandler(
    VehicleModPanelActionHandler handler) {
    m_vehicleModsActionHandler = std::move(handler);
    if (m_vehicleMods != nullptr) {
        m_vehicleMods->SetActionHandler(m_vehicleModsActionHandler);
    }
}

bool UiSystem::IsVehicleModsOpen() const {
    return m_vehicleMods != nullptr && m_vehicleMods->IsOpen();
}

void UiSystem::ApplyHudState(const HudState& state, bool force) {
    if (m_hudDocument == nullptr) {
        return;
    }
    const bool first = force || !m_hudCacheValid;
    m_nativeHudNeedsFullRefresh = false;

    auto setText = [](Rml::Element* element, const std::string& text) {
        if (element != nullptr) {
            element->SetInnerRML(text);
        }
    };
    auto setModText = [](Rml::Element* element, const std::string& text) {
        if (element != nullptr) {
            element->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
        }
    };
    auto setBar = [&](Rml::Element* fill, Rml::Element* label, float value, float maxValue,
                      bool valid) {
        const float ratio =
            valid && maxValue > 0.0f ? std::clamp(value / maxValue, 0.0f, 1.0f) : 0.0f;
        char width[32];
        std::snprintf(width, sizeof(width), "%.1f%%", ratio * 100.0f);
        if (fill != nullptr) {
            fill->SetProperty("width", width);
        }
        if (label != nullptr) {
            label->SetInnerRML(valid ? std::to_string(static_cast<int>(std::lround(value)))
                                     : "--");
        }
    };

    if (first || state.day != m_hudCache.day) {
        setText(m_hudDayElement, state.day);
    }
    if (first || state.time != m_hudCache.time) {
        setText(m_hudTimeElement, state.time);
    }
    if (first || state.speed != m_hudCache.speed) {
        setText(m_hudSpeedElement, state.speed);
        if (m_hudSpeedElement != nullptr) {
            m_hudSpeedElement->SetClass("paused", state.speed == "暂停");
        }
    }
    if (first || state.playerValid != m_hudCache.playerValid ||
        state.health != m_hudCache.health || state.healthMax != m_hudCache.healthMax) {
        setBar(m_hpFillElement, m_hpTextElement, state.health, state.healthMax,
               state.playerValid);
        if (m_hpFillElement != nullptr) {
            m_hpFillElement->SetClass("low",
                                      state.playerValid && state.healthMax > 0.0f &&
                                          state.health / state.healthMax < 0.3f);
        }
    }
    if (first || state.playerValid != m_hudCache.playerValid ||
        state.stamina != m_hudCache.stamina || state.staminaMax != m_hudCache.staminaMax) {
        setBar(m_staminaFillElement, m_staminaTextElement, state.stamina, state.staminaMax,
               state.playerValid);
        if (m_staminaFillElement != nullptr) {
            m_staminaFillElement->SetClass(
                "low", state.playerValid && state.staminaMax > 0.0f &&
                           state.stamina / state.staminaMax < 0.3f);
        }
    }
    if (first || state.combatStatus != m_hudCache.combatStatus ||
        state.combatTone != m_hudCache.combatTone) {
        setText(m_combatStatusElement, state.combatStatus);
        if (m_combatStatusElement != nullptr) {
            m_combatStatusElement->SetProperty(
                "display", state.combatStatus.empty() ? "none" : "block");
            m_combatStatusElement->SetClass("status-warn",
                                            state.combatTone == "warn");
            m_combatStatusElement->SetClass("status-fail",
                                            state.combatTone == "fail");
        }
    }
    if (first || state.stealthStatus != m_hudCache.stealthStatus ||
        state.stealthTone != m_hudCache.stealthTone) {
        setText(m_stealthStatusElement, state.stealthStatus);
        if (m_stealthStatusElement != nullptr) {
            m_stealthStatusElement->SetProperty(
                "display", state.stealthStatus.empty() ? "none" : "block");
            m_stealthStatusElement->SetClass("status-ok",
                                             state.stealthTone == "ok");
            m_stealthStatusElement->SetClass("status-warn",
                                             state.stealthTone == "warn");
            m_stealthStatusElement->SetClass("status-fail",
                                             state.stealthTone == "fail");
        }
    }
    if (first || state.stealthPrompt != m_hudCache.stealthPrompt) {
        setText(m_stealthPromptElement, state.stealthPrompt);
        if (m_stealthPromptElement != nullptr) {
            m_stealthPromptElement->SetProperty(
                "display", state.stealthPrompt.empty() ? "none" : "block");
        }
    }
    if (first || state.rangedStatus != m_hudCache.rangedStatus ||
        state.rangedTone != m_hudCache.rangedTone) {
        setText(m_rangedStatusElement, state.rangedStatus);
        if (m_rangedStatusElement != nullptr) {
            m_rangedStatusElement->SetProperty(
                "display", state.rangedStatus.empty() ? "none" : "block");
            m_rangedStatusElement->SetClass("status-warn",
                                            state.rangedTone == "warn");
            m_rangedStatusElement->SetClass("status-fail",
                                            state.rangedTone == "fail");
        }
    }
    if (first || state.throwStatus != m_hudCache.throwStatus ||
        state.throwTone != m_hudCache.throwTone) {
        setText(m_throwStatusElement, state.throwStatus);
        if (m_throwStatusElement != nullptr) {
            m_throwStatusElement->SetProperty(
                "display", state.throwStatus.empty() ? "none" : "block");
            m_throwStatusElement->SetClass("status-warn",
                                           state.throwTone == "warn");
            m_throwStatusElement->SetClass("status-fail",
                                           state.throwTone == "fail");
            m_throwStatusElement->SetClass("status-ok",
                                           state.throwTone == "ok");
        }
    }
    if (first || state.forageStatus != m_hudCache.forageStatus ||
        state.forageTone != m_hudCache.forageTone) {
        setText(m_forageStatusElement, state.forageStatus);
        if (m_forageStatusElement != nullptr) {
            m_forageStatusElement->SetProperty(
                "display", state.forageStatus.empty() ? "none" : "block");
            m_forageStatusElement->SetClass("status-warn",
                                            state.forageTone == "warn");
            m_forageStatusElement->SetClass("status-fail",
                                            state.forageTone == "fail");
        }
    }
    if (first || state.fishingStatus != m_hudCache.fishingStatus ||
        state.fishingTone != m_hudCache.fishingTone) {
        setText(m_fishingStatusElement, state.fishingStatus);
        if (m_fishingStatusElement != nullptr) {
            m_fishingStatusElement->SetProperty(
                "display", state.fishingStatus.empty() ? "none" : "block");
            m_fishingStatusElement->SetClass("status-warn",
                                             state.fishingTone == "warn");
            m_fishingStatusElement->SetClass("status-fail",
                                             state.fishingTone == "fail");
        }
    }
    if (first || state.needsStatus != m_hudCache.needsStatus ||
        state.needsTone != m_hudCache.needsTone) {
        setText(m_needsStatusElement, state.needsStatus);
        if (m_needsStatusElement != nullptr) {
            m_needsStatusElement->SetProperty(
                "display", state.needsStatus.empty() ? "none" : "block");
            m_needsStatusElement->SetClass("status-warn", state.needsTone == "warn");
            m_needsStatusElement->SetClass("status-fail", state.needsTone == "fail");
        }
    }
    if (first || state.woundStatus != m_hudCache.woundStatus ||
        state.woundTone != m_hudCache.woundTone) {
        setText(m_woundStatusElement, state.woundStatus);
        if (m_woundStatusElement != nullptr) {
            m_woundStatusElement->SetProperty(
                "display", state.woundStatus.empty() ? "none" : "block");
            m_woundStatusElement->SetClass("status-warn", state.woundTone == "warn");
            m_woundStatusElement->SetClass("status-fail", state.woundTone == "fail");
        }
    }
    if (first || state.painStatus != m_hudCache.painStatus ||
        state.painTone != m_hudCache.painTone) {
        setText(m_painStatusElement, state.painStatus);
        if (m_painStatusElement != nullptr) {
            m_painStatusElement->SetProperty(
                "display", state.painStatus.empty() ? "none" : "block");
            m_painStatusElement->SetClass("status-warn", state.painTone == "warn");
            m_painStatusElement->SetClass("status-fail", state.painTone == "fail");
        }
    }
    if (first || state.armorStatus != m_hudCache.armorStatus ||
        state.armorTone != m_hudCache.armorTone) {
        setText(m_armorStatusElement, state.armorStatus);
        if (m_armorStatusElement != nullptr) {
            m_armorStatusElement->SetProperty(
                "display", state.armorStatus.empty() ? "none" : "block");
            m_armorStatusElement->SetClass("status-warn", state.armorTone == "warn");
            m_armorStatusElement->SetClass("status-fail", state.armorTone == "fail");
        }
    }
    if (first || state.infectionStatus != m_hudCache.infectionStatus ||
        state.infectionTone != m_hudCache.infectionTone) {
        setText(m_infectionStatusElement, state.infectionStatus);
        if (m_infectionStatusElement != nullptr) {
            m_infectionStatusElement->SetProperty(
                "display", state.infectionStatus.empty() ? "none" : "block");
            m_infectionStatusElement->SetClass(
                "status-warn", state.infectionTone == "warn");
            m_infectionStatusElement->SetClass(
                "status-fail", state.infectionTone == "fail");
        }
    }
    if (first || state.weatherStatus != m_hudCache.weatherStatus ||
        state.weatherTone != m_hudCache.weatherTone ||
        state.weatherClass != m_hudCache.weatherClass) {
        setText(m_weatherStatusElement, state.weatherStatus);
        if (m_weatherStatusElement != nullptr) {
            m_weatherStatusElement->SetProperty(
                "display", state.weatherStatus.empty() ? "none" : "block");
            m_weatherStatusElement->SetClass("status-warn",
                                             state.weatherTone == "warn");
            m_weatherStatusElement->SetClass("status-fail",
                                             state.weatherTone == "fail");
        }
        if (m_weatherOverlayElement != nullptr) {
            const std::string overlayClass =
                state.weatherClass.empty() ? "clear" : state.weatherClass;
            m_weatherOverlayElement->SetClassNames("weather-overlay weather-" +
                                                   overlayClass);
            m_weatherOverlayElement->SetProperty(
                "display", overlayClass == "clear" ? "none" : "block");
        }
    }
    const bool threatActive =
        !state.threatDirection.empty() && state.threatRemaining > 0.0f;
    if (first || threatActive ||
        state.threatStatus != m_hudCache.threatStatus ||
        state.threatRemaining != m_hudCache.threatRemaining ||
        state.threatDirection != m_hudCache.threatDirection) {
        const float ratio = state.threatDuration > 0.0f
                                ? std::clamp(state.threatRemaining /
                                                 state.threatDuration,
                                             0.0f, 1.0f)
                                : 1.0f;
        char alpha[16];
        std::snprintf(alpha, sizeof(alpha), "%.2f", ratio);
        static constexpr const char* kDirections[4] = {"left", "right", "top",
                                                       "bottom"};
        for (std::size_t i = 0; i < m_threatEdges.size(); ++i) {
            Rml::Element* edge = m_threatEdges[i];
            if (edge == nullptr) {
                continue;
            }
            const bool active =
                threatActive && state.threatDirection == kDirections[i];
            edge->SetProperty("display", active ? "block" : "none");
            if (active) {
                edge->SetProperty("opacity", alpha);
            }
        }
        setText(m_threatStatusElement, state.threatStatus);
        if (m_threatStatusElement != nullptr) {
            m_threatStatusElement->SetProperty(
                "display", state.threatStatus.empty() ? "none" : "block");
        }
    }
    if (first || state.objective != m_hudCache.objective ||
        state.objectiveTone != m_hudCache.objectiveTone ||
        state.objectiveHint != m_hudCache.objectiveHint ||
        state.poiGuide != m_hudCache.poiGuide ||
        state.poiGuideTone != m_hudCache.poiGuideTone ||
        state.poiStageVisible != m_hudCache.poiStageVisible ||
        state.poiStageTitle != m_hudCache.poiStageTitle ||
        state.poiStageObjective != m_hudCache.poiStageObjective ||
        state.poiStageMeta != m_hudCache.poiStageMeta ||
        state.poiStageNarrative != m_hudCache.poiStageNarrative ||
        state.poiStageTone != m_hudCache.poiStageTone ||
        state.poiResultVisible != m_hudCache.poiResultVisible ||
        state.poiResultTitle != m_hudCache.poiResultTitle ||
        state.poiResultBody != m_hudCache.poiResultBody ||
        state.poiResultTone != m_hudCache.poiResultTone ||
        state.missionGuide != m_hudCache.missionGuide ||
        state.missionTone != m_hudCache.missionTone ||
        state.eventGuide != m_hudCache.eventGuide ||
        state.eventTone != m_hudCache.eventTone ||
        state.regionGuide != m_hudCache.regionGuide ||
        state.regionTone != m_hudCache.regionTone ||
        state.settlementGuide != m_hudCache.settlementGuide ||
        state.settlementTone != m_hudCache.settlementTone ||
        state.strategicGuide != m_hudCache.strategicGuide ||
        state.strategicTone != m_hudCache.strategicTone) {
        setText(m_objectiveElement, state.objective);
        setText(m_objectiveHintElement, state.objectiveHint);
        setText(m_poiGuideElement, state.poiGuide);
        setText(m_poiStageTitleElement, state.poiStageTitle);
        setText(m_poiStageObjectiveElement, state.poiStageObjective);
        setText(m_poiStageMetaElement, state.poiStageMeta);
        setText(m_poiStageNarrativeElement, state.poiStageNarrative);
        setText(m_poiResultTitleElement, state.poiResultTitle);
        setText(m_poiResultBodyElement, state.poiResultBody);
        setText(m_missionGuideElement, state.missionGuide);
        setText(m_eventGuideElement, state.eventGuide);
        setText(m_regionGuideElement, state.regionGuide);
        setText(m_settlementGuideElement, state.settlementGuide);
        setText(m_strategicGuideElement, state.strategicGuide);
        if (m_objectivePanelElement != nullptr) {
            m_objectivePanelElement->SetProperty(
                "display", state.objective.empty() &&
                                   !state.poiStageVisible &&
                                   state.missionGuide.empty() &&
                                   state.eventGuide.empty() &&
                                   state.regionGuide.empty() &&
                                   state.settlementGuide.empty() &&
                                   state.strategicGuide.empty()
                               ? "none"
                               : "block");
        }
        if (m_objectiveElement != nullptr) {
            m_objectiveElement->SetClass("objective-warn", state.objectiveTone == "warn");
            m_objectiveElement->SetClass("objective-ok", state.objectiveTone == "ok");
        }
        if (m_objectiveHintElement != nullptr) {
            m_objectiveHintElement->SetProperty(
                "display", state.objectiveHint.empty() ? "none" : "block");
        }
        if (m_poiGuideElement != nullptr) {
            m_poiGuideElement->SetProperty(
                "display", state.poiGuide.empty() ? "none" : "block");
            m_poiGuideElement->SetClass("status-warn",
                                        state.poiGuideTone == "warn");
            m_poiGuideElement->SetClass("status-ok", state.poiGuideTone == "ok");
        }
        if (m_poiStagePanelElement != nullptr) {
            m_poiStagePanelElement->SetProperty(
                "display", state.poiStageVisible ? "block" : "none");
            m_poiStagePanelElement->SetClass("status-warn",
                                             state.poiStageTone == "warn");
            m_poiStagePanelElement->SetClass("status-fail",
                                             state.poiStageTone == "fail");
            m_poiStagePanelElement->SetClass("status-ok",
                                             state.poiStageTone == "ok");
        }
        if (m_poiStageNarrativeElement != nullptr) {
            m_poiStageNarrativeElement->SetProperty(
                "display",
                state.poiStageNarrative.empty() ? "none" : "block");
        }
        if (m_poiResultPanelElement != nullptr) {
            m_poiResultPanelElement->SetProperty(
                "display", state.poiResultVisible ? "block" : "none");
            m_poiResultPanelElement->SetClass(
                "status-fail", state.poiResultTone == "fail");
        }
        if (m_missionGuideElement != nullptr) {
            m_missionGuideElement->SetProperty(
                "display", state.missionGuide.empty() ? "none" : "block");
            m_missionGuideElement->SetClass("status-warn",
                                            state.missionTone == "warn");
            m_missionGuideElement->SetClass("status-ok",
                                            state.missionTone == "ok");
        }
        if (m_eventGuideElement != nullptr) {
            m_eventGuideElement->SetProperty(
                "display", state.eventGuide.empty() ? "none" : "block");
            m_eventGuideElement->SetClass("status-warn",
                                          state.eventTone == "warn");
            m_eventGuideElement->SetClass("status-ok",
                                          state.eventTone == "ok");
            m_eventGuideElement->SetClass("status-fail",
                                          state.eventTone == "fail");
        }
        if (m_regionGuideElement != nullptr) {
            m_regionGuideElement->SetProperty(
                "display", state.regionGuide.empty() ? "none" : "block");
            m_regionGuideElement->SetClass("status-warn",
                                           state.regionTone == "warn");
            m_regionGuideElement->SetClass("status-ok",
                                           state.regionTone == "ok");
        }
        if (m_settlementGuideElement != nullptr) {
            m_settlementGuideElement->SetProperty(
                "display", state.settlementGuide.empty() ? "none" : "block");
            m_settlementGuideElement->SetClass(
                "status-warn", state.settlementTone == "warn");
            m_settlementGuideElement->SetClass(
                "status-fail", state.settlementTone == "fail");
            m_settlementGuideElement->SetClass(
                "status-ok", state.settlementTone == "ok");
        }
        if (m_strategicGuideElement != nullptr) {
            m_strategicGuideElement->SetProperty(
                "display", state.strategicGuide.empty() ? "none" : "block");
            m_strategicGuideElement->SetClass(
                "status-warn", state.strategicTone == "warn");
            m_strategicGuideElement->SetClass(
                "status-fail", state.strategicTone == "fail");
        }
    }
    const bool worldEventChanged =
        first || state.worldEventVisible != m_hudCache.worldEventVisible ||
        state.worldEventCanChoose != m_hudCache.worldEventCanChoose ||
        state.worldEventChoiceOpen != m_hudCache.worldEventChoiceOpen ||
        state.worldEventName != m_hudCache.worldEventName ||
        state.worldEventMeta != m_hudCache.worldEventMeta ||
        state.worldEventStage != m_hudCache.worldEventStage ||
        state.worldEventHint != m_hudCache.worldEventHint ||
        state.worldEventTone != m_hudCache.worldEventTone ||
        state.worldEventResult != m_hudCache.worldEventResult ||
        state.worldEventResultTone != m_hudCache.worldEventResultTone ||
        state.worldEventOptions != m_hudCache.worldEventOptions ||
        state.worldEventOptionCount != m_hudCache.worldEventOptionCount ||
        state.worldEventLog != m_hudCache.worldEventLog ||
        state.worldEventLogCount != m_hudCache.worldEventLogCount;
    if (worldEventChanged) {
        if (m_worldEventPanelElement != nullptr) {
            m_worldEventPanelElement->SetProperty(
                "display", state.worldEventVisible ? "block" : "none");
            m_worldEventPanelElement->SetClass(
                "status-warn", state.worldEventTone == "warn");
            m_worldEventPanelElement->SetClass(
                "status-ok", state.worldEventTone == "ok");
            m_worldEventPanelElement->SetClass(
                "status-fail", state.worldEventTone == "fail");
        }
        setModText(m_worldEventNameElement, state.worldEventName);
        setModText(m_worldEventMetaElement, state.worldEventMeta);
        setModText(m_worldEventStageElement, state.worldEventStage);
        setModText(m_worldEventHintElement, state.worldEventHint);
        setModText(m_worldEventResultElement, state.worldEventResult);
        if (m_worldEventResultElement != nullptr) {
            m_worldEventResultElement->SetProperty(
                "display", state.worldEventVisible &&
                                   !state.worldEventResult.empty()
                               ? "block"
                               : "none");
            m_worldEventResultElement->SetClass(
                "status-ok", state.worldEventResultTone == "ok");
            m_worldEventResultElement->SetClass(
                "status-fail", state.worldEventResultTone == "fail");
        }
        if (m_worldEventHintElement != nullptr) {
            m_worldEventHintElement->SetProperty(
                "display", state.worldEventVisible &&
                                   !state.worldEventHint.empty()
                               ? "block"
                               : "none");
        }
        const bool showOptions =
            state.worldEventVisible && state.worldEventCanChoose &&
            state.worldEventOptionCount > 0;
        if (m_worldEventOptionsElement != nullptr) {
            m_worldEventOptionsElement->SetProperty(
                "display", showOptions ? "block" : "none");
        }
        for (std::size_t i = 0; i < m_worldEventOptionElements.size(); ++i) {
            Rml::Element* option = m_worldEventOptionElements[i];
            if (option == nullptr) {
                continue;
            }
            const bool visible =
                showOptions &&
                static_cast<int>(i) < state.worldEventOptionCount;
            option->SetProperty("display", visible ? "block" : "none");
            if (visible) {
                setModText(option,
                           state.worldEventOptions[i]);
                option->SetClass("active",
                                 state.worldEventChoiceOpen);
            }
        }
        if (m_worldEventLogElement != nullptr) {
            m_worldEventLogElement->SetProperty(
                "display",
                state.worldEventVisible && state.worldEventLogCount > 0
                    ? "block"
                    : "none");
        }
        for (std::size_t i = 0; i < m_worldEventLogLines.size(); ++i) {
            Rml::Element* line = m_worldEventLogLines[i];
            if (line == nullptr) {
                continue;
            }
            const bool visible =
                state.worldEventVisible &&
                static_cast<int>(i) < state.worldEventLogCount;
            line->SetProperty("display", visible ? "block" : "none");
            if (visible) {
                setModText(line, state.worldEventLog[i]);
            }
        }
    }
    if (first || state.siegeStatus != m_hudCache.siegeStatus ||
        state.siegeTone != m_hudCache.siegeTone) {
        setText(m_siegeStatusElement, state.siegeStatus);
        if (m_siegeStatusElement != nullptr) {
            m_siegeStatusElement->SetProperty(
                "display", state.siegeStatus.empty() ? "none" : "block");
            m_siegeStatusElement->SetClass("status-warn",
                                           state.siegeTone == "warn");
            m_siegeStatusElement->SetClass("status-fail",
                                           state.siegeTone == "fail");
        }
    }
    if (first || state.baseStatus != m_hudCache.baseStatus ||
        state.baseTone != m_hudCache.baseTone) {
        setText(m_baseStatusElement, state.baseStatus);
        if (m_baseStatusElement != nullptr) {
            m_baseStatusElement->SetProperty(
                "display", state.baseStatus.empty() ? "none" : "block");
            m_baseStatusElement->SetClass("status-warn",
                                          state.baseTone == "warn");
            m_baseStatusElement->SetClass("status-fail",
                                          state.baseTone == "fail");
        }
    }
    bool modFieldsChanged =
        first || state.modFieldCount != m_hudCache.modFieldCount;
    for (int i = 0; i < state.modFieldCount && !modFieldsChanged; ++i) {
        const std::size_t index = static_cast<std::size_t>(i);
        if (!(state.modFields[index] == m_hudCache.modFields[index])) {
            modFieldsChanged = true;
        }
    }
    if (modFieldsChanged) {
        bool anyVisible = false;
        for (std::size_t i = 0; i < m_modFieldRows.size(); ++i) {
            Rml::Element* row = m_modFieldRows[i];
            const bool visible =
                static_cast<int>(i) < state.modFieldCount &&
                !state.modFields[i].text.empty();
            if (row != nullptr) {
                row->SetProperty("display", visible ? "block" : "none");
                row->SetClass("status-warn",
                              visible && state.modFields[i].tone == "warn");
                row->SetClass("status-ok",
                              visible && state.modFields[i].tone == "ok");
                row->SetClass("status-fail",
                              visible && state.modFields[i].tone == "fail");
            }
            if (visible) {
                anyVisible = true;
                setModText(m_modFieldLabels[i], state.modFields[i].label);
                setModText(m_modFieldValues[i], state.modFields[i].text);
            }
        }
        if (m_modFieldsElement != nullptr) {
            m_modFieldsElement->SetProperty("display",
                                            anyVisible ? "block" : "none");
        }
    }
    if (first || state.siegeReportOpen != m_hudCache.siegeReportOpen ||
        state.siegeReportTitle != m_hudCache.siegeReportTitle ||
        state.siegeReportRating != m_hudCache.siegeReportRating ||
        state.siegeReportTone != m_hudCache.siegeReportTone ||
        state.siegeReportHint != m_hudCache.siegeReportHint ||
        state.siegeReportLines != m_hudCache.siegeReportLines) {
        setText(m_siegeReportTitleElement, state.siegeReportTitle);
        setText(m_siegeReportRatingElement, state.siegeReportRating);
        setText(m_siegeReportHintElement, state.siegeReportHint);
        if (m_siegeReportElement != nullptr) {
            m_siegeReportElement->SetProperty(
                "display", state.siegeReportOpen ? "block" : "none");
            m_siegeReportElement->SetClass("status-warn",
                                           state.siegeReportTone == "warn");
            m_siegeReportElement->SetClass("status-fail",
                                           state.siegeReportTone == "fail");
        }
        if (m_siegeReportRatingElement != nullptr) {
            m_siegeReportRatingElement->SetProperty(
                "display", state.siegeReportRating.empty() ? "none" : "block");
        }
        for (std::size_t i = 0; i < m_siegeReportLineElements.size(); ++i) {
            Rml::Element* line = m_siegeReportLineElements[i];
            if (line == nullptr) {
                continue;
            }
            const std::string& text = state.siegeReportLines[i];
            setText(line, text);
            line->SetProperty("display", text.empty() ? "none" : "block");
        }
    }
    const bool progressionChanged =
        first || state.progressionValid != m_hudCache.progressionValid ||
        state.progressionLevel != m_hudCache.progressionLevel ||
        state.progressionXp != m_hudCache.progressionXp ||
        state.progressionXpNext != m_hudCache.progressionXpNext ||
        state.progressionPerkPoints != m_hudCache.progressionPerkPoints ||
        state.progressionSkills != m_hudCache.progressionSkills ||
        state.progressionPerks != m_hudCache.progressionPerks ||
        state.progressionChoices != m_hudCache.progressionChoices ||
        state.progressionChoiceCount != m_hudCache.progressionChoiceCount ||
        state.perkPrompt != m_hudCache.perkPrompt ||
        state.characterOpen != m_hudCache.characterOpen;
    if (progressionChanged) {
        const float ratio =
            state.progressionXpNext > 0.0f
                ? std::clamp(state.progressionXp / state.progressionXpNext, 0.0f,
                             1.0f)
                : (state.progressionValid ? 1.0f : 0.0f);
        char percent[24];
        std::snprintf(percent, sizeof(percent), "%.1f%%",
                      static_cast<double>(ratio * 100.0f));
        if (m_levelPanelElement != nullptr) {
            m_levelPanelElement->SetProperty(
                "display", state.progressionValid ? "block" : "none");
        }
        setText(m_levelTextElement,
                "Lv." + std::to_string(state.progressionLevel));
        setText(m_levelPerkElement,
                state.progressionPerkPoints > 0
                    ? "Perk×" + std::to_string(state.progressionPerkPoints)
                    : "");
        if (m_levelPerkElement != nullptr) {
            m_levelPerkElement->SetProperty(
                "display",
                state.progressionPerkPoints > 0 ? "block" : "none");
        }
        if (m_levelFillElement != nullptr) {
            m_levelFillElement->SetProperty("width", percent);
        }
        setText(m_levelXpElement,
                state.progressionXpNext > 0.0f
                    ? std::to_string(static_cast<int>(std::lround(
                          state.progressionXp))) +
                          " / " +
                          std::to_string(static_cast<int>(std::lround(
                              state.progressionXpNext)))
                    : "MAX");
        setText(m_perkPromptElement, state.perkPrompt);
        if (m_perkPromptElement != nullptr) {
            m_perkPromptElement->SetProperty(
                "display", state.perkPrompt.empty() ? "none" : "block");
        }
        if (m_characterPanelElement != nullptr) {
            m_characterPanelElement->SetProperty(
                "display",
                state.characterOpen && state.progressionValid ? "block" : "none");
        }
        const std::string characterLevel =
            "等级 " + std::to_string(state.progressionLevel) +
            (state.progressionPerkPoints > 0
                 ? " · 待选 Perk ×" +
                       std::to_string(state.progressionPerkPoints)
                 : std::string());
        setText(m_characterLevelElement, characterLevel);
        if (m_characterXpFillElement != nullptr) {
            m_characterXpFillElement->SetProperty("width", percent);
        }
        setText(m_characterXpElement,
                state.progressionXpNext > 0.0f
                    ? "XP " +
                          std::to_string(static_cast<int>(std::lround(
                              state.progressionXp))) +
                          " / " +
                          std::to_string(static_cast<int>(std::lround(
                              state.progressionXpNext)))
                    : "XP MAX");
        for (std::size_t i = 0; i < m_characterSkillElements.size(); ++i) {
            setText(m_characterSkillElements[i], state.progressionSkills[i]);
        }
        setText(m_characterPerksElement, state.progressionPerks);
        for (std::size_t i = 0; i < m_perkChoiceElements.size(); ++i) {
            setText(m_perkChoiceElements[i], state.progressionChoices[i]);
            if (m_perkChoiceElements[i] != nullptr) {
                m_perkChoiceElements[i]->SetProperty(
                    "display",
                    state.progressionChoices[i].empty() ? "none" : "block");
            }
        }
    }
    const bool companionChanged =
        first || !(state.companion == m_hudCache.companion);
    if (companionChanged) {
        const CompanionUiState& companion = state.companion;
        if (m_companionPanelElement != nullptr) {
            m_companionPanelElement->SetProperty(
                "display",
                companion.valid && companion.open ? "block" : "none");
        }
        setText(m_companionNameElement, companion.name);
        setText(m_companionHealthElement, companion.health);
        setText(m_companionStatusElement, companion.status);
        setText(m_companionTrustElement, companion.trust);
        setText(m_companionEquipmentElement, companion.equipment);
        setText(m_companionBagElement, companion.bag);
        setText(m_companionQuestElement, companion.quest);
        setText(m_companionHintElement, companion.hint);
        if (m_companionHealthFillElement != nullptr) {
            const float ratio =
                companion.healthMax > 0.0f
                    ? std::clamp(companion.healthCurrent /
                                     companion.healthMax,
                                 0.0f, 1.0f)
                    : 0.0f;
            char width[24];
            std::snprintf(width, sizeof(width), "%.1f%%",
                          static_cast<double>(ratio * 100.0f));
            m_companionHealthFillElement->SetProperty("width", width);
            m_companionHealthFillElement->SetClass(
                "low", companion.lowHealth || companion.downed ||
                           companion.dead);
        }
        setText(m_companionAlertElement, companion.alert);
        if (m_companionAlertElement != nullptr) {
            m_companionAlertElement->SetProperty(
                "display",
                companion.valid && !companion.alert.empty() ? "block"
                                                            : "none");
            m_companionAlertElement->SetClassNames(
                "companion-status status-" + companion.alertTone);
        }
    }
    const bool dialogueChanged =
        first || state.dialogueOpen != m_hudCache.dialogueOpen ||
        state.dialogueSpeaker != m_hudCache.dialogueSpeaker ||
        state.dialogueText != m_hudCache.dialogueText ||
        state.dialogueOptions != m_hudCache.dialogueOptions ||
        state.dialogueOptionCount != m_hudCache.dialogueOptionCount ||
        state.dialogueSelected != m_hudCache.dialogueSelected ||
        state.dialogueHint != m_hudCache.dialogueHint;
    if (dialogueChanged) {
        if (m_dialoguePanelElement != nullptr) {
            m_dialoguePanelElement->SetProperty(
                "display", state.dialogueOpen ? "block" : "none");
        }
        setText(m_dialogueSpeakerElement, state.dialogueSpeaker);
        setText(m_dialogueTextElement, state.dialogueText);
        setText(m_dialogueHintElement, state.dialogueHint);
        for (int i = 0; i < 5; ++i) {
            Rml::Element* option =
                m_dialogueOptionElements[static_cast<std::size_t>(i)];
            if (option == nullptr) {
                continue;
            }
            const bool visible = state.dialogueOpen &&
                                 i < state.dialogueOptionCount;
            option->SetProperty("display", visible ? "block" : "none");
            if (visible) {
                setText(option,
                        state.dialogueOptions[static_cast<std::size_t>(i)]);
                option->SetClass("selected", i == state.dialogueSelected);
            }
        }
    }
    if (first || state.actionHint != m_hudCache.actionHint ||
        state.actionTone != m_hudCache.actionTone) {
        setText(m_actionHintElement, state.actionHint);
        if (m_actionHintElement != nullptr) {
            m_actionHintElement->SetProperty(
                "display", state.actionHint.empty() ? "none" : "block");
            m_actionHintElement->SetClass("status-warn", state.actionTone == "warn");
            m_actionHintElement->SetClass("status-fail", state.actionTone == "fail");
        }
    }
    if (first || state.selectedSlot != m_hudCache.selectedSlot) {
        for (int i = 0; i < 5; ++i) {
            if (m_hotbarSlots[i] != nullptr) {
                m_hotbarSlots[i]->SetClass("selected", state.selectedSlot == i);
            }
        }
    }
    for (int i = 0; i < 5; ++i) {
        const HudSlot& slot = state.slots[static_cast<std::size_t>(i)];
        const HudSlot& previous = m_hudCache.slots[static_cast<std::size_t>(i)];
        if (first || slot.label != previous.label) {
            setText(m_hotbarNames[i], slot.label);
        }
        if (first || slot.count != previous.count) {
            setText(m_hotbarCounts[i], slot.count > 0 ? std::to_string(slot.count) : "");
        }
        if (first || slot.throwTag != previous.throwTag) {
            setText(m_hotbarThrows[i], slot.throwable ? slot.throwTag : std::string());
            if (m_hotbarThrows[i] != nullptr) {
                m_hotbarThrows[i]->SetProperty(
                    "display",
                    slot.throwable && !slot.throwTag.empty() ? "block" : "none");
            }
            if (m_hotbarSlots[i] != nullptr) {
                m_hotbarSlots[i]->SetClass("throwable", slot.throwable);
            }
        }
    }

    const bool noticeActive = !state.notice.empty() && state.noticeRemaining > 0.0f;
    if (!noticeActive) {
        if (m_noticeVisible && m_noticeElement != nullptr) {
            m_noticeElement->SetProperty("display", "none");
        }
        m_noticeVisible = false;
    } else if (m_noticeElement != nullptr) {
        if (!m_noticeVisible) {
            m_noticeElement->SetProperty("display", "block");
            m_noticeVisible = true;
        }
        if (first || state.notice != m_hudCache.notice) {
            m_noticeElement->SetInnerRML(state.notice);
        }
        if (first || state.noticeTone != m_hudCache.noticeTone) {
            m_noticeElement->SetClassNames("notice notice-" + state.noticeTone);
        }
        const float duration = std::max(state.noticeDuration, 0.05f);
        const float elapsed = std::max(0.0f, duration - state.noticeRemaining);
        const float rise = std::min(elapsed * 30.0f, 30.0f);
        const float opacity = std::min(1.0f, state.noticeRemaining / 0.35f);
        char bottom[32];
        char alpha[16];
        std::snprintf(bottom, sizeof(bottom), "%.0fpx", 116.0f + rise);
        std::snprintf(alpha, sizeof(alpha), "%.2f", std::max(0.0f, opacity));
        m_noticeElement->SetProperty("bottom", bottom);
        m_noticeElement->SetProperty("opacity", alpha);
    }

    m_hudCache = state;
    m_hudCacheValid = true;
}

void UiSystem::SetInventoryState(const InventoryState& state) {
    InventoryState current = state;
    current.open = IsInventoryOpen();
    const bool changed = !m_hasPendingInventoryState || !(current == m_pendingInventoryState);
    m_pendingInventoryState = current;
    m_hasPendingInventoryState = true;
    if (m_inventory != nullptr) {
        m_inventory->SetState(current);
    }
    if (m_scriptInventoryOpen && changed) {
        NotifyInventoryChanged();
    }
}

void UiSystem::SetInventoryActionHandler(InventoryActionHandler handler) {
    m_pendingInventoryActionHandler = handler;
    if (m_inventory != nullptr) {
        m_inventory->SetActionHandler(std::move(handler));
    }
}

bool UiSystem::GetInventoryState(InventoryState& out) const {
    out = m_pendingInventoryState;
    out.open = IsInventoryOpen();
    return m_initialized;
}

bool UiSystem::InvokeInventoryAction(const InventoryAction& action) {
    if (!m_pendingInventoryActionHandler) {
        return false;
    }
    m_pendingInventoryActionHandler(action);
    return true;
}

void UiSystem::PlayInventoryClick() {
    if (m_inventorySoundHandler) {
        m_inventorySoundHandler();
    }
}

bool UiSystem::IsInventoryOpen() const {
    return m_scriptInventoryOpen || (m_inventory != nullptr && m_inventory->IsOpen());
}

void UiSystem::ToggleInventory() {
    if (IsMenuOpen()) {
        return;
    }
    if (m_scriptInventoryOpen) {
        CloseInventory();
        return;
    }
    if (ScriptInventoryAvailable()) {
        if (Rml::Element* focus = m_context->GetFocusElement(); focus != nullptr) {
            focus->Blur();
        }
        m_scriptInventoryOpen = true;
        SyncInventoryChrome(true);
        m_inventoryWasOpen = true;
        NotifyInventoryChanged();
        if (!ScriptInventoryVisible()) {
            ZL_WARN("ui", "core_ui 背包文档未显示，回退 native 背包");
            m_scriptInventoryOpen = false;
            ShowNativeInventoryFallback();
        }
        return;
    }
    if (!EnsureInventoryPanel()) {
        return;
    }
    if (m_inventory->IsOpen()) {
        m_inventory->Hide();
        SyncInventoryChrome(false);
        m_inventoryWasOpen = false;
        return;
    }
    if (Rml::Element* focus = m_context->GetFocusElement(); focus != nullptr) {
        focus->Blur();
    }
    m_inventory->Show();
    SyncInventoryChrome(true);
    m_inventoryWasOpen = true;
}

void UiSystem::CloseInventory() {
    if (m_scriptInventoryOpen) {
        m_scriptInventoryOpen = false;
        NotifyInventoryChanged();
        SyncInventoryChrome(false);
        m_inventoryWasOpen = false;
        return;
    }
    if (m_inventory != nullptr && m_inventory->IsOpen()) {
        m_inventory->Hide();
        SyncInventoryChrome(false);
        m_inventoryWasOpen = false;
    }
}

void UiSystem::SyncInventoryChrome(bool open) {
    if (open || IsMenuOpen()) {
        if (m_hudDocument != nullptr) {
            m_hudDocument->Hide();
        }
        if (m_scriptUi != nullptr) {
            m_scriptUi->SetGroupVisible(kHudGroup, false);
        }
        if (m_visible && m_document != nullptr) {
            m_document->Hide();
        }
        return;
    }
    if (m_hudDocument != nullptr && !ScriptHudAvailable()) {
        m_hudDocument->Show();
    }
    if (m_scriptUi != nullptr) {
        m_scriptUi->SetGroupVisible(kHudGroup, true);
    }
    if (m_visible && m_document != nullptr) {
        m_document->Show();
        m_document->PullToFront();
    }
}

void UiSystem::SetMenuSettings(const MenuSettings& settings) {
    m_pendingMenuSettings = settings;
    if (m_menu != nullptr) {
        m_menu->SetSettings(settings);
    }
}

void UiSystem::SetMenuSettingsHandler(MenuSettingsHandler handler) {
    m_pendingMenuSettingsHandler = handler;
    if (m_menu != nullptr) {
        m_menu->SetSettingsHandler(std::move(handler));
    }
}

void UiSystem::SetMenuActionHandler(MenuActionHandler handler) {
    m_pendingMenuActionHandler = handler;
    if (m_menu != nullptr) {
        m_menu->SetActionHandler(std::move(handler));
    }
}

void UiSystem::SetMenuSoundHandler(std::function<void()> handler) {
    m_menuSoundHandler = handler;
    if (m_menu != nullptr) {
        m_menu->SetSoundHandler(std::move(handler));
    }
}

void UiSystem::SetInventorySoundHandler(std::function<void()> handler) {
    m_inventorySoundHandler = handler;
    if (m_inventory != nullptr) {
        m_inventory->SetSoundHandler(std::move(handler));
    }
}

void UiSystem::ShowMainMenu(bool canContinue) {
    CloseInventory();
    if (IsMultiplayerOpen()) {
        HideMultiplayerMenu();
    }
    if (ScriptMenuAvailable()) {
        m_scriptMenuOpen = true;
        m_scriptMenuPauseMode = false;
        m_scriptMenuCanContinue = canContinue;
        SyncMenuChrome(true);
        m_menuWasOpen = true;
        NotifyMenuChanged();
        if (m_scriptUi->IsLoaded(kCoreUiOwner, kCoreMenuDocument)) {
            return;
        }
        ZL_WARN("ui", "core_ui 菜单文档未加载，回退 native 主菜单");
        m_scriptMenuOpen = false;
        m_scriptMenuPauseMode = false;
        ShowNativeMenuFallback(false, canContinue);
        return;
    }
    if (!EnsureMenuPanel()) {
        return;
    }
    m_menu->ShowMainMenu(canContinue);
    if (!m_menu->IsOpen()) {
        return;
    }
    SyncMenuChrome(true);
    m_menuWasOpen = true;
}

void UiSystem::ShowPauseMenu() {
    CloseInventory();
    if (ScriptMenuAvailable()) {
        m_scriptMenuOpen = true;
        m_scriptMenuPauseMode = true;
        SyncMenuChrome(true);
        m_menuWasOpen = true;
        NotifyMenuChanged();
        if (m_scriptUi->IsLoaded(kCoreUiOwner, kCoreMenuDocument)) {
            return;
        }
        ZL_WARN("ui", "core_ui 菜单文档未加载，回退 native 暂停菜单");
        m_scriptMenuOpen = false;
        m_scriptMenuPauseMode = false;
        ShowNativeMenuFallback(true, m_scriptMenuCanContinue);
        return;
    }
    if (!EnsureMenuPanel()) {
        return;
    }
    m_menu->ShowPauseMenu();
    if (!m_menu->IsOpen()) {
        return;
    }
    SyncMenuChrome(true);
    m_menuWasOpen = true;
}

void UiSystem::ShowEndScreen(bool victory, const std::string& title,
                             const std::string& subtitle, const std::string& stats,
                             bool showRestart) {
    CloseInventory();
    if (IsMultiplayerOpen()) {
        HideMultiplayerMenu();
    }
    if (m_scriptMenuOpen) {
        m_scriptMenuOpen = false;
        m_scriptMenuPauseMode = false;
        NotifyMenuChanged();
    }
    if (!EnsureMenuPanel()) {
        ZL_ERROR("ui", "结算页不可用：assets/ui/menu.rml");
        return;
    }
    m_menu->ShowEndScreen(victory, title, subtitle, stats, showRestart);
    if (!m_menu->IsOpen()) {
        return;
    }
    SyncMenuChrome(true);
    m_menuWasOpen = true;
}

void UiSystem::HideMenu() {
    if (m_scriptMenuOpen) {
        m_scriptMenuOpen = false;
        m_scriptMenuPauseMode = false;
        m_consumeGameplayThisFrame = true;
        SyncMenuChrome(false);
        m_menuWasOpen = false;
        NotifyMenuChanged();
        return;
    }
    if (m_menu == nullptr || !m_menu->IsOpen()) {
        return;
    }
    m_menu->Hide();
    SyncMenuChrome(false);
    m_menuWasOpen = false;
}

bool UiSystem::IsMenuOpen() const {
    return m_scriptMenuOpen || (m_menu != nullptr && m_menu->IsOpen());
}

bool UiSystem::IsMenuSettingsOpen() const {
    return m_menu != nullptr && m_menu->IsSettingsOpen();
}

bool UiSystem::IsPointerOverUi() const {
    if (m_context == nullptr) {
        return false;
    }
    Rml::Element* hover = m_context->GetHoverElement();
    if (hover == nullptr) {
        return false;
    }
    const Rml::String tag = hover->GetTagName();
    return tag != "body" && tag != "html" &&
           dynamic_cast<Rml::ElementDocument*>(hover) == nullptr;
}

bool UiSystem::GetMenuState(MenuState& out) const {
    out = {};
    out.open = IsMenuOpen();
    out.pauseMode = m_scriptMenuPauseMode;
    out.canContinue = m_scriptMenuCanContinue;
    out.settings = m_pendingMenuSettings;
    return m_initialized;
}

void UiSystem::SetMultiplayerActionHandler(MultiplayerActionHandler handler) {
    m_pendingMultiplayerActionHandler = std::move(handler);
    if (m_multiplayer != nullptr) {
        m_multiplayer->SetActionHandler(m_pendingMultiplayerActionHandler);
    }
}

void UiSystem::ShowMultiplayerMenu(const MultiplayerMenuState& state) {
    CloseInventory();
    if (m_scriptMenuOpen && m_scriptUi != nullptr) {
        (void)m_scriptUi->ShowDocument(kCoreUiOwner, kCoreMenuDocument, false);
    }
    if (m_menu != nullptr && m_menu->IsOpen()) {
        m_menu->Hide();
    }
    if (!EnsureMultiplayerPanel()) {
        return;
    }
    m_pendingMultiplayerState = state;
    if (m_pendingMultiplayerActionHandler) {
        m_multiplayer->SetActionHandler(m_pendingMultiplayerActionHandler);
    }
    m_multiplayer->Show(state);
    SyncMenuChrome(true);
    m_multiplayerWasOpen = true;
}

void UiSystem::SetMultiplayerState(const MultiplayerMenuState& state) {
    m_pendingMultiplayerState = state;
    if (m_multiplayer != nullptr) {
        m_multiplayer->SetState(state);
    }
}

void UiSystem::HideMultiplayerMenu() {
    if (m_multiplayer == nullptr || !m_multiplayer->IsOpen()) {
        return;
    }
    m_multiplayer->Hide();
    SyncMenuChrome(false);
    m_multiplayerWasOpen = false;
}

bool UiSystem::IsMultiplayerOpen() const {
    return m_multiplayer != nullptr && m_multiplayer->IsOpen();
}

void UiSystem::SetSocialState(const SocialUiState& state) {
    m_socialState = state;
    if (state.visible && m_social == nullptr) {
        EnsureSocialPanel();
    }
    if (m_social != nullptr) {
        m_social->SetState(state);
    }
}

void UiSystem::SetSocialChatHandler(SocialChatHandler handler) {
    m_socialChatHandler = std::move(handler);
    if (m_social != nullptr) {
        m_social->SetSendHandler(m_socialChatHandler);
    }
}

bool UiSystem::IsSocialVisible() const {
    return m_socialState.visible;
}

bool UiSystem::IsSocialChatOpen() const {
    return m_social != nullptr && m_social->IsChatOpen();
}
bool UiSystem::ApplyScriptMenuSettings(const MenuSettings& settings, std::string& error) {
    if (!m_pendingMenuSettingsHandler) {
        error = "设置保存不可用";
        return false;
    }
    if (!m_pendingMenuSettingsHandler(settings, error)) {
        return false;
    }
    m_pendingMenuSettings = settings;
    return true;
}

void UiSystem::InvokeMenuAction(MenuActionType action) {
    if (m_pendingMenuActionHandler) {
        m_pendingMenuActionHandler(action);
    }
}

void UiSystem::PlayUiClick() {
    if (m_menuSoundHandler) {
        m_menuSoundHandler();
    }
}

void UiSystem::SyncMenuChrome(bool open) {
    if (open) {
        if (m_hudDocument != nullptr) {
            m_hudDocument->Hide();
        }
        if (m_scriptUi != nullptr) {
            m_scriptUi->SetGroupVisible(kHudGroup, false);
        }
        if (m_visible && m_document != nullptr) {
            m_document->Hide();
        }
        return;
    }
    if (IsMenuOpen() || IsMultiplayerOpen()) {
        return;
    }
    if (IsInventoryOpen()) {
        SyncInventoryChrome(true);
        return;
    }
    if (m_hudDocument != nullptr && !ScriptHudAvailable()) {
        m_hudDocument->Show();
    }
    if (m_scriptUi != nullptr) {
        m_scriptUi->SetGroupVisible(kHudGroup, true);
    }
    if (m_visible && m_document != nullptr) {
        m_document->Show();
        m_document->PullToFront();
    }
}

void UiSystem::SetVisible(bool visible) {
    if (!m_initialized) {
        return;
    }
    if (visible && !EnsureDebugDocument()) {
        return;
    }
    if (m_document == nullptr) {
        return;
    }
    if (visible) {
        m_document->Show();
        m_document->PullToFront();
    } else {
        m_document->Hide();
        m_pendingChars.clear();
        if (Rml::Element* focus = m_context->GetFocusElement(); focus != nullptr) {
            focus->Blur();
        }
    }
    m_visible = visible;
}

bool UiSystem::ToggleVisible() {
    SetVisible(!m_visible);
    return m_visible;
}

void UiSystem::HandleChar(unsigned int codepoint) {
    if (m_initialized) {
        m_pendingChars.push_back(codepoint);
    }
}

bool UiSystem::HasInputFocus() const {
    return IsInputFocusBlocking(m_context);
}

void UiSystem::AddClickHandler(const char* elementId, std::function<void()> action) {
    AddClickHandler(m_document, elementId, std::move(action));
}

void UiSystem::AddClickHandler(Rml::ElementDocument* document, const char* elementId,
                               std::function<void()> action) {
    if (document == nullptr) {
        return;
    }
    Rml::Element* element = document->GetElementById(elementId);
    if (element == nullptr) {
        ZL_WARN("ui", "UI 元素不存在：{}", elementId);
        return;
    }
    auto listener = std::make_unique<FunctionListener>(std::move(action));
    element->AddEventListener("click", listener.get());
    m_listeners.push_back(ListenerBinding{element, "click", std::move(listener)});
}

void UiSystem::AddEventHandler(const char* elementId, const char* event,
                               std::function<void()> action) {
    if (m_document == nullptr) {
        return;
    }
    Rml::Element* element = m_document->GetElementById(elementId);
    if (element == nullptr) {
        ZL_WARN("ui", "UI 元素不存在：{}", elementId);
        return;
    }
    auto listener = std::make_unique<FunctionListener>(std::move(action));
    element->AddEventListener(event, listener.get());
    m_listeners.push_back(ListenerBinding{element, event, std::move(listener)});
}

}  // namespace zl::ui
