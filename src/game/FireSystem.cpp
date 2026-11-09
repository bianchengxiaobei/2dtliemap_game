#include "game/FireSystem.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/systems/CombatSystem.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/WorldQueries.h"
#include "items/InventoryOps.h"
#include "items/ItemRegistry.h"
#include "world/Chunk.h"

namespace zl::game {
namespace {

float Clamp01(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

float ClampNonNegative(float value) {
    return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
}

void AddFireEntity(ecs::EcsWorld& ecs, int wx, int wy, int z,
                   ecs::FireSource source) {
    const ecs::Entity entity = ecs.Create();
    entt::registry& registry = ecs.Registry();
    registry.emplace<ecs::Position>(
        entity, ecs::Position{static_cast<float>(wx) + 0.5f,
                              static_cast<float>(wy) + 0.5f, z});
    registry.emplace<ecs::FireAnchor>(entity, ecs::FireAnchor{wx, wy, z});
    registry.emplace<ecs::FireSource>(entity, std::move(source));
}

bool SquareLoaded(world::World& world, int wx, int wy) {
    const world::ChunkCoord coord = world::ChunkOf(wx, wy);
    const world::Chunk* chunk =
        FindChunkInWorld(world, coord.cx, coord.cy);
    return chunk != nullptr && chunk->State() == world::ChunkState::Loaded;
}

}  // namespace

FireParams SanitizeFireParams(FireParams params) {
    params.ignitionSeconds =
        std::clamp(std::isfinite(params.ignitionSeconds)
                       ? params.ignitionSeconds
                       : 1.5f,
                   0.05f, 60.0f);
    params.wetnessPenalty =
        std::clamp(std::isfinite(params.wetnessPenalty) ? params.wetnessPenalty
                                                        : 0.9f,
                   0.0f, 4.0f);
    params.minIgnitionChance = Clamp01(params.minIgnitionChance);
    params.smolderSeconds =
        std::clamp(std::isfinite(params.smolderSeconds) ? params.smolderSeconds
                                                        : 20.0f,
                   0.0f, 600.0f);
    params.spreadIntervalSeconds =
        std::clamp(std::isfinite(params.spreadIntervalSeconds)
                       ? params.spreadIntervalSeconds
                       : 6.0f,
                   0.5f, 60.0f);
    params.spreadChanceScale =
        std::clamp(std::isfinite(params.spreadChanceScale)
                       ? params.spreadChanceScale
                       : 0.20f,
                   0.0f, 1.0f);
    params.rainBurnDecayScale =
        std::clamp(std::isfinite(params.rainBurnDecayScale)
                       ? params.rainBurnDecayScale
                       : 1.0f,
                   0.0f, 10.0f);
    params.maxActiveSources = std::clamp(params.maxActiveSources, 1, 4096);
    params.maxSpreadsPerTick = std::clamp(params.maxSpreadsPerTick, 0, 64);
    params.extinguishRange =
        std::clamp(std::isfinite(params.extinguishRange)
                       ? params.extinguishRange
                       : 2.5f,
                   0.5f, 8.0f);
    params.bareHandsChance = Clamp01(params.bareHandsChance);
    params.bareHandsSelfDamage =
        std::clamp(std::isfinite(params.bareHandsSelfDamage)
                       ? params.bareHandsSelfDamage
                       : 2.0f,
                   0.0f, 50.0f);
    params.fireDamagePerSecond =
        std::clamp(std::isfinite(params.fireDamagePerSecond)
                       ? params.fireDamagePerSecond
                       : 6.0f,
                   0.0f, 500.0f);
    params.fireDamageRadius =
        std::clamp(std::isfinite(params.fireDamageRadius)
                       ? params.fireDamageRadius
                       : 1.0f,
                   0.0f, 8.0f);
    params.fireDamageIntervalSeconds =
        std::clamp(std::isfinite(params.fireDamageIntervalSeconds)
                       ? params.fireDamageIntervalSeconds
                       : 0.5f,
                   0.05f, 10.0f);
    params.zombieBurnSeconds =
        std::clamp(std::isfinite(params.zombieBurnSeconds)
                       ? params.zombieBurnSeconds
                       : 8.0f,
                   0.1f, 120.0f);
    params.zombieBurnDamagePerSecond =
        std::clamp(std::isfinite(params.zombieBurnDamagePerSecond)
                       ? params.zombieBurnDamagePerSecond
                       : 3.0f,
                   0.0f, 200.0f);
    params.zombieBurnDamageIntervalSeconds =
        std::clamp(std::isfinite(params.zombieBurnDamageIntervalSeconds)
                       ? params.zombieBurnDamageIntervalSeconds
                       : 0.5f,
                   0.05f, 10.0f);
    params.zombieSpreadIntervalSeconds =
        std::clamp(std::isfinite(params.zombieSpreadIntervalSeconds)
                       ? params.zombieSpreadIntervalSeconds
                       : 1.5f,
                   0.1f, 30.0f);
    params.zombieSpreadChance = Clamp01(params.zombieSpreadChance);
    params.zombiePanicSeconds =
        std::clamp(std::isfinite(params.zombiePanicSeconds)
                       ? params.zombiePanicSeconds
                       : 4.0f,
                   0.0f, 120.0f);
    params.zombiePanicSpeedScale =
        std::clamp(std::isfinite(params.zombiePanicSpeedScale)
                       ? params.zombiePanicSpeedScale
                       : 1.25f,
                   0.5f, 3.0f);
    params.zombiePanicChaseRadius =
        std::clamp(std::isfinite(params.zombiePanicChaseRadius)
                       ? params.zombiePanicChaseRadius
                       : 12.0f,
                   0.0f, 64.0f);
    return params;
}

float FireIgnitionChance(const Flammability& flammability, float wetness,
                         const FireParams& params) {
    const FireParams sanitized = SanitizeFireParams(params);
    const float resistance = Clamp01(flammability.ignitionThreshold);
    const float moisture = Clamp01(wetness);
    const float chance =
        1.0f - resistance - moisture * sanitized.wetnessPenalty;
    return std::clamp(chance, sanitized.minIgnitionChance, 1.0f);
}

float FireIgnitionSeconds(const Flammability& flammability,
                          const FireParams& params) {
    const FireParams sanitized = SanitizeFireParams(params);
    const float resistance = Clamp01(flammability.ignitionThreshold);
    return std::clamp(sanitized.ignitionSeconds * (1.0f + resistance),
                      0.05f, 60.0f);
}

float FireSpreadInterval(float burnSeconds, const FireParams& params) {
    const FireParams sanitized = SanitizeFireParams(params);
    const float burnScale = std::isfinite(burnSeconds)
                                ? std::clamp(burnSeconds / 60.0f, 0.0f, 3.0f)
                                : 1.0f;
    return std::clamp(
        sanitized.spreadIntervalSeconds * (0.5f + burnScale), 0.5f, 120.0f);
}

float FireSpreadChance(const Flammability& flammability, float wetness,
                       const FireParams& params) {
    const FireParams sanitized = SanitizeFireParams(params);
    return Clamp01(FireIgnitionChance(flammability, wetness, sanitized) *
                   sanitized.spreadChanceScale);
}

std::size_t ApplyFireDamage(ecs::EcsWorld& ecs,
                            characters::CombatSystem& combat,
                            const FireParams& params, float elapsedSeconds) {
    const FireParams sanitized = SanitizeFireParams(params);
    if (!(elapsedSeconds > 0.0f) ||
        !(sanitized.fireDamagePerSecond > 0.0f) ||
        !(sanitized.fireDamageRadius > 0.0f)) {
        return 0;
    }
    struct FirePoint {
        float x = 0.0f;
        float y = 0.0f;
        int z = 0;
    };
    std::vector<FirePoint> fires;
    ecs.Registry().view<ecs::FireSource, ecs::Position>().each(
        [&](const ecs::Entity, const ecs::FireSource& source,
            const ecs::Position& position) {
            if (source.phase == ecs::FirePhase::Burning) {
                fires.push_back(FirePoint{position.x, position.y, position.z});
            }
        });
    if (fires.empty()) {
        return 0;
    }

    const float damage =
        sanitized.fireDamagePerSecond * elapsedSeconds;
    const float radiusSq =
        sanitized.fireDamageRadius * sanitized.fireDamageRadius;
    std::size_t hitCount = 0;
    ecs.Registry().view<ecs::Health, ecs::Position>().each(
        [&](const ecs::Entity entity, ecs::Health& health,
            const ecs::Position& position) {
            if (health.current <= 0.0f) {
                return;
            }
            bool inFire = false;
            for (const FirePoint& fire : fires) {
                if (fire.z != position.z) {
                    continue;
                }
                const float dx = position.x - fire.x;
                const float dy = position.y - fire.y;
                if (dx * dx + dy * dy <= radiusSq) {
                    inFire = true;
                    break;
                }
            }
            if (!inFire) {
                return;
            }
            // M23-F3-T5：僵尸进入燃烧区才获得燃烧状态；玩家不会获得该组件。
            if (ecs.Registry().all_of<ecs::ZombieVariant>(entity) &&
                !ecs.Registry().any_of<ecs::BurningZombie>(entity)) {
                ecs::BurningZombie burning;
                burning.remaining = sanitized.zombieBurnSeconds;
                burning.panicRemaining = sanitized.zombiePanicSeconds;
                burning.panicSpeedScale = sanitized.zombiePanicSpeedScale;
                burning.panicChaseRadius = sanitized.zombiePanicChaseRadius;
                ecs.Registry().emplace<ecs::BurningZombie>(entity, burning);
                if (auto* perception =
                        ecs.Registry().try_get<ecs::PerceptionState>(entity);
                    perception != nullptr) {
                    perception->alertness = 1.0f;
                }
            }
            ecs::DamageEvent event;
            event.type = ecs::DamageType::Fire;
            event.amount = damage;
            if (combat.ApplyDamage(ecs, entity, event)) {
                ++hitCount;
            }
        });
    return hitCount;
}

std::size_t ApplyBurningZombieDamage(ecs::EcsWorld& ecs,
                                     characters::CombatSystem& combat,
                                     const FireParams& params,
                                     float elapsedSeconds) {
    const FireParams sanitized = SanitizeFireParams(params);
    if (!std::isfinite(elapsedSeconds) || !(elapsedSeconds > 0.0f)) {
        return 0;
    }
    std::vector<ecs::Entity> expired;
    std::size_t hitCount = 0;
    ecs.Registry().view<ecs::BurningZombie, ecs::Health>().each(
        [&](const ecs::Entity entity, ecs::BurningZombie& burning,
            ecs::Health& health) {
            if (health.current <= 0.0f) {
                expired.push_back(entity);
                return;
            }
            burning.remaining = ClampNonNegative(burning.remaining -
                                                 elapsedSeconds);
            burning.panicRemaining = ClampNonNegative(
                burning.panicRemaining - elapsedSeconds);
            burning.damageAccumulator =
                ClampNonNegative(burning.damageAccumulator + elapsedSeconds);
            if (sanitized.zombieBurnDamagePerSecond > 0.0f &&
                burning.damageAccumulator >=
                    sanitized.zombieBurnDamageIntervalSeconds) {
                ecs::DamageEvent event;
                event.type = ecs::DamageType::Fire;
                event.amount = sanitized.zombieBurnDamagePerSecond *
                               burning.damageAccumulator;
                burning.damageAccumulator = 0.0f;
                if (combat.ApplyDamage(ecs, entity, event)) {
                    ++hitCount;
                }
            } else if (!(sanitized.zombieBurnDamagePerSecond > 0.0f)) {
                burning.damageAccumulator = 0.0f;
            }
            if (!(burning.remaining > 0.0f)) {
                expired.push_back(entity);
            }
        });
    for (const ecs::Entity entity : expired) {
        if (ecs.Alive(entity)) {
            ecs.Registry().remove<ecs::BurningZombie>(entity);
        }
    }
    return hitCount;
}

FireIgnitionResult TryIgniteFire(ecs::FireSource& source,
                                 const FireMaterialDef& material, float wetness,
                                 float roll, const FireParams& params) {
    if (FireIsActive(source)) {
        return FireIgnitionResult::AlreadyActive;
    }
    const FireParams sanitized = SanitizeFireParams(params);
    const float chance =
        FireIgnitionChance(material.flammability, wetness, sanitized);
    const float clampedRoll = Clamp01(roll);
    source.materialId = material.id;
    source.ignitionProgress = 0.0f;
    source.ignitionDuration =
        FireIgnitionSeconds(material.flammability, sanitized);
    source.burnDuration =
        ClampNonNegative(material.flammability.burnSeconds);
    source.burnRemaining = source.burnDuration;
    source.smokePerSecond =
        ClampNonNegative(material.flammability.smokePerSecond);
    source.heatRadius = ClampNonNegative(material.flammability.heatRadius);
    source.smolderElapsed = 0.0f;
    source.spreadAccumulator = 0.0f;
    if (clampedRoll >= chance) {
        source.phase = ecs::FirePhase::Extinguished;
        return FireIgnitionResult::TooWet;
    }
    source.phase = ecs::FirePhase::Igniting;
    return FireIgnitionResult::Ignited;
}

void AdvanceFire(ecs::FireSource& source, float dt, const FireParams& params,
                 float ambientWetness) {
    if (!std::isfinite(dt) || !(dt > 0.0f)) {
        return;
    }
    const FireParams sanitized = SanitizeFireParams(params);
    switch (source.phase) {
        case ecs::FirePhase::Igniting: {
            source.ignitionProgress += dt;
            const float duration = std::max(0.01f, source.ignitionDuration);
            if (source.ignitionProgress >= duration) {
                source.ignitionProgress = duration;
                source.phase = ecs::FirePhase::Burning;
            }
            break;
        }
        case ecs::FirePhase::Burning: {
            const float rainScale =
                1.0f + Clamp01(ambientWetness) *
                           sanitized.rainBurnDecayScale;
            source.burnRemaining =
                std::max(0.0f, source.burnRemaining - dt * rainScale);
            if (source.burnRemaining <= 0.0f) {
                source.phase = ecs::FirePhase::Smoldering;
                source.smolderElapsed = 0.0f;
            }
            break;
        }
        case ecs::FirePhase::Smoldering:
            source.smolderElapsed += dt;
            if (source.smolderElapsed >= sanitized.smolderSeconds) {
                ExtinguishFire(source);
            }
            break;
        case ecs::FirePhase::Extinguished:
            break;
    }
}

void ExtinguishFire(ecs::FireSource& source) {
    source.phase = ecs::FirePhase::Extinguished;
    source.ignitionProgress = 0.0f;
    source.burnRemaining = 0.0f;
    source.smokePerSecond = 0.0f;
    source.smolderElapsed = 0.0f;
    source.spreadAccumulator = 0.0f;
}

bool FireIsActive(const ecs::FireSource& source) {
    return source.phase != ecs::FirePhase::Extinguished;
}

bool FireProvidesHeat(const ecs::FireSource& source) {
    return source.heatRadius > 0.0f &&
           (source.phase == ecs::FirePhase::Burning ||
            source.phase == ecs::FirePhase::Smoldering);
}

float FireSmokePerSecond(const ecs::FireSource& source) {
    switch (source.phase) {
        case ecs::FirePhase::Igniting:
            return source.smokePerSecond * 0.2f;
        case ecs::FirePhase::Burning:
            return source.smokePerSecond;
        case ecs::FirePhase::Smoldering:
            return source.smokePerSecond * 0.25f;
        case ecs::FirePhase::Extinguished:
            return 0.0f;
    }
    return 0.0f;
}

const char* FirePhaseName(ecs::FirePhase phase) {
    switch (phase) {
        case ecs::FirePhase::Igniting:
            return "点燃中";
        case ecs::FirePhase::Burning:
            return "燃烧";
        case ecs::FirePhase::Smoldering:
            return "阴燃";
        case ecs::FirePhase::Extinguished:
            return "已熄灭";
    }
    return "未知";
}

FireSystem::FireSystem(const FireRegistry& registry, FireParams params,
                       std::uint32_t seed)
    : m_registry(&registry),
      m_params(SanitizeFireParams(params)),
      m_rng(seed == 0 ? 20261029u : seed) {}

void FireSystem::Update(ecs::EcsWorld& ecs, float dt,
                        world::World* world, float ambientWetness,
                        std::vector<FireBurnOut>* burnOuts) {
    if (!(dt > 0.0f)) {
        return;
    }

    struct SpreadPulse {
        int x = 0;
        int y = 0;
        int z = 0;
    };
    std::vector<SpreadPulse> pulses;
    entt::registry& registry = ecs.Registry();
    registry.view<ecs::FireSource>().each(
        [&](const ecs::Entity entity, ecs::FireSource& source) {
            const ecs::FireAnchor* anchor =
                registry.try_get<ecs::FireAnchor>(entity);
            if (world != nullptr &&
                (anchor == nullptr ||
                 !SquareLoaded(*world, anchor->x, anchor->y))) {
                return;
            }
            const ecs::FirePhase before = source.phase;
            AdvanceFire(source, dt, m_params, ambientWetness);
            if (burnOuts != nullptr && before != ecs::FirePhase::Extinguished &&
                source.phase == ecs::FirePhase::Extinguished &&
                anchor != nullptr) {
                burnOuts->push_back(FireBurnOut{
                    anchor->x, anchor->y, anchor->z, source.materialId});
            }
            if (world == nullptr || source.phase != ecs::FirePhase::Burning ||
                anchor == nullptr) {
                return;
            }
            const float interval =
                FireSpreadInterval(source.burnDuration, m_params);
            source.spreadAccumulator =
                ClampNonNegative(source.spreadAccumulator + dt);
            if (source.spreadAccumulator < interval) {
                return;
            }
            source.spreadAccumulator =
                ClampNonNegative(source.spreadAccumulator - interval);
            pulses.push_back(SpreadPulse{anchor->x, anchor->y, anchor->z});
        });

    if (world == nullptr || pulses.empty() ||
        m_params.maxSpreadsPerTick <= 0) {
        return;
    }
    std::size_t active = ActiveSourceCount(ecs);
    if (active >= static_cast<std::size_t>(m_params.maxActiveSources)) {
        return;
    }

    struct SpreadCandidate {
        int x = 0;
        int y = 0;
        int z = 0;
        const FireMaterialDef* material = nullptr;
    };
    constexpr int kDirections[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    std::vector<SpreadCandidate> candidates;
    std::set<std::tuple<int, int, int>> seen;
    for (const SpreadPulse& pulse : pulses) {
        if (!SquareLoaded(*world, pulse.x, pulse.y)) {
            continue;
        }
        for (const auto& direction : kDirections) {
            const int x = pulse.x + direction[0];
            const int y = pulse.y + direction[1];
            if (!SquareLoaded(*world, x, y)) {
                continue;
            }
            if (!seen.emplace(x, y, pulse.z).second) {
                continue;
            }
            const world::ChunkCoord coord = world::ChunkOf(x, y);
            const world::Chunk* chunk =
                FindChunkInWorld(*world, coord.cx, coord.cy);
            if (chunk == nullptr) {
                continue;
            }
            const world::SquareLocal local = world::SquareLocalOf(x, y);
            const FireMaterialDef* material =
                FindFireMaterial(*m_registry,
                                 chunk->Square(local.lx, local.ly, pulse.z));
            if (material == nullptr) {
                continue;
            }
            candidates.push_back(
                SpreadCandidate{x, y, pulse.z, material});
        }
    }

    if (candidates.empty()) {
        return;
    }
    std::shuffle(candidates.begin(), candidates.end(), m_rng);
    std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
    int created = 0;
    for (const SpreadCandidate& candidate : candidates) {
        if (created >= m_params.maxSpreadsPerTick ||
            active >= static_cast<std::size_t>(m_params.maxActiveSources)) {
            break;
        }
        ecs::FireSource* existing = FindAt(ecs, candidate.x, candidate.y,
                                            candidate.z);
        if (existing != nullptr && FireIsActive(*existing)) {
            continue;
        }
        const float chance =
            FireSpreadChance(candidate.material->flammability,
                             ambientWetness, m_params);
        if (!(chance > 0.0f) || distribution(m_rng) >= chance) {
            continue;
        }
        if (existing != nullptr) {
            if (TryIgniteFire(*existing, *candidate.material, 0.0f, 0.0f,
                              m_params) != FireIgnitionResult::Ignited) {
                continue;
            }
        } else {
            ecs::FireSource fired;
            if (TryIgniteFire(fired, *candidate.material, 0.0f, 0.0f,
                              m_params) != FireIgnitionResult::Ignited) {
                continue;
            }
            AddFireEntity(ecs, candidate.x, candidate.y, candidate.z,
                          std::move(fired));
        }
        ++active;
        ++created;
    }
}

FireIgnitionResult FireSystem::IgniteAt(ecs::EcsWorld& ecs,
                                        world::World& world, int wx, int wy,
                                        int z, float wetness,
                                        std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
    };
    if (m_registry == nullptr) {
        fail("火焰数据未加载");
        return FireIgnitionResult::InvalidMaterial;
    }
    const world::ChunkCoord chunkCoord = world::ChunkOf(wx, wy);
    const world::Chunk* chunk =
        FindChunkInWorld(world, chunkCoord.cx, chunkCoord.cy);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
        fail("目标格未加载");
        return FireIgnitionResult::InvalidMaterial;
    }
    const world::SquareLocal local = world::SquareLocalOf(wx, wy);
    const FireMaterialDef* material =
        FindFireMaterial(*m_registry, chunk->Square(local.lx, local.ly, z));
    if (material == nullptr) {
        fail("目标格没有可燃物");
        return FireIgnitionResult::InvalidMaterial;
    }

