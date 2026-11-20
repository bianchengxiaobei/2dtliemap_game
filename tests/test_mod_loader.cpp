#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "ScriptTestHelpers.h"
#include "core/EventBus.h"
#include "script/EventBridge.h"
#include "script/LuaRuntime.h"
#include "script/ModLoader.h"
#include "script/ScriptApi.h"
#include "script/ScriptManager.h"

using namespace zl;
using zltest::LogCapture;
using zltest::TempModRoot;

TEST_CASE("M7-T5：mod.json 解析与校验") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zl_mod_dir" / "hello_mod";
    script::ModInfo info;
    std::string error;

    // 完整字段
    REQUIRE(script::ModLoader::ParseModInfo(R"({
      "id": "hello_mod", "name": "Hello", "version": "1.2.3",
      "api": 1, "backend": "lua", "main": "scripts/main.lua", "loadOrder": 42
    })", dir, "lua", info, error));
    CHECK(info.id == "hello_mod");
    CHECK(info.name == "Hello");
    CHECK(info.version == "1.2.3");
    CHECK(info.apiVersion == 1);
    CHECK(info.backend == "lua");
    CHECK(info.mainScript == "scripts/main.lua");
    CHECK(info.loadOrder == 42);
    CHECK(info.rootPath == dir.string());

    // 缺省：id=目录名、name=id、api=当前、backend=调用方默认、main/loadOrder 默认
    REQUIRE(script::ModLoader::ParseModInfo("{}", dir, "lua", info, error));
    CHECK(info.id == "hello_mod");
    CHECK(info.name == "hello_mod");
    CHECK(info.version == "0.0.0");
    CHECK(info.apiVersion == 2);
    CHECK(info.backend == "lua");
    CHECK(info.mainScript == "scripts/main.lua");
    CHECK(info.loadOrder == 100);

    // id 与目录名不一致：以 id 为准（仅告警）
    REQUIRE(script::ModLoader::ParseModInfo(R"({"id": "other"})", dir, "lua", info, error));
    CHECK(info.id == "other");

    // 旧 api 版本：尽力加载
    REQUIRE(script::ModLoader::ParseModInfo(R"({"api": 0})", dir, "lua", info, error));
    CHECK(info.apiVersion == 0);

    // api 过新 → 拒绝
    CHECK_FALSE(script::ModLoader::ParseModInfo(R"({"api": 99})", dir, "lua", info, error));
    CHECK(error.find("API") != std::string::npos);

    // JSON 坏 / 顶层非对象 / 字段类型不符 → 拒绝
    CHECK_FALSE(script::ModLoader::ParseModInfo("{oops", dir, "lua", info, error));
    CHECK(error.find("解析失败") != std::string::npos);
    CHECK_FALSE(script::ModLoader::ParseModInfo("[1, 2]", dir, "lua", info, error));
    CHECK_FALSE(script::ModLoader::ParseModInfo(R"({"loadOrder": "fast"})", dir, "lua", info,
                                                error));
    CHECK(error.find("类型") != std::string::npos);
}

TEST_CASE("M35-F4-T5：v1 mod 旧 API 回归与 API 版本告警") {
    script::ScriptApi api;
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
    mods.WriteManifest("legacy_v1",
                       R"({"id": "legacy_v1", "api": 1, "backend": "lua"})");
    mods.WriteLua("legacy_v1", R"lua(
Log.Info('v1 loaded api=' .. Game.GetApiVersion())
Events.On('OnTick', function(e)
    Log.Info('v1 tick=' .. e.tick)
end)
)lua");

    LogCapture capture;
    script::ModLoader loader(manager, "lua");
    const script::ModLoader::Result result = loader.ScanAndLoad(mods.root);
    CHECK(result.loaded == 1);
    CHECK(manager.IsModLoaded("legacy_v1"));

    std::string logs = capture.Str();
    CHECK(logs.find("v1 loaded api=2") != std::string::npos);
    CHECK(logs.find("API") != std::string::npos);

    bus.EmitQueued(core::kOnTick, static_cast<std::int64_t>(3));
    bus.FlushQueued();
    logs = capture.Str();
    CHECK(logs.find("v1 tick=3") != std::string::npos);
}

