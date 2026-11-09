#include "game/ModContent.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Config.h"
#include "core/Log.h"
#include "items/ItemRegistry.h"
#include "items/RecipeRegistry.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "mod-content";

struct ModManifest {
    std::filesystem::path dir;
    std::string id;
    int loadOrder = 100;
};

enum class ContentReadStatus {
    Missing,
    Ok,
    Error,
};

std::optional<std::string> ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

ContentReadStatus ReadJson(const std::filesystem::path& path,
                           nlohmann::json& out, std::string& error) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return ContentReadStatus::Missing;
    }
    const std::optional<std::string> text = ReadText(path);
    if (!text.has_value()) {
        error = "无法读取 " + path.generic_string();
        return ContentReadStatus::Error;
    }
    try {
        out = nlohmann::json::parse(*text);
    } catch (const std::exception& e) {
        error = path.generic_string() + ": JSON 解析失败：" + e.what();
        return ContentReadStatus::Error;
    }
    return ContentReadStatus::Ok;
}

std::optional<ModManifest> ParseManifest(const std::filesystem::path& dir,
                                         std::string& error) {
    const std::optional<std::string> text = ReadText(dir / "mod.json");
    if (!text.has_value()) {
        return std::nullopt;
    }
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(*text);
    } catch (const std::exception& e) {
        error = "mod.json 解析失败：" + std::string(e.what());
        return std::nullopt;
    }
    if (!doc.is_object()) {
        error = "mod.json 顶层必须是对象";
        return std::nullopt;
    }

    ModManifest manifest;
    manifest.dir = dir;
    manifest.id = dir.filename().string();
    if (doc.contains("id")) {
        if (!doc["id"].is_string() || doc["id"].get<std::string>().empty()) {
            error = "mod.json 的 id 必须是非空字符串";
            return std::nullopt;
        }
        manifest.id = doc["id"].get<std::string>();
    }
    if (doc.contains("loadOrder")) {
        if (!doc["loadOrder"].is_number_integer()) {
            error = "mod.json 的 loadOrder 必须是整数";
            return std::nullopt;
        }
        manifest.loadOrder = doc["loadOrder"].get<int>();
    }
    return manifest;
}

bool ValidateItemsDoc(const nlohmann::json& doc, std::string& error) {
    if (!doc.is_object()) {
        error = "根节点必须是对象";
        return false;
    }
    if (!doc.contains("format") || !doc["format"].is_string() ||
        doc["format"].get<std::string>() != "items-v1") {
        error = "format 必须是 items-v1";
        return false;
    }
    if (!doc.contains("items") || !doc["items"].is_array() ||
        doc["items"].empty()) {
        error = "items 必须是非空数组";
        return false;
    }
    return true;
}

bool ValidateRecipesDoc(const nlohmann::json& doc, std::string& error) {
    if (!doc.is_object()) {
        error = "根节点必须是对象";
        return false;
    }
    if (!doc.contains("format") || !doc["format"].is_string()) {
        error = "缺少 format";
        return false;
    }
    const std::string format = doc["format"].get<std::string>();
    if (format != "recipes-v1" && format != "recipes-v2" &&
        format != "recipes-v3" && format != "recipes-v4") {
        error = "format 必须是 recipes-v1/v2/v3/v4";
        return false;
    }
    if (!doc.contains("recipes") || !doc["recipes"].is_array() ||
        doc["recipes"].empty()) {
        error = "recipes 必须是非空数组";
        return false;
    }
    return true;
}

bool ValidateBuildingsDoc(const nlohmann::json& doc, std::string& error) {
    if (!doc.is_object()) {
        error = "根节点必须是对象";
        return false;
    }
    if (!doc.contains("format") || !doc["format"].is_string() ||
        doc["format"].get<std::string>() != "buildings-v1") {
        error = "format 必须是 buildings-v1";
        return false;
    }
    if (!doc.contains("types") || !doc["types"].is_array() ||
        doc["types"].empty()) {
        error = "types 必须是非空数组";
        return false;
    }
    return true;
}

bool ValidateLootDoc(const nlohmann::json& doc, std::string& error) {
    if (!doc.is_object()) {
        error = "根节点必须是对象";
        return false;
    }
    if (!doc.contains("format") || !doc["format"].is_string() ||
        doc["format"].get<std::string>() != "loot-v1") {
        error = "format 必须是 loot-v1";
        return false;
    }
    if (!doc.contains("tables") || !doc["tables"].is_array() ||
        doc["tables"].empty()) {
        error = "tables 必须是非空数组";
        return false;
    }
    return true;
}

