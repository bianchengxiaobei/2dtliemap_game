#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "render/Atlas.h"

namespace zl::game {

// M2-T8：资源提供者层（docs/02 §9.1）。
// 游戏代码只按逻辑名取资源（tile.floor.grass / object.tree ...），
// 实际映射由 assets/asset_manifest.json 描述，按 provider（own / placeholder_pz）切换；
// 换资源 = 换目录 + 改 manifest + 改 config，零代码改动。
class AssetProvider {
public:
    struct Entry {
        const render::Atlas* atlas = nullptr;
        const render::AtlasFrame* frame = nullptr;
    };

    // M14-T2：角色动画 v2 片段。frames 顺序 = dir * framesPerDir + frame。
    struct AnimationClip {
        std::uint8_t framesPerDir = 0;
        std::uint8_t eventMask = 0;
        ecs::AnimEvent event = ecs::AnimEvent::None;
        std::vector<Entry> frames;
    };
    struct AnimationSet {
        std::array<AnimationClip, static_cast<std::size_t>(ecs::AnimClip::Count)> clips;
        std::array<bool, static_cast<std::size_t>(ecs::AnimClip::Count)> present{};

        bool Empty() const {
            for (const bool value : present) {
                if (value) {
                    return false;
                }
            }
            return true;
        }
        const AnimationClip* Find(ecs::AnimClip clip) const {
            const auto index = static_cast<std::size_t>(clip);
            return index < clips.size() && present[index] ? &clips[index] : nullptr;
        }
    };

    // provider 不可用（图集缺失/帧缺失）时逐项回退 own；
    // 返回 false 表示 manifest 都没加载成功（资源缺失，渲染会退化）
    bool Init(const std::string& manifestPath, const std::string& provider,
              render::AtlasManager& atlases);

    const std::string& Provider() const { return m_provider; }
    bool Has(std::string_view logical) const;

    // 某逻辑名的全部变体帧（空表示缺失）
    const std::vector<Entry>* Frames(std::string_view logical) const;
    // 从变体里随机挑一个（缺失时返回空 Entry）
    Entry Pick(std::string_view logical, std::mt19937& rng) const;

    // ECS 通道（M4-T2）：SpriteRef 只存 HashName(逻辑名)，这里按哈希反查
    const std::vector<Entry>* FramesByHash(std::uint32_t hash) const {
        const auto it = m_byHash.find(hash);
        return it == m_byHash.end() ? nullptr : it->second;
    }

    // M14-T2：动画 v2 片段；缺失返回 nullptr（渲染侧回退旧 frames）。
    const AnimationSet* Animations(std::string_view logical) const;
    const AnimationSet* AnimationsByHash(std::uint32_t hash) const {
        const auto it = m_animByHash.find(hash);
        return it == m_animByHash.end() ? nullptr : it->second;
    }

private:
    std::string m_provider;
    std::unordered_map<std::string, std::vector<Entry>> m_resolved;
    std::unordered_map<std::uint32_t, const std::vector<Entry>*> m_byHash;  // HashName → 变体表
    std::unordered_map<std::string, AnimationSet> m_animationSets;
    std::unordered_map<std::uint32_t, const AnimationSet*> m_animByHash;
};

}  // namespace zl::game
