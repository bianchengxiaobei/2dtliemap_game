#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

#include "core/Config.h"
#include "game/AssetManifest.h"
#include "game/ModContent.h"
#include "items/ItemRegistry.h"
#include "items/RecipeRegistry.h"

using namespace zl;

namespace {

struct TempRoot {
    std::filesystem::path path;

    explicit TempRoot(const std::string& name)
        : path(std::filesystem::temp_directory_path() / "zl_mod_assets" / name) {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path, ec);
    }

    ~TempRoot() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

void WriteText(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << text;
}

const char* kBaseItems = R"({
  "format": "items-v1",
  "items": [
    { "id": "base_item", "name": "基础物品", "icon": "item.rag", "weight": 0.1, "maxStack": 2, "type": "generic" }
  ]
})";

const char* kBaseRecipes = R"({
  "format": "recipes-v3",
  "recipes": [
    {
      "id": "base_recipe",
      "name": "基础配方",
      "output": { "item": "base_item", "count": 1 },
      "ingredients": [ { "item": "base_item", "count": 1 } ],
      "time": 0.1
    }
  ]
})";

std::string BaseManifest(const std::string& extraLogical = "") {
    return R"({
  "version": 1,
  "logical": {
    "base.icon": {
      "own": {
        "atlas": "assets/base.atlas.json",
        "frames": [ "base" ]
      }
    })" + extraLogical + R"(
  }
})";
}

}  // namespace

TEST_CASE("X-F2b-2 资源清单：mod 新逻辑名与本地图集路径合并") {
    TempRoot root("merge");
    const std::filesystem::path baseManifest =
        root.path / "assets" / "asset_manifest.json";
    const std::filesystem::path baseItems = root.path / "base_items.json";
    const std::filesystem::path baseRecipes = root.path / "base_recipes.json";
    WriteText(baseManifest, BaseManifest());
    WriteText(baseItems, kBaseItems);
    WriteText(baseRecipes, kBaseRecipes);

    const std::filesystem::path modDir = root.path / "mods" / "asset_mod";
    const std::filesystem::path modManifest =
        modDir / "assets" / "asset_manifest.json";
    WriteText(modDir / "mod.json", R"({"id":"asset_mod","loadOrder":10})");
    WriteText(modManifest, R"({
  "logical": {
    "mod.icon": {
      "own": {
        "atlas": "atlas_mod.atlas.json",
        "frames": [ "mod_icon" ]
      }
    }
  }
})");
    WriteText(modDir / "assets" / "atlas_mod.atlas.json", "{}");

    core::Config config;
    config.Set("items.file", baseItems.generic_string());
    config.Set("recipes.file", baseRecipes.generic_string());
    config.Set("script.modsDir", (root.path / "mods").generic_string());

    items::ItemRegistry items;
    items::RecipeRegistry recipes;
    REQUIRE(items.LoadFile(baseItems));
    REQUIRE(recipes.LoadFile(baseRecipes, items));

    const game::ModContentResult content =
        game::LoadModContent(config, items, recipes);
    CHECK(content.found == 1);
    CHECK(content.applied == 1);
    CHECK(content.skipped == 0);
    REQUIRE(content.assetManifests.size() == 1);
    CHECK(std::filesystem::exists(content.assetManifests[0]));

    nlohmann::json merged;
    game::AssetManifestMergeStats stats;
    std::string error;
    REQUIRE_MESSAGE(game::LoadMergedAssetManifest(
                        baseManifest, {modManifest}, &merged, &stats, &error),
                    error);
    CHECK(stats.manifestsLoaded == 1);
    CHECK(stats.manifestsSkipped == 0);
    CHECK(stats.bindingsAdded == 1);
    CHECK(stats.bindingsSkipped == 0);
    CHECK(merged["logical"].contains("base.icon"));
    REQUIRE(merged["logical"].contains("mod.icon"));
    CHECK(merged["logical"]["mod.icon"]["own"]["atlas"].get<std::string>() ==
          (modDir / "assets" / "atlas_mod.atlas.json")
              .lexically_normal()
              .generic_string());
}

