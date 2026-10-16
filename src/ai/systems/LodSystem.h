#pragma once

#include <cstdint>

namespace zl::ecs {
class EcsWorld;
}  // namespace zl::ecs

namespace zl::ai {

// M5-T5：LOD 档位系统。按"实体到焦点（相机中心）距离"每 tick 写 LodState：
//   全速（≤ fullRadius）：每 tick 全量更新；
//   30Hz（≤ midRadius）：每 2 tick 更新一次，dtScale=2；
//   10Hz（> midRadius）：每 6 tick 更新一次，dtScale=6。
// 门控方（消费端）：PerceptionSystem 的视觉、AiSystem 转移、PathFollowSystem 提交/跟随、
// FlockSystem —— 均以 `LodState.updateThisTick` 判定；无 LodState 的实体永远全量更新。
class LodSystem {
public:
    struct Params {
        float fullRadius = 25.0f;  // 全速半径（格）
        float midRadius = 60.0f;   // 30Hz 半径（格）；更远 = 10Hz
    };

    struct Stats {
        int fullCount = 0;  // 本帧全速档实体数
        int midCount = 0;   // 30Hz 档
        int farCount = 0;   // 10Hz 档（注意：字段名不能用 far —— windef.h 宏）
        int updated = 0;    // 本 tick 实际执行"重"逻辑的实体数
    };

    LodSystem(Params params = {}) : m_params(params) {}

    void SetFocus(float x, float y) {
        m_focusX = x;
        m_focusY = y;
    }

    void Update(ecs::EcsWorld& world);
    const Stats& GetStats() const { return m_stats; }

private:
    Params m_params;
    float m_focusX = 0.0f;
    float m_focusY = 0.0f;
    std::uint32_t m_tick = 0;  // 自增 tick（Update 次数）
    Stats m_stats;
};

}  // namespace zl::ai
