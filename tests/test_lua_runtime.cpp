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
#include "audio/SoundIds.h"
#include "audio/SoundManager.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/systems/CombatSystem.h"
#include "core/Config.h"
#include "core/EventBus.h"
#include "core/GameLoop.h"
#include "core/GameTime.h"
#include "core/Log.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/GameContext.h"
#include "game/GroundItems.h"
#include "game/Missions.h"
#include "game/NetworkClient.h"
#include "game/ProgressionRules.h"
#include "game/ZombieFactory.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"
#include "items/RecipeRegistry.h"
#include "net/Command.h"
#include "net/CommandPipeline.h"
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

TEST_CASE("M7-T2：沙箱——白名单可用，os/io/dofile 不可用，require 受限") {
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
                                " ' i=' .. tostring(io) .. ' r=' .. type(require) .."
                                " ' lf=' .. tostring(loadfile) .. ' l=' .. tostring(load) .."
                                " ' s=' .. tostring(type(string)))"),
        error));
    CHECK(error.empty());
    CHECK(capture.Str().find("d=nil p=nil i=nil r=function lf=nil l=nil s=table") !=
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
    items::RecipeRegistry recipes;
    REQUIRE(recipes.LoadFromString(R"({
      "format": "recipes-v3",
      "recipes": [
        {
          "id": "base_bandage",
          "name": "基础绷带",
          "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "bandage", "count": 1 } ]
        }
      ]
    })", "test-recipes", items, &registryError));
    core::Config config;
    game::MissionConfig missionConfig;
    game::ProgressionConfig progressionConfig =
        game::DefaultProgressionConfig();
    missionConfig.enabled = true;
    game::MissionDef missionDef;
    missionDef.id = "m1";
    missionDef.name = "测试任务";
    missionDef.chain = "main";
    missionDef.order = 1;
    missionDef.kind = game::MissionKind::Once;
    missionDef.minDay = 1;
    missionDef.objectives.push_back(game::MissionObjectiveDef{
        game::MissionObjectiveKind::CollectItem, "bandage", 1, 0.0f, 0.0f,
        1.5f, "收集绷带"});
    missionDef.reward.xp = 10.0f;
    missionConfig.defs.push_back(missionDef);
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
        registry.emplace<ecs::Needs>(
            player, ecs::Needs{0.2f, 0.3f, 0.4f, 1.0f, 1.0f, 0.0f, 1});
        auto& progression = registry.emplace<ecs::Progression>(player);
        progression.level = 2;
        progression.skills[ecs::SkillIndex(ecs::SkillKind::Melee)] = {1, 10.0f};
        progression.perks = {"athletic"};
        ecs::MissionLog missionLog;
        missionLog.missions.push_back(ecs::MissionRunState{
            "m1", {1}, ecs::MissionRunStatus::Active, 2, false, ""});
        registry.emplace<ecs::MissionLog>(player, std::move(missionLog));
        ecs::CraftingJob craftJob;
        craftJob.active = true;
        craftJob.recipeId = "base_bandage";
        registry.emplace<ecs::CraftingJob>(player, std::move(craftJob));
    }

    game::GameContext ctx;
    core::EventBus bus;
    characters::CombatSystem combat;
    ctx.events = &bus;
    ctx.config = &config;
    ctx.ecs = &ecs;
    ctx.items = &items;
    ctx.recipes = &recipes;
    ctx.missionConfig = &missionConfig;
    ctx.progressionConfig = &progressionConfig;
    ctx.world = &world;
    ctx.loop = &loop;
    ctx.gameTime = &gameTime;
    ctx.pathfinding = &pathfinding;
    ctx.combat = &combat;
    std::vector<net::Command> actionCommands;
    ctx.submitPlayerCommand = [&](const net::Command& command, bool& queued,
                                  std::string&) {
        actionCommands.push_back(command);
        queued = false;
        return true;
    };
    std::string buildPlaced;
    bool buildVertical = false;
    int buildDemolishX = -1;
    int buildRepairX = -1;
    int buildDoorX = -1;
    ctx.buildCanPlace = [](const game::BuildableDef&, int x, int,
                           bool, std::string& error) {
        if (x < 0) {
            error = "非法格";
            return false;
        }
        return true;
    };
    ctx.buildPlace = [&](const game::BuildableDef& def, int, int,
                         bool vertical, std::string&) {
        buildPlaced = def.id;
        buildVertical = vertical;
        return true;
    };
    ctx.buildDemolish = [&](int x, int, std::string&) {
        buildDemolishX = x;
        return true;
    };
    ctx.buildRepair = [&](int x, int, std::string&) {
        buildRepairX = x;
        return true;
    };
    ctx.buildToggleDoor = [&](int x, int, std::string&) {
        buildDoorX = x;
        return true;
    };
    script::ScriptApi api;
    api.Attach(&ctx);

    TempModRoot mods;
    script::LuaRuntime runtime;
    runtime.SetApi(&api);
    std::string error;
    REQUIRE(runtime.Initialize(error));

    std::vector<core::SoundEvent> sounds;
    bus.Subscribe(core::kOnSound, [&](const core::Event& event) {
        if (const auto* sound = std::get_if<core::SoundEvent>(&event.payload)) {
            sounds.push_back(*sound);
        }
    });
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
Log.Info('api=' .. Game.GetApiVersion() .. '/mp=' .. tostring(Game.IsMultiplayer()) .. '/' .. Game.GetNetMode())
local n = Player.GetNeeds()
Log.Info(string.format('needs=%.1f/%.1f', n.hunger, n.health))
local inv = Player.GetInventory()
Log.Info('inv=' .. inv.totalStacks .. '/' .. #inv.stacks .. '/' .. inv.stacks[1].defId)
Log.Info('equip=' .. tostring(Player.GetEquipment() ~= nil))
local pr = Player.GetProgression()
Log.Info('prog=' .. pr.level .. '/' .. pr.skills.melee.level .. '/' .. #pr.perks)
local info = Items.GetInfo('bandage')
Log.Info('info=' .. info.type .. '/' .. tostring(info.usable))
local list = Craft.List()
Log.Info('craft=' .. list.count .. '/' .. tostring(list.recipes[1].known) .. '/' .. tostring(list.recipes[1].canCraft))
Log.Info('craftKnown=' .. tostring(Craft.IsKnown('base_bandage')) .. '/' .. tostring(Craft.GetState().active))
local builds = Build.List()
Log.Info('builds=' .. tostring(builds.count > 0))
local missions = Mission.List('all')
Log.Info('missions=' .. missions.count .. '/' .. missions.missions[1].status)
local m = Mission.Get('m1')
Log.Info('mission=' .. m.id .. '/' .. m.progress[1])
local w = World.GetWeather()
Log.Info('weather=' .. w.kind .. '/' .. tostring(w.isNight))
Log.Info(string.format('tod=%.2f', World.GetTimeOfDay()))
local me = Entities.Get(Game.GetPlayerId())
local q = Entities.Query(5, 6, 0, 8)
local qt = Entities.Query(5, 6, 0, 8, 'any', 1)
Log.Info('entities=' .. me.kind .. '/' .. q.count .. '/' .. q.entities[1].kind)
Log.Info('trunc=' .. tostring(qt.truncated))
local container = Entities.GetContainer(Game.GetPlayerId())
Log.Info('container=' .. container.totalStacks .. '/' .. container.stacks[1].defId)
local vf = Zombies.Spawn(20, 20, 0, 'fast')
local vs = Zombies.Spawn(21, 20, 0, 'strong')
local va = Zombies.Spawn(22, 20, 0, 'armored')
Log.Info('variants=' .. tostring(vf ~= 0) .. '/' .. tostring(vs ~= 0) .. '/' .. tostring(va ~= 0))
local snd = World.EmitSound(5, 6, 18, 'gunshot', 'script_shot')
Log.Info('sound=' .. tostring(snd))
local u = Player.UseItem(0)
Log.Info('act.use=' .. tostring(u.ok) .. '/' .. tostring(u.queued) .. '/' .. u.error)
local e = Player.EquipItem(0)
Log.Info('act.equip=' .. tostring(e.ok))
local un = Player.Unequip(0)
Log.Info('act.unequip=' .. tostring(un.ok))
local d = Player.DropItem(0)
Log.Info('act.drop=' .. tostring(d.ok))
local pn = Player.PickupNearest()
Log.Info('act.pickupNearest=' .. tostring(pn.ok))
local p = Player.Pickup(123)
Log.Info('act.pickup=' .. tostring(p.ok))
local a = Player.Attack(0, true)
Log.Info('act.attack=' .. tostring(a.ok))
local bad = Player.UseItem(99)
Log.Info('act.bad=' .. tostring(bad.ok) .. '/' .. bad.error)
local dmg = Player.ApplyDamage(5, 'debug')
Log.Info(string.format('act.damage=%s/%.0f', tostring(dmg.ok), Player.GetHealth()))
local dmgBad = Player.ApplyDamage(-1)
Log.Info('act.damageBad=' .. tostring(dmgBad.ok))
local adj = Player.AdjustNeeds(0.1, 0.2, 0.3)
local n2 = Player.GetNeeds()
Log.Info(string.format('act.needs=%s/%.1f/%.1f/%.1f', tostring(adj.ok), n2.hunger, n2.thirst, n2.fatigue))
local xp = Player.GiveXp('melee', 500)
Log.Info('act.xp=' .. tostring(xp.ok) .. '/' .. xp.characterLevels .. '/' .. xp.skillLevels .. '/' .. xp.perkPoints)
local cs = Craft.Start('base_bandage', 2)
Log.Info('act.craftStart=' .. tostring(cs.ok) .. '/' .. tostring(cs.queued))
local cc = Craft.Cancel()
Log.Info('act.craftCancel=' .. tostring(cc.ok))
local cl = Craft.Learn('base_bandage')
Log.Info('act.craftLearn=' .. tostring(cl.ok))
local bc = Build.CanPlace('barricade', 3, 4)
Log.Info('act.buildCanPlace=' .. tostring(bc.ok))
local bp = Build.Place('door', 4, 4, true)
Log.Info('act.buildPlace=' .. tostring(bp.ok) .. '/' .. tostring(bp.queued))
local bd = Build.Demolish(4, 4)
Log.Info('act.buildDemolish=' .. tostring(bd.ok))
local br = Build.Repair(4, 4)
Log.Info('act.buildRepair=' .. tostring(br.ok))
local bt = Build.ToggleDoor(4, 4)
Log.Info('act.buildDoor=' .. tostring(bt.ok))
local bcBad = Build.CanPlace('missing', 4, 4)
Log.Info('act.buildCanPlaceBad=' .. tostring(bcBad.ok))
)lua");
    CHECK(runtime.LoadMod(mod, error));
    CHECK(error.empty());
    api.FlushQueryStats();

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
    CHECK(logs.find("api=2/mp=false/direct") != std::string::npos);
    CHECK(logs.find("needs=0.2/80.0") != std::string::npos);
    CHECK(logs.find("inv=1/1/bandage") != std::string::npos);
    CHECK(logs.find("equip=true") != std::string::npos);
    CHECK(logs.find("prog=2/1/1") != std::string::npos);
    CHECK(logs.find("info=consumable/false") != std::string::npos);
    CHECK(logs.find("craft=1/true/true") != std::string::npos);
    CHECK(logs.find("craftKnown=true/true") != std::string::npos);
    CHECK(logs.find("builds=true") != std::string::npos);
    CHECK(logs.find("missions=1/active") != std::string::npos);
    CHECK(logs.find("mission=m1/1") != std::string::npos);
    CHECK(logs.find("weather=clear/true") != std::string::npos);
    CHECK(logs.find("tod=0.00") != std::string::npos);
    CHECK(logs.find("entities=player/3/player") != std::string::npos);
    CHECK(logs.find("trunc=true") != std::string::npos);
    CHECK(logs.find("[apitest] Entities.Query 截断") != std::string::npos);
    CHECK(logs.find("[apitest] 查询统计：tick=1 次数=2 截断=1") !=
          std::string::npos);
    CHECK(logs.find("container=1/bandage") != std::string::npos);
    CHECK(logs.find("variants=true/true/true") != std::string::npos);
    CHECK(logs.find("sound=true") != std::string::npos);
    bus.FlushQueued();
    REQUIRE(sounds.size() == 1);
    CHECK(sounds[0].x == doctest::Approx(5.0f));
    CHECK(sounds[0].y == doctest::Approx(6.0f));
    CHECK(sounds[0].radius == doctest::Approx(18.0f));
    CHECK(sounds[0].type == core::SoundType::Gunshot);
    CHECK(sounds[0].clip == "script_shot");
    CHECK(logs.find("act.use=true/false/") != std::string::npos);
    CHECK(logs.find("act.equip=true") != std::string::npos);
    CHECK(logs.find("act.unequip=true") != std::string::npos);
    CHECK(logs.find("act.drop=true") != std::string::npos);
    CHECK(logs.find("act.pickupNearest=true") != std::string::npos);
    CHECK(logs.find("act.pickup=true") != std::string::npos);
    CHECK(logs.find("act.attack=true") != std::string::npos);
    CHECK(logs.find("act.bad=false/") != std::string::npos);
    CHECK(logs.find("act.damage=true/75") != std::string::npos);
    CHECK(logs.find("act.damageBad=false") != std::string::npos);
    CHECK(logs.find("act.needs=true/0.3/0.5/0.7") != std::string::npos);
    CHECK(logs.find("act.xp=true/") != std::string::npos);
    CHECK(logs.find("act.craftStart=true/false") != std::string::npos);
    CHECK(logs.find("act.craftCancel=true") != std::string::npos);
    CHECK(logs.find("act.craftLearn=false") != std::string::npos);
    CHECK(logs.find("act.buildCanPlace=true") != std::string::npos);
    CHECK(logs.find("act.buildPlace=true/false") != std::string::npos);
    CHECK(logs.find("act.buildDemolish=true") != std::string::npos);
    CHECK(logs.find("act.buildRepair=true") != std::string::npos);
    CHECK(logs.find("act.buildDoor=true") != std::string::npos);
    CHECK(logs.find("act.buildCanPlaceBad=false") != std::string::npos);
    CHECK(buildPlaced == "door");
    CHECK(buildVertical);
    CHECK(buildDemolishX == 4);
    CHECK(buildRepairX == 4);
    CHECK(buildDoorX == 4);
    REQUIRE(actionCommands.size() == 9);
    CHECK(actionCommands[0].type == net::CommandType::UseBag);
    CHECK(actionCommands[0].payload.index == 0);
    CHECK(actionCommands[1].type == net::CommandType::EquipBag);
    CHECK(actionCommands[1].payload.targetIndex == 0);
    CHECK(actionCommands[2].type == net::CommandType::Unequip);
    CHECK(actionCommands[3].type == net::CommandType::DropBag);
    CHECK(actionCommands[4].type == net::CommandType::PickupNearest);
    CHECK(actionCommands[5].type == net::CommandType::PickupEntity);
    CHECK(actionCommands[5].payload.entityId == 123);
    CHECK(actionCommands[6].type == net::CommandType::Attack);
    CHECK((actionCommands[6].payload.flags & net::kAttackHeavyFlag) != 0);
    CHECK(actionCommands[7].type == net::CommandType::InventoryAction);
    CHECK(static_cast<ui::InventoryActionType>(
              actionCommands[7].payload.flags & 0xFFu) ==
          ui::InventoryActionType::Craft);
    CHECK((actionCommands[7].payload.flags >> 8) == 2);
    CHECK(actionCommands[7].payload.index == 0);
    CHECK(actionCommands[8].type == net::CommandType::InventoryAction);
    CHECK(static_cast<ui::InventoryActionType>(
              actionCommands[8].payload.flags & 0xFFu) ==
          ui::InventoryActionType::CancelCraft);

    // 副作用落地：位置/背包/地面/僵尸
    const ecs::Position& position = ecs.Registry().get<ecs::Position>(player);
    CHECK(position.x == doctest::Approx(5.0f));
    CHECK(position.y == doctest::Approx(6.0f));
    CHECK(items::CountOf(ecs.Registry().get<ecs::Container>(player), "bandage") == 2);
    CHECK(game::CountGroundItems(ecs) == 1);
    CHECK(api.ZombieCount() == 4);
    CHECK(ecs.Registry().get<ecs::Health>(player).current == doctest::Approx(75.0f));
    const ecs::Needs& finalNeeds = ecs.Registry().get<ecs::Needs>(player);
    CHECK(finalNeeds.hunger == doctest::Approx(0.3f));
    CHECK(finalNeeds.thirst == doctest::Approx(0.5f));
    CHECK(finalNeeds.fatigue == doctest::Approx(0.7f));
    CHECK(ecs.Registry().get<ecs::Progression>(player).level > 2);
}

