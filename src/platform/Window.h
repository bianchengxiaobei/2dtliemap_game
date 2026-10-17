#pragma once

#include <functional>
#include <string>

struct GLFWwindow;

namespace zl::core {
class Input;
}

namespace zl::platform {

// GLFW 窗口封装（M1-T7）；M2 起由 render 层接管视口/缩放细节。
class Window {
public:
    struct Desc {
        int width = 1280;
        int height = 720;
        std::string title = "ZomboidLike";
        bool resizable = true;
        bool vsync = true;
    };

    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool Create(const Desc& desc);
    void Destroy();

    // glfwPollEvents + 把键鼠状态喂给 Input（每帧调用）
    void PollEvents(core::Input& input);

    bool ShouldClose() const;
    void SetShouldClose(bool value);

    GLFWwindow* Handle() const { return m_window; }
    void* NativeHandle() const;  // Win32 HWND

    int Width() const { return m_width; }
    int Height() const { return m_height; }
    int FramebufferWidth() const { return m_fbWidth; }
    int FramebufferHeight() const { return m_fbHeight; }

    // 窗口（framebuffer）尺寸变化回调；bgfx::reset 由上层负责
    void SetResizeCallback(std::function<void(int, int)> callback);
    // 原始按键回调（key, action, mods），用于调试/日志
    void SetKeyCallback(std::function<void(int, int, int)> callback);
    // 文本输入（Unicode 码点），供 UI 文本框使用
    void SetCharCallback(std::function<void(unsigned int)> callback);

private:
    GLFWwindow* m_window = nullptr;
    int m_width = 0;
    int m_height = 0;
    int m_fbWidth = 0;
    int m_fbHeight = 0;
    float m_pendingScrollX = 0.0f;
    float m_pendingScrollY = 0.0f;
    std::function<void(int, int)> m_resizeCallback;
    std::function<void(int, int, int)> m_keyCallback;
    std::function<void(unsigned int)> m_charCallback;
};

}  // namespace zl::platform
