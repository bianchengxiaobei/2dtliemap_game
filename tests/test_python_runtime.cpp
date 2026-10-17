#include <doctest/doctest.h>

#ifdef ZL_ENABLE_PYTHON

#include <algorithm>
#include <string>

#include "script/PythonRuntime.h"

#include "ScriptTestHelpers.h"

using namespace zl;

namespace {

// 测试进程共享一个解释器实例（pybind11 不适合反复 finalize/initialize）；
// 故意泄漏到进程结束，模拟游戏内"单实例 runtime"的真实生命周期。
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

}  // namespace

TEST_CASE("M8-T2：Python mod 加载 + print 重定向到游戏日志") {
    script::PythonRuntime* runtime = SharedRuntimePtr();
    REQUIRE(runtime != nullptr);
    CHECK(std::string(runtime->Name()) == "python");

    zltest::TempModRoot mods;
    zltest::LogCapture logs;

    const script::ModInfo mod =
        mods.MakeModPy("py_hello", "print('hello from python', 42)\nprint('中文输出 ok')\n");
    std::string error;
    CHECK(runtime->LoadMod(mod, error));
    CHECK(error.empty());

    const std::string captured = logs.Str();
    CHECK(captured.find("[py_hello] hello from python\t42") != std::string::npos);
    CHECK(captured.find("[py_hello] 中文输出 ok") != std::string::npos);

    const auto& loaded = runtime->LoadedMods();
    CHECK(std::find(loaded.begin(), loaded.end(), "py_hello") != loaded.end());
}

TEST_CASE("M8-T2：沙箱 — import 白名单 + 危险内建不可见") {
    script::PythonRuntime* runtime = SharedRuntimePtr();
    REQUIRE(runtime != nullptr);

    zltest::TempModRoot mods;
    zltest::LogCapture logs;

    const script::ModInfo mod = mods.MakeModPy("py_sandbox",
                                               "import math\n"
                                               "from json import dumps\n"
                                               "assert math.sqrt(9.0) == 3.0\n"
                                               "try:\n"
                                               "    import os\n"
                                               "except ImportError:\n"
                                               "    pass\n"
                                               "else:\n"
                                               "    raise AssertionError('os 不应可导入')\n"
                                               "try:\n"
                                               "    open\n"
                                               "except NameError:\n"
                                               "    pass\n"
                                               "else:\n"
                                               "    raise AssertionError('open 不应可见')\n"
                                               "print('sandbox ok', dumps({'a': 1}))\n");
    std::string error;
    CHECK(runtime->LoadMod(mod, error));
    CHECK(error.empty());
    CHECK(logs.Str().find("sandbox ok") != std::string::npos);
}

TEST_CASE("M8-T2：脚本错误按 mod 归因上报（含路径与 traceback），不抛出") {
    script::PythonRuntime* runtime = SharedRuntimePtr();
    REQUIRE(runtime != nullptr);

    zltest::TempModRoot mods;
    std::string error;

    const script::ModInfo badSyntax = mods.MakeModPy("py_bad_syntax", "def broken(:\n    pass\n");
    CHECK_FALSE(runtime->LoadMod(badSyntax, error));
    CHECK(error.find("main.py") != std::string::npos);
    CHECK(error.find("SyntaxError") != std::string::npos);

    const script::ModInfo badRuntime =
        mods.MakeModPy("py_bad_runtime", "value = 1\nresult = value / 0\n");
    CHECK_FALSE(runtime->LoadMod(badRuntime, error));
    CHECK(error.find("main.py") != std::string::npos);
    CHECK(error.find("ZeroDivisionError") != std::string::npos);
}

TEST_CASE("M8-T2：重载 — 成功替换 / 失败保留旧版（不卸载）") {
    script::PythonRuntime* runtime = SharedRuntimePtr();
    REQUIRE(runtime != nullptr);

    zltest::TempModRoot mods;
    std::string error;

    const script::ModInfo mod = mods.MakeModPy("py_reload", "print('py_reload v1')\n");
    CHECK(runtime->LoadMod(mod, error));

    {
        zltest::LogCapture logs;
        mods.WritePy("py_reload", "print('py_reload v2')\n");
        CHECK(runtime->ReloadMod("py_reload", error));
        CHECK(logs.Str().find("py_reload v2") != std::string::npos);
    }

    mods.WritePy("py_reload", "def broken(:\n");
    CHECK_FALSE(runtime->ReloadMod("py_reload", error));
    CHECK(error.find("SyntaxError") != std::string::npos);

    // 失败的重载不卸载 mod：修复后还能成功重载
    {
        zltest::LogCapture logs;
        mods.WritePy("py_reload", "print('py_reload v3')\n");
        CHECK(runtime->ReloadMod("py_reload", error));
        CHECK(logs.Str().find("py_reload v3") != std::string::npos);
    }
}

#else

TEST_CASE("M8-T2：Python 后端未编译（本构建跳过 PythonRuntime 用例）") {
    CHECK(true);
}

#endif  // ZL_ENABLE_PYTHON
