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

#include "core/EventBus.h"
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

// M8-T3：一次性定时器（seconds 为游戏秒；回调在后续 OnTick 派发时泵出）。
struct Timer {
    py::function fn;
    double dueMinutes = 0.0;
};

// 事件名 → 订阅函数列表（每 mod 独立；Events.On/Off 随时可变）。
using HandlerMap = std::unordered_map<std::string, std::vector<py::function>>;

// 事件字段 → dict（与 Lua 侧同名字段；monostate 跳过）。
py::dict MakeEventArgDict(const core::ScriptEvent& event) {
    py::dict out;
    for (const core::ScriptField& field : event.fields) {
        if (const auto* value = std::get_if<std::int64_t>(&field.value)) {
            out[py::str(field.name)] = *value;
        } else if (const auto* value = std::get_if<double>(&field.value)) {
            out[py::str(field.name)] = *value;
        } else if (const auto* value = std::get_if<bool>(&field.value)) {
            out[py::str(field.name)] = *value;
        } else if (const auto* value = std::get_if<std::string>(&field.value)) {
            out[py::str(field.name)] = *value;
        }
    }
    return out;
}

}  // namespace

struct PythonRuntime::Impl {
    struct ModEntry {
        ModInfo info;
        py::dict globals;
        std::shared_ptr<HandlerMap> handlers;          // 事件订阅（按 mod 隔离）
        std::shared_ptr<std::vector<Timer>> timers;    // SetTimer 定时器
    };

    bool initialized = false;
    bool ownsInterpreter = false;  // false = 复用已存在的解释器（测试进程等）
    const ScriptApi* api = nullptr;

    std::unordered_map<std::string, ModEntry> mods;
    std::vector<std::string> loadedIds;

