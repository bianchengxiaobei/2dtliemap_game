#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "core/EventBus.h"

namespace zl::game {

struct WorldStartInfo {
    std::string saveId;
    bool isNewGame = false;
    int day = 1;
    std::uint64_t playerId = 0;
};

// 世界生命周期事件只发一次：Start 需要玩家已生成，Exit 只在 Start 后发。
class WorldLifecycle {
public:
    void Reset() {
        m_started = false;
        m_exited = false;
    }

    bool Started() const { return m_started; }

    bool Start(core::EventBus* events, const WorldStartInfo& info) {
        if (m_started || events == nullptr || info.playerId == 0) {
            return false;
        }
        m_started = true;
        events->Emit(core::kOnWorldStart,
                     core::WorldStartEvent{info.saveId, info.isNewGame,
                                           info.day, info.playerId});
        return true;
    }

    bool Exit(core::EventBus* events, std::string_view saveId) {
        if (!m_started || m_exited) {
            return false;
        }
        m_exited = true;
        if (events == nullptr) {
            return false;
        }
        events->Emit(core::kOnWorldExit,
                     core::WorldExitEvent{std::string(saveId)});
        return true;
    }

private:
    bool m_started = false;
    bool m_exited = false;
};

}  // namespace zl::game
