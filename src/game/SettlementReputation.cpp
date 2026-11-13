#include "game/SettlementReputation.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace zl::game {
namespace {

std::string TradeKeyPrefix(std::string_view settlementId, int day) {
    std::string prefix = "trade:";
    prefix += settlementId;
    prefix += ':';
    prefix += std::to_string(day);
    prefix += ':';
    return prefix;
}

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() &&
           value.substr(0, prefix.size()) == prefix;
}

SettlementReputationEntry* FindEntry(SettlementReputation* reputation,
                                     std::string_view settlementId) {
    for (SettlementReputationEntry& entry : reputation->settlements) {
        if (entry.settlementId == settlementId) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace

const char* SettlementReputationEventName(SettlementReputationEvent event) {
    switch (event) {
        case SettlementReputationEvent::MissionComplete:
            return "missionComplete";
        case SettlementReputationEvent::MissionAbandon:
            return "missionAbandon";
        case SettlementReputationEvent::MissionTimeout:
            return "missionTimeout";
        case SettlementReputationEvent::Trade:
            return "trade";
        case SettlementReputationEvent::DefenseComplete:
            return "defenseComplete";
        case SettlementReputationEvent::EventComplete:
            return "eventComplete";
        case SettlementReputationEvent::AttackNpc:
            return "attackNpc";
        case SettlementReputationEvent::Theft:
            return "theft";
    }
    return "unknown";
}

int SettlementReputationDelta(const SettlementReputationConfig& config,
                              SettlementReputationEvent event) {
    switch (event) {
        case SettlementReputationEvent::MissionComplete:
        case SettlementReputationEvent::EventComplete:
            return config.sources.missionComplete;
        case SettlementReputationEvent::MissionAbandon:
        case SettlementReputationEvent::MissionTimeout:
            return config.sources.missionAbandon;
        case SettlementReputationEvent::Trade:
            return config.sources.trade;
        case SettlementReputationEvent::DefenseComplete:
            return config.sources.defenseComplete;
        case SettlementReputationEvent::AttackNpc:
            return config.sources.attackNpc;
        case SettlementReputationEvent::Theft:
            return config.sources.theft;
    }
    return 0;
}

bool IsValidSettlementReputationId(std::string_view id) {
    if (id.empty() || id.size() > kMaxSettlementReputationIdBytes) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
               c == '.';
    });
}

int SettlementReputationScore(const SettlementReputation& reputation,
                              std::string_view settlementId,
                              int fallbackScore) {
    for (const SettlementReputationEntry& entry : reputation.settlements) {
        if (entry.settlementId == settlementId) {
            return std::clamp(entry.score, kSettlementReputationMinScore,
                              kSettlementReputationMaxScore);
        }
    }
    return std::clamp(fallbackScore, kSettlementReputationMinScore,
                      kSettlementReputationMaxScore);
}

int SettlementReputationDailyTradeGain(const SettlementReputation& reputation,
                                       std::string_view settlementId, int day) {
    const std::string prefix = TradeKeyPrefix(settlementId, day);
    int count = 0;
    for (const std::string& key : reputation.appliedKeys) {
        if (StartsWith(key, prefix)) {
            ++count;
        }
    }
    return count;
}

SettlementReputationChange ApplySettlementReputation(
    SettlementReputation* reputation, const SettlementReputationConfig& config,
    std::string_view settlementId, SettlementReputationEvent event,
    std::string_view idempotencyKey, int fallbackScore, int day) {
    SettlementReputationChange change;
    change.previous = std::clamp(fallbackScore, kSettlementReputationMinScore,
                                 kSettlementReputationMaxScore);
    change.current = change.previous;
    if (reputation == nullptr || !config.enabled ||
        !IsValidSettlementReputationId(settlementId)) {
        return change;
    }
    if (SettlementReputationEntry* entry = FindEntry(reputation, settlementId);
        entry != nullptr) {
        change.previous = std::clamp(entry->score, kSettlementReputationMinScore,
                                     kSettlementReputationMaxScore);
        change.current = change.previous;
    }

    std::string key;
    if (event == SettlementReputationEvent::Trade) {
        const int limit = std::clamp(config.dailyTradeGainLimit, 0, 100);
        const int gained =
            SettlementReputationDailyTradeGain(*reputation, settlementId, day);
        if (limit <= 0 || gained >= limit) {
            change.limited = true;
            return change;
        }
        key = TradeKeyPrefix(settlementId, day) + std::to_string(gained);
    } else {
        key.assign(idempotencyKey);
        if (key.empty() || key.size() > kMaxSettlementReputationKeyBytes) {
            return change;
        }
        if (std::find(reputation->appliedKeys.begin(),
                      reputation->appliedKeys.end(),
                      key) != reputation->appliedKeys.end()) {
            return change;
        }
    }

    const int delta = SettlementReputationDelta(config, event);
    change.current =
        std::clamp(change.previous + delta, kSettlementReputationMinScore,
                   kSettlementReputationMaxScore);
    change.delta = change.current - change.previous;
    change.applied = true;

    if (SettlementReputationEntry* entry = FindEntry(reputation, settlementId);
        entry != nullptr) {
        entry->score = change.current;
    } else if (reputation->settlements.size() <
               kMaxSettlementReputationEntries) {
        reputation->settlements.push_back(
            SettlementReputationEntry{std::string(settlementId),
                                      change.current});
        std::sort(reputation->settlements.begin(),
                  reputation->settlements.end(),
                  [](const SettlementReputationEntry& left,
                     const SettlementReputationEntry& right) {
                      return left.settlementId < right.settlementId;
                  });
    }

    reputation->appliedKeys.push_back(std::move(key));
    if (reputation->appliedKeys.size() > kMaxSettlementReputationKeys) {
        reputation->appliedKeys.erase(reputation->appliedKeys.begin());
    }
    return change;
}

