#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ai/components/AiComponents.h"
#include "game/BaseSiegeDirector.h"

namespace zl::game {

enum class BaseSiegePhase : std::uint8_t {
    Idle = 0,
    Warning = 1,
    Waves = 2,
    Finished = 3,
};

struct BaseSiegeConfig {
    bool enabled = true;
    int minStructures = 1;
    int startHour = 22;
    int endHour = 5;
    float warningSeconds = 30.0f;
    float waveIntervalSeconds = 45.0f;
    std::vector<int> waves{4, 6, 8};
    int maxAlive = 12;  // 硬上限：同时存活的事件僵尸（含特殊）
    int maxWaveSize = 24;  // 每波硬上限，防止天数/基地成长突破性能预算
    float scalePerDay = 0.0f;
    float scalePerStructure = 0.0f;
    int specialStartWave = 1;  // 0-based；默认第二波开始
    int specialCount = 0;      // 每波特殊数量，0 保持旧行为
    float triggerRadius = 12.0f;
    float spawnRadiusMin = 10.0f;
    float spawnRadiusMax = 14.0f;
    float leaveRadius = 25.0f;
    bool directorEnabled = true;
    int directorMaxAxes = 2;
    float intelFullDelayRatio = 0.5f;
    float weakestWeight = 0.7f;
    bool rewardEnabled = true;
};

struct BaseSiegeTime {
    int day = 1;
    int hour = 0;
    int minute = 0;
    bool paused = false;
};

struct BaseSiegePoint {
    float x = 0.0f;
    float y = 0.0f;
};

using BaseSiegeNow = std::function<BaseSiegeTime()>;
using BaseSiegePlayerPos = std::function<bool(BaseSiegePoint*)>;
using BaseSiegeCount = std::function<int(const BaseSiegePoint&, float)>;
using BaseSiegeCellQuery = std::function<bool(int, int)>;
using BaseSiegeSpawn =
    std::function<std::uint64_t(int, int, ecs::ZombieSpecialKind)>;
using BaseSiegeAliveQuery = std::function<bool(std::uint64_t)>;
using BaseSiegeDespawn = std::function<void(std::uint64_t)>;
using BaseSiegeNotice = std::function<void(const std::string&, const std::string&)>;
using BaseSiegeStructureQuery =
    std::function<std::vector<BaseSiegeStructure>(const BaseSiegePoint&, float)>;
using BaseSiegeResultCallback =
    std::function<std::string(const BaseSiegeResult&)>;

struct BaseSiegeCallbacks {
    BaseSiegeNow now;
    BaseSiegePlayerPos playerPos;
    BaseSiegeCount structureCount;
    BaseSiegeCount barricadeCount;
    BaseSiegeStructureQuery structureList;
    BaseSiegeCellQuery isWalkableCell;
    BaseSiegeSpawn spawnZombie;
    BaseSiegeAliveQuery isAlive;
    BaseSiegeDespawn despawnZombie;
    BaseSiegeNotice notice;
    BaseSiegeResultCallback onResult;
};

class BaseSiege {
public:
    void Configure(BaseSiegeConfig config, BaseSiegeCallbacks callbacks) {
        m_config = std::move(config);
        SanitizeConfig(m_config);
        m_callbacks = std::move(callbacks);
        Reset();
    }

    void Reset() {
        ClearEventZombies();
        m_phase = BaseSiegePhase::Idle;
        m_lastNightKey = std::numeric_limits<int>::min();
        m_center = {};
        m_initialStructures = 0;
        m_initialBarricades = 0;
        m_currentStructures = 0;
        m_currentBarricades = 0;
        m_warningLeft = 0.0f;
        m_waveIndex = 0;
        m_pendingSpawn = 0;
        m_waveElapsed = 0.0f;
        m_aliveCount = 0;
        m_statusTimer = 0.0f;
        m_baseLost = false;
        m_waves = m_config.waves;
        m_pendingSpecials.clear();
        m_specials.clear();
        m_currentDay = 1;
        m_warningElapsed = 0.0f;
        m_warningTotal = 0.0f;
        m_wavesStarted = false;
        m_hadTarget = false;
        m_wavesCleared = 0;
        m_kills = 0;
        m_breached = false;
        m_directionSwitched = false;
        m_lastAttackDirection = -1;
        m_resultAwarded = false;
        m_plan = {};
        m_result = {};
        m_breachPoint = {};
        m_breachDirection = -1;
        m_hudText.clear();
        m_hudTone = "info";
    }

