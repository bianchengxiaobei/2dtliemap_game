#include "save/SaveManager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/Log.h"
#include "save/BinaryReader.h"
#include "save/BinaryWriter.h"
#include "save/ChunkDiffStore.h"
#include "save/EntityStore.h"
#include "save/SqliteMeta.h"
#include "save/Thumbnail.h"
#include "world/Chunk.h"

namespace zl::save {
namespace {

constexpr std::uint64_t kMaxSaveFileBytes =
    kMaxSegmentPayloadBytes + kSegmentHeaderSize;

enum class FileReadResult {
    Ok,
    Missing,
    Error,
};

bool Fail(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
    return false;
}

bool IsVersionError(const std::string& error) {
    return error.find("不兼容") != std::string::npos ||
           error.find("incompatible") != std::string::npos;
}

bool ValidSaveId(std::string_view id) {
    if (id.empty() || id.size() > 128) {
        return false;
    }
    for (const char c : id) {
        const bool valid = (c >= 'a' && c <= 'z') ||
                           (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!valid) {
            return false;
        }
    }
    return true;
}

std::int64_t NowUnixSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string UniqueSuffix() {
    static std::atomic<std::uint64_t> counter{1};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(now) + "-" + std::to_string(counter.fetch_add(1));
}

std::string MakeSaveId(const std::filesystem::path& root) {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char base[32]{};
    std::strftime(base, sizeof(base), "%Y%m%d-%H%M%S", &utc);
    for (int suffix = 0; suffix < 1000; ++suffix) {
        std::string id = base;
        if (suffix > 0) {
            id += "-" + std::to_string(suffix);
        }
        std::error_code ec;
        if (!std::filesystem::exists(root / id, ec) && !ec) {
            return id;
        }
    }
    return {};
}

bool WriteFile(const std::filesystem::path& path,
               std::span<const std::uint8_t> bytes, std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return Fail(error, "创建文件目录失败: " + path.parent_path().string() +
                               "（" + ec.message() + "）");
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return Fail(error, "无法写入文件: " + path.string());
    }
    if (!bytes.empty()) {
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    file.flush();
    if (!file) {
        file.close();
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        return Fail(error, "写入文件失败: " + path.string());
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

FileReadResult ReadFile(const std::filesystem::path& path,
                        std::vector<std::uint8_t>* out, std::string* error) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (ec) {
            Fail(error, "检查文件失败: " + path.string());
            return FileReadResult::Error;
        }
        return FileReadResult::Missing;
    }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        Fail(error, "无法读取文件: " + path.string());
        return FileReadResult::Error;
    }
    const std::streamoff end = file.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) > kMaxSaveFileBytes) {
        Fail(error, "文件大小非法: " + path.string());
        return FileReadResult::Error;
    }
    out->resize(static_cast<std::size_t>(end));
    file.seekg(0, std::ios::beg);
    if (!out->empty()) {
        file.read(reinterpret_cast<char*>(out->data()),
                  static_cast<std::streamsize>(out->size()));
        if (!file || file.gcount() != static_cast<std::streamsize>(out->size())) {
            Fail(error, "读取文件失败: " + path.string());
            return FileReadResult::Error;
        }
    }
    return FileReadResult::Ok;
}

std::vector<std::uint8_t> EncodeHeader(const SaveHeader& header,
                                       std::string* error) {
    BinaryWriter writer;
    if (!writer.BeginSegment(kMagicSaveHeader, kSaveFormatVersion) ||
        !writer.WriteString(header.gameVersion) ||
        !writer.WriteString(header.mapId) ||
        !writer.Write(static_cast<std::int64_t>(header.createdAt)) ||
        !writer.EndSegment()) {
        Fail(error, writer.Error().empty() ? "存档头编码失败" : writer.Error());
        return {};
    }
    const auto bytes = writer.Data();
    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
}

