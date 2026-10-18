#include "core/Config.h"

#include <fstream>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace zl::core {

bool Config::LoadFile(const std::filesystem::path& path) {
    m_path = path;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        ZL_WARN("config", "配置文件不存在: {}（使用代码内默认值）", path.string());
        m_fileData = nlohmann::json::object();
        Rebuild();
        return false;
    }

    try {
        m_fileData = nlohmann::json::parse(file);
    } catch (const nlohmann::json::parse_error& e) {
        ZL_ERROR("config", "配置文件解析失败: {} | 字节偏移 {} | {}", path.string(), e.byte,
                 e.what());
        m_fileData = nlohmann::json::object();
        Rebuild();
        return false;
    }

    ZL_INFO("config", "配置已加载: {}", path.string());
    Rebuild();
    return true;
}

bool Config::Reload() {
    if (m_path.empty()) {
        return false;
    }
    bool ok = LoadFile(m_path);
    if (ok) {
        ZL_INFO("config", "配置已热重载: {}", m_path.string());
    }
    return ok;
}

void Config::Rebuild() {
    m_data = m_fileData;
    MergeJson(m_data, m_cmdOverrides);
    MergeJson(m_data, m_runtimeOverrides);
}

void Config::MergeJson(nlohmann::json& dst, const nlohmann::json& src) {
    for (auto it = src.begin(); it != src.end(); ++it) {
        if (it->is_object() && dst.contains(it.key()) && dst[it.key()].is_object()) {
            MergeJson(dst[it.key()], *it);
        } else {
            dst[it.key()] = *it;
        }
    }
}

const nlohmann::json* Config::FindNodeIn(const nlohmann::json& root, std::string_view path) {
    const nlohmann::json* node = &root;
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const std::size_t dot = path.find('.', begin);
        const std::string_view part = path.substr(
            begin, dot == std::string_view::npos ? std::string_view::npos : dot - begin);
        if (!node->is_object()) {
            return nullptr;
        }
        const auto it = node->find(std::string(part));
        if (it == node->end()) {
            return nullptr;
        }
        node = &*it;
        if (dot == std::string_view::npos) {
            break;
        }
        begin = dot + 1;
    }
    return node;
}

const nlohmann::json* Config::FindNode(std::string_view path) const {
    return FindNodeIn(m_data, path);
}

void Config::SetNode(nlohmann::json& root, std::string_view path, const nlohmann::json& value) {
    nlohmann::json* node = &root;
    std::size_t begin = 0;
    for (;;) {
        const std::size_t dot = path.find('.', begin);
        const std::string key(path.substr(begin, dot == std::string_view::npos
                                                     ? std::string_view::npos
                                                     : dot - begin));
        if (dot == std::string_view::npos) {
            (*node)[key] = value;
            return;
        }
        node = &(*node)[key];
        begin = dot + 1;
    }
}

void Config::Set(std::string_view path, const nlohmann::json& value) {
    SetNode(m_runtimeOverrides, path, value);
    Rebuild();
}

bool Config::Save(std::string* error) {
    if (m_path.empty()) {
        if (error != nullptr) {
            *error = "配置文件路径为空";
        }
        return false;
    }

    nlohmann::json saved = m_fileData;
    MergeJson(saved, m_runtimeOverrides);

    std::filesystem::path tempPath = m_path;
    tempPath += ".tmp";
    std::error_code ec;
    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            if (error != nullptr) {
                *error = "无法写入临时配置文件: " + tempPath.string();
            }
            return false;
        }
        file << saved.dump(2) << '\n';
        file.flush();
        if (!file) {
            if (error != nullptr) {
                *error = "写入临时配置文件失败: " + tempPath.string();
            }
            std::filesystem::remove(tempPath, ec);
            return false;
        }
    }

#if defined(_WIN32)
    if (!MoveFileExW(tempPath.c_str(), m_path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        if (error != nullptr) {
            *error = "替换配置文件失败（Win32 错误 " +
                     std::to_string(static_cast<int>(GetLastError())) + "）";
        }
        std::error_code cleanup;
        std::filesystem::remove(tempPath, cleanup);
        return false;
    }
#else
    std::filesystem::rename(tempPath, m_path, ec);
    if (ec) {
        if (error != nullptr) {
            *error = "替换配置文件失败: " + ec.message();
        }
        std::error_code cleanup;
        std::filesystem::remove(tempPath, cleanup);
        return false;
    }
#endif

    m_fileData = saved;
    m_runtimeOverrides = nlohmann::json::object();
    Rebuild();
    if (error != nullptr) {
        error->clear();
    }
    ZL_INFO("config", "配置已保存: {}", m_path.string());
    return true;
}

void Config::ApplyCommandLine(int argc, const char* const* argv) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--config") {
            ++i;  // 跳过路径值
            continue;
        }
        if (arg.size() < 3 || !arg.starts_with("--")) {
            continue;
        }
        arg.remove_prefix(2);
        const std::size_t eq = arg.find('=');
        if (eq == std::string_view::npos) {
            continue;  // 非 key=value 形式暂不支持
        }
        const std::string_view key = arg.substr(0, eq);
        const std::string_view rawValue = arg.substr(eq + 1);

        nlohmann::json value;
        try {
            value = nlohmann::json::parse(rawValue);
        } catch (const std::exception&) {
            value = std::string(rawValue);
        }
        SetNode(m_cmdOverrides, key, value);
        ZL_INFO("config", "命令行覆盖: {} = {}", key, value.dump());
    }
    Rebuild();
}

std::string Config::FindConfigPath(int argc, const char* const* argv,
                                   const std::string& fallback) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string_view(argv[i]) == "--config") {
            return argv[i + 1];
        }
    }
    return fallback;
}

}  // namespace zl::core
