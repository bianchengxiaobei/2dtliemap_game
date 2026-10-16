#include "script/LuaRuntime.h"

#include <sol/sol.hpp>

#include <algorithm>
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

namespace zl::script {
namespace {

constexpr const char* kModule = "script-lua";

// 沙箱白名单（docs/03 §7）：base 库中允许脚本使用的函数（其余不暴露）。
// os/io/debug/package 从不加载；dofile/loadfile/load 不在白名单内。
constexpr std::string_view kSandboxBaseFuncs[] = {
    "assert",  "error",     "getmetatable", "ipairs",  "next",     "pairs",
    "pcall",   "rawequal",  "rawget",       "rawlen",  "rawset",   "select",
    "setmetatable", "tonumber", "tostring",  "type",    "xpcall",
};

// 允许脚本使用的标准库表。
constexpr std::string_view kSandboxLibs[] = {"string", "table", "math", "coroutine", "utf8"};

// 死循环防护（M7-T7，docs/03 §7）：每次「外部进入脚本」的调用（chunk 执行 / 事件回调 /
// 定时器回调）最多执行约 kMaxInstructionsPerCall 条 Lua 指令；超限抛 Lua 错误 →
// 走既有 pcall + traceback 路径 → ModError 归因该 mod → manager 禁用。
// 说明：拦截的是显而易见的死循环（如 `while true do end`）；脚本自行 pcall 吞错的
// 对抗性循环不在防护范围（定位"防手滑"，信任本地 mod）。
constexpr std::size_t kMaxInstructionsPerCall = 5'000'000;
constexpr int kInstructionHookStep = 10'000;  // hook 每 1 万条指令触发一次

// print 重定向（upvalue[1] = mod id）：多参数以 \t 连接，走 luaL_tolstring（尊重 __tostring）。
int LuaPrintBridge(lua_State* L) {
    const char* modId = lua_tostring(L, lua_upvalueindex(1));
    std::string line;
    const int count = lua_gettop(L);
    for (int i = 1; i <= count; ++i) {
        if (i > 1) {
            line += '\t';
        }
        std::size_t length = 0;
        const char* text = luaL_tolstring(L, i, &length);  // push 结果
        if (text != nullptr) {
            line.append(text, length);
        }
        lua_pop(L, 1);
    }
    ZL_INFO(kModule, "[{}] {}", modId != nullptr ? modId : "?", line);
    return 0;
}

// 错误消息处理器：给 pcall 捕获的错误附加调用栈 traceback。
int LuaTracebackHandler(lua_State* L) {
    const char* message = lua_tostring(L, 1);
    if (message == nullptr) {
        message = "(错误值为非字符串)";
    }
    luaL_traceback(L, L, message, 1);
    return 1;
}

std::optional<std::string> ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();
    // 容忍 UTF-8 BOM（Windows 编辑器默认写入）：Lua 词法层不识别 BOM，会报
    // "unexpected symbol near '<\239>'"（M7-T5 实机踩坑）
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
}

// 裸建表（sol::table::create 静态重载被派生类成员隐藏）
sol::table NewTable(lua_State* L, int nrec = 8) {
    lua_createtable(L, 0, nrec);
    return sol::stack::pop<sol::table>(L);
}

sol::table MakePositionTable(lua_State* L, const ScriptVec3& position) {
    sol::table out = NewTable(L, 3);
    out["x"] = position.x;
    out["y"] = position.y;
    out["z"] = position.z;
    return out;
}

// 事件表（事件名 + 命名字段）→ Lua 表（字段名 → 值；monostate 跳过）
sol::table MakeEventArgTable(lua_State* L, const core::ScriptEvent& event) {
    sol::table out = NewTable(L, static_cast<int>(event.fields.size()) + 1);
    for (const core::ScriptField& field : event.fields) {
        if (const auto* value = std::get_if<std::int64_t>(&field.value)) {
            out[field.name] = *value;
        } else if (const auto* value = std::get_if<double>(&field.value)) {
            out[field.name] = *value;
        } else if (const auto* value = std::get_if<std::string>(&field.value)) {
            out[field.name] = *value;
        } else if (const auto* value = std::get_if<bool>(&field.value)) {
            out[field.name] = *value;
        }
    }
    return out;
}

}  // namespace

struct LuaRuntime::Impl {
    // Game.SetTimer 的一次性定时器（按游戏分钟计时；OnTick 派发时泵）
    struct Timer {
        sol::function fn;
        double dueMinutes = 0.0;
    };

    struct ModEntry {
        ModInfo info;
        sol::environment env;
        sol::table eventHandlers;  // 事件名 → 函数数组（Events.On 注册；On/Off 随时可变）
        std::shared_ptr<std::vector<Timer>> timers;
    };

