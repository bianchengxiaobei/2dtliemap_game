#pragma once

#include <cstdint>
#include <vector>

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>

#include "core/AppState.h"
#include "game/AssetProvider.h"
#include "game/GameContext.h"
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
    struct DrawItem {
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
        render::SpriteRect rect;
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 0.0f;
        float v1 = 0.0f;
        uint32_t color = 0xffffffff;
        int stage = 0;  // 0=地面批，1=对象批
        int z = 0;
        int diag = 0;  // x + y
        int gx = 0;
        int layer = 0;
    };

    void HandleFrameInput(double frameDt);
    void CollectWorld();
    void PushObject(const world::SquareObject& object, int wx, int wy, int z,
                    world::SquareLayer layer);
    world::Chunk* FindChunk(int chunkX, int chunkY) const;
    void DrawWorld();
    void DrawDebugOverlay();
    void DrawDebugText();
    glm::vec2 MouseToFramebuffer(const glm::vec2& mousePx) const;
    render::GridPick PickMouseCell(const glm::vec2& mousePx) const;

    GameContext& m_ctx;

    std::vector<DrawItem> m_items;
    int m_collectedChunks = 0;  // 本帧进入收集的已加载 chunk 数
    int m_missingChunks = 0;    // 本帧可见但尚未加载的 chunk 数

    glm::vec2 m_prevMouse{0.0f, 0.0f};
    bool m_prevMouseValid = false;
    float m_scrollAccum = 0.0f;
    float m_statsTimer = 0.0f;

    int m_hoverX = 0;
    int m_hoverY = 0;
    bool m_hoverHit = true;
    bool m_showGrid = false;  // 世界地图默认关网格（F9 开）

    uint64_t m_ticksInState = 0;
};

}  // namespace zl::game
