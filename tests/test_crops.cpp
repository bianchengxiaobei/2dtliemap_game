#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <string>

#include "core/Config.h"
#include "game/Crops.h"
#include "game/Homestead.h"
#include "items/ItemRegistry.h"

using namespace zl;

namespace {

const std::string kItems = R"({
  "format": "items-v1",
  "items": [
    { "id": "wood", "name": "木材", "icon": "item.wood", "weight": 0.4, "maxStack": 20, "type": "generic" },
    { "id": "test_seed", "name": "测试种子", "icon": "item.rag", "weight": 0.05, "maxStack": 10, "type": "generic" },
    { "id": "test_produce", "name": "测试作物", "icon": "item.canned_beans", "weight": 0.5, "maxStack": 3, "type": "consumable" }
  ]
})";

const std::string kCrops = R"({
  "format": "crops-v1",
  "crops": [
    {
      "id": "test_crop",
      "name": "测试作物",
      "seedItem": "test_seed",
      "produceItem": "test_produce",
      "growMinutes": 120.0,
      "waterMinutes": 60.0,
      "maxWaterMinutes": 120.0,
      "witherMinutes": 300.0,
      "waterUsePerMinute": 2.0,
      "diseaseChance": 0.1,
      "harvestYield": 3,
      "seedYield": 1,
      "stages": [
        { "name": "幼苗", "start": 0.0 },
        { "name": "生长", "start": 0.5 },
        { "name": "成熟", "start": 1.0 }
      ]
    }
  ]
})";

bool LoadFixture(items::ItemRegistry* items) {
    std::string error;
    return items->LoadFromString(kItems, "crop-items", &error);
}

}  // namespace

TEST_CASE("F5-T1 crop registry parses stages, water, disease and yields") {
    items::ItemRegistry items;
    REQUIRE(LoadFixture(&items));
    game::CropRegistry crops;
    std::string error;
    REQUIRE_MESSAGE(crops.LoadFromString(kCrops, "crops.json", items, &error),
                    error);
    REQUIRE(crops.Size() == 1);
    const game::CropDef* crop = crops.Find("test_crop");
    REQUIRE(crop != nullptr);
    CHECK(crop->name == "测试作物");
    CHECK(crop->seedItem == "test_seed");
    CHECK(crop->produceItem == "test_produce");
    CHECK(crop->growMinutes == doctest::Approx(120.0f));
    CHECK(crop->waterUsePerMinute == doctest::Approx(2.0f));
    CHECK(crop->diseaseChance == doctest::Approx(0.1f));
    CHECK(crop->harvestYield == 3);
    REQUIRE(crop->stages.size() == 3);
    CHECK(crop->stages[0].start == doctest::Approx(0.0f));
    CHECK(crop->stages[2].start == doctest::Approx(1.0f));
    CHECK(crops.FindBySeed("test_seed") == crop);
    CHECK(crops.FindBySeed("wood") == nullptr);
    CHECK(std::string(game::CropGrowthStageName(*crop, 0.0f)) == "幼苗");
    CHECK(std::string(game::CropGrowthStageName(*crop, 0.6f)) == "生长");
    CHECK(std::string(game::CropGrowthStageName(*crop, 1.0f)) == "成熟");
}

TEST_CASE("F5-T1 crop registry rejects bad refs, duplicate seeds and stages") {
    items::ItemRegistry items;
    REQUIRE(LoadFixture(&items));
    std::string error;
    game::CropRegistry crops;
    std::string bad = kCrops;
    bad.replace(bad.find("\"test_produce\""), 14, "\"missing_item\"");
    CHECK_FALSE(crops.LoadFromString(bad, "bad-crop", items, &error));
    CHECK(crops.Size() == 0);
    CHECK(error.find("未知物品") != std::string::npos);

    bad = kCrops;
    const auto growPos = bad.find("120.0");
    REQUIRE(growPos != std::string::npos);
    bad.replace(growPos, 5, "0.0");
    CHECK_FALSE(crops.LoadFromString(bad, "bad-crop", items, &error));
    CHECK(crops.Size() == 0);

    bad = kCrops;
    bad.replace(bad.find("\"start\": 0.5"), 12, "\"start\": 1.0");
    CHECK_FALSE(crops.LoadFromString(bad, "bad-crop", items, &error));
    CHECK(crops.Size() == 0);
}

