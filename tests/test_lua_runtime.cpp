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

#include "ai/PathfindingService.h"
#include "characters/components/CharacterComponents.h"
#include "core/GameLoop.h"
#include "core/GameTime.h"
#include "core/Log.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/GameContext.h"
#include "game/GroundItems.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"
#include "script/LuaRuntime.h"
#include "script/ScriptApi.h"
#include "world/Chunk.h"

using namespace zl;

namespace {

// 测试进程独立的临时 mod 根目录（自动清理）
class TempModRoot {
public:
    TempModRoot() {
        std::random_device rd;
        root = std::filesystem::temp_directory_path() / ("zl_lua_test_" + std::to_string(rd()));
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
    }
    ~TempModRoot() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    script::ModInfo MakeMod(const std::string& id, const std::string& source) {
        WriteLua(id, source);
        script::ModInfo mod;
        mod.id = id;
        mod.name = id;
        mod.version = "1.0.0";
        mod.backend = "lua";
        mod.rootPath = (root / id).string();
        mod.mainScript = "scripts/main.lua";
        return mod;
    }

    void WriteLua(const std::string& id, const std::string& source) {
        const std::filesystem::path dir = root / id / "scripts";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::ofstream out(dir / "main.lua", std::ios::binary | std::ios::trunc);
        out << source;
    }

    std::filesystem::path root;
};

// 捕获游戏日志输出（验证 print 重定向）
class LogCapture {
public:
    LogCapture() : m_sink(std::make_shared<spdlog::sinks::ostream_sink_mt>(m_stream)) {
        core::Log()->sinks().push_back(m_sink);
    }
    ~LogCapture() {
        auto& sinks = core::Log()->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), m_sink), sinks.end());
    }

    std::string Str() const { return m_stream.str(); }

private:
    std::ostringstream m_stream;
    std::shared_ptr<spdlog::sinks::ostream_sink_mt> m_sink;
};

}  // namespace

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

    CHECK(runtime.LoadedMods() == std::vector<std::string>{"safe"});
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
    CHECK(logs.find("date=2000-7-9 tick=1") != std::string::npos);

    // 副作用落地：位置/背包/地面/僵尸
    const ecs::Position& position = ecs.Registry().get<ecs::Position>(player);
    CHECK(position.x == doctest::Approx(5.0f));
    CHECK(position.y == doctest::Approx(6.0f));
    CHECK(items::CountOf(ecs.Registry().get<ecs::Container>(player), "bandage") == 2);
    CHECK(game::CountGroundItems(ecs) == 1);
    CHECK(api.ZombieCount() == 1);
}
