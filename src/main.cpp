// ZomboidLike 入口（M0：GLFW 窗口 + bgfx 初始化 + 日志系统）
// 后续 M1 会把窗口封装进 platform/Window、主循环移入 core/Loop。

#include <chrono>
#include <cstdint>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include "core/Log.h"

namespace {

constexpr uint32_t kWidth = 1280;
constexpr uint32_t kHeight = 720;
constexpr const char* kTitle = "ZomboidLike";

struct AppContext {
    uint32_t width = kWidth;
    uint32_t height = kHeight;
};

void GlfwErrorCallback(int code, const char* description) {
    ZL_ERROR("platform", "GLFW 错误 {}: {}", code, description);
}

void KeyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) {
        return;
    }
    ZL_DEBUG("input", "按键按下 key={}", key);
    if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

void FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto* ctx = static_cast<AppContext*>(glfwGetWindowUserPointer(window));
    if (width <= 0 || height <= 0) {
        return;
    }
    ctx->width = static_cast<uint32_t>(width);
    ctx->height = static_cast<uint32_t>(height);
    bgfx::reset(ctx->width, ctx->height, BGFX_RESET_VSYNC);
    ZL_INFO("platform", "窗口尺寸变化: {}x{}", width, height);
}

}  // namespace

int main() {
    zl::core::InitLogging();

    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit()) {
        ZL_CRITICAL("platform", "glfwInit 失败");
        zl::core::ShutdownLogging();
        return 1;
    }

    // bgfx 自行管理渲染上下文，GLFW 不创建 OpenGL 上下文
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* window = glfwCreateWindow(static_cast<int>(kWidth), static_cast<int>(kHeight),
                                          kTitle, nullptr, nullptr);
    if (!window) {
        ZL_CRITICAL("platform", "创建窗口失败");
        glfwTerminate();
        zl::core::ShutdownLogging();
        return 1;
    }

    AppContext ctx;
    glfwSetWindowUserPointer(window, &ctx);
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
    glfwSetWindowSizeCallback(window, [](GLFWwindow* w, int width, int height) {
        ZL_INFO("platform", "窗口缩放至 {}x{}", width, height);
        (void)w;
    });

    // bgfx 初始化：把 GLFW 的 Win32 窗口句柄交给 bgfx
    bgfx::PlatformData pd{};
    pd.nwh = glfwGetWin32Window(window);
    bgfx::Init init;
    init.type = bgfx::RendererType::Count;  // 自动选择后端（Windows 上通常 D3D11）
    init.platformData = pd;
    init.resolution.width = ctx.width;
    init.resolution.height = ctx.height;
    init.resolution.reset = BGFX_RESET_VSYNC;
    if (!bgfx::init(init)) {
        ZL_CRITICAL("render", "bgfx::init 失败");
        glfwDestroyWindow(window);
        glfwTerminate();
        zl::core::ShutdownLogging();
        return 1;
    }
    ZL_INFO("render", "bgfx 初始化完成，渲染后端: {}",
            bgfx::getRendererName(bgfx::getRendererType()));
    bgfx::setDebug(BGFX_DEBUG_TEXT);

    // 主循环：M1 会替换为固定 tick GameLoop，这里先空转 + 每帧清屏统计
    double lastStatsTime = glfwGetTime();
    uint32_t frameCount = 0;
    uint32_t fps = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (ctx.width > 0 && ctx.height > 0) {
            bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(ctx.width),
                              static_cast<uint16_t>(ctx.height));
        }
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x1d1d1dff, 1.0f, 0);
        bgfx::touch(0);

        const bgfx::Stats* stats = bgfx::getStats();

        ++frameCount;
        const double now = glfwGetTime();
        if (now - lastStatsTime >= 1.0) {
            fps = static_cast<uint32_t>(frameCount / (now - lastStatsTime));
            frameCount = 0;
            lastStatsTime = now;
            ZL_DEBUG("render", "FPS {} | draw {}", fps, stats->numDraw);
        }

        bgfx::dbgTextClear();
        bgfx::dbgTextPrintf(0, 1, 0x0f, "ZomboidLike M0 | FPS: %u", fps);
        bgfx::dbgTextPrintf(0, 2, 0x0f, "View: %ux%u | Draw: %u | Compute: %u | Blit: %u",
                            ctx.width, ctx.height, stats->numDraw, stats->numCompute,
                            stats->numBlit);
        bgfx::dbgTextPrintf(0, 3, 0x07, "ESC to quit");

        bgfx::frame();
    }

    ZL_INFO("core", "开始退出...");
    bgfx::shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    ZL_INFO("core", "退出完成");
    zl::core::ShutdownLogging();
    return 0;
}
