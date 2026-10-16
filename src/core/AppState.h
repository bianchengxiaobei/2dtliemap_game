#pragma once

#include <memory>

namespace zl::core {

class AppStateMachine;

// 顶层状态（Boot → MainMenu → InGame）；M1 用 SandboxState 占位。
// 状态本身不持有服务；需要访问服务的状态在构造时拿 GameContext&（game 层）。
class AppState {
public:
    virtual ~AppState() = default;

    virtual const char* Name() const = 0;

    virtual void OnEnter() {}
    virtual void OnExit() {}
    virtual void OnTick(double /*tickDt*/) {}
    virtual void OnFrame(double /*frameDt*/, double /*alpha*/) {}
};

// 延迟切换：SetState 后本帧末 ApplyPendingState 生效（避免在状态回调里销毁自身）
class AppStateMachine {
public:
    void SetState(std::unique_ptr<AppState> next);
    void ApplyPendingState();

    AppState* Current() const { return m_current.get(); }
    const char* CurrentName() const;

    void Clear();

private:
    std::unique_ptr<AppState> m_current;
    std::unique_ptr<AppState> m_pending;
};

}  // namespace zl::core
