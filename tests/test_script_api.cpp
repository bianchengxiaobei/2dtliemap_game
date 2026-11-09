#include <doctest/doctest.h>

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "ai/PathfindingService.h"
#include "characters/components/CharacterComponents.h"
#include "core/Config.h"
#include "core/GameLoop.h"
#include "core/GameTime.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/ContentReload.h"
#include "game/GameContext.h"
#include "game/GroundItems.h"
#include "game/Missions.h"
#include "game/Poi.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"
#include "items/RecipeRegistry.h"
#include "save/SaveTypes.h"
#include "script/ScriptApi.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"
#include "world/World.h"
#include "world/WorldCoords.h"

using namespace zl;

namespace {

const char* kItemsJson = R"({
  "format": "items-v1",
  "items": [
    { "id": "bandage", "name": "绷带", "icon": "i", "weight": 0.1, "maxStack": 5, "type": "consumable" },
    { "id": "axe", "name": "斧", "icon": "i", "weight": 3.0, "maxStack": 1, "type": "weapon" }
  ]
})";

const char* kRecipesJson = R"({
  "format": "recipes-v3",
  "recipes": [
    {
      "id": "base_bandage",
      "name": "基础绷带",
      "output": { "item": "bandage", "count": 1 },
      "ingredients": [ { "item": "bandage", "count": 1 } ]
    }
  ]
})";

// ScriptApi 测试夹具：最小完整 GameContext（无窗口/渲染/物理）
struct Fixture {
    ecs::EcsWorld ecs;
    items::ItemRegistry items;
    items::RecipeRegistry recipes;
    core::Config config;
    game::PoiConfig poiConfig;
    game::MissionConfig missionConfig;
    world::World world;
    core::GameLoop loop{60.0};
    core::GameTime gameTime;
    ai::PathfindingService pathfinding{nullptr, ai::MapChunkBounds{0, 0, 4, 4}};
    game::GameContext ctx;
    script::ScriptApi api;

    Fixture() {
        std::string error;
        CHECK(items.LoadFromString(kItemsJson, "test", &error));
        CHECK(recipes.LoadFromString(kRecipesJson, "test-recipes", items, &error));
        gameTime.Configure(1.0, core::DateTime{});
        ctx.config = &config;
        ctx.ecs = &ecs;
        ctx.items = &items;
        ctx.recipes = &recipes;
        ctx.world = &world;
        ctx.loop = &loop;
        ctx.gameTime = &gameTime;
        ctx.pathfinding = &pathfinding;
        api.Attach(&ctx);
    }

    ecs::Entity MakePlayer(float x, float y) {
        const ecs::Entity e = ecs.Create();
        auto& registry = ecs.Registry();
        registry.emplace<ecs::PlayerTag>(e);
        registry.emplace<ecs::Position>(e, ecs::Position{x, y, 0});
        registry.emplace<ecs::Velocity>(e);
        registry.emplace<ecs::Health>(e, ecs::Health{80.0f, 100.0f});
        ecs::Container bag;
        bag.capacityKg = 20.0f;
        registry.emplace<ecs::Container>(e, bag);
        return e;
    }

    void LoadOpenChunk() {
        world::Chunk chunk(0, 0);
        pathfinding.OnChunkLoaded(chunk);
    }
};

}  // namespace

TEST_CASE("M7-T3：玩家查询（稳定 ID/位置/血量；无玩家防护）") {
    Fixture f;
    CHECK(f.api.GetPlayerId() == 0);
    script::ScriptVec3 position;
    CHECK_FALSE(f.api.GetPlayerPosition(position));
    float hp = 0.0f;
    CHECK_FALSE(f.api.GetPlayerHealth(hp));

    const ecs::Entity player = f.MakePlayer(3.0f, 4.0f);
    CHECK(f.api.GetPlayerId() == f.ecs.IdOf(player).id);
    CHECK(f.api.GetPlayerId() != 0);

    REQUIRE(f.api.GetPlayerPosition(position));
    CHECK(position.x == doctest::Approx(3.0f));
    CHECK(position.y == doctest::Approx(4.0f));
    CHECK(position.z == 0);

    REQUIRE(f.api.GetPlayerHealth(hp));
    CHECK(hp == doctest::Approx(80.0f));
}

