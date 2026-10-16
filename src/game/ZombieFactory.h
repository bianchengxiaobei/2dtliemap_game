#pragma once

#include "ecs/EcsWorld.h"

namespace zl::physics {
class PhysicsWorld;
}  // namespace zl::physics

namespace zl::game {

// M5-T7：僵尸实体工厂 —— WorldDemoState 的初始种子生成与 SpawnLodSystem 的实体化共用
// （组件构成单一出处：AI 状态机/感知/寻路/战斗/LOD 全套齐备，与演示僵尸完全一致）。
ecs::Entity SpawnZombie(ecs::EcsWorld& world, physics::PhysicsWorld* physics, float px, float py);

}  // namespace zl::game
