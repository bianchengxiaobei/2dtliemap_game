#include <doctest/doctest.h>

#ifdef ZL_ENABLE_PYTHON

#include <cstdint>
#include <string>
#include <vector>

#include "ai/PathfindingService.h"
#include "audio/SoundIds.h"
#include "audio/SoundManager.h"
#include "characters/components/CharacterComponents.h"
#include "characters/systems/CombatSystem.h"
#include "core/Config.h"
#include "core/EventBus.h"
#include "core/GameLoop.h"
#include "core/GameTime.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/GameContext.h"
#include "game/Missions.h"
#include "game/NetworkClient.h"
#include "game/ProgressionRules.h"
#include "items/InventoryOps.h"
#include "items/ItemRegistry.h"
#include "items/RecipeRegistry.h"
#include "net/Command.h"
#include "net/CommandPipeline.h"
#include "script/PythonRuntime.h"
#include "script/ScriptApi.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"
#include "world/World.h"
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
    items::RecipeRegistry recipes;
    core::Config config;
    game::MissionConfig missionConfig;
    game::ProgressionConfig progressionConfig =
        game::DefaultProgressionConfig();
    world::World world;
    core::GameLoop loop{60.0};
    core::GameTime gameTime;
    ai::PathfindingService pathfinding{nullptr, ai::MapChunkBounds{0, 0, 4, 4}};
    characters::CombatSystem combat;
    game::GameContext ctx;
    script::ScriptApi api;

    Fixture() {
        std::string error;
        CHECK(items.LoadFromString(kItemsJson, "test", &error));
        CHECK(recipes.LoadFromString(R"({
          "format": "recipes-v3",
          "recipes": [
            {
              "id": "base_bandage",
              "name": "基础绷带",
              "output": { "item": "bandage", "count": 1 },
              "ingredients": [ { "item": "bandage", "count": 1 } ]
            }
          ]
        })", "test-recipes", items, &error));
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
        missionConfig.defs.push_back(missionDef);
        gameTime.Configure(60.0, core::DateTime{});  // 1 tick = 1 游戏分钟（定时器测试用）
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
        world::GridSquare& square = world.GetOrCreateSquare(2, 3, 0);
        square.SetFloor(world::SquareObject{"tile.floor.wood", 0, 0, false});
        square.AddObject(world::SquareLayer::Wall,
                         world::SquareObject{"wall.brick", 0, 0, true});
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
    core::EventBus bus;
    f.ctx.events = &bus;
    const ecs::Entity player = f.MakePlayer(3.0f, 4.0f);
    entt::registry& registry = f.ecs.Registry();
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
    f.LoadOpenChunkWithWall();
    std::vector<net::Command> actionCommands;
    f.ctx.submitPlayerCommand = [&](const net::Command& command, bool& queued,
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
    f.ctx.buildCanPlace = [](const game::BuildableDef&, int x, int,
                             bool, std::string& error) {
        if (x < 0) {
            error = "非法格";
            return false;
        }
        return true;
    };
    f.ctx.buildPlace = [&](const game::BuildableDef& def, int, int,
                           bool vertical, std::string&) {
        buildPlaced = def.id;
        buildVertical = vertical;
        return true;
    };
    f.ctx.buildDemolish = [&](int x, int, std::string&) {
        buildDemolishX = x;
        return true;
    };
    f.ctx.buildRepair = [&](int x, int, std::string&) {
        buildRepairX = x;
        return true;
    };
    f.ctx.buildToggleDoor = [&](int x, int, std::string&) {
        buildDoorX = x;
        return true;
    };
    std::vector<core::SoundEvent> sounds;
    bus.Subscribe(core::kOnSound, [&](const core::Event& event) {
        if (const auto* sound = std::get_if<core::SoundEvent>(&event.payload)) {
            sounds.push_back(*sound);
        }
    });
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
sq = World.GetSquare(2, 3, 0)
assert sq['loaded'] is True and sq['hasFloor'] is True
assert sq['floor']['sprite'] == 'tile.floor.wood'
assert len(sq['wall']) == 1 and sq['wall'][0]['sprite'] == 'wall.brick'
assert World.GetSquare(50, 50) is None
assert Zombies.Count() == 0
h = Zombies.Spawn(10.0, 11.0, 0)
assert h != 0 and Zombies.Count() == 1
zp = Zombies.GetPosition(h)
assert abs(zp['x'] - 10.0) < 1e-6 and abs(zp['y'] - 11.0) < 1e-6
assert Items.Exists('bandage') is True
assert Items.GetName('bandage') == '绷带'
assert Game.GetApiVersion() == 2
assert Game.IsMultiplayer() is False
assert Game.GetNetMode() == 'direct'
n = Player.GetNeeds()
assert abs(n['hunger'] - 0.2) < 1e-6 and abs(n['health'] - 80.0) < 1e-6
inv = Player.GetInventory()
assert inv['totalStacks'] == 1 and inv['stacks'][0]['defId'] == 'bandage'
assert Player.GetEquipment() == {}
pr = Player.GetProgression()
assert pr['level'] == 2 and pr['skills']['melee']['level'] == 1
assert len(pr['perks']) == 1
info = Items.GetInfo('bandage')
assert info['type'] == 'consumable' and info['usable'] is False
recipeList = Craft.List()
assert recipeList['count'] == 1 and recipeList['recipes'][0]['known'] is True
assert recipeList['recipes'][0]['canCraft'] is True
assert Craft.IsKnown('base_bandage') is True
assert Craft.GetState()['active'] is True
assert Build.List()['count'] > 0
missions = Mission.List('all')
assert missions['count'] == 1 and missions['missions'][0]['status'] == 'active'
m = Mission.Get('m1')
assert m['id'] == 'm1' and m['progress'][0] == 1
w = World.GetWeather()
assert w['kind'] == 'clear'
assert 0.0 <= World.GetTimeOfDay() <= 1.0
me = Entities.Get(Game.GetPlayerId())
assert me['kind'] == 'player'
q = Entities.Query(7.5, 8.25, 0, 8)
assert q['count'] == 2 and q['entities'][0]['kind'] == 'player'
qSmall = Entities.Query(7.5, 8.25, 0, 8, 'any', 1)
assert qSmall['truncated'] is True
container = Entities.GetContainer(Game.GetPlayerId())
assert container['totalStacks'] == 1 and container['stacks'][0]['defId'] == 'bandage'
assert Zombies.Spawn(20.0, 20.0, 0, 'fast') != 0
assert Zombies.Spawn(21.0, 20.0, 0, 'strong') != 0
assert Zombies.Spawn(22.0, 20.0, 0, 'armored') != 0
assert Zombies.Spawn(23.0, 20.0, 0, 'boss') == 0
assert World.EmitSound(5.0, 6.0, 18.0, 'gunshot', 'py_shot') is True
assert World.EmitSound(5.0, 6.0, 0.0) is False
u = Player.UseItem(0)
assert u['ok'] is True and u['queued'] is False and u['error'] == ''
e = Player.EquipItem(0)
assert e['ok'] is True
un = Player.Unequip(0)
assert un['ok'] is True
d = Player.DropItem(0)
assert d['ok'] is True
pn = Player.PickupNearest()
assert pn['ok'] is True
p = Player.Pickup(123)
assert p['ok'] is True
a = Player.Attack(0, True)
assert a['ok'] is True
bad = Player.UseItem(99)
assert bad['ok'] is False and bad['error'] != ''
dmg = Player.ApplyDamage(5, 'debug')
assert dmg['ok'] is True
assert abs(Player.GetHealth() - 75.0) < 1e-6
dmgBad = Player.ApplyDamage(-1)
assert dmgBad['ok'] is False
adj = Player.AdjustNeeds(0.1, 0.2, 0.3)
assert adj['ok'] is True
n2 = Player.GetNeeds()
assert abs(n2['hunger'] - 0.3) < 1e-6
assert abs(n2['thirst'] - 0.5) < 1e-6
assert abs(n2['fatigue'] - 0.7) < 1e-6
xp = Player.GiveXp('melee', 500)
assert xp['ok'] is True and xp['characterLevels'] >= 1
assert xp['skillLevels'] >= 1 and xp['perkPoints'] >= 1
xpBad = Player.GiveXp('Melee', -1)
assert xpBad['ok'] is False
cs = Craft.Start('base_bandage', 2)
assert cs['ok'] is True and cs['queued'] is False
cc = Craft.Cancel()
assert cc['ok'] is True
cl = Craft.Learn('base_bandage')
assert cl['ok'] is False
bc = Build.CanPlace('barricade', 3, 4)
assert bc['ok'] is True
bp = Build.Place('door', 4, 4, True)
assert bp['ok'] is True and bp['queued'] is False
bd = Build.Demolish(4, 4)
assert bd['ok'] is True
br = Build.Repair(4, 4)
assert br['ok'] is True
bt = Build.ToggleDoor(4, 4)
assert bt['ok'] is True
bcBad = Build.CanPlace('missing', 4, 4)
assert bcBad['ok'] is False
Log.Info('api ok')
)py");
    std::string error;
    CHECK(runtime->LoadMod(mod, error));
    CHECK(error.empty());
    f.api.FlushQueryStats();
    CHECK(logs.Str().find("api ok") != std::string::npos);
    CHECK(logs.Str().find("[py_api] Entities.Query 截断") !=
          std::string::npos);
    CHECK(logs.Str().find("[py_api] 查询统计：tick=0 次数=2 截断=1") !=
          std::string::npos);

    bus.FlushQueued();
    REQUIRE(sounds.size() == 1);
    CHECK(sounds[0].x == doctest::Approx(5.0f));
    CHECK(sounds[0].y == doctest::Approx(6.0f));
    CHECK(sounds[0].radius == doctest::Approx(18.0f));
    CHECK(sounds[0].type == core::SoundType::Gunshot);
    CHECK(sounds[0].clip == "py_shot");
    CHECK(f.api.ZombieCount() == 4);
    int normal = 0;
    int fast = 0;
    int strong = 0;
    int armored = 0;
    f.ecs.Registry().view<ecs::ZombieVariant>().each(
        [&](entt::entity, const ecs::ZombieVariant& variant) {
            switch (variant.kind) {
                case ecs::ZombieVariantKind::Fast:
                    ++fast;
                    break;
                case ecs::ZombieVariantKind::Strong:
                    ++strong;
                    break;
                case ecs::ZombieVariantKind::Armored:
                    ++armored;
                    break;
                case ecs::ZombieVariantKind::Normal:
                    ++normal;
                    break;
            }
        });
    CHECK(normal == 1);
    CHECK(fast == 1);
    CHECK(strong == 1);
    CHECK(armored == 1);

    // C++ 侧确认脚本确实改到了游戏状态
    CHECK(items::CountOf(f.ecs.Registry().get<ecs::Container>(player), "bandage") == 2);
    const ecs::Position& position = f.ecs.Registry().get<ecs::Position>(player);
    CHECK(position.x == doctest::Approx(7.5f));
    CHECK(position.y == doctest::Approx(8.25f));
    CHECK(f.ecs.Registry().get<ecs::Health>(player).current ==
          doctest::Approx(75.0f));
    const ecs::Needs& finalNeeds = f.ecs.Registry().get<ecs::Needs>(player);
    CHECK(finalNeeds.hunger == doctest::Approx(0.3f));
    CHECK(finalNeeds.thirst == doctest::Approx(0.5f));
    CHECK(finalNeeds.fatigue == doctest::Approx(0.7f));
    CHECK(progression.level > 2);
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

TEST_CASE("M8-T3：Python Menu API 状态/动作/设置/键名") {
    script::PythonRuntime* runtime = SharedRuntimePtr();
    REQUIRE(runtime != nullptr);
    Fixture f;
    ui::MenuState menuState;
    menuState.open = true;
    menuState.pauseMode = true;
    menuState.canContinue = true;
    menuState.settings.volume = 42;
    menuState.settings.bindings.push_back(ui::MenuKeyBinding{"MoveUp", "向上移动", "W"});
    f.ctx.getMenuState = [&menuState](ui::MenuState& out) {
        out = menuState;
        return true;
    };
    bool acted = false;
    f.ctx.menuAction = [&acted](ui::MenuActionType action) {
        acted = action == ui::MenuActionType::Resume;
    };
    bool applied = false;
    f.ctx.applyMenuSettings = [&applied](const ui::MenuSettings& settings,
                                         std::string& error) {
        if (settings.volume != 55 || settings.bindings.size() != 1) {
            error = "bad settings";
            return false;
        }
        applied = true;
        return true;
    };
    runtime->SetApi(&f.api);

    zltest::TempModRoot mods;
    const script::ModInfo mod = mods.MakeModPy("py_menu", R"py(
m = Menu.GetState()
assert m['open'] is True and m['pauseMode'] is True
assert m['volume'] == 42 and len(m['bindings']) == 1
assert Menu.KeyName(34) == 'W'
assert Menu.Action('resume') is True
assert Menu.Action('unknown') is False
r = Menu.ApplySettings({
    'volume': 55, 'musicVolume': 60, 'sfxVolume': 70,
    'ambientVolume': 80, 'bindings': m['bindings']
})
assert r['ok'] is True and r['error'] == ''
)py");
    std::string error;
    CHECK(runtime->LoadMod(mod, error));
    CHECK(error.empty());
    CHECK(acted);
    CHECK(applied);
    runtime->UnloadMod("py_menu");
}

TEST_CASE("M15-T5：Python HUD 暴露操作提示字段") {
    script::PythonRuntime* runtime = SharedRuntimePtr();
    REQUIRE(runtime != nullptr);
    Fixture f;
    f.ctx.hudState.active = true;
    f.ctx.hudState.rangedValid = true;
    f.ctx.hudState.rangedName = "手枪";
    f.ctx.hudState.rangedLoaded = 0;
    f.ctx.hudState.rangedMagazine = 8;
    f.ctx.hudState.rangedReserve = 7;
    f.ctx.hudState.rangedReloading = true;
    f.ctx.hudState.rangedReloadProgress = 0.42f;
    f.ctx.hudState.rangedReloadRemaining = 1.16f;
    f.ctx.hudState.rangedStatus = "手枪 换弹中 42% · 0/8 · 备弹 7";
    f.ctx.hudState.rangedTone = "warn";
    f.ctx.hudState.combatStatus = "格挡 · 弹反窗口 0.2s";
    f.ctx.hudState.combatTone = "ok";
    f.ctx.hudState.stealthValid = true;
    f.ctx.hudState.stealthCrouched = true;
    f.ctx.hudState.stealthMoveSpeed = 2.2f;
    f.ctx.hudState.stealthFootstepRadius = 1.125f;
    f.ctx.hudState.stealthAwarenessValid = true;
    f.ctx.hudState.stealthNearbyAlertCount = 2;
    f.ctx.hudState.stealthBackstabAvailable = true;
    f.ctx.hudState.stealthStatus =
        "潜行 · 2.2 格/s · 脚步 1.1 格 · 附近警觉 2 · 可背刺";
    f.ctx.hudState.stealthTone = "warn";
    f.ctx.hudState.stealthPrompt = "背刺可用 · SPACE";
    f.ctx.hudState.trapValid = true;
    f.ctx.hudState.trapName = "捕兽夹";
    f.ctx.hudState.trapEffect = "55 伤害 · 硬直 2 秒";
    f.ctx.hudState.trapArmed = true;
    f.ctx.hudState.trapArming = false;
    f.ctx.hudState.trapArmRemaining = 0.0f;
    f.ctx.hudState.trapUses = 3;
    f.ctx.hudState.trapUsesMax = 4;
    f.ctx.hudState.trapCooldownRemaining = 0.8f;
    f.ctx.hudState.trapCooldownDuration = 2.0f;
    f.ctx.hudState.trapReadOnly = true;
    f.ctx.hudState.trapStatus =
        "捕兽夹 · 3/4 次 · 已布防 · 冷却 0.8s/2.0s · 仅所有者可操作";
    f.ctx.hudState.trapTone = "warn";
    f.ctx.hudState.forageValid = true;
    f.ctx.hudState.forageName = "森林";
    f.ctx.hudState.forageToolName = "树枝";
    f.ctx.hudState.forageMissingTool = true;
    f.ctx.hudState.forageExhausted = false;
    f.ctx.hudState.forageRespawnMinutes = 4.2;
    f.ctx.hudState.forageSeconds = 2.6f;
    f.ctx.hudState.forageSearching = false;
    f.ctx.hudState.forageProgress = 0.0f;
    f.ctx.hudState.forageStatus = "可觅食 · 森林（需要 树枝）";
    f.ctx.hudState.forageTone = "warn";
    f.ctx.hudState.fishingValid = true;
    f.ctx.hudState.fishingName = "静水";
    f.ctx.hudState.fishingRodName = "鱼竿";
    f.ctx.hudState.fishingBaitName = "鱼饵";
    f.ctx.hudState.fishingMissingRod = false;
    f.ctx.hudState.fishingMissingBait = true;
    f.ctx.hudState.fishingSeconds = 5.0f;
    f.ctx.hudState.fishingWaiting = false;
    f.ctx.hudState.fishingProgress = 0.0f;
    f.ctx.hudState.fishingBiting = false;
    f.ctx.hudState.fishingBiteRemaining = 0.0f;
    f.ctx.hudState.fishingStatus = "可钓鱼 · 静水（需要 鱼饵）";
    f.ctx.hudState.fishingTone = "warn";
    f.ctx.hudState.woundStatus = "流血：1 处伤口（0.8 HP/s）";
    f.ctx.hudState.woundTone = "warn";
    f.ctx.hudState.armorStatus = "护甲 爪 12% 咬 18% 钝 16% · 移速 96%";
    f.ctx.hudState.armorTone = "warn";
    f.ctx.hudState.infectionStatus = "感染：发热/疲劳 52%";
    f.ctx.hudState.infectionTone = "warn";
    f.ctx.hudState.weatherStatus = "天气：雨 13°C · 体温 35.8°C · 潮湿 62% · 室内";
    f.ctx.hudState.weatherTone = "warn";
    f.ctx.hudState.weatherClass = "rain";
    f.ctx.hudState.actionHint = "流血中：按 I 打开背包使用绷带";
    f.ctx.hudState.actionTone = "fail";
    f.ctx.hudState.baseActive = true;
    f.ctx.hudState.baseStorageCount = 2;
    f.ctx.hudState.baseStorageWeight = 12.5f;
    f.ctx.hudState.baseStorageCapacity = 100.0f;
    f.ctx.hudState.baseBeds = 1;
    f.ctx.hudState.baseCampfires = 2;
    f.ctx.hudState.baseWorkbenches = 3;
    f.ctx.hudState.baseDefensesIntact = 4;
    f.ctx.hudState.baseDefensesDamaged = 1;
    f.ctx.hudState.baseStatus = "基地 · 仓库 2（12.5/100.0kg）";
    f.ctx.hudState.baseTone = "warn";
    f.ctx.hudState.siegeReportOpen = true;
    f.ctx.hudState.siegeReportTitle = "守夜战报";
    f.ctx.hudState.siegeReportRating = "S";
    f.ctx.hudState.siegeReportTone = "ok";
    f.ctx.hudState.siegeReportHint = "ESC 关闭";
    f.ctx.hudState.siegeReportLines[0] = "损失 1 座栅栏";
    f.ctx.hudState.siegeReportLines[1] = "波次 3/3";
    f.ctx.hudState.poiGuide = "POI · 废弃加油站 [威胁低] 东 10格 · 清剿并搜索";
    f.ctx.hudState.poiGuideTone = "warn";
    f.ctx.hudState.missionGuide = "任务[电台] 无线电求救 · 搜索工具柜 0/1 · 奖励 40 生存经验";
    f.ctx.hudState.missionTone = "warn";
    f.ctx.hudState.eventGuide = "事件 · 被感染房屋 · 清理感染者 1/3 · 目标东南 38格";
    f.ctx.hudState.eventTone = "warn";
    f.ctx.hudState.regionGuide = "区域 · 村庄核心 · 最近路标 村庄避风港";
    f.ctx.hudState.regionTone = "info";
    f.ctx.hudState.settlementGuide =
        "聚落 · 村庄营地 · 声望 中立（0） · 服务 交易/委托/防御/招募 · 防御 正常";
    f.ctx.hudState.settlementTone = "info";
    f.ctx.hudState.strategicGuide =
        "战略争夺 · 河谷 · 守住归人类/失守沦陷 · M 地图支援";
    f.ctx.hudState.strategicTone = "warn";
    f.ctx.hudState.progressionValid = true;
    f.ctx.hudState.progressionLevel = 3;
    f.ctx.hudState.progressionXp = 42.0f;
    f.ctx.hudState.progressionXpNext = 120.0f;
    f.ctx.hudState.progressionPerkPoints = 1;
    f.ctx.hudState.progressionSkills = {"近战 Lv.2", "建造 Lv.1", "制作 Lv.0",
                                        "生存 Lv.1"};
    f.ctx.hudState.progressionPerks = "猛击、疾行";
    f.ctx.hudState.progressionChoices = {"1. 工匠 — 维修 +25%", "", ""};
    f.ctx.hudState.progressionChoiceCount = 1;
    f.ctx.hudState.perkPrompt = "可分配 Perk ×1";
    f.ctx.hudState.characterOpen = true;
    runtime->SetApi(&f.api);

    zltest::TempModRoot mods;
    const script::ModInfo mod = mods.MakeModPy("py_hud", R"py(
h = Hud.GetState()
assert h['active'] is True
assert Content.RegisterItems('{"format":"items-v1","items":[{"id":"py_dynamic","name":"Python 动态","icon":"i","weight":0.1,"maxStack":1,"type":"generic"}]}') == 1
assert Items.Exists('py_dynamic')
assert h['rangedValid'] is True
assert h['rangedName'] == '手枪'
assert h['rangedLoaded'] == 0
assert h['rangedMagazine'] == 8
assert h['rangedReserve'] == 7
assert h['rangedReloading'] is True
assert abs(h['rangedReloadProgress'] - 0.42) < 0.001
assert abs(h['rangedReloadRemaining'] - 1.16) < 0.001
assert h['rangedStatus'] == '手枪 换弹中 42% · 0/8 · 备弹 7'
assert h['rangedTone'] == 'warn'
assert h['combatStatus'] == '格挡 · 弹反窗口 0.2s'
assert h['combatTone'] == 'ok'
assert h['stealthValid'] is True
assert h['stealthCrouched'] is True
assert abs(h['stealthMoveSpeed'] - 2.2) < 0.001
assert abs(h['stealthFootstepRadius'] - 1.125) < 0.001
assert h['stealthAwarenessValid'] is True
assert h['stealthNearbyAlertCount'] == 2
assert h['stealthBackstabAvailable'] is True
assert h['stealthStatus'] == '潜行 · 2.2 格/s · 脚步 1.1 格 · 附近警觉 2 · 可背刺'
assert h['stealthTone'] == 'warn'
assert h['stealthPrompt'] == '背刺可用 · SPACE'
assert h['trapValid'] is True
assert h['trapName'] == '捕兽夹'
assert h['trapEffect'] == '55 伤害 · 硬直 2 秒'
assert h['trapArmed'] is True
assert h['trapArming'] is False
assert abs(h['trapArmRemaining']) < 0.001
assert h['trapUses'] == 3
assert h['trapUsesMax'] == 4
assert abs(h['trapCooldownRemaining'] - 0.8) < 0.001
assert abs(h['trapCooldownDuration'] - 2.0) < 0.001
assert h['trapReadOnly'] is True
assert h['trapStatus'] == '捕兽夹 · 3/4 次 · 已布防 · 冷却 0.8s/2.0s · 仅所有者可操作'
assert h['trapTone'] == 'warn'
assert h['forageValid'] is True
assert h['forageName'] == '森林'
assert h['forageToolName'] == '树枝'
assert h['forageMissingTool'] is True
assert h['forageExhausted'] is False
assert abs(h['forageRespawnMinutes'] - 4.2) < 0.001
assert abs(h['forageSeconds'] - 2.6) < 0.001
assert h['forageSearching'] is False
assert abs(h['forageProgress']) < 0.001
assert h['forageStatus'] == '可觅食 · 森林（需要 树枝）'
assert h['forageTone'] == 'warn'
assert h['fishingValid'] is True
assert h['fishingName'] == '静水'
assert h['fishingRodName'] == '鱼竿'
assert h['fishingBaitName'] == '鱼饵'
assert h['fishingMissingRod'] is False
assert h['fishingMissingBait'] is True
assert abs(h['fishingSeconds'] - 5.0) < 0.001
assert h['fishingWaiting'] is False
assert abs(h['fishingProgress']) < 0.001
assert h['fishingBiting'] is False
assert abs(h['fishingBiteRemaining']) < 0.001
assert h['fishingStatus'] == '可钓鱼 · 静水（需要 鱼饵）'
assert h['fishingTone'] == 'warn'
assert h['woundTone'] == 'warn'
assert h['armorStatus'] == '护甲 爪 12% 咬 18% 钝 16% · 移速 96%'
assert h['armorTone'] == 'warn'
assert h['infectionStatus'] == '感染：发热/疲劳 52%'
assert h['infectionTone'] == 'warn'
assert h["weatherStatus"] == "天气：雨 13°C · 体温 35.8°C · 潮湿 62% · 室内"
assert h["weatherTone"] == "warn"
assert h["weatherClass"] == "rain"
assert h['actionHint'] == '流血中：按 I 打开背包使用绷带'
assert h['actionTone'] == 'fail'
assert h['baseActive'] is True
assert h['baseStorageCount'] == 2
assert abs(h['baseStorageWeight'] - 12.5) < 0.001
assert abs(h['baseStorageCapacity'] - 100.0) < 0.001
assert h['baseBeds'] == 1
assert h['baseCampfires'] == 2
assert h['baseWorkbenches'] == 3
assert h['baseDefensesIntact'] == 4
assert h['baseDefensesDamaged'] == 1
assert h['baseStatus'] == '基地 · 仓库 2（12.5/100.0kg）'
assert h['baseTone'] == 'warn'
assert h['siegeReportOpen'] is True
assert h['siegeReportTitle'] == '守夜战报'
assert h['siegeReportRating'] == 'S'
assert h['siegeReportTone'] == 'ok'
assert h['siegeReportHint'] == 'ESC 关闭'
assert h['siegeReportLines'][0] == '损失 1 座栅栏'
assert h['siegeReportLines'][1] == '波次 3/3'
assert len(h['siegeReportLines']) == 8
assert h['poiGuide'] == 'POI · 废弃加油站 [威胁低] 东 10格 · 清剿并搜索'
assert h['poiGuideTone'] == 'warn'
assert h['missionGuide'] == '任务[电台] 无线电求救 · 搜索工具柜 0/1 · 奖励 40 生存经验'
assert h['missionTone'] == 'warn'
assert h['eventGuide'] == '事件 · 被感染房屋 · 清理感染者 1/3 · 目标东南 38格'
assert h['eventTone'] == 'warn'
assert h['regionGuide'] == '区域 · 村庄核心 · 最近路标 村庄避风港'
assert h['regionTone'] == 'info'
assert h['settlementGuide'] == '聚落 · 村庄营地 · 声望 中立（0） · 服务 交易/委托/防御/招募 · 防御 正常'
assert h['settlementTone'] == 'info'
assert h['strategicGuide'] == '战略争夺 · 河谷 · 守住归人类/失守沦陷 · M 地图支援'
assert h['strategicTone'] == 'warn'
assert h['progressionValid'] is True
assert h['progressionLevel'] == 3
assert abs(h['progressionXp'] - 42.0) < 0.001
assert abs(h['progressionXpNext'] - 120.0) < 0.001
assert h['progressionPerkPoints'] == 1
assert h['progressionSkills'][0] == '近战 Lv.2'
assert len(h['progressionSkills']) == 4
assert h['progressionPerks'] == '猛击、疾行'
assert h['progressionChoices'][0] == '1. 工匠 — 维修 +25%'
assert h['progressionChoiceCount'] == 1
assert h['perkPrompt'] == '可分配 Perk ×1'
assert h['characterOpen'] is True
assert len(h['modFields']) == 0
assert Hud.RegisterField('status', 'Python 状态')
assert Hud.RegisterField('status', 'Python 状态')
assert Hud.SetField('status', '运行中', 'ok')
assert Hud.SetField('missing', 'x') is False
assert Hud.SetField('status', 'x', 'bad') is False
fields = Hud.GetState()['modFields']
assert len(fields) == 1
assert fields[0]['modId'] == 'py_hud'
assert fields[0]['id'] == 'status'
assert fields[0]['label'] == 'Python 状态'
assert fields[0]['text'] == '运行中'
assert fields[0]['tone'] == 'ok'
assert Hud.ClearField('status')
assert Hud.GetState()['modFields'][0]['text'] == ''
assert Hud.SetField('status', '运行中', 'ok')
)py");
    std::string error;
    CHECK(runtime->LoadMod(mod, error));
    CHECK(error.empty());
    REQUIRE(f.ctx.modHudFields.size() == 1);
    CHECK(f.ctx.modHudFields[0].modId == "py_hud");
    CHECK(f.ctx.modHudFields[0].id == "status");
    CHECK(f.ctx.modHudFields[0].label == "Python 状态");
    CHECK(f.ctx.modHudFields[0].text == "运行中");
    CHECK(f.ctx.modHudFields[0].tone == "ok");
    CHECK(f.ctx.dynamicContentCount == 1);
    CHECK(f.items.Find("py_dynamic") != nullptr);
    runtime->UnloadMod("py_hud");
}

TEST_CASE("M8-T3：Python Inventory API 状态/动作/点击音效") {
    script::PythonRuntime* runtime = SharedRuntimePtr();
    REQUIRE(runtime != nullptr);
    Fixture f;
    ui::InventoryState source;
    source.open = true;
    source.playerValid = true;
    source.weight = 3.2f;
    source.capacity = 20.0f;
    source.bag[0] = ui::InventorySlot{true, "绷带", 2, true, false, 0};
    source.bag[0].condition = 0.75f;
    source.bag[0].damaged = true;
    source.bag[0].repairAvailable = true;
    source.loot[0] = ui::InventorySlot{true, "破布", 3, false, false, 77};
    source.recipes.push_back(ui::InventoryRecipe{
        "field_bandage", "简易绷带", "bandage", "绷带", 1, true, true,
        {{"破布", 2, 2}}, {{"锤子", 1, 1}}});
    source.recipes.back().estimatedSeconds = 1.5f;
    source.recipes.back().station = "workbench";
    source.recipes.back().stationLabel = "工作台";
    source.recipes.back().stationNearby = true;
    source.recipes.back().category = "cooking";
    source.recipes.back().categoryLabel = "烹饪";
    source.recipes.back().cookware =
        ui::InventoryRecipeIngredient{"锅", 1, 1};
    source.recipes.back().fuel =
        ui::InventoryRecipeIngredient{"木材", 1, 2};
    source.crafting.active = true;
    source.crafting.recipeId = "field_bandage";
    source.crafting.outputLabel = "绷带";
    source.crafting.outputCount = 1;
    source.crafting.elapsed = 0.75f;
    source.crafting.duration = 1.5f;
    source.crafting.progress = 0.5f;
    source.crafting.toolEfficiency = 0.75f;
    source.crafting.batchCount = 3;
    source.crafting.completedCount = 1;
    source.crafting.queue.push_back(
        ui::InventoryQueuedCraft{"makeshift_spear", "简易长矛", 1, 2});
    f.ctx.getInventoryState = [&source](ui::InventoryState& out) {
        out = source;
        return true;
    };
    bool transferActed = false;
    bool craftActed = false;
    bool cancelCraftActed = false;
    bool repairActed = false;
    bool queueUpActed = false;
    bool queueDownActed = false;
    bool queueRemoveActed = false;
    bool queueCountActed = false;
    bool storeAllActed = false;
    bool takeAllActed = false;
    bool sortStorageActed = false;
    int queueActionIndex = -1;
    int queueCountValue = 0;
    int craftBatchCount = 0;
    f.ctx.inventoryAction = [&](const ui::InventoryAction& action) {
        if (action.type == ui::InventoryActionType::Transfer &&
            action.fromIndex == 0 && action.toIndex == 0) {
            transferActed = true;
            return true;
        }
        if (action.type == ui::InventoryActionType::Craft && action.fromIndex == 0) {
            craftActed = true;
            craftBatchCount = action.count;
            return true;
        }
        if (action.type == ui::InventoryActionType::CancelCraft) {
            cancelCraftActed = true;
            return true;
        }
        if (action.type == ui::InventoryActionType::Repair &&
            action.fromIndex == 0) {
            repairActed = true;
            return true;
        }
        if (action.type == ui::InventoryActionType::QueueMoveUp &&
            action.fromIndex == 0) {
            queueUpActed = true;
            queueActionIndex = action.fromIndex;
            return true;
        }
        if (action.type == ui::InventoryActionType::QueueMoveDown &&
            action.fromIndex == 0) {
            queueDownActed = true;
            queueActionIndex = action.fromIndex;
            return true;
        }
        if (action.type == ui::InventoryActionType::QueueRemove &&
            action.fromIndex == 0) {
            queueRemoveActed = true;
            queueActionIndex = action.fromIndex;
            return true;
        }
        if (action.type == ui::InventoryActionType::QueueSetCount &&
            action.fromIndex == 0 && action.count == 7) {
            queueCountActed = true;
            queueActionIndex = action.fromIndex;
            queueCountValue = action.count;
            return true;
        }
        if (action.type == ui::InventoryActionType::StoreAll) {
            storeAllActed = true;
            return true;
        }
        if (action.type == ui::InventoryActionType::TakeAll) {
            takeAllActed = true;
            return true;
        }
        if (action.type == ui::InventoryActionType::SortStorage) {
            sortStorageActed = true;
            return true;
        }
        return false;
    };
    int clicks = 0;
    f.ctx.playInventoryClick = [&clicks] { ++clicks; };
    runtime->SetApi(&f.api);

    zltest::TempModRoot mods;
    const script::ModInfo mod = mods.MakeModPy("py_inventory", R"py(
s = Inventory.GetState()
assert s['open'] is True and s['playerValid'] is True
assert len(s['bag']) == 25 and len(s['loot']) == 8
assert s['bag'][0]['label'] == '绷带'
assert s['bag'][0]['condition'] == 0.75
assert s['bag'][0]['damaged'] and s['bag'][0]['repairAvailable']
assert len(s['recipes']) == 1 and s['recipes'][0]['canCraft'] is True
assert s['recipes'][0]['estimatedSeconds'] == 1.5
assert s['recipes'][0]['known'] is True
assert s['recipes'][0]['station'] == 'workbench'
assert s['recipes'][0]['stationLabel'] == '工作台'
assert s['recipes'][0]['stationNearby'] is True
assert s['recipes'][0]['category'] == 'cooking'
assert s['recipes'][0]['categoryLabel'] == '烹饪'
assert s['recipes'][0]['cookware']['label'] == '锅'
assert s['recipes'][0]['fuel']['label'] == '木材'
assert s['recipes'][0]['ingredients'][0]['label'] == '破布'
assert len(s['recipes'][0]['tools']) == 1 and s['recipes'][0]['tools'][0]['label'] == '锤子'
assert s['crafting']['active'] and s['crafting']['recipeId'] == 'field_bandage'
assert s['crafting']['outputLabel'] == '绷带' and s['crafting']['outputCount'] == 1
assert s['crafting']['elapsed'] == 0.75 and s['crafting']['duration'] == 1.5
assert s['crafting']['progress'] == 0.5
assert s['crafting']['toolEfficiency'] == 0.75
assert s['crafting']['batchCount'] == 3 and s['crafting']['completedCount'] == 1
assert len(s['crafting']['queue']) == 1
assert s['crafting']['queue'][0]['recipeId'] == 'makeshift_spear'
assert s['crafting']['queue'][0]['outputLabel'] == '简易长矛'
assert s['crafting']['queue'][0]['outputCount'] == 1 and s['crafting']['queue'][0]['count'] == 2
assert Inventory.Action({
    'type': 'transfer', 'from': 'bag', 'fromIndex': 0,
    'to': 'equipment', 'toIndex': 0
})
assert Inventory.Action({
    'type': 'craft', 'from': 'bag', 'fromIndex': 0, 'to': 'bag', 'toIndex': -1,
    'count': 3
})
assert Inventory.Action({'type': 'cancel_craft'})
assert Inventory.Action({'type': 'repair', 'from': 'bag', 'fromIndex': 0})
assert Inventory.Action({'type': 'queue_up', 'from': 'bag', 'fromIndex': 0})
assert Inventory.Action({'type': 'queue_down', 'from': 'bag', 'fromIndex': 0})
assert Inventory.Action({'type': 'queue_remove', 'from': 'bag', 'fromIndex': 0})
assert Inventory.Action({
    'type': 'queue_count', 'from': 'bag', 'fromIndex': 0, 'count': 7
})
assert Inventory.Action({'type': 'store_all', 'from': 'loot'})
assert Inventory.Action({'type': 'take_all', 'from': 'loot'})
assert Inventory.Action({'type': 'sort_storage', 'from': 'loot'})
assert Inventory.Action({'type': 'bogus'}) is False
Inventory.PlayClick()
)py");
    std::string error;
    CHECK(runtime->LoadMod(mod, error));
    CHECK(error.empty());
    CHECK(transferActed);
    CHECK(craftActed);
    CHECK(craftBatchCount == 3);
    CHECK(cancelCraftActed);
    CHECK(repairActed);
    CHECK(queueUpActed);
    CHECK(queueDownActed);
    CHECK(queueRemoveActed);
    CHECK(queueCountActed);
    CHECK(storeAllActed);
    CHECK(takeAllActed);
    CHECK(sortStorageActed);
    CHECK(queueActionIndex == 0);
    CHECK(queueCountValue == 7);
    CHECK(clicks == 1);
    runtime->UnloadMod("py_inventory");
}

TEST_CASE("Python Save API：列表、读档与快速保存") {
    script::PythonRuntime* runtime = SharedRuntimePtr();
    REQUIRE(runtime != nullptr);
    Fixture f;
    save::SaveMetadata entry;
    entry.id = "py-save";
    entry.name = "Python 存档";
    entry.mapId = "village_demo";
    f.ctx.listSaves = [&entry](std::vector<save::SaveMetadata>& out,
                               std::string& error) {
        out.assign(1, entry);
        error.clear();
        return true;
    };
    std::string loadedSave;
    f.ctx.loadSave = [&loadedSave](const std::string& id, std::string& error) {
        loadedSave = id;
        error.clear();
        return true;
    };
    int quickSaves = 0;
    f.ctx.quickSave = [&quickSaves] {
        ++quickSaves;
        return true;
    };
    std::string deletedSave;
    f.ctx.deleteSave = [&deletedSave](const std::string& id, std::string& error) {
        if (id != "py-save") {
            error = "不存在";
            return false;
        }
        deletedSave = id;
        return true;
    };
    runtime->SetApi(&f.api);

    zltest::TempModRoot mods;
    const script::ModInfo mod = mods.MakeModPy("py_save", R"py(
s = Save.List()
assert s['ok'] is True and len(s['saves']) == 1
assert s['saves'][0]['id'] == 'py-save'
assert s['saves'][0]['name'] == 'Python 存档'
assert Save.QuickSave() is True
assert Save.Load('py-save')['ok'] is True
assert Save.Delete('py-save')['ok'] is True
assert Save.Delete('missing')['ok'] is False
)py");
    std::string error;
    CHECK(runtime->LoadMod(mod, error));
    CHECK(error.empty());
    CHECK(quickSaves == 1);
    CHECK(loadedSave == "py-save");
    CHECK(deletedSave == "py-save");
    runtime->UnloadMod("py_save");
}

TEST_CASE("M35-F3-T7：Python 联机写 API fail-closed") {
    script::PythonRuntime* runtime = SharedRuntimePtr();
    REQUIRE(runtime != nullptr);
    Fixture f;
    const ecs::Entity player = f.MakePlayer(1.0f, 2.0f);
    entt::registry& registry = f.ecs.Registry();
    registry.emplace<ecs::Needs>(
        player, ecs::Needs{0.2f, 0.3f, 0.4f, 1.0f, 1.0f, 0.0f, 0});
    registry.emplace<ecs::Progression>(player);
    runtime->SetApi(&f.api);

    const auto scriptFor = [](const std::string& mode) {
        return std::string("assert Game.GetNetMode() == '") + mode + "'\n" + R"py(
assert Game.IsMultiplayer() is True
dmg = Player.ApplyDamage(5.0, 'debug')
adj = Player.AdjustNeeds(0.1)
xp = Player.GiveXp('melee', 10.0)
learn = Craft.Learn('base_bandage')
can = Build.CanPlace('barricade', 3, 4)
assert dmg['ok'] is False and '联机' in dmg['error']
assert adj['ok'] is False and '联机' in adj['error']
assert xp['ok'] is False and '联机' in xp['error']
assert learn['ok'] is False and '联机' in learn['error']
assert can['ok'] is False and '联机' in can['error']
assert World.EmitSound(1.0, 1.0, 10.0) is False
assert abs(Player.GetHealth() - 80.0) < 1e-6
Log.Info('py mp ok')
)py";
    };

    zltest::TempModRoot mods;
    zltest::LogCapture logs;
    net::CommandPipeline::Options loopOptions;
    loopOptions.mode = net::TransportMode::Loopback;
    net::CommandPipeline pipeline(loopOptions);
    f.ctx.commands = &pipeline;
    std::string error;
    CHECK(runtime->LoadMod(mods.MakeModPy("py_mp_loop", scriptFor("loopback")),
                           error));
    CHECK(error.empty());
    runtime->UnloadMod("py_mp_loop");

    f.ctx.commands = nullptr;
    game::NetworkClient client;
    f.ctx.networkClient = &client;
    CHECK(runtime->LoadMod(mods.MakeModPy("py_mp_client", scriptFor("client")),
                           error));
    CHECK(error.empty());
    runtime->UnloadMod("py_mp_client");

    CHECK(logs.Str().find("py mp ok") != std::string::npos);
    CHECK(registry.get<ecs::Health>(player).current == doctest::Approx(80.0f));
    CHECK(registry.get<ecs::Needs>(player).hunger ==
          doctest::Approx(0.2f));
    CHECK(registry.get<ecs::Progression>(player).xp ==
          doctest::Approx(0.0f));
}

