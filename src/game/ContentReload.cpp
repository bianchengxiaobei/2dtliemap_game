#include "game/ContentReload.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Config.h"
#include "core/Log.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "content-reload";

ContentBundleResult Fail(std::string error) {
    ContentBundleResult result;
    result.error = std::move(error);
    ZL_WARN(kLogModule, "内容事务构建失败：{}", result.error);
    return result;
}

std::string SkipSummary(const std::vector<std::string>& ids) {
    if (ids.empty()) {
        return "未知 mod";
    }
    std::string out;
    const std::size_t limit = ids.size() < 5 ? ids.size() : 5;
    for (std::size_t i = 0; i < limit; ++i) {
        if (!out.empty()) {
            out += ", ";
        }
        out += ids[i];
    }
    if (ids.size() > limit) {
        out += ", ...";
    }
    return out;
}

bool MergeLoot(const std::vector<std::string>& files,
               const items::ItemRegistry& items,
               items::LootTableRegistry* out, std::string* error) {
    for (const std::string& file : files) {
        if (!out->MergeFile(file, items, error)) {
            *error = "mod 战利品表 " + file + "：" + *error;
            return false;
        }
    }
    return true;
}

bool LoadForage(const core::Config& config,
                const items::LootTableRegistry& loot,
                const std::vector<std::string>& modFiles,
                ForageRegistry* out, std::string* error) {
    const std::string path =
        config.Get<std::string>("forage.file", "assets/forage.json");
    if (!out->LoadFile(path, loot, error)) {
        *error = "基础觅食数据 " + path + "：" + *error;
        return false;
    }
    for (const std::string& file : modFiles) {
        if (!out->MergeFile(file, loot, error)) {
            *error = "mod 觅食数据 " + file + "：" + *error;
            return false;
        }
    }
    return true;
}

bool LoadAnimals(const core::Config& config,
                 const items::ItemRegistry& items,
                 const items::LootTableRegistry& loot,
                 AnimalRegistry* out, std::string* error) {
    const std::string path =
        config.Get<std::string>("animals.file", "assets/animals.json");
    if (!out->LoadFile(path, items, loot, error)) {
        *error = "基础动物数据 " + path + "：" + *error;
        return false;
    }
    return true;
}

bool LoadCrops(const core::Config& config,
               const items::ItemRegistry& items,
               const std::vector<std::string>& modFiles,
               CropRegistry* out, std::string* error) {
    const std::string path =
        config.Get<std::string>("crops.file", "assets/crops.json");
    if (!out->LoadFile(path, items, error)) {
        *error = "基础作物数据 " + path + "：" + *error;
        return false;
    }
    for (const std::string& file : modFiles) {
        if (!out->MergeFile(file, items, error)) {
            *error = "mod 作物数据 " + file + "：" + *error;
            return false;
        }
    }
    return true;
}

bool LoadPois(const core::Config& config,
              const std::vector<std::string>& modFiles,
              PoiConfig* out, std::string* error) {
    if (!LoadPoiConfig(config, out, error)) {
        *error = "基础 POI 配置：" + *error;
        return false;
    }
    if (!out->enabled || modFiles.empty()) {
        return true;
    }
    for (const std::string& file : modFiles) {
        PoiMergeStats stats;
        if (!MergePoiFile(file, out, &stats, error)) {
            *error = "mod POI 文件 " + file + "：" + *error;
            return false;
        }
        if (stats.defsSkipped > 0) {
            *error = "mod POI 文件 " + file + " 有 " +
                     std::to_string(stats.defsSkipped) + " 条定义被跳过";
            return false;
        }
    }
    return true;
}

bool LoadMissions(const core::Config& config,
                  const std::vector<std::string>& modFiles,
                  MissionConfig* out, std::string* missionSourceJson,
                  std::string* error) {
    if (!LoadMissionConfig(config, out, error)) {
        *error = "基础任务配置：" + *error;
        return false;
    }
    missionSourceJson->clear();
    const nlohmann::json& root = config.Raw();
    if (root.contains("missions") && root["missions"].is_object()) {
        *missionSourceJson = root["missions"].dump();
    }
    if (!out->enabled || modFiles.empty()) {
        return true;
    }
    MissionMergeStats stats;
    if (!MergeMissionFiles(modFiles, config, out, &stats, error,
                           missionSourceJson)) {
        *error = "mod 任务合并：" + *error;
        return false;
    }
    if (stats.filesLoaded != static_cast<int>(modFiles.size())) {
        *error = "mod 任务文件有 " +
                 std::to_string(static_cast<int>(modFiles.size()) -
                                stats.filesLoaded) +
                 " 个未通过校验";
        return false;
    }
    return true;
}