SaveManager::LoadResult ReadHeaderFile(std::span<const std::uint8_t> bytes,
                                      SaveHeader* header,
                                      std::string* error) {
    if (header == nullptr) {
        Fail(error, "存档头输出为空");
        return SaveManager::LoadResult::Corrupt;
    }

    BinaryReader reader(bytes);
    SegmentHeader segment;
    if (!reader.BeginSegment(segment)) {
        Fail(error, reader.Error().empty() ? "存档头缺失或截断" : reader.Error());
        return SaveManager::LoadResult::Corrupt;
    }
    if (segment.magic != kMagicSaveHeader) {
        Fail(error, "存档头标识不匹配");
        return SaveManager::LoadResult::Corrupt;
    }
    if (segment.version != kSaveFormatVersion) {
        Fail(error, "存档版本不兼容: 读取到 " + std::to_string(segment.version) +
                        "，支持 " + std::to_string(kSaveFormatVersion));
        return SaveManager::LoadResult::Incompatible;
    }

    SaveHeader loaded;
    loaded.version = segment.version;
    if (!reader.ReadString(loaded.gameVersion, 256) ||
        !reader.ReadString(loaded.mapId, 256) ||
        !reader.Read(loaded.createdAt) || !reader.EndSegment()) {
        Fail(error, reader.Error().empty() ? "存档头数据损坏" : reader.Error());
        return SaveManager::LoadResult::Corrupt;
    }
    if (loaded.gameVersion.empty()) {
        Fail(error, "存档头缺少游戏版本");
        return SaveManager::LoadResult::Corrupt;
    }
    *header = std::move(loaded);
    return SaveManager::LoadResult::Ok;
}

bool ValidatePlayers(const std::vector<SavePlayer>& players,
                     std::string* error) {
    std::unordered_set<std::uint64_t> ids;
    for (const SavePlayer& player : players) {
        if (player.name.empty()) {
            Fail(error, "玩家名称不能为空");
            return false;
        }
        if (player.stableId >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            Fail(error, "玩家 StableId 超出范围");
            return false;
        }
        if (!ids.insert(player.stableId).second) {
            Fail(error, "玩家 StableId 重复");
            return false;
        }
    }
    return true;
}

bool CopyDirectoryContents(const std::filesystem::path& source,
                           const std::filesystem::path& target,
                           std::string* error) {
    std::error_code ec;
    std::filesystem::directory_iterator it(source, ec);
    if (ec) {
        return Fail(error, "枚举存档目录失败: " + source.string() + "（" +
                               ec.message() + "）");
    }
    for (const auto& entry : it) {
        const std::filesystem::path destination = target / entry.path().filename();
        std::filesystem::copy(entry.path(), destination,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing,
                              ec);
        if (ec) {
            return Fail(error, "复制存档文件失败: " + entry.path().string() + "（" +
                                   ec.message() + "）");
        }
    }
    return true;
}

}  // namespace

SaveManager::SaveManager(Options options) : m_options(std::move(options)) {}

bool SaveManager::EnsureRoot(std::string* error) const {
    if (m_options.root.empty()) {
        return Fail(error, "存档根目录为空");
    }
    std::error_code ec;
    std::filesystem::create_directories(m_options.root, ec);
    if (ec) {
        return Fail(error, "创建存档根目录失败: " + m_options.root.string() +
                               "（" + ec.message() + "）");
    }
    return true;
}

std::filesystem::path SaveManager::SavePath(std::string_view id) const {
    return m_options.root / std::string(id);
}

std::filesystem::path SaveManager::BackupPath(std::string_view id) const {
    return m_options.root / ".backup" / std::string(id);
}

std::filesystem::path SaveManager::WorldDir(std::string_view id) const {
    return SavePath(id) / "world";
}

std::filesystem::path SaveManager::MetaPath(std::string_view id) const {
    return SavePath(id) / kSaveMetaFile;
}

std::filesystem::path SaveManager::HeaderPath(std::string_view id) const {
    return SavePath(id) / kSaveHeaderFile;
}

std::filesystem::path SaveManager::EntitiesPath(std::string_view id) const {
    return SavePath(id) / kSaveEntitiesFile;
}

std::filesystem::path SaveManager::ThumbnailPath(std::string_view id) const {
    return SavePath(id) / kSaveThumbnailFile;
}

bool SaveManager::ExistsAt(const std::filesystem::path& dir,
                           std::string_view id) const {
    if (!ValidSaveId(id)) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(dir / kSaveMetaFile, ec) && !ec;
}

bool SaveManager::Exists(std::string_view id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return ExistsAt(SavePath(id), id);
}

