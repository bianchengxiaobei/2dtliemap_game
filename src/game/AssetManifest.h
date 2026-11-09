#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace zl::game {

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
