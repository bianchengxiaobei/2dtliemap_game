#include "render/Atlas.h"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "core/Log.h"

namespace zl::render {

std::unique_ptr<Atlas> Atlas::LoadFromJson(const std::string& jsonPath, TextureManager& textures,
                                           std::string* error) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        if (error != nullptr) {
            *error = "无法打开图集 JSON: " + jsonPath;
        }
        return nullptr;
    }

    nlohmann::json doc;
    try {
        file >> doc;
    } catch (const nlohmann::json::exception& e) {
        if (error != nullptr) {
            *error = std::string("图集 JSON 解析失败: ") + e.what() + " <- " + jsonPath;
        }
        return nullptr;
    }

    const std::string format = doc.value("format", "");
    if (format != "pz-atlas-v1") {
        if (error != nullptr) {
            *error = "不支持的图集格式: \"" + format + "\" <- " + jsonPath;
        }
        return nullptr;
    }

    const std::string imageName = doc.value("image", "");
    if (imageName.empty()) {
        if (error != nullptr) {
            *error = "图集缺少 image 字段: " + jsonPath;
        }
        return nullptr;
    }

    const std::filesystem::path imagePath = std::filesystem::path(jsonPath).parent_path() / imageName;
    Texture2D* texture = textures.Load(imagePath.string());
    if (texture == nullptr) {
        if (error != nullptr) {
            *error = "图集纹理加载失败: " + imagePath.string();
        }
        return nullptr;
    }

    auto atlas = std::unique_ptr<Atlas>(new Atlas());
    atlas->m_jsonPath = jsonPath;
    atlas->m_texture = texture;

    const auto subtexturesIt = doc.find("subtextures");
    if (subtexturesIt == doc.end() || !subtexturesIt->is_object()) {
        if (error != nullptr) {
            *error = "图集缺少 subtextures: " + jsonPath;
        }
        return nullptr;
    }

    const float texW = static_cast<float>(texture->Width());
    const float texH = static_cast<float>(texture->Height());
    for (const auto& [name, value] : subtexturesIt->items()) {
        AtlasFrame frame;
        frame.name = name;
        frame.x = value.value("x", 0);
        frame.y = value.value("y", 0);
        frame.w = value.value("w", 0);
        frame.h = value.value("h", 0);
        frame.ox = value.value("ox", 0);
        frame.oy = value.value("oy", 0);
        frame.fx = value.value("fx", frame.w);
        frame.fy = value.value("fy", frame.h);
        if (frame.w <= 0 || frame.h <= 0) {
            ZL_WARN("render", "图集子图尺寸非法，跳过: {} / {}", jsonPath, name);
            continue;
        }
        frame.u0 = static_cast<float>(frame.x) / texW;
        frame.v0 = static_cast<float>(frame.y) / texH;
        frame.u1 = static_cast<float>(frame.x + frame.w) / texW;
        frame.v1 = static_cast<float>(frame.y + frame.h) / texH;
        atlas->m_frames.emplace(name, std::move(frame));
    }

    ZL_INFO("render", "图集已加载: {} （{} 帧, 纹理 {}x{}）", jsonPath, atlas->m_frames.size(),
            texture->Width(), texture->Height());
    return atlas;
}

const AtlasFrame* Atlas::Find(std::string_view name) const {
    const auto it = m_frames.find(std::string(name));
    return it != m_frames.end() ? &it->second : nullptr;
}

Atlas* AtlasManager::Load(const std::string& jsonPath) {
    const auto it = m_cache.find(jsonPath);
    if (it != m_cache.end()) {
        return it->second.get();
    }

    std::string error;
    std::unique_ptr<Atlas> atlas = Atlas::LoadFromJson(jsonPath, *m_textures, &error);
    if (atlas == nullptr) {
        ZL_ERROR("render", "图集加载失败: {}", error);
    }
    Atlas* raw = atlas.get();
    m_cache.emplace(jsonPath, std::move(atlas));
    return raw;
}

}  // namespace zl::render