bool SaveManager::CommitGeneration(std::string_view id, bool copyExisting,
                                   const GenerationWriter& writer,
                                   std::string* error) {
    if (!ValidSaveId(id)) {
        return Fail(error, "存档 id 非法");
    }
    if (!EnsureRoot(error)) {
        return false;
    }
    if (!writer) {
        return Fail(error, "存档 generation 写入器为空");
    }

    const std::filesystem::path savePath = SavePath(id);
    const std::filesystem::path backupPath = BackupPath(id);
    const std::filesystem::path tempPath =
        m_options.root / (".tmp-" + std::string(id) + "-" + UniqueSuffix());

    auto cleanupTemp = [&tempPath]() {
        std::error_code ignored;
        std::filesystem::remove_all(tempPath, ignored);
    };

    std::error_code ec;
    std::filesystem::create_directories(tempPath, ec);
    if (ec) {
        return Fail(error, "创建临时存档目录失败: " + tempPath.string() + "（" +
                               ec.message() + "）");
    }

    const bool pathExists = std::filesystem::exists(savePath, ec);
    if (ec) {
        cleanupTemp();
        return Fail(error, "检查存档目录失败: " + savePath.string() + "（" +
                               ec.message() + "）");
    }
    const bool hasExisting =
        pathExists && std::filesystem::is_directory(savePath, ec);
    if (ec) {
        cleanupTemp();
        return Fail(error, "检查存档目录类型失败: " + savePath.string() + "（" +
                               ec.message() + "）");
    }
    if (copyExisting && hasExisting) {
        if (!CopyDirectoryContents(savePath, tempPath, error)) {
            cleanupTemp();
            return false;
        }
    }

    if (!writer(tempPath, error)) {
        cleanupTemp();
        return false;
    }

    std::filesystem::remove_all(backupPath, ec);
    if (ec) {
        cleanupTemp();
        return Fail(error, "清理旧备份失败: " + backupPath.string() + "（" +
                               ec.message() + "）");
    }

    bool movedOld = false;
    if (hasExisting) {
        std::filesystem::create_directories(backupPath.parent_path(), ec);
        if (ec) {
            cleanupTemp();
            return Fail(error, "创建备份目录失败: " + backupPath.parent_path().string() +
                                   "（" + ec.message() + "）");
        }
        std::filesystem::rename(savePath, backupPath, ec);
        if (ec) {
            cleanupTemp();
            return Fail(error, "移动旧存档到备份失败: " + savePath.string() + "（" +
                                   ec.message() + "）");
        }
        movedOld = true;
    }

    std::filesystem::rename(tempPath, savePath, ec);
    if (ec) {
        const std::string commitError = ec.message();
        if (movedOld) {
            std::error_code restoreEc;
            std::filesystem::rename(backupPath, savePath, restoreEc);
            if (restoreEc) {
                cleanupTemp();
                return Fail(error, "提交存档失败且恢复旧存档失败: " + commitError +
                                       "；恢复错误: " + restoreEc.message() +
                                       "；备份保留在 " + backupPath.string());
            }
        }
        cleanupTemp();
        return Fail(error, "提交存档失败: " + commitError);
    }

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool SaveManager::WriteChunks(const std::filesystem::path& dir,
                              const std::vector<ChunkDiff>& chunks,
                              std::string* error) const {
    for (const ChunkDiff& diff : chunks) {
        if (!ValidateChunkDiff(diff, error)) {
            return false;
        }
        const std::filesystem::path path =
            dir / "world" / ("chunk_" + std::to_string(diff.chunkX) + "_" +
                             std::to_string(diff.chunkY) + ".bin");
        if (diff.squares.empty()) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
            if (ec) {
                return Fail(error, "移除空 chunk diff 失败: " + path.string() + "（" +
                                       ec.message() + "）");
            }
            continue;
        }
        const std::vector<std::uint8_t> bytes = EncodeChunkDiff(diff, error);
        if (bytes.empty() || !WriteFile(path, bytes, error)) {
            return false;
        }
        ZL_DEBUG("save", "写入 chunk diff: ({},{}) {} 格", diff.chunkX, diff.chunkY,
                 diff.squares.size());
    }
    return true;
}