    void Update(float dt) {
        if (!m_config.enabled || !m_callbacks.now) {
            return;
        }
        const BaseSiegeTime now = m_callbacks.now();
        if (now.paused) {
            return;
        }
        dt = std::max(0.0f, dt);

        if (m_phase == BaseSiegePhase::Finished) {
            if (InWindow(now) && NightKey(now) != m_lastNightKey) {
                m_phase = BaseSiegePhase::Idle;
            } else {
                return;
            }
        }

        if (m_phase == BaseSiegePhase::Idle) {
            if (!TryTrigger(now)) {
                return;
            }
            if (m_phase == BaseSiegePhase::Warning) {
                UpdateWarning(0.0f, now);
            } else if (m_phase == BaseSiegePhase::Waves) {
                UpdateWaves(0.0f, now);
            }
            return;
        }

        if (m_phase == BaseSiegePhase::Warning) {
            UpdateWarning(dt, now);
        } else if (m_phase == BaseSiegePhase::Waves) {
            UpdateWaves(dt, now);
        }
    }

    void Abort(const std::string& reason, bool notify) {
        if (!Active()) {
            return;
        }
        FinishInternal(reason, "warn", notify);
    }

    BaseSiegePhase Phase() const { return m_phase; }
    bool Active() const {
        return m_phase == BaseSiegePhase::Warning ||
               m_phase == BaseSiegePhase::Waves;
    }
    int LastNightKey() const { return m_lastNightKey; }
    int WaveIndex() const { return m_waveIndex; }
    int WaveCount() const {
        return static_cast<int>(m_waves.empty() ? m_config.waves.size()
                                                : m_waves.size());
    }
    int WaveSize(int index) const {
        return index >= 0 && index < static_cast<int>(m_waves.size())
                   ? m_waves[static_cast<std::size_t>(index)]
                   : 0;
    }
    int AliveCount() const { return m_aliveCount; }
    int PendingSpawn() const { return m_pendingSpawn; }
    int PendingSpecialCount() const {
        return static_cast<int>(m_pendingSpecials.size());
    }
    int SpecialAliveCount() const {
        return static_cast<int>(m_specials.size());
    }
    int SpecialAlive(ecs::ZombieSpecialKind kind) const {
        int count = 0;
        for (const auto& [id, value] : m_specials) {
            (void)id;
            if (value == kind) {
                ++count;
            }
        }
        return count;
    }
    int InitialStructures() const { return m_initialStructures; }
    int CurrentStructures() const { return m_currentStructures; }
    int InitialBarricades() const { return m_initialBarricades; }
    int CurrentBarricades() const { return m_currentBarricades; }
    float WarningRemaining() const { return m_warningLeft; }
    const BaseSiegePoint& Center() const { return m_center; }
    const std::vector<std::uint64_t>& EventZombies() const {
        return m_eventZombies;
    }
    const std::string& HudText() const { return m_hudText; }
    const std::string& HudTone() const { return m_hudTone; }
    bool DirectorActive() const {
        return m_config.directorEnabled && m_callbacks.structureList;
    }
    bool Breached() const { return m_breached; }
    const BaseSiegePoint& BreachPoint() const { return m_breachPoint; }
    int BreachDirection() const { return m_breachDirection; }
    const BaseSiegeAttackPlan& AttackPlan() const { return m_plan; }
    const BaseSiegeResult& Result() const { return m_result; }

private:
    static std::string FormatClock(float seconds) {
        const int total = std::max(0, static_cast<int>(std::ceil(seconds)));
        char text[16];
        std::snprintf(text, sizeof(text), "%02d:%02d", total / 60, total % 60);
        return text;
    }

    static bool InWindow(const BaseSiegeTime& time, int startHour, int endHour) {
        if (startHour == endHour) {
            return false;
        }
        if (startHour < endHour) {
            return time.hour >= startHour && time.hour < endHour;
        }
        return time.hour >= startHour || time.hour < endHour;
    }

    static int NightKey(const BaseSiegeTime& time, int startHour, int endHour) {
        if (startHour < endHour) {
            return time.day;
        }
        return time.hour < endHour ? time.day - 1 : time.day;
    }

