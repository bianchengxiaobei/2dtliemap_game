#pragma once

#include <string>
#include <vector>

#include "core/ScriptEvent.h"

namespace zl::script {

// mod 描述（M7-T5 ModLoader 从 mods/<id>/mod.json 解析）。
struct ModInfo {
    std::string id;
    std::string name;
    std::string version;
    int apiVersion = 1;       // 对照 ScriptApi 的 API_VERSION（M7-T3）
    std::string backend;      // "lua" / "python"
    std::string rootPath;     // mods/<id>/
    std::string mainScript;   // 入口脚本（相对 rootPath，如 scripts/main.lua）
    int loadOrder = 100;      // 越小越先加载/派发（T5 加载顺序表）
};

// 派发期脚本错误（runtime 捕获后上报；ScriptManager 负责日志与禁用策略）。
struct ModError {
    std::string modId;
    std::string message;  // 含脚本路径与行号（由 runtime 组装）
};

// 脚本后端抽象（M7-T1，docs/03 §3.1）：一个实例 = 一类脚本环境（Lua 解释器等）。
// 职责边界：runtime 只管「执行 + 捕获错误」；禁用/日志/路由策略由 ScriptManager 决定。
// 说明：docs/03 原文的 DispatchEvent(ev) 落在 ScriptManager 层（跨后端全量派发）；
// runtime 提供逐 mod 的 DispatchEventToMod，使错误能精确归因到 mod。
class IScriptRuntime {
public:
    virtual ~IScriptRuntime() = default;

    virtual const char* Name() const = 0;  // "lua" / "python"

    virtual bool Initialize(std::string& errorOut) = 0;  // 创建解释器/沙箱
    virtual void Shutdown() = 0;

    virtual bool LoadMod(const ModInfo& mod, std::string& errorOut) = 0;
    virtual bool ReloadMod(const std::string& modId, std::string& errorOut) = 0;
    virtual void UnloadMod(const std::string& modId) = 0;

    // 把事件派发给单个 mod 的订阅者；捕获的脚本错误按列表返回（空 = 成功）。
    virtual std::vector<ModError> DispatchEventToMod(const std::string& modId,
                                                     const core::ScriptEvent& ev) = 0;

    virtual const std::vector<std::string>& LoadedMods() const = 0;
};

}  // namespace zl::script
