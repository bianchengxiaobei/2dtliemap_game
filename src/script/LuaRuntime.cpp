#include "script/LuaRuntime.h"

#include <sol/sol.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/EventBus.h"
#include "core/Log.h"
#include "script/ScriptApi.h"
#include "script/ScriptUiApi.h"
#include "ui/HudState.h"
#include "ui/ScriptUiHost.h"

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

// F4-T4：Lua require 只读 <modRoot>/scripts/**，单文件 1 MB、递归 32 层。
constexpr std::size_t kMaxLuaModuleBytes = 1024 * 1024;
constexpr int kMaxRequireDepth = 32;

bool EndsWithLuaExtension(std::string_view name) {
    if (name.size() < 4) {
        return false;
    }
    const std::string_view suffix = name.substr(name.size() - 4);
    return std::tolower(static_cast<unsigned char>(suffix[0])) == '.' &&
           std::tolower(static_cast<unsigned char>(suffix[1])) == 'l' &&
           std::tolower(static_cast<unsigned char>(suffix[2])) == 'u' &&
           std::tolower(static_cast<unsigned char>(suffix[3])) == 'a';
}

// 把 require 参数规范化为 scripts/ 下的相对路径：
// - "wave.director" -> "wave/director.lua"（标准 Lua 点号分隔）
// - "./x" / ".\\x"  -> relative=true，去掉前缀后相对调用文件目录
// - 禁止绝对路径、盘符/ADS 冒号、空段、"." / ".." 段
bool NormalizeRequireName(std::string_view name, bool& relative,
                          std::string& pathOut, std::string& errorOut) {
    relative = false;
    if (name.empty()) {
        errorOut = "require: 模块名不能为空";
        return false;
    }
    if (name.find('\0') != std::string_view::npos) {
        errorOut = "require: 模块名包含非法字符";
        return false;
    }
    if (name.size() >= 2 && name[0] == '.' &&
        (name[1] == '/' || name[1] == '\\')) {
        relative = true;
        name.remove_prefix(2);
    }
    if (name.empty()) {
        errorOut = "require: 模块名不能为空";
        return false;
    }
    if (EndsWithLuaExtension(name)) {
        name.remove_suffix(4);
    }
    if (name.empty()) {
        errorOut = "require: 模块名不能为空";
        return false;
    }

    std::string normalized;
    normalized.reserve(name.size() + 4);
    for (char ch : name) {
        if (ch == '\\' || ch == '.') {
            normalized.push_back('/');
        } else {
            normalized.push_back(ch);
        }
    }
    if (normalized.empty() || normalized.front() == '/' ||
        normalized.find(':') != std::string::npos) {
        errorOut = "require: 非法模块名（禁止绝对路径/盘符/..）";
        return false;
    }

    std::size_t start = 0;
    while (start <= normalized.size()) {
        const std::size_t slash = normalized.find('/', start);
        const std::size_t end = slash == std::string::npos ? normalized.size() : slash;
        const std::string_view component(normalized.data() + start, end - start);
        if (component.empty() || component == "." || component == "..") {
            errorOut = "require: 非法模块名（禁止绝对路径/..）";
            return false;
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }

    pathOut = normalized;
    pathOut += ".lua";
    return true;
}

// 取 require 调用者的源码路径（chunkName 以 '@' 开头）。C 回调/无源码时返回空。
std::filesystem::path CallerSourcePath(lua_State* L) {
    lua_Debug ar{};
    if (lua_getstack(L, 1, &ar) == 0 || lua_getinfo(L, "S", &ar) == 0 ||
        ar.source == nullptr || ar.source[0] != '@') {
        return {};
    }
    std::error_code ec;
    const std::filesystem::path source(ar.source + 1);
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(source, ec);
    return ec ? source : canonical;
}

// canonical 必须落在 scriptsRoot 内（canonical 已解析 symlink）。
bool RelativeWithinRoot(const std::filesystem::path& canonical,
                        const std::filesystem::path& root) {
    std::error_code ec;
    const std::filesystem::path relative =
        std::filesystem::relative(canonical, root, ec);
    if (ec || relative.empty() || relative.is_absolute() ||
        relative.has_root_path()) {
        return false;
    }
    const auto first = relative.begin();
    return first == relative.end() || *first != "..";
}

std::string CanonicalScriptsRoot(const std::string& rootPath) {
    std::error_code ec;
    const std::filesystem::path raw =
        std::filesystem::path(rootPath) / "scripts";
    const std::filesystem::path absolute = std::filesystem::absolute(raw, ec);
    const std::filesystem::path base = ec ? raw : absolute;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(base, ec);
    return (ec ? base : canonical).generic_string();
}

constexpr std::size_t kMaxJsonDepth = 16;
constexpr std::size_t kMaxJsonStringBytes = 64 * 1024;
constexpr std::size_t kMaxJsonContainerItems = 1024;

struct LuaJsonMarkers {
    sol::table array;
    sol::table null;
};

enum class LuaJsonStatus {
    Ok,
    Cycle,
    Invalid,
};

bool LuaJsonHasMarker(const sol::table& table, const sol::table& marker) {
    sol::object value = table.raw_get<sol::object>(marker);
    return value.get_type() != sol::type::nil;
}

bool EncodeLuaJsonValue(const sol::object& value, lua_State* L,
                        const LuaJsonMarkers& markers, std::size_t depth,
                        std::unordered_set<const void*>& stack,
                        nlohmann::json& out, LuaJsonStatus& status) {
    switch (value.get_type()) {
        case sol::type::nil:
            out = nullptr;
            return true;
        case sol::type::boolean:
            out = value.as<bool>();
            return true;
        case sol::type::number: {
            value.push(L);
            const bool isInteger = lua_isinteger(L, -1) != 0;
            if (isInteger) {
                out = static_cast<std::int64_t>(lua_tointeger(L, -1));
                lua_pop(L, 1);
                return true;
            }
            const double number = lua_tonumber(L, -1);
            lua_pop(L, 1);
            if (!std::isfinite(number)) {
                status = LuaJsonStatus::Invalid;
                return false;
            }
            out = number;
            return true;
        }
        case sol::type::string: {
            const std::string text = value.as<std::string>();
            if (text.size() > kMaxJsonStringBytes) {
                status = LuaJsonStatus::Invalid;
                return false;
            }
            out = text;
            return true;
        }
        case sol::type::table: {
            const sol::table table = value.as<sol::table>();
            if (LuaJsonHasMarker(table, markers.null)) {
                out = nullptr;
                return true;
            }
            if (LuaJsonHasMarker(table, markers.array)) {
                out = nlohmann::json::array();
                return true;
            }
            if (depth > kMaxJsonDepth) {
                status = LuaJsonStatus::Invalid;
                return false;
            }

            const void* pointer = table.pointer();
            if (pointer == nullptr || !stack.insert(pointer).second) {
                status = LuaJsonStatus::Cycle;
                return false;
            }

            const bool ok = [&]() -> bool {
                std::vector<std::pair<sol::object, sol::object>> entries;
                for (const auto& item : table) {
                    entries.emplace_back(item.first, item.second);
                }
                if (entries.size() > kMaxJsonContainerItems) {
                    status = LuaJsonStatus::Invalid;
                    return false;
                }

                bool isArray = !entries.empty();
                for (const auto& [key, unused] : entries) {
                    (void)unused;
                    key.push(L);
                    const bool isInteger = lua_isinteger(L, -1) != 0;
                    const std::int64_t index =
                        isInteger ? static_cast<std::int64_t>(
                                        lua_tointeger(L, -1))
                                  : 0;
                    lua_pop(L, 1);
                    if (!isInteger || index < 1 ||
                        index > static_cast<std::int64_t>(entries.size())) {
                        isArray = false;
                        break;
                    }
                }

                if (isArray) {
                    out = nlohmann::json::array();
                    std::vector<std::optional<sol::object>> values(
                        entries.size());
                    for (const auto& [key, item] : entries) {
                        key.push(L);
                        const std::int64_t rawIndex = lua_tointeger(L, -1);
                        lua_pop(L, 1);
                        const std::size_t index =
                            static_cast<std::size_t>(rawIndex - 1);
                        values[index] = item;
                    }
                    for (const auto& item : values) {
                        if (!item.has_value()) {
                            status = LuaJsonStatus::Invalid;
                            return false;
                        }
                        nlohmann::json child;
                        if (!EncodeLuaJsonValue(*item, L, markers, depth + 1,
                                                stack, child, status)) {
                            return false;
                        }
                        out.push_back(std::move(child));
                    }
                    return true;
                }

                out = nlohmann::json::object();
                for (const auto& [key, item] : entries) {
                    if (!key.is<std::string>()) {
                        status = LuaJsonStatus::Invalid;
                        return false;
                    }
                    const std::string keyText = key.as<std::string>();
                    if (keyText.size() > kMaxJsonStringBytes) {
                        status = LuaJsonStatus::Invalid;
                        return false;
                    }
                    nlohmann::json child;
                    if (!EncodeLuaJsonValue(item, L, markers, depth + 1, stack,
                                            child, status)) {
                        return false;
                    }
                    out[keyText] = std::move(child);
                }
                return true;
            }();

            stack.erase(pointer);
            return ok;
        }
        default:
            status = LuaJsonStatus::Invalid;
            return false;
    }
}

bool ValidateDecodedJson(const nlohmann::json& value, std::size_t depth) {
    if (value.is_string()) {
        return value.get_ref<const std::string&>().size() <=
               kMaxJsonStringBytes;
    }
    if (value.is_number_float()) {
        return std::isfinite(value.get<double>());
    }
    if (value.is_array()) {
        if (depth > kMaxJsonDepth ||
            value.size() > kMaxJsonContainerItems) {
            return false;
        }
        for (const nlohmann::json& item : value) {
            if (!ValidateDecodedJson(item, depth + 1)) {
                return false;
            }
        }
        return true;
    }
    if (value.is_object()) {
        if (depth > kMaxJsonDepth ||
            value.size() > kMaxJsonContainerItems) {
            return false;
        }
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (it.key().size() > kMaxJsonStringBytes ||
                !ValidateDecodedJson(it.value(), depth + 1)) {
                return false;
            }
        }
        return true;
    }
    return true;
}

