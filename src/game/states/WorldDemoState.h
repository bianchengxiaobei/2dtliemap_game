#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "core/AppState.h"
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
    void UpdateCruise(double frameDt);
    void LogPerfSample(const char* tag);
    void PushObject(const world::SquareObject& object, int wx, int wy, int z,
                    world::SquareLayer layer);
    world::Chunk* FindChunk(int chunkX, int chunkY) const;
    void DrawWorld();
    void DrawDebugOverlay();
    void DrawDebugText();
    glm::vec2 MouseToFramebuffer(const glm::vec2& mousePx) const;
    render::GridPick PickMouseCell(const glm::vec2& mousePx) const;

    GameContext& m_ctx;

    std::vector<render::DrawItem> m_items;
    RenderCollectSystem::ViewRect m_view;  // 本帧可见范围（世界格坐标）
    int m_collectedChunks = 0;  // 本帧进入收集的已加载 chunk 数
    int m_missingChunks = 0;    // 本帧可见但尚未加载的 chunk 数

    // M4-T2 演示脚手架：ECS 实体在盒内漂移（M4-T3 MovementSystem 接管后删除）
    float m_demoBoxX0 = 0.0f;
    float m_demoBoxY0 = 0.0f;
    float m_demoBoxX1 = 0.0f;
    float m_demoBoxY1 = 0.0f;
    int m_demoEntityCount = 0;

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
