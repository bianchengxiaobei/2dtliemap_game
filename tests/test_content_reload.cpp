#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "core/Config.h"
#include "ecs/EcsWorld.h"
#include "game/ContentHash.h"
#include "game/ContentReload.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"

using namespace zl;

namespace {

struct TempRoot {
    std::filesystem::path path;

    explicit TempRoot(const std::string& name)
        : path(std::filesystem::temp_directory_path() / "zl_content_reload" /
               name) {
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
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

void Configure(core::Config& config, const TempRoot& root) {
    const std::filesystem::path source{ZL_SOURCE_DIR};
    config.Set("items.file", (source / "assets" / "items.json").generic_string());
    config.Set("recipes.file",
               (source / "assets" / "recipes.json").generic_string());
    config.Set("loot.file", (source / "assets" / "loot.json").generic_string());
    config.Set("forage.file",
               (source / "assets" / "forage.json").generic_string());
    config.Set("animals.file",
               (source / "assets" / "animals.json").generic_string());
    config.Set("crops.file", (source / "assets" / "crops.json").generic_string());
    config.Set("settlements.file",
               (source / "assets" / "settlements.json").generic_string());
    config.Set("companions.file",
               (source / "assets" / "companions.json").generic_string());
    config.Set("vehicleMods.file",
               (source / "assets" / "vehicle_mods.json").generic_string());
    config.Set("script.modsDir", (root.path / "mods").generic_string());
}

std::string ItemsJson(const std::string& name, bool withExtra) {
    std::string items = R"({"format":"items-v1","items":[)";
    items += R"({"id":"hot_item","name":")" + name +
             R"(","icon":"item.rag","weight":0.1,"maxStack":2,"type":"generic"})";
    if (withExtra) {
        items += R"(,{"id":"hot_extra","name":"Hot Extra","icon":"item.rag","weight":0.1,"maxStack":2,"type":"generic"})";
    }
    items += "]}";
    return items;
}

std::string RecipesJson() {
    return R"({"format":"recipes-v3","recipes":[{"id":"hot_recipe","name":"Hot Recipe","output":{"item":"hot_item","count":1},"ingredients":[{"item":"rag","count":1}],"time":0.1}]})";
}

std::string BuildingsJson(const std::string& name) {
    return R"({"format":"buildings-v1","types":[{"id":"hot_build","name":")" +
           name +
           R"(","sprite":"object.hot_build","woodCost":1,"maxHp":0,"blocksMovement":false}]})";
}

std::string LootJson() {
    return R"({"format":"loot-v1","tables":[{"id":"hot_table","name":"Hot Table","capacityKg":12,"rolls":1,"entries":[{"defId":"hot_item","weight":1,"min":1,"max":1}]}]})";
}

std::string PoiJson(const std::string& name) {
    return R"({"format":"pois-v1","defs":[{"id":"hot_poi","name":")" + name +
           R"(","markerX":10,"markerY":10,"containers":[{"x":10.5,"y":10.5,"tableId":"hot_table","sprite":"object.crate"}],"task":{"description":"搜索","targetTable":"hot_table","rewardXp":1}}]})";
}

std::string MissionJson(const std::string& name) {
    return R"({"format":"missions-v1","defs":[{"id":"hot_mission","name":")" +
           name +
           R"(","kind":"once","source":"radio","objectives":[{"kind":"collect","target":"hot_item","count":1,"description":"鏀堕泦"}],"reward":{"label":"Hot"}}]})";
}

std::string WorldEventJson(const std::string& name,
                           const std::string& nextEvent = {}) {
    std::string next;
    if (!nextEvent.empty()) {
        next = R"(,"nextEvent":")" + nextEvent + R"(")";
    }
    return std::string(
               R"({"format":"world-events-v1","defs":[{"id":"hot_event","name":")") +
           name +
           R"(","stages":[{"kind":"reach","radius":6}],"choices":[{"id":"leave","label":"离开"}])" +
           next + "}]}";
}

