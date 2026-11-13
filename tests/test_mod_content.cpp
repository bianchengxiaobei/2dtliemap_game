#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "core/Config.h"
#include "game/BuildingRules.h"
#include "game/Crops.h"
#include "game/Missions.h"
#include "game/ModContent.h"
#include "game/Poi.h"
#include "game/Settlement.h"
#include "game/WorldEvent.h"
#include "items/ItemRegistry.h"
#include "items/LootTable.h"
#include "items/RecipeRegistry.h"

using namespace zl;

namespace {

struct TempRoot {
    std::filesystem::path path;

    explicit TempRoot(const std::string& name)
        : path(std::filesystem::temp_directory_path() / "zl_mod_content" / name) {
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

void MakeMod(const std::filesystem::path& root, const std::string& id,
             int loadOrder, const std::string& items,
             const std::string& recipes, const std::string& buildings,
             const std::string& loot = {}, const std::string& pois = {},
             const std::string& missions = {},
             const std::string& forage = {},
             const std::string& settlements = {},
             const std::string& worldEvents = {}) {
    const std::filesystem::path dir = root / id;
    WriteText(dir / "mod.json",
              "{\"id\":\"" + id + "\",\"loadOrder\":" +
                  std::to_string(loadOrder) + "}");
    if (!items.empty()) {
        WriteText(dir / "content" / "items.json", items);
    }
    if (!recipes.empty()) {
        WriteText(dir / "content" / "recipes.json", recipes);
    }
    if (!buildings.empty()) {
        WriteText(dir / "content" / "buildings.json", buildings);
    }
    if (!loot.empty()) {
        WriteText(dir / "content" / "loot.json", loot);
    }
    if (!pois.empty()) {
        WriteText(dir / "content" / "pois.json", pois);
    }
    if (!missions.empty()) {
        WriteText(dir / "content" / "missions.json", missions);
    }
    if (!forage.empty()) {
        WriteText(dir / "content" / "forage.json", forage);
    }
    if (!settlements.empty()) {
        WriteText(dir / "content" / "settlements.json", settlements);
    }
    if (!worldEvents.empty()) {
        WriteText(dir / "content" / "world_events.json", worldEvents);
    }
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

}  // namespace

TEST_CASE("X-F2：mod 内容合并物品/配方/建造并可回滚失败条目") {
    TempRoot root("merge");
    const std::filesystem::path baseItems = root.path / "base_items.json";
    const std::filesystem::path baseRecipes = root.path / "base_recipes.json";
    WriteText(baseItems, kBaseItems);
    WriteText(baseRecipes, kBaseRecipes);

    MakeMod(root.path / "mods", "alpha", 20, R"({
      "format": "items-v1",
      "items": [
        { "id": "mod_item", "name": "模组物品", "icon": "item.rag", "weight": 0.2, "maxStack": 3, "type": "generic", "unlocksRecipes": ["mod_recipe"] }
      ]
    })", R"({
      "format": "recipes-v3",
      "recipes": [
        {
          "id": "mod_recipe",
          "name": "模组配方",
          "output": { "item": "mod_item", "count": 1 },
          "ingredients": [ { "item": "base_item", "count": 1 } ],
          "time": 0.2
        }
      ]
    })", R"({
      "format": "buildings-v1",
      "types": [
        {
          "id": "mod_crate",
          "name": "模组补给箱",
          "sprite": "object.crate",
          "woodCost": 3,
          "blocksMovement": false,
          "storageKg": 35,
          "storageSlots": 4,
          "category": "storage"
        }
      ]
    })");

    MakeMod(root.path / "mods", "beta", 30, R"({
      "format": "items-v1",
      "items": [
        { "id": "base_item", "name": "重复物品", "icon": "item.rag", "weight": 0.1, "maxStack": 1, "type": "generic" }
      ]
    })", "", "");

    MakeMod(root.path / "mods", "delta", 35, R"({
      "format": "items-v1",
      "items": [
        { "id": "delta_item", "name": "待回滚物品", "icon": "item.rag", "weight": 0.1, "maxStack": 1, "type": "generic", "unlocksRecipes": ["missing_recipe"] }
      ]
    })", "", "");

