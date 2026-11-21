#pragma once

// M36-T5：上层/屋顶归属判定（纯逻辑，不依赖 world/ECS/渲染后端）。
// 对齐 PZ 的"屋顶属于 z=0 地面房间，只有玩家所在/被遮挡建筑的上层才隐藏"行为；
// 不复制 PZ 的 haveRoof/roofHideBuilding 字段或常量。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "render/BuildingOcclusion.h"
#include "render/CutawayLogic.h"
#include "render/DrawList.h"

namespace zl::render {

inline bool BuildingHasRoom(const BuildingInfo& building, int roomId) {
    return roomId >= 0 &&
           std::find(building.roomIds.begin(), building.roomIds.end(), roomId) !=
               building.roomIds.end();
}

inline const BuildingInfo* FindBuildingContainingRoom(
    const std::vector<BuildingInfo>& buildings, int roomId) {
    for (const BuildingInfo& building : buildings) {
        if (BuildingHasRoom(building, roomId)) {
            return &building;
        }
    }
    return nullptr;
}

// 空悬上层（地面无房间归属）只在正下方 1 格内确有剖切格时隐藏，避免误删露台。
inline bool AnyCutawaySquareNear(const CutawaySquareMap& squares, int gx, int gy, int z,
                                 int radius) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (SquareEnabled(squares, gx + dx, gy + dy, z)) {
                return true;
            }
        }
    }
    return false;
}

// 是否隐藏该 z≥1 上层对象：
//   - roofCull=false：回退到 T4 的按格剖切语义；
//   - 有地面房间归属：房间在剖切集合、玩家所在建筑、或被 T3 选中的建筑里才隐藏；
//   - 无归属（露台/空悬屋顶）：仅当正下方 1 格内有剖切格时隐藏。
inline bool ShouldHideUpper(const DrawItem& item, const std::vector<int>& cutawayRooms,
                            const std::vector<BuildingInfo>& buildings,
                            const std::vector<int>& occluderBuildingIds, int playerBuildingId,
                            const CutawaySquareMap& cutawaySquares, bool roofCull) {
    if (item.z < 1) {
        return false;
    }
    const int gy = item.diag - item.gx;
    if (!roofCull) {
        return SquareEnabled(cutawaySquares, item.gx, gy, 0);
    }
    if (!item.isRoof) {
        return false;
    }
    if (item.roofOwnerRoom >= 0) {
        if (RoomInSet(item.roofOwnerRoom, cutawayRooms)) {
            return true;
        }
        if (playerBuildingId >= 0) {
            const BuildingInfo* playerBuilding =
                FindBuildingContainingRoom(buildings, item.roofOwnerRoom);
            if (playerBuilding != nullptr && playerBuilding->id == playerBuildingId) {
                return true;
            }
        }
        for (const int id : occluderBuildingIds) {
            for (const BuildingInfo& building : buildings) {
                if (building.id == id && BuildingHasRoom(building, item.roofOwnerRoom)) {
                    return true;
                }
            }
        }
        return false;
    }
    return AnyCutawaySquareNear(cutawaySquares, item.gx, gy, 0, 1);
}

}  // namespace zl::render
