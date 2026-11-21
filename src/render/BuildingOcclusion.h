#pragma once

// M36-T3：室外建筑遮挡的纯逻辑（不依赖 world/ECS/渲染后端）。
// 参考 PZ IsoCell::collapsibleBuildingSquareAlgorithm 的主路径：
// 玩家在建筑西北侧时，剖掉建筑朝向相机（东南）的墙体与上层；
// 这里用"房间矩形聚合成建筑 + 屏幕矩形相交 + 距离/方位"做等价近似，
// 不复制 PZ 源码、常量或地图 BuildingDef 数据。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "render/IsoMath.h"

namespace zl::render {

struct RoomBounds {
    int roomId = -1;
    int minX = 0;
    int minY = 0;
    int maxX = 0;  // 含
    int maxY = 0;  // 含
    int z = 0;
};

struct BuildingInfo {
    int id = -1;
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
    int z = 0;
    std::vector<int> roomIds;
};

inline bool BuildingContains(const BuildingInfo& b, int x, int y) {
    return x >= b.minX && x <= b.maxX && y >= b.minY && y <= b.maxY;
}

// 房间 -> 建筑：同 roomId 先合并，矩形相交或间隔 ≤1 格且同 z 再合并。
inline std::vector<BuildingInfo> GroupRoomsIntoBuildings(std::vector<RoomBounds> rooms) {
    std::vector<BuildingInfo> buildings;
    if (rooms.empty()) {
        return buildings;
    }
    std::sort(rooms.begin(), rooms.end(), [](const RoomBounds& a, const RoomBounds& b) {
        if (a.roomId != b.roomId) {
            return a.roomId < b.roomId;
        }
        if (a.z != b.z) {
            return a.z < b.z;
        }
        if (a.minY != b.minY) {
            return a.minY < b.minY;
        }
        return a.minX < b.minX;
    });

    std::vector<BuildingInfo> merged;
    for (const RoomBounds& room : rooms) {
        bool absorbed = false;
        for (BuildingInfo& building : merged) {
            if (building.z != room.z) {
                continue;
            }
            const bool sameRoom = std::find(building.roomIds.begin(), building.roomIds.end(),
                                            room.roomId) != building.roomIds.end();
            const bool adjacent = room.minX <= building.maxX + 1 &&
                                  building.minX <= room.maxX + 1 &&
                                  room.minY <= building.maxY + 1 &&
                                  building.minY <= room.maxY + 1;
            if (!sameRoom && !adjacent) {
                continue;
            }
            building.minX = std::min(building.minX, room.minX);
            building.minY = std::min(building.minY, room.minY);
            building.maxX = std::max(building.maxX, room.maxX);
            building.maxY = std::max(building.maxY, room.maxY);
            building.id = std::min(building.id, room.roomId);
            building.roomIds.push_back(room.roomId);
            absorbed = true;
            break;
        }
        if (!absorbed) {
            BuildingInfo building;
            building.id = room.roomId;
            building.minX = room.minX;
            building.minY = room.minY;
            building.maxX = room.maxX;
            building.maxY = room.maxY;
            building.z = room.z;
            building.roomIds.push_back(room.roomId);
            merged.push_back(std::move(building));
        }
    }

    // 合并后可能让两个建筑变得相邻，做一次收敛。
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < merged.size() && !changed; ++i) {
            for (std::size_t j = i + 1; j < merged.size(); ++j) {
                BuildingInfo& a = merged[i];
                const BuildingInfo& b = merged[j];
                if (a.z != b.z) {
                    continue;
                }
                const bool adjacent = a.minX <= b.maxX + 1 && b.minX <= a.maxX + 1 &&
                                      a.minY <= b.maxY + 1 && b.minY <= a.maxY + 1;
                if (!adjacent) {
                    continue;
                }
                a.minX = std::min(a.minX, b.minX);
                a.minY = std::min(a.minY, b.minY);
                a.maxX = std::max(a.maxX, b.maxX);
                a.maxY = std::max(a.maxY, b.maxY);
                a.id = std::min(a.id, b.id);
                a.roomIds.insert(a.roomIds.end(), b.roomIds.begin(), b.roomIds.end());
                merged.erase(merged.begin() + static_cast<std::ptrdiff_t>(j));
                changed = true;
                break;
            }
        }
    }

    for (BuildingInfo& building : merged) {
        std::sort(building.roomIds.begin(), building.roomIds.end());
        building.roomIds.erase(std::unique(building.roomIds.begin(), building.roomIds.end()),
                               building.roomIds.end());
        if (building.id < 0 && !building.roomIds.empty()) {
            building.id = building.roomIds.front();
        }
    }
    std::sort(merged.begin(), merged.end(),
              [](const BuildingInfo& a, const BuildingInfo& b) { return a.id < b.id; });
    // 同一 roomId 在跨 chunk 被裁成多块时可能产生重复 id；保证内部 id 唯一。
    int nextId = 0;
    for (BuildingInfo& building : merged) {
        if (building.id < nextId) {
            building.id = nextId;
        }
        nextId = std::max(nextId, building.id + 1);
    }
    buildings = std::move(merged);
    return buildings;
}

