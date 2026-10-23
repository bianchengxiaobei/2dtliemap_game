#include "net/SocialProtocol.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>
#include <utility>

namespace zl::net {
namespace {

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool IsContinuation(unsigned char value) { return (value & 0xC0u) == 0x80u; }

bool IsValidUtf8(std::string_view text) {
    std::size_t index = 0;
    while (index < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        if (lead <= 0x7Fu) {
            ++index;
            continue;
        }
        if (lead >= 0xC2u && lead <= 0xDFu) {
            if (index + 1 >= text.size() ||
                !IsContinuation(static_cast<unsigned char>(text[index + 1]))) {
                return false;
            }
            index += 2;
            continue;
        }
        if (lead >= 0xE0u && lead <= 0xEFu) {
            if (index + 2 >= text.size()) {
                return false;
            }
            const unsigned char second = static_cast<unsigned char>(text[index + 1]);
            const unsigned char third = static_cast<unsigned char>(text[index + 2]);
            if (!IsContinuation(third)) {
                return false;
            }
            if (lead == 0xE0u) {
                if (second < 0xA0u || second > 0xBFu) {
                    return false;
                }
            } else if (lead == 0xEDu) {
                if (second < 0x80u || second > 0x9Fu) {
                    return false;
                }
            } else if (!IsContinuation(second)) {
                return false;
            }
            index += 3;
            continue;
        }
        if (lead >= 0xF0u && lead <= 0xF4u) {
            if (index + 3 >= text.size()) {
                return false;
            }
            const unsigned char second = static_cast<unsigned char>(text[index + 1]);
            const unsigned char third = static_cast<unsigned char>(text[index + 2]);
            const unsigned char fourth = static_cast<unsigned char>(text[index + 3]);
            if (!IsContinuation(third) || !IsContinuation(fourth)) {
                return false;
            }
            if (lead == 0xF0u) {
                if (second < 0x90u || second > 0xBFu) {
                    return false;
                }
            } else if (lead == 0xF4u) {
                if (second < 0x80u || second > 0x8Fu) {
                    return false;
                }
            } else if (!IsContinuation(second)) {
                return false;
            }
            index += 4;
            continue;
        }
        return false;
    }
    return true;
}

bool ValidDisplayText(std::string_view text, std::size_t maxBytes) {
    if (text.empty() || text.size() > maxBytes || !IsValidUtf8(text)) {
        return false;
    }
    for (const unsigned char value : text) {
        if (value < 0x20u || value == 0x7Fu) {
            return false;
        }
    }
    return true;
}

void PutU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFu));
}

void PutU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFu));
    }
}

void PutU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFu));
    }
}

void PutF64(std::vector<std::uint8_t>& out, double value) {
    std::uint64_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    PutU64(out, raw);
}

bool PutString(std::vector<std::uint8_t>& out, std::string_view value,
               std::size_t maxBytes, std::string* error) {
    if (!ValidDisplayText(value, maxBytes)) {
        SetError(error, "社交协议字符串非法");
        return false;
    }
    PutU16(out, static_cast<std::uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
    return true;
}

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : m_bytes(bytes) {}

    bool U16(std::uint16_t* value) {
        if (!Has(2)) {
            return false;
        }
        *value = static_cast<std::uint16_t>(m_bytes[m_offset]) |
                 (static_cast<std::uint16_t>(m_bytes[m_offset + 1]) << 8U);
        m_offset += 2;
        return true;
    }

    bool U32(std::uint32_t* value) {
        if (!Has(4)) {
            return false;
        }
        *value = static_cast<std::uint32_t>(m_bytes[m_offset]) |
                 (static_cast<std::uint32_t>(m_bytes[m_offset + 1]) << 8U) |
                 (static_cast<std::uint32_t>(m_bytes[m_offset + 2]) << 16U) |
                 (static_cast<std::uint32_t>(m_bytes[m_offset + 3]) << 24U);
        m_offset += 4;
        return true;
    }

    bool U64(std::uint64_t* value) {
        if (!Has(8)) {
            return false;
        }
        std::uint64_t result = 0;
        for (std::size_t i = 0; i < 8; ++i) {
            result |= static_cast<std::uint64_t>(m_bytes[m_offset + i]) << (i * 8U);
        }
        *value = result;
        m_offset += 8;
        return true;
    }

    bool F64(double* value) {
        std::uint64_t raw = 0;
        if (!U64(&raw)) {
            return false;
        }
        std::memcpy(value, &raw, sizeof(raw));
        return true;
    }

    bool String(std::string* value, std::size_t maxBytes, bool* valid) {
        std::uint16_t size = 0;
        if (!U16(&size) || size > maxBytes || !Has(size)) {
            return false;
        }
        value->assign(m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset),
                      m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset + size));
        m_offset += size;
        *valid = ValidDisplayText(*value, maxBytes);
        return true;
    }

    bool AtEnd() const { return m_offset == m_bytes.size(); }