TEST_CASE("M7-T3：Player.Teleport 与 AddItem（含非法输入）") {
    Fixture f;
    const ecs::Entity player = f.MakePlayer(3.0f, 4.0f);
    f.ecs.Registry().get<ecs::Velocity>(player).x = 1.5f;

    CHECK(f.api.TeleportPlayer(7.5f, 8.25f, 0));
    const ecs::Position& position = f.ecs.Registry().get<ecs::Position>(player);
    CHECK(position.x == doctest::Approx(7.5f));
    CHECK(position.y == doctest::Approx(8.25f));
    CHECK(f.ecs.Registry().get<ecs::Velocity>(player).x == doctest::Approx(0.0f));

    CHECK(f.api.AddPlayerItem("bandage", 3) == 3);
    CHECK(items::CountOf(f.ecs.Registry().get<ecs::Container>(player), "bandage") == 3);
    CHECK(f.api.AddPlayerItem("bandage", 4) == 4);  // maxStack=5：合并到 5 + 新堆 2
    CHECK(items::CountOf(f.ecs.Registry().get<ecs::Container>(player), "bandage") == 7);
    CHECK(f.api.AddPlayerItem("no_such_item", 1) == 0);
    CHECK(f.api.AddPlayerItem("bandage", 0) == 0);
    CHECK(f.api.AddPlayerItem("bandage", -1) == 0);
}

TEST_CASE("M7-T3：World.IsPassable 与 SpawnItem") {
    Fixture f;
    world::Chunk chunk(0, 0);
    chunk.Square(2, 3, 0).AddObject(world::SquareLayer::Wall, {"wall.brick", 0, 0, true});
    f.pathfinding.OnChunkLoaded(chunk);

    CHECK_FALSE(f.api.IsPassable(2, 3));    // 墙：阻挡
    CHECK(f.api.IsPassable(0, 0));          // 空地面
    CHECK_FALSE(f.api.IsPassable(50, 50));  // 未加载 chunk = 不可通行

    CHECK(f.api.SpawnItem("bandage", 5.0f, 6.0f, 0));
    CHECK(game::CountGroundItems(f.ecs) == 1);
    CHECK_FALSE(f.api.SpawnItem("no_such_item", 5.0f, 6.0f, 0));
    CHECK(game::CountGroundItems(f.ecs) == 1);
}

TEST_CASE("X-F2：World.GetSquare 返回只读分层快照") {
    Fixture f;
    world::GridSquare& square = f.world.GetOrCreateSquare(2, 3, 0);
    square.SetFloor(world::SquareObject{"tile.floor.wood", 0, 0, false});
    square.AddObject(
        world::SquareLayer::Wall,
        world::SquareObject{"object.wall_h", 0, 0, true, 80.0f, true});
    square.AddObject(
        world::SquareLayer::Furniture,
        world::SquareObject{"object.crate", 0, 0, false, -1.0f, true});
    square.AddObject(world::SquareLayer::Item,
                     world::SquareObject{"object.locker", 0, 0, false});

    script::ScriptSquare out;
    REQUIRE(f.api.GetSquare(2, 3, 0, out));
    CHECK(out.loaded);
    CHECK(out.hasFloor);
    CHECK(out.floor.sprite == "tile.floor.wood");
    CHECK_FALSE(out.floor.playerBuilt);
    REQUIRE(out.wall.size() == 1);
    CHECK(out.wall[0].sprite == "object.wall_h");
    CHECK(out.wall[0].blocksMovement);
    CHECK(out.wall[0].playerBuilt);
    CHECK(out.wall[0].hp == doctest::Approx(80.0f));
    REQUIRE(out.furniture.size() == 1);
    CHECK(out.furniture[0].sprite == "object.crate");
    CHECK_FALSE(out.furniture[0].blocksMovement);
    REQUIRE(out.item.size() == 1);
    CHECK(out.item[0].sprite == "object.locker");

    CHECK_FALSE(f.api.GetSquare(50, 50, 0, out));
    CHECK_FALSE(out.loaded);
    CHECK_FALSE(f.api.GetSquare(2, 3, 8, out));
}