    static void SanitizeConfig(BaseSiegeConfig& config) {
        config.minStructures = std::max(0, config.minStructures);
        config.startHour = std::clamp(config.startHour, 0, 23);
        config.endHour = std::clamp(config.endHour, 0, 23);
        config.warningSeconds =
            std::clamp(config.warningSeconds, 0.0f, 3600.0f);
        config.waveIntervalSeconds =
            std::clamp(config.waveIntervalSeconds, 0.0f, 3600.0f);
        config.maxAlive = std::clamp(config.maxAlive, 1, 32);
        config.maxWaveSize = std::clamp(config.maxWaveSize, 1, 32);
        config.scalePerDay = std::clamp(config.scalePerDay, 0.0f, 2.0f);
        config.scalePerStructure =
            std::clamp(config.scalePerStructure, 0.0f, 1.0f);
        config.specialStartWave = std::max(0, config.specialStartWave);
        config.specialCount = std::clamp(config.specialCount, 0, 4);
        config.triggerRadius = std::clamp(config.triggerRadius, 0.5f, 100.0f);
        config.spawnRadiusMin =
            std::clamp(config.spawnRadiusMin, 1.0f, 100.0f);
        config.spawnRadiusMax =
            std::max(config.spawnRadiusMin, config.spawnRadiusMax);
        config.spawnRadiusMax = std::clamp(config.spawnRadiusMax, 1.0f, 100.0f);
        config.leaveRadius = std::clamp(config.leaveRadius, 1.0f, 200.0f);
        config.directorMaxAxes = std::clamp(config.directorMaxAxes, 1, 2);
        config.intelFullDelayRatio =
            std::clamp(config.intelFullDelayRatio, 0.0f, 1.0f);
        config.weakestWeight = std::clamp(config.weakestWeight, 0.0f, 1.0f);
        if (config.waves.empty()) {
            config.waves = {4, 6, 8};
        }
        for (int& wave : config.waves) {
            wave = std::clamp(wave, 0, config.maxWaveSize);
        }
    }

    bool InWindow(const BaseSiegeTime& time) const {
        return InWindow(time, m_config.startHour, m_config.endHour);
    }

    int NightKey(const BaseSiegeTime& time) const {
        return NightKey(time, m_config.startHour, m_config.endHour);
    }

    bool TryTrigger(const BaseSiegeTime& now) {
        if (!InWindow(now)) {
            return false;
        }
        const int key = NightKey(now);
        if (key == m_lastNightKey) {
            return false;
        }
        BaseSiegePoint player;
        if (!m_callbacks.playerPos || !m_callbacks.playerPos(&player)) {
            return false;
        }
        m_lastNightKey = key;
        const int structures =
            m_callbacks.structureCount
                ? m_callbacks.structureCount(player, m_config.triggerRadius)
                : 0;
        if (structures < m_config.minStructures) {
            return false;
        }
        StartWarning(player, structures, now.day);
        return true;
    }

    void StartWarning(const BaseSiegePoint& center, int structures, int day) {
        m_phase = BaseSiegePhase::Warning;
        m_center = center;
        m_initialStructures = structures;
        m_currentStructures = structures;
        m_currentDay = std::max(1, day);
        BuildWaves(m_currentDay, structures);
        m_initialBarricades =
            m_callbacks.barricadeCount
                ? m_callbacks.barricadeCount(center, m_config.triggerRadius)
                : 0;
        m_currentBarricades = m_initialBarricades;
        m_warningLeft = m_config.warningSeconds;
        m_warningTotal = m_config.warningSeconds;
        m_warningElapsed = 0.0f;
        m_waveIndex = 0;
        m_pendingSpawn = 0;
        m_waveElapsed = 0.0f;
        m_aliveCount = 0;
        m_statusTimer = 0.0f;
        m_baseLost = false;
        m_wavesStarted = false;
        m_hadTarget = false;
        m_wavesCleared = 0;
        m_kills = 0;
        m_breached = false;
        m_directionSwitched = false;
        m_resultAwarded = false;
        m_result = {};
        m_plan = {};
        m_breachPoint = {};
        m_breachDirection = -1;
        m_rng = SeedFor(m_lastNightKey, center);
        BuildAttackPlan();
        m_hadTarget = m_plan.hasTarget;
        RefreshHud();
        if (m_callbacks.notice) {
            std::string text =
                "守夜将至：" + FormatClock(m_warningLeft) + " 后尸潮来袭";
            if (DirectorActive() && m_plan.primaryDirection >= 0) {
                text += "，来袭方向：";
                text += BaseSiegeDirectionName(m_plan.primaryDirection);
            }
            m_callbacks.notice(text, "warn");
        }
        if (m_warningLeft <= 0.0f) {
            StartWaves();
        }
    }

    void BuildAttackPlan() {
        if (!DirectorActive()) {
            return;
        }
        std::vector<BaseSiegeStructure> structures =
            m_callbacks.structureList(m_center,
                                      m_config.triggerRadius + 1.0f);
        BaseSiegePlanInput input;
        input.centerX = m_center.x;
        input.centerY = m_center.y;
        input.triggerRadius = m_config.triggerRadius;
        input.maxAxes = m_config.directorMaxAxes;
        input.lastDirection = m_lastAttackDirection;
        input.weakestWeight = m_config.weakestWeight;
        input.fallbackSeed = StructureSeed(structures, m_rng);
        m_plan = MakeBaseSiegeAttackPlan(structures, input);
        if (m_plan.primaryDirection >= 0) {
            m_lastAttackDirection = m_plan.primaryDirection;
        }
    }

