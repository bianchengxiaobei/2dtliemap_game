#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include <doctest/doctest.h>

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "core/Config.h"
#include "ecs/EcsWorld.h"
#include "game/BarricadeDefense.h"
#include "game/Homestead.h"
#include "game/PlayerStorage.h"

using namespace zl;

TEST_CASE("M20-F4 farm plot supports plant water grow harvest loop") {
    ecs::FarmPlot plot;
    game::FarmConfig config;
    config.growMinutes = 120.0f;
    config.waterMinutes = 60.0f;
    config.maxWaterMinutes = 120.0f;
    config.harvestYield = 2;

    REQUIRE(game::PlantFarmPlot(plot, "cabbage", config.growMinutes,
                                config.harvestYield));
    CHECK(plot.stage == ecs::CropStage::Growing);
    CHECK_FALSE(game::HarvestFarmPlot(plot, nullptr));

    game::AdvanceFarmPlot(plot, 30.0f, config);
    CHECK(plot.stage == ecs::CropStage::Growing);
    CHECK(plot.dryMinutes == doctest::Approx(30.0f));

    REQUIRE(game::WaterFarmPlot(plot, 60.0f, config.maxWaterMinutes));
    game::AdvanceFarmPlot(plot, 60.0f, config);
    CHECK(plot.growthMinutes == doctest::Approx(60.0f));
    CHECK(plot.waterMinutes == doctest::Approx(0.0f));

    REQUIRE(game::WaterFarmPlot(plot, 60.0f, config.maxWaterMinutes));
    game::AdvanceFarmPlot(plot, 60.0f, config);
    CHECK(plot.stage == ecs::CropStage::Mature);
    int harvested = 0;
    REQUIRE(game::HarvestFarmPlot(plot, &harvested));
    CHECK(harvested == 2);
    CHECK(plot.stage == ecs::CropStage::Empty);
    CHECK(plot.cropId.empty());
}

TEST_CASE("M20-F4 farm plot withers without water") {
    ecs::FarmPlot plot;
    game::FarmConfig config;
    config.witherMinutes = 30.0f;
    REQUIRE(game::PlantFarmPlot(plot, "cabbage", 120.0f, 2));
    game::AdvanceFarmPlot(plot, 30.0f, config);
    CHECK(plot.stage == ecs::CropStage::Withered);
    CHECK_FALSE(game::WaterFarmPlot(plot, 60.0f, 120.0f));
    CHECK_FALSE(game::HarvestFarmPlot(plot, nullptr));
    CHECK(game::FarmStatusText(plot).find("枯萎") != std::string::npos);
}

TEST_CASE("M20-F4 generator fuel and cold storage radius") {
    ecs::Generator generator;
    generator.running = true;
    generator.fuelMinutes = 10.0f;
    generator.radius = 6.0f;
    CHECK(game::AdvanceGenerator(generator, 4.0f) == doctest::Approx(4.0f));
    CHECK(generator.fuelMinutes == doctest::Approx(6.0f));
    CHECK(game::IsColdStoragePowered(generator, 10.0f, 10.0f, 14.0f, 14.0f));
    CHECK_FALSE(
        game::IsColdStoragePowered(generator, 10.0f, 10.0f, 20.0f, 20.0f));
    game::AdvanceGenerator(generator, 20.0f);
    CHECK(generator.fuelMinutes == doctest::Approx(0.0f));
    CHECK_FALSE(generator.running);
    CHECK(game::GeneratorStatusText(generator).find("停电") !=
          std::string::npos);
}

TEST_CASE("M21-F2 power load scales fuel use and picks nearest source") {
    ecs::Generator generator;
    generator.running = true;
    generator.fuelMinutes = 10.0f;
    generator.load = 1.5f;
    CHECK(game::GeneratorEffectiveMinutes(generator, 1.0f) ==
          doctest::Approx(4.0f));
    CHECK(game::GeneratorStatusText(generator, 1.0f).find("负载 2.5") !=
          std::string::npos);
    CHECK(game::AdvanceGenerator(generator, 2.0f, 2.5f) ==
          doctest::Approx(5.0f));
    CHECK(generator.fuelMinutes == doctest::Approx(5.0f));

    const std::vector<game::PowerSource> sources{
        {0.0f, 0.0f, 4.0f}, {3.0f, 0.0f, 4.0f}};
    CHECK(game::FindPowerSourceFor(sources, 2.9f, 0.0f) == 1);
    CHECK(game::FindPowerSourceFor(sources, 10.0f, 0.0f) == -1);

    ecs::Floodlight light;
    CHECK(game::FloodlightStatusText(light, true, true).find("未供电") !=
          std::string::npos);
    light.powered = true;
    CHECK(game::FloodlightStatusText(light, true, true).find("照明中") !=
          std::string::npos);
    CHECK(game::FloodlightStatusText(light, false, true).find("待夜间") !=
          std::string::npos);
    light.on = false;
    CHECK(game::FloodlightStatusText(light, true, true).find("关闭") !=
          std::string::npos);
}