TEST_CASE("M35-F5-T3：Python Audio 绑定、缺失 clip 与设备关闭") {
    script::PythonRuntime* runtime = SharedRuntimePtr();
    REQUIRE(runtime != nullptr);
    Fixture f;
    runtime->SetApi(&f.api);

    core::EventBus bus;
    audio::SoundManagerOptions options;
    options.engine.root = std::string(ZL_SOURCE_DIR) + "/assets/audio";
    options.engine.device = "null";
    audio::SoundManager sound;
    REQUIRE(sound.Init(bus, options));
    f.ctx.audio = &sound;

    zltest::TempModRoot mods;
    zltest::LogCapture logs;
    const script::ModInfo mod = mods.MakeModPy("py_audio", R"py(
assert Audio.Play2D('ui_click', 0.5) is True
assert Audio.Play2D('missing_clip') is False
assert Audio.Play2D('../ui_click') is False
assert Audio.Play3D('impact', 1.0, 2.0, 18.0, 0.5) is True
assert Audio.Play3D('impact', 1.0, 2.0) is True
assert Audio.Play3D('impact', 1.0, 2.0, -1.0) is False
assert Audio.Play3D('missing_clip', 1.0, 2.0, 18.0, 0.5) is False
print('py audio ok')
)py");
    std::string error;
    REQUIRE(runtime->LoadMod(mod, error));
    CHECK(logs.Str().find("py audio ok") != std::string::npos);
    runtime->UnloadMod("py_audio");

    audio::SoundManagerOptions offOptions;
    offOptions.engine.root = options.engine.root;
    offOptions.engine.device = "off";
    audio::SoundManager offSound;
    REQUIRE(offSound.Init(bus, offOptions));
    f.ctx.audio = &offSound;
    const script::ModInfo offMod = mods.MakeModPy("py_audio_off", R"py(
assert Audio.Play2D('ui_click') is False
assert Audio.Play3D('impact', 1.0, 2.0, 18.0) is False
print('py audio off ok')
)py");
    REQUIRE(runtime->LoadMod(offMod, error));
    CHECK(logs.Str().find("py audio off ok") != std::string::npos);
    runtime->UnloadMod("py_audio_off");
}

