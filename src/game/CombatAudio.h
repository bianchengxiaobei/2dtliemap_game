#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>

#include "audio/SoundIds.h"

namespace zl::game {

// M30-T10：近战动作音效逻辑名。表现层只选 cue，具体 WAV/音量/可闻半径
// 集中在这里，便于后续替换录制素材而不改战斗逻辑。
enum class CombatCue : std::uint8_t {
    LightSwing = 0,
    HeavySwing = 1,
    Hit = 2,
    Block = 3,
    Parry = 4,
    GuardBreak = 5,
    Dodge = 6,
    Count = 7,
};

struct CombatCueSpec {
    const char* name = "";
    const char* clip = "";
    float radius = 6.0f;
};

inline const CombatCueSpec& CombatCueSpecFor(CombatCue cue) {
    static constexpr CombatCueSpec kSpecs[] = {
        {"light_swing", audio::kMeleeSwingLight, 6.0f},
        {"heavy_swing", audio::kMeleeSwingHeavy, 7.5f},
        {"hit", audio::kMeleeHit, 7.5f},
        {"block", audio::kMeleeBlock, 7.0f},
        {"parry", audio::kMeleeParry, 8.5f},
        {"guard_break", audio::kMeleeGuardBreak, 9.5f},
        {"dodge", audio::kMeleeDodge, 4.5f},
    };
    const auto index = static_cast<std::size_t>(cue);
    if (index >= std::size(kSpecs)) {
        return kSpecs[0];
    }
    return kSpecs[index];
}

inline const char* CombatCueName(CombatCue cue) {
    return CombatCueSpecFor(cue).name;
}

inline const char* CombatCueClip(CombatCue cue) {
    return CombatCueSpecFor(cue).clip;
}

inline float CombatCueRadius(CombatCue cue) {
    return CombatCueSpecFor(cue).radius;
}

}  // namespace zl::game
