#include "world/ChunkData.h"

#include "core/Log.h"
#include "world/Chunk.h"

namespace zl::world {

void ApplyChunkData(ChunkData& data, Chunk& chunk) {
    ZL_ASSERT(chunk.ChunkX() == data.chunkX && chunk.ChunkY() == data.chunkY,
              "ChunkData 与目标 Chunk 坐标不一致: data({},{}) chunk({},{})", data.chunkX,
              data.chunkY, chunk.ChunkX(), chunk.ChunkY());

    for (int ly = 0; ly < core::kChunkSize; ++ly) {
        for (int lx = 0; lx < core::kChunkSize; ++lx) {
            std::string& name = data.floors[static_cast<std::size_t>(ly) * core::kChunkSize + lx];
            if (!name.empty()) {
                chunk.Square(lx, ly, 0).SetFloor({std::move(name), 0, 0});
            }
        }
    }

    for (ChunkObjectData& object : data.objects) {
        chunk.Square(object.localX, object.localY, object.z)
            .AddObject(object.layer,
                       {std::move(object.sprite), object.ox, object.oy, object.solid});
    }

    for (const RoomRectData& room : data.rooms) {
        for (int ly = room.localY; ly < room.localY + room.h; ++ly) {
            for (int lx = room.localX; lx < room.localX + room.w; ++lx) {
                chunk.Square(lx, ly, room.z).SetRoomId(room.id);
            }
        }
    }
}

}  // namespace zl::world
