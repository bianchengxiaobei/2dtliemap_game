#include "script/ScriptApi.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <ctime>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include "ai/PathfindingService.h"
#include "ai/components/AiComponents.h"
#include "audio/SoundManager.h"
#include "characters/components/AnimalComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/systems/CombatSystem.h"
#include "core/Config.h"
#include "core/Constants.h"
#include "core/GameLoop.h"
#include "core/Log.h"
#include "ecs/EcsWorld.h"
#include "ecs/SpatialIndex.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/BuildingRules.h"
#include "game/Companion.h"
#include "game/ContentReload.h"
#include "game/FireComponents.h"
#include "game/GameContext.h"
#include "game/GroundItems.h"
#include "game/NetworkClient.h"
#include "game/Missions.h"
#include "game/Poi.h"
#include "game/ProgressionRules.h"
#include "game/SettlementNpc.h"
#include "game/VehicleComponents.h"
#include "game/Weather.h"
#include "game/WorldSimulation.h"
#include "game/ZombieFactory.h"
#include "items/Crafting.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"
#include "items/RecipeRegistry.h"
#include "net/CommandPipeline.h"
#include "physics/PhysicsWorld.h"
#include "script/ModDataComponents.h"
#include "ui/HudState.h"
#include "ui/UiInput.h"
#include "world/GridSquare.h"
#include "world/World.h"
#include "world/WorldCoords.h"