    MakeMod(root.path / "mods", "gamma", 40, R"({
      "format": "items-v1",
      "items": [
        { "id": "gamma_item", "name": "后续物品", "icon": "item.rag", "weight": 0.1, "maxStack": 1, "type": "generic" }
      ]
    })", R"({
      "format": "recipes-v3",
      "recipes": [
        {
          "id": "gamma_recipe",
          "name": "后续配方",
          "output": { "item": "gamma_item", "count": 1 },
          "ingredients": [ { "item": "base_item", "count": 1 } ],
          "time": 0.1
        }
      ]
    })", "");

    MakeMod(root.path / "mods", "broken", 50, "{ this is not json", "", "");

    core::Config config;
    config.Set("items.file", baseItems.generic_string());
    config.Set("recipes.file", baseRecipes.generic_string());
    config.Set("script.modsDir", (root.path / "mods").generic_string());

    items::ItemRegistry items;
    items::RecipeRegistry recipes;
    REQUIRE(items.LoadFile(baseItems));
    REQUIRE(recipes.LoadFile(baseRecipes, items));

    const game::ModContentResult result =
        game::LoadModContent(config, items, recipes);
    CHECK(result.found == 5);
    CHECK(result.applied == 2);
    CHECK(result.skipped == 3);
    CHECK(result.itemsAdded == 2);
    CHECK(result.recipesAdded == 2);
    REQUIRE(result.buildables.size() == 1);
    CHECK(result.buildables[0].id == "mod_crate");
    CHECK(result.buildables[0].storageKg == doctest::Approx(35.0f));
    CHECK(result.buildables[0].storageSlots == 4);

    REQUIRE(items.Find("base_item") != nullptr);
    REQUIRE(items.Find("mod_item") != nullptr);
    REQUIRE(items.Find("gamma_item") != nullptr);
    CHECK(items.Find("beta_item") == nullptr);
    CHECK(items.Find("delta_item") == nullptr);
    CHECK(items.Find("mod_item") != nullptr);
    REQUIRE(recipes.Find("base_recipe") != nullptr);
    REQUIRE(recipes.Find("mod_recipe") != nullptr);
    REQUIRE(recipes.Find("gamma_recipe") != nullptr);

    std::vector<game::BuildableDef> defs;
    const auto& defaults = game::DefaultBuildables();
    defs.assign(defaults.begin(), defaults.end());
    CHECK(game::AppendBuildableDefs(defs, result.buildables) == 1);
    CHECK(game::AppendBuildableDefs(defs, result.buildables) == 0);
    const game::BuildableDef* storage =
        game::FindBuildableBySprite(defs, "object.crate");
    REQUIRE(storage != nullptr);
    CHECK(storage->id == "storage");

    std::vector<game::BuildableDef> extra;
    extra.push_back(game::BuildableDef{
        .id = "bad_sprite",
        .name = "冲突",
        .sprite = "object.barricade",
        .woodCost = 1,
        .maxHp = 0.0f,
        .blocksMovement = false,
    });
    CHECK(game::AppendBuildableDefs(defs, extra) == 0);
}