TEST_CASE("X-F2b-2d：Hud 自定义字段注册、更新与上限") {
    Fixture f;
    f.ctx.hudState.active = true;
    CHECK_FALSE(f.api.RegisterHudField("", "status", "状态"));
    CHECK_FALSE(f.api.RegisterHudField("mod", "", "状态"));
    CHECK_FALSE(f.api.RegisterHudField("mod", "status", ""));
    CHECK(f.api.RegisterHudField("mod", "status", "状态"));
    CHECK(f.api.RegisterHudField("mod", "status", "新状态"));
    CHECK_FALSE(f.api.SetHudField("missing", "x", "x", "info"));
    CHECK_FALSE(f.api.SetHudField("mod", "status", "x", "bad"));
    CHECK(f.api.SetHudField("mod", "status", "运行中", "warn"));

    ui::HudState state;
    REQUIRE(f.api.GetHudState(state));
    CHECK(state.modFieldCount == 1);
    CHECK(state.modFields[0].modId == "mod");
    CHECK(state.modFields[0].id == "status");
    CHECK(state.modFields[0].label == "新状态");
    CHECK(state.modFields[0].text == "运行中");
    CHECK(state.modFields[0].tone == "warn");

    CHECK(f.api.ClearHudField("mod", "status"));
    REQUIRE(f.api.GetHudState(state));
    CHECK(state.modFields[0].text.empty());
    CHECK(state.modFields[0].tone == "info");
    CHECK_FALSE(f.api.ClearHudField("mod", "missing"));

    for (int i = 1; i < 8; ++i) {
        CHECK(f.api.RegisterHudField("mod", "extra_" + std::to_string(i),
                                     "扩展"));
    }
    CHECK_FALSE(f.api.RegisterHudField("mod", "extra_8", "扩展"));
    f.api.RemoveHudFields("mod");
    REQUIRE(f.api.GetHudState(state));
    CHECK(state.modFieldCount == 0);
}

TEST_CASE("M7-T3：Zombies.Spawn/Count/GetPosition（稳定 ID 句柄）") {
    Fixture f;
    CHECK(f.api.ZombieCount() == 0);

    const std::uint64_t handle = f.api.SpawnZombie(10.0f, 11.0f, 0);
    CHECK(handle != 0);
    CHECK(f.api.ZombieCount() == 1);

    script::ScriptVec3 position;
    REQUIRE(f.api.GetZombiePosition(handle, position));
    CHECK(position.x == doctest::Approx(10.0f));
    CHECK(position.y == doctest::Approx(11.0f));
    CHECK_FALSE(f.api.GetZombiePosition(0, position));     // 无效句柄
    CHECK_FALSE(f.api.GetZombiePosition(9999, position));  // 不存在

    f.ecs.Destroy(f.ecs.FindById(handle));
    f.ecs.Flush();
    CHECK(f.api.ZombieCount() == 0);
    CHECK_FALSE(f.api.GetZombiePosition(handle, position));  // 已销毁：句柄失效
}

