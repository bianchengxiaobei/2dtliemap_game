#include "save/WorldManifest.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <exception>
#include <string>
#include <unordered_set>
#include <utility>

#include "save/BinaryFormat.h"
#include "save/BinaryReader.h"
#include "save/BinaryWriter.h"

namespace zl::save {
namespace {

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool Fail(std::string* error, std::string message) {
    SetError(error, std::move(message));
    return false;
}

std::string HashSuffix(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char c : text) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ull;
    }
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int i = 15; i >= 0; --i) {
        result[static_cast<std::size_t>(i)] = kHex[hash & 0xFu];
        hash >>= 4u;
    }
    return result;
}

std::string SanitizeBucketId(std::string_view id) {
    std::string result;
    result.reserve(id.size());
    for (const unsigned char c : id) {
        if (std::isalnum(c) != 0 || c == '_' || c == '-') {
            result.push_back(static_cast<char>(c));
        } else {
            result.push_back('_');
        }
    }
    return result;
}

}  // namespace

std::string EntityBucketFile(std::string_view bucketId) {
    if (bucketId == "global") {
        return "entities_global.bin";
    }
    return "entities_" + SanitizeBucketId(bucketId) + "_" + HashSuffix(bucketId) +
           ".bin";
}

bool IsSafeEntityBucketFile(std::string_view file) {
    if (file.empty() || file.size() > 128) {
        return false;
    }
    if (file.find("..") != std::string_view::npos ||
        file.find('/') != std::string_view::npos ||
        file.find('\\') != std::string_view::npos) {
        return false;
    }
    return file.starts_with("entities_") && file.ends_with(".bin");
}

