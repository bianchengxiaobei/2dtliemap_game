#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "ai/components/AiComponents.h"
#include "core/AppState.h"
#include "ecs/EcsWorld.h"
#include "game/AssetProvider.h"
#include "game/GameContext.h"
#include "game/RenderCollectSystem.h"
#include "render/DrawList.h"
#include "render/IsoMath.h"
#include "world/GridSquare.h"

namespace zl::world {
class Chunk;
}

namespace zl::game {

// M3-T5：世界渲染接入（World + ChunkManager 流式加载 → 逐格收集精灵实例 → SpriteBatch）。
// 收集分两段（对齐 PZ SpriteRenderer 的批处理思路）：
//   0) 地面层（Floor）整批先画：地面块之间互不遮挡，按纹理分批即 1~3 个 drawcall；
//   1) 其余层：全局画家序（z → x+y → x → 格内层序）——墙/树/草丛互相遮挡必须保序。
//   实体高度精灵（M4-T2）由 RenderCollectSystem 追加进同一份列表（render::DrawItem）。
// 本状态只读 ChunkManager 已挂接（Loaded）的 chunk；加载/卸载全在后台+预算内完成。
class WorldDemoState final : public core::AppState {
public:
    explicit WorldDemoState(GameContext& context) : m_ctx(context) {}

    const char* Name() const override { return "WorldDemo"; }

    void OnEnter() override;
    void OnExit() override;
    void OnTick(double tickDt) override;
    void OnFrame(double frameDt, double alpha) override;

private:
    void HandleFrameInput(double frameDt);
    void CollectWorld();
    void SpawnDemoEntities();
    void DespawnDemoEntities();
    // M4-T6：游荡演示——定时给空闲的演示实体派发随机目标（PathRequest → 后台 A*）
    void RetargetDemoEntities();
    bool PickWanderTarget(int* gx, int* gy);
    // M5-T1：演示击杀——离参考点（玩家/相机中心）最近的存活游荡者 HP 归零 → DeathSystem 转尸体
    void KillNearestDemoEntity();
    // M4-T4：玩家（PlayerTag）——chunk 加载完成后在村庄中心附近找可走格生成
    void EnsurePlayerSpawned();
    ecs::Entity SpawnCharacter(float px, float py);
    bool IsSolidSquare(int wx, int wy) const;
    // M4-T7：点击移动——屏幕拾取 → 目标格校验（越界/未加载/就近可走格）→ 写 PathRequest
    void HandleClickToMove(const render::GridPick& pick);
    // M4-T7：寻路反馈——玩家路径状态跃迁（到达/失败）→ HUD 提示 + 目标格标记
    void UpdatePathFeedback(double frameDt);
    void SetNotice(std::string text, uint32_t color, float seconds);
    // M4-T5：相机跟随（follow=平滑跟随玩家+地图边界；自由=方向键/拖拽，无边界）
    void ApplyCameraMode();
    void UpdateCameraFollow(double frameDt);
    void UpdateCruise(double frameDt);
    void LogPerfSample(const char* tag);
    void PushObject(const world::SquareObject& object, int wx, int wy, int z,
                    world::SquareLayer layer);
    world::Chunk* FindChunk(int chunkX, int chunkY) const;
    void DrawWorld();
    void DrawDebugOverlay();
    void DrawPhysicsDebug();
    void DrawDebugText();
    glm::vec2 MouseToFramebuffer(const glm::vec2& mousePx) const;
    render::GridPick PickMouseCell(const glm::vec2& mousePx) const;

    GameContext& m_ctx;

    std::vector<render::DrawItem> m_items;
    RenderCollectSystem::ViewRect m_view;  // 本帧可见范围（世界格坐标）
    int m_collectedChunks = 0;  // 本帧进入收集的已加载 chunk 数
    int m_missingChunks = 0;    // 本帧可见但尚未加载的 chunk 数

    // M4-T6 演示：40 个游荡者（PathRequest 随机目标 → 后台 A* → 路点跟随；
    // 替代 M4-T2/T3 的盒内漂移脚手架）
    float m_demoBoxX0 = 0.0f;
    float m_demoBoxY0 = 0.0f;
    float m_demoBoxX1 = 0.0f;
    float m_demoBoxY1 = 0.0f;
    std::vector<ecs::Entity> m_demoEntities;
    float m_retargetTimer = 0.0f;
    std::mt19937 m_demoRng{20261016u};
    // M4-T4：本地玩家（WASD 控制）
    ecs::Entity m_player = entt::null;
    // M4-T7：点击移动反馈（notice 文本 + 目标格标记）
    std::string m_noticeText;
    float m_noticeTimer = 0.0f;
    uint32_t m_noticeColor = 0x0F;
    int m_markerX = 0;
    int m_markerY = 0;
    float m_markerTimer = 0.0f;
    bool m_markerPathActive = false;  // 寻路中（Pending/Following）：目标标记常显
    uint32_t m_markerColor = 0xFFFFFFFFu;
    ecs::PathStatus m_lastPlayerPathState = ecs::PathStatus::None;
    // M4-T5：相机模式（F4 切换；follow 平滑跟随玩家并夹取到地图边界）
    bool m_followCamera = true;
    bool m_followSnapped = false;  // 下次跟随直接落位（刚切换/生成时不做缓动）
    float m_followRate = 8.0f;     // 指数趋近速率（1/s）
    bool m_physicsDebug = false;  // F10：Bullet 线框
    int m_wireDiagCount = 0;      // 线框诊断日志计数（开启后头几帧）

    glm::vec2 m_prevMouse{0.0f, 0.0f};
    bool m_prevMouseValid = false;
    float m_scrollAccum = 0.0f;
    float m_statsTimer = 0.0f;

    // 自动巡航 + 压测采样（M3-T7）：绕 (cx,cy) 圆周巡航，每 5s 打一条 perf 统计
    bool m_cruiseEnabled = false;
    bool m_cruiseFinished = false;
    double m_cruiseElapsed = 0.0;
    float m_cruiseAngle = 0.0f;
    float m_cruiseSpeed = 30.0f;     // 世界格/秒
    float m_cruiseRadius = 40.0f;    // 世界格
    float m_cruiseCenterX = 500.0f;  // 世界格
    float m_cruiseCenterY = 500.0f;
    double m_cruiseMinutes = 5.0;
    float m_perfTimer = 0.0f;

    int m_hoverX = 0;
    int m_hoverY = 0;
    bool m_hoverHit = true;
    bool m_showGrid = false;  // 世界地图默认关网格（F9 开）

    uint64_t m_ticksInState = 0;
};

}  // namespace zl::game
