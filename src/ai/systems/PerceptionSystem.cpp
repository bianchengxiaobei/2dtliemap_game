#include "ai/systems/PerceptionSystem.h"

#include <algorithm>
#include <cmath>

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "core/SoundPropagation.h"
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

void PerceptionSystem::SetEnvironmentSightScale(float scale) {
    m_environmentSightScale = std::clamp(std::isfinite(scale) ? scale : 1.0f,
                                          0.25f, 1.5f);
}

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
    const float sightRadius = m_params.sightRadius * m_environmentSightScale;
    if (distSq > sightRadius * sightRadius) {
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

bool PerceptionSystem::HearsSound(const core::SoundEvent& sound, float lx, float ly) const {
    const float dx = sound.x - lx;  // 听者 → 声源
    const float dy = sound.y - ly;
    const float distSq = dx * dx + dy * dy;
    if (sound.radius <= 0.0f || distSq > sound.radius * sound.radius) {
        return false;  // 无衰减已达上限 → 不可闻
    }
    if (!m_walkable) {
        return true;  // 无阻挡查询：不衰减（降级）
    }
    const float dist = std::sqrt(distSq);
    // M11-T4：阻挡格计数与隔墙衰减统一走 core::SoundPropagation（音频系统共用）。
    const int walls = core::CountBlockingCells(lx, ly, sound.x, sound.y, m_walkable);
    return core::IsSoundAudible(sound.radius, dist, walls, m_params.wallAttenuation);
}

void PerceptionSystem::Update(ecs::EcsWorld& world, float dt) {
    entt::registry& registry = world.Registry();
    // 每帧快照：perceivers/seeing/alerted 清点；heard 跨帧累计（HUD 显示总数）
    const int totalHeard = m_stats.heard;
    m_stats = Stats{};
    m_stats.heard = totalHeard;

    const float decayPerSecond = m_params.alertDecaySeconds > 0.0f ? 1.0f / m_params.alertDecaySeconds : 1.0f;
    const float boost = m_params.soundAlertBoost;

    // 视觉目标（玩家）每 tick 收集一次：typically 1 个。
    // M5-T6：倒地/死亡玩家不再是视觉目标（HP<=0）——僵尸应立即失去追击对象
    m_targetsScratch.clear();
    registry.view<ecs::PlayerTag, ecs::Position>().each(
        [&](const entt::entity pe, const ecs::Position& playerPos) {
            const auto* health = registry.try_get<ecs::Health>(pe);
            if (health != nullptr && health->current <= 0.0f) {
                return;  // continue
            }
            m_targetsScratch.push_back(
                VisibleTarget{world.IdOf(pe).id, playerPos.x, playerPos.y});
        });

    registry.view<ecs::AiState, ecs::Position, ecs::PerceptionState>().each(
        [&](const entt::entity e, ecs::AiState& ai, const ecs::Position& position,
            ecs::PerceptionState& percep) {
            ++m_stats.perceivers;
            // M5-T5：LOD 门控——视觉按档位跳过（计时用 dt×dtScale 补齐）；
            // 听觉与警觉衰减一律逐 tick（声源队列逐 tick 即弃，门控会漏听）
            const auto* lod = registry.try_get<ecs::LodState>(e);
            const bool gated = lod != nullptr && !lod->updateThisTick;
            const float visionDt = gated ? 0.0f : (lod != nullptr ? dt * lod->dtScale : dt);

            percep.sinceHeard += dt;
            percep.alertness = std::max(0.0f, percep.alertness - decayPerSecond * dt);
            percep.sinceSeen += visionDt;

            // 1) 视觉：可见玩家（取第一个命中的）；LOD 跳过的 tick 保持既有 targetId
            std::uint64_t seenId = 0;
            float seenX = 0.0f;
            float seenY = 0.0f;
            if (!gated) {
                const auto* facing = registry.try_get<ecs::Facing>(e);
                for (const VisibleTarget& target : m_targetsScratch) {
                    if (CanSee(facing, position.x, position.y, target.x, target.y)) {
                        seenId = target.id;
                        seenX = target.x;
                        seenY = target.y;
                        break;
                    }
                }
                if (seenId != 0) {
                    ai.targetId = seenId;
                    percep.lastKnownX = seenX;
                    percep.lastKnownY = seenY;
                    percep.alertness = 1.0f;
                    percep.sinceSeen = 0.0f;
                } else if (ai.targetId != 0) {
                    ai.targetId = 0;  // 丢失当前视觉（lastKnown 保留 → AI 去搜索）
                }
            }

            // 2) 听觉（M5-T4）：本 tick 入队的声源 —— 基础半径 + 隔墙衰减判定可闻
            for (const core::SoundEvent& sound : m_pendingSounds) {
                if (!HearsSound(sound, position.x, position.y)) {
                    continue;
                }
                ++m_stats.heard;
                percep.alertness = std::min(1.0f, percep.alertness + boost);
                percep.sinceHeard = 0.0f;
                if (seenId == 0) {
                    percep.lastKnownX = sound.x;  // 听得见但看不见：以声源为可疑点
                    percep.lastKnownY = sound.y;
                }
            }

            if (ai.targetId != 0) {
                ++m_stats.seeing;  // 含 LOD 冻结 tick（保持既有目标）
            }
            if (percep.alertness > 0.5f) {
                ++m_stats.alerted;
            }
        });

    m_pendingSounds.clear();
}

}  // namespace zl::ai
