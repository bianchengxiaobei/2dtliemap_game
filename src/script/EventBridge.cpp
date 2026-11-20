#include "script/EventBridge.h"

#include <iterator>
#include <string_view>
#include <utility>

#include "core/Log.h"
#include "core/SoundEvent.h"

namespace zl::script {
namespace {

constexpr const char* kModule = "script";

// 事件表 v1 全部键（docs/03 §5）：桥接订阅集。
constexpr std::string_view kEventKeys[] = {
    core::kOnGameStart,     core::kOnWorldStart,   core::kOnWorldExit,
    core::kOnTick,          core::kOnPlayerMove,   core::kOnPlayerDeath,
    core::kOnDamage,        core::kOnZombieSpawn,  core::kOnZombieDead,
    core::kOnAnimalKilled,  core::kOnItemPickup,   core::kOnItemDrop,
    core::kOnItemUsed,      core::kOnLootSearched, core::kOnCraftStart,
    core::kOnCraftFinished, core::kOnBuildPlaced,  core::kOnBuildDemolished,
    core::kOnBuildRepaired,
    core::kOnSound,         core::kOnChunkLoaded,  core::kOnChunkUnloaded,
    core::kOnSaveBegin,     core::kOnSaveEnd,      core::kOnModReload,
    core::kOnUiFrame,       core::kOnUiReady,      core::kOnUiEvent,
    core::kOnMenuChanged,   core::kOnInventoryChanged,
};

std::string_view SoundTypeName(core::SoundType type) {
    switch (type) {
        case core::SoundType::Gunshot:
            return "Gunshot";
        case core::SoundType::Impact:
            return "Impact";
        case core::SoundType::Footstep:
            return "Footstep";
        case core::SoundType::ZombieGrowl:
            return "ZombieGrowl";
        case core::SoundType::Ui:
            return "Ui";
        case core::SoundType::Vehicle:
            return "Vehicle";
        case core::SoundType::Generic:
        default:
            return "Generic";
    }
}

void AddField(core::ScriptEvent& event, std::string name, core::ScriptValue value) {
    event.fields.push_back(core::ScriptField{std::move(name), std::move(value)});
}

void AddId(core::ScriptEvent& event, std::string name, std::uint64_t id) {
    AddField(event, std::move(name), static_cast<std::int64_t>(id));
}

}  // namespace

EventBridge::EventBridge(core::EventBus& bus, ScriptManager& scripts)
    : m_bus(bus), m_scripts(scripts) {
    m_subscriptions.reserve(std::size(kEventKeys));
    for (const std::string_view key : kEventKeys) {
        m_subscriptions.push_back(bus.Subscribe(key, [this](const core::Event& event) {
            std::optional<core::ScriptEvent> scriptEvent = Translate(event);
            if (scriptEvent.has_value()) {
                m_scripts.DispatchEvent(*scriptEvent);
            }
        }));
    }
    ZL_INFO(kModule, "事件桥接就绪（v1 + M35 增补共 {} 个事件）", std::size(kEventKeys));
}

EventBridge::~EventBridge() {
    for (const core::EventBus::SubscriptionId id : m_subscriptions) {
        m_bus.Unsubscribe(id);
    }
}

std::optional<core::ScriptEvent> EventBridge::Translate(const core::Event& event) {
    core::ScriptEvent out;
    out.name = event.key;

    if (event.key == core::kOnGameStart || event.key == core::kOnUiReady ||
        event.key == core::kOnUiFrame) {
        return out;  // 无载荷
    }
    if (event.key == core::kOnWorldStart) {
        const auto* world = std::get_if<core::WorldStartEvent>(&event.payload);
        if (world == nullptr) {
            return std::nullopt;
        }
        AddField(out, "saveId", world->saveId);
        AddField(out, "isNewGame", world->isNewGame);
        AddField(out, "day", static_cast<std::int64_t>(world->day));
        AddId(out, "playerId", world->playerId);
        return out;
    }
    if (event.key == core::kOnWorldExit) {
        const auto* world = std::get_if<core::WorldExitEvent>(&event.payload);
        if (world == nullptr) {
            return std::nullopt;
        }
        AddField(out, "saveId", world->saveId);
        return out;
    }
    if (event.key == core::kOnTick) {
        if (const auto* tick = std::get_if<std::int64_t>(&event.payload)) {
            AddField(out, "tick", *tick);
            return out;
        }
        return std::nullopt;
    }
    if (event.key == core::kOnPlayerMove || event.key == core::kOnPlayerDeath) {
        const auto* player = std::get_if<core::PlayerEvent>(&event.payload);
        if (player == nullptr) {
            return std::nullopt;
        }
        AddId(out, "playerId", player->playerId);
        if (event.key == core::kOnPlayerMove) {
            AddField(out, "x", static_cast<double>(player->x));
            AddField(out, "y", static_cast<double>(player->y));
            AddField(out, "z", static_cast<std::int64_t>(player->z));
        } else {
            AddId(out, "sourceId", player->sourceId);
            AddField(out, "cause", player->cause);
        }
        return out;
    }
    if (event.key == core::kOnDamage) {
        const auto* damage = std::get_if<core::DamageEvent>(&event.payload);
        if (damage == nullptr) {
            return std::nullopt;
        }
        AddId(out, "targetId", damage->targetId);
        AddId(out, "sourceId", damage->sourceId);
        AddField(out, "amount", static_cast<double>(damage->amount));
        AddField(out, "damageType", damage->damageType);
        AddField(out, "cause", damage->cause);
        AddField(out, "remainingHp", static_cast<double>(damage->remainingHp));
        AddField(out, "lethal", damage->lethal);
        AddField(out, "blocked", damage->blocked);
        return out;
    }
    if (event.key == core::kOnZombieSpawn || event.key == core::kOnZombieDead) {
        const auto* zombie = std::get_if<core::ZombieEvent>(&event.payload);
        if (zombie == nullptr) {
            return std::nullopt;
        }
        AddId(out, "zombieId", zombie->zombieId);
        if (event.key == core::kOnZombieSpawn) {
            AddField(out, "x", static_cast<double>(zombie->x));
            AddField(out, "y", static_cast<double>(zombie->y));
            AddField(out, "z", static_cast<std::int64_t>(zombie->z));
        } else {
            AddId(out, "killer", zombie->killerId);
        }
        return out;
    }
    if (event.key == core::kOnAnimalKilled) {
        const auto* animal = std::get_if<core::AnimalKilledEvent>(&event.payload);
        if (animal == nullptr) {
            return std::nullopt;
        }
        AddId(out, "animalId", animal->animalId);
        AddField(out, "speciesId", animal->speciesId);
        AddId(out, "killerId", animal->killerId);
        AddField(out, "x", static_cast<double>(animal->x));
        AddField(out, "y", static_cast<double>(animal->y));
        AddField(out, "z", static_cast<std::int64_t>(animal->z));
        return out;
    }
    if (event.key == core::kOnItemPickup || event.key == core::kOnItemDrop) {
        const auto* item = std::get_if<core::ItemEvent>(&event.payload);
        if (item == nullptr) {
            return std::nullopt;
        }
        AddId(out, "itemId", item->itemId);
        AddField(out, "defId", item->defId);
        AddField(out, "count", static_cast<std::int64_t>(item->count));
        return out;
    }
    if (event.key == core::kOnItemUsed) {
        const auto* item = std::get_if<core::ItemUsedEvent>(&event.payload);
        if (item == nullptr) {
            return std::nullopt;
        }
        AddId(out, "playerId", item->playerId);
        AddField(out, "defId", item->defId);
        AddField(out, "spoiled", item->spoiled);
        AddField(out, "remaining", static_cast<std::int64_t>(item->remaining));
        return out;
    }
    if (event.key == core::kOnLootSearched) {
        const auto* loot = std::get_if<core::LootSearchedEvent>(&event.payload);
        if (loot == nullptr) {
            return std::nullopt;
        }
        AddId(out, "playerId", loot->playerId);
        AddId(out, "containerId", loot->containerId);
        AddField(out, "tableId", loot->tableId);
        AddField(out, "itemCount", static_cast<std::int64_t>(loot->itemCount));
        return out;
    }
    if (event.key == core::kOnCraftStart) {
        const auto* craft = std::get_if<core::CraftStartEvent>(&event.payload);
        if (craft == nullptr) {
            return std::nullopt;
        }
        AddId(out, "playerId", craft->playerId);
        AddField(out, "recipeId", craft->recipeId);
        AddField(out, "outputDefId", craft->outputDefId);
        AddField(out, "outputCount", static_cast<std::int64_t>(craft->outputCount));
        AddField(out, "batchCount", static_cast<std::int64_t>(craft->batchCount));
        return out;
    }
    if (event.key == core::kOnCraftFinished) {
        const auto* craft = std::get_if<core::CraftFinishedEvent>(&event.payload);
        if (craft == nullptr) {
            return std::nullopt;
        }
        AddId(out, "playerId", craft->playerId);
        AddField(out, "recipeId", craft->recipeId);
        AddField(out, "outputDefId", craft->outputDefId);
        AddField(out, "outputCount", static_cast<std::int64_t>(craft->outputCount));
        AddField(out, "batchCompleted",
                 static_cast<std::int64_t>(craft->batchCompleted));
        AddField(out, "failed", craft->failed);
        return out;
    }
    if (event.key == core::kOnBuildPlaced) {
        const auto* build = std::get_if<core::BuildPlacedEvent>(&event.payload);
        if (build == nullptr) {
            return std::nullopt;
        }
        AddId(out, "playerId", build->playerId);
        AddField(out, "defId", build->defId);
        AddField(out, "x", static_cast<std::int64_t>(build->x));
        AddField(out, "y", static_cast<std::int64_t>(build->y));
        AddField(out, "z", static_cast<std::int64_t>(build->z));
        AddField(out, "vertical", build->vertical);
        return out;
    }
    if (event.key == core::kOnBuildDemolished) {
        const auto* build = std::get_if<core::BuildDemolishedEvent>(&event.payload);
        if (build == nullptr) {
            return std::nullopt;
        }
        AddId(out, "playerId", build->playerId);
        AddField(out, "defId", build->defId);
        AddField(out, "x", static_cast<std::int64_t>(build->x));
        AddField(out, "y", static_cast<std::int64_t>(build->y));
        AddField(out, "z", static_cast<std::int64_t>(build->z));
        return out;
    }
    if (event.key == core::kOnBuildRepaired) {
        const auto* build = std::get_if<core::BuildRepairedEvent>(&event.payload);
        if (build == nullptr) {
            return std::nullopt;
        }
        AddId(out, "playerId", build->playerId);
        AddField(out, "x", static_cast<std::int64_t>(build->x));
        AddField(out, "y", static_cast<std::int64_t>(build->y));
        AddField(out, "z", static_cast<std::int64_t>(build->z));
        return out;
    }
    if (event.key == core::kOnSound) {
        const auto* sound = std::get_if<core::SoundEvent>(&event.payload);
        if (sound == nullptr) {
            return std::nullopt;
        }
        AddField(out, "x", static_cast<double>(sound->x));
        AddField(out, "y", static_cast<double>(sound->y));
        AddField(out, "z", static_cast<std::int64_t>(0));  // SoundEvent 无 z（声音按层不区分）
        AddField(out, "radius", static_cast<double>(sound->radius));
        AddField(out, "soundType", std::string(SoundTypeName(sound->type)));
        AddField(out, "clip", sound->clip);
        return out;
    }
    if (event.key == core::kOnChunkLoaded || event.key == core::kOnChunkUnloaded) {
        const auto* chunk = std::get_if<core::ChunkEvent>(&event.payload);
        if (chunk == nullptr) {
            return std::nullopt;
        }
        AddField(out, "cx", static_cast<std::int64_t>(chunk->cx));
        AddField(out, "cy", static_cast<std::int64_t>(chunk->cy));
        return out;
    }
    if (event.key == core::kOnSaveBegin || event.key == core::kOnSaveEnd) {
        return out;  // 无字段；存档 id 由脚本通过 Save.List() 获取
    }
    if (event.key == core::kOnModReload) {
        const auto* text = std::get_if<std::string>(&event.payload);
        if (text == nullptr) {
            return std::nullopt;
        }
        AddField(out, "modId", *text);
        return out;
    }
    if (event.key == core::kOnUiEvent) {
        const auto* ui = std::get_if<core::UiEvent>(&event.payload);
        if (ui == nullptr) {
            return std::nullopt;
        }
        AddField(out, "modId", ui->modId);
        AddField(out, "document", ui->documentId);
        AddField(out, "element", ui->elementId);
        AddField(out, "type", ui->type);
        AddField(out, "value", ui->value);
        AddField(out, "key", static_cast<std::int64_t>(ui->key));
        AddField(out, "button", static_cast<std::int64_t>(ui->button));
        AddField(out, "mouseX", static_cast<double>(ui->mouseX));
        AddField(out, "mouseY", static_cast<double>(ui->mouseY));
        return out;
    }
    if (event.key == core::kOnMenuChanged) {
        const auto* menu = std::get_if<core::MenuChangedEvent>(&event.payload);
        if (menu == nullptr) {
            return std::nullopt;
        }
        AddField(out, "open", menu->open);
        AddField(out, "pauseMode", menu->pauseMode);
        AddField(out, "canContinue", menu->canContinue);
        return out;
    }
    if (event.key == core::kOnInventoryChanged) {
        const auto* inventory = std::get_if<core::InventoryChangedEvent>(&event.payload);
        if (inventory == nullptr) {
            return std::nullopt;
        }
        AddField(out, "open", inventory->open);
        return out;
    }
    return std::nullopt;  // 非事件表 v1 的键（如 EventProbe 内部事件）：不派发给脚本
}

}  // namespace zl::script