TEST_CASE("X-F2b-2b mod 搜刮表与 POI 文件可收集并合并") {
    TempRoot root("loot_poi");
    const std::filesystem::path baseItems = root.path / "base_items.json";
    const std::filesystem::path baseRecipes = root.path / "base_recipes.json";
    const std::filesystem::path baseLoot = root.path / "base_loot.json";
    WriteText(baseItems, kBaseItems);
    WriteText(baseRecipes, kBaseRecipes);
    WriteText(baseLoot, R"({
      "format": "loot-v1",
      "tables": [
        { "id": "base_table", "name": "Base", "rolls": 0 }
      ]
    })");

    MakeMod(root.path / "mods", "content_mod", 20, R"({
      "format": "items-v1",
      "items": [
        { "id": "mod_item", "name": "模组物资", "icon": "item.rag", "weight": 0.2, "maxStack": 3, "type": "generic" }
      ]
    })", "", "", R"({
      "format": "loot-v1",
      "tables": [
        {
          "id": "mod_table",
          "name": "模组补给",
          "rolls": 1,
          "guaranteed": [ { "defId": "mod_item", "count": 1 } ],
          "entries": [ { "defId": "mod_item", "weight": 1, "min": 1, "max": 2 } ]
        }
      ]
    })", R"({
      "format": "pois-v1",
      "defs": [
        {
          "id": "mod_poi",
          "name": "模组补给站",
          "threat": 1,
          "markerX": 54.5,
          "markerY": 28.5,
          "containers": [
            { "x": 54.5, "y": 28.5, "tableId": "mod_table", "sprite": "object.crate" }
          ],
          "enemies": { "count": 1, "kind": "normal", "radius": 4.0 },
          "task": {
            "description": "搜索补给",
            "targetTable": "mod_table",
            "rewardXp": 20.0,
            "rewardItem": "mod_item",
            "rewardCount": 1
          }
        }
      ]
    })");

    core::Config config;
    config.Set("items.file", baseItems.generic_string());
    config.Set("recipes.file", baseRecipes.generic_string());
    config.Set("script.modsDir", (root.path / "mods").generic_string());

    items::ItemRegistry items;
    items::RecipeRegistry recipes;
    items::LootTableRegistry loot;
    REQUIRE(items.LoadFile(baseItems));
    REQUIRE(recipes.LoadFile(baseRecipes, items));
    REQUIRE(loot.LoadFile(baseLoot, items));

    const game::ModContentResult result =
        game::LoadModContent(config, items, recipes);
    CHECK(result.applied == 1);
    CHECK(result.skipped == 0);
    REQUIRE(result.lootFiles.size() == 1);
    REQUIRE(result.poiFiles.size() == 1);
    REQUIRE(items.Find("mod_item") != nullptr);

    std::string error;
    REQUIRE(loot.MergeFile(result.lootFiles[0], items, &error));
    CHECK(error.empty());
    REQUIRE(loot.Find("mod_table") != nullptr);
    CHECK(loot.Size() == 2u);

    game::PoiConfig pois;
    pois.enabled = true;
    pois.guideRange = 77.0f;
    game::PoiMergeStats stats;
    REQUIRE(game::MergePoiFile(result.poiFiles[0], &pois, &stats, &error));
    CHECK(error.empty());
    CHECK(stats.filesLoaded == 1);
    CHECK(stats.defsAdded == 1);
    CHECK(stats.defsSkipped == 0);
    REQUIRE(pois.Find("mod_poi") != nullptr);
    CHECK(pois.guideRange == doctest::Approx(77.0f));
    CHECK(pois.Find("mod_poi")->task.rewardItem == "mod_item");
}

TEST_CASE("X-F2b-2b 非法 loot/POI 格式跳过 mod") {
    TempRoot root("bad_loot_poi");
    const std::filesystem::path baseItems = root.path / "base_items.json";
    const std::filesystem::path baseRecipes = root.path / "base_recipes.json";
    WriteText(baseItems, kBaseItems);
    WriteText(baseRecipes, kBaseRecipes);
    MakeMod(root.path / "mods", "bad_loot", 20, R"({
      "format": "items-v1",
      "items": [
        { "id": "bad_loot_item", "name": "Bad", "icon": "item.rag", "weight": 0.1, "maxStack": 1, "type": "generic" }
      ]
    })", "", "", R"({ "format": "loot-v0", "tables": [] })");
    MakeMod(root.path / "mods", "bad_poi", 30, R"({
      "format": "items-v1",
      "items": [
        { "id": "bad_poi_item", "name": "Bad", "icon": "item.rag", "weight": 0.1, "maxStack": 1, "type": "generic" }
      ]
    })", "", "", "", R"({ "format": "pois-v0", "defs": [] })");
    MakeMod(root.path / "mods", "bad_mission", 40, R"({
      "format": "items-v1",
      "items": [
        { "id": "bad_mission_item", "name": "Bad", "icon": "item.rag", "weight": 0.1, "maxStack": 1, "type": "generic" }
      ]
    })", "", "", "", "", R"({ "format": "missions-v0", "defs": [] })");

    core::Config config;
    config.Set("items.file", baseItems.generic_string());
    config.Set("recipes.file", baseRecipes.generic_string());
    config.Set("script.modsDir", (root.path / "mods").generic_string());

    items::ItemRegistry items;
    items::RecipeRegistry recipes;
    REQUIRE(items.LoadFile(baseItems));
    REQUIRE(recipes.LoadFile(baseRecipes, items));
    const game::ModContentResult result =
        game::LoadModContent(config, items, recipes);
    CHECK(result.found == 3);
    CHECK(result.applied == 0);
    CHECK(result.skipped == 3);
    CHECK(items.Find("bad_loot_item") == nullptr);
    CHECK(items.Find("bad_poi_item") == nullptr);
    CHECK(items.Find("bad_mission_item") == nullptr);
    CHECK(result.lootFiles.empty());
    CHECK(result.poiFiles.empty());
    CHECK(result.missionFiles.empty());
}

