#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ecs/EcsWorld.h"
#include "game/HomesteadComponents.h"

namespace zl::core {
class Config;
}

namespace zl::items {
class ItemRegistry;
}

namespace zl::game {

struct CropDef;

inline constexpr std::size_t kMerchantOfferCount = 5;
inline constexpr const char* kFarmPlotSprite = "object.farm_plot";
inline constexpr const char* kGeneratorSprite = "object.generator";
inline constexpr const char* kColdStorageSprite = "object.fridge";
inline constexpr const char* kSpikeTrapSprite = "object.trap";
inline constexpr const char* kFloodlightSprite = "object.floodlight";
inline constexpr const char* kElectricFenceSprite = "object.electric_fence";

inline constexpr std::uint16_t kFarmActionPlant = 1u << 0;
inline constexpr std::uint16_t kFarmActionCare = 1u << 1;
inline constexpr std::uint16_t kFarmActionMask =
    kFarmActionPlant | kFarmActionCare;

enum class FarmSeason : std::uint8_t {
    Spring = 0,
    Summer = 1,
    Autumn = 2,
    Winter = 3,
};

struct FarmConfig {
    bool enabled = true;
    float growMinutes = 240.0f;
    float waterMinutes = 360.0f;
    float maxWaterMinutes = 720.0f;
    float witherMinutes = 1440.0f;
    int harvestYield = 2;
    int seedYield = 1;
    std::string seedItem = "cabbage_seeds";
    std::string produceItem = "fresh_cabbage";
    std::string waterItem = "water_bottle";
    std::string fertilizerItem = "fertilizer";
    std::string fungicideItem = "fungicide";
    int fertilizerYieldBonus = 1;
    float diseaseChance = 0.05f;
    float diseaseDryFraction = 0.45f;
    float diseaseYieldPenalty = 0.5f;
    float rainWaterPerMinute = 0.5f;
    std::array<float, 4> seasonGrowthScale{{1.0f, 1.0f, 0.9f, 0.25f}};
    std::array<float, 4> seasonDiseaseScale{{1.0f, 1.2f, 0.9f, 0.6f}};
};

struct PowerConfig {
    bool enabled = true;
    float fuelMinutesPerCan = 720.0f;
    float radius = 8.0f;
    std::string fuelItem = "fuel_can";
    float baseLoad = 1.0f;
    float coldStorageLoad = 0.5f;
    float floodlightLoad = 0.5f;
    float electricFenceLoad = 1.0f;
    float floodlightRadius = 6.0f;
    float attractRadius = 18.0f;
    float electricFenceDamage = 25.0f;
    float electricFenceCooldownSeconds = 1.2f;
    float electricFenceStaggerSeconds = 0.5f;
    float lowFuelWarningMinutes = 10.0f;
    bool deviceRequiresNight = true;
};

struct TrapConfig {
    bool enabled = true;
    float damage = 45.0f;
    float radius = 1.0f;
    float cooldownSeconds = 1.5f;
    float staggerSeconds = 0.35f;
    int uses = 6;
};

struct TradeConfig {
    bool enabled = true;
    float range = 3.5f;
    float merchantX = 68.5f;
    float merchantY = 26.5f;
    int restockDays = 1;
};

struct HomesteadConfig {
    FarmConfig farm;
    PowerConfig power;
    TrapConfig trap;
    TradeConfig trade;
};

struct TradeOffer {
    std::string_view id;
    std::string_view name;
    std::string_view costDefId;
    int costCount = 0;
    std::string_view rewardDefId;
    int rewardCount = 0;
    std::string_view blueprint;
    int maxStock = 0;
};

struct PowerSource {
    float x = 0.0f;
    float y = 0.0f;
    float radius = 0.0f;
};

int FindPowerSourceFor(const std::vector<PowerSource>& sources, float x,
                       float y);

const std::array<TradeOffer, kMerchantOfferCount>& MerchantOffers();
void ClampMerchantStock(ecs::MerchantStock& stock);

FarmSeason FarmSeasonFromMonth(int month);
const char* FarmSeasonName(FarmSeason season);
float FarmSeasonGrowthScale(const FarmConfig& config, FarmSeason season);
float FarmSeasonDiseaseScale(const FarmConfig& config, FarmSeason season);

const char* CropStageName(ecs::CropStage stage);
std::string FarmStatusText(const ecs::FarmPlot& plot,
                           const CropDef* crop = nullptr,
                           const char* seasonName = nullptr);
bool PlantFarmPlot(ecs::FarmPlot& plot, std::string_view cropId,
                   float growMinutes, int harvestYield);
bool PlantFarmPlot(ecs::FarmPlot& plot, const CropDef& crop);
bool WaterFarmPlot(ecs::FarmPlot& plot, float waterMinutes,
                   float maxWaterMinutes);
bool FertilizeFarmPlot(ecs::FarmPlot& plot, int yieldBonus);
bool CureFarmDisease(ecs::FarmPlot& plot);
bool HarvestFarmPlot(ecs::FarmPlot& plot, int* harvested);
void AdvanceFarmPlot(ecs::FarmPlot& plot, float gameMinutes,
                     const FarmConfig& config, const CropDef* crop = nullptr,
                     FarmSeason season = FarmSeason::Summer,
                     bool raining = false);

float AdvanceGenerator(ecs::Generator& generator, float gameMinutes,
                       float loadPerMinute = 1.0f);
float GeneratorEffectiveMinutes(const ecs::Generator& generator,
                                float baseLoad = 1.0f);
std::string GeneratorStatusText(const ecs::Generator& generator,
                                float baseLoad = 1.0f);
std::string FloodlightStatusText(const ecs::Floodlight& floodlight,
                                 bool night, bool requiresNight);

bool IsColdStoragePowered(const ecs::Generator& generator, float gx, float gy,
                          float storageX, float storageY);

bool TrapCanTrigger(const ecs::SpikeTrap& trap);
void AdvanceTrap(ecs::SpikeTrap& trap, float seconds);
bool ConsumeTrapUse(ecs::SpikeTrap& trap);

HomesteadConfig LoadHomesteadConfig(const core::Config& config);

ecs::Entity SpawnFarmPlot(ecs::EcsWorld& world, float x, float y, int z,
                          const FarmConfig& config);
ecs::Entity SpawnGenerator(ecs::EcsWorld& world, float x, float y, int z,
                           const PowerConfig& config);
ecs::Entity SpawnColdStorage(ecs::EcsWorld& world, float x, float y, int z,
                             std::string_view sprite, float capacityKg,
                             int maxStacks);
ecs::Entity SpawnFloodlight(ecs::EcsWorld& world, float x, float y, int z,
                            float maxHp = 80.0f);
void EnsureFloodlightHealth(ecs::EcsWorld& world, float maxHp = 80.0f);
ecs::Entity SpawnSpikeTrap(ecs::EcsWorld& world, float x, float y, int z,
                           const TrapConfig& config);
ecs::Entity SpawnMerchant(ecs::EcsWorld& world, float x, float y, int z);

}  // namespace zl::game
