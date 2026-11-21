#include "script/PythonRuntime.h"

#ifdef ZL_ENABLE_PYTHON

#include <pybind11/embed.h>
#include <pybind11/pybind11.h>

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "core/EventBus.h"
#include "core/Log.h"
#include "script/ScriptApi.h"
#include "script/ScriptUiApi.h"
#include "ui/HudState.h"
#include "ui/ScriptUiHost.h"

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

constexpr std::size_t kMaxJsonDepth = 16;
constexpr std::size_t kMaxJsonStringBytes = 64 * 1024;
constexpr std::size_t kMaxJsonContainerItems = 1024;

enum class PyJsonStatus {
    Ok,
    Cycle,
    Invalid,
};

bool ValidatePyJsonString(PyObject* value, PyJsonStatus& status) {
    Py_ssize_t size = 0;
    const char* text = PyUnicode_AsUTF8AndSize(value, &size);
    if (text == nullptr) {
        PyErr_Clear();
        status = PyJsonStatus::Invalid;
        return false;
    }
    if (size < 0 ||
        static_cast<std::size_t>(size) > kMaxJsonStringBytes) {
        status = PyJsonStatus::Invalid;
        return false;
    }
    return true;
}

bool ValidatePyJsonValue(py::handle value, std::size_t depth,
                         std::unordered_set<PyObject*>& path,
                         PyJsonStatus& status) {
    PyObject* object = value.ptr();
    if (object == Py_None || PyBool_Check(object)) {
        return true;
    }
    if (PyUnicode_Check(object)) {
        return ValidatePyJsonString(object, status);
    }
    if (PyLong_Check(object)) {
        return true;
    }
    if (PyFloat_Check(object)) {
        if (!std::isfinite(PyFloat_AS_DOUBLE(object))) {
            status = PyJsonStatus::Invalid;
            return false;
        }
        return true;
    }

    if (PyDict_Check(object)) {
        if (depth > kMaxJsonDepth ||
            PyDict_Size(object) > static_cast<Py_ssize_t>(
                                     kMaxJsonContainerItems)) {
            status = PyJsonStatus::Invalid;
            return false;
        }
        if (!path.insert(object).second) {
            status = PyJsonStatus::Cycle;
            return false;
        }
        bool ok = true;
        PyObject* key = nullptr;
        PyObject* item = nullptr;
        Py_ssize_t position = 0;
        while (PyDict_Next(object, &position, &key, &item)) {
            if (!PyUnicode_Check(key) ||
                !ValidatePyJsonString(key, status) ||
                !ValidatePyJsonValue(py::handle(item), depth + 1, path,
                                     status)) {
                ok = false;
                break;
            }
        }
        path.erase(object);
        return ok;
    }

    if (PyList_Check(object)) {
        const Py_ssize_t size = PyList_GET_SIZE(object);
        if (depth > kMaxJsonDepth ||
            size > static_cast<Py_ssize_t>(kMaxJsonContainerItems)) {
            status = PyJsonStatus::Invalid;
            return false;
        }
        if (!path.insert(object).second) {
            status = PyJsonStatus::Cycle;
            return false;
        }
        bool ok = true;
        for (Py_ssize_t i = 0; i < size; ++i) {
            if (!ValidatePyJsonValue(py::handle(PyList_GET_ITEM(object, i)),
                                     depth + 1, path, status)) {
                ok = false;
                break;
            }
        }
        path.erase(object);
        return ok;
    }

    status = PyJsonStatus::Invalid;
    return false;
}

std::optional<std::string> EncodePythonJson(py::handle value,
                                            const std::string& modId) {
    PyJsonStatus status = PyJsonStatus::Invalid;
    std::unordered_set<PyObject*> path;
    if (!ValidatePyJsonValue(value, 1, path, status)) {
        if (status == PyJsonStatus::Cycle) {
            ZL_WARN("script-mod", "[{}] Json.Encode 检测到循环引用", modId);
        }
        return std::nullopt;
    }

    try {
        py::object dumps = py::module_::import("json").attr("dumps");
        py::dict kwargs;
        kwargs["ensure_ascii"] = false;
        kwargs["sort_keys"] = true;
        kwargs["allow_nan"] = false;
        kwargs["separators"] = py::make_tuple(",", ":");
        py::tuple args = py::make_tuple(value);
        PyObject* result =
            PyObject_Call(dumps.ptr(), args.ptr(), kwargs.ptr());
        if (result == nullptr) {
            PyErr_Clear();
            return std::nullopt;
        }
        py::object text = py::reinterpret_steal<py::object>(result);
        if (!py::isinstance<py::str>(text)) {
            return std::nullopt;
        }
        return py::cast<std::string>(text);
    } catch (const py::error_already_set&) {
        PyErr_Clear();
        return std::nullopt;
    } catch (const py::cast_error&) {
        return std::nullopt;
    }
}

py::object DecodePythonJson(py::handle text) {
    try {
        py::object loads = py::module_::import("json").attr("loads");
        py::object parsed = loads(py::str(text));
        PyJsonStatus status = PyJsonStatus::Invalid;
        std::unordered_set<PyObject*> path;
        if (!ValidatePyJsonValue(parsed, 1, path, status)) {
            return py::none();
        }
        return parsed;
    } catch (const py::error_already_set&) {
        PyErr_Clear();
        return py::none();
    }
}

// M8-T3：一次性定时器（seconds 为游戏秒；回调在后续 OnTick 派发时泵出）。
struct Timer {
    py::function fn;
    double dueMinutes = 0.0;
};

// 事件名 → 订阅函数列表（每 mod 独立；Events.On/Off 随时可变）。
using HandlerMap = std::unordered_map<std::string, std::vector<py::function>>;

// 与 Lua 行为对齐：Lua 的 `function()` 回调会忽略多余实参，Python 不会。
// 对"0 形参且无 *args"的可调用对象按无参方式调用（否则照常传事件 dict）。
bool WantsNoArguments(const py::function& handler) {
    if (!py::hasattr(handler, "__code__")) {
        return false;  // 内建/可调用对象：按常规传参
    }
    try {
        const py::object code = handler.attr("__code__");
        const int argCount = py::cast<int>(code.attr("co_argcount"));
        const int flags = py::cast<int>(code.attr("co_flags"));
        return argCount == 0 && (flags & 0x04) == 0;  // CO_VARARGS = 0x04
    } catch (const py::error_already_set&) {
        return false;
    }
}

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

py::dict MakeItemStackDict(const ScriptItemStack& stack) {
    py::dict out;
    out["index"] = stack.index;
    out["defId"] = stack.defId;
    out["name"] = stack.name;
    out["type"] = stack.type;
    out["count"] = stack.count;
    out["condition"] = stack.condition;
    out["loadedAmmo"] = stack.loadedAmmo;
    out["weightKg"] = stack.weightKg;
    out["maxStack"] = stack.maxStack;
    out["usable"] = stack.usable;
    out["equippable"] = stack.equippable;
    out["perishable"] = stack.perishable;
    out["spoilDays"] = stack.spoilDays;
    return out;
}

py::dict MakeActionResultDict(const ScriptActionResult& result) {
    py::dict out;
    out["ok"] = result.ok;
    out["queued"] = result.queued;
    out["error"] = result.error;
    return out;
}

py::dict MakeXpResultDict(const ScriptXpResult& value) {
    py::dict out = MakeActionResultDict(value.result);
    out["characterLevels"] = value.award.characterLevels;
    out["skillLevels"] = value.award.skillLevels;
    out["perkPoints"] = value.award.perkPoints;
    out["characterXp"] = value.award.characterXp;
    out["skillXp"] = value.award.skillXp;
    return out;
}

std::string EquipSlotText(const py::object& slot, const char* fallback) {
    if (slot.is_none()) {
        return fallback;
    }
    if (py::isinstance<py::bool_>(slot)) {
        return "invalid";
    }
    if (py::isinstance<py::int_>(slot)) {
        const int index = slot.cast<int>();
        if (index == 0) {
            return "primary";
        }
        if (index == 1) {
            return "body";
        }
        return "invalid";
    }
    if (py::isinstance<py::str>(slot)) {
        return slot.cast<std::string>();
    }
    return "invalid";
}

py::dict MakeEntityInfoDict(const ScriptEntityInfo& info) {
    py::dict out;
    out["id"] = info.id;
    out["kind"] = info.kind;
    out["x"] = info.x;
    out["y"] = info.y;
    out["z"] = info.z;
    out["hasHealth"] = info.hasHealth;
    out["health"] = info.health;
    out["healthMax"] = info.healthMax;
    out["defId"] = info.defId;
    out["subKind"] = info.subKind;
    out["ownerId"] = info.ownerId;
    out["count"] = info.count;
    out["tableId"] = info.tableId;
    out["searched"] = info.searched;
    out["downed"] = info.downed;
    out["dead"] = info.dead;
    out["command"] = info.command;
    return out;
}

py::dict MakeRecipeIngredientDict(const ScriptRecipeIngredient& ingredient) {
    py::dict out;
    out["defId"] = ingredient.defId;
    out["label"] = ingredient.label;
    out["required"] = ingredient.required;
    out["have"] = ingredient.have;
    out["wear"] = ingredient.wear;
    return out;
}

py::dict MakeRecipeDict(const ScriptRecipeInfo& recipe) {
    py::list ingredients;
    for (const ScriptRecipeIngredient& ingredient : recipe.ingredients) {
        ingredients.append(MakeRecipeIngredientDict(ingredient));
    }
    py::list tools;
    for (const ScriptRecipeIngredient& tool : recipe.tools) {
        tools.append(MakeRecipeIngredientDict(tool));
    }
    py::dict out;
    out["index"] = recipe.index;
    out["id"] = recipe.id;
    out["name"] = recipe.name;
    out["category"] = recipe.category;
    out["categoryLabel"] = recipe.categoryLabel;
    out["outputDefId"] = recipe.outputDefId;
    out["outputLabel"] = recipe.outputLabel;
    out["outputCount"] = recipe.outputCount;
    out["seconds"] = recipe.seconds;
    out["station"] = recipe.station;
    out["stationLabel"] = recipe.stationLabel;
    out["stationNearby"] = recipe.stationNearby;
    out["known"] = recipe.known;
    out["canCraft"] = recipe.canCraft;
    out["ingredients"] = ingredients;
    out["tools"] = tools;
    if (recipe.hasCookware) {
        out["cookware"] = MakeRecipeIngredientDict(recipe.cookware);
    }
    if (recipe.hasFuel) {
        out["fuel"] = MakeRecipeIngredientDict(recipe.fuel);
    }
    return out;
}

