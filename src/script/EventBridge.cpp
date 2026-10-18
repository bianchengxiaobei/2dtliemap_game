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
    core::kOnGameStart,   core::kOnTick,        core::kOnPlayerMove,  core::kOnPlayerDeath,
    core::kOnZombieSpawn, core::kOnZombieDead,  core::kOnItemPickup,  core::kOnItemDrop,
    core::kOnSound,       core::kOnChunkLoaded, core::kOnChunkUnloaded, core::kOnSaveBegin,
    core::kOnSaveEnd,     core::kOnModReload,
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
    ZL_INFO(kModule, "事件桥接就绪（事件表 v1 共 {} 个事件）", std::size(kEventKeys));
}

EventBridge::~EventBridge() {
    for (const core::EventBus::SubscriptionId id : m_subscriptions) {
        m_bus.Unsubscribe(id);
    }
}

std::optional<core::ScriptEvent> EventBridge::Translate(const core::Event& event) {
    core::ScriptEvent out;
    out.name = event.key;

    if (event.key == core::kOnGameStart) {
        return out;  // 无载荷
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
        }
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
    if (event.key == core::kOnSaveBegin || event.key == core::kOnSaveEnd ||
        event.key == core::kOnModReload) {
        const auto* text = std::get_if<std::string>(&event.payload);
        if (text == nullptr) {
            return std::nullopt;
        }
        AddField(out, event.key == core::kOnModReload ? "modId" : "slotName", *text);
        return out;
    }
    return std::nullopt;  // 非事件表 v1 的键（如 EventProbe 内部事件）：不派发给脚本
}

}  // namespace zl::script
