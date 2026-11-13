#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "core/Config.h"
#include "game/Animals.h"
#include "game/BuildingRules.h"
#include "game/ContentHash.h"
#include "game/Crops.h"
#include "game/Forage.h"
#include "game/Missions.h"
#include "game/Poi.h"
#include "game/Settlement.h"
#include "game/WorldEvent.h"
#include "items/ItemRegistry.h"
#include "items/LootTable.h"
#include "items/RecipeRegistry.h"

using namespace zl;

namespace {

std::string ItemsJson(float woodWeight) {
    return std::string(R"({
  "format": "items-v1",
  "items": [
    { "id": "wood", "name": "木材", "icon": "item.wood", "weight": )") +
           std::to_string(woodWeight) +
           R"(, "maxStack": 10, "type": "generic" },
    { "id": "crate", "name": "木箱", "icon": "object.crate", "weight": 1.0, "maxStack": 1, "type": "generic" }
  ]
})";
}

std::string RangedItemsJson(float damage) {
    return std::string(R"({
  "format": "items-v1",
  "items": [
    { "id": "wood", "name": "木材", "icon": "item.wood", "weight": 0.5, "maxStack": 10, "type": "generic" },
    { "id": "crate", "name": "木箱", "icon": "object.crate", "weight": 1.0, "maxStack": 1, "type": "generic" },
    { "id": "ammo_9mm", "name": "9mm", "icon": "item.energy_bar", "weight": 0.01, "maxStack": 60, "type": "generic" },
    { "id": "pistol", "name": "手枪", "icon": "item.kitchen_knife", "weight": 1.0, "maxStack": 1, "type": "weapon", "ranged": { "damage": )") +
           std::to_string(damage) +
           R"(, "range": 18, "spreadDegrees": 3, "pellets": 1, "cooldownSeconds": 0.45, "magazine": 8, "reloadSeconds": 2.0, "ammoDefId": "ammo_9mm", "noiseRadius": 28, "wearPerShot": 0.01, "projectileSpeed": 20, "falloffStart": 8, "falloffEnd": 18, "falloffScale": 0.55 } }
  ]
})";
}

std::string HeavyItemsJson(float heavyDamage) {
    return std::string(R"({
  "format": "items-v1",
  "items": [
    { "id": "wood", "name": "木材", "icon": "item.wood", "weight": 0.5, "maxStack": 10, "type": "generic" },
    { "id": "crate", "name": "木箱", "icon": "object.crate", "weight": 1.0, "maxStack": 1, "type": "generic" },
    { "id": "axe", "name": "消防斧", "icon": "item.axe", "weight": 3.0, "maxStack": 1, "type": "weapon", "combat": { "damage": 65, "range": 1.9, "arc": 110, "cooldown": 0.7, "knockback": 4.0, "stagger": 0.45, "wear": 0.035, "stamina": 15, "windup": 0.10, "poise": 11, "blockReduction": 0.55, "blockStamina": 16, "heavy": { "damage": )") +
           std::to_string(heavyDamage) +
           R"(, "range": 2.0, "arc": 100, "cooldown": 0.95, "knockback": 5.0, "stagger": 0.60, "wear": 0.045, "stamina": 28, "windup": 0.38, "poise": 38 } } }
  ]
})";
}

std::string RecipesJson(float craftSeconds) {
    return std::string(R"({
  "format": "recipes-v3",
  "recipes": [
    {
      "id": "make_crate",
      "name": "木箱",
      "output": { "item": "crate", "count": 1 },
      "ingredients": [ { "item": "wood", "count": 4 } ],
      "time": )") +
           std::to_string(craftSeconds) + R"(
    }
  ]
})";
}

std::vector<game::BuildableDef> Buildables() {
    game::BuildableDef fence;
    fence.id = "fence";
    fence.name = "木栅栏";
    fence.sprite = "object.fence";
    fence.woodCost = 3;
    fence.maxHp = 100.0f;
    fence.blocksMovement = true;
    fence.layer = world::SquareLayer::Wall;
    fence.category = "defense";

    game::BuildableDef crate;
    crate.id = "crate";
    crate.name = "木箱";
    crate.sprite = "object.crate";
    crate.woodCost = 4;
    crate.storageKg = 50.0f;
    crate.storageSlots = 6;
    crate.layer = world::SquareLayer::Furniture;
    crate.category = "storage";

    return {fence, crate};
}

