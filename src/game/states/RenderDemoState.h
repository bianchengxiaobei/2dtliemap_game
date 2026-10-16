#pragma once

#include <cstdint>
#include <vector>

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>

#include "core/AppState.h"
#include "game/AssetProvider.h"
#include "game/GameContext.h"
#include "render/IsoMath.h"

namespace zl::game {

// M2-T6：测试地图渲染（32×32 硬编码格子 + 随机地面 + 树），
// M2-T5 相机（WASD/拖拽平移、滚轮缩放、边界夹取），
// M2-T7 调试叠加（网格线/坐标轴/悬停高亮），M2-T8 资源走 AssetProvider。
// 绘制顺序：稳定排序（z, x+y, x, 格内层序），画家算法。
class RenderDemoState final : public core::AppState {
public:
    explicit RenderDemoState(GameContext& context) : m_ctx(context) {}

    const char* Name() const override { return "RenderDemo"; }

    void OnEnter() override;
    void OnExit() override;
    void OnTick(double tickDt) override;
    void OnFrame(double frameDt, double alpha) override;

private:
    // 格内层序（docs/00 §5.3；M3 再扩全量层）
    enum class Layer : int {
        Floor = 0,
        Decor = 1,
        Object = 2,
    };

    struct DrawItem {
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
        render::SpriteRect rect;
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 0.0f;
        float v1 = 0.0f;
        uint32_t color = 0xffffffff;
        int z = 0;
        int diag = 0;  // x + y
        int gx = 0;
        int layer = 0;
    };

    void BuildMap();
    void AddItem(const AssetProvider::Entry& entry, int gx, int gy, int z, Layer layer);
    void HandleFrameInput(double frameDt);
    void DrawWorld();
    void DrawDebugOverlay();
    void DrawDebugText();
    glm::vec2 MouseToFramebuffer(const glm::vec2& mousePx) const;
    render::GridPick PickMouseCell(const glm::vec2& mousePx) const;

    GameContext& m_ctx;

    std::vector<DrawItem> m_items;  // 已按画家顺序排序
    int m_mapW = 32;
    int m_mapH = 32;
    int m_treeCount = 0;

    int m_hoverX = -1;
    int m_hoverY = -1;
    bool m_hoverHit = false;
    bool m_showGrid = true;

    glm::vec2 m_prevMouse{0.0f, 0.0f};
    bool m_prevMouseValid = false;
    float m_scrollAccum = 0.0f;
    float m_statsTimer = 0.0f;

    uint64_t m_ticksInState = 0;
};

}  // namespace zl::game
