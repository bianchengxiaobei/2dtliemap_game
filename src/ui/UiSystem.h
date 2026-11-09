#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/HudState.h"
#include "ui/InventoryUi.h"
#include "ui/MenuUi.h"
#include "ui/MultiplayerUi.h"
#include "ui/SocialUi.h"

namespace Rml {
class Context;
class Element;
class ElementDocument;
class EventListener;
}  // namespace Rml

namespace zl::core {
class EventBus;
class Input;
}

namespace zl::platform {
class Window;
}

namespace zl::ui {

class InventoryPanel;
class MenuPanel;
class MultiplayerPanel;
class SocialPanel;
class RmlBgfxRenderInterface;
class RmlSystemInterface;
class ScriptUiHost;

class UiSystem {
public:
    UiSystem();
    ~UiSystem();

    UiSystem(const UiSystem&) = delete;
    UiSystem& operator=(const UiSystem&) = delete;

    bool Init(platform::Window& window, int fbWidth, int fbHeight);
    void Shutdown();
    void Resize(int fbWidth, int fbHeight);
    void Update(core::Input& input);
    void Render();
    void SetHudState(const HudState& state);
    void SetInventoryState(const InventoryState& state);
    void SetInventoryActionHandler(InventoryActionHandler handler);
    bool GetInventoryState(InventoryState& out) const;
    bool InvokeInventoryAction(const InventoryAction& action);
    void PlayInventoryClick();

    // 脚本 UI 文档宿主；无业务语义，供 ScriptUiApi 使用。
    ScriptUiHost* ScriptHost() const { return m_scriptUi.get(); }
    void SetEventBus(core::EventBus* events);

    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible);
    bool ToggleVisible();
    bool IsInventoryOpen() const;
    void ToggleInventory();
    void CloseInventory();
    void SyncInventoryChrome(bool open);

    // M9-T6：主菜单/暂停菜单（共享 menu.rml；暂停菜单保留当前世界状态）
    void SetMenuSettings(const MenuSettings& settings);
    void SetMenuSettingsHandler(MenuSettingsHandler handler);
    void SetMenuActionHandler(MenuActionHandler handler);
    void SetMenuSoundHandler(std::function<void()> handler);
    void SetInventorySoundHandler(std::function<void()> handler);
    void ShowMainMenu(bool canContinue);
    void ShowPauseMenu();
    // M14-T8：单机局结束结算；native 面板独立于脚本主菜单，保证核心流程可用。
    void ShowEndScreen(bool victory, const std::string& title,
                       const std::string& subtitle, const std::string& stats,
                       bool showRestart = true);
    void HideMenu();
    bool IsMenuOpen() const;
    bool IsMenuSettingsOpen() const;
    // 鼠标是否悬停在可交互 UI 元素上（body/文档背景不算）。
    bool IsPointerOverUi() const;
    void SyncMenuChrome(bool open);

    void SetMultiplayerActionHandler(MultiplayerActionHandler handler);
    void ShowMultiplayerMenu(const MultiplayerMenuState& state);
    void SetMultiplayerState(const MultiplayerMenuState& state);
    void HideMultiplayerMenu();
    bool IsMultiplayerOpen() const;

    void SetSocialState(const SocialUiState& state);
    void SetSocialChatHandler(SocialChatHandler handler);
    bool IsSocialVisible() const;
    bool IsSocialChatOpen() const;