    ecs::FireSource* existing = FindAt(ecs, wx, wy, z);
    if (existing != nullptr && FireIsActive(*existing)) {
        fail("该格已有火源");
        return FireIgnitionResult::AlreadyActive;
    }
    if (ActiveSourceCount(ecs) >=
        static_cast<std::size_t>(m_params.maxActiveSources)) {
        fail("活跃火源已达上限");
        return FireIgnitionResult::AtCapacity;
    }

    std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
    const float roll = distribution(m_rng);
    if (existing != nullptr) {
        const FireIgnitionResult result =
            TryIgniteFire(*existing, *material, wetness, roll, m_params);
        if (result == FireIgnitionResult::TooWet) {
            fail("环境过于潮湿，点火失败（成功率 " +
                 std::to_string(static_cast<int>(
                     FireIgnitionChance(material->flammability, wetness,
                                        m_params) *
                     100.0f)) +
                 "%）");
        }
        return result;
    }

    ecs::FireSource candidate;
    const FireIgnitionResult result =
        TryIgniteFire(candidate, *material, wetness, roll, m_params);
    if (result != FireIgnitionResult::Ignited) {
        fail("环境过于潮湿，点火失败（成功率 " +
             std::to_string(static_cast<int>(
                 FireIgnitionChance(material->flammability, wetness, m_params) *
                 100.0f)) +
             "%）");
        return result;
    }