TEST_CASE("M35-F4-T2：Python Mod.Data 绑定、热重载与新世界") {
    script::PythonRuntime* runtime = SharedRuntimePtr();
    REQUIRE(runtime != nullptr);
    Fixture f;
    runtime->SetApi(&f.api);

    zltest::TempModRoot mods;
    zltest::LogCapture logs;
    const script::ModInfo mod = mods.MakeModPy("py_mod_data", R"py(
assert Mod.Id() == 'py_mod_data'
assert Mod.Data.Clear() is True
assert Mod.Data.Get('missing') is None
assert Mod.Data.Set('b', '2') is True
assert Mod.Data.Set('a', '1') is True
assert Mod.Data.Set('a', '3') is True
assert Mod.Data.Set('empty', '') is True
assert Mod.Data.Get('empty') == ''
assert Mod.Data.Set('keep', 'ok') is True
assert Mod.Data.Set('score', '1') is True
assert Mod.Data.Set('non_string', 123) is False
assert Mod.Data.Remove('b') is True
assert Mod.Data.Remove('b') is False
keys = Mod.Data.Keys()
assert keys == ['a', 'empty', 'keep', 'score']
print('py mod.data init')
)py");
    std::string error;
    REQUIRE(runtime->LoadMod(mod, error));
    CHECK(f.api.GetModData("py_mod_data", "score").value() == "1");
    CHECK(f.api.GetModData("py_mod_data", "empty").value().empty());
    CHECK(f.api.ListModDataKeys("py_mod_data") ==
          std::vector<std::string>{"a", "empty", "keep", "score"});

    mods.WritePy("py_mod_data", R"py(
assert Mod.Id() == 'py_mod_data'
assert Mod.Data.Get('score') == '1'
assert Mod.Data.Set('score', '2') is True
assert Mod.Data.Remove('keep') is True
assert Mod.Data.Remove('keep') is False
keys = Mod.Data.Keys()
assert keys == ['a', 'empty', 'score']
print('py mod.data reload')
)py");
    CAPTURE(error);
    REQUIRE(runtime->ReloadMod("py_mod_data", error));
    CHECK(f.api.GetModData("py_mod_data", "score").value() == "2");
    CHECK(f.api.ListModDataKeys("py_mod_data") ==
          std::vector<std::string>{"a", "empty", "score"});

    ecs::EcsWorld fresh;
    game::GameContext freshCtx;
    freshCtx.ecs = &fresh;
    script::ScriptApi freshApi;
    freshApi.Attach(&freshCtx);
    runtime->SetApi(&freshApi);
    mods.WritePy("py_mod_data", R"py(
assert Mod.Data.Get('score') is None
assert Mod.Data.Keys() == []
assert Mod.Data.Set('score', 'fresh') is True
print('py mod.data fresh')
)py");
    CAPTURE(error);
    REQUIRE(runtime->ReloadMod("py_mod_data", error));
    CHECK(freshApi.GetModData("py_mod_data", "score").value() == "fresh");
    CHECK(f.api.GetModData("py_mod_data", "score").value() == "2");
    const std::string logText = logs.Str();
    CHECK(logText.find("py mod.data init") != std::string::npos);
    CHECK(logText.find("py mod.data reload") != std::string::npos);
    CHECK(logText.find("py mod.data fresh") != std::string::npos);
    runtime->UnloadMod("py_mod_data");
}

