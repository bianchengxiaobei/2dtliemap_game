#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/ScriptEvent.h"
#include "script/IScriptRuntime.h"
#include "script/ScriptManager.h"

using namespace zl;

namespace {

// 假后端：记录调用并可按 mod 注入失败（加载/重载/派发），验证 ScriptManager 的策略。
class FakeRuntime : public script::IScriptRuntime {
public:
    explicit FakeRuntime(std::string name, bool initOk = true)
        : m_name(std::move(name)), m_initOk(initOk) {}

    const char* Name() const override { return m_name.c_str(); }

    bool Initialize(std::string& errorOut) override {
        if (!m_initOk) {
            errorOut = m_name + " 初始化失败（fake）";
            return false;
        }
        m_initialized = true;
        return true;
    }

    void Shutdown() override {
        ++m_shutdownCount;
        m_initialized = false;
    }

    bool LoadMod(const script::ModInfo& mod, std::string& errorOut) override {
        if (m_failLoadMods.count(mod.id) > 0) {
            errorOut = "脚本语法错误（fake）：" + mod.id + "/scripts/main.lua:3";
            return false;
        }
        m_loaded.push_back(mod.id);
        return true;
    }

    bool ReloadMod(const std::string& modId, std::string& errorOut) override {
        if (m_failReloadMods.count(modId) > 0) {
            errorOut = "重载时报错（fake）：" + modId + "/scripts/main.lua:9";
            return false;
        }
        ++m_reloadCount[modId];
        return true;
    }

    void UnloadMod(const std::string& modId) override {
        m_loaded.erase(std::remove(m_loaded.begin(), m_loaded.end(), modId), m_loaded.end());
        ++m_unloadCount;
    }

    std::vector<script::ModError> DispatchEventToMod(const std::string& modId,
                                                     const core::ScriptEvent& ev) override {
        m_dispatchOrder.push_back(modId);
        m_received.emplace_back(modId, ev);
        if (m_failDispatchMods.count(modId) > 0) {
            return {script::ModError{modId, modId + "/scripts/main.lua:7: attempt to call a nil value"}};
        }
        return {};
    }

    const std::vector<std::string>& LoadedMods() const override { return m_loaded; }

    // ---- 测试夹具 ----
    std::set<std::string> m_failLoadMods;
    std::set<std::string> m_failReloadMods;
    std::set<std::string> m_failDispatchMods;
    std::vector<std::string> m_dispatchOrder;
    std::vector<std::pair<std::string, core::ScriptEvent>> m_received;
    std::map<std::string, int> m_reloadCount;
    int m_shutdownCount = 0;
    int m_unloadCount = 0;
    bool m_initialized = false;

private:
    std::string m_name;
    bool m_initOk = true;
    std::vector<std::string> m_loaded;
};

script::ModInfo MakeMod(const char* id, const char* backend, int loadOrder = 100) {
    script::ModInfo mod;
    mod.id = id;
    mod.name = id;
    mod.version = "1.0.0";
    mod.backend = backend;
    mod.rootPath = std::string("mods/") + id + "/";
    mod.mainScript = "scripts/main.lua";
    mod.loadOrder = loadOrder;
    return mod;
}

core::ScriptEvent MakeTickEvent(int64_t tick) {
    core::ScriptEvent ev;
    ev.name = "OnTick";
    ev.fields.push_back(core::ScriptField{"tick", tick});
    return ev;
}

}  // namespace

TEST_CASE("M7-T1：后端注册表——注册/查找/重名拒绝") {
    script::ScriptManager manager;
    CHECK(manager.RuntimeCount() == 0);
    CHECK(manager.FindRuntime("lua") == nullptr);

    manager.RegisterRuntime(std::make_unique<FakeRuntime>("lua"));
    CHECK(manager.RuntimeCount() == 1);
    REQUIRE(manager.FindRuntime("lua") != nullptr);
    CHECK(std::string(manager.FindRuntime("lua")->Name()) == "lua");
    CHECK(manager.FindRuntime("python") == nullptr);

    manager.RegisterRuntime(std::make_unique<FakeRuntime>("lua"));  // 重名 → 拒绝
    CHECK(manager.RuntimeCount() == 1);

    manager.RegisterRuntime(std::make_unique<FakeRuntime>("python"));
    CHECK(manager.RuntimeCount() == 2);
    CHECK(manager.FindRuntime("python") != nullptr);
}