TEST_CASE("M7-T3：Game 信息（tick/时间/玩家 ID）与 Items 查询") {
    Fixture f;
    CHECK(f.api.GetTick() == 0);
    f.loop.Frame(1.0 / 60.0, [](double) {}, [](double, double) {});
    f.loop.Frame(1.0 / 60.0, [](double) {}, [](double, double) {});
    CHECK(f.api.GetTick() == 2);

    const core::DateTime now = f.api.GetTime();
    CHECK(now.year == 2000);
    CHECK(now.month == 7);
    CHECK(now.day == 9);
    CHECK(now.hour == 0);
    CHECK(f.api.GetTotalGameMinutes() == doctest::Approx(0.0));

    CHECK(f.api.ItemExists("bandage"));
    CHECK_FALSE(f.api.ItemExists("nope"));
    CHECK(f.api.ItemName("bandage") == "绷带");
    CHECK(f.api.ItemName("axe") == "斧");
    CHECK(f.api.ItemName("nope").empty());
}

TEST_CASE("M9-T7：Inventory API 读取状态、提交动作与点击音效") {
    Fixture f;
    ui::InventoryState source;
    source.open = true;
    source.playerValid = true;
    source.weight = 3.2f;
    source.capacity = 20.0f;
    source.bagStacks = 1;
    source.lootStacks = 2;
    source.status = "已拾取";
    source.statusTone = "ok";
    source.bag[0] = ui::InventorySlot{true, "绷带", 2, true, false, 0};
    source.equipment[0] = ui::InventorySlot{true, "斧", 1, false, true, 0};
    source.loot[0] = ui::InventorySlot{true, "破布", 3, false, false, 77};
    f.ctx.getInventoryState = [&source](ui::InventoryState& out) {
        out = source;
        return true;
    };
    ui::InventoryAction received;
    bool hasAction = false;
    f.ctx.inventoryAction = [&received, &hasAction](const ui::InventoryAction& action) {
        received = action;
        hasAction = true;
        return true;
    };
    int clicks = 0;
    f.ctx.playInventoryClick = [&clicks] { ++clicks; };

    ui::InventoryState state;
    REQUIRE(f.api.GetInventoryState(state));
    CHECK(state.open);
    CHECK(state.playerValid);
    CHECK(state.weight == doctest::Approx(3.2f));
    CHECK(state.capacity == doctest::Approx(20.0f));
    CHECK(state.bagStacks == 1);
    CHECK(state.lootStacks == 2);
    CHECK(state.status == "已拾取");
    CHECK(state.bag.size() == static_cast<std::size_t>(ui::kInventoryBagSlots));
    CHECK(state.bag[0].label == "绷带");
    CHECK(state.bag[0].usable);
    CHECK(state.equipment[0].equippable);
    CHECK(state.loot[0].entityId == 77);

    ui::InventoryAction action;
    action.type = ui::InventoryActionType::Transfer;
    action.from = ui::InventoryArea::Backpack;
    action.fromIndex = 0;
    action.to = ui::InventoryArea::Equipment;
    action.toIndex = 0;
    CHECK(f.api.InventoryAction(action));
    CHECK(hasAction);
    CHECK(received.type == action.type);
    CHECK(received.from == action.from);
    CHECK(received.fromIndex == 0);
    CHECK(received.to == action.to);
    CHECK(received.toIndex == 0);

    f.api.PlayInventoryClick();
    CHECK(clicks == 1);

    f.ctx.inventoryAction = nullptr;
    CHECK_FALSE(f.api.InventoryAction(action));
}

