#include <doctest/doctest.h>

#include <filesystem>
#include <initializer_list>
#include <string>

#include "core/Config.h"
#include "game/VehicleComponents.h"
#include "game/VehicleMods.h"
#include "game/VehicleSpike.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"

using namespace zl;

namespace {

game::VehicleModsConfig LoadVehicleMods(items::ItemRegistry* items) {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(items->LoadFile(root / "assets" / "items.json"));
    core::Config config;
    REQUIRE(config.LoadFile(root / "config" / "game.json"));
    config.Set("vehicleMods.file",
               (root / "assets" / "vehicle_mods.json").generic_string());
    game::VehicleModsConfig out;
    std::string error;
    REQUIRE_MESSAGE(game::LoadVehicleModConfig(config, &out, &error, items),
                    error);
    return out;
}

ecs::VehicleMods ModsWith(std::initializer_list<std::string> ids) {
    ecs::VehicleMods mods;
    for (const std::string& id : ids) {
        if (id == "vehicle_armor_plate") {
            mods.defIds[0] = id;
        } else if (id == "vehicle_storage_rack") {
            mods.defIds[1] = id;
        } else if (id == "vehicle_ram_plow") {
            mods.defIds[2] = id;
        } else if (id == "vehicle_searchlight") {
            mods.defIds[3] = id;
        } else if (id == "vehicle_engine_tune") {
            mods.defIds[4] = id;
        }
    }
    return mods;
}

void StepOpen(game::VehicleMotion* motion, const game::VehicleModStats& stats,
              float seconds) {
    const auto open = [](int, int, int) { return false; };
    const int frames = static_cast<int>(seconds * 60.0f);
    for (int i = 0; i < frames; ++i) {
        (void)game::VehicleStep(
            motion, 1.0f, 0.0f, 1.0f / 60.0f, open,
            game::kVehicleDefaultTuning, game::VehicleSurface::Paved, {},
            stats);
    }
}

}  // namespace

TEST_CASE("M33-F4-T3 改装效果聚合、重量与 clamps") {
    items::ItemRegistry items;
    const game::VehicleModsConfig config = LoadVehicleMods(&items);
    CHECK(config.weightAccelerationKg == doctest::Approx(400.0f));
    CHECK(config.weightFuelKg == doctest::Approx(600.0f));

    const game::VehicleModStats empty = game::VehicleModStatsFor(config, nullptr);
    CHECK(empty.totalWeightKg == doctest::Approx(0.0f));
    CHECK(game::VehicleModMaxSpeedScale(empty) == doctest::Approx(1.0f));
    CHECK(game::VehicleModAccelerationScale(empty) == doctest::Approx(1.0f));
    CHECK(game::VehicleModFuelUseScale(empty) == doctest::Approx(1.0f));
    CHECK(game::VehicleModTrunkCapacityKg(empty, 40.0f) ==
          doctest::Approx(40.0f));

    const ecs::VehicleMods armorMods = ModsWith({"vehicle_armor_plate"});
    const game::VehicleModStats armor =
        game::VehicleModStatsFor(config, armorMods);
    CHECK(armor.totalWeightKg == doctest::Approx(28.0f));
    CHECK(game::VehicleModCrashWearScale(armor) == doctest::Approx(0.68f));
    CHECK(game::VehicleModZombieWearScale(armor) == doctest::Approx(0.72f));
    CHECK(game::VehicleModAccelerationScale(armor) < 1.0f);
    CHECK(game::VehicleModFuelUseScale(armor) > 1.07f);

    const ecs::VehicleMods all = ModsWith(
        {"vehicle_armor_plate", "vehicle_storage_rack", "vehicle_ram_plow",
         "vehicle_searchlight", "vehicle_engine_tune"});
    const game::VehicleModStats stats = game::VehicleModStatsFor(config, all);
    CHECK(stats.totalWeightKg == doctest::Approx(86.0f));
    CHECK(game::VehicleModMaxSpeedScale(stats) == doctest::Approx(1.18f));
    CHECK(game::VehicleModAccelerationScale(stats) < 1.0f);
    CHECK(game::VehicleModFuelUseScale(stats) > 1.4f);
    CHECK(game::VehicleModRamDamageScale(stats) == doctest::Approx(1.45f));
    CHECK(game::VehicleModHeadlightRangeScale(stats) ==
          doctest::Approx(1.45f));
    CHECK(game::VehicleModHeadlightBrightnessScale(stats) ==
          doctest::Approx(1.25f));
    CHECK(game::VehicleModTrunkCapacityKg(stats, 40.0f) ==
          doctest::Approx(64.0f));
    CHECK(game::VehicleModTrunkStacks(stats, 6) == 8);
    CHECK(game::VehicleModCrashWearScale(stats) == doctest::Approx(0.748f));
    CHECK(game::VehicleModRamWearScale(stats) == doctest::Approx(0.715f));
    CHECK(game::VehicleModZombieWearScale(stats) == doctest::Approx(0.792f));
}

