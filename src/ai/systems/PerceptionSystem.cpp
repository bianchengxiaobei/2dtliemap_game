#include "ai/systems/PerceptionSystem.h"

#include <algorithm>
#include <cmath>

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"

namespace zl::ai {
namespace {

using ecs::Direction;
using ecs::Facing;

// Facing（屏幕语义 4 向）→ 世界格前进方向（与 AnimationSystem 的格轴判定一致）
void FacingGridDir(Direction dir, float* fx, float* fy) {
    switch (dir) {
        case Direction::Down:
            *fx = 0.0f;
            *fy = 1.0f;
            break;
        case Direction::Up:
            *fx = 0.0f;
            *fy = -1.0f;
            break;
        case Direction::Left:
            *fx = -1.0f;
            *fy = 0.0f;
            break;
        case Direction::Right:
            *fx = 1.0f;
            *fy = 0.0f;
            break;
    }
}

}  // namespace

void PerceptionSystem::PushSound(const core::SoundEvent& sound) {
    m_pendingSounds.push_back(sound);
}

bool PerceptionSystem::LineOfSight(float px, float py, float tx, float ty) const {
    if (!m_walkable) {
        return true;  // 无阻挡查询：不作视线遮挡（降级）
    }
    const float dx = tx - px;
    const float dy = ty - py;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist <= 0.001f) {
        return true;
    }
    const int steps = std::max(2, static_cast<int>(std::ceil(dist / 0.5f)));
    for (int i = 1; i < steps; ++i) {  // 不含端点（两端必然可走）
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const int wx = static_cast<int>(std::floor(px + dx * t));
        const int wy = static_cast<int>(std::floor(py + dy * t));
        if (!m_walkable(wx, wy)) {
            return false;
        }
    }
    return true;
}

bool PerceptionSystem::CanSee(const Facing* facing, float px, float py, float tx, float ty) const {
    const float dx = tx - px;
    const float dy = ty - py;
    const float distSq = dx * dx + dy * dy;
    if (distSq > m_params.sightRadius * m_params.sightRadius) {
        return false;
    }
    const bool close = distSq <= m_params.closeRadius * m_params.closeRadius;
    if (!close) {
        // 视锥：以 Facing 四向为轴（无 Facing 默认向下，与动画默认一致）
        float fx = 0.0f;
        float fy = 1.0f;
        if (facing != nullptr) {
            FacingGridDir(facing->dir, &fx, &fy);
        }
        const float dist = std::sqrt(distSq);
        if (dist > 0.001f) {
            const float cosAngle = (dx * fx + dy * fy) / dist;
            const float halfRad = m_params.fovDegrees * 0.5f * 0.017453292f;
            if (cosAngle < std::cos(halfRad)) {
                return false;
            }
        }
    }
    return LineOfSight(px, py, tx, ty);
}

void PerceptionSystem::Update(ecs::EcsWorld& world, float dt) {
    entt::registry& registry = world.Registry();
    m_stats = Stats{};

    const float decayPerSecond = m_params.alertDecaySeconds > 0.0f ? 1.0f / m_params.alertDecaySeconds : 1.0f;
    const float boost = m_params.soundAlertBoost;

    registry.view<ecs::AiState, ecs::Position, ecs::PerceptionState>().each(
        [&](const entt::entity e, ecs::AiState& ai, const ecs::Position& position,
            ecs::PerceptionState& percep) {
            ++m_stats.perceivers;
            percep.sinceSeen += dt;
            percep.sinceHeard += dt;
            percep.alertness = std::max(0.0f, percep.alertness - decayPerSecond * dt);

            // 1) 视觉：可见玩家（取第一个命中的）
            const auto* facing = registry.try_get<ecs::Facing>(e);
            std::uint64_t seenId = 0;
            float seenX = 0.0f;
            float seenY = 0.0f;
            registry.view<ecs::PlayerTag, ecs::Position>().each(
                [&](const entt::entity pe, const ecs::Position& playerPos) {
                    if (seenId != 0) {
                        return;  // continue：已命中
                    }
                    if (CanSee(facing, position.x, position.y, playerPos.x, playerPos.y)) {
                        seenId = world.IdOf(pe).id;
                        seenX = playerPos.x;
                        seenY = playerPos.y;
                    }
                });
            if (seenId != 0) {
                ai.targetId = seenId;
                percep.lastKnownX = seenX;
                percep.lastKnownY = seenY;
                percep.alertness = 1.0f;
                percep.sinceSeen = 0.0f;
                ++m_stats.seeing;
            } else if (ai.targetId != 0) {
                ai.targetId = 0;  // 丢失当前视觉（lastKnown 保留 → AI 去搜索）
            }

            // 2) 听觉：本 tick 入队的声源（距离 ≤ radius 可闻）
            for (const core::SoundEvent& sound : m_pendingSounds) {
                const float sdx = position.x - sound.x;
                const float sdy = position.y - sound.y;
                if (sdx * sdx + sdy * sdy > sound.radius * sound.radius) {
                    continue;
                }
                percep.alertness = std::min(1.0f, percep.alertness + boost);
                percep.sinceHeard = 0.0f;
                if (seenId == 0) {
                    percep.lastKnownX = sound.x;  // 听得见但看不见：以声源为可疑点
                    percep.lastKnownY = sound.y;
                }
            }

            if (percep.alertness > 0.5f) {
                ++m_stats.alerted;
            }
        });

    m_pendingSounds.clear();
}

}  // namespace zl::ai
