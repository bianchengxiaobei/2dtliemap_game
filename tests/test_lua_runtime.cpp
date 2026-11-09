#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "ScriptTestHelpers.h"
#include "ai/PathfindingService.h"
#include "characters/components/CharacterComponents.h"
#include "core/EventBus.h"
#include "core/GameLoop.h"
#include "core/GameTime.h"
#include "core/Log.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/GameContext.h"
#include "game/GroundItems.h"
#include "game/ZombieFactory.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"
#include "script/EventBridge.h"
#include "script/LuaRuntime.h"
#include "script/ScriptApi.h"
#include "script/ScriptManager.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"
#include "world/World.h"

using namespace zl;
using zltest::LogCapture;
using zltest::TempModRoot;

TEST_CASE("M7-T2：初始化/关闭与未初始化防护") {
    script::LuaRuntime runtime;
    CHECK(std::string(runtime.Name()) == "lua");
    CHECK(runtime.LoadedMods().empty());

    TempModRoot mods;
    std::string error;
    CHECK_FALSE(runtime.LoadMod(mods.MakeMod("m", "print('x')"), error));
    CHECK(error.find("未初始化") != std::string::npos);

    CHECK(runtime.Initialize(error));
    CHECK(error.empty());
    CHECK(runtime.Initialize(error));  // 幂等

    runtime.Shutdown();
    CHECK_FALSE(runtime.LoadMod(mods.MakeMod("m", "print('x')"), error));
    CHECK(error.find("未初始化") != std::string::npos);

    CHECK(runtime.Initialize(error));  // 可再次初始化
    CHECK(runtime.LoadMod(mods.MakeMod("m", "print('x')"), error));
    CHECK(runtime.LoadedMods() == std::vector<std::string>{"m"});
}

TEST_CASE("M7-T2：加载成功与 print 重定向到游戏日志") {
    TempModRoot mods;
    script::LuaRuntime runtime;
    std::string error;
    REQUIRE(runtime.Initialize(error));

    LogCapture capture;
    CHECK(runtime.LoadMod(mods.MakeMod("hello", "print('hello', 'from lua', 42, true)"), error));
    CHECK(error.empty());
    CHECK(runtime.LoadedMods() == std::vector<std::string>{"hello"});

    const std::string logs = capture.Str();
    CHECK(logs.find("[script-lua] [hello] hello\tfrom lua\t42\ttrue") != std::string::npos);
}

TEST_CASE("M7-T2：沙箱——白名单可用，os/io/dofile 不可用") {
    TempModRoot mods;
    script::LuaRuntime runtime;
    std::string error;
    REQUIRE(runtime.Initialize(error));

    LogCapture capture;
    CHECK(runtime.LoadMod(mods.MakeMod("safe",
                                       "local t = { 3, 1, 2 }\n"
                                       "table.sort(t)\n"
                                       "print(math.floor(3.7), string.upper('ok'), #t, tostring(nil))\n"),
                          error));
    CHECK(error.empty());
    CHECK(capture.Str().find("3\tOK\t3\tnil") != std::string::npos);

    CHECK_FALSE(runtime.LoadMod(mods.MakeMod("uses_os", "os.time()"), error));
    CHECK(error.find("main.lua:1") != std::string::npos);
    CHECK(error.find("os") != std::string::npos);

    CHECK_FALSE(runtime.LoadMod(mods.MakeMod("uses_dofile", "dofile('x.lua')"), error));
    CHECK(error.find("dofile") != std::string::npos);

    // M7-T5 沙箱收口：危险库与动态加载入口一律不可见（nil）
    CHECK(runtime.LoadMod(
        mods.MakeMod("surface", "Log.Info('d=' .. tostring(debug) .. ' p=' .. tostring(package) .."
                                " ' i=' .. tostring(io) .. ' r=' .. tostring(require) .."
                                " ' lf=' .. tostring(loadfile) .. ' l=' .. tostring(load) .."
                                " ' s=' .. tostring(type(string)))"),
        error));
    CHECK(error.empty());
    CHECK(capture.Str().find("d=nil p=nil i=nil r=nil lf=nil l=nil s=table") !=
          std::string::npos);

    CHECK(runtime.LoadedMods() == std::vector<std::string>{"safe", "surface"});
}

