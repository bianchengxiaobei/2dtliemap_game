#include <doctest/doctest.h>

#ifdef ZL_ENABLE_PYTHON

#include <cstdint>
#include <string>

#include "ai/PathfindingService.h"
#include "characters/components/CharacterComponents.h"
#include "core/EventBus.h"
#include "core/GameLoop.h"
#include "core/GameTime.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/GameContext.h"
#include "items/InventoryOps.h"
#include "items/ItemRegistry.h"
#include "script/PythonRuntime.h"
#include "script/ScriptApi.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"
#include "world/WorldCoords.h"

#include "ScriptTestHelpers.h"

using namespace zl;

namespace {

// 测试进程共享一个解释器实例（与 test_python_runtime.cpp 相同的约定）。
script::PythonRuntime* SharedRuntimePtr() {
    static script::PythonRuntime* runtime = []() -> script::PythonRuntime* {
        auto* candidate = new script::PythonRuntime();
        std::string error;
        if (!candidate->Initialize(error)) {
            return nullptr;
        }
        return candidate;
    }();
    return runtime;
}

const char* kItemsJson = R"({
  "format": "items-v1",
  "items": [
    { "id": "bandage", "name": "绷带", "icon": "i", "weight": 0.1, "maxStack": 5, "type": "consumable" },
    { "id": "axe", "name": "斧", "icon": "i", "weight": 3.0, "maxStack": 1, "type": "weapon" }
  ]
})";

// ScriptApi 测试夹具（同 M7-T3）：最小完整 GameContext（无窗口/渲染/物理）。
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
        gameTime.Configure(60.0, core::DateTime{});  // 1 tick = 1 游戏分钟（定时器测试用）
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

    void LoadOpenChunkWithWall() {
        world::Chunk chunk(0, 0);
        chunk.Square(2, 3, 0).AddObject(world::SquareLayer::Wall, {"wall.brick", 0, 0, true});
        pathfinding.OnChunkLoaded(chunk);
    }
};

core::ScriptEvent MakeEvent(std::string name) {
    core::ScriptEvent event;
    event.name = std::move(name);
    return event;
}

core::ScriptEvent MakeTickEvent(std::int64_t tick) {
    core::ScriptEvent event = MakeEvent(std::string(core::kOnTick));
    event.fields.push_back(core::ScriptField{"tick", tick});
    return event;
}

}  // namespace

TEST_CASE("M8-T3：Python 侧 API 全套（与 Lua 同名同参数）") {
    script::PythonRuntime* runtime = SharedRuntimePtr();
    REQUIRE(runtime != nullptr);
    Fixture f;
    const ecs::Entity player = f.MakePlayer(3.0f, 4.0f);
    f.LoadOpenChunkWithWall();
    runtime->SetApi(&f.api);

    zltest::TempModRoot mods;
    zltest::LogCapture logs;

    const script::ModInfo mod = mods.MakeModPy("py_api", R"py(
assert Game.GetTick() == 0
assert Game.GetPlayerId() != 0
t = Game.GetTime()
assert t['year'] == 2000 and 'totalMinutes' in t
pos = Player.GetPosition()
assert abs(pos['x'] - 3.0) < 1e-6 and pos['z'] == 0
assert abs(Player.GetHealth() - 80.0) < 1e-6
assert Player.Teleport(7.5, 8.25, 0) is True
assert abs(Player.GetPosition()['x'] - 7.5) < 1e-6
assert Player.AddItem('bandage', 2) == 2
assert World.IsPassable(0, 0) is True
assert World.IsPassable(2, 3) is False
assert Zombies.Count() == 0
h = Zombies.Spawn(10.0, 11.0, 0)
assert h != 0 and Zombies.Count() == 1
zp = Zombies.GetPosition(h)
assert abs(zp['x'] - 10.0) < 1e-6 and abs(zp['y'] - 11.0) < 1e-6
assert Items.Exists('bandage') is True
assert Items.GetName('bandage') == '绷带'
Log.Info('api ok')
)py");
    std::string error;
    CHECK(runtime->LoadMod(mod, error));
    CHECK(error.empty());
    CHECK(logs.Str().find("api ok") != std::string::npos);

    // C++ 侧确认脚本确实改到了游戏状态
    CHECK(items::CountOf(f.ecs.Registry().get<ecs::Container>(player), "bandage") == 2);
    const ecs::Position& position = f.ecs.Registry().get<ecs::Position>(player);
    CHECK(position.x == doctest::Approx(7.5f));
    CHECK(position.y == doctest::Approx(8.25f));
}