bool SaveManager::WriteGeneration(
    const std::filesystem::path& dir, const SaveHeader& header,
    const SaveMetadata& metadata, const std::vector<std::uint8_t>& entityBytes,
    const ThumbnailImage* thumbnail, const std::vector<ChunkDiff>* chunks,
    std::string* error) const {
    std::error_code ec;
    std::filesystem::create_directories(dir / "world", ec);
    if (ec) {
        return Fail(error, "创建存档世界目录失败: " + (dir / "world").string() +
                               "（" + ec.message() + "）");
    }
    const std::vector<std::uint8_t> headerBytes = EncodeHeader(header, error);
    if (headerBytes.empty() || !WriteFile(dir / kSaveHeaderFile, headerBytes, error)) {
        return false;
    }
    if (!WriteFile(dir / kSaveEntitiesFile, entityBytes, error)) {
        return false;
    }
    if (thumbnail != nullptr && !WritePng(dir / kSaveThumbnailFile, *thumbnail, error)) {
        return false;
    }
    if (chunks != nullptr && !WriteChunks(dir, *chunks, error)) {
        return false;
    }

    SqliteMeta database;
    std::string sqliteError;
    if (!database.Open(dir / kSaveMetaFile, true, &sqliteError) ||
        !database.Initialize(&sqliteError) ||
        !database.Write(metadata, &sqliteError)) {
        return Fail(error, sqliteError);
    }
    return true;
}

bool SaveManager::Create(const CreateOptions& options, std::string* outId,
                         std::string* error) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (options.name.empty()) {
        return Fail(error, "存档名称不能为空");
    }
    if (!EnsureRoot(error)) {
        return false;
    }

    const bool hasThumbnail = options.thumbnail.width != 0 ||
                              options.thumbnail.height != 0 ||
                              !options.thumbnail.rgba.empty();
    if (hasThumbnail && !options.thumbnail.Valid()) {
        return Fail(error, "缩略图数据非法");
    }

    std::vector<SavePlayer> players = options.players;
    for (SavePlayer& player : players) {
        if (player.name.empty()) {
            player.name = "Player";
        }
    }
    if (!ValidatePlayers(players, error)) {
        return false;
    }

    const std::string gameVersion =
        options.gameVersion.empty() ? m_options.gameVersion : options.gameVersion;
    if (gameVersion.empty()) {
        return Fail(error, "游戏版本不能为空");
    }
    if (!std::isfinite(options.gameMinutes) || options.gameMinutes < 0.0) {
        return Fail(error, "存档游戏时间非法");
    }

    EntitySnapshot emptyEntities;
    const EntitySnapshot& sourceEntities =
        options.entities != nullptr ? *options.entities : emptyEntities;
    const std::vector<std::uint8_t> entityBytes =
        EncodeEntitySnapshot(sourceEntities, error);
    if (entityBytes.empty()) {
        return false;
    }

    const std::string id = MakeSaveId(m_options.root);
    if (id.empty()) {
        return Fail(error, "无法生成唯一存档 id");
    }

    const std::int64_t now = NowUnixSeconds();
    SaveHeader header;
    header.version = kSaveFormatVersion;
    header.gameVersion = gameVersion;
    header.mapId = options.mapId;
    header.createdAt = now;

    SaveMetadata metadata;
    metadata.id = id;
    metadata.name = options.name;
    metadata.createdAt = now;
    metadata.updatedAt = now;
    metadata.saveVersion = kSaveFormatVersion;
    metadata.gameVersion = gameVersion;
    metadata.mapId = options.mapId;
    metadata.thumbnail = hasThumbnail ? kSaveThumbnailFile : "";
    metadata.gameMinutes = options.gameMinutes;
    metadata.players = std::move(players);

    const ThumbnailImage* thumbnail = hasThumbnail ? &options.thumbnail : nullptr;
    if (!CommitGeneration(
            id, false,
            [&](const std::filesystem::path& dir, std::string* writeError) {
                return WriteGeneration(dir, header, metadata, entityBytes, thumbnail,
                                       &options.chunks, writeError);
            },
            error)) {
        return false;
    }

    if (outId != nullptr) {
        *outId = id;
    }
    return true;
}

bool SaveManager::Save(std::string_view id, const EntitySnapshot& entities,
                       const ThumbnailImage* thumbnail, std::string* error) {
    SaveOptions options;
    options.entities = &entities;
    options.thumbnail = thumbnail;
    return Save(id, options, error);
}

