#include "game/states/SandboxState.h"

#include <chrono>
#include <string>
#include <thread>

#include <bgfx/bgfx.h>

#include "core/EventBus.h"
#include "core/GameLoop.h"
#include "core/Input.h"
#include "core/Log.h"
#include "core/TaskScheduler.h"
#include "game/EventProbe.h"
#include "platform/Window.h"

namespace zl::game {
namespace {

// 每多少 tick 打印一次"tick 数 + 游戏时间"（300 tick = 5 秒 @1x）
constexpr uint64_t kTimeLogInterval = 300;

constexpr int kTaskBatchSize = 100;

}  // namespace

void SandboxState::OnEnter() {
    ZL_INFO("sandbox", "进入 Sandbox（M1 验收场景）");
    ZL_INFO("sandbox", "按键：F1 暂停 | F2 倍速 | F3 重跑任务 demo | F5 热重载键位 | ESC 退出");
    StartTaskDemo();
}

void SandboxState::OnExit() {
    ZL_INFO("sandbox", "退出 Sandbox：本状态共 {} tick", m_ticksInState);
}

void SandboxState::OnTick(double tickDt) {
    (void)tickDt;
    ++m_ticksInState;
    if (m_ticksInState % kTimeLogInterval == 0) {
        const auto& now = m_ctx.gameTime->Now();
        ZL_INFO("sandbox", "tick={} | 游戏时间 {} | 倍速 {} | {}{}", m_ctx.loop->TotalTicks(),
                core::FormatDateTime(now), core::GameTime::SpeedName(m_ctx.gameTime->GetSpeed()),
                m_ctx.gameTime->IsNight() ? "夜晚" : "白天", "");
    }
}

void SandboxState::OnFrame(double frameDt, double alpha) {
    (void)frameDt;
    (void)alpha;
    HandleFrameInput();
    DrawDebugText();
}

void SandboxState::HandleFrameInput() {
    core::Input& input = *m_ctx.input;
    core::EventBus& events = *m_ctx.events;

    if (input.IsActionPressed("Quit")) {
        ZL_INFO("sandbox", "退出请求（Quit）");
        events.Emit(EventProbe::kInputActionEvent, std::string("Quit"));
        m_ctx.window->SetShouldClose(true);
    }
    if (input.IsActionPressed("Pause")) {
        events.Emit(EventProbe::kInputActionEvent, std::string("Pause"));
        TogglePause();
    }
    if (input.IsActionPressed("CycleSpeed")) {
        events.Emit(EventProbe::kInputActionEvent, std::string("CycleSpeed"));
        CycleSpeed();
    }
    if (input.IsActionPressed("DemoTasks")) {
        events.Emit(EventProbe::kInputActionEvent, std::string("DemoTasks"));
        StartTaskDemo();
    }
    if (input.IsActionPressed("ReloadBindings")) {
        events.Emit(EventProbe::kInputActionEvent, std::string("ReloadBindings"));
        m_ctx.reloadConfigAndBindings();
    }
}

void SandboxState::TogglePause() {
    core::GameTime& time = *m_ctx.gameTime;
    if (time.GetSpeed() == core::GameTime::Speed::Paused) {
        time.SetSpeed(m_savedSpeed);
    } else {
        m_savedSpeed = time.GetSpeed();
        time.SetSpeed(core::GameTime::Speed::Paused);
    }
    ZL_INFO("sandbox", "F1 暂停切换 → 当前 {}", core::GameTime::SpeedName(time.GetSpeed()));
    m_ctx.events->Emit(EventProbe::kSpeedChangedEvent, time.SpeedScale());
}

void SandboxState::CycleSpeed() {
    core::GameTime& time = *m_ctx.gameTime;
    time.CycleSpeed();
    if (time.GetSpeed() != core::GameTime::Speed::Paused) {
        m_savedSpeed = time.GetSpeed();
    }
    ZL_INFO("sandbox", "F2 倍速循环 → 当前 {}", core::GameTime::SpeedName(time.GetSpeed()));
    m_ctx.events->Emit(EventProbe::kSpeedChangedEvent, time.SpeedScale());
}

void SandboxState::StartTaskDemo() {
    ++m_taskBatches;
    m_tasksCompleted = 0;
    m_taskBatchSize = kTaskBatchSize;

    ZL_INFO("sandbox", "提交 {} 个模拟加载任务（批次 #{}）...", kTaskBatchSize, m_taskBatches);
    const auto submitTime = std::chrono::steady_clock::now();
    for (int i = 0; i < kTaskBatchSize; ++i) {
        m_ctx.tasks->SubmitResult(
            // 后台线程：模拟加载耗时的差异（结果完成顺序是乱序的）
            [i] { std::this_thread::sleep_for(std::chrono::milliseconds(5 + (i * 7) % 36)); },
            // 主线程消费：按结果队列 FIFO 顺序回投
            [this, i, submitTime] {
                ++m_tasksCompleted;
                const auto elapsedMs = std::chrono::duration<double, std::milli>(
                                           std::chrono::steady_clock::now() - submitTime)
                                           .count();
                ZL_INFO("sandbox", "任务 {} 结果回投（第 {}/{} 个，距提交 {:.1f}ms）", i,
                        m_tasksCompleted, m_taskBatchSize, elapsedMs);
                if (m_tasksCompleted == m_taskBatchSize) {
                    ZL_INFO("sandbox", "批次 #{} 全部 {} 个任务回投完成，主线程按序消费无遗漏",
                            m_taskBatches, m_taskBatchSize);
                }
            });
    }
}

void SandboxState::DrawDebugText() {
    const bgfx::Stats* stats = bgfx::getStats();
    const auto& now = m_ctx.gameTime->Now();

    const std::string timeText = core::FormatDateTime(now);
    bgfx::dbgTextClear();
    bgfx::dbgTextPrintf(0, 1, 0x0f, "ZomboidLike M1 | State: %s", Name());
    bgfx::dbgTextPrintf(0, 2, 0x0f, "FPS: %.0f | TPS: %.0f | frame: %.1fms | draw: %u",
                        m_ctx.loop->Fps(), m_ctx.loop->Tps(), m_ctx.loop->LastFrameMs(),
                        stats->numDraw);
    bgfx::dbgTextPrintf(0, 3, 0x0f, "tick: %llu | %s | speed: %s | %s",
                        static_cast<unsigned long long>(m_ctx.loop->TotalTicks()),
                        timeText.c_str(),
                        core::GameTime::SpeedName(m_ctx.gameTime->GetSpeed()),
                        m_ctx.gameTime->IsNight() ? "night" : "day");
    bgfx::dbgTextPrintf(0, 4, 0x0f, "pending results: %zu | tasks done: %d/%d (batch #%d)",
                        m_ctx.tasks->PendingResults(), m_tasksCompleted, m_taskBatchSize,
                        m_taskBatches);
    bgfx::dbgTextPrintf(0, 6, 0x07, "F1 pause | F2 speed | F3 task demo | F5 reload keys | ESC quit");
}

}  // namespace zl::game
