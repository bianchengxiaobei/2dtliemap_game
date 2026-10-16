#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "script/IScriptRuntime.h"

namespace zl::script {

class ScriptManager;

// M7-T5：mod 加载器（docs/03 §6）——扫描 mods/<目录>/mod.json 并交给 ScriptManager 加载。
// - mod.json 字段：id/name/version/api/backend/main/loadOrder（缺省见 ParseModInfo）；
// - 目录扫描按目录名排序（加载/日志确定性）；派发顺序由 manager 按 loadOrder 稳定排序；
// - 单个 mod 失败（无清单/JSON 坏/api 过新/后端不可用…）只记日志并跳过，不阻断其它 mod；
// - 沙箱收口见 LuaRuntime（os/io/debug/package 从不加载，T2 起生效）。
class ModLoader {
public:
    struct Result {
        int found = 0;                       // 发现的候选子目录数
        int loaded = 0;                      // 成功加载数
        int skipped = 0;                     // 跳过数（含解析/加载失败）
        std::vector<std::string> loadedIds;  // 按目录序
        std::vector<std::string> failedIds;  // 名称（无清单时用目录名）
    };

    // defaultBackend：mod.json 无 backend 字段时使用（config script.backend）
    ModLoader(ScriptManager& scripts, std::string defaultBackend)
        : m_scripts(scripts), m_defaultBackend(std::move(defaultBackend)) {}

    // 目录不存在 → 空结果（不算错误；mod 是可选内容）
    Result ScanAndLoad(const std::filesystem::path& modsDir);

    // 解析单个 mod.json（热重载/测试复用）：失败写 error，成功填 out
    // （rootPath = modDir；id 缺省用目录名；api 默认 = 当前 API 版本）
    static bool ParseModInfo(const std::string& jsonText, const std::filesystem::path& modDir,
                             const std::string& defaultBackend, ModInfo& out, std::string& error);

private:
    ScriptManager& m_scripts;
    std::string m_defaultBackend;
};

}  // namespace zl::script