TEST_CASE("脚本存档 UI：Save API 列表、读档、删除与快速保存") {
    Fixture f;
    save::SaveMetadata first;
    first.id = "save-one";
    first.name = "第一次存档";
    first.updatedAt = 1;
    first.mapId = "village_demo";
    first.gameVersion = "0.3.0";
    save::SaveMetadata second;
    second.id = "save-two";
    second.updatedAt = 2;
    std::vector<save::SaveMetadata> source{first, second};

    f.ctx.listSaves = [&source](std::vector<save::SaveMetadata>& out,
                                std::string& error) {
        out = source;
        error.clear();
        return true;
    };
    std::string loadedId;
    f.ctx.loadSave = [&loadedId](const std::string& id, std::string& error) {
        if (id != "save-one") {
            error = "不存在";
            return false;
        }
        loadedId = id;
        return true;
    };
    int quickSaves = 0;
    f.ctx.quickSave = [&quickSaves] {
        ++quickSaves;
        return true;
    };
    std::vector<std::string> deleted;
    f.ctx.deleteSave = [&deleted](const std::string& id, std::string& error) {
        if (id != "save-one") {
            error = "不存在";
            return false;
        }
        deleted.push_back(id);
        return true;
    };

    std::vector<script::ScriptSaveInfo> saves;
    std::string error;
    REQUIRE(f.api.ListSaves(saves, error));
    REQUIRE(saves.size() == 2);
    CHECK(saves[0].id == "save-one");
    CHECK(saves[0].name == "第一次存档");
    CHECK(saves[0].mapId == "village_demo");
    CHECK_FALSE(saves[0].updatedText.empty());
    CHECK(saves[1].name == "save-two");  // 空名称回退到 id

    CHECK(f.api.LoadSave("save-one", error));
    CHECK(loadedId == "save-one");
    CHECK_FALSE(f.api.LoadSave("missing", error));
    CHECK(error == "不存在");
    CHECK_FALSE(f.api.LoadSave("", error));
    CHECK(f.api.QuickSave());
    CHECK(quickSaves == 1);

    CHECK(f.api.DeleteSave("save-one", error));
    CHECK(deleted.size() == 1);
    CHECK(deleted[0] == "save-one");
    CHECK_FALSE(f.api.DeleteSave("missing", error));
    CHECK(error == "不存在");
    CHECK_FALSE(f.api.DeleteSave("", error));
    CHECK(error == "存档 id 为空");

    f.ctx.listSaves = nullptr;
    CHECK_FALSE(f.api.ListSaves(saves, error));
    f.ctx.quickSave = nullptr;
    CHECK_FALSE(f.api.QuickSave());
    f.ctx.deleteSave = nullptr;
    CHECK_FALSE(f.api.DeleteSave("save-one", error));
    CHECK(error == "删除存档不可用");
}

