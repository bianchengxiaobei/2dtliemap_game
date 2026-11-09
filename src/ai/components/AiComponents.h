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
    std::uint64_t targetId = 0;  // 追击目标 StableId（0 = 当前未感知；PerceptionSystem 维护）
    float attackCooldown = 0.0f;     // 攻击冷却（<=0 且无前摇时开始下一击）
    float attackWindupLeft = 0.0f;   // M15-T3：可读攻击前摇剩余（秒；>0 = 蓄力中）
    std::uint32_t attackPulses = 0;  // 累计攻击次数（M5-T6 伤害挂钩点）
};

// M5-T3：感知状态（PerceptionSystem 写、AiSystem 读）。
//  - alertness：警觉度 [0,1]（看见 = 1；听见 += boost；每秒 -1/alertDecaySeconds）；
//  - lastKnown：LastKnownPos —— AI 的"可疑点"（看见 = 玩家位置；听见 = 声源位置；
//    目标丢失后 SearchLastKnown 就围着它巡点）；
//  - sinceSeen/sinceHeard：距上次看见/听见（秒）——AI 用它判"刚听见声音"（去查看）。
struct PerceptionState {
    float alertness = 0.0f;
    float lastKnownX = 0.0f;
    float lastKnownY = 0.0f;
    float sinceSeen = 1000.0f;
    float sinceHeard = 1000.0f;
};

// M5-T5：LOD 档位（LodSystem 按"到相机焦点距离"每 tick 写；AI 相关系统读）。
//  - tier：0=全速（每 tick）/ 1=30Hz（每 2 tick）/ 2=10Hz（每 6 tick，60Hz 基准）；
//  - updateThisTick：本 tick 是否轮到该实体做"重"逻辑（视觉/AI 转移/寻路跟随/群体行为）；
//  - dtScale：轮到时的等效步长 = dt × 倍率（2/6），保证计时语义与全速一致。
// 听觉与警觉衰减不门控（声音半径大、成本低，且声源队列逐 tick 即弃）。
struct LodState {
    std::uint8_t tier = 0;
    bool updateThisTick = true;
    float dtScale = 1.0f;
};

// M20-F3：夜间事件特殊僵尸标记。kind 只描述行为，不替换 ZombieVariant；
// cooldown/windup 由 ZombieSpecialSystem 维护，windup 同时供 HUD/AI 表达可读预警。
// serial 在每次成功嚎叫/破门时递增，表现层可据此去重音效与提示。
enum class ZombieSpecialKind : std::uint8_t {
    None = 0,
    Howler = 1,
    Breaker = 2,
};

struct ZombieSpecial {
    ZombieSpecialKind kind = ZombieSpecialKind::None;
    float cooldown = 0.0f;
    float windup = 0.0f;
    float windupDuration = 0.0f;
    std::uint32_t serial = 0;
};

struct SiegeAssaultTarget {
    float x = 0.0f;
    float y = 0.0f;
    bool active = false;
};

}  // namespace zl::ecs
