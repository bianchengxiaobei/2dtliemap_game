#pragma once

#include <cstdint>
#include <memory>

#include "core/AppState.h"
#include "core/Config.h"
#include "core/EventBus.h"
#include "core/GameLoop.h"
#include "core/GameTime.h"
#include "core/Input.h"
#include "core/TaskScheduler.h"
#include "game/EventProbe.h"
#include "game/GameContext.h"
#include "platform/Window.h"

namespace zl::game {

// 顶层组装（M1-T7）：服务初始化 → 状态机（Sandbox 占位）→ 固定 tick 主循环。
class GameApp {
public:
    static int Run(int argc, char** argv);

    GameApp() = default;
    ~GameApp();

    GameApp(const GameApp&) = delete;
    GameApp& operator=(const GameApp&) = delete;

private:
    bool Init(int argc, char** argv);
    void Shutdown();
    void MainLoop();
    void Tick(double tickDt);
    void RenderFrame(double frameDt, double alpha);
    void ApplyInputBindings();
    void ReloadConfigAndBindings();

    core::Config m_config;
    core::EventBus m_events;
    core::Input m_input;
    core::GameTime m_gameTime;
    platform::Window m_window;
    core::AppStateMachine m_states;
    EventProbe m_probe;
    std::unique_ptr<core::TaskScheduler> m_tasks;
    std::unique_ptr<core::GameLoop> m_loop;
    GameContext m_ctx;

    uint32_t m_resetFlags = 0;
    double m_lastFrameTime = 0.0;
    bool m_bgfxInitialized = false;
    bool m_shutdownDone = false;
};

}  // namespace zl::game