TEST_CASE("M7-T1：初始化失败的后端不影响其它后端") {
    script::ScriptManager manager;
    manager.RegisterRuntime(std::make_unique<FakeRuntime>("lua", /*initOk=*/false));
    manager.RegisterRuntime(std::make_unique<FakeRuntime>("python", /*initOk=*/true));
    CHECK_FALSE(manager.InitializeAll());

    std::string error;
    CHECK_FALSE(manager.LoadMod(MakeMod("bad", "lua"), error));
    CHECK(error.find("未就绪") != std::string::npos);
    CHECK_FALSE(manager.IsModLoaded("bad"));

    CHECK(manager.LoadMod(MakeMod("ok", "python"), error));
    CHECK(error.empty());
    CHECK(manager.IsModLoaded("ok"));
}

TEST_CASE("M7-T1：mod 加载路由与错误传播") {
    script::ScriptManager manager;
    auto lua = std::make_unique<FakeRuntime>("lua");
    FakeRuntime* raw = lua.get();
    manager.RegisterRuntime(std::move(lua));
    CHECK(manager.InitializeAll());

    // 未注册的后端
    std::string error;
    CHECK_FALSE(manager.LoadMod(MakeMod("m1", "scheme"), error));
    CHECK(error.find("未注册的脚本后端") != std::string::npos);

    // runtime 加载失败：错误原样传播（含脚本路径+行号），不进入已加载表
    raw->m_failLoadMods.insert("broken");
    CHECK_FALSE(manager.LoadMod(MakeMod("broken", "lua"), error));
    CHECK(error.find("main.lua:3") != std::string::npos);
    CHECK_FALSE(manager.IsModLoaded("broken"));

    // 成功加载
    CHECK(manager.LoadMod(MakeMod("good", "lua"), error));
    CHECK(error.empty());
    CHECK(manager.IsModLoaded("good"));
    REQUIRE(raw->LoadedMods().size() == 1);
    CHECK(raw->LoadedMods()[0] == "good");
    CHECK(manager.LoadedModIds() == std::vector<std::string>{"good"});

    // 重复加载同一 id：拒绝
    CHECK_FALSE(manager.LoadMod(MakeMod("good", "lua"), error));
    CHECK(error.find("已加载") != std::string::npos);
}

TEST_CASE("M7-T1：事件派发按 loadOrder 且携带命名字段") {
    script::ScriptManager manager;
    auto lua = std::make_unique<FakeRuntime>("lua");
    FakeRuntime* raw = lua.get();
    manager.RegisterRuntime(std::move(lua));
    CHECK(manager.InitializeAll());

    std::string error;
    CHECK(manager.LoadMod(MakeMod("later", "lua", /*loadOrder=*/200), error));
    CHECK(manager.LoadMod(MakeMod("early", "lua", /*loadOrder=*/50), error));
    CHECK(manager.LoadedModIds() == std::vector<std::string>{"early", "later"});

    manager.DispatchEvent(MakeTickEvent(42));

    CHECK(raw->m_dispatchOrder == std::vector<std::string>{"early", "later"});
    REQUIRE(raw->m_received.size() == 2);
    CHECK(raw->m_received[0].second.name == "OnTick");
    const core::ScriptValue* tick = raw->m_received[0].second.Find("tick");
    REQUIRE(tick != nullptr);
    CHECK(std::get<int64_t>(*tick) == 42);
    CHECK(raw->m_received[0].second.Find("nope") == nullptr);
}

TEST_CASE("M7-T1：脚本错误 → 记日志并禁用该 mod，其它 mod 不受影响") {
    script::ScriptManager manager;
    auto lua = std::make_unique<FakeRuntime>("lua");
    FakeRuntime* raw = lua.get();
    manager.RegisterRuntime(std::move(lua));
    CHECK(manager.InitializeAll());

    std::string error;
    CHECK(manager.LoadMod(MakeMod("crasher", "lua"), error));
    CHECK(manager.LoadMod(MakeMod("healthy", "lua"), error));
    raw->m_failDispatchMods.insert("crasher");

    manager.DispatchEvent(MakeTickEvent(1));
    CHECK(manager.IsModDisabled("crasher"));
    CHECK_FALSE(manager.IsModDisabled("healthy"));
    CHECK(manager.DisabledModCount() == 1);
    CHECK(raw->m_dispatchOrder == std::vector<std::string>{"crasher", "healthy"});

    // 后续派发：crasher 被跳过，healthy 照常收到
    raw->m_dispatchOrder.clear();
    manager.DispatchEvent(MakeTickEvent(2));
    CHECK(raw->m_dispatchOrder == std::vector<std::string>{"healthy"});
    CHECK(manager.IsModLoaded("crasher"));  // 禁用 ≠ 卸载
}

