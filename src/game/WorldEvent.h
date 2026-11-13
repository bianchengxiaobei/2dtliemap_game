#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace zl::core {
class Config;
}

namespace zl::items {
class ItemRegistry;
}

namespace zl::game {

inline constexpr std::size_t kMaxWorldEventDefs = 64;
inline constexpr std::size_t kMaxWorldEventStages = 8;
inline constexpr std::size_t kMaxWorldEventChoices = 8;
inline constexpr std::size_t kMaxWorldEventIdBytes = 64;
inline constexpr std::size_t kMaxWorldEventNameBytes = 128;
inline constexpr std::size_t kMaxWorldEventKindBytes = 32;
inline constexpr std::size_t kMaxWorldEventTargetBytes = 64;
inline constexpr std::size_t kMaxWorldEventTextBytes = 256;
inline constexpr std::size_t kMaxWorldEventChainDepth = 8;

enum class WorldEventStageKind : std::uint8_t {
    Reach = 0,
    Interact = 1,
    Kill = 2,
    Loot = 3,
    Defend = 4,
    Choice = 5,
};

struct WorldEventStageDef {
    WorldEventStageKind kind = WorldEventStageKind::Reach;
    std::string target;
    int count = 1;
    float radius = 4.0f;
    std::string description;
};

struct WorldEventChoiceDef {
    std::string id;
    std::string label;
    int reputation = 0;
    float rewardXp = 0.0f;
    std::string rewardItem;
    int rewardItemCount = 1;
    int pressure = 0;
};

struct WorldEventDef {
    std::string id;
    std::string name;
    std::string kind = "custom";
    int minDay = 1;
    int maxDay = 100000;
    float weight = 1.0f;
    int cooldownDays = 1;
    int warningMinutes = 60;
    int durationMinutes = 30;
    std::string region = "any";
    std::vector<WorldEventStageDef> stages;
    std::vector<WorldEventChoiceDef> choices;
    std::string nextEvent;
    int failureReputation = -5;
    int failurePressure = 5;
};

struct WorldEventConfig {
    bool enabled = false;
    std::uint32_t randomSeed = 20261113u;
    std::vector<WorldEventDef> defs;

    const WorldEventDef* Find(std::string_view id) const {
        for (const WorldEventDef& def : defs) {
            if (def.id == id) {
                return &def;
            }
        }
        return nullptr;
    }
};

struct WorldEventMergeStats {
    int filesLoaded = 0;
    int defsAdded = 0;
    int defsSkipped = 0;
};

bool LoadWorldEventConfig(const core::Config& config, WorldEventConfig* out,
                          std::string* error,
                          const items::ItemRegistry* items = nullptr);
bool MergeWorldEventFile(const std::filesystem::path& path,
                         WorldEventConfig* out,
                         WorldEventMergeStats* stats = nullptr,
                         std::string* error = nullptr,
                         const items::ItemRegistry* items = nullptr);
bool MergeWorldEventJson(std::string_view json, const std::string& source,
                         WorldEventConfig* out,
                         WorldEventMergeStats* stats = nullptr,
                         std::string* error = nullptr,
                         const items::ItemRegistry* items = nullptr);

const char* WorldEventStageKindName(WorldEventStageKind kind);
bool ParseWorldEventStageKind(std::string_view value,
                              WorldEventStageKind* out);
bool WorldEventChainValid(const std::vector<WorldEventDef>& defs,
                          const WorldEventDef& def);
bool WorldEventHasNext(const WorldEventConfig& config,
                       const WorldEventDef& def);

}  // namespace zl::game