TEST_CASE("M35-F3-T7：Lua 联机写 API fail-closed") {
    ecs::EcsWorld ecs;
    core::GameLoop loop(60.0);
    game::GameContext ctx;
    ctx.ecs = &ecs;
    ctx.loop = &loop;
    const ecs::Entity player = ecs.Create();
    ecs.Registry().emplace<ecs::PlayerTag>(player);
    ecs.Registry().emplace<ecs::Health>(player, ecs::Health{80.0f, 100.0f});
    ecs.Registry().emplace<ecs::Needs>(
        player, ecs::Needs{0.2f, 0.3f, 0.4f, 1.0f, 1.0f, 0.0f, 0});

    script::ScriptApi api;
    api.Attach(&ctx);
    script::LuaRuntime runtime;
    runtime.SetApi(&api);
    std::string error;
    REQUIRE(runtime.Initialize(error));

    const std::string scriptSource = R"lua(
Log.Info('mp=' .. tostring(Game.IsMultiplayer()) .. '/' .. Game.GetNetMode())
local dmg = Player.ApplyDamage(5, 'debug')
local adj = Player.AdjustNeeds(0.1)
local xp = Player.GiveXp('melee', 10)
local learn = Craft.Learn('base_bandage')
local can = Build.CanPlace('barricade', 3, 4)
local snd = World.EmitSound(1, 1, 10)
Log.Info('mp.block=' .. tostring(dmg.ok) .. '/' .. tostring(adj.ok) .. '/' ..
         tostring(xp.ok) .. '/' .. tostring(learn.ok) .. '/' ..
         tostring(can.ok) .. '/' .. tostring(snd))
Log.Info(string.format('mp.health=%.0f', Player.GetHealth()))
)lua";

    TempModRoot mods;
    LogCapture capture;
    net::CommandPipeline::Options loopOptions;
    loopOptions.mode = net::TransportMode::Loopback;
    net::CommandPipeline pipeline(loopOptions);
    ctx.commands = &pipeline;
    CHECK(runtime.LoadMod(mods.MakeMod("mp_loop", scriptSource), error));
    CHECK(error.empty());

    ctx.commands = nullptr;
    game::NetworkClient client;
    ctx.networkClient = &client;
    CHECK(runtime.LoadMod(mods.MakeMod("mp_client", scriptSource), error));
    CHECK(error.empty());

    const std::string logs = capture.Str();
    CHECK(logs.find("mp=true/loopback") != std::string::npos);
    CHECK(logs.find("mp=true/client") != std::string::npos);
    CHECK(logs.find("mp.block=false/false/false/false/false/false") !=
          std::string::npos);
    CHECK(logs.find("mp.health=80") != std::string::npos);
    CHECK(ecs.Registry().get<ecs::Health>(player).current ==
          doctest::Approx(80.0f));
    CHECK(ecs.Registry().get<ecs::Needs>(player).hunger ==
          doctest::Approx(0.2f));
}

