#include "save/ChunkDiffStore.h"

#include <cstdint>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

#include "save/AtomicFile.h"
#include "save/BinaryFormat.h"

namespace zl::save {
namespace {

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool Fail(std::string* error, std::string message) {
    SetError(error, std::move(message));
    return false;
}

}  // namespace

ChunkDiffStore::ChunkDiffStore(std::filesystem::path saveRoot)
    : m_saveRoot(std::move(saveRoot)) {}

std::filesystem::path ChunkDiffStore::WorldDir() const {
    return m_saveRoot / "world";
}

std::filesystem::path ChunkDiffStore::ChunkPath(int chunkX, int chunkY) const {
    return WorldDir() /
           ("chunk_" + std::to_string(chunkX) + "_" + std::to_string(chunkY) + ".bin");
}

bool ChunkDiffStore::Exists(int chunkX, int chunkY) const {
    std::error_code ec;
    const bool exists = std::filesystem::exists(ChunkPath(chunkX, chunkY), ec);
    return !ec && exists;
}

bool ChunkDiffStore::SaveChunk(const world::Chunk& chunk, std::string* error) const {
    ChunkDiff diff;
    if (!CaptureChunkDiff(chunk, &diff, error)) {
        return false;
    }
    return Save(diff, error);
}

bool ChunkDiffStore::Save(const ChunkDiff& diff, std::string* error) const {
    if (m_saveRoot.empty()) {
        return Fail(error, "存档目录为空");
    }
    if (diff.squares.empty()) {
        // 世界改动已全部还原：不再保留旧 diff 文件。
        return Remove(diff.chunkX, diff.chunkY, error);
    }

    std::vector<std::uint8_t> bytes = EncodeChunkDiff(diff, error);
    if (bytes.empty()) {
        return false;
    }

    const std::filesystem::path path = ChunkPath(diff.chunkX, diff.chunkY);
    return WriteFileAtomic(path, bytes, error);
}

ChunkDiffStore::LoadResult ChunkDiffStore::Load(int chunkX, int chunkY, ChunkDiff* out,
                                                std::string* error) const {
    if (out == nullptr) {
        Fail(error, "ChunkDiffStore::Load: 输出为空");
        return LoadResult::Error;
    }
    if (m_saveRoot.empty()) {
        Fail(error, "存档目录为空");
        return LoadResult::Error;
    }

    const std::filesystem::path path = ChunkPath(chunkX, chunkY);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (ec) {
            Fail(error, "检查 chunk diff 失败: " + path.string() + "（" + ec.message() + "）");
            return LoadResult::Error;
        }
        SetError(error, {});
        return LoadResult::NotFound;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        Fail(error, "无法打开 chunk diff: " + path.string());
        return LoadResult::Error;
    }
    const std::streamoff end = file.tellg();
    if (end < 0) {
        Fail(error, "无法获取 chunk diff 大小: " + path.string());
        return LoadResult::Error;
    }
    const auto size = static_cast<std::uint64_t>(end);
    if (size > kMaxSegmentPayloadBytes + kSegmentHeaderSize) {
        Fail(error, "chunk diff 文件过大: " + path.string());
        return LoadResult::Error;
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        file.read(reinterpret_cast<char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!file || file.gcount() != static_cast<std::streamsize>(bytes.size())) {
            Fail(error, "读取 chunk diff 失败: " + path.string());
            return LoadResult::Error;
        }
    }

    ChunkDiff diff;
    if (!DecodeChunkDiff(bytes, &diff, error)) {
        return LoadResult::Error;
    }
    if (diff.chunkX != chunkX || diff.chunkY != chunkY) {
        Fail(error, "chunk diff 坐标与文件名不匹配: " + path.string());
        return LoadResult::Error;
    }
    *out = std::move(diff);
    SetError(error, {});
    return LoadResult::Loaded;
}

ChunkDiffStore::LoadResult ChunkDiffStore::LoadAndApply(int chunkX, int chunkY,
                                                        world::Chunk& chunk,
                                                        std::string* error) const {
    ChunkDiff diff;
    const LoadResult result = Load(chunkX, chunkY, &diff, error);
    if (result != LoadResult::Loaded) {
        return result;
    }
    if (!ApplyChunkDiff(diff, chunk, error)) {
        return LoadResult::Error;
    }
    return LoadResult::Loaded;
}

bool ChunkDiffStore::Remove(int chunkX, int chunkY, std::string* error) const {
    if (m_saveRoot.empty()) {
        return Fail(error, "存档目录为空");
    }
    const std::filesystem::path path = ChunkPath(chunkX, chunkY);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec) {
        return Fail(error, "删除 chunk diff 失败: " + path.string() + "（" + ec.message() + "）");
    }
    SetError(error, {});
    return true;
}

}  // namespace zl::save