TEST_CASE("M8-T3：Events.On/Off 与事件字段（Python）") {
    script::PythonRuntime* runtime = SharedRuntimePtr();
    REQUIRE(runtime != nullptr);
    Fixture f;
    f.MakePlayer(3.0f, 4.0f);
    runtime->SetApi(&f.api);

    zltest::TempModRoot mods;
    zltest::LogCapture logs;

    const script::ModInfo mod = mods.MakeModPy("py_events", R"py(
Events.On('OnTick', lambda e: Log.Info('tick=%d' % e['tick']))

# 与 Lua 对齐：0 形参回调也应被正常调用（Lua 会忽略多余实参）
Events.On('OnTick', lambda: Log.Info('noarg'))

def once(e):
    Log.Info('once=%d' % e['tick'])
    Events.Off('OnTick', once)
Events.On('OnTick', once)

def dead(e):
    Log.Info('dead=%d' % e['zombieId'])
Events.On('OnZombieDead', dead)

def move(e):
    Log.Info('move=%d,%d' % (e['x'], e['y']))
    Events.Off('OnPlayerMove')
Events.On('OnPlayerMove', move)
)py");
    std::string error;
    REQUIRE(runtime->LoadMod(mod, error));

    CHECK(runtime->DispatchEventToMod("py_events", MakeTickEvent(5)).empty());
    std::string captured = logs.Str();
    CHECK(captured.find("tick=5") != std::string::npos);
    CHECK(captured.find("once=5") != std::string::npos);
    CHECK(captured.find("noarg") != std::string::npos);

    CHECK(runtime->DispatchEventToMod("py_events", MakeTickEvent(6)).empty());
    captured = logs.Str();
    CHECK(captured.find("tick=6") != std::string::npos);
    CHECK(captured.find("once=6") == std::string::npos);  // Off 生效

    core::ScriptEvent dead = MakeEvent("OnZombieDead");
    dead.fields.push_back(core::ScriptField{"zombieId", std::int64_t{42}});
    CHECK(runtime->DispatchEventToMod("py_events", dead).empty());
    CHECK(logs.Str().find("dead=42") != std::string::npos);

    core::ScriptEvent move = MakeEvent("OnPlayerMove");
    move.fields.push_back(core::ScriptField{"playerId", std::int64_t{1}});
    move.fields.push_back(core::ScriptField{"x", 3.0});
    move.fields.push_back(core::ScriptField{"y", 4.0});
    move.fields.push_back(core::ScriptField{"z", std::int64_t{0}});
    CHECK(runtime->DispatchEventToMod("py_events", move).empty());
    CHECK(logs.Str().find("move=3,4") != std::string::npos);
    const std::size_t before = logs.Str().find("move=3,4");
    CHECK(runtime->DispatchEventToMod("py_events", move).empty());
    CHECK(logs.Str().find("move=3,4") == before);  // Off(name) 全部取消后不再派发
}

TEST_CASE("M8-T3：Game.SetTimer 按游戏时间泵出（Python）") {
    script::PythonRuntime* runtime = SharedRuntimePtr();
    REQUIRE(runtime != nullptr);
    Fixture f;
    f.MakePlayer(3.0f, 4.0f);
    runtime->SetApi(&f.api);

    zltest::TempModRoot mods;
    zltest::LogCapture logs;

    const script::ModInfo mod = mods.MakeModPy("py_timer", R"py(
Game.SetTimer(0, lambda: Log.Info('timer=now'))
Game.SetTimer(30, lambda: Log.Info('timer=30s'))
)py");
    std::string error;
    REQUIRE(runtime->LoadMod(mod, error));

    CHECK(runtime->DispatchEventToMod("py_timer", MakeTickEvent(1)).empty());
    std::string captured = logs.Str();
    CHECK(captured.find("timer=now") != std::string::npos);
    CHECK(captured.find("timer=30s") == std::string::npos);

    f.gameTime.Tick();  // 推进 1 游戏分钟（60 秒）> 30 秒
    CHECK(runtime->DispatchEventToMod("py_timer", MakeTickEvent(2)).empty());
    CHECK(logs.Str().find("timer=30s") != std::string::npos);
}

TEST_CASE("M8-T3：事件回调错误按 mod 归因（traceback），不抛出") {
    script::PythonRuntime* runtime = SharedRuntimePtr();
    REQUIRE(runtime != nullptr);
    Fixture f;
    f.MakePlayer(3.0f, 4.0f);
    runtime->SetApi(&f.api);

    zltest::TempModRoot mods;

    const script::ModInfo mod = mods.MakeModPy("py_bad_handler", R"py(
def bad(e):
    raise ValueError('boom')
Events.On('OnTick', bad)
)py");
    std::string error;
    REQUIRE(runtime->LoadMod(mod, error));

    const std::vector<script::ModError> errors =
        runtime->DispatchEventToMod("py_bad_handler", MakeTickEvent(1));
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].modId == "py_bad_handler");
    CHECK(errors[0].message.find("事件 OnTick 处理错误：") != std::string::npos);
    CHECK(errors[0].message.find("ValueError") != std::string::npos);
    CHECK(errors[0].message.find("boom") != std::string::npos);
    CHECK(errors[0].message.find("main.py") != std::string::npos);
}

#else

TEST_CASE("M8-T3：Python 后端未编译（本构建跳过 Python API 用例）") {
    CHECK(true);
}

#endif  // ZL_ENABLE_PYTHON