bool SaveManager::Save(std::string_view id, const SaveOptions& options,
                       std::string* error) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!ValidSaveId(id)) {
        return Fail(error, "存档 id 非法");
    }
    if (options.entities == nullptr) {
        return Fail(error, "实体快照为空");
    }
    SaveMetadata metadata;
    if (ReadMetadataAt(SavePath(id), id, &metadata, error) != LoadResult::Ok) {
        return false;
    }

    if (options.thumbnail != nullptr && !options.thumbnail->Valid()) {
        return Fail(error, "缩略图数据非法");
    }
    if (options.gameMinutes.has_value()) {
        if (!std::isfinite(*options.gameMinutes) || *options.gameMinutes < 0.0) {
            return Fail(error, "存档游戏时间非法");
        }
        metadata.gameMinutes = *options.gameMinutes;
    }
    const std::vector<std::uint8_t> entityBytes =
        EncodeEntitySnapshot(*options.entities, error);
    if (entityBytes.empty()) {
        return false;
    }

    SaveHeader header;
    header.version = kSaveFormatVersion;
    header.gameVersion = metadata.gameVersion;
    header.mapId = metadata.mapId;
    header.createdAt = metadata.createdAt;

    if (options.thumbnail != nullptr) {
        metadata.thumbnail = kSaveThumbnailFile;
    }
    metadata.updatedAt = NowUnixSeconds();

    return CommitGeneration(
        id, true,
        [&](const std::filesystem::path& dir, std::string* writeError) {
            return WriteGeneration(dir, header, metadata, entityBytes,
                                   options.thumbnail, options.chunks, writeError);
        },
        error);
}

SaveManager::LoadResult SaveManager::ReadMetadataAt(
    const std::filesystem::path& dir, std::string_view id,
    SaveMetadata* metadata, std::string* error) const {
    if (metadata == nullptr) {
        Fail(error, "存档元数据输出为空");
        return LoadResult::Corrupt;
    }
    if (!ValidSaveId(id)) {
        Fail(error, "存档 id 非法");
        return LoadResult::NotFound;
    }
    const std::filesystem::path path = dir / kSaveMetaFile;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (ec) {
            Fail(error, "检查存档元数据失败: " + ec.message());
            return LoadResult::Corrupt;
        }
        Fail(error, "存档不存在: " + std::string(id));
        return LoadResult::NotFound;
    }

    SqliteMeta database;
    std::string openError;
    if (!database.Open(path, false, &openError)) {
        Fail(error, openError);
        return LoadResult::Corrupt;
    }
    if (!database.Initialize(&openError)) {
        Fail(error, openError);
        return LoadResult::Corrupt;
    }
    SaveMetadata loaded;
    if (!database.Read(&loaded, &openError)) {
        Fail(error, openError);
        return IsVersionError(openError) ? LoadResult::Incompatible
                                         : LoadResult::Corrupt;
    }
    if (loaded.id != id) {
        Fail(error, "存档元数据 id 与目录不一致");
        return LoadResult::Corrupt;
    }
    if (loaded.saveVersion == 0 || loaded.gameVersion.empty() ||
        loaded.createdAt <= 0 || loaded.updatedAt <= 0) {
        Fail(error, "存档元数据字段非法");
        return LoadResult::Corrupt;
    }
    if (loaded.saveVersion != kSaveFormatVersion) {
        Fail(error, "存档版本不兼容: 读取到 " +
                        std::to_string(loaded.saveVersion) + "，支持 " +
                        std::to_string(kSaveFormatVersion));
        return LoadResult::Incompatible;
    }
    *metadata = std::move(loaded);
    return LoadResult::Ok;
}

SaveManager::LoadResult SaveManager::ReadHeaderAt(
    const std::filesystem::path& dir, std::string_view id,
    SaveHeader* header, std::string* error) const {
    if (header == nullptr) {
        Fail(error, "存档头输出为空");
        return LoadResult::Corrupt;
    }
    if (!ValidSaveId(id)) {
        Fail(error, "存档 id 非法");
        return LoadResult::NotFound;
    }
    std::vector<std::uint8_t> bytes;
    const FileReadResult read = ReadFile(dir / kSaveHeaderFile, &bytes, error);
    if (read == FileReadResult::Missing) {
        Fail(error, "存档头不存在: " + std::string(id));
        return LoadResult::NotFound;
    }
    if (read == FileReadResult::Error) {
        return LoadResult::Corrupt;
    }
    return ReadHeaderFile(bytes, header, error);
}

