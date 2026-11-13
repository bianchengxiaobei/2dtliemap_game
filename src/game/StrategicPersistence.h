#pragma once

#include <string>

namespace zl::save {
struct WorldManifest;
}

namespace zl::game {

struct StrategicConfig;
struct StrategicState;

bool CaptureStrategicState(const StrategicState& state,
                           save::WorldManifest* manifest,
                           std::string* error = nullptr);
bool RestoreStrategicState(const save::WorldManifest& manifest,
                           const StrategicConfig& config,
                           StrategicState* state,
                           std::string* error = nullptr);

}  // namespace zl::game
