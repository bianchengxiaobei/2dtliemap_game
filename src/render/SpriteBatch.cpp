#include "render/SpriteBatch.h"

#include <cstring>

#include "core/Log.h"
#include "render/shaders_generated/fs_sprite.bin.h"
#include "render/shaders_generated/fs_sprite.glsl.bin.h"
#include "render/shaders_generated/vs_sprite.bin.h"
#include "render/shaders_generated/vs_sprite.glsl.bin.h"

namespace zl::render {
namespace {

bgfx::ShaderHandle CreateShaderForBackend(const uint8_t* dx11Data, size_t dx11Size,
                                          const uint8_t* glslData, size_t glslSize,
                                          const char* what) {
    switch (bgfx::getRendererType()) {
        case bgfx::RendererType::Direct3D11:
            return bgfx::createShader(bgfx::copy(dx11Data, static_cast<uint32_t>(dx11Size)));
        case bgfx::RendererType::OpenGL:
            return bgfx::createShader(bgfx::copy(glslData, static_cast<uint32_t>(glslSize)));
        default:
            ZL_ERROR("render", "{} 着色器不支持当前后端: {}", what,
                     bgfx::getRendererName(bgfx::getRendererType()));
            return BGFX_INVALID_HANDLE;
    }
}

}  // namespace

bool SpriteBatch::Init() {
    const bgfx::ShaderHandle vs =
        CreateShaderForBackend(_vs_sprite_dx11, sizeof(_vs_sprite_dx11), _vs_sprite_glsl,
                               sizeof(_vs_sprite_glsl), "vs_sprite");
    const bgfx::ShaderHandle fs =
        CreateShaderForBackend(_fs_sprite_dx11, sizeof(_fs_sprite_dx11), _fs_sprite_glsl,
                               sizeof(_fs_sprite_glsl), "fs_sprite");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        return false;
    }
    m_program = bgfx::createProgram(vs, fs, true);
    if (!bgfx::isValid(m_program)) {
        ZL_ERROR("render", "sprite 着色器程序创建失败");
        return false;
    }
    m_texSampler = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);

    m_layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    m_vertices.resize(static_cast<size_t>(kMaxQuadsPerFlush) * 4);

    const uint32_t whitePixel = 0xffffffff;
    m_whiteTexture = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                           BGFX_TEXTURE_NONE | BGFX_SAMPLER_POINT |
                                               BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                                           bgfx::copy(&whitePixel, sizeof(whitePixel)));

    const bool ok = bgfx::isValid(m_whiteTexture);
    ZL_INFO("render", "SpriteBatch 初始化{}（单批上限 {} 四边形）", ok ? "完成" : "失败",
            kMaxQuadsPerFlush);
    return ok;
}

void SpriteBatch::Shutdown() {
    if (bgfx::isValid(m_whiteTexture)) {
        bgfx::destroy(m_whiteTexture);
        m_whiteTexture = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_program)) {
        bgfx::destroy(m_program);
        m_program = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_texSampler)) {
        bgfx::destroy(m_texSampler);
        m_texSampler = BGFX_INVALID_HANDLE;
    }
}

void SpriteBatch::FrameBegin() {
    m_frameQuads = 0;
    m_frameDrawCalls = 0;
}

void SpriteBatch::Begin(bgfx::ViewId view) {
    m_view = view;
    m_quads = 0;
    m_currentTexture = BGFX_INVALID_HANDLE;
}

void SpriteBatch::End() { Flush(); }

void SpriteBatch::PushQuad(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2,
                           const glm::vec2& p3, float u0, float v0, float u1, float v1,
                           uint32_t color) {
    SpriteVertex* verts = m_vertices.data() + static_cast<size_t>(m_quads) * 4;
    verts[0] = {p0.x, p0.y, u0, v0, color};
    verts[1] = {p1.x, p1.y, u1, v0, color};
    verts[2] = {p2.x, p2.y, u0, v1, color};
    verts[3] = {p3.x, p3.y, u1, v1, color};
    ++m_quads;
}

