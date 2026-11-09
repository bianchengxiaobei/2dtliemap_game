#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

#include "core/Constants.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"
#include "world/World.h"
#include "world/WorldCoords.h"

namespace zl::game {

// M5-T2：世界查询小工具（点击校验 / AiSystem 游荡目标挑选共用）。
// 语义：只认已挂接（Loaded）chunk —— 未加载 / 越界一律按"不可用"处理。

// 查已挂接 chunk（cell / chunk 链路未创建或未挂接 → nullptr）
inline world::Chunk* FindChunkInWorld(world::World& world, int chunkX, int chunkY) {
    const world::CellCoord cell{world::FloorDiv(chunkX, core::kChunksPerCell),
                                world::FloorDiv(chunkY, core::kChunksPerCell)};
    world::Cell* owner = world.GetCell(cell.cx, cell.cy);
    if (owner == nullptr) {
        return nullptr;
    }
    const world::ChunkLocal local = world::ChunkLocalOf(chunkX, chunkY);
    return owner->GetChunk(local.lcx, local.lcy);
}

// 格是否阻挡（"未加载 = 未知 = 阻挡"；任一层的对象 blocksMovement 即阻挡）
inline bool IsSquareBlocked(world::World& world, int wx, int wy, int z) {
    const world::ChunkCoord cc = world::ChunkOf(wx, wy);
    const world::Chunk* chunk = FindChunkInWorld(world, cc.cx, cc.cy);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
        return true;
    }
    const world::SquareLocal local = world::SquareLocalOf(wx, wy);
    const world::GridSquare& square = chunk->Square(local.lx, local.ly, z);
    for (int i = 0; i < world::kSquareLayerCount; ++i) {
        for (const world::SquareObject& object :
             square.Layer(static_cast<world::SquareLayer>(i))) {
            if (object.blocksMovement) {
                return true;
            }
        }
    }
    return false;
}

// M20-F2：格所在房间 ID（-1 = 室外/未加载）。
inline int RoomIdAt(world::World& world, int wx, int wy, int z) {
    const world::ChunkCoord cc = world::ChunkOf(wx, wy);
    const world::Chunk* chunk = FindChunkInWorld(world, cc.cx, cc.cy);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
        return -1;
    }
    const world::SquareLocal local = world::SquareLocalOf(wx, wy);
    return chunk->Square(local.lx, local.ly, z).RoomId();
}

// 玩家是否在室内：先看脚下房间，再以约一个碰撞半径向 3×3 邻格找最近房间，
// 与渲染剖切使用同一套“贴墙/角落仍算室内”规则。
inline bool IsIndoors(world::World& world, float px, float py, int z) {
    if (!std::isfinite(px) || !std::isfinite(py)) {
        return false;
    }
    const int cx = static_cast<int>(std::floor(px));
    const int cy = static_cast<int>(std::floor(py));
    if (RoomIdAt(world, cx, cy, z) >= 0) {
        return true;
    }
    constexpr float kProbeDistance = 0.75f;
    constexpr float kProbeDistanceSq = kProbeDistance * kProbeDistance;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int wx = cx + dx;
            const int wy = cy + dy;
            if (RoomIdAt(world, wx, wy, z) < 0) {
                continue;
            }
            const float nearestX =
                std::clamp(px, static_cast<float>(wx), static_cast<float>(wx + 1));
            const float nearestY =
                std::clamp(py, static_cast<float>(wy), static_cast<float>(wy + 1));
            const float ddx = px - nearestX;
            const float ddy = py - nearestY;
            if (ddx * ddx + ddy * ddy <= kProbeDistanceSq) {
                return true;
            }
        }
    }
    return false;
}

