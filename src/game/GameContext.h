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

namespace zl::physics {
class PhysicsWorld;
}  // namespace zl::physics

namespace zl::platform {
class Window;
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

namespace zl::game {

class AssetProvider;

// 状态的依赖注入容器：由 GameApp 组装，状态只拿指针不拿所有权。
struct GameContext {
    core::Config* config = nullptr;
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

    // 世界与流式加载（M3 起）
    world::World* world = nullptr;
    world::ChunkManager* chunkManager = nullptr;

    // ECS（M4 起）
    ecs::EcsWorld* ecs = nullptr;
    ecs::SpatialIndex* spatialIndex = nullptr;

    // 物理（M4-T3 起）
    physics::PhysicsWorld* physics = nullptr;

    // GameApp 提供的动作：重载配置文件并重新应用键位（F5）
    std::function<void()> reloadConfigAndBindings;
};

}  // namespace zl::game