void SpriteBatch::DrawQuad(bgfx::TextureHandle texture, float x, float y, float w, float h,
                           float u0, float v0, float u1, float v1, uint32_t color, bool flipX) {
    if (m_quads >= kMaxQuadsPerFlush || (m_quads > 0 && texture.idx != m_currentTexture.idx)) {
        Flush();
    }
    m_currentTexture = texture;
    if (flipX) {
        const float t = u0;
        u0 = u1;
        u1 = t;
    }
    PushQuad({x, y}, {x + w, y}, {x, y + h}, {x + w, y + h}, u0, v0, u1, v1, color);
}

void SpriteBatch::DrawSprite(bgfx::TextureHandle texture, const SpriteRect& rect, float u0,
                             float v0, float u1, float v1, uint32_t color, bool flipX) {
    DrawQuad(texture, rect.x, rect.y, rect.w, rect.h, u0, v0, u1, v1, color, flipX);
}

void SpriteBatch::DrawLine(const glm::vec2& p0, const glm::vec2& p1, float thickness,
                           uint32_t color) {
    glm::vec2 d = p1 - p0;
    const float len = glm::length(d);
    if (len < 0.001f) {
        return;
    }
    d /= len;
    const glm::vec2 n(-d.y * thickness * 0.5f, d.x * thickness * 0.5f);
    // 采样白纹理中心
    const float c = 0.5f;
    if (m_quads >= kMaxQuadsPerFlush || (m_quads > 0 && m_whiteTexture.idx != m_currentTexture.idx)) {
        Flush();
    }
    m_currentTexture = m_whiteTexture;
    PushQuad(p0 - n, p0 + n, p1 - n, p1 + n, c, c, c, c, color);
}

void SpriteBatch::DrawRectFill(float x, float y, float w, float h, uint32_t color) {
    DrawQuad(m_whiteTexture, x, y, w, h, 0.5f, 0.5f, 0.5f, 0.5f, color);
}

void SpriteBatch::Flush() {
    if (m_quads == 0 || !bgfx::isValid(m_currentTexture)) {
        return;
    }
    const uint32_t vertexCount = m_quads * 4;
    const uint32_t indexCount = m_quads * 6;

    if (bgfx::getAvailTransientVertexBuffer(vertexCount, m_layout) < vertexCount ||
        bgfx::getAvailTransientIndexBuffer(indexCount) < indexCount) {
        if (!m_transientFailed) {
            m_transientFailed = true;
            ZL_WARN("render", "transient 缓冲不足，本帧部分精灵未绘制（配额 {} 顶点 / {} 索引）",
                    vertexCount, indexCount);
        }
        m_quads = 0;
        return;
    }

    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, vertexCount, m_layout);
    std::memcpy(tvb.data, m_vertices.data(),
                static_cast<size_t>(vertexCount) * sizeof(SpriteVertex));

    bgfx::TransientIndexBuffer tib;
    bgfx::allocTransientIndexBuffer(&tib, indexCount);
    auto* indices = reinterpret_cast<uint16_t*>(tib.data);
    for (uint32_t q = 0; q < m_quads; ++q) {
        const uint16_t base = static_cast<uint16_t>(q * 4);
        uint16_t* dst = indices + static_cast<size_t>(q) * 6;
        dst[0] = base;
        dst[1] = static_cast<uint16_t>(base + 1);
        dst[2] = static_cast<uint16_t>(base + 2);
        dst[3] = static_cast<uint16_t>(base + 2);
        dst[4] = static_cast<uint16_t>(base + 1);
        dst[5] = static_cast<uint16_t>(base + 3);
    }

    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                   BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::setTexture(0, m_texSampler, m_currentTexture);
    bgfx::submit(m_view, m_program);

    m_frameQuads += m_quads;
    ++m_frameDrawCalls;
    m_quads = 0;
}

}  // namespace zl::render