TEST_CASE("M35-F4-T2：Python Mod.Data 联机拒绝且只警告一次") {
    script::PythonRuntime* runtime = SharedRuntimePtr();
    REQUIRE(runtime != nullptr);
    Fixture f;
    net::CommandPipeline::Options options;
    options.mode = net::TransportMode::Loopback;
    net::CommandPipeline loop(options);
    f.ctx.commands = &loop;
    runtime->SetApi(&f.api);

    zltest::TempModRoot mods;
    zltest::LogCapture logs;
    const script::ModInfo mod = mods.MakeModPy("py_mod_data_mp", R"py(
assert Mod.Id() == 'py_mod_data_mp'
assert Mod.Data.Get('k') is None
assert Mod.Data.Set('k', 'v') is False
assert Mod.Data.Set('x', 1) is False
assert Mod.Data.Remove('k') is False
assert Mod.Data.Clear() is False
assert Mod.Data.Keys() == []
print('py mod.data mp ok')
)py");
    std::string error;
    REQUIRE(runtime->LoadMod(mod, error));
    CHECK(f.api.GetModData("py_mod_data_mp", "k") == std::nullopt);
    const std::string logText = logs.Str();
    CHECK(logText.find("py mod.data mp ok") != std::string::npos);
    const std::string warning = "Mod.Data 在联机模式下不可用";
    const std::size_t first = logText.find(warning);
    REQUIRE(first != std::string::npos);
    CHECK(logText.find(warning, first + 1) == std::string::npos);
    runtime->UnloadMod("py_mod_data_mp");
}