py::dict MakeBuildableDict(const ScriptBuildableInfo& info) {
    py::dict out;
    out["index"] = info.index;
    out["id"] = info.id;
    out["name"] = info.name;
    out["sprite"] = info.sprite;
    out["woodCost"] = info.woodCost;
    out["maxHp"] = info.maxHp;
    out["blocksMovement"] = info.blocksMovement;
    out["storageKg"] = info.storageKg;
    out["storageSlots"] = info.storageSlots;
    out["layer"] = info.layer;
    out["category"] = info.category;
    out["door"] = info.door;
    out["requiresMission"] = info.requiresMission;
    return out;
}

py::dict MakeMissionDict(const ScriptMissionInfo& mission) {
    py::list progress;
    for (const int value : mission.progress) {
        progress.append(value);
    }
    py::list objectives;
    for (const ScriptMissionObjective& objective : mission.objectives) {
        py::dict entry;
        entry["kind"] = objective.kind;
        entry["target"] = objective.target;
        entry["count"] = objective.count;
        entry["progress"] = objective.progress;
        entry["description"] = objective.description;
        objectives.append(entry);
    }
    py::dict reward;
    reward["xp"] = mission.reward.xp;
    reward["skill"] = mission.reward.skill;
    reward["item"] = mission.reward.item;
    reward["itemCount"] = mission.reward.itemCount;
    reward["extraItem"] = mission.reward.extraItem;
    reward["extraItemCount"] = mission.reward.extraItemCount;
    reward["blueprint"] = mission.reward.blueprint;
    reward["label"] = mission.reward.label;

    py::dict out;
    out["index"] = mission.index;
    out["id"] = mission.id;
    out["name"] = mission.name;
    out["chain"] = mission.chain;
    out["order"] = mission.order;
    out["kind"] = mission.kind;
    out["source"] = mission.source;
    out["task"] = mission.task;
    out["status"] = mission.status;
    out["day"] = mission.day;
    out["progress"] = progress;
    out["objectives"] = objectives;
    out["reward"] = reward;
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
    ui::ScriptUiHost* uiHost = nullptr;

    std::unordered_map<std::string, ModEntry> mods;
    std::vector<std::string> loadedIds;

    // Host API（M8-T3）：与 Lua 侧同名同参数，绑定为 per-mod 命名空间对象。
    // api 为 nullptr 时绑定安全默认值。
    void BindApi(py::dict& globals, const ModInfo& mod,
                 const std::shared_ptr<HandlerMap>& handlers,
                 const std::shared_ptr<std::vector<Timer>>& timers) {
        const ScriptApi* scriptApi = api;
        const std::string& modId = mod.id;
        auto uiApi = std::make_shared<ScriptUiApi>(uiHost, mod.id, mod.rootPath);
        const auto makeNamespace = [] {
            return py::module_::import("types").attr("SimpleNamespace")();
        };

        py::object game = makeNamespace();
        game.attr("GetApiVersion") = py::cpp_function([scriptApi]() -> int {
            return scriptApi != nullptr ? scriptApi->GetApiVersion() : 0;
        });
        game.attr("IsMultiplayer") = py::cpp_function([scriptApi]() -> bool {
            return scriptApi != nullptr && scriptApi->IsMultiplayer();
        });
        game.attr("GetNetMode") = py::cpp_function([scriptApi]() -> std::string {
            return scriptApi != nullptr ? scriptApi->GetNetMode() : std::string("direct");
        });
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
        game.attr("ToggleInventory") = py::cpp_function([scriptApi]() -> bool {
            return scriptApi != nullptr && scriptApi->ToggleInventory();
        });
        game.attr("IsInventoryOpen") = py::cpp_function([scriptApi]() -> bool {
            return scriptApi != nullptr && scriptApi->IsInventoryOpen();
        });
        game.attr("IsMenuOpen") = py::cpp_function([scriptApi]() -> bool {
            return scriptApi != nullptr && scriptApi->IsMenuOpen();
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
        player.attr("GetNeeds") = py::cpp_function([scriptApi]() -> py::object {
            ScriptNeeds needs;
            if (scriptApi == nullptr || !scriptApi->GetPlayerNeeds(needs)) {
                return py::none();
            }
            py::dict out;
            out["hunger"] = needs.hunger;
            out["thirst"] = needs.thirst;
            out["fatigue"] = needs.fatigue;
            out["nutrition"] = needs.nutrition;
            out["health"] = needs.health;
            out["healthMax"] = needs.healthMax;
            out["moveScale"] = needs.moveScale;
            out["damageScale"] = needs.damageScale;
            out["warning"] = needs.warning;
            return out;
        });
        player.attr("GetStamina") = py::cpp_function([scriptApi]() -> py::object {
            ScriptStamina stamina;
            if (scriptApi == nullptr || !scriptApi->GetPlayerStamina(stamina)) {
                return py::none();
            }
            py::dict out;
            out["current"] = stamina.current;
            out["max"] = stamina.max;
            out["exhausted"] = stamina.exhausted;
            return out;
        });
        player.attr("GetWounds") = py::cpp_function([scriptApi]() -> py::object {
            ScriptWounds wounds;
            if (scriptApi == nullptr || !scriptApi->GetPlayerWounds(wounds)) {
                return py::none();
            }
            py::list list;
            for (const ScriptWound& wound : wounds.wounds) {
                py::dict entry;
                entry["type"] = wound.type;
                entry["severity"] = wound.severity;
                entry["bandaged"] = wound.bandaged;
                entry["bleeding"] = wound.bleeding;
                list.append(entry);
            }
            py::dict out;
            out["count"] = wounds.count;
            out["wounds"] = list;
            return out;
        });
        player.attr("GetPain") = py::cpp_function([scriptApi]() -> py::object {
            ScriptPain pain;
            if (scriptApi == nullptr || !scriptApi->GetPlayerPain(pain)) {
                return py::none();
            }
            py::dict out;
            out["level"] = pain.level;
            out["baseLevel"] = pain.baseLevel;
            out["reliefRemaining"] = pain.reliefRemaining;
            return out;
        });
        player.attr("GetInfection") = py::cpp_function([scriptApi]() -> py::object {
            ScriptInfection infection;
            if (scriptApi == nullptr || !scriptApi->GetPlayerInfection(infection)) {
                return py::none();
            }
            py::dict out;
            out["stage"] = infection.stage;
            out["progress"] = infection.progress;
            out["slowRemaining"] = infection.slowRemaining;
            out["timeToNextStage"] = infection.timeToNextStage;
            return out;
        });
        player.attr("GetClimate") = py::cpp_function([scriptApi]() -> py::object {
            ScriptClimate climate;
            if (scriptApi == nullptr || !scriptApi->GetPlayerClimate(climate)) {
                return py::none();
            }
            py::dict out;
            out["bodyC"] = climate.bodyC;
            out["wetness"] = climate.wetness;
            out["ambientC"] = climate.ambientC;
            out["effectiveC"] = climate.effectiveC;
            out["insulation"] = climate.insulation;
            out["waterproof"] = climate.waterproof;
            out["indoor"] = climate.indoor;
            out["nearFire"] = climate.nearFire;
            out["warning"] = climate.warning;
            return out;
        });
        player.attr("GetProgression") = py::cpp_function([scriptApi]() -> py::object {
            ScriptProgression progression;
            if (scriptApi == nullptr || !scriptApi->GetPlayerProgression(progression)) {
                return py::none();
            }
            py::dict skills;
            for (const ScriptSkillProgress& skill : progression.skills) {
                py::dict entry;
                entry["level"] = skill.level;
                entry["xp"] = skill.xp;
                entry["xpNext"] = skill.xpNext;
                skills[py::str(skill.id)] = entry;
            }
            py::list perks;
            for (const std::string& perk : progression.perks) {
                perks.append(perk);
            }
            py::dict out;
            out["level"] = progression.level;
            out["xp"] = progression.xp;
            out["xpNext"] = progression.xpNext;
            out["perkPoints"] = progression.perkPoints;
            out["skills"] = skills;
            out["perks"] = perks;
            return out;
        });
        player.attr("GetInventory") = py::cpp_function(
            [scriptApi](int offset, int limit) -> py::object {
                ScriptInventory inventory;
                if (scriptApi == nullptr ||
                    !scriptApi->GetPlayerInventory(offset, limit, inventory)) {
                    return py::none();
                }
                py::list stacks;
                for (const ScriptItemStack& stack : inventory.stacks) {
                    stacks.append(MakeItemStackDict(stack));
                }
                py::dict out;
                out["weightKg"] = inventory.weightKg;
                out["capacityKg"] = inventory.capacityKg;
                out["totalStacks"] = inventory.totalStacks;
                out["truncated"] = inventory.truncated;
                out["stacks"] = stacks;
                return out;
            },
            py::arg("offset") = 0, py::arg("limit") = 0);
        player.attr("GetEquipment") = py::cpp_function([scriptApi]() -> py::object {
            ScriptEquipment equipment;
            if (scriptApi == nullptr || !scriptApi->GetPlayerEquipment(equipment)) {
                return py::none();
            }
            py::dict out;
            if (equipment.hasPrimary) {
                out["primary"] = MakeItemStackDict(equipment.primary);
            }
            if (equipment.hasBody) {
                out["body"] = MakeItemStackDict(equipment.body);
            }
            return out;
        });
        player.attr("CountItem") = py::cpp_function(
            [scriptApi](const std::string& defId) -> int {
                return scriptApi != nullptr ? scriptApi->CountPlayerItem(defId) : 0;
            },
            py::arg("defId"));
        player.attr("HasItem") = py::cpp_function(
            [scriptApi](const std::string& defId, int count) -> bool {
                return scriptApi != nullptr &&
                       scriptApi->HasPlayerItem(defId, count);
            },
            py::arg("defId"), py::arg("count") = 1);
        player.attr("AddItem") = py::cpp_function(
            [scriptApi](const std::string& defId, int count) -> int {
                return scriptApi != nullptr ? scriptApi->AddPlayerItem(defId, count) : 0;
            },
            py::arg("defId"), py::arg("count") = 1);
        player.attr("UseItem") = py::cpp_function(
            [scriptApi](int index) -> py::dict {
                if (scriptApi == nullptr) {
                    return MakeActionResultDict(
                        ScriptActionResult{false, false, "脚本 API 不可用"});
                }
                return MakeActionResultDict(scriptApi->UseItem(index));
            },
            py::arg("index") = -1);
        player.attr("EquipItem") = py::cpp_function(
            [scriptApi](int index, const py::object& slot) -> py::dict {
                if (scriptApi == nullptr) {
                    return MakeActionResultDict(
                        ScriptActionResult{false, false, "脚本 API 不可用"});
                }
                return MakeActionResultDict(
                    scriptApi->EquipItem(index, EquipSlotText(slot, "primary")));
            },
            py::arg("index"), py::arg("slot") = py::none());
        player.attr("Unequip") = py::cpp_function(
            [scriptApi](const py::object& slot) -> py::dict {
                if (scriptApi == nullptr) {
                    return MakeActionResultDict(
                        ScriptActionResult{false, false, "脚本 API 不可用"});
                }
                return MakeActionResultDict(
                    scriptApi->Unequip(EquipSlotText(slot, "invalid")));
            },
            py::arg("slot"));
        player.attr("DropItem") = py::cpp_function(
            [scriptApi](int index) -> py::dict {
                if (scriptApi == nullptr) {
                    return MakeActionResultDict(
                        ScriptActionResult{false, false, "脚本 API 不可用"});
                }
                return MakeActionResultDict(scriptApi->DropItem(index));
            },
            py::arg("index") = -1);
        player.attr("PickupNearest") = py::cpp_function([scriptApi]() -> py::dict {
            if (scriptApi == nullptr) {
                return MakeActionResultDict(
                    ScriptActionResult{false, false, "脚本 API 不可用"});
            }
            return MakeActionResultDict(scriptApi->PickupNearest());
        });
        player.attr("Pickup") = py::cpp_function(
            [scriptApi](std::uint64_t entityId) -> py::dict {
                if (scriptApi == nullptr) {
                    return MakeActionResultDict(
                        ScriptActionResult{false, false, "脚本 API 不可用"});
                }
                return MakeActionResultDict(scriptApi->Pickup(entityId));
            },
            py::arg("entityId"));
        player.attr("Attack") = py::cpp_function(
            [scriptApi](std::uint64_t targetId, bool heavy) -> py::dict {
                if (scriptApi == nullptr) {
                    return MakeActionResultDict(
                        ScriptActionResult{false, false, "脚本 API 不可用"});
                }
                return MakeActionResultDict(scriptApi->Attack(targetId, heavy));
            },
            py::arg("targetId") = 0, py::arg("heavy") = false);
        player.attr("ApplyDamage") = py::cpp_function(
            [scriptApi](float amount, const py::object& damageType) -> py::dict {
                if (scriptApi == nullptr) {
                    return MakeActionResultDict(
                        ScriptActionResult{false, false, "脚本 API 不可用"});
                }
                const std::string type = damageType.is_none()
                                             ? "blunt"
                                             : damageType.cast<std::string>();
                return MakeActionResultDict(
                    scriptApi->ApplyDamage(amount, type));
            },
            py::arg("amount"), py::arg("damageType") = py::none());
        player.attr("AdjustNeeds") = py::cpp_function(
            [scriptApi](const py::object& hunger, const py::object& thirst,
                        const py::object& fatigue) -> py::dict {
                if (scriptApi == nullptr) {
                    return MakeActionResultDict(
                        ScriptActionResult{false, false, "脚本 API 不可用"});
                }
                const auto valueOrZero = [](const py::object& value) {
                    return value.is_none() ? 0.0f : value.cast<float>();
                };
                return MakeActionResultDict(scriptApi->AdjustNeeds(
                    valueOrZero(hunger), valueOrZero(thirst),
                    valueOrZero(fatigue)));
            },
            py::arg("hunger") = py::none(), py::arg("thirst") = py::none(),
            py::arg("fatigue") = py::none());
        player.attr("GiveXp") = py::cpp_function(
            [scriptApi](const std::string& skill, float amount) -> py::dict {
                if (scriptApi == nullptr) {
                    ScriptXpResult out;
                    out.result =
                        ScriptActionResult{false, false, "脚本 API 不可用"};
                    return MakeXpResultDict(out);
                }
                return MakeXpResultDict(scriptApi->GiveXp(skill, amount));
            },
            py::arg("skill"), py::arg("amount"));
        globals["Player"] = player;

        py::object world = makeNamespace();
        world.attr("IsPassable") = py::cpp_function([scriptApi](int gx, int gy) -> bool {
            return scriptApi != nullptr && scriptApi->IsPassable(gx, gy);
        });
        world.attr("GetSquare") = py::cpp_function(
            [scriptApi](int gx, int gy, int z) -> py::object {
                if (scriptApi == nullptr) {
                    return py::none();
                }
                ScriptSquare square;
                if (!scriptApi->GetSquare(gx, gy, z, square)) {
                    return py::none();
                }
                const auto makeObject = [](const ScriptSquareObject& object) {
                    py::dict out;
                    out["sprite"] = object.sprite;
                    out["hp"] = object.hp;
                    out["playerBuilt"] = object.playerBuilt;
                    out["blocksMovement"] = object.blocksMovement;
                    return out;
                };
                py::dict out;
                out["loaded"] = square.loaded;
                out["hasFloor"] = square.hasFloor;
                if (square.hasFloor) {
                    out["floor"] = makeObject(square.floor);
                }
                py::list wall;
                for (const ScriptSquareObject& object : square.wall) {
                    wall.append(makeObject(object));
                }
                out["wall"] = wall;
                py::list furniture;
                for (const ScriptSquareObject& object : square.furniture) {
                    furniture.append(makeObject(object));
                }
                out["furniture"] = furniture;
                py::list item;
                for (const ScriptSquareObject& object : square.item) {
                    item.append(makeObject(object));
                }
                out["item"] = item;
                return out;
            },
            py::arg("x"), py::arg("y"), py::arg("z") = 0);
        world.attr("SpawnItem") = py::cpp_function(
            [scriptApi](const std::string& defId, double x, double y, int z) -> bool {
                return scriptApi != nullptr && scriptApi->SpawnItem(
                                                   defId, static_cast<float>(x),
                                                   static_cast<float>(y), z);
            },
            py::arg("defId"), py::arg("x"), py::arg("y"), py::arg("z") = 0);
        world.attr("EmitSound") = py::cpp_function(
            [scriptApi](double x, double y, double radius,
                        const std::string& kind,
                        const std::string& clip) -> bool {
                return scriptApi != nullptr &&
                       scriptApi->EmitSound(static_cast<float>(x),
                                            static_cast<float>(y),
                                            static_cast<float>(radius), kind,
                                            clip);
            },
            py::arg("x"), py::arg("y"), py::arg("radius"),
            py::arg("kind") = "generic", py::arg("clip") = "");
        world.attr("GetWeather") = py::cpp_function([scriptApi]() -> py::object {
            ScriptWeather weather;
            if (scriptApi == nullptr || !scriptApi->GetWeather(weather)) {
                return py::none();
            }
            py::dict out;
            out["kind"] = weather.kind;
            out["ambientC"] = weather.ambientC;
            out["isNight"] = weather.isNight;
            return out;
        });
        world.attr("GetTimeOfDay") = py::cpp_function([scriptApi]() -> double {
            return scriptApi != nullptr ? scriptApi->GetTimeOfDay() : 0.0;
        });
        globals["World"] = world;

        py::object audio = makeNamespace();
        audio.attr("Play2D") = py::cpp_function(
            [scriptApi](const std::string& clip, double volume) -> bool {
                return scriptApi != nullptr &&
                       scriptApi->PlayAudio2D(
                           clip, static_cast<float>(volume));
            },
            py::arg("clip"), py::arg("volume") = 1.0);
        audio.attr("Play3D") = py::cpp_function(
            [scriptApi](const std::string& clip, double x, double y,
                        double radius, double volume) -> bool {
                return scriptApi != nullptr &&
                       scriptApi->PlayAudio3D(
                           clip, static_cast<float>(x),
                           static_cast<float>(y),
                           static_cast<float>(radius),
                           static_cast<float>(volume));
            },
            py::arg("clip"), py::arg("x"), py::arg("y"),
            py::arg("radius") = 0.0, py::arg("volume") = 1.0);
        globals["Audio"] = audio;

        py::object zombies = makeNamespace();
        zombies.attr("Count") = py::cpp_function([scriptApi]() -> int {
            return scriptApi != nullptr ? scriptApi->ZombieCount() : 0;
        });
        zombies.attr("Spawn") = py::cpp_function(
            [scriptApi](double x, double y, int z,
                        const std::string& variant) -> std::uint64_t {
                if (scriptApi == nullptr) {
                    return 0;
                }
                return scriptApi->SpawnZombie(static_cast<float>(x),
                                              static_cast<float>(y), z, variant);
            },
            py::arg("x"), py::arg("y"), py::arg("z") = 0,
            py::arg("variant") = "normal");
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

        py::object entities = makeNamespace();
        entities.attr("Get") = py::cpp_function(
            [scriptApi](std::uint64_t id) -> py::object {
                ScriptEntityInfo info;
                if (scriptApi == nullptr || !scriptApi->GetEntity(id, info)) {
                    return py::none();
                }
                return MakeEntityInfoDict(info);
            },
            py::arg("id"));
        entities.attr("Query") = py::cpp_function(
            [scriptApi, modId](double x, double y, int z, double radius,
                               const std::string& kind, int limit) -> py::dict {
                py::dict out;
                out["count"] = 0;
                out["truncated"] = false;
                out["entities"] = py::list();
                if (scriptApi == nullptr) {
                    return out;
                }
                ScriptEntityQuery query;
                if (!scriptApi->QueryEntitiesForMod(
                        modId, static_cast<float>(x), static_cast<float>(y), z,
                        static_cast<float>(radius), kind, limit, query)) {
                    return out;
                }
                py::list list;
                for (const ScriptEntityInfo& info : query.entities) {
                    list.append(MakeEntityInfoDict(info));
                }
                out["count"] = query.count;
                out["truncated"] = query.truncated;
                out["entities"] = list;
                return out;
            },
            py::arg("x"), py::arg("y"), py::arg("z") = 0,
            py::arg("radius") = 8.0, py::arg("kind") = "any",
            py::arg("limit") = 0);
        entities.attr("GetContainer") = py::cpp_function(
            [scriptApi](std::uint64_t id) -> py::object {
                ScriptContainerInfo container;
                if (scriptApi == nullptr ||
                    !scriptApi->GetEntityContainer(id, container)) {
                    return py::none();
                }
                py::list stacks;
                for (const ScriptItemStack& stack : container.stacks) {
                    stacks.append(MakeItemStackDict(stack));
                }
                py::dict out;
                out["id"] = container.id;
                out["tableId"] = container.tableId;
                out["searched"] = container.searched;
                out["isStorage"] = container.isStorage;
                out["capacityKg"] = container.capacityKg;
                out["weightKg"] = container.weightKg;
                out["totalStacks"] = container.totalStacks;
                out["truncated"] = container.truncated;
                out["stacks"] = stacks;
                return out;
            },
            py::arg("id"));
        globals["Entities"] = entities;

        py::object itemsApi = makeNamespace();
        itemsApi.attr("Exists") = py::cpp_function([scriptApi](const std::string& defId) -> bool {
            return scriptApi != nullptr && scriptApi->ItemExists(defId);
        });
        itemsApi.attr("GetName") = py::cpp_function(
            [scriptApi](const std::string& defId) -> std::string {
                return scriptApi != nullptr ? scriptApi->ItemName(defId) : std::string();
            },
            py::arg("defId"));
        itemsApi.attr("GetInfo") = py::cpp_function(
            [scriptApi](const std::string& defId) -> py::object {
                ScriptItemInfo info;
                if (scriptApi == nullptr || !scriptApi->GetItemInfo(defId, info)) {
                    return py::none();
                }
                py::dict out;
                out["id"] = info.id;
                out["name"] = info.name;
                out["icon"] = info.icon;
                out["weight"] = info.weight;
                out["maxStack"] = info.maxStack;
                out["type"] = info.type;
                out["spoilDays"] = info.spoilDays;
                out["repairEfficiency"] = info.repairEfficiency;
                out["usable"] = info.usable;
                out["equippable"] = info.equippable;
                out["throwable"] = info.throwable;
                out["ranged"] = info.ranged;
                return out;
            },
            py::arg("defId"));
        globals["Items"] = itemsApi;

        py::object craft = makeNamespace();
        craft.attr("List") = py::cpp_function([scriptApi]() -> py::dict {
            ScriptRecipeList recipes;
            if (scriptApi != nullptr) {
                (void)scriptApi->ListRecipes(recipes);
            }
            py::list list;
            for (const ScriptRecipeInfo& recipe : recipes.recipes) {
                list.append(MakeRecipeDict(recipe));
            }
            py::dict out;
            out["count"] = recipes.count;
            out["truncated"] = recipes.truncated;
            out["recipes"] = list;
            return out;
        });
        craft.attr("GetState") = py::cpp_function([scriptApi]() -> py::dict {
            ScriptCraftState state;
            if (scriptApi != nullptr) {
                (void)scriptApi->GetCraftState(state);
            }
            py::list queue;
            for (const ScriptCraftQueueEntry& entry : state.queue) {
                py::dict item;
                item["recipeId"] = entry.recipeId;
                item["count"] = entry.count;
                queue.append(item);
            }
            py::dict out;
            out["active"] = state.active;
            out["recipeId"] = state.recipeId;
            out["outputDefId"] = state.outputDefId;
            out["outputLabel"] = state.outputLabel;
            out["outputCount"] = state.outputCount;
            out["elapsed"] = state.elapsed;
            out["duration"] = state.duration;
            out["progress"] = state.progress;
            out["batchTotal"] = state.batchTotal;
            out["batchCompleted"] = state.batchCompleted;
            out["queue"] = queue;
            return out;
        });
        craft.attr("IsKnown") = py::cpp_function(
            [scriptApi](const std::string& recipeId) -> bool {
                return scriptApi != nullptr &&
                       scriptApi->IsRecipeKnown(recipeId);
            },
            py::arg("recipeId"));
        craft.attr("Start") = py::cpp_function(
            [scriptApi](const std::string& recipeId, int count) -> py::dict {
                if (scriptApi == nullptr) {
                    return MakeActionResultDict(
                        ScriptActionResult{false, false, "脚本 API 不可用"});
                }
                return MakeActionResultDict(
                    scriptApi->StartCraft(recipeId, count));
            },
            py::arg("recipeId"), py::arg("count") = 1);
        craft.attr("Cancel") = py::cpp_function([scriptApi]() -> py::dict {
            if (scriptApi == nullptr) {
                return MakeActionResultDict(
                    ScriptActionResult{false, false, "脚本 API 不可用"});
            }
            return MakeActionResultDict(scriptApi->CancelCraft());
        });
        craft.attr("Learn") = py::cpp_function(
            [scriptApi](const std::string& recipeId) -> py::dict {
                if (scriptApi == nullptr) {
                    return MakeActionResultDict(
                        ScriptActionResult{false, false, "脚本 API 不可用"});
                }
                return MakeActionResultDict(
                    scriptApi->LearnRecipe(recipeId));
            },
            py::arg("recipeId"));
        globals["Craft"] = craft;

        py::object build = makeNamespace();
        build.attr("List") = py::cpp_function([scriptApi]() -> py::dict {
            ScriptBuildableList buildables;
            if (scriptApi != nullptr) {
                (void)scriptApi->ListBuildables(buildables);
            }
            py::list list;
            for (const ScriptBuildableInfo& info : buildables.buildables) {
                list.append(MakeBuildableDict(info));
            }
            py::dict out;
            out["count"] = buildables.count;
            out["truncated"] = buildables.truncated;
            out["items"] = list;
            return out;
        });
        build.attr("CanPlace") = py::cpp_function(
            [scriptApi](const std::string& defId, int x, int y,
                        bool vertical) -> py::dict {
                if (scriptApi == nullptr) {
                    return MakeActionResultDict(
                        ScriptActionResult{false, false, "脚本 API 不可用"});
                }
                return MakeActionResultDict(
                    scriptApi->CanPlaceBuild(defId, x, y, vertical));
            },
            py::arg("defId"), py::arg("x"), py::arg("y"),
            py::arg("vertical") = false);
        build.attr("Place") = py::cpp_function(
            [scriptApi](const std::string& defId, int x, int y,
                        bool vertical) -> py::dict {
                if (scriptApi == nullptr) {
                    return MakeActionResultDict(
                        ScriptActionResult{false, false, "脚本 API 不可用"});
                }
                return MakeActionResultDict(
                    scriptApi->PlaceBuild(defId, x, y, vertical));
            },
            py::arg("defId"), py::arg("x"), py::arg("y"),
            py::arg("vertical") = false);
        build.attr("Demolish") = py::cpp_function(
            [scriptApi](int x, int y) -> py::dict {
                if (scriptApi == nullptr) {
                    return MakeActionResultDict(
                        ScriptActionResult{false, false, "脚本 API 不可用"});
                }
                return MakeActionResultDict(
                    scriptApi->DemolishBuild(x, y));
            },
            py::arg("x"), py::arg("y"));
        build.attr("Repair") = py::cpp_function(
            [scriptApi](int x, int y) -> py::dict {
                if (scriptApi == nullptr) {
                    return MakeActionResultDict(
                        ScriptActionResult{false, false, "脚本 API 不可用"});
                }
                return MakeActionResultDict(scriptApi->RepairBuild(x, y));
            },
            py::arg("x"), py::arg("y"));
        build.attr("ToggleDoor") = py::cpp_function(
            [scriptApi](int x, int y) -> py::dict {
                if (scriptApi == nullptr) {
                    return MakeActionResultDict(
                        ScriptActionResult{false, false, "脚本 API 不可用"});
                }
                return MakeActionResultDict(
                    scriptApi->ToggleBuildDoor(x, y));
            },
            py::arg("x"), py::arg("y"));
        globals["Build"] = build;

        py::object mission = makeNamespace();
        mission.attr("List") = py::cpp_function(
            [scriptApi](const std::string& filter) -> py::dict {
                ScriptMissionList missions;
                if (scriptApi != nullptr) {
                    (void)scriptApi->ListMissions(filter, missions);
                }
                py::list list;
                for (const ScriptMissionInfo& info : missions.missions) {
                    list.append(MakeMissionDict(info));
                }
                py::dict out;
                out["count"] = missions.count;
                out["truncated"] = missions.truncated;
                out["missions"] = list;
                return out;
            },
            py::arg("filter") = "all");
        mission.attr("Get") = py::cpp_function(
            [scriptApi](const std::string& id) -> py::object {
                ScriptMissionInfo info;
                if (scriptApi == nullptr || !scriptApi->GetMission(id, info)) {
                    return py::none();
                }
                return MakeMissionDict(info);
            },
            py::arg("id"));
        globals["Mission"] = mission;

        py::object contentApi = makeNamespace();
        contentApi.attr("RegisterItems") = py::cpp_function(
            [scriptApi, modId](const std::string& json) -> int {
                std::string error;
                const int added = scriptApi != nullptr
                                      ? scriptApi->RegisterItems(modId, json, error)
                                      : 0;
                if (added == 0 && !error.empty()) {
                    ZL_WARN("script-mod", "[{}] {}", modId, error);
                }
                return added;
            },
            py::arg("json"));
        contentApi.attr("RegisterRecipes") = py::cpp_function(
            [scriptApi, modId](const std::string& json) -> int {
                std::string error;
                const int added = scriptApi != nullptr
                                      ? scriptApi->RegisterRecipes(modId, json, error)
                                      : 0;
                if (added == 0 && !error.empty()) {
                    ZL_WARN("script-mod", "[{}] {}", modId, error);
                }
                return added;
            },
            py::arg("json"));
        contentApi.attr("RegisterBuildings") = py::cpp_function(
            [scriptApi, modId](const std::string& json) -> int {
                std::string error;
                const int added = scriptApi != nullptr
                                      ? scriptApi->RegisterBuildings(modId, json, error)
                                      : 0;
                if (added == 0 && !error.empty()) {
                    ZL_WARN("script-mod", "[{}] {}", modId, error);
                }
                return added;
            },
            py::arg("json"));
        contentApi.attr("RegisterPois") = py::cpp_function(
            [scriptApi, modId](const std::string& json) -> int {
                std::string error;
                const int added = scriptApi != nullptr
                                      ? scriptApi->RegisterPois(modId, json, error)
                                      : 0;
                if (added == 0 && !error.empty()) {
                    ZL_WARN("script-mod", "[{}] {}", modId, error);
                }
                return added;
            },
            py::arg("json"));
        contentApi.attr("RegisterMissions") = py::cpp_function(
            [scriptApi, modId](const std::string& json) -> int {
                std::string error;
                const int added = scriptApi != nullptr
                                      ? scriptApi->RegisterMissions(modId, json, error)
                                      : 0;
                if (added == 0 && !error.empty()) {
                    ZL_WARN("script-mod", "[{}] {}", modId, error);
                }
                return added;
            },
            py::arg("json"));
        globals["Content"] = contentApi;

        py::object hud = makeNamespace();
        hud.attr("GetState") = py::cpp_function([scriptApi]() -> py::dict {
            ui::HudState state;
            if (scriptApi != nullptr) {
                (void)scriptApi->GetHudState(state);
            }
            py::dict out;
            out["active"] = state.active;
            out["day"] = state.day;
            out["time"] = state.time;
            out["speed"] = state.speed;
            out["playerValid"] = state.playerValid;
            out["health"] = state.health;
            out["healthMax"] = state.healthMax;
            out["stamina"] = state.stamina;
            out["staminaMax"] = state.staminaMax;
            out["combatStatus"] = state.combatStatus;
            out["combatTone"] = state.combatTone;
            out["stealthValid"] = state.stealthValid;
            out["stealthCrouched"] = state.stealthCrouched;
            out["stealthMoveSpeed"] = state.stealthMoveSpeed;
            out["stealthFootstepRadius"] = state.stealthFootstepRadius;
            out["stealthAwarenessValid"] = state.stealthAwarenessValid;
            out["stealthNearbyAlertCount"] = state.stealthNearbyAlertCount;
            out["stealthBackstabAvailable"] = state.stealthBackstabAvailable;
            out["stealthStatus"] = state.stealthStatus;
            out["stealthTone"] = state.stealthTone;
            out["stealthPrompt"] = state.stealthPrompt;
            out["rangedValid"] = state.rangedValid;
            out["rangedName"] = state.rangedName;
            out["rangedLoaded"] = state.rangedLoaded;
            out["rangedMagazine"] = state.rangedMagazine;
            out["rangedReserve"] = state.rangedReserve;
            out["rangedReloading"] = state.rangedReloading;
            out["rangedReloadProgress"] = state.rangedReloadProgress;
            out["rangedReloadRemaining"] = state.rangedReloadRemaining;
            out["rangedStatus"] = state.rangedStatus;
            out["rangedTone"] = state.rangedTone;
            out["throwValid"] = state.throwValid;
            out["throwName"] = state.throwName;
            out["throwRange"] = state.throwRange;
            out["throwReturnable"] = state.throwReturnable;
            out["throwBreakChance"] = state.throwBreakChance;
            out["throwFire"] = state.throwFire;
            out["throwFireRadius"] = state.throwFireRadius;
            out["throwCooldownRemaining"] = state.throwCooldownRemaining;
            out["throwCooldownDuration"] = state.throwCooldownDuration;
            out["throwLastResult"] = state.throwLastResult;
            out["throwLastTone"] = state.throwLastTone;
            out["throwTone"] = state.throwTone;
            out["throwStatus"] = state.throwStatus;
            out["trapValid"] = state.trapValid;
            out["trapName"] = state.trapName;
            out["trapEffect"] = state.trapEffect;
            out["trapArmed"] = state.trapArmed;
            out["trapArming"] = state.trapArming;
            out["trapArmRemaining"] = state.trapArmRemaining;
            out["trapUses"] = state.trapUses;
            out["trapUsesMax"] = state.trapUsesMax;
            out["trapCooldownRemaining"] = state.trapCooldownRemaining;
            out["trapCooldownDuration"] = state.trapCooldownDuration;
            out["trapReadOnly"] = state.trapReadOnly;
            out["trapStatus"] = state.trapStatus;
            out["trapTone"] = state.trapTone;
            out["forageValid"] = state.forageValid;
            out["forageName"] = state.forageName;
            out["forageToolName"] = state.forageToolName;
            out["forageMissingTool"] = state.forageMissingTool;
            out["forageExhausted"] = state.forageExhausted;
            out["forageRespawnMinutes"] = state.forageRespawnMinutes;
            out["forageSeconds"] = state.forageSeconds;
            out["forageSearching"] = state.forageSearching;
            out["forageProgress"] = state.forageProgress;
            out["forageStatus"] = state.forageStatus;
            out["forageTone"] = state.forageTone;
            out["fishingValid"] = state.fishingValid;
            out["fishingName"] = state.fishingName;
            out["fishingRodName"] = state.fishingRodName;
            out["fishingBaitName"] = state.fishingBaitName;
            out["fishingMissingRod"] = state.fishingMissingRod;
            out["fishingMissingBait"] = state.fishingMissingBait;
            out["fishingSeconds"] = state.fishingSeconds;
            out["fishingWaiting"] = state.fishingWaiting;
            out["fishingProgress"] = state.fishingProgress;
            out["fishingBiting"] = state.fishingBiting;
            out["fishingBiteRemaining"] = state.fishingBiteRemaining;
            out["fishingStatus"] = state.fishingStatus;
            out["fishingTone"] = state.fishingTone;
            out["needsValid"] = state.needsValid;
            out["needsStatus"] = state.needsStatus;
            out["needsTone"] = state.needsTone;
            out["woundStatus"] = state.woundStatus;
            out["woundTone"] = state.woundTone;
            out["painStatus"] = state.painStatus;
            out["painTone"] = state.painTone;
            out["armorStatus"] = state.armorStatus;
            out["armorTone"] = state.armorTone;
            out["infectionStatus"] = state.infectionStatus;
            out["infectionTone"] = state.infectionTone;
            out["weatherStatus"] = state.weatherStatus;
            out["weatherTone"] = state.weatherTone;
            out["weatherClass"] = state.weatherClass;
            out["threatStatus"] = state.threatStatus;
            out["threatDirection"] = state.threatDirection;
            out["threatRemaining"] = state.threatRemaining;
            out["threatDuration"] = state.threatDuration;
            out["objective"] = state.objective;
            out["objectiveTone"] = state.objectiveTone;
            out["objectiveHint"] = state.objectiveHint;
            out["poiGuide"] = state.poiGuide;
            out["poiGuideTone"] = state.poiGuideTone;
            out["poiStageVisible"] = state.poiStageVisible;
            out["poiStageTitle"] = state.poiStageTitle;
            out["poiStageObjective"] = state.poiStageObjective;
            out["poiStageMeta"] = state.poiStageMeta;
            out["poiStageNarrative"] = state.poiStageNarrative;
            out["poiStageTone"] = state.poiStageTone;
            out["poiResultVisible"] = state.poiResultVisible;
            out["poiResultTitle"] = state.poiResultTitle;
            out["poiResultBody"] = state.poiResultBody;
            out["poiResultTone"] = state.poiResultTone;
            out["missionGuide"] = state.missionGuide;
            out["missionTone"] = state.missionTone;
            out["eventGuide"] = state.eventGuide;
            out["eventTone"] = state.eventTone;
            out["worldEventVisible"] = state.worldEventVisible;
            out["worldEventCanChoose"] = state.worldEventCanChoose;
            out["worldEventChoiceOpen"] = state.worldEventChoiceOpen;
            out["worldEventName"] = state.worldEventName;
            out["worldEventMeta"] = state.worldEventMeta;
            out["worldEventStage"] = state.worldEventStage;
            out["worldEventHint"] = state.worldEventHint;
            out["worldEventTone"] = state.worldEventTone;
            out["worldEventResult"] = state.worldEventResult;
            out["worldEventResultTone"] = state.worldEventResultTone;
            out["worldEventOptionCount"] = state.worldEventOptionCount;
            out["worldEventLogCount"] = state.worldEventLogCount;
            py::list worldEventOptions;
            for (const std::string& option : state.worldEventOptions) {
                worldEventOptions.append(option);
            }
            out["worldEventOptions"] = worldEventOptions;
            py::list worldEventLog;
            for (const std::string& line : state.worldEventLog) {
                worldEventLog.append(line);
            }
            out["worldEventLog"] = worldEventLog;
            out["regionGuide"] = state.regionGuide;
            out["regionTone"] = state.regionTone;
            out["settlementGuide"] = state.settlementGuide;
            out["settlementTone"] = state.settlementTone;
            out["strategicGuide"] = state.strategicGuide;
            out["strategicTone"] = state.strategicTone;
            out["siegeStatus"] = state.siegeStatus;
            out["siegeTone"] = state.siegeTone;
            out["siegeReportOpen"] = state.siegeReportOpen;
            out["siegeReportTitle"] = state.siegeReportTitle;
            out["siegeReportRating"] = state.siegeReportRating;
            out["siegeReportTone"] = state.siegeReportTone;
            out["siegeReportHint"] = state.siegeReportHint;
            py::list siegeReportLines;
            for (const std::string& line : state.siegeReportLines) {
                siegeReportLines.append(line);
            }
            out["siegeReportLines"] = siegeReportLines;
            out["baseActive"] = state.baseActive;
            out["baseStorageCount"] = state.baseStorageCount;
            out["baseStorageWeight"] = state.baseStorageWeight;
            out["baseStorageCapacity"] = state.baseStorageCapacity;
            out["baseBeds"] = state.baseBeds;
            out["baseCampfires"] = state.baseCampfires;
            out["baseWorkbenches"] = state.baseWorkbenches;
            out["baseDefensesIntact"] = state.baseDefensesIntact;
            out["baseDefensesDamaged"] = state.baseDefensesDamaged;
            out["baseFarms"] = state.baseFarms;
            out["baseFarmGrowing"] = state.baseFarmGrowing;
            out["baseFarmMature"] = state.baseFarmMature;
            out["baseFarmDiseased"] = state.baseFarmDiseased;
            out["baseFarmThirsty"] = state.baseFarmThirsty;
            out["baseFarmFertilized"] = state.baseFarmFertilized;
            out["baseGenerators"] = state.baseGenerators;
            out["baseGeneratorsOnline"] = state.baseGeneratorsOnline;
            out["baseFridges"] = state.baseFridges;
            out["baseFridgesPowered"] = state.baseFridgesPowered;
            out["baseTraps"] = state.baseTraps;
            out["baseTrapsArmed"] = state.baseTrapsArmed;
            out["baseFloodlights"] = state.baseFloodlights;
            out["baseFloodlightsPowered"] = state.baseFloodlightsPowered;
            out["baseElectricFences"] = state.baseElectricFences;
            out["baseElectricFencesPowered"] = state.baseElectricFencesPowered;
            out["basePowerFuelMinutes"] = state.basePowerFuelMinutes;
            out["basePowerLoad"] = state.basePowerLoad;
            out["basePowerLowFuel"] = state.basePowerLowFuel;
            out["basePowerOutage"] = state.basePowerOutage;
            out["baseStatus"] = state.baseStatus;
            out["baseTone"] = state.baseTone;
            out["progressionValid"] = state.progressionValid;
            out["progressionLevel"] = state.progressionLevel;
            out["progressionXp"] = state.progressionXp;
            out["progressionXpNext"] = state.progressionXpNext;
            out["progressionPerkPoints"] = state.progressionPerkPoints;
            out["progressionPerks"] = state.progressionPerks;
            out["progressionChoiceCount"] = state.progressionChoiceCount;
            out["perkPrompt"] = state.perkPrompt;
            out["characterOpen"] = state.characterOpen;
            py::dict companion;
            companion["valid"] = state.companion.valid;
            companion["open"] = state.companion.open;
            companion["name"] = state.companion.name;
            companion["health"] = state.companion.health;
            companion["status"] = state.companion.status;
            companion["trust"] = state.companion.trust;
            companion["equipment"] = state.companion.equipment;
            companion["bag"] = state.companion.bag;
            companion["quest"] = state.companion.quest;
            companion["hint"] = state.companion.hint;
            companion["alert"] = state.companion.alert;
            companion["alertTone"] = state.companion.alertTone;
            companion["lowHealth"] = state.companion.lowHealth;
            companion["downed"] = state.companion.downed;
            companion["dead"] = state.companion.dead;
            companion["healthCurrent"] = state.companion.healthCurrent;
            companion["healthMax"] = state.companion.healthMax;
            out["companion"] = companion;
            py::list skills;
            for (const std::string& line : state.progressionSkills) {
                skills.append(line);
            }
            out["progressionSkills"] = skills;
            py::list choices;
            for (const std::string& line : state.progressionChoices) {
                choices.append(line);
            }
            out["progressionChoices"] = choices;
            out["dialogueOpen"] = state.dialogueOpen;
            out["dialogueSpeaker"] = state.dialogueSpeaker;
            out["dialogueText"] = state.dialogueText;
            out["dialogueOptionCount"] = state.dialogueOptionCount;
            out["dialogueSelected"] = state.dialogueSelected;
            out["dialogueHint"] = state.dialogueHint;
            py::list dialogueOptions;
            for (const std::string& option : state.dialogueOptions) {
                dialogueOptions.append(option);
            }
            out["dialogueOptions"] = dialogueOptions;
            out["actionHint"] = state.actionHint;
            out["actionTone"] = state.actionTone;
            out["selectedSlot"] = state.selectedSlot;
            out["notice"] = state.notice;
            out["noticeTone"] = state.noticeTone;
            out["noticeRemaining"] = state.noticeRemaining;
            out["noticeDuration"] = state.noticeDuration;
            py::list slots;
            for (const ui::HudSlot& slot : state.slots) {
                py::dict item;
                item["label"] = slot.label;
                item["count"] = slot.count;
                item["throwable"] = slot.throwable;
                item["throwTag"] = slot.throwTag;
                slots.append(item);
            }
            out["slots"] = slots;
            py::list modFields;
            for (int i = 0; i < state.modFieldCount; ++i) {
                const ui::HudModField& field =
                    state.modFields[static_cast<std::size_t>(i)];
                py::dict item;
                item["modId"] = field.modId;
                item["id"] = field.id;
                item["label"] = field.label;
                item["text"] = field.text;
                item["tone"] = field.tone;
                modFields.append(item);
            }
            out["modFields"] = modFields;
            return out;
        });
        hud.attr("RegisterField") = py::cpp_function(
            [scriptApi, modId](const std::string& id,
                               const std::string& label) -> bool {
                return scriptApi != nullptr &&
                       scriptApi->RegisterHudField(modId, id, label);
            },
            py::arg("id"), py::arg("label"));
        hud.attr("SetField") = py::cpp_function(
            [scriptApi, modId](const std::string& id, const std::string& text,
                               const std::string& tone) -> bool {
                return scriptApi != nullptr &&
                       scriptApi->SetHudField(modId, id, text, tone);
            },
            py::arg("id"), py::arg("text"), py::arg("tone") = "info");
        hud.attr("ClearField") = py::cpp_function(
            [scriptApi, modId](const std::string& id) -> bool {
                return scriptApi != nullptr &&
                       scriptApi->ClearHudField(modId, id);
            },
            py::arg("id"));
        globals["Hud"] = hud;

        py::object menu = makeNamespace();
        menu.attr("GetState") = py::cpp_function([scriptApi]() -> py::dict {
            ui::MenuState state;
            if (scriptApi != nullptr) {
                (void)scriptApi->GetMenuState(state);
            }
            py::dict out;
            out["open"] = state.open;
            out["pauseMode"] = state.pauseMode;
            out["canContinue"] = state.canContinue;
            out["volume"] = state.settings.volume;
            out["musicVolume"] = state.settings.musicVolume;
            out["sfxVolume"] = state.settings.sfxVolume;
            out["ambientVolume"] = state.settings.ambientVolume;
            py::list bindings;
            for (const ui::MenuKeyBinding& binding : state.settings.bindings) {
                py::dict item;
                item["action"] = binding.action;
                item["label"] = binding.label;
                item["key"] = binding.key;
                bindings.append(item);
            }
            out["bindings"] = bindings;
            return out;
        });
        menu.attr("Action") = py::cpp_function(
            [scriptApi](const std::string& action) -> bool {
                return scriptApi != nullptr && scriptApi->MenuAction(action);
            },
            py::arg("action"));
        menu.attr("ApplySettings") = py::cpp_function(
            [scriptApi](py::dict settings) -> py::dict {
                py::dict result;
                ui::MenuSettings parsed;
                parsed.volume = py::cast<int>(settings["volume"]);
                parsed.musicVolume = py::cast<int>(settings["musicVolume"]);
                parsed.sfxVolume = py::cast<int>(settings["sfxVolume"]);
                parsed.ambientVolume = py::cast<int>(settings["ambientVolume"]);
                py::object rawBindings = settings["bindings"];
                if (!py::isinstance<py::list>(rawBindings)) {
                    result["ok"] = false;
                    result["error"] = "bindings 必须是列表";
                    return result;
                }
                for (py::handle item : rawBindings) {
                    if (!py::isinstance<py::dict>(item)) {
                        result["ok"] = false;
                        result["error"] = "bindings 项必须是 dict";
                        return result;
                    }
                    py::dict entry = py::reinterpret_borrow<py::dict>(item);
                    ui::MenuKeyBinding binding;
                    binding.action = py::cast<std::string>(entry["action"]);
                    binding.label = py::cast<std::string>(entry["label"]);
                    binding.key = py::cast<std::string>(entry["key"]);
                    parsed.bindings.push_back(std::move(binding));
                }
                std::string error;
                const bool ok = scriptApi != nullptr &&
                                scriptApi->ApplyMenuSettings(parsed, error);
                result["ok"] = ok;
                result["error"] = ok ? std::string() : (error.empty() ? "保存设置失败" : error);
                return result;
            },
            py::arg("settings"));
        menu.attr("KeyName") = py::cpp_function(
            [scriptApi](int key) -> std::string {
                return scriptApi != nullptr ? scriptApi->MenuKeyName(key) : std::string();
            },
            py::arg("key"));
        menu.attr("PlayClick") = py::cpp_function([scriptApi] {
            if (scriptApi != nullptr) {
                scriptApi->PlayUiClick();
            }
        });
        globals["Menu"] = menu;

        py::object save = makeNamespace();
        save.attr("List") = py::cpp_function([scriptApi]() -> py::dict {
            std::vector<ScriptSaveInfo> saves;
            std::string error;
            const bool ok = scriptApi != nullptr && scriptApi->ListSaves(saves, error);
            py::dict out;
            out["ok"] = ok;
            out["error"] = ok ? std::string() : (error.empty() ? "读取存档列表失败" : error);
            py::list list;
            for (const ScriptSaveInfo& entry : saves) {
                py::dict item;
                item["id"] = entry.id;
                item["name"] = entry.name;
                item["updatedText"] = entry.updatedText;
                item["mapId"] = entry.mapId;
                item["gameVersion"] = entry.gameVersion;
                list.append(item);
            }
            out["saves"] = list;
            return out;
        });
        save.attr("Load") = py::cpp_function(
            [scriptApi](const std::string& id) -> py::dict {
                std::string error;
                const bool ok = scriptApi != nullptr && scriptApi->LoadSave(id, error);
                py::dict out;
                out["ok"] = ok;
                out["error"] = ok ? std::string() : (error.empty() ? "读取存档失败" : error);
                return out;
            },
            py::arg("id"));
        save.attr("Delete") = py::cpp_function(
            [scriptApi](const std::string& id) -> py::dict {
                std::string error;
                const bool ok = scriptApi != nullptr && scriptApi->DeleteSave(id, error);
                py::dict out;
                out["ok"] = ok;
                out["error"] = ok ? std::string() : (error.empty() ? "删除存档失败" : error);
                return out;
            },
            py::arg("id"));
        save.attr("QuickSave") = py::cpp_function([scriptApi]() -> bool {
            return scriptApi != nullptr && scriptApi->QuickSave();
        });
        globals["Save"] = save;

        auto modDataWarned = std::make_shared<bool>(false);
        auto warnModDataBlocked = [scriptApi, modId, modDataWarned]() {
            if (scriptApi == nullptr || !scriptApi->IsMultiplayer() ||
                *modDataWarned) {
                return;
            }
            *modDataWarned = true;
            ZL_WARN("script-mod",
                    "[{}] Mod.Data 在联机模式下不可用（后续不再警告）", modId);
        };

        py::object modDataApi = makeNamespace();
        modDataApi.attr("Get") = py::cpp_function(
            [scriptApi, modId, warnModDataBlocked](
                const std::string& key) -> py::object {
                if (scriptApi == nullptr) {
                    return py::none();
                }
                if (scriptApi->IsMultiplayer()) {
                    warnModDataBlocked();
                    return py::none();
                }
                const std::optional<std::string> value =
                    scriptApi->GetModData(modId, key);
                return value.has_value() ? py::object(py::str(*value))
                                         : py::object(py::none());
            },
            py::arg("key"));
        modDataApi.attr("Set") = py::cpp_function(
            [scriptApi, modId, warnModDataBlocked](
                const std::string& key, py::object value) -> bool {
                if (scriptApi == nullptr) {
                    return false;
                }
                if (scriptApi->IsMultiplayer()) {
                    warnModDataBlocked();
                    return false;
                }
                if (!py::isinstance<py::str>(value)) {
                    return false;
                }
                return scriptApi->SetModData(
                    modId, key, py::cast<std::string>(value));
            },
            py::arg("key"), py::arg("value"));
        modDataApi.attr("Remove") = py::cpp_function(
            [scriptApi, modId, warnModDataBlocked](
                const std::string& key) -> bool {
                if (scriptApi == nullptr) {
                    return false;
                }
                if (scriptApi->IsMultiplayer()) {
                    warnModDataBlocked();
                    return false;
                }
                return scriptApi->RemoveModData(modId, key);
            },
            py::arg("key"));
        modDataApi.attr("Keys") = py::cpp_function(
            [scriptApi, modId, warnModDataBlocked]() -> py::list {
                py::list out;
                if (scriptApi == nullptr) {
                    return out;
                }
                if (scriptApi->IsMultiplayer()) {
                    warnModDataBlocked();
                    return out;
                }
                for (const std::string& key :
                     scriptApi->ListModDataKeys(modId)) {
                    out.append(key);
                }
                return out;
            });
        modDataApi.attr("Clear") =
            py::cpp_function([scriptApi, modId, warnModDataBlocked]() -> bool {
                if (scriptApi == nullptr) {
                    return false;
                }
                if (scriptApi->IsMultiplayer()) {
                    warnModDataBlocked();
                    return false;
                }
                return scriptApi->ClearModData(modId);
            });

        py::object modApi = makeNamespace();
        modApi.attr("Id") = py::cpp_function([modId]() -> std::string {
            return modId;
        });
        modApi.attr("Data") = modDataApi;
        globals["Mod"] = modApi;

        py::object jsonApi = makeNamespace();
        jsonApi.attr("Array") = py::cpp_function([]() -> py::list {
            return py::list();
        });
        jsonApi.attr("Null") = py::cpp_function([]() -> py::object {
            return py::none();
        });
        jsonApi.attr("Encode") = py::cpp_function(
            [modId](py::object value) -> py::object {
                const std::optional<std::string> text =
                    EncodePythonJson(value, modId);
                return text.has_value() ? py::object(py::str(*text))
                                        : py::object(py::none());
            },
            py::arg("value"));
        jsonApi.attr("Decode") = py::cpp_function(
            [](py::object text) -> py::object {
                if (!py::isinstance<py::str>(text)) {
                    return py::none();
                }
                return DecodePythonJson(text);
            },
            py::arg("text"));
        globals["Json"] = jsonApi;

        py::object inventory = makeNamespace();
        inventory.attr("GetState") = py::cpp_function([scriptApi]() -> py::dict {
            ui::InventoryState state;
            if (scriptApi != nullptr) {
                (void)scriptApi->GetInventoryState(state);
            }
            py::dict out;
            out["open"] = state.open;
            out["playerValid"] = state.playerValid;
            out["weight"] = state.weight;
            out["capacity"] = state.capacity;
            out["bagStacks"] = state.bagStacks;
            out["lootStacks"] = state.lootStacks;
            out["lootTitle"] = state.lootTitle;
            out["lootIsStorage"] = state.lootIsStorage;
            out["lootIsVehicle"] = state.lootIsVehicle;
            out["vehicleModsEnabled"] = state.vehicleModsEnabled;
            out["vehicleModsReadOnly"] = state.vehicleModsReadOnly;
            out["lootCanTakeAll"] = state.lootCanTakeAll;
            out["lootEntityId"] = state.lootEntityId;
            out["status"] = state.status;
            out["statusTone"] = state.statusTone;
            py::list recipes;
            for (const ui::InventoryRecipe& entry : state.recipes) {
                py::dict recipe;
                recipe["id"] = entry.id;
                recipe["name"] = entry.name;
                recipe["outputDefId"] = entry.outputDefId;
                recipe["outputLabel"] = entry.outputLabel;
                recipe["outputCount"] = entry.outputCount;
                recipe["known"] = entry.known;
                recipe["canCraft"] = entry.canCraft;
                recipe["estimatedSeconds"] = entry.estimatedSeconds;
                recipe["station"] = entry.station;
                recipe["stationLabel"] = entry.stationLabel;
                recipe["stationNearby"] = entry.stationNearby;
                recipe["category"] = entry.category;
                recipe["categoryLabel"] = entry.categoryLabel;
                auto appendExtraRequirement =
                    [&](const char* key,
                        const std::optional<ui::InventoryRecipeIngredient>&
                            requirement) {
                        if (!requirement.has_value()) {
                            return;
                        }
                        py::dict item;
                        item["label"] = requirement->label;
                        item["required"] = requirement->required;
                        item["have"] = requirement->have;
                        recipe[key] = item;
                    };
                appendExtraRequirement("cookware", entry.cookware);
                appendExtraRequirement("fuel", entry.fuel);
                py::list ingredients;
                for (const ui::InventoryRecipeIngredient& ingredient :
                     entry.ingredients) {
                    py::dict item;
                    item["label"] = ingredient.label;
                    item["required"] = ingredient.required;
                    item["have"] = ingredient.have;
                    ingredients.append(item);
                }
                recipe["ingredients"] = ingredients;
                py::list tools;
                for (const ui::InventoryRecipeIngredient& tool : entry.tools) {
                    py::dict item;
                    item["label"] = tool.label;
                    item["required"] = tool.required;
                    item["have"] = tool.have;
                    tools.append(item);
                }
                recipe["tools"] = tools;
                recipes.append(recipe);
            }
            out["recipes"] = recipes;
            py::dict crafting;
            crafting["active"] = state.crafting.active;
            crafting["recipeId"] = state.crafting.recipeId;
            crafting["outputLabel"] = state.crafting.outputLabel;
            crafting["outputCount"] = state.crafting.outputCount;
            crafting["elapsed"] = state.crafting.elapsed;
            crafting["duration"] = state.crafting.duration;
            crafting["progress"] = state.crafting.progress;
            crafting["toolEfficiency"] = state.crafting.toolEfficiency;
            crafting["batchCount"] = state.crafting.batchCount;
            crafting["completedCount"] = state.crafting.completedCount;
            py::list queue;
            for (const ui::InventoryQueuedCraft& entry : state.crafting.queue) {
                py::dict item;
                item["recipeId"] = entry.recipeId;
                item["outputLabel"] = entry.outputLabel;
                item["outputCount"] = entry.outputCount;
                item["count"] = entry.count;
                queue.append(item);
            }
            crafting["queue"] = queue;
            out["crafting"] = crafting;
            auto appendSlots = [](py::list& target, const auto& source) {
                for (const ui::InventorySlot& entry : source) {
                    py::dict slot;
                    slot["filled"] = entry.filled;
                    slot["label"] = entry.label;
                    slot["count"] = entry.count;
                    slot["usable"] = entry.usable;
                    slot["equippable"] = entry.equippable;
                    slot["entityId"] = entry.entityId;
                    slot["condition"] = entry.condition;
                    slot["damaged"] = entry.damaged;
                    slot["repairAvailable"] = entry.repairAvailable;
                    slot["perishable"] = entry.perishable;
                    slot["throwable"] = entry.throwable;
                    slot["throwTag"] = entry.throwTag;
                    target.append(slot);
                }
            };
            py::list bag;
            py::list equipment;
            py::list loot;
            appendSlots(bag, state.bag);
            appendSlots(equipment, state.equipment);
            appendSlots(loot, state.loot);
            out["bag"] = bag;
            out["equipment"] = equipment;
            out["loot"] = loot;
            return out;
        });
        inventory.attr("Action") = py::cpp_function(
            [scriptApi](py::dict action) -> bool {
                if (scriptApi == nullptr) {
                    return false;
                }
                auto parseArea = [](const std::string& value,
                                    ui::InventoryArea& out) -> bool {
                    if (value == "bag" || value == "backpack") {
                        out = ui::InventoryArea::Backpack;
                    } else if (value == "equipment") {
                        out = ui::InventoryArea::Equipment;
                    } else if (value == "loot") {
                        out = ui::InventoryArea::Loot;
                    } else {
                        return false;
                    }
                    return true;
                };
                ui::InventoryAction parsed;
                const std::string type = action.contains("type")
                                             ? py::cast<std::string>(action["type"])
                                             : std::string();
                if (type == "transfer") {
                    parsed.type = ui::InventoryActionType::Transfer;
                } else if (type == "use") {
                    parsed.type = ui::InventoryActionType::Use;
                } else if (type == "equip") {
                    parsed.type = ui::InventoryActionType::Equip;
                } else if (type == "unequip") {
                    parsed.type = ui::InventoryActionType::Unequip;
                } else if (type == "drop") {
                    parsed.type = ui::InventoryActionType::Drop;
                } else if (type == "pickup") {
                    parsed.type = ui::InventoryActionType::Pickup;
                } else if (type == "craft") {
                    parsed.type = ui::InventoryActionType::Craft;
                } else if (type == "cancel_craft") {
                    parsed.type = ui::InventoryActionType::CancelCraft;
                } else if (type == "repair") {
                    parsed.type = ui::InventoryActionType::Repair;
                } else if (type == "queue_up") {
                    parsed.type = ui::InventoryActionType::QueueMoveUp;
                } else if (type == "queue_down") {
                    parsed.type = ui::InventoryActionType::QueueMoveDown;
                } else if (type == "queue_remove") {
                    parsed.type = ui::InventoryActionType::QueueRemove;
                } else if (type == "queue_count") {
                    parsed.type = ui::InventoryActionType::QueueSetCount;
                } else if (type == "store_all") {
                    parsed.type = ui::InventoryActionType::StoreAll;
                } else if (type == "take_all") {
                    parsed.type = ui::InventoryActionType::TakeAll;
                } else if (type == "sort_storage") {
                    parsed.type = ui::InventoryActionType::SortStorage;
                } else if (type == "open_vehicle_mods") {
                    parsed.type = ui::InventoryActionType::OpenVehicleMods;
                } else {
                    return false;
                }
                const std::string from =
                    action.contains("from") ? py::cast<std::string>(action["from"]) : "bag";
                const std::string to =
                    action.contains("to") ? py::cast<std::string>(action["to"]) : "bag";
                if (!parseArea(from, parsed.from) || !parseArea(to, parsed.to)) {
                    return false;
                }
                parsed.fromIndex = action.contains("fromIndex")
                                        ? py::cast<int>(action["fromIndex"])
                                        : -1;
                parsed.toIndex = action.contains("toIndex")
                                      ? py::cast<int>(action["toIndex"])
                                      : -1;
                parsed.fromEntityId = action.contains("fromEntityId")
                                          ? py::cast<std::uint64_t>(action["fromEntityId"])
                                          : 0;
                parsed.count = std::clamp(
                    action.contains("count") ? py::cast<int>(action["count"]) : 1, 1, 99);
                return scriptApi->InventoryAction(parsed);
            },
            py::arg("action"));
        inventory.attr("Close") = py::cpp_function([scriptApi]() -> bool {
            if (scriptApi == nullptr || !scriptApi->IsInventoryOpen()) {
                return false;
            }
            return !scriptApi->ToggleInventory();
        });
        inventory.attr("PlayClick") = py::cpp_function([scriptApi] {
            if (scriptApi != nullptr) {
                scriptApi->PlayInventoryClick();
            }
        });
        globals["Inventory"] = inventory;

        py::object ui = makeNamespace();
        ui.attr("LoadDocument") = py::cpp_function(
            [uiApi](const std::string& documentId, const std::string& path, bool show,
                    bool modal, const std::string& group) {
                return uiApi->LoadDocument(documentId, path, show, modal, group);
            },
            py::arg("documentId"), py::arg("path"), py::arg("show") = true,
            py::arg("modal") = false, py::arg("group") = std::string());
        ui.attr("OpenDocument") = py::cpp_function(
            [uiApi](const std::string& documentId, const std::string& path, bool modal,
                    const std::string& group) {
                return uiApi->OpenDocument(documentId, path, modal, group);
            },
            py::arg("documentId"), py::arg("path"), py::arg("modal") = true,
            py::arg("group") = std::string());
        ui.attr("CloseDocument") = py::cpp_function(
            [uiApi](const std::string& documentId, bool unload) {
                return uiApi->CloseDocument(documentId, unload);
            },
            py::arg("documentId"), py::arg("unload") = false);
        ui.attr("UnloadDocument") = py::cpp_function(
            [uiApi](const std::string& documentId) {
                return uiApi->UnloadDocument(documentId);
            },
            py::arg("documentId"));
        ui.attr("UnloadGroup") = py::cpp_function(
            [uiApi](const std::string& group) { uiApi->UnloadGroup(group); },
            py::arg("group"));
        ui.attr("Show") = py::cpp_function(
            [uiApi](const std::string& documentId, bool visible) {
                return uiApi->Show(documentId, visible);
            },
            py::arg("documentId"), py::arg("visible") = true);
        ui.attr("IsLoaded") = py::cpp_function(
            [uiApi](const std::string& documentId) {
                return uiApi->IsLoaded(documentId);
            },
            py::arg("documentId"));
        ui.attr("IsVisible") = py::cpp_function(
            [uiApi](const std::string& documentId) {
                return uiApi->IsVisible(documentId);
            },
            py::arg("documentId"));
        ui.attr("SetModal") = py::cpp_function(
            [uiApi](const std::string& documentId, bool modal) {
                return uiApi->SetModal(documentId, modal);
            },
            py::arg("documentId"), py::arg("modal"));
        ui.attr("IsInputCaptured") = py::cpp_function(
            [uiApi]() { return uiApi->IsInputCaptured(); });
        ui.attr("SetText") = py::cpp_function(
            [uiApi](const std::string& documentId, const std::string& elementId,
                    const std::string& text) {
                return uiApi->SetText(documentId, elementId, text);
            },
            py::arg("documentId"), py::arg("elementId"), py::arg("text"));
        ui.attr("SetAttribute") = py::cpp_function(
            [uiApi](const std::string& documentId, const std::string& elementId,
                    const std::string& name, const std::string& value) {
                return uiApi->SetAttribute(documentId, elementId, name, value);
            },
            py::arg("documentId"), py::arg("elementId"), py::arg("name"),
            py::arg("value"));
        ui.attr("RemoveAttribute") = py::cpp_function(
            [uiApi](const std::string& documentId, const std::string& elementId,
                    const std::string& name) {
                return uiApi->RemoveAttribute(documentId, elementId, name);
            },
            py::arg("documentId"), py::arg("elementId"), py::arg("name"));
        ui.attr("SetClass") = py::cpp_function(
            [uiApi](const std::string& documentId, const std::string& elementId,
                    const std::string& className, bool enabled) {
                return uiApi->SetClass(documentId, elementId, className, enabled);
            },
            py::arg("documentId"), py::arg("elementId"), py::arg("className"),
            py::arg("enabled") = true);
        ui.attr("SetProperty") = py::cpp_function(
            [uiApi](const std::string& documentId, const std::string& elementId,
                    const std::string& property, const std::string& value) {
                return uiApi->SetProperty(documentId, elementId, property, value);
            },
            py::arg("documentId"), py::arg("elementId"), py::arg("property"),
            py::arg("value"));
        ui.attr("GetValue") = py::cpp_function(
            [uiApi](const std::string& documentId, const std::string& elementId) {
                return uiApi->GetValue(documentId, elementId);
            },
            py::arg("documentId"), py::arg("elementId"));
        ui.attr("SetValue") = py::cpp_function(
            [uiApi](const std::string& documentId, const std::string& elementId,
                    const std::string& value) {
                return uiApi->SetValue(documentId, elementId, value);
            },
            py::arg("documentId"), py::arg("elementId"), py::arg("value"));
        ui.attr("Focus") = py::cpp_function(
            [uiApi](const std::string& documentId, const std::string& elementId) {
                return uiApi->Focus(documentId, elementId);
            },
            py::arg("documentId"), py::arg("elementId"));
        globals["UI"] = ui;

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
        BindApi(globals, mod, handlers, timers);
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

void PythonRuntime::SetUiHost(ui::ScriptUiHost* host) {
    m_impl->uiHost = host;
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
            if (WantsNoArguments(handler)) {
                handler();
            } else {
                handler(arg);
            }
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