TEST_CASE("X-F2：示例 mod 内容文件可被合并") {
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

    const game::ModContentResult result =
        game::LoadModContent(config, items, recipes);
    CHECK(result.applied >= 1);
    REQUIRE(items.Find("camp_stew") != nullptr);
    CHECK(items.Find("camp_stew")->icon == "mod.camp_stew");
    CHECK(items.Find("camp_stew")->use.nutrition > 0.0f);
    REQUIRE(items.Find("pumpkin_soup") != nullptr);
    CHECK(items.Find("pumpkin_soup")->use.nutrition > 0.0f);
    REQUIRE(recipes.Find("cook_camp_stew") != nullptr);
    const items::RecipeDef* pumpkinSoup =
        recipes.Find("cook_pumpkin_soup");
    REQUIRE(pumpkinSoup != nullptr);
    CHECK(pumpkinSoup->category == items::RecipeCategory::Cooking);
    REQUIRE(pumpkinSoup->cookware.has_value());
    CHECK(pumpkinSoup->cookware->defId == "cooking_pot");
    REQUIRE(pumpkinSoup->fuel.has_value());
    CHECK(pumpkinSoup->fuel->defId == "wood");
    REQUIRE(!result.assetManifests.empty());
    CHECK(std::filesystem::exists(result.assetManifests.front()));
    REQUIRE(result.lootFiles.size() == 1u);
    REQUIRE(result.poiFiles.size() == 1u);
    REQUIRE(result.missionFiles.size() == 1u);
    REQUIRE(result.cropFiles.size() == 1u);
    game::CropRegistry crops;
    std::string cropError;
    REQUIRE_MESSAGE(crops.LoadFile(root / "assets" / "crops.json", items,
                                   &cropError),
                    cropError);
    REQUIRE_MESSAGE(crops.MergeFile(result.cropFiles.front(), items,
                                    &cropError),
                    cropError);
    REQUIRE(crops.Find("mod_pumpkin") != nullptr);
    CHECK(crops.Find("mod_pumpkin")->seedItem == "pumpkin_seeds");
    CHECK(crops.Find("mod_pumpkin")->produceItem == "fresh_pumpkin");
    bool foundCrate = false;
    for (const game::BuildableDef& def : result.buildables) {
        if (def.id == "mod_supply_crate") {
            foundCrate = true;
            CHECK(def.sprite == "mod.supply_crate");
            CHECK(def.storageKg == doctest::Approx(35.0f));
            CHECK(def.storageSlots == 4);
        }
    }
    CHECK(foundCrate);

    items::LootTableRegistry loot;
    std::string error;
    REQUIRE(loot.LoadFile(root / "assets" / "loot.json", items, &error));
    REQUIRE(loot.MergeFile(result.lootFiles.front(), items, &error));
    REQUIRE(loot.Find("mod_camp_cache") != nullptr);
    game::PoiConfig modPois;
    modPois.enabled = true;
    game::PoiMergeStats poiStats;
    REQUIRE(game::MergePoiFile(result.poiFiles.front(), &modPois, &poiStats,
                               &error));
    REQUIRE(modPois.Find("mod_depot") != nullptr);
    CHECK(modPois.Find("mod_depot")->containers.front().tableId ==
          "mod_camp_cache");
    CHECK(poiStats.defsAdded == 1);

    core::Config gameConfig;
    REQUIRE(gameConfig.LoadFile(root / "config" / "game.json"));
    game::MissionConfig baseMissions;
    REQUIRE(game::LoadMissionConfig(gameConfig, &baseMissions, &error));
    REQUIRE(baseMissions.defs.size() == 35u);
    game::MissionMergeStats missionStats;
    REQUIRE(game::MergeMissionFiles(result.missionFiles, gameConfig,
                                    &baseMissions, &missionStats, &error));
    CHECK(error.empty());
    CHECK(missionStats.filesLoaded == 1);
    CHECK(missionStats.defsAdded == 1);
    REQUIRE(baseMissions.Find("mod_depot_supply") != nullptr);
    CHECK(baseMissions.Find("mod_depot_supply")->objectives.front().target ==
          "mod_depot");
    CHECK(baseMissions.defs.size() == 36u);
    std::vector<game::BuildableDef> defs = game::LoadBuildableDefs(gameConfig);
    CHECK(defs.size() == 15);
    CHECK(game::AppendBuildableDefs(defs, result.buildables) >= 1);
    CHECK(defs.size() >= 16);
    bool foundInCatalog = false;
    for (const game::BuildableDef& def : defs) {
        if (def.id == "mod_supply_crate") {
            foundInCatalog = true;
        }
    }
    CHECK(foundInCatalog);
}