    AddFireEntity(ecs, wx, wy, z, std::move(candidate));
    return FireIgnitionResult::Ignited;
}

bool FireSystem::ExtinguishAt(ecs::EcsWorld& ecs, int wx, int wy, int z) {
    ecs::FireSource* source = FindAt(ecs, wx, wy, z);
    if (source == nullptr || !FireIsActive(*source)) {
        return false;
    }
    ExtinguishFire(*source);
    return true;
}

std::size_t FireSystem::UnloadChunk(ecs::EcsWorld& ecs, int chunkX,
                                    int chunkY) {
    std::vector<ecs::Entity> removed;
    ecs.Registry().view<ecs::FireSource, ecs::FireAnchor>().each(
        [&](const ecs::Entity entity, const ecs::FireSource& source,
            const ecs::FireAnchor& anchor) {
            if (!FireIsActive(source)) {
                return;
            }
            const world::ChunkCoord coord = world::ChunkOf(anchor.x, anchor.y);
            if (coord.cx == chunkX && coord.cy == chunkY) {
                removed.push_back(entity);
            }
        });
    for (const ecs::Entity entity : removed) {
        ecs.Destroy(entity);
    }
    return removed.size();
}

FireBurnResolution FireSystem::ResolveBurnedCell(
    world::World& world, const FireBurnOut& burned) const {
    FireBurnResolution result;
    if (m_registry == nullptr) {
        return result;
    }
    const FireMaterialDef* material = m_registry->Find(burned.materialId);
    if (material == nullptr || material->id == "campfire") {
        return result;
    }
    const world::ChunkCoord coord = world::ChunkOf(burned.x, burned.y);
    world::Chunk* chunk = FindChunkInWorld(world, coord.cx, coord.cy);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded ||
        burned.z < 0 || burned.z >= core::kMaxZ) {
        return result;
    }
    const world::SquareLocal local = world::SquareLocalOf(burned.x, burned.y);
    world::GridSquare& square = chunk->Square(local.lx, local.ly, burned.z);
    constexpr world::SquareLayer kLayers[] = {
        world::SquareLayer::Furniture, world::SquareLayer::Wall,
        world::SquareLayer::Floor};
    for (const world::SquareLayer layer : kLayers) {
        auto& objects = square.Layer(layer);
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            if (std::find(material->sprites.begin(), material->sprites.end(),
                          it->sprite) == material->sprites.end()) {
                continue;
            }
            if (layer == world::SquareLayer::Floor) {
                if (!it->playerBuilt) {
                    return result;
                }
                if (!it->underlay.empty()) {
                    square.SetFloor(world::SquareObject{it->underlay});
                } else {
                    square.ClearLayer(world::SquareLayer::Floor);
                }
            } else {
                objects.erase(it);
            }
            result.structureRemoved = true;
            for (const FireDrop& drop : material->charredDrops) {
                result.drops.push_back(FireBurnedDrop{drop.itemId, drop.count});
            }
            return result;
        }
    }
    return result;
}

