#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "core/Config.h"
#include "game/VehicleComponents.h"
#include "game/VehicleMods.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"

using namespace zl;

namespace {

const char* kValidMaterials = R"([{ "item": "rag", "count": 1 }])";

}  // namespace

TEST_CASE("M33-F4-T1 内置车辆改装数据契约") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    items::ItemRegistry items;
    REQUIRE(items.LoadFile(root / "assets" / "items.json"));
    core::Config config;
    REQUIRE(config.LoadFile(root / "config" / "game.json"));
    config.Set("vehicleMods.file",
               (root / "assets" / "vehicle_mods.json").generic_string());

    game::VehicleModsConfig mods;
    std::string error;
    REQUIRE_MESSAGE(game::LoadVehicleModConfig(config, &mods, &error, &items),
                    error);
    CHECK(mods.enabled);
    CHECK(mods.weightAccelerationKg == doctest::Approx(400.0f));
    CHECK(mods.weightFuelKg == doctest::Approx(600.0f));
    REQUIRE(mods.defs.size() == 5u);

    std::vector<bool> slots(5, false);
    for (const game::VehicleModDef& def : mods.defs) {
        CHECK(game::IsValidVehicleModToken(def.id));
        CHECK_FALSE(def.name.empty());
        CHECK_FALSE(def.materials.empty());
        CHECK(def.installSeconds > 0.0f);
        CHECK(def.weightKg >= 0.0f);
        slots[static_cast<std::size_t>(def.slot)] = true;
    }
    CHECK(std::all_of(slots.begin(), slots.end(), [](bool value) {
        return value;
    }));

    const game::VehicleModDef* armor =
        mods.Find("vehicle_armor_plate");
    REQUIRE(armor != nullptr);
    CHECK(armor->slot == game::VehicleModSlot::Armor);
    CHECK(armor->effects.crashConditionScale == doctest::Approx(0.68f));
    CHECK(armor->effects.zombieConditionScale == doctest::Approx(0.72f));
    CHECK(armor->weightKg == doctest::Approx(28.0f));

    const game::VehicleModDef* rack =
        mods.Find("vehicle_storage_rack");
    REQUIRE(rack != nullptr);
    CHECK(rack->slot == game::VehicleModSlot::Storage);
    CHECK(rack->effects.trunkCapacityKg == doctest::Approx(24.0f));
    CHECK(rack->effects.trunkStacks == 2);

    const game::VehicleModDef* plow = mods.Find("vehicle_ram_plow");
    REQUIRE(plow != nullptr);
    CHECK(plow->slot == game::VehicleModSlot::Plow);
    CHECK(plow->effects.ramDamageScale == doctest::Approx(1.45f));
    CHECK(plow->effects.ramConditionScale == doctest::Approx(0.65f));

    const game::VehicleModDef* light =
        mods.Find("vehicle_searchlight");
    REQUIRE(light != nullptr);
    CHECK(light->slot == game::VehicleModSlot::Lights);
    CHECK(light->effects.headlightRangeScale == doctest::Approx(1.45f));

    const game::VehicleModDef* engine =
        mods.Find("vehicle_engine_tune");
    REQUIRE(engine != nullptr);
    CHECK(engine->slot == game::VehicleModSlot::Engine);
    CHECK(engine->effects.maxSpeedScale == doctest::Approx(1.18f));
    CHECK(engine->effects.fuelUseScale == doctest::Approx(1.15f));
}

