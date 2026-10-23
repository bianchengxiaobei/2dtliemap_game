#pragma once

#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"

namespace zl::core {
class EventBus;
}

namespace zl::physics {
class PhysicsWorld;
}  // namespace zl::physics

namespace zl::game {

// M5-T7：僵尸实体工厂 —— WorldDemoState 的初始种子生成与 SpawnLodSystem 的实体化共用
// （组件构成单一出处：AI 状态机/感知/寻路/战斗/LOD 全套齐备，与演示僵尸完全一致）。
// M7-T4：events 非空时发 OnZombieSpawn（排队派发；脚本/HUD 等消费）。
ecs::Entity SpawnZombie(ecs::EcsWorld& world, physics::PhysicsWorld* physics, float px, float py,
                        core::EventBus* events = nullptr,
                        ecs::ZombieVariantKind variant = ecs::ZombieVariantKind::Normal);

}  // namespace zl::game