    // Host API（M8-T3）：与 Lua 侧同名同参数，绑定为 per-mod 命名空间对象。
    // api 为 nullptr 时绑定安全默认值。
    void BindApi(py::dict& globals, const std::string& modId,
                 const std::shared_ptr<HandlerMap>& handlers,
                 const std::shared_ptr<std::vector<Timer>>& timers) {
        const ScriptApi* scriptApi = api;
        const auto makeNamespace = [] {
            return py::module_::import("types").attr("SimpleNamespace")();
        };

        py::object game = makeNamespace();
        game.attr("GetTick") = py::cpp_function([scriptApi]() -> std::int64_t {
            return scriptApi != nullptr ? scriptApi->GetTick() : 0;
        });
        game.attr("GetPlayerId") = py::cpp_function([scriptApi]() -> std::uint64_t {
            return scriptApi != nullptr ? scriptApi->GetPlayerId() : 0;
        });
        game.attr("GetTime") = py::cpp_function([scriptApi]() -> py::dict {
            const core::DateTime dt =
                scriptApi != nullptr ? scriptApi->GetTime() : core::DateTime{};
            py::dict out;
            out["year"] = dt.year;
            out["month"] = dt.month;
            out["day"] = dt.day;
            out["hour"] = dt.hour;
            out["minute"] = dt.minute;
            out["totalMinutes"] = scriptApi != nullptr ? scriptApi->GetTotalGameMinutes() : 0.0;
            return out;
        });
        game.attr("SetTimer") = py::cpp_function(
            [scriptApi, timers](double seconds, py::function fn) {
                Timer timer;
                timer.fn = std::move(fn);
                const double now = scriptApi != nullptr ? scriptApi->GetTotalGameMinutes() : 0.0;
                timer.dueMinutes = now + (seconds > 0.0 ? seconds : 0.0) / 60.0;
                timers->push_back(std::move(timer));
            },
            py::arg("seconds"), py::arg("fn"));
        globals["Game"] = game;

        py::object log = makeNamespace();
        log.attr("Info") = py::cpp_function([modId](const std::string& message) {
            ZL_INFO("script-mod", "[{}] {}", modId, message);
        });
        log.attr("Warn") = py::cpp_function([modId](const std::string& message) {
            ZL_WARN("script-mod", "[{}] {}", modId, message);
        });
        log.attr("Error") = py::cpp_function([modId](const std::string& message) {
            ZL_ERROR("script-mod", "[{}] {}", modId, message);
        });
        globals["Log"] = log;

        py::object player = makeNamespace();
        player.attr("GetPosition") = py::cpp_function([scriptApi]() -> py::object {
            ScriptVec3 position;
            if (scriptApi == nullptr || !scriptApi->GetPlayerPosition(position)) {
                return py::none();
            }
            py::dict out;
            out["x"] = position.x;
            out["y"] = position.y;
            out["z"] = position.z;
            return out;
        });
        player.attr("Teleport") = py::cpp_function(
            [scriptApi](double x, double y, int z) -> bool {
                return scriptApi != nullptr && scriptApi->TeleportPlayer(
                                                   static_cast<float>(x), static_cast<float>(y), z);
            },
            py::arg("x"), py::arg("y"), py::arg("z") = 0);
        player.attr("GetHealth") = py::cpp_function([scriptApi]() -> py::object {
            float hp = 0.0f;
            if (scriptApi == nullptr || !scriptApi->GetPlayerHealth(hp)) {
                return py::none();
            }
            return py::float_(static_cast<double>(hp));
        });
        player.attr("AddItem") = py::cpp_function(
            [scriptApi](const std::string& defId, int count) -> int {
                return scriptApi != nullptr ? scriptApi->AddPlayerItem(defId, count) : 0;
            },
            py::arg("defId"), py::arg("count") = 1);
        globals["Player"] = player;

        py::object world = makeNamespace();
        world.attr("IsPassable") = py::cpp_function([scriptApi](int gx, int gy) -> bool {
            return scriptApi != nullptr && scriptApi->IsPassable(gx, gy);
        });
        world.attr("SpawnItem") = py::cpp_function(
            [scriptApi](const std::string& defId, double x, double y, int z) -> bool {
                return scriptApi != nullptr && scriptApi->SpawnItem(
                                                   defId, static_cast<float>(x),
                                                   static_cast<float>(y), z);
            },
            py::arg("defId"), py::arg("x"), py::arg("y"), py::arg("z") = 0);
        globals["World"] = world;

        py::object zombies = makeNamespace();
        zombies.attr("Count") = py::cpp_function([scriptApi]() -> int {
            return scriptApi != nullptr ? scriptApi->ZombieCount() : 0;
        });
        zombies.attr("Spawn") = py::cpp_function(
            [scriptApi](double x, double y, int z) -> std::uint64_t {
                if (scriptApi == nullptr) {
                    return 0;
                }
                return scriptApi->SpawnZombie(static_cast<float>(x), static_cast<float>(y), z);
            },
            py::arg("x"), py::arg("y"), py::arg("z") = 0);
        zombies.attr("GetPosition") = py::cpp_function(
            [scriptApi](std::uint64_t handle) -> py::object {
                ScriptVec3 position;
                if (scriptApi == nullptr || !scriptApi->GetZombiePosition(handle, position)) {
                    return py::none();
                }
                py::dict out;
                out["x"] = position.x;
                out["y"] = position.y;
                out["z"] = position.z;
                return out;
            },
            py::arg("handle"));
        globals["Zombies"] = zombies;

        py::object itemsApi = makeNamespace();
        itemsApi.attr("Exists") = py::cpp_function([scriptApi](const std::string& defId) -> bool {
            return scriptApi != nullptr && scriptApi->ItemExists(defId);
        });
        itemsApi.attr("GetName") = py::cpp_function(
            [scriptApi](const std::string& defId) -> std::string {
                return scriptApi != nullptr ? scriptApi->ItemName(defId) : std::string();
            },
            py::arg("defId"));
        globals["Items"] = itemsApi;

        py::object events = makeNamespace();
        events.attr("On") = py::cpp_function(
            [handlers](const std::string& name, py::function fn) {
                if (name.empty()) {
                    return;
                }
                (*handlers)[name].push_back(std::move(fn));
            },
            py::arg("name"), py::arg("fn"));
        events.attr("Off") = py::cpp_function(
            [handlers](const std::string& name, py::object fn) {
                const auto it = handlers->find(name);
                if (it == handlers->end()) {
                    return;  // 未订阅
                }
                if (fn.is_none()) {
                    handlers->erase(it);  // 取消该事件的全部订阅
                    return;
                }
                std::vector<py::function>& list = it->second;
                list.erase(std::remove_if(list.begin(), list.end(),
                                          [&fn](const py::function& handler) {
                                              return handler.is(fn);
                                          }),
                           list.end());
                if (list.empty()) {
                    handlers->erase(it);
                }
            },
            py::arg("name"), py::arg("fn") = py::none());
        globals["Events"] = events;
    }

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

