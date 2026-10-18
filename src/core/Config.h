#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "core/Log.h"

namespace zl::core {

// 配置系统（M1-T5）：nlohmann-json 加载 config/game.json；
// 缺失键用调用方默认值；命令行 --a.b=value 覆盖；Reload() 支持热重载键位。
class Config {
public:
    bool LoadFile(const std::filesystem::path& path);
    bool Reload();

    // 解析 --a.b=value（值按 JSON 解析，失败则当字符串）；跳过 --config 及其值
    void ApplyCommandLine(int argc, const char* const* argv);

    // 从命令行找 --config 路径；没有则返回 fallback
    static std::string FindConfigPath(int argc, const char* const* argv,
                                      const std::string& fallback);

    template <typename T>
    T Get(std::string_view path, const T& fallback) const {
        const nlohmann::json* node = FindNode(path);
        if (node == nullptr) {
            return fallback;
        }
        try {
            return node->get<T>();
        } catch (const std::exception&) {
            ZL_WARN("config", "配置项 '{}' 类型不符，使用默认值", path);
            return fallback;
        }
    }

    void Set(std::string_view path, const nlohmann::json& value);
    bool Has(std::string_view path) const { return FindNode(path) != nullptr; }
    // M9-T6：设置页把运行时修改写回配置文件（临时文件 + 替换，失败不清空原文件）。
    bool Save(std::string* error = nullptr);

    const nlohmann::json& Raw() const { return m_data; }
    const std::filesystem::path& Path() const { return m_path; }

private:
    static const nlohmann::json* FindNodeIn(const nlohmann::json& root, std::string_view path);
    static void MergeJson(nlohmann::json& dst, const nlohmann::json& src);
    static void SetNode(nlohmann::json& root, std::string_view path, const nlohmann::json& value);

    const nlohmann::json* FindNode(std::string_view path) const;
    void Rebuild();

    std::filesystem::path m_path;
    nlohmann::json m_fileData = nlohmann::json::object();
    nlohmann::json m_cmdOverrides = nlohmann::json::object();
    nlohmann::json m_runtimeOverrides = nlohmann::json::object();
    nlohmann::json m_data = nlohmann::json::object();
};

}  // namespace zl::core