TEST_CASE("M7-T1：重载——成功解除禁用，失败保持禁用") {
    script::ScriptManager manager;
    auto lua = std::make_unique<FakeRuntime>("lua");
    FakeRuntime* raw = lua.get();
    manager.RegisterRuntime(std::move(lua));
    CHECK(manager.InitializeAll());

    std::string error;
    CHECK(manager.LoadMod(MakeMod("m", "lua"), error));
    raw->m_failDispatchMods.insert("m");
    manager.DispatchEvent(MakeTickEvent(1));
    REQUIRE(manager.IsModDisabled("m"));

    // 重载失败：错误传播 + 保持禁用
    raw->m_failReloadMods.insert("m");
    CHECK_FALSE(manager.ReloadMod("m", error));
    CHECK(error.find("main.lua:9") != std::string::npos);
    CHECK(manager.IsModDisabled("m"));

    // 重载成功：解除禁用并恢复派发
    raw->m_failReloadMods.clear();
    raw->m_failDispatchMods.clear();
    CHECK(manager.ReloadMod("m", error));
    CHECK(error.empty());
    CHECK_FALSE(manager.IsModDisabled("m"));
    CHECK(manager.DisabledModCount() == 0);

    raw->m_dispatchOrder.clear();
    manager.DispatchEvent(MakeTickEvent(2));
    CHECK(raw->m_dispatchOrder == std::vector<std::string>{"m"});
    CHECK(raw->m_reloadCount["m"] == 1);

    // 未加载的 mod：重载失败
    CHECK_FALSE(manager.ReloadMod("ghost", error));
    CHECK(error.find("未加载") != std::string::npos);
}

TEST_CASE("M7-T1：卸载 mod 后不再参与派发") {
    script::ScriptManager manager;
    auto lua = std::make_unique<FakeRuntime>("lua");
    FakeRuntime* raw = lua.get();
    manager.RegisterRuntime(std::move(lua));
    CHECK(manager.InitializeAll());

    std::string error;
    CHECK(manager.LoadMod(MakeMod("a", "lua"), error));
    CHECK(manager.LoadMod(MakeMod("b", "lua"), error));
    REQUIRE(raw->LoadedMods().size() == 2);

    manager.UnloadMod("a");
    CHECK_FALSE(manager.IsModLoaded("a"));
    REQUIRE(raw->LoadedMods().size() == 1);
    CHECK(raw->LoadedMods()[0] == "b");
    CHECK(raw->m_unloadCount == 1);

    raw->m_dispatchOrder.clear();
    manager.DispatchEvent(MakeTickEvent(1));
    CHECK(raw->m_dispatchOrder == std::vector<std::string>{"b"});

    manager.UnloadMod("not_loaded");  // 告警不崩
    CHECK(raw->m_unloadCount == 1);
}

TEST_CASE("M7-T1：ShutdownAll 关闭全部后端并清空 mod 记录") {
    script::ScriptManager manager;
    auto a = std::make_unique<FakeRuntime>("a");
    FakeRuntime* aRaw = a.get();
    auto b = std::make_unique<FakeRuntime>("b");
    FakeRuntime* bRaw = b.get();
    manager.RegisterRuntime(std::move(a));
    manager.RegisterRuntime(std::move(b));
    CHECK(manager.InitializeAll());

    std::string error;
    CHECK(manager.LoadMod(MakeMod("m", "b"), error));

    manager.ShutdownAll();
    CHECK(aRaw->m_shutdownCount == 1);
    CHECK(bRaw->m_shutdownCount == 1);
    CHECK(manager.LoadedModIds().empty());
    CHECK_FALSE(manager.IsModLoaded("m"));

    // 关闭后加载：后端未就绪
    CHECK_FALSE(manager.LoadMod(MakeMod("m2", "b"), error));
    CHECK(error.find("未就绪") != std::string::npos);
}
