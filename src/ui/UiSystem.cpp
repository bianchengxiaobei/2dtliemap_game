#include "ui/UiSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <GLFW/glfw3.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
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
    m_noticeElement = m_hudDocument->GetElementById("notice");
    for (int i = 0; i < 5; ++i) {
        const std::string suffix = std::to_string(i);
        m_hotbarSlots[i] = m_hudDocument->GetElementById("slot-" + suffix);
        m_hotbarNames[i] = m_hudDocument->GetElementById("slot-name-" + suffix);
        m_hotbarCounts[i] = m_hudDocument->GetElementById("slot-count-" + suffix);
    }
    AddClickHandler(m_hudDocument, "hud-bag", [this] { ToggleInventory(); });
    ZL_INFO("ui", "核心 UI 按需加载：HUD");
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
    m_noticeElement = nullptr;
    m_hotbarSlots.fill(nullptr);
    m_hotbarNames.fill(nullptr);
    m_hotbarCounts.fill(nullptr);
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
        !multiplayerOpenAtStart && !IsInventoryOpen()) {
        ToggleVisible();
    }

    // 内置 HUD 的 native 版本只作为 fallback：core_ui 脚本存在时由脚本负责；
    // 第一个 tick 前脚本可能还没加载 HUD 文档，因此这里延迟到 owner 已确定后。
    if (m_hudCacheValid && !ScriptHudAvailable()) {
        if (EnsureHudDocument()) {
            if (m_nativeHudNeedsFullRefresh) {
                ApplyHudState(m_hudCache, true);
            }
            if (IsMenuOpen() || IsInventoryOpen() || IsMultiplayerOpen()) {
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
        if (input.IsKeyPressed(GLFW_KEY_I) && (!typing || inventoryOpen)) {
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
        const bool enter = input.IsKeyPressed(GLFW_KEY_ENTER);
        const bool keypadEnter = input.IsKeyPressed(GLFW_KEY_KP_ENTER);
        if (enter || keypadEnter) {
            if (m_social != nullptr) {
                m_social->OpenChat();
            }
            consumedKey = enter ? GLFW_KEY_ENTER : GLFW_KEY_KP_ENTER;
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
    }
    const bool focusBlocked = HasInputFocus();
    const bool scriptBlocked = m_scriptUi != nullptr && m_scriptUi->IsInputCaptured();
    const bool scriptConsumed = m_scriptUi != nullptr && m_scriptUi->InputConsumedThisFrame();
    input.SetGameplayBlocked(menuOpen || inventoryOpen || multiplayerOpen ||
                             focusBlocked || scriptBlocked || scriptConsumed ||
                             m_consumeGameplayThisFrame);
    m_consumeGameplayThisFrame = false;
}

void UiSystem::Render() {
    if (!m_initialized || m_context == nullptr) {
        return;
    }
    if (!m_visible && m_hudDocument == nullptr &&
        !IsInventoryOpen() && !IsMenuOpen() && !IsMultiplayerOpen() &&
        !IsSocialVisible() &&
        (m_scriptUi == nullptr || !m_scriptUi->HasVisibleDocuments())) {
        return;
    }
    const glm::mat4 view(1.0f);
    const glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(m_width),
                                            static_cast<float>(m_height), 0.0f, -1.0f, 1.0f);
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
        std::snprintf(bottom, sizeof(bottom), "%.0fpx", 96.0f + rise);
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
    if (m_context == nullptr) {
        return false;
    }
    Rml::Element* focus = m_context->GetFocusElement();
    if (focus == nullptr) {
        return false;
    }
    // RmlUi 在无显式焦点时会返回 body；它不是输入控件，不应阻断游戏输入。
    const Rml::String tag = focus->GetTagName();
    return tag != "body" && tag != "html";
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
