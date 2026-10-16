#pragma once

#include <cstdint>

namespace zl::ecs {
class EcsWorld;
enum class Direction : std::uint8_t;
}  // namespace zl::ecs

namespace zl::characters {

// M4-T4：Velocity → Facing + AnimState 帧推进（占位动画，4 向 × 2 帧）。
//
// 朝向取"移动速度在屏幕坐标下的主轴"：
//   屏幕速度 sx = vx - vy, sy = vx + vy（IsoMath 投影的线性部分），
//   |sy| >= |sx| 取上/下，否则取左/右。
// 静止（|v| < 阈值）时：frame 归 0（站立帧），朝向保持最后值。
class AnimationSystem {
public:
    static constexpr float kMoveThreshold = 0.1f;  // 格/秒：低于此视为静止

    void Update(ecs::EcsWorld& world, float dt) const;

    // 世界格速度 → 4 向朝向（纯函数，可单测）
    static ecs::Direction FacingFromVelocity(float vx, float vy);
};

}  // namespace zl::characters