SaveManager::LoadResult SaveManager::ReadMetadata(
    std::string_view id, SaveMetadata* metadata, std::string* error) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return ReadMetadataAt(SavePath(id), id, metadata, error);
}

SaveManager::LoadResult SaveManager::ReadHeader(
    std::string_view id, SaveHeader* header, std::string* error) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return ReadHeaderAt(SavePath(id), id, header, error);
}

bool SaveManager::List(std::vector<SaveMetadata>* out, std::string* error) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (out == nullptr) {
        return Fail(error, "存档列表输出为空");
    }
    out->clear();
    std::error_code ec;
    if (!std::filesystem::exists(m_options.root, ec)) {
        if (ec) {
            return Fail(error, "检查存档根目录失败: " + ec.message());
        }
        return true;
    }
    std::filesystem::directory_iterator it(m_options.root, ec);
    if (ec) {
        return Fail(error, "枚举存档目录失败: " + ec.message());
    }

    std::unordered_set<std::string> seen;
    auto addMetadata = [&](const SaveMetadata& metadata) {
        if (!seen.insert(metadata.id).second) {
            return;
        }
        out->push_back(metadata);
    };

    for (const auto& entry : it) {
        std::error_code typeError;
        if (!entry.is_directory(typeError) || typeError) {
            continue;
        }
        const std::string id = entry.path().filename().string();
        if (!ValidSaveId(id)) {
            continue;
        }
        SaveMetadata metadata;
        std::string readError;
        const LoadResult primaryResult =
            ReadMetadataAt(entry.path(), id, &metadata, &readError);
        if (primaryResult == LoadResult::Ok) {
            addMetadata(metadata);
            continue;
        }
        if (primaryResult == LoadResult::Incompatible) {
            ZL_WARN("save", "跳过不兼容存档 '{}'：{}", id, readError);
            continue;
        }

        const std::filesystem::path backup = BackupPath(id);
        SaveMetadata backupMetadata;
        std::string backupError;
        if (std::filesystem::is_directory(backup, typeError) &&
            ReadMetadataAt(backup, id, &backupMetadata, &backupError) ==
                LoadResult::Ok) {
            ZL_WARN("save", "存档 '{}' 主目录损坏，列表使用备份：{}", id, readError);
            addMetadata(backupMetadata);
        } else {
            ZL_WARN("save", "跳过无效存档 '{}'：{}", id, readError);
        }
    }

    const std::filesystem::path backupRoot = m_options.root / ".backup";
    if (std::filesystem::is_directory(backupRoot, ec)) {
        std::filesystem::directory_iterator backups(backupRoot, ec);
        if (!ec) {
            for (const auto& entry : backups) {
                std::error_code typeError;
                if (!entry.is_directory(typeError) || typeError) {
                    continue;
                }
                const std::string id = entry.path().filename().string();
                if (!ValidSaveId(id) || seen.contains(id)) {
                    continue;
                }
                if (std::filesystem::exists(SavePath(id), typeError)) {
                    continue;
                }
                SaveMetadata metadata;
                std::string readError;
                if (ReadMetadataAt(entry.path(), id, &metadata, &readError) ==
                    LoadResult::Ok) {
                    ZL_WARN("save", "存档 '{}' 主目录缺失，列表使用备份", id);
                    addMetadata(metadata);
                }
            }
        }
    }

    std::sort(out->begin(), out->end(),
              [](const SaveMetadata& left, const SaveMetadata& right) {
                  if (left.updatedAt != right.updatedAt) {
                      return left.updatedAt > right.updatedAt;
                  }
                  return left.id > right.id;
              });
    return true;
}

