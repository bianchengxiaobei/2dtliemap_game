#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace zl::ecs {

enum class CropStage : std::uint8_t {
    Empty = 0,
    Growing = 1,
    Mature = 2,
    Withered = 3,
};

struct FarmPlot {
    std::string cropId;
    float growthMinutes = 0.0f;
    float waterMinutes = 0.0f;
    float dryMinutes = 0.0f;
    float growMinutes = 240.0f;
    CropStage stage = CropStage::Empty;
    int harvestYield = 2;
    bool diseased = false;
    bool fertilized = false;
};

struct Generator {
    float fuelMinutes = 0.0f;
    float radius = 8.0f;
    bool running = false;
    float load = 0.0f;
};

struct ColdStorage {
    bool powered = false;
};

struct Floodlight {
    bool on = true;
    bool powered = false;
};

struct SpikeTrap {
    bool armed = true;
    float cooldown = 0.0f;
    float cooldownSeconds = 1.5f;
    float damage = 45.0f;
    float radius = 1.0f;
    float staggerSeconds = 0.35f;
    int uses = 6;
};

struct PlacedTrap {
    std::string defId;
    bool armed = false;
    float cooldown = 0.0f;
    int uses = 0;
    std::uint64_t owner = 0;
    float armRemaining = 0.0f;
};

struct MerchantStock {
    std::array<int, 5> stock{{4, 2, 1, 2, 1}};
    int restockDay = 0;
};

}  // namespace zl::ecs