    void BuildWaves(int day, int structures) {
        m_waves.clear();
        const float dayScale =
            1.0f + static_cast<float>(std::max(0, day - 1)) *
                       m_config.scalePerDay;
        const float structureScale =
            1.0f + static_cast<float>(std::max(0, structures - 1)) *
                       m_config.scalePerStructure;
        const float scale = std::max(0.0f, dayScale * structureScale);
        for (const int base : m_config.waves) {
            const int scaled =
                static_cast<int>(std::lround(static_cast<float>(base) * scale));
            m_waves.push_back(
                std::clamp(scaled, 0, m_config.maxWaveSize));
        }
    }

    ecs::ZombieSpecialKind SpecialKindForWave(int waveIndex,
                                              int ordinal) const {
        return ((waveIndex + ordinal) % 2 == 1)
                   ? ecs::ZombieSpecialKind::Howler
                   : ecs::ZombieSpecialKind::Breaker;
    }

    void BuildPendingSpecials(int waveIndex) {
        m_pendingSpecials.clear();
        if (waveIndex < 0 || waveIndex >= static_cast<int>(m_waves.size()) ||
            waveIndex < m_config.specialStartWave ||
            m_config.specialCount <= 0) {
            return;
        }
        const int count =
            std::min(m_config.specialCount, m_waves[static_cast<std::size_t>(
                                                 waveIndex)]);
        for (int i = 0; i < count; ++i) {
            m_pendingSpecials.push_back(SpecialKindForWave(waveIndex, i));
        }
    }

    void StartWaves() {
        m_phase = BaseSiegePhase::Waves;
        m_waveIndex = 0;
        m_pendingSpawn = WaveCount() > 0 ? WaveSize(0) : 0;
        BuildPendingSpecials(0);
        m_waveElapsed = 0.0f;
        m_statusTimer = 0.0f;
        m_wavesStarted = true;
        if (DirectorActive()) {
            m_plan.currentDirection = m_plan.primaryDirection;
            RefreshBreachTarget();
        }
        RefreshHud();
        if (m_callbacks.notice && WaveCount() > 0) {
            m_callbacks.notice(
                "尸潮来袭：第 1/" + std::to_string(WaveCount()) + " 波", "fail");
        }
        SpawnPending();
        RefreshHud();
    }

    void RefreshBreachTarget() {
        if (!DirectorActive() || m_phase != BaseSiegePhase::Waves) {
            return;
        }
        std::vector<BaseSiegeStructure> structures =
            m_callbacks.structureList(m_center,
                                      m_config.triggerRadius + 1.0f);
        const bool hadTarget = m_plan.hasTarget;
        if (hadTarget) {
            for (const BaseSiegeStructure& structure : structures) {
                if (structure.id != m_plan.targetId ||
                    !BaseSiegeStructureUsable(structure)) {
                    continue;
                }
                m_plan.targetX = structure.x;
                m_plan.targetY = structure.y;
                m_plan.targetHp = structure.hp;
                m_plan.targetMaxHp = structure.maxHp;
                m_plan.targetName = structure.name;
                return;
            }
        }

        if (!hadTarget) {
            m_plan.hasTarget = false;
            m_plan.targetId = 0;
            m_plan.targetHp = 0.0f;
            m_plan.targetMaxHp = 0.0f;
            m_plan.targetName.clear();
            return;
        }

        if (!m_breached) {
            m_breached = true;
            m_breachPoint = {m_plan.targetX, m_plan.targetY};
            m_breachDirection = m_plan.currentDirection;
            if (m_callbacks.notice) {
                std::string text = "防线被突破";
                if (m_plan.currentDirection >= 0) {
                    text += "：";
                    text += BaseSiegeDirectionName(m_plan.currentDirection);
                }
                m_callbacks.notice(text, "fail");
            }
        }

        const std::optional<BaseSiegeStructure> replacement =
            FindBaseSiegeTargetInDirection(
                structures, m_center.x, m_center.y, m_plan.currentDirection,
                m_config.triggerRadius, m_config.weakestWeight,
                m_plan.hasTarget ? m_plan.targetId : 0);
        if (replacement.has_value()) {
            SetPlanTarget(*replacement);
            return;
        }

        if (!m_directionSwitched && m_plan.secondaryDirection >= 0) {
            const std::optional<BaseSiegeStructure> alternate =
                FindBaseSiegeTargetInDirection(
                    structures, m_center.x, m_center.y,
                    m_plan.secondaryDirection, m_config.triggerRadius,
                    m_config.weakestWeight);
            if (alternate.has_value()) {
                m_plan.currentDirection = m_plan.secondaryDirection;
                m_directionSwitched = true;
                SetPlanTarget(*alternate);
                if (m_callbacks.notice) {
                    m_callbacks.notice(
                        "尸潮转向：" +
                            std::string(BaseSiegeDirectionName(
                                m_plan.currentDirection)),
                        "fail");
                }
                return;
            }
        }

        m_plan.hasTarget = false;
        m_plan.targetId = 0;
        m_plan.targetHp = 0.0f;
        m_plan.targetMaxHp = 0.0f;
        m_plan.targetName.clear();
    }

