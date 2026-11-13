#include "net/StrategicProtocol.h"

#include <cmath>
#include <cstring>
#include <string_view>
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

void PutString(std::vector<std::uint8_t>& out, std::string_view value) {
    PutU16(out, static_cast<std::uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

bool ValidId(std::string_view value) {
    if (value.empty() || value.size() > kMaxStrategicIdBytes) {
        return false;
    }
    for (const unsigned char c : value) {
        if (c < 0x20 || c == 0x7F) {
            return false;
        }
    }
    return true;
}

bool ValidReason(std::string_view value) {
    if (value.size() > kMaxStrategicReasonBytes) {
        return false;
    }
    for (const unsigned char c : value) {
        if (c < 0x20 || c == 0x7F) {
            return false;
        }
    }
    return true;
}

bool ValidFinite(float value, float min, float max) {
    return std::isfinite(value) && value >= min && value <= max;
}

bool ValidOwner(std::uint8_t value) { return value <= 3; }
bool ValidSiegePhase(std::uint8_t value) { return value <= 4; }
bool ValidRecoveryStage(std::uint8_t value) { return value <= 4; }
bool ValidEventKind(std::uint8_t value) { return value <= 5; }

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

bool ValidRegionSnapshot(const StrategicRegionSnapshot& region) {
    if (!ValidId(region.id) || !ValidOwner(region.owner) ||
        region.contestedFaction > 2 ||
        !ValidSiegePhase(region.siegePhase) ||
        !ValidRecoveryStage(region.recoveryStage) ||
        region.intelLevel < 0 || region.intelLevel > 3 ||
        region.warningIntelLevel < 0 || region.warningIntelLevel > 3 ||
        region.strategicValue < 1 ||
        region.population < 0 || region.garrison < 0 ||
        region.fortification < 0 || region.facilityDamage < 0 ||
        region.facilityDamage > 100 || region.refugees < 0 ||
        region.infectionCores < 0 || region.recoveryReadyMinute < -1 ||
        region.warningMinutes < 0 || region.remainingMinutes < 0 ||
        !ValidFinite(region.pressure, 0.0f, 100.0f) ||
        !ValidFinite(region.defense, 0.0f, 100.0f) ||
        !ValidFinite(region.supply, 0.0f, 100.0f) ||
        !ValidFinite(region.morale, 0.0f, 100.0f)) {
        return false;
    }
    if (region.warningValid &&
        (region.warningMinute < 0 || region.attackMinute < 0 ||
         region.reportedAttackMinute < 0 || region.enemyStrength < 0 ||
         region.reportedEnemyStrength < 0 ||
         region.warningDefenseScore < 0)) {
        return false;
    }
    return true;
}

bool ValidEventSnapshot(const StrategicEventSnapshot& event) {
    return ValidId(event.id) && ValidId(event.regionId) &&
           ValidEventKind(event.kind) && ValidOwner(event.ownerAfter) &&
           event.gameMinute >= 0;
}

}  // namespace

const char* StrategicRequestActionName(StrategicRequestAction action) {
    switch (action) {
        case StrategicRequestAction::Reclaim:
            return "Reclaim";
        case StrategicRequestAction::ClearCore:
            return "ClearCore";
        case StrategicRequestAction::RepairFacility:
            return "RepairFacility";
        case StrategicRequestAction::DonateSupply:
            return "DonateSupply";
    }
    return "Unknown";
}

bool IsValidStrategicRequestAction(StrategicRequestAction action) {
    return action == StrategicRequestAction::Reclaim ||
           action == StrategicRequestAction::ClearCore ||
           action == StrategicRequestAction::RepairFacility ||
           action == StrategicRequestAction::DonateSupply;
}

std::vector<std::uint8_t> EncodeStrategicSnapshot(
    const StrategicSnapshot& snapshot, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (snapshot.regions.size() > kMaxStrategicRegions ||
        snapshot.events.size() > kMaxStrategicEvents ||
        snapshot.gameMinute < 0 || snapshot.tickMinutes <= 0 ||
        snapshot.warningHours < 0 || snapshot.remoteWarningHours < 0 ||
        snapshot.autoResolveGraceMinutes < 0 ||
        snapshot.people < 0 || snapshot.supplies < 0 || snapshot.ammo < 0 ||
        snapshot.fuel < 0) {
        SetError(error, "战略快照头部非法");
        return {};
    }
    for (const StrategicRegionSnapshot& region : snapshot.regions) {
        if (!ValidRegionSnapshot(region)) {
            SetError(error, "战略快照区域非法：" + region.id);
            return {};
        }
    }
    for (const StrategicEventSnapshot& event : snapshot.events) {
        if (!ValidEventSnapshot(event)) {
            SetError(error, "战略快照事件非法：" + event.id);
            return {};
        }
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(256 + snapshot.regions.size() * 128 +
                  snapshot.events.size() * 32);
    PutU32(bytes, kStrategicSnapshotMagic);
    PutU16(bytes, kStrategicSnapshotVersion);
    PutU32(bytes, snapshot.sequence);
    PutU64(bytes, snapshot.serverTick);
    PutU8(bytes, snapshot.enabled ? 1U : 0U);
    PutU8(bytes, 0);
    PutI64(bytes, snapshot.gameMinute);
    PutI32(bytes, snapshot.tickMinutes);
    PutI32(bytes, snapshot.warningHours);
    PutI32(bytes, snapshot.remoteWarningHours);
    PutI32(bytes, snapshot.autoResolveGraceMinutes);
    PutI32(bytes, snapshot.people);
    PutI32(bytes, snapshot.supplies);
    PutI32(bytes, snapshot.ammo);
    PutI32(bytes, snapshot.fuel);
    PutU16(bytes, static_cast<std::uint16_t>(snapshot.regions.size()));
    PutU16(bytes, static_cast<std::uint16_t>(snapshot.events.size()));

    for (const StrategicRegionSnapshot& region : snapshot.regions) {
        PutString(bytes, region.id);
        PutU8(bytes, region.owner);
        PutU8(bytes, region.siegePhase);
        PutU8(bytes, region.recoveryStage);
        PutU8(bytes, static_cast<std::uint8_t>(
                         (region.coreSafe ? 1U : 0U) |
                         (region.remote ? 2U : 0U)));
        PutF32(bytes, region.pressure);
        PutF32(bytes, region.defense);
        PutF32(bytes, region.supply);
        PutF32(bytes, region.morale);
        PutI32(bytes, region.population);
        PutI32(bytes, region.garrison);
        PutI32(bytes, region.fortification);
        PutI32(bytes, region.intelLevel);
        PutI32(bytes, region.strategicValue);
        PutI32(bytes, region.facilityDamage);
        PutI32(bytes, region.refugees);
        PutI32(bytes, region.infectionCores);
        PutI64(bytes, region.recoveryReadyMinute);
        PutU8(bytes, region.warningValid ? 1U : 0U);
        PutI64(bytes, region.warningMinute);
        PutI64(bytes, region.attackMinute);
        PutI64(bytes, region.reportedAttackMinute);
        PutI32(bytes, region.warningMinutes);
        PutI32(bytes, region.remainingMinutes);
        PutI32(bytes, region.warningIntelLevel);
        PutI32(bytes, region.enemyStrength);
        PutI32(bytes, region.reportedEnemyStrength);
        PutI32(bytes, region.warningDefenseScore);
        PutU8(bytes, region.contestedFaction);
    }
    for (const StrategicEventSnapshot& event : snapshot.events) {
        PutString(bytes, event.id);
        PutString(bytes, event.regionId);
        PutU8(bytes, event.kind);
        PutU8(bytes, event.ownerAfter);
        PutI64(bytes, event.gameMinute);
        PutI32(bytes, event.people);
        PutI32(bytes, event.supplies);
        PutI32(bytes, event.ammo);
        PutI32(bytes, event.fuel);
    }
    if (bytes.size() > kMaxStrategicSnapshotBytes) {
        SetError(error, "战略快照超过大小上限");
        return {};
    }
    return bytes;
}

bool DecodeStrategicSnapshot(std::span<const std::uint8_t> bytes,
                             StrategicSnapshot* snapshot,
                             std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (snapshot == nullptr) {
        SetError(error, "战略快照输出为空");
        return false;
    }
    if (bytes.size() > kMaxStrategicSnapshotBytes) {
        SetError(error, "战略快照超过大小上限");
        return false;
    }
    Reader reader(bytes);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint8_t flags = 0;
    std::uint8_t reserved = 0;
    std::uint16_t regionCount = 0;
    std::uint16_t eventCount = 0;
    StrategicSnapshot decoded;
    if (!reader.U32(&magic) || magic != kStrategicSnapshotMagic ||
        !reader.U16(&version) || version != kStrategicSnapshotVersion ||
        !reader.U32(&decoded.sequence) || !reader.U64(&decoded.serverTick) ||
        !reader.U8(&flags) || (flags & 0xFEu) != 0 ||
        !reader.U8(&reserved) || reserved != 0 ||
        !reader.I64(&decoded.gameMinute) ||
        !reader.I32(&decoded.tickMinutes) ||
        !reader.I32(&decoded.warningHours) ||
        !reader.I32(&decoded.remoteWarningHours) ||
        !reader.I32(&decoded.autoResolveGraceMinutes) ||
        !reader.I32(&decoded.people) || !reader.I32(&decoded.supplies) ||
        !reader.I32(&decoded.ammo) || !reader.I32(&decoded.fuel) ||
        !reader.U16(&regionCount) || !reader.U16(&eventCount)) {
        SetError(error, "战略快照头部截断或版本不支持");
        return false;
    }
    if (regionCount > kMaxStrategicRegions ||
        eventCount > kMaxStrategicEvents) {
        SetError(error, "战略快照数量超过上限");
        return false;
    }
    if (decoded.gameMinute < 0 || decoded.tickMinutes <= 0 ||
        decoded.warningHours < 0 || decoded.remoteWarningHours < 0 ||
        decoded.autoResolveGraceMinutes < 0 || decoded.people < 0 ||
        decoded.supplies < 0 || decoded.ammo < 0 || decoded.fuel < 0) {
        SetError(error, "战略快照头部字段非法");
        return false;
    }
    decoded.enabled = (flags & 1U) != 0;
    decoded.regions.reserve(regionCount);
    for (std::size_t i = 0; i < regionCount; ++i) {
        StrategicRegionSnapshot region;
        std::uint8_t regionFlags = 0;
        if (!reader.String(&region.id, kMaxStrategicIdBytes) ||
            !reader.U8(&region.owner) || !reader.U8(&region.siegePhase) ||
            !reader.U8(&region.recoveryStage) ||
            !reader.U8(&regionFlags) || (regionFlags & 0xFCu) != 0 ||
            !reader.F32(&region.pressure) || !reader.F32(&region.defense) ||
            !reader.F32(&region.supply) || !reader.F32(&region.morale) ||
            !reader.I32(&region.population) ||
            !reader.I32(&region.garrison) ||
            !reader.I32(&region.fortification) ||
            !reader.I32(&region.intelLevel) ||
            !reader.I32(&region.strategicValue) ||
            !reader.I32(&region.facilityDamage) ||
            !reader.I32(&region.refugees) ||
            !reader.I32(&region.infectionCores) ||
            !reader.I64(&region.recoveryReadyMinute)) {
            SetError(error, "战略快照区域截断");
            return false;
        }
        region.coreSafe = (regionFlags & 1U) != 0;
        region.remote = (regionFlags & 2U) != 0;
        std::uint8_t warningFlags = 0;
        if (!reader.U8(&warningFlags) || (warningFlags & 0xFEu) != 0 ||
            !reader.I64(&region.warningMinute) ||
            !reader.I64(&region.attackMinute) ||
            !reader.I64(&region.reportedAttackMinute) ||
            !reader.I32(&region.warningMinutes) ||
            !reader.I32(&region.remainingMinutes) ||
            !reader.I32(&region.warningIntelLevel) ||
            !reader.I32(&region.enemyStrength) ||
            !reader.I32(&region.reportedEnemyStrength) ||
            !reader.I32(&region.warningDefenseScore) ||
            !reader.U8(&region.contestedFaction)) {
            SetError(error, "战略快照预警截断");
            return false;
        }
        region.warningValid = (warningFlags & 1U) != 0;
        if (!ValidRegionSnapshot(region)) {
            SetError(error, "战略快照区域字段非法：" + region.id);
            return false;
        }
        decoded.regions.push_back(std::move(region));
    }
    decoded.events.reserve(eventCount);
    for (std::size_t i = 0; i < eventCount; ++i) {
        StrategicEventSnapshot event;
        if (!reader.String(&event.id, kMaxStrategicIdBytes) ||
            !reader.String(&event.regionId, kMaxStrategicIdBytes) ||
            !reader.U8(&event.kind) || !reader.U8(&event.ownerAfter) ||
            !reader.I64(&event.gameMinute) ||
            !reader.I32(&event.people) || !reader.I32(&event.supplies) ||
            !reader.I32(&event.ammo) || !reader.I32(&event.fuel)) {
            SetError(error, "战略快照事件截断");
            return false;
        }
        if (!ValidEventSnapshot(event)) {
            SetError(error, "战略快照事件字段非法：" + event.id);
            return false;
        }
        decoded.events.push_back(std::move(event));
    }
    if (!reader.AtEnd()) {
        SetError(error, "战略快照尾部有多余字节");
        return false;
    }
    *snapshot = std::move(decoded);
    return true;
}

std::vector<std::uint8_t> EncodeStrategicRequest(
    const StrategicRequest& request, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (request.sequence == 0 ||
        !IsValidStrategicRequestAction(request.action) ||
        !ValidId(request.regionId) || request.amount < 0 ||
        request.amount > 1000) {
        SetError(error, "战略请求字段非法");
        return {};
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kMaxStrategicRequestBytes);
    PutU32(bytes, kStrategicRequestMagic);
    PutU16(bytes, kStrategicRequestVersion);
    PutU32(bytes, request.sequence);
    PutU8(bytes, static_cast<std::uint8_t>(request.action));
    PutU8(bytes, 0);
    PutString(bytes, request.regionId);
    PutI32(bytes, request.amount);
    if (bytes.size() > kMaxStrategicRequestBytes) {
        SetError(error, "战略请求超过大小上限");
        return {};
    }
    return bytes;
}

bool DecodeStrategicRequest(std::span<const std::uint8_t> bytes,
                            StrategicRequest* request,
                            std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (request == nullptr || bytes.size() > kMaxStrategicRequestBytes) {
        SetError(error, "战略请求为空或超过大小上限");
        return false;
    }
    Reader reader(bytes);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint8_t action = 0;
    std::uint8_t reserved = 0;
    StrategicRequest decoded;
    if (!reader.U32(&magic) || magic != kStrategicRequestMagic ||
        !reader.U16(&version) || version != kStrategicRequestVersion ||
        !reader.U32(&decoded.sequence) || !reader.U8(&action) ||
        !reader.U8(&reserved) || reserved != 0 ||
        !reader.String(&decoded.regionId, kMaxStrategicIdBytes) ||
        !reader.I32(&decoded.amount) || !reader.AtEnd()) {
        SetError(error, "战略请求截断或版本不支持");
        return false;
    }
    decoded.action = static_cast<StrategicRequestAction>(action);
    if (decoded.sequence == 0 || !IsValidStrategicRequestAction(decoded.action) ||
        !ValidId(decoded.regionId) || decoded.amount < 0 ||
        decoded.amount > 1000) {
        SetError(error, "战略请求字段非法");
        return false;
    }
    *request = std::move(decoded);
    return true;
}

std::vector<std::uint8_t> EncodeStrategicResult(
    const StrategicRequestResult& result, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (result.sequence == 0 ||
        !IsValidStrategicRequestAction(result.action) ||
        !ValidId(result.regionId) || !ValidReason(result.reason)) {
        SetError(error, "战略结果字段非法");
        return {};
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kMaxStrategicResultBytes);
    PutU32(bytes, kStrategicResultMagic);
    PutU16(bytes, kStrategicResultVersion);
    PutU32(bytes, result.sequence);
    PutU8(bytes, static_cast<std::uint8_t>(result.action));
    PutU8(bytes, result.accepted ? 1U : 0U);
    PutString(bytes, result.regionId);
    PutString(bytes, result.reason);
    if (bytes.size() > kMaxStrategicResultBytes) {
        SetError(error, "战略结果超过大小上限");
        return {};
    }
    return bytes;
}

bool DecodeStrategicResult(std::span<const std::uint8_t> bytes,
                           StrategicRequestResult* result,
                           std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (result == nullptr || bytes.size() > kMaxStrategicResultBytes) {
        SetError(error, "战略结果为空或超过大小上限");
        return false;
    }
    Reader reader(bytes);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint8_t action = 0;
    std::uint8_t accepted = 0;
    StrategicRequestResult decoded;
    if (!reader.U32(&magic) || magic != kStrategicResultMagic ||
        !reader.U16(&version) || version != kStrategicResultVersion ||
        !reader.U32(&decoded.sequence) || !reader.U8(&action) ||
        !reader.U8(&accepted) || accepted > 1 ||
        !reader.String(&decoded.regionId, kMaxStrategicIdBytes) ||
        !reader.String(&decoded.reason, kMaxStrategicReasonBytes) ||
        !reader.AtEnd()) {
        SetError(error, "战略结果截断或版本不支持");
        return false;
    }
    decoded.action = static_cast<StrategicRequestAction>(action);
    decoded.accepted = accepted != 0;
    if (decoded.sequence == 0 ||
        !IsValidStrategicRequestAction(decoded.action) ||
        !ValidId(decoded.regionId) || !ValidReason(decoded.reason)) {
        SetError(error, "战略结果字段非法");
        return false;
    }
    *result = std::move(decoded);
    return true;
}

}  // namespace zl::net
