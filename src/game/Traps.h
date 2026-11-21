#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ecs/EcsWorld.h"
#include "game/HomesteadComponents.h"

namespace zl::core {
class Config;
}

namespace zl::game {

struct BuildableDef;

inline constexpr std::size_t kMaxTrapDefs = 64;
inline constexpr std::size_t kMaxTrapIdBytes = 64;
inline constexpr std::size_t kMaxTrapNameBytes = 96;

inline constexpr std::uint8_t kTrapTriggerZombie = 1u << 0;
inline constexpr std::uint8_t kTrapTriggerAnimal = 1u << 1;
inline constexpr std::uint8_t kTrapTriggerDefault =
    kTrapTriggerZombie | kTrapTriggerAnimal;

struct TrapFireDef {
    float radius = 0.0f;
    float minutes = 0.0f;
};

struct TrapDef {
    std::string id;
    std::string name;
    std::string buildingId;
    float damage = 0.0f;
    float radius = 1.0f;
    float cooldownSeconds = 0.0f;
    int uses = 1;
    float staggerSeconds = 0.0f;
    float noiseRadius = 0.0f;
    std::uint8_t triggerMask = kTrapTriggerDefault;
    float armSeconds = 1.0f;
    std::optional<TrapFireDef> fire;
};

struct TrapMergeStats {
    int filesLoaded = 0;
    int defsAdded = 0;
    int defsSkipped = 0;
};

struct TrapDefsConfig {
    bool enabled = false;
    std::vector<TrapDef> defs;

    const TrapDef* Find(std::string_view id) const;
    const TrapDef* FindByBuilding(std::string_view buildingId) const;
};

std::string TrapEffectText(const TrapDef& trap);
std::string TrapBuildSummary(const TrapDef& trap);
bool TrapCanToggle(const ecs::PlacedTrap& trap);
bool SetPlacedTrapArmed(ecs::PlacedTrap& trap, bool armed);
bool AdvancePlacedTrapArming(ecs::PlacedTrap& trap, float seconds);
ecs::Entity SpawnPlacedTrap(ecs::EcsWorld& world, float x, float y, int z,
                            const TrapDef& trap, const BuildableDef& buildable,
                            std::uint64_t owner);

bool IsValidTrapToken(std::string_view value);
const char* TrapTriggerName(std::uint8_t bit);

bool LoadTrapDefsConfig(const core::Config& config,
                    const std::vector<BuildableDef>& buildables,
                    TrapDefsConfig* out, std::string* error = nullptr);
bool MergeTrapJson(std::string_view json, const std::string& source,
                   TrapDefsConfig* out, TrapMergeStats* stats,
                   const std::vector<BuildableDef>& buildables,
                   std::string* error = nullptr);
bool MergeTrapFile(const std::filesystem::path& path, TrapDefsConfig* out,
                   TrapMergeStats* stats,
                   const std::vector<BuildableDef>& buildables,
                   std::string* error = nullptr);

}  // namespace zl::game