    // 脚本菜单控制器使用的数据/动作接口（native MenuPanel 保留为 fallback）。
    bool GetMenuState(MenuState& out) const;
    bool ApplyScriptMenuSettings(const MenuSettings& settings, std::string& error);
    void InvokeMenuAction(MenuActionType action);
    void PlayUiClick();

private:
    bool EnsureDebugDocument();
    bool EnsureHudDocument();
    bool EnsureInventoryPanel();
    bool EnsureMenuPanel();
    bool ScriptHudAvailable() const;
    bool ScriptMenuAvailable() const;
    bool ScriptInventoryAvailable() const;
    bool ScriptInventoryVisible() const;
    bool EnsureMultiplayerPanel();
    bool EnsureSocialPanel();
    bool ShowNativeMenuFallback(bool pauseMode, bool canContinue);
    bool ShowNativeInventoryFallback();
    void NotifyMenuChanged();
    void NotifyInventoryChanged();
    void ApplyHudState(const HudState& state, bool force);
    void ApplyMenuSettings();
    void HandleChar(unsigned int codepoint);
    bool HasInputFocus() const;
    void UpdateFontAtlasStats();
    void AddClickHandler(const char* elementId, std::function<void()> action);
    void AddClickHandler(Rml::ElementDocument* document, const char* elementId,
                         std::function<void()> action);
    void AddEventHandler(const char* elementId, const char* event,
                         std::function<void()> action);

