#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "game/WorldEvent.h"
#include "game/WorldEventDirector.h"

namespace zl::game {

inline constexpr std::size_t kMaxWorldEventSpawns = 32;
inline constexpr std::size_t kMaxWorldEventOutcomeKeys = 512;
inline constexpr float kWorldEventChoiceRadius = 6.0f;

enum class WorldEventSpawnKind : std::uint8_t {
    Zombie = 0,
    Container = 1,
};

struct WorldEventSpawnRequest {
    std::string eventId;
    WorldEventSpawnKind kind = WorldEventSpawnKind::Zombie;
    std::string target;
    float x = 0.0f;
    float y = 0.0f;
    std::uint64_t stableId = 0;
    int index = 0;
};

struct WorldEventSpawnPlan {
    bool content = false;
    std::vector<WorldEventSpawnRequest> requests;
};

struct WorldEventSpawnCallbacks {
    std::function<bool(const WorldEventSpawnRequest&)> spawn;
};

struct WorldEventSpawnResult {
    bool content = false;
    bool ok = true;
    int total = 0;
    int ready = 0;
};

std::uint64_t WorldEventStableId(std::string_view eventId, int index);
bool WorldEventCanChoose(const WorldEventDef& def,
                         const WorldEventInstance& event, float playerX,
                         float playerY);
WorldEventSpawnPlan BuildWorldEventSpawnPlan(
    const WorldEventDef& def, const WorldEventInstance& event,
    std::size_t maxSpawns = kMaxWorldEventSpawns);
bool EnsureWorldEventContent(const WorldEventSpawnPlan& plan,
                             const WorldEventSpawnCallbacks& callbacks,
                             WorldEventSpawnResult* out = nullptr);

enum class WorldEventOutcomeKind : std::uint8_t {
    Resolved = 0,
    Failed = 1,
    Expired = 2,
};

struct WorldEventOutcome {
    WorldEventOutcomeKind kind = WorldEventOutcomeKind::Resolved;
    std::string eventId;
    std::string defId;
    std::string regionId;
    std::string choiceId;
    int reputation = 0;
    float xp = 0.0f;
    std::string skill = "survival";
    std::string item;
    int itemCount = 0;
    int pressure = 0;
    int day = 1;
};

bool BuildWorldEventOutcome(const WorldEventDef& def,
                            const WorldEventInstance& event,
                            WorldEventOutcome* out,
                            std::string* error = nullptr);
std::string WorldEventOutcomeKey(std::string_view eventId,
                                 std::uint64_t playerId);

class WorldEventOutcomeLedger {
public:
    bool Claim(std::string_view eventId, std::uint64_t playerId);
    bool Contains(std::string_view eventId, std::uint64_t playerId) const;
    bool Restore(const std::vector<std::string>& keys);
    const std::vector<std::string>& Keys() const { return m_order; }
    void Reset();
    std::size_t Size() const { return m_order.size(); }

private:
    std::unordered_set<std::string> m_keys;
    std::vector<std::string> m_order;
};

}  // namespace zl::game
