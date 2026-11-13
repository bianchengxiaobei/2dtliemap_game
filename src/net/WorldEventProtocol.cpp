#include "net/WorldEventProtocol.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace zl::net {
namespace {

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

void PutU8(std::vector<std::uint8_t>& out, std::uint8_t value) {
    out.push_back(value);
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

void PutI32(std::vector<std::uint8_t>& out, std::int32_t value) {
    PutU32(out, static_cast<std::uint32_t>(value));
}

void PutI64(std::vector<std::uint8_t>& out, std::int64_t value) {
    PutU64(out, static_cast<std::uint64_t>(value));
}

void PutF32(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    PutU32(out, raw);
}

bool PutString(std::vector<std::uint8_t>& out, std::string_view value,
               std::size_t maxBytes) {
    if (value.size() > maxBytes || value.size() > 0xFFFFu) {
        return false;
    }
    PutU16(out, static_cast<std::uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
    return true;
}

bool ValidText(std::string_view value, std::size_t maxBytes, bool nonEmpty) {
    if (nonEmpty && value.empty()) {
        return false;
    }
    if (value.size() > maxBytes) {
        return false;
    }
    for (const unsigned char c : value) {
        if (c < 0x20u || c == 0x7Fu) {
            return false;
        }
    }
    return true;
}

bool ValidFinite(float value, float min, float max) {
    return std::isfinite(value) && value >= min && value <= max;
}

bool ValidEntry(const WorldEventSnapshotEntry& event) {
    if (!ValidText(event.id, kMaxWorldEventIdBytes, true) ||
        !ValidText(event.defId, kMaxWorldEventDefIdBytes, true) ||
        !ValidText(event.name, kMaxWorldEventNameBytes, true) ||
        !ValidText(event.kind, kMaxWorldEventKindBytes, false) ||
        !ValidText(event.regionId, kMaxWorldEventRegionBytes, true) ||
        !ValidText(event.stageTargetId, kMaxWorldEventTargetBytes, false) ||
        !ValidText(event.choiceId, kMaxWorldEventTargetBytes, false) ||
        !ValidText(event.nextEventId, kMaxWorldEventTargetBytes, false)) {
        return false;
    }
    if (event.status > 5u || event.stageKind > 5u || event.day < 1 ||
        event.day > 100000 || event.stageIndex < 0 || event.stageIndex > 8 ||
        event.stageProgress < 0 || event.stageTarget < 0 ||
        event.stageTarget > 10000 || event.stageProgress > event.stageTarget ||
        event.chainDepth < 0 || event.chainDepth > 8 ||
        !ValidFinite(event.x, -1.0e7f, 1.0e7f) ||
        !ValidFinite(event.y, -1.0e7f, 1.0e7f) ||
        !ValidFinite(event.stageRadius, 0.0f, 1000.0f) ||
        event.scheduledMinute < 0 || event.warningMinute < 0 ||
        event.activeMinute < 0 || event.deadlineMinute <= event.activeMinute ||
        event.warningMinute < event.scheduledMinute ||
        event.activeMinute < event.warningMinute ||
        event.resolvedMinute < 0) {
        return false;
    }
    if (event.status == 3u && event.choiceId.empty()) {
        return false;
    }
    return true;
}

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : m_bytes(bytes) {}

    bool U8(std::uint8_t* value) {
        if (!Has(1)) {
            return false;
        }
        *value = m_bytes[m_offset++];
        return true;
    }

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
            result |= static_cast<std::uint64_t>(m_bytes[m_offset + i])
                      << (i * 8U);
        }
        *value = result;
        m_offset += 8;
        return true;
    }

    bool I32(std::int32_t* value) {
        std::uint32_t raw = 0;
        if (!U32(&raw)) {
            return false;
        }
        *value = static_cast<std::int32_t>(raw);
        return true;
    }