    std::unique_ptr<sol::state> lua;
    sol::table sandboxGlobals;
    std::unordered_map<std::string, ModEntry> mods;
    std::vector<std::string> loadedIds;
    const ScriptApi* api = nullptr;
    bool initialized = false;
    // 指令预算（hook 触发，见 kMaxInstructionsPerCall；启动后仅由主线程访问）
    bool budgetActive = false;
    std::int64_t instructionBudget = 0;

    // 进入脚本调用前装配预算；调用结束（含出错）后解除
    void ArmCallBudget() {
        budgetActive = true;
        instructionBudget = static_cast<std::int64_t>(kMaxInstructionsPerCall);
    }
    void DisarmCallBudget() { budgetActive = false; }

    // Lua 指令计数 hook：预算耗尽 → 抛错（可与脚本错误同等归因/报告）
    static void InstructionHook(lua_State* L, lua_Debug*) {
        Impl* impl = *static_cast<Impl**>(lua_getextraspace(L));
        if (impl == nullptr || !impl->budgetActive) {
            return;  // 非脚本调用段（内部 C API 操作不经过 VM）
        }
        impl->instructionBudget -= kInstructionHookStep;
        if (impl->instructionBudget <= 0) {
            luaL_error(L, "脚本指令数超限（疑似死循环）：单次调用超过约 %d 万条指令，已中止",
                       static_cast<int>(kMaxInstructionsPerCall / 10000));
        }
    }

