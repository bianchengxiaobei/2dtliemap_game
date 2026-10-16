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