bool ValidateForageDoc(const nlohmann::json& doc, std::string& error) {
    if (!doc.is_object()) {
        error = "根节点必须是对象";
        return false;
    }
    if (!doc.contains("format") || !doc["format"].is_string() ||
        doc["format"].get<std::string>() != "forage-v1") {
        error = "format 必须是 forage-v1";
        return false;
    }
    if (!doc.contains("forageZones") || !doc["forageZones"].is_array()) {
        error = "forageZones 必须是数组";
        return false;
    }
    if (!doc.contains("fishingSpots") || !doc["fishingSpots"].is_array()) {
        error = "fishingSpots 必须是数组";
        return false;
    }
    if (doc["forageZones"].empty() && doc["fishingSpots"].empty()) {
        error = "forageZones 与 fishingSpots 不能同时为空";
        return false;
    }
    return true;
}

bool ValidateCropsDoc(const nlohmann::json& doc, std::string& error) {
    if (!doc.is_object()) {
        error = "根节点必须是对象";
        return false;
    }
    if (!doc.contains("format") || !doc["format"].is_string() ||
        doc["format"].get<std::string>() != "crops-v1") {
        error = "format 必须是 crops-v1";
        return false;
    }
    if (!doc.contains("crops") || !doc["crops"].is_array() ||
        doc["crops"].empty()) {
        error = "crops 必须是非空数组";
        return false;
    }
    return true;
}

bool ValidatePoiDoc(const nlohmann::json& doc, std::string& error) {
    if (!doc.is_object()) {
        error = "根节点必须是对象";
        return false;
    }
    if (!doc.contains("format") || !doc["format"].is_string() ||
        doc["format"].get<std::string>() != "pois-v1") {
        error = "format 必须是 pois-v1";
        return false;
    }
    if (!doc.contains("defs") || !doc["defs"].is_array() ||
        doc["defs"].empty()) {
        error = "defs 必须是非空数组";
        return false;
    }
    return true;
}

bool ValidateSettlementDoc(const nlohmann::json& doc, std::string& error) {
    if (!doc.is_object()) {
        error = "根节点必须是对象";
        return false;
    }
    if (!doc.contains("format") || !doc["format"].is_string() ||
        doc["format"].get<std::string>() != "settlements-v1") {
        error = "format 必须是 settlements-v1";
        return false;
    }
    if (!doc.contains("defs") || !doc["defs"].is_array() ||
        doc["defs"].empty()) {
        error = "defs 必须是非空数组";
        return false;
    }
    return true;
}

bool ValidateMissionsDoc(const nlohmann::json& doc, std::string& error) {
    if (!doc.is_object()) {
        error = "根节点必须是对象";
        return false;
    }
    if (!doc.contains("format") || !doc["format"].is_string() ||
        doc["format"].get<std::string>() != "missions-v1") {
        error = "format 必须是 missions-v1";
        return false;
    }
    if (!doc.contains("defs") || !doc["defs"].is_array() ||
        doc["defs"].empty()) {
        error = "defs 必须是非空数组";
        return false;
    }
    return true;
}

void AppendJsonArray(nlohmann::json& target, const nlohmann::json& source,
                     const char* key) {
    for (const nlohmann::json& entry : source[key]) {
        target[key].push_back(entry);
    }
}

}  // namespace

