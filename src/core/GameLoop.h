#pragma once

#include <cstdint>
#include <functional>

namespace zl::core {

// 固定步长主循环（docs/00 §3.1）：累加器 + 倍速；渲染跟随 vsync 与模拟解耦。
// 用法：每帧调一次 Frame()，onTick 会被调用 0..N 次（N 上限防死亡螺旋）。
class GameLoop {
public:
    using TickFn = std::function<void(double tickDt)>;
    using FrameFn = std::function<void(double frameDt, double alpha)>;

    explicit GameLoop(double tickRate);

    // 时间倍速：0=暂停；GameTime 的倍速档位同步到这里
    void SetTimeScale(double scale);
    double TimeScale() const { return m_timeScale; }

    void Frame(double frameDt, const TickFn& onTick, const FrameFn& onFrame);

    double Fps() const { return m_fps; }
    double Tps() const { return m_tps; }
    double LastFrameMs() const { return m_lastFrameMs; }
    uint64_t TotalTicks() const { return m_totalTicks; }
    uint64_t TotalFrames() const { return m_totalFrames; }

private:
    double m_tickDt;
    double m_accumulator = 0.0;
    double m_timeScale = 1.0;

    uint64_t m_totalTicks = 0;
    uint64_t m_totalFrames = 0;

    // 秒级统计窗口
    double m_statsElapsed = 0.0;
    uint32_t m_framesInWindow = 0;
    uint32_t m_ticksInWindow = 0;
    double m_fps = 0.0;
    double m_tps = 0.0;

    double m_lastFrameMs = 0.0;
    double m_warnCooldown = 0.0;
};

}  // namespace zl::core