TEST_CASE("M7-T2：错误定位——语法错误行号 + 运行时错误带 traceback") {
    TempModRoot mods;
    script::LuaRuntime runtime;
    std::string error;
    REQUIRE(runtime.Initialize(error));

    // 语法错误：文件:行号
    CHECK_FALSE(runtime.LoadMod(mods.MakeMod("syntax", "local x = ( **invalid** )"), error));
    CHECK(error.find("main.lua:1") != std::string::npos);

    // 运行时错误：消息 + 行号 + traceback（含函数名）
    CHECK_FALSE(runtime.LoadMod(mods.MakeMod("boom",
                                             "local function explode()\n"
                                             "    error('炸弹爆炸')\n"
                                             "end\n"
                                             "explode()\n"),
                                error));
    CHECK(error.find("main.lua:2") != std::string::npos);
    CHECK(error.find("炸弹爆炸") != std::string::npos);
    CHECK(error.find("stack traceback:") != std::string::npos);
    CHECK(error.find("explode") != std::string::npos);

    CHECK(runtime.LoadedMods().empty());
    CHECK(runtime.LoadMod(mods.MakeMod("fine", "print('fine')"), error));  // 出错后仍可加载
}

TEST_CASE("M7-T2：ReloadMod——成功换新、失败保留旧环境、恢复可用") {
    TempModRoot mods;
    script::LuaRuntime runtime;
    std::string error;
    REQUIRE(runtime.Initialize(error));

    LogCapture capture;
    const script::ModInfo mod = mods.MakeMod("hot", "print('v1')");
    CHECK(runtime.LoadMod(mod, error));

    mods.WriteLua("hot", "print('v2')");
    CHECK(runtime.ReloadMod("hot", error));
    CHECK(error.empty());
    CHECK(capture.Str().find("v2") != std::string::npos);
    CHECK(runtime.LoadedMods() == std::vector<std::string>{"hot"});  // 不重复登记

    mods.WriteLua("hot", "error('坏了')");
    CHECK_FALSE(runtime.ReloadMod("hot", error));
    CHECK(error.find("坏了") != std::string::npos);
    CHECK(runtime.LoadedMods() == std::vector<std::string>{"hot"});  // 失败保留旧版本

    mods.WriteLua("hot", "print('v3')");
    CHECK(runtime.ReloadMod("hot", error));

    CHECK_FALSE(runtime.ReloadMod("ghost", error));
    CHECK(error.find("未加载") != std::string::npos);
}

TEST_CASE("M7-T2：mod 环境隔离与卸载") {
    TempModRoot mods;
    script::LuaRuntime runtime;
    std::string error;
    REQUIRE(runtime.Initialize(error));

    LogCapture capture;
    CHECK(runtime.LoadMod(mods.MakeMod("a", "value = 42\nprint('a sees', value)"), error));
    CHECK(runtime.LoadMod(mods.MakeMod("b", "print('b sees', value)"), error));

    const std::string logs = capture.Str();
    CHECK(logs.find("a sees\t42") != std::string::npos);
    CHECK(logs.find("b sees\tnil") != std::string::npos);  // 看不到 a 的全局（_ENV 隔离）

    runtime.UnloadMod("a");
    CHECK(runtime.LoadedMods() == std::vector<std::string>{"b"});
    runtime.UnloadMod("a");  // 未加载：不崩
    CHECK(runtime.LoadedMods() == std::vector<std::string>{"b"});
}

TEST_CASE("M7-T2：缺失脚本文件与重复加载错误") {
    TempModRoot mods;
    script::LuaRuntime runtime;
    std::string error;
    REQUIRE(runtime.Initialize(error));

    script::ModInfo missing;
    missing.id = "missing";
    missing.backend = "lua";
    missing.rootPath = (mods.root / "nope").string();
    missing.mainScript = "scripts/main.lua";
    CHECK_FALSE(runtime.LoadMod(missing, error));
    CHECK(error.find("无法读取脚本文件") != std::string::npos);

    CHECK(runtime.LoadMod(mods.MakeMod("dup", "print('ok')"), error));
    CHECK_FALSE(runtime.LoadMod(mods.MakeMod("dup", "print('again')"), error));
    CHECK(error.find("已加载") != std::string::npos);
}

