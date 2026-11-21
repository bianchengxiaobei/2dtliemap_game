#pragma once

// M36-T4：窗口扒看的纯逻辑（不依赖 world/ECS/渲染后端）。
// 参考 PZ IsoCell::GetPeekedInBuilding 的主路径：玩家在室外、朝向前方
// 且视线可穿过窗/墙时，把窗后房间并入剖切集合。
// 这里用"距离 + 朝向点积 + 窗后一格房间"做等价近似，不复制 PZ 源码/常量。

#include <algorithm>
#include <cmath>

namespace zl::render {

// 玩家 (px,py) 是否在窗格 (wx,wy) 前扒看，并返回窗后房间。
//   - 到窗格矩形最近点距离 ≤ maxRange（贴墙站位也能命中）；
//   - 朝向点积 > 0.5（正对窗户）；
//   - 沿主朝向轴的下一格房间 ≥0（未加载/室外返回 -1）。
template <typename RoomAt>
inline int WindowPeekRoomId(float px, float py, float facingX, float facingY, int wx, int wy,
                            RoomAt&& roomAt, float maxRange = 0.9f) {
    const float length = std::sqrt(facingX * facingX + facingY * facingY);
    if (length <= 1e-6f) {
        return -1;
    }
    facingX /= length;
    facingY /= length;

    const float dx = static_cast<float>(wx) + 0.5f - px;
    const float dy = static_cast<float>(wy) + 0.5f - py;
    const float centerDist = std::sqrt(dx * dx + dy * dy);
    // 距离按"到窗格矩形最近点"算：贴墙站位中心距窗格中心约 1 格，
    // 用中心距会让 0.9 格阈值永远失败。
    const float nearestX = std::clamp(px, static_cast<float>(wx), static_cast<float>(wx + 1));
    const float nearestY = std::clamp(py, static_cast<float>(wy), static_cast<float>(wy + 1));
    const float edgeDx = px - nearestX;
    const float edgeDy = py - nearestY;
    const float edgeDist = std::sqrt(edgeDx * edgeDx + edgeDy * edgeDy);
    if (maxRange > 0.0f && edgeDist > maxRange) {
        return -1;
    }
    if (centerDist > 1e-6f) {
        const float dot = (dx * facingX + dy * facingY) / centerDist;
        if (dot <= 0.5f) {
            return -1;
        }
    }

    // 窗后一格：沿朝向的主轴取反（PZ 对 N/S/W/E 各查相邻格）。
    int targetX = wx;
    int targetY = wy;
    if (std::abs(facingX) >= std::abs(facingY)) {
        targetX += facingX >= 0.0f ? 1 : -1;
    } else {
        targetY += facingY >= 0.0f ? 1 : -1;
    }
    int roomId = roomAt(targetX, targetY);
    if (roomId < 0) {
        // 有些地图把房间 ID 记在窗格所在格；再查一次。
        roomId = roomAt(wx, wy);
    }
    return roomId;
}

}  // namespace zl::render
