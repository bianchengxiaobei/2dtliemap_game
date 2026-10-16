#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace zl::ecs {

// M4-T6：寻路 ECS 组件（PathRequest / PathFollow）。
// 组件 = 纯数据；行为在 ai::PathFollowSystem（固定链：Input 之后、Movement 之前）。

enum class PathStatus : std::uint8_t {
    None = 0,       // 空闲（无路径）
    Pending = 1,    // 后台寻路中
    Following = 2,  // 沿路点前进
    Arrived = 3,    // 已到达目标格
    Failed = 4,     // 不可达/越界/超预算（goal 保留在 PathRequest，便于重试/提示）
};

// 路点（世界格坐标；格中心 = 整数 + 0.5）
struct Waypoint {
    float x = 0.0f;
    float y = 0.0f;
};

// 寻路请求：外部（点击/AI）只写 goalX/goalY + newRequest=true；
// PathFollowSystem 负责提交后台查询、消费结果、推进 state。
struct PathRequest {
    int goalX = 0;
    int goalY = 0;
    bool newRequest = false;    // 置 true 发起（或重发）请求
    std::uint64_t queryId = 0;  // 当前在途查询（0 = 无）
    PathStatus state = PathStatus::None;
};

// 已生成路径的跟随状态（PathFollowSystem 写 MoveIntent，MovementSystem 消费）。
struct PathFollow {
    std::vector<Waypoint> waypoints;  // 格中心序列（含终点，不含起点）
    std::size_t index = 0;            // 下一个未到达的 waypoint
    float speed = 4.0f;               // 跟随速度（格/秒）
    float reachRadius = 0.35f;        // 路点到达半径（< 半格，防跨格抖动）
    PathStatus status = PathStatus::None;
    // M4-T7：进度跟踪（卡住检测 → 自动重寻路）
    float anchorX = 0.0f;   // 上次采样位置（每 stuckWindow 更新）
    float anchorY = 0.0f;
    float stuckTimer = 0.0f;  // 距上次采样的累计时间
};

}  // namespace zl::ecs