    platform::Window* m_window = nullptr;
    core::EventBus* m_events = nullptr;
    std::unique_ptr<RmlBgfxRenderInterface> m_renderer;
    std::unique_ptr<RmlSystemInterface> m_system;
    Rml::Context* m_context = nullptr;
    Rml::ElementDocument* m_document = nullptr;
    Rml::ElementDocument* m_hudDocument = nullptr;
    std::unique_ptr<InventoryPanel> m_inventory;
    std::unique_ptr<MenuPanel> m_menu;
    std::unique_ptr<MultiplayerPanel> m_multiplayer;
    std::unique_ptr<SocialPanel> m_social;
    std::unique_ptr<ScriptUiHost> m_scriptUi;
    MenuSettings m_pendingMenuSettings;
    MenuSettingsHandler m_pendingMenuSettingsHandler;
    MenuActionHandler m_pendingMenuActionHandler;
    MultiplayerActionHandler m_pendingMultiplayerActionHandler;
    MultiplayerMenuState m_pendingMultiplayerState;
    SocialUiState m_socialState;
    SocialChatHandler m_socialChatHandler;
    InventoryActionHandler m_pendingInventoryActionHandler;
    std::function<void()> m_menuSoundHandler;
    std::function<void()> m_inventorySoundHandler;
    InventoryState m_pendingInventoryState;
    bool m_hasPendingInventoryState = false;
    bool m_debugLoadAttempted = false;
    bool m_inventoryLoadAttempted = false;
    bool m_menuLoadAttempted = false;
    bool m_multiplayerLoadAttempted = false;
    bool m_socialLoadAttempted = false;
    bool m_hudLoadAttempted = false;
    Rml::Element* m_counterElement = nullptr;
    Rml::Element* m_disableSampleElement = nullptr;
    Rml::Element* m_volumeElement = nullptr;
    Rml::Element* m_volumeValueElement = nullptr;
    Rml::Element* m_atlasStatsElement = nullptr;
    Rml::Element* m_hudDayElement = nullptr;
    Rml::Element* m_hudTimeElement = nullptr;
    Rml::Element* m_hudSpeedElement = nullptr;
    Rml::Element* m_hpFillElement = nullptr;
    Rml::Element* m_hpTextElement = nullptr;
    Rml::Element* m_staminaFillElement = nullptr;
    Rml::Element* m_staminaTextElement = nullptr;
    Rml::Element* m_combatStatusElement = nullptr;
    Rml::Element* m_rangedStatusElement = nullptr;
    Rml::Element* m_forageStatusElement = nullptr;
    Rml::Element* m_fishingStatusElement = nullptr;
    Rml::Element* m_needsStatusElement = nullptr;
    Rml::Element* m_woundStatusElement = nullptr;
    Rml::Element* m_painStatusElement = nullptr;
    Rml::Element* m_armorStatusElement = nullptr;
    Rml::Element* m_infectionStatusElement = nullptr;
    Rml::Element* m_weatherStatusElement = nullptr;
    Rml::Element* m_weatherOverlayElement = nullptr;
    Rml::Element* m_threatStatusElement = nullptr;
    std::array<Rml::Element*, 4> m_threatEdges{};
    Rml::Element* m_objectivePanelElement = nullptr;
    Rml::Element* m_objectiveElement = nullptr;
    Rml::Element* m_objectiveHintElement = nullptr;
    Rml::Element* m_poiGuideElement = nullptr;
    Rml::Element* m_missionGuideElement = nullptr;
    Rml::Element* m_eventGuideElement = nullptr;
    Rml::Element* m_siegeStatusElement = nullptr;
    Rml::Element* m_baseStatusElement = nullptr;
    Rml::Element* m_modFieldsElement = nullptr;
    std::array<Rml::Element*, 8> m_modFieldRows{};
    std::array<Rml::Element*, 8> m_modFieldLabels{};
    std::array<Rml::Element*, 8> m_modFieldValues{};
    Rml::Element* m_siegeReportElement = nullptr;
    Rml::Element* m_siegeReportTitleElement = nullptr;
    Rml::Element* m_siegeReportRatingElement = nullptr;
    Rml::Element* m_siegeReportHintElement = nullptr;
    std::array<Rml::Element*, 8> m_siegeReportLineElements{};
    Rml::Element* m_levelPanelElement = nullptr;
    Rml::Element* m_levelTextElement = nullptr;
    Rml::Element* m_levelPerkElement = nullptr;
    Rml::Element* m_levelFillElement = nullptr;
    Rml::Element* m_levelXpElement = nullptr;
    Rml::Element* m_perkPromptElement = nullptr;
    Rml::Element* m_characterPanelElement = nullptr;
    Rml::Element* m_characterLevelElement = nullptr;
    Rml::Element* m_characterXpElement = nullptr;
    Rml::Element* m_characterXpFillElement = nullptr;
    Rml::Element* m_characterPerksElement = nullptr;
    std::array<Rml::Element*, 4> m_characterSkillElements{};
    std::array<Rml::Element*, 3> m_perkChoiceElements{};
    Rml::Element* m_dialoguePanelElement = nullptr;
    Rml::Element* m_dialogueSpeakerElement = nullptr;
    Rml::Element* m_dialogueTextElement = nullptr;
    std::array<Rml::Element*, 5> m_dialogueOptionElements{};
    Rml::Element* m_dialogueHintElement = nullptr;
    Rml::Element* m_actionHintElement = nullptr;
    Rml::Element* m_noticeElement = nullptr;
    std::array<Rml::Element*, 5> m_hotbarSlots{};
    std::array<Rml::Element*, 5> m_hotbarNames{};
    std::array<Rml::Element*, 5> m_hotbarCounts{};
    struct ListenerBinding {
        Rml::Element* element = nullptr;
        std::string event;
        std::unique_ptr<Rml::EventListener> listener;
    };
    std::vector<ListenerBinding> m_listeners;
    std::deque<unsigned int> m_pendingChars;

    int m_width = 0;
    int m_height = 0;
    int m_mouseX = 0;
    int m_mouseY = 0;
    bool m_mouseMoved = false;
    int m_counter = 0;
    uint32_t m_lastAtlasPages = 0xffffffffu;
    uint64_t m_lastAtlasBytes = 0;
    bool m_atlasBudgetWarned = false;
    HudState m_hudCache;
    bool m_hudCacheValid = false;
    bool m_nativeHudNeedsFullRefresh = false;
    bool m_noticeVisible = false;
    bool m_consumeGameplayThisFrame = false;
    bool m_inventoryWasOpen = false;
    bool m_menuWasOpen = false;
    bool m_multiplayerWasOpen = false;
    bool m_scriptMenuOpen = false;
    bool m_scriptMenuPauseMode = false;
    bool m_scriptMenuCanContinue = false;
    bool m_scriptInventoryOpen = false;
    bool m_visible = false;
    bool m_initialized = false;
};

}  // namespace zl::ui