bool ValidateWorldManifest(const WorldManifest& manifest, std::string* error) {
    if (manifest.version < kWorldManifestMinVersion ||
        manifest.version > kWorldManifestVersion) {
        return Fail(error, "world manifest 版本不兼容");
    }
    if (manifest.identity.mapId.empty() || manifest.identity.mapId.size() > 256) {
        return Fail(error, "world manifest mapId 非法");
    }
    if (manifest.worldSaveVersion == 0) {
        return Fail(error, "world manifest worldSaveVersion 非法");
    }
    if (manifest.worldSaveVersion > kWorldSaveVersion) {
        return Fail(error, "world save 版本不兼容");
    }
    if (manifest.chunks.size() > kMaxContainerElements ||
        manifest.regions.size() > kMaxContainerElements ||
        manifest.buckets.size() > kMaxContainerElements ||
        manifest.events.size() > 256u) {
        return Fail(error, "world manifest 鍒楄〃闀垮害瓒呰繃涓婇檺");
    }

    std::unordered_set<std::uint64_t> chunkKeys;
    for (const ManifestChunk& chunk : manifest.chunks) {
        const std::uint64_t key =
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunk.chunkX))
             << 32u) |
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunk.chunkY));
        if (!chunkKeys.insert(key).second) {
            return Fail(error, "world manifest chunk 坐标重复");
        }
    }

    std::unordered_set<std::string> regionIds;
    for (const ManifestRegion& region : manifest.regions) {
        if (region.id.empty() || region.id.size() > 128) {
            return Fail(error, "world manifest region id 非法");
        }
        if (!regionIds.insert(region.id).second) {
            return Fail(error, "world manifest region id 重复");
        }
        if (region.phase > 2) {
            return Fail(error, "world manifest region phase 非法");
        }
    }

    std::unordered_set<std::string> bucketIds;
    std::unordered_set<std::string> bucketFiles;
    for (const ManifestEntityBucket& bucket : manifest.buckets) {
        if (bucket.id.empty() || bucket.id.size() > 128) {
            return Fail(error, "world manifest entity bucket id 非法");
        }
        if (!bucketIds.insert(bucket.id).second) {
            return Fail(error, "world manifest entity bucket id 重复");
        }
        if (!IsSafeEntityBucketFile(bucket.file)) {
            return Fail(error, "world manifest entity bucket 文件非法");
        }
        if (!bucketFiles.insert(bucket.file).second) {
            return Fail(error, "world manifest entity bucket 文件重复");
        }
        if (bucket.entityCount > kMaxContainerElements) {
            return Fail(error, "world manifest entity bucket 鏁伴噺瓒呰繃涓婇檺");
        }
    }

    std::unordered_set<std::string> eventIds;
    for (const ManifestRegionEvent& event : manifest.events) {
        if (event.id.empty() || event.id.size() > 128 ||
            event.regionId.empty() || event.regionId.size() > 128) {
            return Fail(error, "world manifest region event id 闈炴硶");
        }
        if (!eventIds.insert(event.id).second) {
            return Fail(error, "world manifest region event id 閲嶅");
        }
        if (event.kind > 3 || event.status > 3) {
            return Fail(error, "world manifest region event 类型/状态非法");
        }
        if (!std::isfinite(event.x) || !std::isfinite(event.y) ||
            !std::isfinite(event.remainingSeconds) ||
            event.remainingSeconds < 0.0f || event.day < 0 ||
            event.targetCount < 0 || event.targetCount > 10000 ||
            event.spawnedCount < 0 || event.spawnedCount > 10000) {
            return Fail(error, "world manifest region event 坐标/计数非法");
        }
    }

    const bool hasStrategic =
        manifest.strategicTickMinute != 0 ||
        manifest.strategicResources.people != 0 ||
        manifest.strategicResources.supplies != 0 ||
        manifest.strategicResources.ammo != 0 ||
        manifest.strategicResources.fuel != 0 ||
        !manifest.strategicRegions.empty() ||
        !manifest.strategicEvents.empty();
    if (manifest.version < 3u && hasStrategic) {
        return Fail(error, "旧版 world manifest 不能携带战略状态");
    }
    if (manifest.strategicTickMinute < 0 ||
        manifest.strategicResources.people < 0 ||
        manifest.strategicResources.supplies < 0 ||
        manifest.strategicResources.ammo < 0 ||
        manifest.strategicResources.fuel < 0) {
        return Fail(error, "world manifest 战略资源非法");
    }
    if (manifest.strategicRegions.size() > 64u ||
        manifest.strategicEvents.size() > 256u) {
        return Fail(error, "world manifest 战略状态数量超过上限");
    }

    std::unordered_set<std::string> strategicRegionIds;
    for (const ManifestStrategicRegion& region : manifest.strategicRegions) {
        if (region.id.empty() || region.id.size() > 128) {
            return Fail(error, "world manifest 战略区域 id 非法");
        }
        if (!strategicRegionIds.insert(region.id).second) {
            return Fail(error, "world manifest 战略区域 id 重复");
        }
        if (region.owner > 3 || region.siegePhase > 4 ||
            region.intelLevel > 3 || region.garrison < 0 ||
            region.fortification < 0 || region.population < 0 ||
            region.facilityDamage < 0 || region.facilityDamage > 100 ||
            region.refugees < 0 || region.infectionCores < 0 ||
            region.recoveryStage > 4 || region.recoveryReadyMinute < -1 ||
            region.refugeeHost.size() > 128 ||
            region.nextAttackMinute < -1 || region.lastEventMinute < -1) {
            return Fail(error, "world manifest 战略区域摘要非法");
        }
        if (!std::isfinite(region.pressure) ||
            !std::isfinite(region.defense) ||
            !std::isfinite(region.supply) ||
            !std::isfinite(region.morale) || region.pressure < 0.0f ||
            region.pressure > 100.0f || region.defense < 0.0f ||
            region.defense > 100.0f || region.supply < 0.0f ||
            region.supply > 100.0f || region.morale < 0.0f ||
            region.morale > 100.0f) {
            return Fail(error, "world manifest 战略区域数值非法");
        }
    }

    std::unordered_set<std::string> strategicEventIds;
    for (const ManifestStrategicEvent& event : manifest.strategicEvents) {
        if (event.id.empty() || event.id.size() > 128 ||
            event.regionId.empty() || event.regionId.size() > 128) {
            return Fail(error, "world manifest 战略事件 id 非法");
        }
        if (!strategicEventIds.insert(event.id).second) {
            return Fail(error, "world manifest 战略事件 id 重复");
        }
        if (event.kind > 5 || event.ownerAfter > 3 ||
            event.gameMinute < 0) {
            return Fail(error, "world manifest 战略事件字段非法");
        }
        const auto inRange = [](std::int32_t value) {
            return value >= -1000000 && value <= 1000000;
        };
        if (!inRange(event.people) || !inRange(event.supplies) ||
            !inRange(event.ammo) || !inRange(event.fuel)) {
            return Fail(error, "world manifest 战略事件资源变化非法");
        }
    }

    if (!manifest.strategicContributions.empty() && manifest.version < 5u) {
        return Fail(error, "旧版 world manifest 不能携带战略贡献");
    }
    if (manifest.strategicContributions.size() > 1024u) {
        return Fail(error, "world manifest 战略贡献数量超过上限");
    }
    std::unordered_set<std::string> contributionKeys;
    for (const ManifestStrategicContribution& entry :
         manifest.strategicContributions) {
        if (entry.stableId == 0 || entry.regionId.empty() ||
            entry.regionId.size() > 128 || entry.points < 0 ||
            entry.points > 1000000) {
            return Fail(error, "world manifest 战略贡献字段非法");
        }
        const std::string key =
            std::to_string(entry.stableId) + ":" + entry.regionId;
        if (!contributionKeys.insert(key).second) {
            return Fail(error, "world manifest 战略贡献重复");
        }
    }

    if ((!manifest.worldEvents.empty() ||
         !manifest.worldEventOutcomes.empty()) &&
        manifest.version < 6u) {
        return Fail(error, "旧版 world manifest 不能携带世界事件状态");
    }
    if (manifest.worldEvents.size() > kMaxManifestWorldEvents ||
        manifest.worldEventOutcomes.size() >
            kMaxManifestWorldEventOutcomes) {
        return Fail(error, "world manifest 世界事件数量超过上限");
    }
    std::unordered_set<std::string> worldEventIds;
    for (const ManifestWorldEvent& event : manifest.worldEvents) {
        if (event.id.empty() || event.id.size() > 128 ||
            event.defId.empty() || event.defId.size() > 64 ||
            event.name.empty() || event.name.size() > 128 ||
            event.kind.size() > 32 || event.regionId.empty() ||
            event.regionId.size() > 128 ||
            event.stageTargetId.size() > 64 ||
            event.choiceId.size() > 64 ||
            event.nextEventId.size() > 64) {
            return Fail(error, "world manifest 世界事件字符串非法");
        }
        if (!worldEventIds.insert(event.id).second) {
            return Fail(error, "world manifest 世界事件 id 重复");
        }
        if (event.status > 5u || event.stageKind > 5u ||
            event.day < 1 || event.day > 100000 ||
            event.stageIndex < 0 || event.stageIndex > 8 ||
            event.stageProgress < 0 || event.stageTarget < 0 ||
            event.stageTarget > 10000 ||
            event.stageProgress > event.stageTarget ||
            event.chainDepth < 0 || event.chainDepth > 8 ||
            event.scheduledMinute < 0 || event.warningMinute < 0 ||
            event.activeMinute < 0 ||
            event.deadlineMinute <= event.activeMinute ||
            event.warningMinute < event.scheduledMinute ||
            event.activeMinute < event.warningMinute ||
            event.resolvedMinute < 0) {
            return Fail(error, "world manifest 世界事件字段非法");
        }
        if (!std::isfinite(event.x) || !std::isfinite(event.y) ||
            !std::isfinite(event.stageRadius) || event.stageRadius < 0.0f ||
            event.stageRadius > 1000.0f) {
            return Fail(error, "world manifest 世界事件坐标非法");
        }
        if (event.status == 3u && event.choiceId.empty()) {
            return Fail(error, "world manifest 已结束事件缺少选项");
        }
    }
    std::unordered_set<std::string> worldEventOutcomeKeys;
    for (const std::string& key : manifest.worldEventOutcomes) {
        if (key.size() < 24 || key.size() > 192 ||
            key.rfind("worldevent:", 0) != 0 ||
            key.find(':', 11) == std::string::npos) {
            return Fail(error, "world manifest 世界事件结果键非法");
        }
        if (!worldEventOutcomeKeys.insert(key).second) {
            return Fail(error, "world manifest 世界事件结果键重复");
        }
    }

    SetError(error, {});
    return true;
}