// 附近最近的篝火（玩家建造或地图对象；逻辑名 object.campfire）。
inline bool FindCampfireNearby(world::World& world, float px, float py, int z,
                               float radius, int* outX = nullptr,
                               int* outY = nullptr) {
    if (!(radius > 0.0f) || !std::isfinite(radius) ||
        !std::isfinite(px) || !std::isfinite(py)) {
        return false;
    }
    const float radiusSq = radius * radius;
    float bestDistanceSq = radiusSq;
    int bestX = 0;
    int bestY = 0;
    bool found = false;
    const int x0 = static_cast<int>(std::floor(px - radius));
    const int x1 = static_cast<int>(std::ceil(px + radius));
    const int y0 = static_cast<int>(std::floor(py - radius));
    const int y1 = static_cast<int>(std::ceil(py + radius));
    for (int wy = y0; wy <= y1; ++wy) {
        for (int wx = x0; wx <= x1; ++wx) {
            const float dx = (static_cast<float>(wx) + 0.5f) - px;
            const float dy = (static_cast<float>(wy) + 0.5f) - py;
            const float distanceSq = dx * dx + dy * dy;
            if (distanceSq > bestDistanceSq) {
                continue;
            }
            const world::ChunkCoord cc = world::ChunkOf(wx, wy);
            const world::Chunk* chunk = FindChunkInWorld(world, cc.cx, cc.cy);
            if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
                continue;
            }
            const world::SquareLocal local = world::SquareLocalOf(wx, wy);
            const world::GridSquare& square = chunk->Square(local.lx, local.ly, z);
            bool squareHasCampfire = false;
            for (int layer = 0; layer < world::kSquareLayerCount; ++layer) {
                for (const world::SquareObject& object :
                     square.Layer(static_cast<world::SquareLayer>(layer))) {
                    if (object.sprite == "object.campfire") {
                        squareHasCampfire = true;
                        break;
                    }
                }
                if (squareHasCampfire) {
                    break;
                }
            }
            if (squareHasCampfire) {
                bestDistanceSq = distanceSq;
                bestX = wx;
                bestY = wy;
                found = true;
            }
        }
    }
    if (found) {
        if (outX != nullptr) {
            *outX = bestX;
        }
        if (outY != nullptr) {
            *outY = bestY;
        }
    }
    return found;
}

inline bool HasCampfireNearby(world::World& world, float px, float py, int z,
                              float radius) {
    return FindCampfireNearby(world, px, py, z, radius);
}

// 附近是否有制作站（recipes-v3 station）。站点用世界对象的逻辑名
// "object.<stationId>" 标记（例如 workbench -> object.workbench）；只扫描
// Furniture / Item 层与已挂接 chunk，半径按格中心到对象格中心的距离计算。
inline bool HasStationNearby(world::World& world, float px, float py, int z,
                             std::string_view stationId, float radius) {
    if (stationId.empty() || !(radius > 0.0f) || !std::isfinite(radius)) {
        return false;
    }
    const std::string sprite = stationId.starts_with("object.")
                                   ? std::string(stationId)
                                   : "object." + std::string(stationId);
    const float radiusSq = radius * radius;
    const int x0 = static_cast<int>(std::floor(px - radius));
    const int x1 = static_cast<int>(std::ceil(px + radius));
    const int y0 = static_cast<int>(std::floor(py - radius));
    const int y1 = static_cast<int>(std::ceil(py + radius));
    for (int wy = y0; wy <= y1; ++wy) {
        for (int wx = x0; wx <= x1; ++wx) {
            const float dx = (static_cast<float>(wx) + 0.5f) - px;
            const float dy = (static_cast<float>(wy) + 0.5f) - py;
            if (dx * dx + dy * dy > radiusSq) {
                continue;
            }
            const world::ChunkCoord cc = world::ChunkOf(wx, wy);
            const world::Chunk* chunk = FindChunkInWorld(world, cc.cx, cc.cy);
            if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
                continue;
            }
            const world::SquareLocal local = world::SquareLocalOf(wx, wy);
            const world::GridSquare& square = chunk->Square(local.lx, local.ly, z);
            for (world::SquareLayer layer :
                 {world::SquareLayer::Furniture, world::SquareLayer::Item}) {
                for (const world::SquareObject& object : square.Layer(layer)) {
                    if (object.sprite == sprite) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

}  // namespace zl::game