TEST_CASE("M7-T3：Lua 绑定端到端（Game/Player/World/Zombies/Items/Log）") {
    // 最小游戏环境：玩家 + 物品注册表 + 空 chunk + 一个 tick
    ecs::EcsWorld ecs;
    items::ItemRegistry items;
    std::string registryError;
    REQUIRE(items.LoadFromString(R"({
      "format": "items-v1",
      "items": [
        { "id": "bandage", "name": "绷带", "icon": "i", "weight": 0.1, "maxStack": 5, "type": "consumable" }
      ]
    })", "test", &registryError));
    core::GameLoop loop(60.0);
    loop.Frame(1.0 / 60.0, [](double) {}, [](double, double) {});
    core::GameTime gameTime;
    gameTime.Configure(1.0, core::DateTime{});
    ai::PathfindingService pathfinding(nullptr, ai::MapChunkBounds{0, 0, 4, 4});
    world::Chunk chunk(0, 0);
    pathfinding.OnChunkLoaded(chunk);
    world::World world;
    world::GridSquare& square = world.GetOrCreateSquare(1, 0, 0);
    square.SetFloor(world::SquareObject{"tile.floor.wood", 0, 0, false});
    square.AddObject(world::SquareLayer::Wall,
                     world::SquareObject{"wall.brick", 0, 0, true});

    const ecs::Entity player = ecs.Create();
    {
        auto& registry = ecs.Registry();
        registry.emplace<ecs::PlayerTag>(player);
        registry.emplace<ecs::Position>(player, ecs::Position{3.0f, 4.0f, 0});
        registry.emplace<ecs::Velocity>(player);
        registry.emplace<ecs::Health>(player, ecs::Health{80.0f, 100.0f});
        ecs::Container bag;
        bag.capacityKg = 20.0f;
        registry.emplace<ecs::Container>(player, bag);
    }

    game::GameContext ctx;
    ctx.ecs = &ecs;
    ctx.items = &items;
    ctx.world = &world;
    ctx.loop = &loop;
    ctx.gameTime = &gameTime;
    ctx.pathfinding = &pathfinding;
    script::ScriptApi api;
    api.Attach(&ctx);

    TempModRoot mods;
    script::LuaRuntime runtime;
    runtime.SetApi(&api);
    std::string error;
    REQUIRE(runtime.Initialize(error));

    LogCapture capture;
    const script::ModInfo mod = mods.MakeMod("apitest", R"lua(
Log.Info('boot pid=' .. Game.GetPlayerId())
local p = Player.GetPosition()
Log.Info(string.format('at %.1f,%.1f,%d', p.x, p.y, p.z))
Log.Info('added=' .. Player.AddItem('bandage', 2))
Player.Teleport(5, 6, 0)
Log.Info('hp=' .. tostring(Player.GetHealth()))
local h = Zombies.Spawn(9, 9, 0)
Log.Info('zombies=' .. Zombies.Count() .. ' handle0=' .. tostring(h ~= 0))
local z = Zombies.GetPosition(h)
Log.Info(string.format('z at %.0f,%.0f', z.x, z.y))
Log.Info('item=' .. Items.GetName('bandage') .. ' exists=' .. tostring(Items.Exists('missing')))
Log.Info('spawnitem=' .. tostring(World.SpawnItem('bandage', 1.5, 2.5, 0)) .. '/' .. tostring(World.SpawnItem('nope', 1.5, 2.5, 0)))
Log.Info('passable=' .. tostring(World.IsPassable(0, 0)))
local sq = World.GetSquare(1, 0, 0)
Log.Info('square=' .. tostring(sq ~= nil) .. '/' .. tostring(sq.hasFloor) .. '/' .. sq.floor.sprite .. '/' .. sq.wall[1].sprite)
local t = Game.GetTime()
Log.Info('date=' .. t.year .. '-' .. t.month .. '-' .. t.day .. ' tick=' .. Game.GetTick())
)lua");
    CHECK(runtime.LoadMod(mod, error));
    CHECK(error.empty());

    const std::string logs = capture.Str();
    CHECK(logs.find("boot pid=1") != std::string::npos);
    CHECK(logs.find("at 3.0,4.0,0") != std::string::npos);
    CHECK(logs.find("added=2") != std::string::npos);
    CHECK(logs.find("hp=80.0") != std::string::npos);
    CHECK(logs.find("zombies=1 handle0=true") != std::string::npos);
    CHECK(logs.find("z at 9,9") != std::string::npos);
    CHECK(logs.find("item=绷带 exists=false") != std::string::npos);
    CHECK(logs.find("spawnitem=true/false") != std::string::npos);
    CHECK(logs.find("passable=true") != std::string::npos);
    CHECK(logs.find("square=true/true/tile.floor.wood/wall.brick") !=
          std::string::npos);
    CHECK(logs.find("date=2000-7-9 tick=1") != std::string::npos);

    // 副作用落地：位置/背包/地面/僵尸
    const ecs::Position& position = ecs.Registry().get<ecs::Position>(player);
    CHECK(position.x == doctest::Approx(5.0f));
    CHECK(position.y == doctest::Approx(6.0f));
    CHECK(items::CountOf(ecs.Registry().get<ecs::Container>(player), "bandage") == 2);
    CHECK(game::CountGroundItems(ecs) == 1);
    CHECK(api.ZombieCount() == 1);
}