TEST_CASE("M35-F5-T3：Lua Audio 绑定、缺失 clip 与设备关闭") {
    ecs::EcsWorld ecs;
    game::GameContext ctx;
    ctx.ecs = &ecs;
    core::EventBus bus;
    audio::SoundManagerOptions options;
    options.engine.root = std::string(ZL_SOURCE_DIR) + "/assets/audio";
    options.engine.device = "null";
    audio::SoundManager sound;
    REQUIRE(sound.Init(bus, options));
    ctx.audio = &sound;

    script::ScriptApi api;
    api.Attach(&ctx);
    script::LuaRuntime runtime;
    runtime.SetApi(&api);
    std::string error;
    REQUIRE(runtime.Initialize(error));

    TempModRoot mods;
    LogCapture capture;
    const script::ModInfo mod = mods.MakeMod("audio_lua", R"lua(
assert(Audio.Play2D('ui_click', 0.5) == true)
assert(Audio.Play2D('missing_clip') == false)
assert(Audio.Play2D('../ui_click') == false)
assert(Audio.Play3D('impact', 1, 2, 18, 0.5) == true)
assert(Audio.Play3D('impact', 1, 2) == true)
assert(Audio.Play3D('impact', 1, 2, -1) == false)
assert(Audio.Play3D('missing_clip', 1, 2, 18, 0.5) == false)
Log.Info('lua audio ok')
)lua");
    REQUIRE(runtime.LoadMod(mod, error));
    CHECK(capture.Str().find("lua audio ok") != std::string::npos);

    audio::SoundManagerOptions offOptions;
    offOptions.engine.root = options.engine.root;
    offOptions.engine.device = "off";
    audio::SoundManager offSound;
    REQUIRE(offSound.Init(bus, offOptions));
    ctx.audio = &offSound;
    const script::ModInfo offMod = mods.MakeMod("audio_lua_off", R"lua(
assert(Audio.Play2D('ui_click') == false)
assert(Audio.Play3D('impact', 1, 2, 18) == false)
Log.Info('lua audio off ok')
)lua");
    REQUIRE(runtime.LoadMod(offMod, error));
    CHECK(capture.Str().find("lua audio off ok") != std::string::npos);
    runtime.UnloadMod("audio_lua");
    runtime.UnloadMod("audio_lua_off");
}