    bool I64(std::int64_t* value) {
        std::uint64_t raw = 0;
        if (!U64(&raw)) {
            return false;
        }
        *value = static_cast<std::int64_t>(raw);
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

    bool String(std::string* value, std::size_t maxBytes) {
        std::uint16_t size = 0;
        if (!U16(&size) || size > maxBytes || !Has(size)) {
            return false;
        }
        value->assign(
            reinterpret_cast<const char*>(m_bytes.data() + m_offset), size);
        m_offset += size;
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

std::vector<std::uint8_t> EncodeWorldEventSnapshot(
    const WorldEventSnapshot& snapshot, std::string* error) {
    if (snapshot.events.size() > kMaxWorldEventSnapshotEntries) {
        SetError(error, "world event snapshot has too many entries");
        return {};
    }
    if (!snapshot.enabled && !snapshot.events.empty()) {
        SetError(error, "disabled world event snapshot has entries");
        return {};
    }
    if (snapshot.gameMinute < 0) {
        SetError(error, "world event snapshot game minute is invalid");
        return {};
    }
    for (const WorldEventSnapshotEntry& event : snapshot.events) {
        if (!ValidEntry(event)) {
            SetError(error, "world event snapshot entry is invalid");
            return {};
        }
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(1024);
    PutU32(bytes, kWorldEventSnapshotMagic);
    PutU16(bytes, kWorldEventSnapshotVersion);
    PutU16(bytes, 0);
    PutU32(bytes, snapshot.sequence);
    PutU64(bytes, snapshot.serverTick);
    PutU8(bytes, snapshot.enabled ? 1u : 0u);
    PutI64(bytes, snapshot.gameMinute);
    PutU16(bytes, static_cast<std::uint16_t>(snapshot.events.size()));
    for (const WorldEventSnapshotEntry& event : snapshot.events) {
        if (!PutString(bytes, event.id, kMaxWorldEventIdBytes) ||
            !PutString(bytes, event.defId, kMaxWorldEventDefIdBytes) ||
            !PutString(bytes, event.name, kMaxWorldEventNameBytes) ||
            !PutString(bytes, event.kind, kMaxWorldEventKindBytes) ||
            !PutString(bytes, event.regionId, kMaxWorldEventRegionBytes)) {
            SetError(error, "world event snapshot string is too long");
            return {};
        }
        PutF32(bytes, event.x);
        PutF32(bytes, event.y);
        PutU8(bytes, event.status);
        PutU8(bytes, event.stageKind);
        PutI32(bytes, event.day);
        PutI32(bytes, event.stageIndex);
        PutI32(bytes, event.stageProgress);
        PutI32(bytes, event.stageTarget);
        PutF32(bytes, event.stageRadius);
        if (!PutString(bytes, event.stageTargetId, kMaxWorldEventTargetBytes) ||
            !PutString(bytes, event.choiceId, kMaxWorldEventTargetBytes) ||
            !PutString(bytes, event.nextEventId, kMaxWorldEventTargetBytes)) {
            SetError(error, "world event snapshot string is too long");
            return {};
        }
        PutI64(bytes, event.scheduledMinute);
        PutI64(bytes, event.warningMinute);
        PutI64(bytes, event.activeMinute);
        PutI64(bytes, event.deadlineMinute);
        PutI64(bytes, event.resolvedMinute);
        PutI32(bytes, event.chainDepth);
        PutU8(bytes, event.detail ? 1u : 0u);
        PutU8(bytes, event.downgraded ? 1u : 0u);
        PutU8(bytes, event.stagesComplete ? 1u : 0u);
    }
    if (bytes.size() > kMaxWorldEventSnapshotBytes) {
        SetError(error, "world event snapshot exceeds byte limit");
        return {};
    }
    if (error != nullptr) {
        error->clear();
    }
    return bytes;
}

bool DecodeWorldEventSnapshot(std::span<const std::uint8_t> bytes,
                              WorldEventSnapshot* snapshot,
                              std::string* error) {
    if (snapshot == nullptr) {
        SetError(error, "world event snapshot output is null");
        return false;
    }
    if (bytes.size() > kMaxWorldEventSnapshotBytes) {
        SetError(error, "world event snapshot exceeds byte limit");
        return false;
    }
    Reader reader(bytes);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t reserved = 0;
    if (!reader.U32(&magic) || !reader.U16(&version) ||
        !reader.U16(&reserved) || magic != kWorldEventSnapshotMagic ||
        version != kWorldEventSnapshotVersion || reserved != 0) {
        SetError(error, "world event snapshot header is invalid");
        return false;
    }
    WorldEventSnapshot decoded;
    std::uint8_t enabled = 0;
    std::uint16_t count = 0;
    if (!reader.U32(&decoded.sequence) || !reader.U64(&decoded.serverTick) ||
        !reader.U8(&enabled) || !reader.I64(&decoded.gameMinute) ||
        !reader.U16(&count) || enabled > 1u ||
        count > kMaxWorldEventSnapshotEntries) {
        SetError(error, "world event snapshot header is truncated");
        return false;
    }
    decoded.enabled = enabled != 0;
    decoded.events.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        WorldEventSnapshotEntry event;
        std::uint8_t detail = 0;
        std::uint8_t downgraded = 0;
        std::uint8_t stagesComplete = 0;
        if (!reader.String(&event.id, kMaxWorldEventIdBytes) ||
            !reader.String(&event.defId, kMaxWorldEventDefIdBytes) ||
            !reader.String(&event.name, kMaxWorldEventNameBytes) ||
            !reader.String(&event.kind, kMaxWorldEventKindBytes) ||
            !reader.String(&event.regionId, kMaxWorldEventRegionBytes) ||
            !reader.F32(&event.x) || !reader.F32(&event.y) ||
            !reader.U8(&event.status) || !reader.U8(&event.stageKind) ||
            !reader.I32(&event.day) || !reader.I32(&event.stageIndex) ||
            !reader.I32(&event.stageProgress) ||
            !reader.I32(&event.stageTarget) ||
            !reader.F32(&event.stageRadius) ||
            !reader.String(&event.stageTargetId, kMaxWorldEventTargetBytes) ||
            !reader.String(&event.choiceId, kMaxWorldEventTargetBytes) ||
            !reader.String(&event.nextEventId, kMaxWorldEventTargetBytes) ||
            !reader.I64(&event.scheduledMinute) ||
            !reader.I64(&event.warningMinute) ||
            !reader.I64(&event.activeMinute) ||
            !reader.I64(&event.deadlineMinute) ||
            !reader.I64(&event.resolvedMinute) ||
            !reader.I32(&event.chainDepth) || !reader.U8(&detail) ||
            !reader.U8(&downgraded) || !reader.U8(&stagesComplete) ||
            detail > 1u || downgraded > 1u || stagesComplete > 1u) {
            SetError(error, "world event snapshot entry is truncated");
            return false;
        }
        event.detail = detail != 0;
        event.downgraded = downgraded != 0;
        event.stagesComplete = stagesComplete != 0;
        if (!ValidEntry(event)) {
            SetError(error, "world event snapshot entry is invalid");
            return false;
        }
        decoded.events.push_back(std::move(event));
    }
    if (!reader.AtEnd()) {
        SetError(error, "world event snapshot has trailing data");
        return false;
    }
    if (!decoded.enabled && !decoded.events.empty()) {
        SetError(error, "disabled world event snapshot has entries");
        return false;
    }
    if (decoded.gameMinute < 0) {
        SetError(error, "world event snapshot game minute is invalid");
        return false;
    }
    *snapshot = std::move(decoded);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

}  // namespace zl::net