TEST_CASE("M7-T4：事件桥接端到端（Events.On/Off + SetTimer + 错误隔离）") {
    // 环境（同 M7-T3：玩家 + 注册表 + chunk；游戏时间可推进：1 游戏分钟 / tick）
    ecs::EcsWorld ecs;
    items::ItemRegistry items;
    std::string registryError;
    REQUIRE(items.LoadFromString(R"({
      "format": "items-v1",
      "items": [
        { "id": "bandage", "name": "绷带", "icon": "i", "weight": 0.1, "maxStack": 5, "type": "consumable" }
      ]
    })", "test", &registryError));
    core::GameLoop loop(60.0);
    core::GameTime gameTime;
    gameTime.Configure(60.0, core::DateTime{});
    ai::PathfindingService pathfinding(nullptr, ai::MapChunkBounds{0, 0, 4, 4});
    world::Chunk chunk(0, 0);
    pathfinding.OnChunkLoaded(chunk);

    const ecs::Entity player = ecs.Create();
    {
        auto& registry = ecs.Registry();
        registry.emplace<ecs::PlayerTag>(player);
        registry.emplace<ecs::Position>(player, ecs::Position{3.0f, 4.0f, 0});
        registry.emplace<ecs::Velocity>(player);
        registry.emplace<ecs::Health>(player, ecs::Health{100.0f, 100.0f});
        ecs::Container bag;
        bag.capacityKg = 20.0f;
        registry.emplace<ecs::Container>(player, bag);
    }

    game::GameContext ctx;
    ctx.ecs = &ecs;
    ctx.items = &items;
    ctx.loop = &loop;
    ctx.gameTime = &gameTime;
    ctx.pathfinding = &pathfinding;
    script::ScriptApi api;
    api.Attach(&ctx);

    core::EventBus bus;
    script::ScriptManager manager;
    {
        auto lua = std::make_unique<script::LuaRuntime>();
        lua->SetApi(&api);
        manager.RegisterRuntime(std::move(lua));
    }
    REQUIRE(manager.InitializeAll());
    manager.SetEventBus(&bus);
    script::EventBridge bridge(bus, manager);

    TempModRoot mods;
    std::string error;
    const script::ModInfo good = mods.MakeMod("good", R"lua(
Log.Info('t4 loaded')
Events.On("OnGameStart", function()
    Player.AddItem('bandage', 3)
end)
Events.On("OnTick", function(e)
    Log.Info('tick=' .. e.tick)
    if e.tick == 9 then
        Events.Off("OnTick")
    end
end)
Events.On("OnZombieSpawn", function(e)
    Log.Info('zspawn=' .. e.zombieId .. ' at ' .. string.format('%.0f,%.0f', e.x, e.y))
end)
Game.SetTimer(0, function() Log.Info('timer=now') end)
Game.SetTimer(30, function() Log.Info('timer=30s') end)
)lua");
    REQUIRE(manager.LoadMod(good, error));
    // bad mod：OnTick 抛错 → 被禁用（错误隔离；good 不受影响）
    REQUIRE(manager.LoadMod(
        mods.MakeMod("bad", "Events.On('OnTick', function(e) error('boom') end)"), error));
    LogCapture capture;

    auto dispatchTick = [&](std::int64_t tick) {
        bus.EmitQueued(core::kOnTick, tick);
        bus.FlushQueued();
    };

    // OnGameStart（立即派发）：脚本发 3 个绷带
    bus.Emit(core::kOnGameStart);
    CHECK(items::CountOf(ecs.Registry().get<ecs::Container>(player), "bandage") == 3);

    // OnTick：good 收到字段 tick；SetTimer(0) 在首个 tick 泵出；bad 报错被禁用
    dispatchTick(7);
    std::string logs = capture.Str();
    CHECK(logs.find("tick=7") != std::string::npos);
    CHECK(logs.find("timer=now") != std::string::npos);
    CHECK(manager.IsModDisabled("bad"));
    CHECK_FALSE(manager.IsModDisabled("good"));
    CHECK(logs.find("mod [bad]") != std::string::npos);
    CHECK(logs.find("boom") != std::string::npos);

    // OnZombieSpawn：工厂发事件（排队）→ 桥接 → 脚本（id=2：玩家之后第二个实体）
    const ecs::Entity zombie = game::SpawnZombie(ecs, nullptr, 12.0f, 12.0f, &bus);
    const bool zombieSpawned = zombie != entt::null;  // doctest 分解与 EnTT 比较符冲突 → 先归约
    REQUIRE(zombieSpawned);
    bus.FlushQueued();
    logs = capture.Str();
    CHECK(logs.find("zspawn=2 at 12,12") != std::string::npos);

    // SetTimer(30 游戏秒 = 0.5 分钟)：推进 1 分钟后下一 tick 泵出
    gameTime.Tick();
    dispatchTick(8);
    logs = capture.Str();
    CHECK(logs.find("timer=30s") != std::string::npos);
    CHECK(logs.find("tick=8") != std::string::npos);

    // Events.Off：tick 9 取消订阅 → tick 10 起不再派发
    dispatchTick(9);
    dispatchTick(10);
    logs = capture.Str();
    CHECK(logs.find("tick=9") != std::string::npos);
    CHECK(logs.find("tick=10") == std::string::npos);
}

