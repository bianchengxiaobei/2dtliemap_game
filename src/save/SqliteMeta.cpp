#include "save/SqliteMeta.h"

#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

#include <sqlite3.h>

namespace zl::save {
namespace {

constexpr const char* kSchemaSql = R"sql(
CREATE TABLE IF NOT EXISTS meta (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    save_id TEXT NOT NULL,
    name TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    save_version INTEGER NOT NULL,
    game_version TEXT NOT NULL,
    map_id TEXT NOT NULL,
    thumbnail TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS players (
    stable_id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    is_local INTEGER NOT NULL
);
PRAGMA user_version = 1;
)sql";

bool Fail(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
    return false;
}

std::string PathToUtf8(const std::filesystem::path& path) {
    const std::u8string value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

bool Exec(sqlite3* db, const char* sql, std::string* error) {
    char* message = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        std::string detail = message != nullptr ? message : sqlite3_errmsg(db);
        sqlite3_free(message);
        return Fail(error, "SQLite 执行失败: " + detail);
    }
    return true;
}

bool Prepare(sqlite3* db, const char* sql, sqlite3_stmt** statement,
             std::string* error) {
    const int rc = sqlite3_prepare_v2(db, sql, -1, statement, nullptr);
    if (rc != SQLITE_OK) {
        return Fail(error, "SQLite 准备失败: " + std::string(sqlite3_errmsg(db)));
    }
    return true;
}

class Statement {
public:
    Statement() = default;
    ~Statement() {
        if (m_statement != nullptr) {
            sqlite3_finalize(m_statement);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    sqlite3_stmt** Address() { return &m_statement; }
    sqlite3_stmt* Get() const { return m_statement; }

private:
    sqlite3_stmt* m_statement = nullptr;
};

bool BindText(sqlite3_stmt* statement, int index, const std::string& value,
              std::string* error) {
    const int rc = sqlite3_bind_text(statement, index, value.c_str(), -1,
                                     SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        return Fail(error, "SQLite 绑定文本失败");
    }
    return true;
}

bool ReadUserVersion(sqlite3* db, int* version, std::string* error) {
    sqlite3_stmt* raw = nullptr;
    if (!Prepare(db, "PRAGMA user_version;", &raw, error)) {
        return false;
    }
    Statement statement;
    *statement.Address() = raw;
    const int rc = sqlite3_step(statement.Get());
    if (rc != SQLITE_ROW) {
        return Fail(error, "SQLite 读取元数据版本失败");
    }
    *version = sqlite3_column_int(statement.Get(), 0);
    return true;
}

std::string ColumnText(sqlite3_stmt* statement, int column) {
    const unsigned char* text = sqlite3_column_text(statement, column);
    if (text == nullptr) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(text));
}

}  // namespace

SqliteMeta::~SqliteMeta() { Close(); }

bool SqliteMeta::Open(const std::filesystem::path& path, bool createIfMissing,
                      std::string* error) {
    Close();
    const std::string utf8Path = PathToUtf8(path);
    const int flags = SQLITE_OPEN_READWRITE | (createIfMissing ? SQLITE_OPEN_CREATE : 0);
    const int rc = sqlite3_open_v2(utf8Path.c_str(), &m_db, flags, nullptr);
    if (rc != SQLITE_OK) {
        std::string detail = m_db != nullptr ? sqlite3_errmsg(m_db) : "未知错误";
        if (m_db != nullptr) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        return Fail(error, "打开元数据库失败: " + utf8Path + "（" + detail + "）");
    }
    sqlite3_busy_timeout(m_db, 2000);
    return true;
}

bool SqliteMeta::Initialize(std::string* error) {
    if (m_db == nullptr) {
        return Fail(error, "元数据库未打开");
    }
    int version = 0;
    if (!ReadUserVersion(m_db, &version, error)) {
        return false;
    }
    if (version > kMetaSchemaVersion) {
        return Fail(error, "元数据版本不兼容: 读取到 " + std::to_string(version) +
                               "，支持 " + std::to_string(kMetaSchemaVersion));
    }
    return Exec(m_db, kSchemaSql, error);
}

bool SqliteMeta::Write(const SaveMetadata& metadata, std::string* error) {
    if (m_db == nullptr) {
        return Fail(error, "元数据库未打开");
    }
    if (metadata.id.empty() || metadata.name.empty()) {
        return Fail(error, "存档 id 或名称不能为空");
    }
    if (metadata.saveVersion == 0) {
        return Fail(error, "存档版本不能为 0");
    }
    if (metadata.gameVersion.empty()) {
        return Fail(error, "游戏版本不能为空");
    }

    std::unordered_set<std::uint64_t> playerIds;
    for (const SavePlayer& player : metadata.players) {
        if (player.name.empty()) {
            return Fail(error, "玩家名称不能为空");
        }
        if (player.stableId > static_cast<std::uint64_t>(
                                 std::numeric_limits<sqlite3_int64>::max())) {
            return Fail(error, "玩家 StableId 超出 SQLite 范围");
        }
        if (!playerIds.insert(player.stableId).second) {
            return Fail(error, "玩家 StableId 重复");
        }
    }

    if (!Exec(m_db, "BEGIN IMMEDIATE;", error)) {
        return false;
    }

    auto rollback = [this]() {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
    };

    Statement metaStatement;
    if (!Prepare(m_db,
                 "INSERT OR REPLACE INTO meta "
                 "(id, save_id, name, created_at, updated_at, save_version, "
                 "game_version, map_id, thumbnail) "
                 "VALUES (1, ?, ?, ?, ?, ?, ?, ?, ?);",
                 metaStatement.Address(), error)) {
        rollback();
        return false;
    }
    sqlite3_stmt* meta = metaStatement.Get();
    if (!BindText(meta, 1, metadata.id, error) ||
        !BindText(meta, 2, metadata.name, error) ||
        sqlite3_bind_int64(meta, 3, static_cast<sqlite3_int64>(metadata.createdAt)) !=
            SQLITE_OK ||
        sqlite3_bind_int64(meta, 4, static_cast<sqlite3_int64>(metadata.updatedAt)) !=
            SQLITE_OK ||
        sqlite3_bind_int(meta, 5, static_cast<int>(metadata.saveVersion)) != SQLITE_OK ||
        !BindText(meta, 6, metadata.gameVersion, error) ||
        !BindText(meta, 7, metadata.mapId, error) ||
        !BindText(meta, 8, metadata.thumbnail, error)) {
        rollback();
        return false;
    }
    if (sqlite3_step(meta) != SQLITE_DONE) {
        const std::string detail = sqlite3_errmsg(m_db);
        rollback();
        return Fail(error, "写入存档元数据失败: " + detail);
    }

    if (!Exec(m_db, "DELETE FROM players;", error)) {
        rollback();
        return false;
    }
    if (!metadata.players.empty()) {
        Statement playerStatement;
        if (!Prepare(m_db,
                     "INSERT INTO players (stable_id, name, is_local) VALUES (?, ?, ?);",
                     playerStatement.Address(), error)) {
            rollback();
            return false;
        }
        sqlite3_stmt* player = playerStatement.Get();
        for (const SavePlayer& entry : metadata.players) {
            sqlite3_reset(player);
            sqlite3_clear_bindings(player);
            if (sqlite3_bind_int64(player, 1,
                                   static_cast<sqlite3_int64>(entry.stableId)) !=
                    SQLITE_OK ||
                !BindText(player, 2, entry.name, error) ||
                sqlite3_bind_int(player, 3, entry.local ? 1 : 0) != SQLITE_OK) {
                rollback();
                return false;
            }
            if (sqlite3_step(player) != SQLITE_DONE) {
                const std::string detail = sqlite3_errmsg(m_db);
                rollback();
                return Fail(error, "写入玩家元数据失败: " + detail);
            }
        }
    }

    if (!Exec(m_db, "COMMIT;", error)) {
        rollback();
        return false;
    }
    return true;
}

bool SqliteMeta::Read(SaveMetadata* metadata, std::string* error) const {
    if (m_db == nullptr) {
        return Fail(error, "元数据库未打开");
    }
    if (metadata == nullptr) {
        return Fail(error, "元数据输出为空");
    }

    int version = 0;
    if (!ReadUserVersion(m_db, &version, error)) {
        return false;
    }
    if (version == 0) {
        return Fail(error, "元数据未初始化或已损坏");
    }
    if (version != kMetaSchemaVersion) {
        return Fail(error, "元数据版本不兼容: 读取到 " + std::to_string(version) +
                               "，支持 " + std::to_string(kMetaSchemaVersion));
    }

    Statement metaStatement;
    if (!Prepare(m_db,
                 "SELECT save_id, name, created_at, updated_at, save_version, "
                 "game_version, map_id, thumbnail FROM meta WHERE id = 1;",
                 metaStatement.Address(), error)) {
        return false;
    }
    sqlite3_stmt* meta = metaStatement.Get();
    const int rc = sqlite3_step(meta);
    if (rc == SQLITE_DONE) {
        return Fail(error, "存档元数据记录不存在");
    }
    if (rc != SQLITE_ROW) {
        return Fail(error, "读取存档元数据失败: " +
                               std::string(sqlite3_errmsg(m_db)));
    }

    SaveMetadata loaded;
    loaded.id = ColumnText(meta, 0);
    loaded.name = ColumnText(meta, 1);
    loaded.createdAt = sqlite3_column_int64(meta, 2);
    loaded.updatedAt = sqlite3_column_int64(meta, 3);
    const int saveVersion = sqlite3_column_int(meta, 4);
    if (saveVersion <= 0 || saveVersion > 65535) {
        return Fail(error, "存档元数据版本号非法");
    }
    loaded.saveVersion = static_cast<std::uint16_t>(saveVersion);
    loaded.gameVersion = ColumnText(meta, 5);
    loaded.mapId = ColumnText(meta, 6);
    loaded.thumbnail = ColumnText(meta, 7);

    Statement playerStatement;
    if (!Prepare(m_db,
                 "SELECT stable_id, name, is_local FROM players ORDER BY stable_id;",
                 playerStatement.Address(), error)) {
        return false;
    }
    sqlite3_stmt* player = playerStatement.Get();
    while (true) {
        const int step = sqlite3_step(player);
        if (step == SQLITE_DONE) {
            break;
        }
        if (step != SQLITE_ROW) {
            return Fail(error, "读取玩家元数据失败: " +
                                   std::string(sqlite3_errmsg(m_db)));
        }
        SavePlayer entry;
        const sqlite3_int64 stableId = sqlite3_column_int64(player, 0);
        if (stableId < 0) {
            return Fail(error, "玩家 StableId 非法");
        }
        entry.stableId = static_cast<std::uint64_t>(stableId);
        entry.name = ColumnText(player, 1);
        entry.local = sqlite3_column_int(player, 2) != 0;
        loaded.players.push_back(std::move(entry));
    }

    if (loaded.id.empty() || loaded.name.empty() ||
        loaded.saveVersion == 0 || loaded.gameVersion.empty()) {
        return Fail(error, "存档元数据缺少必要字段");
    }
    *metadata = std::move(loaded);
    return true;
}

void SqliteMeta::Close() {
    if (m_db != nullptr) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

}  // namespace zl::save
