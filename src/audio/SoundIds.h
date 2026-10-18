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
