#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zl::net {

// M13-T4：实体复制协议 v1（服务器 → 客户端）。
//
// 这一层只认识稳定 ID、archetype、owner 与不透明组件字节，不依赖 world/ecs；
// 具体组件（Position/Health/SpriteRef...）由 game/EntityReplication 注册并编解码。
// KCP 只提供可靠有序字节流，本协议负责快照语义：
//   Full  = 客户端清空旧副本后应用；baseTick 必须为 0
//   Delta = 只包含相对 baseTick 的变化；baseTick 必须等于客户端已应用 tick
// 组件按 type/version 注册，未知或不兼容组件严格拒绝，避免静默错位。
inline constexpr std::uint32_t kReplicationMagic = 0x31524C5Au;  // "ZLR1"
inline constexpr std::uint16_t kReplicationVersion = 1;
inline constexpr std::size_t kReplicationHeaderSize = 32;
inline constexpr std::size_t kReplicationEntityHeaderSize = 22;
inline constexpr std::size_t kReplicationComponentHeaderSize = 8;
inline constexpr std::size_t kMaxReplicationPacketBytes = 60 * 1024;
inline constexpr std::size_t kMaxReplicationEntities = 4096;
inline constexpr std::size_t kMaxReplicationComponents = 32;
inline constexpr std::uint32_t kMaxReplicationComponentBytes = 1024;
inline constexpr std::uint32_t kMaxReplicationComponentNameBytes = 64;

constexpr std::uint32_t MakeReplicationComponentId(char a, char b, char c, char d) noexcept {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(a)) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 8U) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 16U) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d)) << 24U);
}

struct ReplicationComponentSpec {
    std::uint32_t type = 0;
    std::string name;
    std::uint16_t version = 1;
    std::uint32_t maxBytes = 0;
};

class ReplicationRegistry {
public:
    bool Register(ReplicationComponentSpec spec, std::string* error = nullptr);
    bool Register(std::uint32_t type, std::string name, std::uint32_t maxBytes,
                  std::uint16_t version = 1, std::string* error = nullptr);

    const ReplicationComponentSpec* Find(std::uint32_t type) const;
    const std::vector<ReplicationComponentSpec>& All() const { return m_specs; }
    std::size_t Size() const { return m_specs.size(); }

private:
    std::vector<ReplicationComponentSpec> m_specs;
    std::unordered_map<std::uint32_t, std::size_t> m_byType;
};

struct ReplicatedComponent {
    std::uint32_t type = 0;
    std::uint16_t version = 1;
    std::vector<std::uint8_t> data;
};

struct ReplicatedEntity {
    std::uint64_t stableId = 0;
    std::uint32_t archetype = 0;
    std::uint64_t ownerStableId = 0;
    std::vector<ReplicatedComponent> components;

    const ReplicatedComponent* Find(std::uint32_t type) const;
};

struct ReplicationSnapshot {
    std::uint32_t sequence = 0;
    std::uint64_t serverTick = 0;
    std::uint64_t baseTick = 0;
    bool full = true;
    std::vector<ReplicatedEntity> upserts;
    std::vector<std::uint64_t> removals;

    bool HasChanges() const { return !upserts.empty() || !removals.empty(); }
};

std::vector<std::uint8_t> EncodeReplicationSnapshot(
    const ReplicationSnapshot& snapshot, const ReplicationRegistry& registry,
    std::string* error = nullptr);
bool DecodeReplicationSnapshot(std::span<const std::uint8_t> bytes,
                               const ReplicationRegistry& registry,
                               ReplicationSnapshot* snapshot,
                               std::string* error = nullptr);

}  // namespace zl::net
