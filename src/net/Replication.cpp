#include "net/Replication.h"

#include <algorithm>
#include <bit>
#include <unordered_set>
#include <utility>

namespace zl::net {
namespace {

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

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : m_bytes(bytes) {}

    bool U8(std::uint8_t* value) {
        if (!Has(1)) {
            return false;
        }
        *value = m_bytes[m_offset];
        ++m_offset;
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
            result |= static_cast<std::uint64_t>(m_bytes[m_offset + i]) << (i * 8U);
        }
        *value = result;
        m_offset += 8;
        return true;
    }

    bool Bytes(std::vector<std::uint8_t>* out, std::size_t size) {
        if (!Has(size)) {
            return false;
        }
        out->assign(m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset),
                    m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset + size));
        m_offset += size;
        return true;
    }

    bool AtEnd() const { return m_offset == m_bytes.size(); }
    bool Has(std::size_t count) const { return count <= m_bytes.size() - m_offset; }
    std::size_t Remaining() const { return m_bytes.size() - m_offset; }

private:
    std::span<const std::uint8_t> m_bytes;
    std::size_t m_offset = 0;
};

bool AddSize(std::size_t* total, std::size_t addition, std::string* error) {
    if (addition > kMaxReplicationPacketBytes - *total) {
        SetError(error, "复制快照超过单包字节上限");
        return false;
    }
    *total += addition;
    return true;
}

bool ValidComponentPayload(const ReplicatedComponent& component,
                           const ReplicationRegistry& registry, std::string* error) {
    const ReplicationComponentSpec* spec = registry.Find(component.type);
    if (spec == nullptr) {
        SetError(error, "复制组件未注册（type " +
                            std::to_string(component.type) + "）");
        return false;
    }
    if (component.version != spec->version) {
        SetError(error, "复制组件版本不匹配：" + spec->name + "（收到 " +
                            std::to_string(component.version) + "，期望 " +
                            std::to_string(spec->version) + "）");
        return false;
    }
    if (component.data.size() > spec->maxBytes ||
        component.data.size() > kMaxReplicationComponentBytes) {
        SetError(error, "复制组件负载超限：" + spec->name);
        return false;
    }
    return true;
}

}  // namespace

bool ReplicationRegistry::Register(ReplicationComponentSpec spec,
                                   std::string* error) {
    if (spec.type == 0) {
        SetError(error, "复制组件 type 不能为 0");
        return false;
    }
    if (spec.name.empty() || spec.name.size() > kMaxReplicationComponentNameBytes) {
        SetError(error, "复制组件名称长度非法");
        return false;
    }
    if (spec.version == 0) {
        SetError(error, "复制组件版本不能为 0");
        return false;
    }
    if (spec.maxBytes == 0 || spec.maxBytes > kMaxReplicationComponentBytes) {
        SetError(error, "复制组件负载上限非法");
        return false;
    }
    if (m_byType.find(spec.type) != m_byType.end()) {
        SetError(error, "复制组件 type 重复注册");
        return false;
    }
    const std::size_t index = m_specs.size();
    m_specs.push_back(std::move(spec));
    m_byType.emplace(m_specs.back().type, index);
    return true;
}

bool ReplicationRegistry::Register(std::uint32_t type, std::string name,
                                   std::uint32_t maxBytes, std::uint16_t version,
                                   std::string* error) {
    ReplicationComponentSpec spec;
    spec.type = type;
    spec.name = std::move(name);
    spec.version = version;
    spec.maxBytes = maxBytes;
    return Register(std::move(spec), error);
}

const ReplicationComponentSpec* ReplicationRegistry::Find(std::uint32_t type) const {
    const auto it = m_byType.find(type);
    return it == m_byType.end() ? nullptr : &m_specs[it->second];
}

const ReplicatedComponent* ReplicatedEntity::Find(std::uint32_t type) const {
    for (const ReplicatedComponent& component : components) {
        if (component.type == type) {
            return &component;
        }
    }
    return nullptr;
}

