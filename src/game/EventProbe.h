#pragma once

#include <cstdint>
#include <vector>

#include "core/EventBus.h"

namespace zl::game {

// 事件探针（M1 验收用）：独立模块，不依赖任何游戏逻辑，
// 只订阅事件并打日志，验证"发送方与接收方解耦"。
class EventProbe {
public:
    static constexpr std::string_view kInputActionEvent = "input.action";
    static constexpr std::string_view kSpeedChangedEvent = "speed.changed";

    void Attach(core::EventBus& bus);
    void Detach(core::EventBus& bus);

private:
    void OnGameStart(const core::Event& event);
    void OnTick(const core::Event& event);
    void OnInputAction(const core::Event& event);
    void OnSpeedChanged(const core::Event& event);

    std::vector<core::EventBus::SubscriptionId> m_subscriptions;
    int64_t m_lastLoggedTick = -1;
};

}  // namespace zl::game