namespace zl::script {
namespace {

constexpr const char* kModule = "script-api";

ScriptActionResult MakeActionError(std::string error) {
    return ScriptActionResult{false, false, std::move(error)};
}

ScriptXpResult MakeXpError(std::string error) {
    ScriptXpResult out;
    out.result = MakeActionError(std::move(error));
    return out;
}

bool FindBuildableDef(const game::GameContext& ctx, const std::string& id,
                      game::BuildableDef& out, int& index) {
    if (ctx.config == nullptr || id.empty()) {
        return false;
    }
    std::vector<game::BuildableDef> defs =
        game::LoadBuildableDefs(*ctx.config);
    game::AppendBuildableDefs(defs, ctx.modBuildables);
    for (std::size_t i = 0; i < defs.size(); ++i) {
        if (defs[i].id == id) {
            out = defs[i];
            index = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

bool ParseDamageType(std::string_view value, ecs::DamageType& out) {
    if (value.empty() || value == "blunt") {
        out = ecs::DamageType::Blunt;
        return true;
    }
    if (value == "bite") {
        out = ecs::DamageType::Bite;
        return true;
    }
    if (value == "claw") {
        out = ecs::DamageType::Claw;
        return true;
    }
    if (value == "fire") {
        out = ecs::DamageType::Fire;
        return true;
    }
    if (value == "debug") {
        out = ecs::DamageType::Debug;
        return true;
    }
    return false;
}

bool ParseSkillKind(std::string_view value, ecs::SkillKind& out) {
    for (std::size_t i = 0; i < ecs::kSkillCount; ++i) {
        if (value == ecs::kSkillIds[i]) {
            out = static_cast<ecs::SkillKind>(i);
            return true;
        }
    }
    return false;
}

bool ParseZombieVariant(std::string_view value,
                        ecs::ZombieVariantKind& out) {
    if (value == "normal") {
        out = ecs::ZombieVariantKind::Normal;
        return true;
    }
    if (value == "fast") {
        out = ecs::ZombieVariantKind::Fast;
        return true;
    }
    if (value == "strong") {
        out = ecs::ZombieVariantKind::Strong;
        return true;
    }
    if (value == "armored") {
        out = ecs::ZombieVariantKind::Armored;
        return true;
    }
    return false;
}

bool IsValidAudioClip(std::string_view clip) {
    if (clip.empty() || clip.size() > 64) {
        return false;
    }
    for (const char ch : clip) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (!(std::isalnum(c) || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

bool ParseSoundKind(std::string_view value, core::SoundType& out) {
    if (value.empty() || value == "generic") {
        out = core::SoundType::Generic;
        return true;
    }
    if (value == "gunshot") {
        out = core::SoundType::Gunshot;
        return true;
    }
    if (value == "impact") {
        out = core::SoundType::Impact;
        return true;
    }
    if (value == "footstep") {
        out = core::SoundType::Footstep;
        return true;
    }
    if (value == "growl" || value == "zombie_growl" || value == "zombie") {
        out = core::SoundType::ZombieGrowl;
        return true;
    }
    if (value == "ui") {
        out = core::SoundType::Ui;
        return true;
    }
    if (value == "vehicle") {
        out = core::SoundType::Vehicle;
        return true;
    }
    return false;
}

void SetDamageFromFront(const ecs::Facing* facing, ecs::DamageEvent& event) {
    if (facing == nullptr) {
        return;
    }
    constexpr float kIso = 0.70710678f;
    float x = 0.0f;
    float y = 0.0f;
    switch (facing->dir) {
        case ecs::Direction::Down:
            x = kIso;
            y = kIso;
            break;
        case ecs::Direction::Left:
            x = -kIso;
            y = kIso;
            break;
        case ecs::Direction::Right:
            x = kIso;
            y = -kIso;
            break;
        case ecs::Direction::Up:
            x = -kIso;
            y = -kIso;
            break;
    }
    event.dirX = -x;
    event.dirY = -y;
}

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

float FiniteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

float ClampUnit(float value, float fallback = 0.0f) {
    return std::clamp(FiniteOr(value, fallback), 0.0f, 1.0f);
}

float NonNegative(float value) {
    return std::max(0.0f, FiniteOr(value, 0.0f));
}

const char* DamageTypeKey(ecs::DamageType type) {
    switch (type) {
        case ecs::DamageType::Blunt:
            return "blunt";
        case ecs::DamageType::Bite:
            return "bite";
        case ecs::DamageType::Claw:
            return "claw";
        case ecs::DamageType::Fire:
            return "fire";
        case ecs::DamageType::Debug:
            return "debug";
    }
    return "blunt";
}

const char* InfectionStageKey(ecs::InfectionStage stage) {
    switch (stage) {
        case ecs::InfectionStage::Incubating:
            return "incubating";
        case ecs::InfectionStage::Fever:
            return "fever";
        case ecs::InfectionStage::Severe:
            return "severe";
        case ecs::InfectionStage::None:
            return "none";
    }
    return "none";
}

ScriptItemStack MakeItemStack(const ecs::ItemInstance& instance, int index,
                              const items::ItemRegistry* registry) {
    ScriptItemStack out;
    out.index = index;
    out.defId = instance.defId;
    out.count = std::max(0, instance.count);
    out.condition = ClampUnit(instance.condition, 1.0f);
    out.loadedAmmo = std::max(0, instance.loadedAmmo);
    if (registry == nullptr) {
        return out;
    }
    const items::ItemDef* def = registry->Find(instance.defId);
    if (def == nullptr) {
        return out;
    }
    out.name = def->name;
    out.type = items::ItemTypeToString(def->type);
    out.weightKg = static_cast<float>(out.count) * NonNegative(def->weight);
    out.maxStack = std::max(0, def->maxStack);
    out.usable = def->use.Any() || !def->unlocksRecipes.empty();
    out.equippable = def->type == items::ItemType::Weapon ||
                     def->type == items::ItemType::Clothing;
    out.perishable = def->spoilDays > 0.0f;
    out.spoilDays = NonNegative(def->spoilDays);
    return out;
}

const char* ZombieVariantKey(ecs::ZombieVariantKind kind) {
    switch (kind) {
        case ecs::ZombieVariantKind::Fast:
            return "fast";
        case ecs::ZombieVariantKind::Strong:
            return "strong";
        case ecs::ZombieVariantKind::Armored:
            return "armored";
        case ecs::ZombieVariantKind::Normal:
            return "normal";
    }
    return "normal";
}

const char* AnimalKindKey(ecs::AnimalKind kind) {
    return kind == ecs::AnimalKind::Dangerous ? "dangerous" : "passive";
}

const char* VehicleTypeKey(ecs::VehicleType type) {
    switch (type) {
        case ecs::VehicleType::LightPickup:
            return "light";
        case ecs::VehicleType::HeavyPickup:
            return "heavy";
        case ecs::VehicleType::Pickup:
            return "pickup";
    }
    return "pickup";
}

const char* FirePhaseKey(ecs::FirePhase phase) {
    switch (phase) {
        case ecs::FirePhase::Igniting:
            return "igniting";
        case ecs::FirePhase::Burning:
            return "burning";
        case ecs::FirePhase::Smoldering:
            return "smoldering";
        case ecs::FirePhase::Extinguished:
            return "extinguished";
    }
    return "extinguished";
}

const char* BuildLayerKey(world::SquareLayer layer) {
    switch (layer) {
        case world::SquareLayer::Floor:
            return "floor";
        case world::SquareLayer::Wall:
            return "wall";
        case world::SquareLayer::Furniture:
            return "furniture";
        case world::SquareLayer::Item:
            return "item";
        case world::SquareLayer::Mark:
            return "mark";
    }
    return "furniture";
}

ScriptMissionInfo MakeMissionInfo(const game::MissionConfig& config,
                                  const game::MissionDef& def, int index,
                                  const ecs::MissionLog& log, int day) {
    (void)config;
    ScriptMissionInfo out;
    out.index = index;
    out.id = def.id;
    out.name = def.name;
    out.chain = def.chain;
    out.order = def.order;
    out.kind = game::MissionKindName(def.kind);
    out.source = game::MissionSourceName(def.source);
    out.task = game::MissionTaskKindName(def.task);
    out.progress.assign(def.objectives.size(), 0);
    const ecs::MissionRunState* state = game::FindMissionState(log, def.id);
    if (state != nullptr) {
        out.status = game::MissionRunStatusName(state->status);
        out.day = state->day;
        out.progress = state->progress;
        out.progress.resize(def.objectives.size(), 0);
    } else {
        out.status = game::MissionCanActivate(def, log, day) ? "available"
                                                            : "locked";
    }
    out.objectives.reserve(def.objectives.size());
    for (std::size_t i = 0; i < def.objectives.size(); ++i) {
        const game::MissionObjectiveDef& objective = def.objectives[i];
        ScriptMissionObjective entry;
        entry.kind = game::MissionObjectiveKindName(objective.kind);
        entry.target = objective.target;
        entry.count = objective.count;
        entry.progress = out.progress[i];
        entry.description = objective.description;
        out.objectives.push_back(std::move(entry));
    }
    out.reward.xp = def.reward.xp;
    out.reward.skill = def.reward.skill;
    out.reward.item = def.reward.item;
    out.reward.itemCount = def.reward.itemCount;
    out.reward.extraItem = def.reward.extraItem;
    out.reward.extraItemCount = def.reward.extraItemCount;
    out.reward.blueprint = def.reward.blueprint;
    out.reward.label = def.reward.label;
    return out;
}

template <typename T>
struct ComponentCache {
    using Storage =
        decltype(std::declval<const entt::registry&>().storage<T>());

    Storage storage = nullptr;

    explicit ComponentCache(const entt::registry& registry)
        : storage(registry.storage<T>()) {}

    bool Contains(entt::entity entity) const {
        return storage != nullptr && storage->contains(entity);
    }

    const T* TryGet(entt::entity entity) const {
        return Contains(entity) ? &storage->get(entity) : nullptr;
    }
};

// 查询/GetEntity 共用：每个组件只做一次 type_hash 查找，避免逐实体 try_get。
struct EntityInfoCaches {
    ComponentCache<ecs::Position> position;
    ComponentCache<ecs::Health> health;
    ComponentCache<ecs::StableId> stableId;
    ComponentCache<ecs::PlayerTag> playerTag;
    ComponentCache<ecs::ZombieVariant> zombieVariant;
    ComponentCache<ecs::Animal> animal;
    ComponentCache<game::Companion> companion;
    ComponentCache<game::CompanionState> companionState;
    ComponentCache<game::SettlementNpc> settlementNpc;
    ComponentCache<ecs::FriendlyNpc> friendlyNpc;
    ComponentCache<ecs::Vehicle> vehicle;
    ComponentCache<ecs::LootContainer> lootContainer;
    ComponentCache<ecs::PlayerStorage> playerStorage;
    ComponentCache<ecs::ItemInstance> itemInstance;
    ComponentCache<ecs::AnimalCorpse> animalCorpse;
    ComponentCache<ecs::Corpse> corpse;
    ComponentCache<ecs::FireSource> fireSource;
    ComponentCache<ecs::Dying> dying;

    explicit EntityInfoCaches(const entt::registry& registry)
        : position(registry),
          health(registry),
          stableId(registry),
          playerTag(registry),
          zombieVariant(registry),
          animal(registry),
          companion(registry),
          companionState(registry),
          settlementNpc(registry),
          friendlyNpc(registry),
          vehicle(registry),
          lootContainer(registry),
          playerStorage(registry),
          itemInstance(registry),
          animalCorpse(registry),
          corpse(registry),
          fireSource(registry),
          dying(registry) {}
};

std::string_view EntityKindName(const EntityInfoCaches& caches,
                                entt::entity entity) {
    if (caches.playerTag.Contains(entity)) {
        return "player";
    }
    if (caches.zombieVariant.TryGet(entity) != nullptr) {
        return "zombie";
    }
    if (caches.animal.TryGet(entity) != nullptr) {
        return "animal";
    }
    if (caches.companion.TryGet(entity) != nullptr) {
        return "companion";
    }
    if (caches.settlementNpc.TryGet(entity) != nullptr) {
        return "npc";
    }
    if (caches.friendlyNpc.Contains(entity)) {
        return "npc";
    }
    if (caches.vehicle.TryGet(entity) != nullptr) {
        return "vehicle";
    }
    if (caches.lootContainer.TryGet(entity) != nullptr) {
        return "container";
    }
    if (caches.playerStorage.Contains(entity)) {
        return "storage";
    }
    if (caches.itemInstance.TryGet(entity) != nullptr) {
        return "item";
    }
    if (caches.animalCorpse.TryGet(entity) != nullptr ||
        caches.corpse.Contains(entity)) {
        return "corpse";
    }
    if (caches.fireSource.TryGet(entity) != nullptr) {
        return "fire";
    }
    return "other";
}

ScriptEntityInfo MakeEntityInfo(entt::entity entity, std::uint64_t id,
                                const EntityInfoCaches& caches) {
    ScriptEntityInfo out;
    out.id = id;
    if (const auto* position = caches.position.TryGet(entity);
        position != nullptr) {
        out.x = position->x;
        out.y = position->y;
        out.z = position->z;
    }
    const auto* health = caches.health.TryGet(entity);
    if (health != nullptr) {
        out.hasHealth = true;
        out.health = NonNegative(health->current);
        out.healthMax = NonNegative(health->max);
    }

    const std::string_view kind = EntityKindName(caches, entity);
    out.kind = kind;
    if (kind == "zombie") {
        if (const auto* variant = caches.zombieVariant.TryGet(entity);
            variant != nullptr) {
            out.subKind = ZombieVariantKey(variant->kind);
        }
    } else if (kind == "animal") {
        if (const auto* animal = caches.animal.TryGet(entity);
            animal != nullptr) {
            out.defId = animal->speciesId;
            out.subKind = AnimalKindKey(animal->kind);
        }
    } else if (kind == "companion") {
        if (const auto* companion = caches.companion.TryGet(entity);
            companion != nullptr) {
            out.defId = companion->defId;
            out.ownerId = companion->ownerStableId;
        }
        if (const auto* state = caches.companionState.TryGet(entity);
            state != nullptr) {
            out.downed = state->downed;
            out.dead = state->dead;
            out.command = game::CompanionCommandName(state->command);
        }
    } else if (kind == "npc") {
        if (const auto* npc = caches.settlementNpc.TryGet(entity);
            npc != nullptr) {
            out.defId = npc->settlementId;
            out.subKind = game::NpcActivityName(npc->activity);
        }
    } else if (kind == "vehicle") {
        if (const auto* vehicle = caches.vehicle.TryGet(entity);
            vehicle != nullptr) {
            out.subKind = VehicleTypeKey(vehicle->type);
        }
    } else if (kind == "container") {
        if (const auto* loot = caches.lootContainer.TryGet(entity);
            loot != nullptr) {
            out.tableId = loot->tableId;
            out.searched = loot->searched;
        }
    } else if (kind == "item") {
        if (const auto* item = caches.itemInstance.TryGet(entity);
            item != nullptr) {
            out.defId = item->defId;
            out.count = std::max(0, item->count);
        }
    } else if (kind == "corpse") {
        if (const auto* animalCorpse = caches.animalCorpse.TryGet(entity);
            animalCorpse != nullptr) {
            out.defId = animalCorpse->speciesId;
            out.subKind = "animal";
        }
    } else if (kind == "fire") {
        if (const auto* fire = caches.fireSource.TryGet(entity);
            fire != nullptr) {
            out.defId = fire->materialId;
            out.subKind = FirePhaseKey(fire->phase);
            out.ownerId = fire->ownerStableId;
            if (fire->phase == ecs::FirePhase::Extinguished) {
                out.dead = true;
            }
        }
    }

    if (health != nullptr && health->current <= 0.0f) {
        out.dead = true;
    }
    if (caches.dying.Contains(entity)) {
        out.dead = true;
    }
    return out;
}

ScriptEntityInfo MakeEntityInfo(ecs::EcsWorld& world, entt::entity entity) {
    const EntityInfoCaches caches(world.Registry());
    return MakeEntityInfo(entity, world.IdOf(entity).id, caches);
}
const ecs::ModData* TryGetModData(const ecs::EcsWorld& world) {
    const ecs::Entity entity = ecs::FindModDataEntity(world);
    if (entity == ecs::EcsWorld::kNull) {
        return nullptr;
    }
    return world.Registry().try_get<ecs::ModData>(entity);
}

const std::vector<ecs::ModDataEntry>* FindModDataEntries(
    const ecs::ModData& data, std::string_view modId) {
    for (const auto& [id, entries] : data.mods) {
        if (id == modId) {
            return &entries;
        }
    }
    return nullptr;
}

std::vector<ecs::ModDataEntry>* FindModDataEntries(ecs::ModData& data,
                                                   std::string_view modId) {
    for (auto& [id, entries] : data.mods) {
        if (id == modId) {
            return &entries;
        }
    }
    return nullptr;
}

const ecs::ModDataEntry* FindModDataEntry(
    const std::vector<ecs::ModDataEntry>& entries, std::string_view key) {
    for (const ecs::ModDataEntry& entry : entries) {
        if (entry.key == key) {
            return &entry;
        }
    }
    return nullptr;
}

ecs::ModDataEntry* FindModDataEntry(std::vector<ecs::ModDataEntry>& entries,
                                    std::string_view key) {
    for (ecs::ModDataEntry& entry : entries) {
        if (entry.key == key) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace

bool ScriptApi::IsMultiplayer() const {
    if (m_ctx == nullptr) {
        return false;
    }
    if (m_ctx->networkClient != nullptr) {
        return true;
    }
    return m_ctx->commands != nullptr &&
           m_ctx->commands->Mode() != net::TransportMode::Direct;
}

std::string ScriptApi::GetNetMode() const {
    if (m_ctx == nullptr) {
        return "direct";
    }
    if (m_ctx->networkClient != nullptr) {
        return "client";
    }
    if (m_ctx->commands != nullptr &&
        m_ctx->commands->Mode() == net::TransportMode::Loopback) {
        return "loopback";
    }
    return "direct";
}

std::int64_t ScriptApi::GetTick() const {
    if (m_ctx == nullptr || m_ctx->loop == nullptr) {
        return 0;
    }
    return static_cast<std::int64_t>(m_ctx->loop->TotalTicks());
}

ScriptActionResult ScriptApi::SubmitCommand(net::Command command) const {
    ScriptActionResult result;
    if (m_ctx == nullptr) {
        result.error = "脚本 API 未绑定游戏上下文";
        return result;
    }
    const std::uint64_t playerId = GetPlayerId();
    if (playerId == 0) {
        result.error = "当前没有玩家";
        return result;
    }
    if (!net::IsValidCommandType(command.type)) {
        result.error = "无效的玩家命令类型";
        return result;
    }
    if (!m_ctx->submitPlayerCommand) {
        result.error = "当前状态不支持玩家命令";
        return result;
    }
    command.playerStableId = playerId;
    command.tick = m_ctx->loop != nullptr ? m_ctx->loop->TotalTicks() : 0;
    bool queued = false;
    std::string error;
    if (!m_ctx->submitPlayerCommand(command, queued, error)) {
        result.error = error.empty() ? "玩家命令提交失败" : std::move(error);
        return result;
    }
    result.ok = true;
    result.queued = queued;
    return result;
}

bool ScriptApi::ValidateBagIndex(int index, bool allowDefault,
                                 std::string& error) const {
    if (allowDefault && index == -1) {
        const entt::entity player = FindPlayer();
        const auto* container =
            (player != entt::null && m_ctx != nullptr && m_ctx->ecs != nullptr)
                ? m_ctx->ecs->Registry().try_get<ecs::Container>(player)
                : nullptr;
        if (container == nullptr) {
            error = "玩家背包不可用";
            return false;
        }
        if (container->items.empty()) {
            error = "背包为空";
            return false;
        }
        return true;
    }
    if (index < 0) {
        error = "背包索引非法";
        return false;
    }
    if (m_ctx == nullptr || m_ctx->ecs == nullptr) {
        error = "ECS 未初始化";
        return false;
    }
    const entt::entity player = FindPlayer();
    if (player == entt::null) {
        error = "玩家尚未生成";
        return false;
    }
    const auto* container = m_ctx->ecs->Registry().try_get<ecs::Container>(player);
    if (container == nullptr) {
        error = "玩家背包不可用";
        return false;
    }
    if (static_cast<std::size_t>(index) >= container->items.size()) {
        error = "背包索引越界";
        return false;
    }
    return true;
}

bool ScriptApi::ParseEquipSlot(const std::string& slot, int& out) const {
    if (slot == "primary" || slot == "0") {
        out = 0;
        return true;
    }
    if (slot == "body" || slot == "1") {
        out = 1;
        return true;
    }
    return false;
}

bool ScriptApi::ValidateAttackTarget(std::uint64_t targetId,
                                     std::string& error) const {
    if (targetId == 0) {
        return true;
    }
    if (m_ctx == nullptr || m_ctx->ecs == nullptr) {
        error = "ECS 未初始化";
        return false;
    }
    const entt::entity player = FindPlayer();
    const entt::entity target = m_ctx->ecs->FindById(targetId);
    if (player == entt::null || target == entt::null || target == player ||
        !m_ctx->ecs->Alive(target)) {
        error = "攻击目标不可用";
        return false;
    }
    entt::registry& registry = m_ctx->ecs->Registry();
    const auto* position = registry.try_get<ecs::Position>(target);
    const auto* health = registry.try_get<ecs::Health>(target);
    const bool attackable = registry.all_of<ecs::AiState>(target) ||
                            registry.all_of<ecs::Animal>(target);
    if (!attackable || position == nullptr || health == nullptr ||
        !std::isfinite(health->current) || health->current <= 0.0f ||
        registry.all_of<ecs::Dying>(target) ||
        registry.all_of<ecs::Corpse>(target)) {
        error = "攻击目标不可用";
        return false;
    }
    if (const auto* ai = registry.try_get<ecs::AiState>(target);
        ai != nullptr && ai->status == ecs::AiStatus::Dead) {
        error = "攻击目标不可用";
        return false;
    }
    if (const auto* stance = registry.try_get<ecs::CombatStance>(target);
        stance != nullptr && std::isfinite(stance->dodgeInvulnLeft) &&
        stance->dodgeInvulnLeft > 0.0f) {
        error = "攻击目标处于无敌状态";
        return false;
    }
    return true;
}

ScriptActionResult ScriptApi::UseItem(int index) const {
    std::string error;
    if (!ValidateBagIndex(index, true, error)) {
        return MakeActionError(std::move(error));
    }
    net::Command command;
    command.type = net::CommandType::UseBag;
    command.payload.index = index;
    return SubmitCommand(command);
}

ScriptActionResult ScriptApi::EquipItem(int index, const std::string& slot) const {
    int slotIndex = 0;
    if (!ParseEquipSlot(slot, slotIndex)) {
        return MakeActionError("slot 仅支持 primary/body 或 0/1");
    }
    std::string error;
    if (!ValidateBagIndex(index, false, error)) {
        return MakeActionError(std::move(error));
    }
    net::Command command;
    command.type = net::CommandType::EquipBag;
    command.payload.index = index;
    command.payload.targetIndex = slotIndex;
    return SubmitCommand(command);
}

ScriptActionResult ScriptApi::Unequip(const std::string& slot) const {
    int slotIndex = 0;
    if (!ParseEquipSlot(slot, slotIndex)) {
        return MakeActionError("slot 仅支持 primary/body 或 0/1");
    }
    net::Command command;
    command.type = net::CommandType::Unequip;
    command.payload.index = slotIndex;
    return SubmitCommand(command);
}

ScriptActionResult ScriptApi::DropItem(int index) const {
    std::string error;
    if (!ValidateBagIndex(index, true, error)) {
        return MakeActionError(std::move(error));
    }
    net::Command command;
    command.type = net::CommandType::DropBag;
    command.payload.index = index;
    return SubmitCommand(command);
}

ScriptActionResult ScriptApi::PickupNearest() const {
    net::Command command;
    command.type = net::CommandType::PickupNearest;
    return SubmitCommand(command);
}

ScriptActionResult ScriptApi::Pickup(std::uint64_t entityId) const {
    if (entityId == 0) {
        return MakeActionError("地面物品 ID 非法");
    }
    net::Command command;
    command.type = net::CommandType::PickupEntity;
    command.payload.entityId = entityId;
    return SubmitCommand(command);
}

ScriptActionResult ScriptApi::Attack(std::uint64_t targetId, bool heavy) const {
    std::string error;
    if (!ValidateAttackTarget(targetId, error)) {
        return MakeActionError(std::move(error));
    }
    net::Command command;
    command.type = net::CommandType::Attack;
    command.payload.entityId = targetId;
    command.payload.flags = heavy ? net::kAttackHeavyFlag : 0;
    return SubmitCommand(command);
}

ScriptActionResult ScriptApi::ApplyDamage(float amount,
                                          const std::string& damageType) const {
    if (IsMultiplayer()) {
        return MakeActionError("联机模式不支持直接状态效果");
    }
    ecs::DamageType type = ecs::DamageType::Blunt;
    if (!ParseDamageType(damageType, type)) {
        return MakeActionError("damageType 仅支持 blunt/bite/claw/fire/debug");
    }
    if (!std::isfinite(amount) || amount <= 0.0f) {
        return MakeActionError("伤害量必须是有限正数");
    }
    if (m_ctx == nullptr || m_ctx->ecs == nullptr || m_ctx->combat == nullptr) {
        return MakeActionError("战斗系统不可用");
    }
    const entt::entity player = FindPlayer();
    if (player == entt::null) {
        return MakeActionError("玩家尚未生成");
    }
    const auto* health = m_ctx->ecs->Registry().try_get<ecs::Health>(player);
    if (health == nullptr) {
        return MakeActionError("玩家生命组件不可用");
    }
    if (health->current <= 0.0f) {
        return MakeActionError("玩家已死亡");
    }
    ecs::DamageEvent event;
    event.sourceId = 0;
    event.type = type;
    event.amount = amount;
    SetDamageFromFront(
        m_ctx->ecs->Registry().try_get<ecs::Facing>(player), event);
    (void)m_ctx->combat->ApplyDamage(*m_ctx->ecs, player, event);
    return ScriptActionResult{true, false, {}};
}

ScriptActionResult ScriptApi::AdjustNeeds(float hunger, float thirst,
                                          float fatigue) const {
    if (IsMultiplayer()) {
        return MakeActionError("联机模式不支持直接状态效果");
    }
    const float values[] = {hunger, thirst, fatigue};
    for (float value : values) {
        if (!std::isfinite(value) || value < 0.0f) {
            return MakeActionError("需求增量必须是非负有限数");
        }
    }
    if (m_ctx == nullptr || m_ctx->ecs == nullptr) {
        return MakeActionError("ECS 未初始化");
    }
    const entt::entity player = FindPlayer();
    if (player == entt::null) {
        return MakeActionError("玩家尚未生成");
    }
    auto* needs = m_ctx->ecs->Registry().try_get<ecs::Needs>(player);
    if (needs == nullptr) {
        return MakeActionError("玩家需求组件不可用");
    }
    needs->hunger =
        std::clamp(FiniteOr(needs->hunger, 0.0f) + hunger, 0.0f, 1.0f);
    needs->thirst =
        std::clamp(FiniteOr(needs->thirst, 0.0f) + thirst, 0.0f, 1.0f);
    needs->fatigue =
        std::clamp(FiniteOr(needs->fatigue, 0.0f) + fatigue, 0.0f, 1.0f);
    return ScriptActionResult{true, false, {}};
}

ScriptXpResult ScriptApi::GiveXp(const std::string& skill, float amount) const {
    if (IsMultiplayer()) {
        return MakeXpError("联机模式不支持直接状态效果");
    }
    if (!std::isfinite(amount) || amount <= 0.0f) {
        return MakeXpError("经验值必须是有限正数");
    }
    ecs::SkillKind kind = ecs::SkillKind::Survival;
    if (!ParseSkillKind(skill, kind)) {
        return MakeXpError("skill 仅支持 melee/building/crafting/survival");
    }
    if (m_ctx == nullptr || m_ctx->ecs == nullptr ||
        m_ctx->progressionConfig == nullptr) {
        return MakeXpError("成长系统不可用");
    }
    if (!m_ctx->progressionConfig->enabled) {
        return MakeXpError("成长系统未启用");
    }
    const entt::entity player = FindPlayer();
    if (player == entt::null) {
        return MakeXpError("玩家尚未生成");
    }
    auto* progression =
        m_ctx->ecs->Registry().try_get<ecs::Progression>(player);
    if (progression == nullptr) {
        return MakeXpError("玩家成长组件不可用");
    }
    const game::ProgressionAward award =
        game::AwardXp(*progression, kind, amount, *m_ctx->progressionConfig);
    game::RefreshDerived(*progression, *m_ctx->progressionConfig);
    ScriptXpResult out;
    out.result = ScriptActionResult{true, false, {}};
    out.award.characterLevels = std::max(0, award.characterLevels);
    out.award.skillLevels = std::max(0, award.skillLevels);
    out.award.perkPoints = std::max(0, award.perkPoints);
    out.award.characterXp =
        std::isfinite(award.characterXp) ? std::max(0.0f, award.characterXp)
                                         : 0.0f;
    out.award.skillXp = std::isfinite(award.skillXp)
                            ? std::max(0.0f, award.skillXp)
                            : 0.0f;
    return out;
}

ScriptActionResult ScriptApi::StartCraft(const std::string& recipeId,
                                         int count) const {
    if (count < 1 || count > 99) {
        return MakeActionError("制作数量必须在 1~99 之间");
    }
    if (m_ctx == nullptr || m_ctx->ecs == nullptr ||
        m_ctx->items == nullptr || m_ctx->recipes == nullptr) {
        return MakeActionError("制作系统不可用");
    }
    const items::RecipeDef* recipe = m_ctx->recipes->Find(recipeId);
    if (recipe == nullptr) {
        return MakeActionError("配方不存在");
    }
    if (!IsRecipeKnown(recipeId)) {
        return MakeActionError("尚未学会该配方");
    }
    const entt::entity player = FindPlayer();
    if (player == entt::null) {
        return MakeActionError("玩家尚未生成");
    }
    if (!recipe->station.empty()) {
        bool stationNearby = false;
        if (m_ctx->craftStationNearby) {
            stationNearby = m_ctx->craftStationNearby(*recipe);
        } else {
            ui::InventoryState uiState;
            if (m_ctx->getInventoryState && m_ctx->getInventoryState(uiState)) {
                for (const ui::InventoryRecipe& entry : uiState.recipes) {
                    if (entry.id == recipeId) {
                        stationNearby = entry.stationNearby;
                        break;
                    }
                }
            }
        }
        if (!stationNearby) {
            return MakeActionError("需要附近制作站：" + recipe->station);
        }
    }
    const auto* container =
        m_ctx->ecs->Registry().try_get<ecs::Container>(player);
    if (container == nullptr) {
        return MakeActionError("玩家背包不可用");
    }
    std::string reason;
    if (!items::CanCraft(*container, *m_ctx->items, *recipe, &reason)) {
        if (reason.empty()) {
            reason = "材料或背包空间不足";
        }
        return MakeActionError(std::move(reason));
    }
    std::size_t recipeIndex = 0;
    bool found = false;
    const std::vector<items::RecipeDef>& all = m_ctx->recipes->All();
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i].id == recipeId) {
            recipeIndex = i;
            found = true;
            break;
        }
    }
    if (!found) {
        return MakeActionError("配方索引不可用");
    }
    net::Command command;
    command.type = net::CommandType::InventoryAction;
    command.payload.flags =
        static_cast<std::uint16_t>(ui::InventoryActionType::Craft) |
        static_cast<std::uint16_t>(count << 8);
    command.payload.area =
        static_cast<std::uint8_t>(ui::InventoryArea::Backpack);
    command.payload.index = static_cast<int>(recipeIndex);
    return SubmitCommand(command);
}

ScriptActionResult ScriptApi::CancelCraft() const {
    if (m_ctx == nullptr || m_ctx->ecs == nullptr) {
        return MakeActionError("ECS 未初始化");
    }
    const entt::entity player = FindPlayer();
    if (player == entt::null) {
        return MakeActionError("玩家尚未生成");
    }
    ScriptCraftState state;
    bool active = GetCraftState(state) && (state.active || !state.queue.empty());
    if (!active) {
        const auto* job =
            m_ctx->ecs->Registry().try_get<ecs::CraftingJob>(player);
        active = job != nullptr && (job->active || !job->queue.empty());
    }
    if (!active) {
        return MakeActionError("没有正在制作的项目");
    }
    net::Command command;
    command.type = net::CommandType::InventoryAction;
    command.payload.flags =
        static_cast<std::uint16_t>(ui::InventoryActionType::CancelCraft);
    command.payload.area =
        static_cast<std::uint8_t>(ui::InventoryArea::Backpack);
    command.payload.index = -1;
    return SubmitCommand(command);
}

ScriptActionResult ScriptApi::CanPlaceBuild(const std::string& defId, int wx,
                                            int wy, bool vertical) const {
    if (m_ctx == nullptr) {
        return MakeActionError("脚本 API 未绑定游戏上下文");
    }
    if (IsMultiplayer()) {
        return MakeActionError("联机模式请直接调用 Build.Place，由服务器校验");
    }
    if (GetPlayerId() == 0) {
        return MakeActionError("当前没有玩家");
    }
    game::BuildableDef def;
    int index = 0;
    if (!FindBuildableDef(*m_ctx, defId, def, index)) {
        return MakeActionError("建造项不存在");
    }
    if (vertical && !game::BuildableSupportsVertical(def)) {
        return MakeActionError("该建造项不支持竖向");
    }
    if (!m_ctx->buildCanPlace) {
        return MakeActionError("当前状态不支持建造预检");
    }
    std::string error;
    if (!m_ctx->buildCanPlace(def, wx, wy, vertical, error)) {
        return MakeActionError(error.empty() ? "无法在该位置建造" : error);
    }
    return ScriptActionResult{true, false, {}};
}

ScriptActionResult ScriptApi::PlaceBuild(const std::string& defId, int wx,
                                         int wy, bool vertical) const {
    if (m_ctx == nullptr) {
        return MakeActionError("脚本 API 未绑定游戏上下文");
    }
    if (GetPlayerId() == 0) {
        return MakeActionError("当前没有玩家");
    }
    game::BuildableDef def;
    int index = 0;
    if (!FindBuildableDef(*m_ctx, defId, def, index)) {
        return MakeActionError("建造项不存在");
    }
    if (vertical && !game::BuildableSupportsVertical(def)) {
        return MakeActionError("该建造项不支持竖向");
    }
    if (IsMultiplayer()) {
        net::Command command;
        command.type = net::CommandType::PlaceStructure;
        command.payload.x = static_cast<float>(wx);
        command.payload.y = static_cast<float>(wy);
        command.payload.index = index;
        command.payload.flags = vertical ? std::uint16_t{1} : std::uint16_t{0};
        return SubmitCommand(command);
    }
    if (!m_ctx->buildPlace) {
        return MakeActionError("当前状态不支持建造动作");
    }
    std::string error;
    if (!m_ctx->buildPlace(def, wx, wy, vertical, error)) {
        return MakeActionError(error.empty() ? "建造失败" : error);
    }
    return ScriptActionResult{true, false, {}};
}

ScriptActionResult ScriptApi::DemolishBuild(int wx, int wy) const {
    if (m_ctx == nullptr) {
        return MakeActionError("脚本 API 未绑定游戏上下文");
    }
    if (GetPlayerId() == 0) {
        return MakeActionError("当前没有玩家");
    }
    if (IsMultiplayer()) {
        net::Command command;
        command.type = net::CommandType::DemolishStructure;
        command.payload.x = static_cast<float>(wx);
        command.payload.y = static_cast<float>(wy);
        return SubmitCommand(command);
    }
    if (!m_ctx->buildDemolish) {
        return MakeActionError("当前状态不支持拆除动作");
    }
    std::string error;
    if (!m_ctx->buildDemolish(wx, wy, error)) {
        return MakeActionError(error.empty() ? "拆除失败" : error);
    }
    return ScriptActionResult{true, false, {}};
}

ScriptActionResult ScriptApi::RepairBuild(int wx, int wy) const {
    if (m_ctx == nullptr) {
        return MakeActionError("脚本 API 未绑定游戏上下文");
    }
    if (GetPlayerId() == 0) {
        return MakeActionError("当前没有玩家");
    }
    if (IsMultiplayer()) {
        net::Command command;
        command.type = net::CommandType::RepairStructure;
        command.payload.x = static_cast<float>(wx);
        command.payload.y = static_cast<float>(wy);
        return SubmitCommand(command);
    }
    if (!m_ctx->buildRepair) {
        return MakeActionError("当前状态不支持维修动作");
    }
    std::string error;
    if (!m_ctx->buildRepair(wx, wy, error)) {
        return MakeActionError(error.empty() ? "维修失败" : error);
    }
    return ScriptActionResult{true, false, {}};
}

ScriptActionResult ScriptApi::ToggleBuildDoor(int wx, int wy) const {
    if (m_ctx == nullptr) {
        return MakeActionError("脚本 API 未绑定游戏上下文");
    }
    if (GetPlayerId() == 0) {
        return MakeActionError("当前没有玩家");
    }
    if (IsMultiplayer()) {
        net::Command command;
        command.type = net::CommandType::ToggleDoor;
        command.payload.x = static_cast<float>(wx);
        command.payload.y = static_cast<float>(wy);
        return SubmitCommand(command);
    }
    if (!m_ctx->buildToggleDoor) {
        return MakeActionError("当前状态不支持开关门动作");
    }
    std::string error;
    if (!m_ctx->buildToggleDoor(wx, wy, error)) {
        return MakeActionError(error.empty() ? "开关门失败" : error);
    }
    return ScriptActionResult{true, false, {}};
}

ScriptActionResult ScriptApi::LearnRecipe(const std::string& recipeId) const {
    if (IsMultiplayer()) {
        return MakeActionError("联机模式不支持直接学习配方");
    }
    if (m_ctx == nullptr || m_ctx->ecs == nullptr || m_ctx->recipes == nullptr) {
        return MakeActionError("制作系统不可用");
    }
    if (m_ctx->recipes->Find(recipeId) == nullptr) {
        return MakeActionError("配方不存在");
    }
    if (IsRecipeKnown(recipeId)) {
        return MakeActionError("该配方已学会");
    }
    const entt::entity player = FindPlayer();
    if (player == entt::null) {
        return MakeActionError("玩家尚未生成");
    }
    auto& known = m_ctx->ecs->Registry().get_or_emplace<ecs::KnownRecipes>(player);
    known.recipeIds.push_back(recipeId);
    std::sort(known.recipeIds.begin(), known.recipeIds.end());
    known.recipeIds.erase(
        std::unique(known.recipeIds.begin(), known.recipeIds.end()),
        known.recipeIds.end());
    return ScriptActionResult{true, false, {}};
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

std::optional<std::string> ScriptApi::GetModData(
    const std::string& modId, const std::string& key) const {
    if (m_ctx == nullptr || m_ctx->ecs == nullptr || IsMultiplayer() ||
        !ecs::IsValidModDataId(modId) || !ecs::IsValidModDataKey(key)) {
        return std::nullopt;
    }
    const ecs::ModData* data = TryGetModData(*m_ctx->ecs);
    if (data == nullptr) {
        return std::nullopt;
    }
    const std::vector<ecs::ModDataEntry>* entries =
        FindModDataEntries(*data, modId);
    if (entries == nullptr) {
        return std::nullopt;
    }
    for (const ecs::ModDataEntry& entry : *entries) {
        if (entry.key == key) {
            return entry.value;
        }
    }
    return std::nullopt;
}

bool ScriptApi::SetModData(const std::string& modId, const std::string& key,
                           const std::string& value) const {
    if (m_ctx == nullptr || m_ctx->ecs == nullptr || IsMultiplayer() ||
        !ecs::IsValidModDataId(modId) || !ecs::IsValidModDataKey(key) ||
        value.size() > ecs::kMaxModDataValueBytes) {
        return false;
    }

    ecs::ModData updated;
    if (const ecs::ModData* data = TryGetModData(*m_ctx->ecs);
        data != nullptr) {
        updated = *data;
    }

    std::vector<ecs::ModDataEntry>* entries =
        FindModDataEntries(updated, modId);
    if (entries == nullptr) {
        if (updated.mods.size() >= ecs::kMaxModDataMods) {
            return false;
        }
        updated.mods.push_back({modId, {}});
        entries = &updated.mods.back().second;
    }

    ecs::ModDataEntry* entry = FindModDataEntry(*entries, key);
    if (entry != nullptr) {
        entry->value = value;
    } else {
        if (entries->size() >= ecs::kMaxModDataEntriesPerMod) {
            return false;
        }
        entries->push_back(ecs::ModDataEntry{key, value});
    }

    if (!ecs::IsValidModData(updated)) {
        return false;
    }
    ecs::ModData& storage = ecs::EnsureModData(*m_ctx->ecs);
    storage = std::move(updated);
    return true;
}

bool ScriptApi::RemoveModData(const std::string& modId,
                              const std::string& key) const {
    if (m_ctx == nullptr || m_ctx->ecs == nullptr || IsMultiplayer() ||
        !ecs::IsValidModDataId(modId) || !ecs::IsValidModDataKey(key)) {
        return false;
    }
    const ecs::ModData* data = TryGetModData(*m_ctx->ecs);
    if (data == nullptr) {
        return false;
    }

    ecs::ModData updated = *data;
    for (std::size_t i = 0; i < updated.mods.size(); ++i) {
        if (updated.mods[i].first != modId) {
            continue;
        }
        std::vector<ecs::ModDataEntry>& entries = updated.mods[i].second;
        for (std::size_t j = 0; j < entries.size(); ++j) {
            if (entries[j].key != key) {
                continue;
            }
            entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(j));
            if (entries.empty()) {
                updated.mods.erase(updated.mods.begin() +
                                   static_cast<std::ptrdiff_t>(i));
            }
            ecs::ModData& storage = ecs::EnsureModData(*m_ctx->ecs);
            storage = std::move(updated);
            return true;
        }
        return false;
    }
    return false;
}

std::vector<std::string> ScriptApi::ListModDataKeys(
    const std::string& modId) const {
    std::vector<std::string> keys;
    if (m_ctx == nullptr || m_ctx->ecs == nullptr || IsMultiplayer() ||
        !ecs::IsValidModDataId(modId)) {
        return keys;
    }
    const ecs::ModData* data = TryGetModData(*m_ctx->ecs);
    if (data == nullptr) {
        return keys;
    }
    const std::vector<ecs::ModDataEntry>* entries =
        FindModDataEntries(*data, modId);
    if (entries == nullptr) {
        return keys;
    }
    keys.reserve(entries->size());
    for (const ecs::ModDataEntry& entry : *entries) {
        keys.push_back(entry.key);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

bool ScriptApi::ClearModData(const std::string& modId) const {
    if (m_ctx == nullptr || m_ctx->ecs == nullptr || IsMultiplayer() ||
        !ecs::IsValidModDataId(modId)) {
        return false;
    }
    const ecs::ModData* data = TryGetModData(*m_ctx->ecs);
    if (data == nullptr) {
        return true;
    }

    ecs::ModData updated = *data;
    updated.mods.erase(
        std::remove_if(updated.mods.begin(), updated.mods.end(),
                       [&modId](const auto& item) {
                           return item.first == modId;
                       }),
        updated.mods.end());
    ecs::ModData& storage = ecs::EnsureModData(*m_ctx->ecs);
    storage = std::move(updated);
    return true;
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

bool ScriptApi::GetPlayerNeeds(ScriptNeeds& out) const {
    const entt::entity player = FindPlayer();
    if (player == entt::null) {
        return false;
    }
    const entt::registry& registry = m_ctx->ecs->Registry();
    const auto* needs = registry.try_get<ecs::Needs>(player);
    if (needs == nullptr) {
        return false;
    }
    out = ScriptNeeds{};
    out.hunger = ClampUnit(needs->hunger);
    out.thirst = ClampUnit(needs->thirst);
    out.fatigue = ClampUnit(needs->fatigue);
    out.moveScale = ClampUnit(needs->moveScale, 1.0f);
    out.damageScale = ClampUnit(needs->damageScale, 1.0f);
    out.warning = static_cast<int>(needs->warning);
    if (const auto* food = registry.try_get<ecs::FoodState>(player);
        food != nullptr) {
        out.nutrition = ClampUnit(food->nutrition);
    }
    if (const auto* health = registry.try_get<ecs::Health>(player);
        health != nullptr) {
        out.health = NonNegative(health->current);
        out.healthMax = NonNegative(health->max);
    }
    return true;
}

bool ScriptApi::GetPlayerStamina(ScriptStamina& out) const {
    const entt::entity player = FindPlayer();
    if (player == entt::null) {
        return false;
    }
    const auto* stamina =
        m_ctx->ecs->Registry().try_get<ecs::CombatStamina>(player);
    if (stamina == nullptr) {
        return false;
    }
    out = ScriptStamina{};
    out.max = NonNegative(stamina->max);
    out.current = std::min(NonNegative(stamina->current), out.max);
    out.exhausted = FiniteOr(stamina->exhausted, 0.0f) > 0.0f;
    return true;
}

bool ScriptApi::GetPlayerWounds(ScriptWounds& out) const {
    const entt::entity player = FindPlayer();
    if (player == entt::null) {
        return false;
    }
    const auto* wounds = m_ctx->ecs->Registry().try_get<ecs::Wounds>(player);
    if (wounds == nullptr) {
        return false;
    }
    out = ScriptWounds{};
    const std::uint8_t count =
        std::min<std::uint8_t>(wounds->count, ecs::kMaxWounds);
    out.wounds.reserve(count);
    for (std::uint8_t i = 0; i < count; ++i) {
        const ecs::WoundEntry& entry = wounds->entries[i];
        ScriptWound wound;
        wound.type = DamageTypeKey(entry.source);
        wound.severity = ClampUnit(entry.severity);
        wound.bandaged = entry.bandaged;
        wound.bleeding =
            NonNegative(entry.bleedRate) > 0.0f &&
            NonNegative(entry.bleedRemaining) > 0.0f;
        out.wounds.push_back(std::move(wound));
    }
    out.count = static_cast<int>(out.wounds.size());
    return true;
}

bool ScriptApi::GetPlayerPain(ScriptPain& out) const {
    const entt::entity player = FindPlayer();
    if (player == entt::null) {
        return false;
    }
    const auto* pain = m_ctx->ecs->Registry().try_get<ecs::Pain>(player);
    if (pain == nullptr) {
        return false;
    }
    out.level = ClampUnit(pain->level);
    out.baseLevel = ClampUnit(pain->baseLevel);
    out.reliefRemaining = NonNegative(pain->reliefRemaining);
    return true;
}

bool ScriptApi::GetPlayerInfection(ScriptInfection& out) const {
    const entt::entity player = FindPlayer();
    if (player == entt::null) {
        return false;
    }
    const auto* infection =
        m_ctx->ecs->Registry().try_get<ecs::Infection>(player);
    if (infection == nullptr) {
        return false;
    }
    out.stage = InfectionStageKey(infection->stage);
    out.progress = ClampUnit(infection->progress);
    out.slowRemaining = NonNegative(infection->slowRemaining);
    out.timeToNextStage = NonNegative(infection->timeToNextStage);
    return true;
}

bool ScriptApi::GetPlayerClimate(ScriptClimate& out) const {
    const entt::entity player = FindPlayer();
    if (player == entt::null) {
        return false;
    }
    const auto* climate = m_ctx->ecs->Registry().try_get<ecs::Climate>(player);
    if (climate == nullptr) {
        return false;
    }
    out.bodyC = FiniteOr(climate->bodyC, 0.0f);
    out.wetness = ClampUnit(climate->wetness);
    out.ambientC = FiniteOr(climate->ambientC, 0.0f);
    out.effectiveC = FiniteOr(climate->effectiveC, 0.0f);
    out.insulation = ClampUnit(climate->insulation);
    out.waterproof = ClampUnit(climate->waterproof);
    out.indoor = climate->indoor;
    out.nearFire = climate->nearFire;
    out.warning = static_cast<int>(climate->warning);
    return true;
}

bool ScriptApi::GetPlayerProgression(ScriptProgression& out) const {
    const entt::entity player = FindPlayer();
    if (player == entt::null || m_ctx == nullptr ||
        m_ctx->progressionConfig == nullptr) {
        return false;
    }
    const auto* progression =
        m_ctx->ecs->Registry().try_get<ecs::Progression>(player);
    if (progression == nullptr) {
        return false;
    }
    const game::ProgressionConfig& config = *m_ctx->progressionConfig;
    out = ScriptProgression{};
    out.level = std::max(0, progression->level);
    out.xp = NonNegative(progression->xp);
    out.xpNext = NonNegative(game::CharacterXpToNext(*progression, config));
    out.perkPoints = std::max(0, progression->perkPoints);
    out.perks = progression->perks;
    out.skills.reserve(ecs::kSkillCount);
    for (std::size_t i = 0; i < ecs::kSkillCount; ++i) {
        const ecs::SkillKind kind = static_cast<ecs::SkillKind>(i);
        ScriptSkillProgress skill;
        skill.id = ecs::kSkillIds[i];
        skill.level = std::max(0, progression->skills[i].level);
        skill.xp = NonNegative(progression->skills[i].xp);
        skill.xpNext = NonNegative(game::SkillXpToNext(*progression, kind, config));
        out.skills.push_back(std::move(skill));
    }
    return true;
}

bool ScriptApi::GetPlayerInventory(int offset, int limit,
                                   ScriptInventory& out) const {
    const entt::entity player = FindPlayer();
    if (player == entt::null) {
        return false;
    }
    const auto* bag =
        m_ctx->ecs->Registry().try_get<ecs::Container>(player);
    if (bag == nullptr) {
        return false;
    }
    out = ScriptInventory{};
    out.weightKg = m_ctx->items != nullptr
                      ? items::TotalWeight(*bag, *m_ctx->items)
                      : 0.0f;
    out.capacityKg = NonNegative(bag->capacityKg);
    out.totalStacks = static_cast<int>(bag->items.size());

    const std::size_t start = static_cast<std::size_t>(std::max(0, offset));
    const int requested = limit > 0 ? limit : 64;
    const int capped = std::clamp(requested, 1, 128);
    if (start >= bag->items.size()) {
        out.truncated = false;
        return true;
    }
    const std::size_t available = bag->items.size() - start;
    const std::size_t count =
        std::min<std::size_t>(available, static_cast<std::size_t>(capped));
    out.stacks.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t index = start + i;
        out.stacks.push_back(MakeItemStack(bag->items[index],
                                           static_cast<int>(index),
                                           m_ctx->items));
    }
    out.truncated = count < available;
    return true;
}

bool ScriptApi::GetPlayerEquipment(ScriptEquipment& out) const {
    const entt::entity player = FindPlayer();
    if (player == entt::null) {
        return false;
    }
    out = ScriptEquipment{};
    const auto* equipment =
        m_ctx->ecs->Registry().try_get<ecs::Equipment>(player);
    if (equipment == nullptr) {
        return true;
    }
    if (equipment->slots[0].has_value()) {
        out.hasPrimary = true;
        out.primary = MakeItemStack(*equipment->slots[0], -1, m_ctx->items);
    }
    if (equipment->slots[1].has_value()) {
        out.hasBody = true;
        out.body = MakeItemStack(*equipment->slots[1], -1, m_ctx->items);
    }
    return true;
}

int ScriptApi::CountPlayerItem(const std::string& defId) const {
    const entt::entity player = FindPlayer();
    if (player == entt::null || defId.empty()) {
        return 0;
    }
    const auto* bag =
        m_ctx->ecs->Registry().try_get<ecs::Container>(player);
    if (bag == nullptr) {
        return 0;
    }
    return items::CountOf(*bag, defId);
}

bool ScriptApi::HasPlayerItem(const std::string& defId, int count) const {
    return count > 0 && CountPlayerItem(defId) >= count;
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

bool ScriptApi::EmitSound(float x, float y, float radius,
                          const std::string& kind,
                          const std::string& clip) const {
    if (IsMultiplayer()) {
        return false;
    }
    if (m_ctx == nullptr || !std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(radius) || radius <= 0.0f || radius > 256.0f) {
        return false;
    }
    core::SoundType type = core::SoundType::Generic;
    if (!ParseSoundKind(kind, type)) {
        ZL_WARN(kModule, "EmitSound：未知声音类型 kind={}", kind);
        return false;
    }
    core::SoundEvent sound;
    sound.x = x;
    sound.y = y;
    sound.radius = radius;
    sound.type = type;
    sound.clip = clip;
    if (m_ctx->events == nullptr) {
        return false;
    }
    if (m_ctx->audio != nullptr) {
        m_ctx->audio->EmitWorldSound(std::move(sound));
    } else {
        m_ctx->events->EmitQueued(core::kOnSound, std::move(sound));
    }
    return true;
}

bool ScriptApi::PlayAudio2D(const std::string& clip, float volume) const {
    if (m_ctx == nullptr || m_ctx->audio == nullptr ||
        !m_ctx->audio->IsReady() || !IsValidAudioClip(clip) ||
        !std::isfinite(volume)) {
        return false;
    }
    const float clamped = std::clamp(volume, 0.0f, 1.0f);
    return m_ctx->audio->Engine()
        .Play2D(clip, audio::AudioGroup::SFX, clamped)
        .Valid();
}

bool ScriptApi::PlayAudio3D(const std::string& clip, float x, float y,
                            float radius, float volume) const {
    if (m_ctx == nullptr || m_ctx->audio == nullptr ||
        !m_ctx->audio->IsReady() || !IsValidAudioClip(clip) ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius) ||
        radius < 0.0f || radius > 256.0f || !std::isfinite(volume)) {
        return false;
    }
    const float clamped = std::clamp(volume, 0.0f, 1.0f);
    return m_ctx->audio->Engine()
        .Play3D(clip, glm::vec2(x, y), audio::AudioGroup::SFX, clamped, false,
                radius)
        .Valid();
}

bool ScriptApi::GetWeather(ScriptWeather& out) const {
    out = ScriptWeather{};
    out.kind = "clear";
    out.ambientC = 24.0f;
    if (m_ctx == nullptr) {
        return false;
    }
    if (m_ctx->gameTime != nullptr) {
        out.isNight = m_ctx->gameTime->IsNight();
    }
    if (m_ctx->simulation != nullptr) {
        const game::WeatherState weather = m_ctx->simulation->CurrentWeather();
        out.kind = game::WeatherSystem::KindClass(weather.kind);
        out.ambientC = FiniteOr(weather.ambientC, 24.0f);
        out.isNight = weather.night;
    }
    return true;
}

float ScriptApi::GetTimeOfDay() const {
    if (m_ctx == nullptr || m_ctx->gameTime == nullptr) {
        return 0.0f;
    }
    return ClampUnit(m_ctx->gameTime->DayFraction());
}

bool ScriptApi::GetEntity(std::uint64_t id, ScriptEntityInfo& out) const {
    if (m_ctx == nullptr || m_ctx->ecs == nullptr || id == 0) {
        return false;
    }
    const entt::entity entity = m_ctx->ecs->FindById(id);
    if (entity == entt::null || !m_ctx->ecs->Alive(entity)) {
        return false;
    }
    out = MakeEntityInfo(*m_ctx->ecs, entity);
    return true;
}

bool ScriptApi::QueryEntities(float x, float y, int z, float radius,
                              const std::string& kind, int limit,
                              ScriptEntityQuery& out) const {
    return QueryEntitiesForMod(std::string(), x, y, z, radius, kind, limit, out);
}

bool ScriptApi::QueryEntitiesForMod(const std::string& modId, float x, float y,
                                    int z, float radius,
                                    const std::string& kind, int limit,
                                    ScriptEntityQuery& out) const {
    out = ScriptEntityQuery{};
    if (m_ctx == nullptr || m_ctx->ecs == nullptr ||
        !std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    const float safeRadius =
        std::isfinite(radius) ? std::clamp(radius, 0.0f, 32.0f) : 8.0f;
    const int safeLimit = std::clamp(limit > 0 ? limit : 32, 1, 128);
    const float radiusSq = safeRadius * safeRadius;
    const std::string filter =
        kind.empty() || kind == "any" ? std::string() : kind;

    // 排序只处理轻量候选；ScriptEntityInfo（含多个 string）仅对最终
    // limit 条构造，避免 100+ 实体时把大头花在无用的快照和搬运上。
    struct Candidate {
        entt::entity entity = entt::null;
        std::uint64_t id = 0;
        float distanceSq = 0.0f;
    };
    std::vector<Candidate> candidates;
    const entt::registry& registry = m_ctx->ecs->Registry();
    const EntityInfoCaches caches(registry);
    const auto consider = [&](entt::entity entity) {
        if (entity == entt::null || !m_ctx->ecs->Alive(entity)) {
            return;
        }
        const auto* position = caches.position.TryGet(entity);
        if (position == nullptr || position->z != z) {
            return;
        }
        const float dx = position->x - x;
        const float dy = position->y - y;
        const float distanceSq = dx * dx + dy * dy;
        if (distanceSq > radiusSq) {
            return;
        }
        const auto* stableId = caches.stableId.TryGet(entity);
        if (stableId == nullptr || stableId->id == 0) {
            return;
        }
        if (!filter.empty() && EntityKindName(caches, entity) != filter) {
            return;
        }
        candidates.push_back(Candidate{entity, stableId->id, distanceSq});
    };

    if (m_ctx->spatialIndex != nullptr) {
        const int minWx = static_cast<int>(std::floor(x - safeRadius));
        const int maxWx = static_cast<int>(std::floor(x + safeRadius));
        const int minWy = static_cast<int>(std::floor(y - safeRadius));
        const int maxWy = static_cast<int>(std::floor(y + safeRadius));
        const world::ChunkCoord minChunk = world::ChunkOf(minWx, minWy);
        const world::ChunkCoord maxChunk = world::ChunkOf(maxWx, maxWy);
        m_ctx->spatialIndex->ForEachInChunks(
            minChunk.cx, minChunk.cy, maxChunk.cx, maxChunk.cy,
            [&](const ecs::Entity entity) { consider(entity); });
    } else {
        m_ctx->ecs->Registry().view<ecs::Position>().each(
            [&](const entt::entity entity, const ecs::Position&) {
                consider(entity);
            });
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  if (a.distanceSq != b.distanceSq) {
                      return a.distanceSq < b.distanceSq;
                  }
                  return a.id < b.id;
              });
    const std::size_t available = candidates.size();
    out.entities.reserve(
        std::min<std::size_t>(available, static_cast<std::size_t>(safeLimit)));
    std::size_t visited = 0;
    while (visited < candidates.size() &&
           out.entities.size() < static_cast<std::size_t>(safeLimit)) {
        ScriptEntityInfo info =
            MakeEntityInfo(candidates[visited].entity, candidates[visited].id,
                           caches);
        ++visited;
        if (info.id == 0 || info.dead) {
            continue;
        }
        out.entities.push_back(std::move(info));
    }
    // 仍在 limit 内遍历完 = 没有更多匹配；否则保守标记截断。
    out.truncated = visited < candidates.size();
    out.count = static_cast<int>(out.entities.size());

    // F5-T4：统计归属到发起查询的 mod；截断日志每 mod 每 tick 只打一条，
    // 高频调用仍继续计数（只观测，不额外限流）。
    const std::string owner = modId.empty() ? "<direct>" : modId;
    QueryTickStats& stats = m_queryStats[owner];
    const std::int64_t tick = GetTick();
    if (stats.tick != tick) {
        stats = QueryTickStats{};
        stats.tick = tick;
    }
    ++stats.queries;
    if (out.truncated) {
        if (stats.truncated == 0) {
            const std::string filterName = kind.empty() ? "any" : kind;
            ZL_WARN(kModule,
                    "[{}] Entities.Query 截断：匹配 {} 条，limit={}，返回 {} 条"
                    "（tick={}，radius={:.1f}，kind={}；本 tick 后续截断仅计数）",
                    owner, available, safeLimit, out.count, tick, safeRadius,
                    filterName);
        }
        ++stats.truncated;
    }
    return true;
}

void ScriptApi::FlushQueryStats() const {
    if (m_queryStats.empty()) {
        return;
    }
    std::vector<std::pair<std::string, QueryTickStats>> entries;
    entries.reserve(m_queryStats.size());
    for (const auto& [modId, stats] : m_queryStats) {
        entries.emplace_back(modId, stats);
    }
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [modId, stats] : entries) {
        ZL_INFO(kModule, "[{}] 查询统计：tick={} 次数={} 截断={}", modId,
                stats.tick, stats.queries, stats.truncated);
    }
    m_queryStats.clear();
}

bool ScriptApi::GetEntityContainer(std::uint64_t id,
                                   ScriptContainerInfo& out) const {
    out = ScriptContainerInfo{};
    if (m_ctx == nullptr || m_ctx->ecs == nullptr || id == 0) {
        return false;
    }
    const entt::entity entity = m_ctx->ecs->FindById(id);
    if (entity == entt::null || !m_ctx->ecs->Alive(entity)) {
        return false;
    }
    const entt::registry& registry = m_ctx->ecs->Registry();
    const auto* container = registry.try_get<ecs::Container>(entity);
    if (container == nullptr) {
        return false;
    }
    out.id = id;
    out.capacityKg = NonNegative(container->capacityKg);
    if (const auto* loot = registry.try_get<ecs::LootContainer>(entity);
        loot != nullptr) {
        out.tableId = loot->tableId;
        out.searched = loot->searched;
    } else {
        out.searched = true;
    }
    out.isStorage = registry.all_of<ecs::PlayerStorage>(entity);
    if (!out.searched) {
        return true;
    }
    out.weightKg = m_ctx->items != nullptr
                       ? items::TotalWeight(*container, *m_ctx->items)
                       : 0.0f;
    out.totalStacks = static_cast<int>(container->items.size());
    constexpr std::size_t kLimit = 128;
    const std::size_t count =
        std::min(container->items.size(), kLimit);
    out.stacks.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.stacks.push_back(MakeItemStack(container->items[i],
                                           static_cast<int>(i),
                                           m_ctx->items));
    }
    out.truncated = container->items.size() > count;
    return true;
}

std::uint64_t ScriptApi::SpawnZombie(float x, float y, int z,
                                     const std::string& variant) const {
    if (m_ctx == nullptr || m_ctx->ecs == nullptr || !std::isfinite(x) ||
        !std::isfinite(y)) {
        return 0;
    }
    (void)z;  // 僵尸生成目前只支持地面层（z=0 语义，与 ZombieFactory 一致）
    ecs::ZombieVariantKind kind = ecs::ZombieVariantKind::Normal;
    if (!ParseZombieVariant(variant, kind)) {
        ZL_WARN(kModule, "SpawnZombie：未知僵尸变体 variant={}", variant);
        return 0;
    }
    const ecs::Entity zombie =
        game::SpawnZombie(*m_ctx->ecs, m_ctx->physics, x, y, m_ctx->events,
                          kind);
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

bool ScriptApi::GetItemInfo(const std::string& defId, ScriptItemInfo& out) const {
    out = ScriptItemInfo{};
    if (m_ctx == nullptr || m_ctx->items == nullptr) {
        return false;
    }
    const items::ItemDef* def = m_ctx->items->Find(defId);
    if (def == nullptr) {
        return false;
    }
    out.id = def->id;
    out.name = def->name;
    out.icon = def->icon;
    out.weight = NonNegative(def->weight);
    out.maxStack = std::max(0, def->maxStack);
    out.type = items::ItemTypeToString(def->type);
    out.spoilDays = NonNegative(def->spoilDays);
    out.repairEfficiency = NonNegative(def->repairEfficiency);
    out.usable = def->use.Any() || !def->unlocksRecipes.empty();
    out.equippable = def->type == items::ItemType::Weapon ||
                     def->type == items::ItemType::Clothing;
    out.throwable = def->throwable.has_value();
    out.ranged = def->ranged.has_value();
    return true;
}

bool ScriptApi::ListRecipes(ScriptRecipeList& out) const {
    out = ScriptRecipeList{};
    if (m_ctx == nullptr || m_ctx->recipes == nullptr) {
        return false;
    }
    ui::InventoryState uiState;
    const bool hasUi = m_ctx->getInventoryState &&
                       m_ctx->getInventoryState(uiState);
    std::unordered_map<std::string, const ui::InventoryRecipe*> uiById;
    if (hasUi) {
        for (const ui::InventoryRecipe& recipe : uiState.recipes) {
            uiById.emplace(recipe.id, &recipe);
        }
    }
    const entt::entity player = FindPlayer();
    const auto* bag = (player != entt::null && m_ctx->ecs != nullptr)
                          ? m_ctx->ecs->Registry().try_get<ecs::Container>(player)
                          : nullptr;

    const auto makeIngredient = [&](const items::RecipeIngredient& ingredient,
                                    int have) {
        ScriptRecipeIngredient entry;
        entry.defId = ingredient.defId;
        const items::ItemDef* def =
            m_ctx->items != nullptr ? m_ctx->items->Find(ingredient.defId)
                                    : nullptr;
        entry.label = def != nullptr ? def->name : ingredient.defId;
        entry.required = ingredient.count;
        entry.have = have;
        entry.wear = NonNegative(ingredient.wear);
        return entry;
    };

    constexpr std::size_t kLimit = 128;
    const std::vector<items::RecipeDef>& all = m_ctx->recipes->All();
    const std::size_t count = std::min(all.size(), kLimit);
    out.recipes.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const items::RecipeDef& def = all[i];
        ScriptRecipeInfo recipe;
        recipe.index = static_cast<int>(i);
        recipe.id = def.id;
        recipe.name = def.name;
        recipe.category = items::RecipeCategoryToString(def.category);
        recipe.categoryLabel = items::RecipeCategoryLabel(def.category);
        recipe.outputDefId = def.outputDefId;
        recipe.outputCount = def.outputCount;
        const items::ItemDef* output =
            m_ctx->items != nullptr ? m_ctx->items->Find(def.outputDefId)
                                    : nullptr;
        recipe.outputLabel =
            output != nullptr ? output->name : def.outputDefId;
        recipe.station = def.station;
        recipe.stationLabel = def.station;
        recipe.seconds = NonNegative(def.craftSeconds);
        recipe.stationNearby = def.station.empty();
        recipe.known = m_ctx->recipeKnown ? m_ctx->recipeKnown(def.id) : true;

        const auto uiIt = uiById.find(def.id);
        if (uiIt != uiById.end()) {
            const ui::InventoryRecipe& uiRecipe = *uiIt->second;
            recipe.known = uiRecipe.known;
            recipe.canCraft = uiRecipe.canCraft;
            recipe.stationNearby = uiRecipe.stationNearby;
            recipe.stationLabel = uiRecipe.stationLabel;
            recipe.seconds = NonNegative(uiRecipe.estimatedSeconds);
        } else if (bag != nullptr && m_ctx->items != nullptr) {
            recipe.canCraft =
                recipe.known &&
                items::CanCraft(*bag, *m_ctx->items, def, nullptr) &&
                recipe.stationNearby;
        }

        recipe.ingredients.reserve(def.ingredients.size());
        for (const items::RecipeIngredient& ingredient : def.ingredients) {
            const int have = bag != nullptr
                                 ? items::CountOf(*bag, ingredient.defId)
                                 : 0;
            recipe.ingredients.push_back(makeIngredient(ingredient, have));
        }
        recipe.tools.reserve(def.tools.size());
        for (const items::RecipeIngredient& tool : def.tools) {
            const int have = bag != nullptr
                                 ? items::CountUsableTools(*bag, tool.defId)
                                 : 0;
            recipe.tools.push_back(makeIngredient(tool, have));
        }
        if (def.cookware.has_value()) {
            recipe.hasCookware = true;
            const int have =
                bag != nullptr
                    ? items::CountUsableTools(*bag, def.cookware->defId)
                    : 0;
            recipe.cookware = makeIngredient(*def.cookware, have);
        }
        if (def.fuel.has_value()) {
            recipe.hasFuel = true;
            const int have =
                bag != nullptr ? items::CountOf(*bag, def.fuel->defId) : 0;
            recipe.fuel = makeIngredient(*def.fuel, have);
        }
        out.recipes.push_back(std::move(recipe));
    }
    out.count = static_cast<int>(out.recipes.size());
    out.truncated = all.size() > count;
    return true;
}

bool ScriptApi::GetCraftState(ScriptCraftState& out) const {
    out = ScriptCraftState{};
    if (m_ctx == nullptr) {
        return false;
    }
    ui::InventoryState uiState;
    if (m_ctx->getInventoryState && m_ctx->getInventoryState(uiState)) {
        const ui::InventoryCrafting& crafting = uiState.crafting;
        out.active = crafting.active;
        out.recipeId = crafting.recipeId;
        out.outputLabel = crafting.outputLabel;
        out.outputCount = crafting.outputCount;
        out.elapsed = NonNegative(crafting.elapsed);
        out.duration = NonNegative(crafting.duration);
        out.progress = ClampUnit(crafting.progress);
        out.batchTotal =
            crafting.active ? std::max(0, crafting.batchCount) : 0;
        out.batchCompleted =
            crafting.active
                ? std::clamp(crafting.completedCount, 0,
                             std::max(0, out.batchTotal))
                : 0;
        if (m_ctx->recipes != nullptr && !out.recipeId.empty()) {
            if (const items::RecipeDef* def =
                    m_ctx->recipes->Find(out.recipeId);
                def != nullptr) {
                out.outputDefId = def->outputDefId;
            }
        }
        out.queue.reserve(crafting.queue.size());
        for (const ui::InventoryQueuedCraft& entry : crafting.queue) {
            out.queue.push_back(
                ScriptCraftQueueEntry{entry.recipeId, entry.count});
        }
        return true;
    }

    const entt::entity player = FindPlayer();
    const auto* job =
        (player != entt::null && m_ctx->ecs != nullptr)
            ? m_ctx->ecs->Registry().try_get<ecs::CraftingJob>(player)
            : nullptr;
    if (job == nullptr) {
        return true;
    }
    out.active = job->active;
    out.recipeId = job->recipeId;
    out.elapsed = NonNegative(job->elapsed);
    out.duration = NonNegative(job->duration);
    out.progress = out.duration > 0.0f
                       ? std::clamp(out.elapsed / out.duration, 0.0f, 1.0f)
                       : 0.0f;
    out.batchTotal = std::max(0, job->batchTotal);
    out.batchCompleted =
        std::clamp(job->batchCompleted, 0, out.batchTotal);
    if (m_ctx->recipes != nullptr && !out.recipeId.empty()) {
        if (const items::RecipeDef* def = m_ctx->recipes->Find(out.recipeId);
            def != nullptr) {
            out.outputDefId = def->outputDefId;
            out.outputCount = def->outputCount;
            const items::ItemDef* output =
                m_ctx->items != nullptr ? m_ctx->items->Find(def->outputDefId)
                                        : nullptr;
            out.outputLabel =
                output != nullptr ? output->name : def->outputDefId;
        }
    }
    out.queue.reserve(job->queue.size());
    for (const ecs::CraftQueueItem& entry : job->queue) {
        out.queue.push_back(ScriptCraftQueueEntry{entry.recipeId, entry.count});
    }
    return true;
}

bool ScriptApi::IsRecipeKnown(const std::string& recipeId) const {
    if (m_ctx == nullptr || m_ctx->recipes == nullptr ||
        m_ctx->recipes->Find(recipeId) == nullptr) {
        return false;
    }
    if (m_ctx->recipeKnown) {
        return m_ctx->recipeKnown(recipeId);
    }
    ui::InventoryState uiState;
    if (m_ctx->getInventoryState && m_ctx->getInventoryState(uiState)) {
        for (const ui::InventoryRecipe& recipe : uiState.recipes) {
            if (recipe.id == recipeId) {
                return recipe.known;
            }
        }
    }
    const entt::entity player = FindPlayer();
    if (player != entt::null && m_ctx->ecs != nullptr) {
        const auto* known =
            m_ctx->ecs->Registry().try_get<ecs::KnownRecipes>(player);
        if (known != nullptr &&
            std::find(known->recipeIds.begin(), known->recipeIds.end(),
                      recipeId) != known->recipeIds.end()) {
            return true;
        }
    }
    return true;
}

bool ScriptApi::ListBuildables(ScriptBuildableList& out) const {
    out = ScriptBuildableList{};
    if (m_ctx == nullptr || m_ctx->config == nullptr) {
        return false;
    }
    std::vector<game::BuildableDef> defs =
        game::LoadBuildableDefs(*m_ctx->config);
    game::AppendBuildableDefs(defs, m_ctx->modBuildables);
    constexpr std::size_t kLimit = 128;
    const std::size_t count = std::min(defs.size(), kLimit);
    out.buildables.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const game::BuildableDef& def = defs[i];
        ScriptBuildableInfo info;
        info.index = static_cast<int>(i);
        info.id = def.id;
        info.name = def.name;
        info.sprite = def.sprite;
        info.woodCost = std::max(0, def.woodCost);
        info.maxHp = NonNegative(def.maxHp);
        info.blocksMovement = def.blocksMovement;
        info.storageKg = NonNegative(def.storageKg);
        info.storageSlots = std::max(0, def.storageSlots);
        info.layer = BuildLayerKey(def.layer);
        info.category = def.category;
        info.door = def.door;
        info.requiresMission = def.requiresMission;
        out.buildables.push_back(std::move(info));
    }
    out.count = static_cast<int>(out.buildables.size());
    out.truncated = defs.size() > count;
    return true;
}

bool ScriptApi::ListMissions(const std::string& filter,
                             ScriptMissionList& out) const {
    out = ScriptMissionList{};
    if (m_ctx == nullptr || m_ctx->missionConfig == nullptr) {
        return false;
    }
    const std::string normalized = filter.empty() ? "all" : filter;
    const bool all = normalized == "all";
    if (!all && normalized != "active" && normalized != "available" &&
        normalized != "completed" && normalized != "failed" &&
        normalized != "locked") {
        return false;
    }
    const entt::entity player = FindPlayer();
    ecs::MissionLog emptyLog;
    const ecs::MissionLog* log =
        (player != entt::null && m_ctx->ecs != nullptr)
            ? m_ctx->ecs->Registry().try_get<ecs::MissionLog>(player)
            : nullptr;
    if (log == nullptr) {
        log = &emptyLog;
    }
    const int day =
        m_ctx->gameTime != nullptr ? std::max(1, m_ctx->gameTime->Now().day) : 1;
    const std::vector<game::MissionDef>& defs = m_ctx->missionConfig->defs;
    constexpr std::size_t kLimit = 128;
    bool truncated = false;
    for (std::size_t i = 0; i < defs.size(); ++i) {
        ScriptMissionInfo info =
            MakeMissionInfo(*m_ctx->missionConfig, defs[i],
                            static_cast<int>(i), *log, day);
        if (!all && info.status != normalized) {
            continue;
        }
        if (out.missions.size() >= kLimit) {
            truncated = true;
            break;
        }
        out.missions.push_back(std::move(info));
    }
    out.count = static_cast<int>(out.missions.size());
    out.truncated = truncated;
    return true;
}

bool ScriptApi::GetMission(const std::string& id,
                           ScriptMissionInfo& out) const {
    out = ScriptMissionInfo{};
    if (m_ctx == nullptr || m_ctx->missionConfig == nullptr || id.empty()) {
        return false;
    }
    const entt::entity player = FindPlayer();
    ecs::MissionLog emptyLog;
    const ecs::MissionLog* log =
        (player != entt::null && m_ctx->ecs != nullptr)
            ? m_ctx->ecs->Registry().try_get<ecs::MissionLog>(player)
            : nullptr;
    if (log == nullptr) {
        log = &emptyLog;
    }
    const int day =
        m_ctx->gameTime != nullptr ? std::max(1, m_ctx->gameTime->Now().day) : 1;
    const std::vector<game::MissionDef>& defs = m_ctx->missionConfig->defs;
    for (std::size_t i = 0; i < defs.size(); ++i) {
        if (defs[i].id == id) {
            out = MakeMissionInfo(*m_ctx->missionConfig, defs[i],
                                  static_cast<int>(i), *log, day);
            return true;
        }
    }
    return false;
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
