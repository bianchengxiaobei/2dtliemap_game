#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace zl::net {

inline constexpr std::uint32_t kSocialStateMagic = 0x32534C5Au;
inline constexpr std::uint16_t kSocialStateVersion = 2;
inline constexpr std::uint32_t kChatMessageMagic = 0x31484C5Au;
inline constexpr std::uint16_t kChatMessageVersion = 1;
inline constexpr std::size_t kMaxSocialPlayers = 32;
inline constexpr std::size_t kMaxSocialChatMessages = 16;
inline constexpr std::size_t kMaxSocialNameBytes = 48;
inline constexpr std::size_t kMaxChatBytes = 200;
inline constexpr std::size_t kMaxSocialStateBytes = 16 * 1024;

struct SocialPlayer {
    std::uint64_t stableId = 0;
    std::string name;
};

struct SocialChatLine {
    std::uint64_t serverTick = 0;
    std::string sender;
    std::string text;
};

struct SocialState {
    std::uint64_t serverTick = 0;
    std::uint32_t sequence = 0;
    double gameMinutes = 0.0;
    std::vector<SocialPlayer> players;
    std::vector<SocialChatLine> chat;
};

struct ChatMessage {
    std::string text;
};

bool IsValidChatText(std::string_view text);

std::vector<std::uint8_t> EncodeChatMessage(const ChatMessage& message,
                                            std::string* error = nullptr);
bool DecodeChatMessage(std::span<const std::uint8_t> bytes, ChatMessage* message,
                       std::string* error = nullptr);

std::vector<std::uint8_t> EncodeSocialState(const SocialState& state,
                                            std::string* error = nullptr);
bool DecodeSocialState(std::span<const std::uint8_t> bytes, SocialState* state,
                       std::string* error = nullptr);

}  // namespace zl::net