std::vector<std::uint8_t> EncodeReplicationSnapshot(
    const ReplicationSnapshot& snapshot, const ReplicationRegistry& registry,
    std::string* error) {
    if (snapshot.serverTick == 0) {
        SetError(error, "复制快照 serverTick 不能为 0");
        return {};
    }
    if (snapshot.full && snapshot.baseTick != 0) {
        SetError(error, "全量复制快照 baseTick 必须为 0");
        return {};
    }
    if (snapshot.full && !snapshot.removals.empty()) {
        SetError(error, "全量复制快照不能携带移除列表");
        return {};
    }
    if (!snapshot.full &&
        (snapshot.baseTick == 0 || snapshot.baseTick >= snapshot.serverTick)) {
        SetError(error, "增量复制快照 baseTick 非法");
        return {};
    }
    if (snapshot.upserts.size() > kMaxReplicationEntities ||
        snapshot.removals.size() > kMaxReplicationEntities) {
        SetError(error, "复制实体数量超过上限");
        return {};
    }

    std::size_t total = kReplicationHeaderSize;
    total += snapshot.upserts.size() * kReplicationEntityHeaderSize;
    total += snapshot.removals.size() * sizeof(std::uint64_t);
    if (total > kMaxReplicationPacketBytes) {
        SetError(error, "复制快照超过单包字节上限");
        return {};
    }

    std::unordered_set<std::uint64_t> upsertIds;
    upsertIds.reserve(snapshot.upserts.size());
    std::unordered_set<std::uint64_t> removalIds;
    removalIds.reserve(snapshot.removals.size());

    for (const ReplicatedEntity& entity : snapshot.upserts) {
        if (entity.stableId == 0) {
            SetError(error, "复制实体 StableId 不能为 0");
            return {};
        }
        if (!upsertIds.insert(entity.stableId).second) {
            SetError(error, "复制快照包含重复 StableId");
            return {};
        }
        if (entity.archetype == 0) {
            SetError(error, "复制实体 archetype 不能为 0");
            return {};
        }
        if (entity.components.size() > kMaxReplicationComponents) {
            SetError(error, "复制实体组件数量超过上限");
            return {};
        }
        std::unordered_set<std::uint32_t> componentTypes;
        componentTypes.reserve(entity.components.size());
        for (const ReplicatedComponent& component : entity.components) {
            if (!componentTypes.insert(component.type).second) {
                SetError(error, "复制实体包含重复组件类型");
                return {};
            }
            if (!ValidComponentPayload(component, registry, error)) {
                return {};
            }
            if (!AddSize(&total, kReplicationComponentHeaderSize + component.data.size(),
                         error)) {
                return {};
            }
        }
    }
    for (const std::uint64_t stableId : snapshot.removals) {
        if (stableId == 0) {
            SetError(error, "复制移除 StableId 不能为 0");
            return {};
        }
        if (!removalIds.insert(stableId).second) {
            SetError(error, "复制快照包含重复移除 StableId");
            return {};
        }
        if (upsertIds.find(stableId) != upsertIds.end()) {
            SetError(error, "复制快照同时更新和移除了同一实体");
            return {};
        }
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(total);
    PutU32(bytes, kReplicationMagic);
    PutU16(bytes, kReplicationVersion);
    bytes.push_back(snapshot.full ? 1u : 0u);
    bytes.push_back(0);
    PutU32(bytes, snapshot.sequence);
    PutU64(bytes, snapshot.serverTick);
    PutU64(bytes, snapshot.baseTick);
    PutU16(bytes, static_cast<std::uint16_t>(snapshot.upserts.size()));
    PutU16(bytes, static_cast<std::uint16_t>(snapshot.removals.size()));

    for (const ReplicatedEntity& entity : snapshot.upserts) {
        PutU64(bytes, entity.stableId);
        PutU32(bytes, entity.archetype);
        PutU64(bytes, entity.ownerStableId);
        PutU16(bytes, static_cast<std::uint16_t>(entity.components.size()));
        for (const ReplicatedComponent& component : entity.components) {
            PutU32(bytes, component.type);
            PutU16(bytes, component.version);
            PutU16(bytes, static_cast<std::uint16_t>(component.data.size()));
            bytes.insert(bytes.end(), component.data.begin(), component.data.end());
        }
    }
    for (const std::uint64_t stableId : snapshot.removals) {
        PutU64(bytes, stableId);
    }
    return bytes;
}

bool DecodeReplicationSnapshot(std::span<const std::uint8_t> bytes,
                               const ReplicationRegistry& registry,
                               ReplicationSnapshot* snapshot, std::string* error) {
    if (snapshot == nullptr) {
        SetError(error, "输出 ReplicationSnapshot 为空");
        return false;
    }
    if (bytes.size() < kReplicationHeaderSize) {
        SetError(error, "复制快照头截断");
        return false;
    }
    if (bytes.size() > kMaxReplicationPacketBytes) {
        SetError(error, "复制快照超过单包字节上限");
        return false;
    }

    Reader reader(bytes);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint8_t flags = 0;
    std::uint8_t reserved = 0;
    std::uint32_t sequence = 0;
    std::uint64_t serverTick = 0;
    std::uint64_t baseTick = 0;
    std::uint16_t entityCount = 0;
    std::uint16_t removalCount = 0;
    if (!reader.U32(&magic) || !reader.U16(&version) || !reader.U8(&flags) ||
        !reader.U8(&reserved) || !reader.U32(&sequence) ||
        !reader.U64(&serverTick) || !reader.U64(&baseTick) ||
        !reader.U16(&entityCount) || !reader.U16(&removalCount)) {
        SetError(error, "复制快照头截断");
        return false;
    }
    if (magic != kReplicationMagic) {
        SetError(error, "复制快照 magic 不匹配");
        return false;
    }
    if (version != kReplicationVersion) {
        SetError(error, "复制协议版本不兼容");
        return false;
    }
    if ((flags & ~1u) != 0 || reserved != 0) {
        SetError(error, "复制快照保留字段非 0");
        return false;
    }
    const bool full = (flags & 1u) != 0;
    if (serverTick == 0) {
        SetError(error, "复制快照 serverTick 不能为 0");
        return false;
    }
    if (full && baseTick != 0) {
        SetError(error, "全量复制快照 baseTick 必须为 0");
        return false;
    }
    if (!full && (baseTick == 0 || baseTick >= serverTick)) {
        SetError(error, "增量复制快照 baseTick 非法");
        return false;
    }
    if (full && removalCount != 0) {
        SetError(error, "全量复制快照不能携带移除列表");
        return false;
    }
    if (entityCount > kMaxReplicationEntities || removalCount > kMaxReplicationEntities) {
        SetError(error, "复制实体数量超过上限");
        return false;
    }
    if (entityCount > reader.Remaining() / kReplicationEntityHeaderSize ||
        removalCount > reader.Remaining() / sizeof(std::uint64_t)) {
        SetError(error, "复制实体数量与剩余字节不符");
        return false;
    }

    ReplicationSnapshot decoded;
    decoded.sequence = sequence;
    decoded.serverTick = serverTick;
    decoded.baseTick = baseTick;
    decoded.full = full;
    decoded.upserts.reserve(entityCount);
    decoded.removals.reserve(removalCount);

    std::unordered_set<std::uint64_t> upsertIds;
    upsertIds.reserve(entityCount);
    for (std::uint16_t i = 0; i < entityCount; ++i) {
        ReplicatedEntity entity;
        std::uint16_t componentCount = 0;
        if (!reader.U64(&entity.stableId) || !reader.U32(&entity.archetype) ||
            !reader.U64(&entity.ownerStableId) || !reader.U16(&componentCount)) {
            SetError(error, "复制实体头截断");
            return false;
        }
        if (entity.stableId == 0) {
            SetError(error, "复制实体 StableId 为 0");
            return false;
        }
        if (!upsertIds.insert(entity.stableId).second) {
            SetError(error, "复制快照包含重复 StableId");
            return false;
        }
        if (entity.archetype == 0) {
            SetError(error, "复制实体 archetype 为 0");
            return false;
        }
        if (componentCount > kMaxReplicationComponents) {
            SetError(error, "复制实体组件数量超过上限");
            return false;
        }
        if (componentCount > reader.Remaining() / kReplicationComponentHeaderSize) {
            SetError(error, "复制实体组件数量与剩余字节不符");
            return false;
        }
        entity.components.reserve(componentCount);
        std::unordered_set<std::uint32_t> componentTypes;
        componentTypes.reserve(componentCount);
        for (std::uint16_t j = 0; j < componentCount; ++j) {
            ReplicatedComponent component;
            std::uint16_t payloadSize = 0;
            if (!reader.U32(&component.type) || !reader.U16(&component.version) ||
                !reader.U16(&payloadSize)) {
                SetError(error, "复制组件头截断");
                return false;
            }
            if (!componentTypes.insert(component.type).second) {
                SetError(error, "复制实体包含重复组件类型");
                return false;
            }
            if (!ValidComponentPayload(component, registry, error)) {
                return false;
            }
            const ReplicationComponentSpec* spec = registry.Find(component.type);
            if (spec != nullptr && payloadSize > spec->maxBytes) {
                SetError(error, "复制组件负载超限：" + spec->name);
                return false;
            }
            if (!reader.Bytes(&component.data, payloadSize)) {
                SetError(error, "复制组件负载截断");
                return false;
            }
            entity.components.push_back(std::move(component));
        }
        decoded.upserts.push_back(std::move(entity));
    }

    std::unordered_set<std::uint64_t> removalIds;
    removalIds.reserve(removalCount);
    for (std::uint16_t i = 0; i < removalCount; ++i) {
        std::uint64_t stableId = 0;
        if (!reader.U64(&stableId)) {
            SetError(error, "复制移除列表截断");
            return false;
        }
        if (stableId == 0) {
            SetError(error, "复制移除 StableId 为 0");
            return false;
        }
        if (!removalIds.insert(stableId).second) {
            SetError(error, "复制快照包含重复移除 StableId");
            return false;
        }
        if (upsertIds.find(stableId) != upsertIds.end()) {
            SetError(error, "复制快照同时更新和移除了同一实体");
            return false;
        }
        decoded.removals.push_back(stableId);
    }
    if (!reader.AtEnd()) {
        SetError(error, "复制快照存在尾部多余字节");
        return false;
    }
    *snapshot = std::move(decoded);
    return true;
}

}  // namespace zl::net
