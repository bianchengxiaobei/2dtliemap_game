#include "platform/Window.h"

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#endif
#include <GLFW/glfw3native.h>

#include "core/Input.h"
#include "core/Log.h"

namespace zl::platform {
namespace {

bool s_glfwInitialized = false;

void GlfwErrorCallback(int code, const char* description) {
    ZL_ERROR("platform", "GLFW 错误 {}: {}", code, description);
}

}  // namespace

Window::~Window() {
    Destroy();
}

bool Window::Create(const Desc& desc) {
    if (!s_glfwInitialized) {
        glfwSetErrorCallback(GlfwErrorCallback);
        if (!glfwInit()) {
            ZL_CRITICAL("platform", "glfwInit 失败");
            return false;
        }
        s_glfwInitialized = true;
    }

    // bgfx 自行管理渲染上下文
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);

    GLFWmonitor* monitor = desc.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    if (monitor != nullptr) {
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    }

    m_window = glfwCreateWindow(desc.width, desc.height, desc.title.c_str(), monitor, nullptr);
    if (!m_window && monitor != nullptr) {
        glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
        m_window = glfwCreateWindow(desc.width, desc.height, desc.title.c_str(), nullptr, nullptr);
        monitor = nullptr;
    }
    if (!m_window) {
        ZL_CRITICAL("platform", "创建窗口失败");
        return false;
    }

    m_fullscreen = monitor != nullptr;
    m_resizable = desc.resizable;
    m_windowedWidth = desc.width;
    m_windowedHeight = desc.height;
    if (GLFWmonitor* primary = glfwGetPrimaryMonitor()) {
        int wx = 0;
        int wy = 0;
        int ww = 0;
        int wh = 0;
        glfwGetMonitorWorkarea(primary, &wx, &wy, &ww, &wh);
        if (m_fullscreen && (m_windowedWidth > ww - 60 || m_windowedHeight > wh - 60)) {
            m_windowedWidth = 1280;
            m_windowedHeight = 720;
        }
        if (ww > 0 && wh > 0) {
            m_windowedX = wx + (ww - m_windowedWidth) / 2;
            m_windowedY = wy + (wh - m_windowedHeight) / 2;
        }
    }
    ApplyWindowSize();

    glfwSetWindowUserPointer(m_window, this);

    glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* w, int width, int height) {
        if (width <= 0 || height <= 0) {
            return;
        }
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        self->m_fbWidth = width;
        self->m_fbHeight = height;
        glfwGetWindowSize(w, &self->m_width, &self->m_height);
        if (self->m_resizeCallback) {
            self->m_resizeCallback(width, height);
        }
    });

    glfwSetWindowSizeCallback(m_window, [](GLFWwindow* w, int width, int height) {
        if (width <= 0 || height <= 0) {
            return;
        }
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        self->m_width = width;
        self->m_height = height;
        ZL_INFO("platform", "窗口缩放至 {}x{}", width, height);
    });

    glfwSetScrollCallback(m_window, [](GLFWwindow* w, double dx, double dy) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        self->m_pendingScrollX += static_cast<float>(dx);
        self->m_pendingScrollY += static_cast<float>(dy);
    });

    glfwSetMouseButtonCallback(m_window, [](GLFWwindow* w, int button, int action,
                                            int mods) {
        (void)mods;
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        if (self->m_input != nullptr) {
            self->m_input->LatchMouseButton(button, action == GLFW_PRESS);
        }
    });

    glfwSetKeyCallback(m_window, [](GLFWwindow* w, int key, int scancode, int action, int mods) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        if (self->m_keyCallback) {
            self->m_keyCallback(key, action, mods);
        }
        (void)scancode;
    });

    glfwSetCharCallback(m_window, [](GLFWwindow* w, unsigned int codepoint) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        if (self->m_charCallback) {
            self->m_charCallback(codepoint);
        }
    });

    ZL_INFO("platform", "窗口创建: {} {}x{} fullscreen={}", desc.title, m_width, m_height,
            m_fullscreen);
    return true;
}

void Window::Destroy() {
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
}

