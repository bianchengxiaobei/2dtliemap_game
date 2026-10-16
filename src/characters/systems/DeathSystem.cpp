#include "characters/systems/DeathSystem.h"

#include <vector>

#include "characters/components/CharacterComponents.h"
#include "core/EventBus.h"
#include "core/GameEvents.h"
#include "core/Log.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "physics/PhysicsWorld.h"

namespace zl::characters {
namespace {

constexpr const char* kLogModule = "characters";
constexpr const char* kCorpseSpriteName = "corpse.placeholder";

}  // namespace

void DeathSystem::Update(ecs::EcsWorld& world, float dt) {
    entt::registry& registry = world.Registry();

    // 先收集（不在 view 遍历中改结构；Alive 过滤本 tick 已入销毁队列的实体）
    std::vector<ecs::Entity> dead;
    registry.view<ecs::Health>().each([&](const ecs::Entity e, const ecs::Health& health) {
        if (health.current <= 0.0f && world.Alive(e)) {
            dead.push_back(e);
        }
    });

    for (const ecs::Entity e : dead) {
        // —— 阶段 1：倒地窗口（M5-T6）——
        auto* dying = registry.try_get<ecs::Dying>(e);
        if (dying == nullptr) {
            ecs::Dying state;
            state.remaining = m_fallSeconds;
            registry.emplace<ecs::Dying>(e, state);
            // 停步：清移动意图 + 冻结速度（击退由 CombatSystem 的硬直接管滑行）
            if (auto* intent = registry.try_get<ecs::MoveIntent>(e); intent != nullptr) {
                intent->x = 0.0f;
                intent->y = 0.0f;
            }
            if (auto* velocity = registry.try_get<ecs::Velocity>(e); velocity != nullptr) {
                velocity->x = 0.0f;
                velocity->y = 0.0f;
            }
            // 表现：换成尸体帧（"倒下"）；闪烁 tint 由 CombatSystem 在硬直结束时恢复
            if (auto* sprite = registry.try_get<ecs::SpriteRef>(e); sprite != nullptr) {
                sprite->sprite = ecs::HashName(kCorpseSpriteName);
            }
            ZL_INFO(kLogModule, "倒地：稳定 ID {}（{:.1f}s 后转尸体）", world.IdOf(e).id,
                    m_fallSeconds);
            continue;
        }
        dying->remaining -= dt;
        if (dying->remaining > 0.0f) {
            continue;  // 窗口未结束：还在"倒下"过程（击退滑行仍生效）
        }

        // —— 阶段 2：转尸体 + 回收 ——
        float x = 0.0f;
        float y = 0.0f;
        int z = 0;
        if (const auto* position = registry.try_get<ecs::Position>(e)) {
            x = position->x;
            y = position->y;
            z = position->z;
        }
        const std::uint64_t sourceId = world.IdOf(e).id;

        // M7-T4：死亡事件（排队：tick 末统一派发；killer 暂无追踪，未知 = 0）
        if (m_events != nullptr) {
            if (registry.all_of<ecs::PlayerTag>(e)) {
                m_events->EmitQueued(core::kOnPlayerDeath,
                                     core::PlayerEvent{sourceId, x, y, z});
            } else {
                m_events->EmitQueued(core::kOnZombieDead,
                                     core::ZombieEvent{sourceId, 0, x, y, z});
            }
        }

        // 尸体（延迟创建：与销毁同 tick 入队，Flush 统一生效；日志在 Flush 时输出）
        world.QueueCreate([x, y, z, sourceId](ecs::EcsWorld& w) {
            const ecs::Entity corpse = w.Create();
            entt::registry& reg = w.Registry();
            reg.emplace<ecs::Position>(corpse, ecs::Position{x, y, z});
            reg.emplace<ecs::SpriteRef>(
                corpse,
                ecs::SpriteRef{ecs::HashName(kCorpseSpriteName), 0, 0xFFFFFFFFu});
            reg.emplace<ecs::Corpse>(corpse, ecs::Corpse{sourceId});
            ZL_INFO(kLogModule, "实体死亡：稳定 ID {}（格 ({:.1f}, {:.1f}) z={}）→ 尸体（稳定 ID {}）",
                    sourceId, x, y, z, w.IdOf(corpse).id);
            return corpse;
        });

        // 物理体先于实体回收（RigidBody 不拥有 body，RigidBody 置空防悬垂指针）
        if (m_physics != nullptr) {
            if (auto* rigid = registry.try_get<ecs::RigidBody>(e);
                rigid != nullptr && rigid->body != nullptr) {
                m_physics->DestroyBody(rigid->body);
                rigid->body = nullptr;
            }
        }

        world.Destroy(e);
    }
}

}  // namespace zl::characters
