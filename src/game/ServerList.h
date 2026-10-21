#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace zl::game {

struct ServerListEntry {
    std::string name;
    std::string host;
    std::uint16_t port = 7777;
};

class ServerListStore {
public:
    static constexpr std::size_t kMaxEntries = 8;

    explicit ServerListStore(std::filesystem::path path = "saves/servers.json");

    bool Load(std::string* error = nullptr);
    bool Save(std::string* error = nullptr);

    bool Upsert(const std::string& name, const std::string& host, std::uint16_t port,
                std::string* error = nullptr);
    void Clear();

    const std::vector<ServerListEntry>& Entries() const { return m_entries; }
    const std::filesystem::path& Path() const { return m_path; }

    static bool IsValidEntry(const ServerListEntry& entry);

private:
    std::filesystem::path m_path;
    std::vector<ServerListEntry> m_entries;
};

}  // namespace zl::game