void Window::PollEvents(core::Input& input) {
    m_input = &input;
    glfwPollEvents();
    if (!m_window) {
        m_input = nullptr;
        return;
    }

    // GLFW 有效键码从 GLFW_KEY_SPACE(32) 开始；0..31 是无效键码会触发 GLFW 错误
    for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
        input.SetKeyState(key, glfwGetKey(m_window, key) == GLFW_PRESS);
    }
    for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
        input.SetMouseButton(button, glfwGetMouseButton(m_window, button) == GLFW_PRESS);
    }

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(m_window, &mouseX, &mouseY);
    input.SetMousePosition(static_cast<float>(mouseX), static_cast<float>(mouseY));

    if (m_pendingScrollX != 0.0f || m_pendingScrollY != 0.0f) {
        input.AddScroll(m_pendingScrollX, m_pendingScrollY);
        m_pendingScrollX = 0.0f;
        m_pendingScrollY = 0.0f;
    }
    m_input = nullptr;
}

bool Window::ShouldClose() const {
    return m_window == nullptr || glfwWindowShouldClose(m_window) != 0;
}

void Window::SetShouldClose(bool value) {
    if (m_window) {
        glfwSetWindowShouldClose(m_window, value ? GLFW_TRUE : GLFW_FALSE);
    }
}

void Window::ToggleFullscreen() {
    if (!m_window) {
        return;
    }

    if (!m_fullscreen) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
        if (monitor == nullptr || mode == nullptr) {
            return;
        }
        glfwGetWindowPos(m_window, &m_windowedX, &m_windowedY);
        glfwGetWindowSize(m_window, &m_windowedWidth, &m_windowedHeight);
        glfwSetWindowAttrib(m_window, GLFW_DECORATED, GLFW_FALSE);
        glfwSetWindowMonitor(m_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        m_fullscreen = true;
    } else {
        glfwSetWindowAttrib(m_window, GLFW_DECORATED, GLFW_TRUE);
        glfwSetWindowAttrib(m_window, GLFW_RESIZABLE,
                            m_resizable ? GLFW_TRUE : GLFW_FALSE);
        glfwSetWindowMonitor(m_window, nullptr, m_windowedX, m_windowedY, m_windowedWidth,
                             m_windowedHeight, GLFW_DONT_CARE);
        m_fullscreen = false;
    }

    ApplyWindowSize();
    ZL_INFO("platform", "全屏切换: {} {}x{}", m_fullscreen ? "开" : "关", m_width, m_height);
}

void* Window::NativeHandle() const {
#if defined(_WIN32)
    return m_window ? glfwGetWin32Window(m_window) : nullptr;
#elif defined(__linux__)
    return m_window ? reinterpret_cast<void*>(glfwGetX11Window(m_window)) : nullptr;
#else
    return nullptr;
#endif
}

void* Window::NativeDisplayHandle() const {
#if defined(__linux__)
    return m_window ? static_cast<void*>(glfwGetX11Display()) : nullptr;
#else
    return nullptr;
#endif
}

void Window::SetResizeCallback(std::function<void(int, int)> callback) {
    m_resizeCallback = std::move(callback);
}

void Window::SetKeyCallback(std::function<void(int, int, int)> callback) {
    m_keyCallback = std::move(callback);
}

void Window::SetCharCallback(std::function<void(unsigned int)> callback) {
    m_charCallback = std::move(callback);
}

void Window::ApplyWindowSize() {
    if (!m_window) {
        return;
    }
    int width = 0;
    int height = 0;
    int fbWidth = 0;
    int fbHeight = 0;
    glfwGetWindowSize(m_window, &width, &height);
    glfwGetFramebufferSize(m_window, &fbWidth, &fbHeight);
    if (width <= 0 || height <= 0 || fbWidth <= 0 || fbHeight <= 0) {
        return;
    }
    m_width = width;
    m_height = height;
    m_fbWidth = fbWidth;
    m_fbHeight = fbHeight;
    if (m_resizeCallback) {
        m_resizeCallback(m_fbWidth, m_fbHeight);
    }
}

}  // namespace zl::platform
