#include "game/Homestead.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "characters/components/CharacterComponents.h"
#include "core/Config.h"
#include "ecs/components/CommonComponents.h"
#include "game/Crops.h"
#include "game/PlayerStorage.h"

namespace zl::game {

const std::array<TradeOffer, kMerchantOfferCount>& MerchantOffers() {
    static const std::array<TradeOffer, kMerchantOfferCount> kOffers{{
        TradeOffer{"bandage", "绷带 ×2", "wood", 2, "bandage", 2, {}, 4},
        TradeOffer{"antibiotics", "抗生素 ×1", "wood", 3, "antibiotics", 1,
                   {}, 2},
        TradeOffer{"campfire_recipe", "图纸：烤豆子", "wood", 3, {}, 0,
                   "campfire_beans", 1},
        TradeOffer{"ammo_9mm", "9mm 弹药 ×12", "wood", 2, "ammo_9mm", 12,
                   {}, 2},
        TradeOffer{"pistol", "手枪 ×1", "wood", 8, "pistol", 1, {}, 1},
    }};
    return kOffers;
}

void ClampMerchantStock(ecs::MerchantStock& stock) {
    const auto& offers = MerchantOffers();
    for (std::size_t i = 0; i < stock.stock.size() && i < offers.size(); ++i) {
        stock.stock[i] = std::clamp(stock.stock[i], 0, offers[i].maxStock);
    }
}

FarmSeason FarmSeasonFromMonth(int month) {
    if (month >= 3 && month <= 5) {
        return FarmSeason::Spring;
    }
    if (month >= 6 && month <= 8) {
        return FarmSeason::Summer;
    }
    if (month >= 9 && month <= 11) {
        return FarmSeason::Autumn;
    }
    return FarmSeason::Winter;
}

const char* FarmSeasonName(FarmSeason season) {
    switch (season) {
        case FarmSeason::Spring:
            return "春季";
        case FarmSeason::Summer:
            return "夏季";
        case FarmSeason::Autumn:
            return "秋季";
        case FarmSeason::Winter:
            return "冬季";
    }
    return "夏季";
}

float FarmSeasonGrowthScale(const FarmConfig& config, FarmSeason season) {
    const std::size_t index = static_cast<std::size_t>(season);
    const float scale =
        index < config.seasonGrowthScale.size()
            ? config.seasonGrowthScale[index]
            : 1.0f;
    return std::clamp(scale, 0.05f, 4.0f);
}

float FarmSeasonDiseaseScale(const FarmConfig& config, FarmSeason season) {
    const std::size_t index = static_cast<std::size_t>(season);
    const float scale =
        index < config.seasonDiseaseScale.size()
            ? config.seasonDiseaseScale[index]
            : 1.0f;
    return std::clamp(scale, 0.05f, 4.0f);
}

const char* CropStageName(ecs::CropStage stage) {
    switch (stage) {
        case ecs::CropStage::Empty:
            return "空地";
        case ecs::CropStage::Growing:
            return "生长中";
        case ecs::CropStage::Mature:
            return "成熟";
        case ecs::CropStage::Withered:
            return "枯萎";
    }
    return "未知";
}

std::string FarmStatusText(const ecs::FarmPlot& plot,
                           const CropDef* crop,
                           const char* seasonName) {
    const std::string season =
        seasonName != nullptr && *seasonName != '\0'
            ? std::string(" · ") + seasonName
            : std::string();
    switch (plot.stage) {
        case ecs::CropStage::Empty:
            return "农田：空地（E 种植 · Q 选种）";
        case ecs::CropStage::Growing: {
            const float growth =
                plot.growMinutes > 0.0f
                    ? std::clamp(plot.growthMinutes / plot.growMinutes, 0.0f,
                                 1.0f)
                    : 1.0f;
            const int water =
                static_cast<int>(std::lround(plot.waterMinutes));
            const bool thirsty = plot.waterMinutes <= 0.5f;
            std::string flags;
            if (plot.fertilized) {
                flags += " · 已施肥";
            }
            if (plot.diseased) {
                flags += " · 病害（T 杀菌）";
            }
            std::string text =
                crop != nullptr
                    ? "农田：" + crop->name + " · " +
                          CropGrowthStageName(*crop, growth) + " " +
                          std::to_string(static_cast<int>(
                              std::lround(growth * 100.0f))) +
                          "%"
                    : "农田：生长 " +
                          std::to_string(static_cast<int>(
                              std::lround(growth * 100.0f))) +
                          "%";
            text += thirsty ? " · 缺水"
                            : " · 水 " + std::to_string(water) + " 分钟";
            text += flags;
            text += season;
            text += plot.diseased
                        ? "（E 清理）"
                        : (plot.fertilized ? "（E 浇水）"
                                            : "（E 浇水 · T 施肥）");
            return text;
        }
        case ecs::CropStage::Mature:
            if (crop != nullptr) {
                return "农田：" + crop->name + "成熟（E 收获）" + season;
            }
            return "农田：成熟（E 收获）" + season;
        case ecs::CropStage::Withered:
            if (crop != nullptr) {
                return "农田：" + crop->name + "枯萎（E 清理）" + season;
            }
            return "农田：枯萎（E 清理）" + season;
    }
    return "农田";
}

bool PlantFarmPlot(ecs::FarmPlot& plot, std::string_view cropId,
                   float growMinutes, int harvestYield) {
    if (plot.stage != ecs::CropStage::Empty || cropId.empty() ||
        growMinutes <= 0.0f || harvestYield <= 0) {
        return false;
    }
    plot.cropId = std::string(cropId);
    plot.growthMinutes = 0.0f;
    plot.waterMinutes = 0.0f;
    plot.dryMinutes = 0.0f;
    plot.growMinutes = growMinutes;
    plot.harvestYield = harvestYield;
    plot.stage = ecs::CropStage::Growing;
    plot.diseased = false;
    plot.fertilized = false;
    return true;
}

bool PlantFarmPlot(ecs::FarmPlot& plot, const CropDef& crop) {
    return PlantFarmPlot(plot, crop.id, crop.growMinutes, crop.harvestYield);
}

bool WaterFarmPlot(ecs::FarmPlot& plot, float waterMinutes,
                   float maxWaterMinutes) {
    if (plot.stage != ecs::CropStage::Growing || !(waterMinutes > 0.0f) ||
        !(maxWaterMinutes > 0.0f)) {
        return false;
    }
    plot.waterMinutes =
        std::min(maxWaterMinutes, plot.waterMinutes + waterMinutes);
    plot.dryMinutes = 0.0f;
    return true;
}

bool FertilizeFarmPlot(ecs::FarmPlot& plot, int yieldBonus) {
    if (plot.stage != ecs::CropStage::Growing || plot.fertilized ||
        plot.diseased || yieldBonus <= 0) {
        return false;
    }
    plot.fertilized = true;
    plot.harvestYield =
        std::clamp(plot.harvestYield + yieldBonus, 1, 999);
    return true;
}

bool CureFarmDisease(ecs::FarmPlot& plot) {
    if (plot.stage != ecs::CropStage::Growing || !plot.diseased) {
        return false;
    }
    plot.diseased = false;
    plot.dryMinutes = 0.0f;
    return true;
}

bool HarvestFarmPlot(ecs::FarmPlot& plot, int* harvested) {
    if (plot.stage != ecs::CropStage::Mature) {
        return false;
    }
    if (harvested != nullptr) {
        *harvested = std::max(0, plot.harvestYield);
    }
    const float growMinutes = plot.growMinutes;
    const int harvestYield = plot.harvestYield;
    plot = ecs::FarmPlot{};
    plot.growMinutes = growMinutes;
    plot.harvestYield = harvestYield;
    return true;
}

void AdvanceFarmPlot(ecs::FarmPlot& plot, float gameMinutes,
                     const FarmConfig& config, const CropDef* crop,
                     FarmSeason season, bool raining) {
    if (plot.stage != ecs::CropStage::Growing || !(gameMinutes > 0.0f) ||
        plot.diseased) {
        return;
    }
    const float waterUse =
        crop != nullptr ? std::clamp(crop->waterUsePerMinute, 0.1f, 5.0f)
                        : 1.0f;
    const float maxWater =
        crop != nullptr ? std::max(1.0f, crop->maxWaterMinutes)
                        : std::max(1.0f, config.maxWaterMinutes);
    if (raining && config.rainWaterPerMinute > 0.0f) {
        plot.waterMinutes = std::min(
            maxWater,
            plot.waterMinutes +
                config.rainWaterPerMinute * gameMinutes);
        plot.dryMinutes = 0.0f;
    }
    if (plot.waterMinutes > 0.0f) {
        const float grown =
            std::min(gameMinutes, plot.waterMinutes / waterUse);
        plot.waterMinutes =
            std::max(0.0f, plot.waterMinutes - grown * waterUse);
        plot.growthMinutes +=
            grown * FarmSeasonGrowthScale(config, season);
        plot.dryMinutes = 0.0f;
        if (plot.growthMinutes >= plot.growMinutes) {
            plot.growthMinutes = plot.growMinutes;
            plot.stage = ecs::CropStage::Mature;
        }
        return;
    }
    plot.dryMinutes += gameMinutes;
    const float witherMinutes =
        crop != nullptr ? crop->witherMinutes : config.witherMinutes;
    if (plot.dryMinutes >= std::max(0.0f, witherMinutes)) {
        plot.stage = ecs::CropStage::Withered;
        return;
    }
    const float diseaseChance =
        crop != nullptr ? crop->diseaseChance : config.diseaseChance;
    if (diseaseChance > 0.0f && witherMinutes > 0.0f) {
        const float threshold =
            std::max(15.0f,
                     witherMinutes *
                         std::clamp(config.diseaseDryFraction, 0.05f, 0.9f) *
                         (1.0f - 0.35f * std::clamp(diseaseChance, 0.0f, 1.0f)) /
                         std::max(0.05f, FarmSeasonDiseaseScale(config, season)));
        if (plot.dryMinutes >= threshold) {
            plot.diseased = true;
            const float penalty =
                std::clamp(config.diseaseYieldPenalty, 0.0f, 0.9f);
            plot.harvestYield = std::max(
                1, static_cast<int>(std::lround(
                       static_cast<float>(plot.harvestYield) * (1.0f - penalty))));
        }
    }
}

float AdvanceGenerator(ecs::Generator& generator, float gameMinutes,
                       float loadPerMinute) {
    if (!generator.running || !(gameMinutes > 0.0f)) {
        return 0.0f;
    }
    const float load = std::max(0.1f, loadPerMinute);
    const float demand = gameMinutes * load;
    const float consumed = std::min(generator.fuelMinutes, demand);
    generator.fuelMinutes = std::max(0.0f, generator.fuelMinutes - consumed);
    if (generator.fuelMinutes <= 0.0f) {
        generator.fuelMinutes = 0.0f;
        generator.running = false;
    }
    return consumed;
}

float GeneratorEffectiveMinutes(const ecs::Generator& generator,
                                float baseLoad) {
    const float load =
        std::max(0.1f, baseLoad + std::max(0.0f, generator.load));
    return std::max(0.0f, generator.fuelMinutes) / load;
}

std::string GeneratorStatusText(const ecs::Generator& generator,
                                float baseLoad) {
    char buffer[160];
    const float load =
        std::max(0.1f, baseLoad + std::max(0.0f, generator.load));
    std::snprintf(buffer, sizeof(buffer),
                  "发电机：%s · 负载 %.1f · 燃料 %d 分钟",
                  generator.running ? "供电中"
                                    : (generator.fuelMinutes <= 0.0f ? "停电"
                                                                     : "停止"),
                  static_cast<double>(load),
                  static_cast<int>(std::lround(
                      GeneratorEffectiveMinutes(generator, baseLoad))));
    return buffer;
}

std::string FloodlightStatusText(const ecs::Floodlight& floodlight,
                                 bool night, bool requiresNight) {
    if (!floodlight.on) {
        return "泛光灯：关闭";
    }
    if (!floodlight.powered) {
        return "泛光灯：未供电";
    }
    if (requiresNight && !night) {
        return "泛光灯：供电中 · 待夜间";
    }
    return "泛光灯：供电中 · 照明中";
}

int FindPowerSourceFor(const std::vector<PowerSource>& sources, float x,
                       float y) {
    int best = -1;
    float bestDistance = 0.0f;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const PowerSource& source = sources[i];
        if (!(source.radius > 0.0f)) {
            continue;
        }
        const float dx = source.x - x;
        const float dy = source.y - y;
        const float distance = dx * dx + dy * dy;
        if (distance > source.radius * source.radius) {
            continue;
        }
        if (best < 0 || distance < bestDistance) {
            best = static_cast<int>(i);
            bestDistance = distance;
        }
    }
    return best;
}

