#include "script/ModLoader.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "script/ScriptApi.h"
#include "script/ScriptManager.h"

namespace zl::script {
namespace {

constexpr const char* kModule = "script";

// 读整个文件（失败 → nullopt）
std::optional<std::string> ReadFileText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

bool ModLoader::ParseModInfo(const std::string& jsonText, const std::filesystem::path& modDir,
                             const std::string& defaultBackend, ModInfo& out,
                             std::string& error) {
    error.clear();
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(jsonText);
    } catch (const nlohmann::json::parse_error& e) {
        error = std::string("mod.json 解析失败：") + e.what();
        return false;
    }
    if (!doc.is_object()) {
        error = "mod.json 顶层必须是对象";
        return false;
    }

    const std::string dirName = modDir.filename().string();
    try {
        out = ModInfo{};
        std::string id = doc.value("id", std::string());
        if (id.empty()) {
            id = dirName;  // 缺省：目录名即 mod id
        } else if (id != dirName) {
            ZL_WARN(kModule, "mod id 与目录名不一致：{}（目录 {}；以 id 为准）", id, dirName);
        }
        out.id = id;
        out.name = doc.value("name", id);
        out.version = doc.value("version", std::string("0.0.0"));

        const int api = doc.value("api", ScriptApi::kApiVersion);
        if (api > ScriptApi::kApiVersion) {
            error = "需要更新的 API 版本：" + std::to_string(api) + "（当前 " +
                    std::to_string(ScriptApi::kApiVersion) + "）";
            return false;
        }
        if (api < ScriptApi::kApiVersion) {
            ZL_WARN(kModule, "mod [{}] 声明的 API 版本较旧：{}（当前 {}；尽力加载）", id, api,
                    ScriptApi::kApiVersion);
        }
        out.apiVersion = api;

        out.backend = doc.value("backend", defaultBackend);
        out.mainScript = doc.value("main", std::string("scripts/main.lua"));
        out.loadOrder = doc.value("loadOrder", 100);
        out.rootPath = modDir.string();
        return true;
    } catch (const nlohmann::json::exception& e) {
        error = std::string("mod.json 字段类型不符：") + e.what();
        return false;
    }
}

ModLoader::Result ModLoader::ScanAndLoad(const std::filesystem::path& modsDir) {
    Result result;
    std::error_code ec;
    if (!std::filesystem::is_directory(modsDir, ec)) {
        ZL_INFO(kModule, "mods 目录不存在，跳过 mod 加载：{}", modsDir.generic_string());
        return result;
    }

    std::vector<std::filesystem::path> dirs;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(modsDir, ec)) {
        if (entry.is_directory(ec)) {
            dirs.push_back(entry.path());
        }
    }
    std::sort(dirs.begin(), dirs.end());  // 目录名排序：加载/日志确定性

    for (const std::filesystem::path& dir : dirs) {
        ++result.found;
        const std::string dirName = dir.filename().string();
        const std::optional<std::string> manifest = ReadFileText(dir / "mod.json");
        if (!manifest.has_value()) {
            ZL_WARN(kModule, "跳过 mod 目录（无 mod.json）：{}", dirName);
            ++result.skipped;
            result.failedIds.push_back(dirName);
            continue;
        }

        ModInfo info;
        std::string error;
        if (!ParseModInfo(*manifest, dir, m_defaultBackend, info, error)) {
            ZL_ERROR(kModule, "mod 清单无效 [{}]：{}", dirName, error);
            ++result.skipped;
            result.failedIds.push_back(dirName);
            continue;
        }

        if (!m_scripts.LoadMod(info, error)) {
            ZL_ERROR(kModule, "mod 加载失败 [{}]：{}", info.id, error);
            ++result.skipped;
            result.failedIds.push_back(info.id);
            continue;
        }
        ++result.loaded;
        result.loadedIds.push_back(info.id);
    }

    ZL_INFO(kModule, "mod 扫描完成：{} 个目录，加载 {}，跳过 {}", result.found, result.loaded,
            result.skipped);
    return result;
}

}  // namespace zl::script