private:
    bool Has(std::size_t count) const {
        return count <= m_bytes.size() - m_offset;
    }

    std::span<const std::uint8_t> m_bytes;
    std::size_t m_offset = 0;
};

}  // namespace

bool IsValidChatText(std::string_view text) {
    return ValidDisplayText(text, kMaxChatBytes);
}

std::vector<std::uint8_t> EncodeChatMessage(const ChatMessage& message,
                                            std::string* error) {
    if (!IsValidChatText(message.text)) {
        SetError(error, "聊天文本为空、过长或包含非法字符");
        return {};
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(8 + message.text.size());
    PutU32(bytes, kChatMessageMagic);
    PutU16(bytes, kChatMessageVersion);
    PutU16(bytes, static_cast<std::uint16_t>(message.text.size()));
    bytes.insert(bytes.end(), message.text.begin(), message.text.end());
    return bytes;
}

bool DecodeChatMessage(std::span<const std::uint8_t> bytes, ChatMessage* message,
                       std::string* error) {
    if (message == nullptr) {
        SetError(error, "输出聊天消息为空");
        return false;
    }
    Reader reader(bytes);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t length = 0;
    if (!reader.U32(&magic) || !reader.U16(&version) || !reader.U16(&length)) {
        SetError(error, "聊天消息头截断");
        return false;
    }
    if (magic != kChatMessageMagic) {
        SetError(error, "聊天消息 magic 不匹配");
        return false;
    }
    if (version != kChatMessageVersion) {
        SetError(error, "聊天消息版本不支持");
        return false;
    }
    if (length > kMaxChatBytes || bytes.size() != 8 + length) {
        SetError(error, "聊天消息长度非法");
        return false;
    }
    std::string text(reinterpret_cast<const char*>(bytes.data() + 8), length);
    if (!IsValidChatText(text)) {
        SetError(error, "聊天文本非法");
        return false;
    }
    message->text = std::move(text);
    return true;
}

std::vector<std::uint8_t> EncodeSocialState(const SocialState& state,
                                            std::string* error) {
    if (state.players.size() > kMaxSocialPlayers) {
        SetError(error, "玩家列表超过上限");
        return {};
    }
    if (state.chat.size() > kMaxSocialChatMessages) {
        SetError(error, "聊天记录超过上限");
        return {};
    }

    if (!std::isfinite(state.gameMinutes) || state.gameMinutes < 0.0) {
        SetError(error, "游戏时间非法");
        return {};
    }

    std::unordered_set<std::uint64_t> ids;
    std::size_t estimated = 32;
    for (const SocialPlayer& player : state.players) {
        if (player.stableId == 0 || !ids.insert(player.stableId).second ||
            !ValidDisplayText(player.name, kMaxSocialNameBytes)) {
            SetError(error, "玩家列表条目非法");
            return {};
        }
        estimated += 10 + player.name.size();
    }
    for (const SocialChatLine& line : state.chat) {
        if (line.serverTick > state.serverTick ||
            !ValidDisplayText(line.sender, kMaxSocialNameBytes) ||
            !IsValidChatText(line.text)) {
            SetError(error, "聊天记录条目非法");
            return {};
        }
        estimated += 12 + line.sender.size() + line.text.size();
    }
    if (estimated > kMaxSocialStateBytes) {
        SetError(error, "社交状态超过字节上限");
        return {};
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(estimated);
    PutU32(bytes, kSocialStateMagic);
    PutU16(bytes, kSocialStateVersion);
    PutU16(bytes, 0);
    PutU64(bytes, state.serverTick);
    PutU32(bytes, state.sequence);
    PutF64(bytes, state.gameMinutes);
    PutU16(bytes, static_cast<std::uint16_t>(state.players.size()));
    PutU16(bytes, static_cast<std::uint16_t>(state.chat.size()));
    for (const SocialPlayer& player : state.players) {
        PutU64(bytes, player.stableId);
        if (!PutString(bytes, player.name, kMaxSocialNameBytes, error)) {
            return {};
        }
    }
    for (const SocialChatLine& line : state.chat) {
        PutU64(bytes, line.serverTick);
        if (!PutString(bytes, line.sender, kMaxSocialNameBytes, error) ||
            !PutString(bytes, line.text, kMaxChatBytes, error)) {
            return {};
        }
    }
    return bytes;
}

bool DecodeSocialState(std::span<const std::uint8_t> bytes, SocialState* state,
                       std::string* error) {
    if (state == nullptr) {
        SetError(error, "输出社交状态为空");
        return false;
    }
    if (bytes.size() > kMaxSocialStateBytes) {
        SetError(error, "社交状态超过字节上限");
        return false;
    }
    Reader reader(bytes);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t reserved = 0;
    std::uint16_t playerCount = 0;
    std::uint16_t chatCount = 0;
    SocialState decoded;
    if (!reader.U32(&magic) || !reader.U16(&version) || !reader.U16(&reserved) ||
        !reader.U64(&decoded.serverTick) || !reader.U32(&decoded.sequence) ||
        !reader.F64(&decoded.gameMinutes) || !reader.U16(&playerCount) ||
        !reader.U16(&chatCount)) {
        SetError(error, "社交状态头截断");
        return false;
    }
    if (magic != kSocialStateMagic) {
        SetError(error, "社交状态 magic 不匹配");
        return false;
    }
    if (version != kSocialStateVersion) {
        SetError(error, "社交状态版本不支持");
        return false;
    }
    if (reserved != 0) {
        SetError(error, "社交状态保留字段非 0");
        return false;
    }
    if (!std::isfinite(decoded.gameMinutes) || decoded.gameMinutes < 0.0) {
        SetError(error, "社交状态游戏时间非法");
        return false;
    }
    if (playerCount > kMaxSocialPlayers || chatCount > kMaxSocialChatMessages) {
        SetError(error, "社交状态条目数量超过上限");
        return false;
    }

    std::unordered_set<std::uint64_t> ids;
    decoded.players.reserve(playerCount);
    for (std::uint16_t i = 0; i < playerCount; ++i) {
        SocialPlayer player;
        bool validName = false;
        if (!reader.U64(&player.stableId) ||
            !reader.String(&player.name, kMaxSocialNameBytes, &validName) ||
            !validName) {
            SetError(error, "社交玩家条目截断或非法");
            return false;
        }
        if (player.stableId == 0 || !ids.insert(player.stableId).second) {
            SetError(error, "社交玩家 StableId 非法或重复");
            return false;
        }
        decoded.players.push_back(std::move(player));
    }

    decoded.chat.reserve(chatCount);
    for (std::uint16_t i = 0; i < chatCount; ++i) {
        SocialChatLine line;
        bool validSender = false;
        bool validText = false;
        if (!reader.U64(&line.serverTick) ||
            !reader.String(&line.sender, kMaxSocialNameBytes, &validSender) ||
            !reader.String(&line.text, kMaxChatBytes, &validText) || !validSender ||
            !validText) {
            SetError(error, "聊天记录条目截断或非法");
            return false;
        }
        if (line.serverTick > decoded.serverTick) {
            SetError(error, "聊天记录 tick 超过服务器 tick");
            return false;
        }
        decoded.chat.push_back(std::move(line));
    }
    if (!reader.AtEnd()) {
        SetError(error, "社交状态尾部存在多余字节");
        return false;
    }
    *state = std::move(decoded);
    return true;
}

}  // namespace zl::net
