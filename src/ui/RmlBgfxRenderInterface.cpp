#include "ui/RmlBgfxRenderInterface.h"

#include <algorithm>
#include <cstring>

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <stb_image.h>

#include "core/Log.h"
#include "render/shaders_generated/fs_sprite.bin.h"
#include "render/shaders_generated/fs_sprite.glsl.bin.h"
#include "render/shaders_generated/vs_sprite.bin.h"
#include "render/shaders_generated/vs_sprite.glsl.bin.h"

namespace zl::ui {
namespace {

static_assert(sizeof(Rml::Vertex) == 20, "RmlUi 顶点必须是紧凑 20 字节");

bgfx::ShaderHandle CreateShaderForBackend(const uint8_t* dx11Data, size_t dx11Size,
                                          const uint8_t* glslData, size_t glslSize,
                                          const char* what) {
    switch (bgfx::getRendererType()) {
        case bgfx::RendererType::Direct3D11:
            return bgfx::createShader(bgfx::copy(dx11Data, static_cast<uint32_t>(dx11Size)));
        case bgfx::RendererType::OpenGL:
            return bgfx::createShader(bgfx::copy(glslData, static_cast<uint32_t>(glslSize)));
        default:
            ZL_ERROR("ui", "{} 着色器不支持当前后端: {}", what,
                     bgfx::getRendererName(bgfx::getRendererType()));
            return BGFX_INVALID_HANDLE;
    }
}

}  // namespace

RmlBgfxRenderInterface::~RmlBgfxRenderInterface() { Shutdown(); }

bool RmlBgfxRenderInterface::Init(bgfx::ViewId view) {
    if (m_initialized) {
        return true;
    }
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
        ZL_ERROR("ui", "UI 着色器程序创建失败");
        return false;
    }
    m_sampler = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);

    m_layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();

    const uint32_t white = 0xffffffff;
    m_whiteTexture = bgfx::createTexture2D(
        1, 1, false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_NONE | BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        bgfx::copy(&white, sizeof(white)));
    if (!bgfx::isValid(m_whiteTexture)) {
        ZL_ERROR("ui", "UI 白纹理创建失败");
        return false;
    }

    m_view = view;
    m_initialized = true;
    ZL_INFO("ui", "RmlUi bgfx 渲染接口初始化完成（view {}）", static_cast<int>(view));
    return true;
}

void RmlBgfxRenderInterface::Shutdown() {
    if (!m_initialized) {
        return;
    }
    for (bgfx::TextureHandle& texture : m_textures) {
        if (bgfx::isValid(texture)) {
            bgfx::destroy(texture);
        }
    }
    m_textures.clear();
    m_geometries.clear();
    if (bgfx::isValid(m_whiteTexture)) {
        bgfx::destroy(m_whiteTexture);
        m_whiteTexture = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_sampler)) {
        bgfx::destroy(m_sampler);
        m_sampler = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_program)) {
        bgfx::destroy(m_program);
        m_program = BGFX_INVALID_HANDLE;
    }
    m_initialized = false;
}

void RmlBgfxRenderInterface::SetViewport(int width, int height) {
    m_width = width;
    m_height = height;
}

Rml::CompiledGeometryHandle RmlBgfxRenderInterface::CompileGeometry(
    Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) {
    if (vertices.empty() || indices.empty()) {
        return 0;
    }
    if (vertices.size() > 65535 || indices.size() > 65535) {
        ZL_WARN("ui", "UI 几何过大，忽略（{} 顶点 / {} 索引）", vertices.size(), indices.size());
        return 0;
    }
    GeometryData data;
    data.vertices.assign(vertices.begin(), vertices.end());
    data.indices.reserve(indices.size());
    for (int index : indices) {
        data.indices.push_back(static_cast<uint16_t>(index));
    }
    m_geometries.push_back(std::move(data));
    return static_cast<Rml::CompiledGeometryHandle>(m_geometries.size());
}

void RmlBgfxRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometry,
                                            Rml::Vector2f translation,
                                            Rml::TextureHandle texture) {
    if (!m_initialized || geometry == 0 || geometry > m_geometries.size()) {
        return;
    }
    const GeometryData& data = m_geometries[geometry - 1];
    if (data.vertices.empty() || data.indices.empty()) {
        return;
    }

    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, static_cast<uint32_t>(data.vertices.size()), m_layout);
    if (tvb.data == nullptr) {
        return;
    }
    auto* dst = reinterpret_cast<Rml::Vertex*>(tvb.data);
    for (size_t i = 0; i < data.vertices.size(); ++i) {
        dst[i] = data.vertices[i];
        dst[i].position.x += translation.x;
        dst[i].position.y += translation.y;
    }

    bgfx::TransientIndexBuffer tib;
    bgfx::allocTransientIndexBuffer(&tib, static_cast<uint32_t>(data.indices.size()));
    if (tib.data == nullptr) {
        return;
    }
    std::memcpy(tib.data, data.indices.data(), data.indices.size() * sizeof(uint16_t));

    if (m_scissorEnabled) {
        const int x0 = std::clamp(m_scissor.Left(), 0, m_width);
        const int y0 = std::clamp(m_scissor.Top(), 0, m_height);
        const int x1 = std::clamp(m_scissor.Right(), 0, m_width);
        const int y1 = std::clamp(m_scissor.Bottom(), 0, m_height);
        const uint16_t sx = static_cast<uint16_t>(x0);
        const uint16_t sy = static_cast<uint16_t>(y0);
        const uint16_t sw = static_cast<uint16_t>(std::max(x1 - x0, 0));
        const uint16_t sh = static_cast<uint16_t>(std::max(y1 - y0, 0));
        bgfx::setScissor(sx, sy, sw, sh);
    } else {
        bgfx::setScissor(0, 0, static_cast<uint16_t>(std::max(m_width, 0)),
                         static_cast<uint16_t>(std::max(m_height, 0)));
    }

    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                   BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ONE,
                                                  BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                                  BGFX_STATE_BLEND_ONE,
                                                  BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::TextureHandle tex = TextureFromHandle(texture);
    if (!bgfx::isValid(tex)) {
        tex = m_whiteTexture;
    }
    bgfx::setTexture(0, m_sampler, tex);
    bgfx::submit(m_view, m_program);
}

void RmlBgfxRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
    if (geometry > 0 && geometry <= m_geometries.size()) {
        m_geometries[geometry - 1] = GeometryData{};
    }
}

Rml::TextureHandle RmlBgfxRenderInterface::LoadTexture(Rml::Vector2i& texture_dimensions,
                                                       const Rml::String& source) {
    Rml::String data;
    if (Rml::GetFileInterface() == nullptr ||
        !Rml::GetFileInterface()->LoadFile(source, data)) {
        ZL_WARN("ui", "纹理读取失败：{}", source.c_str());
        texture_dimensions = Rml::Vector2i(0, 0);
        return 0;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.data()),
                                            static_cast<int>(data.size()), &width, &height,
                                            &channels, 4);
    if (pixels == nullptr) {
        ZL_WARN("ui", "纹理解码失败：{}", source.c_str());
        texture_dimensions = Rml::Vector2i(0, 0);
        return 0;
    }
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t alpha = pixels[i * 4 + 3];
        pixels[i * 4 + 0] = static_cast<uint8_t>((pixels[i * 4 + 0] * alpha) / 255);
        pixels[i * 4 + 1] = static_cast<uint8_t>((pixels[i * 4 + 1] * alpha) / 255);
        pixels[i * 4 + 2] = static_cast<uint8_t>((pixels[i * 4 + 2] * alpha) / 255);
    }
    texture_dimensions = Rml::Vector2i(width, height);
    const Rml::TextureHandle handle =
        MakeTexture(pixels, static_cast<uint32_t>(pixelCount * 4), width, height, false);
    stbi_image_free(pixels);
    return handle;
}

Rml::TextureHandle RmlBgfxRenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source,
                                                           Rml::Vector2i source_dimensions) {
    if (source.empty() || source_dimensions.x <= 0 || source_dimensions.y <= 0) {
        return 0;
    }
    return MakeTexture(source.data(), static_cast<uint32_t>(source.size()),
                       source_dimensions.x, source_dimensions.y, false);
}

void RmlBgfxRenderInterface::ReleaseTexture(Rml::TextureHandle texture) {
    if (texture == 0 || texture > m_textures.size()) {
        return;
    }
    bgfx::TextureHandle& handle = m_textures[texture - 1];
    if (bgfx::isValid(handle)) {
        bgfx::destroy(handle);
        handle = BGFX_INVALID_HANDLE;
    }
}

void RmlBgfxRenderInterface::EnableScissorRegion(bool enable) { m_scissorEnabled = enable; }

void RmlBgfxRenderInterface::SetScissorRegion(Rml::Rectanglei region) { m_scissor = region; }

Rml::TextureHandle RmlBgfxRenderInterface::MakeTexture(const void* data, uint32_t byteSize,
                                                       int width, int height,
                                                       bool pointSampling) {
    if (width <= 0 || height <= 0 || data == nullptr || byteSize == 0) {
        return 0;
    }
    const uint64_t samplerFlags =
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | (pointSampling ? BGFX_SAMPLER_POINT : 0);
    const bgfx::TextureHandle handle = bgfx::createTexture2D(
        static_cast<uint16_t>(width), static_cast<uint16_t>(height), false, 1,
        bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE | samplerFlags,
        bgfx::copy(data, byteSize));
    if (!bgfx::isValid(handle)) {
        return 0;
    }
    m_textures.push_back(handle);
    return static_cast<Rml::TextureHandle>(m_textures.size());
}

bgfx::TextureHandle RmlBgfxRenderInterface::TextureFromHandle(Rml::TextureHandle handle) const {
    if (handle == 0 || handle > m_textures.size()) {
        return BGFX_INVALID_HANDLE;
    }
    return m_textures[handle - 1];
}

}  // namespace zl::ui
