#include "net/WorldSound.h"

#include <cmath>
#include <cstring>
#include <string_view>
#include <utility>

namespace zl::net {
namespace {

constexpr std::uint8_t kMaxWorldSoundType = 6;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
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

void PutF32(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    PutU32(out, raw);
}

bool ValidClip(std::string_view clip) {
    if (clip.size() > kMaxWorldSoundClipBytes) {
        return false;
    }
    for (const unsigned char value : clip) {
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '_' || value == '-' ||
              value == '.')) {
            return false;
        }
    }
    return true;
}

bool ValidFinite(float value, float min, float max) {
    return std::isfinite(value) && value >= min && value <= max;
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

    bool F32(float* value) {
        std::uint32_t raw = 0;
        if (!U32(&raw)) {
            return false;
        }
        std::memcpy(value, &raw, sizeof(raw));
        return true;
    }

    bool Skip(std::size_t count) {
        if (!Has(count)) {
            return false;
        }
        m_offset += count;
        return true;
    }

    bool Bytes(std::size_t count, std::string* value) {
        if (!Has(count)) {
            return false;
        }
        value->assign(
            reinterpret_cast<const char*>(m_bytes.data() + m_offset), count);
        m_offset += count;
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

std::vector<std::uint8_t> EncodeWorldSound(const WorldSoundEvent& sound,
                                           std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (sound.sourcePlayerStableId == 0) {
        SetError(error, "世界声音缺少来源玩家");
        return {};
    }
    if (sound.type > kMaxWorldSoundType) {
        SetError(error, "世界声音类型非法");
        return {};
    }
    if (!ValidFinite(sound.x, -1000000.0f, 1000000.0f) ||
        !ValidFinite(sound.y, -1000000.0f, 1000000.0f) ||
        !ValidFinite(sound.radius, 0.0f, 1024.0f) || sound.z < 0 ||
        sound.z > 8) {
        SetError(error, "世界声音位置或半径非法");
        return {};
    }
    if (!ValidClip(sound.clip)) {
        SetError(error, "世界声音 clip 非法");
        return {};
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(kWorldSoundHeaderSize + sound.clip.size());
    PutU32(bytes, kWorldSoundMagic);
    PutU16(bytes, kWorldSoundVersion);
    bytes.push_back(sound.type);
    bytes.push_back(0);
    PutU64(bytes, sound.sourcePlayerStableId);
    PutF32(bytes, sound.x);
    PutF32(bytes, sound.y);
    PutU32(bytes, static_cast<std::uint32_t>(sound.z));
    PutF32(bytes, sound.radius);
    PutU16(bytes, static_cast<std::uint16_t>(sound.clip.size()));
    bytes.insert(bytes.end(), sound.clip.begin(), sound.clip.end());
    return bytes;
}

bool DecodeWorldSound(std::span<const std::uint8_t> bytes,
                      WorldSoundEvent* sound, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (sound == nullptr) {
        SetError(error, "世界声音输出为空");
        return false;
    }
    if (bytes.size() < kWorldSoundHeaderSize) {
        SetError(error, "世界声音消息头截断");
        return false;
    }

    Reader reader(bytes);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint64_t source = 0;
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t z = 0;
    float radius = 0.0f;
    std::uint16_t clipLength = 0;
    if (!reader.U32(&magic) || !reader.U16(&version) ||
        !reader.Skip(1) || !reader.Skip(1) || !reader.U64(&source) ||
        !reader.F32(&x) || !reader.F32(&y) || !reader.U32(&z) ||
        !reader.F32(&radius) || !reader.U16(&clipLength)) {
        SetError(error, "世界声音消息头截断");
        return false;
    }
    if (magic != kWorldSoundMagic) {
        SetError(error, "世界声音 magic 不匹配");
        return false;
    }
    if (version != kWorldSoundVersion) {
        SetError(error, "世界声音版本不支持");
        return false;
    }
    if (bytes[7] != 0) {
        SetError(error, "世界声音保留字段非 0");
        return false;
    }
    if (source == 0) {
        SetError(error, "世界声音来源玩家非法");
        return false;
    }
    if (bytes[6] > kMaxWorldSoundType) {
        SetError(error, "世界声音类型非法");
        return false;
    }
    if (!ValidFinite(x, -1000000.0f, 1000000.0f) ||
        !ValidFinite(y, -1000000.0f, 1000000.0f) ||
        !ValidFinite(radius, 0.0f, 1024.0f) || z > 8) {
        SetError(error, "世界声音位置或半径非法");
        return false;
    }
    if (clipLength > kMaxWorldSoundClipBytes ||
        bytes.size() != kWorldSoundHeaderSize + clipLength) {
        SetError(error, "世界声音 clip 长度非法");
        return false;
    }

    WorldSoundEvent decoded;
    decoded.type = bytes[6];
    decoded.sourcePlayerStableId = source;
    decoded.x = x;
    decoded.y = y;
    decoded.z = static_cast<int>(z);
    decoded.radius = radius;
    if (!reader.Bytes(clipLength, &decoded.clip) || !reader.AtEnd()) {
        SetError(error, "世界声音 clip 长度非法");
        return false;
    }
    if (!ValidClip(decoded.clip)) {
        SetError(error, "世界声音 clip 非法");
        return false;
    }
    *sound = std::move(decoded);
    return true;
}

}  // namespace zl::net
