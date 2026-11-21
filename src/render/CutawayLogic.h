#pragma once

// M36-T1：剖切的纯逻辑（不依赖 world/ECS/渲染后端），便于单测。
// 对齐 PZ 的"每格标志 + 750ms 锁"行为，但不复制其源码、常量或命名；
// 具体阈值由 cutaway 配置提供，默认值只是本项目自定。
//
// 使用方式：
//   1. WorldDemoState 收集可见格的 CutawayProbe（房间、邻格、视线、距离）；
//   2. 调 IsCutawaySquareTarget 得到该格目标；
//   3. 调 UpdateCutawaySquareState 合入 750ms 锁；
//   4. 用 WallSquareCut/SquareEnabled 决定上层与边界墙的裁切。

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace zl::render {

struct CutawaySquareKey {
    int x = 0;
    int y = 0;
    int z = 0;

    friend bool operator==(const CutawaySquareKey& a, const CutawaySquareKey& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
};

struct CutawaySquareKeyHash {
    std::size_t operator()(const CutawaySquareKey& key) const noexcept {
        std::uint64_t h = static_cast<std::uint32_t>(key.x);
        h = (h ^ static_cast<std::uint32_t>(key.y)) * 0x9E3779B185EBCA87ull;
        h = (h ^ static_cast<std::uint32_t>(key.z)) * 0xC2B2AE3D27D4EB4Full;
        return static_cast<std::size_t>(h ^ (h >> 29));
    }
};

struct CutawaySquareState {
    bool enabled = false;    // 当前生效值
    bool target = false;     // 最新目标值
    bool hasTarget = false;  // 是否已收到过目标
    double lockUntil = 0.0;  // 生效值下次允许翻转的时间（秒）
};

// 只有目标变化才更新生效值并加锁；锁内保持旧值。返回生效值是否改变。
inline bool UpdateCutawaySquareState(CutawaySquareState& state, bool target, double now,
                                     double lockSeconds) {
    state.target = target;
    state.hasTarget = true;
    if (state.enabled == target) {
        return false;
    }
    if (now < state.lockUntil) {
        return false;
    }
    state.enabled = target;
    state.lockUntil = now + std::max(0.0, lockSeconds);
    return true;
}

struct CutawayRules {
    float maxWallDistance = 6.0f;  // 边界墙参与的最近距离（格），0 = 不限制
};

struct CutawayProbe {
    CutawaySquareKey key;
    int roomId = -1;
    // 8 邻格房间 ID（N, NE, E, SE, S, SW, W, NW；-1 = 无/室外）。
    std::array<int, 8> neighborRooms{{-1, -1, -1, -1, -1, -1, -1, -1}};
    bool hasWall = false;      // 该格有墙/门（Wall 层）
    bool lineOfSight = false;  // 玩家到该格是否可达（调用方射线结果）
    float distToPlayer = 0.0f; // 玩家到格中心距离（格）
};

inline bool RoomInSet(int roomId, const std::vector<int>& rooms) {
    return roomId >= 0 && std::find(rooms.begin(), rooms.end(), roomId) != rooms.end();
}

// 某格是否应剖切：
//   - 房间在剖切集合内（地面/上层直接参与）；
//   - 或该格有墙，且自身/8 邻格接触集合内房间，同时满足距离与视线。
inline bool IsCutawaySquareTarget(const CutawayProbe& probe, const std::vector<int>& rooms,
                                  const CutawayRules& rules) {
    if (rooms.empty() || probe.key.z < 0) {
        return false;
    }
    if (RoomInSet(probe.roomId, rooms)) {
        return true;
    }
    if (!probe.hasWall) {
        return false;
    }
    if (rules.maxWallDistance > 0.0f && probe.distToPlayer > rules.maxWallDistance) {
        return false;
    }
    if (!probe.lineOfSight) {
        return false;
    }
    if (RoomInSet(probe.roomId, rooms)) {
        return true;
    }
    for (int neighbor : probe.neighborRooms) {
        if (RoomInSet(neighbor, rooms)) {
            return true;
        }
    }
    return false;
}

using CutawaySquareMap =
    std::unordered_map<CutawaySquareKey, CutawaySquareState, CutawaySquareKeyHash>;

inline bool SquareEnabled(const CutawaySquareMap& squares, int x, int y, int z) {
    const auto it = squares.find({x, y, z});
    return it != squares.end() && it->second.enabled;
}

// 对齐 PZ DoCutawayShaderSprite 的主路径：N/W 墙的裁切由自身与 S/E 邻格的剖切标志决定。
inline bool WallSquareCut(const CutawaySquareMap& squares, int x, int y, int z) {
    return SquareEnabled(squares, x, y, z) || SquareEnabled(squares, x, y + 1, z) ||
           SquareEnabled(squares, x + 1, y, z);
}

// M36-T2：墙体可剖边（manifest cutaway.edges），与 PZ sprite.cutN/cutW 对应。
inline constexpr std::uint8_t kCutawayEdgeNorth = 1u;
inline constexpr std::uint8_t kCutawayEdgeWest = 2u;

// 裁切风格（manifest cutaway.style）；数值与 AssetCutawayMeta::Style 一致。
enum class CutawayStyle : std::uint8_t {
    Full = 0,   // 整块保留顶部（N/W 任一方向触发都压下沿）
    Top = 1,    // 同 Full，语义上只保留墙顶
    Left = 2,   // 按列裁：保留左侧
    Right = 3,  // 按列裁：保留右侧
};

struct WallCutFlags {
    bool north = false;
    bool west = false;

    bool Any() const { return north || west; }
};

// 对齐 PZ DoCutawayShaderSprite：
//   N 面墙看自身 + E 邻格（iso 中 N 墙沿 NE-SW 延伸）；
//   W 面墙看自身 + S 邻格。
inline WallCutFlags EvaluateWallCut(const CutawaySquareMap& squares, int x, int y, int z,
                                    std::uint8_t edges) {
    WallCutFlags flags;
    if (edges == 0u) {
        return flags;
    }
    const bool self = SquareEnabled(squares, x, y, z);
    if ((edges & kCutawayEdgeNorth) != 0u) {
        flags.north = self || SquareEnabled(squares, x + 1, y, z);
    }
    if ((edges & kCutawayEdgeWest) != 0u) {
        flags.west = self || SquareEnabled(squares, x, y + 1, z);
    }
    return flags;
}

// 删除"已关闭且锁已过期"的格，避免长期驻留；返回删除数量。
inline std::size_t PruneCutawaySquares(CutawaySquareMap& squares, double now) {
    std::size_t removed = 0;
    for (auto it = squares.begin(); it != squares.end();) {
        const CutawaySquareState& state = it->second;
        if (!state.enabled && now >= state.lockUntil) {
            it = squares.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

}  // namespace zl::render