TEST_CASE("F5-T1 crop registry merges mod crops atomically") {
    items::ItemRegistry items;
    REQUIRE(LoadFixture(&items));
    game::CropRegistry crops;
    std::string error;
    REQUIRE(crops.LoadFromString(kCrops, "base-crops", items, &error));
    const std::string mod = R"({
      "format": "crops-v1",
      "crops": [
        {
          "id": "mod_crop",
          "name": "模组作物",
          "seedItem": "wood",
          "produceItem": "test_produce",
          "growMinutes": 60.0,
          "waterMinutes": 30.0,
          "maxWaterMinutes": 60.0,
          "witherMinutes": 120.0,
          "waterUsePerMinute": 1.0,
          "diseaseChance": 0.0,
          "harvestYield": 1,
          "seedYield": 0,
          "stages": [
            { "name": "幼苗", "start": 0.0 },
            { "name": "成熟", "start": 1.0 }
          ]
        }
      ]
    })";
    REQUIRE(crops.MergeFromString(mod, "mod-crops", items, &error));
    CHECK(crops.Size() == 2);
    CHECK(crops.Find("mod_crop") != nullptr);
    CHECK(crops.FindBySeed("wood") != nullptr);

    const std::string duplicate = R"({
      "format": "crops-v1",
      "crops": [
        {
          "id": "mod_crop",
          "name": "重复作物",
          "seedItem": "test_seed",
          "produceItem": "test_produce",
          "growMinutes": 60.0,
          "waterMinutes": 30.0,
          "maxWaterMinutes": 60.0,
          "witherMinutes": 120.0,
          "waterUsePerMinute": 1.0,
          "diseaseChance": 0.0,
          "harvestYield": 1,
          "seedYield": 0,
          "stages": [
            { "name": "幼苗", "start": 0.0 },
            { "name": "成熟", "start": 1.0 }
          ]
        }
      ]
    })";
    CHECK_FALSE(crops.MergeFromString(duplicate, "duplicate-crops", items,
                                      &error));
    CHECK(crops.Size() == 2);
}

TEST_CASE("F5-T1 crop values drive farm growth and status") {
    items::ItemRegistry items;
    REQUIRE(LoadFixture(&items));
    game::CropRegistry crops;
    std::string error;
    REQUIRE(crops.LoadFromString(kCrops, "crops.json", items, &error));
    const game::CropDef* crop = crops.Find("test_crop");
    REQUIRE(crop != nullptr);

    ecs::FarmPlot plot;
    REQUIRE(game::PlantFarmPlot(plot, *crop));
    CHECK(plot.cropId == "test_crop");
    CHECK(plot.growMinutes == doctest::Approx(120.0f));
    CHECK(plot.harvestYield == 3);
    REQUIRE(game::WaterFarmPlot(plot, crop->waterMinutes,
                                crop->maxWaterMinutes));
    CHECK(plot.waterMinutes == doctest::Approx(60.0f));

    game::FarmConfig config;
    game::AdvanceFarmPlot(plot, 10.0f, config, crop);
    CHECK(plot.growthMinutes == doctest::Approx(10.0f));
    CHECK(plot.waterMinutes == doctest::Approx(40.0f));
    CHECK(game::FarmStatusText(plot, crop).find("测试作物") !=
          std::string::npos);
    CHECK(game::FarmStatusText(plot, crop).find("幼苗") != std::string::npos);

    REQUIRE(game::WaterFarmPlot(plot, 120.0f, 120.0f));
    game::AdvanceFarmPlot(plot, 60.0f, config, crop);
    CHECK(plot.growthMinutes == doctest::Approx(70.0f));
    CHECK(plot.waterMinutes == doctest::Approx(0.0f));
    REQUIRE(game::WaterFarmPlot(plot, 120.0f, 120.0f));
    game::AdvanceFarmPlot(plot, 60.0f, config, crop);
    CHECK(plot.stage == ecs::CropStage::Mature);
    int harvested = 0;
    REQUIRE(game::HarvestFarmPlot(plot, &harvested));
    CHECK(harvested == 3);
}

TEST_CASE("F5-T1 base crops-v1 has five distinct crops and valid refs") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    items::ItemRegistry items;
    std::string error;
    REQUIRE_MESSAGE(items.LoadFile(root / "assets" / "items.json", &error),
                    error);
    game::CropRegistry crops;
    REQUIRE_MESSAGE(crops.LoadFile(root / "assets" / "crops.json", items,
                                   &error),
                    error);
    CHECK(crops.Size() == 5);
    for (const game::CropDef& crop : crops.All()) {
        CHECK(items.Find(crop.seedItem) != nullptr);
        CHECK(items.Find(crop.produceItem) != nullptr);
        CHECK(crop.stages.size() >= 2);
        CHECK(crop.stages.front().start == doctest::Approx(0.0f));
        CHECK(crop.stages.back().start == doctest::Approx(1.0f));
    }
    CHECK(crops.Find("cabbage") != nullptr);
    CHECK(crops.Find("potato") != nullptr);
    CHECK(crops.Find("tomato") != nullptr);
    CHECK(crops.Find("corn") != nullptr);
    CHECK(crops.Find("carrot") != nullptr);
    CHECK(crops.FindBySeed("potato_seeds") == crops.Find("potato"));
    CHECK(crops.FindBySeed("carrot_seeds") == crops.Find("carrot"));
}