std::vector<std::uint8_t> EncodeWorldManifest(const WorldManifest& manifest,
                                              std::string* error) {
    if (!ValidateWorldManifest(manifest, error)) {
        return {};
    }

    BinaryWriter writer;
    if (!writer.BeginSegment(kMagicWorldManifest, manifest.version) ||
        !writer.WriteString(manifest.identity.mapId, 256) ||
        !writer.Write(static_cast<std::int64_t>(manifest.identity.seed)) ||
        !writer.Write(manifest.identity.generatorVersion) ||
        !writer.Write(manifest.identity.mapHash) ||
        !writer.Write(manifest.worldSaveVersion) ||
        !writer.Write(static_cast<std::uint32_t>(manifest.chunks.size()))) {
        Fail(error, writer.Error().empty() ? "world manifest 写入失败" : writer.Error());
        return {};
    }

    for (const ManifestChunk& chunk : manifest.chunks) {
        if (!writer.Write(chunk.chunkX) || !writer.Write(chunk.chunkY) ||
            !writer.Write(chunk.hasDiff)) {
            Fail(error, writer.Error());
            return {};
        }
    }

    if (!writer.Write(static_cast<std::uint32_t>(manifest.regions.size()))) {
        Fail(error, writer.Error());
        return {};
    }
    for (const ManifestRegion& region : manifest.regions) {
        if (!writer.WriteString(region.id, 128) ||
            !writer.Write(region.phase)) {
            Fail(error, writer.Error());
            return {};
        }
    }

    if (!writer.Write(static_cast<std::uint32_t>(manifest.buckets.size()))) {
        Fail(error, writer.Error());
        return {};
    }
    for (const ManifestEntityBucket& bucket : manifest.buckets) {
        if (!writer.WriteString(bucket.id, 128) ||
            !writer.WriteString(bucket.file, 128) ||
            !writer.Write(bucket.entityCount)) {
            Fail(error, writer.Error());
            return {};
        }
    }

    if (!writer.Write(static_cast<std::uint32_t>(manifest.events.size()))) {
        Fail(error, writer.Error());
        return {};
    }
    for (const ManifestRegionEvent& event : manifest.events) {
        if (!writer.WriteString(event.id, 128) ||
            !writer.WriteString(event.regionId, 128) ||
            !writer.Write(event.kind) || !writer.Write(event.status) ||
            !writer.Write(event.day) || !writer.Write(event.x) ||
            !writer.Write(event.y) || !writer.Write(event.remainingSeconds) ||
            !writer.Write(event.targetCount) ||
            !writer.Write(event.spawnedCount) ||
            !writer.Write(event.contentSpawned)) {
            Fail(error, writer.Error());
            return {};
        }
    }

    if (manifest.version >= 3u) {
        if (!writer.Write(manifest.strategicTickMinute) ||
            !writer.Write(manifest.strategicResources.people) ||
            !writer.Write(manifest.strategicResources.supplies) ||
            !writer.Write(manifest.strategicResources.ammo) ||
            !writer.Write(manifest.strategicResources.fuel) ||
            !writer.Write(
                static_cast<std::uint32_t>(manifest.strategicRegions.size()))) {
            Fail(error, writer.Error());
            return {};
        }
        for (const ManifestStrategicRegion& region : manifest.strategicRegions) {
            if (!writer.WriteString(region.id, 128) ||
                !writer.Write(region.owner) || !writer.Write(region.siegePhase) ||
                !writer.Write(region.intelLevel) || !writer.Write(region.coreSafe) ||
                !writer.Write(region.garrison) ||
                !writer.Write(region.fortification) ||
                !writer.Write(region.population) || !writer.Write(region.pressure) ||
                !writer.Write(region.defense) || !writer.Write(region.supply) ||
                !writer.Write(region.morale) ||
                !writer.Write(region.nextAttackMinute) ||
                !writer.Write(region.lastEventMinute)) {
                Fail(error, writer.Error());
                return {};
            }
            if (manifest.version >= 4u) {
                if (!writer.Write(region.facilityDamage) ||
                    !writer.Write(region.refugees) ||
                    !writer.Write(region.infectionCores) ||
                    !writer.Write(region.recoveryStage) ||
                    !writer.Write(region.recoveryReadyMinute) ||
                    !writer.WriteString(region.refugeeHost, 128)) {
                    Fail(error, writer.Error());
                    return {};
                }
            }
        }
        if (!writer.Write(
                static_cast<std::uint32_t>(manifest.strategicEvents.size()))) {
            Fail(error, writer.Error());
            return {};
        }
        for (const ManifestStrategicEvent& event : manifest.strategicEvents) {
            if (!writer.WriteString(event.id, 128) ||
                !writer.WriteString(event.regionId, 128) ||
                !writer.Write(event.kind) || !writer.Write(event.ownerAfter) ||
                !writer.Write(event.gameMinute) || !writer.Write(event.people) ||
                !writer.Write(event.supplies) || !writer.Write(event.ammo) ||
                !writer.Write(event.fuel)) {
                Fail(error, writer.Error());
                return {};
            }
        }
        if (manifest.version >= 5u) {
            if (!writer.Write(static_cast<std::uint32_t>(
                    manifest.strategicContributions.size()))) {
                Fail(error, writer.Error());
                return {};
            }
            for (const ManifestStrategicContribution& entry :
                 manifest.strategicContributions) {
                if (!writer.Write(entry.stableId) ||
                    !writer.WriteString(entry.regionId, 128) ||
                    !writer.Write(entry.points)) {
                    Fail(error, writer.Error());
                    return {};
                }
            }
        }
    }

    if (manifest.version >= 6u) {
        if (!writer.Write(static_cast<std::uint32_t>(
                manifest.worldEvents.size()))) {
            Fail(error, writer.Error());
            return {};
        }
        for (const ManifestWorldEvent& event : manifest.worldEvents) {
            if (!writer.WriteString(event.id, 128) ||
                !writer.WriteString(event.defId, 64) ||
                !writer.WriteString(event.name, 128) ||
                !writer.WriteString(event.kind, 32) ||
                !writer.WriteString(event.regionId, 128) ||
                !writer.Write(event.x) || !writer.Write(event.y) ||
                !writer.Write(event.status) || !writer.Write(event.stageKind) ||
                !writer.Write(event.day) || !writer.Write(event.stageIndex) ||
                !writer.Write(event.stageProgress) ||
                !writer.Write(event.stageTarget) ||
                !writer.Write(event.stageRadius) ||
                !writer.WriteString(event.stageTargetId, 64) ||
                !writer.Write(event.scheduledMinute) ||
                !writer.Write(event.warningMinute) ||
                !writer.Write(event.activeMinute) ||
                !writer.Write(event.deadlineMinute) ||
                !writer.Write(event.resolvedMinute) ||
                !writer.Write(event.chainDepth) || !writer.Write(event.detail) ||
                !writer.Write(event.downgraded) ||
                !writer.Write(event.stagesComplete) ||
                !writer.Write(event.contentSpawned) ||
                !writer.WriteString(event.choiceId, 64) ||
                !writer.WriteString(event.nextEventId, 64)) {
                Fail(error, writer.Error());
                return {};
            }
        }
        if (!writer.Write(static_cast<std::uint32_t>(
                manifest.worldEventOutcomes.size()))) {
            Fail(error, writer.Error());
            return {};
        }
        for (const std::string& key : manifest.worldEventOutcomes) {
            if (!writer.WriteString(key, 192)) {
                Fail(error, writer.Error());
                return {};
            }
        }
    }

    if (!writer.EndSegment() || !writer.Ok()) {
        Fail(error, writer.Error().empty() ? "world manifest 收尾失败" : writer.Error());
        return {};
    }
    const auto bytes = writer.Data();
    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
}

