#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/ScriptEvent.h"
#include "script/IScriptRuntime.h"

namespace zl::script {

class ScriptApi;

// Lua 5.4 后端（M7-T2，docs/03 §9.1）：
// - sol2 管理解释器；每个 mod 独立 _ENV 沙箱（白名单基础库，见 cpp）；
// - 脚本 print 重定向到游戏日志（[mod-id] 前缀）；
// - 加载/执行错误带 traceback 上报（errorOut / ModError），不抛出；
// - M7-T3：SetApi 后，每个 mod 环境获得 Host API 表（Game/Log/Player/World/Items/Zombies）。
class LuaRuntime final : public IScriptRuntime {
public:
    LuaRuntime();
    ~LuaRuntime() override;

    LuaRuntime(const LuaRuntime&) = delete;
    LuaRuntime& operator=(const LuaRuntime&) = delete;

    // Host API 接入（须在 LoadMod 前调用；nullptr = 未接入，API 调用返回安全默认值）
    void SetApi(const ScriptApi* api);

    const char* Name() const override;
    bool Initialize(std::string& errorOut) override;
    void Shutdown() override;

    bool LoadMod(const ModInfo& mod, std::string& errorOut) override;
    bool ReloadMod(const std::string& modId, std::string& errorOut) override;
    void UnloadMod(const std::string& modId) override;

    // M7-T2 阶段无事件订阅（Events.On 在 M7-T4 接通），恒返回空。
    std::vector<ModError> DispatchEventToMod(const std::string& modId,
                                             const core::ScriptEvent& ev) override;

    const std::vector<std::string>& LoadedMods() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace zl::script