TEST_CASE("M23-F5-T5 real crop cycles mature under season and irrigation") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    items::ItemRegistry items;
    game::CropRegistry crops;
    std::string error;
    REQUIRE_MESSAGE(items.LoadFile(root / "assets" / "items.json", &error),
                    error);
    REQUIRE_MESSAGE(crops.LoadFile(root / "assets" / "crops.json", items,
                                   &error),
                    error);
    core::Config config;
    REQUIRE(config.LoadFile(root / "config" / "game.json"));
    const game::HomesteadConfig homestead = game::LoadHomesteadConfig(config);

    struct CycleResult {
        ecs::FarmPlot plot;
        float elapsed = 0.0f;
        int waterings = 0;
        bool planted = false;
        bool watered = true;
    };
    const auto runCycle = [&](const game::CropDef& crop,
                              game::FarmSeason season) {
        CycleResult result;
        result.planted = game::PlantFarmPlot(result.plot, crop);
        if (!result.planted) {
            return result;
        }
        while (result.plot.stage == ecs::CropStage::Growing &&
               result.elapsed < crop.growMinutes * 5.0f + 120.0f) {
            if (result.plot.waterMinutes <= 0.5f) {
                if (!game::WaterFarmPlot(result.plot, crop.waterMinutes,
                                         crop.maxWaterMinutes)) {
                    result.watered = false;
                    break;
                }
                ++result.waterings;
            }
            game::AdvanceFarmPlot(result.plot, 10.0f, homestead.farm, &crop,
                                  season, false);
            result.elapsed += 10.0f;
        }
        return result;
    };

    for (const game::CropDef& crop : crops.All()) {
        INFO("crop " << crop.id);
        CycleResult summer = runCycle(crop, game::FarmSeason::Summer);
        REQUIRE(summer.planted);
        REQUIRE(summer.watered);
        REQUIRE(summer.plot.stage == ecs::CropStage::Mature);
        CHECK_FALSE(summer.plot.diseased);
        CHECK(std::fabs(summer.elapsed - crop.growMinutes) <= 10.01f);
        CHECK(summer.waterings >= 1);
        int harvested = 0;
        REQUIRE(game::HarvestFarmPlot(summer.plot, &harvested));
        CHECK(harvested == crop.harvestYield);
        CHECK(summer.plot.stage == ecs::CropStage::Empty);

        const CycleResult winter = runCycle(crop, game::FarmSeason::Winter);
        REQUIRE(winter.planted);
        REQUIRE(winter.watered);
        REQUIRE(winter.plot.stage == ecs::CropStage::Mature);
        CHECK_FALSE(winter.plot.diseased);
        CHECK(winter.elapsed > summer.elapsed * 3.5f);
        CHECK(winter.elapsed < summer.elapsed * 4.5f);
        CHECK(winter.waterings > summer.waterings);
    }
}

TEST_CASE("M23-F5-T5 disease threshold, yield loss and cure recovery") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    items::ItemRegistry items;
    game::CropRegistry crops;
    std::string error;
    REQUIRE_MESSAGE(items.LoadFile(root / "assets" / "items.json", &error),
                    error);
    REQUIRE_MESSAGE(crops.LoadFile(root / "assets" / "crops.json", items,
                                   &error),
                    error);
    core::Config config;
    REQUIRE(config.LoadFile(root / "config" / "game.json"));
    const game::HomesteadConfig homestead = game::LoadHomesteadConfig(config);
    const game::CropDef* tomato = crops.Find("tomato");
    REQUIRE(tomato != nullptr);

    ecs::FarmPlot plot;
    REQUIRE(game::PlantFarmPlot(plot, *tomato));
    int steps = 0;
    while (!plot.diseased && plot.stage == ecs::CropStage::Growing &&
           steps < 500) {
        game::AdvanceFarmPlot(plot, 10.0f, homestead.farm, tomato,
                              game::FarmSeason::Summer, false);
        ++steps;
    }
    REQUIRE(plot.diseased);
    REQUIRE(plot.stage == ecs::CropStage::Growing);
    CHECK(plot.dryMinutes < tomato->witherMinutes);
    CHECK(plot.harvestYield == 2);
    CHECK_FALSE(game::FertilizeFarmPlot(
        plot, homestead.farm.fertilizerYieldBonus));
    const float dryAtDisease = plot.dryMinutes;
    game::AdvanceFarmPlot(plot, 600.0f, homestead.farm, tomato,
                          game::FarmSeason::Summer, false);
    CHECK(plot.dryMinutes == doctest::Approx(dryAtDisease));

    REQUIRE(game::CureFarmDisease(plot));
    CHECK_FALSE(plot.diseased);
    CHECK(plot.dryMinutes == doctest::Approx(0.0f));
    REQUIRE(game::WaterFarmPlot(plot, tomato->waterMinutes,
                                tomato->maxWaterMinutes));
    REQUIRE(game::FertilizeFarmPlot(
        plot, homestead.farm.fertilizerYieldBonus));
    CHECK(plot.harvestYield == 3);
    steps = 0;
    while (plot.stage == ecs::CropStage::Growing && steps < 500) {
        if (plot.waterMinutes <= 0.5f) {
            REQUIRE(game::WaterFarmPlot(plot, tomato->waterMinutes,
                                        tomato->maxWaterMinutes));
        }
        game::AdvanceFarmPlot(plot, 10.0f, homestead.farm, tomato,
                              game::FarmSeason::Summer, false);
        ++steps;
    }
    REQUIRE(plot.stage == ecs::CropStage::Mature);
    int harvested = 0;
    REQUIRE(game::HarvestFarmPlot(plot, &harvested));
    CHECK(harvested == 3);
}
