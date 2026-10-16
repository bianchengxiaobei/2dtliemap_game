#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/ScriptEvent.h"
#include "script/IScriptRuntime.h"

namespace zl::script {

// 脚本系统门面（M7-T1，docs/03 §3.2）：
// - 后端注册表：RegisterRuntime / FindRuntime；
// - mod 生命周期路由：按 mod.backend 选择 runtime；
// - 事件派发：DispatchEvent 按加载顺序发给所有已加载 mod（M7-T4 接 EventBus）；
// - 错误隔离：派发期脚本错误 → 记日志（mod id + 路径 + 行号）并禁用该 mod，
//   后续事件跳过它；ReloadMod 成功即恢复（失败保持禁用）。
class ScriptManager {
public:
    ScriptManager() = default;
    ~ScriptManager();

    ScriptManager(const ScriptManager&) = delete;
    ScriptManager& operator=(const ScriptManager&) = delete;

    // ---- 后端注册表 ----
    void RegisterRuntime(std::unique_ptr<IScriptRuntime> runtime);  // 重名拒绝
    std::size_t RuntimeCount() const { return m_runtimes.size(); }
    IScriptRuntime* FindRuntime(std::string_view name);

    // ---- 生命周期 ----
    bool InitializeAll();  // 初始化全部后端；返回是否全部成功（失败后端不阻断其它）
    void ShutdownAll();    // 逆序关闭；清空已加载 mod 记录（可再次 InitializeAll）

    // ---- mod 生命周期（M7-T5 ModLoader 消费；errorOut 仅失败时写入） ----
    bool LoadMod(const ModInfo& mod, std::string& errorOut);
    bool ReloadMod(std::string_view modId, std::string& errorOut);
    void UnloadMod(std::string_view modId);
    bool IsModLoaded(std::string_view modId) const;
    std::vector<std::string> LoadedModIds() const;  // 按加载顺序

    // ---- 事件派发（M7-T4 接 EventBus） ----
    void DispatchEvent(const core::ScriptEvent& ev);

    // ---- 错误隔离 ----
    bool IsModDisabled(std::string_view modId) const;
    std::size_t DisabledModCount() const;

private:
    struct RuntimeEntry {
        std::unique_ptr<IScriptRuntime> runtime;
        bool initialized = false;
    };

    struct ModRecord {
        ModInfo info;
        IScriptRuntime* runtime = nullptr;
        bool disabled = false;
        std::string disableReason;
        int loadSeq = 0;  // loadOrder 相同时保持加载先后
    };

    RuntimeEntry* FindEntry(std::string_view name);
    ModRecord* FindMod(std::string_view modId);
    const ModRecord* FindMod(std::string_view modId) const;
    void DisableMod(ModRecord& record, std::string reason);

    std::vector<RuntimeEntry> m_runtimes;
    std::vector<ModRecord> m_mods;  // 按 (loadOrder, loadSeq) 排序
    int m_nextLoadSeq = 0;
};

}  // namespace zl::script
