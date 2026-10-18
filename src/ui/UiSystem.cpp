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

#include "core/Input.h"
#include "core/Log.h"
#include "platform/Window.h"
#include "render/Renderer.h"
#include "ui/InventoryPanel.h"
#include "ui/MenuPanel.h"
#include "ui/RmlBgfxRenderInterface.h"
#include "ui/RmlSystemInterface.h"
#include "ui/UiInput.h"

namespace zl::ui {
namespace {

constexpr uint32_t kFontAtlasPageBudget = 12;
constexpr uint64_t kFontAtlasByteBudget = 48ull * 1024 * 1024;

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

    m_document = m_context->LoadDocument("assets/ui/debug.rml");
    if (m_document == nullptr) {
        ZL_ERROR("ui", "UI 文档加载失败：assets/ui/debug.rml");
        Rml::Shutdown();
        m_system.reset();
        m_renderer->Shutdown();
        m_renderer.reset();
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

    m_hudDocument = m_context->LoadDocument("assets/ui/hud.rml");
    if (m_hudDocument != nullptr) {
        m_hudDocument->Show();
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
    } else {
        ZL_WARN("ui", "HUD 加载失败：assets/ui/hud.rml（回退到调试面板）");
        m_document->Show();
        m_visible = true;
    }

    if (m_hudDocument != nullptr) {
        AddClickHandler(m_hudDocument, "hud-bag", [this] { ToggleInventory(); });
    }

    m_inventory = std::make_unique<InventoryPanel>();
    if (!m_inventory->Init(*m_context, "assets/ui/inventory.rml")) {
        ZL_WARN("ui", "背包界面不可用：assets/ui/inventory.rml");
        m_inventory.reset();
    }

    m_menu = std::make_unique<MenuPanel>();
    if (!m_menu->Init(*m_context, "assets/ui/menu.rml")) {
        ZL_WARN("ui", "主菜单/暂停菜单不可用：assets/ui/menu.rml");
        m_menu.reset();
    }

    window.SetCharCallback([this](unsigned int codepoint) { HandleChar(codepoint); });
    m_context->Update();

    m_initialized = true;
    ZL_INFO("ui", "RmlUi {} 初始化完成：{}x{}，HUD 常驻，I/右下角背包，Esc 暂停，F11 调试面板",
            Rml::GetVersion().c_str(), m_width, m_height);
    return true;
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
    m_hudDocument = nullptr;
    m_document = nullptr;
    m_hudCacheValid = false;
    m_noticeVisible = false;
    m_consumeGameplayThisFrame = false;
    m_inventoryWasOpen = false;
    m_menuWasOpen = false;
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
    const bool menuOpenAtStart = IsMenuOpen();
    if (input.IsKeyPressed(GLFW_KEY_F11) && !menuOpenAtStart &&
        (m_inventory == nullptr || !m_inventory->IsOpen())) {
        ToggleVisible();
    }

    bool escapeHandled = false;
    if (menuOpenAtStart && m_menu != nullptr) {
        if (input.IsKeyPressed(GLFW_KEY_ESCAPE)) {
            m_menu->OnEscape();
            m_consumeGameplayThisFrame = true;
            escapeHandled = true;
        }
    } else if (m_inventory != nullptr) {
        const bool typing = HasInputFocus();
        if (input.IsKeyPressed(GLFW_KEY_I) && (!typing || m_inventory->IsOpen())) {
            ToggleInventory();
        }
        if (m_inventory->IsOpen() && input.IsKeyPressed(GLFW_KEY_ESCAPE)) {
            m_inventory->Hide();
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
        (m_inventory == nullptr || !m_inventory->IsOpen())) {
        m_pendingChars.clear();
        input.SetGameplayBlocked(false);
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
        if (key == capturedKey) {
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
    const bool inventoryOpen = m_inventory != nullptr && m_inventory->IsOpen();
    if (menuOpen != m_menuWasOpen) {
        SyncMenuChrome(menuOpen);
        m_menuWasOpen = menuOpen;
    }
    if (inventoryOpen != m_inventoryWasOpen) {
        SyncInventoryChrome(inventoryOpen);
        m_inventoryWasOpen = inventoryOpen;
    }
    const bool focusBlocked = HasInputFocus();
    input.SetGameplayBlocked(menuOpen || inventoryOpen || focusBlocked ||
                             m_consumeGameplayThisFrame);
    m_consumeGameplayThisFrame = false;
}

void UiSystem::Render() {
    if (!m_initialized || m_context == nullptr) {
        return;
    }
    if (!m_visible && m_hudDocument == nullptr &&
        (m_inventory == nullptr || !m_inventory->IsOpen()) && !IsMenuOpen()) {
        return;
    }
    const glm::mat4 view(1.0f);
    const glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(m_width),
                                            static_cast<float>(m_height), 0.0f, -1.0f, 1.0f);
    bgfx::setViewTransform(static_cast<bgfx::ViewId>(render::ViewId::UI),
                           glm::value_ptr(view), glm::value_ptr(projection));
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
    if (m_hudDocument == nullptr) {
        return;
    }
    const bool first = !m_hudCacheValid;

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
    if (m_inventory != nullptr) {
        m_inventory->SetState(state);
    }
}

void UiSystem::SetInventoryActionHandler(InventoryActionHandler handler) {
    if (m_inventory != nullptr) {
        m_inventory->SetActionHandler(std::move(handler));
    }
}

bool UiSystem::IsInventoryOpen() const {
    return m_inventory != nullptr && m_inventory->IsOpen();
}

void UiSystem::ToggleInventory() {
    if (m_inventory == nullptr || IsMenuOpen()) {
        return;
    }
    if (m_inventory->IsOpen()) {
        m_inventory->Hide();
        return;
    }
    if (Rml::Element* focus = m_context->GetFocusElement(); focus != nullptr) {
        focus->Blur();
    }
    m_inventory->Show();
}

void UiSystem::CloseInventory() {
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
        if (m_visible && m_document != nullptr) {
            m_document->Hide();
        }
        return;
    }
    if (m_hudDocument != nullptr) {
        m_hudDocument->Show();
    }
    if (m_visible && m_document != nullptr) {
        m_document->Show();
        m_document->PullToFront();
    }
}

void UiSystem::SetMenuSettings(const MenuSettings& settings) {
    if (m_menu != nullptr) {
        m_menu->SetSettings(settings);
    }
}

void UiSystem::SetMenuSettingsHandler(MenuSettingsHandler handler) {
    if (m_menu != nullptr) {
        m_menu->SetSettingsHandler(std::move(handler));
    }
}

void UiSystem::SetMenuActionHandler(MenuActionHandler handler) {
    if (m_menu != nullptr) {
        m_menu->SetActionHandler(std::move(handler));
    }
}

void UiSystem::SetMenuSoundHandler(std::function<void()> handler) {
    if (m_menu != nullptr) {
        m_menu->SetSoundHandler(std::move(handler));
    }
}

void UiSystem::SetInventorySoundHandler(std::function<void()> handler) {
    if (m_inventory != nullptr) {
        m_inventory->SetSoundHandler(std::move(handler));
    }
}

void UiSystem::ShowMainMenu(bool canContinue) {
    if (m_menu == nullptr) {
        return;
    }
    if (m_inventory != nullptr && m_inventory->IsOpen()) {
        m_inventory->Hide();
        m_inventoryWasOpen = false;
    }
    m_menu->ShowMainMenu(canContinue);
    if (!m_menu->IsOpen()) {
        return;
    }
    SyncMenuChrome(true);
    m_menuWasOpen = true;
}

void UiSystem::ShowPauseMenu() {
    if (m_menu == nullptr) {
        return;
    }
    if (m_inventory != nullptr && m_inventory->IsOpen()) {
        m_inventory->Hide();
        m_inventoryWasOpen = false;
    }
    m_menu->ShowPauseMenu();
    if (!m_menu->IsOpen()) {
        return;
    }
    SyncMenuChrome(true);
    m_menuWasOpen = true;
}

void UiSystem::HideMenu() {
    if (m_menu == nullptr || !m_menu->IsOpen()) {
        return;
    }
    m_menu->Hide();
    SyncMenuChrome(false);
    m_menuWasOpen = false;
}

bool UiSystem::IsMenuOpen() const {
    return m_menu != nullptr && m_menu->IsOpen();
}

bool UiSystem::IsMenuSettingsOpen() const {
    return m_menu != nullptr && m_menu->IsSettingsOpen();
}

void UiSystem::SyncMenuChrome(bool open) {
    if (open) {
        if (m_hudDocument != nullptr) {
            m_hudDocument->Hide();
        }
        if (m_visible && m_document != nullptr) {
            m_document->Hide();
        }
        return;
    }
    if (IsMenuOpen()) {
        return;
    }
    if (m_inventory != nullptr && m_inventory->IsOpen()) {
        SyncInventoryChrome(true);
        return;
    }
    if (m_hudDocument != nullptr) {
        m_hudDocument->Show();
    }
    if (m_visible && m_document != nullptr) {
        m_document->Show();
        m_document->PullToFront();
    }
}

void UiSystem::SetVisible(bool visible) {
    if (!m_initialized || m_document == nullptr) {
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
    if (m_initialized && m_visible) {
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