TEST_CASE("M21-F2 power config and floodlight spawn") {
    core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    const game::HomesteadConfig loaded = game::LoadHomesteadConfig(config);
    CHECK(loaded.power.baseLoad == doctest::Approx(1.0f));
    CHECK(loaded.power.coldStorageLoad == doctest::Approx(0.5f));
    CHECK(loaded.power.floodlightLoad == doctest::Approx(0.5f));
    CHECK(loaded.power.electricFenceLoad == doctest::Approx(1.0f));
    CHECK(loaded.power.floodlightRadius == doctest::Approx(6.0f));
    CHECK(loaded.power.attractRadius == doctest::Approx(18.0f));
    CHECK(loaded.power.electricFenceDamage == doctest::Approx(25.0f));
    CHECK(loaded.power.lowFuelWarningMinutes == doctest::Approx(10.0f));

    ecs::EcsWorld world;
    const ecs::Entity floodlight =
        game::SpawnFloodlight(world, 20.5f, 20.5f, 0);
    REQUIRE(static_cast<bool>(floodlight != entt::null));
    CHECK(world.Registry().all_of<ecs::Position, ecs::SpriteRef,
                                  ecs::Floodlight>(floodlight));
    const auto& light = world.Registry().get<ecs::Floodlight>(floodlight);
    CHECK(light.on);
    CHECK_FALSE(light.powered);
}

TEST_CASE("M21-F2b floodlight health, siege damage and legacy migration") {
    ecs::EcsWorld world;
    const ecs::Entity light =
        game::SpawnFloodlight(world, 10.5f, 10.5f, 0, 80.0f);
    REQUIRE(static_cast<bool>(light != entt::null));
    entt::registry& registry = world.Registry();
    const auto* health = registry.try_get<ecs::Health>(light);
    REQUIRE(health != nullptr);
    CHECK(health->current == doctest::Approx(80.0f));
    CHECK(health->max == doctest::Approx(80.0f));
    CHECK(registry.all_of<ecs::NoCombatTarget>(light));

    const ecs::Entity zombie = world.Create();
    registry.emplace<ecs::Position>(zombie, ecs::Position{11.0f, 10.5f, 0});
    registry.emplace<ecs::Health>(zombie, ecs::Health{100.0f, 100.0f});
    registry.emplace<ecs::BaseSiegeZombie>(zombie);
    auto& ai = registry.emplace<ecs::AiState>(zombie);
    ai.status = ecs::AiStatus::SearchLastKnown;

    std::unordered_map<std::uint64_t, float> cooldowns;
    const auto attack =
        game::TickZombieFloodlightAttacks(registry, cooldowns, 0.1f, 12.0f,
                                          1.0f, 28.0f, 1.2f);
    REQUIRE(attack.size() == 1);
    CHECK(attack.front().previousHp == doctest::Approx(80.0f));
    CHECK(attack.front().remainingHp == doctest::Approx(68.0f));
    CHECK_FALSE(attack.front().destroyed);

    CHECK(game::TickZombieFloodlightAttacks(registry, cooldowns, 0.5f, 12.0f,
                                            1.0f, 28.0f, 1.2f)
              .empty());
    CHECK(registry.get<ecs::Health>(light).current == doctest::Approx(68.0f));

    bool destroyed = false;
    for (int i = 0; i < 8 && !destroyed; ++i) {
        const auto hits = game::TickZombieFloodlightAttacks(
            registry, cooldowns, 1.1f, 12.0f, 1.0f, 28.0f, 1.2f);
        destroyed = !hits.empty() && hits.front().destroyed;
    }
    CHECK(destroyed);
    CHECK(registry.get<ecs::Health>(light).current == doctest::Approx(0.0f));
    if (destroyed) {
        world.Destroy(light);
        world.Flush();
    }
    CHECK_FALSE(world.Alive(light));

    const ecs::Entity legacy = world.Create();
    registry.emplace<ecs::Position>(legacy, ecs::Position{12.5f, 10.5f, 0});
    registry.emplace<ecs::Floodlight>(legacy, ecs::Floodlight{});
    game::EnsureFloodlightHealth(world, 80.0f);
    REQUIRE(registry.try_get<ecs::Health>(legacy) != nullptr);
    CHECK(registry.get<ecs::Health>(legacy).current == doctest::Approx(80.0f));
    CHECK(registry.all_of<ecs::NoCombatTarget>(legacy));
}

