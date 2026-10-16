#include "game/AssetProvider.h"

#include <fstream>

#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "ecs/components/CommonComponents.h"

namespace zl::game {
namespace {

// 从 manifest 的某个 provider 条目解析帧列表；失败返回空
std::vector<AssetProvider::Entry> ResolveBinding(const nlohmann::json& binding,
                                                 render::AtlasManager& atlases) {
    std::vector<AssetProvider::Entry> entries;
    const std::string atlasPath = binding.value("atlas", "");
    if (atlasPath.empty()) {
        return entries;
    }
    render::Atlas* atlas = atlases.Load(atlasPath);
    if (atlas == nullptr) {
        return entries;
    }
    const auto framesIt = binding.find("frames");
    if (framesIt == binding.end() || !framesIt->is_array()) {
        return entries;
    }
    for (const auto& frameName : *framesIt) {
        const render::AtlasFrame* frame = atlas->Find(frameName.get<std::string>());
        if (frame == nullptr) {
            ZL_WARN("assets", "manifest 帧不存在: {} <- {}", frameName.get<std::string>(), atlasPath);
            continue;
        }
        entries.push_back({atlas, frame});
    }
    return entries;
}

}  // namespace

bool AssetProvider::Init(const std::string& manifestPath, const std::string& provider,
                         render::AtlasManager& atlases) {
    m_provider = provider;
    m_resolved.clear();
    m_byHash.clear();

    std::ifstream file(manifestPath);
    if (!file.is_open()) {
        ZL_ERROR("assets", "资源清单不存在: {}", manifestPath);
        return false;
    }
    nlohmann::json doc;
    try {
        file >> doc;
    } catch (const nlohmann::json::exception& e) {
        ZL_ERROR("assets", "资源清单解析失败: {} <- {}", e.what(), manifestPath);
        return false;
    }

    const auto logicalIt = doc.find("logical");
    if (logicalIt == doc.end() || !logicalIt->is_object()) {
        ZL_ERROR("assets", "资源清单缺少 logical 段: {}", manifestPath);
        return false;
    }

    int fallbackCount = 0;
    for (const auto& [logicalName, providers] : logicalIt->items()) {
        std::vector<Entry> entries;
        const auto chosenIt = providers.find(m_provider);
        if (chosenIt != providers.end()) {
            entries = ResolveBinding(*chosenIt, atlases);
        }
        if (entries.empty() && m_provider != "own") {
            const auto ownIt = providers.find("own");
            if (ownIt != providers.end()) {
                entries = ResolveBinding(*ownIt, atlases);
                if (!entries.empty()) {
                    ++fallbackCount;
                    ZL_DEBUG("assets", "资源回退 own: {}（provider {} 无绑定或加载失败）",
                             logicalName, m_provider);
                }
            }
        }
        if (entries.empty()) {
            ZL_ERROR("assets", "逻辑资源解析失败: {}（provider={}）", logicalName, m_provider);
        } else {
            ZL_DEBUG("assets", "逻辑资源: {} -> {} 帧", logicalName, entries.size());
        }
        m_resolved.emplace(logicalName, std::move(entries));
    }

    // 哈希反查表（M4-T2：ECS SpriteRef 只存 HashName(逻辑名)）
    for (const auto& [logicalName, entries] : m_resolved) {
        const std::uint32_t hash = ecs::HashName(logicalName);
        const auto [it, inserted] = m_byHash.emplace(hash, &entries);
        if (!inserted && it->second != &entries) {
            ZL_WARN("assets", "资源哈希碰撞（FNV-1a 32）: {} 与既有逻辑名同哈希 {:#x}", logicalName,
                    hash);
        }
    }

    ZL_INFO("assets", "AssetProvider 就绪：provider={}，逻辑资源 {} 项（own 回退 {} 项）", m_provider,
            m_resolved.size(), fallbackCount);
    return true;
}

bool AssetProvider::Has(std::string_view logical) const {
    const auto it = m_resolved.find(std::string(logical));
    return it != m_resolved.end() && !it->second.empty();
}

const std::vector<AssetProvider::Entry>* AssetProvider::Frames(std::string_view logical) const {
    const auto it = m_resolved.find(std::string(logical));
    return it != m_resolved.end() ? &it->second : nullptr;
}

AssetProvider::Entry AssetProvider::Pick(std::string_view logical, std::mt19937& rng) const {
    const std::vector<Entry>* frames = Frames(logical);
    if (frames == nullptr || frames->empty()) {
        return {};
    }
    std::uniform_int_distribution<size_t> dist(0, frames->size() - 1);
    return (*frames)[dist(rng)];
}

}  // namespace zl::game
