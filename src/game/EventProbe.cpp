#include "game/EventProbe.h"

#include <variant>

#include "core/Log.h"

namespace zl::game {
namespace {

// 每多少 tick 打一次 OnTick 日志（300 tick = 5 秒 @1x）
constexpr int64_t kTickLogInterval = 300;

}  // namespace

void EventProbe::Attach(core::EventBus& bus) {
    m_subscriptions.push_back(bus.Subscribe(core::kOnGameStart,
                                            [this](const core::Event& e) { OnGameStart(e); }));
    m_subscriptions.push_back(
        bus.Subscribe(core::kOnTick, [this](const core::Event& e) { OnTick(e); }));
    m_subscriptions.push_back(bus.Subscribe(kInputActionEvent,
                                            [this](const core::Event& e) { OnInputAction(e); }));
    m_subscriptions.push_back(bus.Subscribe(kSpeedChangedEvent,
                                            [this](const core::Event& e) { OnSpeedChanged(e); }));
    ZL_INFO("game", "[EventProbe] 已订阅 OnGameStart/OnTick/input.action/speed.changed");
}

void EventProbe::Detach(core::EventBus& bus) {
    for (const auto id : m_subscriptions) {
        bus.Unsubscribe(id);
    }
    m_subscriptions.clear();
}

void EventProbe::OnGameStart(const core::Event& event) {
    (void)event;
    ZL_INFO("game", "[EventProbe] 收到 {} → 新游戏开始", core::kOnGameStart);
}

void EventProbe::OnTick(const core::Event& event) {
    const auto* tick = std::get_if<int64_t>(&event.payload);
    if (tick == nullptr) {
        return;
    }
    if (m_lastLoggedTick < 0 || *tick - m_lastLoggedTick >= kTickLogInterval) {
        m_lastLoggedTick = *tick;
        ZL_INFO("game", "[EventProbe] 收到 OnTick tick={}", *tick);
    }
}

void EventProbe::OnInputAction(const core::Event& event) {
    const auto* action = std::get_if<std::string>(&event.payload);
    ZL_INFO("game", "[EventProbe] 收到按键动作事件: {}", action ? *action : "(?)");
}

void EventProbe::OnSpeedChanged(const core::Event& event) {
    const auto* scale = std::get_if<double>(&event.payload);
    ZL_INFO("game", "[EventProbe] 收到倍速变化: {:.0f}x", scale ? *scale : -1.0);
}

}  // namespace zl::game