bool IsColdStoragePowered(const ecs::Generator& generator, float gx,
                          float gy, float storageX, float storageY) {
    if (!generator.running || generator.fuelMinutes <= 0.0f ||
        generator.radius <= 0.0f) {
        return false;
    }
    const float dx = gx - storageX;
    const float dy = gy - storageY;
    return dx * dx + dy * dy <= generator.radius * generator.radius;
}

bool TrapCanTrigger(const ecs::SpikeTrap& trap) {
    return trap.armed && trap.uses > 0 && trap.cooldown <= 0.0f;
}

void AdvanceTrap(ecs::SpikeTrap& trap, float seconds) {
    if (seconds > 0.0f) {
        trap.cooldown = std::max(0.0f, trap.cooldown - seconds);
    }
}

bool ConsumeTrapUse(ecs::SpikeTrap& trap) {
    if (!trap.armed || trap.uses <= 0) {
        return false;
    }
    --trap.uses;
    trap.cooldown = std::max(0.0f, trap.cooldownSeconds);
    if (trap.uses <= 0) {
        trap.armed = false;
    }
    return true;
}

HomesteadConfig LoadHomesteadConfig(const core::Config& config) {
    HomesteadConfig result;
    result.farm.enabled = config.Get<bool>("farming.enabled", true);
    result.farm.growMinutes = std::clamp(
        static_cast<float>(config.Get<double>("farming.growMinutes", 240.0)),
        10.0f, 100000.0f);
    result.farm.waterMinutes = std::clamp(
        static_cast<float>(config.Get<double>("farming.waterMinutes", 360.0)),
        1.0f, 100000.0f);
    result.farm.maxWaterMinutes = std::clamp(
        static_cast<float>(
            config.Get<double>("farming.maxWaterMinutes", 720.0)),
        result.farm.waterMinutes, 100000.0f);
    result.farm.witherMinutes = std::clamp(
        static_cast<float>(
            config.Get<double>("farming.witherMinutes", 1440.0)),
        1.0f, 100000.0f);
    result.farm.harvestYield = std::clamp(
        config.Get<int>("farming.harvestYield", 2), 1, 20);
    result.farm.seedYield = std::clamp(
        config.Get<int>("farming.seedYield", 1), 0, 5);
    result.farm.seedItem =
        config.Get<std::string>("farming.seedItem", "cabbage_seeds");
    result.farm.produceItem =
        config.Get<std::string>("farming.produceItem", "fresh_cabbage");
    result.farm.waterItem =
        config.Get<std::string>("farming.waterItem", "water_bottle");
    result.farm.fertilizerItem =
        config.Get<std::string>("farming.fertilizerItem", "fertilizer");
    result.farm.fungicideItem =
        config.Get<std::string>("farming.fungicideItem", "fungicide");
    result.farm.fertilizerYieldBonus =
        std::clamp(config.Get<int>("farming.fertilizerYieldBonus", 1), 0, 20);
    result.farm.diseaseChance = std::clamp(
        static_cast<float>(config.Get<double>("farming.diseaseChance", 0.05)),
        0.0f, 1.0f);
    result.farm.diseaseDryFraction = std::clamp(
        static_cast<float>(
            config.Get<double>("farming.diseaseDryFraction", 0.45)),
        0.05f, 0.9f);
    result.farm.diseaseYieldPenalty = std::clamp(
        static_cast<float>(
            config.Get<double>("farming.diseaseYieldPenalty", 0.5)),
        0.0f, 0.9f);
    result.farm.rainWaterPerMinute = std::clamp(
        static_cast<float>(
            config.Get<double>("farming.rainWaterPerMinute", 0.5)),
        0.0f, 10.0f);
    result.farm.seasonGrowthScale = {
        std::clamp(static_cast<float>(config.Get<double>(
                       "farming.season.springGrowth", 1.0)),
                   0.05f, 4.0f),
        std::clamp(static_cast<float>(config.Get<double>(
                       "farming.season.summerGrowth", 1.0)),
                   0.05f, 4.0f),
        std::clamp(static_cast<float>(config.Get<double>(
                       "farming.season.autumnGrowth", 0.9)),
                   0.05f, 4.0f),
        std::clamp(static_cast<float>(config.Get<double>(
                       "farming.season.winterGrowth", 0.25)),
                   0.05f, 4.0f)};
    result.farm.seasonDiseaseScale = {
        std::clamp(static_cast<float>(config.Get<double>(
                       "farming.season.springDisease", 1.0)),
                   0.05f, 4.0f),
        std::clamp(static_cast<float>(config.Get<double>(
                       "farming.season.summerDisease", 1.2)),
                   0.05f, 4.0f),
        std::clamp(static_cast<float>(config.Get<double>(
                       "farming.season.autumnDisease", 0.9)),
                   0.05f, 4.0f),
        std::clamp(static_cast<float>(config.Get<double>(
                       "farming.season.winterDisease", 0.6)),
                   0.05f, 4.0f)};

    result.power.enabled = config.Get<bool>("power.enabled", true);
    result.power.fuelMinutesPerCan = std::clamp(
        static_cast<float>(
            config.Get<double>("power.fuelMinutesPerCan", 720.0)),
        1.0f, 100000.0f);
    result.power.radius = std::clamp(
        static_cast<float>(config.Get<double>("power.radius", 8.0)), 0.0f,
        64.0f);
    result.power.fuelItem =
        config.Get<std::string>("power.fuelItem", "fuel_can");
    result.power.baseLoad = std::clamp(
        static_cast<float>(config.Get<double>("power.baseLoad", 1.0)), 0.1f,
        20.0f);
    result.power.coldStorageLoad = std::clamp(
        static_cast<float>(config.Get<double>("power.coldStorageLoad", 0.5)),
        0.0f, 20.0f);
    result.power.floodlightLoad = std::clamp(
        static_cast<float>(config.Get<double>("power.floodlightLoad", 0.5)),
        0.0f, 20.0f);
    result.power.electricFenceLoad = std::clamp(
        static_cast<float>(
            config.Get<double>("power.electricFenceLoad", 1.0)),
        0.0f, 20.0f);
    result.power.floodlightRadius = std::clamp(
        static_cast<float>(config.Get<double>("power.floodlightRadius", 6.0)),
        1.0f, 32.0f);
    result.power.attractRadius = std::clamp(
        static_cast<float>(config.Get<double>("power.attractRadius", 18.0)),
        1.0f, 64.0f);
    result.power.electricFenceDamage = std::clamp(
        static_cast<float>(
            config.Get<double>("power.electricFenceDamage", 25.0)),
        0.0f, 1000.0f);
    result.power.electricFenceCooldownSeconds = std::clamp(
        static_cast<float>(
            config.Get<double>("power.electricFenceCooldownSeconds", 1.2)),
        0.0f, 60.0f);
    result.power.electricFenceStaggerSeconds = std::clamp(
        static_cast<float>(
            config.Get<double>("power.electricFenceStaggerSeconds", 0.5)),
        0.0f, 5.0f);
    result.power.lowFuelWarningMinutes = std::clamp(
        static_cast<float>(
            config.Get<double>("power.lowFuelWarningMinutes", 10.0)),
        0.0f, 10000.0f);
    result.power.deviceRequiresNight =
        config.Get<bool>("power.deviceRequiresNight", true);

    result.trap.enabled = config.Get<bool>("traps.enabled", true);
    result.trap.damage = std::clamp(
        static_cast<float>(config.Get<double>("traps.damage", 45.0)), 0.0f,
        1000.0f);
    result.trap.radius = std::clamp(
        static_cast<float>(config.Get<double>("traps.radius", 1.0)), 0.1f,
        5.0f);
    result.trap.cooldownSeconds = std::clamp(
        static_cast<float>(
            config.Get<double>("traps.cooldownSeconds", 1.5)),
        0.0f, 60.0f);
    result.trap.staggerSeconds = std::clamp(
        static_cast<float>(
            config.Get<double>("traps.staggerSeconds", 0.35)),
        0.0f, 5.0f);
    result.trap.uses = std::clamp(config.Get<int>("traps.uses", 6), 1, 99);

    result.trade.enabled = config.Get<bool>("trading.enabled", true);
    result.trade.range = std::clamp(
        static_cast<float>(config.Get<double>("trading.range", 3.5)), 0.5f,
        10.0f);
    result.trade.merchantX = static_cast<float>(
        config.Get<double>("trading.merchantX", 68.5));
    result.trade.merchantY = static_cast<float>(
        config.Get<double>("trading.merchantY", 26.5));
    result.trade.restockDays =
        std::clamp(config.Get<int>("trading.restockDays", 1), 1, 30);
    return result;
}

