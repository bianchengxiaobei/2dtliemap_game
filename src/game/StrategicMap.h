#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "game/StrategicState.h"

namespace zl::net {
struct StrategicSnapshot;
}

namespace zl::game {

inline constexpr std::size_t kMaxStrategicMapEvents = 8;

struct StrategicMapWarning {
    bool valid = false;
    std::int64_t warningMinute = 0;
    std::int64_t attackMinute = 0;
    std::int64_t reportedAttackMinute = 0;
    int warningMinutes = 0;
    int remainingMinutes = 0;
    int intelLevel = 0;
    int enemyStrength = 0;
    int reportedEnemyStrength = 0;
    int defenseScore = 0;
};

struct StrategicMapRegion {
    std::string id;
    StrategicOwner owner = StrategicOwner::Neutral;
    ContestedFaction contestedFaction = ContestedFaction::Infected;
    StrategicSiegePhase siegePhase = StrategicSiegePhase::None;
    float pressure = 0.0f;
    float defense = 0.0f;
    float supply = 0.0f;
    float morale = 0.0f;
    int population = 0;
    int garrison = 0;
    int fortification = 0;
    int intelLevel = 0;
    int strategicValue = 1;
    bool coreSafe = false;
    bool remote = false;
    int facilityDamage = 0;
    int refugees = 0;
    int infectionCores = 0;
    StrategicRecoveryStage recoveryStage = StrategicRecoveryStage::None;
    std::int64_t recoveryReadyMinute = -1;
    StrategicMapWarning warning;
};

struct StrategicMapEvent {
    std::string id;
    std::string regionId;
    StrategicEventKind kind = StrategicEventKind::Owner;
    std::int64_t gameMinute = 0;
    StrategicOwner ownerAfter = StrategicOwner::Neutral;
    StrategicResources delta;
};

struct StrategicMapState {
    bool enabled = false;
    std::int64_t gameMinute = 0;
    int tickMinutes = 30;
    int warningHours = 48;
    int remoteWarningHours = 84;
    int autoResolveGraceMinutes = 2880;
    StrategicResources resources;
    std::vector<StrategicMapRegion> regions;
    std::vector<StrategicMapEvent> events;

    const StrategicMapRegion* FindRegion(std::string_view id) const;
};

StrategicMapState BuildStrategicMapState(const StrategicConfig& config,
                                         const StrategicState& state,
                                         std::int64_t gameMinute,
                                         std::size_t maxEvents =
                                             kMaxStrategicMapEvents);
// M32-T7：网络客户端只读消费服务器快照；不持有本地 StrategicState。
StrategicMapState BuildStrategicMapStateFromSnapshot(
    const net::StrategicSnapshot& snapshot);

}  // namespace zl::game
