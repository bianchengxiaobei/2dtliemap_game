#include "core/AppState.h"

#include "core/Log.h"

namespace zl::core {

void AppStateMachine::SetState(std::unique_ptr<AppState> next) {
    m_pending = std::move(next);
}

void AppStateMachine::ApplyPendingState() {
    if (!m_pending) {
        return;
    }
    if (m_current) {
        ZL_INFO("core", "状态退出: {}", m_current->Name());
        m_current->OnExit();
    }
    m_current = std::move(m_pending);
    ZL_INFO("core", "状态进入: {}", m_current->Name());
    m_current->OnEnter();
}

const char* AppStateMachine::CurrentName() const {
    return m_current ? m_current->Name() : "(none)";
}

void AppStateMachine::Clear() {
    if (m_current) {
        m_current->OnExit();
        m_current.reset();
    }
    m_pending.reset();
}

}  // namespace zl::core
