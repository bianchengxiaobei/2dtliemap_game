#include <doctest/doctest.h>

#ifdef ZL_ENABLE_PYTHON

#include <cstdint>
#include <string>
#include <vector>

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
        return false;
    };
    int clicks = 0;
    f.ctx.playInventoryClick = [&clicks] { ++clicks; };
    runtime->SetApi(&f.api);

    zltest::TempModRoot mods;
    const script::ModInfo mod = mods.MakeModPy("py_inventory", R"py(
s = Inventory.GetState()
assert s['open'] is True and s['playerValid'] is True
assert len(s['bag']) == 25 and len(s['loot']) == 6
assert s['bag'][0]['label'] == '绷带'
assert s['bag'][0]['condition'] == 0.75
assert s['bag'][0]['damaged'] and s['bag'][0]['repairAvailable']
assert len(s['recipes']) == 1 and s['recipes'][0]['canCraft'] is True
assert s['recipes'][0]['estimatedSeconds'] == 1.5
assert s['recipes'][0]['known'] is True
assert s['recipes'][0]['station'] == 'workbench'
assert s['recipes'][0]['stationLabel'] == '工作台'
assert s['recipes'][0]['stationNearby'] is True
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

#else

TEST_CASE("M8-T3：Python 后端未编译（本构建跳过 Python API 用例）") {
    CHECK(true);
}

#endif  // ZL_ENABLE_PYTHON