TEST_CASE("M7-T5：mods 目录扫描（失败隔离 + 加载顺序）") {
    // 最小脚本环境（脚本只打日志/订阅事件；ScriptApi 未 Attach → API 调用安全默认值）
    script::ScriptApi api;
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
    // alpha：loadOrder 90（后派发）
    mods.WriteManifest("alpha",
                       R"({"id": "alpha", "name": "Alpha", "version": "1.0.0", "api": 1,)"
                       R"( "backend": "lua", "main": "scripts/main.lua", "loadOrder": 90})");
    mods.WriteLua("alpha", "Events.On('OnTick', function() Log.Info('tick=alpha') end)");
    // zeta：loadOrder 10（先派发；目录名反而靠后）
    mods.WriteManifest("zeta", R"({"id": "zeta", "loadOrder": 10})");
    mods.WriteLua("zeta", "Events.On('OnTick', function() Log.Info('tick=zeta') end)");
    // 缺 id 清单：id 回退目录名
    mods.WriteManifest("nameless", "{}");
    mods.WriteLua("nameless", "Log.Info('nameless loaded')");
    // 以下四个均应被跳过：无清单 / 坏清单 / api 过新 / 后端不可用
    mods.WriteLua("no_manifest", "Log.Info('never1')");
    mods.WriteManifest("bad_json", "{oops");
    mods.WriteManifest("too_new", R"({"id": "too_new", "api": 99})");
    mods.WriteLua("too_new", "Log.Info('never2')");
    mods.WriteManifest("wrong_backend", R"({"backend": "python"})");
    mods.WriteLua("wrong_backend", "Log.Info('never3')");

    LogCapture capture;
    script::ModLoader loader(manager, "lua");
    const script::ModLoader::Result result = loader.ScanAndLoad(mods.root);

    CHECK(result.found == 7);
    CHECK(result.loaded == 3);
    CHECK(result.skipped == 4);
    CHECK(manager.IsModLoaded("alpha"));
    CHECK(manager.IsModLoaded("zeta"));
    CHECK(manager.IsModLoaded("nameless"));
    CHECK_FALSE(manager.IsModLoaded("too_new"));
    CHECK_FALSE(manager.IsModLoaded("wrong_backend"));

    const std::string loadLogs = capture.Str();
    CHECK(loadLogs.find("mod 扫描完成：7 个目录，加载 3，跳过 4") != std::string::npos);
    CHECK(loadLogs.find("跳过 mod 目录（无 mod.json）：no_manifest") != std::string::npos);
    CHECK(loadLogs.find("需要更新的 API") != std::string::npos);
    CHECK(loadLogs.find("未注册的脚本后端：python") != std::string::npos);
    CHECK(loadLogs.find("never1") == std::string::npos);  // 被跳过 mod 的脚本从未执行

    // 派发顺序：按 loadOrder（zeta=10 先于 alpha=90），与目录名顺序无关
    bus.EmitQueued(core::kOnTick, static_cast<std::int64_t>(1));
    bus.FlushQueued();
    const std::string tickLogs = capture.Str();
    const std::size_t zeta = tickLogs.find("tick=zeta");
    const std::size_t alpha = tickLogs.find("tick=alpha");
    REQUIRE(zeta != std::string::npos);
    REQUIRE(alpha != std::string::npos);
    CHECK(zeta < alpha);

    // 目录不存在：返回空结果（不算错误）
    const script::ModLoader::Result missing =
        loader.ScanAndLoad(mods.root / "no_such_dir");
    CHECK(missing.found == 0);
    CHECK(missing.loaded == 0);
}
