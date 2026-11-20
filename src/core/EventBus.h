#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "core/GameEvents.h"
#include "core/SoundEvent.h"

namespace zl::core {

// 事件载荷：默认 monostate；模块载荷按需扩充（只加类型，不改派发机制）。
// 事件表 v1 见 docs/03 §5，命名 OnXxx（M5-T4：OnSound = SoundEvent；
// M7-T4：多字段事件载荷见 GameEvents.h）。
using EventPayload = std::variant<std::monostate, bool, int64_t, double, std::string, SoundEvent,
                                   WorldStartEvent, WorldExitEvent, PlayerEvent, DamageEvent,
                                   ZombieEvent, AnimalKilledEvent,
                                   ItemEvent, ItemUsedEvent, LootSearchedEvent, CraftStartEvent,
                                   CraftFinishedEvent, BuildPlacedEvent, BuildDemolishedEvent,
                                   BuildRepairedEvent, ChunkEvent, UiEvent, MenuChangedEvent,
                                   InventoryChangedEvent>;

struct Event {
    std::string key;
    EventPayload payload;
};

inline constexpr std::string_view kOnGameStart = "OnGameStart";
inline constexpr std::string_view kOnTick = "OnTick";

// 事件总线（docs/00 §3.1）：字符串键 + variant 载荷；
// 同步派发（Emit）与排队派发（EmitQueued → tick 末 FlushQueued）；
// 同优先级按订阅顺序调用；派发中退订的订阅者本轮不再收到。
class EventBus {
public:
    using Handler = std::function<void(const Event&)>;
    using SubscriptionId = uint64_t;
    static constexpr SubscriptionId kInvalidSubscription = 0;

    SubscriptionId Subscribe(std::string_view key, Handler handler, int priority = 0);
    void Unsubscribe(SubscriptionId id);

    void Emit(std::string_view key, EventPayload payload = {});
    void EmitQueued(std::string_view key, EventPayload payload = {});
    void FlushQueued();
    std::size_t QueuedCount() const { return m_queue.size(); }

    void Clear();

private:
    struct Sub {
        SubscriptionId id = 0;
        int priority = 0;
        uint64_t seq = 0;
        Handler handler;
    };

    std::unordered_map<std::string, std::vector<Sub>> m_subs;
    std::unordered_map<SubscriptionId, std::string> m_idToKey;
    std::vector<Event> m_queue;
    SubscriptionId m_nextId = 1;
    uint64_t m_nextSeq = 0;
};

}  // namespace zl::core
