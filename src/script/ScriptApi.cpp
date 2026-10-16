#include "script/ScriptApi.h"

#include <utility>

#include <entt/entt.hpp>

#include "ai/PathfindingService.h"
#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "core/GameLoop.h"
#include "core/Log.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/GameContext.h"
#include "game/GroundItems.h"
#include "game/ZombieFactory.h"
#include "items/InventoryOps.h"
#include "physics/PhysicsWorld.h"

namespace zl::script {
namespace {

constexpr const char* kModule = "script-api";

}  // namespace

std::int64_t ScriptApi::GetTick() const {
    if (m_ctx == nullptr || m_ctx->loop == nullptr) {
        return 0;
    }
    return static_cast<std::int64_t>(m_ctx->loop->TotalTicks());
}

std::uint64_t ScriptApi::GetPlayerId() const {
    const entt::entity player = FindPlayer();
    if (m_ctx == nullptr || m_ctx->ecs == nullptr || player == entt::null) {
        return 0;
    }
    return m_ctx->ecs->IdOf(player).id;
}

core::DateTime ScriptApi::GetTime() const {
    if (m_ctx == nullptr || m_ctx->gameTime == nullptr) {
        return {};
    }
    return m_ctx->gameTime->Now();
}

double ScriptApi::GetTotalGameMinutes() const {
    if (m_ctx == nullptr || m_ctx->gameTime == nullptr) {
        return 0.0;
    }
    return m_ctx->gameTime->TotalGameMinutes();
}

bool ScriptApi::GetPlayerPosition(ScriptVec3& out) const {
    const entt::entity player = FindPlayer();
    if (player == entt::null) {
        return false;
    }
    const auto* position = m_ctx->ecs->Registry().try_get<ecs::Position>(player);
    if (position == nullptr) {
        return false;
    }
    out = ScriptVec3{position->x, position->y, position->z};
    return true;
}

bool ScriptApi::TeleportPlayer(float x, float y, int z) const {
    const entt::entity player = FindPlayer();
    if (player == entt::null) {
        return false;
    }
    entt::registry& registry = m_ctx->ecs->Registry();
    auto* position = registry.try_get<ecs::Position>(player);
    if (position == nullptr) {
        return false;
    }
    position->x = x;
    position->y = y;
    position->z = z;
    if (auto* velocity = registry.try_get<ecs::Velocity>(player)) {
        velocity->x = 0.0f;
        velocity->y = 0.0f;
    }
    // 物理体才是位置的权威（PhysicsSyncSystem 每 tick 从体回写）：一并传送
    if (auto* rigid = registry.try_get<ecs::RigidBody>(player)) {
        if (m_ctx->physics != nullptr && rigid->body != nullptr) {
            m_ctx->physics->TeleportBody(rigid->body, x, y, z);
        }
    }
    return true;
}

bool ScriptApi::GetPlayerHealth(float& out) const {
    const entt::entity player = FindPlayer();
    if (player == entt::null) {
        return false;
    }
    const auto* health = m_ctx->ecs->Registry().try_get<ecs::Health>(player);
    if (health == nullptr) {
        return false;
    }
    out = health->current;
    return true;
}

int ScriptApi::AddPlayerItem(const std::string& defId, int count) const {
    const entt::entity player = FindPlayer();
    if (player == entt::null || m_ctx->items == nullptr || count <= 0) {
        return 0;
    }
    auto* bag = m_ctx->ecs->Registry().try_get<ecs::Container>(player);
    if (bag == nullptr) {
        return 0;
    }
    return items::AddItem(*bag, *m_ctx->items, ecs::ItemInstance{defId, count, 1.0f});
}

bool ScriptApi::IsPassable(int gx, int gy) const {
    if (m_ctx == nullptr || m_ctx->pathfinding == nullptr) {
        return false;
    }
    return m_ctx->pathfinding->IsPassable(gx, gy);
}

bool ScriptApi::SpawnItem(const std::string& defId, float x, float y, int z) const {
    if (m_ctx == nullptr || m_ctx->ecs == nullptr || m_ctx->items == nullptr) {
        return false;
    }
    if (m_ctx->items->Find(defId) == nullptr) {
        ZL_WARN(kModule, "SpawnItem：未知物品定义 id={}", defId);
        return false;
    }
    const ecs::Entity entity = game::SpawnGroundItem(*m_ctx->ecs, *m_ctx->items,
                                                     ecs::ItemInstance{defId, 1, 1.0f}, x, y, z);
    return entity != ecs::EcsWorld::kNull;
}

std::uint64_t ScriptApi::SpawnZombie(float x, float y, int z) const {
    if (m_ctx == nullptr || m_ctx->ecs == nullptr) {
        return 0;
    }
    (void)z;  // 僵尸生成目前只支持地面层（z=0 语义，与 ZombieFactory 一致）
    const ecs::Entity zombie = game::SpawnZombie(*m_ctx->ecs, m_ctx->physics, x, y);
    if (zombie == ecs::EcsWorld::kNull) {
        return 0;
    }
    return m_ctx->ecs->IdOf(zombie).id;
}

int ScriptApi::ZombieCount() const {
    if (m_ctx == nullptr || m_ctx->ecs == nullptr) {
        return 0;
    }
    int count = 0;
    m_ctx->ecs->Registry().view<ecs::AiState>().each(
        [&](entt::entity, const ecs::AiState&) { ++count; });
    return count;
}

bool ScriptApi::GetZombiePosition(std::uint64_t handle, ScriptVec3& out) const {
    if (m_ctx == nullptr || m_ctx->ecs == nullptr || handle == 0) {
        return false;
    }
    const ecs::Entity zombie = m_ctx->ecs->FindById(handle);
    if (zombie == ecs::EcsWorld::kNull) {
        return false;
    }
    const auto* position = m_ctx->ecs->Registry().try_get<ecs::Position>(zombie);
    if (position == nullptr) {
        return false;
    }
    out = ScriptVec3{position->x, position->y, position->z};
    return true;
}

bool ScriptApi::ItemExists(const std::string& defId) const {
    return m_ctx != nullptr && m_ctx->items != nullptr && m_ctx->items->Find(defId) != nullptr;
}

std::string ScriptApi::ItemName(const std::string& defId) const {
    const items::ItemDef* def =
        (m_ctx != nullptr && m_ctx->items != nullptr) ? m_ctx->items->Find(defId) : nullptr;
    return def != nullptr ? def->name : std::string();
}

entt::entity ScriptApi::FindPlayer() const {
    if (m_ctx == nullptr || m_ctx->ecs == nullptr) {
        return entt::null;
    }
    entt::entity found = entt::null;
    m_ctx->ecs->Registry().view<ecs::PlayerTag>().each([&](entt::entity entity) {
        if (found == entt::null) {
            found = entity;
        }
    });
    return found;
}

}  // namespace zl::script
