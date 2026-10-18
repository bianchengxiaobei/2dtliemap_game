#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zl::save {

inline constexpr std::uint16_t kSaveFormatVersion = 1;
inline constexpr const char* kDefaultGameVersion = "0.3.0";
inline constexpr const char* kSaveMetaFile = "meta.sqlite";
inline constexpr const char* kSaveHeaderFile = "save.bin";
inline constexpr const char* kSaveEntitiesFile = "entities.bin";
inline constexpr const char* kSaveThumbnailFile = "thumbnail.png";

struct ThumbnailImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;

    bool Valid() const {
        if (width <= 0 || height <= 0) {
            return false;
        }
        const std::uint64_t expected =
            static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4u;
        return rgba.size() == expected;
    }
};

struct SavePlayer {
    std::uint64_t stableId = 0;
    std::string name;
    bool local = false;
};

struct SaveMetadata {
    std::string id;
    std::string name;
    std::int64_t createdAt = 0;
    std::int64_t updatedAt = 0;
    std::uint16_t saveVersion = kSaveFormatVersion;
    std::string gameVersion;
    std::string mapId;
    std::string thumbnail;
    std::vector<SavePlayer> players;
};

struct SaveHeader {
    std::uint16_t version = kSaveFormatVersion;
    std::string gameVersion;
    std::string mapId;
    std::int64_t createdAt = 0;
};

}  // namespace zl::save
