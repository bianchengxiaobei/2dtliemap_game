#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "save/ChunkDiff.h"
#include "save/RegionEntityBuckets.h"
#include "save/SaveTypes.h"
#include "save/WorldManifest.h"

namespace zl::world {
class Chunk;
}

namespace zl::save {

class ComponentRegistry;
struct EntitySnapshot;

class SaveManager {
public:
    enum class LoadResult {
        Ok,
        NotFound,
        Incompatible,
        Corrupt,
    };

    struct LoadReport {
        bool usedBackup = false;
        bool usedWorldManifest = false;
        std::size_t entityBuckets = 0;
        std::string warning;
    };

    // M31-T9: coarse on-disk accounting for save-size/file-count budgets.
    // MeasureStorage must be called while no background save is in flight.
    struct StorageStats {
        bool valid = false;
        std::size_t files = 0;
        std::size_t chunkFiles = 0;
        std::size_t bucketFiles = 0;
        std::uint64_t totalBytes = 0;
        std::uint64_t chunkBytes = 0;
        std::uint64_t bucketBytes = 0;
        std::uint64_t manifestBytes = 0;
        std::uint64_t entityBytes = 0;
    };

    struct Options {
        std::filesystem::path root;
        std::string gameVersion = kDefaultGameVersion;
    };

    struct CreateOptions {
        std::string name;
        std::string mapId;
        std::string gameVersion;
        double gameMinutes = 0.0;
        std::vector<SavePlayer> players;
        ThumbnailImage thumbnail;
        const EntitySnapshot* entities = nullptr;
        const WorldManifest* manifest = nullptr;
        const std::vector<EntityBucket>* buckets = nullptr;
        std::vector<ChunkDiff> chunks;
    };

    struct SaveOptions {
        const EntitySnapshot* entities = nullptr;
        const ThumbnailImage* thumbnail = nullptr;
        const WorldManifest* manifest = nullptr;
        const std::vector<EntityBucket>* buckets = nullptr;
        const std::vector<ChunkDiff>* chunks = nullptr;
        std::optional<double> gameMinutes;
    };

    explicit SaveManager(Options options);

    const Options& GetOptions() const { return m_options; }
    const std::filesystem::path& Root() const { return m_options.root; }

    bool EnsureRoot(std::string* error = nullptr) const;
    std::filesystem::path SavePath(std::string_view id) const;
    std::filesystem::path BackupPath(std::string_view id) const;
    std::filesystem::path WorldDir(std::string_view id) const;
    std::filesystem::path MetaPath(std::string_view id) const;
    std::filesystem::path HeaderPath(std::string_view id) const;
    std::filesystem::path EntitiesPath(std::string_view id) const;
    std::filesystem::path ThumbnailPath(std::string_view id) const;
    std::filesystem::path WorldManifestPath(std::string_view id) const;
    bool Exists(std::string_view id) const;
    StorageStats MeasureStorage(std::string_view id,
                               std::string* error = nullptr) const;

    bool List(std::vector<SaveMetadata>* out,
              std::string* error = nullptr) const;

    LoadResult ReadMetadata(std::string_view id, SaveMetadata* metadata,
                            std::string* error = nullptr) const;
    LoadResult ReadHeader(std::string_view id, SaveHeader* header,
                          std::string* error = nullptr) const;
    LoadResult ReadWorldManifest(std::string_view id, WorldManifest* manifest,
                                 std::string* error = nullptr,
                                 LoadReport* report = nullptr) const;

    bool Create(const CreateOptions& options, std::string* outId = nullptr,
                std::string* error = nullptr);
    bool Save(std::string_view id, const EntitySnapshot& entities,
              const ThumbnailImage* thumbnail = nullptr,
              std::string* error = nullptr);
    bool Save(std::string_view id, const SaveOptions& options,
              std::string* error = nullptr);
    LoadResult Load(std::string_view id, const ComponentRegistry& registry,
                    EntitySnapshot* entities, SaveMetadata* metadata = nullptr,
                    std::string* error = nullptr,
                    LoadReport* report = nullptr) const;
    bool Delete(std::string_view id, std::string* error = nullptr);

    bool SaveChunk(std::string_view id, const world::Chunk& chunk,
                   std::string* error = nullptr) const;
    bool SaveChunkDiff(std::string_view id, const ChunkDiff& diff,
                       std::string* error = nullptr) const;
    LoadResult LoadChunk(std::string_view id, world::Chunk& chunk,
                         std::string* error = nullptr,
                         LoadReport* report = nullptr) const;
    LoadResult LoadChunkFromBackup(std::string_view id, world::Chunk& chunk,
                                   std::string* error = nullptr) const;

private:
    using GenerationWriter =
        std::function<bool(const std::filesystem::path& dir, std::string* error)>;

    bool CommitGeneration(std::string_view id, bool copyExisting,
                          const GenerationWriter& writer, std::string* error);
    bool WriteGeneration(const std::filesystem::path& dir, const SaveHeader& header,
                         const SaveMetadata& metadata,
                         const std::vector<std::uint8_t>& entityBytes,
                         const ThumbnailImage* thumbnail,
                         const WorldManifest* manifest,
                         const std::vector<EntityBucket>* buckets,
                         const std::vector<ChunkDiff>* chunks,
                         std::string* error) const;
    bool WriteChunks(const std::filesystem::path& dir,
                     const std::vector<ChunkDiff>& chunks,
                     std::string* error) const;

    LoadResult ReadMetadataAt(const std::filesystem::path& dir, std::string_view id,
                              SaveMetadata* metadata, std::string* error) const;
    LoadResult ReadHeaderAt(const std::filesystem::path& dir, std::string_view id,
                            SaveHeader* header, std::string* error) const;
    LoadResult ReadWorldManifestAt(const std::filesystem::path& dir,
                                   WorldManifest* manifest,
                                   std::string* error) const;
    LoadResult LoadAt(const std::filesystem::path& dir, std::string_view id,
                      const ComponentRegistry& registry, EntitySnapshot* entities,
                      SaveMetadata* metadata, std::string* error,
                      LoadReport* report) const;
    LoadResult LoadChunkAt(const std::filesystem::path& dir, std::string_view id,
                           world::Chunk& chunk, std::string* error) const;
    bool ExistsAt(const std::filesystem::path& dir, std::string_view id) const;

    Options m_options;
    mutable std::mutex m_mutex;
};

}  // namespace zl::save