ecs::FireSource* FireSystem::FindAt(ecs::EcsWorld& ecs, int wx, int wy,
                                    int z) {
    ecs::FireSource* found = nullptr;
    ecs.Registry().view<ecs::FireSource, ecs::FireAnchor>().each(
        [&](const ecs::Entity, ecs::FireSource& source,
            const ecs::FireAnchor& anchor) {
            if (found == nullptr && anchor.x == wx && anchor.y == wy &&
                anchor.z == z) {
                found = &source;
            }
        });
    return found;
}

const ecs::FireSource* FireSystem::FindAt(const ecs::EcsWorld& ecs, int wx,
                                          int wy, int z) const {
    const ecs::FireSource* found = nullptr;
    ecs.Registry().view<ecs::FireSource, ecs::FireAnchor>().each(
        [&](const ecs::Entity, const ecs::FireSource& source,
            const ecs::FireAnchor& anchor) {
            if (found == nullptr && anchor.x == wx && anchor.y == wy &&
                anchor.z == z) {
                found = &source;
            }
        });
    return found;
}

bool FireSystem::FindNearestActiveFire(const ecs::EcsWorld& ecs, float x,
                                       float y, int z, float radius,
                                       FireTarget* out) const {
    if (!std::isfinite(x) || !std::isfinite(y) || !(radius > 0.0f)) {
        return false;
    }
    const float radiusSq = radius * radius;
    FireTarget best;
    bool found = false;
    ecs.Registry().view<ecs::FireSource, ecs::FireAnchor>().each(
        [&](const ecs::Entity, const ecs::FireSource& source,
            const ecs::FireAnchor& anchor) {
            if (!FireIsActive(source) || anchor.z != z) {
                return;
            }
            const float centerX = static_cast<float>(anchor.x) + 0.5f;
            const float centerY = static_cast<float>(anchor.y) + 0.5f;
            const float dx = centerX - x;
            const float dy = centerY - y;
            const float distanceSq = dx * dx + dy * dy;
            if (distanceSq > radiusSq ||
                (found && distanceSq >= best.distanceSq)) {
                return;
            }
            best.x = anchor.x;
            best.y = anchor.y;
            best.z = anchor.z;
            best.source = source;
            best.material =
                m_registry != nullptr ? m_registry->Find(source.materialId)
                                      : nullptr;
            best.distanceSq = distanceSq;
            found = true;
        });
    if (found && out != nullptr) {
        *out = best;
    }
    return found;
}

