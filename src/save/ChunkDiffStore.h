#pragma once

#include <filesystem>
#include <string>

#include "save/ChunkDiff.h"

namespace zl::save {

// M10-T2：把 ChunkDiff 落到 <saveRoot>/world/chunk_<x>_<y>.bin。
// saveRoot 是某个存档目录（T4 的 SaveManager 负责选择/创建）；本类不创建整档结构。
class ChunkDiffStore {
public:
    enum class LoadResult {
        NotFound,
        Loaded,
        Error,
    };

    explicit ChunkDiffStore(std::filesystem::path saveRoot);

    const std::filesystem::path& SaveRoot() const { return m_saveRoot; }
    std::filesystem::path ChunkPath(int chunkX, int chunkY) const;
    std::filesystem::path WorldDir() const;

    bool Exists(int chunkX, int chunkY) const;

    // 当前 chunk 相对基线的 diff；没有改动时不写文件（若旧文件存在则删除）。
    bool SaveChunk(const world::Chunk& chunk, std::string* error = nullptr) const;
    bool Save(const ChunkDiff& diff, std::string* error = nullptr) const;

    LoadResult Load(int chunkX, int chunkY, ChunkDiff* out,
                    std::string* error = nullptr) const;
    LoadResult LoadAndApply(int chunkX, int chunkY, world::Chunk& chunk,
                            std::string* error = nullptr) const;
    bool Remove(int chunkX, int chunkY, std::string* error = nullptr) const;

private:
    std::filesystem::path m_saveRoot;
};

}  // namespace zl::save