TEST_CASE("M23-F2a-T1 mod forage.json 进入内容结果") {
    TempRoot root("forage");
    const std::filesystem::path baseItems = root.path / "base_items.json";
    const std::filesystem::path baseRecipes = root.path / "base_recipes.json";
    WriteText(baseItems, kBaseItems);
    WriteText(baseRecipes, kBaseRecipes);

    MakeMod(root.path / "mods", "forage_mod", 20, "", "", "", "", "", "",
            R"({
              "format": "forage-v1",
              "forageZones": [
                { "id": "mod_forest", "name": "Mod 森林", "tableId": "forage_forest",
                  "floors": ["tile.floor.grass"] }
              ],
              "fishingSpots": []
            })");

    core::Config config;
    config.Set("items.file", baseItems.generic_string());
    config.Set("recipes.file", baseRecipes.generic_string());
    config.Set("script.modsDir", (root.path / "mods").generic_string());

    items::ItemRegistry items;
    items::RecipeRegistry recipes;
    REQUIRE(items.LoadFile(baseItems));
    REQUIRE(recipes.LoadFile(baseRecipes, items));

    const game::ModContentResult result =
        game::LoadModContent(config, items, recipes);
    CHECK(result.found == 1);
    CHECK(result.applied == 1);
    CHECK(result.forageFiles.size() == 1u);
    CHECK(result.forageFiles.front().find("forage.json") != std::string::npos);
}

TEST_CASE("M23-F4-T1 mod settlements.json 进入内容结果并可追加") {
    TempRoot root("settlement");
    const std::filesystem::path baseItems = root.path / "base_items.json";
    const std::filesystem::path baseRecipes = root.path / "base_recipes.json";
    WriteText(baseItems, kBaseItems);
    WriteText(baseRecipes, kBaseRecipes);
    MakeMod(root.path / "mods", "settlement_mod", 20, "", "", "", "", "", "",
            "", R"({
      "format": "settlements-v1",
      "defs": [
        {
          "id": "mod_haven",
          "name": "模组聚落",
          "centerX": 70.5,
          "centerY": 24.5,
          "safeRadius": 10.5,
          "npcPoints": [
            { "id": "mod_merchant", "kind": "merchant", "x": 71.5, "y": 23.5, "wanderRadius": 1.0 },
            { "id": "mod_guard", "kind": "guard", "x": 73.5, "y": 25.5, "wanderRadius": 2.0 },
            { "id": "mod_requester", "kind": "requester", "x": 68.5, "y": 26.5, "wanderRadius": 1.0 }
          ],
          "merchantStock": [1, 2, 3, 4, 5],
          "restockDays": 2,
          "missionPool": ["event_supply_drop"]
        }
      ]
    })");

    core::Config config;
    config.Set("items.file", baseItems.generic_string());
    config.Set("recipes.file", baseRecipes.generic_string());
    config.Set("script.modsDir", (root.path / "mods").generic_string());
    items::ItemRegistry items;
    items::RecipeRegistry recipes;
    REQUIRE(items.LoadFile(baseItems));
    REQUIRE(recipes.LoadFile(baseRecipes, items));
    const game::ModContentResult result =
        game::LoadModContent(config, items, recipes);
    REQUIRE(result.settlementFiles.size() == 1u);

    game::SettlementConfig settlements;
    settlements.enabled = true;
    std::string error;
    REQUIRE(game::MergeSettlementFile(result.settlementFiles.front(),
                                      &settlements, nullptr, &error));
    CHECK(settlements.Find("mod_haven") != nullptr);
    CHECK(settlements.defs[0].restockDays == 2);
}

