#include "game/AssetProvider.h"

#include <filesystem>
#include <utility>

#include <nlohmann/json.hpp>

#include "characters/systems/AnimationSystem.h"
#include "core/Log.h"
#include "ecs/components/CommonComponents.h"
#include "game/AnimationManifest.h"
#include "game/AssetManifest.h"

namespace zl::game {
namespace {

struct ResolvedBinding {
    std::vector<AssetProvider::Entry> entries;
    AssetProvider::AnimationSet animations;
};

ResolvedBinding ResolveBinding(const nlohmann::json& binding,
                               render::AtlasManager& atlases) {
    ResolvedBinding resolved;
    const std::string atlasPath = binding.value("atlas", "");
    if (atlasPath.empty()) {
        return resolved;
    }
    render::Atlas* atlas = atlases.Load(atlasPath);
    if (atlas == nullptr) {
        return resolved;
    }

    const auto framesIt = binding.find("frames");
    if (framesIt != binding.end() && framesIt->is_array()) {
        for (const auto& frameName : *framesIt) {
            const std::string name = frameName.get<std::string>();
            const render::AtlasFrame* frame = atlas->Find(name);
            if (frame == nullptr) {
                ZL_WARN("assets", "manifest 帧不存在: {} <- {}", name, atlasPath);
                continue;
            }
            resolved.entries.push_back({atlas, frame});
        }
    }

    AnimationManifest manifest;
    std::string animationError;
    if (!ParseAnimationManifest(binding, &manifest, &animationError)) {
        ZL_ERROR("assets", "动画 manifest 非法: {} <- {}", animationError, atlasPath);
        return resolved;
    }
    if (manifest.Empty()) {
        return resolved;
    }

    bool complete = true;
    for (std::uint8_t i = 1;
         i < static_cast<std::uint8_t>(ecs::AnimClip::Count) && complete; ++i) {
        const ecs::AnimClip clip = static_cast<ecs::AnimClip>(i);
        const AnimationManifestClip* source = manifest.Find(clip);
        if (source == nullptr) {
            complete = false;
            break;
        }
        auto& target = resolved.animations.clips[i];
        target.framesPerDir = source->framesPerDir;
        target.eventMask = source->eventMask;
        target.event = source->event;
        target.frames.reserve(source->frames.size());
        for (const std::string& frameName : source->frames) {
            const render::AtlasFrame* frame = atlas->Find(frameName);
            if (frame == nullptr) {
                ZL_ERROR("assets", "动画片段帧不存在: {} / {} <- {}",
                         characters::AnimationSystem::ClipName(clip), frameName, atlasPath);
                complete = false;
                break;
            }
            target.frames.push_back({atlas, frame});
        }
    }
    if (!complete) {
        ZL_ERROR("assets", "动画 v2 片段不完整，回退旧帧: {}", atlasPath);
        resolved.animations = AssetProvider::AnimationSet{};
        return resolved;
    }
    for (std::uint8_t i = 1; i < static_cast<std::uint8_t>(ecs::AnimClip::Count); ++i) {
        resolved.animations.present[i] = true;
    }
    return resolved;
}

}  // namespace

bool AssetProvider::Init(const std::string& manifestPath, const std::string& provider,
                         render::AtlasManager& atlases,
                         const std::vector<std::string>& modManifestPaths) {
    m_provider = provider;
    m_resolved.clear();
    m_byHash.clear();
    m_animationSets.clear();
    m_animByHash.clear();
    m_cutaway.clear();
    m_cutawayByHash.clear();
    m_windows.clear();
    m_windowHashes.clear();
    m_foliage.clear();
    m_foliageHashes.clear();

    std::vector<std::filesystem::path> modPaths;
    modPaths.reserve(modManifestPaths.size());
    for (const std::string& path : modManifestPaths) {
        modPaths.emplace_back(path);
    }
    nlohmann::json doc;
    AssetManifestMergeStats mergeStats;
    std::string manifestError;
    if (!LoadMergedAssetManifest(manifestPath, modPaths, &doc, &mergeStats,
                                 &manifestError)) {
        ZL_ERROR("assets", "资源清单加载失败: {}", manifestError);
        return false;
    }

    const auto logicalIt = doc.find("logical");
    if (logicalIt == doc.end() || !logicalIt->is_object()) {
        ZL_ERROR("assets", "资源清单缺少 logical 段: {}", manifestPath);
        return false;
    }

    int fallbackCount = 0;
    int animationCount = 0;
    int cutawayCount = 0;
    int windowCount = 0;
    int foliageCount = 0;
    for (const auto& [logicalName, providers] : logicalIt->items()) {
        ResolvedBinding resolved;
        const auto chosenIt = providers.find(m_provider);
        if (chosenIt != providers.end()) {
            resolved = ResolveBinding(*chosenIt, atlases);
        }
        if (m_provider != "own" && resolved.entries.empty()) {
            const auto ownIt = providers.find("own");
            if (ownIt != providers.end()) {
                ResolvedBinding own = ResolveBinding(*ownIt, atlases);
                resolved.entries = std::move(own.entries);
                if (resolved.animations.Empty()) {
                    resolved.animations = std::move(own.animations);
                }
                if (!resolved.entries.empty()) {
                    ++fallbackCount;
                    ZL_DEBUG("assets", "资源回退 own: {}（provider {} 无绑定或加载失败）",
                             logicalName, m_provider);
                }
            }
        }
        // 角色 v2 片段来自 own 图集；切换 provider 只影响静态帧时仍可播放动画。
        if (m_provider != "own" && resolved.animations.Empty()) {
            const auto ownIt = providers.find("own");
            if (ownIt != providers.end()) {
                ResolvedBinding own = ResolveBinding(*ownIt, atlases);
                if (!own.animations.Empty()) {
                    resolved.animations = std::move(own.animations);
                }
            }
        }
        if (resolved.entries.empty()) {
            ZL_ERROR("assets", "逻辑资源解析失败: {}（provider={}）", logicalName, m_provider);
        } else {
            ZL_DEBUG("assets", "逻辑资源: {} -> {} 帧", logicalName, resolved.entries.size());
        }
        if (!resolved.animations.Empty()) {
            ++animationCount;
            auto [animIt, inserted] =
                m_animationSets.emplace(logicalName, std::move(resolved.animations));
            if (!inserted) {
                ZL_WARN("assets", "动画集重复: {}", logicalName);
            } else {
                m_animByHash.emplace(ecs::HashName(logicalName), &animIt->second);
            }
        }
        m_resolved.emplace(logicalName, std::move(resolved.entries));

        const auto cutawayIt = providers.find("cutaway");
        if (cutawayIt != providers.end()) {
            AssetCutawayMeta meta;
            std::string cutawayError;
            if (ParseCutawayMeta(*cutawayIt, &meta, &cutawayError)) {
                m_cutaway.emplace(logicalName, meta);
                ++cutawayCount;
            } else {
                ZL_WARN("assets", "忽略非法 cutaway 元数据: {}（{}）", logicalName,
                        cutawayError);
            }
        }
        const auto windowIt = providers.find("window");
        if (windowIt != providers.end()) {
            if (windowIt->is_boolean() && windowIt->get<bool>()) {
                m_windows.insert(logicalName);
                ++windowCount;
            } else if (!windowIt->is_boolean()) {
                ZL_WARN("assets", "忽略非法 window 元数据（应为 bool）: {}", logicalName);
            }
        }
        const auto foliageIt = providers.find("foliage");
        if (foliageIt != providers.end()) {
            if (foliageIt->is_boolean() && foliageIt->get<bool>()) {
                m_foliage.insert(logicalName);
                ++foliageCount;
            } else if (!foliageIt->is_boolean()) {
                ZL_WARN("assets", "忽略非法 foliage 元数据（应为 bool）: {}", logicalName);
            }
        }
    }

    for (const auto& [logicalName, entries] : m_resolved) {
        const std::uint32_t hash = ecs::HashName(logicalName);
        const auto [it, inserted] = m_byHash.emplace(hash, &entries);
        if (!inserted && it->second != &entries) {
            ZL_WARN("assets", "资源哈希碰撞（FNV-1a 32）: {} 与既有逻辑名同哈希 {:#x}", logicalName,
                    hash);
        }
    }
    for (const auto& [logicalName, meta] : m_cutaway) {
        m_cutawayByHash.emplace(ecs::HashName(logicalName), &meta);
    }
    for (const std::string& logicalName : m_windows) {
        m_windowHashes.insert(ecs::HashName(logicalName));
    }
    for (const std::string& logicalName : m_foliage) {
        m_foliageHashes.insert(ecs::HashName(logicalName));
    }

    ZL_INFO("assets",
            "AssetProvider 就绪：provider={}，逻辑资源 {} 项（own 回退 {} 项，动画集 {} 项，"
            "可剖 {} 项，窗 {} 项，植被 {} 项，mod 清单 {}，新增绑定 {}）",
            m_provider, m_resolved.size(), fallbackCount, animationCount, cutawayCount,
            windowCount, foliageCount, mergeStats.manifestsLoaded, mergeStats.bindingsAdded);
    if (mergeStats.manifestsSkipped > 0 || mergeStats.bindingsSkipped > 0) {
        ZL_WARN("assets", "mod 资源清单跳过：清单 {}，绑定 {}",
                mergeStats.manifestsSkipped, mergeStats.bindingsSkipped);
    }
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

const AssetProvider::AnimationSet* AssetProvider::Animations(std::string_view logical) const {
    const auto it = m_animationSets.find(std::string(logical));
    return it != m_animationSets.end() ? &it->second : nullptr;
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
