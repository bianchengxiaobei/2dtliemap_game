#include "game/AssetManifest.h"

#include <fstream>
#include <system_error>
#include <utility>

#include "core/Log.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "assets";

bool IsMetadataKey(const std::string& key) {
    return key == "cutaway" || key == "window" || key == "foliage";
}

bool ReadJson(const std::filesystem::path& path, nlohmann::json* out,
              std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        if (error != nullptr) {
            *error = "无法打开 " + path.generic_string();
        }
        return false;
    }
    try {
        file >> *out;
    } catch (const nlohmann::json::exception& e) {
        if (error != nullptr) {
            *error = path.generic_string() + ": JSON 解析失败：" + e.what();
        }
        return false;
    }
    return true;
}

bool ValidBinding(const nlohmann::json& binding) {
    if (!binding.is_object()) {
        return false;
    }
    for (const auto& [provider, value] : binding.items()) {
        if (IsMetadataKey(provider)) {
            continue;
        }
        if (!value.is_object()) {
            return false;
        }
        const auto atlas = value.find("atlas");
        if (atlas != value.end() && !atlas->is_string()) {
            return false;
        }
        const auto frames = value.find("frames");
        if (frames != value.end()) {
            if (!frames->is_array()) {
                return false;
            }
            for (const auto& frame : *frames) {
                if (!frame.is_string()) {
                    return false;
                }
            }
        }
    }
    return true;
}

std::string ResolveAtlasPath(const std::string& path,
                             const std::filesystem::path& baseDir) {
    if (path.empty()) {
        return path;
    }
    const std::filesystem::path raw(path);
    if (raw.is_absolute()) {
        return raw.lexically_normal().generic_string();
    }
    std::error_code ec;
    const std::filesystem::path local = baseDir / raw;
    if (std::filesystem::exists(local, ec)) {
        return local.lexically_normal().generic_string();
    }
    return raw.generic_string();
}

nlohmann::json NormalizeBinding(const nlohmann::json& binding,
                                const std::filesystem::path& baseDir) {
    nlohmann::json normalized = binding;
    for (auto& [provider, value] : normalized.items()) {
        if (IsMetadataKey(provider)) {
            continue;
        }
        if (!value.is_object()) {
            continue;
        }
        const auto atlas = value.find("atlas");
        if (atlas != value.end() && atlas->is_string()) {
            *atlas = ResolveAtlasPath(atlas->get<std::string>(), baseDir);
        }
    }
    return normalized;
}

}  // namespace

bool ParseCutawayMeta(const nlohmann::json& node, AssetCutawayMeta* out,
                      std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "剖切元数据输出为空";
        }
        return false;
    }
    *out = {};
    if (!node.is_object()) {
        if (error != nullptr) {
            *error = "cutaway 必须是对象";
        }
        return false;
    }
    for (const auto& [key, value] : node.items()) {
        if (key == "edges" || key == "style" || key == "mask") {
            continue;
        }
        if (error != nullptr) {
            *error = "未知 cutaway 字段: " + key;
        }
        return false;
    }

    const auto edgesIt = node.find("edges");
    if (edgesIt == node.end() || !edgesIt->is_array()) {
        if (error != nullptr) {
            *error = "cutaway.edges 必须是数组";
        }
        return false;
    }
    std::uint8_t edges = 0;
    for (const auto& edge : *edgesIt) {
        if (!edge.is_string()) {
            if (error != nullptr) {
                *error = "cutaway.edges 元素必须是字符串";
            }
            return false;
        }
        const std::string value = edge.get<std::string>();
        if (value == "n") {
            edges |= 1u;
        } else if (value == "w") {
            edges |= 2u;
        } else {
            if (error != nullptr) {
                *error = "未知 cutaway 边: " + value;
            }
            return false;
        }
    }
    if (edges == 0u) {
        if (error != nullptr) {
            *error = "cutaway.edges 为空";
        }
        return false;
    }

    AssetCutawayMeta::Style style = AssetCutawayMeta::Style::Full;
    const auto styleIt = node.find("style");
    if (styleIt != node.end()) {
        if (!styleIt->is_string()) {
            if (error != nullptr) {
                *error = "cutaway.style 必须是字符串";
            }
            return false;
        }
        const std::string value = styleIt->get<std::string>();
        if (value == "full") {
            style = AssetCutawayMeta::Style::Full;
        } else if (value == "top") {
            style = AssetCutawayMeta::Style::Top;
        } else if (value == "left") {
            style = AssetCutawayMeta::Style::Left;
        } else if (value == "right") {
            style = AssetCutawayMeta::Style::Right;
        } else {
            if (error != nullptr) {
                *error = "未知 cutaway.style: " + value;
            }
            return false;
        }
    }
    const auto maskIt = node.find("mask");
    if (maskIt != node.end() && !maskIt->is_string()) {
        if (error != nullptr) {
            *error = "cutaway.mask 必须是字符串";
        }
        return false;
    }

    out->edges = edges;
    out->style = style;
    return true;
}

bool LoadMergedAssetManifest(
    const std::filesystem::path& basePath,
    const std::vector<std::filesystem::path>& modPaths,
    nlohmann::json* out, AssetManifestMergeStats* stats, std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "资源清单输出为空";
        }
        return false;
    }
    if (stats != nullptr) {
        *stats = {};
    }
    nlohmann::json base;
    std::string readError;
    if (!ReadJson(basePath, &base, &readError)) {
        if (error != nullptr) {
            *error = readError;
        }
        return false;
    }
    if (!base.is_object() || !base.contains("logical") ||
        !base["logical"].is_object()) {
        if (error != nullptr) {
            *error = "资源清单缺少 logical 段: " + basePath.generic_string();
        }
        return false;
    }

    for (const std::filesystem::path& modPath : modPaths) {
        nlohmann::json mod;
        if (!ReadJson(modPath, &mod, &readError)) {
            ZL_WARN(kLogModule, "跳过 mod 资源清单：{}", readError);
            if (stats != nullptr) {
                ++stats->manifestsSkipped;
            }
            continue;
        }
        if (!mod.is_object() || !mod.contains("logical") ||
            !mod["logical"].is_object()) {
            ZL_WARN(kLogModule, "跳过 mod 资源清单（缺少 logical 段）：{}",
                    modPath.generic_string());
            if (stats != nullptr) {
                ++stats->manifestsSkipped;
            }
            continue;
        }

        const std::filesystem::path baseDir = modPath.parent_path();
        if (stats != nullptr) {
            ++stats->manifestsLoaded;
        }
        for (const auto& [name, binding] : mod["logical"].items()) {
            if (base["logical"].contains(name)) {
                ZL_WARN(kLogModule, "忽略重复资源逻辑名：{} <- {}",
                        name, modPath.generic_string());
                if (stats != nullptr) {
                    ++stats->bindingsSkipped;
                }
                continue;
            }
            if (!ValidBinding(binding)) {
                ZL_WARN(kLogModule, "忽略非法资源绑定：{} <- {}",
                        name, modPath.generic_string());
                if (stats != nullptr) {
                    ++stats->bindingsSkipped;
                }
                continue;
            }
            base["logical"][name] = NormalizeBinding(binding, baseDir);
            if (stats != nullptr) {
                ++stats->bindingsAdded;
            }
        }
    }

    *out = std::move(base);
    return true;
}

}  // namespace zl::game
