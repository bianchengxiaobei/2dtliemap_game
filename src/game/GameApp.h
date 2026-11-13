#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "ai/PathfindingService.h"
#include "ai/VirtualHorde.h"
#include "ai/systems/AiSystem.h"
#include "ai/systems/BoidsSystem.h"
#include "ai/systems/LodSystem.h"
#include "ai/systems/PathFollowSystem.h"
#include "ai/systems/PerceptionSystem.h"
#include "ai/systems/SpawnLodSystem.h"
#include "audio/SoundManager.h"
#include "characters/systems/AnimationSystem.h"
#include "characters/systems/CombatSystem.h"
#include "characters/systems/DeathSystem.h"
#include "characters/systems/InputSystem.h"
#include "characters/systems/MovementSystem.h"
#include "core/AppState.h"
#include "core/Config.h"
#include "core/EventBus.h"
#include "core/GameLoop.h"
#include "core/GameTime.h"
#include "core/Input.h"
#include "core/TaskScheduler.h"
#include "ecs/EcsWorld.h"
#include "ecs/SpatialIndex.h"
#include "game/Animals.h"
#include "game/AssetProvider.h"
#include "game/Crops.h"
#include "game/EventProbe.h"
#include "game/Fire.h"
#include "game/Forage.h"
#include "game/GameContext.h"
#include "game/Missions.h"
#include "game/Poi.h"
#include "game/Settlement.h"
#include "game/StrategicState.h"
#include "game/WorldEvent.h"
#include "game/WorldSimulation.h"
#include "items/ItemRegistry.h"
#include "items/LootTable.h"
#include "items/RecipeRegistry.h"
#include "net/CommandPipeline.h"
#include "physics/PhysicsSyncSystem.h"
#include "physics/PhysicsWorld.h"
#include "platform/Window.h"
#include "render/Atlas.h"
#include "render/Camera.h"
#include "render/Renderer.h"
#include "render/SpriteBatch.h"
#include "render/Texture2D.h"
#include "save/ComponentRegistry.h"
#include "save/SaveManager.h"
#include "script/EventBridge.h"
#include "script/ModWatcher.h"
#include "script/ScriptApi.h"
#include "script/ScriptManager.h"
#include "ui/UiSystem.h"
#include "world/ChunkManager.h"
#include "world/World.h"
#include "world/WorldSelection.h"

namespace zl::save {
class SaveService;
}

namespace zl::game {

class EmbeddedServer;
class NetworkClient;

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
    void RefreshContentHash();
    bool ReloadContentFromDisk(std::string& error);
    ui::MenuSettings BuildMenuSettings() const;
    bool ApplyMenuSettings(const ui::MenuSettings& settings, std::string& error);
    void EnsureCommandPipeline();
    bool StartClientOnly(const MultiplayerRequest& request, std::string* error,
                         bool keepOnFailure = false);
    bool StartHosting(const MultiplayerRequest& request, MultiplayerStartResult& result,
                      std::string& error);
    bool StartJoining(const MultiplayerRequest& request, MultiplayerStartResult& result,
                      std::string& error);
    void StopMultiplayer();

    core::Config m_config;
    world::WorldSelection m_worldSelection;
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
    std::unique_ptr<ui::UiSystem> m_ui;
    // 音频（M11-T1）：miniaudio 设备 + 音量组；无设备时静音运行
    std::unique_ptr<audio::SoundManager> m_audio;

    // M13-T3：无窗口模拟（世界/ECS/物理/寻路/AI/固定链）的单一所有者。
    // 渲染、UI、输入和存档只读取这里的指针；ServerHost 复用同一实现。
    std::unique_ptr<WorldSimulation> m_simulation;

    // 物品定义注册表（M6-T1）：启动时加载 assets/items.json（校验失败以空表继续）
    items::ItemRegistry m_itemRegistry;
    // 配方注册表：依赖物品定义做交叉校验（校验失败以空表继续）
    items::RecipeRegistry m_recipeRegistry;
    items::LootTableRegistry m_lootTableRegistry;
    ForageRegistry m_forageRegistry;
    AnimalRegistry m_animalRegistry;
    CropRegistry m_cropRegistry;
    FireRegistry m_fireRegistry;
    PoiConfig m_poiConfig;
    MissionConfig m_missionConfig;
    SettlementConfig m_settlements;
    WorldEventConfig m_worldEvents;
    StrategicConfig m_strategic;

    // 存档管理（M10-T4/T5）：save.root 下的元数据、原子写盘与 zl-save 后台线程
    std::unique_ptr<save::SaveManager> m_saveManager;
    std::unique_ptr<save::SaveService> m_saveService;
    save::ComponentRegistry m_saveComponents;

    // 玩家命令层（M12）：direct/loopback 可切换；状态通过 GameContext::commands 提交
    std::unique_ptr<net::CommandPipeline> m_commandPipeline;
    // M13-T5：真实 KCP 客户端（net.mode=client）；状态通过 GameContext::networkClient 访问。
    std::unique_ptr<NetworkClient> m_networkClient;
    std::unique_ptr<EmbeddedServer> m_embeddedServer;
    bool m_networkMode = false;
    bool m_hosting = false;

    // 脚本系统（M7-T1）：后端注册表 + mod 生命周期门面；后端实例由 M7-T2（Lua）注册
    script::ScriptManager m_script;
    // Host API 薄包装（M7-T3）：GameContext 挂接发生在 Init 稍后（1.6 时机均可）
    script::ScriptApi m_scriptApi;
    // 事件桥接（M7-T4）：EventBus → ScriptManager::DispatchEvent（事件表 v1 全表）
    std::unique_ptr<script::EventBridge> m_eventBridge;
    // 热重载（M7-T6）：mods mtime 轮询后台线程；变更在主循环消费
    std::unique_ptr<script::ModWatcher> m_modWatcher;

    double m_lastFrameTime = 0.0;
    bool m_shutdownDone = false;
};

}  // namespace zl::game
