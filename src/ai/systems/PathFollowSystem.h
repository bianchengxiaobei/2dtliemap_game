#pragma once

namespace zl::ecs {
class EcsWorld;
}

namespace zl::ai {

class PathfindingService;

// M4-T6：PathRequest / PathFollow 组件驱动（docs/00 §6.1 固定链：
// InputSystem 之后、MovementSystem 之前）。每 tick 三阶段：
//   1) 消费后台完成结果 → 写 PathFollow（Found）或 Failed；
//   2) 处理 newRequest → Submit 后台查询（queryId 记入 PathRequest）；
//   3) 沿路点写 MoveIntent；本地控制实体本 tick 有手动输入（InputSystem 已写
//      非零意图）→ 取消路径（手动优先）。
class PathFollowSystem {
public:
    explicit PathFollowSystem(PathfindingService* service) : m_service(service) {}

    void Update(ecs::EcsWorld& world, float dt);

    // M20-F2：天气对非本地控制实体（僵尸）的轻量移速缩放。
    void SetEnvironmentMoveScale(float scale);

private:
    PathfindingService* m_service = nullptr;
    float m_environmentMoveScale = 1.0f;
};

}  // namespace zl::ai
