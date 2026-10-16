#include "core/Config.h"

#include <fstream>

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
    SetNode(m_cmdOverrides, path, value);
    Rebuild();
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