FireExtinguishPlan PlanFireExtinguish(const ecs::Container& container,
                                      const items::ItemRegistry& registry,
                                      const FireParams& params) {
    const FireParams sanitized = SanitizeFireParams(params);
    FireExtinguishPlan plan;
    float bestChance = -1.0f;
    for (std::size_t i = 0; i < container.items.size(); ++i) {
        const ecs::ItemInstance& entry = container.items[i];
        const items::ItemDef* def = registry.Find(entry.defId);
        if (def == nullptr || !(entry.condition > 0.0f) ||
            !(def->use.extinguish > 0.0f)) {
            continue;
        }
        const float chance = Clamp01(
            def->use.extinguish * (0.5f + 0.5f * entry.condition));
        if (chance <= bestChance) {
            continue;
        }
        bestChance = chance;
        plan.usesItem = true;
        plan.bareHands = false;
        plan.index = static_cast<int>(i);
        plan.defId = def->id;
        plan.name = def->name;
        plan.replacementDefId = def->use.replaceWith;
        plan.chance = chance;
        plan.wear = def->use.replaceWith.empty()
                        ? Clamp01(def->use.extinguishWear)
                        : 0.0f;
        plan.selfDamage = 0.0f;
    }
    if (!plan.usesItem) {
        plan.bareHands = true;
        plan.chance = sanitized.bareHandsChance;
        plan.selfDamage = sanitized.bareHandsSelfDamage;
    }
    return plan;
}

