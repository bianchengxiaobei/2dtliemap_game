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
#include "game/AssetProvider.h"
#include "game/EventProbe.h"
#include "game/GameContext.h"
#include "platform/Window.h"
#include "render/Atlas.h"
#include "render/Camera.h"
#include "render/Renderer.h"
#include "render/SpriteBatch.h"
#include "render/Texture2D.h"
#include "world/ChunkManager.h"
#include "world/World.h"

namespace zl::game {

// 顶层组装（M1-T7 / M2 扩展）：服务初始化 → 渲染栈 → 状态机 → 固定 tick 主循环。
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

    // 渲染栈（声明顺序 = 初始化/析构顺序约束：textures → atlases → sprites → renderer）
    render::TextureManager m_textures;
    render::AtlasManager m_atlases{m_textures};
    render::Renderer m_renderer;
    render::SpriteBatch m_sprites;
    render::Camera m_camera;
    AssetProvider m_assets;

    // 世界 + chunk 流式加载（M3）
    std::unique_ptr<world::World> m_world;
    std::unique_ptr<world::ChunkManager> m_chunkManager;

    double m_lastFrameTime = 0.0;
    bool m_shutdownDone = false;
};

}  // namespace zl::game
