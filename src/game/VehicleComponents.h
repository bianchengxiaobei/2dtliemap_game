#pragma once

#include <cstdint>

namespace zl::ecs {

enum class VehicleType : std::uint8_t {
    LightPickup = 0,
    Pickup = 1,
    HeavyPickup = 2,
};

struct Vehicle {
    float condition = 1.0f;
    float fuel = 1.0f;
    bool headlights = false;
    VehicleType type = VehicleType::Pickup;
    float heading = 1.5707963267948966f;
};

struct VehicleDriver {
    std::uint64_t stableId = 0;
};

struct VehiclePassenger {
    std::uint64_t stableId = 0;
    std::uint64_t secondStableId = 0;
};

}  // namespace zl::ecs
