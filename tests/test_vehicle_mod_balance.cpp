#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <map>
#include <string>

#include "core/Config.h"
#include "game/VehicleComponents.h"
#include "game/VehicleMods.h"
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

bool HasBenefit(const game::VehicleModDef& def) {
    const game::VehicleModEffects& effects = def.effects;
    return effects.crashConditionScale < 1.0f ||
           effects.zombieConditionScale < 1.0f ||
           effects.ramConditionScale < 1.0f ||
           effects.ramDamageScale > 1.0f ||
           effects.trunkCapacityKg > 0.0f ||
           effects.headlightRangeScale > 1.0f ||
           effects.headlightBrightnessScale > 1.0f ||
           effects.maxSpeedScale > 1.0f ||
           effects.accelerationScale > 1.0f;
}

bool HasCost(const game::VehicleModDef& def) {
    const game::VehicleModEffects& effects = def.effects;
    return def.weightKg > 0.0f || effects.fuelUseScale > 1.0f ||
           effects.conditionWearScale > 1.0f;
}

}  // namespace

TEST_CASE("M33-F4-T6 五个改装槽位均有收益与代价") {
    items::ItemRegistry items;
    const game::VehicleModsConfig config = LoadVehicleMods(&items);
    REQUIRE(config.defs.size() == 5u);

    std::array<int, ecs::kVehicleModSlotCount> perSlot{};
    int explicitCosts = 0;
    int heavyCosts = 0;
    for (const game::VehicleModDef& def : config.defs) {
        CHECK_FALSE(def.name.empty());
        CHECK_FALSE(def.description.empty());
        CHECK(HasBenefit(def));
        CHECK(HasCost(def));
        CHECK(def.installSeconds > 0.0f);
        REQUIRE_FALSE(def.materials.empty());
        for (const game::VehicleModMaterial& material : def.materials) {
            CHECK(items.Find(material.item) != nullptr);
            CHECK(material.count > 0);
        }
        ++perSlot[static_cast<std::size_t>(def.slot)];
        if (def.effects.fuelUseScale > 1.0f ||
            def.effects.conditionWearScale > 1.0f) {
            ++explicitCosts;
        }
        if (def.weightKg >= 20.0f) {
            ++heavyCosts;
        }
    }
    for (int count : perSlot) {
        CHECK(count == 1);
    }
    CHECK(explicitCosts >= 2);
    CHECK(heavyCosts >= 2);
}

TEST_CASE("M33-F4-T6 全套改装总账、材料与 UI 摘要") {
    items::ItemRegistry items;
    const game::VehicleModsConfig config = LoadVehicleMods(&items);
    ecs::VehicleMods all;
    std::string error;
    for (const char* id : {"vehicle_armor_plate", "vehicle_storage_rack",
                           "vehicle_ram_plow", "vehicle_searchlight",
                           "vehicle_engine_tune"}) {
        REQUIRE_MESSAGE(game::InstallVehicleMod(config, &all, id, &error),
                        error);
    }

    const game::VehicleModStats stats = game::VehicleModStatsFor(config, all);
    CHECK(stats.totalWeightKg == doctest::Approx(86.0f));
    CHECK(game::VehicleModMaxSpeedScale(stats) == doctest::Approx(1.18f));
    CHECK(game::VehicleModAccelerationScale(stats) ==
          doctest::Approx(0.9218107f).epsilon(0.0001));
    CHECK(game::VehicleModFuelUseScale(stats) ==
          doctest::Approx(1.5519623f).epsilon(0.0001));
    CHECK(game::VehicleModCrashWearScale(stats) == doctest::Approx(0.748f));
    CHECK(game::VehicleModZombieWearScale(stats) == doctest::Approx(0.792f));
    CHECK(game::VehicleModRamWearScale(stats) == doctest::Approx(0.715f));
    CHECK(game::VehicleModRamDamageScale(stats) == doctest::Approx(1.45f));
    CHECK(game::VehicleModHeadlightRangeScale(stats) ==
          doctest::Approx(1.45f));
    CHECK(game::VehicleModHeadlightBrightnessScale(stats) ==
          doctest::Approx(1.25f));
    CHECK(game::VehicleModTrunkCapacityKg(stats, 40.0f) ==
          doctest::Approx(64.0f));
    CHECK(game::VehicleModTrunkStacks(stats, 6) == 8);

    CHECK(game::VehicleModAccelerationScale(stats) < 0.95f);
    CHECK(game::VehicleModFuelUseScale(stats) > 1.5f);
    CHECK(game::VehicleModFuelUseScale(stats) < 2.0f);
    CHECK(game::VehicleModCrashWearScale(stats) > 0.5f);
    CHECK(game::VehicleModZombieWearScale(stats) > 0.5f);
    CHECK(game::VehicleModRamWearScale(stats) > 0.5f);

    const std::string summary = game::VehicleModStatsSummary(stats, 40.0f);
    CHECK(summary.find("总重 86kg") != std::string::npos);
    CHECK(summary.find("后备箱 64kg（+24kg）") != std::string::npos);
    CHECK(summary.find("极速 +18%") != std::string::npos);
    CHECK(summary.find("加速 -8%") != std::string::npos);
    CHECK(summary.find("油耗 +55%") != std::string::npos);
    CHECK(summary.find("撞墙 -25%") != std::string::npos);
    CHECK(summary.find("撞尸 -21%") != std::string::npos);
    CHECK(summary.find("撞击 +45%") != std::string::npos);
    CHECK(summary.find("车灯 +45%") != std::string::npos);

    std::map<std::string, int> totals;
    float installSeconds = 0.0f;
    for (const game::VehicleModDef& def : config.defs) {
        installSeconds += def.installSeconds;
        for (const game::VehicleModMaterial& material : def.materials) {
            totals[material.item] += material.count;
        }
    }
    CHECK(totals.size() == 4u);
    CHECK(totals["repair_kit"] == 8);
    CHECK(totals["wood"] == 14);
    CHECK(totals["radio_part"] == 2);
    CHECK(totals["screwdriver"] == 1);
    CHECK(installSeconds == doctest::Approx(145.0f));
}
