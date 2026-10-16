#include "ecs/SystemScheduler.h"

#include <chrono>
#include <utility>

#include "core/Log.h"
#include "ecs/EcsWorld.h"

namespace zl::ecs {

void SystemScheduler::AddSystem(std::string name, SystemFn fn) {
    if (m_index.contains(name)) {
        ZL_WARN("ecs", "系统重复注册：{}（将以后注册者为准，执行顺序不变）", name);
    }
    m_index[name] = m_entries.size();
    Entry entry;
    entry.name = std::move(name);
    entry.fn = std::move(fn);
    m_entries.push_back(std::move(entry));
}

void SystemScheduler::SetEnabled(std::string_view name, bool enabled) {
    const auto it = m_index.find(std::string(name));
    if (it == m_index.end()) {
        ZL_WARN("ecs", "SetEnabled：未注册的系统 {}", name);
        return;
    }
    m_entries[it->second].enabled = enabled;
}

bool SystemScheduler::IsEnabled(std::string_view name) const {
    const auto it = m_index.find(std::string(name));
    return it != m_index.end() && m_entries[it->second].enabled;
}

void SystemScheduler::Run(EcsWorld& world, float dt) {
    for (auto& entry : m_entries) {
        if (!entry.enabled || !entry.fn) {
            continue;
        }
        const auto start = std::chrono::steady_clock::now();
        entry.fn(world, dt);
        const auto end = std::chrono::steady_clock::now();
        entry.lastMs = std::chrono::duration<double, std::milli>(end - start).count();
        entry.avgMs = entry.avgMs <= 0.0 ? entry.lastMs : entry.avgMs * 0.9 + entry.lastMs * 0.1;
    }
}

void SystemScheduler::Clear() {
    m_entries.clear();
    m_index.clear();
}

}  // namespace zl::ecs
