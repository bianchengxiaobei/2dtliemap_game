#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace zl::net {

// M32-T7：服务器权威战略层只读同步与玩家战略请求。
//
// 快照消息独立于 ZLR1 实体复制，旧客户端收到未知 magic 会忽略；
// 不升级 SessionProtocolVersion，避免把“只读 UI”变成强协议门槛。
inline constexpr std::uint32_t kStrategicSnapshotMagic = 0x33534C5Au;  // "ZLS3"
inline constexpr std::uint16_t kStrategicSnapshotVersion = 2;
inline constexpr std::uint32_t kStrategicRequestMagic = 0x34534C5Au;  // "ZLS4"
inline constexpr std::uint16_t kStrategicRequestVersion = 1;
inline constexpr std::uint32_t kStrategicResultMagic = 0x35534C5Au;  // "ZLS5"
inline constexpr std::uint16_t kStrategicResultVersion = 1;

inline constexpr std::size_t kMaxStrategicRegions = 32;
inline constexpr std::size_t kMaxStrategicEvents = 8;
inline constexpr std::size_t kMaxStrategicIdBytes = 96;
inline constexpr std::size_t kMaxStrategicReasonBytes = 160;
inline constexpr std::size_t kMaxStrategicSnapshotBytes = 32 * 1024;
inline constexpr std::size_t kMaxStrategicRequestBytes = 512;
inline constexpr std::size_t kMaxStrategicResultBytes = 512;

enum class StrategicRequestAction : std::uint8_t {
    Reclaim = 1,         // 反攻：玩家必须在目标区域且阶段为 Counterattack
    ClearCore = 2,       // 清理感染核心
    RepairFacility = 3,  // 修复区域设施
    DonateSupply = 4,    // 将全球补给转入目标区域补给（受资源/上限校验）
};

const char* StrategicRequestActionName(StrategicRequestAction action);
bool IsValidStrategicRequestAction(StrategicRequestAction action);

struct StrategicRegionSnapshot {
    std::string id;
    std::uint8_t owner = 0;
    std::uint8_t contestedFaction = 1;
    std::uint8_t siegePhase = 0;
    std::uint8_t recoveryStage = 0;
    bool coreSafe = false;
    bool remote = false;
    float pressure = 0.0f;
    float defense = 0.0f;
    float supply = 0.0f;
    float morale = 0.0f;
    std::int32_t population = 0;
    std::int32_t garrison = 0;
    std::int32_t fortification = 0;
    std::int32_t intelLevel = 0;
    std::int32_t strategicValue = 1;
    std::int32_t facilityDamage = 0;
    std::int32_t refugees = 0;
    std::int32_t infectionCores = 0;
    std::int64_t recoveryReadyMinute = -1;

    bool warningValid = false;
    std::int64_t warningMinute = 0;
    std::int64_t attackMinute = 0;
    std::int64_t reportedAttackMinute = 0;
    std::int32_t warningMinutes = 0;
    std::int32_t remainingMinutes = 0;
    std::int32_t warningIntelLevel = 0;
    std::int32_t enemyStrength = 0;
    std::int32_t reportedEnemyStrength = 0;
    std::int32_t warningDefenseScore = 0;
};

struct StrategicEventSnapshot {
    std::string id;
    std::string regionId;
    std::uint8_t kind = 0;
    std::uint8_t ownerAfter = 0;
    std::int64_t gameMinute = 0;
    std::int32_t people = 0;
    std::int32_t supplies = 0;
    std::int32_t ammo = 0;
    std::int32_t fuel = 0;
};

struct StrategicSnapshot {
    std::uint32_t sequence = 0;
    std::uint64_t serverTick = 0;
    bool enabled = false;
    std::int64_t gameMinute = 0;
    std::int32_t tickMinutes = 30;
    std::int32_t warningHours = 48;
    std::int32_t remoteWarningHours = 84;
    std::int32_t autoResolveGraceMinutes = 2880;
    std::int32_t people = 0;
    std::int32_t supplies = 0;
    std::int32_t ammo = 0;
    std::int32_t fuel = 0;
    std::vector<StrategicRegionSnapshot> regions;
    std::vector<StrategicEventSnapshot> events;
};

struct StrategicRequest {
    std::uint32_t sequence = 0;
    StrategicRequestAction action = StrategicRequestAction::Reclaim;
    std::string regionId;
    std::int32_t amount = 0;
};

struct StrategicRequestResult {
    std::uint32_t sequence = 0;
    StrategicRequestAction action = StrategicRequestAction::Reclaim;
    std::string regionId;
    bool accepted = false;
    std::string reason;
};

std::vector<std::uint8_t> EncodeStrategicSnapshot(
    const StrategicSnapshot& snapshot, std::string* error = nullptr);
bool DecodeStrategicSnapshot(std::span<const std::uint8_t> bytes,
                             StrategicSnapshot* snapshot,
                             std::string* error = nullptr);

std::vector<std::uint8_t> EncodeStrategicRequest(
    const StrategicRequest& request, std::string* error = nullptr);
bool DecodeStrategicRequest(std::span<const std::uint8_t> bytes,
                            StrategicRequest* request,
                            std::string* error = nullptr);

std::vector<std::uint8_t> EncodeStrategicResult(
    const StrategicRequestResult& result, std::string* error = nullptr);
bool DecodeStrategicResult(std::span<const std::uint8_t> bytes,
                           StrategicRequestResult* result,
                           std::string* error = nullptr);

}  // namespace zl::net
