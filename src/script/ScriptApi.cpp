#include "script/ScriptApi.h"

#include <algorithm>
#include <ctime>
#include <string_view>
#include <utility>

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include "ai/PathfindingService.h"
#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "core/Config.h"
#include "core/Constants.h"
#include "core/GameLoop.h"
#include "core/Log.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/BuildingRules.h"
#include "game/ContentReload.h"
#include "game/GameContext.h"
#include "game/GroundItems.h"
#include "game/NetworkClient.h"
#include "game/Missions.h"
#include "game/Poi.h"
#include "game/ZombieFactory.h"
#include "items/InventoryOps.h"
#include "items/ItemRegistry.h"
#include "items/RecipeRegistry.h"
#include "physics/PhysicsWorld.h"
#include "ui/HudState.h"
#include "ui/UiInput.h"
#include "world/GridSquare.h"
#include "world/World.h"

namespace zl::script {
namespace {

constexpr const char* kModule = "script-api";

ui::HudModField* FindModHudField(std::vector<ui::HudModField>& fields,
                                 const std::string& modId,
                                 const std::string& id) {
    for (ui::HudModField& field : fields) {
        if (field.modId == modId && field.id == id) {
            return &field;
        }
    }
    return nullptr;
}

bool IsHudTone(std::string_view tone) {
    return tone == "info" || tone == "warn" || tone == "ok" ||
           tone == "fail";
}

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
    out.modFieldCount = 0;
    out.modFields.fill({});
    const std::size_t count =
        std::min(m_ctx->modHudFields.size(), ui::kMaxModHudFields);
    for (std::size_t i = 0; i < count; ++i) {
        out.modFields[i] = m_ctx->modHudFields[i];
    }
    out.modFieldCount = static_cast<int>(count);
    return out.active;
}

bool ScriptApi::RegisterHudField(const std::string& modId,
                                 const std::string& id,
                                 const std::string& label) const {
    if (m_ctx == nullptr || modId.empty() || id.empty() || label.empty()) {
        return false;
    }
    if (ui::HudModField* existing =
            FindModHudField(m_ctx->modHudFields, modId, id);
        existing != nullptr) {
        existing->label = label;
        return true;
    }
    if (m_ctx->modHudFields.size() >= ui::kMaxModHudFields) {
        return false;
    }
    ui::HudModField field;
    field.modId = modId;
    field.id = id;
    field.label = label;
    m_ctx->modHudFields.push_back(std::move(field));
    ZL_INFO(kModule, "注册 Mod HUD 字段：{}.{}（{}）", modId, id, label);
    return true;
}

bool ScriptApi::SetHudField(const std::string& modId, const std::string& id,
                            const std::string& text,
                            const std::string& tone) const {
    if (m_ctx == nullptr || !IsHudTone(tone)) {
        return false;
    }
    ui::HudModField* field =
        FindModHudField(m_ctx->modHudFields, modId, id);
    if (field == nullptr) {
        return false;
    }
    field->text = text;
    field->tone = tone;
    return true;
}

bool ScriptApi::ClearHudField(const std::string& modId,
                              const std::string& id) const {
    if (m_ctx == nullptr) {
        return false;
    }
    ui::HudModField* field =
        FindModHudField(m_ctx->modHudFields, modId, id);
    if (field == nullptr) {
        return false;
    }
    field->text.clear();
    field->tone = "info";
    return true;
}

void ScriptApi::RemoveHudFields(const std::string& modId) const {
    if (m_ctx == nullptr) {
        return;
    }
    m_ctx->modHudFields.erase(
        std::remove_if(m_ctx->modHudFields.begin(), m_ctx->modHudFields.end(),
                       [&modId](const ui::HudModField& field) {
                           return field.modId == modId;
                       }),
        m_ctx->modHudFields.end());
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

bool ScriptApi::GetSquare(int gx, int gy, int z, ScriptSquare& out) const {
    out = {};
    if (m_ctx == nullptr || m_ctx->world == nullptr || z < 0 ||
        z >= core::kMaxZ) {
        return false;
    }
    const world::GridSquare* square = m_ctx->world->GetSquare(gx, gy, z);
    if (square == nullptr) {
        return false;
    }
    const auto convert = [](const world::SquareObject& object) {
        return ScriptSquareObject{object.sprite, object.hp, object.playerBuilt,
                                  object.blocksMovement};
    };
    out.loaded = true;
    if (const world::SquareObject* floor = square->Floor(); floor != nullptr) {
        out.hasFloor = true;
        out.floor = convert(*floor);
    }
    for (const world::SquareObject& object :
         square->Layer(world::SquareLayer::Wall)) {
        out.wall.push_back(convert(object));
    }
    for (const world::SquareObject& object :
         square->Layer(world::SquareLayer::Furniture)) {
        out.furniture.push_back(convert(object));
    }
    for (const world::SquareObject& object :
         square->Layer(world::SquareLayer::Item)) {
        out.item.push_back(convert(object));
    }
    return true;
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

int ScriptApi::RegisterItems(const std::string& modId, const std::string& json,
                             std::string& error) const {
    error.clear();
    if (m_ctx == nullptr || m_ctx->items == nullptr) {
        error = "物品注册表不可用";
        return 0;
    }
    if (const std::string blocked =
            game::DynamicContentBlockedReason(m_ctx->allowDynamicContent);
        !blocked.empty()) {
        error = blocked;
        return 0;
    }
    std::size_t added = 0;
    if (!m_ctx->items->MergeFromString(json, "mod:" + modId + " items",
                                       &added, &error)) {
        ZL_WARN(kModule, "动态物品注册失败 [{}]：{}", modId, error);
        return 0;
    }
    if (added > 0) {
        m_ctx->dynamicContentCount += static_cast<int>(added);
        if (m_ctx->contentChanged) {
            m_ctx->contentChanged();
        }
        ZL_INFO(kModule, "动态物品注册 [{}]：+{}（总计 {}）", modId, added,
                m_ctx->items->Size());
    }
    return static_cast<int>(added);
}

int ScriptApi::RegisterRecipes(const std::string& modId, const std::string& json,
                               std::string& error) const {
    error.clear();
    if (m_ctx == nullptr || m_ctx->items == nullptr ||
        m_ctx->recipes == nullptr) {
        error = "配方注册表不可用";
        return 0;
    }
    if (const std::string blocked =
            game::DynamicContentBlockedReason(m_ctx->allowDynamicContent);
        !blocked.empty()) {
        error = blocked;
        return 0;
    }
    std::size_t added = 0;
    if (!m_ctx->recipes->MergeFromString(json, "mod:" + modId + " recipes",
                                         *m_ctx->items, &added, &error)) {
        ZL_WARN(kModule, "动态配方注册失败 [{}]：{}", modId, error);
        return 0;
    }
    if (added > 0) {
        m_ctx->dynamicContentCount += static_cast<int>(added);
        if (m_ctx->contentChanged) {
            m_ctx->contentChanged();
        }
        ZL_INFO(kModule, "动态配方注册 [{}]：+{}（总计 {}）", modId, added,
                m_ctx->recipes->Size());
    }
    return static_cast<int>(added);
}

int ScriptApi::RegisterBuildings(const std::string& modId,
                                 const std::string& json,
                                 std::string& error) const {
    error.clear();
    if (m_ctx == nullptr || m_ctx->config == nullptr) {
        error = "配置不可用";
        return 0;
    }
    if (const std::string blocked =
            game::DynamicContentBlockedReason(m_ctx->allowDynamicContent);
        !blocked.empty()) {
        error = blocked;
        return 0;
    }
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(json);
    } catch (const std::exception& e) {
        error = std::string("JSON 解析失败：") + e.what();
        return 0;
    }
    if (!doc.is_object() || !doc.contains("format") ||
        !doc["format"].is_string() ||
        doc["format"].get<std::string>() != "buildings-v1") {
        error = "format 必须是 buildings-v1";
        return 0;
    }
    if (!doc.contains("types") || !doc["types"].is_array() ||
        doc["types"].empty()) {
        error = "types 必须是非空数组";
        return 0;
    }
    const std::string source = "mod:" + modId + " buildings";
    const std::vector<game::BuildableDef> parsed =
        game::ParseBuildableDefs(doc["types"], source);
    if (parsed.empty()) {
        error = "没有有效的建造定义";
        return 0;
    }

    std::vector<game::BuildableDef> all = game::LoadBuildableDefs(*m_ctx->config);
    game::AppendBuildableDefs(all, m_ctx->modBuildables);
    int added = 0;
    for (const game::BuildableDef& def : parsed) {
        const std::vector<game::BuildableDef> one{def};
        if (game::AppendBuildableDefs(all, one) != 1) {
            continue;
        }
        m_ctx->modBuildables.push_back(def);
        ++added;
    }
    if (added == 0) {
        error = "没有可追加的建造项（重复 id 或 sprite 冲突）";
        return 0;
    }
    m_ctx->dynamicContentCount += added;
    if (m_ctx->contentChanged) {
        m_ctx->contentChanged();
    }
    ZL_INFO(kModule, "动态建造注册 [{}]：+{}（总计 {}）", modId, added,
            m_ctx->modBuildables.size());
    return added;
}

int ScriptApi::RegisterPois(const std::string& modId, const std::string& json,
                            std::string& error) const {
    error.clear();
    if (m_ctx == nullptr || m_ctx->poiConfig == nullptr) {
        error = "POI 配置不可用";
        return 0;
    }
    if (const std::string blocked =
            game::DynamicContentBlockedReason(m_ctx->allowDynamicContent);
        !blocked.empty()) {
        error = blocked;
        return 0;
    }
    if (!m_ctx->poiConfig->enabled) {
        error = "基础 POI 配置未启用";
        return 0;
    }
    game::PoiMergeStats stats;
    if (!game::MergePoiJson(json, "mod:" + modId + " pois",
                            m_ctx->poiConfig, &stats, &error)) {
        ZL_WARN(kModule, "动态 POI 注册失败 [{}]：{}", modId, error);
        return 0;
    }
    if (stats.defsAdded == 0) {
        error = "没有可追加的 POI（重复 id 或无效定义）";
        return 0;
    }
    m_ctx->dynamicContentCount += stats.defsAdded;
    if (m_ctx->contentChanged) {
        m_ctx->contentChanged();
    }
    ZL_INFO(kModule, "动态 POI 注册 [{}]：+{}（总计 {}）", modId,
            stats.defsAdded, m_ctx->poiConfig->defs.size());
    return stats.defsAdded;
}

int ScriptApi::RegisterMissions(const std::string& modId,
                                const std::string& json,
                                std::string& error) const {
    error.clear();
    if (m_ctx == nullptr || m_ctx->missionConfig == nullptr) {
        error = "任务配置不可用";
        return 0;
    }
    if (const std::string blocked =
            game::DynamicContentBlockedReason(m_ctx->allowDynamicContent);
        !blocked.empty()) {
        error = blocked;
        return 0;
    }
    if (!m_ctx->missionConfig->enabled) {
        error = "基础任务配置未启用";
        return 0;
    }
    if (m_ctx->missionSourceJson.empty()) {
        error = "任务配置没有可追加的基础文档";
        return 0;
    }
    nlohmann::json incoming;
    try {
        incoming = nlohmann::json::parse(json);
    } catch (const std::exception& e) {
        error = std::string("JSON 解析失败：") + e.what();
        return 0;
    }
    if (!incoming.is_object() || !incoming.contains("format") ||
        !incoming["format"].is_string() ||
        incoming["format"].get<std::string>() != "missions-v1") {
        error = "format 必须是 missions-v1";
        return 0;
    }
    if (!incoming.contains("defs") || !incoming["defs"].is_array() ||
        incoming["defs"].empty()) {
        error = "defs 必须是非空数组";
        return 0;
    }
    nlohmann::json candidate;
    try {
        candidate = nlohmann::json::parse(m_ctx->missionSourceJson);
    } catch (const std::exception& e) {
        error = std::string("基础任务文档解析失败：") + e.what();
        return 0;
    }
    if (!candidate.is_object() || !candidate.contains("defs") ||
        !candidate["defs"].is_array()) {
        error = "基础任务文档缺少 defs 数组";
        return 0;
    }
    const std::size_t incomingCount = incoming["defs"].size();
    for (const nlohmann::json& def : incoming["defs"]) {
        candidate["defs"].push_back(def);
    }
    core::Config candidateConfig;
    candidateConfig.Set("missions", candidate);
    game::MissionConfig parsed;
    std::string parseError;
    const std::size_t expected =
        m_ctx->missionConfig->defs.size() + incomingCount;
    if (!game::LoadMissionConfig(candidateConfig, &parsed, &parseError) ||
        parsed.defs.size() != expected) {
        error = parseError.empty()
                    ? std::string("任务定义无效、重复或前置不存在")
                    : parseError;
        ZL_WARN(kModule, "动态任务注册失败 [{}]：{}", modId, error);
        return 0;
    }
    *m_ctx->missionConfig = std::move(parsed);
    m_ctx->missionSourceJson = candidate.dump();
    m_ctx->dynamicContentCount += static_cast<int>(incomingCount);
    if (m_ctx->contentChanged) {
        m_ctx->contentChanged();
    }
    ZL_INFO(kModule, "动态任务注册 [{}]：+{}（总计 {}）", modId, incomingCount,
            m_ctx->missionConfig->defs.size());
    return static_cast<int>(incomingCount);
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
    if (found == entt::null && m_ctx->networkClient != nullptr) {
        const std::uint64_t localId = m_ctx->networkClient->PlayerStableId();
        if (localId != 0) {
            found = m_ctx->ecs->FindById(localId);
        }
    }
    return found;
}

}  // namespace zl::script
