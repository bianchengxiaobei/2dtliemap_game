#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace zl::net {

// M23-F1-T11：服务器权威枪声的远端表现同步。
// 服务器仍用 core::SoundEvent 驱动 AI；网络客户端只根据本消息播放音频和枪口效果。
inline constexpr std::uint32_t kWorldSoundMagic = 0x31574C5Au;  // "ZLW1"
inline constexpr std::uint16_t kWorldSoundVersion = 1;
inline constexpr std::size_t kWorldSoundHeaderSize = 34;
inline constexpr std::size_t kMaxWorldSoundClipBytes = 64;

struct WorldSoundEvent {
    std::uint64_t sourcePlayerStableId = 0;
    float x = 0.0f;
    float y = 0.0f;
    int z = 0;
    float radius = 0.0f;
    std::uint8_t type = 0;
    std::string clip;
};

std::vector<std::uint8_t> EncodeWorldSound(const WorldSoundEvent& sound,
                                           std::string* error = nullptr);
bool DecodeWorldSound(std::span<const std::uint8_t> bytes,
                      WorldSoundEvent* sound, std::string* error = nullptr);

}  // namespace zl::net
