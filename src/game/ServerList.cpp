#include "game/ServerList.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <span>
#include <sstream>
#include <string_view>

#include <nlohmann/json.hpp>

#include "save/AtomicFile.h"

namespace zl::game {
namespace {

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool ValidName(std::string_view name) {
    if (name.empty() || name.size() > 64) {
        return false;
    }
    for (const unsigned char value : name) {
        if (value < 0x20u || value == 0x7Fu) {
            return false;
        }
    }
    return true;
}

bool ValidHost(std::string_view host) {
    if (host.empty() || host.size() > 255) {
        return false;
    }
    for (const unsigned char value : host) {
        if (value <= 0x20u || value == 0x7Fu || value == '/' || value == '\\' ||
            value == '"' || value == '\'') {
            return false;
        }
    }
    return true;
}

}  // namespace

ServerListStore::ServerListStore(std::filesystem::path path)
    : m_path(std::move(path)) {}

bool ServerListStore::IsValidEntry(const ServerListEntry& entry) {
    return ValidName(entry.name) && ValidHost(entry.host) && entry.port != 0;
}

bool ServerListStore::Load(std::string* error) {
    m_entries.clear();
    std::error_code filesystemError;
    if (m_path.empty() || !std::filesystem::exists(m_path, filesystemError)) {
        return true;
    }
    std::ifstream input(m_path, std::ios::binary);
    if (!input) {
        SetError(error, "无法读取服务器列表：" + m_path.string());
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    nlohmann::json root = nlohmann::json::parse(buffer.str(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        SetError(error, "服务器列表格式损坏：" + m_path.string());
        return false;
    }
    const auto servers = root.find("servers");
    if (servers == root.end() || !servers->is_array()) {
        return true;
    }
    for (const auto& item : *servers) {
        if (!item.is_object()) {
            continue;
        }
        ServerListEntry entry;
        entry.name = item.value("name", std::string());
        entry.host = item.value("host", std::string());
        const int port = item.value("port", 0);
        if (port < 1 || port > 65535) {
            continue;
        }
        entry.port = static_cast<std::uint16_t>(port);
        if (!IsValidEntry(entry)) {
            continue;
        }
        const bool duplicate = std::any_of(
            m_entries.begin(), m_entries.end(), [&](const ServerListEntry& existing) {
                return existing.host == entry.host && existing.port == entry.port;
            });
        if (!duplicate) {
            m_entries.push_back(std::move(entry));
        }
        if (m_entries.size() >= kMaxEntries) {
            break;
        }
    }
    return true;
}

bool ServerListStore::Save(std::string* error) {
    nlohmann::json root = nlohmann::json::object();
    nlohmann::json servers = nlohmann::json::array();
    for (const ServerListEntry& entry : m_entries) {
        if (!IsValidEntry(entry)) {
            continue;
        }
        servers.push_back(
            nlohmann::json{{"name", entry.name}, {"host", entry.host}, {"port", entry.port}});
    }
    root["servers"] = std::move(servers);
    const std::string text = root.dump(2) + "\n";
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    std::error_code filesystemError;
    if (!m_path.parent_path().empty()) {
        std::filesystem::create_directories(m_path.parent_path(), filesystemError);
    }
    return save::WriteFileAtomic(
        m_path, std::span<const std::uint8_t>(bytes.data(), bytes.size()), error);
}

bool ServerListStore::Upsert(const std::string& name, const std::string& host,
                             std::uint16_t port, std::string* error) {
    ServerListEntry entry{name, host, port};
    if (!IsValidEntry(entry)) {
        SetError(error, "服务器名称、地址或端口非法");
        return false;
    }
    auto it = std::find_if(
        m_entries.begin(), m_entries.end(), [&](const ServerListEntry& existing) {
            return existing.host == host && existing.port == port;
        });
    if (it != m_entries.end()) {
        it->name = name;
        std::rotate(m_entries.begin(), it, it + 1);
    } else {
        m_entries.insert(m_entries.begin(), std::move(entry));
        if (m_entries.size() > kMaxEntries) {
            m_entries.resize(kMaxEntries);
        }
    }
    return true;
}

void ServerListStore::Clear() { m_entries.clear(); }

}  // namespace zl::game
