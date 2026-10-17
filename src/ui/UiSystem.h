#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

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

class RmlBgfxRenderInterface;
class RmlSystemInterface;

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

    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible);
    bool ToggleVisible();

private:
    void HandleChar(unsigned int codepoint);
    void AddClickHandler(const char* elementId, std::function<void()> action);
    void AddEventHandler(const char* elementId, const char* event,
                         std::function<void()> action);

    platform::Window* m_window = nullptr;
    std::unique_ptr<RmlBgfxRenderInterface> m_renderer;
    std::unique_ptr<RmlSystemInterface> m_system;
    Rml::Context* m_context = nullptr;
    Rml::ElementDocument* m_document = nullptr;
    Rml::Element* m_counterElement = nullptr;
    Rml::Element* m_disableSampleElement = nullptr;
    Rml::Element* m_volumeElement = nullptr;
    Rml::Element* m_volumeValueElement = nullptr;
    std::vector<std::unique_ptr<Rml::EventListener>> m_listeners;
    std::deque<unsigned int> m_pendingChars;

    int m_width = 0;
    int m_height = 0;
    int m_mouseX = 0;
    int m_mouseY = 0;
    bool m_mouseMoved = false;
    int m_counter = 0;
    bool m_visible = false;
    bool m_initialized = false;
};

}  // namespace zl::ui
