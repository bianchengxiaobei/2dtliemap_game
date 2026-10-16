#pragma once

#include <cstdint>

#include <bgfx/bgfx.h>

namespace zl::platform {
class Window;
}

namespace zl::render {

// View 规划（docs/00 §5.1）
enum class ViewId : bgfx::ViewId {
    World = 0,  // 等距世界（clear color+depth，先透明黑底）
    UI = 1,     // 正交屏幕空间 UI（M9 接入）
    Debug = 2,  // 调试叠加：网格线/坐标轴/高亮（可开关）
};

struct RendererDesc {
    int width = 1280;
    int height = 720;
    bool vsync = true;
    bgfx::RendererType::Enum backend = bgfx::RendererType::Count;  // Count=自动选择
};

// M2-T1：bgfx 初始化参数化、View/视口管理、每帧统计。
class Renderer {
public:
    bool Init(platform::Window& window, const RendererDesc& desc);
    void Shutdown();

    // 窗口尺寸变化（framebuffer 分辨率）时调用
    void Resize(int fbWidth, int fbHeight);

    // 帧边界：BeginFrame 设置 3 个 view 的 rect 与 clear；EndFrame 提交 bgfx::frame()
    void BeginFrame();
    void EndFrame();

    struct Stats {
        float fps = 0.0f;
        uint32_t drawCalls = 0;  // 上一帧 bgfx 提交的 drawcall 数
    };
    Stats GetStats() const;

    int FramebufferWidth() const { return m_fbWidth; }
    int FramebufferHeight() const { return m_fbHeight; }
    const char* BackendName() const { return bgfx::getRendererName(bgfx::getRendererType()); }

private:
    int m_fbWidth = 0;
    int m_fbHeight = 0;
    uint32_t m_resetFlags = BGFX_RESET_NONE;
    bool m_initialized = false;
};

}  // namespace zl::render
