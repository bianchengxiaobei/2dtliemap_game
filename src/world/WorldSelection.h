#pragma once

#include <string>
#include <string_view>

namespace zl::core {
class Config;
}

namespace zl::world {

struct WorldSelection {
    bool m31 = false;
    std::string mapDir;
    std::string contentFile;
};

// Resolve the active map and optional content profile. Single-player defaults
// to the M31 vertical slice when world.m31.enabled is set. Dedicated servers
// keep the legacy map unless serverEnabled is set or the caller explicitly
// passes the M31 map directory, so existing server fixtures remain stable.
WorldSelection ResolveWorldSelection(const core::Config& config,
                                     bool dedicatedServer);

std::string NormalizeWorldPath(std::string_view path);

}  // namespace zl::world
