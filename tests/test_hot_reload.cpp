#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "ScriptTestHelpers.h"
#include "core/EventBus.h"
#include "script/EventBridge.h"
#include "script/LuaRuntime.h"
#include "script/ModWatcher.h"
#include "script/ScriptApi.h"
#include "script/ScriptManager.h"

using namespace zl;
using zltest::LogCapture;
using zltest::TempModRoot;

namespace {

// 等待 watcher 报告某 mod 变更（最长 ~4s；测试用轮询间隔 0.02s）
bool WaitForChange(script::ModWatcher& watcher, const std::string& modId) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        for (const std::string& changed : watcher.PollChanges()) {
            if (changed == modId) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

std::optional<script::ModWatcher::ContentChange> WaitForContentChange(
    script::ModWatcher& watcher, const std::string& modId) {
    for (int attempt = 0; attempt < 300; ++attempt) {
        for (const script::ModWatcher::ContentChange& change :
             watcher.PollContentChanges()) {
            if (change.modId == modId) {
                return change;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return std::nullopt;
}

std::vector<script::ModWatcher::ContentChange> CollectContentChanges(
    script::ModWatcher& watcher, const std::string& modId, int milliseconds) {
    std::vector<script::ModWatcher::ContentChange> out;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        for (const script::ModWatcher::ContentChange& change :
             watcher.PollContentChanges()) {
            if (change.modId == modId) {
                out.push_back(change);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return out;
}

void WriteModFile(const TempModRoot& mods, const std::string& id,
                  const std::string& relative, const std::string& text) {
    const std::filesystem::path path = mods.root / id / relative;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

}  // namespace

TEST_CASE("M7-T6：ModWatcher 检测文件变更（不初始触发、不重复、按 mod 隔离）") {
    TempModRoot mods;
    mods.WriteLua("hot", "print('v1')");
    mods.WriteLua("cold", "print('keep')");

    script::ModWatcher watcher({{"hot", mods.root / "hot"}, {"cold", mods.root / "cold"}}, 0.02);
    CHECK(watcher.WatchedCount() == 2);
    CHECK(watcher.PollChanges().empty());  // 基准 = 加载时刻磁盘状态：无初始通知

    mods.WriteLua("hot", "print('v2')");
    CHECK(WaitForChange(watcher, "hot"));
    CHECK(watcher.PollChanges().empty());  // 已消费；同一变更不重复通知

    mods.WriteLua("hot", "print('v3')");
    CHECK(WaitForChange(watcher, "hot"));
    CHECK(watcher.PollChanges().empty());  // cold 从未变化
}

TEST_CASE("M7-T6：同尺寸同 mtime 覆盖仍触发（内容指纹）") {
    TempModRoot mods;
    mods.WriteLua("hot", "print('A')");
    const std::filesystem::path script =
        mods.root / "hot" / "scripts" / "main.lua";
    std::error_code ec;
    const std::filesystem::file_time_type stamp =
        std::filesystem::last_write_time(script, ec);
    REQUIRE_FALSE(ec);

    script::ModWatcher watcher({{"hot", mods.root / "hot"}}, 1.0);
    mods.WriteLua("hot", "print('B')");
    std::filesystem::last_write_time(script, stamp, ec);
    REQUIRE_FALSE(ec);

    CHECK(WaitForChange(watcher, "hot"));
}

TEST_CASE("M7-T6：热重载端到端（检测→重载生效→失败禁用→修复恢复）") {
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

    std::vector<core::Event> reloadEvents;
    bus.Subscribe(core::kOnModReload,
                  [&reloadEvents](const core::Event& event) { reloadEvents.push_back(event); });

    TempModRoot mods;
    const script::ModInfo mod =
        mods.MakeMod("hot", "Events.On('OnTick', function() Log.Info('v=1') end)");
    std::string error;
    REQUIRE(manager.LoadMod(mod, error));

    script::ModWatcher watcher({{"hot", mods.root / "hot"}}, 0.02);
    LogCapture capture;
    auto dispatchTick = [&](std::int64_t tick) {
        bus.EmitQueued(core::kOnTick, tick);
        bus.FlushQueued();
    };

    dispatchTick(1);
    CHECK(capture.Str().find("v=1") != std::string::npos);

    // 改文件 → 检测 → 重载成功：新版本生效、旧订阅不再存在、发 OnModReload
    mods.WriteLua("hot", "Events.On('OnTick', function() Log.Info('v=2') end)");
    REQUIRE(WaitForChange(watcher, "hot"));
    REQUIRE(manager.ReloadMod("hot", error));
    bus.FlushQueued();  // OnModReload 为排队派发
    REQUIRE(reloadEvents.size() == 1);
    CHECK(std::get<std::string>(reloadEvents[0].payload) == "hot");

    const std::size_t lengthBefore = capture.Str().size();
    dispatchTick(2);
    const std::string afterReload = capture.Str().substr(lengthBefore);
    CHECK(afterReload.find("v=2") != std::string::npos);
    CHECK(afterReload.find("v=1") == std::string::npos);

    // 写入语法错误 → 重载失败 → mod 被禁用（旧版本保留但不派发；游戏不崩）
    mods.WriteLua("hot", "this is (not lua");
    REQUIRE(WaitForChange(watcher, "hot"));
    CHECK_FALSE(manager.ReloadMod("hot", error));
    CHECK(manager.IsModDisabled("hot"));
    const std::size_t lengthDisabled = capture.Str().size();
    dispatchTick(3);
    CHECK(capture.Str().size() == lengthDisabled);  // 禁用后不再产生脚本日志

    // 修复文件 → 再次检测 → 重载成功：解除禁用、新版本生效
    mods.WriteLua("hot", "Events.On('OnTick', function() Log.Info('v=3') end)");
    REQUIRE(WaitForChange(watcher, "hot"));
    REQUIRE(manager.ReloadMod("hot", error));
    CHECK_FALSE(manager.IsModDisabled("hot"));
    const std::size_t lengthRecovered = capture.Str().size();
    dispatchTick(4);
    const std::string afterRecovery = capture.Str().substr(lengthRecovered);
    CHECK(afterRecovery.find("v=3") != std::string::npos);
}

TEST_CASE("X3-T1：content/assets 变更分类、防抖并与脚本热重载隔离") {
    TempModRoot mods;
    mods.WriteLua("hot", "print('v1')");
    WriteModFile(mods, "hot", "content/items.json", "{\"v\":1}");
    WriteModFile(mods, "hot", "assets/asset_manifest.json", "{\"v\":1}");

    script::ModWatcher watcher({{"hot", mods.root / "hot"}}, 0.02, 0.12);
    CHECK(watcher.PollChanges().empty());
    CHECK(watcher.PollContentChanges().empty());

    mods.WriteLua("hot", "print('v2')");
    CHECK(WaitForChange(watcher, "hot"));
    CHECK(watcher.PollContentChanges().empty());

    WriteModFile(mods, "hot", "content/items.json", "{\"v\":2}");
    const auto contentChange = WaitForContentChange(watcher, "hot");
    REQUIRE(contentChange.has_value());
    CHECK(contentChange->content);
    CHECK_FALSE(contentChange->assets);
    CHECK(watcher.PollChanges().empty());  // content 变更不触发脚本重载

    WriteModFile(mods, "hot", "assets/asset_manifest.json", "{\"v\":2}");
    const auto assetChange = WaitForContentChange(watcher, "hot");
    REQUIRE(assetChange.has_value());
    CHECK_FALSE(assetChange->content);
    CHECK(assetChange->assets);
    CHECK(watcher.PollChanges().empty());
}

TEST_CASE("X3-T1：连续写入 content/assets 合并为一次防抖通知") {
    TempModRoot mods;
    WriteModFile(mods, "hot", "content/items.json", "{\"v\":1}");
    WriteModFile(mods, "hot", "assets/asset_manifest.json", "{\"v\":1}");

    script::ModWatcher watcher({{"hot", mods.root / "hot"}}, 0.02, 0.15);
    CHECK(watcher.PollContentChanges().empty());

    WriteModFile(mods, "hot", "content/items.json", "{\"v\":2}");
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    WriteModFile(mods, "hot", "content/items.json", "{\"v\":3}");
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    WriteModFile(mods, "hot", "assets/asset_manifest.json", "{\"v\":2}");

    const auto changes = CollectContentChanges(watcher, "hot", 600);
    REQUIRE(changes.size() == 1u);
    CHECK(changes[0].content);
    CHECK(changes[0].assets);
}

TEST_CASE("X3-T1：仅 content 的 mod 也能监听，未变化 mod 不通知") {
    TempModRoot mods;
    WriteModFile(mods, "content_only", "content/items.json", "{\"v\":1}");
    WriteModFile(mods, "cold", "content/items.json", "{\"v\":1}");

    script::ModWatcher watcher(
        {{"content_only", mods.root / "content_only"},
         {"cold", mods.root / "cold"}},
        0.02, 0.08);
    CHECK(watcher.PollContentChanges().empty());

    WriteModFile(mods, "content_only", "content/items.json", "{\"v\":2}");
    const auto change = WaitForContentChange(watcher, "content_only");
    REQUIRE(change.has_value());
    CHECK(change->content);
    CHECK_FALSE(change->assets);

    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    const auto all = watcher.PollContentChanges();
    CHECK(std::none_of(all.begin(), all.end(), [](const auto& item) {
        return item.modId == "cold";
    }));
}
