#pragma once

#include <cstdint>
#include <vector>

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>

#include "render/IsoMath.h"

namespace zl::render {

// 顶点色打包：内存字节序 R,G,B,A（bgfx Color0 = Uint8Normalized）
inline uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
}

struct SpriteVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    uint32_t abgr = 0xffffffff;
};
static_assert(sizeof(SpriteVertex) == 20, "SpriteVertex 必须是紧凑 20 字节（与 VertexLayout 一致）");

// M2-T3：精灵批渲染。
// - 动态顶点缓冲（一次分配上限，帧内复用，不触发堆分配增长）；
// - 按纹理分批：同纹理连续绘制合并为一次 submit，纹理切换自动 flush；
// - 顶点色 tint/alpha（昼夜调色、高亮用）；
// - 绘制顺序完全由调用方决定（画家算法，排序在游戏侧）。
class SpriteBatch {
public:
    // 单批上限：16384 四边形（65536 顶点，uint16 索引极限）
    static constexpr uint32_t kMaxQuadsPerFlush = 16384;

    bool Init();
    void Shutdown();

    // 帧统计（每帧开始由拥有者调用）
    void FrameBegin();

    // 向指定 bgfx view 开始收集（同一帧内可多次 Begin/End，切换 view）
    void Begin(bgfx::ViewId view);
    void End();

    void DrawQuad(bgfx::TextureHandle texture, float x, float y, float w, float h, float u0,
                  float v0, float u1, float v1, uint32_t color, bool flipX = false);

    // 便捷：按图集帧参数直接绘制（内部走 SpriteDrawRect 定位规则）
    void DrawSprite(bgfx::TextureHandle texture, const SpriteRect& rect, float u0, float v0,
                    float u1, float v1, uint32_t color, bool flipX = false);

    // 调试线（白 1x1 纹理；用于网格线/描边/坐标轴）
    void DrawLine(const glm::vec2& p0, const glm::vec2& p1, float thickness, uint32_t color);
    // 调试矩形填充（轴对齐，白纹理）
    void DrawRectFill(float x, float y, float w, float h, uint32_t color);

    bgfx::TextureHandle WhiteTexture() const { return m_whiteTexture; }
    bgfx::TextureHandle SoftParticleTexture() const {
        return m_softParticleTexture;
    }

    uint32_t QuadsThisFrame() const { return m_frameQuads; }
    uint32_t DrawCallsThisFrame() const { return m_frameDrawCalls; }

private:
    void Flush();
    void PushQuad(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2,
                  const glm::vec2& p3, float u0, float v0, float u1, float v1, uint32_t color);

    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_texSampler = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_layout;
    bgfx::TextureHandle m_whiteTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_softParticleTexture = BGFX_INVALID_HANDLE;

    // 帧内暂存：Flush 时整体灌入 transient 顶点缓冲（bgfx 专为"一帧多次小提交"设计，
    // 避免多次 bgfx::update 同一动态缓冲的语义问题）
    std::vector<SpriteVertex> m_vertices;  // 预留 kMaxQuadsPerFlush*4

    bgfx::ViewId m_view = 0;
    bgfx::TextureHandle m_currentTexture = BGFX_INVALID_HANDLE;
    uint32_t m_quads = 0;
    bool m_transientFailed = false;

    uint32_t m_frameQuads = 0;
    uint32_t m_frameDrawCalls = 0;
};

}  // namespace zl::render