bool ConsumeFireExtinguishItem(ecs::Container& container,
                               const items::ItemRegistry& registry,
                               const FireExtinguishPlan& plan) {
    if (!plan.usesItem || plan.index < 0 ||
        static_cast<std::size_t>(plan.index) >= container.items.size()) {
        return false;
    }
    const std::size_t index = static_cast<std::size_t>(plan.index);
    ecs::ItemInstance& entry = container.items[index];
    const items::ItemDef* def = registry.Find(entry.defId);
    if (def == nullptr || !(def->use.extinguish > 0.0f) ||
        !(entry.condition > 0.0f)) {
        return false;
    }
    if (!plan.replacementDefId.empty()) {
        const items::ItemDef* replacement =
            registry.Find(plan.replacementDefId);
        if (replacement == nullptr) {
            return false;
        }
        if (entry.count <= 1) {
            entry.defId = replacement->id;
            entry.condition = 1.0f;
            entry.loadedAmmo = 0;
            return true;
        }
        --entry.count;
        (void)items::AddItem(
            container, registry,
            ecs::ItemInstance{replacement->id, 1, 1.0f});
        return true;
    }
    const float wear = plan.wear > 0.0f ? plan.wear : 1.0f;
    entry.condition = std::max(0.0f, entry.condition - wear);
    if (entry.condition <= 0.0f) {
        container.items.erase(container.items.begin() +
                              static_cast<std::ptrdiff_t>(index));
    }
    return true;
}