TEST_CASE("M35-F4-T2：Lua Mod.Data 绑定、热重载与新世界") {
    ecs::EcsWorld ecs;
    game::GameContext ctx;
    ctx.ecs = &ecs;
    script::ScriptApi api;
    api.Attach(&ctx);
    script::LuaRuntime runtime;
    runtime.SetApi(&api);
    std::string error;
    REQUIRE(runtime.Initialize(error));

    TempModRoot mods;
    LogCapture capture;
    const script::ModInfo mod = mods.MakeMod("mod_data_lua", R"lua(
assert(Mod.Id() == 'mod_data_lua')
assert(Mod.Data.Clear())
assert(Mod.Data.Get('missing') == nil)
assert(Mod.Data.Set('b', '2'))
assert(Mod.Data.Set('a', '1'))
assert(Mod.Data.Set('a', '3'))
assert(Mod.Data.Set('empty', ''))
assert(Mod.Data.Get('empty') == '')
assert(Mod.Data.Set('keep', 'ok'))
assert(Mod.Data.Set('score', '1'))
assert(Mod.Data.Set('non_string', 123) == false)
assert(Mod.Data.Remove('b'))
assert(Mod.Data.Remove('b') == false)
local keys = Mod.Data.Keys()
assert(#keys == 4)
assert(keys[1] == 'a' and keys[2] == 'empty' and
       keys[3] == 'keep' and keys[4] == 'score')
Log.Info('lua mod.data init')
)lua");
    REQUIRE(runtime.LoadMod(mod, error));
    CHECK(api.GetModData("mod_data_lua", "score").value() == "1");
    CHECK(api.GetModData("mod_data_lua", "empty").value().empty());
    CHECK(api.ListModDataKeys("mod_data_lua") ==
          std::vector<std::string>{"a", "empty", "keep", "score"});

    mods.WriteLua("mod_data_lua", R"lua(
assert(Mod.Id() == 'mod_data_lua')
assert(Mod.Data.Get('score') == '1')
assert(Mod.Data.Set('score', '2'))
assert(Mod.Data.Remove('keep'))
assert(Mod.Data.Remove('keep') == false)
local keys = Mod.Data.Keys()
assert(#keys == 3)
assert(keys[1] == 'a' and keys[2] == 'empty' and keys[3] == 'score')
Log.Info('lua mod.data reload')
)lua");
    CAPTURE(error);
    REQUIRE(runtime.ReloadMod("mod_data_lua", error));
    CHECK(api.GetModData("mod_data_lua", "score").value() == "2");
    CHECK(api.ListModDataKeys("mod_data_lua") ==
          std::vector<std::string>{"a", "empty", "score"});

    ecs::EcsWorld fresh;
    game::GameContext freshCtx;
    freshCtx.ecs = &fresh;
    script::ScriptApi freshApi;
    freshApi.Attach(&freshCtx);
    runtime.SetApi(&freshApi);
    mods.WriteLua("mod_data_lua", R"lua(
assert(Mod.Data.Get('score') == nil)
assert(#Mod.Data.Keys() == 0)
assert(Mod.Data.Set('score', 'fresh'))
Log.Info('lua mod.data fresh')
)lua");
    CAPTURE(error);
    REQUIRE(runtime.ReloadMod("mod_data_lua", error));
    CHECK(freshApi.GetModData("mod_data_lua", "score").value() == "fresh");
    CHECK(api.GetModData("mod_data_lua", "score").value() == "2");
    const std::string logs = capture.Str();
    CHECK(logs.find("lua mod.data init") != std::string::npos);
    CHECK(logs.find("lua mod.data reload") != std::string::npos);
    CHECK(logs.find("lua mod.data fresh") != std::string::npos);
    runtime.UnloadMod("mod_data_lua");
}

TEST_CASE("M35-F4-T2：Lua Mod.Data 联机拒绝且只警告一次") {
    ecs::EcsWorld ecs;
    game::GameContext ctx;
    ctx.ecs = &ecs;
    net::CommandPipeline::Options options;
    options.mode = net::TransportMode::Loopback;
    net::CommandPipeline loop(options);
    ctx.commands = &loop;
    script::ScriptApi api;
    api.Attach(&ctx);
    script::LuaRuntime runtime;
    runtime.SetApi(&api);
    std::string error;
    REQUIRE(runtime.Initialize(error));

    TempModRoot mods;
    LogCapture capture;
    const script::ModInfo mod = mods.MakeMod("mod_data_mp", R"lua(
assert(Mod.Id() == 'mod_data_mp')
assert(Mod.Data.Get('k') == nil)
assert(Mod.Data.Set('k', 'v') == false)
assert(Mod.Data.Set('x', 1) == false)
assert(Mod.Data.Remove('k') == false)
assert(Mod.Data.Clear() == false)
assert(#Mod.Data.Keys() == 0)
Log.Info('lua mod.data mp ok')
)lua");
    REQUIRE(runtime.LoadMod(mod, error));
    CHECK(api.GetModData("mod_data_mp", "k") == std::nullopt);
    const std::string logs = capture.Str();
    CHECK(logs.find("lua mod.data mp ok") != std::string::npos);
    const std::string warning = "Mod.Data 在联机模式下不可用";
    const std::size_t first = logs.find(warning);
    REQUIRE(first != std::string::npos);
    CHECK(logs.find(warning, first + 1) == std::string::npos);
    runtime.UnloadMod("mod_data_mp");
}

TEST_CASE("M35-F4-T3：Lua Json 编解码、空表/数组/Null 与限制") {
    ecs::EcsWorld ecs;
    game::GameContext ctx;
    ctx.ecs = &ecs;
    script::ScriptApi api;
    api.Attach(&ctx);
    script::LuaRuntime runtime;
    runtime.SetApi(&api);
    std::string error;
    REQUIRE(runtime.Initialize(error));

    TempModRoot mods;
    LogCapture capture;
    const script::ModInfo mod = mods.MakeMod("json_lua", R"lua(
local function eq(actual, expected)
    assert(actual == expected, tostring(actual) .. ' ~= ' .. tostring(expected))
end

eq(Json.Encode({}), '{}')
eq(Json.Encode(Json.Array()), '[]')
eq(Json.Encode(Json.Null()), 'null')
eq(Json.Encode(nil), 'null')
eq(Json.Encode('中文'), '"中文"')
eq(Json.Encode(true), 'true')
eq(Json.Encode(1), '1')
eq(Json.Encode(1.0), '1.0')
eq(Json.Encode(1.5), '1.5')
eq(Json.Encode({ b = 2, a = 1 }), '{"a":1,"b":2}')
eq(Json.Encode({ 1, 2.5, true, '中文' }), '[1,2.5,true,"中文"]')

local value = assert(Json.Decode(
    '{"a":[1,2.5,true,null,"中文"],"empty":[],"obj":{}}'))
assert(type(value) == 'table')
assert(value.a[1] == 1 and value.a[2] == 2.5 and value.a[3] == true)
assert(value.a[4] ~= nil and value.a[5] == '中文')
eq(Json.Encode(value.a[4]), 'null')
eq(Json.Encode(value.empty), '[]')
eq(Json.Encode(value.obj), '{}')
eq(Json.Encode(value),
   '{"a":[1,2.5,true,null,"中文"],"empty":[],"obj":{}}')
eq(Json.Encode(Json.Decode('null')), 'null')
eq(Json.Encode(Json.Decode('[]')), '[]')
eq(Json.Decode('{bad'), nil)
eq(Json.Decode(123), nil)

assert(Json.Encode({ [1.5] = 'x' }) == nil)
assert(Json.Encode({ [1] = 'x', foo = 'y' }) == nil)
local cycle = {}
cycle.self = cycle
assert(Json.Encode(cycle) == nil)
local deep = 0
for _ = 1, 16 do deep = { deep } end
assert(Json.Encode(deep) ~= nil)
deep = { deep }
assert(Json.Encode(deep) == nil)
local items = {}
for i = 1, 1025 do items[i] = i end
assert(Json.Encode(items) == nil)
assert(Json.Encode(string.rep('x', 65537)) == nil)
assert(Json.Encode(0 / 0) == nil)
local nested = string.rep('[', 17) .. '0' .. string.rep(']', 17)
assert(Json.Decode(nested) == nil)
assert(Json.Decode('"' .. string.rep('x', 65537) .. '"') == nil)
assert(Json.Decode('[' .. string.rep('0,', 1024) .. '0]') == nil)
Log.Info('lua json ok')
)lua");
    CAPTURE(error);
    REQUIRE(runtime.LoadMod(mod, error));
    const std::string logs = capture.Str();
    CHECK(logs.find("lua json ok") != std::string::npos);
    CHECK(logs.find("Json.Encode 检测到循环引用") != std::string::npos);
    runtime.UnloadMod("json_lua");
}

TEST_CASE("M35-F4-T4：Lua require 多文件、缓存与相对路径") {
    TempModRoot mods;
    script::LuaRuntime runtime;
    std::string error;
    REQUIRE(runtime.Initialize(error));

    LogCapture capture;
    const script::ModInfo mod = mods.MakeMod("require_basic", R"lua(
local util = require("util")
local utilAgain = require("util")
local utilLua = require("util.lua")
local director = require("wave.director")
local localMod = require("./local")
local flag = require("flag")
Log.Info(string.format("req %s/%s/%s/%s/%s/%s/%s",
    util.name, director.name, localMod.name,
    tostring(util == utilAgain), tostring(util == utilLua),
    tostring(flag), tostring(loadCount)))
)lua");
    mods.WriteLuaFile("require_basic", "scripts/util.lua", R"lua(
loadCount = (loadCount or 0) + 1
assert(Log ~= nil and Game ~= nil)
return { name = "util:" .. loadCount }
)lua");
    mods.WriteLuaFile("require_basic", "scripts/local.lua",
                      "return { name = 'local' }");
    mods.WriteLuaFile("require_basic", "scripts/flag.lua", "flagSeen = true\n");
    mods.WriteLuaFile("require_basic", "scripts/wave/director.lua", R"lua(
local util = require("util")
local sibling = require("./sibling")
return { name = "director:" .. util.name .. ":" .. sibling.name }
)lua");
    mods.WriteLuaFile("require_basic", "scripts/wave/sibling.lua",
                      "return { name = 'sibling' }");

    CAPTURE(error);
    REQUIRE(runtime.LoadMod(mod, error));
    const std::string logs = capture.Str();
    CHECK(logs.find("req util:1/director:util:1:sibling/local/true/true/true/1") !=
          std::string::npos);
    runtime.UnloadMod("require_basic");
}

TEST_CASE("M35-F4-T4：Lua require 路径沙箱、循环/深度/大小与错误行号") {
    TempModRoot mods;
    script::LuaRuntime runtime;
    std::string error;
    REQUIRE(runtime.Initialize(error));

    auto loadError = [&](const script::ModInfo& mod) {
        std::string message;
        const bool loaded = runtime.LoadMod(mod, message);
        CHECK_FALSE(loaded);
        return message;
    };

    std::string message =
        loadError(mods.MakeMod("require_type", "require(123)"));
    CHECK(message.find("模块名必须是字符串") != std::string::npos);
    message = loadError(mods.MakeMod("require_empty", "require('')"));
    CHECK(message.find("模块名不能为空") != std::string::npos);
    message = loadError(mods.MakeMod("require_parent", "require('../escape')"));
    CHECK(message.find("非法模块名") != std::string::npos);
    message = loadError(mods.MakeMod("require_abs", "require('/tmp/escape')"));
    CHECK(message.find("非法模块名") != std::string::npos);
    message = loadError(mods.MakeMod("require_abs_win", "require('C:/Windows/win.ini')"));
    CHECK(message.find("非法模块名") != std::string::npos);

    const script::ModInfo cycle = mods.MakeMod("require_cycle", "require('a')");
    mods.WriteLuaFile("require_cycle", "scripts/a.lua", "return require('b')");
    mods.WriteLuaFile("require_cycle", "scripts/b.lua", "return require('a')");
    message = loadError(cycle);
    CHECK(message.find("循环依赖") != std::string::npos);

    const script::ModInfo depth = mods.MakeMod("require_depth", "require('m0')");
    for (int i = 0; i < 32; ++i) {
        mods.WriteLuaFile("require_depth", "scripts/m" + std::to_string(i) + ".lua",
                          "return require('m" + std::to_string(i + 1) + "')");
    }
    mods.WriteLuaFile("require_depth", "scripts/m32.lua", "return 'ok'");
    message = loadError(depth);
    CHECK(message.find("递归深度超过 32") != std::string::npos);

    const script::ModInfo big = mods.MakeMod("require_big", "require('big')");
    std::string bigSource = "return 1\n";
    bigSource.append(1024 * 1024, ' ');
    mods.WriteLuaFile("require_big", "scripts/big.lua", bigSource);
    message = loadError(big);
    CHECK(message.find("超过 1 MB") != std::string::npos);

    const script::ModInfo badRuntime =
        mods.MakeMod("require_bad_runtime", "require('bad_runtime')");
    mods.WriteLuaFile("require_bad_runtime", "scripts/bad_runtime.lua",
                      "local x = 1\nerror('模块炸了')\n");
    message = loadError(badRuntime);
    CHECK(message.find("bad_runtime.lua:2") != std::string::npos);
    CHECK(message.find("模块炸了") != std::string::npos);
    CHECK(message.find("stack traceback") != std::string::npos);

    const script::ModInfo badSyntax =
        mods.MakeMod("require_bad_syntax", "require('bad_syntax')");
    mods.WriteLuaFile("require_bad_syntax", "scripts/bad_syntax.lua",
                      "local x = ( **invalid** )\n");
    message = loadError(badSyntax);
    CHECK(message.find("bad_syntax.lua:1") != std::string::npos);

    const script::ModInfo symlink =
        mods.MakeMod("require_symlink", "require('escape_link')");
    mods.WriteLuaFile("require_symlink", "outside.lua", "return 'outside'");
    const std::filesystem::path target =
        mods.root / "require_symlink" / "outside.lua";
    const std::filesystem::path link =
        mods.root / "require_symlink" / "scripts" / "escape_link.lua";
    std::error_code ec;
    std::filesystem::create_symlink(target, link, ec);
    if (!ec) {
        message = loadError(symlink);
        CHECK(message.find("模块越界") != std::string::npos);
    }
}

TEST_CASE("M35-F4-T4：Lua require 热重载清缓存与每 mod 隔离") {
    TempModRoot mods;
    script::LuaRuntime runtime;
    std::string error;
    REQUIRE(runtime.Initialize(error));

    LogCapture capture;
    const script::ModInfo reload =
        mods.MakeMod("require_reload", "local m = require('util')\n"
                                       "Log.Info('reload=' .. m.version)\n");
    mods.WriteLuaFile("require_reload", "scripts/util.lua",
                      "return { version = 'v1' }");
    REQUIRE(runtime.LoadMod(reload, error));
    CHECK(capture.Str().find("reload=v1") != std::string::npos);

    mods.WriteLuaFile("require_reload", "scripts/util.lua",
                      "return { version = 'v2' }");
    REQUIRE(runtime.ReloadMod("require_reload", error));
    CHECK(capture.Str().find("reload=v2") != std::string::npos);

    const script::ModInfo isoA =
        mods.MakeMod("require_iso_a", "local m = require('shared')\n"
                                      "Log.Info('isoA=' .. m.value)\n");
    const script::ModInfo isoB =
        mods.MakeMod("require_iso_b", "local m = require('shared')\n"
                                      "Log.Info('isoB=' .. m.value)\n");
    mods.WriteLuaFile("require_iso_a", "scripts/shared.lua",
                      "return { value = 'a' }");
    mods.WriteLuaFile("require_iso_b", "scripts/shared.lua",
                      "return { value = 'b' }");
    REQUIRE(runtime.LoadMod(isoA, error));
    REQUIRE(runtime.LoadMod(isoB, error));
    CHECK(capture.Str().find("isoA=a") != std::string::npos);
    CHECK(capture.Str().find("isoB=b") != std::string::npos);

    runtime.UnloadMod("require_reload");
    runtime.UnloadMod("require_iso_a");
    runtime.UnloadMod("require_iso_b");
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
