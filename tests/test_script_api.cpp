#include <doctest/doctest.h>

#include <cstdint>
#include <string>

#include "ai/PathfindingService.h"
#include "characters/components/CharacterComponents.h"
#include "core/GameLoop.h"
#include "core/GameTime.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/GameContext.h"
#include "game/GroundItems.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"
#include "save/SaveTypes.h"
#include "script/ScriptApi.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"
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

// ScriptApi 测试夹具：最小完整 GameContext（无窗口/渲染/物理）
struct Fixture {
    ecs::EcsWorld ecs;
    items::ItemRegistry items;
    core::GameLoop loop{60.0};
    core::GameTime gameTime;
    ai::PathfindingService pathfinding{nullptr, ai::MapChunkBounds{0, 0, 4, 4}};
    game::GameContext ctx;
    script::ScriptApi api;

    Fixture() {
        std::string error;
        CHECK(items.LoadFromString(kItemsJson, "test", &error));
        gameTime.Configure(1.0, core::DateTime{});
        ctx.ecs = &ecs;
        ctx.items = &items;
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
