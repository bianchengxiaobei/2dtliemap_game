#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace zl::game {

// M36-T2：逻辑名级别的可选剖切元数据（与 provider 绑定并列）。
// edges 位：1=N 面，2=W 面；0 表示该资源不可剖。
struct AssetCutawayMeta {
    enum class Style : std::uint8_t {
        Full = 0,
        Top = 1,
        Left = 2,
        Right = 3,
    };

    std::uint8_t edges = 0;
    Style style = Style::Full;

    bool Empty() const { return edges == 0; }
};

// 解析 logical.<name>.cutaway；非法时返回 false（out->edges=0），由调用方告警并忽略。
bool ParseCutawayMeta(const nlohmann::json& node, AssetCutawayMeta* out,
                      std::string* error = nullptr);

struct AssetManifestMergeStats {
    int manifestsLoaded = 0;
    int manifestsSkipped = 0;
    int bindingsAdded = 0;
    int bindingsSkipped = 0;
};

bool LoadMergedAssetManifest(
    const std::filesystem::path& basePath,
    const std::vector<std::filesystem::path>& modPaths,
    nlohmann::json* out,
    AssetManifestMergeStats* stats = nullptr,
    std::string* error = nullptr);

}  // namespace zl::game