struct Content {
    items::ItemRegistry items;
    items::RecipeRegistry recipes;

    bool Load(float woodWeight, float craftSeconds) {
        std::string error;
        if (!items.LoadFromString(ItemsJson(woodWeight), "content-hash-items",
                                  &error)) {
            return false;
        }
        return recipes.LoadFromString(RecipesJson(craftSeconds),
                                      "content-hash-recipes", items, &error);
    }

    bool LoadRanged(float damage) {
        std::string error;
        if (!items.LoadFromString(RangedItemsJson(damage), "content-hash-ranged-items",
                                  &error)) {
            return false;
        }
        return recipes.LoadFromString(RecipesJson(1.0f),
                                      "content-hash-ranged-recipes", items,
                                      &error);
    }

    bool LoadHeavy(float heavyDamage) {
        std::string error;
        if (!items.LoadFromString(HeavyItemsJson(heavyDamage),
                                  "content-hash-heavy-items", &error)) {
            return false;
        }
        return recipes.LoadFromString(RecipesJson(1.0f),
                                      "content-hash-heavy-recipes", items,
                                      &error);
    }
};

const items::LootTableRegistry& EmptyLoot() {
    static const items::LootTableRegistry loot;
    return loot;
}

const game::PoiConfig& EmptyPois() {
    static const game::PoiConfig pois;
    return pois;
}

const game::MissionConfig& EmptyMissions() {
    static const game::MissionConfig missions;
    return missions;
}

std::uint64_t Hash(const Content& content,
                   const std::vector<game::BuildableDef>& buildables) {
    return game::ComputeContentHash(content.items, content.recipes, buildables,
                                    EmptyLoot(), EmptyPois(), EmptyMissions());
}

std::uint64_t Hash(const Content& content,
                   const std::vector<game::BuildableDef>& buildables,
                   const items::LootTableRegistry& loot,
                   const game::PoiConfig& pois,
                   const game::MissionConfig& missions) {
    return game::ComputeContentHash(content.items, content.recipes, buildables,
                                    loot, pois, missions);
}

}  // namespace

TEST_CASE("M33-F2-T3 内容指纹版本升级到 v17") {
    CHECK(zl::game::kContentHashVersion == 17u);
    Content content;
    REQUIRE(content.Load(0.5f, 1.0f));
    const std::vector<game::BuildableDef> buildables = Buildables();
    game::MissionConfig missions;
    missions.enabled = true;
    game::MissionDef def;
    def.id = "hash_reputation";
    def.name = "指纹声望";
    def.chain = "hash_reputation";
    def.kind = game::MissionKind::Once;
    def.source = game::MissionSource::Npc;
    def.task = game::MissionTaskKind::Help;
    def.minSettlementReputation = -100;
    missions.defs.push_back(def);
    const std::uint64_t without = Hash(content, buildables, EmptyLoot(),
                                       EmptyPois(), missions);
    missions.defs[0].minSettlementReputation = 20;
    const std::uint64_t with = Hash(content, buildables, EmptyLoot(),
                                    EmptyPois(), missions);
    CHECK(without != with);
}