TEST_CASE("M33-F4-T3 发动机极速、加速与重量油耗") {
    items::ItemRegistry items;
    const game::VehicleModsConfig config = LoadVehicleMods(&items);
    const game::VehicleModStats empty = game::VehicleModStatsFor(config, nullptr);
    const game::VehicleModStats engine = game::VehicleModStatsFor(
        config, ModsWith({"vehicle_engine_tune"}));
    const game::VehicleModStats all =
        game::VehicleModStatsFor(config,
                                 ModsWith({"vehicle_armor_plate",
                                           "vehicle_storage_rack",
                                           "vehicle_ram_plow",
                                           "vehicle_searchlight",
                                           "vehicle_engine_tune"}));

    game::VehicleMotion baseline;
    baseline.x = 1.0f;
    baseline.y = 1.0f;
    game::VehicleMotion tuned = baseline;
    game::VehicleMotion heavy = baseline;
    StepOpen(&baseline, empty, 2.5f);
    StepOpen(&tuned, engine, 2.5f);
    StepOpen(&heavy, all, 2.5f);

    CHECK(game::VehicleSpeed(baseline) ==
          doctest::Approx(game::kVehicleMaxSpeed));
    CHECK(game::VehicleSpeed(tuned) > game::VehicleSpeed(baseline) + 1.0f);
    CHECK(game::VehicleSpeed(heavy) > game::VehicleSpeed(baseline));
    CHECK(tuned.fuel < baseline.fuel);
    CHECK(heavy.fuel < baseline.fuel);

    game::VehicleMotion baselineBurst;
    game::VehicleMotion engineBurst;
    game::VehicleMotion heavyBurst;
    baselineBurst.x = 1.0f;
    engineBurst.x = 1.0f;
    heavyBurst.x = 1.0f;
    StepOpen(&baselineBurst, empty, 0.25f);
    StepOpen(&engineBurst, engine, 0.25f);
    StepOpen(&heavyBurst, all, 0.25f);
    CHECK(game::VehicleSpeed(engineBurst) >
          game::VehicleSpeed(baselineBurst));
    CHECK(game::VehicleSpeed(heavyBurst) <
          game::VehicleSpeed(baselineBurst));
}

TEST_CASE("M33-F4-T3 装甲碰撞减伤、犁撞伤害与车况磨损") {
    items::ItemRegistry items;
    const game::VehicleModsConfig config = LoadVehicleMods(&items);
    const game::VehicleModStats empty = game::VehicleModStatsFor(config, nullptr);
    const game::VehicleModStats armor =
        game::VehicleModStatsFor(config, ModsWith({"vehicle_armor_plate"}));
    const game::VehicleModStats plow =
        game::VehicleModStatsFor(config, ModsWith({"vehicle_ram_plow"}));
    const game::VehicleModStats engine =
        game::VehicleModStatsFor(config, ModsWith({"vehicle_engine_tune"}));

    const auto closed = [](int, int, int) { return true; };
    game::VehicleMotion baseline;
    baseline.x = 0.5f;
    baseline.y = 0.5f;
    baseline.vx = 10.0f;
    game::VehicleMotion armored = baseline;
    const game::VehicleStepResult baselineStep = game::VehicleStep(
        &baseline, 0.0f, 0.0f, 1.0f / 60.0f, closed,
        game::kVehicleDefaultTuning, game::VehicleSurface::Paved, {}, empty);
    const game::VehicleStepResult armoredStep = game::VehicleStep(
        &armored, 0.0f, 0.0f, 1.0f / 60.0f, closed,
        game::kVehicleDefaultTuning, game::VehicleSurface::Paved, {}, armor);
    REQUIRE(baselineStep.damaged);
    REQUIRE(armoredStep.damaged);
    CHECK(baselineStep.damage == doctest::Approx(0.5413f).epsilon(0.02));
    CHECK(armoredStep.damage ==
          doctest::Approx(baselineStep.damage * 0.68f).epsilon(0.02));

    const float baseRam = game::VehicleRamDamage(
        10.0f, game::kVehicleDefaultTuning, 1.0f);
    const float plowRam = game::VehicleRamDamage(
        10.0f, game::kVehicleDefaultTuning,
        game::VehicleModRamDamageScale(plow));
    CHECK(plowRam == doctest::Approx(baseRam * 1.45f));

    game::VehicleMotion ramBase;
    game::VehicleMotion ramPlow;
    game::VehicleMotion ramEngine;
    CHECK(game::VehicleApplyRamConditionDamage(
              &ramBase, 10.0f, game::kVehicleDefaultTuning,
              game::VehicleModRamWearScale(empty)) ==
          doctest::Approx(0.079f));
    CHECK(game::VehicleApplyRamConditionDamage(
              &ramPlow, 10.0f, game::kVehicleDefaultTuning,
              game::VehicleModRamWearScale(plow)) ==
          doctest::Approx(0.079f * 0.65f));
    CHECK(game::VehicleApplyRamConditionDamage(
              &ramEngine, 10.0f, game::kVehicleDefaultTuning,
              game::VehicleModRamWearScale(engine)) ==
          doctest::Approx(0.079f * 1.10f));

    game::VehicleMotion zombieBase;
    game::VehicleMotion zombieArmor;
    CHECK(game::VehicleApplyZombieAttackConditionDamage(
              &zombieBase, game::kVehicleDefaultTuning, 1.0f) ==
          doctest::Approx(0.015f));
    CHECK(game::VehicleApplyZombieAttackConditionDamage(
              &zombieArmor, game::kVehicleDefaultTuning,
              game::VehicleModZombieWearScale(armor)) ==
          doctest::Approx(0.015f * 0.72f));
}