const SettlementReputation* FindSettlementReputation(
    const ecs::EcsWorld& world, ecs::Entity player) {
    if (player == entt::null) {
        return nullptr;
    }
    return world.Registry().try_get<SettlementReputation>(player);
}

int SettlementReputationScoreFor(const ecs::EcsWorld& world,
                                 ecs::Entity player,
                                 std::string_view settlementId,
                                 int fallbackScore) {
    const SettlementReputation* reputation =
        FindSettlementReputation(world, player);
    return reputation != nullptr
               ? SettlementReputationScore(*reputation, settlementId,
                                           fallbackScore)
               : std::clamp(fallbackScore, kSettlementReputationMinScore,
                            kSettlementReputationMaxScore);
}

SettlementReputationChange AwardSettlementReputation(
    ecs::EcsWorld& world, ecs::Entity player,
    const SettlementReputationConfig& config, std::string_view settlementId,
    SettlementReputationEvent event, std::string_view idempotencyKey,
    int fallbackScore, int day) {
    if (player == entt::null || !world.Alive(player) || !config.enabled ||
        !IsValidSettlementReputationId(settlementId)) {
        return {};
    }
    auto* reputation = world.Registry().try_get<SettlementReputation>(player);
    if (reputation == nullptr) {
        reputation = &world.Registry().emplace<SettlementReputation>(player);
    }
    return ApplySettlementReputation(reputation, config, settlementId, event,
                                     idempotencyKey, fallbackScore, day);
}

SettlementReputationChange ApplySettlementReputationDelta(
    SettlementReputation* reputation, const SettlementReputationConfig& config,
    std::string_view settlementId, int delta,
    std::string_view idempotencyKey, int fallbackScore) {
    SettlementReputationChange change;
    change.previous = std::clamp(fallbackScore, kSettlementReputationMinScore,
                                 kSettlementReputationMaxScore);
    change.current = change.previous;
    if (reputation == nullptr || !config.enabled ||
        !IsValidSettlementReputationId(settlementId) || delta == 0) {
        return change;
    }
    if (SettlementReputationEntry* entry = FindEntry(reputation, settlementId);
        entry != nullptr) {
        change.previous = std::clamp(entry->score, kSettlementReputationMinScore,
                                     kSettlementReputationMaxScore);
        change.current = change.previous;
    }
    if (idempotencyKey.empty() ||
        idempotencyKey.size() > kMaxSettlementReputationKeyBytes) {
        return change;
    }
    if (std::find(reputation->appliedKeys.begin(),
                  reputation->appliedKeys.end(),
                  idempotencyKey) != reputation->appliedKeys.end()) {
        return change;
    }
    change.current =
        std::clamp(change.previous + delta, kSettlementReputationMinScore,
                   kSettlementReputationMaxScore);
    change.delta = change.current - change.previous;
    if (change.delta == 0) {
        return change;
    }
    if (SettlementReputationEntry* entry = FindEntry(reputation, settlementId);
        entry != nullptr) {
        entry->score = change.current;
    } else if (reputation->settlements.size() <
               kMaxSettlementReputationEntries) {
        reputation->settlements.push_back(
            SettlementReputationEntry{std::string(settlementId),
                                      change.current});
        std::sort(reputation->settlements.begin(),
                  reputation->settlements.end(),
                  [](const SettlementReputationEntry& left,
                     const SettlementReputationEntry& right) {
                      return left.settlementId < right.settlementId;
                  });
    } else {
        change.current = change.previous;
        change.delta = 0;
        return change;
    }
    change.applied = true;
    reputation->appliedKeys.push_back(std::string(idempotencyKey));
    if (reputation->appliedKeys.size() > kMaxSettlementReputationKeys) {
        reputation->appliedKeys.erase(reputation->appliedKeys.begin());
    }
    return change;
}

SettlementReputationChange AwardSettlementReputationDelta(
    ecs::EcsWorld& world, ecs::Entity player,
    const SettlementReputationConfig& config, std::string_view settlementId,
    int delta, std::string_view idempotencyKey, int fallbackScore) {
    if (player == entt::null || !world.Alive(player) || !config.enabled ||
        !IsValidSettlementReputationId(settlementId)) {
        return {};
    }
    auto* reputation = world.Registry().try_get<SettlementReputation>(player);
    if (reputation == nullptr) {
        reputation = &world.Registry().emplace<SettlementReputation>(player);
    }
    return ApplySettlementReputationDelta(reputation, config, settlementId,
                                          delta, idempotencyKey,
                                          fallbackScore);
}

}  // namespace zl::game