void WriteMod(const TempRoot& root, const std::string& itemName,
              const std::string& buildingName, const std::string& poiName,
              const std::string& missionName, bool withExtra = false) {
    const std::filesystem::path dir = root.path / "mods" / "hot";
    WriteText(dir / "mod.json", R"({"id":"hot","loadOrder":10})");
    WriteText(dir / "content" / "items.json", ItemsJson(itemName, withExtra));
    WriteText(dir / "content" / "recipes.json", RecipesJson());
    WriteText(dir / "content" / "buildings.json", BuildingsJson(buildingName));
    WriteText(dir / "content" / "loot.json", LootJson());
    WriteText(dir / "content" / "pois.json", PoiJson(poiName));
    WriteText(dir / "content" / "missions.json", MissionJson(missionName));
}

const game::BuildableDef* FindModBuildable(const game::ContentBundle& content,
                                           const std::string& id) {
    for (const game::BuildableDef& def : content.modContent.buildables) {
        if (def.id == id) {
            return &def;
        }
    }
    return nullptr;
}

std::uint64_t BundleHash(const game::ContentBundle& content,
                         const std::string& modsDir) {
    return game::ComputeContentHash(
        content.items, content.recipes, content.modContent.buildables,
        content.lootTables, content.poiConfig, content.missionConfig,
        game::ComputeTreeHash(modsDir), &content.forage, &content.animals,
        &content.crops, &content.settlements, &content.worldEvents,
        &content.companions, &content.vehicleMods);
}

}  // namespace

TEST_CASE("X3-T2：候选事务一次性加载物品/配方/建造/战利品/POI/任务") {
    TempRoot root("valid");
    WriteMod(root, "Hot V1", "Hot Build V1", "Hot POI V1", "Hot Mission V1");
    core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    Configure(config, root);

    game::ContentBundleResult loaded = game::LoadContentBundle(config);
    REQUIRE_MESSAGE(loaded.Ok(), loaded.error);
    const game::ContentBundle& content = *loaded.bundle;
    CHECK(content.modContent.applied == 1);
    CHECK(content.modContent.skipped == 0);
    REQUIRE(content.items.Find("hot_item") != nullptr);
    CHECK(content.items.Find("hot_item")->name == "Hot V1");
    REQUIRE(content.recipes.Find("hot_recipe") != nullptr);
    CHECK(content.recipes.Find("hot_recipe")->outputDefId == "hot_item");
    CHECK(FindModBuildable(content, "hot_build") != nullptr);
    CHECK(FindModBuildable(content, "hot_build")->name == "Hot Build V1");
    REQUIRE(content.lootTables.Find("hot_table") != nullptr);
    REQUIRE(content.poiConfig.Find("hot_poi") != nullptr);
    CHECK(content.poiConfig.Find("hot_poi")->name == "Hot POI V1");
    REQUIRE(content.missionConfig.Find("hot_mission") != nullptr);
    CHECK(content.missionConfig.Find("hot_mission")->name == "Hot Mission V1");
    CHECK_FALSE(content.missionSourceJson.empty());
    CHECK(content.missionSourceJson.find("hot_mission") != std::string::npos);
    CHECK(content.modContent.lootFiles.size() == 1u);
    CHECK(content.modContent.poiFiles.size() == 1u);
    CHECK(content.modContent.missionFiles.size() == 1u);
    CHECK(content.companions.enabled);
    CHECK(content.companions.Find("companion_medic_lin") != nullptr);
}