TEST_CASE("M33-F4-T3 行李架容量与探照灯范围") {
    items::ItemRegistry items;
    const game::VehicleModsConfig config = LoadVehicleMods(&items);
    const game::VehicleModStats rack =
        game::VehicleModStatsFor(config, ModsWith({"vehicle_storage_rack"}));
    const game::VehicleModStats light =
        game::VehicleModStatsFor(config, ModsWith({"vehicle_searchlight"}));
    const game::VehicleModStats empty = game::VehicleModStatsFor(config, nullptr);

    CHECK(game::VehicleModTrunkCapacityKg(empty, 40.0f) ==
          doctest::Approx(40.0f));
    CHECK(game::VehicleModTrunkCapacityKg(rack, 40.0f) ==
          doctest::Approx(64.0f));
    CHECK(game::VehicleModTrunkStacks(empty, 6) == 6);
    CHECK(game::VehicleModTrunkStacks(rack, 6) == 8);

    ecs::Container container;
    container.capacityKg = 40.0f;
    CHECK(game::ApplyVehicleModTrunkCapacity(rack, 40.0f, &container));
    CHECK(container.capacityKg == doctest::Approx(64.0f));
    CHECK_FALSE(game::ApplyVehicleModTrunkCapacity(rack, 40.0f, &container));
    CHECK(game::ApplyVehicleModTrunkCapacity(empty, 40.0f, &container));
    CHECK(container.capacityKg == doctest::Approx(40.0f));

    CHECK(game::VehicleModHeadlightRangeScale(light) ==
          doctest::Approx(1.45f));
    CHECK(game::VehicleModHeadlightBrightnessScale(light) ==
          doctest::Approx(1.25f));
    CHECK(game::VehicleModHeadlightRangeScale(empty) ==
          doctest::Approx(1.0f));
    CHECK(game::VehicleModHeadlightBrightnessScale(empty) ==
          doctest::Approx(1.0f));
}

TEST_CASE("M33-F4-T3 无改装驾驶基线保持 M25 行为") {
    items::ItemRegistry items;
    const game::VehicleModsConfig config = LoadVehicleMods(&items);
    const game::VehicleModStats empty = game::VehicleModStatsFor(config, nullptr);

    game::VehicleMotion defaultTuning;
    defaultTuning.x = 1.0f;
    game::VehicleMotion emptyStats;
    emptyStats.x = 1.0f;
    StepOpen(&defaultTuning, empty, 2.0f);
    StepOpen(&emptyStats, game::VehicleModStats{}, 2.0f);
    CHECK(defaultTuning.x == doctest::Approx(emptyStats.x));
    CHECK(defaultTuning.fuel == doctest::Approx(emptyStats.fuel));
    CHECK(game::VehicleModCrashWearScale(empty) == doctest::Approx(1.0f));
    CHECK(game::VehicleModRamWearScale(empty) == doctest::Approx(1.0f));
    CHECK(game::VehicleModZombieWearScale(empty) == doctest::Approx(1.0f));
}