    void SetPlanTarget(const BaseSiegeStructure& structure) {
        m_plan.hasTarget = true;
        m_plan.targetId = structure.id;
        m_plan.targetX = structure.x;
        m_plan.targetY = structure.y;
        m_plan.targetHp = structure.hp;
        m_plan.targetMaxHp = structure.maxHp;
        m_plan.targetName = structure.name;
    }

    void UpdateWarning(float dt, const BaseSiegeTime& now) {
        if (!InWindow(now)) {
            FinishInternal({}, "ok", true, true);
            return;
        }
        BaseSiegePoint player;
        if (!m_callbacks.playerPos || !m_callbacks.playerPos(&player)) {
            Abort("守夜中止：玩家已离开", false);
            return;
        }
        const float dx = player.x - m_center.x;
        const float dy = player.y - m_center.y;
        if (dx * dx + dy * dy >
            m_config.leaveRadius * m_config.leaveRadius) {
            Abort("你离开了基地，守夜中止", true);
            return;
        }
        m_statusTimer -= dt;
        if (m_statusTimer <= 0.0f) {
            RefreshCounts();
            m_statusTimer = 0.5f;
        }
        m_warningElapsed =
            std::min(m_warningTotal, m_warningElapsed + std::max(0.0f, dt));
        m_warningLeft = std::max(0.0f, m_warningLeft - dt);
        if (m_warningLeft <= 0.0f) {
            StartWaves();
            return;
        }
        RefreshHud();
    }

    void UpdateWaves(float dt, const BaseSiegeTime& now) {
        if (!InWindow(now)) {
            FinishInternal({}, "ok", true, true);
            return;
        }
        BaseSiegePoint player;
        if (!m_callbacks.playerPos || !m_callbacks.playerPos(&player)) {
            Abort("守夜中止：玩家已离开", false);
            return;
        }
        const float dx = player.x - m_center.x;
        const float dy = player.y - m_center.y;
        if (dx * dx + dy * dy >
            m_config.leaveRadius * m_config.leaveRadius) {
            Abort("你离开了基地，守夜中止", true);
            return;
        }

        m_statusTimer -= dt;
        if (m_statusTimer <= 0.0f) {
            RefreshCounts();
            RefreshBreachTarget();
            m_statusTimer = 0.5f;
        }
        PruneDead();
        SpawnPending();
        if (!m_baseLost && m_initialStructures > 0 &&
            m_currentStructures == 0) {
            m_baseLost = true;
            if (m_callbacks.notice) {
                m_callbacks.notice("基地失守", "fail");
            }
        }

        if (m_pendingSpawn == 0) {
            if (m_aliveCount == 0) {
                m_wavesCleared =
                    std::max(m_wavesCleared, m_waveIndex + 1);
            }
            if (m_waveIndex + 1 < WaveCount()) {
                m_waveElapsed += dt;
                if (m_waveElapsed >= m_config.waveIntervalSeconds) {
                    ++m_waveIndex;
                    m_waveElapsed = 0.0f;
                    m_pendingSpawn = WaveSize(m_waveIndex);
                    BuildPendingSpecials(m_waveIndex);
                    RefreshBreachTarget();
                    if (m_callbacks.notice) {
                        m_callbacks.notice(
                            "尸潮来袭：第 " +
                                std::to_string(m_waveIndex + 1) + "/" +
                                std::to_string(WaveCount()) + " 波",
                            "fail");
                    }
                    SpawnPending();
                }
            } else if (m_aliveCount == 0) {
                m_wavesCleared = WaveCount();
                FinishInternal({}, "ok", true, true);
                return;
            }
        }
        RefreshHud();
    }

    void BuildResult() {
        m_result = {};
        m_result.wavesStarted = m_wavesStarted;
        m_result.hadTarget = m_hadTarget;
        m_result.breached = m_breached;
        m_result.baseLost = m_baseLost;
        m_result.kills = m_kills;
        m_result.wavesCleared = m_wavesCleared;
        m_result.waveCount = WaveCount();
        m_result.direction =
            m_plan.currentDirection >= 0 ? m_plan.currentDirection
                                         : m_plan.primaryDirection;
        m_result.breachDirection = m_breachDirection;
        m_result.initialStructures = m_initialStructures;
        m_result.currentStructures = m_currentStructures;
        m_result.breachX = m_breachPoint.x;
        m_result.breachY = m_breachPoint.y;
        m_result.survivingRatio =
            m_initialStructures > 0
                ? std::clamp(static_cast<float>(m_currentStructures) /
                                 static_cast<float>(m_initialStructures),
                             0.0f, 1.0f)
                : 0.0f;
        if (!DirectorActive()) {
            m_result.rating = '-';
            return;
        }
        m_result.rating = BaseSiegeRatingFor(m_result);
        if (m_config.rewardEnabled && m_wavesStarted && m_hadTarget &&
            !m_resultAwarded && m_callbacks.onResult) {
            m_resultAwarded = true;
            m_result.rewardText = m_callbacks.onResult(m_result);
        }
    }

