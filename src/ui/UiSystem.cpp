#include "ui/UiSystem.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <GLFW/glfw3.h>
#include <RmlUi/Core.h>
#include <bgfx/bgfx.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "core/Input.h"
#include "core/Log.h"
#include "platform/Window.h"
#include "render/Renderer.h"
#include "ui/RmlBgfxRenderInterface.h"
#include "ui/RmlSystemInterface.h"
#include "ui/UiInput.h"

namespace zl::ui {
namespace {

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
        ZL_INFO("ui", "UI 字体已加载：NotoSansSC-Regular（zl_ui）");
    } else {
        ZL_WARN("ui", "UI 字体加载失败：assets/fonts/NotoSansSC-Regular.otf（中文将无法显示）");
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
    m_document->Show();
    m_visible = true;
    m_counterElement = m_document->GetElementById("count");

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

    window.SetCharCallback([this](unsigned int codepoint) { HandleChar(codepoint); });
    m_context->Update();

    m_initialized = true;
    ZL_INFO("ui", "RmlUi {} 初始化完成：{}x{}，文档 assets/ui/debug.rml（F11 隐藏/显示）",
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
    m_listeners.clear();
    m_counterElement = nullptr;
    m_document = nullptr;
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
    if (input.IsKeyPressed(GLFW_KEY_F11)) {
        ToggleVisible();
    }
    if (!m_visible) {
        m_pendingChars.clear();
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
}

void UiSystem::Render() {
    if (!m_initialized || !m_visible || m_context == nullptr) {
        return;
    }
    const glm::mat4 view(1.0f);
    const glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(m_width),
                                            static_cast<float>(m_height), 0.0f, -1.0f, 1.0f);
    bgfx::setViewTransform(static_cast<bgfx::ViewId>(render::ViewId::UI),
                           glm::value_ptr(view), glm::value_ptr(projection));
    m_context->Render();
}

void UiSystem::SetVisible(bool visible) {
    if (!m_initialized || m_document == nullptr) {
        return;
    }
    if (visible) {
        m_document->Show();
    } else {
        m_document->Hide();
        m_pendingChars.clear();
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

void UiSystem::AddClickHandler(const char* elementId, std::function<void()> action) {
    if (m_document == nullptr) {
        return;
    }
    Rml::Element* element = m_document->GetElementById(elementId);
    if (element == nullptr) {
        ZL_WARN("ui", "UI 元素不存在：{}", elementId);
        return;
    }
    auto listener = std::make_unique<FunctionListener>(std::move(action));
    element->AddEventListener("click", listener.get());
    m_listeners.push_back(std::move(listener));
}

}  // namespace zl::ui
