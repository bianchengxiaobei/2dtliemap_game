#include "core/GameLoop.h"

#include <algorithm>
#include <chrono>

#include "core/Log.h"

namespace zl::core {
namespace {

constexpr int kMaxTicksPerFrame = 10;      // 单帧 tick 上限（断点/卡顿恢复后防连续追帧）
constexpr double kFrameBudgetMs = 16.6;    // 60fps 帧预算
constexpr double kBudgetWarnScale = 1.2;   // 超预算 20% 才告警
constexpr double kWarnCooldownSec = 1.0;

}  // namespace

GameLoop::GameLoop(double tickRate) : m_tickDt(1.0 / std::max(tickRate, 1.0)) {}

void GameLoop::SetTimeScale(double scale) {
    m_timeScale = std::max(scale, 0.0);
}

void GameLoop::Frame(double frameDt, const TickFn& onTick, const FrameFn& onFrame) {
    const auto frameStart = std::chrono::steady_clock::now();

    m_accumulator += frameDt * m_timeScale;

    int ticksThisFrame = 0;
    while (m_accumulator >= m_tickDt) {
        onTick(m_tickDt);
        m_accumulator -= m_tickDt;
        ++m_totalTicks;
        ++m_ticksInWindow;

        if (++ticksThisFrame >= kMaxTicksPerFrame) {
            if (m_accumulator > m_tickDt * 2.0) {
                ZL_WARN("core", "单帧 tick 数达到上限 {}，丢弃 {:.1f}ms 积压",
                        kMaxTicksPerFrame, m_accumulator * 1000.0);
                m_accumulator = 0.0;
            }
            break;
        }
    }

    const double alpha = m_accumulator / m_tickDt;
    onFrame(frameDt, alpha);

    ++m_totalFrames;
    ++m_framesInWindow;
    m_statsElapsed += frameDt;
    if (m_statsElapsed >= 1.0) {
        m_fps = static_cast<double>(m_framesInWindow) / m_statsElapsed;
        m_tps = static_cast<double>(m_ticksInWindow) / m_statsElapsed;
        m_framesInWindow = 0;
        m_ticksInWindow = 0;
        m_statsElapsed = 0.0;
    }

    m_lastFrameMs = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - frameStart)
                        .count();
    if (m_lastFrameMs > kFrameBudgetMs * kBudgetWarnScale) {
        m_warnCooldown -= frameDt;
        if (m_warnCooldown <= 0.0) {
            ZL_WARN("core", "帧时长 {:.1f}ms 超出预算 {:.1f}ms（FPS {:.0f}）",
                    m_lastFrameMs, kFrameBudgetMs, m_fps);
            m_warnCooldown = kWarnCooldownSec;
        }
    }
}

}  // namespace zl::core
