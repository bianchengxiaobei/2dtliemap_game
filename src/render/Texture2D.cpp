#include "render/Texture2D.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "core/Log.h"

namespace zl::render {

Texture2D::~Texture2D() {
    if (bgfx::isValid(m_handle)) {
        bgfx::destroy(m_handle);
        m_handle = BGFX_INVALID_HANDLE;
    }
}

std::unique_ptr<Texture2D> Texture2D::LoadFromFile(const std::string& path, std::string* error) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        if (error != nullptr) {
            *error = "stbi_load 失败: " + path;
        }
        return nullptr;
    }

    const uint32_t size = static_cast<uint32_t>(width * height * 4);
    const bgfx::Memory* mem = bgfx::copy(pixels, size);
    stbi_image_free(pixels);

    const uint64_t flags = BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    bgfx::TextureHandle handle = bgfx::createTexture2D(
        static_cast<uint16_t>(width), static_cast<uint16_t>(height), false, 1,
        bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE | flags, mem);
    if (!bgfx::isValid(handle)) {
        if (error != nullptr) {
            *error = "bgfx::createTexture2D 失败: " + path;
        }
        return nullptr;
    }

    auto texture = std::unique_ptr<Texture2D>(new Texture2D());
    texture->m_handle = handle;
    texture->m_width = width;
    texture->m_height = height;
    texture->m_path = path;
    return texture;
}

Texture2D* TextureManager::Load(const std::string& path) {
    const auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        return it->second.get();
    }

    std::string error;
    std::unique_ptr<Texture2D> texture = Texture2D::LoadFromFile(path, &error);
    if (texture == nullptr) {
        ZL_ERROR("render", "纹理加载失败: {} ({})", path, error);
    } else {
        ZL_DEBUG("render", "纹理已加载: {} ({}x{})", path, texture->Width(), texture->Height());
    }

    // 失败也缓存 nullptr 占位，避免每帧重复报错
    Texture2D* raw = texture.get();
    m_cache.emplace(path, std::move(texture));
    return raw;
}

void TextureManager::Clear() { m_cache.clear(); }

}  // namespace zl::render
