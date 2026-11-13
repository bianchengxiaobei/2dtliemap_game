#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "game/BaseSiegeDirector.h"
#include "game/StrategicState.h"

namespace zl::game {

// M32-T4：战略层“详细围攻”的单区域实时控制器。
// 战略预警/到期由 StrategicDirector 负责；这里只处理玩家在场后的预热、
// M21 风格波次、M23 守卫/平民、破口和最终战报。同一时间只允许一个实例。
enum class RegionalSiegePhase : std::uint8_t {
    Idle = 0,
    Warmup = 1,
    Waves = 2,
    Finished = 3,
};

struct RegionalSiegeConfig {
    bool enabled = true;
    float warmupSeconds = 10.0f;
    float waveIntervalSeconds = 25.0f;
    std::vector<int> waves{4, 6, 8};
    int maxAlive = 12;
    int maxWaveSize = 12;
    float breachRadius = 6.0f;
    float lossDelaySeconds = 8.0f;
};

struct RegionalSiegeStart {
    std::string regionId;
    std::string settlementId;
    std::string regionName;
    ContestedFaction contestedFaction = ContestedFaction::Infected;
    float centerX = 0.0f;
    float centerY = 0.0f;
    std::int64_t attackMinute = 0;
    int defenseScore = 0;
    int enemyStrength = 0;
    int primaryDirection = -1;
    int secondaryDirection = -1;
    bool reclaim = false;
};

struct RegionalSiegeResult {
    std::string regionId;
    std::string settlementId;
    std::string regionName;
    std::int64_t attackMinute = 0;
    std::int64_t resolvedMinute = 0;
    bool reclaim = false;
    bool held = false;
    bool breached = false;
    char rating = '-';
    int kills = 0;
    int wavesSpawned = 0;
    int waveCount = 0;
    int guardsStart = 0;
    int guardsEnd = 0;
    int raidersLeft = 0;
    int direction = -1;
    int breachDirection = -1;
};

std::string RegionalSiegeReportText(const RegionalSiegeResult& result);

struct RegionalSiegeSnapshot {
    bool active = false;
    RegionalSiegePhase phase = RegionalSiegePhase::Idle;
    bool reclaim = false;
    std::string regionId;
    std::string settlementId;
    std::string regionName;
    int wave = 0;
    int waveCount = 0;
    int aliveEnemies = 0;
    int aliveGuards = 0;
    int guardsStart = 0;
    bool breached = false;
    std::string statusText;
};

struct RegionalSiegeCallbacks {
    std::function<bool(std::string_view)> beginDefense;
    std::function<bool(std::string_view)> beginReclaim;
    std::function<int(std::string_view, int, int, ContestedFaction)> spawnWave;
    std::function<int(std::string_view)> aliveEnemies;
    std::function<int(std::string_view)> aliveGuards;
    std::function<int(std::string_view, float, float, float)> raidersWithin;
    std::function<void(std::string_view, bool, std::string_view)> endDefense;
    std::function<void(std::string_view, bool, std::string_view)> endReclaim;
    std::function<void(const RegionalSiegeResult&)> onResult;
};

class RegionalSiege {
public:
    void Configure(RegionalSiegeConfig config, RegionalSiegeCallbacks callbacks);
    void ConfigureReclaim(RegionalSiegeConfig config);
    void Reset();

    bool Start(const RegionalSiegeStart& start, std::int64_t gameMinute);
    void Update(float dt, std::int64_t gameMinute, bool playerInside);
    void Abort(std::int64_t gameMinute, std::string_view reason);

    bool Active() const {
        return m_phase == RegionalSiegePhase::Warmup ||
               m_phase == RegionalSiegePhase::Waves;
    }
    bool Finished() const { return m_phase == RegionalSiegePhase::Finished; }
    const std::string& RegionId() const { return m_start.regionId; }
    const RegionalSiegeSnapshot& Snapshot() const { return m_snapshot; }
    const RegionalSiegeResult& Result() const { return m_result; }
    const RegionalSiegeConfig& Config() const { return m_config; }

private:
    static void SanitizeConfig(RegionalSiegeConfig& config);
    void StartNextWave();
    void ApplyPendingSpawn();
    void RefreshSnapshot();
    void Finish(bool held, std::int64_t gameMinute, std::string_view reason);
    void ResetRuntime();
    const RegionalSiegeConfig& ActiveConfig() const;

    RegionalSiegeConfig m_config;
    RegionalSiegeConfig m_reclaimConfig;
    RegionalSiegeCallbacks m_callbacks;
    RegionalSiegeStart m_start;
    RegionalSiegeSnapshot m_snapshot;
    RegionalSiegeResult m_result;

    RegionalSiegePhase m_phase = RegionalSiegePhase::Idle;
    int m_nextWave = 0;
    int m_pendingWave = 0;
    int m_activeWave = 0;
    int m_spawned = 0;
    int m_guardsStart = 0;
    int m_guardsNow = 0;
    int m_alive = 0;
    float m_phaseTimer = 0.0f;
    float m_lossTimer = 0.0f;
    bool m_breached = false;
    bool m_resultEmitted = false;
};

}  // namespace zl::game
