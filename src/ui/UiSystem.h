#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/InventoryUi.h"
#include "ui/MenuUi.h"

namespace Rml {
class Context;
class Element;
class ElementDocument;
class EventListener;
}  // namespace Rml

namespace zl::core {
class Input;
}

namespace zl::platform {
class Window;
}

namespace zl::ui {

class InventoryPanel;
class MenuPanel;
class RmlBgfxRenderInterface;
class RmlSystemInterface;

struct HudSlot {
    std::string label;
    int count = 0;
};

struct HudState {
    std::string day;
    std::string time;
    std::string speed;
    bool playerValid = false;
    float health = 0.0f;
    float healthMax = 100.0f;
    float stamina = 0.0f;
    float staminaMax = 100.0f;
    std::array<HudSlot, 5> slots{};
    int selectedSlot = 0;
    std::string notice;
    std::string noticeTone = "info";
    float noticeRemaining = 0.0f;
    float noticeDuration = 1.0f;
};

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
    void HideMenu();
    bool IsMenuOpen() const;
    bool IsMenuSettingsOpen() const;
    void SyncMenuChrome(bool open);

private:
    void HandleChar(unsigned int codepoint);
    bool HasInputFocus() const;
    void UpdateFontAtlasStats();
    void AddClickHandler(const char* elementId, std::function<void()> action);
    void AddClickHandler(Rml::ElementDocument* document, const char* elementId,
                         std::function<void()> action);
    void AddEventHandler(const char* elementId, const char* event,
                         std::function<void()> action);

    platform::Window* m_window = nullptr;
    std::unique_ptr<RmlBgfxRenderInterface> m_renderer;
    std::unique_ptr<RmlSystemInterface> m_system;
    Rml::Context* m_context = nullptr;
    Rml::ElementDocument* m_document = nullptr;
    Rml::ElementDocument* m_hudDocument = nullptr;
    std::unique_ptr<InventoryPanel> m_inventory;
    std::unique_ptr<MenuPanel> m_menu;
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
    bool m_noticeVisible = false;
    bool m_consumeGameplayThisFrame = false;
    bool m_inventoryWasOpen = false;
    bool m_menuWasOpen = false;
    bool m_visible = false;
    bool m_initialized = false;
};

}  // namespace zl::ui