TEST_CASE("M33-F2-T1 内容指纹纳入世界事件") {
    Content content;
    REQUIRE(content.Load(0.5f, 1.0f));
    const std::vector<game::BuildableDef> buildables = Buildables();
    game::WorldEventConfig events;
    events.enabled = true;
    events.randomSeed = 7;
    game::WorldEventDef def;
    def.id = "hash_event";
    def.name = "指纹事件";
    def.kind = "caravan";
    def.minDay = 2;
    def.maxDay = 30;
    def.weight = 1.0f;
    def.cooldownDays = 3;
    def.region = "village_core";
    def.stages.push_back(game::WorldEventStageDef{
        game::WorldEventStageKind::Kill, "zombie", 2, 6.0f, "清理"});
    def.choices.push_back(game::WorldEventChoiceDef{
        "rescue", "救援", 10, 40.0f, "ammo_9mm", 2});
    def.nextEvent = "hash_event_after";
    events.defs.push_back(def);
    game::WorldEventDef after;
    after.id = "hash_event_after";
    after.name = "指纹后续";
    after.stages.push_back(game::WorldEventStageDef{
        game::WorldEventStageKind::Choice, "", 1, 4.0f, ""});
    after.choices.push_back(
        game::WorldEventChoiceDef{"leave", "离开", 0, 0.0f, "", 1});
    events.defs.push_back(after);

    const auto hash = [&] {
        return game::ComputeContentHash(
            content.items, content.recipes, buildables, EmptyLoot(),
            EmptyPois(), EmptyMissions(), 0, nullptr, nullptr, nullptr,
            nullptr, &events);
    };
    const std::uint64_t base = hash();
    CHECK(base != game::ComputeContentHash(
                       content.items, content.recipes, buildables, EmptyLoot(),
                       EmptyPois(), EmptyMissions(), 0, nullptr, nullptr,
                       nullptr, nullptr, nullptr));

    events.defs[0].stages[0].count = 3;
    const std::uint64_t changedStage = hash();
    CHECK(changedStage != base);

    events.defs[0].stages[0].count = 2;
    events.defs[0].choices[0].rewardItemCount = 3;
    const std::uint64_t changedReward = hash();
    CHECK(changedReward != base);

    events.defs[0].choices[0].rewardItemCount = 2;
    events.defs[0].choices[0].pressure = -10;
    const std::uint64_t changedPressure = hash();
    CHECK(changedPressure != base);

    events.defs[0].choices[0].pressure = 0;
    events.defs[0].failurePressure = 8;
    const std::uint64_t changedFailure = hash();
    CHECK(changedFailure != base);

    events.defs[0].failurePressure = 5;
    events.defs[0].nextEvent = "hash_event_after";
    events.randomSeed = 8;
    CHECK(hash() != base);
}

TEST_CASE("X-F2b：内容指纹确定且非零") {
    Content content;
    REQUIRE(content.Load(0.5f, 1.0f));
    const std::vector<game::BuildableDef> buildables = Buildables();

    const std::uint64_t first =
        Hash(content, buildables);
    const std::uint64_t second =
        Hash(content, buildables);
    CHECK(first != 0);
    CHECK(first == second);
}

TEST_CASE("X-F2b：物品与配方字段变化会改变内容指纹") {
    Content baseline;
    REQUIRE(baseline.Load(0.5f, 1.0f));
    const std::vector<game::BuildableDef> buildables = Buildables();
    const std::uint64_t baselineHash =
        Hash(baseline, buildables);

    Content changedItems;
    REQUIRE(changedItems.Load(0.6f, 1.0f));
    CHECK(Hash(changedItems, buildables) != baselineHash);

    Content changedRecipes;
    REQUIRE(changedRecipes.Load(0.5f, 1.5f));
    CHECK(Hash(changedRecipes, buildables) != baselineHash);
}

TEST_CASE("M23-F1：ranged 字段变化会改变内容指纹") {
    Content baseline;
    REQUIRE(baseline.LoadRanged(18.0f));
    const std::vector<game::BuildableDef> buildables = Buildables();
    const std::uint64_t baselineHash = Hash(baseline, buildables);
    CHECK(baselineHash != 0);

    Content changed;
    REQUIRE(changed.LoadRanged(19.0f));
    CHECK(Hash(changed, buildables) != baselineHash);
}

TEST_CASE("M30-T1：heavy/体力/格挡字段变化会改变内容指纹") {
    Content baseline;
    REQUIRE(baseline.LoadHeavy(92.0f));
    const std::vector<game::BuildableDef> buildables = Buildables();
    const std::uint64_t baselineHash = Hash(baseline, buildables);
    CHECK(baselineHash != 0);

    Content changedHeavy;
    REQUIRE(changedHeavy.LoadHeavy(93.0f));
    CHECK(Hash(changedHeavy, buildables) != baselineHash);
}