TEST_CASE("X-F2b-2e：Content.RegisterItems/Recipes/Buildings 动态注册") {
    Fixture f;
    int contentChanges = 0;
    f.ctx.contentChanged = [&contentChanges] { ++contentChanges; };
    std::string error;

    const std::string itemJson = R"({
      "format": "items-v1",
      "items": [
        { "id": "script_dynamic", "name": "脚本物品", "icon": "item.dynamic", "weight": 0.2, "maxStack": 2, "type": "generic" }
      ]
    })";
    CHECK(f.api.RegisterItems("test_mod", itemJson, error) == 1);
    CHECK(error.empty());
    CHECK(f.items.Find("script_dynamic") != nullptr);
    CHECK(f.ctx.dynamicContentCount == 1);
    CHECK(contentChanges == 1);

    CHECK(f.api.RegisterItems("test_mod", itemJson, error) == 0);
    CHECK_FALSE(error.empty());
    CHECK(f.items.Size() == 3);
    CHECK(f.ctx.dynamicContentCount == 1);
    CHECK(contentChanges == 1);

    const std::string recipeJson = R"({
      "format": "recipes-v3",
      "recipes": [
        {
          "id": "script_dynamic_recipe",
          "name": "脚本配方",
          "output": { "item": "script_dynamic", "count": 1 },
          "ingredients": [ { "item": "bandage", "count": 1 } ]
        }
      ]
    })";
    CHECK(f.api.RegisterRecipes("test_mod", recipeJson, error) == 1);
    CHECK(error.empty());
    CHECK(f.recipes.Find("script_dynamic_recipe") != nullptr);
    CHECK(f.ctx.dynamicContentCount == 2);
    CHECK(contentChanges == 2);

    const std::string buildingJson = R"({
      "format": "buildings-v1",
      "types": [
        {
          "id": "script_dynamic_crate",
          "name": "脚本仓库",
          "sprite": "object.script_dynamic_crate",
          "woodCost": 2,
          "storageKg": 12,
          "storageSlots": 2,
          "category": "storage"
        }
      ]
    })";
    CHECK(f.api.RegisterBuildings("test_mod", buildingJson, error) == 1);
    CHECK(error.empty());
    REQUIRE(f.ctx.modBuildables.size() == 1);
    CHECK(f.ctx.modBuildables[0].id == "script_dynamic_crate");
    CHECK(f.ctx.dynamicContentCount == 3);
    CHECK(contentChanges == 3);

    std::vector<game::BuildableDef> buildables = game::LoadBuildableDefs(f.config);
    CHECK(game::AppendBuildableDefs(buildables, f.ctx.modBuildables) == 1);
    CHECK(game::FindBuildableBySprite(buildables, "object.script_dynamic_crate") !=
          nullptr);

    CHECK(f.api.RegisterBuildings("test_mod", buildingJson, error) == 0);
    CHECK_FALSE(error.empty());
    CHECK(f.ctx.modBuildables.size() == 1);
    CHECK(f.ctx.dynamicContentCount == 3);

    f.ctx.allowDynamicContent = false;
    const std::string anotherItem = R"({
      "format": "items-v1",
      "items": [
        { "id": "script_dynamic_two", "name": "脚本物品二", "icon": "item.dynamic", "weight": 0.2, "maxStack": 2, "type": "generic" }
      ]
    })";
    CHECK(f.api.RegisterItems("test_mod", anotherItem, error) == 0);
    CHECK(error == game::kDynamicContentSinglePlayerOnlyMessage);
    CHECK(f.items.Find("script_dynamic_two") == nullptr);
    CHECK(f.ctx.dynamicContentCount == 3);
    CHECK(contentChanges == 3);
}

TEST_CASE("X3-T5：联机禁用时五类动态内容注册全部拒绝") {
    Fixture f;
    f.ctx.allowDynamicContent = false;
    f.poiConfig.enabled = true;
    f.ctx.poiConfig = &f.poiConfig;
    f.ctx.missionConfig = &f.missionConfig;
    int contentChanges = 0;
    f.ctx.contentChanged = [&contentChanges] { ++contentChanges; };
    std::string error;

    const std::string expected = game::kDynamicContentSinglePlayerOnlyMessage;
    CHECK(f.api.RegisterItems("test_mod", "{}", error) == 0);
    CHECK(error == expected);
    CHECK(f.api.RegisterRecipes("test_mod", "{}", error) == 0);
    CHECK(error == expected);
    CHECK(f.api.RegisterBuildings("test_mod", "{}", error) == 0);
    CHECK(error == expected);
    CHECK(f.api.RegisterPois("test_mod", "{}", error) == 0);
    CHECK(error == expected);
    CHECK(f.api.RegisterMissions("test_mod", "{}", error) == 0);
    CHECK(error == expected);

    CHECK(f.items.Size() == 2);
    CHECK(f.recipes.Size() == 1);
    CHECK(f.ctx.modBuildables.empty());
    CHECK(f.poiConfig.defs.empty());
    CHECK(f.missionConfig.defs.empty());
    CHECK(f.ctx.dynamicContentCount == 0);
    CHECK(contentChanges == 0);
}