        // 先在临时 globals 执行；成功才替换旧版本（重载失败保留旧版本；
        // 旧的订阅/定时器随旧 entry 一起释放 → 重载 = 清订阅后重执行）
        const auto handlers = std::make_shared<HandlerMap>();
        const auto timers = std::make_shared<std::vector<Timer>>();
        py::dict globals = MakeModGlobals(mod.id, path);
        BindApi(globals, mod.id, handlers, timers);
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

        mods.insert_or_assign(mod.id, ModEntry{mod, std::move(globals), handlers, timers});
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
    std::vector<ModError> errors;
    if (!m_impl->initialized) {
        return errors;
    }
    const auto it = m_impl->mods.find(modId);
    if (it == m_impl->mods.end()) {
        return errors;
    }
    Impl::ModEntry& entry = it->second;

    // GIL 纪律：派发只发生在主线程；入口统一 acquire。
    py::gil_scoped_acquire gil;

    auto handleError = [&](const std::string& prefix, const std::exception& e) {
        errors.push_back(ModError{modId, prefix + e.what()});
    };

    // 1) 定时器泵：仅在 OnTick 派发时检查（秒 → 游戏分钟，取当前游戏时钟）
    if (ev.name == core::kOnTick) {
        const double now = m_impl->api != nullptr ? m_impl->api->GetTotalGameMinutes() : 0.0;
        std::vector<Timer> due;
        std::vector<Timer>& timers = *entry.timers;
        for (auto timer = timers.begin(); timer != timers.end();) {
            if (timer->dueMinutes <= now) {
                due.push_back(std::move(*timer));
                timer = timers.erase(timer);
            } else {
                ++timer;
            }
        }
        for (Timer& timer : due) {
            try {
                timer.fn();
            } catch (const py::error_already_set& e) {
                handleError("SetTimer 回调错误：", e);
                return errors;  // mod 即将被禁用：不再继续泵
            } catch (const std::exception& e) {
                handleError("SetTimer 回调错误：", e);
                return errors;
            }
        }
    }

    // 2) Events.On 订阅派发（先拷贝本轮函数列表：回调中 On/Off 不影响当前遍历）
    const auto hit = entry.handlers->find(ev.name);
    if (hit == entry.handlers->end()) {
        return errors;
    }
    const std::vector<py::function> handlers = hit->second;
    if (handlers.empty()) {
        return errors;
    }
    const py::dict arg = MakeEventArgDict(ev);
    for (const py::function& handler : handlers) {
        try {
            handler(arg);
        } catch (const py::error_already_set& e) {
            handleError("事件 " + ev.name + " 处理错误：", e);
            return errors;
        } catch (const std::exception& e) {
            handleError("事件 " + ev.name + " 处理错误：", e);
            return errors;
        }
    }
    return errors;
}

const std::vector<std::string>& PythonRuntime::LoadedMods() const {
    return m_impl->loadedIds;
}

}  // namespace zl::script

#endif  // ZL_ENABLE_PYTHON
