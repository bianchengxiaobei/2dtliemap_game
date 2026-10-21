#include "script/ScriptApi.h"

#include <ctime>
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
#include "ui/HudState.h"
#include "ui/UiInput.h"

namespace zl::script {
namespace {

constexpr const char* kModule = "script-api";

std::string FormatSaveTime(std::int64_t epochSeconds) {
    if (epochSeconds <= 0) {
        return {};
    }
    const std::time_t value = static_cast<std::time_t>(epochSeconds);
    std::tm local{};
#if defined(_WIN32)
    if (localtime_s(&local, &value) != 0) {
        return {};
    }
#else
    if (localtime_r(&value, &local) == nullptr) {
        return {};
    }
#endif
    char text[32] = {};
    if (std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &local) == 0) {
        return {};
    }
    return text;
}

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

bool ScriptApi::GetHudState(ui::HudState& out) const {
    if (m_ctx == nullptr) {
        out = {};
        return false;
    }
    out = m_ctx->hudState;
    return out.active;
}

bool ScriptApi::ToggleInventory() const {
    return m_ctx != nullptr && m_ctx->toggleInventory && m_ctx->toggleInventory();
}

bool ScriptApi::IsInventoryOpen() const {
    return m_ctx != nullptr && m_ctx->isInventoryOpen && m_ctx->isInventoryOpen();
}

bool ScriptApi::IsMenuOpen() const {
    return m_ctx != nullptr && m_ctx->isMenuOpen && m_ctx->isMenuOpen();
}

bool ScriptApi::GetMenuState(ui::MenuState& out) const {
    if (m_ctx == nullptr || !m_ctx->getMenuState) {
        out = {};
        return false;
    }
    return m_ctx->getMenuState(out);
}

bool ScriptApi::ApplyMenuSettings(const ui::MenuSettings& settings,
                                  std::string& error) const {
    if (m_ctx == nullptr || !m_ctx->applyMenuSettings) {
        error = "设置保存不可用";
        return false;
    }
    return m_ctx->applyMenuSettings(settings, error);
}

bool ScriptApi::MenuAction(const std::string& action) const {
    if (m_ctx == nullptr || !m_ctx->menuAction) {
        return false;
    }
    ui::MenuActionType type;
    if (action == "new_game") {
        type = ui::MenuActionType::NewGame;
    } else if (action == "multiplayer") {
        type = ui::MenuActionType::Multiplayer;
    } else if (action == "continue") {
        type = ui::MenuActionType::Continue;
    } else if (action == "resume") {
        type = ui::MenuActionType::Resume;
    } else if (action == "return_to_main") {
        type = ui::MenuActionType::ReturnToMainMenu;
    } else if (action == "quit") {
        type = ui::MenuActionType::Quit;
    } else {
        return false;
    }
    m_ctx->menuAction(type);
    return true;
}

std::string ScriptApi::MenuKeyName(int rmlKey) const {
    return ui::KeyNameFromRmlKey(static_cast<Rml::Input::KeyIdentifier>(rmlKey));
}

void ScriptApi::PlayUiClick() const {
    if (m_ctx != nullptr && m_ctx->playUiClick) {
        m_ctx->playUiClick();
    }
}

bool ScriptApi::GetInventoryState(ui::InventoryState& out) const {
    if (m_ctx == nullptr || !m_ctx->getInventoryState) {
        out = {};
        return false;
    }
    return m_ctx->getInventoryState(out);
}

bool ScriptApi::InventoryAction(const ui::InventoryAction& action) const {
    return m_ctx != nullptr && m_ctx->inventoryAction && m_ctx->inventoryAction(action);
}

void ScriptApi::PlayInventoryClick() const {
    if (m_ctx != nullptr && m_ctx->playInventoryClick) {
        m_ctx->playInventoryClick();
    }
}

bool ScriptApi::ListSaves(std::vector<ScriptSaveInfo>& out, std::string& error) const {
    out.clear();
    if (m_ctx == nullptr || !m_ctx->listSaves) {
        error = "存档列表不可用";
        return false;
    }
    std::vector<save::SaveMetadata> saves;
    if (!m_ctx->listSaves(saves, error)) {
        return false;
    }
    out.reserve(saves.size());
    for (const save::SaveMetadata& entry : saves) {
        ScriptSaveInfo info;
        info.id = entry.id;
        info.name = entry.name.empty() ? entry.id : entry.name;
        info.updatedText = FormatSaveTime(entry.updatedAt);
        info.mapId = entry.mapId;
        info.gameVersion = entry.gameVersion;
        out.push_back(std::move(info));
    }
    return true;
}

bool ScriptApi::LoadSave(const std::string& id, std::string& error) const {
    if (id.empty()) {
        error = "存档 id 为空";
        return false;
    }
    if (m_ctx == nullptr || !m_ctx->loadSave) {
        error = "读档不可用";
        return false;
    }
    if (!m_ctx->loadSave(id, error)) {
        if (error.empty()) {
            error = "读取存档失败";
        }
        return false;
    }
    return true;
}

bool ScriptApi::DeleteSave(const std::string& id, std::string& error) const {
    if (id.empty()) {
        error = "存档 id 为空";
        return false;
    }
    if (m_ctx == nullptr || !m_ctx->deleteSave) {
        error = "删除存档不可用";
        return false;
    }
    if (!m_ctx->deleteSave(id, error)) {
        if (error.empty()) {
            error = "删除存档失败";
        }
        return false;
    }
    return true;
}

bool ScriptApi::QuickSave() const {
    return m_ctx != nullptr && m_ctx->quickSave && m_ctx->quickSave();
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
    const ecs::Entity zombie = game::SpawnZombie(*m_ctx->ecs, m_ctx->physics, x, y, m_ctx->events);
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
