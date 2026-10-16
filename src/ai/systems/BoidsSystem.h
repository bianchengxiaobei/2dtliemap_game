#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace zl::ecs {
class EcsWorld;
struct MoveIntent;
struct Velocity;
}  // namespace zl::ecs

namespace zl::ai {

// M5-T5：Boids 式轻量 flocking（ECS 批量处理的优势点）。
//
// 玩家（非 AiState 实体）不参与。对每个"僵尸"实体在 MoveIntent（速度向量）上叠加：
//   分离：邻域内过近（< separationRadius）的同伴 → 推开（含精确重叠的确定性方向）；
//   聚合/对齐：仅在实体处于"移动中"（意图非零）时生效，避免唤醒原地站立的僵尸；
// 结果按 maxSpeed（PathFollow.speed 或默认值）截断。在固定链中位于
// PathFollow 之后（改写其写的意图）、Movement 之前（意图本 tick 生效）。
// LOD：仅对 updateThisTick 的实体计算（与感知/AI 同口径）。
//
// 邻域查询：每 tick 把候选（位置快照 + 组件指针）收集一遍，再建一张
// "格子边长 = neighborRadius"的均匀哈希网格（3×3 格必覆盖查询半径）。
// 相对直接对 chunk 粒度空间索引逐候选做 EnTT 组件查询（chunk=10 格 vs 查询 2.5 格，
// 稠密场景下候选放大 ~16×，且每候选多次 registry 查询）——实测 500 僵尸 Debug
// 单系统 242ms/tick → 网格缓存后量级下降到亚毫秒（见 devlog M5-T5）。
class BoidsSystem {
public:
    struct Params {
        float neighborRadius = 2.5f;     // 聚合/对齐感知半径（格）；亦为网格格边长
        float separationRadius = 0.8f;   // 分离距离（格）
        float separationWeight = 1.6f;   // 分离权重（速度域）
        float cohesionWeight = 0.5f;     // 聚合权重
        float alignmentWeight = 0.4f;    // 对齐权重
        float fallbackMaxSpeed = 2.0f;   // 无 PathFollow 时的意图速度上限（格/秒）
    };

    struct Stats {
        int zombies = 0;    // 参与计算（未 LOD 跳过）的僵尸数
        int adjusted = 0;   // 意图被本系统改写的次数
        int separated = 0;  // 触发过分离的实体数
    };

    BoidsSystem(Params params = {}) : m_params(params) {}

    void Update(ecs::EcsWorld& world);
    const Stats& GetStats() const { return m_stats; }

private:
    struct Candidate {
        float x = 0.0f;
        float y = 0.0f;
        std::uint64_t id = 0;
        ecs::MoveIntent* intent = nullptr;
        const ecs::Velocity* velocity = nullptr;
        float maxSpeed = 2.0f;
    };
    static std::uint64_t PackCell(int cx, int cy) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32) |
               static_cast<std::uint32_t>(cy);
    }

    Params m_params;
    Stats m_stats;
    std::vector<Candidate> m_candidates;  // 本 tick 候选快照（复用容量）
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> m_grid;  // 复用容量
};

}  // namespace zl::ai