    // Host API 表（M7-T3/T4）：Game/Log/Player/World/Items/Zombies/Events；
    // 每 mod 独立绑定（Log 前缀、事件表与定时器均按 mod 隔离）。
    // api 为 nullptr 时绑定安全默认值的空壳。
    void BindApiTables(sol::environment& env, const std::string& modId, sol::table handlers,
                       const std::shared_ptr<std::vector<Timer>>& timers) {
        const ScriptApi* scriptApi = api;

        sol::table game = NewTable(lua->lua_state());
        game["GetTick"] = [scriptApi]() -> std::int64_t {
            return scriptApi != nullptr ? scriptApi->GetTick() : 0;
        };
        game["GetPlayerId"] = [scriptApi]() -> std::uint64_t {
            return scriptApi != nullptr ? scriptApi->GetPlayerId() : 0;
        };
        game["GetTime"] = [scriptApi](sol::this_state ts) -> sol::table {
            sol::table out = NewTable(ts.L, 6);
            if (scriptApi != nullptr) {
                const core::DateTime dt = scriptApi->GetTime();
                out["year"] = dt.year;
                out["month"] = dt.month;
                out["day"] = dt.day;
                out["hour"] = dt.hour;
                out["minute"] = dt.minute;
                out["totalMinutes"] = scriptApi->GetTotalGameMinutes();
            }
            return out;
        };
        // M7-T4：一次性定时器（seconds 为游戏秒；回调在后续 OnTick 泵出）
        game["SetTimer"] = [scriptApi, timers](double seconds, sol::function fn) {
            Timer timer;
            timer.fn = std::move(fn);
            const double now = scriptApi != nullptr ? scriptApi->GetTotalGameMinutes() : 0.0;
            timer.dueMinutes = now + (seconds > 0.0 ? seconds : 0.0) / 60.0;
            timers->push_back(std::move(timer));
        };
        env["Game"] = game;

        sol::table log = NewTable(lua->lua_state());
        log["Info"] = [modId](const std::string& message) {
            ZL_INFO("script-mod", "[{}] {}", modId, message);
        };
        log["Warn"] = [modId](const std::string& message) {
            ZL_WARN("script-mod", "[{}] {}", modId, message);
        };
        log["Error"] = [modId](const std::string& message) {
            ZL_ERROR("script-mod", "[{}] {}", modId, message);
        };
        env["Log"] = log;

        sol::table player = NewTable(lua->lua_state());
        player["GetPosition"] = [scriptApi](sol::this_state ts) -> std::optional<sol::table> {
            ScriptVec3 position;
            if (scriptApi == nullptr || !scriptApi->GetPlayerPosition(position)) {
                return std::nullopt;
            }
            return MakePositionTable(ts.L, position);
        };
        player["Teleport"] = [scriptApi](double x, double y, sol::optional<int> z) -> bool {
            return scriptApi != nullptr && scriptApi->TeleportPlayer(
                                               static_cast<float>(x), static_cast<float>(y),
                                               z.value_or(0));
        };
        player["GetHealth"] = [scriptApi](sol::this_state) -> std::optional<double> {
            float hp = 0.0f;
            if (scriptApi == nullptr || !scriptApi->GetPlayerHealth(hp)) {
                return std::nullopt;
            }
            return static_cast<double>(hp);
        };
        player["AddItem"] = [scriptApi](const std::string& defId, sol::optional<int> count) -> int {
            return scriptApi != nullptr ? scriptApi->AddPlayerItem(defId, count.value_or(1)) : 0;
        };
        env["Player"] = player;

        sol::table world = NewTable(lua->lua_state());
        world["IsPassable"] = [scriptApi](int gx, int gy) -> bool {
            return scriptApi != nullptr && scriptApi->IsPassable(gx, gy);
        };
        world["SpawnItem"] = [scriptApi](const std::string& defId, double x, double y,
                                         sol::optional<int> z) -> bool {
            return scriptApi != nullptr && scriptApi->SpawnItem(defId, static_cast<float>(x),
                                                                static_cast<float>(y), z.value_or(0));
        };
        env["World"] = world;

        sol::table zombies = NewTable(lua->lua_state());
        zombies["Count"] = [scriptApi]() -> int {
            return scriptApi != nullptr ? scriptApi->ZombieCount() : 0;
        };
        zombies["Spawn"] = [scriptApi](double x, double y, sol::optional<int> z) -> std::uint64_t {
            if (scriptApi == nullptr) {
                return 0;
            }
            return scriptApi->SpawnZombie(static_cast<float>(x), static_cast<float>(y),
                                          z.value_or(0));
        };
        zombies["GetPosition"] = [scriptApi](sol::this_state ts,
                                             std::uint64_t handle) -> std::optional<sol::table> {
            ScriptVec3 position;
            if (scriptApi == nullptr || !scriptApi->GetZombiePosition(handle, position)) {
                return std::nullopt;
            }
            return MakePositionTable(ts.L, position);
        };
        env["Zombies"] = zombies;

        sol::table itemsApi = NewTable(lua->lua_state());
        itemsApi["Exists"] = [scriptApi](const std::string& defId) -> bool {
            return scriptApi != nullptr && scriptApi->ItemExists(defId);
        };
        itemsApi["GetName"] = [scriptApi](const std::string& defId) -> std::string {
            return scriptApi != nullptr ? scriptApi->ItemName(defId) : std::string();
        };
        env["Items"] = itemsApi;

        // M7-T4：事件订阅（Events.On/Off）。注册表按 mod 隔离（每 mod 独立 handlers 表）；
        // 派发时先拷贝出本轮函数列表：回调中 On/Off 不破坏当前遍历。
        lua_State* L = lua->lua_state();
        sol::table events = NewTable(L, 4);
        events["On"] = [handlers, L](const std::string& name, sol::function fn) mutable {
            if (name.empty()) {
                return;
            }
            sol::object existing = handlers[name];
            sol::table list;
            if (existing.is<sol::table>()) {
                list = existing.as<sol::table>();
            } else {
                list = NewTable(L, 4);
                handlers[name] = list;
            }
            list[list.size() + 1] = fn;
        };
        events["Off"] =
            [handlers, L](const std::string& name, sol::optional<sol::function> fn) mutable {
            sol::object existing = handlers[name];
            if (!existing.is<sol::table>()) {
                return;  // 未订阅
            }
            if (!fn.has_value()) {
                handlers[name] = sol::nil;  // 取消该事件的全部订阅
                return;
            }
            sol::table list = existing.as<sol::table>();
            sol::table kept = NewTable(L, 4);
            std::size_t keptCount = 0;
            const std::size_t count = list.size();
            for (std::size_t i = 1; i <= count; ++i) {
                sol::object item = list[i];
                bool same = false;
                if (item.get_type() == sol::type::function) {
                    item.push();
                    fn->push();
                    same = lua_rawequal(L, -1, -2) != 0;
                    lua_pop(L, 2);
                }
                if (!same) {
                    kept[++keptCount] = item;
                }
            }
            handlers[name] = kept;
        };
        env["Events"] = events;
    }

    // 调用栈约定（自底向上）：[fn, args...]；成功后恢复栈，失败给出带 traceback 的错误。
    // M7-T7：装配指令预算（单次调用死循环防护）。
    bool CallStackWithTraceback(int nargs, std::string& errorOut) {
        lua_State* L = lua->lua_state();
        lua_pushcfunction(L, LuaTracebackHandler);
        lua_insert(L, -(nargs + 2));
        ArmCallBudget();
        const int status = lua_pcall(L, nargs, 0, -(nargs + 2));
        DisarmCallBudget();
        if (status != LUA_OK) {
            const char* message = lua_tostring(L, -1);
            errorOut = message != nullptr ? message : "(未知脚本错误)";
            lua_pop(L, 2);  // 错误消息 + 处理器
            return false;
        }
        lua_pop(L, 1);  // 处理器
        return true;
    }