TEST_CASE("M33-F4-T1 非法改装跳过并保留其他定义") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    items::ItemRegistry items;
    REQUIRE(items.LoadFile(root / "assets" / "items.json"));

    const std::string json = std::string(R"({
      "format": "vehicle_mods-v1",
      "defs": [
        {
          "id": "default_mod",
          "name": "默认改装",
          "slot": "armor",
          "materials": )") + kValidMaterials + R"(
        },
        {
          "id": "bad_slot",
          "name": "坏槽位",
          "slot": "turret",
          "materials": )" + kValidMaterials + R"(
        },
        {
          "id": "bad_effect",
          "name": "坏效果",
          "slot": "engine",
          "effects": { "maxSpeedScale": 9.0 },
          "materials": )" + kValidMaterials + R"(
        },
        {
          "id": "bad_item",
          "name": "坏材料",
          "slot": "storage",
          "materials": [{ "item": "missing_item", "count": 1 }]
        },
        {
          "id": "self_exclude",
          "name": "自互斥",
          "slot": "plow",
          "materials": )" + kValidMaterials + R"(,
          "excludes": ["self_exclude"]
        },
        {
          "id": "ghost_exclude",
          "name": "坏互斥",
          "slot": "lights",
          "materials": )" + kValidMaterials + R"(,
          "excludes": ["missing_mod"]
        },
        {
          "id": "duplicate_material",
          "name": "重复材料",
          "slot": "armor",
          "materials": [
            { "item": "rag", "count": 1 },
            { "item": "rag", "count": 2 }
          ]
        },
        {
          "id": "default_mod",
          "name": "重复 id",
          "slot": "storage",
          "materials": )" + kValidMaterials + R"(
        }
      ]
    })";

    game::VehicleModsConfig mods;
    mods.enabled = true;
    game::VehicleModMergeStats stats;
    std::string error;
    REQUIRE_MESSAGE(
        game::MergeVehicleModJson(json, "inline", &mods, &stats, &error, &items),
        error);
    REQUIRE(mods.defs.size() == 1u);
    CHECK(stats.filesLoaded == 1);
    CHECK(stats.defsAdded == 1);
    CHECK(stats.defsSkipped == 7);

    const game::VehicleModDef* def = mods.Find("default_mod");
    REQUIRE(def != nullptr);
    CHECK(def->effects.crashConditionScale == doctest::Approx(1.0f));
    CHECK(def->effects.zombieConditionScale == doctest::Approx(1.0f));
    CHECK(def->effects.trunkStacks == 0);
    CHECK(def->installSeconds == doctest::Approx(10.0f));
    CHECK(def->weightKg == doctest::Approx(0.0f));
    CHECK(def->excludes.empty());
}

TEST_CASE("M33-F4-T1 互斥引用与 mod 追加") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    items::ItemRegistry items;
    REQUIRE(items.LoadFile(root / "assets" / "items.json"));

    game::VehicleModsConfig mods;
    mods.enabled = true;
    game::VehicleModMergeStats stats;
    std::string error;
    REQUIRE(game::MergeVehicleModJson(
        R"({
          "format": "vehicle_mods-v1",
          "defs": [{
            "id": "base_armor",
            "name": "基础装甲",
            "slot": "armor",
            "materials": [{ "item": "rag", "count": 1 }]
          }]
        })",
        "base", &mods, &stats, &error, &items));

    REQUIRE(game::MergeVehicleModJson(
        R"({
          "format": "vehicle_mods-v1",
          "defs": [
            {
              "id": "mod_lights",
              "name": "模组探照灯",
              "slot": "lights",
              "materials": [{ "item": "rag", "count": 1 }],
              "excludes": ["base_armor"]
            },
            {
              "id": "orphan",
              "name": "孤儿互斥",
              "slot": "engine",
              "materials": [{ "item": "rag", "count": 1 }],
              "excludes": ["missing_mod"]
            },
            {
              "id": "base_armor",
              "name": "重复装甲",
              "slot": "armor",
              "materials": [{ "item": "rag", "count": 1 }]
            }
          ]
        })",
        "mod", &mods, &stats, &error, &items));

    CHECK(stats.filesLoaded == 2);
    CHECK(stats.defsAdded == 2);
    CHECK(stats.defsSkipped == 2);
    REQUIRE(mods.Find("mod_lights") != nullptr);
    CHECK(mods.Find("mod_lights")->excludes.size() == 1u);
    CHECK(mods.Find("orphan") == nullptr);
    CHECK(mods.defs.size() == 2u);
}

TEST_CASE("M33-F4-T1 无 vehicleMods 配置保持无改装基线") {
    core::Config config;
    game::VehicleModsConfig mods;
    mods.enabled = true;
    mods.defs.push_back(game::VehicleModDef{});
    std::string error;
    REQUIRE(game::LoadVehicleModConfig(config, &mods, &error));
    CHECK_FALSE(mods.enabled);
    CHECK(mods.defs.empty());
}

