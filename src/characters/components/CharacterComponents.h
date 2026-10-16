#pragma once

#include <cstdint>

namespace zl::ecs {

// M4-T4：角色领域组件（玩家控制 + 占位动画）。
// 组件 = 纯数据；行为在 characters::InputSystem / AnimationSystem / MovementSystem。

// 本地玩家标记（一局一个）。只有带 PlayerTag 的实体读键盘。
struct PlayerTag {};

// 本地控制参数（玩家专属；AI 实体不挂）
struct LocalControl {
    float walkSpeed = 4.0f;  // 步行速度（格/秒）
};

// 期望速度（格/秒，世界格空间）：InputSystem / AI 写入，MovementSystem 消费。
// 语义 = "本 tick 想以这个速度移动"；写入方每 tick 全量重写（无输入 = 零向量）。
struct MoveIntent {
    float x = 0.0f;
    float y = 0.0f;
};

// 4 向朝向（屏幕语义，取值即帧序：Down/Left/Right/Up）。
// 由移动速度在屏幕坐标下的主轴导出（AnimationSystem）。
enum class Direction : std::uint8_t {
    Down = 0,
    Left = 1,
    Right = 2,
    Up = 3,
};

struct Facing {
    Direction dir = Direction::Down;
};

// 占位动画（M4-T4：4 向 × 2 帧，站立用 frame 0）。
// 帧序约定：图集/清单必须按 index = dir * framesPerDir + frame 列出帧
// （own 图集：character_down_0/1, left_0/1, right_0/1, up_0/1）。
struct AnimState {
    std::uint8_t frame = 0;         // [0, framesPerDir)
    std::uint8_t framesPerDir = 2;  // 每方向帧数
    float fps = 8.0f;               // 行走帧率
    float time = 0.0f;              // 帧计时累加器（循环取模，不无界增长）
};

}  // namespace zl::ecs
