#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>

#include "render/Texture2D.h"

namespace zl::render {

// M2-T2：图集子图（对应 pz-atlas-v1 的 subtextures 条目）。
// x/y/w/h = 图内矩形；ox/oy = 相对画布枢轴的绘制偏移；fx/fy = 画布原始尺寸。
// 绘制定位规则见 IsoMath::SpriteDrawRect。
struct AtlasFrame {
    std::string name;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int ox = 0;
    int oy = 0;
    int fx = 0;
    int fy = 0;
    float u0 = 0.0f;  // 归一化 uv（预计算）
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

// 一张纹理页 + 其子图集合；JSON 格式与 tools/pz_pack_unpack.py 导出的 pz-atlas-v1 一致。
class Atlas {
public:
    static std::unique_ptr<Atlas> LoadFromJson(const std::string& jsonPath, TextureManager& textures,
                                               std::string* error);

    const AtlasFrame* Find(std::string_view name) const;
    const std::string& JsonPath() const { return m_jsonPath; }
    bgfx::TextureHandle Texture() const {
        if (m_texture != nullptr) {
            return m_texture->Handle();
        }
        return BGFX_INVALID_HANDLE;
    }
    const Texture2D* TextureInfo() const { return m_texture; }
    size_t FrameCount() const { return m_frames.size(); }

private:
    std::string m_jsonPath;
    Texture2D* m_texture = nullptr;  // 由 TextureManager 拥有
    std::unordered_map<std::string, AtlasFrame> m_frames;
};

// 按 JSON 路径缓存的图集仓库
class AtlasManager {
public:
    explicit AtlasManager(TextureManager& textures) : m_textures(&textures) {}

    Atlas* Load(const std::string& jsonPath);  // 失败返回 nullptr
    void Clear() { m_cache.clear(); }

private:
    TextureManager* m_textures = nullptr;
    std::unordered_map<std::string, std::unique_ptr<Atlas>> m_cache;
};

}  // namespace zl::render