TEST_CASE("X-F2b：建造字段与顺序变化会改变内容指纹") {
    Content content;
    REQUIRE(content.Load(0.5f, 1.0f));
    const std::uint64_t baselineHash =
        Hash(content, Buildables());

    std::vector<game::BuildableDef> changed = Buildables();
    changed[0].maxHp += 1.0f;
    CHECK(Hash(content, changed) !=
          baselineHash);

    std::vector<game::BuildableDef> reordered = Buildables();
    std::swap(reordered[0], reordered[1]);
    CHECK(Hash(content, reordered) !=
          baselineHash);
}

TEST_CASE("X-F2b：战利品/POI/任务变化会改变内容指纹") {
    Content content;
    REQUIRE(content.Load(0.5f, 1.0f));
    const std::vector<game::BuildableDef> buildables = Buildables();

    items::LootTableRegistry loot;
    std::string error;
    REQUIRE(loot.LoadFromString(
        R"({
  "format": "loot-v1",
  "seed": 7,
  "tables": [
    {
      "id": "hash_loot",
      "name": "指纹测试",
      "capacityKg": 10.0,
      "rolls": 1,
      "entries": [
        { "defId": "wood", "weight": 1, "min": 1, "max": 1 }
      ]
    }
  ]
})",
        "content-hash-loot", content.items, &error));

    game::PoiConfig pois;
    pois.enabled = true;
    game::PoiDef poi;
    poi.id = "hash_poi";
    poi.name = "指纹 POI";
    poi.threat = 1;
    poi.markerX = 10.0f;
    poi.markerY = 20.0f;
    poi.task.description = "测试";
    poi.task.targetTable = "hash_loot";
    pois.defs.push_back(poi);

    game::MissionConfig missions;
    missions.enabled = true;
    game::MissionDef mission;
    mission.id = "hash_mission";
    mission.name = "指纹任务";
    mission.chain = "hash";
    mission.objectives.push_back(game::MissionObjectiveDef{
        game::MissionObjectiveKind::Loot, "hash_loot", 1, 0.0f, 0.0f, 1.5f,
        "搜刮"});
    missions.defs.push_back(mission);

    const std::uint64_t baseline =
        Hash(content, buildables, loot, pois, missions);
    CHECK(baseline != 0);

    items::LootTableRegistry changedLoot;
    REQUIRE(changedLoot.LoadFromString(
        R"({
  "format": "loot-v1",
  "seed": 7,
  "tables": [
    {
      "id": "hash_loot",
      "name": "指纹测试",
      "capacityKg": 10.0,
      "rolls": 1,
      "entries": [
        { "defId": "wood", "weight": 2, "min": 1, "max": 1 }
      ]
    }
  ]
})",
        "content-hash-loot-changed", content.items, &error));
    CHECK(Hash(content, buildables, changedLoot, pois, missions) != baseline);

    game::PoiConfig changedPois = pois;
    changedPois.defs[0].threat = 2;
    CHECK(Hash(content, buildables, loot, changedPois, missions) != baseline);

    game::PoiConfig changedArmored = pois;
    changedArmored.defs[0].enemies.armoredCount = 1;
    CHECK(Hash(content, buildables, loot, changedArmored, missions) != baseline);

    game::MissionConfig changedMissions = missions;
    changedMissions.defs[0].objectives[0].count = 2;
    CHECK(Hash(content, buildables, loot, pois, changedMissions) != baseline);

    game::MissionConfig changedTask = missions;
    changedTask.defs[0].task = game::MissionTaskKind::Supply;
    CHECK(Hash(content, buildables, loot, pois, changedTask) != baseline);

    game::MissionConfig manualMission = missions;
    manualMission.defs[0].autoActivate = false;
    CHECK(Hash(content, buildables, loot, pois, manualMission) != baseline);

    game::MissionConfig extraRewardMission = missions;
    extraRewardMission.defs[0].reward.extraItem = "bandage";
    CHECK(Hash(content, buildables, loot, pois, extraRewardMission) !=
          baseline);

    game::MissionConfig timedMission = missions;
    timedMission.defs[0].timeLimitDays = 2;
    CHECK(Hash(content, buildables, loot, pois, timedMission) != baseline);

    game::MissionConfig retryMission = missions;
    retryMission.defs[0].retryDays = 3;
    CHECK(Hash(content, buildables, loot, pois, retryMission) != baseline);
}

