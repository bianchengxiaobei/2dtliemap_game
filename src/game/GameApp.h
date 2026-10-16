#pragma once

#include <cstdint>
#include <memory>

#include "ai/PathfindingService.h"
#include "ai/VirtualHorde.h"
#include "ai/systems/AiSystem.h"
#include "ai/systems/BoidsSystem.h"
#include "ai/systems/LodSystem.h"
#include "ai/systems/PathFollowSystem.h"
#include "ai/systems/PerceptionSystem.h"
#include "ai/systems/SpawnLodSystem.h"
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
#include "game/AssetProvider.h"
#include "game/EventProbe.h"
#include "game/GameContext.h"
#include "items/ItemRegistry.h"
#include "physics/PhysicsSyncSystem.h"
#include "physics/PhysicsWorld.h"
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

    // 物理（M4-T3）：静态体随 chunk 生命周期生成/移除；系统实例与注册集中管理
    std::unique_ptr<physics::PhysicsWorld> m_physics;
    // 角色系统（M4-T3/T4）
    std::unique_ptr<characters::InputSystem> m_inputSystem;
    std::unique_ptr<characters::MovementSystem> m_movementSystem;
    std::unique_ptr<physics::PhysicsSyncSystem> m_physicsSyncSystem;
    std::unique_ptr<characters::AnimationSystem> m_animationSystem;
    // 死亡流程（M5-T1/T6）：倒地窗口 → 尸体（PhysicsSync 之后、Animation 之前）
    std::unique_ptr<characters::DeathSystem> m_deathSystem;
    // 战斗（M5-T6）：伤害管线 + 受击反应（Boids 之后、Movement 之前）
    std::unique_ptr<characters::CombatSystem> m_combatSystem;

    // 寻路（M4-T6）：passability 位图随 chunk 生命周期维护；PathFollowSystem 在固定链中
    std::unique_ptr<ai::PathfindingService> m_pathfinding;
    std::unique_ptr<ai::PathFollowSystem> m_pathFollowSystem;
    // AI（M5-T2）：僵尸状态机（Input 之后、PathFollow 之前）
    std::unique_ptr<ai::AiSystem> m_aiSystem;
    // 感知（M5-T3）：视觉/听觉 → AiSystem 之前（本 tick 即被消费）
    std::unique_ptr<ai::PerceptionSystem> m_perceptionSystem;
    // M5-T4：OnSound（事件总线）→ PerceptionSystem::PushSound 的订阅
    core::EventBus::SubscriptionId m_soundSubscription = core::EventBus::kInvalidSubscription;

    // LOD（M5-T5）：档位门控（链首，供感知/AI/寻路/群体行为判定）
    std::unique_ptr<ai::LodSystem> m_lodSystem;
    // 群体行为（M5-T5）：Boids 式 flocking（PathFollow 之后、Movement 之前）
    std::unique_ptr<ai::BoidsSystem> m_boidsSystem;
    // 虚拟化（M5-T5）：离屏僵尸的统计表示（不入 registry；T7 SpawnLodSystem 消费）
    ai::VirtualHordeStore m_virtualHordes;
    // 刷怪与虚拟化（M5-T7）：密度目标 + 实体化/虚拟化（Death 之后、Animation 之前）
    std::unique_ptr<ai::SpawnLodSystem> m_spawnLodSystem;

    // ECS 世界（M4 起）：系统注册集中在 Init（SystemScheduler 单一注册点）
    ecs::EcsWorld m_ecs;
    ecs::SpatialIndex m_spatialIndex;

    // 物品定义注册表（M6-T1）：启动时加载 assets/items.json（校验失败以空表继续）
    items::ItemRegistry m_itemRegistry;

    double m_lastFrameTime = 0.0;
    bool m_shutdownDone = false;
};

}  // namespace zl::game
