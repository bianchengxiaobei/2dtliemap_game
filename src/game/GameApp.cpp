#include "game/GameApp.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <GLFW/glfw3.h>

#include "characters/components/CharacterComponents.h"
#include "core/Log.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/ContentHash.h"
#include "game/ContentReload.h"
#include "game/EmbeddedServer.h"
#include "game/ModContent.h"
#include "game/NetworkClient.h"
#include "game/Poi.h"
#include "game/StrategicDirector.h"
#include "game/WorldQueries.h"
#include "net/PlayerIdentity.h"
#include "game/ZombieFactory.h"
#include "game/states/MainMenuState.h"
#include "game/states/RenderDemoState.h"
#include "game/states/SandboxState.h"
#include "game/states/WorldDemoState.h"
#include "platform/KeyNames.h"
#include "render/IsoMath.h"
#include "save/SaveService.h"
#include "script/LuaRuntime.h"
#include "script/ModLoader.h"
#include "script/PythonRuntime.h"
#include "script/PythonSupport.h"
#include "ui/ScriptUiHost.h"
#include "world/MapLoader.h"
#include "world/WorldGraph.h"
#include "world/WorldSelection.h"

namespace zl::game {
namespace {

// 拖动窗口/断点恢复后 frameDt 会很大：钳制上限（why：防止物理/累加器爆炸）
constexpr double kMaxFrameDt = 0.25;

constexpr std::pair<const char*, const char*> kDefaultBindings[] = {
    {"Quit", "ESCAPE"},      {"Pause", "F1"},       {"CycleSpeed", "F2"},
    {"DemoTasks", "F3"},     {"ReloadBindings", "F5"}, {"ToggleCruise", "F8"},
    {"ToggleDebugOverlay", "F9"}, {"TogglePhysicsDebug", "F10"}, {"DemoKill", "F6"},
    {"DemoGunshot", "F7"},   // M5-T4：调试枪声（正式枪声走 M23 远程武器）
    {"DemoProjectile", "V"}, // M23-F1-T4：单机远程开火；联机暂保留演示投射物
    {"Attack", "SPACE"},     // M5-T6：玩家轻击（演示用自动瞄准，M6 接武器/朝向）
    {"HeavyAttack", "LSHIFT"},  // M30-T3：玩家重击（单机；联机协议 T6）
    {"Block", "LCTRL"},      // M30-T4：按住格挡（单机；联机命令 T6）
    {"Dodge", "Z"},          // M30-T4：闪避（单机；联机命令 T6）
    {"Pickup", "F"},         // M6-T3：拾取最近地面物品
    {"Use", "E"},            // M6-T5：使用消耗品（回血/饱食）
    {"FarmCare", "T"},       // F5-T2：农田施肥/杀菌
    {"FarmSeed", "Q"},       // F5-T2：切换空农田种子
    {"Equip", "R"},          // M6-T5：装备/卸下；单机主手远程武器时优先换弹
    {"Drop", "G"},           // M6-T5：丢弃背包第一堆
    {"BuildMode", "B"},      // M16-T1：进入/退出建造模式
    {"RepairStructure", "H"},// M17-T2：维修最近结构
    {"RepairAllStructures", "J"}, // M18-F4：批量维修
    {"DemolishStructure", "X"}, // M17-T2：拆除最近结构
    {"CharacterPanel", "C"}, // M19-F1：角色技能/Perk 面板
    {"WorldMap", "M"},       // M31-T7：世界地图/发现/导航
    {"MissionFocus", "L"},   // M19-F3：切换任务焦点
    {"MissionAbandon", "K"}, // M19-F3：放弃当前焦点任务
    {"Vehicle", "ENTER"},
    {"Headlights", "L"},     // M25-T4-V2：驾驶时切换车灯
    {"QuickSave", "F12"},    // M10-T5：快速存档
    {"MoveUp", "W"},         {"MoveDown", "S"},     {"MoveLeft", "A"},
    {"MoveRight", "D"},
    // M4-T4：WASD 归玩家；方向键平移相机（自由相机模式，M4-T5）
    {"PanUp", "UP"},         {"PanDown", "DOWN"},   {"PanLeft", "LEFT"},
    {"PanRight", "RIGHT"},   {"ToggleFreeCam", "F4"},
    // M9-T4：快捷栏 1~5
    {"Hotbar1", "1"},        {"Hotbar2", "2"},      {"Hotbar3", "3"},
    {"Hotbar4", "4"},        {"Hotbar5", "5"},      {"Hotbar6", "6"},
    {"Hotbar7", "7"},        {"Hotbar8", "8"},
};

double NowSeconds() {
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

int GameApp::Run(int argc, char** argv) {
    core::InitLogging();
    const auto startupBegin = std::chrono::steady_clock::now();
    int result = 1;
    try {
        GameApp app;
        if (app.Init(argc, argv)) {
            const double startupMs = std::chrono::duration<double, std::milli>(
                                         std::chrono::steady_clock::now() - startupBegin)
                                         .count();
            ZL_INFO("core", "启动耗时：{:.0f} ms（初始化完成，进入主循环）", startupMs);
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

    // M31-T10: single-player defaults to the vertical-slice map/content.
    // Keep a profile config alive for the initial content load; the base
    // config is still used for settings, saves and legacy fallback.
    m_worldSelection = world::ResolveWorldSelection(m_config, false);
    core::Config contentProfile;
    const core::Config* contentConfig = &m_config;
    if (!m_worldSelection.contentFile.empty()) {
        if (!contentProfile.LoadFile(m_worldSelection.contentFile)) {
            ZL_ERROR("world", "M31 内容配置加载失败：{}", m_worldSelection.contentFile);
            return false;
        }
        contentConfig = &contentProfile;
    }
    ZL_INFO("world", "地图选择：{}（{}；内容 {}）", m_worldSelection.mapDir,
            m_worldSelection.m31 ? "M31 垂直切片" : "旧地图",
            m_worldSelection.contentFile.empty() ? "config/game.json"
                                                 : m_worldSelection.contentFile);

    // 1.5 物品定义注册表（M6-T1）：启动加载 + 校验；失败不阻断启动（空表降级）
    const std::string itemsFile = m_config.Get<std::string>("items.file", "assets/items.json");
    if (!m_itemRegistry.LoadFile(itemsFile)) {
        ZL_WARN("items", "物品注册表为空，物品相关功能不可用（文件: {}）", itemsFile);
    }
    m_ctx.items = &m_itemRegistry;

    // 1.52 配方注册表：与物品定义交叉校验；失败只禁用制作，不阻断启动。
    const std::string recipesFile =
        m_config.Get<std::string>("recipes.file", "assets/recipes.json");
    if (!m_recipeRegistry.LoadFile(recipesFile, m_itemRegistry)) {
        ZL_WARN("items", "配方注册表为空，制作功能不可用（文件: {}）", recipesFile);
    }
    m_ctx.recipes = &m_recipeRegistry;

    ModContentResult modContent =
        LoadModContent(m_config, m_itemRegistry, m_recipeRegistry);
    m_ctx.modBuildables = std::move(modContent.buildables);
    m_ctx.modAssetManifests = std::move(modContent.assetManifests);
    m_ctx.allowDynamicContent =
        m_config.Get<std::string>("net.mode", "direct") == "direct";
    m_ctx.contentChanged = [this] { RefreshContentHash(); };

    // 1.53 战利品表（M14-T5）：与物品定义交叉校验；失败只让容器搜索不可用。
    const std::string lootFile =
        m_config.Get<std::string>("loot.file", "assets/loot.json");
    if (!m_lootTableRegistry.LoadFile(lootFile, m_itemRegistry)) {
        ZL_WARN("items", "战利品表为空，可搜刮容器不可用（文件: {}）", lootFile);
    }
    int lootMerged = 0;
    for (const std::string& modLoot : modContent.lootFiles) {
        std::string lootError;
        if (m_lootTableRegistry.MergeFile(modLoot, m_itemRegistry, &lootError)) {
            ++lootMerged;
        } else {
            ZL_WARN("items", "跳过 mod 战利品表 {}：{}", modLoot, lootError);
        }
    }
    if (!modContent.lootFiles.empty()) {
        ZL_INFO("items", "mod 战利品表合并：{}/{} 个文件，共 {} 张表", lootMerged,
                modContent.lootFiles.size(), m_lootTableRegistry.Size());
    }
    m_ctx.lootTables = &m_lootTableRegistry;

    // 1.54 觅食/钓鱼节点（M23-F2a-T1）：先加载基础数据，再按 mod 追加；
    // 数据只定义区域和产出表，节点位置在已加载 chunk 上确定性规划。
    const std::string forageFile =
        m_config.Get<std::string>("forage.file", "assets/forage.json");
    if (!m_forageRegistry.LoadFile(forageFile, m_lootTableRegistry)) {
        ZL_WARN("forage", "觅食数据为空，资源节点不可用（文件: {}）", forageFile);
    }
    int forageMerged = 0;
    for (const std::string& modForage : modContent.forageFiles) {
        std::string forageError;
        if (m_forageRegistry.MergeFile(modForage, m_lootTableRegistry, &forageError)) {
            ++forageMerged;
        } else {
            ZL_WARN("forage", "跳过 mod 觅食数据 {}：{}", modForage, forageError);
        }
    }
    if (!modContent.forageFiles.empty()) {
        ZL_INFO("forage", "mod 觅食数据合并：{}/{} 个文件，共 {} 区域 / {} 钓鱼点",
                forageMerged, modContent.forageFiles.size(),
                m_forageRegistry.ZoneCount(), m_forageRegistry.SpotCount());
    }
    m_ctx.forage = &m_forageRegistry;

    const std::string animalsFile =
        m_config.Get<std::string>("animals.file", "assets/animals.json");
    std::string animalsError;
    if (!m_animalRegistry.LoadFile(animalsFile, m_itemRegistry,
                                   m_lootTableRegistry, &animalsError)) {
        ZL_WARN("animals", "动物数据为空（文件: {}）：{}", animalsFile,
                animalsError);
    } else {
        ZL_INFO("animals", "动物物种：{} 项（文件: {}）",
                m_animalRegistry.Size(), animalsFile);
    }
    m_ctx.animals = &m_animalRegistry;

    const std::string cropsFile =
        m_config.Get<std::string>("crops.file", "assets/crops.json");
    std::string cropsError;
    if (!m_cropRegistry.LoadFile(cropsFile, m_itemRegistry, &cropsError)) {
        ZL_WARN("crops", "作物数据为空，使用 farming 配置回退（文件: {}）：{}",
                cropsFile, cropsError);
    }
    int cropsMerged = 0;
    for (const std::string& modCrop : modContent.cropFiles) {
        std::string mergeError;
        if (m_cropRegistry.MergeFile(modCrop, m_itemRegistry, &mergeError)) {
            ++cropsMerged;
        } else {
            ZL_WARN("crops", "跳过 mod 作物数据 {}：{}", modCrop, mergeError);
        }
    }
    if (m_cropRegistry.Size() > 0) {
        ZL_INFO("crops",
                "作物定义：{} 项（mod 文件 {}/{}，文件: {}）",
                m_cropRegistry.Size(), cropsMerged,
                modContent.cropFiles.size(), cropsFile);
    }
    m_ctx.crops = &m_cropRegistry;

    const std::string fireFile =
        m_config.Get<std::string>("fire.file", "assets/fire.json");
    std::string fireError;
    if (!m_fireRegistry.LoadFile(fireFile, &fireError)) {
        ZL_WARN("fire", "火焰数据为空，可燃物查询不可用（文件: {}）：{}", fireFile,
                fireError);
    } else {
        ZL_INFO("fire", "火焰材料：{} 项（文件: {}）", m_fireRegistry.Size(),
                fireFile);
    }
    m_ctx.fire = &m_fireRegistry;

    std::string poiError;
    if (!LoadPoiConfig(*contentConfig, &m_poiConfig, &poiError)) {
        ZL_ERROR("poi", "POI 配置加载失败：{}", poiError);
    } else {
        PoiMergeStats poiStats;
        if (m_poiConfig.enabled) {
            for (const std::string& modPoi : modContent.poiFiles) {
                if (!MergePoiFile(modPoi, &m_poiConfig, &poiStats, &poiError)) {
                    ZL_WARN("poi", "跳过 mod POI 文件 {}：{}", modPoi, poiError);
                }
            }
        }
        if (m_poiConfig.enabled) {
            ZL_INFO("poi", "POI 配置：{} 个区域（指引范围 {:.0f} 格，mod 文件 {}，新增 {}）",
                    m_poiConfig.defs.size(),
                    static_cast<double>(m_poiConfig.guideRange),
                    poiStats.filesLoaded, poiStats.defsAdded);
        }
    }
    m_ctx.poiConfig = &m_poiConfig;

    const nlohmann::json& configRoot = contentConfig->Raw();
    if (configRoot.contains("missions") && configRoot["missions"].is_object()) {
        m_ctx.missionSourceJson = configRoot["missions"].dump();
    }
    std::string missionError;
    if (!LoadMissionConfig(*contentConfig, &m_missionConfig, &missionError)) {
        ZL_ERROR("missions", "任务配置加载失败：{}", missionError);
    } else {
        MissionMergeStats missionStats;
        if (m_missionConfig.enabled && !modContent.missionFiles.empty()) {
            if (!MergeMissionFiles(modContent.missionFiles, *contentConfig,
                                   &m_missionConfig, &missionStats,
                                   &missionError, &m_ctx.missionSourceJson)) {
                ZL_WARN("missions", "mod 任务合并失败：{}", missionError);
            }
        }
        if (m_missionConfig.enabled) {
            ZL_INFO("missions",
                    "任务配置：{} 条（随机种子 {}，mod 文件 {}，新增 {}）",
                    m_missionConfig.defs.size(), m_missionConfig.randomSeed,
                    missionStats.filesLoaded, missionStats.defsAdded);
        }
    }
    m_ctx.missionConfig = &m_missionConfig;

    std::string settlementError;
    if (!LoadSettlementConfig(m_config, &m_settlements, &settlementError)) {
        ZL_ERROR("settlement", "聚落配置加载失败：{}", settlementError);
    } else {
        SettlementMergeStats settlementStats;
        if (m_settlements.enabled) {
            for (const std::string& modSettlement :
                 modContent.settlementFiles) {
                if (!MergeSettlementFile(modSettlement, &m_settlements,
                                         &settlementStats, &settlementError)) {
                    ZL_WARN("settlement", "跳过 mod 聚落文件 {}：{}",
                            modSettlement, settlementError);
                }
            }
            ZL_INFO("settlement",
                    "聚落配置：{} 个（mod 文件 {}，新增 {}）",
                    m_settlements.defs.size(), settlementStats.filesLoaded,
                    settlementStats.defsAdded);
        }
    }
    m_ctx.settlements = &m_settlements;

    std::string worldEventError;
    if (!LoadWorldEventConfig(m_config, &m_worldEvents, &worldEventError,
                              &m_itemRegistry)) {
        ZL_ERROR("world-events", "world event config load failed: {}",
                 worldEventError);
    } else {
        WorldEventMergeStats worldEventStats;
        if (m_worldEvents.enabled) {
            for (const std::string& modWorldEvent :
                 modContent.worldEventFiles) {
                if (!MergeWorldEventFile(modWorldEvent, &m_worldEvents,
                                         &worldEventStats, &worldEventError,
                                         &m_itemRegistry)) {
                    ZL_WARN("world-events",
                            "skipping mod world event file {}: {}",
                            modWorldEvent, worldEventError);
                }
            }
            ZL_INFO("world-events",
                    "world events: {} definitions (mod files {}, added {})",
                    m_worldEvents.defs.size(), worldEventStats.filesLoaded,
                    worldEventStats.defsAdded);
        }
    }
    m_ctx.worldEvents = &m_worldEvents;

    const std::string strategicFile =
        m_config.Get<std::string>("strategic.file", "assets/strategic.json");
    std::string strategicError;
    if (!LoadStrategicConfig(strategicFile, &m_strategic, &strategicError)) {
        ZL_WARN("strategic", "战略配置不可用（文件: {}）：{}", strategicFile,
                strategicError);
        m_strategic.enabled = false;
    } else {
        ZL_INFO("strategic",
                "战略配置：{} 个区域（tick {} 分钟，预警 {} / {} 小时）",
                m_strategic.regions.size(), m_strategic.tickMinutes,
                m_strategic.warningHours, m_strategic.remoteWarningHours);
    }

    // 内容指纹覆盖启动时合并后的全部玩法数据，必须在 loot/POI/任务之后计算。
    RefreshContentHash();

    // 1.55 存档管理（M10-T4）：根目录来自 save.root；文件在首次创建时落盘
    save::SaveManager::Options saveOptions;
    saveOptions.root = m_config.Get<std::string>("save.root", "saves");
    saveOptions.gameVersion =
        m_config.Get<std::string>("save.gameVersion", save::kDefaultGameVersion);
    m_saveManager =
        std::make_unique<save::SaveManager>(std::move(saveOptions));
    std::string saveComponentError;
    if (!save::RegisterBuiltinComponents(m_saveComponents, &saveComponentError)) {
        ZL_ERROR("save", "存档组件注册失败：{}", saveComponentError);
    }
    m_saveService = std::make_unique<save::SaveService>(*m_saveManager);

    // 1.58 音频（M11-T1）：设备不可用/被拔掉时静音运行；音量组从 config 恢复。
    {
        audio::SoundManagerOptions audioOptions;
        audio::AudioEngineOptions& engine = audioOptions.engine;
        engine.root = m_config.Get<std::string>("audio.root", "assets/audio");
        engine.device = m_config.Get<std::string>("audio.device", "default");
        engine.maxVoices = m_config.Get<int>("audio.maxVoices", 64);
        engine.masterVolume =
            static_cast<float>(m_config.Get<int>("audio.masterVolume", 65)) / 100.0f;
        engine.musicVolume =
            static_cast<float>(m_config.Get<int>("audio.musicVolume", 70)) / 100.0f;
        engine.sfxVolume =
            static_cast<float>(m_config.Get<int>("audio.sfxVolume", 85)) / 100.0f;
        engine.ambientVolume =
            static_cast<float>(m_config.Get<int>("audio.ambientVolume", 55)) / 100.0f;
        engine.referenceDistance = static_cast<float>(
            m_config.Get<double>("audio.referenceDistance", 2.0));
        engine.maxDistance =
            static_cast<float>(m_config.Get<double>("audio.maxDistance", 48.0));
        engine.wallAttenuation = static_cast<float>(
            m_config.Get<double>("perception.wallAttenuation", 0.6));
        audioOptions.growlRadius =
            static_cast<float>(m_config.Get<double>("audio.growlRadius", 24.0));
        audioOptions.growlMinInterval =
            static_cast<float>(m_config.Get<double>("audio.growlMinInterval", 1.1));
        audioOptions.growlMaxInterval =
            static_cast<float>(m_config.Get<double>("audio.growlMaxInterval", 4.2));
        audioOptions.growlVolume =
            static_cast<float>(m_config.Get<double>("audio.growlVolume", 0.9));
        audioOptions.ambienceVolume =
            static_cast<float>(m_config.Get<double>("audio.ambienceVolume", 0.55));
        m_audio = std::make_unique<audio::SoundManager>();
        m_audio->Init(m_events, audioOptions);
    }

    // 2. 窗口
    platform::Window::Desc desc;
    desc.width = m_config.Get<int>("window.width", 1280);
    desc.height = m_config.Get<int>("window.height", 720);
    desc.title = m_config.Get<std::string>("window.title", "ZomboidLike");
    desc.vsync = m_config.Get<bool>("window.vsync", true);
    desc.fullscreen = m_config.Get<bool>("window.fullscreen", false);
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

    m_ui = std::make_unique<ui::UiSystem>();
    if (!m_ui->Init(m_window, m_renderer.FramebufferWidth(), m_renderer.FramebufferHeight())) {
        ZL_WARN("core", "UI 初始化失败，以无 UI 模式继续");
        m_ui.reset();
    }
    if (m_ui) {
        m_ui->SetEventBus(&m_events);
        m_ui->SetMenuSettings(BuildMenuSettings());
        m_ui->SetMenuSettingsHandler(
            [this](const ui::MenuSettings& settings, std::string& error) {
                return ApplyMenuSettings(settings, error);
            });
        const auto clickSound = [this] {
            if (m_audio != nullptr) {
                m_audio->PlayUiClick();
            }
        };
        m_ui->SetMenuSoundHandler(clickSound);
        m_ui->SetInventorySoundHandler(clickSound);
    }

    m_window.SetResizeCallback([this](int width, int height) {
        if (width > 0 && height > 0) {
            m_renderer.Resize(width, height);
            m_camera.SetViewport(width, height);
            if (m_ui) {
                m_ui->Resize(width, height);
            }
        }
    });
    m_window.SetKeyCallback([this](int key, int action, int mods) {
        if (action == GLFW_PRESS) {
            if (key == GLFW_KEY_ENTER && (mods & GLFW_MOD_ALT) != 0) {
                m_window.ToggleFullscreen();
                return;
            }
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
    startDate.hour = std::clamp(m_config.Get<int>("game.startHour", 0), 0, 23);
    m_gameTime.Configure(gameMinutesPerRealSecond, startDate);
    ZL_INFO("core", "游戏时间：现实 {:.0f} 分钟 = 游戏 1 天；开局时间 {}",
            dayLengthMinutes, core::FormatDateTime(startDate));

    ApplyInputBindings();

    // 5. M13-T3：无窗口模拟是唯一所有者（GameApp 与 ServerHost 共用）。
    // 世界、chunk 流式加载、物理、寻路、AI 和固定系统链都在 WorldSimulation 内组装，
    // 本类只把指针挂到 GameContext 供渲染/UI/状态读取。
    {
        WorldSimulation::Options simulationOptions;
        simulationOptions.config = &m_config;
        simulationOptions.events = &m_events;
        simulationOptions.gameTime = &m_gameTime;
        simulationOptions.items = &m_itemRegistry;
        simulationOptions.lootTables = &m_lootTableRegistry;
        simulationOptions.animals = &m_animalRegistry;
        simulationOptions.pois = &m_poiConfig;
        simulationOptions.settlements = &m_settlements;
        simulationOptions.worldEvents = &m_worldEvents;
        simulationOptions.strategic = &m_strategic;
        simulationOptions.spawnAnimals = true;
        simulationOptions.fire = &m_fireRegistry;
        simulationOptions.tasks = m_tasks.get();
        simulationOptions.saveManager = m_saveManager.get();
        simulationOptions.saveService = m_saveService.get();
        simulationOptions.saveComponents = &m_saveComponents;
        simulationOptions.activeSaveId = &m_ctx.activeSaveId;
        simulationOptions.activeSaveUseBackup = &m_ctx.activeSaveUseBackup;
        simulationOptions.saveWarning = &m_ctx.saveWarning;
        simulationOptions.mapDir = m_worldSelection.mapDir;
        simulationOptions.focus = glm::vec2(70.0f, 18.0f);
        m_simulation = std::make_unique<WorldSimulation>(simulationOptions);
        std::string simulationError;
        if (!m_simulation->Init(&simulationError)) {
            ZL_ERROR("sim", "无窗口模拟初始化失败：{}", simulationError);
            return false;
        }
        m_ctx.simulation = m_simulation.get();
        m_ctx.world = &m_simulation->World();
        m_ctx.chunkManager = &m_simulation->Chunks();
        m_ctx.worldGraph = m_simulation->Graph();
        m_ctx.physics = &m_simulation->Physics();
        m_ctx.pathfinding = m_simulation->Pathfinding();
        m_ctx.ecs = &m_simulation->Ecs();
        m_ctx.spatialIndex = &m_simulation->Spatial();
        m_ctx.aiSystem = m_simulation->Ai();
        m_ctx.perception = m_simulation->Perception();
        m_ctx.lod = m_simulation->Lod();
        m_ctx.boids = m_simulation->Boids();
        m_ctx.virtualHordes = m_simulation->VirtualHordes();
        m_ctx.spawnLod = m_simulation->SpawnLod();
        m_ctx.animalSystem = m_simulation->Animals();
        m_ctx.combat = m_simulation->Combat();
    }
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
    m_ctx.ui = m_ui.get();
    m_ctx.toggleInventory = [this]() -> bool {
        if (m_ui == nullptr) {
            return false;
        }
        m_ui->ToggleInventory();
        return m_ui->IsInventoryOpen();
    };
    m_ctx.isInventoryOpen = [this]() -> bool {
        return m_ui != nullptr && m_ui->IsInventoryOpen();
    };
    m_ctx.isMenuOpen = [this]() -> bool {
        return m_ui != nullptr && m_ui->IsMenuOpen();
    };
    m_ctx.getMenuState = [this](ui::MenuState& state) -> bool {
        return m_ui != nullptr && m_ui->GetMenuState(state);
    };
    m_ctx.applyMenuSettings = [this](const ui::MenuSettings& settings,
                                     std::string& error) -> bool {
        return m_ui != nullptr && m_ui->ApplyScriptMenuSettings(settings, error);
    };
    m_ctx.menuAction = [this](ui::MenuActionType action) {
        if (m_ui != nullptr) {
            m_ui->InvokeMenuAction(action);
        }
    };
    m_ctx.getInventoryState = [this](ui::InventoryState& state) -> bool {
        return m_ui != nullptr && m_ui->GetInventoryState(state);
    };
    m_ctx.inventoryAction = [this](const ui::InventoryAction& action) -> bool {
        return m_ui != nullptr && m_ui->InvokeInventoryAction(action);
    };
    m_ctx.playUiClick = [this] {
        if (m_ui != nullptr) {
            m_ui->PlayUiClick();
        }
    };
    m_ctx.playInventoryClick = [this] {
        if (m_ui != nullptr) {
            m_ui->PlayInventoryClick();
        }
    };
    m_ctx.audio = m_audio.get();
    m_ctx.saveManager = m_saveManager.get();
    m_ctx.saveService = m_saveService.get();
    m_ctx.saveComponents = &m_saveComponents;
    m_ctx.listSaves = [this](std::vector<save::SaveMetadata>& out,
                             std::string& error) -> bool {
        out.clear();
        return m_saveManager != nullptr && m_saveManager->List(&out, &error);
    };
    m_ctx.loadSave = [this](const std::string& id, std::string& error) -> bool {
        if (m_saveManager == nullptr || id.empty()) {
            error = "存档不存在";
            return false;
        }
        if (!m_saveManager->Exists(id)) {
            error = "存档不存在：" + id;
            return false;
        }
        if (m_saveService != nullptr && m_saveService->Busy()) {
            error = "存档写入中，请稍后再试";
            return false;
        }
        if (m_states.CurrentName() != std::string("MainMenu")) {
            error = "请在主菜单读取存档";
            return false;
        }
        m_states.SetState(std::make_unique<WorldDemoState>(m_ctx, id));
        return true;
    };
    m_ctx.deleteSave = [this](const std::string& id, std::string& error) -> bool {
        if (m_saveManager == nullptr || id.empty()) {
            error = "存档不存在";
            return false;
        }
        if (m_saveService != nullptr && m_saveService->Busy()) {
            error = "存档写入中，请稍后再试";
            return false;
        }
        if (m_states.CurrentName() != std::string("MainMenu")) {
            error = "请在主菜单删除存档";
            return false;
        }
        return m_saveManager->Delete(id, &error);
    };
    m_ctx.reloadConfigAndBindings = [this] { ReloadConfigAndBindings(); };

    // 6.1 脚本系统：UI Host 已就绪，脚本可以绑定 UI API。
    if (m_config.Get<bool>("script.enabled", true)) {
        m_scriptApi.Attach(&m_ctx);
        ui::ScriptUiHost* uiHost = m_ui != nullptr ? m_ui->ScriptHost() : nullptr;
        auto luaRuntime = std::make_unique<script::LuaRuntime>();
        luaRuntime->SetApi(&m_scriptApi);
        luaRuntime->SetUiHost(uiHost);
        m_script.RegisterRuntime(std::move(luaRuntime));
#ifdef ZL_ENABLE_PYTHON
        auto pythonRuntime = std::make_unique<script::PythonRuntime>();
        pythonRuntime->SetApi(&m_scriptApi);
        pythonRuntime->SetUiHost(uiHost);
        m_script.RegisterRuntime(std::move(pythonRuntime));
#endif
        m_script.InitializeAll();
        // M7-T4：事件桥接（EventBus → 脚本 Events.On）；manager 借此在重载后发 OnModReload
        m_script.SetEventBus(&m_events);
        if (uiHost != nullptr) {
            uiHost->SetEventBus(&m_events);
        }
        m_script.SetModDisabledHandler([this](const std::string& modId) {
            m_scriptApi.RemoveHudFields(modId);
            if (m_ui != nullptr && m_ui->ScriptHost() != nullptr) {
                m_ui->ScriptHost()->ReleaseOwner(modId);
            }
        });
        m_eventBridge = std::make_unique<script::EventBridge>(m_events, m_script);
        // M7-T5：扫描并加载 mods/（目录不存在则跳过；单个 mod 失败不阻断其它）
        script::ModLoader loader(m_script, m_config.Get<std::string>("script.backend", "lua"));
        loader.ScanAndLoad(m_config.Get<std::string>("script.modsDir", "mods"));
        if (m_ctx.dynamicContentCount > 0) {
            RefreshContentHash();
            ZL_INFO("script",
                    "动态内容 {} 项：仅单机可用；联机连接/主机启动将拒绝",
                    m_ctx.dynamicContentCount);
        }
        ZL_DEBUG("script", "Python 后端：{}", script::PythonBackendVersion());
        ZL_INFO("script", "脚本系统就绪（后端 {} 个，API v{}）", m_script.RuntimeCount(),
                script::ScriptApi::kApiVersion);
    } else {
        ZL_INFO("script", "脚本系统已禁用（script.enabled=false）");
    }

    // M7-T6/X3-T1：脚本、content 与 assets 变更监控（后台轮询，主线程消费）
    if (m_config.Get<bool>("script.hotReload", true) &&
        !modContent.sources.empty()) {
        std::vector<script::ModWatcher::Entry> watched;
        watched.reserve(modContent.sources.size());
        for (const ModContentSource& source : modContent.sources) {
            watched.push_back(
                script::ModWatcher::Entry{source.id, source.root});
        }
        const double interval =
            m_config.Get<double>("script.hotReloadSeconds", 1.0);
        const double debounce = m_config.Get<double>(
            "script.contentHotReloadDebounceSeconds", 0.5);
        m_modWatcher = std::make_unique<script::ModWatcher>(
            std::move(watched), interval, debounce);
        ZL_INFO("script",
                "mod 文件监控已启动：{} 个 mod（轮询 {:.1f}s，内容防抖 {:.1f}s）",
                m_modWatcher->WatchedCount(), interval, debounce);
    }

    // 6.5 玩家命令层（M12/T5）：client=真实 KCP；direct/loopback=本地命令通路。
    EnsureCommandPipeline();
    const std::string netMode = m_config.Get<std::string>("net.mode", "direct");
    if (netMode == "client") {
        MultiplayerRequest request;
        request.host = m_config.Get<std::string>("net.host", "127.0.0.1");
        const int port = m_config.Get<int>("net.port", 7777);
        request.port = static_cast<std::uint16_t>(std::clamp(port, 1, 65535));
        request.playerName = m_config.Get<std::string>("net.playerName", "Player");
        std::string connectError;
        if (!StartClientOnly(request, &connectError, true)) {
            ZL_WARN("net", "首次连接失败，将自动重试：{}", connectError);
        }
        m_ctx.commands = nullptr;
        ZL_INFO("net", "玩家命令层：模式=client（服务器 {}:{}，身份 {}）", request.host,
                request.port, m_networkClient != nullptr
                                   ? m_networkClient->PlayerIdentity()
                                   : std::string());
    } else {
        m_ctx.commands = m_commandPipeline.get();
    }
    m_ctx.startHosting =
        [this](const MultiplayerRequest& request, MultiplayerStartResult& result,
               std::string& error) { return StartHosting(request, result, error); };
    m_ctx.startJoining =
        [this](const MultiplayerRequest& request, MultiplayerStartResult& result,
               std::string& error) { return StartJoining(request, result, error); };
    m_ctx.leaveMultiplayer = [this] { StopMultiplayer(); };

    // 系统固定链由 WorldSimulation::Init 统一注册（顺序见 docs/00 §6.1）。
    m_probe.Attach(m_events);

    // 7. 初始状态（M9-T6：默认 Boot → MainMenu；调试可用 --game.startState=world）
    const std::string startState = m_config.Get<std::string>("game.startState", "menu");
    if (startState == "sandbox") {
        m_states.SetState(std::make_unique<SandboxState>(m_ctx));
    } else if (startState == "render") {
        m_states.SetState(std::make_unique<RenderDemoState>(m_ctx));
    } else if (startState == "world") {
        m_states.SetState(std::make_unique<WorldDemoState>(m_ctx));
    } else if (startState == "menu") {
        m_states.SetState(std::make_unique<MainMenuState>(m_ctx));
    } else {
        ZL_WARN("core", "未知 game.startState='{}'，回退到主菜单", startState);
        m_states.SetState(std::make_unique<MainMenuState>(m_ctx));
    }
    m_states.ApplyPendingState();
    if (m_ui != nullptr) {
        m_events.Emit(core::kOnUiReady);
    }
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
    m_script.ShutdownAll();  // 脚本先于世界/状态销毁（M7 起脚本可能引用世界状态）
    m_states.Clear();
    StopMultiplayer();
    m_commandPipeline.reset();
    m_ctx.networkClient = nullptr;
    if (m_audio != nullptr) {
        m_audio->Shutdown();
        m_audio.reset();
    }
    // 状态 OnExit 可能提交退出自动存档；先等待 zl-save 收尾再销毁 SaveManager
    if (m_saveService != nullptr) {
        m_saveService->WaitIdle();
        m_saveService.reset();
    }
    // 模拟先于 SaveManager 销毁；WorldSimulation 内部会退订事件并释放物理/chunk。
    m_simulation.reset();
    m_saveManager.reset();
    // 渲染资源必须在 bgfx::shutdown 之前释放
    if (m_ui) {
        m_ui->Shutdown();
        m_ui.reset();
    }
    m_sprites.Shutdown();
    m_atlases.Clear();
    m_textures.Clear();
    m_renderer.Shutdown();
    m_window.Destroy();
    m_loop.reset();
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

        if (m_ui) {
            m_ui->Update(m_input);
        }

        m_loop->SetTimeScale(m_gameTime.SpeedScale());
        m_loop->Frame(frameDt, [this](double tickDt) { Tick(tickDt); },
                      [this](double dt, double alpha) { RenderFrame(dt, alpha); });
        // 帧末发送本帧累积的玩家命令（loopback）；下一 tick 开头由服务器消费。
        if (m_commandPipeline != nullptr) {
            m_commandPipeline->Flush();
        }

        // 主线程消费后台结果（chunk 加载完成、存档快照等）
        m_tasks->ProcessResults();
        if (m_saveService != nullptr) {
            for (const save::SaveResult& result : m_saveService->PollResults()) {
                if (m_ctx.onSaveFinished) {
                    m_ctx.onSaveFinished(result.ok, result.created, result.id,
                                         result.error);
                } else if (!result.ok) {
                    ZL_ERROR("save", "后台存档失败 '{}'：{}", result.id, result.error);
                }
            }
        }

        // M7-T6/X3-T1：轮询线程只通知；脚本重载与内容消费都在主线程
        if (m_modWatcher != nullptr) {
            bool contentChanged = false;
            bool assetsChanged = false;
            for (const script::ModWatcher::ContentChange& change :
                 m_modWatcher->PollContentChanges()) {
                ZL_INFO("content",
                        "检测到 mod 内容文件变更：[{}] content={} assets={}",
                        change.modId, change.content ? "是" : "否",
                        change.assets ? "是" : "否");
                contentChanged = contentChanged || change.content;
                assetsChanged = assetsChanged || change.assets;
            }
            if (assetsChanged) {
                ZL_INFO("content", "资源文件变更本次只通知；资源热替换仍待后续任务");
            }
            if (contentChanged) {
                const std::string blocked = ContentReloadBlockedReason(
                    m_ctx.allowDynamicContent, m_networkMode,
                    m_ctx.dynamicContentCount);
                if (!blocked.empty()) {
                    ZL_WARN("content", "mod 内容变更未应用：{}", blocked);
                    if (m_states.CurrentName() == std::string("WorldDemo")) {
                        m_ctx.contentNotice = blocked;
                    }
                } else {
                    std::string reloadError;
                    if (!ReloadContentFromDisk(reloadError)) {
                        ZL_WARN("content",
                                "mod 内容热重载失败，保留旧内容：{}", reloadError);
                    }
                }
            }
            for (const std::string& modId : m_modWatcher->PollChanges()) {
                if (!m_script.IsModLoaded(modId)) {
                    continue;
                }
                if (m_ui != nullptr && m_ui->ScriptHost() != nullptr) {
                    m_ui->ScriptHost()->UnloadOwner(modId);
                }
                std::string error;
                if (!m_script.ReloadMod(modId, error)) {
                    ZL_WARN("core", "mod 热重载失败：[{}]：{}（已禁用；修复文件再保存可恢复）",
                            modId, error);
                } else {
                    // OnModReload 是排队事件；主菜单/暂停时 timeScale=0 不会有 tick，
                    // 这里立即泵一次，让脚本能重建刚被卸载的 UI。
                    m_events.FlushQueued();
                }
            }
        }

        // 状态延迟切换生效；切换后丢弃旧状态的未消费命令/快照
        core::AppState* stateBefore = m_states.Current();
        m_states.ApplyPendingState();
        if (m_states.Current() != stateBefore && m_commandPipeline != nullptr) {
            m_commandPipeline->Clear();
        }
    }
    ZL_INFO("core", "主循环退出");
}

void GameApp::Tick(double tickDt) {
    if (m_commandPipeline != nullptr) {
        m_commandPipeline->Pump();
    }
    if (m_networkClient != nullptr) {
        m_networkClient->Poll();
    }
    m_gameTime.Tick();
    m_events.EmitQueued(core::kOnTick, static_cast<int64_t>(m_loop->TotalTicks()));
    if (m_simulation != nullptr) {
        // 单机焦点=相机中心；服务器焦点=玩家。LOD/刷怪只读该值，不依赖渲染。
        const glm::vec2 focus = render::FromScreen(m_camera.Center(), 0);
        m_simulation->SetFocus(focus.x, focus.y);
        // 网络客户端只流式加载地图和渲染副本；AI/物理/战斗全部由服务器权威推进。
        if (!m_networkMode) {
            m_simulation->Tick(static_cast<float>(tickDt));
            StrategicTickInput strategic;
            strategic.gameMinute =
                static_cast<std::int64_t>(m_gameTime.TotalGameMinutes());
            strategic.anyPlayerOnline = true;
            strategic.detailedEnabled = true;
            std::string playerRegion;
            if (m_simulation->Graph() != nullptr &&
                m_simulation->Player() != entt::null) {
                const auto* position =
                    m_simulation->Ecs().Registry().try_get<ecs::Position>(
                        m_simulation->Player());
                if (position != nullptr) {
                    if (const world::WorldRegion* region =
                            m_simulation->Graph()->RegionAt(position->x,
                                                            position->y);
                        region != nullptr) {
                        playerRegion = region->id;
                        strategic.playerRegions.push_back(playerRegion);
                    }
                }
            }
            std::string strategicError;
            if (!m_simulation->TickStrategic(strategic, nullptr,
                                             &strategicError)) {
                ZL_WARN("strategic", "战略 tick 失败：{}", strategicError);
            }
            m_simulation->UpdateRegionalSiege(
                static_cast<float>(tickDt),
                m_gameTime.GetSpeed() == core::GameTime::Speed::Paused,
                playerRegion);
        }
    }
    if (auto* state = m_states.Current()) {
        state->OnTick(tickDt);
    }
    if (m_simulation != nullptr) {
        m_simulation->Flush();  // 状态逻辑里入队的创建/销毁也在本 tick 末统一 flush
    }
    m_events.FlushQueued();
}

void GameApp::RenderFrame(double frameDt, double alpha) {
    m_sprites.FrameBegin();
    m_renderer.BeginFrame();
    if (auto* state = m_states.Current()) {
        state->OnFrame(frameDt, alpha);
    }
    if (m_audio != nullptr) {
        // M11-T2：听者 = 玩家相机中心；暂停菜单打开时不播放僵尸低吼。
        const glm::vec2 listener = render::FromScreen(m_camera.Center(), 0);
        const bool gameplay = m_states.Current() != nullptr &&
                              (m_ui == nullptr || !m_ui->IsMenuOpen()) &&
                              m_gameTime.SpeedScale() > 0.0;
        m_audio->Update(static_cast<float>(frameDt), m_ctx.ecs, listener, m_gameTime.IsNight(),
                        gameplay);
    }
    if (m_ui) {
        // UI 刷新绑定渲染帧而非模拟 tick：F1/菜单暂停时 HUD 仍能反映最新状态。
        m_events.Emit(core::kOnUiFrame);
        m_ui->Render();
    }
    m_renderer.EndFrame();
}

ui::MenuSettings GameApp::BuildMenuSettings() const {
    ui::MenuSettings settings;
    settings.volume = std::clamp(m_config.Get<int>("audio.masterVolume", 65), 0, 100);
    settings.musicVolume = std::clamp(m_config.Get<int>("audio.musicVolume", 70), 0, 100);
    settings.sfxVolume = std::clamp(m_config.Get<int>("audio.sfxVolume", 85), 0, 100);
    settings.ambientVolume =
        std::clamp(m_config.Get<int>("audio.ambientVolume", 55), 0, 100);
    settings.bindings.reserve(sizeof(ui::kMenuBindingDefs) / sizeof(ui::kMenuBindingDefs[0]));
    for (const ui::MenuKeyBindingDef& def : ui::kMenuBindingDefs) {
        ui::MenuKeyBinding binding;
        binding.action = def.action;
        binding.label = def.label;
        binding.key = m_config.Get<std::string>(std::string("input.bindings.") + def.action,
                                                def.defaultKey);
        settings.bindings.push_back(std::move(binding));
    }
    return settings;
}

bool GameApp::ApplyMenuSettings(const ui::MenuSettings& settings, std::string& error) {
    if (settings.volume < 0 || settings.volume > 100 || settings.musicVolume < 0 ||
        settings.musicVolume > 100 || settings.sfxVolume < 0 || settings.sfxVolume > 100 ||
        settings.ambientVolume < 0 || settings.ambientVolume > 100) {
        error = "音量必须在 0–100 之间";
        return false;
    }

    std::unordered_set<std::string> usedKeys;
    std::vector<std::pair<std::string, std::string>> resolved;
    resolved.reserve(sizeof(ui::kMenuBindingDefs) / sizeof(ui::kMenuBindingDefs[0]));
    for (const ui::MenuKeyBindingDef& def : ui::kMenuBindingDefs) {
        const ui::MenuKeyBinding* binding = nullptr;
        for (const ui::MenuKeyBinding& candidate : settings.bindings) {
            if (candidate.action == def.action) {
                binding = &candidate;
                break;
            }
        }
        if (binding == nullptr) {
            error = std::string("设置缺少键位项：") + def.label;
            return false;
        }
        const int code = platform::KeyCodeFromName(binding->key);
        if (code < 0) {
            error = std::string("键位无效：") + def.label + " = " + binding->key;
            return false;
        }
        const std::string canonical = platform::KeyNameFromCode(code);
        if (canonical.empty()) {
            error = std::string("键位无效：") + def.label;
            return false;
        }
        if (!usedKeys.insert(canonical).second) {
            error = std::string("键位重复：") + canonical + "（" + def.label + "）";
            return false;
        }
        resolved.emplace_back(def.action, canonical);
    }

    for (const auto& [action, key] : resolved) {
        m_config.Set(std::string("input.bindings.") + action, key);
    }
    m_config.Set("audio.masterVolume", settings.volume);
    m_config.Set("audio.musicVolume", settings.musicVolume);
    m_config.Set("audio.sfxVolume", settings.sfxVolume);
    m_config.Set("audio.ambientVolume", settings.ambientVolume);
    ApplyInputBindings();
    if (m_audio != nullptr) {
        m_audio->Engine().SetGroupVolume(audio::AudioGroup::Master,
                                         static_cast<float>(settings.volume) / 100.0f);
        m_audio->Engine().SetGroupVolume(audio::AudioGroup::Music,
                                         static_cast<float>(settings.musicVolume) / 100.0f);
        m_audio->Engine().SetGroupVolume(audio::AudioGroup::SFX,
                                         static_cast<float>(settings.sfxVolume) / 100.0f);
        m_audio->Engine().SetGroupVolume(audio::AudioGroup::Ambient,
                                         static_cast<float>(settings.ambientVolume) / 100.0f);
    }

    std::string saveError;
    if (!m_config.Save(&saveError)) {
        error = "设置已在本次运行生效，但保存失败：" + saveError;
        return false;
    }
    ZL_INFO("ui",
            "菜单设置已保存：音量组 {} / {} / {} / {}，键位配置写入 {}",
            settings.volume, settings.musicVolume, settings.sfxVolume, settings.ambientVolume,
            m_config.Path().string());
    error.clear();
    return true;
}

void GameApp::EnsureCommandPipeline() {
    if (m_commandPipeline != nullptr) {
        return;
    }
    const std::string netMode = m_config.Get<std::string>("net.mode", "direct");
    net::CommandPipeline::Options commandOptions;
    if (netMode == "loopback") {
        commandOptions.mode = net::TransportMode::Loopback;
    } else if (netMode != "direct" && netMode != "client") {
        ZL_WARN("net", "未知 net.mode='{}'，回退 direct", netMode);
    }
    commandOptions.maxCommandsPerPacket = static_cast<std::size_t>(
        std::max(1, m_config.Get<int>("net.maxCommandsPerPacket", 32)));
    commandOptions.maxPacketsPerPump = static_cast<std::size_t>(
        std::max(1, m_config.Get<int>("net.maxPacketsPerPump", 64)));
    commandOptions.maxQueuedPackets = static_cast<std::size_t>(
        std::max(1, m_config.Get<int>("net.maxQueuedPackets", 256)));
    m_commandPipeline = std::make_unique<net::CommandPipeline>(commandOptions);
    m_commandPipeline->SetHandler(
        [this](const net::Command& command, std::string& error) {
            if (!m_ctx.executePlayerCommand) {
                error = "当前状态不接收玩家命令";
                return false;
            }
            return m_ctx.executePlayerCommand(command, error);
        });
    m_commandPipeline->SetSnapshotProvider([this] {
        net::ServerSnapshot snapshot;
        snapshot.serverTick = m_loop != nullptr ? m_loop->TotalTicks() : 0;
        if (m_ctx.ecs != nullptr) {
            m_ctx.ecs->Registry().view<ecs::PlayerTag, ecs::Position>().each(
                [&](const ecs::Entity entity, const ecs::Position& position) {
                    if (snapshot.playerStableId == 0) {
                        snapshot.playerStableId = m_ctx.ecs->IdOf(entity).id;
                        snapshot.x = position.x;
                        snapshot.y = position.y;
                        snapshot.z = position.z;
                    }
                });
        }
        return snapshot;
    });
    ZL_INFO("net", "本地玩家命令层：模式={}（单包上限 {} 条，队列上限 {} 包）",
            net::TransportModeName(commandOptions.mode),
            commandOptions.maxCommandsPerPacket, commandOptions.maxQueuedPackets);
}

bool GameApp::StartClientOnly(const MultiplayerRequest& request, std::string* error,
                              bool keepOnFailure) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_ctx.dynamicContentCount > 0) {
        ZL_WARN("net", "联机启动被拒绝：{}", kDynamicContentSinglePlayerOnlyMessage);
        return fail(kDynamicContentSinglePlayerOnlyMessage);
    }
    if (request.playerName.empty() || request.playerName.size() > 48) {
        return fail("玩家名称需为 1~48 字节");
    }
    if (request.host.empty()) {
        return fail("服务器地址为空");
    }
    if (request.port == 0) {
        return fail("服务器端口非法");
    }

    NetworkClient::Options options;
    options.host = request.host;
    options.port = request.port;
    const std::int64_t configuredContentHash =
        m_config.Get<std::int64_t>("net.contentHash", 0);
    options.contentHash = configuredContentHash != 0
                              ? static_cast<std::uint64_t>(configuredContentHash)
                              : m_ctx.contentHash;
    options.playerIdentity = m_config.Get<std::string>("net.playerIdentity", "");
    if (!net::IsValidPlayerIdentity(options.playerIdentity)) {
        const std::string identityFile =
            m_config.Get<std::string>("net.identityFile", "saves/client_identity.txt");
        std::string identityError;
        options.playerIdentity =
            net::LoadOrCreatePlayerIdentity(identityFile, &identityError);
        if (!identityError.empty()) {
            ZL_WARN("net", "{}（本次使用临时身份）", identityError);
        }
    }
    options.playerName = request.playerName;
    options.autoReconnect = m_config.Get<bool>("net.autoReconnect", true);
    options.reconnectDelayMs = static_cast<std::uint32_t>(
        std::max(0, m_config.Get<int>("net.reconnectDelayMs", 1000)));
    options.handshakeTimeoutMs = static_cast<std::uint32_t>(
        std::max(0, m_config.Get<int>("net.handshakeTimeoutMs", 5000)));
    options.heartbeatIntervalMs = static_cast<std::uint32_t>(
        std::max(0, m_config.Get<int>("net.heartbeatIntervalMs", 5000)));
    options.heartbeatTimeoutMs = static_cast<std::uint32_t>(
        std::max(0, m_config.Get<int>("net.heartbeatTimeoutMs", 15000)));
    options.localPredictionSpeed =
        static_cast<float>(m_config.Get<double>("net.predictionSpeed", 4.0));

    auto client = std::make_unique<NetworkClient>(options);
    std::string connectError;
    const bool connected = client->Connect(&connectError);
    if (!connected && !keepOnFailure) {
        return fail(connectError.empty() ? "连接服务器失败" : connectError);
    }
    if (!connected) {
        ZL_WARN("net", "首次连接失败，将自动重试：{}", connectError);
    }
    m_networkClient = std::move(client);
    m_networkMode = true;
    m_ctx.networkClient = m_networkClient.get();
    m_ctx.commands = nullptr;
    m_ctx.allowDynamicContent = false;
    ZL_INFO("net", "联机客户端就绪：服务器 {}:{}（身份 {}，名称 {}，contentHash={:#x}）",
            options.host, options.port, options.playerIdentity, options.playerName,
            options.contentHash);
    return true;
}

bool GameApp::StartJoining(const MultiplayerRequest& request,
                           MultiplayerStartResult& result, std::string& error) {
    StopMultiplayer();
    if (!StartClientOnly(request, &error, false)) {
        return false;
    }
    result.serverPort = request.port;
    return true;
}

bool GameApp::StartHosting(const MultiplayerRequest& request,
                           MultiplayerStartResult& result, std::string& error) {
    StopMultiplayer();
    if (m_ctx.dynamicContentCount > 0) {
        ZL_WARN("net", "联机启动被拒绝：{}", kDynamicContentSinglePlayerOnlyMessage);
        error = kDynamicContentSinglePlayerOnlyMessage;
        return false;
    }
    if (request.playerName.empty() || request.playerName.size() > 48) {
        error = "玩家名称需为 1~48 字节";
        return false;
    }
    EmbeddedServer::Options options;
    options.configPath = m_config.Path().string();
    options.bindHost = "0.0.0.0";
    options.port = request.port;
    options.contentHash = static_cast<std::uint64_t>(
        m_config.Get<std::int64_t>("net.contentHash", 0));
    options.mapDir = m_worldSelection.mapDir;
    options.contentFile = m_worldSelection.contentFile;
    options.spawnDemoPopulation = true;
    options.playerProfilesFile =
        m_config.Get<std::string>("net.playerProfilesFile",
                                  "saves/multiplayer_players.bin");
    options.tickRate = static_cast<float>(
        m_config.Get<double>("server.tickRate", 60.0));
    m_embeddedServer = std::make_unique<EmbeddedServer>();
    std::string hostError;
    if (!m_embeddedServer->Start(options, &hostError)) {
        m_embeddedServer.reset();
        error = hostError.empty() ? "启动主机失败" : hostError;
        return false;
    }
    result.serverPort = m_embeddedServer->Port();
    MultiplayerRequest local = request;
    local.host = "127.0.0.1";
    local.port = result.serverPort;
    if (!StartClientOnly(local, &error, false)) {
        m_embeddedServer->Stop();
        m_embeddedServer.reset();
        return false;
    }
    m_hosting = true;
    ZL_INFO("net", "已开始主机模式：UDP 端口 {}，本机客户端已连接", result.serverPort);
    return true;
}

void GameApp::StopMultiplayer() {
    if (m_networkClient != nullptr) {
        m_networkClient->Close();
        m_networkClient.reset();
    }
    m_ctx.networkClient = nullptr;
    m_networkMode = false;
    m_hosting = false;
    if (m_embeddedServer != nullptr) {
        m_embeddedServer->Stop();
        m_embeddedServer.reset();
    }
    if (m_commandPipeline != nullptr) {
        m_ctx.commands = m_commandPipeline.get();
    } else {
        m_ctx.commands = nullptr;
    }
    m_ctx.allowDynamicContent =
        m_config.Get<std::string>("net.mode", "direct") == "direct";
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
    ZL_INFO("input", "战斗键位：{} 轻击 / {} 重击 / {} 格挡 / {} 闪避",
            platform::KeyNameFromCode(m_input.KeyCodeFor("Attack")),
            platform::KeyNameFromCode(m_input.KeyCodeFor("HeavyAttack")),
            platform::KeyNameFromCode(m_input.KeyCodeFor("Block")),
            platform::KeyNameFromCode(m_input.KeyCodeFor("Dodge")));
}

void GameApp::ReloadConfigAndBindings() {
    if (m_config.Reload()) {
        ApplyInputBindings();
        if (m_audio != nullptr) {
            m_audio->Engine().SetGroupVolume(
                audio::AudioGroup::Master,
                static_cast<float>(m_config.Get<int>("audio.masterVolume", 65)) / 100.0f);
            m_audio->Engine().SetGroupVolume(
                audio::AudioGroup::Music,
                static_cast<float>(m_config.Get<int>("audio.musicVolume", 70)) / 100.0f);
            m_audio->Engine().SetGroupVolume(
                audio::AudioGroup::SFX,
                static_cast<float>(m_config.Get<int>("audio.sfxVolume", 85)) / 100.0f);
            m_audio->Engine().SetGroupVolume(
                audio::AudioGroup::Ambient,
                static_cast<float>(m_config.Get<int>("audio.ambientVolume", 55)) / 100.0f);
        }
        ZL_INFO("input", "键位/音量已按新配置重建（窗口尺寸等其余项需重启生效）");
    }
}

bool GameApp::ReloadContentFromDisk(std::string& error) {
    error = ContentReloadBlockedReason(m_ctx.allowDynamicContent, m_networkMode,
                                       m_ctx.dynamicContentCount);
    if (!error.empty()) {
        return false;
    }

    core::Config contentProfile;
    const core::Config* contentConfig = &m_config;
    if (!m_worldSelection.contentFile.empty()) {
        if (!contentProfile.LoadFile(m_worldSelection.contentFile)) {
            error = "M31 内容配置加载失败：" + m_worldSelection.contentFile;
            return false;
        }
        contentConfig = &contentProfile;
    }
    ContentBundleResult loaded = LoadContentBundle(m_config, contentConfig);
    if (!loaded.Ok()) {
        error = std::move(loaded.error);
        return false;
    }
    ContentBundle& bundle = *loaded.bundle;
    const std::size_t oldItems = m_itemRegistry.Size();
    const std::size_t oldRecipes = m_recipeRegistry.Size();
    const std::size_t oldBuildables = m_ctx.modBuildables.size();
    const std::uint64_t oldContentHash = m_ctx.contentHash;

    m_itemRegistry = std::move(bundle.items);
    m_recipeRegistry = std::move(bundle.recipes);
    m_lootTableRegistry = std::move(bundle.lootTables);
    m_forageRegistry = std::move(bundle.forage);
    m_animalRegistry = std::move(bundle.animals);
    m_cropRegistry = std::move(bundle.crops);
    m_poiConfig = std::move(bundle.poiConfig);
    m_missionConfig = std::move(bundle.missionConfig);
    m_settlements = std::move(bundle.settlements);
    m_worldEvents = std::move(bundle.worldEvents);
    m_ctx.missionSourceJson = std::move(bundle.missionSourceJson);
    m_ctx.modBuildables = std::move(bundle.modContent.buildables);
    ++m_ctx.contentReloadCount;

    if (m_ctx.contentChanged) {
        m_ctx.contentChanged();
    } else {
        RefreshContentHash();
    }

    ZL_INFO("content",
            "mod 内容热重载成功：物品 {}→{}，配方 {}→{}，建造 mod {}→{}，"
            "战利品 {}，POI {}，任务 {}，世界事件 {}",
            oldItems, m_itemRegistry.Size(), oldRecipes, m_recipeRegistry.Size(),
            oldBuildables, m_ctx.modBuildables.size(), m_lootTableRegistry.Size(),
            m_poiConfig.defs.size(), m_missionConfig.defs.size(),
            m_worldEvents.defs.size());
    ZL_INFO("content", "mod 内容指纹刷新：{:#x}→{:#x}", oldContentHash,
            m_ctx.contentHash);
    return true;
}

void GameApp::RefreshContentHash() {
    std::vector<BuildableDef> buildables = LoadBuildableDefs(m_config);
    AppendBuildableDefs(buildables, m_ctx.modBuildables);
    const std::string modsDir =
        m_config.Get<std::string>("script.modsDir", "mods");
    const std::uint64_t modsHash = ComputeTreeHash(modsDir);
    m_ctx.contentHash =
        ComputeContentHash(m_itemRegistry, m_recipeRegistry, buildables,
                           m_lootTableRegistry, m_poiConfig, m_missionConfig,
                           modsHash, &m_forageRegistry, &m_animalRegistry,
                           &m_cropRegistry, &m_settlements, &m_worldEvents);
    ZL_INFO("mod-content",
            "客户端内容指纹刷新：{:#x}（物品 {}，配方 {}，建造 {}，战利品 {}，"
            "POI {}，任务 {}，动物 {}，作物 {}，聚落 {}，世界事件 {}，mod 文件树 {:#x}）",
            m_ctx.contentHash, m_itemRegistry.Size(), m_recipeRegistry.Size(),
            buildables.size(), m_lootTableRegistry.Size(), m_poiConfig.defs.size(),
            m_missionConfig.defs.size(), m_animalRegistry.Size(),
            m_cropRegistry.Size(), m_settlements.defs.size(),
            m_worldEvents.defs.size(), modsHash);
}

}  // namespace zl::game