TEST_CASE("X-F2b：mod 文件树变化会改变内容指纹") {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "zl_content_tree_test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "scripts", ec);
    REQUIRE_FALSE(ec);
    const auto writeFile = [](const std::filesystem::path& path,
                              const std::string& text) {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << text;
    };
    writeFile(root / "scripts" / "main.lua", "print('a')");
    writeFile(root / "scripts" / "helper.py", "print('b')");
    std::filesystem::create_directories(root / "scripts" / "__pycache__", ec);
    std::filesystem::create_directories(root / ".git", ec);
    std::filesystem::create_directories(root / ".pytest_cache", ec);
    REQUIRE_FALSE(ec);
    writeFile(root / "scripts" / "__pycache__" / "main.cpython-39.pyc",
              "bytecode");
    writeFile(root / ".git" / "index", "v1");
    writeFile(root / ".pytest_cache" / "state", "cache");
    writeFile(root / "scripts" / "main.pyc", "bytecode");
    writeFile(root / "Thumbs.db", "thumbnail");

    const std::uint64_t first = game::ComputeTreeHash(root.string());
    const std::uint64_t second = game::ComputeTreeHash(root.string());
    CHECK(first != 0);
    CHECK(first == second);
    CHECK(game::ComputeTreeHash((root / "missing").string()) == 0);

    writeFile(root / "scripts" / "__pycache__" / "main.cpython-311.pyc",
              "other bytecode");
    writeFile(root / ".git" / "index", "v2");
    CHECK(game::ComputeTreeHash(root.string()) == first);

    writeFile(root / "scripts" / "main.lua", "print('changed')");
    const std::uint64_t changed = game::ComputeTreeHash(root.string());
    CHECK(changed != first);

    Content content;
    REQUIRE(content.Load(0.5f, 1.0f));
    const std::vector<game::BuildableDef> buildables = Buildables();
    const std::uint64_t contentFirst = game::ComputeContentHash(
        content.items, content.recipes, buildables, EmptyLoot(), EmptyPois(),
        EmptyMissions(), first);
    const std::uint64_t contentChanged = game::ComputeContentHash(
        content.items, content.recipes, buildables, EmptyLoot(), EmptyPois(),
        EmptyMissions(), changed);
    CHECK(contentFirst != contentChanged);

    std::filesystem::remove_all(root, ec);
}

TEST_CASE("M23-F2a-T6 内容指纹纳入 forage 数据") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    items::ItemRegistry items;
    REQUIRE(items.LoadFile(root / "assets" / "items.json"));
    items::RecipeRegistry recipes;
    items::LootTableRegistry loot;
    REQUIRE(loot.LoadFile(root / "assets" / "loot.json", items));
    game::ForageRegistry forage;
    std::string error;
    REQUIRE_MESSAGE(forage.LoadFile(root / "assets" / "forage.json", loot,
                                    &error),
                    error);
    const std::uint64_t without = game::ComputeContentHash(
        items, recipes, {}, loot, EmptyPois(), EmptyMissions());
    const std::uint64_t with = game::ComputeContentHash(
        items, recipes, {}, loot, EmptyPois(), EmptyMissions(), 0, &forage);
    CHECK(without != with);
    CHECK(with == game::ComputeContentHash(items, recipes, {}, loot,
                                           EmptyPois(), EmptyMissions(), 0,
                                           &forage));
}

TEST_CASE("M23-F2b-T1 内容指纹纳入动物数据") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    items::ItemRegistry items;
    REQUIRE(items.LoadFile(root / "assets" / "items.json"));
    items::RecipeRegistry recipes;
    items::LootTableRegistry loot;
    REQUIRE(loot.LoadFile(root / "assets" / "loot.json", items));
    game::AnimalRegistry animals;
    std::string error;
    REQUIRE_MESSAGE(animals.LoadFile(root / "assets" / "animals.json", items,
                                     loot, &error),
                    error);
    const std::uint64_t without = game::ComputeContentHash(
        items, recipes, {}, loot, EmptyPois(), EmptyMissions());
    const std::uint64_t with = game::ComputeContentHash(
        items, recipes, {}, loot, EmptyPois(), EmptyMissions(), 0, nullptr,
        &animals);
    CHECK(without != with);
    CHECK(with == game::ComputeContentHash(items, recipes, {}, loot,
                                           EmptyPois(), EmptyMissions(), 0,
                                           nullptr, &animals));
}