TEST_CASE("M7-T5：Lua 源文件容忍 UTF-8 BOM（Windows 编辑器常见）") {
    TempModRoot mods;
    script::LuaRuntime runtime;
    std::string error;
    REQUIRE(runtime.Initialize(error));

    LogCapture capture;
    CHECK(runtime.LoadMod(mods.MakeMod("bom", "\xEF\xBB\xBFLog.Info('bom ok')"), error));
    CHECK(error.empty());
    CHECK(capture.Str().find("bom ok") != std::string::npos);
}

TEST_CASE("M7-T7：死循环防护（指令数超限 → 中止本次调用并按错误上报）") {
    TempModRoot mods;
    script::LuaRuntime runtime;
    std::string error;
    REQUIRE(runtime.Initialize(error));

    // 加载期死循环：LoadMod 失败并给出指令超限错误（不挂起游戏）
    CHECK_FALSE(runtime.LoadMod(mods.MakeMod("spin_load", "while true do end"), error));
    CHECK(error.find("指令数超限") != std::string::npos);

    // 派发期死循环：错误归因到该 mod（含 traceback），调用方继续可用
    REQUIRE(runtime.LoadMod(
        mods.MakeMod("spin_dispatch", "Events.On('OnTick', function(e) while true do end end)"),
        error));
    core::ScriptEvent event;
    event.name = std::string(core::kOnTick);
    event.fields.push_back(core::ScriptField{"tick", static_cast<std::int64_t>(1)});
    const std::vector<script::ModError> errors = runtime.DispatchEventToMod("spin_dispatch", event);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].modId == "spin_dispatch");
    CHECK(errors[0].message.find("指令数超限") != std::string::npos);
    CHECK(errors[0].message.find("stack traceback") != std::string::npos);

    // 预算按调用重置：正常 mod 的小循环（10 万次）不受影响
    REQUIRE(runtime.LoadMod(mods.MakeMod("calm",
        "Events.On('OnTick', function(e) local s = 0 for i = 1, 100000 do s = s + i end end)"),
        error));
    CHECK(runtime.DispatchEventToMod("calm", event).empty());
}