ecs::Entity SpawnFarmPlot(ecs::EcsWorld& world, float x, float y, int z,
                          const FarmConfig& config) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return entt::null;
    }
    const ecs::Entity entity = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(entity, ecs::Position{x, y, z});
    registry.emplace<ecs::SpriteRef>(
        entity, ecs::SpriteRef{ecs::HashName(kFarmPlotSprite), 0,
                               0xFFFFFFFFu});
    ecs::FarmPlot plot;
    plot.growMinutes = config.growMinutes;
    plot.harvestYield = config.harvestYield;
    registry.emplace<ecs::FarmPlot>(entity, std::move(plot));
    return entity;
}

ecs::Entity SpawnGenerator(ecs::EcsWorld& world, float x, float y, int z,
                           const PowerConfig& config) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return entt::null;
    }
    const ecs::Entity entity = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(entity, ecs::Position{x, y, z});
    registry.emplace<ecs::SpriteRef>(
        entity, ecs::SpriteRef{ecs::HashName(kGeneratorSprite), 0,
                               0xFFFFFFFFu});
    registry.emplace<ecs::Generator>(
        entity, ecs::Generator{0.0f, config.radius, false});
    return entity;
}

ecs::Entity SpawnColdStorage(ecs::EcsWorld& world, float x, float y, int z,
                             std::string_view sprite, float capacityKg,
                             int maxStacks) {
    const ecs::Entity entity =
        SpawnPlayerStorage(world, x, y, z, sprite, capacityKg, maxStacks);
    if (entity == entt::null) {
        return entity;
    }
    world.Registry().emplace<ecs::ColdStorage>(entity, ecs::ColdStorage{});
    return entity;
}