SaveManager::LoadResult SaveManager::LoadAt(
    const std::filesystem::path& dir, std::string_view id,
    const ComponentRegistry& registry, EntitySnapshot* entities,
    SaveMetadata* metadata, std::string* error) const {
    (void)registry;
    SaveMetadata loadedMetadata;
    const LoadResult metaResult =
        ReadMetadataAt(dir, id, &loadedMetadata, error);
    if (metaResult != LoadResult::Ok) {
        return metaResult;
    }

    SaveHeader header;
    const LoadResult headerResult =
        ReadHeaderAt(dir, id, &header, error);
    if (headerResult != LoadResult::Ok) {
        return headerResult;
    }
    if (loadedMetadata.saveVersion != header.version ||
        loadedMetadata.gameVersion != header.gameVersion ||
        loadedMetadata.mapId != header.mapId ||
        loadedMetadata.createdAt != header.createdAt) {
        Fail(error, "存档头与元数据不一致");
        return LoadResult::Corrupt;
    }

    std::vector<std::uint8_t> entityBytes;
    const FileReadResult read =
        ReadFile(dir / kSaveEntitiesFile, &entityBytes, error);
    if (read == FileReadResult::Missing) {
        Fail(error, "实体存档不存在: " + std::string(id));
        return LoadResult::Corrupt;
    }
    if (read == FileReadResult::Error) {
        return LoadResult::Corrupt;
    }

    EntitySnapshot loadedEntities;
    std::string decodeError;
    if (!DecodeEntitySnapshot(entityBytes, &loadedEntities, &decodeError)) {
        Fail(error, decodeError);
        return IsVersionError(decodeError) ? LoadResult::Incompatible
                                           : LoadResult::Corrupt;
    }

    *entities = std::move(loadedEntities);
    if (metadata != nullptr) {
        *metadata = std::move(loadedMetadata);
    }
    return LoadResult::Ok;
}

SaveManager::LoadResult SaveManager::Load(
    std::string_view id, const ComponentRegistry& registry,
    EntitySnapshot* entities, SaveMetadata* metadata, std::string* error,
    LoadReport* report) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (entities == nullptr) {
        Fail(error, "实体快照输出为空");
        return LoadResult::Corrupt;
    }
    if (!ValidSaveId(id)) {
        Fail(error, "存档 id 非法");
        return LoadResult::NotFound;
    }

    const std::filesystem::path primary = SavePath(id);
    const LoadResult primaryResult =
        LoadAt(primary, id, registry, entities, metadata, error);
    if (primaryResult == LoadResult::Ok) {
        return LoadResult::Ok;
    }
    if (primaryResult != LoadResult::Corrupt &&
        primaryResult != LoadResult::NotFound) {
        return primaryResult;
    }

    const std::filesystem::path backup = BackupPath(id);
    std::error_code ec;
    if (!std::filesystem::is_directory(backup, ec) || ec) {
        return primaryResult;
    }

    const std::string primaryError =
        error != nullptr ? *error : std::string("主存档不可读");
    EntitySnapshot backupEntities;
    SaveMetadata backupMetadata;
    std::string backupError;
    const LoadResult backupResult = LoadAt(
        backup, id, registry, &backupEntities, &backupMetadata, &backupError);
    if (backupResult != LoadResult::Ok) {
        Fail(error, primaryError + "；备份恢复失败: " + backupError +
                       "；建议删除该存档或使用备份目录");
        return primaryResult;
    }

    ZL_WARN("save", "存档 '{}' 损坏，已回退备份：{}", id, primaryError);
    *entities = std::move(backupEntities);
    if (metadata != nullptr) {
        *metadata = std::move(backupMetadata);
    }
    if (report != nullptr) {
        report->usedBackup = true;
        report->warning = "主存档损坏，已回退备份：" + primaryError;
    }
    if (error != nullptr) {
        error->clear();
    }
    return LoadResult::Ok;
}

