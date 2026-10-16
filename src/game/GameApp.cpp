#include "game/GameApp.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <string>

#include <GLFW/glfw3.h>

#include "core/Log.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/WorldQueries.h"
#include "game/ZombieFactory.h"
#include "game/states/RenderDemoState.h"
#include "game/states/SandboxState.h"
#include "game/states/WorldDemoState.h"
#include "platform/KeyNames.h"
#include "render/IsoMath.h"
#include "script/LuaRuntime.h"
#include "script/ModLoader.h"
#include "script/PythonSupport.h"
#include "world/MapLoader.h"

namespace zl::game {
namespace {

// 拖动窗口/断点恢复后 frameDt 会很大：钳制上限（why：防止物理/累加器爆炸）
constexpr double kMaxFrameDt = 0.25;

constexpr std::pair<const char*, const char*> kDefaultBindings[] = {
    {"Quit", "ESCAPE"},      {"Pause", "F1"},       {"CycleSpeed", "F2"},
    {"DemoTasks", "F3"},     {"ReloadBindings", "F5"}, {"ToggleCruise", "F8"},
    {"ToggleDebugOverlay", "F9"}, {"TogglePhysicsDebug", "F10"}, {"DemoKill", "F6"},
    {"DemoGunshot", "F7"},   // M5-T4：演示开枪（大半径 SoundEvent）
    {"Attack", "SPACE"},     // M5-T6：玩家挥击（演示用自动瞄准，M6 接武器/朝向）
    {"Pickup", "F"},         // M6-T3：拾取最近地面物品
    {"Use", "E"},            // M6-T5：使用消耗品（回血/饱食）
    {"Equip", "R"},          // M6-T5：装备/卸下主手武器
    {"Drop", "G"},           // M6-T5：丢弃背包第一堆
    {"MoveUp", "W"},         {"MoveDown", "S"},     {"MoveLeft", "A"},
    {"MoveRight", "D"},
    // M4-T4：WASD 归玩家；方向键平移相机（自由相机模式，M4-T5）
    {"PanUp", "UP"},         {"PanDown", "DOWN"},   {"PanLeft", "LEFT"},
    {"PanRight", "RIGHT"},   {"ToggleFreeCam", "F4"},
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

    // 1.5 物品定义注册表（M6-T1）：启动加载 + 校验；失败不阻断启动（空表降级）
    const std::string itemsFile = m_config.Get<std::string>("items.file", "assets/items.json");
    if (!m_itemRegistry.LoadFile(itemsFile)) {
        ZL_WARN("items", "物品注册表为空，物品相关功能不可用（文件: {}）", itemsFile);
    }
    m_ctx.items = &m_itemRegistry;

    // 1.6 脚本系统（M7-T1/T2/T3）：注册 Lua 后端 + Host API 接入并初始化
    // （script.backend 决定 mod 默认后端；mod 扫描/加载在 M7-T5 接入）
    if (m_config.Get<bool>("script.enabled", true)) {
        m_scriptApi.Attach(&m_ctx);
        auto luaRuntime = std::make_unique<script::LuaRuntime>();
        luaRuntime->SetApi(&m_scriptApi);
        m_script.RegisterRuntime(std::move(luaRuntime));
        m_script.InitializeAll();
        // M7-T4：事件桥接（EventBus → 脚本 Events.On）；manager 借此在重载后发 OnModReload
        m_script.SetEventBus(&m_events);
        m_eventBridge = std::make_unique<script::EventBridge>(m_events, m_script);
        // M7-T5：扫描并加载 mods/（目录不存在则跳过；单个 mod 失败不阻断其它）
        script::ModLoader loader(m_script, m_config.Get<std::string>("script.backend", "lua"));
        const script::ModLoader::Result mods =
            loader.ScanAndLoad(m_config.Get<std::string>("script.modsDir", "mods"));
        // M7-T6：热重载监控（后台线程 mtime 轮询；变更在主循环消费 → ReloadMod）
        if (m_config.Get<bool>("script.hotReload", true)) {
            std::vector<script::ModWatcher::Entry> watched;
            for (const std::string& id : mods.loadedIds) {
                script::ModInfo info;
                if (m_script.GetModInfo(id, info)) {
                    watched.push_back(script::ModWatcher::Entry{id, info.rootPath});
                }
            }
            if (!watched.empty()) {
                const double interval =
                    m_config.Get<double>("script.hotReloadSeconds", 1.0);
                m_modWatcher = std::make_unique<script::ModWatcher>(std::move(watched), interval);
                ZL_INFO("script", "热重载监控已启动：{} 个 mod（轮询 {:.1f}s）",
                        m_modWatcher->WatchedCount(), interval);
            }
        }
        ZL_DEBUG("script", "Python 后端：{}", script::PythonBackendVersion());
        ZL_INFO("script", "脚本系统就绪（后端 {} 个，API v{}）", m_script.RuntimeCount(),
                script::ScriptApi::kApiVersion);
    } else {
        ZL_INFO("script", "脚本系统已禁用（script.enabled=false）");
    }

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

    // 5.5 物理（M4-T3）+ 寻路（M4-T6）：chunk 生命周期回调（静态碰撞体 / 通行位图）
    m_physics = std::make_unique<physics::PhysicsWorld>();
    const world::MapLoader::Meta& mapMeta = m_chunkManager->Map();
    ai::AStarOptions pathOptions;
    pathOptions.maxExpansions = m_config.Get<int>("pathfinding.maxExpansions", 200000);
    m_pathfinding = std::make_unique<ai::PathfindingService>(
        m_tasks.get(), ai::MapChunkBounds{mapMeta.chunkX0, mapMeta.chunkY0, mapMeta.chunksX,
                                          mapMeta.chunksY},
        pathOptions);
    m_chunkManager->SetChunkCallbacks(
        [this](world::Chunk& chunk, int cx, int cy) {
            m_physics->BuildChunkStatics(cx, cy, chunk);
            m_pathfinding->OnChunkLoaded(chunk);
            m_events.EmitQueued(core::kOnChunkLoaded, core::ChunkEvent{cx, cy});  // M7-T4
        },
        [this](world::Chunk& chunk, int cx, int cy) {
            (void)chunk;  // 数据仍可读，但静态体/位图按 chunk 键移除即可
            m_physics->RemoveChunkStatics(cx, cy);
            m_pathfinding->OnChunkUnloading(cx, cy);
            m_events.EmitQueued(core::kOnChunkUnloaded, core::ChunkEvent{cx, cy});  // M7-T4
        });
    m_ctx.physics = m_physics.get();
    m_ctx.pathfinding = m_pathfinding.get();

    // 5.6 AI（M5-T2）+ 感知（M5-T3）：可走查询共用（"未加载 = 不可走/遮挡"）
    const auto walkable = [this](int wx, int wy) {
        return !IsSquareBlocked(*m_world, wx, wy, 0);
    };
    // M5-T6：僵尸攻击参数（攻击脉冲 → AttackIntent → CombatSystem 伤害管线）
    ai::AiSystem::AttackParams aiAttack;
    aiAttack.damage = static_cast<float>(m_config.Get<double>("combat.zombieDamage", 8.0));
    aiAttack.knockback =
        static_cast<float>(m_config.Get<double>("combat.zombieKnockback", 2.0));
    aiAttack.staggerSeconds =
        static_cast<float>(m_config.Get<double>("combat.zombieStaggerSeconds", 0.3));
    aiAttack.range = static_cast<float>(m_config.Get<double>("combat.zombieRange", 1.3));
    aiAttack.arcDegrees =
        static_cast<float>(m_config.Get<double>("combat.zombieArcDegrees", 120.0));
    m_aiSystem = std::make_unique<ai::AiSystem>(walkable, aiAttack);
    ai::PerceptionParams perceptionParams;
    perceptionParams.sightRadius =
        static_cast<float>(m_config.Get<double>("perception.sightRadius", 12.0));
    perceptionParams.closeRadius =
        static_cast<float>(m_config.Get<double>("perception.closeRadius", 2.5));
    perceptionParams.fovDegrees =
        static_cast<float>(m_config.Get<double>("perception.fovDegrees", 110.0));
    perceptionParams.alertDecaySeconds =
        static_cast<float>(m_config.Get<double>("perception.alertDecaySeconds", 8.0));
    perceptionParams.wallAttenuation =
        static_cast<float>(m_config.Get<double>("perception.wallAttenuation", 0.6));
    m_perceptionSystem =
        std::make_unique<ai::PerceptionSystem>(walkable, perceptionParams);
    m_ctx.aiSystem = m_aiSystem.get();
    m_ctx.perception = m_perceptionSystem.get();
    // M5-T4：声音事件接线 —— 发射方（开枪等）只发事件，不感知感知系统
    m_soundSubscription = m_events.Subscribe(
        core::kOnSound, [this](const core::Event& event) {
            if (m_perceptionSystem == nullptr) {
                return;
            }
            if (const auto* sound = std::get_if<core::SoundEvent>(&event.payload)) {
                m_perceptionSystem->PushSound(*sound);
            }
        });

    // 5.7 LOD 档位（M5-T5）+ 群体行为 + 虚拟化
    ai::LodSystem::Params lodParams;
    lodParams.fullRadius = static_cast<float>(m_config.Get<double>("lod.fullRadius", 25.0));
    lodParams.midRadius = static_cast<float>(m_config.Get<double>("lod.midRadius", 60.0));
    m_lodSystem = std::make_unique<ai::LodSystem>(lodParams);
    ai::BoidsSystem::Params boidsParams;
    boidsParams.neighborRadius =
        static_cast<float>(m_config.Get<double>("boids.neighborRadius", 2.5));
    boidsParams.separationRadius =
        static_cast<float>(m_config.Get<double>("boids.separationRadius", 0.8));
    boidsParams.separationWeight =
        static_cast<float>(m_config.Get<double>("boids.separationWeight", 1.6));
    boidsParams.cohesionWeight =
        static_cast<float>(m_config.Get<double>("boids.cohesionWeight", 0.5));
    boidsParams.alignmentWeight =
        static_cast<float>(m_config.Get<double>("boids.alignmentWeight", 0.4));
    m_boidsSystem = std::make_unique<ai::BoidsSystem>(boidsParams);
    m_ctx.lod = m_lodSystem.get();
    m_ctx.boids = m_boidsSystem.get();
    m_ctx.virtualHordes = &m_virtualHordes;

    // 5.8 战斗（M5-T6）：伤害管线（伤害事件 → Health/HitReaction 统一入口）+ 倒地窗口时长
    m_combatSystem = std::make_unique<characters::CombatSystem>();
    m_ctx.combat = m_combatSystem.get();
    const float deathFallSeconds =
        static_cast<float>(m_config.Get<double>("combat.deathFallSeconds", 0.5));

    // 5.9 刷怪与虚拟化（M5-T7）：区域密度目标 + 离屏实体化/虚拟化（人口守恒）
    ai::SpawnLodSystem::Params spawnParams;
    spawnParams.populationTarget = m_config.Get<int>("spawner.populationTarget", 240);
    spawnParams.maxLive = m_config.Get<int>("spawner.maxLive", 320);
    spawnParams.materializeRadius =
        static_cast<float>(m_config.Get<double>("spawner.materializeRadius", 32.0));
    spawnParams.materializePerSecond =
        static_cast<float>(m_config.Get<double>("spawner.materializePerSecond", 16.0));
    spawnParams.minSpawnDistance =
        static_cast<float>(m_config.Get<double>("spawner.minSpawnDistance", 30.0));
    spawnParams.virtualizeRadius =
        static_cast<float>(m_config.Get<double>("spawner.virtualizeRadius", 48.0));
    spawnParams.virtualizePerSecond =
        static_cast<float>(m_config.Get<double>("spawner.virtualizePerSecond", 12.0));
    spawnParams.refillPerSecond =
        static_cast<float>(m_config.Get<double>("spawner.refillPerSecond", 2.0));
    spawnParams.refillRadius =
        static_cast<float>(m_config.Get<double>("spawner.refillRadius", 40.0));
    spawnParams.refillBandWidth =
        static_cast<float>(m_config.Get<double>("spawner.refillBandWidth", 20.0));
    spawnParams.mergeRadius =
        static_cast<float>(m_config.Get<double>("spawner.mergeRadius", 15.0));
    m_spawnLodSystem = std::make_unique<ai::SpawnLodSystem>(
        walkable,
        [this](ecs::EcsWorld& w, float px, float py) {
            return SpawnZombie(w, m_physics.get(), px, py, &m_events);
        },
        [this](ecs::EcsWorld& w, ecs::Entity e) {
            if (m_physics == nullptr) {
                return;
            }
            if (const auto* rigid = w.Registry().try_get<ecs::RigidBody>(e);
                rigid != nullptr && rigid->body != nullptr) {
                m_physics->DestroyBody(rigid->body);  // 实体销毁前回收物理体
            }
        },
        m_virtualHordes, spawnParams);
    m_ctx.spawnLod = m_spawnLodSystem.get();

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
    m_ctx.ecs = &m_ecs;
    m_ctx.spatialIndex = &m_spatialIndex;
    m_ctx.reloadConfigAndBindings = [this] { ReloadConfigAndBindings(); };

    // ECS 固定链集中注册（docs/00 §6.1）；顺序 = 执行顺序。
    // 目前：索引维护 → LOD → Input → Perception → Ai → PathFollow → Boids → Combat →
    //   VirtualHorde → Movement → PhysicsSync → Death → Animation；
    // 后续任务按链序插入（目标链：… → Movement → PhysicsSync → Combat/Death → SpawnLod →
    //   RenderCollect）。
    m_ecs.Systems().AddSystem("SpatialIndexSystem",
                              [this](ecs::EcsWorld& w, float) { m_spatialIndex.Update(w); });
    // LOD 档位（M5-T5）：紧跟索引之后、感知之前——本 tick 的门控标志供后续系统读取
    m_ecs.Systems().AddSystem("LodSystem", [this](ecs::EcsWorld& w, float) {
        const glm::vec2 camGrid = render::FromScreen(m_camera.Center(), 0);
        m_lodSystem->SetFocus(camGrid.x, camGrid.y);
        m_lodSystem->Update(w);
    });
    m_inputSystem = std::make_unique<characters::InputSystem>();
    m_movementSystem = std::make_unique<characters::MovementSystem>();
    m_physicsSyncSystem = std::make_unique<physics::PhysicsSyncSystem>(m_physics.get());
    m_animationSystem = std::make_unique<characters::AnimationSystem>();
    m_pathFollowSystem = std::make_unique<ai::PathFollowSystem>(m_pathfinding.get());
    m_ecs.Systems().AddSystem("InputSystem", [this](ecs::EcsWorld& w, float) {
        m_inputSystem->Update(m_input, w);
    });
    // 感知在 AI 之前（本 tick 写出的 targetId/lastKnown 同 tick 被 AI 消费）
    m_ecs.Systems().AddSystem("PerceptionSystem", [this](ecs::EcsWorld& w, float dt) {
        m_perceptionSystem->Update(w, dt);
    });
    // AI 在 PathFollow 之前（本 tick 写的 goal 同 tick 被提交为后台查询）
    m_ecs.Systems().AddSystem("AiSystem", [this](ecs::EcsWorld& w, float dt) {
        m_aiSystem->Update(w, dt);
    });
    // 寻路在 Input 之后（能读到本 tick 输入做"手动优先"判定）、Movement 之前（写意图）
    m_ecs.Systems().AddSystem("PathFollowSystem", [this](ecs::EcsWorld& w, float dt) {
        m_pathFollowSystem->Update(w, dt);
    });
    // 群体行为（M5-T5）：改写 PathFollow 写的意图（分离/聚合/对齐），先于 Movement 生效
    m_ecs.Systems().AddSystem("BoidsSystem",
                              [this](ecs::EcsWorld& w, float) { m_boidsSystem->Update(w); });
    // 战斗（M5-T6）：扇形攻击判定 + 伤害管线 —— 在群体行为之后（击退覆盖 flocking）、
    // Movement 之前（硬直实体的击退速度同 tick 生效）
    m_ecs.Systems().AddSystem("CombatSystem", [this](ecs::EcsWorld& w, float dt) {
        m_combatSystem->Update(w, dt);
    });
    // 虚拟群落（M5-T5）：统计层推进（朝玩家漂移）；T7 SpawnLodSystem 负责实体化
    m_ecs.Systems().AddSystem("VirtualHordeSystem", [this](ecs::EcsWorld& w, float dt) {
        float px = 0.0f;
        float py = 0.0f;
        bool hasPlayer = false;
        w.Registry().view<ecs::PlayerTag, ecs::Position>().each(
            [&](const ecs::Entity, const ecs::Position& p) {
                if (!hasPlayer) {
                    px = p.x;
                    py = p.y;
                    hasPlayer = true;
                }
            });
        if (hasPlayer) {
            m_virtualHordes.Update(dt, px, py, /*stopDistance=*/12.0f);
        }
    });
    m_ecs.Systems().AddSystem("MovementSystem", [this](ecs::EcsWorld& w, float dt) {
        m_movementSystem->Update(w, dt);
    });
    m_ecs.Systems().AddSystem("PhysicsSyncSystem", [this](ecs::EcsWorld& w, float dt) {
        m_physicsSyncSystem->Update(w, dt);
    });
    // 死亡流程（M5-T1/T6）：位置已由 PhysicsSync 回写；先倒地窗口（Dying）再转尸体；
    // 尸体创建与实体销毁在 tick 末 Flush 生效
    m_deathSystem = std::make_unique<characters::DeathSystem>(m_physics.get(), deathFallSeconds,
                                                              &m_events);
    m_ecs.Systems().AddSystem("DeathSystem",
                              [this](ecs::EcsWorld& w, float dt) { m_deathSystem->Update(w, dt); });
    // 刷怪与虚拟化（M5-T7）：在死亡结算之后（倒地窗口的实体不参与虚拟化）、动画之前；
    // 实体化生成的新实体本 tick 不参与移动（Movement 已过），下一 tick 起全链生效
    m_ecs.Systems().AddSystem("SpawnLodSystem", [this](ecs::EcsWorld& w, float dt) {
        const glm::vec2 camGrid = render::FromScreen(m_camera.Center(), 0);
        m_spawnLodSystem->SetFocus(camGrid.x, camGrid.y);
        m_spawnLodSystem->Update(w, dt);
    });
    // 动画读"物理实际速度"（撞墙≈0 → 自动进站立帧），因此排在 PhysicsSync 之后
    m_ecs.Systems().AddSystem("AnimationSystem", [this](ecs::EcsWorld& w, float dt) {
        m_animationSystem->Update(w, dt);
    });

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
    m_eventBridge.reset();  // 先退订桥接（不再向脚本派发）
    m_modWatcher.reset();   // 停止轮询线程（先于脚本运行时关闭）
    if (m_soundSubscription != core::EventBus::kInvalidSubscription) {
        m_events.Unsubscribe(m_soundSubscription);  // 先于感知系统销毁
        m_soundSubscription = core::EventBus::kInvalidSubscription;
    }
    m_script.ShutdownAll();  // 脚本先于世界/状态销毁（M7 起脚本可能引用世界状态）
    m_states.Clear();
    // 渲染资源必须在 bgfx::shutdown 之前释放
    m_sprites.Shutdown();
    m_atlases.Clear();
    m_textures.Clear();
    m_renderer.Shutdown();
    m_window.Destroy();
    m_loop.reset();
    m_physicsSyncSystem.reset();
    m_animationSystem.reset();
    m_deathSystem.reset();
    m_combatSystem.reset();
    m_spawnLodSystem.reset();
    m_inputSystem.reset();
    m_movementSystem.reset();
    m_pathFollowSystem.reset();  // 先于服务
    m_boidsSystem.reset();
    m_lodSystem.reset();
    m_aiSystem.reset();
    m_perceptionSystem.reset();
    m_pathfinding.reset();       // 在途后台查询经共享 Core 安全收敛
    m_physics.reset();  // 物理体先于 chunk 生命周期回调销毁
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

        // M7-T6：消费热重载通知（轮询线程只通知；重载与脚本执行都在主线程）
        if (m_modWatcher != nullptr) {
            for (const std::string& modId : m_modWatcher->PollChanges()) {
                std::string error;
                if (!m_script.ReloadMod(modId, error)) {
                    ZL_WARN("core", "mod 热重载失败：[{}]：{}（已禁用；修复文件再保存可恢复）",
                            modId, error);
                }
            }
        }

        // 状态延迟切换生效
        m_states.ApplyPendingState();
    }
    ZL_INFO("core", "主循环退出");
}

void GameApp::Tick(double tickDt) {
    m_gameTime.Tick();
    m_events.EmitQueued(core::kOnTick, static_cast<int64_t>(m_loop->TotalTicks()));
    // ECS 系统（注册顺序即执行顺序，SystemScheduler 集中注册）；Update 内含 tick 首尾两次 flush
    m_ecs.Update(static_cast<float>(tickDt));
    if (auto* state = m_states.Current()) {
        state->OnTick(tickDt);
    }
    m_ecs.Flush();  // 状态逻辑里入队的创建/销毁也在本 tick 末统一 flush
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
