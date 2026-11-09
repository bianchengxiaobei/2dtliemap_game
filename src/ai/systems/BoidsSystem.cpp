#include "ai/systems/BoidsSystem.h"

#include <algorithm>
#include <cmath>

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"

namespace zl::ai {
namespace {

// 精确重叠时的成对确定性推开方向：方向由无序对 (min,max) 的哈希决定，
// 再按 id 大小取反 → 一对实体的推开方向严格相反（不依赖遍历顺序）。
void OverlapDirection(std::uint64_t lo, std::uint64_t hi, std::uint64_t selfId, float* dx,
                      float* dy) {
    std::uint64_t h = lo * 0x9E3779B97F4A7C15ull + hi * 0xBF58476D1CE4E5B9ull +
                      0x94D049BB133111EBull;
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    const float angle = static_cast<float>(h & 0xFFFFFFu) * (6.2831853f / 16777216.0f);
    float dirX = std::cos(angle);
    float dirY = std::sin(angle);
    if (selfId > lo) {  // 对中较大 id 一方取反 → 两边严格相背
        dirX = -dirX;
        dirY = -dirY;
    }
    *dx = dirX;
    *dy = dirY;
}

void NormalizeOr(float x, float y, float* outX, float* outY) {
    const float len = std::sqrt(x * x + y * y);
    if (len > 0.0001f) {
        *outX = x / len;
        *outY = y / len;
    } else {
        *outX = 0.0f;
        *outY = 0.0f;
    }
}

}  // namespace

void BoidsSystem::Update(ecs::EcsWorld& world) {
    entt::registry& registry = world.Registry();
    m_stats = Stats{};

    // 1) 候选收集（一次组件遍历：位置快照 + 写入指针 + 速度上限）
    m_candidates.clear();
    registry.view<ecs::AiState, ecs::Position, ecs::MoveIntent>().each(
        [&](const ecs::Entity e, const ecs::AiState& ai, const ecs::Position& position,
            ecs::MoveIntent& intent) {
            if (ai.status == ecs::AiStatus::Dead) {
                return;  // continue：尸体不参与
            }
            if (const auto* lod = registry.try_get<ecs::LodState>(e);
                lod != nullptr && !lod->updateThisTick) {
                return;  // LOD 跳过（与感知/AI 同口径）
            }
            Candidate candidate;
            candidate.x = position.x;
            candidate.y = position.y;
            candidate.id = world.IdOf(e).id;
            candidate.intent = &intent;
            candidate.velocity = registry.try_get<ecs::Velocity>(e);
            if (const auto* follow = registry.try_get<ecs::PathFollow>(e);
                follow != nullptr && follow->speed > 0.0f) {
                candidate.maxSpeed = follow->speed;
            } else {
                candidate.maxSpeed = m_params.fallbackMaxSpeed;
            }
            if (const auto* burning = registry.try_get<ecs::BurningZombie>(e);
                burning != nullptr && burning->panicRemaining > 0.0f) {
                candidate.maxSpeed *= std::clamp(burning->panicSpeedScale,
                                                 0.5f, 3.0f);
            }
            m_candidates.push_back(candidate);
        });
    if (m_candidates.empty()) {
        return;
    }

    // 2) 均匀网格（格边长 = 感知半径 → 3×3 格必覆盖）；清桶保留容量
    for (auto& [key, bucket] : m_grid) {
        (void)key;
        bucket.clear();
    }
    const float cell = std::max(0.1f, m_params.neighborRadius);
    for (std::uint32_t i = 0; i < m_candidates.size(); ++i) {
        const Candidate& c = m_candidates[i];
        m_grid[PackCell(static_cast<int>(std::floor(c.x / cell)),
                        static_cast<int>(std::floor(c.y / cell)))]
            .push_back(i);
    }

    // 3) 邻域计算（3×3 格；纯数组运算，无 registry 查询）
    const float sepSq = m_params.separationRadius * m_params.separationRadius;
    const float neighSq = m_params.neighborRadius * m_params.neighborRadius;

    for (std::uint32_t i = 0; i < m_candidates.size(); ++i) {
        Candidate& self = m_candidates[i];
        ++m_stats.zombies;

        const int cellX = static_cast<int>(std::floor(self.x / cell));
        const int cellY = static_cast<int>(std::floor(self.y / cell));

        float sepX = 0.0f;
        float sepY = 0.0f;
        float cohX = 0.0f;
        float cohY = 0.0f;
        int cohCount = 0;
        float aliX = 0.0f;
        float aliY = 0.0f;
        int aliCount = 0;
        bool separated = false;

        for (int gy = cellY - 1; gy <= cellY + 1; ++gy) {
            for (int gx = cellX - 1; gx <= cellX + 1; ++gx) {
                const auto it = m_grid.find(PackCell(gx, gy));
                if (it == m_grid.end()) {
                    continue;
                }
                for (const std::uint32_t j : it->second) {
                    if (j == i) {
                        continue;
                    }
                    const Candidate& other = m_candidates[j];
                    const float dx = other.x - self.x;
                    const float dy = other.y - self.y;
                    const float distSq = dx * dx + dy * dy;
                    if (distSq <= sepSq) {
                        // 分离：方向 = 远离对方；近距线性加权（越近越强）
                        const float dist = std::sqrt(distSq);
                        float pushX = 0.0f;
                        float pushY = 0.0f;
                        if (dist > 0.001f) {
                            pushX = -dx / dist;
                            pushY = -dy / dist;
                        } else {
                            const std::uint64_t lo = std::min(self.id, other.id);
                            const std::uint64_t hi = std::max(self.id, other.id);
                            OverlapDirection(lo, hi, self.id, &pushX, &pushY);
                        }
                        const float weight = 1.0f - dist / m_params.separationRadius;
                        sepX += pushX * weight;
                        sepY += pushY * weight;
                        separated = true;
                    }
                    if (distSq <= neighSq) {
                        cohX += dx;
                        cohY += dy;
                        ++cohCount;
                        if (other.velocity != nullptr) {
                            aliX += other.velocity->x;
                            aliY += other.velocity->y;
                            ++aliCount;
                        }
                    }
                }
            }
        }

        const float speedSq = self.intent->x * self.intent->x + self.intent->y * self.intent->y;
        const bool moving = speedSq > 0.0001f;

        float addX = sepX * m_params.separationWeight;
        float addY = sepY * m_params.separationWeight;
        if (moving && cohCount > 0) {
            float dirX = 0.0f;
            float dirY = 0.0f;
            NormalizeOr(cohX, cohY, &dirX, &dirY);  // 朝邻域重心
            addX += dirX * m_params.cohesionWeight;
            addY += dirY * m_params.cohesionWeight;
        }
        if (moving && aliCount > 0) {
            float dirX = 0.0f;
            float dirY = 0.0f;
            NormalizeOr(aliX, aliY, &dirX, &dirY);  // 对齐平均速度方向
            addX += dirX * m_params.alignmentWeight;
            addY += dirY * m_params.alignmentWeight;
        }
        if (addX == 0.0f && addY == 0.0f) {
            continue;
        }

        // 截断到该实体的意图速度上限
        self.intent->x += addX;
        self.intent->y += addY;
        const float len =
            std::sqrt(self.intent->x * self.intent->x + self.intent->y * self.intent->y);
        if (len > self.maxSpeed) {
            self.intent->x = self.intent->x / len * self.maxSpeed;
            self.intent->y = self.intent->y / len * self.maxSpeed;
        }
        ++m_stats.adjusted;
        if (separated) {
            ++m_stats.separated;
        }
    }
}

}  // namespace zl::ai
