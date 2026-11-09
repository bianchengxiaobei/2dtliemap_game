#pragma once

#include <string_view>

namespace zl::audio {

// M11-T3：游戏音效逻辑名（AudioEngine 会追加 .wav）。
inline constexpr const char* kGunshot = "gunshot";
inline constexpr const char* kImpact = "impact";
inline constexpr const char* kZombieGrowl = "zombie_growl";
inline constexpr const char* kUiClick = "ui_click";
inline constexpr const char* kPickup = "pickup";
inline constexpr const char* kMusicCalm = "music_calm";
inline constexpr const char* kAmbientDay = "ambient_day";
inline constexpr const char* kAmbientNight = "ambient_night";
inline constexpr const char* kFootstepGrass = "footstep_grass";
inline constexpr const char* kFootstepDirt = "footstep_dirt";
inline constexpr const char* kFootstepWood = "footstep_wood";
inline constexpr const char* kVehicleEngine = "vehicle_engine";
inline constexpr const char* kVehicleRam = "vehicle_ram";
inline constexpr const char* kVehicleHorn = "vehicle_horn";
inline constexpr const char* kVehicleHit = "vehicle_hit";
// M30-T10：近战挥击/命中/防御反馈；由 CombatAudio 映射，占位 WAV 可替换。
inline constexpr const char* kMeleeSwingLight = "melee_swing_light";
inline constexpr const char* kMeleeSwingHeavy = "melee_swing_heavy";
inline constexpr const char* kMeleeHit = "melee_hit";
inline constexpr const char* kMeleeBlock = "melee_block";
inline constexpr const char* kMeleeParry = "melee_parry";
inline constexpr const char* kMeleeGuardBreak = "melee_guard_break";
inline constexpr const char* kMeleeDodge = "melee_dodge";

// 地面逻辑 sprite → 脚步音效。未知地面按草地处理。
inline std::string_view FootstepClipForFloor(std::string_view floorSprite) {
    if (floorSprite == "tile.floor.wood") {
        return kFootstepWood;
    }
    if (floorSprite == "tile.floor.dirt") {
        return kFootstepDirt;
    }
    return kFootstepGrass;
}

}  // namespace zl::audio