    std::string EndText() const {
        const int lost =
            std::max(0, m_initialBarricades - m_currentBarricades);
        std::string text =
            "守夜结束：损失 " + std::to_string(lost) + " 座栅栏";
        if (m_result.rating != '-') {
            text += " | 评级 ";
            text += m_result.rating;
            text += " | 波次 " + std::to_string(m_result.wavesCleared) +
                    "/" + std::to_string(m_result.waveCount);
            if (m_result.initialStructures > 0) {
                const int lostStructures = std::max(
                    0, m_result.initialStructures - m_result.currentStructures);
                text += " | 结构损失 " + std::to_string(lostStructures) +
                        " 座（存活 " +
                        std::to_string(static_cast<int>(std::lround(
                            m_result.survivingRatio * 100.0f))) +
                        "%）";
            } else {
                text += " | 无成型防线";
            }
            if (m_result.breached) {
                text += " | 防线已破口";
                if (m_result.breachDirection >= 0) {
                    text += "（";
                    text += BaseSiegeDirectionName(m_result.breachDirection);
                    text += "）";
                }
            } else if (m_result.hadTarget) {
                text += " | 防线完好";
            }
            if (m_result.baseLost) {
                text += " | 基地失守";
            }
        }
        if (m_result.kills > 0) {
            text += " | 击杀 " + std::to_string(m_result.kills);
        }
        if (!m_result.rewardText.empty()) {
            text += " | 奖励 " + m_result.rewardText;
        }
        if (DirectorActive() && m_plan.currentDirection >= 0) {
            text += " | 来袭 ";
            text += BaseSiegeDirectionName(m_plan.currentDirection);
        }
        return text;
    }

    void RefreshCounts() {
        if (m_callbacks.structureCount) {
            m_currentStructures = m_callbacks.structureCount(
                m_center, m_config.triggerRadius);
        }
        if (m_callbacks.barricadeCount) {
            m_currentBarricades = m_callbacks.barricadeCount(
                m_center, m_config.triggerRadius);
        }
    }

    bool ShowPreciseIntel() const {
        if (!m_plan.hasTarget || m_config.warningSeconds <= 0.0f) {
            return m_plan.hasTarget;
        }
        if (m_warningElapsed >=
            m_warningTotal * m_config.intelFullDelayRatio) {
            return true;
        }
        BaseSiegePoint player;
        if (!m_callbacks.playerPos || !m_callbacks.playerPos(&player)) {
            return false;
        }
        const float dx = player.x - m_plan.targetX;
        const float dy = player.y - m_plan.targetY;
        return dx * dx + dy * dy <= 144.0f;
    }

    std::string TargetLabel() const {
        if (!m_plan.hasTarget) {
            return {};
        }
        return std::string(BaseSiegeDirectionSide(m_plan.currentDirection)) +
               m_plan.targetName;
    }

    std::string TargetHealthText() const {
        return std::to_string(
                   static_cast<int>(std::lround(m_plan.targetHp))) +
               "/" +
               std::to_string(
                   static_cast<int>(std::lround(m_plan.targetMaxHp)));
    }