bool SaveManager::Delete(std::string_view id, std::string* error) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!ValidSaveId(id)) {
        return Fail(error, "存档 id 非法");
    }
    const std::filesystem::path path = SavePath(id);
    const std::filesystem::path backup = BackupPath(id);
    std::error_code ec;
    const bool savePathExists = std::filesystem::exists(path, ec);
    if (ec) {
        return Fail(error, "检查存档目录失败: " + path.string() + "（" +
                               ec.message() + "）");
    }
    const bool hasSave = savePathExists && std::filesystem::is_directory(path, ec);
    if (ec) {
        return Fail(error, "检查存档目录类型失败: " + path.string() + "（" +
                               ec.message() + "）");
    }
    const bool backupPathExists = std::filesystem::exists(backup, ec);
    if (ec) {
        return Fail(error, "检查存档备份失败: " + backup.string() + "（" +
                               ec.message() + "）");
    }
    const bool hasBackup =
        backupPathExists && std::filesystem::is_directory(backup, ec);
    if (ec) {
        return Fail(error, "检查存档备份类型失败: " + backup.string() + "（" +
                               ec.message() + "）");
    }
    if (!hasSave && !hasBackup) {
        return Fail(error, "存档不存在: " + std::string(id));
    }
    std::filesystem::remove_all(path, ec);
    if (ec) {
        return Fail(error, "删除存档失败: " + std::string(id) + "（" +
                               ec.message() + "）");
    }
    std::filesystem::remove_all(backup, ec);
    if (ec) {
        return Fail(error, "删除存档备份失败: " + backup.string() + "（" +
                               ec.message() + "）");
    }
    return true;
}

bool SaveManager::SaveChunk(std::string_view id, const world::Chunk& chunk,
                            std::string* error) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!ValidSaveId(id)) {
        return Fail(error, "存档 id 非法");
    }
    if (!ExistsAt(SavePath(id), id)) {
        return Fail(error, "存档不存在: " + std::string(id));
    }
    ChunkDiffStore store(SavePath(id));
    return store.SaveChunk(chunk, error);
}

SaveManager::LoadResult SaveManager::LoadChunkAt(
    const std::filesystem::path& dir, std::string_view id,
    world::Chunk& chunk, std::string* error) const {
    (void)id;
    ChunkDiffStore store(dir);
    const ChunkDiffStore::LoadResult result =
        store.LoadAndApply(chunk.ChunkX(), chunk.ChunkY(), chunk, error);
    if (result == ChunkDiffStore::LoadResult::Loaded) {
        return LoadResult::Ok;
    }
    if (result == ChunkDiffStore::LoadResult::NotFound) {
        return LoadResult::NotFound;
    }
    const std::string currentError = error != nullptr ? *error : std::string();
    return IsVersionError(currentError) ? LoadResult::Incompatible
                                        : LoadResult::Corrupt;
}

SaveManager::LoadResult SaveManager::LoadChunk(
    std::string_view id, world::Chunk& chunk, std::string* error,
    LoadReport* report) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!ValidSaveId(id)) {
        Fail(error, "存档 id 非法");
        return LoadResult::NotFound;
    }

    const std::filesystem::path primary = SavePath(id);
    const LoadResult primaryResult = LoadChunkAt(primary, id, chunk, error);
    if (primaryResult == LoadResult::Ok ||
        primaryResult == LoadResult::NotFound) {
        return primaryResult;
    }
    if (primaryResult == LoadResult::Incompatible) {
        return primaryResult;
    }

    const std::filesystem::path backup = BackupPath(id);
    std::error_code ec;
    if (!std::filesystem::is_directory(backup, ec) || ec) {
        return primaryResult;
    }

    const std::string primaryError =
        error != nullptr ? *error : std::string("chunk 存档损坏");
    std::string backupError;
    const LoadResult backupResult =
        LoadChunkAt(backup, id, chunk, &backupError);
    if (backupResult != LoadResult::Ok) {
        Fail(error, primaryError + "；chunk 备份恢复失败: " + backupError +
                       "；建议删除该存档或使用备份目录");
        return primaryResult;
    }

    ZL_WARN("save", "chunk ({},{}) 存档损坏，已回退备份：{}", chunk.ChunkX(),
            chunk.ChunkY(), primaryError);
    if (report != nullptr) {
        report->usedBackup = true;
        report->warning = "chunk (" + std::to_string(chunk.ChunkX()) + "," +
                          std::to_string(chunk.ChunkY()) +
                          ") 损坏，已回退备份：" + primaryError;
    }
    if (error != nullptr) {
        error->clear();
    }
    return LoadResult::Ok;
}

SaveManager::LoadResult SaveManager::LoadChunkFromBackup(
    std::string_view id, world::Chunk& chunk, std::string* error) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!ValidSaveId(id)) {
        Fail(error, "存档 id 非法");
        return LoadResult::NotFound;
    }
    return LoadChunkAt(BackupPath(id), id, chunk, error);
}

}  // namespace zl::save