ecs::Entity SpawnFloodlight(ecs::EcsWorld& world, float x, float y, int z,
                            float maxHp) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return entt::null;
    }
    const float hp = std::clamp(maxHp, 1.0f, 100000.0f);
    const ecs::Entity entity = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(entity, ecs::Position{x, y, z});
    registry.emplace<ecs::SpriteRef>(
        entity, ecs::SpriteRef{ecs::HashName(kFloodlightSprite), 0,
                               0xFFFFFFFFu});
    registry.emplace<ecs::Floodlight>(entity, ecs::Floodlight{});
    registry.emplace<ecs::Health>(entity, ecs::Health{hp, hp});
    registry.emplace<ecs::NoCombatTarget>(entity);
    return entity;
}

void EnsureFloodlightHealth(ecs::EcsWorld& world, float maxHp) {
    const float hp = std::clamp(maxHp, 1.0f, 100000.0f);
    entt::registry& registry = world.Registry();
    registry.view<ecs::Floodlight>().each(
        [&](const ecs::Entity entity, const ecs::Floodlight&) {
            auto* health = registry.try_get<ecs::Health>(entity);
            if (health == nullptr) {
                registry.emplace<ecs::Health>(entity, ecs::Health{hp, hp});
            } else if (!std::isfinite(health->max) || health->max <= 0.0f) {
                health->max = hp;
                health->current = hp;
            } else {
                health->current = std::clamp(health->current, 0.0f,
                                             health->max);
            }
            registry.emplace_or_replace<ecs::NoCombatTarget>(entity);
        });
}

ecs::Entity SpawnSpikeTrap(ecs::EcsWorld& world, float x, float y, int z,
                           const TrapConfig& config) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return entt::null;
    }
    const ecs::Entity entity = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(entity, ecs::Position{x, y, z});
    registry.emplace<ecs::SpriteRef>(
        entity,
        ecs::SpriteRef{ecs::HashName(kSpikeTrapSprite), 0, 0xFFFFFFFFu});
    ecs::SpikeTrap trap;
    trap.damage = config.damage;
    trap.radius = config.radius;
    trap.cooldownSeconds = config.cooldownSeconds;
    trap.staggerSeconds = config.staggerSeconds;
    trap.uses = config.uses;
    registry.emplace<ecs::SpikeTrap>(entity, std::move(trap));
    return entity;
}

ecs::Entity SpawnMerchant(ecs::EcsWorld& world, float x, float y, int z) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return entt::null;
    }
    const ecs::Entity entity = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(entity, ecs::Position{x, y, z});
    registry.emplace<ecs::SpriteRef>(
        entity, ecs::SpriteRef{ecs::HashName("character.placeholder"), 0,
                               0xFFFFFFFFu});
    registry.emplace<ecs::MerchantStock>(entity);
    return entity;
}

}  // namespace zl::game