TEST_CASE("X-F2b-2e：Content.RegisterPois/RegisterMissions 动态注册") {
    Fixture f;
    std::string error;

    f.poiConfig.enabled = true;
    f.poiConfig.guideRange = 70.0f;
    f.ctx.poiConfig = &f.poiConfig;
    const std::string poiJson = R"({
      "format": "pois-v1",
      "defs": [
        {
          "id": "script_poi",
          "name": "脚本 POI",
          "threat": 1,
          "markerX": 10.0,
          "markerY": 11.0,
          "containers": [
            { "x": 10.5, "y": 11.5, "tableId": "script_table", "sprite": "object.crate" }
          ],
          "enemies": { "count": 0, "kind": "normal", "radius": 4.0 },
          "task": {
            "description": "搜索补给",
            "targetTable": "script_table",
            "rewardXp": 5.0,
            "rewardItem": "bandage",
            "rewardCount": 1
          }
        }
      ]
    })";
    CHECK(f.api.RegisterPois("test_mod", poiJson, error) == 1);
    CHECK(error.empty());
    REQUIRE(f.poiConfig.defs.size() == 1);
    CHECK(f.poiConfig.Find("script_poi") != nullptr);
    CHECK(f.ctx.dynamicContentCount == 1);

    CHECK(f.api.RegisterPois("test_mod", poiJson, error) == 0);
    CHECK_FALSE(error.empty());
    CHECK(f.poiConfig.defs.size() == 1);

    const std::string baseMissions = R"({
      "enabled": true,
      "randomSeed": 7,
      "defs": [
        {
          "id": "base_script",
          "name": "基础脚本任务",
          "chain": "base",
          "order": 1,
          "kind": "once",
          "source": "base",
          "objectives": [
            { "kind": "kill", "target": "any", "count": 1, "description": "清理僵尸" }
          ],
          "reward": { "xp": 10.0, "skill": "melee", "label": "10 经验" }
        }
      ]
    })";
    f.config.Set("missions", nlohmann::json::parse(baseMissions));
    REQUIRE(game::LoadMissionConfig(f.config, &f.missionConfig, &error));
    f.ctx.missionConfig = &f.missionConfig;
    f.ctx.missionSourceJson = f.config.Raw()["missions"].dump();

    const std::string missionJson = R"({
      "format": "missions-v1",
      "defs": [
        {
          "id": "script_mission",
          "name": "脚本任务",
          "chain": "script",
          "order": 1,
          "kind": "once",
          "source": "note",
          "prerequisite": "base_script",
          "objectives": [
            { "kind": "kill", "target": "any", "count": 2, "description": "清理两只僵尸" }
          ],
          "reward": { "xp": 20.0, "skill": "melee", "label": "20 经验" }
        }
      ]
    })";
    CHECK(f.api.RegisterMissions("test_mod", missionJson, error) == 1);
    CHECK(error.empty());
    REQUIRE(f.missionConfig.defs.size() == 2);
    REQUIRE(f.missionConfig.Find("script_mission") != nullptr);
    CHECK(f.missionConfig.Find("script_mission")->prerequisite == "base_script");
    CHECK(f.ctx.dynamicContentCount == 2);

    CHECK(f.api.RegisterMissions("test_mod", missionJson, error) == 0);
    CHECK_FALSE(error.empty());
    CHECK(f.missionConfig.defs.size() == 2);

    const std::string badPrerequisite = R"({
      "format": "missions-v1",
      "defs": [
        {
          "id": "bad_script_mission",
          "name": "坏前置",
          "chain": "bad",
          "order": 1,
          "kind": "once",
          "source": "note",
          "prerequisite": "missing_mission",
          "objectives": [
            { "kind": "kill", "target": "any", "count": 1, "description": "清理僵尸" }
          ],
          "reward": { "xp": 1.0, "skill": "melee", "label": "1 经验" }
        }
      ]
    })";
    CHECK(f.api.RegisterMissions("test_mod", badPrerequisite, error) == 0);
    CHECK_FALSE(error.empty());
    CHECK(f.missionConfig.defs.size() == 2);
    CHECK(f.missionConfig.Find("bad_script_mission") == nullptr);

    f.poiConfig.enabled = false;
    CHECK(f.api.RegisterPois("test_mod", poiJson, error) == 0);
    f.missionConfig.enabled = false;
    CHECK(f.api.RegisterMissions("test_mod", missionJson, error) == 0);
}