bool DecodeWorldManifest(std::span<const std::uint8_t> bytes,
                         WorldManifest* out, std::string* error) {
    if (out == nullptr) {
        return Fail(error, "world manifest 输出为空");
    }

    BinaryReader reader(bytes);
    SegmentHeader header;
    if (!reader.BeginSegment(header, kMagicWorldManifest, kWorldManifestMinVersion,
                             kWorldManifestVersion)) {
        return Fail(error, "world manifest 段读取失败: " + reader.Error());
    }

    WorldManifest parsed;
    parsed.version = header.version;
    std::int64_t seed = 0;
    if (!reader.ReadString(parsed.identity.mapId, 256) || !reader.Read(seed) ||
        !reader.Read(parsed.identity.generatorVersion) ||
        !reader.Read(parsed.identity.mapHash) ||
        !reader.Read(parsed.worldSaveVersion)) {
        return Fail(error, "world manifest 头读取失败: " + reader.Error());
    }
    parsed.identity.seed = seed;

    std::uint32_t chunkCount = 0;
    if (!reader.Read(chunkCount) || chunkCount > kMaxContainerElements ||
        chunkCount > reader.RemainingInSegment() / 9u) {
        return Fail(error, "world manifest chunk 数量非法");
    }
    parsed.chunks.reserve(std::min<std::uint32_t>(chunkCount, 4096));
    try {
        for (std::uint32_t i = 0; i < chunkCount; ++i) {
            ManifestChunk chunk;
            if (!reader.Read(chunk.chunkX) || !reader.Read(chunk.chunkY) ||
                !reader.Read(chunk.hasDiff)) {
                return Fail(error, "world manifest chunk 读取失败: " + reader.Error());
            }
            parsed.chunks.push_back(chunk);
        }
    } catch (const std::exception&) {
        return Fail(error, "world manifest chunk 内存不足");
    }

    std::uint32_t regionCount = 0;
    if (!reader.Read(regionCount) || regionCount > kMaxContainerElements) {
        return Fail(error, "world manifest region 数量非法");
    }
    parsed.regions.reserve(std::min<std::uint32_t>(regionCount, 1024));
    for (std::uint32_t i = 0; i < regionCount; ++i) {
        ManifestRegion region;
        if (!reader.ReadString(region.id, 128) || !reader.Read(region.phase)) {
            return Fail(error, "world manifest region 读取失败: " + reader.Error());
        }
        parsed.regions.push_back(std::move(region));
    }

    std::uint32_t bucketCount = 0;
    if (!reader.Read(bucketCount) || bucketCount > kMaxContainerElements) {
        return Fail(error, "world manifest bucket 数量非法");
    }
    parsed.buckets.reserve(std::min<std::uint32_t>(bucketCount, 1024));
    for (std::uint32_t i = 0; i < bucketCount; ++i) {
        ManifestEntityBucket bucket;
        if (!reader.ReadString(bucket.id, 128) ||
            !reader.ReadString(bucket.file, 128) ||
            !reader.Read(bucket.entityCount)) {
            return Fail(error, "world manifest bucket 璇诲彇澶辫触: " + reader.Error());
        }
        parsed.buckets.push_back(std::move(bucket));
    }

    if (header.version >= 2u) {
        std::uint32_t eventCount = 0;
        if (!reader.Read(eventCount) || eventCount > 256u) {
            return Fail(error, "world manifest region event 数量非法");
        }
        parsed.events.reserve(eventCount);
        for (std::uint32_t i = 0; i < eventCount; ++i) {
            ManifestRegionEvent event;
            if (!reader.ReadString(event.id, 128) ||
                !reader.ReadString(event.regionId, 128) ||
                !reader.Read(event.kind) || !reader.Read(event.status) ||
                !reader.Read(event.day) || !reader.Read(event.x) ||
                !reader.Read(event.y) || !reader.Read(event.remainingSeconds) ||
                !reader.Read(event.targetCount) ||
                !reader.Read(event.spawnedCount) ||
                !reader.Read(event.contentSpawned)) {
                return Fail(error,
                            "world manifest region event 读取失败: " +
                                reader.Error());
            }
            parsed.events.push_back(std::move(event));
        }
    }

    if (header.version >= 3u) {
        if (!reader.Read(parsed.strategicTickMinute) ||
            !reader.Read(parsed.strategicResources.people) ||
            !reader.Read(parsed.strategicResources.supplies) ||
            !reader.Read(parsed.strategicResources.ammo) ||
            !reader.Read(parsed.strategicResources.fuel)) {
            return Fail(error,
                        "world manifest 战略资源读取失败: " + reader.Error());
        }
        std::uint32_t strategicRegionCount = 0;
        if (!reader.Read(strategicRegionCount) || strategicRegionCount > 64u) {
            return Fail(error, "world manifest 战略区域数量非法");
        }
        parsed.strategicRegions.reserve(strategicRegionCount);
        for (std::uint32_t i = 0; i < strategicRegionCount; ++i) {
            ManifestStrategicRegion region;
            if (!reader.ReadString(region.id, 128) ||
                !reader.Read(region.owner) || !reader.Read(region.siegePhase) ||
                !reader.Read(region.intelLevel) || !reader.Read(region.coreSafe) ||
                !reader.Read(region.garrison) ||
                !reader.Read(region.fortification) ||
                !reader.Read(region.population) ||
                !reader.Read(region.pressure) || !reader.Read(region.defense) ||
                !reader.Read(region.supply) ||                 !reader.Read(region.morale) ||
                !reader.Read(region.nextAttackMinute) ||
                !reader.Read(region.lastEventMinute)) {
                return Fail(error,
                            "world manifest 战略区域读取失败: " +
                                reader.Error());
            }
            if (header.version >= 4u) {
                if (!reader.Read(region.facilityDamage) ||
                    !reader.Read(region.refugees) ||
                    !reader.Read(region.infectionCores) ||
                    !reader.Read(region.recoveryStage) ||
                    !reader.Read(region.recoveryReadyMinute) ||
                    !reader.ReadString(region.refugeeHost, 128)) {
                    return Fail(error,
                                "world manifest 战略恢复读取失败: " +
                                    reader.Error());
                }
            }
            parsed.strategicRegions.push_back(std::move(region));
        }
        std::uint32_t strategicEventCount = 0;
        if (!reader.Read(strategicEventCount) || strategicEventCount > 256u) {
            return Fail(error, "world manifest 战略事件数量非法");
        }
        parsed.strategicEvents.reserve(strategicEventCount);
        for (std::uint32_t i = 0; i < strategicEventCount; ++i) {
            ManifestStrategicEvent event;
            if (!reader.ReadString(event.id, 128) ||
                !reader.ReadString(event.regionId, 128) ||
                !reader.Read(event.kind) || !reader.Read(event.ownerAfter) ||
                !reader.Read(event.gameMinute) || !reader.Read(event.people) ||
                !reader.Read(event.supplies) || !reader.Read(event.ammo) ||
                !reader.Read(event.fuel)) {
                return Fail(error,
                            "world manifest 战略事件读取失败: " +
                                reader.Error());
            }
            parsed.strategicEvents.push_back(std::move(event));
        }
        if (header.version >= 5u) {
            std::uint32_t contributionCount = 0;
            if (!reader.Read(contributionCount) ||
                contributionCount > 1024u) {
                return Fail(error, "world manifest 战略贡献数量非法");
            }
            parsed.strategicContributions.reserve(contributionCount);
            for (std::uint32_t i = 0; i < contributionCount; ++i) {
                ManifestStrategicContribution entry;
                if (!reader.Read(entry.stableId) ||
                    !reader.ReadString(entry.regionId, 128) ||
                    !reader.Read(entry.points)) {
                    return Fail(error,
                                "world manifest 战略贡献读取失败: " +
                                    reader.Error());
                }
                parsed.strategicContributions.push_back(std::move(entry));
            }
        }
    }

    if (header.version >= 6u) {
        std::uint32_t worldEventCount = 0;
        if (!reader.Read(worldEventCount) ||
            worldEventCount > kMaxManifestWorldEvents) {
            return Fail(error, "world manifest 世界事件数量非法");
        }
        parsed.worldEvents.reserve(worldEventCount);
        for (std::uint32_t i = 0; i < worldEventCount; ++i) {
            ManifestWorldEvent event;
            if (!reader.ReadString(event.id, 128) ||
                !reader.ReadString(event.defId, 64) ||
                !reader.ReadString(event.name, 128) ||
                !reader.ReadString(event.kind, 32) ||
                !reader.ReadString(event.regionId, 128) ||
                !reader.Read(event.x) || !reader.Read(event.y) ||
                !reader.Read(event.status) || !reader.Read(event.stageKind) ||
                !reader.Read(event.day) || !reader.Read(event.stageIndex) ||
                !reader.Read(event.stageProgress) ||
                !reader.Read(event.stageTarget) ||
                !reader.Read(event.stageRadius) ||
                !reader.ReadString(event.stageTargetId, 64) ||
                !reader.Read(event.scheduledMinute) ||
                !reader.Read(event.warningMinute) ||
                !reader.Read(event.activeMinute) ||
                !reader.Read(event.deadlineMinute) ||
                !reader.Read(event.resolvedMinute) ||
                !reader.Read(event.chainDepth) || !reader.Read(event.detail) ||
                !reader.Read(event.downgraded) ||
                !reader.Read(event.stagesComplete) ||
                !reader.Read(event.contentSpawned) ||
                !reader.ReadString(event.choiceId, 64) ||
                !reader.ReadString(event.nextEventId, 64)) {
                return Fail(error,
                            "world manifest 世界事件读取失败: " +
                                reader.Error());
            }
            parsed.worldEvents.push_back(std::move(event));
        }
        std::uint32_t worldEventOutcomeCount = 0;
        if (!reader.Read(worldEventOutcomeCount) ||
            worldEventOutcomeCount > kMaxManifestWorldEventOutcomes) {
            return Fail(error, "world manifest 世界事件结果数量非法");
        }
        parsed.worldEventOutcomes.reserve(worldEventOutcomeCount);
        for (std::uint32_t i = 0; i < worldEventOutcomeCount; ++i) {
            std::string key;
            if (!reader.ReadString(key, 192)) {
                return Fail(error,
                            "world manifest 世界事件结果读取失败: " +
                                reader.Error());
            }
            parsed.worldEventOutcomes.push_back(std::move(key));
        }
    }

    if (!reader.EndSegment() || !reader.Ok()) {
        return Fail(error, "world manifest 段校验失败: " + reader.Error());
    }
    if (!reader.AtEnd()) {
        return Fail(error, "world manifest 段后存在多余数据");
    }
    if (!ValidateWorldManifest(parsed, error)) {
        return false;
    }
    *out = std::move(parsed);
    return true;
}

}  // namespace zl::save
