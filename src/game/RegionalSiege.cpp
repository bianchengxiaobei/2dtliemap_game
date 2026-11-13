#include "game/RegionalSiege.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace zl::game {

void RegionalSiege::Configure(RegionalSiegeConfig config,
                              RegionalSiegeCallbacks callbacks) {
    SanitizeConfig(config);
    m_config = std::move(config);
    m_callbacks = std::move(callbacks);
    Reset();
}

void RegionalSiege::ConfigureReclaim(RegionalSiegeConfig config) {
    SanitizeConfig(config);
    m_reclaimConfig = std::move(config);
}

const RegionalSiegeConfig& RegionalSiege::ActiveConfig() const {
    return m_start.reclaim ? m_reclaimConfig : m_config;
}

void RegionalSiege::SanitizeConfig(RegionalSiegeConfig& config) {
    config.warmupSeconds = std::clamp(config.warmupSeconds, 0.0f, 3600.0f);
    config.waveIntervalSeconds =
        std::clamp(config.waveIntervalSeconds, 0.0f, 3600.0f);
    config.maxAlive = std::clamp(config.maxAlive, 1, 32);
    config.maxWaveSize = std::clamp(config.maxWaveSize, 1, 32);
    config.breachRadius = std::clamp(config.breachRadius, 0.5f, 100.0f);
    config.lossDelaySeconds =
        std::clamp(config.lossDelaySeconds, 0.0f, 120.0f);
    if (config.waves.empty()) {
        config.waves = {4, 6, 8};
    }
    for (int& wave : config.waves) {
        wave = std::clamp(wave, 0, config.maxWaveSize);
    }
}

void RegionalSiege::Reset() {
    ResetRuntime();
    m_phase = RegionalSiegePhase::Idle;
    m_start = {};
    m_result = {};
    m_snapshot = {};
}

void RegionalSiege::ResetRuntime() {
    m_nextWave = 0;
    m_pendingWave = 0;
    m_activeWave = 0;
    m_spawned = 0;
    m_guardsStart = 0;
    m_guardsNow = 0;
    m_alive = 0;
    m_phaseTimer = 0.0f;
    m_lossTimer = 0.0f;
    m_breached = false;
    m_resultEmitted = false;
}

bool RegionalSiege::Start(const RegionalSiegeStart& start,
                          std::int64_t gameMinute) {
    const RegionalSiegeConfig& config =
        start.reclaim ? m_reclaimConfig : m_config;
    if (!config.enabled || start.regionId.empty()) {
        return false;
    }
    if (Active()) {
        return start.regionId == m_start.regionId;
    }
    const std::string settlementId =
        start.settlementId.empty() ? start.regionId : start.settlementId;
    if (start.reclaim) {
        if (!m_callbacks.beginReclaim ||
            !m_callbacks.beginReclaim(settlementId)) {
            return false;
        }
    } else if (!m_callbacks.beginDefense ||
               !m_callbacks.beginDefense(settlementId)) {
        return false;
    }

    ResetRuntime();
    m_start = start;
    m_start.settlementId = settlementId;
    m_phase = RegionalSiegePhase::Warmup;
    m_phaseTimer = config.warmupSeconds;
    m_guardsStart = m_callbacks.aliveGuards
                        ? std::max(0, m_callbacks.aliveGuards(settlementId))
                        : 0;
    m_guardsNow = m_guardsStart;
    m_result.regionId = start.regionId;
    m_result.settlementId = settlementId;
    m_result.regionName = start.regionName;
    m_result.attackMinute = start.attackMinute;
    m_result.reclaim = start.reclaim;
    m_result.waveCount = static_cast<int>(config.waves.size());
    m_result.guardsStart = m_guardsStart;
    m_result.direction = start.primaryDirection;
    m_result.breachDirection = start.primaryDirection;
    (void)gameMinute;
    RefreshSnapshot();
    return true;
}

void RegionalSiege::StartNextWave() {
    const RegionalSiegeConfig& config = ActiveConfig();
    if (m_nextWave >= static_cast<int>(config.waves.size())) {
        return;
    }
    m_activeWave = m_nextWave;
    m_nextWave += 1;
    m_pendingWave = std::clamp(
        config.waves[static_cast<std::size_t>(m_activeWave)], 0,
        config.maxWaveSize);
    m_phaseTimer = config.waveIntervalSeconds;
    ApplyPendingSpawn();
}

void RegionalSiege::ApplyPendingSpawn() {
    if (m_pendingWave <= 0 || !m_callbacks.spawnWave) {
        return;
    }
    const int capacity = std::max(0, ActiveConfig().maxAlive - m_alive);
    if (capacity <= 0) {
        return;
    }
    const int requested = std::min(m_pendingWave, capacity);
    const int spawned = std::clamp(
        m_callbacks.spawnWave(m_start.settlementId, requested, m_activeWave,
                              m_start.contestedFaction),
        0, requested);
    if (spawned <= 0) {
        return;
    }
    m_pendingWave -= spawned;
    m_spawned += spawned;
    if (m_callbacks.aliveEnemies) {
        m_alive = std::max(0, m_callbacks.aliveEnemies(m_start.settlementId));
    }
}