ModContentResult LoadModContent(const core::Config& config,
                                items::ItemRegistry& items,
                                items::RecipeRegistry& recipes) {
    ModContentResult result;
    const std::filesystem::path modsDir =
        config.Get<std::string>("script.modsDir", "mods");
    std::error_code ec;
    if (!std::filesystem::is_directory(modsDir, ec)) {
        return result;
    }

    std::vector<ModManifest> manifests;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(modsDir, ec)) {
        if (!entry.is_directory(ec)) {
            continue;
        }
        ++result.found;
        std::string error;
        std::optional<ModManifest> manifest =
            ParseManifest(entry.path(), error);
        if (!manifest.has_value()) {
            if (!error.empty()) {
                ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：{}",
                        entry.path().filename().string(), error);
                result.skippedIds.push_back(
                    entry.path().filename().string());
                ++result.skipped;
            }
            continue;
        }
        manifests.push_back(std::move(*manifest));
    }

    std::sort(manifests.begin(), manifests.end(),
              [](const ModManifest& a, const ModManifest& b) {
                  if (a.loadOrder != b.loadOrder) {
                      return a.loadOrder < b.loadOrder;
                  }
                  return a.dir.filename().string() <
                         b.dir.filename().string();
              });

    const std::filesystem::path baseItemsPath =
        config.Get<std::string>("items.file", "assets/items.json");
    const std::filesystem::path baseRecipesPath =
        config.Get<std::string>("recipes.file", "assets/recipes.json");
    nlohmann::json acceptedItems;
    nlohmann::json acceptedRecipes;
    std::string error;
    if (ReadJson(baseItemsPath, acceptedItems, error) !=
        ContentReadStatus::Ok) {
        if (error.empty()) {
            error = "文件不存在: " + baseItemsPath.generic_string();
        }
        ZL_ERROR(kLogModule, "无法读取基础物品定义：{}", error);
        result.error = "无法读取基础物品定义：" + error;
        return result;
    }
    if (!ValidateItemsDoc(acceptedItems, error)) {
        ZL_ERROR(kLogModule, "基础物品定义无效：{}", error);
        result.error = "基础物品定义无效：" + error;
        return result;
    }
    if (ReadJson(baseRecipesPath, acceptedRecipes, error) !=
        ContentReadStatus::Ok) {
        if (error.empty()) {
            error = "文件不存在: " + baseRecipesPath.generic_string();
        }
        ZL_ERROR(kLogModule, "无法读取基础配方定义：{}", error);
        result.error = "无法读取基础配方定义：" + error;
        return result;
    }
    if (!ValidateRecipesDoc(acceptedRecipes, error)) {
        ZL_ERROR(kLogModule, "基础配方定义无效：{}", error);
        result.error = "基础配方定义无效：" + error;
        return result;
    }

    std::vector<BuildableDef> knownBuildings = LoadBuildableDefs(config);
    const std::size_t baseItemCount = items.Size();
    const std::size_t baseRecipeCount = recipes.Size();
    std::vector<std::string> seenIds;

    for (const ModManifest& manifest : manifests) {
        if (std::find(seenIds.begin(), seenIds.end(), manifest.id) !=
            seenIds.end()) {
            ZL_WARN(kLogModule, "跳过重复 mod id：{}", manifest.id);
            result.skippedIds.push_back(manifest.id);
            ++result.skipped;
            continue;
        }
        seenIds.push_back(manifest.id);
        result.sources.push_back(
            ModContentSource{manifest.id, manifest.dir.string()});

        const std::filesystem::path contentDir = manifest.dir / "content";
        const std::filesystem::path assetManifestPath =
            manifest.dir / "assets" / "asset_manifest.json";
        std::error_code assetError;
        const bool hasAssetManifest =
            std::filesystem::is_regular_file(assetManifestPath, assetError);
        nlohmann::json modItems;
        nlohmann::json modRecipes;
        nlohmann::json modBuildings;
        nlohmann::json modLoot;
        nlohmann::json modForage;
        nlohmann::json modPois;
        nlohmann::json modMissions;
        nlohmann::json modCrops;
        nlohmann::json modSettlements;
        const ContentReadStatus itemsStatus =
            ReadJson(contentDir / "items.json", modItems, error);
        if (itemsStatus == ContentReadStatus::Error) {
            ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：{}", manifest.id, error);
            result.skippedIds.push_back(manifest.id);
            ++result.skipped;
            continue;
        }
        const ContentReadStatus recipesStatus =
            ReadJson(contentDir / "recipes.json", modRecipes, error);
        if (recipesStatus == ContentReadStatus::Error) {
            ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：{}", manifest.id, error);
            result.skippedIds.push_back(manifest.id);
            ++result.skipped;
            continue;
        }
        const ContentReadStatus buildingsStatus =
            ReadJson(contentDir / "buildings.json", modBuildings, error);
        if (buildingsStatus == ContentReadStatus::Error) {
            ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：{}", manifest.id, error);
            result.skippedIds.push_back(manifest.id);
            ++result.skipped;
            continue;
        }
        const ContentReadStatus lootStatus =
            ReadJson(contentDir / "loot.json", modLoot, error);
        if (lootStatus == ContentReadStatus::Error) {
            ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：{}", manifest.id, error);
            result.skippedIds.push_back(manifest.id);
            ++result.skipped;
            continue;
        }
        const ContentReadStatus forageStatus =
            ReadJson(contentDir / "forage.json", modForage, error);
        if (forageStatus == ContentReadStatus::Error) {
            ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：{}", manifest.id, error);
            result.skippedIds.push_back(manifest.id);
            ++result.skipped;
            continue;
        }
        const ContentReadStatus poiStatus =
            ReadJson(contentDir / "pois.json", modPois, error);
        if (poiStatus == ContentReadStatus::Error) {
            ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：{}", manifest.id, error);
            result.skippedIds.push_back(manifest.id);
            ++result.skipped;
            continue;
        }
        const ContentReadStatus missionStatus =
            ReadJson(contentDir / "missions.json", modMissions, error);
        if (missionStatus == ContentReadStatus::Error) {
            ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：{}", manifest.id, error);
            result.skippedIds.push_back(manifest.id);
            ++result.skipped;
            continue;
        }
        const ContentReadStatus cropsStatus =
            ReadJson(contentDir / "crops.json", modCrops, error);
        if (cropsStatus == ContentReadStatus::Error) {
            ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：{}", manifest.id, error);
            result.skippedIds.push_back(manifest.id);
            ++result.skipped;
            continue;
        }
        const ContentReadStatus settlementStatus =
            ReadJson(contentDir / "settlements.json", modSettlements, error);
        if (settlementStatus == ContentReadStatus::Error) {
            ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：{}", manifest.id, error);
            result.skippedIds.push_back(manifest.id);
            ++result.skipped;
            continue;
        }
        if (itemsStatus == ContentReadStatus::Missing &&
            recipesStatus == ContentReadStatus::Missing &&
            buildingsStatus == ContentReadStatus::Missing &&
            lootStatus == ContentReadStatus::Missing &&
            forageStatus == ContentReadStatus::Missing &&
            poiStatus == ContentReadStatus::Missing &&
            missionStatus == ContentReadStatus::Missing &&
            cropsStatus == ContentReadStatus::Missing &&
            settlementStatus == ContentReadStatus::Missing) {
            if (hasAssetManifest) {
                ++result.applied;
                result.assetManifests.push_back(
                    assetManifestPath.generic_string());
            }
            continue;
        }

        if (itemsStatus == ContentReadStatus::Ok &&
            !ValidateItemsDoc(modItems, error)) {
            ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：items.json {}", manifest.id,
                    error);
            result.skippedIds.push_back(manifest.id);
            ++result.skipped;
            continue;
        }
        if (recipesStatus == ContentReadStatus::Ok &&
            !ValidateRecipesDoc(modRecipes, error)) {
            ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：recipes.json {}",
                    manifest.id, error);
            result.skippedIds.push_back(manifest.id);
            ++result.skipped;
            continue;
        }
        if (buildingsStatus == ContentReadStatus::Ok &&
            !ValidateBuildingsDoc(modBuildings, error)) {
            ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：buildings.json {}",
                    manifest.id, error);
            result.skippedIds.push_back(manifest.id);
            ++result.skipped;
            continue;
        }
        if (lootStatus == ContentReadStatus::Ok &&
            !ValidateLootDoc(modLoot, error)) {
            ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：loot.json {}", manifest.id,
                    error);
            result.skippedIds.push_back(manifest.id);
            ++result.skipped;
            continue;
        }
        if (forageStatus == ContentReadStatus::Ok &&
            !ValidateForageDoc(modForage, error)) {
            ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：forage.json {}", manifest.id,
                    error);
            result.skippedIds.push_back(manifest.id);
            ++result.skipped;
            continue;
        }
        if (poiStatus == ContentReadStatus::Ok &&
            !ValidatePoiDoc(modPois, error)) {
            ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：pois.json {}", manifest.id,
                    error);
            result.skippedIds.push_back(manifest.id);
            ++result.skipped;
            continue;
        }
        if (missionStatus == ContentReadStatus::Ok &&
            !ValidateMissionsDoc(modMissions, error)) {
            ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：missions.json {}",
                    manifest.id, error);
            result.skippedIds.push_back(manifest.id);
            ++result.skipped;
            continue;
        }
        if (cropsStatus == ContentReadStatus::Ok &&
            !ValidateCropsDoc(modCrops, error)) {
            ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：crops.json {}", manifest.id,
                    error);
            result.skippedIds.push_back(manifest.id);
            ++result.skipped;
            continue;
        }
        if (settlementStatus == ContentReadStatus::Ok &&
            !ValidateSettlementDoc(modSettlements, error)) {
            ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：settlements.json {}",
                    manifest.id, error);
            result.skippedIds.push_back(manifest.id);
            ++result.skipped;
            continue;
        }

        std::vector<BuildableDef> modBuildableDefs;
        if (buildingsStatus == ContentReadStatus::Ok) {
            modBuildableDefs = ParseBuildableDefs(
                modBuildings["types"],
                (contentDir / "buildings.json").generic_string());
            if (modBuildableDefs.empty()) {
                ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：buildings.json 无有效定义",
                        manifest.id);
                result.skippedIds.push_back(manifest.id);
                ++result.skipped;
                continue;
            }
        }

        nlohmann::json candidateItems = acceptedItems;
        nlohmann::json candidateRecipes = acceptedRecipes;
        if (itemsStatus == ContentReadStatus::Ok) {
            AppendJsonArray(candidateItems, modItems, "items");
        }
        if (recipesStatus == ContentReadStatus::Ok) {
            AppendJsonArray(candidateRecipes, modRecipes, "recipes");
        }

        if (itemsStatus == ContentReadStatus::Ok) {
            if (!items.LoadFromString(
                    candidateItems.dump(),
                    "base+mods（" + manifest.id + "）items", &error)) {
                ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：{}", manifest.id, error);
                result.skippedIds.push_back(manifest.id);
                ++result.skipped;
                continue;
            }
        }
        if (!recipes.LoadFromString(
                candidateRecipes.dump(),
                "base+mods（" + manifest.id + "）recipes", items, &error)) {
            ZL_WARN(kLogModule, "跳过 mod 内容 [{}]：{}", manifest.id, error);
            if (itemsStatus == ContentReadStatus::Ok &&
                !items.LoadFromString(acceptedItems.dump(),
                                      "mod rollback", &error)) {
                ZL_ERROR(kLogModule, "回滚基础物品定义失败：{}", error);
                return result;
            }
            result.skippedIds.push_back(manifest.id);
            ++result.skipped;
            continue;
        }

        acceptedItems = std::move(candidateItems);
        acceptedRecipes = std::move(candidateRecipes);
        ++result.applied;
        if (hasAssetManifest) {
            result.assetManifests.push_back(
                assetManifestPath.generic_string());
        }
        if (lootStatus == ContentReadStatus::Ok) {
            result.lootFiles.push_back(
                (contentDir / "loot.json").generic_string());
        }
        if (forageStatus == ContentReadStatus::Ok) {
            result.forageFiles.push_back(
                (contentDir / "forage.json").generic_string());
        }
        if (poiStatus == ContentReadStatus::Ok) {
            result.poiFiles.push_back(
                (contentDir / "pois.json").generic_string());
        }
        if (missionStatus == ContentReadStatus::Ok) {
            result.missionFiles.push_back(
                (contentDir / "missions.json").generic_string());
        }
        if (cropsStatus == ContentReadStatus::Ok) {
            result.cropFiles.push_back(
                (contentDir / "crops.json").generic_string());
        }
        if (settlementStatus == ContentReadStatus::Ok) {
            result.settlementFiles.push_back(
                (contentDir / "settlements.json").generic_string());
        }
        for (const BuildableDef& def : modBuildableDefs) {
            const std::vector<BuildableDef> one{def};
            if (AppendBuildableDefs(knownBuildings, one) == 1) {
                result.buildables.push_back(def);
            } else {
                ZL_WARN(kLogModule, "跳过重复/非法建造项 [{}]：{}",
                        manifest.id, def.id);
                ++result.buildablesSkipped;
            }
        }
    }

    result.itemsAdded =
        static_cast<int>(items.Size()) - static_cast<int>(baseItemCount);
    result.recipesAdded =
        static_cast<int>(recipes.Size()) - static_cast<int>(baseRecipeCount);
    ZL_INFO(kLogModule,
            "mod 内容合并完成：{} 个目录，应用 {}，跳过 {}；物品 +{}，配方 +{}，"
            "建造 +{}（跳过 {}），战利品表 {}，觅食 {}，POI {}，任务 {}，作物 {}，"
            "聚落 {}，资源清单 {}",
            result.found, result.applied, result.skipped, result.itemsAdded,
            result.recipesAdded,
            static_cast<int>(result.buildables.size()),
            result.buildablesSkipped,
            static_cast<int>(result.lootFiles.size()),
            static_cast<int>(result.forageFiles.size()),
            static_cast<int>(result.poiFiles.size()),
            static_cast<int>(result.missionFiles.size()),
            static_cast<int>(result.cropFiles.size()),
            static_cast<int>(result.settlementFiles.size()),
            static_cast<int>(result.assetManifests.size()));
    return result;
}

}  // namespace zl::game
