#pragma once

#include <string>
#include <unordered_map>
#include <memory>

#include <bgfx/bgfx.h>

namespace zl::render {

// M2-T2：单张 2D 纹理（RAII 持有 bgfx::TextureHandle）。
// 像素风采样规范：POINT 采样 + U/V CLAMP（防止图集边缘渗色）。
class Texture2D {
public:
    Texture2D() = default;
    ~Texture2D();

    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;

    // 从 PNG/JPEG 等文件加载（stb_image）；失败返回 nullptr 并填充 error
    static std::unique_ptr<Texture2D> LoadFromFile(const std::string& path, std::string* error);

    bgfx::TextureHandle Handle() const { return m_handle; }
    int Width() const { return m_width; }
    int Height() const { return m_height; }
    const std::string& Path() const { return m_path; }

private:
    bgfx::TextureHandle m_handle = BGFX_INVALID_HANDLE;
    int m_width = 0;
    int m_height = 0;
    std::string m_path;
};

// 按路径缓存的纹理仓库（句柄生命周期由本类统一管理）
class TextureManager {
public:
    Texture2D* Load(const std::string& path);  // 失败返回 nullptr（缓存失败结果以避免反复报错）
    void Clear();

private:
    std::unordered_map<std::string, std::unique_ptr<Texture2D>> m_cache;
};

}  // namespace zl::render