sol::object DecodeJsonToLua(const nlohmann::json& value, lua_State* L,
                            const LuaJsonMarkers& markers) {
    if (value.is_null()) {
        sol::table out = NewTable(L, 1);
        out[markers.null] = true;
        return out;
    }
    if (value.is_boolean()) {
        return sol::make_object(L, value.get<bool>());
    }
    if (value.is_number_unsigned()) {
        const std::uint64_t number = value.get<std::uint64_t>();
        if (number <= static_cast<std::uint64_t>(
                          std::numeric_limits<std::int64_t>::max())) {
            return sol::make_object(L, static_cast<std::int64_t>(number));
        }
        return sol::make_object(L, static_cast<double>(number));
    }
    if (value.is_number_integer()) {
        return sol::make_object(L, value.get<std::int64_t>());
    }
    if (value.is_number_float()) {
        return sol::make_object(L, value.get<double>());
    }
    if (value.is_string()) {
        return sol::make_object(L, value.get_ref<const std::string&>());
    }
    if (value.is_array()) {
        sol::table out = NewTable(L, static_cast<int>(value.size()));
        for (std::size_t i = 0; i < value.size(); ++i) {
            out[i + 1] = DecodeJsonToLua(value[i], L, markers);
        }
        if (value.empty()) {
            out[markers.array] = true;
        }
        return out;
    }

    sol::table out = NewTable(L, static_cast<int>(value.size()));
    for (auto it = value.begin(); it != value.end(); ++it) {
        out[it.key()] = DecodeJsonToLua(it.value(), L, markers);
    }
    return out;
}

sol::table MakePositionTable(lua_State* L, const ScriptVec3& position) {
    sol::table out = NewTable(L, 3);
    out["x"] = position.x;
    out["y"] = position.y;
    out["z"] = position.z;
    return out;
}

sol::table MakeActionResultTable(lua_State* L, const ScriptActionResult& result) {
    sol::table out = NewTable(L, 3);
    out["ok"] = result.ok;
    out["queued"] = result.queued;
    out["error"] = result.error;
    return out;
}

sol::table MakeXpResultTable(lua_State* L, const ScriptXpResult& value) {
    sol::table out = MakeActionResultTable(L, value.result);
    out["characterLevels"] = value.award.characterLevels;
    out["skillLevels"] = value.award.skillLevels;
    out["perkPoints"] = value.award.perkPoints;
    out["characterXp"] = value.award.characterXp;
    out["skillXp"] = value.award.skillXp;
    return out;
}

std::string EquipSlotText(const sol::optional<sol::object>& slot,
                          const char* fallback) {
    if (!slot.has_value()) {
        return fallback;
    }
    const sol::object& value = *slot;
    if (!value.valid() || value.get_type() == sol::type::nil) {
        return fallback;
    }
    if (value.get_type() == sol::type::number) {
        const double index = value.as<double>();
        if (index == 0.0) {
            return "primary";
        }
        if (index == 1.0) {
            return "body";
        }
        return "invalid";
    }
    if (value.get_type() == sol::type::string) {
        return value.as<std::string>();
    }
    return "invalid";
}

