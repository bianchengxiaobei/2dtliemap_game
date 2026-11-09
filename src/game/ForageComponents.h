#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace zl::ecs {

inline constexpr std::size_t kMaxForageHarvests = 256;

struct ForageHarvest {
    std::string zoneId;
    int x = 0;
    int y = 0;
    int z = 0;
    double availableAtMinutes = 0.0;
};

struct ForageState {
    std::vector<ForageHarvest> harvested;
};

}  // namespace zl::ecs
