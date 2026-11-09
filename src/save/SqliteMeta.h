#pragma once

#include <filesystem>
#include <string>

#include "save/SaveTypes.h"

struct sqlite3;

namespace zl::save {

inline constexpr int kMetaSchemaVersion = 2;

class SqliteMeta {
public:
    SqliteMeta() = default;
    ~SqliteMeta();

    SqliteMeta(const SqliteMeta&) = delete;
    SqliteMeta& operator=(const SqliteMeta&) = delete;

    bool Open(const std::filesystem::path& path, bool createIfMissing,
              std::string* error = nullptr);
    bool Initialize(std::string* error = nullptr);
    bool Write(const SaveMetadata& metadata, std::string* error = nullptr);
    bool Read(SaveMetadata* metadata, std::string* error = nullptr) const;
    void Close();
    bool IsOpen() const { return m_db != nullptr; }

private:
    sqlite3* m_db = nullptr;
};

}  // namespace zl::save
