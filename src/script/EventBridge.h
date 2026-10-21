#pragma once

#include <optional>
#include <vector>

#include "core/EventBus.h"
#include "core/ScriptEvent.h"
#include "script/ScriptManager.h"

namespace zl::script {

// M7-T4：事件桥接（EventBus ↔ 脚本 Events.On）。
// - 订阅事件表 v1 的全部键（docs/03 §5）；
// - EventBus 载荷 → ScriptEvent「事件名 + 命名字段」（Translate，可单测）；
// - 经 ScriptManager::DispatchEvent 按加载顺序派发（错误隔离/禁用策略在 manager）。
// 语义：事件随总线队列在主线程 tick 末统一派发（EmitQueued 生产者侧约定）。
// 说明：OnSaveBegin/OnSaveEnd 无字段，由 WorldDemoState 在后台存档提交/完成时发出。
class EventBridge {
public:
    EventBridge(core::EventBus& bus, ScriptManager& scripts);
    ~EventBridge();

    EventBridge(const EventBridge&) = delete;
    EventBridge& operator=(const EventBridge&) = delete;

    // 事件表 v1 键 → 脚本事件；未知键或载荷类型不符 → nullopt。
    static std::optional<core::ScriptEvent> Translate(const core::Event& event);

private:
    core::EventBus& m_bus;
    ScriptManager& m_scripts;
    std::vector<core::EventBus::SubscriptionId> m_subscriptions;
};

}  // namespace zl::script
