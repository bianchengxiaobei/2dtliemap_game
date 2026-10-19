#include "ai/systems/PathFollowSystem.h"

#include <cmath>

#include "ai/PathfindingService.h"
#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "core/Log.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"

namespace zl::ai {
namespace {

constexpr const char* kLogModule = "ai";
constexpr float kAiDefaultSpeed = 2.0f;  // 无 LocalControl 实体的跟随速度（格/秒）
// M4-T7：卡住检测窗口——窗口内位移小于阈值 → 从当前位置重寻路
constexpr float kStuckCheckSeconds = 1.2f;
constexpr float kStuckMinMoveSq = 0.2f * 0.2f;  // (格/窗口)²

namespace ecs = zl::ecs;

// 后台结果 → 组件：Found 写路点进入 Following；其余一律 Failed（清路点、清零意图）
void ApplyResult(ecs::EcsWorld& world, const ecs::Entity e, ecs::PathRequest& request,
                 const PathResult& result) {
    entt::registry& registry = world.Registry();
    switch (result.path.outcome) {
        case AStarResult::Outcome::Found: {
            ecs::PathFollow& follow = registry.get_or_emplace<ecs::PathFollow>(e);
            follow.waypoints.clear();
            follow.waypoints.reserve(result.path.cells.size());
            for (const GridCell& cell : result.path.cells) {
                follow.waypoints.push_back(ecs::Waypoint{static_cast<float>(cell.x) + 0.5f,
                                                         static_cast<float>(cell.y) + 0.5f});
            }
            follow.index = 0;
            follow.status = ecs::PathStatus::Following;
            follow.stuckTimer = 0.0f;
            if (const auto* position = registry.try_get<ecs::Position>(e);
                position != nullptr) {
                follow.anchorX = position->x;  // 进度跟踪基准 = 当前位置
                follow.anchorY = position->y;
            }
            if (const auto* control = registry.try_get<ecs::LocalControl>(e)) {
                follow.speed = control->walkSpeed;
            } else {
                follow.speed = kAiDefaultSpeed;
            }
            request.state = ecs::PathStatus::Following;
            ZL_TRACE(kLogModule, "寻路完成：稳定 ID {} | 路点 {} | 展开 {} | 代价 {:.2f} | {:.2f}ms",
                     world.IdOf(e).id, follow.waypoints.size(), result.path.expanded,
                     result.path.cost, result.path.ms);
            break;
        }
        default: {
            request.state = ecs::PathStatus::Failed;
            if (auto* follow = registry.try_get<ecs::PathFollow>(e); follow != nullptr) {
                follow->status = ecs::PathStatus::Failed;
                follow->waypoints.clear();
                follow->index = 0;
            }
            if (auto* intent = registry.try_get<ecs::MoveIntent>(e); intent != nullptr) {
                intent->x = 0.0f;
                intent->y = 0.0f;
            }
            ZL_TRACE(kLogModule, "寻路失败：稳定 ID {} 目标格 ({}, {}) outcome={}",
                     world.IdOf(e).id, request.goalX, request.goalY,
                     static_cast<int>(result.path.outcome));
            break;
        }
    }
}

}  // namespace

void PathFollowSystem::Update(ecs::EcsWorld& world, float dt) {
    entt::registry& registry = world.Registry();

    // 1) 消费后台完成结果（按 queryId 匹配在途请求；实体已销毁/重发则丢弃）
    if (m_service != nullptr) {
        PathResult result;
        while (m_service->TryTakeResult(&result)) {
            registry.view<ecs::PathRequest>().each(
                [&](const ecs::Entity e, ecs::PathRequest& request) {
                    if (request.state != ecs::PathStatus::Pending ||
                        request.queryId != result.queryId) {
                        return;  // continue
                    }
                    if (!world.Alive(e)) {  // view 内句柄恒有效，此处仅为防御
                        return;
                    }
                    ApplyResult(world, e, request, result);
                });
        }
    }

    // 2) 提交新请求（newRequest 由游戏逻辑/调试写入）；LOD 跳过的实体延到其更新 tick
    registry.view<ecs::PathRequest, ecs::Position>().each(
        [&](const ecs::Entity e, ecs::PathRequest& request, const ecs::Position& position) {
            if (const auto* lod = registry.try_get<ecs::LodState>(e);
                lod != nullptr && !lod->updateThisTick) {
                return;
            }
            if (!request.newRequest) {
                return;
            }
            request.newRequest = false;
            if (m_service == nullptr) {
                request.state = ecs::PathStatus::Failed;
                return;
            }
            request.queryId = m_service->Submit(static_cast<int>(std::floor(position.x)),
                                                static_cast<int>(std::floor(position.y)),
                                                request.goalX, request.goalY);
            request.state = ecs::PathStatus::Pending;
        });

    // 3) 沿路点跟随
    registry.view<ecs::PathFollow, ecs::Position>().each(
        [&](const ecs::Entity e, ecs::PathFollow& follow, const ecs::Position& position) {
            // M5-T5：LOD 跳过的 tick 不推进（意图保持，Movement 沿既有速度续行）
            const auto* lod = registry.try_get<ecs::LodState>(e);
            if (lod != nullptr && !lod->updateThisTick) {
                return;
            }
            const float edt = lod != nullptr ? dt * lod->dtScale : dt;

            auto* intent = registry.try_get<ecs::MoveIntent>(e);
            auto* request = registry.try_get<ecs::PathRequest>(e);

            // 手动优先：本地控制实体本 tick 已被 InputSystem 写入非零意图 → 取消路径
            // （覆盖"在路上"与"在途查询"两种状态；输入意图原样保留）
            const bool pendingPath =
                request != nullptr && request->state == ecs::PathStatus::Pending;
            if (intent != nullptr && registry.any_of<ecs::LocalControl>(e) &&
                (intent->x != 0.0f || intent->y != 0.0f) &&
                (follow.status == ecs::PathStatus::Following || pendingPath)) {
                follow.status = ecs::PathStatus::None;
                follow.waypoints.clear();
                follow.index = 0;
                if (request != nullptr) {
                    request->state = ecs::PathStatus::None;
                    request->queryId = 0;
                }
                ZL_TRACE(kLogModule, "手动移动取消寻路（稳定 ID {}）", world.IdOf(e).id);
                return;
            }
            if (follow.status != ecs::PathStatus::Following) {
                return;  // Arrived/Failed/None：不再触碰意图（避免覆盖后续输入）
            }

            // M4-T7：卡住检测——窗口内位移过小 → 标记重寻路（下 tick 走阶段 2 重新提交）
            follow.stuckTimer += edt;
            if (follow.stuckTimer >= kStuckCheckSeconds) {
                const float movedX = position.x - follow.anchorX;
                const float movedY = position.y - follow.anchorY;
                const bool stuck = movedX * movedX + movedY * movedY < kStuckMinMoveSq;
                if (stuck && request != nullptr && !request->newRequest &&
                    request->state != ecs::PathStatus::Pending) {
                    request->newRequest = true;
                    ZL_TRACE(kLogModule, "卡住重寻路：稳定 ID {} 目标格 ({}, {})",
                             world.IdOf(e).id, request->goalX, request->goalY);
                }
                follow.anchorX = position.x;
                follow.anchorY = position.y;
                follow.stuckTimer = 0.0f;
            }

            // 连续消费已到达的路点（低速大步长时一 tick 可跨多个）
            while (follow.index < follow.waypoints.size()) {
                const ecs::Waypoint& waypoint = follow.waypoints[follow.index];
                const float dx = waypoint.x - position.x;
                const float dy = waypoint.y - position.y;
                if (dx * dx + dy * dy <= follow.reachRadius * follow.reachRadius) {
                    ++follow.index;
                    continue;
                }
                const float length = std::sqrt(dx * dx + dy * dy);
                if (intent != nullptr) {
                    intent->x = dx / length * follow.speed;
                    intent->y = dy / length * follow.speed;
                }
                return;
            }

            // 全部路点走完 = 到达（意图清零一次；后续 tick 不再触碰）
            follow.status = ecs::PathStatus::Arrived;
            follow.waypoints.clear();
            follow.index = 0;
            if (intent != nullptr) {
                intent->x = 0.0f;
                intent->y = 0.0f;
            }
            if (request != nullptr) {
                request->state = ecs::PathStatus::Arrived;
            }
            ZL_TRACE(kLogModule, "寻路到达：稳定 ID {} 终点格 ({}, {})", world.IdOf(e).id,
                     request != nullptr ? request->goalX : 0,
                     request != nullptr ? request->goalY : 0);
        });
}

}  // namespace zl::ai
