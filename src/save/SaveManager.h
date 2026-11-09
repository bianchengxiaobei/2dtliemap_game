#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "save/ChunkDiff.h"
#include "save/SaveTypes.h"

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
        std::string warning;
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
        std::vector<ChunkDiff> chunks;
    };

    struct SaveOptions {
        const EntitySnapshot* entities = nullptr;
        const ThumbnailImage* thumbnail = nullptr;
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
    bool Exists(std::string_view id) const;

    bool List(std::vector<SaveMetadata>* out,
              std::string* error = nullptr) const;

    LoadResult ReadMetadata(std::string_view id, SaveMetadata* metadata,
                            std::string* error = nullptr) const;
    LoadResult ReadHeader(std::string_view id, SaveHeader* header,
                          std::string* error = nullptr) const;

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
                         const std::vector<ChunkDiff>* chunks,
                         std::string* error) const;
    bool WriteChunks(const std::filesystem::path& dir,
                     const std::vector<ChunkDiff>& chunks,
                     std::string* error) const;

    LoadResult ReadMetadataAt(const std::filesystem::path& dir, std::string_view id,
                              SaveMetadata* metadata, std::string* error) const;
    LoadResult ReadHeaderAt(const std::filesystem::path& dir, std::string_view id,
                            SaveHeader* header, std::string* error) const;
    LoadResult LoadAt(const std::filesystem::path& dir, std::string_view id,
                      const ComponentRegistry& registry, EntitySnapshot* entities,
                      SaveMetadata* metadata, std::string* error) const;
    LoadResult LoadChunkAt(const std::filesystem::path& dir, std::string_view id,
                           world::Chunk& chunk, std::string* error) const;
    bool ExistsAt(const std::filesystem::path& dir, std::string_view id) const;

    Options m_options;
    mutable std::mutex m_mutex;
};

}  // namespace zl::save
