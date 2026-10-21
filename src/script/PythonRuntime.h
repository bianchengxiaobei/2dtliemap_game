#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/ScriptEvent.h"
#include "script/IScriptRuntime.h"

namespace zl::ui {
class ScriptUiHost;
}

namespace zl::script {

class ScriptApi;

// Python 3 后端（M8-T2，docs/03 §9.2；实现仅在 ZL_ENABLE_PYTHON=ON 时编入）：
// - pybind11 嵌入 CPython；GIL 纪律：所有脚本调用只发生在主线程，入口统一 gil_scoped_acquire；
// - 每个 mod 独立 globals + 受限 builtins（print 重定向到游戏日志、import 白名单、去危险内建）；
// - 加载/执行错误带 Python traceback 上报（errorOut / ModError），不抛出；
// - M8-T3：SetApi 后绑定 Host API（与 Lua 同名同参数）与 Events.On。
class PythonRuntime final : public IScriptRuntime {
public:
    PythonRuntime();
    ~PythonRuntime() override;

    PythonRuntime(const PythonRuntime&) = delete;
    PythonRuntime& operator=(const PythonRuntime&) = delete;

    // Host API 接入（须在 LoadMod 前调用；nullptr = 未接入）
    void SetApi(const ScriptApi* api);
    void SetUiHost(ui::ScriptUiHost* host);

    const char* Name() const override;
    bool Initialize(std::string& errorOut) override;
    void Shutdown() override;

    bool LoadMod(const ModInfo& mod, std::string& errorOut) override;
    bool ReloadMod(const std::string& modId, std::string& errorOut) override;
    void UnloadMod(const std::string& modId) override;

    std::vector<ModError> DispatchEventToMod(const std::string& modId,
                                             const core::ScriptEvent& ev) override;

    const std::vector<std::string>& LoadedMods() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace zl::script
