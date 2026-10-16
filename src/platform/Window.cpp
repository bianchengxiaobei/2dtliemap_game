#include "platform/Window.h"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
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

    m_window = glfwCreateWindow(desc.width, desc.height, desc.title.c_str(), nullptr, nullptr);
    if (!m_window) {
        ZL_CRITICAL("platform", "创建窗口失败");
        return false;
    }

    m_width = desc.width;
    m_height = desc.height;
    glfwGetFramebufferSize(m_window, &m_fbWidth, &m_fbHeight);

    glfwSetWindowUserPointer(m_window, this);

    glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* w, int width, int height) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        self->m_fbWidth = width;
        self->m_fbHeight = height;
        glfwGetWindowSize(w, &self->m_width, &self->m_height);
        if (self->m_resizeCallback) {
            self->m_resizeCallback(width, height);
        }
    });

    glfwSetWindowSizeCallback(m_window, [](GLFWwindow* w, int width, int height) {
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

    glfwSetKeyCallback(m_window, [](GLFWwindow* w, int key, int scancode, int action, int mods) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        if (self->m_keyCallback) {
            self->m_keyCallback(key, action, mods);
        }
        (void)scancode;
    });

    ZL_INFO("platform", "窗口创建: {} {}x{}", desc.title, desc.width, desc.height);
    return true;
}

void Window::Destroy() {
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
}

void Window::PollEvents(core::Input& input) {
    glfwPollEvents();
    if (!m_window) {
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
}

bool Window::ShouldClose() const {
    return m_window == nullptr || glfwWindowShouldClose(m_window) != 0;
}

void Window::SetShouldClose(bool value) {
    if (m_window) {
        glfwSetWindowShouldClose(m_window, value ? GLFW_TRUE : GLFW_FALSE);
    }
}

void* Window::NativeHandle() const {
    return m_window ? glfwGetWin32Window(m_window) : nullptr;
}

void Window::SetResizeCallback(std::function<void(int, int)> callback) {
    m_resizeCallback = std::move(callback);
}

void Window::SetKeyCallback(std::function<void(int, int, int)> callback) {
    m_keyCallback = std::move(callback);
}

}  // namespace zl::platform