TEST_CASE("X3-T2：成功事务替换旧内容并移除已删除定义") {
    TempRoot root("replace");
    WriteMod(root, "Hot V1", "Hot Build V1", "Hot POI V1", "Hot Mission V1",
             true);
    core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    Configure(config, root);

    game::ContentBundleResult first = game::LoadContentBundle(config);
    REQUIRE_MESSAGE(first.Ok(), first.error);
    REQUIRE(first.bundle->items.Find("hot_extra") != nullptr);

    WriteMod(root, "Hot V2", "Hot Build V2", "Hot POI V2", "Hot Mission V2",
             false);
    game::ContentBundleResult second = game::LoadContentBundle(config);
    REQUIRE_MESSAGE(second.Ok(), second.error);
    REQUIRE(second.bundle->items.Find("hot_item") != nullptr);
    CHECK(second.bundle->items.Find("hot_item")->name == "Hot V2");
    CHECK(second.bundle->items.Find("hot_extra") == nullptr);
    CHECK(second.bundle->poiConfig.Find("hot_poi")->name == "Hot POI V2");
    CHECK(second.bundle->missionConfig.Find("hot_mission")->name ==
          "Hot Mission V2");
    REQUIRE(FindModBuildable(*second.bundle, "hot_build") != nullptr);
    CHECK(FindModBuildable(*second.bundle, "hot_build")->name == "Hot Build V2");

    CHECK(first.bundle->items.Find("hot_item")->name == "Hot V1");
    CHECK(first.bundle->items.Find("hot_extra") != nullptr);
}

TEST_CASE("X3-T3：内容指纹随事务候选变化，联机与动态内容策略明确") {
    TempRoot root("policy");
    WriteMod(root, "Hot V1", "Hot Build V1", "Hot POI V1", "Hot Mission V1");
    core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    Configure(config, root);
    const std::string modsDir = (root.path / "mods").generic_string();

    game::ContentBundleResult first = game::LoadContentBundle(config);
    REQUIRE_MESSAGE(first.Ok(), first.error);
    const std::uint64_t firstHash = BundleHash(*first.bundle, modsDir);

    WriteMod(root, "Hot V2", "Hot Build V2", "Hot POI V2", "Hot Mission V2");
    game::ContentBundleResult second = game::LoadContentBundle(config);
    REQUIRE_MESSAGE(second.Ok(), second.error);
    CHECK(firstHash != BundleHash(*second.bundle, modsDir));

    CHECK(game::ContentReloadBlockedReason(true, false, 0).empty());
    CHECK(game::ContentReloadBlockedReason(true, true, 0).find("重启") !=
          std::string::npos);
    CHECK(game::ContentReloadBlockedReason(false, false, 0).find("重连") !=
          std::string::npos);
    CHECK(game::ContentReloadBlockedReason(true, false, 1).find("重启") !=
          std::string::npos);
}

TEST_CASE("X3-T4：动态内容仅单机策略与提示文案统一") {
    CHECK(game::DynamicContentBlockedReason(true).empty());
    CHECK(game::DynamicContentBlockedReason(false) ==
          game::kDynamicContentSinglePlayerOnlyMessage);
    CHECK(game::ContentReloadBlockedReason(true, true, 0) ==
          game::kContentReloadNetworkBlockedMessage);
    CHECK(game::ContentReloadBlockedReason(true, false, 1) ==
          game::kContentReloadDynamicBlockedMessage);
}