    void RefreshHud() {
        if (m_phase == BaseSiegePhase::Warning) {
            m_hudText = "守夜将至 " + FormatClock(m_warningLeft);
            if (DirectorActive() && m_plan.currentDirection >= 0) {
                m_hudText += " | 来袭 " +
                             std::string(BaseSiegeDirectionName(
                                 m_plan.currentDirection));
                if (m_plan.hasTarget) {
                    m_hudText += ShowPreciseIntel()
                                     ? " | 预计破口 " + TargetLabel() + " " +
                                           TargetHealthText()
                                     : " | 预计破口 未确认";
                } else {
                    m_hudText += " | 无成型防线";
                }
            }
            m_hudText += " | 结构 " + std::to_string(m_currentStructures);
            m_hudTone = "warn";
        } else if (m_phase == BaseSiegePhase::Waves) {
            m_hudText = "守夜 第 " + std::to_string(m_waveIndex + 1) + "/" +
                        std::to_string(WaveCount()) + " 波 | 存活 " +
                        std::to_string(m_aliveCount) + " | 栅栏 " +
                        std::to_string(m_currentBarricades) + "/" +
                        std::to_string(m_initialBarricades);
            if (DirectorActive() && m_plan.currentDirection >= 0) {
                m_hudText += " | 来袭 " +
                             std::string(BaseSiegeDirectionName(
                                 m_plan.currentDirection));
                if (m_breached) {
                    m_hudText += " 已破口";
                    if (m_plan.hasTarget) {
                        m_hudText += " 新目标 " + TargetHealthText();
                    }
                } else if (m_plan.hasTarget) {
                    m_hudText += " 破口 " + TargetHealthText();
                } else {
                    m_hudText += " 无成型防线";
                }
            }
            const int howlers =
                SpecialAlive(ecs::ZombieSpecialKind::Howler);
            const int breakers =
                SpecialAlive(ecs::ZombieSpecialKind::Breaker);
            if (howlers > 0 || breakers > 0) {
                m_hudText += " | 特殊 嚎" + std::to_string(howlers) +
                             " 破" + std::to_string(breakers);
            }
            m_hudTone = m_baseLost || m_breached ? "fail" : "warn";
        } else {
            m_hudText.clear();
            m_hudTone = "info";
        }
    }

    void PruneDead() {
        if (!m_callbacks.isAlive) {
            m_aliveCount = static_cast<int>(m_eventZombies.size());
            return;
        }
        m_eventZombies.erase(
            std::remove_if(m_eventZombies.begin(), m_eventZombies.end(),
                           [this](std::uint64_t id) {
                               if (m_callbacks.isAlive(id)) {
                                   return false;
                               }
                               ++m_kills;
                               return true;
                           }),
            m_eventZombies.end());
        for (auto it = m_specials.begin(); it != m_specials.end();) {
            if (!m_callbacks.isAlive(it->first)) {
                it = m_specials.erase(it);
            } else {
                ++it;
            }
        }
        m_aliveCount = static_cast<int>(m_eventZombies.size());
    }

    void SpawnPending() {
        while (m_pendingSpawn > 0 && m_aliveCount < m_config.maxAlive) {
            ecs::ZombieSpecialKind special = ecs::ZombieSpecialKind::None;
            if (!m_pendingSpecials.empty()) {
                special = m_pendingSpecials.back();
                m_pendingSpecials.pop_back();
            }
            const std::uint64_t id = SpawnOne(special);
            if (id == 0) {
                if (special != ecs::ZombieSpecialKind::None) {
                    m_pendingSpecials.push_back(special);
                }
                return;
            }
            m_eventZombies.push_back(id);
            if (special != ecs::ZombieSpecialKind::None) {
                m_specials[id] = special;
            }
            --m_pendingSpawn;
            ++m_aliveCount;
        }
    }

    std::uint64_t SpawnOne(ecs::ZombieSpecialKind special) {
        if (!m_callbacks.isWalkableCell || !m_callbacks.spawnZombie) {
            return 0;
        }
        const bool directed =
            DirectorActive() && m_plan.currentDirection >= 0;
        for (int attempt = 0; attempt < 32; ++attempt) {
            float angle = Rand01() * 6.2831853f;
            if (directed) {
                const float centerAngle =
                    BaseSiegeDirectionAngle(m_plan.currentDirection);
                const float spread = 0.392698f * 0.9f;
                angle = centerAngle +
                        (Rand01() * 2.0f - 1.0f) * spread;
            }
            const float radius =
                m_config.spawnRadiusMin +
                Rand01() * (m_config.spawnRadiusMax - m_config.spawnRadiusMin);
            const float directionY = directed ? -std::sin(angle)
                                              : std::sin(angle);
            const int wx = static_cast<int>(
                std::floor(m_center.x + std::cos(angle) * radius));
            const int wy = static_cast<int>(
                std::floor(m_center.y + directionY * radius));
            if (!m_callbacks.isWalkableCell(wx, wy)) {
                continue;
            }
            const std::uint64_t id =
                m_callbacks.spawnZombie(wx, wy, special);
            if (id != 0) {
                return id;
            }
        }
        if (directed) {
            for (int attempt = 0; attempt < 16; ++attempt) {
                const float angle = Rand01() * 6.2831853f;
                const float radius =
                    m_config.spawnRadiusMin +
                    Rand01() *
                        (m_config.spawnRadiusMax - m_config.spawnRadiusMin);
                const int wx = static_cast<int>(
                    std::floor(m_center.x + std::cos(angle) * radius));
                const int wy = static_cast<int>(
                    std::floor(m_center.y + std::sin(angle) * radius));
                if (!m_callbacks.isWalkableCell(wx, wy)) {
                    continue;
                }
                const std::uint64_t id =
                    m_callbacks.spawnZombie(wx, wy, special);
                if (id != 0) {
                    return id;
                }
            }
        }
        return 0;
    }

