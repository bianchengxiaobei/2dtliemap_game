#include "script/PythonRuntime.h"

#ifdef ZL_ENABLE_PYTHON

#include <pybind11/embed.h>
#include <pybind11/pybind11.h>

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "core/Log.h"
#include "script/ScriptApi.h"

namespace py = pybind11;

namespace zl::script {
namespace {

constexpr const char* kModule = "script-py";

// 沙箱（docs/03 §7/§9.2）：mod 顶层 import 只允许这些模块；其余抛 ImportError。
// 说明：防止"手滑级"越界（文件/进程/网络），不是强安全边界（信任本地 mod）。
constexpr std::string_view kAllowedImports[] = {
    "math",   "random",      "string",  "json",   "re",         "itertools", "functools",
    "collections", "time",   "datetime", "bisect", "heapq",     "copy",      "enum",
    "dataclasses", "typing", "textwrap", "unicodedata", "statistics",
};

// 受限 builtins：拿掉文件/进程/元编程相关内建。
constexpr const char* kBlockedBuiltins[] = {
    "open", "input", "eval", "exec", "compile", "breakpoint", "exit", "quit", "help",
};

bool IsAllowedImport(std::string_view topLevelName) {
    for (const std::string_view allowed : kAllowedImports) {
        if (topLevelName == allowed) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();
    // 容忍 UTF-8 BOM（Windows 编辑器默认写入）：exec(str) 下 BOM 是非法字符
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
}

// Python home（含 Lib/ 标准库的目录）候选：
// 1) 环境变量 ZL_PYTHON_HOME（打包/换机覆盖）；
// 2) <exe 目录>/python（未来发行版布局）；
// 3) 编译期默认值 ZL_PYTHON_HOME_DEFAULT（开发构建指向 vcpkg 的 tools/python3）。
std::optional<std::string> FindPythonHome() {
    if (const char* env = std::getenv("ZL_PYTHON_HOME"); env != nullptr && env[0] != '\0') {
        return std::string(env);
    }
    wchar_t buffer[4096] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, 4096);
    if (length > 0 && length < 4096) {
        const std::filesystem::path besideExe =
            std::filesystem::path(buffer).parent_path() / "python";
        if (std::filesystem::exists(besideExe / "Lib")) {
            return besideExe.string();
        }
    }
#ifdef ZL_PYTHON_HOME_DEFAULT
    const std::filesystem::path fallback = ZL_PYTHON_HOME_DEFAULT;
    if (std::filesystem::exists(fallback / "Lib")) {
        return fallback.string();
    }
#endif
    return std::nullopt;
}

// 受限 builtins 表：copy 自 builtins，print 重定向，__import__ 白名单，去掉危险内建。
py::dict MakeRestrictedBuiltins(const std::string& modId) {
    py::dict restricted = py::module_::import("builtins").attr("__dict__").attr("copy")();
    for (const char* name : kBlockedBuiltins) {
        if (PyDict_DelItemString(restricted.ptr(), name) < 0) {
            PyErr_Clear();  // 个别版本 builtins 可能没有该名字；忽略并清除错误
        }
    }

    restricted["print"] = py::cpp_function([modId](py::args args, py::kwargs kwargs) {
        try {
            std::string line;
            for (std::size_t i = 0; i < args.size(); ++i) {
                if (i > 0) {
                    line += '\t';
                }
                line += py::cast<std::string>(py::str(args[i]));
            }
            if (kwargs) {
                for (auto item : kwargs) {
                    line += '\t';
                    line += py::cast<std::string>(py::str(item.first));
                    line += '=';
                    line += py::cast<std::string>(py::str(item.second));
                }
            }
            ZL_INFO(kModule, "[{}] {}", modId, line);
        } catch (const std::exception& e) {
            ZL_WARN(kModule, "[{}] print 输出失败：{}", modId, e.what());
        }
    });

    const py::object realImport = restricted["__import__"];
    restricted["__import__"] = py::cpp_function(
        [realImport](const std::string& name, py::object globals, py::object locals,
                     py::object fromlist, int level) -> py::object {
            const std::size_t dot = name.find('.');
            const std::string top = name.substr(0, dot);
            if (level != 0 || !IsAllowedImport(top)) {
                PyErr_SetString(PyExc_ImportError,
                                ("模块 '" + name + "' 不在 Python 后端白名单内（见 docs/03 §7）")
                                    .c_str());
                throw py::error_already_set();
            }
            return realImport(name, globals, locals, fromlist, level);
        },
        py::arg("name"), py::arg("globals") = py::none(), py::arg("locals") = py::none(),
        py::arg("fromlist") = py::tuple(), py::arg("level") = 0);

    return restricted;
}

// 每个 mod 独立的全局命名空间（module 对象等价物）。
py::dict MakeModGlobals(const std::string& modId, const std::string& path) {
    py::dict globals;
    globals["__name__"] = py::str("zl_mod_" + modId);
    globals["__file__"] = py::str(path);
    globals["__builtins__"] = MakeRestrictedBuiltins(modId);
    return globals;
}

}  // namespace

struct PythonRuntime::Impl {
    struct ModEntry {
        ModInfo info;
        py::dict globals;
    };