TEST_CASE("X3-T2：物品/配方/建造/战利品/POI/任务任一失败均保留旧内容") {
    TempRoot root("rollback");
    WriteMod(root, "Hot V1", "Hot Build V1", "Hot POI V1", "Hot Mission V1");
    core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    Configure(config, root);
    game::ContentBundleResult old = game::LoadContentBundle(config);
    REQUIRE_MESSAGE(old.Ok(), old.error);

    const std::filesystem::path content = root.path / "mods" / "hot" / "content";
    const auto checkOld = [&]() {
        REQUIRE(old.bundle->items.Find("hot_item") != nullptr);
        CHECK(old.bundle->items.Find("hot_item")->name == "Hot V1");
        CHECK(old.bundle->recipes.Find("hot_recipe") != nullptr);
        CHECK(old.bundle->lootTables.Find("hot_table") != nullptr);
        CHECK(old.bundle->poiConfig.Find("hot_poi") != nullptr);
        CHECK(old.bundle->missionConfig.Find("hot_mission") != nullptr);
        CHECK(FindModBuildable(*old.bundle, "hot_build") != nullptr);
    };

    WriteText(content / "items.json", "{ invalid json");
    game::ContentBundleResult badItems = game::LoadContentBundle(config);
    CHECK_FALSE(badItems.Ok());
    CHECK_FALSE(badItems.error.empty());
    checkOld();

    WriteMod(root, "Hot V2", "Hot Build V2", "Hot POI V2", "Hot Mission V2");
    WriteText(content / "recipes.json",
              R"({"format":"recipes-v3","recipes":[{"id":"hot_recipe","name":"Hot","output":{"item":"missing_item","count":1},"ingredients":[{"item":"rag","count":1}],"time":0.1}]})");
    game::ContentBundleResult badRecipes = game::LoadContentBundle(config);
    CHECK_FALSE(badRecipes.Ok());
    checkOld();

    WriteMod(root, "Hot V2", "Hot Build V2", "Hot POI V2", "Hot Mission V2");
    WriteText(content / "buildings.json",
              R"({"format":"buildings-v1","types":[{"name":"缺少 id 与 sprite"}]})");
    game::ContentBundleResult badBuildings = game::LoadContentBundle(config);
    CHECK_FALSE(badBuildings.Ok());
    checkOld();

    WriteMod(root, "Hot V2", "Hot Build V2", "Hot POI V2", "Hot Mission V2");
    WriteText(content / "loot.json",
              R"({"format":"loot-v1","tables":[{"id":"hot_table","name":"Hot","capacityKg":12,"rolls":1,"entries":[{"defId":"missing_item","weight":1,"min":1,"max":1}]}]})");
    game::ContentBundleResult badLoot = game::LoadContentBundle(config);
    CHECK_FALSE(badLoot.Ok());
    checkOld();

    WriteMod(root, "Hot V2", "Hot Build V2", "Hot POI V2", "Hot Mission V2");
    WriteText(content / "pois.json",
              R"({"format":"pois-v1","defs":[{"id":"hot_poi","name":"A","markerX":1,"markerY":1,"containers":[{"x":1,"y":1,"tableId":"hot_table","sprite":"object.crate"}],"task":{"targetTable":"hot_table"}},{"id":"hot_poi","name":"B","markerX":2,"markerY":2,"containers":[{"x":2,"y":2,"tableId":"hot_table","sprite":"object.crate"}],"task":{"targetTable":"hot_table"}}]})");
    game::ContentBundleResult badPois = game::LoadContentBundle(config);
    CHECK_FALSE(badPois.Ok());
    checkOld();

    WriteMod(root, "Hot V2", "Hot Build V2", "Hot POI V2", "Hot Mission V2");
    WriteText(content / "missions.json",
              R"({"format":"missions-v1","defs":[{"id":"hot_mission","name":"Bad","kind":"once","source":"radio","objectives":[{"kind":"collect","target":"hot_item","count":1}],"reward":{"label":"Hot"}}]})");
    game::ContentBundleResult badMissions = game::LoadContentBundle(config);
    CHECK_FALSE(badMissions.Ok());
    checkOld();

    WriteMod(root, "Hot V2", "Hot Build V2", "Hot POI V2", "Hot Mission V2");
    game::ContentBundleResult fixed = game::LoadContentBundle(config);
    REQUIRE_MESSAGE(fixed.Ok(), fixed.error);
    CHECK(fixed.bundle->items.Find("hot_item")->name == "Hot V2");
}
TEST_CASE("X3-T5：forage/crops/settlements 失败保留旧候选，未变内容指纹稳定") {
    TempRoot root("t5_failure_matrix");
    WriteMod(root, "Hot V1", "Hot Build V1", "Hot POI V1", "Hot Mission V1");
    core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    Configure(config, root);
    const std::string modsDir = (root.path / "mods").generic_string();
    const std::filesystem::path content = root.path / "mods" / "hot" / "content";

    game::ContentBundleResult old = game::LoadContentBundle(config);
    REQUIRE_MESSAGE(old.Ok(), old.error);
    const std::uint64_t oldHash = BundleHash(*old.bundle, modsDir);

    const auto checkOld = [&] {
        REQUIRE(old.bundle->items.Find("hot_item") != nullptr);
        CHECK(old.bundle->items.Find("hot_item")->name == "Hot V1");
        CHECK(old.bundle->poiConfig.Find("hot_poi") != nullptr);
        CHECK(old.bundle->missionConfig.Find("hot_mission") != nullptr);
    };

    WriteText(content / "forage.json", R"({"format":"forage-v1","forageZones":[],"fishingSpots":[]})");
    game::ContentBundleResult badForage = game::LoadContentBundle(config);
    CHECK_FALSE(badForage.Ok());
    CHECK_FALSE(badForage.error.empty());
    CHECK(badForage.error.find("mod 内容校验失败") != std::string::npos);
    checkOld();
    std::filesystem::remove(content / "forage.json");
    CHECK(BundleHash(*old.bundle, modsDir) == oldHash);

    WriteText(content / "crops.json", R"({"format":"crops-v1","crops":[]})");
    game::ContentBundleResult badCrops = game::LoadContentBundle(config);
    CHECK_FALSE(badCrops.Ok());
    CHECK_FALSE(badCrops.error.empty());
    CHECK(badCrops.error.find("mod 内容校验失败") != std::string::npos);
    checkOld();
    std::filesystem::remove(content / "crops.json");
    CHECK(BundleHash(*old.bundle, modsDir) == oldHash);

    WriteText(content / "settlements.json", R"({"format":"settlements-v1","defs":[]})");
    game::ContentBundleResult badSettlements = game::LoadContentBundle(config);
    CHECK_FALSE(badSettlements.Ok());
    CHECK_FALSE(badSettlements.error.empty());
    CHECK(badSettlements.error.find("mod 内容校验失败") != std::string::npos);
    checkOld();
    std::filesystem::remove(content / "settlements.json");
    CHECK(BundleHash(*old.bundle, modsDir) == oldHash);

    game::ContentBundleResult same = game::LoadContentBundle(config);
    REQUIRE_MESSAGE(same.Ok(), same.error);
    CHECK(BundleHash(*same.bundle, modsDir) == oldHash);
    CHECK(same.bundle->items.Find("hot_item")->name == "Hot V1");
}