TEST_CASE("M35-F4-T3：Python Json 编解码、空表/数组/Null 与限制") {
    script::PythonRuntime* runtime = SharedRuntimePtr();
    REQUIRE(runtime != nullptr);
    Fixture f;
    runtime->SetApi(&f.api);

    zltest::TempModRoot mods;
    zltest::LogCapture logs;
    const script::ModInfo mod = mods.MakeModPy("json_py", R"py(
assert Json.Encode({}) == '{}'
assert Json.Encode(Json.Array()) == '[]'
assert Json.Encode(Json.Null()) == 'null'
assert Json.Encode(None) == 'null'
assert Json.Encode('中文') == '"中文"'
assert Json.Encode(True) == 'true'
assert Json.Encode(1) == '1'
assert Json.Encode(1.0) == '1.0'
assert Json.Encode(1.5) == '1.5'
assert Json.Encode({'b': 2, 'a': 1}) == '{"a":1,"b":2}'
assert Json.Encode([1, 2.5, True, '中文']) == '[1,2.5,true,"中文"]'

value = Json.Decode('{"a":[1,2.5,true,null,"中文"],"empty":[],"obj":{}}')
assert isinstance(value, dict)
assert value['a'][0] == 1 and value['a'][1] == 2.5 and value['a'][2] is True
assert value['a'][3] is None and value['a'][4] == '中文'
assert Json.Encode(value['a'][3]) == 'null'
assert Json.Encode(value['empty']) == '[]'
assert Json.Encode(value['obj']) == '{}'
assert Json.Encode(value) == '{"a":[1,2.5,true,null,"中文"],"empty":[],"obj":{}}'
assert Json.Encode(Json.Decode('null')) == 'null'
assert Json.Encode(Json.Decode('[]')) == '[]'
assert Json.Decode('{bad') is None
assert Json.Decode(123) is None
assert Json.Decode('NaN') is None

assert Json.Encode({1.5: 'x'}) is None
cycle = {}
cycle['self'] = cycle
assert Json.Encode(cycle) is None
deep = 0
for _ in range(16):
    deep = [deep]
assert Json.Encode(deep) is not None
deep = [deep]
assert Json.Encode(deep) is None
assert Json.Encode(list(range(1025))) is None
assert Json.Encode('x' * 65537) is None
assert Json.Encode(float('nan')) is None
assert Json.Encode(float('inf')) is None
assert Json.Decode('[' * 17 + '0' + ']' * 17) is None
assert Json.Decode('"' + 'x' * 65537 + '"') is None
assert Json.Decode('[' + ','.join(['0'] * 1025) + ']') is None
print('py json ok')
)py");
    std::string error;
    REQUIRE(runtime->LoadMod(mod, error));
    const std::string logText = logs.Str();
    CHECK(logText.find("py json ok") != std::string::npos);
    CHECK(logText.find("Json.Encode 检测到循环引用") != std::string::npos);
    runtime->UnloadMod("json_py");
}

#else

TEST_CASE("M8-T3：Python 后端未编译（本构建跳过 Python API 用例）") {
    CHECK(true);
}

#endif  // ZL_ENABLE_PYTHON