void RegionalSiege::RefreshSnapshot() {
    m_snapshot.active = Active();
    m_snapshot.phase = m_phase;
    m_snapshot.reclaim = m_start.reclaim;
    m_snapshot.regionId = m_start.regionId;
    m_snapshot.settlementId = m_start.settlementId;
    m_snapshot.regionName = m_start.regionName;
    m_snapshot.wave = std::min(m_nextWave, m_result.waveCount);
    m_snapshot.waveCount = m_result.waveCount;
    m_snapshot.aliveEnemies = m_alive;
    m_snapshot.aliveGuards = m_guardsNow;
    m_snapshot.guardsStart = m_guardsStart;
    m_snapshot.breached = m_breached;
    const char* label = m_start.reclaim ? "区域反攻" : "区域围攻";
    if (m_phase == RegionalSiegePhase::Warmup) {
        char text[192];
        if (m_start.reclaim) {
            std::snprintf(text, sizeof(text),
                          "%s · %s · 集结中 %.0f 秒 · 清理核心后守住波次",
                          label, m_start.regionName.c_str(),
                          std::max(0.0f, m_phaseTimer));
        } else {
            std::snprintf(text, sizeof(text),
                          "%s · %s · 集结中 %.0f 秒 · 守军 %d", label,
                          m_start.regionName.c_str(),
                          std::max(0.0f, m_phaseTimer),
                          std::max(0, m_guardsNow));
        }
        m_snapshot.statusText = text;
    } else if (m_phase == RegionalSiegePhase::Waves) {
        char text[224];
        std::snprintf(
            text, sizeof(text),
            "%s · %s · 第 %d/%d 波 · 敌 %d · 守军 %d%s", label,
            m_start.regionName.c_str(),
            std::min(std::max(1, m_nextWave), m_result.waveCount),
            m_result.waveCount, std::max(0, m_alive),
            std::max(0, m_guardsNow),
            m_breached ? " · 防线被突破" : "");
        m_snapshot.statusText = text;
    } else {
        m_snapshot.statusText.clear();
    }
}

void RegionalSiege::Update(float dt, std::int64_t gameMinute,
                           bool playerInside) {
    if (!Active()) {
        return;
    }
    if (!playerInside) {
        Abort(gameMinute, "你离开区域，围攻转为战略自动结算");
        return;
    }
    dt = std::max(0.0f, dt);
    if (m_callbacks.aliveEnemies) {
        m_alive = std::max(0, m_callbacks.aliveEnemies(m_start.settlementId));
    }
    if (m_callbacks.aliveGuards) {
        m_guardsNow = std::max(0, m_callbacks.aliveGuards(m_start.settlementId));
    }

    if (m_phase == RegionalSiegePhase::Warmup) {
        m_phaseTimer = std::max(0.0f, m_phaseTimer - dt);
        if (m_phaseTimer <= 0.0f) {
            m_phase = RegionalSiegePhase::Waves;
            StartNextWave();
        }
    }

    if (m_phase == RegionalSiegePhase::Waves) {
        const RegionalSiegeConfig& config = ActiveConfig();
        if (m_pendingWave > 0) {
            ApplyPendingSpawn();
        } else if (m_nextWave <
                   static_cast<int>(config.waves.size())) {
            m_phaseTimer = std::max(0.0f, m_phaseTimer - dt);
            if (m_phaseTimer <= 0.0f) {
                StartNextWave();
            }
        }

        if (!m_breached && m_guardsNow == 0 && m_alive > 0 &&
            m_callbacks.raidersWithin) {
            m_breached =
                m_callbacks.raidersWithin(m_start.settlementId, m_start.centerX,
                                          m_start.centerY,
                                          config.breachRadius) > 0;
        }

        const bool allWavesStarted =
            m_nextWave >= static_cast<int>(config.waves.size()) &&
            m_pendingWave == 0;
        if (allWavesStarted && m_alive == 0) {
            if (m_spawned == 0) {
                Finish(false, gameMinute, m_start.reclaim ? "反攻未能集结"
                                                          : "围攻未能集结");
            } else {
                Finish(true, gameMinute,
                       m_start.reclaim ? "反攻波次击退" : "围攻击退");
            }
        } else if (m_breached && m_guardsNow == 0 && m_alive > 0) {
            m_lossTimer += dt;
            if (m_lossTimer >= config.lossDelaySeconds) {
                Finish(false, gameMinute,
                       m_start.reclaim ? "反攻防线被突破"
                                       : "防线被突破，守卫倒下");
            }
        }
    }

    RefreshSnapshot();
}