TEST_CASE("F5-T1 内容指纹纳入作物数据") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    items::ItemRegistry items;
    REQUIRE(items.LoadFile(root / "assets" / "items.json"));
    items::RecipeRegistry recipes;
    items::LootTableRegistry loot;
    REQUIRE(loot.LoadFile(root / "assets" / "loot.json", items));
    game::CropRegistry crops;
    std::string error;
    REQUIRE_MESSAGE(crops.LoadFile(root / "assets" / "crops.json", items,
                                   &error),
                    error);
    const std::uint64_t without = game::ComputeContentHash(
        items, recipes, {}, loot, EmptyPois(), EmptyMissions());
    const std::uint64_t with = game::ComputeContentHash(
        items, recipes, {}, loot, EmptyPois(), EmptyMissions(), 0, nullptr,
        nullptr, &crops);
    CHECK(without != with);
    CHECK(with == game::ComputeContentHash(items, recipes, {}, loot,
                                           EmptyPois(), EmptyMissions(), 0,
                                           nullptr, nullptr, &crops));
}

TEST_CASE("M23-F4-T1 内容指纹纳入聚落数据") {
    Content content;
    REQUIRE(content.Load(0.5f, 1.0f));
    const std::vector<game::BuildableDef> buildables = Buildables();
    game::SettlementConfig settlements;
    settlements.enabled = true;
    game::SettlementDef def;
    def.id = "village_haven";
    def.name = "村庄营地";
    def.regionId = "village_core";
    def.services = zl::game::kAllSettlementServices;
    def.startingReputation = 5;
    def.dialogueProfile = "village_haven";
    def.centerX = 70.5f;
    def.centerY = 24.5f;
    def.safeRadius = 10.5f;
    def.npcPoints.push_back(game::SettlementNpcPoint{
        "merchant_mara", game::SettlementNpcKind::Merchant, 71.5f, 23.5f,
        1.5f});
    def.npcPoints.push_back(game::SettlementNpcPoint{
        "guard_cole", game::SettlementNpcKind::Guard, 73.5f, 25.5f, 2.0f});
    def.npcPoints.push_back(game::SettlementNpcPoint{
        "requester_ivy", game::SettlementNpcKind::Requester, 68.5f, 26.5f,
        1.5f});
    def.merchantStock = {{4, 2, 1, 2, 1}};
    def.restockDays = 1;
    def.missionPool = {"event_supply_drop"};
    def.defense.enabled = true;
    def.defense.guardHealth = 140.0f;
    def.defense.baseRaiders = 3;
    settlements.defs.push_back(def);

    const std::uint64_t without = game::ComputeContentHash(
        content.items, content.recipes, buildables, EmptyLoot(), EmptyPois(),
        EmptyMissions());
    const std::uint64_t with = game::ComputeContentHash(
        content.items, content.recipes, buildables, EmptyLoot(), EmptyPois(),
        EmptyMissions(), 0, nullptr, nullptr, nullptr, &settlements);
    CHECK(without != with);
    CHECK(with == game::ComputeContentHash(
                       content.items, content.recipes, buildables, EmptyLoot(),
                       EmptyPois(), EmptyMissions(), 0, nullptr, nullptr,
                       nullptr, &settlements));

    settlements.defs[0].merchantStock[4] = 0;
    const std::uint64_t changed = game::ComputeContentHash(
        content.items, content.recipes, buildables, EmptyLoot(), EmptyPois(),
        EmptyMissions(), 0, nullptr, nullptr, nullptr, &settlements);
    CHECK(changed != with);

    settlements.defs[0].defense.guardHealth += 1.0f;
    const std::uint64_t changedDefense = game::ComputeContentHash(
        content.items, content.recipes, buildables, EmptyLoot(), EmptyPois(),
        EmptyMissions(), 0, nullptr, nullptr, nullptr, &settlements);
    CHECK(changedDefense != changed);

    settlements.defs[0].defense.guardHealth -= 1.0f;
    settlements.defs[0].regionId = "south_farmland";
    const std::uint64_t changedRegion = game::ComputeContentHash(
        content.items, content.recipes, buildables, EmptyLoot(), EmptyPois(),
        EmptyMissions(), 0, nullptr, nullptr, nullptr, &settlements);
    CHECK(changedRegion != with);

    settlements.defs[0].regionId = "village_core";
    settlements.defs[0].services = zl::game::kDefaultSettlementServices;
    const std::uint64_t changedServices = game::ComputeContentHash(
        content.items, content.recipes, buildables, EmptyLoot(), EmptyPois(),
        EmptyMissions(), 0, nullptr, nullptr, nullptr, &settlements);
    CHECK(changedServices != with);

    settlements.defs[0].services = zl::game::kAllSettlementServices;
    settlements.defs[0].startingReputation = 6;
    const std::uint64_t changedReputation = game::ComputeContentHash(
        content.items, content.recipes, buildables, EmptyLoot(), EmptyPois(),
        EmptyMissions(), 0, nullptr, nullptr, nullptr, &settlements);
    CHECK(changedReputation != with);

    settlements.defs[0].startingReputation = 5;
    settlements.defs[0].dialogueProfile = "north_fields";
    const std::uint64_t changedDialogue = game::ComputeContentHash(
        content.items, content.recipes, buildables, EmptyLoot(), EmptyPois(),
        EmptyMissions(), 0, nullptr, nullptr, nullptr, &settlements);
    CHECK(changedDialogue != with);

    settlements.defs[0].dialogueProfile = "village_haven";
    settlements.reputation.sources.trade = 2;
    const std::uint64_t changedReputationConfig = game::ComputeContentHash(
        content.items, content.recipes, buildables, EmptyLoot(), EmptyPois(),
        EmptyMissions(), 0, nullptr, nullptr, nullptr, &settlements);
    CHECK(changedReputationConfig != with);
}

