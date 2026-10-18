#include "render/Renderer.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <utility>
#include <vector>

#include <bgfx/platform.h>
#include <bx/timer.h>

#include "core/Log.h"
#include "platform/Window.h"

namespace zl::render {

class ScreenshotCallback final : public bgfx::CallbackI {
public:
    bool Poll(ScreenshotImage* out) {
        if (out == nullptr) {
            return false;
        }
        std::lock_guard lock(m_mutex);
        if (m_pending.empty()) {
            return false;
        }
        *out = std::move(m_pending.front());
        m_pending.erase(m_pending.begin());
        return true;
    }

    void fatal(const char* filePath, std::uint16_t line, bgfx::Fatal::Enum code,
               const char* message) override {
        ZL_CRITICAL("render", "bgfx fatal {}:{} code={} {}", filePath != nullptr ? filePath : "?",
                    line, static_cast<int>(code), message != nullptr ? message : "");
        std::abort();
    }

    void traceVargs(const char* filePath, std::uint16_t line, const char* format,
                    va_list args) override {
        (void)filePath;
        (void)line;
        char buffer[2048]{};
        if (format != nullptr) {
            std::vsnprintf(buffer, sizeof(buffer), format, args);
        }
        ZL_DEBUG("bgfx", "{}", buffer);
    }

    void profilerBegin(const char* name, std::uint32_t color, const char* filePath,
                       std::uint16_t line) override {
        (void)name;
        (void)color;
        (void)filePath;
        (void)line;
    }

    void profilerBeginLiteral(const char* name, std::uint32_t color,
                              const char* filePath, std::uint16_t line) override {
        (void)name;
        (void)color;
        (void)filePath;
        (void)line;
    }

    void profilerEnd() override {}

    std::uint32_t cacheReadSize(std::uint64_t id) override {
        (void)id;
        return 0;
    }

    bool cacheRead(std::uint64_t id, void* data, std::uint32_t size) override {
        (void)id;
        (void)data;
        (void)size;
        return false;
    }

    void cacheWrite(std::uint64_t id, const void* data, std::uint32_t size) override {
        (void)id;
        (void)data;
        (void)size;
    }

    void screenShot(const char* filePath, std::uint32_t width,
                    std::uint32_t height, std::uint32_t pitch, const void* data,
                    std::uint32_t size, bool yflip) override {
        ScreenshotImage image;
        if (!ConvertBgraToRgba(data, width, height, pitch, size, yflip, &image)) {
            ZL_WARN("render", "屏幕截图数据无效: {}x{} pitch={}", width, height, pitch);
            return;
        }
        if (filePath != nullptr) {
            image.filePath = filePath;
        }
        std::lock_guard lock(m_mutex);
        if (m_pending.size() >= 4) {
            m_pending.erase(m_pending.begin());
        }
        m_pending.push_back(std::move(image));
    }

    void captureBegin(std::uint32_t width, std::uint32_t height,
                      std::uint32_t pitch, bgfx::TextureFormat::Enum format,
                      bool yflip) override {
        (void)width;
        (void)height;
        (void)pitch;
        (void)format;
        (void)yflip;
    }

    void captureEnd() override {}

    void captureFrame(const void* data, std::uint32_t size) override {
        (void)data;
        (void)size;
    }

private:
    std::mutex m_mutex;
    std::vector<ScreenshotImage> m_pending;
};

Renderer::Renderer() = default;
Renderer::~Renderer() = default;

bool Renderer::Init(platform::Window& window, const RendererDesc& desc) {
    bgfx::PlatformData pd{};
    pd.nwh = window.NativeHandle();

    bgfx::Init init;
    init.type = desc.backend;
    init.platformData = pd;
    init.resolution.width = static_cast<uint32_t>(std::max(desc.width, 1));
    init.resolution.height = static_cast<uint32_t>(std::max(desc.height, 1));
    m_resetFlags = desc.vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;
    init.resolution.reset = m_resetFlags;
    m_screenshotCallback = std::make_unique<ScreenshotCallback>();
    init.callback = m_screenshotCallback.get();

    if (!bgfx::init(init)) {
        ZL_CRITICAL("render", "bgfx::init 失败");
        m_screenshotCallback.reset();
        return false;
    }
    m_initialized = true;
    m_fbWidth = desc.width;
    m_fbHeight = desc.height;
    bgfx::setDebug(BGFX_DEBUG_TEXT);

    ZL_INFO("render", "Renderer 初始化完成：后端={}，{}x{}，vsync={}", BackendName(), m_fbWidth,
            m_fbHeight, desc.vsync);
    return true;
}

void Renderer::Shutdown() {
    if (m_initialized) {
        bgfx::shutdown();
        m_initialized = false;
    }
    m_screenshotCallback.reset();
    m_screenshotPath.clear();
}

bool Renderer::RequestScreenshot(std::string filePath) {
    if (!m_initialized || m_screenshotCallback == nullptr || filePath.empty()) {
        return false;
    }
    m_screenshotPath = std::move(filePath);
    bgfx::requestScreenShot(BGFX_INVALID_HANDLE, m_screenshotPath.c_str());
    return true;
}

bool Renderer::PollScreenshot(ScreenshotImage* out) {
    return m_screenshotCallback != nullptr &&
           m_screenshotCallback->Poll(out);
}

void Renderer::Resize(int fbWidth, int fbHeight) {
    if (!m_initialized || fbWidth <= 0 || fbHeight <= 0) {
        return;
    }
    m_fbWidth = fbWidth;
    m_fbHeight = fbHeight;
    bgfx::reset(static_cast<uint32_t>(fbWidth), static_cast<uint32_t>(fbHeight), m_resetFlags);
}

void Renderer::BeginFrame() {
    const uint16_t w = static_cast<uint16_t>(m_fbWidth);
    const uint16_t h = static_cast<uint16_t>(m_fbHeight);

    // View 0：世界（清色 + 清深度；先深灰底，M2-T6 起被地图覆盖）
    bgfx::setViewRect(static_cast<bgfx::ViewId>(ViewId::World), 0, 0, w, h);
    bgfx::setViewClear(static_cast<bgfx::ViewId>(ViewId::World),
                       BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x141a20ff, 1.0f, 0);
    bgfx::touch(static_cast<bgfx::ViewId>(ViewId::World));

    // View 1：UI（不清屏，叠加）
    bgfx::setViewRect(static_cast<bgfx::ViewId>(ViewId::UI), 0, 0, w, h);

    // View 2：调试（不清屏，叠加在世界之上）
    bgfx::setViewRect(static_cast<bgfx::ViewId>(ViewId::Debug), 0, 0, w, h);
}

void Renderer::EndFrame() { bgfx::frame(); }

Renderer::Stats Renderer::GetStats() const {
    Stats stats;
    if (!m_initialized) {
        return stats;
    }
    const bgfx::Stats* bgfxStats = bgfx::getStats();
    if (bgfxStats != nullptr) {
        stats.drawCalls = bgfxStats->numDraw;
        // cpuTimeFrame 单位是 QPC 计数（bgfx 内部用 bx::getHPCounter 计时），
        // 注意它不是 gpuTimerFreq 的计数（那常是 TSC 频率，两者不同源）。
        const double freq = static_cast<double>(bx::getHPFrequency());
        const double frameSeconds =
            freq > 0.0 ? static_cast<double>(bgfxStats->cpuTimeFrame) / freq : 0.0;
        stats.fps = frameSeconds > 0.0 ? static_cast<float>(1.0 / frameSeconds) : 0.0f;
    }
    return stats;
}

}  // namespace zl::render