    // 在指定沙箱环境中执行一段 chunk（chunkName 以 '@' 开头 → 错误显示相对路径:行号）
    bool RunChunk(const std::string& source, const std::string& chunkName, sol::environment& env,
                  std::string& errorOut) {
        lua_State* L = lua->lua_state();
        const int base = lua_gettop(L);

        if (luaL_loadbufferx(L, source.data(), source.size(), chunkName.c_str(), "t") != LUA_OK) {
            const char* message = lua_tostring(L, -1);
            errorOut = message != nullptr ? message : "(加载脚本失败)";
            lua_pop(L, 1);
            return false;
        }
        // 主 chunk 的 upvalue[1] = _ENV：指向该 mod 的沙箱环境
        env.push();
        if (lua_setupvalue(L, -2, 1) == nullptr) {
            lua_pop(L, 1);  // 无 _ENV upvalue（不应发生）：弹回环境表
        }

        // pcall + traceback 处理器（处理器在函数之下）；M7-T7：装配指令预算（防死循环）
        lua_pushcfunction(L, LuaTracebackHandler);
        lua_insert(L, -2);
        ArmCallBudget();
        const int status = lua_pcall(L, 0, 0, -2);
        DisarmCallBudget();
        if (status != LUA_OK) {
            const char* message = lua_tostring(L, -1);
            errorOut = message != nullptr ? message : "(未知脚本错误)";
            lua_pop(L, 2);  // 错误消息 + 处理器
            return false;
        }
        lua_pop(L, 1);  // 处理器
        return true;
    }

    sol::environment MakeModEnvironment(const std::string& modId, sol::table handlers,
                                        const std::shared_ptr<std::vector<Timer>>& timers) {
        lua_State* L = lua->lua_state();
        sol::environment env(*lua, sol::create, sandboxGlobals);

        env.push();                                       // [env]
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "_G");                        // env._G = env
        lua_pushstring(L, modId.c_str());
        lua_pushcclosure(L, LuaPrintBridge, 1);
        lua_setfield(L, -2, "print");                     // env.print = 重定向桥
        lua_pop(L, 1);

        BindApiTables(env, modId, handlers, timers);      // Host API 表（M7-T3/T4）
        return env;
    }

    bool LoadOrReloadMod(const ModInfo& mod, bool isReload, std::string& errorOut) {
        if (!initialized) {
            errorOut = "LuaRuntime 未初始化";
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

        // 先在临时环境执行；成功才替换旧环境（重载失败保留旧版本；
        // 旧环境连带的订阅/定时器随其一起释放 → 重载 = 清订阅后重执行）
        const sol::table handlers = NewTable(lua->lua_state(), 8);
        auto timers = std::make_shared<std::vector<Timer>>();
        sol::environment env = MakeModEnvironment(mod.id, handlers, timers);
        if (!RunChunk(*source, "@" + path, env, errorOut)) {
            return false;
        }

        mods.insert_or_assign(mod.id, ModEntry{mod, std::move(env), handlers, std::move(timers)});
        if (!isReload) {
            loadedIds.push_back(mod.id);
        }
        return true;
    }
};

LuaRuntime::LuaRuntime() : m_impl(std::make_unique<Impl>()) {}

LuaRuntime::~LuaRuntime() {
    Shutdown();  // sol::state 析构前释放环境引用
}

const char* LuaRuntime::Name() const {
    return "lua";
}

void LuaRuntime::SetApi(const ScriptApi* api) {
    m_impl->api = api;
}

bool LuaRuntime::Initialize(std::string& errorOut) {
    errorOut.clear();
    if (m_impl->initialized) {
        return true;
    }
    try {
        m_impl->lua = std::make_unique<sol::state>();
        m_impl->lua->open_libraries(sol::lib::base, sol::lib::math, sol::lib::string,
                                    sol::lib::table, sol::lib::coroutine, sol::lib::utf8);

        // M7-T7：extraspace 指向 Impl（指令 hook 取回用）+ 指令计数 hook
        lua_State* L = m_impl->lua->lua_state();
        *static_cast<Impl**>(lua_getextraspace(L)) = m_impl.get();
        lua_sethook(L, &Impl::InstructionHook, LUA_MASKCOUNT, kInstructionHookStep);

        // 构建沙箱基础表：白名单函数 + 安全标准库（globals 里没有 os/io/debug/package）
        sol::table globals = m_impl->lua->globals();
        lua_createtable(L, 0, 32);
        sol::table sandbox = sol::stack::pop<sol::table>(L);
        for (const std::string_view name : kSandboxBaseFuncs) {
            sandbox[name] = globals.get<sol::object>(name);
        }
        for (const std::string_view name : kSandboxLibs) {
            sandbox[name] = globals.get<sol::object>(name);
        }
        m_impl->sandboxGlobals = std::move(sandbox);
        m_impl->initialized = true;
        return true;
    } catch (const std::exception& e) {
        errorOut = std::string("Lua 解释器初始化异常：") + e.what();
        m_impl->lua.reset();
        return false;
    }
}