    void ClearEventZombies() {
        if (m_callbacks.despawnZombie) {
            for (const std::uint64_t id : m_eventZombies) {
                if (!m_callbacks.isAlive || m_callbacks.isAlive(id)) {
                    m_callbacks.despawnZombie(id);
                }
            }
        }
        m_eventZombies.clear();
        m_specials.clear();
        m_pendingSpecials.clear();
        m_aliveCount = 0;
    }

    void FinishInternal(const std::string& text, const std::string& tone,
                        bool notify, bool normalEnd = false) {
        if (normalEnd) {
            RefreshCounts();
            BuildResult();
        }
        const std::string finalText = normalEnd ? EndText() : text;
        ClearEventZombies();
        m_phase = BaseSiegePhase::Finished;
        m_pendingSpawn = 0;
        m_waveElapsed = 0.0f;
        m_hudText.clear();
        m_hudTone = "info";
        if (notify && m_callbacks.notice && !finalText.empty()) {
            m_callbacks.notice(finalText, tone);
        }
    }

    static std::uint64_t StructureSeed(
        const std::vector<BaseSiegeStructure>& structures,
        std::uint64_t seed) {
        auto mix = [](std::uint64_t& hash, std::uint64_t value) {
            hash ^= value + 0x9E3779B97F4A7C15ull + (hash << 6U) +
                    (hash >> 2U);
        };
        for (const BaseSiegeStructure& structure : structures) {
            mix(seed, structure.id);
            mix(seed, static_cast<std::uint64_t>(
                          std::lround(structure.x * 100.0f)));
            mix(seed, static_cast<std::uint64_t>(
                          std::lround(structure.y * 100.0f)));
            mix(seed, static_cast<std::uint64_t>(
                          std::lround(structure.hp * 100.0f)));
            mix(seed, static_cast<std::uint64_t>(
                          std::lround(structure.maxHp * 100.0f)));
        }
        return seed == 0 ? 1 : seed;
    }

    static std::uint64_t SeedFor(int nightKey, const BaseSiegePoint& center) {
        std::uint64_t seed =
            0x9E3779B97F4A7C15ull ^
            (static_cast<std::uint64_t>(
                 static_cast<std::uint32_t>(nightKey)) *
             0xBF58476D1CE4E5B9ull);
        seed ^= static_cast<std::uint64_t>(
                    static_cast<std::uint32_t>(
                        static_cast<std::int32_t>(std::floor(center.x)))) *
                0x94D049BB133111EBull;
        seed ^= static_cast<std::uint64_t>(
                    static_cast<std::uint32_t>(
                        static_cast<std::int32_t>(std::floor(center.y)))) *
                0xD6E8FEB86659FD93ull;
        return seed == 0 ? 1 : seed;
    }

    float Rand01() {
        m_rng ^= m_rng << 13U;
        m_rng ^= m_rng >> 7U;
        m_rng ^= m_rng << 17U;
        return static_cast<float>((m_rng >> 40U) & 0xFFFFFFU) /
               static_cast<float>(0x1000000U);
    }

    BaseSiegeConfig m_config;
    BaseSiegeCallbacks m_callbacks;
    BaseSiegePhase m_phase = BaseSiegePhase::Idle;
    int m_lastNightKey = std::numeric_limits<int>::min();
    BaseSiegePoint m_center;
    int m_initialStructures = 0;
    int m_initialBarricades = 0;
    int m_currentStructures = 0;
    int m_currentBarricades = 0;
    float m_warningLeft = 0.0f;
    int m_waveIndex = 0;
    int m_pendingSpawn = 0;
    int m_currentDay = 1;
    float m_waveElapsed = 0.0f;
    int m_aliveCount = 0;
    float m_statusTimer = 0.0f;
    float m_warningElapsed = 0.0f;
    float m_warningTotal = 0.0f;
    bool m_baseLost = false;
    bool m_wavesStarted = false;
    bool m_hadTarget = false;
    int m_wavesCleared = 0;
    int m_kills = 0;
    bool m_breached = false;
    bool m_directionSwitched = false;
    int m_lastAttackDirection = -1;
    bool m_resultAwarded = false;
    BaseSiegeAttackPlan m_plan;
    BaseSiegeResult m_result;
    BaseSiegePoint m_breachPoint;
    int m_breachDirection = -1;
    std::vector<std::uint64_t> m_eventZombies;
    std::vector<int> m_waves;
    std::vector<ecs::ZombieSpecialKind> m_pendingSpecials;
    std::unordered_map<std::uint64_t, ecs::ZombieSpecialKind> m_specials;
    std::uint64_t m_rng = 1;
    std::string m_hudText;
    std::string m_hudTone = "info";
};

}  // namespace zl::game