TEST_CASE("M33-F2-T1 mod world_events.json enters content result") {
    TempRoot root("world_events");
    const std::filesystem::path baseItems = root.path / "base_items.json";
    const std::filesystem::path baseRecipes = root.path / "base_recipes.json";
    WriteText(baseItems, kBaseItems);
    WriteText(baseRecipes, kBaseRecipes);
    MakeMod(root.path / "mods", "world_event_mod", 20, "", "", "", "", "", "",
            "", "", R"({
      "format": "world-events-v1",
      "defs": [{
        "id": "mod_event",
        "name": "妯＄粍浜嬩欢",
        "stages": [{ "kind": "reach", "radius": 5 }],
        "choices": [{ "id": "leave", "label": "绂诲紑" }]
      }]
    })");

    core::Config config;
    config.Set("items.file", baseItems.generic_string());
    config.Set("recipes.file", baseRecipes.generic_string());
    config.Set("script.modsDir", (root.path / "mods").generic_string());
    items::ItemRegistry items;
    items::RecipeRegistry recipes;
    REQUIRE(items.LoadFile(baseItems));
    REQUIRE(recipes.LoadFile(baseRecipes, items));
    const game::ModContentResult result =
        game::LoadModContent(config, items, recipes);
    REQUIRE(result.worldEventFiles.size() == 1u);

    game::WorldEventConfig events;
    events.enabled = true;
    std::string error;
    REQUIRE(game::MergeWorldEventFile(result.worldEventFiles.front(), &events,
                                      nullptr, &error, &items));
    CHECK(events.Find("mod_event") != nullptr);
}

TEST_CASE("X3-T1：ModContentResult 暴露所有有效 mod 根目录供监听") {
    TempRoot root("watch_sources");
    const std::filesystem::path baseItems = root.path / "base_items.json";
    const std::filesystem::path baseRecipes = root.path / "base_recipes.json";
    WriteText(baseItems, kBaseItems);
    WriteText(baseRecipes, kBaseRecipes);

    MakeMod(root.path / "mods", "content_mod", 20, R"({
      "format": "items-v1",
      "items": [
        { "id": "watch_item", "name": "监听物品", "icon": "item.rag", "weight": 0.2, "maxStack": 3, "type": "generic" }
      ]
    })", "", "");
    MakeMod(root.path / "mods", "empty_mod", 30, "", "", "");
    MakeMod(root.path / "mods", "broken", 40, "{ this is not json", "", "");

    core::Config config;
    config.Set("items.file", baseItems.generic_string());
    config.Set("recipes.file", baseRecipes.generic_string());
    config.Set("script.modsDir", (root.path / "mods").generic_string());
    items::ItemRegistry items;
    items::RecipeRegistry recipes;
    REQUIRE(items.LoadFile(baseItems));
    REQUIRE(recipes.LoadFile(baseRecipes, items));

    const game::ModContentResult result =
        game::LoadModContent(config, items, recipes);
    REQUIRE(result.sources.size() == 3u);
    CHECK(result.sources[0].id == "content_mod");
    CHECK(result.sources[1].id == "empty_mod");
    CHECK(result.sources[2].id == "broken");
    CHECK(std::filesystem::is_directory(result.sources[0].root));
    CHECK(std::filesystem::is_directory(result.sources[1].root));
    CHECK(std::filesystem::is_directory(result.sources[2].root));
}
