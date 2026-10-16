#include "core/EventBus.h"

#include <algorithm>

namespace zl::core {

EventBus::SubscriptionId EventBus::Subscribe(std::string_view key, Handler handler, int priority) {
    const std::string k(key);
    const SubscriptionId id = m_nextId++;
    m_subs[k].push_back(Sub{id, priority, m_nextSeq++, std::move(handler)});
    m_idToKey.emplace(id, k);
    return id;
}

void EventBus::Unsubscribe(SubscriptionId id) {
    const auto keyIt = m_idToKey.find(id);
    if (keyIt == m_idToKey.end()) {
        return;
    }
    const auto subIt = m_subs.find(keyIt->second);
    if (subIt != m_subs.end()) {
        auto& subs = subIt->second;
        subs.erase(std::remove_if(subs.begin(), subs.end(),
                                  [id](const Sub& sub) { return sub.id == id; }),
                   subs.end());
        if (subs.empty()) {
            m_subs.erase(subIt);
        }
    }
    m_idToKey.erase(keyIt);
}

void EventBus::Emit(std::string_view key, EventPayload payload) {
    const auto it = m_subs.find(std::string(key));
    if (it == m_subs.end()) {
        return;
    }

    // 拷贝一份订阅列表：允许派发过程中（退）订阅而不破坏迭代
    auto handlers = it->second;
    std::stable_sort(handlers.begin(), handlers.end(),
                     [](const Sub& a, const Sub& b) { return a.priority > b.priority; });

    const Event event{std::string(key), std::move(payload)};
    for (const auto& sub : handlers) {
        if (m_idToKey.find(sub.id) == m_idToKey.end()) {
            continue;  // 派发中途已被退订
        }
        sub.handler(event);
    }
}

void EventBus::EmitQueued(std::string_view key, EventPayload payload) {
    m_queue.push_back(Event{std::string(key), std::move(payload)});
}

void EventBus::FlushQueued() {
    if (m_queue.empty()) {
        return;
    }
    // 先换出再派发：flush 期间新产生的事件留给下一轮
    std::vector<Event> pending;
    pending.swap(m_queue);
    for (auto& event : pending) {
        Emit(event.key, std::move(event.payload));
    }
}

void EventBus::Clear() {
    m_subs.clear();
    m_idToKey.clear();
    m_queue.clear();
}

}  // namespace zl::core
