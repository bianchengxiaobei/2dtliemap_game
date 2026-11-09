#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zl::ecs {

enum class MissionRunStatus : std::uint8_t {
    Active = 0,
    Completed = 1,
    Failed = 2,
};

struct MissionRunState {
    std::string id;
    std::vector<int> progress;
    MissionRunStatus status = MissionRunStatus::Active;
    int day = 1;
    bool rewardClaimed = false;
};

struct MissionLog {
    std::vector<MissionRunState> missions;
    int lastDay = 1;
    int randomDay = 0;
};

}  // namespace zl::ecs
