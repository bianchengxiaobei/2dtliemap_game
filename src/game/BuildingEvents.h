#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "core/EventBus.h"

namespace zl::game {

inline void EmitBuildPlaced(core::EventBus* events, std::uint64_t playerId,
                            std::string_view defId, int x, int y, int z,
                            bool vertical) {
    if (events == nullptr) {
        return;
    }
    events->EmitQueued(
        core::kOnBuildPlaced,
        core::BuildPlacedEvent{playerId, std::string(defId), x, y, z, vertical});
}

inline void EmitBuildDemolished(core::EventBus* events, std::uint64_t playerId,
                                std::string_view defId, int x, int y, int z) {
    if (events == nullptr) {
        return;
    }
    events->EmitQueued(
        core::kOnBuildDemolished,
        core::BuildDemolishedEvent{playerId, std::string(defId), x, y, z});
}

inline void EmitBuildRepaired(core::EventBus* events, std::uint64_t playerId,
                              int x, int y, int z) {
    if (events == nullptr) {
        return;
    }
    events->EmitQueued(core::kOnBuildRepaired,
                       core::BuildRepairedEvent{playerId, x, y, z});
}

}  // namespace zl::game
