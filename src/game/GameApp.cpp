#include "game/GameApp.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <string>

#include <GLFW/glfw3.h>

#include "core/Log.h"
#include "game/states/RenderDemoState.h"
#include "game/states/SandboxState.h"
#include "game/states/WorldDemoState.h"
#include "platform/KeyNames.h"
#include "world/MapLoader.h"

namespace zl::game {
namespace {

// 拖动窗口/断点恢复后 frameDt 会很大：钳制上限（why：防止物理/累加器爆炸）
constexpr double kMaxFrameDt = 0.25;

constexpr std::pair<const char*, const char*> kDefaultBindings[] = {
    {"Quit", "ESCAPE"},      {"Pause", "F1"},       {"CycleSpeed", "F2"},
    {"DemoTasks", "F3"},     {"ReloadBindings", "F5"}, {"ToggleCruise", "F8"},
    {"ToggleDebugOverlay", "F9"},
    {"MoveUp", "W"},         {"MoveDown", "S"},     {"MoveLeft", "A"},
    {"MoveRight", "D"},
};

double NowSeconds() {
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

int GameApp::Run(int argc, char** argv) {
    core::InitLogging();
    int result = 1;
    try {
        GameApp app;
        if (app.Init(argc, argv)) {
            app.MainLoop();
            result = 0;
        }
    } catch (const std::exception& e) {
        ZL_CRITICAL("core", "未捕获异常: {}", e.what());
    }
    ZL_INFO("core", "退出完成");
    core::ShutdownLogging();
    return result;
}

GameApp::~GameApp() {
    Shutdown();
}

bool GameApp::Init(int argc, char** argv) {
    // 1. 配置
    const std::string configPath = core::Config::FindConfigPath(argc, argv, "config/game.json");
    m_config.LoadFile(configPath);
    m_config.ApplyCommandLine(argc, argv);
    core::SetLogLevel(m_config.Get<std::string>("log.level", "debug"));

    // 2. 窗口
    platform::Window::Desc desc;
    desc.width = m_config.Get<int>("window.width", 1280);
    desc.height = m_config.Get<int>("window.height", 720);
    desc.title = m_config.Get<std::string>("window.title", "ZomboidLike");
    desc.vsync = m_config.Get<bool>("window.vsync", true);
    if (!m_window.Create(desc)) {
        return false;
    }

    // 3. 渲染栈（M2-T1）
    render::RendererDesc rd;
    rd.width = m_window.FramebufferWidth();
    rd.height = m_window.FramebufferHeight();
    rd.vsync = desc.vsync;
    if (!m_renderer.Init(m_window, rd)) {
        return false;
    }
    if (!m_sprites.Init()) {
        return false;
    }
    m_camera.SetViewport(m_renderer.FramebufferWidth(), m_renderer.FramebufferHeight());

    m_window.SetResizeCallback([this](int width, int height) {
        if (width > 0 && height > 0) {
            m_renderer.Resize(width, height);
            m_camera.SetViewport(width, height);
        }
    });
    m_window.SetKeyCallback([](int key, int action, int mods) {
        (void)mods;
        if (action == GLFW_PRESS) {
            ZL_TRACE("input", "原始按键按下 key={}", key);
        }
    });

    // 4. 服务
    const int threadCount = m_config.Get<int>("task.threads", 0);
    m_tasks = std::make_unique<core::TaskScheduler>(static_cast<unsigned>(std::max(threadCount, 0)));
    m_loop = std::make_unique<core::GameLoop>(m_config.Get<double>("game.tickRate", 60.0));

    const double dayLengthMinutes = m_config.Get<double>("game.dayLengthMinutes", 60.0);
    const double gameMinutesPerRealSecond = 1440.0 / (dayLengthMinutes * 60.0);
    core::DateTime startDate;
    startDate.year = m_config.Get<int>("game.startYear", 2000);
    startDate.month = m_config.Get<int>("game.startMonth", 7);
    startDate.day = m_config.Get<int>("game.startDay", 9);
    m_gameTime.Configure(gameMinutesPerRealSecond, startDate);
    ZL_INFO("core", "游戏时间：现实 {:.0f} 分钟 = 游戏 1 天；开局时间 {}",
            dayLengthMinutes, core::FormatDateTime(startDate));

    ApplyInputBindings();

    // 5. 世界 + chunk 流式加载（M3-T4/T5）
    const std::string mapDir = m_config.Get<std::string>("world.mapDir", "assets/maps/village_demo");
    world::MapLoader::Meta worldMeta;
    std::string mapError;
    if (!world::MapLoader::LoadMeta(mapDir, &worldMeta, &mapError)) {
        // 降级：空世界（全默认地形），游戏可运行
        ZL_ERROR("world", "地图元数据加载失败（降级为空世界）: {}", mapError);
        worldMeta = world::MapLoader::Meta{};
        worldMeta.rootDir = mapDir;
        worldMeta.chunksX = 0;
        worldMeta.chunksY = 0;
    } else {
        ZL_INFO("world", "地图已加载: {}（{}×{} chunks，默认地面 {}）", worldMeta.name,
                worldMeta.chunksX, worldMeta.chunksY, worldMeta.defaultFloor);
    }
    const int loadRadius = m_config.Get<int>("world.loadRadius", core::kChunkLoadRadius);
    m_world = std::make_unique<world::World>();
    m_chunkManager =
        std::make_unique<world::ChunkManager>(*m_world, std::move(worldMeta), *m_tasks, loadRadius);
    m_ctx.world = m_world.get();
    m_ctx.chunkManager = m_chunkManager.get();

    // 6. 上下文 + 事件探针
    m_ctx.config = &m_config;
    m_ctx.events = &m_events;
    m_ctx.input = &m_input;
    m_ctx.gameTime = &m_gameTime;
    m_ctx.tasks = m_tasks.get();
    m_ctx.loop = m_loop.get();
    m_ctx.states = &m_states;
    m_ctx.window = &m_window;
    m_ctx.renderer = &m_renderer;
    m_ctx.camera = &m_camera;
    m_ctx.sprites = &m_sprites;
    m_ctx.atlases = &m_atlases;
    m_ctx.assets = &m_assets;
    m_ctx.reloadConfigAndBindings = [this] { ReloadConfigAndBindings(); };

    m_probe.Attach(m_events);

    // 7. 初始状态（Boot/MainMenu 在后续里程碑接入）
    const std::string startState = m_config.Get<std::string>("game.startState", "world");
    if (startState == "sandbox") {
        m_states.SetState(std::make_unique<SandboxState>(m_ctx));
    } else if (startState == "render") {
        m_states.SetState(std::make_unique<RenderDemoState>(m_ctx));
    } else {
        m_states.SetState(std::make_unique<WorldDemoState>(m_ctx));
    }
    m_states.ApplyPendingState();
    m_events.Emit(core::kOnGameStart);

    m_lastFrameTime = NowSeconds();
    ZL_INFO("core", "初始化完成");
    return true;
}

void GameApp::Shutdown() {
    if (m_shutdownDone) {
        return;
    }
    m_shutdownDone = true;

    m_probe.Detach(m_events);
    m_states.Clear();
    // 渲染资源必须在 bgfx::shutdown 之前释放
    m_sprites.Shutdown();
    m_atlases.Clear();
    m_textures.Clear();
    m_renderer.Shutdown();
    m_window.Destroy();
    m_loop.reset();
    m_chunkManager.reset();
    m_world.reset();
    m_tasks.reset();  // 最后回收线程池
}

void GameApp::MainLoop() {
    ZL_INFO("core", "进入主循环");
    while (!m_window.ShouldClose()) {
        // 输入：先快照上帧，再拉取本帧事件
        m_input.NewFrame();
        m_window.PollEvents(m_input);

        double frameDt = NowSeconds() - m_lastFrameTime;
        m_lastFrameTime = NowSeconds();
        frameDt = std::clamp(frameDt, 0.0, kMaxFrameDt);

        m_loop->SetTimeScale(m_gameTime.SpeedScale());
        m_loop->Frame(frameDt, [this](double tickDt) { Tick(tickDt); },
                      [this](double dt, double alpha) { RenderFrame(dt, alpha); });

        // 主线程消费后台结果（chunk 加载完成、存档快照等）
        m_tasks->ProcessResults();

        // 状态延迟切换生效
        m_states.ApplyPendingState();
    }
    ZL_INFO("core", "主循环退出");
}

void GameApp::Tick(double tickDt) {
    m_gameTime.Tick();
    m_events.EmitQueued(core::kOnTick, static_cast<int64_t>(m_loop->TotalTicks()));
    if (auto* state = m_states.Current()) {
        state->OnTick(tickDt);
    }
    m_events.FlushQueued();
}

void GameApp::RenderFrame(double frameDt, double alpha) {
    m_sprites.FrameBegin();
    m_renderer.BeginFrame();
    if (auto* state = m_states.Current()) {
        state->OnFrame(frameDt, alpha);
    }
    m_renderer.EndFrame();
}

void GameApp::ApplyInputBindings() {
    m_input.ClearBindings();
    for (const auto& [action, defaultKey] : kDefaultBindings) {
        const std::string keyName = m_config.Get<std::string>(
            std::string("input.bindings.") + action, defaultKey);
        const int code = platform::KeyCodeFromName(keyName);
        if (code < 0) {
            ZL_ERROR("input", "键位配置无效：action={} key={}", action, keyName);
            continue;
        }
        m_input.Bind(action, code);
        ZL_DEBUG("input", "键位映射: {} = {}", action, keyName);
    }
}

void GameApp::ReloadConfigAndBindings() {
    if (m_config.Reload()) {
        ApplyInputBindings();
        ZL_INFO("input", "键位已按新配置重建（窗口尺寸等其余项需重启生效）");
    }
}

}  // namespace zl::game
