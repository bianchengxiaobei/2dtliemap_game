#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "core/SoundEvent.h"

namespace zl::ecs {
class EcsWorld;
struct Facing;
}  // namespace zl::ecs

namespace zl::ai {

// M5-T3：感知参数（GameApp 从 config perception.* 注入）
struct PerceptionParams {
    float sightRadius = 12.0f;       // 视距（格）
    float closeRadius = 2.5f;        // 近距全向感知半径（格；无视朝向）
    float fovDegrees = 110.0f;       // 视锥总张角（以 Facing 四向为轴）
    float alertDecaySeconds = 8.0f;  // 警觉度 1 → 0 的衰减时长（秒）
    float soundAlertBoost = 0.6f;    // 每声源警觉增量
};

// M5-T3：感知系统。固定链（docs/00 §6.1）：Input → Perception → Ai → PathFollow → …，
// 先于 AiSystem 执行 —— 本 tick 产出的 targetId / lastKnown 同 tick 被 AI 消费。
//
// 视觉：距离 + 视锥（近距全向 + 朝向锥）+ 视线粗判（沿视线 0.5 格步进查阻挡；
//       "未加载 = 阻挡"语义与寻路一致）；
// 听觉：PushSound 入队（M5-T4 由事件总线 SoundEvent 投递进来），距离 ≤ radius 判定可闻；
// 输出：AiState.targetId（当前可见目标实体）、PerceptionState（警觉/最后已知/计时）。
class PerceptionSystem {
public:
    using WalkableQuery = std::function<bool(int, int)>;

    struct Stats {
        int perceivers = 0;  // 带感知组件的实体数
        int seeing = 0;      // 当前有可见目标的
        int alerted = 0;     // alertness > 0.5 的
    };

    PerceptionSystem(WalkableQuery walkable, PerceptionParams params = {})
        : m_walkable(std::move(walkable)), m_params(params) {}

    // 声音输入（主线程；事件总线回调 / 脚本 / 测试直调）。本 tick 内入队，Update 时消费。
    void PushSound(const core::SoundEvent& sound);

    void Update(ecs::EcsWorld& world, float dt);
    const Stats& GetStats() const { return m_stats; }

private:
    // 视线粗判：沿线每 0.5 格采样（不含端点），任一格不可走 → 遮挡
    bool LineOfSight(float px, float py, float tx, float ty) const;
    // 单个玩家的完整可见判定：距离 → （近距全向 | 视锥）→ 视线
    bool CanSee(const ecs::Facing* facing, float px, float py, float tx, float ty) const;

    WalkableQuery m_walkable;
    PerceptionParams m_params;
    std::vector<core::SoundEvent> m_pendingSounds;  // 本 tick 待消费
    Stats m_stats;
};

}  // namespace zl::ai