TEST_CASE("X-F2b-2 资源清单：重复逻辑名和坏清单不破坏基础绑定") {
    TempRoot root("duplicate");
    const std::filesystem::path baseManifest =
        root.path / "assets" / "asset_manifest.json";
    const std::filesystem::path modManifest =
        root.path / "mods" / "asset_mod" / "assets" / "asset_manifest.json";
    const std::filesystem::path brokenManifest =
        root.path / "mods" / "broken" / "assets" / "asset_manifest.json";
    WriteText(baseManifest, BaseManifest(R"(,
    "mod.icon": {
      "own": {
        "atlas": "assets/base.atlas.json",
        "frames": [ "base_icon" ]
      }
    })"));
    WriteText(modManifest, R"({
  "logical": {
    "mod.icon": {
      "own": {
        "atlas": "atlas_mod.atlas.json",
        "frames": [ "mod_icon" ]
      }
    },
    "mod.new": {
      "own": {
        "atlas": "atlas_mod.atlas.json",
        "frames": [ "mod_new" ]
      }
    }
  }
})");
    WriteText(root.path / "mods" / "asset_mod" / "assets" /
                   "atlas_mod.atlas.json",
              "{}");
    WriteText(brokenManifest, "{ this is not json");

    nlohmann::json merged;
    game::AssetManifestMergeStats stats;
    std::string error;
    REQUIRE_MESSAGE(game::LoadMergedAssetManifest(
                        baseManifest, {modManifest, brokenManifest}, &merged,
                        &stats, &error),
                    error);
    CHECK(stats.manifestsLoaded == 1);
    CHECK(stats.manifestsSkipped == 1);
    CHECK(stats.bindingsAdded == 1);
    CHECK(stats.bindingsSkipped == 1);
    CHECK(merged["logical"]["mod.icon"]["own"]["frames"][0].get<std::string>() ==
          "base_icon");
    REQUIRE(merged["logical"].contains("mod.new"));
    CHECK(merged["logical"]["mod.new"]["own"]["atlas"].get<std::string>() ==
          (root.path / "mods" / "asset_mod" / "assets" /
           "atlas_mod.atlas.json")
              .lexically_normal()
              .generic_string());
}

TEST_CASE("X-F2b-2 示例 mod 的资源清单可合并到基础清单") {
    const std::filesystem::path root = std::filesystem::path(ZL_SOURCE_DIR);
    core::Config config;
    config.Set("items.file", (root / "assets" / "items.json").generic_string());
    config.Set("recipes.file",
               (root / "assets" / "recipes.json").generic_string());
    config.Set("script.modsDir", (root / "mods").generic_string());

    items::ItemRegistry items;
    items::RecipeRegistry recipes;
    REQUIRE(items.LoadFile(root / "assets" / "items.json"));
    REQUIRE(recipes.LoadFile(root / "assets" / "recipes.json", items));

    const game::ModContentResult content =
        game::LoadModContent(config, items, recipes);
    REQUIRE(!content.assetManifests.empty());

    nlohmann::json merged;
    game::AssetManifestMergeStats stats;
    std::string error;
    REQUIRE_MESSAGE(game::LoadMergedAssetManifest(
                        root / "assets" / "asset_manifest.json",
                        {std::filesystem::path(content.assetManifests.front())},
                        &merged, &stats, &error),
                    error);
    REQUIRE(merged["logical"].contains("mod.camp_stew"));
    REQUIRE(merged["logical"].contains("mod.supply_crate"));
    const std::string atlas =
        merged["logical"]["mod.camp_stew"]["own"]["atlas"].get<std::string>();
    REQUIRE(std::filesystem::exists(atlas));
    std::ifstream atlasFile(atlas, std::ios::binary);
    REQUIRE(atlasFile.is_open());
    nlohmann::json atlasDoc;
    atlasFile >> atlasDoc;
    const std::string image = atlasDoc.value("image", "");
    REQUIRE(!image.empty());
    CHECK(std::filesystem::exists(std::filesystem::path(atlas).parent_path() /
                                  image));
    CHECK(stats.bindingsAdded >= 2);
}
