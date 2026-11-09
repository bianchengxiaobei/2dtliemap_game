#include "save/ChunkDiff.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <utility>

#include "save/BinaryReader.h"
#include "save/BinaryWriter.h"

namespace zl::save {
namespace {

constexpr int kPersistedLayers = static_cast<int>(world::SquareLayer::Mark);

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool Fail(std::string* error, std::string message) {
    SetError(error, std::move(message));
    return false;
}

bool ValidLocalCoords(int lx, int ly, int z) {
    return lx >= 0 && lx < core::kChunkSize && ly >= 0 &&
           ly < core::kChunkSize && z >= 0 && z < core::kMaxZ;
}

bool ValidateSquare(const ChunkSquareDiff& square, std::size_t* totalObjects,
                    std::string* error) {
    if (!ValidLocalCoords(square.lx, square.ly, square.z)) {
        return Fail(error, "chunk diff 格坐标越界: (" + std::to_string(square.lx) + "," +
                               std::to_string(square.ly) + "," +
                               std::to_string(square.z) + ")");
    }
    for (int layer = 0; layer < world::kSquareLayerCount; ++layer) {
        const auto& objects = square.layers[static_cast<std::size_t>(layer)];
        if (layer >= kPersistedLayers && !objects.empty()) {
            return Fail(error, "chunk diff 不能包含 Mark 临时层对象");
        }
        if (objects.size() > kMaxContainerElements) {
            return Fail(error, "chunk diff 单层对象数量超过上限");
        }
        *totalObjects += objects.size();
        if (*totalObjects > kMaxContainerElements) {
            return Fail(error, "chunk diff 对象总数超过上限");
        }
        for (const world::SquareObject& object : objects) {
            if (object.sprite.size() > kMaxStringBytes) {
                return Fail(error, "chunk diff 对象 sprite 名称过长");
            }
            if (!std::isfinite(object.hp)) {
                return Fail(error, "chunk diff 对象耐久不是有限数值");
            }
            if (object.underlay.size() > kMaxStringBytes) {
                return Fail(error, "chunk diff 地板底层 sprite 名称过长");
            }
        }
    }
    return true;
}

}  // namespace

bool ValidateChunkDiff(const ChunkDiff& diff, std::string* error) {
    if (diff.squares.size() > static_cast<std::size_t>(world::Chunk::kSquareCount)) {
        return Fail(error, "chunk diff 格数量超过上限");
    }

    std::array<bool, static_cast<std::size_t>(world::Chunk::kSquareCount)> seen{};
    std::size_t totalObjects = 0;
    for (const ChunkSquareDiff& square : diff.squares) {
        if (!ValidateSquare(square, &totalObjects, error)) {
            return false;
        }
        const int index = world::Chunk::Index(square.lx, square.ly, square.z);
        if (seen[static_cast<std::size_t>(index)]) {
            return Fail(error, "chunk diff 包含重复格");
        }
        seen[static_cast<std::size_t>(index)] = true;
    }
    SetError(error, {});
    return true;
}

std::vector<ChunkDiff> SplitChunkDiffBySquare(const ChunkDiff& diff) {
    std::vector<ChunkDiff> result;
    result.reserve(diff.squares.size());
    for (const ChunkSquareDiff& square : diff.squares) {
        ChunkDiff one;
        one.chunkX = diff.chunkX;
        one.chunkY = diff.chunkY;
        one.squares.push_back(square);
        result.push_back(std::move(one));
    }
    return result;
}

bool CaptureChunkSquareDiff(const world::Chunk& chunk, int lx, int ly, int z,
                            ChunkDiff* out, std::string* error) {
    if (out == nullptr) {
        return Fail(error, "CaptureChunkSquareDiff: 输出为空");
    }
    if (!ValidLocalCoords(lx, ly, z)) {
        return Fail(error, "CaptureChunkSquareDiff: 格坐标越界");
    }

    ChunkDiff result;
    result.chunkX = chunk.ChunkX();
    result.chunkY = chunk.ChunkY();
    const world::GridSquare& source = chunk.Square(lx, ly, z);
    ChunkSquareDiff square;
    square.lx = static_cast<std::int16_t>(lx);
    square.ly = static_cast<std::int16_t>(ly);
    square.z = static_cast<std::int8_t>(z);
    square.roomId = static_cast<std::int16_t>(source.RoomId());
    for (int layer = 0; layer < kPersistedLayers; ++layer) {
        square.layers[static_cast<std::size_t>(layer)] =
            source.Layer(static_cast<world::SquareLayer>(layer));
    }
    result.squares.push_back(std::move(square));
    if (!ValidateChunkDiff(result, error)) {
        return false;
    }
    *out = std::move(result);
    SetError(error, {});
    return true;
}

bool CaptureChunkDiff(const world::Chunk& chunk, ChunkDiff* out,
                      std::string* error) {
    if (out == nullptr) {
        return Fail(error, "CaptureChunkDiff: 输出为空");
    }
    if (!chunk.HasBaseline()) {
        return Fail(error, "CaptureChunkDiff: chunk 未捕获母地图基线");
    }

    ChunkDiff result;
    result.chunkX = chunk.ChunkX();
    result.chunkY = chunk.ChunkY();
    for (int z = 0; z < core::kMaxZ; ++z) {
        for (int ly = 0; ly < core::kChunkSize; ++ly) {
            for (int lx = 0; lx < core::kChunkSize; ++lx) {
                if (!chunk.IsSquareModified(lx, ly, z)) {
                    continue;
                }
                const world::GridSquare& source = chunk.Square(lx, ly, z);
                ChunkSquareDiff square;
                square.lx = static_cast<std::int16_t>(lx);
                square.ly = static_cast<std::int16_t>(ly);
                square.z = static_cast<std::int8_t>(z);
                square.roomId = static_cast<std::int16_t>(source.RoomId());
                for (int layer = 0; layer < kPersistedLayers; ++layer) {
                    square.layers[static_cast<std::size_t>(layer)] =
                        source.Layer(static_cast<world::SquareLayer>(layer));
                }
                result.squares.push_back(std::move(square));
            }
        }
    }

    *out = std::move(result);
    SetError(error, {});
    return true;
}

bool ApplyChunkDiff(const ChunkDiff& diff, world::Chunk& chunk, std::string* error) {
    if (!ValidateChunkDiff(diff, error)) {
        return false;
    }
    if (diff.chunkX != chunk.ChunkX() || diff.chunkY != chunk.ChunkY()) {
        return Fail(error, "chunk diff 坐标不匹配: diff(" + std::to_string(diff.chunkX) + "," +
                               std::to_string(diff.chunkY) + ") chunk(" +
                               std::to_string(chunk.ChunkX()) + "," +
                               std::to_string(chunk.ChunkY()) + ")");
    }

    for (const ChunkSquareDiff& patch : diff.squares) {
        chunk.MarkZUsed(patch.z);
        world::GridSquare& square = chunk.Square(patch.lx, patch.ly, patch.z);
        square.Clear();
        square.SetRoomId(patch.roomId);
        for (int layer = 0; layer < kPersistedLayers; ++layer) {
            for (const world::SquareObject& object :
                 patch.layers[static_cast<std::size_t>(layer)]) {
                square.AddObject(static_cast<world::SquareLayer>(layer), object);
            }
        }
    }
    chunk.MarkDirty();
    SetError(error, {});
    return true;
}

std::vector<std::uint8_t> EncodeChunkDiff(const ChunkDiff& diff, std::string* error) {
    if (!ValidateChunkDiff(diff, error)) {
        return {};
    }

    BinaryWriter writer;
    if (!writer.BeginSegment(kMagicChunkDiff, kChunkDiffVersion)) {
        SetError(error, "写入 chunk diff 失败: " + writer.Error());
        return {};
    }
    writer.Write(diff.chunkX);
    writer.Write(diff.chunkY);
    writer.Write(static_cast<std::uint32_t>(diff.squares.size()));
    for (const ChunkSquareDiff& square : diff.squares) {
        writer.Write(square.lx);
        writer.Write(square.ly);
        writer.Write(square.z);
        writer.Write(square.roomId);
        for (int layer = 0; layer < kPersistedLayers; ++layer) {
            const auto& objects = square.layers[static_cast<std::size_t>(layer)];
            writer.Write(static_cast<std::uint32_t>(objects.size()));
            for (const world::SquareObject& object : objects) {
                writer.Write(object.sprite);
                writer.Write(object.offsetX);
                writer.Write(object.offsetY);
                writer.Write(object.blocksMovement);
                writer.Write(object.hp);
                writer.Write(object.playerBuilt);
                writer.Write(object.underlay);
                writer.Write(object.structureId);
                writer.Write(object.ownerStableId);
            }
        }
    }
    if (!writer.EndSegment() || !writer.Ok()) {
        SetError(error, "写入 chunk diff 失败: " + writer.Error());
        return {};
    }

    const auto data = writer.Data();
    SetError(error, {});
    return std::vector<std::uint8_t>(data.begin(), data.end());
}

bool DecodeChunkDiff(std::span<const std::uint8_t> bytes, ChunkDiff* out,
                     std::string* error) {
    if (out == nullptr) {
        return Fail(error, "DecodeChunkDiff: 输出为空");
    }

    BinaryReader reader(bytes);
    SegmentHeader header;
    if (!reader.BeginSegment(header, kMagicChunkDiff, kChunkDiffMinVersion,
                             kChunkDiffVersion)) {
        return Fail(error, "读取 chunk diff 段失败: " + reader.Error());
    }
    const bool hasDurability = header.version >= 2;
    const bool hasUnderlay = header.version >= 3;
    const bool hasStructureIdentity = header.version >= 4;

    ChunkDiff parsed;
    if (!reader.Read(parsed.chunkX) || !reader.Read(parsed.chunkY)) {
        return Fail(error, "读取 chunk diff 坐标失败: " + reader.Error());
    }
    std::uint32_t squareCount = 0;
    if (!reader.Read(squareCount)) {
        return Fail(error, "读取 chunk diff 格数量失败: " + reader.Error());
    }
    if (squareCount > static_cast<std::uint32_t>(world::Chunk::kSquareCount)) {
        return Fail(error, "chunk diff 格数量超过上限");
    }

    try {
        parsed.squares.reserve(squareCount);
        std::array<bool, static_cast<std::size_t>(world::Chunk::kSquareCount)> seen{};
        std::size_t totalObjects = 0;
        for (std::uint32_t i = 0; i < squareCount; ++i) {
            ChunkSquareDiff square;
            if (!reader.Read(square.lx) || !reader.Read(square.ly) ||
                !reader.Read(square.z) || !reader.Read(square.roomId)) {
                return Fail(error, "读取 chunk diff 格头失败: " + reader.Error());
            }
            if (!ValidLocalCoords(square.lx, square.ly, square.z)) {
                return Fail(error, "chunk diff 格坐标越界");
            }
            const int index = world::Chunk::Index(square.lx, square.ly, square.z);
            if (seen[static_cast<std::size_t>(index)]) {
                return Fail(error, "chunk diff 包含重复格");
            }
            seen[static_cast<std::size_t>(index)] = true;

            for (int layer = 0; layer < kPersistedLayers; ++layer) {
                std::uint32_t objectCount = 0;
                if (!reader.Read(objectCount)) {
                    return Fail(error, "读取 chunk diff 对象数量失败: " + reader.Error());
                }
                if (objectCount > kMaxContainerElements) {
                    return Fail(error, "chunk diff 单层对象数量超过上限");
                }
                if (totalObjects > kMaxContainerElements - objectCount) {
                    return Fail(error, "chunk diff 对象总数超过上限");
                }
                if (objectCount > reader.RemainingInSegment()) {
                    return Fail(error, "chunk diff 对象数量超过段剩余数据");
                }
                totalObjects += objectCount;

                auto& objects = square.layers[static_cast<std::size_t>(layer)];
                objects.reserve(std::min<std::uint32_t>(objectCount, 1024));
                for (std::uint32_t objectIndex = 0; objectIndex < objectCount; ++objectIndex) {
                    world::SquareObject object;
                    if (!reader.Read(object.sprite) || !reader.Read(object.offsetX) ||
                        !reader.Read(object.offsetY) || !reader.Read(object.blocksMovement)) {
                        return Fail(error, "读取 chunk diff 对象失败: " + reader.Error());
                    }
                    if (hasDurability) {
                        if (!reader.Read(object.hp) || !reader.Read(object.playerBuilt)) {
                            return Fail(error, "读取 chunk diff 对象耐久失败: " +
                                                  reader.Error());
                        }
                    } else if (layer == static_cast<int>(world::SquareLayer::Furniture)) {
                        // v1 无法区分玩家建造/地图对象；diff 中的家具按玩家改动迁移，
                        // 耐久在首次扫描时回填为 maxHp。
                        object.playerBuilt = true;
                    }
                    if (hasUnderlay && !reader.Read(object.underlay)) {
                        return Fail(error, "读取 chunk diff 地板底层失败: " +
                                              reader.Error());
                    }
                    if (hasStructureIdentity &&
                        (!reader.Read(object.structureId) ||
                         !reader.Read(object.ownerStableId))) {
                        return Fail(error, "读取 chunk diff 结构身份失败: " +
                                              reader.Error());
                    }
                    objects.push_back(std::move(object));
                }
            }
            parsed.squares.push_back(std::move(square));
        }
    } catch (const std::exception&) {
        return Fail(error, "读取 chunk diff 失败（内存不足）");
    }

    if (!reader.EndSegment() || !reader.Ok()) {
        return Fail(error, "读取 chunk diff 段失败: " + reader.Error());
    }
    if (!reader.AtEnd()) {
        return Fail(error, "chunk diff 段后存在多余数据");
    }
    if (!ValidateChunkDiff(parsed, error)) {
        return false;
    }
    *out = std::move(parsed);
    SetError(error, {});
    return true;
}

}  // namespace zl::save