TEST_CASE("M33-F1-T6 第四聚落与新任务进入内容指纹") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    core::Config config;
    REQUIRE(config.LoadFile(root / "config" / "game.json"));
    config.Set("settlements.file", (root / "assets" / "settlements.json")
                                       .generic_string());
    game::SettlementConfig settlements;
    game::MissionConfig missions;
    std::string error;
    REQUIRE_MESSAGE(game::LoadSettlementConfig(config, &settlements, &error),
                    error);
    REQUIRE_MESSAGE(game::LoadMissionConfig(config, &missions, &error),
                    error);
    REQUIRE(settlements.defs.size() == 4u);
    const game::SettlementDef* river = settlements.Find("river_camp");
    REQUIRE(river != nullptr);
    REQUIRE(missions.Find("river_fish_supply") != nullptr);

    Content content;
    REQUIRE(content.Load(0.5f, 1.0f));
    const std::vector<game::BuildableDef> buildables = Buildables();
    const auto hash = [&] {
        return game::ComputeContentHash(
            content.items, content.recipes, buildables, EmptyLoot(),
            EmptyPois(), missions, 0, nullptr, nullptr, nullptr,
            &settlements);
    };
    const std::uint64_t base = hash();

    auto riverIt = std::find_if(
        settlements.defs.begin(), settlements.defs.end(),
        [](const game::SettlementDef& def) { return def.id == "river_camp"; });
    REQUIRE(riverIt != settlements.defs.end());
    --riverIt->merchantStock[0];
    const std::uint64_t changedSettlement = hash();
    CHECK(changedSettlement != base);
    ++riverIt->merchantStock[0];

    auto missionIt = std::find_if(
        missions.defs.begin(), missions.defs.end(),
        [](const game::MissionDef& def) { return def.id == "river_fish_supply"; });
    REQUIRE(missionIt != missions.defs.end());
    missionIt->minSettlementReputation = 0;
    const std::uint64_t changedMission = hash();
    CHECK(changedMission != base);
    CHECK(hash() == changedMission);
    missionIt->minSettlementReputation = -39;
    CHECK(hash() == base);
}