// 建筑在世界像素空间的包围盒：地面四角投影 + 墙高/地面厚度。
inline SpriteRect BuildingScreenRect(const BuildingInfo& b) {
    const float x0 = static_cast<float>(b.minX);
    const float y0 = static_cast<float>(b.minY);
    const float x1 = static_cast<float>(b.maxX + 1);
    const float y1 = static_cast<float>(b.maxY + 1);
    const glm::vec2 p0 = ToScreen(x0, y0, 0);
    const glm::vec2 p1 = ToScreen(x0, y1, 0);
    const glm::vec2 p2 = ToScreen(x1, y0, 0);
    const glm::vec2 p3 = ToScreen(x1, y1, 0);
    SpriteRect rect;
    rect.x = std::min({p0.x, p1.x, p2.x, p3.x});
    rect.y = std::min({p0.y, p1.y, p2.y, p3.y}) - 96.0f;  // 墙/屋顶高度余量
    rect.w = std::max({p0.x, p1.x, p2.x, p3.x}) - rect.x;
    rect.h = std::max({p0.y, p1.y, p2.y, p3.y}) + 16.0f - rect.y;
    return rect;
}

inline float BuildingGapToPlayer(const BuildingInfo& b, int px, int py) {
    const float dx = std::max({0.0f, static_cast<float>(b.minX) - static_cast<float>(px),
                               static_cast<float>(px) - static_cast<float>(b.maxX)});
    const float dy = std::max({0.0f, static_cast<float>(b.minY) - static_cast<float>(py),
                               static_cast<float>(py) - static_cast<float>(b.maxY)});
    return dx + dy;
}

// 选择会挡住玩家的建筑：
//   - 排除玩家所在建筑/玩家脚下的建筑；
//   - 屏幕包围盒与玩家相交；
//   - 建筑必须延伸到玩家东/南侧（等距相机下位于屏幕下方、后绘制）；
//   - 按间隙距离排序并截断。
inline std::size_t SelectOccluderBuildings(const SpriteRect& playerRect, int playerGx,
                                           int playerGy, int playerRoomId,
                                           const std::vector<BuildingInfo>& buildings,
                                           std::size_t maxCount,
                                           std::vector<int>* chosen,
                                           float maxGap = 8.0f) {
    if (chosen == nullptr) {
        return 0;
    }
    chosen->clear();
    if (maxCount == 0 || playerRect.w <= 0.0f || playerRect.h <= 0.0f) {
        return 0;
    }
    struct Candidate {
        int id = -1;
        float gap = 0.0f;
    };
    std::vector<Candidate> candidates;
    for (const BuildingInfo& building : buildings) {
        if (building.z != 0) {
            continue;
        }
        if (playerRoomId >= 0 &&
            std::find(building.roomIds.begin(), building.roomIds.end(), playerRoomId) !=
                building.roomIds.end()) {
            continue;
        }
        if (BuildingContains(building, playerGx, playerGy)) {
            continue;
        }
        const float gap = BuildingGapToPlayer(building, playerGx, playerGy);
        if (maxGap > 0.0f && gap > maxGap) {
            continue;
        }
        // 遮挡来自玩家东南侧（更大的 x+y ⇒ 屏幕更靠下、后绘制）。
        if (building.maxX <= playerGx && building.maxY <= playerGy) {
            continue;
        }
        const SpriteRect rect = BuildingScreenRect(building);
        const bool overlaps = rect.x < playerRect.x + playerRect.w &&
                              rect.x + rect.w > playerRect.x &&
                              rect.y < playerRect.y + playerRect.h &&
                              rect.y + rect.h > playerRect.y;
        if (!overlaps) {
            continue;
        }
        candidates.push_back({building.id, gap});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.gap != b.gap) {
            return a.gap < b.gap;
        }
        return a.id < b.id;
    });
    const std::size_t count = std::min(candidates.size(), maxCount);
    chosen->reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        chosen->push_back(candidates[i].id);
    }
    return count;
}

// 某格是否属于被遮挡建筑、且需要剖切：
//   - 在建筑外扩 1 格内、玩家不在建筑内；
//   - 与玩家曼哈顿距离 ≤ maxDistance（PZ 主路径 6）；
//   - 玩家位于该格西北侧（x/y 至少一个方向在前）。
inline bool ShouldCutBuildingSquare(const BuildingInfo& building, int x, int y,
                                    float playerX, float playerY, float maxDistance) {
    if (building.z != 0) {
        return false;
    }
    if (x < building.minX - 1 || x > building.maxX + 1 || y < building.minY - 1 ||
        y > building.maxY + 1) {
        return false;
    }
    if (BuildingContains(building, static_cast<int>(std::floor(playerX)),
                         static_cast<int>(std::floor(playerY)))) {
        return false;
    }
    const float dist = std::abs(static_cast<float>(x) + 0.5f - playerX) +
                       std::abs(static_cast<float>(y) + 0.5f - playerY);
    if (maxDistance > 0.0f && dist > maxDistance) {
        return false;
    }
    return playerX < static_cast<float>(x) + 0.5f ||
           playerY < static_cast<float>(y) + 0.5f;
}

}  // namespace zl::render