void LuaRuntime::Shutdown() {
    if (!m_impl || !m_impl->initialized) {
        return;
    }
    m_impl->mods.clear();  // 先释放 mod 环境引用
    m_impl->loadedIds.clear();
    m_impl->sandboxGlobals = sol::table();
    m_impl->lua.reset();  // lua_close
    m_impl->initialized = false;
}

bool LuaRuntime::LoadMod(const ModInfo& mod, std::string& errorOut) {
    errorOut.clear();
    return m_impl->LoadOrReloadMod(mod, /*isReload=*/false, errorOut);
}

bool LuaRuntime::ReloadMod(const std::string& modId, std::string& errorOut) {
    errorOut.clear();
    const auto it = m_impl->mods.find(modId);
    if (it == m_impl->mods.end()) {
        errorOut = "mod 未加载：" + modId;
        return false;
    }
    return m_impl->LoadOrReloadMod(it->second.info, /*isReload=*/true, errorOut);
}

void LuaRuntime::UnloadMod(const std::string& modId) {
    if (m_impl->mods.erase(modId) == 0) {
        return;
    }
    m_impl->loadedIds.erase(std::remove(m_impl->loadedIds.begin(), m_impl->loadedIds.end(), modId),
                            m_impl->loadedIds.end());
}

std::vector<ModError> LuaRuntime::DispatchEventToMod(const std::string& modId,
                                                     const core::ScriptEvent& ev) {
    std::vector<ModError> errors;
    Impl& impl = *m_impl;
    if (!impl.initialized) {
        return errors;
    }
    const auto it = impl.mods.find(modId);
    if (it == impl.mods.end()) {
        return errors;
    }
    Impl::ModEntry& entry = it->second;
    lua_State* L = impl.lua->lua_state();

    // 1) 定时器泵：仅在每 tick 的 OnTick 派发时检查（秒 → 游戏分钟，取当前游戏时钟）
    if (ev.name == core::kOnTick) {
        const double now = impl.api != nullptr ? impl.api->GetTotalGameMinutes() : 0.0;
        std::vector<Impl::Timer> due;
        std::vector<Impl::Timer>& timers = *entry.timers;
        for (auto timer = timers.begin(); timer != timers.end();) {
            if (timer->dueMinutes <= now) {
                due.push_back(std::move(*timer));
                timer = timers.erase(timer);
            } else {
                ++timer;
            }
        }
        for (Impl::Timer& timer : due) {
            std::string error;
            timer.fn.push();
            if (!impl.CallStackWithTraceback(0, error)) {
                errors.push_back(ModError{modId, "SetTimer 回调错误：" + error});
                return errors;  // mod 即将被禁用：不再继续泵
            }
        }
    }

    // 2) Events.On 订阅派发（先拷贝本轮函数列表：回调中 On/Off 不影响当前遍历）
    sol::object handlersObject = entry.eventHandlers[ev.name];
    if (!handlersObject.is<sol::table>()) {
        return errors;
    }
    sol::table list = handlersObject.as<sol::table>();
    std::vector<sol::function> handlers;
    const std::size_t count = list.size();
    for (std::size_t i = 1; i <= count; ++i) {
        sol::object item = list[i];
        if (item.get_type() == sol::type::function) {
            handlers.push_back(item.as<sol::function>());
        }
    }
    if (handlers.empty()) {
        return errors;
    }
    for (sol::function& handler : handlers) {
        std::string error;
        handler.push();
        sol::table arg = MakeEventArgTable(L, ev);
        arg.push();
        if (!impl.CallStackWithTraceback(1, error)) {
            errors.push_back(ModError{modId, "事件 " + ev.name + " 处理错误：" + error});
            return errors;
        }
    }
    return errors;
}

const std::vector<std::string>& LuaRuntime::LoadedMods() const {
    return m_impl->loadedIds;
}

}  // namespace zl::script
