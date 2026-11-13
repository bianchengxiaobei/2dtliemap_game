#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "world/WorldDefinition.h"

namespace zl::save {

inline constexpr std::uint16_t kWorldManifestVersion = 6;
inline constexpr std::uint16_t kWorldManifestMinVersion = 1;
inline constexpr const char* kWorldManifestFile = "world_manifest_v1.bin";
inline constexpr std::uint32_t kWorldSaveVersion = 1;
inline constexpr std::size_t kMaxManifestWorldEvents = 128;
inline constexpr std::size_t kMaxManifestWorldEventOutcomes = 512;

struct ManifestChunk {
    std::int32_t chunkX = 0;
    std::int32_t chunkY = 0;
    bool hasDiff = true;
};

struct ManifestRegion {
    std::string id;
    std::uint8_t phase = 0;
};

struct ManifestEntityBucket {
    std::string id;
    std::string file;
    std::uint32_t entityCount = 0;
};

struct ManifestRegionEvent {
    std::string id;
    std::string regionId;
    std::uint8_t kind = 0;
    std::uint8_t status = 0;
    std::int32_t day = 1;
    float x = 0.0f;
    float y = 0.0f;
    float remainingSeconds = 0.0f;
    std::int32_t targetCount = 0;
    std::int32_t spawnedCount = 0;
    bool contentSpawned = false;
};

struct ManifestStrategicResources {
    std::int32_t people = 0;
    std::int32_t supplies = 0;
    std::int32_t ammo = 0;
    std::int32_t fuel = 0;
};

struct ManifestStrategicRegion {
    std::string id;
    std::uint8_t owner = 0;
    std::uint8_t siegePhase = 0;
    std::uint8_t intelLevel = 0;
    bool coreSafe = false;
    std::int32_t garrison = 0;
    std::int32_t fortification = 0;
    std::int32_t population = 0;
    float pressure = 0.0f;
    float defense = 0.0f;
    float supply = 0.0f;
    float morale = 100.0f;
    std::int32_t facilityDamage = 0;
    std::int32_t refugees = 0;
    std::int32_t infectionCores = 0;
    std::uint8_t recoveryStage = 0;
    std::int64_t recoveryReadyMinute = -1;
    std::string refugeeHost;
    std::int64_t nextAttackMinute = -1;
    std::int64_t lastEventMinute = -1;
};

struct ManifestStrategicEvent {
    std::string id;
    std::string regionId;
    std::uint8_t kind = 0;
    std::uint8_t ownerAfter = 0;
    std::int64_t gameMinute = 0;
    std::int32_t people = 0;
    std::int32_t supplies = 0;
    std::int32_t ammo = 0;
    std::int32_t fuel = 0;
};

struct ManifestStrategicContribution {
    std::uint64_t stableId = 0;
    std::string regionId;
    std::int32_t points = 0;
};

// M33-F2-T5: interactive world event state. Persisting the director snapshot
// lets a single-player save or a restarted server resume the same event instead
// of rerolling it from the current day.
struct ManifestWorldEvent {
    std::string id;
    std::string defId;
    std::string name;
    std::string kind;
    std::string regionId;
    float x = 0.0f;
    float y = 0.0f;
    std::uint8_t status = 0;
    std::uint8_t stageKind = 0;
    std::int32_t day = 1;
    std::int32_t stageIndex = 0;
    std::int32_t stageProgress = 0;
    std::int32_t stageTarget = 0;
    float stageRadius = 0.0f;
    std::string stageTargetId;
    std::int64_t scheduledMinute = 0;
    std::int64_t warningMinute = 0;
    std::int64_t activeMinute = 0;
    std::int64_t deadlineMinute = 0;
    std::int64_t resolvedMinute = 0;
    std::int32_t chainDepth = 0;
    bool detail = false;
    bool downgraded = false;
    bool stagesComplete = false;
    bool contentSpawned = false;
    std::string choiceId;
    std::string nextEventId;
};

struct WorldManifest {
    std::uint16_t version = kWorldManifestVersion;
    world::WorldIdentity identity;
    std::uint32_t worldSaveVersion = kWorldSaveVersion;
    std::vector<ManifestChunk> chunks;
    std::vector<ManifestRegion> regions;
    std::vector<ManifestEntityBucket> buckets;
    std::vector<ManifestRegionEvent> events;
    std::int64_t strategicTickMinute = 0;
    ManifestStrategicResources strategicResources;
    std::vector<ManifestStrategicRegion> strategicRegions;
    std::vector<ManifestStrategicEvent> strategicEvents;
    std::vector<ManifestStrategicContribution> strategicContributions;
    std::vector<ManifestWorldEvent> worldEvents;
    std::vector<std::string> worldEventOutcomes;
};

std::string EntityBucketFile(std::string_view bucketId);
bool IsSafeEntityBucketFile(std::string_view file);

bool ValidateWorldManifest(const WorldManifest& manifest,
                           std::string* error = nullptr);

std::vector<std::uint8_t> EncodeWorldManifest(const WorldManifest& manifest,
                                              std::string* error = nullptr);

bool DecodeWorldManifest(std::span<const std::uint8_t> bytes,
                         WorldManifest* out, std::string* error = nullptr);

}  // namespace zl::save