TEST_CASE("M33-F4-T2 车辆改装安装、拆除、重复与材料") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    items::ItemRegistry items;
    REQUIRE(items.LoadFile(root / "assets" / "items.json"));
    core::Config config;
    REQUIRE(config.LoadFile(root / "config" / "game.json"));
    config.Set("vehicleMods.file",
               (root / "assets" / "vehicle_mods.json").generic_string());

    game::VehicleModsConfig catalog;
    std::string error;
    REQUIRE_MESSAGE(
        game::LoadVehicleModConfig(config, &catalog, &error, &items), error);
    CHECK(catalog.removeRefundRatio == doctest::Approx(0.5f));
    CHECK(catalog.removeSecondsScale == doctest::Approx(0.5f));
    const game::VehicleModDef* armor = catalog.Find("vehicle_armor_plate");
    REQUIRE(armor != nullptr);

    ecs::VehicleMods mods;
    CHECK(game::InstallVehicleMod(catalog, &mods, armor->id, &error));
    CHECK(game::InstalledVehicleModId(mods, armor->slot) == armor->id);
    CHECK(game::InstalledVehicleMod(catalog, mods, armor->slot) == armor);
    CHECK(game::VehicleModsHasAny(mods));

    error.clear();
    CHECK_FALSE(game::CanInstallVehicleMod(catalog, mods, armor->id, &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(game::InstallVehicleMod(catalog, &mods, armor->id, &error));
    CHECK(game::InstalledVehicleModId(mods, armor->slot) == armor->id);

    ecs::Container bag;
    bag.capacityKg = 80.0f;
    CHECK(items::AddItem(bag, items,
                         ecs::ItemInstance{"repair_kit", 2, 1.0f}) == 2);
    CHECK(items::AddItem(bag, items,
                         ecs::ItemInstance{"wood", 4, 1.0f}) == 4);
    std::string summary;
    CHECK(game::VehicleModMaterialCountsMet(bag, items, *armor, &summary));
    CHECK(summary.find("2/2") != std::string::npos);
    CHECK(game::ConsumeVehicleModMaterials(bag, items, *armor, &error));
    CHECK(items::CountOf(bag, "repair_kit") == 0);
    CHECK(items::CountOf(bag, "wood") == 0);
    error.clear();
    CHECK_FALSE(game::ConsumeVehicleModMaterials(bag, items, *armor, &error));
    CHECK_FALSE(error.empty());

    const std::vector<game::VehicleModMaterial> refund =
        game::VehicleModRefundMaterials(*armor, 0.5f);
    REQUIRE(refund.size() == 2u);
    CHECK(refund[0].item == "repair_kit");
    CHECK(refund[0].count == 1);
    CHECK(refund[1].item == "wood");
    CHECK(refund[1].count == 2);

    ecs::Container refundBag;
    refundBag.capacityKg = 80.0f;
    CHECK(game::AddVehicleModRefund(refundBag, items, *armor, 0.5f).empty());
    CHECK(items::CountOf(refundBag, "repair_kit") == 1);
    CHECK(items::CountOf(refundBag, "wood") == 2);

    ecs::Container fullBag;
    fullBag.capacityKg = 0.0f;
    const std::vector<game::VehicleModMaterial> overflow =
        game::AddVehicleModRefund(fullBag, items, *armor, 0.5f);
    REQUIRE(overflow.size() == 2u);
    CHECK(overflow[0].item == "repair_kit");
    CHECK(overflow[0].count == 1);
    CHECK(overflow[1].item == "wood");
    CHECK(overflow[1].count == 2);

    CHECK(game::VehicleModRemoveSeconds(catalog, *armor) ==
          doctest::Approx(15.0f));
    CHECK(game::RemoveVehicleMod(&mods, armor->slot) == armor->id);
    CHECK(game::InstalledVehicleModId(mods, armor->slot).empty());
    CHECK_FALSE(game::VehicleModsHasAny(mods));
}

TEST_CASE("M33-F4-T2 车辆改装互斥安装与候选") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    items::ItemRegistry items;
    REQUIRE(items.LoadFile(root / "assets" / "items.json"));

    game::VehicleModsConfig catalog;
    catalog.enabled = true;
    game::VehicleModMergeStats stats;
    std::string error;
    REQUIRE(game::MergeVehicleModJson(
        R"({
          "format": "vehicle_mods-v1",
          "defs": [
            {
              "id": "test_armor",
              "name": "测试装甲",
              "slot": "armor",
              "materials": [{ "item": "rag", "count": 1 }]
            },
            {
              "id": "test_lights",
              "name": "测试探照灯",
              "slot": "lights",
              "materials": [{ "item": "rag", "count": 1 }],
              "excludes": ["test_armor"]
            }
          ]
        })",
        "inline", &catalog, &stats, &error, &items));

    ecs::VehicleMods mods;
    CHECK(game::InstallVehicleMod(catalog, &mods, "test_armor", &error));
    error.clear();
    CHECK_FALSE(
        game::CanInstallVehicleMod(catalog, mods, "test_lights", &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(
        game::InstallVehicleMod(catalog, &mods, "test_lights", &error));
    CHECK(game::InstalledVehicleModId(mods, game::VehicleModSlot::Armor) ==
          "test_armor");
    CHECK(game::InstalledVehicleModId(mods, game::VehicleModSlot::Lights)
              .empty());
    CHECK(game::RemoveVehicleMod(&mods, game::VehicleModSlot::Armor) ==
          "test_armor");
    CHECK(game::InstallVehicleMod(catalog, &mods, "test_lights", &error));
    CHECK(game::InstalledVehicleModId(mods, game::VehicleModSlot::Lights) ==
          "test_lights");

    const auto candidates = game::VehicleModCandidatesForSlot(
        catalog, game::VehicleModSlot::Lights);
    REQUIRE(candidates.size() == 1u);
    CHECK(candidates[0]->id == "test_lights");
}

TEST_CASE("M33-F4-T4 改装效果摘要与目标校验") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    items::ItemRegistry items;
    REQUIRE(items.LoadFile(root / "assets" / "items.json"));
    core::Config config;
    REQUIRE(config.LoadFile(root / "config" / "game.json"));
    config.Set("vehicleMods.file",
               (root / "assets" / "vehicle_mods.json").generic_string());

    game::VehicleModsConfig catalog;
    std::string error;
    REQUIRE_MESSAGE(
        game::LoadVehicleModConfig(config, &catalog, &error, &items), error);

    const game::VehicleModDef* armor = catalog.Find("vehicle_armor_plate");
    const game::VehicleModDef* rack = catalog.Find("vehicle_storage_rack");
    const game::VehicleModDef* plow = catalog.Find("vehicle_ram_plow");
    const game::VehicleModDef* light = catalog.Find("vehicle_searchlight");
    const game::VehicleModDef* engine = catalog.Find("vehicle_engine_tune");
    REQUIRE(armor != nullptr);
    REQUIRE(rack != nullptr);
    REQUIRE(plow != nullptr);
    REQUIRE(light != nullptr);
    REQUIRE(engine != nullptr);

    const std::string armorText = game::VehicleModEffectSummary(*armor);
    CHECK(armorText.find("撞墙车况 -32%") != std::string::npos);
    CHECK(armorText.find("撞尸车况 -28%") != std::string::npos);
    CHECK(game::VehicleModEffectSummary(*rack).find("后备箱 +24kg") !=
          std::string::npos);
    CHECK(game::VehicleModEffectSummary(*plow).find("撞击伤害 +45%") !=
          std::string::npos);
    CHECK(game::VehicleModEffectSummary(*light).find("车灯范围 +45%") !=
          std::string::npos);
    CHECK(game::VehicleModEffectSummary(*light).find("车灯亮度 +25%") !=
          std::string::npos);
    CHECK(game::VehicleModEffectSummary(*engine).find("极速 +18%") !=
          std::string::npos);
    CHECK(game::VehicleModEffectSummary(*engine).find("油耗 +15%") !=
          std::string::npos);

    ecs::VehicleMods all;
    for (const char* id : {"vehicle_armor_plate", "vehicle_storage_rack",
                           "vehicle_ram_plow", "vehicle_searchlight",
                           "vehicle_engine_tune"}) {
        REQUIRE_MESSAGE(game::InstallVehicleMod(catalog, &all, id, &error),
                        error);
    }
    const game::VehicleModStats stats = game::VehicleModStatsFor(catalog, all);
    const std::string summary = game::VehicleModStatsSummary(stats, 40.0f);
    CHECK(summary.find("总重 86kg") != std::string::npos);
    CHECK(summary.find("后备箱 64kg") != std::string::npos);
    CHECK(summary.find("极速 +18%") != std::string::npos);
    CHECK(summary.find("撞击 +45%") != std::string::npos);
    CHECK(game::VehicleModStatsSummary(game::VehicleModStats{}, 40.0f).find(
              "无改装效果") != std::string::npos);

    CHECK(game::VehicleModTargetInRange(10.0f, 10.0f, 0.0f, 11.9f, 10.0f,
                                        0.0f, 2.0f, true, true));
    CHECK_FALSE(game::VehicleModTargetInRange(10.0f, 10.0f, 0.0f, 12.1f,
                                              10.0f, 0.0f, 2.0f, true, true));
    CHECK_FALSE(game::VehicleModTargetInRange(10.0f, 10.0f, 0.0f, 10.1f,
                                              10.0f, 1.0f, 2.0f, true, true));
    CHECK_FALSE(game::VehicleModTargetInRange(10.0f, 10.0f, 0.0f, 10.0f,
                                              10.0f, 0.0f, 2.0f, false, true));
    CHECK_FALSE(game::VehicleModTargetInRange(10.0f, 10.0f, 0.0f, 10.0f,
                                              10.0f, 0.0f, 2.0f, true, false));
}
