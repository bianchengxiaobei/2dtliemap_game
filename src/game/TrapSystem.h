#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "game/Traps.h"

namespace zl::ecs {
class EcsWorld;
class SpatialIndex;
}  // namespace zl::ecs

namespace zl::game {

enum class TrapTargetKind : std::uint8_t {
    None = 0,
    Zombie = 1,
    Animal = 2,
};

struct TrapTargetCandidate {
    ecs::Entity entity = ecs::EcsWorld::kNull;
    float x = 0.0f;
    float y = 0.0f;
    int z = 0;
    TrapTargetKind kind = TrapTargetKind::None;
};

// M34-F4-T3：已布防陷阱的局部扫描中心。半径用于把查询限制在附近 chunk，
// 避免每 tick 扫描全世界所有 Health/Position 实体。
struct TrapTargetScanCenter {
    float x = 0.0f;
    float y = 0.0f;
    int z = 0;
    float radius = 0.0f;
};

struct TrapTargetScanStats {
    std::size_t centers = 0;
    std::size_t visited = 0;
    std::size_t candidates = 0;
    bool limited = false;
};

inline constexpr std::size_t kMaxTrapScanCenters = 64;
inline constexpr std::size_t kMaxTrapScanVisited = 2048;
inline constexpr std::size_t kMaxTrapTargetCandidates = 256;

struct TrapFireCell {
    int cellX = 0;
    int cellY = 0;
};

const char* TrapTargetKindName(TrapTargetKind kind);
bool TrapCanActivate(const ecs::PlacedTrap& trap, const TrapDef& def);
bool TrapTargetAllowed(const TrapDef& def, TrapTargetKind kind);
TrapTargetKind ClassifyTrapTarget(const entt::registry& registry,
                                  ecs::Entity entity);
std::vector<TrapTargetCandidate> CollectTrapTargets(
    const ecs::EcsWorld& world);
// 只遍历 centers 附近 chunk；去重实体，并受 visited/candidate 上限保护。
// centers 超过 kMaxTrapScanCenters 时安全跳过尾部（stats->limited=true）。
std::vector<TrapTargetCandidate> CollectTrapTargetsAround(
    const ecs::EcsWorld& world, const ecs::SpatialIndex& spatial,
    const std::vector<TrapTargetScanCenter>& centers,
    TrapTargetScanStats* stats = nullptr);
ecs::Entity FindTrapTarget(const std::vector<TrapTargetCandidate>& candidates,
                           const ecs::Position& trapPosition,
                           const TrapDef& def,
                           TrapTargetKind* outKind = nullptr);
bool AdvancePlacedTrapCooldown(ecs::PlacedTrap& trap, float seconds);
bool ConsumePlacedTrapUse(ecs::PlacedTrap& trap, const TrapDef& def);
float TrapFireDurationSeconds(float minutes,
                              float gameMinutesPerRealSecond);
std::vector<TrapFireCell> TrapFireAreaCells(float x, float y, float radius);

}  // namespace zl::game
