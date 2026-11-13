#include "world/WorldSelection.h"

#include <algorithm>
#include <filesystem>

#include "core/Config.h"

namespace zl::world {
namespace {

constexpr const char* kM31MapDir = "assets/maps/village_demo_m31";
constexpr const char* kLegacyMapDir = "assets/maps/village_demo";
constexpr const char* kM31ContentFile =
    "assets/maps/village_demo_m31_content.json";

}  // namespace

std::string NormalizeWorldPath(std::string_view path) {
    std::string text(path);
    std::replace(text.begin(), text.end(), '\\', '/');
    while (text.size() > 1u && text.back() == '/') {
        text.pop_back();
    }
    if (text.empty()) {
        return {};
    }
    return std::filesystem::path(text).lexically_normal().generic_string();
}

WorldSelection ResolveWorldSelection(const core::Config& config,
                                     bool dedicatedServer) {
    WorldSelection selection;
    const bool m31Enabled = config.Get<bool>("world.m31.enabled", false);
    const std::string configuredMap =
        config.Get<std::string>("world.mapDir", kLegacyMapDir);
    const std::string m31Map =
        config.Get<std::string>("world.m31.mapDir", kM31MapDir);
    const bool explicitM31 = NormalizeWorldPath(configuredMap) ==
                             NormalizeWorldPath(m31Map);
    const bool useM31 = m31Enabled &&
                        (explicitM31 || !dedicatedServer ||
                         config.Get<bool>("world.m31.serverEnabled", false));
    if (useM31) {
        selection.m31 = true;
        selection.mapDir = explicitM31 ? configuredMap : m31Map;
        selection.contentFile = config.Get<std::string>(
            "world.m31.contentFile", kM31ContentFile);
        return selection;
    }

    selection.mapDir =
        config.Get<std::string>("world.m31.legacyMapDir", kLegacyMapDir);
    return selection;
}

}  // namespace zl::world