bool LoadWorldEvents(const core::Config& config,
                     const std::vector<std::string>& modFiles,
                     const items::ItemRegistry& items, WorldEventConfig* out,
                     std::string* error) {
    if (!LoadWorldEventConfig(config, out, error, &items)) {
        *error = "basic world event config: " + *error;
        return false;
    }
    if (!out->enabled || modFiles.empty()) {
        return true;
    }
    WorldEventMergeStats stats;
    for (const std::string& file : modFiles) {
        const int skippedBefore = stats.defsSkipped;
        if (!MergeWorldEventFile(file, out, &stats, error, &items)) {
            *error = "mod world event file " + file + ": " + *error;
            return false;
        }
        if (stats.defsSkipped > skippedBefore) {
            *error = "mod world event file " + file +
                     " has skipped definitions";
            return false;
        }
    }
    return true;
}

bool LoadCompanions(const core::Config& config,
                    const std::vector<std::string>& modFiles,
                    const items::ItemRegistry& items, CompanionConfig* out,
                    std::string* error) {
    if (!LoadCompanionConfig(config, out, error, &items)) {
        *error = "基础同伴配置：" + *error;
        return false;
    }
    if (!out->enabled || modFiles.empty()) {
        return true;
    }
    CompanionMergeStats stats;
    for (const std::string& file : modFiles) {
        const int skippedBefore = stats.defsSkipped;
        if (!MergeCompanionFile(file, out, &stats, error, &items)) {
            *error = "mod 同伴文件 " + file + "：" + *error;
            return false;
        }
        if (stats.defsSkipped > skippedBefore) {
            *error = "mod 同伴文件 " + file + " 有定义被跳过";
            return false;
        }
    }
    return true;
}

bool LoadVehicleMods(const core::Config& config,
                     const std::vector<std::string>& modFiles,
                     const items::ItemRegistry& items, VehicleModsConfig* out,
                     std::string* error) {
    if (!LoadVehicleModConfig(config, out, error, &items)) {
        *error = "基础车辆改装配置：" + *error;
        return false;
    }
    if (!out->enabled || modFiles.empty()) {
        return true;
    }
    VehicleModMergeStats stats;
    for (const std::string& file : modFiles) {
        if (!MergeVehicleModFile(file, out, &stats, error, &items)) {
            *error = "mod 车辆改装文件 " + file + "：" + *error;
            return false;
        }
    }
    return true;
}

bool LoadTraps(const core::Config& config,
               const std::vector<std::string>& modFiles,
               const std::vector<BuildableDef>& buildables, TrapDefsConfig* out,
               std::string* error) {
    if (!LoadTrapDefsConfig(config, buildables, out, error)) {
        *error = "基础陷阱配置：" + *error;
        return false;
    }
    if (!out->enabled || modFiles.empty()) {
        return true;
    }
    TrapMergeStats stats;
    for (const std::string& file : modFiles) {
        if (!MergeTrapFile(file, out, &stats, buildables, error)) {
            *error = "mod 陷阱文件 " + file + "：" + *error;
            return false;
        }
    }
    return true;
}

bool LoadSettlements(const core::Config& config,
                     const std::vector<std::string>& modFiles,
                     SettlementConfig* out, std::string* error) {
    if (!LoadSettlementConfig(config, out, error)) {
        *error = "基础聚落配置：" + *error;
        return false;
    }
    if (!out->enabled || modFiles.empty()) {
        return true;
    }
    SettlementMergeStats stats;
    for (const std::string& file : modFiles) {
        const int skippedBefore = stats.defsSkipped;
        if (!MergeSettlementFile(file, out, &stats, error)) {
            *error = "mod 聚落文件 " + file + "：" + *error;
            return false;
        }
        if (stats.defsSkipped > skippedBefore) {
            *error = "mod 聚落文件 " + file + " 有定义被跳过";
            return false;
        }
    }
    return true;
}

}  // namespace

std::string ContentReloadBlockedReason(bool allowDynamicContent,
                                       bool networkMode,
                                       int dynamicContentCount) {
    if (!allowDynamicContent || networkMode) {
        return kContentReloadNetworkBlockedMessage;
    }
    if (dynamicContentCount > 0) {
        return kContentReloadDynamicBlockedMessage;
    }
    return {};
}

std::string DynamicContentBlockedReason(bool allowDynamicContent) {
    if (!allowDynamicContent) {
        return kDynamicContentSinglePlayerOnlyMessage;
    }
    return {};
}

