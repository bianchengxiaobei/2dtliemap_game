#pragma once

#include <functional>
#include <string>

namespace zl::core {
class AppStateMachine;
class Config;
class EventBus;
class GameLoop;
class GameTime;
class Input;
class TaskScheduler;
}  // namespace zl::core

namespace zl::ecs {
class EcsWorld;
class SpatialIndex;
}  // namespace zl::ecs

namespace zl::ai {
class AiSystem;
class BoidsSystem;
class LodSystem;
class PathfindingService;
class PerceptionSystem;
class SpawnLodSystem;
class VirtualHordeStore;
}  // namespace zl::ai

namespace zl::characters {
class CombatSystem;
}  // namespace zl::characters

namespace zl::items {
class ItemRegistry;
}  // namespace zl::items

namespace zl::save {
class ComponentRegistry;
class SaveManager;
class SaveService;
}  // namespace zl::save

namespace zl::physics {
class PhysicsWorld;
}  // namespace zl::physics

namespace zl::platform {
class Window;
}

namespace zl::ui {
class UiSystem;
}

namespace zl::render {
class AtlasManager;
class Camera;
class Renderer;
class SpriteBatch;
}  // namespace zl::render

namespace zl::world {
class ChunkManager;
class World;
}  // namespace zl::world

namespace zl::audio {
class SoundManager;
}

namespace zl::net {
class CommandPipeline;
struct Command;
}  // namespace zl::net

namespace zl::game {

class AssetProvider;

// 状态的依赖注入容器：由 GameApp 组装，状态只拿指针不拿所有权。
struct GameContext {    core::Config* config = nullptr;
    core::EventBus* events = nullptr;
    core::Input* input = nullptr;
    core::GameTime* gameTime = nullptr;
    core::TaskScheduler* tasks = nullptr;
    core::GameLoop* loop = nullptr;
    core::AppStateMachine* states = nullptr;
    platform::Window* window = nullptr;

    // 渲染（M2 起）
    render::Renderer* renderer = nullptr;
    render::Camera* camera = nullptr;
    render::SpriteBatch* sprites = nullptr;
    render::AtlasManager* atlases = nullptr;
    AssetProvider* assets = nullptr;
    ui::UiSystem* ui = nullptr;

    // 音频（M11）：设备不可用时内部 no-op；状态只发世界声音/2D 音效
    audio::SoundManager* audio = nullptr;

    // 玩家命令（M12）：世界状态只提交 Command；Direct/Loopback 由 pipeline 决定执行时机。
    net::CommandPipeline* commands = nullptr;
    std::function<bool(const net::Command&, std::string&)> executePlayerCommand;

    // 世界与流式加载（M3 起）
    world::World* world = nullptr;
    world::ChunkManager* chunkManager = nullptr;

    // ECS（M4 起）
    ecs::EcsWorld* ecs = nullptr;
    ecs::SpatialIndex* spatialIndex = nullptr;

    // 物理（M4-T3 起）
    physics::PhysicsWorld* physics = nullptr;

    // 寻路（M4-T6 起）：passability 位图缓存 + 后台查询（PathFollowSystem 的依赖）
    ai::PathfindingService* pathfinding = nullptr;

    // AI（M5-T2 起）：僵尸状态机（经固定链驱动；此处仅供 HUD 统计等只读访问）
    ai::AiSystem* aiSystem = nullptr;

    // 感知（M5-T3 起）：视觉/听觉（只读统计；逻辑经固定链）
    ai::PerceptionSystem* perception = nullptr;

    // LOD / 群体行为 / 虚拟化（M5-T5 起；只读统计 + 演示种子）
    ai::LodSystem* lod = nullptr;
    ai::BoidsSystem* boids = nullptr;
    ai::VirtualHordeStore* virtualHordes = nullptr;

    // 战斗（M5-T6 起）：伤害管线入口（演示/脚本可直调 ApplyDamage）+ HUD 统计
    characters::CombatSystem* combat = nullptr;

    // 刷怪与虚拟化（M5-T7 起）：密度目标/实体化/虚拟化（只读统计）
    ai::SpawnLodSystem* spawnLod = nullptr;

    // 物品定义（M6-T1 起）：只读查询（ItemDef 生命周期同进程）
    items::ItemRegistry* items = nullptr;

    // 存档管理（M10-T4/T5 起）：元数据 + 后台写入；组件表只在主线程生成快照时使用
    save::SaveManager* saveManager = nullptr;
    save::SaveService* saveService = nullptr;
    save::ComponentRegistry* saveComponents = nullptr;
    std::string activeSaveId;   // 当前会话存档 id（chunk 加载时叠加 diff）
    bool activeSaveUseBackup = false;  // 实体回退到 backup 时，chunk 也读 backup
    std::string saveWarning;    // 后台/回退警告，由当前状态消费成 HUD 提示
    std::function<void(bool ok, bool created, const std::string& id,
                       const std::string& error)>
        onSaveFinished;

    // GameApp 提供的动作：重载配置文件并重新应用键位（F5）
    std::function<void()> reloadConfigAndBindings;
};

}  // namespace zl::game