sol::table MakeItemStackTable(lua_State* L, const ScriptItemStack& stack) {
    sol::table out = NewTable(L, 13);
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

sol::table MakeEntityInfoTable(lua_State* L, const ScriptEntityInfo& info) {
    sol::table out = NewTable(L, 18);
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

sol::table MakeRecipeIngredientTable(lua_State* L,
                                     const ScriptRecipeIngredient& ingredient) {
    sol::table out = NewTable(L, 5);
    out["defId"] = ingredient.defId;
    out["label"] = ingredient.label;
    out["required"] = ingredient.required;
    out["have"] = ingredient.have;
    out["wear"] = ingredient.wear;
    return out;
}

sol::table MakeRecipeTable(lua_State* L, const ScriptRecipeInfo& recipe) {
    sol::table ingredients =
        NewTable(L, static_cast<int>(recipe.ingredients.size()));
    for (const ScriptRecipeIngredient& ingredient : recipe.ingredients) {
        ingredients.add(MakeRecipeIngredientTable(L, ingredient));
    }
    sol::table tools = NewTable(L, static_cast<int>(recipe.tools.size()));
    for (const ScriptRecipeIngredient& tool : recipe.tools) {
        tools.add(MakeRecipeIngredientTable(L, tool));
    }
    sol::table out = NewTable(L, 20);
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
        out["cookware"] = MakeRecipeIngredientTable(L, recipe.cookware);
    }
    if (recipe.hasFuel) {
        out["fuel"] = MakeRecipeIngredientTable(L, recipe.fuel);
    }
    return out;
}

sol::table MakeBuildableTable(lua_State* L, const ScriptBuildableInfo& info) {
    sol::table out = NewTable(L, 12);
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

sol::table MakeMissionTable(lua_State* L, const ScriptMissionInfo& mission) {
    sol::table progress = NewTable(L, static_cast<int>(mission.progress.size()));
    for (const int value : mission.progress) {
        progress.add(value);
    }
    sol::table objectives =
        NewTable(L, static_cast<int>(mission.objectives.size()));
    for (const ScriptMissionObjective& objective : mission.objectives) {
        sol::table entry = NewTable(L, 5);
        entry["kind"] = objective.kind;
        entry["target"] = objective.target;
        entry["count"] = objective.count;
        entry["progress"] = objective.progress;
        entry["description"] = objective.description;
        objectives.add(entry);
    }
    sol::table reward = NewTable(L, 8);
    reward["xp"] = mission.reward.xp;
    reward["skill"] = mission.reward.skill;
    reward["item"] = mission.reward.item;
    reward["itemCount"] = mission.reward.itemCount;
    reward["extraItem"] = mission.reward.extraItem;
    reward["extraItemCount"] = mission.reward.extraItemCount;
    reward["blueprint"] = mission.reward.blueprint;
    reward["label"] = mission.reward.label;

    sol::table out = NewTable(L, 14);
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
    ui::ScriptUiHost* uiHost = nullptr;
    bool initialized = false;
    // 指令预算（hook 触发，见 kMaxInstructionsPerCall；启动后仅由主线程访问）
    bool budgetActive = false;
    std::int64_t instructionBudget = 0;

    // 进入脚本调用前装配预算；调用结束（含出错）后解除
    void ArmCallBudget() {
        budgetActive = true;
        instructionBudget = static_cast<std::int64_t>(kMaxInstructionsPerCall);
    }
    // 嵌套调用（require 模块）保存/恢复外层预算，避免内层调用关闭外层防护。
    struct CallBudgetScope {
        Impl& impl;
        bool previousActive;
        std::int64_t previousBudget;

        explicit CallBudgetScope(Impl& owner)
            : impl(owner),
              previousActive(owner.budgetActive),
              previousBudget(owner.instructionBudget) {
            impl.ArmCallBudget();
        }
        ~CallBudgetScope() {
            impl.budgetActive = previousActive;
            impl.instructionBudget = previousBudget;
        }
    };

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
    void BindApiTables(sol::environment& env, const ModInfo& mod, sol::table handlers,
                       const std::shared_ptr<std::vector<Timer>>& timers) {
        const ScriptApi* scriptApi = api;
        const std::string& modId = mod.id;

        auto uiApi = std::make_shared<ScriptUiApi>(uiHost, mod.id, mod.rootPath);

        sol::table game = NewTable(lua->lua_state());
        game["GetApiVersion"] = [scriptApi]() -> int {
            return scriptApi != nullptr ? scriptApi->GetApiVersion() : 0;
        };
        game["IsMultiplayer"] = [scriptApi]() -> bool {
            return scriptApi != nullptr && scriptApi->IsMultiplayer();
        };
        game["GetNetMode"] = [scriptApi]() -> std::string {
            return scriptApi != nullptr ? scriptApi->GetNetMode() : std::string("direct");
        };
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
        game["ToggleInventory"] = [scriptApi]() -> bool {
            return scriptApi != nullptr && scriptApi->ToggleInventory();
        };
        game["IsInventoryOpen"] = [scriptApi]() -> bool {
            return scriptApi != nullptr && scriptApi->IsInventoryOpen();
        };
        game["IsMenuOpen"] = [scriptApi]() -> bool {
            return scriptApi != nullptr && scriptApi->IsMenuOpen();
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
        player["GetNeeds"] = [scriptApi](sol::this_state ts) -> std::optional<sol::table> {
            ScriptNeeds needs;
            if (scriptApi == nullptr || !scriptApi->GetPlayerNeeds(needs)) {
                return std::nullopt;
            }
            sol::table out = NewTable(ts.L, 9);
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
        };
        player["GetStamina"] = [scriptApi](sol::this_state ts) -> std::optional<sol::table> {
            ScriptStamina stamina;
            if (scriptApi == nullptr || !scriptApi->GetPlayerStamina(stamina)) {
                return std::nullopt;
            }
            sol::table out = NewTable(ts.L, 3);
            out["current"] = stamina.current;
            out["max"] = stamina.max;
            out["exhausted"] = stamina.exhausted;
            return out;
        };
        player["GetWounds"] = [scriptApi](sol::this_state ts) -> std::optional<sol::table> {
            ScriptWounds wounds;
            if (scriptApi == nullptr || !scriptApi->GetPlayerWounds(wounds)) {
                return std::nullopt;
            }
            sol::table list = NewTable(ts.L, static_cast<int>(wounds.wounds.size()));
            for (const ScriptWound& wound : wounds.wounds) {
                sol::table entry = NewTable(ts.L, 4);
                entry["type"] = wound.type;
                entry["severity"] = wound.severity;
                entry["bandaged"] = wound.bandaged;
                entry["bleeding"] = wound.bleeding;
                list.add(entry);
            }
            sol::table out = NewTable(ts.L, 2);
            out["count"] = wounds.count;
            out["wounds"] = list;
            return out;
        };
        player["GetPain"] = [scriptApi](sol::this_state ts) -> std::optional<sol::table> {
            ScriptPain pain;
            if (scriptApi == nullptr || !scriptApi->GetPlayerPain(pain)) {
                return std::nullopt;
            }
            sol::table out = NewTable(ts.L, 3);
            out["level"] = pain.level;
            out["baseLevel"] = pain.baseLevel;
            out["reliefRemaining"] = pain.reliefRemaining;
            return out;
        };
        player["GetInfection"] = [scriptApi](sol::this_state ts) -> std::optional<sol::table> {
            ScriptInfection infection;
            if (scriptApi == nullptr || !scriptApi->GetPlayerInfection(infection)) {
                return std::nullopt;
            }
            sol::table out = NewTable(ts.L, 4);
            out["stage"] = infection.stage;
            out["progress"] = infection.progress;
            out["slowRemaining"] = infection.slowRemaining;
            out["timeToNextStage"] = infection.timeToNextStage;
            return out;
        };
        player["GetClimate"] = [scriptApi](sol::this_state ts) -> std::optional<sol::table> {
            ScriptClimate climate;
            if (scriptApi == nullptr || !scriptApi->GetPlayerClimate(climate)) {
                return std::nullopt;
            }
            sol::table out = NewTable(ts.L, 10);
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
        };
        player["GetProgression"] = [scriptApi](sol::this_state ts) -> std::optional<sol::table> {
            ScriptProgression progression;
            if (scriptApi == nullptr || !scriptApi->GetPlayerProgression(progression)) {
                return std::nullopt;
            }
            sol::table skills = NewTable(ts.L, static_cast<int>(progression.skills.size()));
            for (const ScriptSkillProgress& skill : progression.skills) {
                sol::table entry = NewTable(ts.L, 3);
                entry["level"] = skill.level;
                entry["xp"] = skill.xp;
                entry["xpNext"] = skill.xpNext;
                skills[skill.id] = entry;
            }
            sol::table perks = NewTable(ts.L, static_cast<int>(progression.perks.size()));
            for (const std::string& perk : progression.perks) {
                perks.add(perk);
            }
            sol::table out = NewTable(ts.L, 6);
            out["level"] = progression.level;
            out["xp"] = progression.xp;
            out["xpNext"] = progression.xpNext;
            out["perkPoints"] = progression.perkPoints;
            out["skills"] = skills;
            out["perks"] = perks;
            return out;
        };
        player["GetInventory"] = [scriptApi](sol::this_state ts, sol::optional<int> offset,
                                             sol::optional<int> limit)
            -> std::optional<sol::table> {
            ScriptInventory inventory;
            if (scriptApi == nullptr ||
                !scriptApi->GetPlayerInventory(offset.value_or(0), limit.value_or(0),
                                               inventory)) {
                return std::nullopt;
            }
            sol::table stacks =
                NewTable(ts.L, static_cast<int>(inventory.stacks.size()));
            for (const ScriptItemStack& stack : inventory.stacks) {
                stacks.add(MakeItemStackTable(ts.L, stack));
            }
            sol::table out = NewTable(ts.L, 5);
            out["weightKg"] = inventory.weightKg;
            out["capacityKg"] = inventory.capacityKg;
            out["totalStacks"] = inventory.totalStacks;
            out["truncated"] = inventory.truncated;
            out["stacks"] = stacks;
            return out;
        };
        player["GetEquipment"] = [scriptApi](sol::this_state ts) -> std::optional<sol::table> {
            ScriptEquipment equipment;
            if (scriptApi == nullptr || !scriptApi->GetPlayerEquipment(equipment)) {
                return std::nullopt;
            }
            sol::table out = NewTable(ts.L, 2);
            if (equipment.hasPrimary) {
                out["primary"] = MakeItemStackTable(ts.L, equipment.primary);
            }
            if (equipment.hasBody) {
                out["body"] = MakeItemStackTable(ts.L, equipment.body);
            }
            return out;
        };
        player["CountItem"] = [scriptApi](const std::string& defId) -> int {
            return scriptApi != nullptr ? scriptApi->CountPlayerItem(defId) : 0;
        };
        player["HasItem"] = [scriptApi](const std::string& defId,
                                        sol::optional<int> count) -> bool {
            return scriptApi != nullptr &&
                   scriptApi->HasPlayerItem(defId, count.value_or(1));
        };
        player["AddItem"] = [scriptApi](const std::string& defId, sol::optional<int> count) -> int {
            return scriptApi != nullptr ? scriptApi->AddPlayerItem(defId, count.value_or(1)) : 0;
        };
        player["UseItem"] = [scriptApi](sol::this_state ts,
                                        sol::optional<int> index) -> sol::table {
            if (scriptApi == nullptr) {
                return MakeActionResultTable(
                    ts.L, ScriptActionResult{false, false, "脚本 API 不可用"});
            }
            return MakeActionResultTable(ts.L, scriptApi->UseItem(index.value_or(-1)));
        };
        player["EquipItem"] = [scriptApi](sol::this_state ts, sol::optional<int> index,
                                          sol::optional<sol::object> slot) -> sol::table {
            if (scriptApi == nullptr) {
                return MakeActionResultTable(
                    ts.L, ScriptActionResult{false, false, "脚本 API 不可用"});
            }
            return MakeActionResultTable(
                ts.L, scriptApi->EquipItem(index.value_or(-1),
                                           EquipSlotText(slot, "primary")));
        };
        player["Unequip"] = [scriptApi](sol::this_state ts,
                                        sol::optional<sol::object> slot) -> sol::table {
            if (scriptApi == nullptr) {
                return MakeActionResultTable(
                    ts.L, ScriptActionResult{false, false, "脚本 API 不可用"});
            }
            return MakeActionResultTable(
                ts.L, scriptApi->Unequip(EquipSlotText(slot, "invalid")));
        };
        player["DropItem"] = [scriptApi](sol::this_state ts,
                                         sol::optional<int> index) -> sol::table {
            if (scriptApi == nullptr) {
                return MakeActionResultTable(
                    ts.L, ScriptActionResult{false, false, "脚本 API 不可用"});
            }
            return MakeActionResultTable(ts.L, scriptApi->DropItem(index.value_or(-1)));
        };
        player["PickupNearest"] = [scriptApi](sol::this_state ts) -> sol::table {
            if (scriptApi == nullptr) {
                return MakeActionResultTable(
                    ts.L, ScriptActionResult{false, false, "脚本 API 不可用"});
            }
            return MakeActionResultTable(ts.L, scriptApi->PickupNearest());
        };
        player["Pickup"] = [scriptApi](sol::this_state ts,
                                       std::uint64_t entityId) -> sol::table {
            if (scriptApi == nullptr) {
                return MakeActionResultTable(
                    ts.L, ScriptActionResult{false, false, "脚本 API 不可用"});
            }
            return MakeActionResultTable(ts.L, scriptApi->Pickup(entityId));
        };
        player["Attack"] = [scriptApi](sol::this_state ts,
                                       sol::optional<std::uint64_t> targetId,
                                       sol::optional<bool> heavy) -> sol::table {
            if (scriptApi == nullptr) {
                return MakeActionResultTable(
                    ts.L, ScriptActionResult{false, false, "脚本 API 不可用"});
            }
            return MakeActionResultTable(
                ts.L, scriptApi->Attack(targetId.value_or(0), heavy.value_or(false)));
        };
        player["ApplyDamage"] = [scriptApi](sol::this_state ts, float amount,
                                            sol::optional<std::string> damageType)
            -> sol::table {
            if (scriptApi == nullptr) {
                return MakeActionResultTable(
                    ts.L, ScriptActionResult{false, false, "脚本 API 不可用"});
            }
            return MakeActionResultTable(
                ts.L, scriptApi->ApplyDamage(amount,
                                             damageType.value_or("blunt")));
        };
        player["AdjustNeeds"] = [scriptApi](sol::this_state ts,
                                            sol::optional<float> hunger,
                                            sol::optional<float> thirst,
                                            sol::optional<float> fatigue)
            -> sol::table {
            if (scriptApi == nullptr) {
                return MakeActionResultTable(
                    ts.L, ScriptActionResult{false, false, "脚本 API 不可用"});
            }
            return MakeActionResultTable(
                ts.L, scriptApi->AdjustNeeds(hunger.value_or(0.0f),
                                             thirst.value_or(0.0f),
                                             fatigue.value_or(0.0f)));
        };
        player["GiveXp"] = [scriptApi](sol::this_state ts,
                                       const std::string& skill,
                                       float amount) -> sol::table {
            if (scriptApi == nullptr) {
                ScriptXpResult out;
                out.result = ScriptActionResult{false, false, "脚本 API 不可用"};
                return MakeXpResultTable(ts.L, out);
            }
            return MakeXpResultTable(ts.L, scriptApi->GiveXp(skill, amount));
        };
        env["Player"] = player;

        sol::table world = NewTable(lua->lua_state());
        world["IsPassable"] = [scriptApi](int gx, int gy) -> bool {
            return scriptApi != nullptr && scriptApi->IsPassable(gx, gy);
        };
        world["GetSquare"] = [scriptApi](sol::this_state ts, int gx, int gy,
                                         sol::optional<int> z)
            -> std::optional<sol::table> {
            if (scriptApi == nullptr) {
                return std::nullopt;
            }
            ScriptSquare square;
            if (!scriptApi->GetSquare(gx, gy, z.value_or(0), square)) {
                return std::nullopt;
            }
            lua_State* luaState = ts.L;
            const auto makeObject = [luaState](const ScriptSquareObject& object) {
                sol::table objectTable = NewTable(luaState);
                objectTable["sprite"] = object.sprite;
                objectTable["hp"] = object.hp;
                objectTable["playerBuilt"] = object.playerBuilt;
                objectTable["blocksMovement"] = object.blocksMovement;
                return objectTable;
            };
            sol::table out = NewTable(luaState);
            out["loaded"] = square.loaded;
            out["hasFloor"] = square.hasFloor;
            if (square.hasFloor) {
                out["floor"] = makeObject(square.floor);
            }
            sol::table wall = NewTable(luaState);
            for (const ScriptSquareObject& object : square.wall) {
                wall.add(makeObject(object));
            }
            out["wall"] = wall;
            sol::table furniture = NewTable(luaState);
            for (const ScriptSquareObject& object : square.furniture) {
                furniture.add(makeObject(object));
            }
            out["furniture"] = furniture;
            sol::table item = NewTable(luaState);
            for (const ScriptSquareObject& object : square.item) {
                item.add(makeObject(object));
            }
            out["item"] = item;
            return out;
        };
        world["SpawnItem"] = [scriptApi](const std::string& defId, double x, double y,
                                         sol::optional<int> z) -> bool {
            return scriptApi != nullptr && scriptApi->SpawnItem(defId, static_cast<float>(x),
                                                                static_cast<float>(y), z.value_or(0));
        };
        world["EmitSound"] = [scriptApi](double x, double y, double radius,
                                         sol::optional<std::string> kind,
                                         sol::optional<std::string> clip) -> bool {
            return scriptApi != nullptr &&
                   scriptApi->EmitSound(static_cast<float>(x),
                                        static_cast<float>(y),
                                        static_cast<float>(radius),
                                        kind.value_or("generic"),
                                        clip.value_or(""));
        };
        world["GetWeather"] = [scriptApi](sol::this_state ts) -> std::optional<sol::table> {
            ScriptWeather weather;
            if (scriptApi == nullptr || !scriptApi->GetWeather(weather)) {
                return std::nullopt;
            }
            sol::table out = NewTable(ts.L, 3);
            out["kind"] = weather.kind;
            out["ambientC"] = weather.ambientC;
            out["isNight"] = weather.isNight;
            return out;
        };
        world["GetTimeOfDay"] = [scriptApi]() -> double {
            return scriptApi != nullptr ? scriptApi->GetTimeOfDay() : 0.0;
        };
        env["World"] = world;

        sol::table audio = NewTable(lua->lua_state());
        audio["Play2D"] = [scriptApi](const std::string& clip,
                                      sol::optional<double> volume) -> bool {
            return scriptApi != nullptr &&
                   scriptApi->PlayAudio2D(
                       clip, static_cast<float>(volume.value_or(1.0)));
        };
        audio["Play3D"] = [scriptApi](const std::string& clip, double x,
                                      double y, sol::optional<double> radius,
                                      sol::optional<double> volume) -> bool {
            return scriptApi != nullptr &&
                   scriptApi->PlayAudio3D(
                       clip, static_cast<float>(x), static_cast<float>(y),
                       static_cast<float>(radius.value_or(0.0)),
                       static_cast<float>(volume.value_or(1.0)));
        };
        env["Audio"] = audio;

        sol::table zombies = NewTable(lua->lua_state());
        zombies["Count"] = [scriptApi]() -> int {
            return scriptApi != nullptr ? scriptApi->ZombieCount() : 0;
        };
        zombies["Spawn"] = [scriptApi](double x, double y, sol::optional<int> z,
                                       sol::optional<std::string> variant)
            -> std::uint64_t {
            if (scriptApi == nullptr) {
                return 0;
            }
            return scriptApi->SpawnZombie(static_cast<float>(x),
                                          static_cast<float>(y), z.value_or(0),
                                          variant.value_or("normal"));
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

        sol::table entities = NewTable(lua->lua_state());
        entities["Get"] = [scriptApi](sol::this_state ts, std::uint64_t id)
            -> std::optional<sol::table> {
            ScriptEntityInfo info;
            if (scriptApi == nullptr || !scriptApi->GetEntity(id, info)) {
                return std::nullopt;
            }
            return MakeEntityInfoTable(ts.L, info);
        };
        entities["Query"] = [scriptApi, modId](sol::this_state ts, double x, double y,
                                                sol::optional<int> z,
                                                sol::optional<double> radius,
                                                sol::optional<std::string> kind,
                                                sol::optional<int> limit) -> sol::table {
            sol::table out = NewTable(ts.L, 3);
            out["count"] = 0;
            out["truncated"] = false;
            out["entities"] = NewTable(ts.L, 0);
            if (scriptApi == nullptr) {
                return out;
            }
            ScriptEntityQuery query;
            if (!scriptApi->QueryEntitiesForMod(
                    modId, static_cast<float>(x), static_cast<float>(y),
                    z.value_or(0), static_cast<float>(radius.value_or(8.0)),
                    kind.value_or("any"), limit.value_or(0), query)) {
                return out;
            }
            sol::table list = NewTable(ts.L, static_cast<int>(query.entities.size()));
            for (const ScriptEntityInfo& info : query.entities) {
                list.add(MakeEntityInfoTable(ts.L, info));
            }
            out["count"] = query.count;
            out["truncated"] = query.truncated;
            out["entities"] = list;
            return out;
        };
        entities["GetContainer"] = [scriptApi](sol::this_state ts, std::uint64_t id)
            -> std::optional<sol::table> {
            ScriptContainerInfo container;
            if (scriptApi == nullptr || !scriptApi->GetEntityContainer(id, container)) {
                return std::nullopt;
            }
            sol::table stacks =
                NewTable(ts.L, static_cast<int>(container.stacks.size()));
            for (const ScriptItemStack& stack : container.stacks) {
                stacks.add(MakeItemStackTable(ts.L, stack));
            }
            sol::table out = NewTable(ts.L, 8);
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
        };
        env["Entities"] = entities;

        sol::table itemsApi = NewTable(lua->lua_state());
        itemsApi["Exists"] = [scriptApi](const std::string& defId) -> bool {
            return scriptApi != nullptr && scriptApi->ItemExists(defId);
        };
        itemsApi["GetName"] = [scriptApi](const std::string& defId) -> std::string {
            return scriptApi != nullptr ? scriptApi->ItemName(defId) : std::string();
        };
        itemsApi["GetInfo"] = [scriptApi](sol::this_state ts, const std::string& defId)
            -> std::optional<sol::table> {
            ScriptItemInfo info;
            if (scriptApi == nullptr || !scriptApi->GetItemInfo(defId, info)) {
                return std::nullopt;
            }
            sol::table out = NewTable(ts.L, 12);
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
        };
        env["Items"] = itemsApi;

        sol::table craft = NewTable(lua->lua_state());
        craft["List"] = [scriptApi](sol::this_state ts) -> sol::table {
            sol::table out = NewTable(ts.L, 3);
            ScriptRecipeList recipes;
            if (scriptApi != nullptr) {
                (void)scriptApi->ListRecipes(recipes);
            }
            sol::table list = NewTable(ts.L, static_cast<int>(recipes.recipes.size()));
            for (const ScriptRecipeInfo& recipe : recipes.recipes) {
                list.add(MakeRecipeTable(ts.L, recipe));
            }
            out["count"] = recipes.count;
            out["truncated"] = recipes.truncated;
            out["recipes"] = list;
            return out;
        };
        craft["GetState"] = [scriptApi](sol::this_state ts) -> sol::table {
            ScriptCraftState state;
            if (scriptApi != nullptr) {
                (void)scriptApi->GetCraftState(state);
            }
            sol::table queue = NewTable(ts.L, static_cast<int>(state.queue.size()));
            for (const ScriptCraftQueueEntry& entry : state.queue) {
                sol::table item = NewTable(ts.L, 2);
                item["recipeId"] = entry.recipeId;
                item["count"] = entry.count;
                queue.add(item);
            }
            sol::table out = NewTable(ts.L, 10);
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
        };
        craft["IsKnown"] = [scriptApi](const std::string& recipeId) -> bool {
            return scriptApi != nullptr && scriptApi->IsRecipeKnown(recipeId);
        };
        craft["Start"] = [scriptApi](sol::this_state ts,
                                     const std::string& recipeId,
                                     sol::optional<int> count) -> sol::table {
            if (scriptApi == nullptr) {
                return MakeActionResultTable(
                    ts.L, ScriptActionResult{false, false, "脚本 API 不可用"});
            }
            return MakeActionResultTable(
                ts.L, scriptApi->StartCraft(recipeId, count.value_or(1)));
        };
        craft["Cancel"] = [scriptApi](sol::this_state ts) -> sol::table {
            if (scriptApi == nullptr) {
                return MakeActionResultTable(
                    ts.L, ScriptActionResult{false, false, "脚本 API 不可用"});
            }
            return MakeActionResultTable(ts.L, scriptApi->CancelCraft());
        };
        craft["Learn"] = [scriptApi](sol::this_state ts,
                                     const std::string& recipeId) -> sol::table {
            if (scriptApi == nullptr) {
                return MakeActionResultTable(
                    ts.L, ScriptActionResult{false, false, "脚本 API 不可用"});
            }
            return MakeActionResultTable(ts.L,
                                         scriptApi->LearnRecipe(recipeId));
        };
        env["Craft"] = craft;

        sol::table build = NewTable(lua->lua_state());
        build["List"] = [scriptApi](sol::this_state ts) -> sol::table {
            sol::table out = NewTable(ts.L, 3);
            ScriptBuildableList buildables;
            if (scriptApi != nullptr) {
                (void)scriptApi->ListBuildables(buildables);
            }
            sol::table list =
                NewTable(ts.L, static_cast<int>(buildables.buildables.size()));
            for (const ScriptBuildableInfo& info : buildables.buildables) {
                list.add(MakeBuildableTable(ts.L, info));
            }
            out["count"] = buildables.count;
            out["truncated"] = buildables.truncated;
            out["items"] = list;
            return out;
        };
        build["CanPlace"] = [scriptApi](sol::this_state ts,
                                        const std::string& defId, int x, int y,
                                        sol::optional<bool> vertical)
            -> sol::table {
            if (scriptApi == nullptr) {
                return MakeActionResultTable(
                    ts.L, ScriptActionResult{false, false, "脚本 API 不可用"});
            }
            return MakeActionResultTable(
                ts.L, scriptApi->CanPlaceBuild(defId, x, y,
                                               vertical.value_or(false)));
        };
        build["Place"] = [scriptApi](sol::this_state ts,
                                     const std::string& defId, int x, int y,
                                     sol::optional<bool> vertical)
            -> sol::table {
            if (scriptApi == nullptr) {
                return MakeActionResultTable(
                    ts.L, ScriptActionResult{false, false, "脚本 API 不可用"});
            }
            return MakeActionResultTable(
                ts.L, scriptApi->PlaceBuild(defId, x, y,
                                            vertical.value_or(false)));
        };
        build["Demolish"] = [scriptApi](sol::this_state ts, int x, int y)
            -> sol::table {
            if (scriptApi == nullptr) {
                return MakeActionResultTable(
                    ts.L, ScriptActionResult{false, false, "脚本 API 不可用"});
            }
            return MakeActionResultTable(ts.L,
                                         scriptApi->DemolishBuild(x, y));
        };
        build["Repair"] = [scriptApi](sol::this_state ts, int x, int y)
            -> sol::table {
            if (scriptApi == nullptr) {
                return MakeActionResultTable(
                    ts.L, ScriptActionResult{false, false, "脚本 API 不可用"});
            }
            return MakeActionResultTable(ts.L, scriptApi->RepairBuild(x, y));
        };
        build["ToggleDoor"] = [scriptApi](sol::this_state ts, int x, int y)
            -> sol::table {
            if (scriptApi == nullptr) {
                return MakeActionResultTable(
                    ts.L, ScriptActionResult{false, false, "脚本 API 不可用"});
            }
            return MakeActionResultTable(ts.L,
                                         scriptApi->ToggleBuildDoor(x, y));
        };
        env["Build"] = build;

        sol::table mission = NewTable(lua->lua_state());
        mission["List"] = [scriptApi](sol::this_state ts, sol::optional<std::string> filter)
            -> sol::table {
            sol::table out = NewTable(ts.L, 3);
            ScriptMissionList missions;
            if (scriptApi != nullptr) {
                (void)scriptApi->ListMissions(filter.value_or("all"), missions);
            }
            sol::table list =
                NewTable(ts.L, static_cast<int>(missions.missions.size()));
            for (const ScriptMissionInfo& info : missions.missions) {
                list.add(MakeMissionTable(ts.L, info));
            }
            out["count"] = missions.count;
            out["truncated"] = missions.truncated;
            out["missions"] = list;
            return out;
        };
        mission["Get"] = [scriptApi](sol::this_state ts, const std::string& id)
            -> std::optional<sol::table> {
            ScriptMissionInfo info;
            if (scriptApi == nullptr || !scriptApi->GetMission(id, info)) {
                return std::nullopt;
            }
            return MakeMissionTable(ts.L, info);
        };
        env["Mission"] = mission;

        sol::table contentApi = NewTable(lua->lua_state(), 3);
        contentApi["RegisterItems"] = [scriptApi, modId](
                                          const std::string& json) -> int {
            std::string error;
            const int added = scriptApi != nullptr
                                  ? scriptApi->RegisterItems(modId, json, error)
                                  : 0;
            if (added == 0 && !error.empty()) {
                ZL_WARN("script-mod", "[{}] {}", modId, error);
            }
            return added;
        };
        contentApi["RegisterRecipes"] = [scriptApi, modId](
                                            const std::string& json) -> int {
            std::string error;
            const int added = scriptApi != nullptr
                                  ? scriptApi->RegisterRecipes(modId, json, error)
                                  : 0;
            if (added == 0 && !error.empty()) {
                ZL_WARN("script-mod", "[{}] {}", modId, error);
            }
            return added;
        };
        contentApi["RegisterBuildings"] = [scriptApi, modId](
                                              const std::string& json) -> int {
            std::string error;
            const int added = scriptApi != nullptr
                                  ? scriptApi->RegisterBuildings(modId, json, error)
                                  : 0;
            if (added == 0 && !error.empty()) {
                ZL_WARN("script-mod", "[{}] {}", modId, error);
            }
            return added;
        };
        contentApi["RegisterPois"] = [scriptApi, modId](
                                         const std::string& json) -> int {
            std::string error;
            const int added = scriptApi != nullptr
                                  ? scriptApi->RegisterPois(modId, json, error)
                                  : 0;
            if (added == 0 && !error.empty()) {
                ZL_WARN("script-mod", "[{}] {}", modId, error);
            }
            return added;
        };
        contentApi["RegisterMissions"] = [scriptApi, modId](
                                             const std::string& json) -> int {
            std::string error;
            const int added = scriptApi != nullptr
                                  ? scriptApi->RegisterMissions(modId, json, error)
                                  : 0;
            if (added == 0 && !error.empty()) {
                ZL_WARN("script-mod", "[{}] {}", modId, error);
            }
            return added;
        };
        env["Content"] = contentApi;

        sol::table hud = NewTable(lua->lua_state(), 16);
        hud["GetState"] = [scriptApi](sol::this_state ts) -> sol::table {
            sol::table out = NewTable(ts.L, 16);
            ui::HudState state;
            if (scriptApi != nullptr) {
                (void)scriptApi->GetHudState(state);
            }
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
            sol::table worldEventOptions =
                NewTable(ts.L, state.worldEventOptions.size());
            for (std::size_t i = 0; i < state.worldEventOptions.size(); ++i) {
                worldEventOptions[i + 1] = state.worldEventOptions[i];
            }
            out["worldEventOptions"] = worldEventOptions;
            sol::table worldEventLog =
                NewTable(ts.L, state.worldEventLog.size());
            for (std::size_t i = 0; i < state.worldEventLog.size(); ++i) {
                worldEventLog[i + 1] = state.worldEventLog[i];
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
            sol::table siegeReportLines = NewTable(ts.L, 8);
            for (std::size_t i = 0; i < state.siegeReportLines.size(); ++i) {
                siegeReportLines[i + 1] = state.siegeReportLines[i];
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
            sol::table companion = NewTable(ts.L, 17);
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
            sol::table skills = NewTable(ts.L, 4);
            for (std::size_t i = 0; i < state.progressionSkills.size(); ++i) {
                skills[i + 1] = state.progressionSkills[i];
            }
            out["progressionSkills"] = skills;
            sol::table choices = NewTable(ts.L, 3);
            for (std::size_t i = 0; i < state.progressionChoices.size(); ++i) {
                choices[i + 1] = state.progressionChoices[i];
            }
            out["progressionChoices"] = choices;
            out["dialogueOpen"] = state.dialogueOpen;
            out["dialogueSpeaker"] = state.dialogueSpeaker;
            out["dialogueText"] = state.dialogueText;
            out["dialogueOptionCount"] = state.dialogueOptionCount;
            out["dialogueSelected"] = state.dialogueSelected;
            out["dialogueHint"] = state.dialogueHint;
            sol::table dialogueOptions = NewTable(ts.L, 5);
            for (std::size_t i = 0; i < state.dialogueOptions.size(); ++i) {
                dialogueOptions[i + 1] = state.dialogueOptions[i];
            }
            out["dialogueOptions"] = dialogueOptions;
            out["actionHint"] = state.actionHint;
            out["actionTone"] = state.actionTone;
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
            out["selectedSlot"] = state.selectedSlot;
            out["notice"] = state.notice;
            out["noticeTone"] = state.noticeTone;
            out["noticeRemaining"] = state.noticeRemaining;
            out["noticeDuration"] = state.noticeDuration;
            sol::table slots = NewTable(ts.L, 5);
            for (std::size_t i = 0; i < state.slots.size(); ++i) {
                sol::table slot = NewTable(ts.L, 4);
                slot["label"] = state.slots[i].label;
                slot["count"] = state.slots[i].count;
                slot["throwable"] = state.slots[i].throwable;
                slot["throwTag"] = state.slots[i].throwTag;
                slots[i + 1] = slot;
            }
            out["slots"] = slots;
            sol::table modFields = NewTable(ts.L, state.modFieldCount);
            for (int i = 0; i < state.modFieldCount; ++i) {
                const ui::HudModField& field =
                    state.modFields[static_cast<std::size_t>(i)];
                sol::table item = NewTable(ts.L, 5);
                item["modId"] = field.modId;
                item["id"] = field.id;
                item["label"] = field.label;
                item["text"] = field.text;
                item["tone"] = field.tone;
                modFields[i + 1] = item;
            }
            out["modFields"] = modFields;
            return out;
        };
        hud["RegisterField"] = [scriptApi, modId](const std::string& id,
                                                  const std::string& label) -> bool {
            return scriptApi != nullptr &&
                   scriptApi->RegisterHudField(modId, id, label);
        };
        hud["SetField"] = [scriptApi, modId](
                              const std::string& id, const std::string& text,
                              sol::optional<std::string> tone) -> bool {
            return scriptApi != nullptr &&
                   scriptApi->SetHudField(modId, id, text,
                                          tone.value_or("info"));
        };
        hud["ClearField"] = [scriptApi, modId](const std::string& id) -> bool {
            return scriptApi != nullptr &&
                   scriptApi->ClearHudField(modId, id);
        };
        env["Hud"] = hud;

        sol::table menu = NewTable(lua->lua_state(), 8);
        menu["GetState"] = [scriptApi](sol::this_state ts) -> sol::table {
            ui::MenuState state;
            if (scriptApi != nullptr) {
                (void)scriptApi->GetMenuState(state);
            }
            sol::table out = NewTable(ts.L, 10);
            out["open"] = state.open;
            out["pauseMode"] = state.pauseMode;
            out["canContinue"] = state.canContinue;
            out["volume"] = state.settings.volume;
            out["musicVolume"] = state.settings.musicVolume;
            out["sfxVolume"] = state.settings.sfxVolume;
            out["ambientVolume"] = state.settings.ambientVolume;
            sol::table bindings = NewTable(ts.L, 10);
            for (std::size_t i = 0; i < state.settings.bindings.size(); ++i) {
                const ui::MenuKeyBinding& binding = state.settings.bindings[i];
                sol::table item = NewTable(ts.L, 3);
                item["action"] = binding.action;
                item["label"] = binding.label;
                item["key"] = binding.key;
                bindings[i + 1] = item;
            }
            out["bindings"] = bindings;
            return out;
        };
        menu["Action"] = [scriptApi](const std::string& action) -> bool {
            return scriptApi != nullptr && scriptApi->MenuAction(action);
        };
        menu["ApplySettings"] = [scriptApi](sol::this_state ts,
                                            sol::table settings) -> sol::table {
            sol::table result = NewTable(ts.L, 2);
            ui::MenuSettings parsed;
            parsed.volume = settings["volume"].get_or(parsed.volume);
            parsed.musicVolume = settings["musicVolume"].get_or(parsed.musicVolume);
            parsed.sfxVolume = settings["sfxVolume"].get_or(parsed.sfxVolume);
            parsed.ambientVolume = settings["ambientVolume"].get_or(parsed.ambientVolume);
            sol::object rawBindings = settings["bindings"];
            if (!rawBindings.is<sol::table>()) {
                result["ok"] = false;
                result["error"] = "bindings 必须是数组";
                return result;
            }
            sol::table bindings = rawBindings.as<sol::table>();
            for (std::size_t i = 1; i <= bindings.size(); ++i) {
                sol::object raw = bindings[i];
                if (!raw.is<sol::table>()) {
                    result["ok"] = false;
                    result["error"] = "bindings 项必须是表";
                    return result;
                }
                sol::table item = raw.as<sol::table>();
                ui::MenuKeyBinding binding;
                binding.action = item["action"].get_or(std::string());
                binding.label = item["label"].get_or(std::string());
                binding.key = item["key"].get_or(std::string());
                parsed.bindings.push_back(std::move(binding));
            }
            std::string error;
            const bool ok = scriptApi != nullptr &&
                            scriptApi->ApplyMenuSettings(parsed, error);
            result["ok"] = ok;
            result["error"] = ok ? std::string() : (error.empty() ? "保存设置失败" : error);
            return result;
        };
        menu["KeyName"] = [scriptApi](int key) -> std::string {
            return scriptApi != nullptr ? scriptApi->MenuKeyName(key) : std::string();
        };
        menu["PlayClick"] = [scriptApi] {
            if (scriptApi != nullptr) {
                scriptApi->PlayUiClick();
            }
        };
        env["Menu"] = menu;

        sol::table save = NewTable(lua->lua_state(), 6);
        save["List"] = [scriptApi](sol::this_state ts) -> sol::table {
            std::vector<ScriptSaveInfo> saves;
            std::string error;
            const bool ok = scriptApi != nullptr && scriptApi->ListSaves(saves, error);
            sol::table out = NewTable(ts.L, 3);
            out["ok"] = ok;
            out["error"] = ok ? std::string() : (error.empty() ? "读取存档列表失败" : error);
            sol::table list = NewTable(ts.L, static_cast<int>(saves.size()));
            for (std::size_t i = 0; i < saves.size(); ++i) {
                const ScriptSaveInfo& entry = saves[i];
                sol::table item = NewTable(ts.L, 5);
                item["id"] = entry.id;
                item["name"] = entry.name;
                item["updatedText"] = entry.updatedText;
                item["mapId"] = entry.mapId;
                item["gameVersion"] = entry.gameVersion;
                list[i + 1] = item;
            }
            out["saves"] = list;
            return out;
        };
        save["Load"] = [scriptApi](sol::this_state ts, const std::string& id) -> sol::table {
            std::string error;
            const bool ok = scriptApi != nullptr && scriptApi->LoadSave(id, error);
            sol::table out = NewTable(ts.L, 2);
            out["ok"] = ok;
            out["error"] = ok ? std::string() : (error.empty() ? "读取存档失败" : error);
            return out;
        };
        save["Delete"] = [scriptApi](sol::this_state ts, const std::string& id) -> sol::table {
            std::string error;
            const bool ok = scriptApi != nullptr && scriptApi->DeleteSave(id, error);
            sol::table out = NewTable(ts.L, 2);
            out["ok"] = ok;
            out["error"] = ok ? std::string() : (error.empty() ? "删除存档失败" : error);
            return out;
        };
        save["QuickSave"] = [scriptApi]() -> bool {
            return scriptApi != nullptr && scriptApi->QuickSave();
        };
        env["Save"] = save;

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

        sol::table modDataApi = NewTable(lua->lua_state(), 5);
        modDataApi["Get"] = [scriptApi, modId,
                             warnModDataBlocked](const std::string& key)
            -> std::optional<std::string> {
            if (scriptApi == nullptr) {
                return std::nullopt;
            }
            if (scriptApi->IsMultiplayer()) {
                warnModDataBlocked();
                return std::nullopt;
            }
            return scriptApi->GetModData(modId, key);
        };
        modDataApi["Set"] = [scriptApi, modId, warnModDataBlocked](
                                const std::string& key,
                                sol::object value) -> bool {
            if (scriptApi == nullptr) {
                return false;
            }
            if (scriptApi->IsMultiplayer()) {
                warnModDataBlocked();
                return false;
            }
            if (!value.is<std::string>()) {
                return false;
            }
            return scriptApi->SetModData(modId, key, value.as<std::string>());
        };
        modDataApi["Remove"] = [scriptApi, modId, warnModDataBlocked](
                                   const std::string& key) -> bool {
            if (scriptApi == nullptr) {
                return false;
            }
            if (scriptApi->IsMultiplayer()) {
                warnModDataBlocked();
                return false;
            }
            return scriptApi->RemoveModData(modId, key);
        };
        modDataApi["Keys"] = [scriptApi, modId, warnModDataBlocked](
                                 sol::this_state ts) -> sol::table {
            sol::table out = NewTable(ts.L, 0);
            if (scriptApi == nullptr) {
                return out;
            }
            if (scriptApi->IsMultiplayer()) {
                warnModDataBlocked();
                return out;
            }
            const std::vector<std::string> keys =
                scriptApi->ListModDataKeys(modId);
            for (std::size_t i = 0; i < keys.size(); ++i) {
                out[i + 1] = keys[i];
            }
            return out;
        };
        modDataApi["Clear"] = [scriptApi, modId, warnModDataBlocked]() -> bool {
            if (scriptApi == nullptr) {
                return false;
            }
            if (scriptApi->IsMultiplayer()) {
                warnModDataBlocked();
                return false;
            }
            return scriptApi->ClearModData(modId);
        };

        sol::table modApi = NewTable(lua->lua_state(), 2);
        modApi["Id"] = [modId]() -> std::string { return modId; };
        modApi["Data"] = modDataApi;
        env["Mod"] = modApi;

        const LuaJsonMarkers jsonMarkers{
            NewTable(lua->lua_state(), 0), NewTable(lua->lua_state(), 0)};
        sol::table jsonApi = NewTable(lua->lua_state(), 5);
        jsonApi["Array"] = [jsonMarkers](sol::this_state ts) -> sol::table {
            sol::table out = NewTable(ts.L, 1);
            out[jsonMarkers.array] = true;
            return out;
        };
        jsonApi["Null"] = [jsonMarkers](sol::this_state ts) -> sol::table {
            sol::table out = NewTable(ts.L, 1);
            out[jsonMarkers.null] = true;
            return out;
        };
        jsonApi["Encode"] = [jsonMarkers, modId](
                                sol::this_state ts,
                                sol::object value) -> std::optional<std::string> {
            nlohmann::json encoded;
            std::unordered_set<const void*> stack;
            LuaJsonStatus status = LuaJsonStatus::Invalid;
            if (!EncodeLuaJsonValue(value, ts.L, jsonMarkers, 1, stack, encoded,
                                    status)) {
                if (status == LuaJsonStatus::Cycle) {
                    ZL_WARN("script-mod",
                            "[{}] Json.Encode 检测到循环引用", modId);
                }
                return std::nullopt;
            }
            try {
                return encoded.dump();
            } catch (const nlohmann::json::exception&) {
                return std::nullopt;
            }
        };
        jsonApi["Decode"] = [jsonMarkers](sol::this_state ts,
                                           sol::object text) -> sol::object {
            if (!text.is<std::string>()) {
                return sol::nil;
            }
            nlohmann::json parsed;
            try {
                parsed = nlohmann::json::parse(text.as<std::string>());
            } catch (const nlohmann::json::exception&) {
                return sol::nil;
            }
            if (!ValidateDecodedJson(parsed, 1)) {
                return sol::nil;
            }
            return DecodeJsonToLua(parsed, ts.L, jsonMarkers);
        };
        env["Json"] = jsonApi;

        sol::table inventory = NewTable(lua->lua_state(), 8);
        inventory["GetState"] = [scriptApi](sol::this_state ts) -> sol::table {
            ui::InventoryState state;
            if (scriptApi != nullptr) {
                (void)scriptApi->GetInventoryState(state);
            }
            sol::table out = NewTable(ts.L, 12);
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
            sol::table recipes =
                NewTable(ts.L, static_cast<int>(state.recipes.size()));
            for (std::size_t i = 0; i < state.recipes.size(); ++i) {
                const ui::InventoryRecipe& entry = state.recipes[i];
                sol::table recipe = NewTable(ts.L, 12);
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
                        sol::table item = NewTable(ts.L, 3);
                        item["label"] = requirement->label;
                        item["required"] = requirement->required;
                        item["have"] = requirement->have;
                        recipe[key] = item;
                    };
                appendExtraRequirement("cookware", entry.cookware);
                appendExtraRequirement("fuel", entry.fuel);
                sol::table ingredients =
                    NewTable(ts.L, static_cast<int>(entry.ingredients.size()));
                for (std::size_t j = 0; j < entry.ingredients.size(); ++j) {
                    const ui::InventoryRecipeIngredient& ingredient =
                        entry.ingredients[j];
                    sol::table item = NewTable(ts.L, 3);
                    item["label"] = ingredient.label;
                    item["required"] = ingredient.required;
                    item["have"] = ingredient.have;
                    ingredients[j + 1] = item;
                }
                recipe["ingredients"] = ingredients;
                sol::table tools =
                    NewTable(ts.L, static_cast<int>(entry.tools.size()));
                for (std::size_t j = 0; j < entry.tools.size(); ++j) {
                    const ui::InventoryRecipeIngredient& tool = entry.tools[j];
                    sol::table item = NewTable(ts.L, 3);
                    item["label"] = tool.label;
                    item["required"] = tool.required;
                    item["have"] = tool.have;
                    tools[j + 1] = item;
                }
                recipe["tools"] = tools;
                recipes[i + 1] = recipe;
            }
            out["recipes"] = recipes;
            sol::table crafting = NewTable(ts.L, 8);
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
            sol::table queue =
                NewTable(ts.L, static_cast<int>(state.crafting.queue.size()));
            for (std::size_t i = 0; i < state.crafting.queue.size(); ++i) {
                const ui::InventoryQueuedCraft& entry = state.crafting.queue[i];
                sol::table item = NewTable(ts.L, 4);
                item["recipeId"] = entry.recipeId;
                item["outputLabel"] = entry.outputLabel;
                item["outputCount"] = entry.outputCount;
                item["count"] = entry.count;
                queue[i + 1] = item;
            }
            crafting["queue"] = queue;
            out["crafting"] = crafting;
            auto appendSlots = [&](sol::table& target, const auto& source) {
                for (std::size_t i = 0; i < source.size(); ++i) {
                    const ui::InventorySlot& entry = source[i];
                    sol::table slot = NewTable(ts.L, 10);
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
                    target[i + 1] = slot;
                }
            };
            sol::table bag = NewTable(ts.L, static_cast<int>(state.bag.size()));
            sol::table equipment =
                NewTable(ts.L, static_cast<int>(state.equipment.size()));
            sol::table loot = NewTable(ts.L, static_cast<int>(state.loot.size()));
            appendSlots(bag, state.bag);
            appendSlots(equipment, state.equipment);
            appendSlots(loot, state.loot);
            out["bag"] = bag;
            out["equipment"] = equipment;
            out["loot"] = loot;
            return out;
        };
        inventory["Action"] = [scriptApi](sol::table action) -> bool {
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
            const std::string type = action["type"].get_or(std::string());
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
            if (!parseArea(action["from"].get_or(std::string("bag")), parsed.from) ||
                !parseArea(action["to"].get_or(std::string("bag")), parsed.to)) {
                return false;
            }
            parsed.fromIndex = action["fromIndex"].get_or(-1);
            parsed.toIndex = action["toIndex"].get_or(-1);
            parsed.fromEntityId = action["fromEntityId"].get_or<std::uint64_t>(0);
            parsed.count = std::clamp(action["count"].get_or(1), 1, 99);
            return scriptApi->InventoryAction(parsed);
        };
        inventory["Close"] = [scriptApi]() -> bool {
            if (scriptApi == nullptr || !scriptApi->IsInventoryOpen()) {
                return false;
            }
            return !scriptApi->ToggleInventory();
        };
        inventory["PlayClick"] = [scriptApi] {
            if (scriptApi != nullptr) {
                scriptApi->PlayInventoryClick();
            }
        };
        env["Inventory"] = inventory;

        sol::table ui = NewTable(lua->lua_state(), 16);
        ui["LoadDocument"] = [uiApi](const std::string& documentId, const std::string& path,
                                     sol::optional<bool> show, sol::optional<bool> modal,
                                     sol::optional<std::string> group) {
            return uiApi->LoadDocument(documentId, path, show.value_or(true),
                                       modal.value_or(false), group.value_or(""));
        };
        ui["OpenDocument"] = [uiApi](const std::string& documentId, const std::string& path,
                                     sol::optional<bool> modal,
                                     sol::optional<std::string> group) {
            return uiApi->OpenDocument(documentId, path, modal.value_or(true),
                                       group.value_or(""));
        };
        ui["CloseDocument"] = [uiApi](const std::string& documentId,
                                      sol::optional<bool> unload) {
            return uiApi->CloseDocument(documentId, unload.value_or(false));
        };
        ui["UnloadDocument"] = [uiApi](const std::string& documentId) {
            return uiApi->UnloadDocument(documentId);
        };
        ui["UnloadGroup"] = [uiApi](const std::string& group) {
            uiApi->UnloadGroup(group);
        };
        ui["Show"] = [uiApi](const std::string& documentId, sol::optional<bool> visible) {
            return uiApi->Show(documentId, visible.value_or(true));
        };
        ui["IsLoaded"] = [uiApi](const std::string& documentId) {
            return uiApi->IsLoaded(documentId);
        };
        ui["IsVisible"] = [uiApi](const std::string& documentId) {
            return uiApi->IsVisible(documentId);
        };
        ui["SetModal"] = [uiApi](const std::string& documentId, bool modal) {
            return uiApi->SetModal(documentId, modal);
        };
        ui["IsInputCaptured"] = [uiApi]() { return uiApi->IsInputCaptured(); };
        ui["SetText"] = [uiApi](const std::string& documentId, const std::string& elementId,
                                const std::string& text) {
            return uiApi->SetText(documentId, elementId, text);
        };
        ui["SetAttribute"] = [uiApi](const std::string& documentId,
                                     const std::string& elementId, const std::string& name,
                                     const std::string& value) {
            return uiApi->SetAttribute(documentId, elementId, name, value);
        };
        ui["RemoveAttribute"] = [uiApi](const std::string& documentId,
                                        const std::string& elementId,
                                        const std::string& name) {
            return uiApi->RemoveAttribute(documentId, elementId, name);
        };
        ui["SetClass"] = [uiApi](const std::string& documentId, const std::string& elementId,
                                 const std::string& className, sol::optional<bool> enabled) {
            return uiApi->SetClass(documentId, elementId, className, enabled.value_or(true));
        };
        ui["SetProperty"] = [uiApi](const std::string& documentId,
                                    const std::string& elementId,
                                    const std::string& property,
                                    const std::string& value) {
            return uiApi->SetProperty(documentId, elementId, property, value);
        };
        ui["GetValue"] = [uiApi](const std::string& documentId,
                                 const std::string& elementId) {
            return uiApi->GetValue(documentId, elementId);
        };
        ui["SetValue"] = [uiApi](const std::string& documentId, const std::string& elementId,
                                 const std::string& value) {
            return uiApi->SetValue(documentId, elementId, value);
        };
        ui["Focus"] = [uiApi](const std::string& documentId, const std::string& elementId) {
            return uiApi->Focus(documentId, elementId);
        };
        env["UI"] = ui;

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
        CallBudgetScope budget(*this);
        const int status = lua_pcall(L, nargs, 0, -(nargs + 2));
        if (status != LUA_OK) {
            const char* message = lua_tostring(L, -1);
            errorOut = message != nullptr ? message : "(未知脚本错误)";
            lua_pop(L, 2);  // 错误消息 + 处理器
            return false;
        }
        lua_pop(L, 1);  // 处理器
        return true;
    }

    // 在指定沙箱环境中执行一段 chunk（chunkName 以 '@' 开头 → 错误显示相对路径:行号）。
    // resultOut 非空时接收 chunk 的首个返回值（module 用）；否则丢弃。
    bool RunChunk(const std::string& source, const std::string& chunkName,
                  const sol::table& env, std::string& errorOut,
                  sol::object* resultOut = nullptr) {
        lua_State* L = lua->lua_state();
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
        CallBudgetScope budget(*this);
        const int status =
            lua_pcall(L, 0, resultOut != nullptr ? 1 : 0, -2);
        if (status != LUA_OK) {
            const char* message = lua_tostring(L, -1);
            errorOut = message != nullptr ? message : "(未知脚本错误)";
            lua_pop(L, 2);  // 错误消息 + 处理器
            return false;
        }
        if (resultOut != nullptr) {
            *resultOut = sol::stack::pop<sol::object>(L);
        }
        lua_pop(L, 1);  // 处理器
        return true;
    }

    bool RequireModule(lua_State* L, std::string& errorOut);
    static int LuaRequire(lua_State* L);

    sol::environment MakeModEnvironment(
        const ModInfo& mod, sol::table handlers,
        const std::shared_ptr<std::vector<Timer>>& timers,
        const std::string& scriptsRoot) {
        lua_State* L = lua->lua_state();
        sol::environment env(*lua, sol::create, sandboxGlobals);

        env.push();                                       // [env]
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "_G");                        // env._G = env
        lua_pushstring(L, mod.id.c_str());
        lua_pushcclosure(L, LuaPrintBridge, 1);
        lua_setfield(L, -2, "print");                     // env.print = 重定向桥
        lua_pop(L, 1);

        BindApiTables(env, mod, handlers, timers);       // Host API 表（M7-T3/T4）

        // F4-T4：per-mod require 闭包。upvalue：
        // 1=Impl*，2=scripts canonical root，3=模块缓存表，4=加载中/深度表，5=本 mod 环境。
        // 缓存/加载表放在 Lua upvalue 中，随环境一起被 GC，重载即清空。
        env.push();
        lua_pushlightuserdata(L, this);
        lua_pushlstring(L, scriptsRoot.data(), scriptsRoot.size());
        lua_newtable(L);
        lua_newtable(L);
        env.push();
        lua_pushcclosure(L, &Impl::LuaRequire, 5);
        lua_setfield(L, -2, "require");
        lua_pop(L, 1);
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
        const std::string scriptsRoot = CanonicalScriptsRoot(mod.rootPath);
        sol::environment env =
            MakeModEnvironment(mod, handlers, timers, scriptsRoot);
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

bool LuaRuntime::Impl::RequireModule(lua_State* L, std::string& errorOut) {
    std::size_t rootLength = 0;
    const char* rootText = lua_tolstring(L, lua_upvalueindex(2), &rootLength);
    if (rootText == nullptr) {
        errorOut = "require: 脚本根目录不可用";
        return false;
    }
    const std::string scriptsRoot(rootText, rootLength);
    const std::filesystem::path scriptsRootPath(scriptsRoot);

    if (lua_type(L, 1) != LUA_TSTRING) {
        errorOut = "require: 模块名必须是字符串";
        return false;
    }
    std::size_t nameLength = 0;
    const char* nameText = lua_tolstring(L, 1, &nameLength);
    if (nameText == nullptr) {
        errorOut = "require: 模块名必须是字符串";
        return false;
    }
    const std::string_view name(nameText, nameLength);

    bool relative = false;
    std::string relativePath;
    if (!NormalizeRequireName(name, relative, relativePath, errorOut)) {
        return false;
    }

    std::filesystem::path base = scriptsRootPath;
    if (relative) {
        const std::filesystem::path caller = CallerSourcePath(L);
        if (caller.empty() || caller.parent_path().empty()) {
            errorOut = "require: ./ 只能从脚本文件内使用";
            return false;
        }
        base = caller.parent_path();
    }

    std::error_code ec;
    const std::filesystem::path lexical = base / relativePath;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(lexical, ec);
    if (ec) {
        errorOut = "require: 无法解析模块路径：" + relativePath;
        return false;
    }
    if (!RelativeWithinRoot(canonical, scriptsRootPath)) {
        errorOut = "require: 模块越界（只允许 scripts/ 下的 .lua）：" +
                   canonical.generic_string();
        return false;
    }
    const std::string key = canonical.generic_string();

    // 每个 mod 的环境各自持有 cache/loading 表：先查缓存，再查循环/深度。
    lua_getfield(L, lua_upvalueindex(3), key.c_str());
    if (!lua_isnil(L, -1)) {
        return true;
    }
    lua_pop(L, 1);

    lua_getfield(L, lua_upvalueindex(4), key.c_str());
    const bool loading = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    if (loading) {
        errorOut = "require: 检测到循环依赖：" + key;
        return false;
    }

    lua_getfield(L, lua_upvalueindex(4), "__depth");
    const bool hasDepth = lua_isinteger(L, -1) != 0;
    const int previousDepth =
        hasDepth ? static_cast<int>(lua_tointeger(L, -1)) : 0;
    lua_pop(L, 1);
    if (previousDepth >= kMaxRequireDepth) {
        errorOut = "require: 递归深度超过 " +
                   std::to_string(kMaxRequireDepth) + " 层：" + key;
        return false;
    }

    if (!std::filesystem::is_regular_file(canonical, ec) || ec) {
        errorOut = "require: 模块不存在：" + key;
        return false;
    }
    const std::uintmax_t size = std::filesystem::file_size(canonical, ec);
    if (ec) {
        errorOut = "require: 无法读取模块大小：" + key;
        return false;
    }
    if (size > kMaxLuaModuleBytes) {
        errorOut = "require: 模块超过 1 MB：" + key;
        return false;
    }
    const std::optional<std::string> source = ReadFile(canonical);
    if (!source.has_value()) {
        errorOut = "require: 无法读取模块文件：" + key;
        return false;
    }
    if (source->size() > kMaxLuaModuleBytes) {
        errorOut = "require: 模块超过 1 MB：" + key;
        return false;
    }

    struct LoadingGuard {
        lua_State* L;
        const std::string& key;
        int previousDepth;
        ~LoadingGuard() {
            lua_pushnil(L);
            lua_setfield(L, lua_upvalueindex(4), key.c_str());
            lua_pushinteger(L, previousDepth);
            lua_setfield(L, lua_upvalueindex(4), "__depth");
        }
    };

    lua_pushboolean(L, 1);
    lua_setfield(L, lua_upvalueindex(4), key.c_str());
    lua_pushinteger(L, previousDepth + 1);
    lua_setfield(L, lua_upvalueindex(4), "__depth");
    LoadingGuard loadingGuard{L, key, previousDepth};

    sol::table env = sol::stack::get<sol::table>(L, lua_upvalueindex(5));
    if (!env.valid()) {
        errorOut = "require: 模块环境不可用";
        return false;
    }
    sol::object result;
    if (!RunChunk(*source, "@" + key, env, errorOut, &result)) {
        return false;
    }

    sol::object value = result;
    if (!value.valid() || value.get_type() == sol::type::nil) {
        value = sol::make_object(L, true);
    }
    value.push();
    lua_setfield(L, lua_upvalueindex(3), key.c_str());
    value.push();
    return true;
}

int LuaRuntime::Impl::LuaRequire(lua_State* L) {
    Impl* impl = static_cast<Impl*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (impl == nullptr) {
        lua_pushliteral(L, "require: 运行时未初始化");
        return lua_error(L);
    }

    int ok = 0;
    {
        std::string error;
        ok = impl->RequireModule(L, error) ? 1 : 0;
        if (ok == 0) {
            if (error.empty()) {
                error = "require: 未知错误";
            }
            luaL_where(L, 1);
            lua_pushlstring(L, error.data(), error.size());
            lua_concat(L, 2);
        }
    }
    return ok != 0 ? 1 : lua_error(L);
}

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

void LuaRuntime::SetUiHost(ui::ScriptUiHost* host) {
    m_impl->uiHost = host;
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