TEST_CASE("M20-F4 spike trap consumes limited uses with cooldown") {
    ecs::SpikeTrap trap;
    trap.uses = 2;
    trap.cooldown = 0.0f;
    CHECK(game::TrapCanTrigger(trap));
    CHECK(game::ConsumeTrapUse(trap));
    CHECK(trap.uses == 1);
    CHECK_FALSE(game::TrapCanTrigger(trap));
    game::AdvanceTrap(trap, 5.0f);
    CHECK(game::TrapCanTrigger(trap));
    CHECK(game::ConsumeTrapUse(trap));
    CHECK(trap.uses == 0);
    CHECK_FALSE(trap.armed);
    CHECK_FALSE(game::TrapCanTrigger(trap));
}

TEST_CASE("M20-F4 merchant offers are stable barter definitions") {
    const auto& offers = game::MerchantOffers();
    REQUIRE(offers.size() == game::kMerchantOfferCount);
    CHECK(offers[0].id == "bandage");
    CHECK(offers[0].costDefId == "wood");
    CHECK(offers[0].costCount == 2);
    CHECK(offers[0].rewardCount == 2);
    CHECK(offers[2].blueprint == "campfire_beans");
    CHECK(offers[3].id == "ammo_9mm");
    CHECK(offers[3].rewardDefId == "ammo_9mm");
    CHECK(offers[3].rewardCount == 12);
    CHECK(offers[3].costDefId == "wood");
    CHECK(offers[3].costCount == 2);
    CHECK(offers[3].maxStock == 2);
    CHECK(offers[4].id == "pistol");
    CHECK(offers[4].rewardDefId == "pistol");
    CHECK(offers[4].rewardCount == 1);
    CHECK(offers[4].costDefId == "wood");
    CHECK(offers[4].costCount == 8);
    CHECK(offers[4].maxStock == 1);
    for (const game::TradeOffer& offer : offers) {
        CHECK(offer.maxStock > 0);
        CHECK(offer.costCount > 0);
        CHECK(!offer.costDefId.empty());
        const bool hasReward =
            !offer.rewardDefId.empty() || !offer.blueprint.empty();
        CHECK(hasReward);
    }

    ecs::MerchantStock legacy;
    legacy.stock = {99, 99, 99, 4, 2};
    game::ClampMerchantStock(legacy);
    CHECK((legacy.stock == std::array<int, 5>{4, 2, 1, 2, 1}));
}

TEST_CASE("M20-F4 homestead config loads and clamps") {
    core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    const game::HomesteadConfig loaded = game::LoadHomesteadConfig(config);
    CHECK(loaded.farm.enabled);
    CHECK(loaded.farm.growMinutes == doctest::Approx(240.0f));
    CHECK(loaded.farm.waterMinutes == doctest::Approx(360.0f));
    CHECK(loaded.farm.harvestYield == 2);
    CHECK(loaded.farm.seedYield == 1);
    CHECK(loaded.power.enabled);
    CHECK(loaded.power.fuelMinutesPerCan == doctest::Approx(720.0f));
    CHECK(loaded.trap.enabled);
    CHECK(loaded.trap.uses == 6);
    CHECK(loaded.trade.enabled);
    CHECK(loaded.trade.merchantX == doctest::Approx(68.5f));
}

TEST_CASE("M20-F4 spawn helpers create persistent facility components") {
    ecs::EcsWorld world;
    const game::FarmConfig farm;
    const game::PowerConfig power;
    const game::TrapConfig trap;

    const ecs::Entity plot = game::SpawnFarmPlot(world, 10.5f, 10.5f, 0, farm);
    REQUIRE(static_cast<bool>(plot != entt::null));
    CHECK(world.Registry().all_of<ecs::Position, ecs::SpriteRef,
                                  ecs::FarmPlot>(plot));

    const ecs::Entity generator =
        game::SpawnGenerator(world, 11.5f, 10.5f, 0, power);
    REQUIRE(static_cast<bool>(generator != entt::null));
    CHECK(world.Registry().all_of<ecs::Generator>(generator));

    const ecs::Entity fridge = game::SpawnColdStorage(
        world, 12.5f, 10.5f, 0, game::kColdStorageSprite, 30.0f, 4);
    REQUIRE(static_cast<bool>(fridge != entt::null));
    CHECK(world.Registry().all_of<ecs::PlayerStorage, ecs::Container,
                                  ecs::ColdStorage>(fridge));

    const ecs::Entity spikeTrap =
        game::SpawnSpikeTrap(world, 13.5f, 10.5f, 0, trap);
    REQUIRE(static_cast<bool>(spikeTrap != entt::null));
    CHECK(world.Registry().all_of<ecs::SpikeTrap>(spikeTrap));

    const ecs::Entity merchant = game::SpawnMerchant(world, 14.5f, 10.5f, 0);
    REQUIRE(static_cast<bool>(merchant != entt::null));
    CHECK(world.Registry().all_of<ecs::MerchantStock>(merchant));
}

