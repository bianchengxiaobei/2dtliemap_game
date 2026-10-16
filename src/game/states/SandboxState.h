#pragma once

#include <cstdint>

#include "core/AppState.h"
#include "core/GameTime.h"
#include "game/GameContext.h"

namespace zl::game {

// M1 占位状态（Sandbox）：驱动验收场景——
//   F1 暂停 / F2 倍速 / F3 重跑 100 任务回投 demo / F5 热重载键位 / ESC 退出；
//   定期打印 tick 数与游戏时间；调试文本显示 FPS/TPS。
class SandboxState final : public core::AppState {
public:
    explicit SandboxState(GameContext& context) : m_ctx(context) {}

    const char* Name() const override { return "Sandbox"; }

    void OnEnter() override;
    void OnExit() override;
    void OnTick(double tickDt) override;
    void OnFrame(double frameDt, double alpha) override;

private:
    void HandleFrameInput();
    void TogglePause();
    void CycleSpeed();
    void StartTaskDemo();
    void DrawDebugText();

    GameContext& m_ctx;
    core::GameTime::Speed m_savedSpeed = core::GameTime::Speed::X1;
    uint64_t m_ticksInState = 0;
    int m_tasksCompleted = 0;
    int m_taskBatchSize = 0;
    int m_taskBatches = 0;
};

}  // namespace zl::game