TEST_CASE("X3-T5：旧档中的已删除 mod 物品保留且未知重量安全忽略") {
    TempRoot root("t5_old_save");
    WriteMod(root, "Hot V1", "Hot Build V1", "Hot POI V1", "Hot Mission V1");
    core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    Configure(config, root);

    game::ContentBundleResult old = game::LoadContentBundle(config);
    REQUIRE_MESSAGE(old.Ok(), old.error);
    REQUIRE(old.bundle->items.Find("hot_item") != nullptr);
    REQUIRE(old.bundle->items.Find("rag") != nullptr);

    ecs::EcsWorld source;
    const ecs::Entity player = source.Create();
    ecs::Container bag;
    bag.capacityKg = 10.0f;
    bag.items.push_back(ecs::ItemInstance{"hot_item", 2, 0.1f});
    bag.items.push_back(ecs::ItemInstance{"rag", 1, 0.1f});
    source.Registry().emplace<ecs::Container>(player, std::move(bag));

    save::ComponentRegistry components;
    std::string error;
    REQUIRE(save::RegisterBuiltinComponents(components, &error));
    REQUIRE(error.empty());

    save::SavedEntity saved;
    REQUIRE(save::CaptureEntity(source, player, components, &saved, &error));
    REQUIRE(error.empty());
    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(save::EntitySnapshot{{saved}}, &error);
    REQUIRE_FALSE(bytes.empty());
    REQUIRE(error.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
    REQUIRE(error.empty());
    REQUIRE(decoded.entities.size() == 1u);

    std::filesystem::remove_all(root.path / "mods");
    game::ContentBundleResult current = game::LoadContentBundle(config);
    REQUIRE_MESSAGE(current.Ok(), current.error);
    CHECK(current.bundle->items.Find("hot_item") == nullptr);
    REQUIRE(current.bundle->items.Find("rag") != nullptr);
    const float ragWeight = current.bundle->items.Find("rag")->weight;

    ecs::EcsWorld target;
    const ecs::Entity loaded =
        target.CreateWithStableId(decoded.entities[0].stableId);
    REQUIRE(loaded != ecs::EcsWorld::kNull);
    REQUIRE(save::ApplyEntity(decoded.entities[0], target, loaded, components,
                              &error));
    CHECK(error.empty());
    const auto* loadedBag = target.Registry().try_get<ecs::Container>(loaded);
    REQUIRE(loadedBag != nullptr);
    REQUIRE(loadedBag->items.size() == 2u);
    CHECK(loadedBag->items[0].defId == "hot_item");
    CHECK(items::CountOf(*loadedBag, "hot_item") == 2);
    CHECK(items::TotalWeight(*loadedBag, current.bundle->items) ==
          doctest::Approx(ragWeight));
}

TEST_CASE("M33-F2-T1 content bundle loads world events and rejects bad mod file") {
    TempRoot root("world_events");
    const std::filesystem::path baseEvents =
        root.path / "base_world_events.json";
    WriteText(baseEvents, WorldEventJson("Base Event"));
    WriteMod(root, "Hot V1", "Hot Build V1", "Hot POI V1", "Hot Mission V1");

    const std::filesystem::path content = root.path / "mods" / "hot" / "content";
    WriteText(content / "world_events.json", R"({
      "format": "world-events-v1",
      "defs": [{
        "id": "mod_event",
        "name": "Mod Event",
        "stages": [{ "kind": "reach", "radius": 5 }],
        "choices": [{ "id": "leave", "label": "Leave" }]
      }]
    })");

    core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    Configure(config, root);
    config.Set("worldEvents", nlohmann::json{
                                  {"enabled", true},
                                  {"file", baseEvents.generic_string()},
                                  {"randomSeed", 42},
                              });
    const std::string modsDir = (root.path / "mods").generic_string();

    game::ContentBundleResult first = game::LoadContentBundle(config);
    REQUIRE_MESSAGE(first.Ok(), first.error);
    CHECK(first.bundle->worldEvents.enabled);
    CHECK(first.bundle->worldEvents.defs.size() == 2u);
    CHECK(first.bundle->worldEvents.Find("mod_event") != nullptr);
    const std::uint64_t firstHash = BundleHash(*first.bundle, modsDir);

    WriteText(content / "world_events.json", R"({
      "format": "world-events-v1",
      "defs": [{
        "id": "broken_event",
        "name": "Broken Event",
        "choices": [{ "id": "leave", "label": "Leave" }]
      }]
    })");
    game::ContentBundleResult bad = game::LoadContentBundle(config);
    CHECK_FALSE(bad.Ok());
    CHECK_FALSE(bad.error.empty());
    CHECK(first.bundle->worldEvents.defs.size() == 2u);

    std::filesystem::remove(content / "world_events.json");
    game::ContentBundleResult same = game::LoadContentBundle(config);
    REQUIRE_MESSAGE(same.Ok(), same.error);
    CHECK(same.bundle->worldEvents.defs.size() == 1u);
    CHECK(same.bundle->worldEvents.Find("hot_event") != nullptr);
    CHECK(BundleHash(*same.bundle, modsDir) != firstHash);
}