TEST_CASE("F5-T2 farm seasons, rain irrigation, fertilizer and disease") {
    game::FarmConfig config;
    config.growMinutes = 120.0f;
    config.waterMinutes = 100.0f;
    config.maxWaterMinutes = 1000.0f;
    config.witherMinutes = 60.0f;
    config.diseaseDryFraction = 0.4f;
    config.diseaseChance = 1.0f;
    config.diseaseYieldPenalty = 0.5f;
    config.fertilizerYieldBonus = 1;
    config.rainWaterPerMinute = 1.0f;
    config.seasonGrowthScale = {1.0f, 1.0f, 1.0f, 0.25f};

    ecs::FarmPlot winter;
    REQUIRE(game::PlantFarmPlot(winter, "cabbage", 120.0f, 2));
    REQUIRE(game::WaterFarmPlot(winter, 100.0f, 1000.0f));
    game::AdvanceFarmPlot(winter, 60.0f, config, nullptr,
                          game::FarmSeason::Winter);
    CHECK(winter.growthMinutes == doctest::Approx(15.0f));

    ecs::FarmPlot summer;
    REQUIRE(game::PlantFarmPlot(summer, "cabbage", 120.0f, 2));
    REQUIRE(game::WaterFarmPlot(summer, 100.0f, 1000.0f));
    game::AdvanceFarmPlot(summer, 60.0f, config, nullptr,
                          game::FarmSeason::Summer);
    CHECK(summer.growthMinutes == doctest::Approx(60.0f));

    ecs::FarmPlot rain;
    REQUIRE(game::PlantFarmPlot(rain, "cabbage", 120.0f, 2));
    game::AdvanceFarmPlot(rain, 30.0f, config, nullptr,
                          game::FarmSeason::Summer, true);
    CHECK(rain.waterMinutes == doctest::Approx(0.0f));
    CHECK(rain.growthMinutes == doctest::Approx(30.0f));

    REQUIRE(game::FertilizeFarmPlot(summer, 1));
    CHECK(summer.fertilized);
    CHECK(summer.harvestYield == 3);
    CHECK_FALSE(game::FertilizeFarmPlot(summer, 1));

    ecs::FarmPlot diseased;
    REQUIRE(game::PlantFarmPlot(diseased, "cabbage", 120.0f, 3));
    game::AdvanceFarmPlot(diseased, 18.0f, config, nullptr,
                          game::FarmSeason::Summer);
    CHECK(diseased.diseased);
    CHECK(diseased.harvestYield == 2);
    CHECK(game::FarmStatusText(diseased, nullptr, "夏季").find("病害") !=
          std::string::npos);
    CHECK_FALSE(game::FertilizeFarmPlot(diseased, 1));
    REQUIRE(game::CureFarmDisease(diseased));
    CHECK_FALSE(diseased.diseased);
    CHECK_FALSE(game::CureFarmDisease(diseased));
}

TEST_CASE("F5-T2 farm season names and config clamping") {
    CHECK(game::FarmSeasonFromMonth(3) == game::FarmSeason::Spring);
    CHECK(game::FarmSeasonFromMonth(7) == game::FarmSeason::Summer);
    CHECK(game::FarmSeasonFromMonth(10) == game::FarmSeason::Autumn);
    CHECK(game::FarmSeasonFromMonth(12) == game::FarmSeason::Winter);
    CHECK(std::string(game::FarmSeasonName(game::FarmSeason::Winter)) ==
          "冬季");

    game::FarmConfig config;
    config.seasonGrowthScale = {4.0f, 0.0f, -1.0f, 0.25f};
    CHECK(game::FarmSeasonGrowthScale(config, game::FarmSeason::Spring) ==
          doctest::Approx(4.0f));
    CHECK(game::FarmSeasonGrowthScale(config, game::FarmSeason::Summer) ==
          doctest::Approx(0.05f));
    CHECK(game::FarmSeasonGrowthScale(config, game::FarmSeason::Autumn) ==
          doctest::Approx(0.05f));
}
