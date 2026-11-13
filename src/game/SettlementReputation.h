#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ecs/EcsWorld.h"
#include "game/Settlement.h"

namespace zl::game {

inline constexpr std::size_t kMaxSettlementReputationEntries = 32;
inline constexpr std::size_t kMaxSettlementReputationKeys = 128;
inline constexpr std::size_t kMaxSettlementReputationIdBytes = 64;
inline constexpr std::size_t kMaxSettlementReputationKeyBytes = 192;

struct SettlementReputationEntry {
    std::string settlementId;
    int score = 0;
};

struct SettlementReputation {
    std::vector<SettlementReputationEntry> settlements;
    std::vector<std::string> appliedKeys;
};

enum class SettlementReputationEvent : std::uint8_t {
    MissionComplete = 0,
    MissionAbandon = 1,
    MissionTimeout = 2,
    Trade = 3,
    DefenseComplete = 4,
    EventComplete = 5,
    AttackNpc = 6,
    Theft = 7,
};

const char* SettlementReputationEventName(SettlementReputationEvent event);
int SettlementReputationDelta(const SettlementReputationConfig& config,
                              SettlementReputationEvent event);
bool IsValidSettlementReputationId(std::string_view id);
int SettlementReputationScore(const SettlementReputation& reputation,
                              std::string_view settlementId,
                              int fallbackScore = 0);
int SettlementReputationDailyTradeGain(const SettlementReputation& reputation,
                                       std::string_view settlementId, int day);

struct SettlementReputationChange {
    bool applied = false;
    bool limited = false;
    int previous = 0;
    int current = 0;
    int delta = 0;
};

SettlementReputationChange ApplySettlementReputation(
    SettlementReputation* reputation, const SettlementReputationConfig& config,
    std::string_view settlementId, SettlementReputationEvent event,
    std::string_view idempotencyKey = {}, int fallbackScore = 0, int day = 0);
SettlementReputationChange ApplySettlementReputationDelta(
    SettlementReputation* reputation, const SettlementReputationConfig& config,
    std::string_view settlementId, int delta,
    std::string_view idempotencyKey = {}, int fallbackScore = 0);

const SettlementReputation* FindSettlementReputation(
    const ecs::EcsWorld& world, ecs::Entity player);
int SettlementReputationScoreFor(const ecs::EcsWorld& world,
                                 ecs::Entity player,
                                 std::string_view settlementId,
                                 int fallbackScore = 0);
SettlementReputationChange AwardSettlementReputation(
    ecs::EcsWorld& world, ecs::Entity player,
    const SettlementReputationConfig& config, std::string_view settlementId,
    SettlementReputationEvent event, std::string_view idempotencyKey = {},
    int fallbackScore = 0, int day = 0);
SettlementReputationChange AwardSettlementReputationDelta(
    ecs::EcsWorld& world, ecs::Entity player,
    const SettlementReputationConfig& config, std::string_view settlementId,
    int delta, std::string_view idempotencyKey = {}, int fallbackScore = 0);

}  // namespace zl::game
