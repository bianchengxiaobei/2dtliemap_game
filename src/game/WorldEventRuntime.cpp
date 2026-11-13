#include "game/WorldEventRuntime.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace zl::game {
namespace {

constexpr std::uint64_t kWorldEventStableIdBase = 0x4D33000000000000ull;

std::uint64_t HashText(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char value : text) {
        hash ^= static_cast<std::uint64_t>(value);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::uint64_t Mix(std::uint64_t value) {
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31;
    return value;
}

bool SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
    return false;
}

}  // namespace

std::uint64_t WorldEventStableId(std::string_view eventId, int index) {
    const int safeIndex = std::max(1, index);
    const std::uint64_t hash =
        Mix(HashText(eventId) ^
            (static_cast<std::uint64_t>(safeIndex) * 0x9E3779B97F4A7C15ull));
    return kWorldEventStableIdBase | (hash & 0x0000FFFFFFFFFFFFull);
}

bool WorldEventCanChoose(const WorldEventDef& def,
                         const WorldEventInstance& event, float playerX,
                         float playerY) {
    if (event.status != WorldEventStatus::Active ||
        def.choices.empty()) {
        return false;
    }
    const bool choiceStage =
        !def.stages.empty() &&
        event.stageIndex == static_cast<int>(def.stages.size()) - 1 &&
        event.stageKind == WorldEventStageKind::Choice;
    if (!event.stagesComplete && !choiceStage) {
        return false;
    }
    if (!std::isfinite(playerX) || !std::isfinite(playerY)) {
        return false;
    }
    const float radius = event.stagesComplete
                             ? kWorldEventChoiceRadius
                             : std::max(kWorldEventChoiceRadius,
                                        event.stageRadius);
    const float dx = playerX - event.x;
    const float dy = playerY - event.y;
    return dx * dx + dy * dy <= radius * radius;
}

WorldEventSpawnPlan BuildWorldEventSpawnPlan(
    const WorldEventDef& def, const WorldEventInstance& event,
    std::size_t maxSpawns) {
    WorldEventSpawnPlan plan;
    const std::size_t budget = std::min(maxSpawns, kMaxWorldEventSpawns);
    if (event.id.empty() || budget == 0) {
        return plan;
    }
    plan.requests.reserve(std::min<std::size_t>(budget, 12));
    const auto add = [&](WorldEventSpawnKind kind, std::string target) {
        if (plan.requests.size() >= budget) {
            return;
        }
        const int index = static_cast<int>(plan.requests.size()) + 1;
        const float angle =
            static_cast<float>(index) * 2.39996323f + 0.7f;
        const float radius = 1.5f + static_cast<float>(index % 3);
        WorldEventSpawnRequest request;
        request.eventId = event.id;
        request.kind = kind;
        request.target = std::move(target);
        request.x = event.x + std::cos(angle) * radius;
        request.y = event.y + std::sin(angle) * radius;
        request.stableId = WorldEventStableId(event.id, index);
        request.index = index;
        plan.requests.push_back(std::move(request));
    };

    for (const WorldEventStageDef& stage : def.stages) {
        if (plan.requests.size() >= budget) {
            break;
        }
        if (stage.kind == WorldEventStageKind::Kill ||
            stage.kind == WorldEventStageKind::Defend) {
            const int count = std::min(stage.count,
                                       static_cast<int>(budget -
                                                        plan.requests.size()));
            for (int i = 0; i < count; ++i) {
                add(WorldEventSpawnKind::Zombie, stage.target);
            }
        } else if (stage.kind == WorldEventStageKind::Loot) {
            add(WorldEventSpawnKind::Container,
                stage.target.empty() ? "event_supply_crate" : stage.target);
        }
    }

    plan.content = !plan.requests.empty();
    return plan;
}

bool EnsureWorldEventContent(const WorldEventSpawnPlan& plan,
                             const WorldEventSpawnCallbacks& callbacks,
                             WorldEventSpawnResult* out) {
    WorldEventSpawnResult result;
    result.content = plan.content;
    result.total = static_cast<int>(plan.requests.size());
    for (const WorldEventSpawnRequest& request : plan.requests) {
        if (!callbacks.spawn || !callbacks.spawn(request)) {
            result.ok = false;
            continue;
        }
        ++result.ready;
    }
    if (out != nullptr) {
        *out = result;
    }
    return result.ok;
}

