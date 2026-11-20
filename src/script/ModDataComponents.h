#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ecs/EcsWorld.h"

namespace zl::ecs {

inline constexpr std::size_t kMaxModDataMods = 256;
inline constexpr std::size_t kMaxModDataEntriesPerMod = 64;
inline constexpr std::size_t kMaxModDataModIdBytes = 128;
inline constexpr std::size_t kMaxModDataKeyBytes = 64;
inline constexpr std::size_t kMaxModDataValueBytes = 4096;
inline constexpr std::size_t kMaxModDataTotalBytes = 128 * 1024;

struct ModDataTag {};

struct ModDataEntry {
    std::string key;
    std::string value;
};

struct ModData {
    std::vector<std::pair<std::string, std::vector<ModDataEntry>>> mods;
};

inline bool IsValidModDataId(std::string_view id) {
    if (id.empty() || id.size() > kMaxModDataModIdBytes) {
        return false;
    }
    for (const char ch : id) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte < 0x20u || byte == 0x7Fu) {
            return false;
        }
    }
    return true;
}

inline bool IsValidModDataKey(std::string_view key) {
    if (key.empty() || key.size() > kMaxModDataKeyBytes) {
        return false;
    }
    for (const char ch : key) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte < 0x20u || byte == 0x7Fu) {
            return false;
        }
    }
    return true;
}

inline bool IsValidModData(const ModData& data) {
    if (data.mods.size() > kMaxModDataMods) {
        return false;
    }
    std::unordered_set<std::string> modIds;
    std::size_t totalBytes = 0;
    for (const auto& [modId, entries] : data.mods) {
        if (!IsValidModDataId(modId) || !modIds.insert(modId).second ||
            entries.size() > kMaxModDataEntriesPerMod) {
            return false;
        }
        std::unordered_set<std::string> keys;
        for (const ModDataEntry& entry : entries) {
            if (!IsValidModDataKey(entry.key) ||
                !keys.insert(entry.key).second ||
                entry.value.size() > kMaxModDataValueBytes) {
                return false;
            }
            totalBytes += entry.key.size() + entry.value.size();
            if (totalBytes > kMaxModDataTotalBytes) {
                return false;
            }
        }
    }
    return true;
}

inline Entity FindModDataEntity(const EcsWorld& world) {
    auto view = world.Registry().view<ModData>();
    const auto it = view.begin();
    return it != view.end() ? *it : EcsWorld::kNull;
}

inline Entity EnsureModDataEntity(EcsWorld& world) {
    entt::registry& registry = world.Registry();
    auto view = registry.view<ModData>();
    const auto it = view.begin();
    if (it != view.end()) {
        const Entity entity = *it;
        registry.emplace_or_replace<ModDataTag>(entity);
        return entity;
    }
    auto tags = registry.view<ModDataTag>();
    const auto tagIt = tags.begin();
    if (tagIt != tags.end()) {
        const Entity entity = *tagIt;
        registry.emplace_or_replace<ModData>(entity);
        return entity;
    }
    const Entity entity = world.Create();
    registry.emplace<ModDataTag>(entity);
    registry.emplace<ModData>(entity);
    return entity;
}

inline ModData& EnsureModData(EcsWorld& world) {
    const Entity entity = EnsureModDataEntity(world);
    return world.Registry().get<ModData>(entity);
}

}  // namespace zl::ecs