void RegionalSiege::Abort(std::int64_t gameMinute, std::string_view reason) {
    if (!Active()) {
        return;
    }
    m_phase = RegionalSiegePhase::Finished;
    m_resultEmitted = true;
    const int aliveLeft = m_alive;
    if (m_start.reclaim) {
        if (m_callbacks.endReclaim) {
            m_callbacks.endReclaim(m_start.settlementId, false, reason);
        }
    } else if (m_callbacks.endDefense) {
        m_callbacks.endDefense(m_start.settlementId, false, reason);
    }
    m_result.regionId = m_start.regionId;
    m_result.settlementId = m_start.settlementId;
    m_result.reclaim = m_start.reclaim;
    m_result.regionName = m_start.regionName;
    m_result.attackMinute = m_start.attackMinute;
    m_result.resolvedMinute = gameMinute;
    m_result.held = false;
    m_result.breached = m_breached;
    m_result.kills = std::max(0, m_spawned - aliveLeft);
    m_result.wavesSpawned = m_nextWave;
    m_result.waveCount = static_cast<int>(ActiveConfig().waves.size());
    m_result.guardsStart = m_guardsStart;
    m_result.guardsEnd = m_guardsNow;
    m_result.raidersLeft = aliveLeft;
    m_result.direction = m_start.primaryDirection;
    m_result.breachDirection = m_start.primaryDirection;
    RefreshSnapshot();
}

void RegionalSiege::Finish(bool held, std::int64_t gameMinute,
                           std::string_view reason) {
    if (m_phase == RegionalSiegePhase::Finished || m_resultEmitted) {
        return;
    }
    if (m_callbacks.aliveEnemies) {
        m_alive = std::max(0, m_callbacks.aliveEnemies(m_start.settlementId));
    }
    if (m_callbacks.aliveGuards) {
        m_guardsNow = std::max(0, m_callbacks.aliveGuards(m_start.settlementId));
    }
    const int aliveLeft = m_alive;
    m_phase = RegionalSiegePhase::Finished;
    m_resultEmitted = true;
    m_result.regionId = m_start.regionId;
    m_result.settlementId = m_start.settlementId;
    m_result.reclaim = m_start.reclaim;
    m_result.regionName = m_start.regionName;
    m_result.attackMinute = m_start.attackMinute;
    m_result.resolvedMinute = gameMinute;
    m_result.held = held;
    m_result.breached = m_breached;
    m_result.kills = std::max(0, m_spawned - aliveLeft);
    m_result.wavesSpawned = m_nextWave;
    m_result.waveCount = static_cast<int>(ActiveConfig().waves.size());
    m_result.guardsStart = m_guardsStart;
    m_result.guardsEnd = m_guardsNow;
    m_result.raidersLeft = aliveLeft;
    m_result.direction = m_start.primaryDirection;
    m_result.breachDirection = m_start.primaryDirection;

    BaseSiegeResult rating;
    rating.wavesStarted = m_spawned > 0;
    rating.hadTarget = true;
    rating.breached = m_breached;
    rating.baseLost = !held;
    rating.kills = m_result.kills;
    rating.wavesCleared = held ? m_result.waveCount : 0;
    rating.waveCount = m_result.waveCount;
    rating.direction = m_start.primaryDirection;
    rating.breachDirection = m_start.primaryDirection;
    rating.survivingRatio =
        m_guardsStart > 0
            ? static_cast<float>(m_guardsNow) /
                  static_cast<float>(m_guardsStart)
            : (held ? 1.0f : 0.0f);
    m_result.rating = BaseSiegeRatingFor(rating);

    if (m_start.reclaim) {
        if (m_callbacks.endReclaim) {
            m_callbacks.endReclaim(m_start.settlementId, held, reason);
        }
    } else if (m_callbacks.endDefense) {
        m_callbacks.endDefense(m_start.settlementId, !held, reason);
    }
    if (m_callbacks.onResult) {
        m_callbacks.onResult(m_result);
    }
    RefreshSnapshot();
}

std::string RegionalSiegeReportText(const RegionalSiegeResult& result) {
    const std::string name =
        result.regionName.empty() ? result.regionId : result.regionName;
    std::string text = "守城战报 · " + name;
    text += result.held ? " · 守住" : " · 失守";
    if (result.reclaim) {
        text += " · 反攻";
    }
    text += " · 波次 " + std::to_string(result.wavesSpawned) + "/" +
            std::to_string(result.waveCount);
    text += " · 击杀 " + std::to_string(result.kills);
    if (result.guardsStart > 0) {
        text += " · 守卫 " + std::to_string(result.guardsEnd) + "/" +
                std::to_string(result.guardsStart);
    }
    if (result.breached) {
        text += " · 破口";
    }
    if (result.rating != '-' && result.rating != '\0') {
        text += " · 评级 " + std::string(1, result.rating);
    }
    return text;
}

}  // namespace zl::game