ContentBundleResult LoadContentBundle(const core::Config& config,
                                       const core::Config* contentConfig) {
    auto bundle = std::make_unique<ContentBundle>();
    std::string error;

    const std::string itemsFile =
        config.Get<std::string>("items.file", "assets/items.json");
    const std::string recipesFile =
        config.Get<std::string>("recipes.file", "assets/recipes.json");
    if (!bundle->items.LoadFile(itemsFile, &error)) {
        return Fail("基础物品定义 " + itemsFile + "：" + error);
    }
    if (!bundle->recipes.LoadFile(recipesFile, bundle->items, &error)) {
        return Fail("基础配方定义 " + recipesFile + "：" + error);
    }

    bundle->modContent =
        LoadModContent(config, bundle->items, bundle->recipes);
    if (!bundle->modContent.error.empty()) {
        return Fail(bundle->modContent.error);
    }
    if (bundle->modContent.skipped > 0) {
        return Fail("mod 内容校验失败，跳过 " +
                    std::to_string(bundle->modContent.skipped) +
                    " 个： " + SkipSummary(bundle->modContent.skippedIds));
    }
    if (bundle->modContent.buildablesSkipped > 0) {
        return Fail("mod 建造项有 " +
                    std::to_string(bundle->modContent.buildablesSkipped) +
                    " 条重复/非法");
    }

    const std::string lootFile =
        config.Get<std::string>("loot.file", "assets/loot.json");
    if (!bundle->lootTables.LoadFile(lootFile, bundle->items, &error)) {
        return Fail("基础战利品表 " + lootFile + "：" + error);
    }
    if (!MergeLoot(bundle->modContent.lootFiles, bundle->items,
                   &bundle->lootTables, &error)) {
        return Fail(std::move(error));
    }
    if (!LoadForage(config, bundle->lootTables, bundle->modContent.forageFiles,
                    &bundle->forage, &error)) {
        return Fail(std::move(error));
    }
    if (!LoadAnimals(config, bundle->items, bundle->lootTables,
                     &bundle->animals, &error)) {
        return Fail(std::move(error));
    }
    if (!LoadCrops(config, bundle->items, bundle->modContent.cropFiles,
                   &bundle->crops, &error)) {
        return Fail(std::move(error));
    }
    const core::Config& poiMissionConfig =
        contentConfig != nullptr ? *contentConfig : config;
    if (!LoadPois(poiMissionConfig, bundle->modContent.poiFiles,
                  &bundle->poiConfig, &error)) {
        return Fail(std::move(error));
    }
    if (!LoadMissions(poiMissionConfig, bundle->modContent.missionFiles,
                      &bundle->missionConfig, &bundle->missionSourceJson,
                      &error)) {
        return Fail(std::move(error));
    }
    if (!LoadSettlements(config, bundle->modContent.settlementFiles,
                         &bundle->settlements, &error)) {
        return Fail(std::move(error));
    }
    if (!LoadWorldEvents(config, bundle->modContent.worldEventFiles,
                         bundle->items, &bundle->worldEvents, &error)) {
        return Fail(std::move(error));
    }
    if (!LoadCompanions(config, bundle->modContent.companionFiles,
                        bundle->items, &bundle->companions, &error)) {
        return Fail(std::move(error));
    }
    if (!LoadVehicleMods(config, bundle->modContent.vehicleModFiles,
                         bundle->items, &bundle->vehicleMods, &error)) {
        return Fail(std::move(error));
    }
    std::vector<BuildableDef> trapBuildables = LoadBuildableDefs(config);
    AppendBuildableDefs(trapBuildables, bundle->modContent.buildables);
    if (!LoadTraps(config, bundle->modContent.trapFiles, trapBuildables,
                   &bundle->traps, &error)) {
        return Fail(std::move(error));
    }

    ZL_INFO(kLogModule,
            "content bundle ready: items {}, recipes {}, buildables +{}, loot {}, "
            "forage {} zones/{} spots, animals {}, crops {}, POI {}, missions {}, "
            "settlements {}, world events {}, companions {}, vehicle mods {}, "
            "traps {}",
            bundle->items.Size(), bundle->recipes.Size(),
            static_cast<int>(bundle->modContent.buildables.size()),
            bundle->lootTables.Size(), bundle->forage.ZoneCount(),
            bundle->forage.SpotCount(), bundle->animals.Size(),
            bundle->crops.Size(), bundle->poiConfig.defs.size(),
            bundle->missionConfig.defs.size(), bundle->settlements.defs.size(),
            bundle->worldEvents.defs.size(), bundle->companions.defs.size(),
            bundle->vehicleMods.defs.size(), bundle->traps.defs.size());

    ContentBundleResult result;
    result.bundle = std::move(bundle);
    return result;
}

}  // namespace zl::game
