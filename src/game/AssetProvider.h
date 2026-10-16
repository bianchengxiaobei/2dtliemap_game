#pragma once

#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

private:
    std::string m_provider;
    std::unordered_map<std::string, std::vector<Entry>> m_resolved;
};

}  // namespace zl::game