    bool initialized = false;
    bool ownsInterpreter = false;  // false = 复用已存在的解释器（测试进程等）
    const ScriptApi* api = nullptr;

    std::unordered_map<std::string, ModEntry> mods;
    std::vector<std::string> loadedIds;

    bool LoadOrReloadMod(const ModInfo& mod, bool isReload, std::string& errorOut) {
        if (!initialized) {
            errorOut = "PythonRuntime 未初始化";
            return false;
        }
        const bool known = mods.count(mod.id) > 0;
        if (!isReload && known) {
            errorOut = "mod 已加载：" + mod.id;
            return false;
        }
        if (isReload && !known) {
            errorOut = "mod 未加载：" + mod.id;
            return false;
        }

        const std::string path =
            (std::filesystem::path(mod.rootPath) / mod.mainScript).generic_string();
        const std::optional<std::string> source = ReadFile(path);
        if (!source.has_value()) {
            errorOut = "无法读取脚本文件：" + path;
            return false;
        }

        // GIL 纪律（docs/03 §9.2）：所有脚本调用只在主线程；入口统一 acquire。
        py::gil_scoped_acquire gil;

        // 先在临时 globals 执行；成功才替换旧版本（重载失败保留旧版本）
        py::dict globals = MakeModGlobals(mod.id, path);
        try {
            // 经 compile(filename=路径) 得到带真实文件名的 code 对象，异常定位更准
            // 不能用 py::exec：pybind11 3.x 只提供 str 重载，会隐式把 code 对象转成
            // "<code object ...>" 再按 <string> 编译（M8-T2 踩坑）。PyEval_EvalCode
            // 直接执行 compile(filename=路径) 得到的 code 对象，traceback 带真实路径。
            const py::object code = py::module_::import("builtins").attr("compile")(
                py::str(*source), py::str(path), py::str("exec"));
            PyObject* executed = PyEval_EvalCode(code.ptr(), globals.ptr(), globals.ptr());
            if (executed == nullptr) {
                throw py::error_already_set();
            }
            Py_DECREF(executed);
        } catch (const py::error_already_set& e) {
            errorOut = e.what();  // 含 Python traceback（File "路径", line N）
            return false;
        } catch (const std::exception& e) {
            errorOut = std::string("执行异常：") + e.what();
            return false;
        }

        mods.insert_or_assign(mod.id, ModEntry{mod, std::move(globals)});
        if (!isReload) {
            loadedIds.push_back(mod.id);
        }
        return true;
    }
};

PythonRuntime::PythonRuntime() : m_impl(std::make_unique<Impl>()) {}

PythonRuntime::~PythonRuntime() {
    Shutdown();
}

const char* PythonRuntime::Name() const {
    return "python";
}

void PythonRuntime::SetApi(const ScriptApi* api) {
    m_impl->api = api;
}

bool PythonRuntime::Initialize(std::string& errorOut) {
    errorOut.clear();
    if (m_impl->initialized) {
        return true;
    }
    try {
        if (Py_IsInitialized() != 0) {
            m_impl->ownsInterpreter = false;  // 复用（如测试进程已初始化）
            m_impl->initialized = true;
            return true;
        }

        // 显式指定 home（含 Lib/ 的目录）：嵌入式解释器默认找不到标准库，
        // Py_InitializeFromConfig 会报 "Failed to import encodings module"（M8-T2 踩坑）。
        PyConfig config;
        PyConfig_InitPythonConfig(&config);
        config.parse_argv = 0;
        config.install_signal_handlers = 0;  // 控制台信号由游戏自己管
        config.user_site_directory = 0;      // 不扫用户 site-packages，启动确定性

        const std::optional<std::string> home = FindPythonHome();
        if (home.has_value()) {
            const PyStatus status =
                PyConfig_SetBytesString(&config, &config.home, home->c_str());
            if (PyStatus_Exception(status) != 0) {
                PyConfig_Clear(&config);
                errorOut = std::string("设置 Python home 失败：") +
                           (status.err_msg != nullptr ? status.err_msg : "unknown");
                return false;
            }
        }

        py::initialize_interpreter(&config, 0, nullptr, /*add_program_dir_to_path=*/false);
        m_impl->ownsInterpreter = true;
        m_impl->initialized = true;
        return true;
    } catch (const std::exception& e) {
        errorOut = std::string("Python 解释器初始化异常：") + e.what();
        return false;
    }
}

void PythonRuntime::Shutdown() {
    if (!m_impl || !m_impl->initialized) {
        return;
    }
    {
        py::gil_scoped_acquire gil;
        m_impl->mods.clear();  // 先释放所有 mod globals（引用清理在解释器存活时完成）
        m_impl->loadedIds.clear();
    }
    if (m_impl->ownsInterpreter) {
        py::finalize_interpreter();
    }
    m_impl->initialized = false;
}

bool PythonRuntime::LoadMod(const ModInfo& mod, std::string& errorOut) {
    errorOut.clear();
    return m_impl->LoadOrReloadMod(mod, /*isReload=*/false, errorOut);
}

bool PythonRuntime::ReloadMod(const std::string& modId, std::string& errorOut) {
    errorOut.clear();
    const auto it = m_impl->mods.find(modId);
    if (it == m_impl->mods.end()) {
        errorOut = "mod 未加载：" + modId;
        return false;
    }
    return m_impl->LoadOrReloadMod(it->second.info, /*isReload=*/true, errorOut);
}

void PythonRuntime::UnloadMod(const std::string& modId) {
    if (!m_impl->initialized) {
        return;
    }
    py::gil_scoped_acquire gil;
    if (m_impl->mods.erase(modId) > 0) {
        m_impl->loadedIds.erase(
            std::remove(m_impl->loadedIds.begin(), m_impl->loadedIds.end(), modId),
            m_impl->loadedIds.end());
    }
}

std::vector<ModError> PythonRuntime::DispatchEventToMod(const std::string& modId,
                                                        const core::ScriptEvent& ev) {
    (void)ev;
    if (!m_impl->initialized || m_impl->mods.count(modId) == 0) {
        return {};
    }
    // M8-T3：Events.On 订阅表在此派发；T2 阶段 Python 侧尚无订阅 API。
    return {};
}

const std::vector<std::string>& PythonRuntime::LoadedMods() const {
    return m_impl->loadedIds;
}

}  // namespace zl::script

#endif  // ZL_ENABLE_PYTHON
