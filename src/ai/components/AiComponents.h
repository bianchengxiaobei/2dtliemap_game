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

// M5-T2：僵尸 AI 状态。AiSystem 用一张集中转移表消费；目标/最后已知位置由感知
// 系统（M5-T3）或演示代码维护，节拍字段供伤害管线（M5-T6）挂钩。
enum class AiStatus : std::uint8_t {
    Idle = 0,             // 原地发呆（个体化时长）→ Wander / Chase
    Wander = 1,           // 随机游荡（PathRequest → PathFollow）→ Idle / Chase
    Chase = 2,            // 追击中（走向最后已知位置；贴近 → Attack，目标丢失去 SearchLastKnown）
    SearchLastKnown = 3,  // 在最后已知位置附近巡点搜索（超时 → Idle）
    Attack = 4,           // 贴身攻击（停步朝向 + 节拍脉冲；M5-T6 接伤害）
    Dead = 5,             // 已死（DeathSystem 转尸体；AI 不再动作）
};

struct AiState {
    AiStatus status = AiStatus::Idle;
    float stateTime = 0.0f;      // 当前状态已持续（秒）
    float actionTimer = 0.0f;    // 通用动作节流（重寻路/搜索巡点间隔，秒）
    float idleSeconds = 3.0f;    // 本次 Idle 时长（进入时按 StableId 确定性随机）
    std::uint64_t targetId = 0;  // 追击目标 StableId（0 = 当前未感知）
    float lastKnownX = 0.0f;     // 目标最后已知位置（丢失后 SearchLastKnown 追踪用）
    float lastKnownY = 0.0f;
    float attackCooldown = 0.0f;     // 攻击节拍计时（<=0 落一次攻击脉冲）
    std::uint32_t attackPulses = 0;  // 累计攻击次数（M5-T6 伤害挂钩点）
};

}  // namespace zl::ecs
