#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace zl::net {

// M33-F2-T5: server-authoritative world event snapshot.
//
// This is intentionally a separate magic message, like ZLS3, so older
// clients simply ignore it. A world event snapshot contains state only; the
// client loads definitions from the same content bundle and never applies
// rewards or advances stages locally.
inline constexpr std::uint32_t kWorldEventSnapshotMagic = 0x32574C5Au;  // "ZLW2"
inline constexpr std::uint16_t kWorldEventSnapshotVersion = 1;
inline constexpr std::size_t kMaxWorldEventSnapshotEntries = 8;
inline constexpr std::size_t kMaxWorldEventSnapshotBytes = 16 * 1024;
inline constexpr std::size_t kMaxWorldEventIdBytes = 128;
inline constexpr std::size_t kMaxWorldEventDefIdBytes = 64;
inline constexpr std::size_t kMaxWorldEventNameBytes = 128;
inline constexpr std::size_t kMaxWorldEventKindBytes = 32;
inline constexpr std::size_t kMaxWorldEventRegionBytes = 128;
inline constexpr std::size_t kMaxWorldEventTargetBytes = 64;

struct WorldEventSnapshotEntry {
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
    std::string choiceId;
    std::string nextEventId;
};

struct WorldEventSnapshot {
    std::uint32_t sequence = 0;
    std::uint64_t serverTick = 0;
    bool enabled = false;
    std::int64_t gameMinute = 0;
    std::vector<WorldEventSnapshotEntry> events;
};

std::vector<std::uint8_t> EncodeWorldEventSnapshot(
    const WorldEventSnapshot& snapshot, std::string* error = nullptr);
bool DecodeWorldEventSnapshot(std::span<const std::uint8_t> bytes,
                              WorldEventSnapshot* snapshot,
                              std::string* error = nullptr);

}  // namespace zl::net