bool FireExtinguishSucceeds(float chance, float roll) {
    const float clampedChance = Clamp01(chance);
    if (!(clampedChance > 0.0f) || !std::isfinite(roll)) {
        return false;
    }
    if (clampedChance >= 1.0f) {
        return true;
    }
    return roll < clampedChance;
}

std::size_t FireSystem::SpreadFromBurningZombies(
    ecs::EcsWorld& ecs, world::World& world, float dt,
    float ambientWetness) {
    if (!(dt > 0.0f) || !(m_params.zombieSpreadChance > 0.0f) ||
        m_params.maxSpreadsPerTick <= 0) {
        return 0;
    }
    struct BurningCell {
        int x = 0;
        int y = 0;
        int z = 0;
    };
    std::vector<BurningCell> cells;
    ecs.Registry().view<ecs::BurningZombie, ecs::Position>().each(
        [&](const ecs::Entity, ecs::BurningZombie& burning,
            const ecs::Position& position) {
            burning.spreadAccumulator += dt;
            if (burning.spreadAccumulator <
                m_params.zombieSpreadIntervalSeconds) {
                return;
            }
            burning.spreadAccumulator = ClampNonNegative(
                burning.spreadAccumulator -
                m_params.zombieSpreadIntervalSeconds);
            cells.push_back(BurningCell{
                static_cast<int>(std::floor(position.x)),
                static_cast<int>(std::floor(position.y)), position.z});
        });
    if (cells.empty()) {
        return 0;
    }
    std::size_t active = ActiveSourceCount(ecs);
    if (active >= static_cast<std::size_t>(m_params.maxActiveSources)) {
        return 0;
    }
    std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
    int created = 0;
    for (const BurningCell& cell : cells) {
        if (created >= m_params.maxSpreadsPerTick ||
            active >= static_cast<std::size_t>(m_params.maxActiveSources)) {
            break;
        }
        if (distribution(m_rng) >= m_params.zombieSpreadChance) {
            continue;
        }
        if (IgniteAt(ecs, world, cell.x, cell.y, cell.z, ambientWetness,
                     nullptr) != FireIgnitionResult::Ignited) {
            continue;
        }
        ++created;
        ++active;
    }
    return static_cast<std::size_t>(created);
}

std::size_t FireSystem::ActiveSourceCount(const ecs::EcsWorld& ecs) const {
    std::size_t count = 0;
    ecs.Registry().view<ecs::FireSource>().each(
        [&](const ecs::Entity, const ecs::FireSource& source) {
            if (FireIsActive(source)) {
                ++count;
            }
        });
    return count;
}

bool FireSystem::HasActiveFireNear(const ecs::EcsWorld& ecs, float x, float y,
                                   int z) const {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    bool near = false;
    ecs.Registry().view<ecs::FireSource, ecs::Position>().each(
        [&](const ecs::Entity, const ecs::FireSource& source,
            const ecs::Position& position) {
            if (near || position.z != z || !FireProvidesHeat(source)) {
                return;
            }
            const float radius = source.heatRadius;
            const float dx = position.x - x;
            const float dy = position.y - y;
            near = dx * dx + dy * dy <= radius * radius;
        });
    return near;
}

}  // namespace zl::game
