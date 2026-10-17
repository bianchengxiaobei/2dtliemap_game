#pragma once

#include <cstdint>
#include <vector>

#include <RmlUi/Core/RenderInterface.h>
#include <bgfx/bgfx.h>

namespace zl::ui {

class RmlBgfxRenderInterface : public Rml::RenderInterface {
public:
    RmlBgfxRenderInterface() = default;
    ~RmlBgfxRenderInterface() override;

    bool Init(bgfx::ViewId view);
    void Shutdown();
    void SetViewport(int width, int height);

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

    Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions,
                                   const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                       Rml::Vector2i source_dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;

    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

    void EnableClipMask(bool enable) override;
    void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry,
                          Rml::Vector2f translation) override;

private:
    struct GeometryData {
        std::vector<Rml::Vertex> vertices;
        std::vector<uint16_t> indices;
    };

    Rml::TextureHandle MakeTexture(const void* data, uint32_t byteSize, int width, int height,
                                   bool pointSampling);
    bgfx::TextureHandle TextureFromHandle(Rml::TextureHandle handle) const;
    void ApplyScissor(bool fullViewport);
    void SubmitGeometry(const Rml::Vertex* vertices, size_t vertexCount,
                        const uint16_t* indices, size_t indexCount, Rml::Vector2f translation,
                        Rml::TextureHandle texture, uint64_t state, uint32_t stencil,
                        bool useScissor);
    void DrawMaskGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                          uint32_t stencil);
    void DrawFullscreenMask(uint8_t reference);
    uint32_t ActiveMaskStencil() const;

    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sampler = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_whiteTexture = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_layout;

    std::vector<GeometryData> m_geometries;
    std::vector<bgfx::TextureHandle> m_textures;

    bgfx::ViewId m_view = 0;
    int m_width = 0;
    int m_height = 0;
    bool m_scissorEnabled = false;
    Rml::Rectanglei m_scissor;
    bool m_clipMaskEnabled = false;
    uint8_t m_maskRef = 0;
    bool m_initialized = false;
};

}  // namespace zl::ui