bool BuildWorldEventOutcome(const WorldEventDef& def,
                            const WorldEventInstance& event,
                            WorldEventOutcome* out, std::string* error) {
    if (out == nullptr) {
        return SetError(error, "world event outcome is null");
    }
    if (event.id.empty() || event.defId != def.id) {
        return SetError(error, "world event outcome identity is invalid");
    }
    WorldEventOutcome result;
    result.eventId = event.id;
    result.defId = def.id;
    result.regionId = event.regionId;
    result.day = event.day;
    const bool combat = std::any_of(
        def.stages.begin(), def.stages.end(), [](const WorldEventStageDef& stage) {
            return stage.kind == WorldEventStageKind::Kill ||
                   stage.kind == WorldEventStageKind::Defend;
        });
    result.skill = combat ? "melee" : "survival";
    if (event.status == WorldEventStatus::Resolved) {
        const auto choice = std::find_if(
            def.choices.begin(), def.choices.end(),
            [&event](const WorldEventChoiceDef& value) {
                return value.id == event.choiceId;
            });
        if (choice == def.choices.end()) {
            return SetError(error, "world event resolved choice is invalid");
        }
        result.kind = WorldEventOutcomeKind::Resolved;
        result.choiceId = choice->id;
        result.reputation = choice->reputation;
        result.xp = choice->rewardXp;
        result.item = choice->rewardItem;
        result.itemCount = choice->rewardItem.empty()
                               ? 0
                               : std::max(1, choice->rewardItemCount);
        result.pressure = choice->pressure;
    } else if (event.status == WorldEventStatus::Failed ||
               event.status == WorldEventStatus::Expired) {
        result.kind = event.status == WorldEventStatus::Failed
                          ? WorldEventOutcomeKind::Failed
                          : WorldEventOutcomeKind::Expired;
        result.reputation = def.failureReputation;
        result.pressure = def.failurePressure;
    } else {
        return SetError(error, "world event is not terminal");
    }
    if (error != nullptr) {
        error->clear();
    }
    *out = std::move(result);
    return true;
}

std::string WorldEventOutcomeKey(std::string_view eventId,
                                 std::uint64_t playerId) {
    std::string key = "worldevent:";
    key += eventId;
    key += ':';
    key += std::to_string(playerId);
    return key;
}

bool WorldEventOutcomeLedger::Claim(std::string_view eventId,
                                    std::uint64_t playerId) {
    if (eventId.empty() || playerId == 0) {
        return false;
    }
    std::string key = WorldEventOutcomeKey(eventId, playerId);
    if (key.size() > 192) {
        return false;
    }
    if (!m_keys.insert(key).second) {
        return false;
    }
    m_order.push_back(key);
    while (m_order.size() > kMaxWorldEventOutcomeKeys) {
        m_keys.erase(m_order.front());
        m_order.erase(m_order.begin());
    }
    return true;
}

bool WorldEventOutcomeLedger::Contains(std::string_view eventId,
                                       std::uint64_t playerId) const {
    if (eventId.empty() || playerId == 0) {
        return false;
    }
    const std::string key = WorldEventOutcomeKey(eventId, playerId);
    return m_keys.find(key) != m_keys.end();
}

bool WorldEventOutcomeLedger::Restore(
    const std::vector<std::string>& keys) {
    if (keys.size() > kMaxWorldEventOutcomeKeys) {
        return false;
    }
    std::unordered_set<std::string> restored;
    std::vector<std::string> order;
    restored.reserve(keys.size());
    order.reserve(keys.size());
    for (const std::string& key : keys) {
        if (key.size() < 24 || key.size() > 192 ||
            key.rfind("worldevent:", 0) != 0) {
            return false;
        }
        const std::size_t separator = key.find(':', 11);
        if (separator == std::string::npos || separator + 1 >= key.size()) {
            return false;
        }
        for (std::size_t i = separator + 1; i < key.size(); ++i) {
            if (key[i] < '0' || key[i] > '9') {
                return false;
            }
        }
        if (!restored.insert(key).second) {
            return false;
        }
        order.push_back(key);
    }
    m_keys = std::move(restored);
    m_order = std::move(order);
    return true;
}

void WorldEventOutcomeLedger::Reset() {
    m_keys.clear();
    m_order.clear();
}

}  // namespace zl::game
