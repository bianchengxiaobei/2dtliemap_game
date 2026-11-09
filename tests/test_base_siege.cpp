#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "game/BaseSiege.h"

namespace {

struct SiegeHarness {
    zl::game::BaseSiege siege;
    zl::game::BaseSiegeTime time;
    zl::game::BaseSiegePoint player{50.0f, 50.0f};
    int structures = 1;
    int barricades = 1;
    int nextStableId = 1;
    int spawned = 0;
    int spawnedSpecials = 0;
    int despawned = 0;
    bool walkable = true;
    bool overCapacity = false;
    bool director = false;
    int rewards = 0;
    std::unordered_set<std::uint64_t> alive;
    std::vector<std::string> notices;
    std::vector<std::pair<int, int>> spawnCells;
    std::vector<zl::game::BaseSiegeStructure> eventStructures;
    zl::game::BaseSiegeResult lastResult;
    std::string lastTone;

    explicit SiegeHarness(zl::game::BaseSiegeConfig config,
                          bool enableDirector = false)
        : director(enableDirector) {
        zl::game::BaseSiegeCallbacks callbacks;
        callbacks.now = [this] { return time; };
        callbacks.playerPos = [this](zl::game::BaseSiegePoint* out) {
            if (out == nullptr) {
                return false;
            }
            *out = player;
            return true;
        };
        callbacks.structureCount = [this](const zl::game::BaseSiegePoint&,
                                          float) { return structures; };
        callbacks.barricadeCount = [this](const zl::game::BaseSiegePoint&,
                                           float) { return barricades; };
        if (director) {
            callbacks.structureList =
                [this](const zl::game::BaseSiegePoint&, float) {
                    return eventStructures;
                };
        }
        callbacks.isWalkableCell = [this](int, int) { return walkable; };
        callbacks.spawnZombie =
            [this, maxAlive = config.maxAlive](
                int wx, int wy, zl::ecs::ZombieSpecialKind special) {
                if (static_cast<int>(alive.size()) >= maxAlive) {
                    overCapacity = true;
                }
                const std::uint64_t id = nextStableId++;
                alive.insert(id);
                spawned += 1;
                spawnCells.emplace_back(wx, wy);
                if (special != zl::ecs::ZombieSpecialKind::None) {
                    ++spawnedSpecials;
                }
                return id;
            };
        callbacks.isAlive = [this](std::uint64_t id) {
            return alive.contains(id);
        };
        callbacks.despawnZombie = [this](std::uint64_t id) {
            alive.erase(id);
            ++despawned;
        };
        callbacks.notice = [this](const std::string& text,
                                  const std::string& tone) {
            notices.push_back(text);
            lastTone = tone;
        };
        if (director) {
            callbacks.onResult =
                [this](const zl::game::BaseSiegeResult& result) {
                    ++rewards;
                    lastResult = result;
                    return std::string("测试奖励");
                };
        }
        siege.Configure(std::move(config), std::move(callbacks));
        time.day = 1;
        time.hour = 21;
    }

    void Update(float dt) { siege.Update(dt); }

    void KillAll() { alive.clear(); }
};

zl::game::BaseSiegeConfig FastConfig() {
    zl::game::BaseSiegeConfig config;
    config.warningSeconds = 2.0f;
    config.waveIntervalSeconds = 1.0f;
    config.waves = {2, 1, 1};
    config.maxAlive = 4;
    return config;
}

}  // namespace

TEST_CASE("M17-T3 base siege warning, countdown, pause and one event per night") {
    SiegeHarness harness(FastConfig());

    harness.Update(10.0f);
    CHECK(harness.siege.Phase() == zl::game::BaseSiegePhase::Idle);

    harness.time.hour = 22;
    harness.Update(0.1f);
    REQUIRE(harness.siege.Phase() == zl::game::BaseSiegePhase::Warning);
    CHECK(harness.siege.WarningRemaining() == doctest::Approx(2.0f));
    CHECK(harness.siege.HudText().find("00:02") != std::string::npos);
    CHECK(harness.siege.HudText().find("结构 1") != std::string::npos);
    CHECK(harness.siege.InitialStructures() == 1);
    CHECK(harness.siege.LastNightKey() == 1);
    REQUIRE_FALSE(harness.notices.empty());
    CHECK(harness.notices.back().find("守夜将至") != std::string::npos);

    harness.time.paused = true;
    harness.Update(10.0f);
    CHECK(harness.siege.WarningRemaining() == doctest::Approx(2.0f));
    harness.time.paused = false;

    harness.Update(1.0f);
    CHECK(harness.siege.WarningRemaining() == doctest::Approx(1.0f));
    CHECK(harness.siege.HudText().find("00:01") != std::string::npos);
    harness.Update(1.2f);
    REQUIRE(harness.siege.Phase() == zl::game::BaseSiegePhase::Waves);
    CHECK(harness.spawned == 2);
    CHECK(harness.siege.AliveCount() == 2);
    CHECK(harness.siege.HudText().find("第 1/3 波") != std::string::npos);

    harness.time.hour = 23;
    harness.Update(0.5f);
    CHECK(harness.siege.Phase() == zl::game::BaseSiegePhase::Waves);
    CHECK(harness.spawned == 2);
}

TEST_CASE("M17-T3 no base skips the night and does not re-trigger") {
    SiegeHarness harness(FastConfig());
    harness.structures = 0;
    harness.barricades = 0;
    harness.time.hour = 22;

    harness.Update(0.1f);
    CHECK(harness.siege.Phase() == zl::game::BaseSiegePhase::Idle);
    CHECK(harness.siege.LastNightKey() == 1);

    harness.structures = 1;
    harness.barricades = 1;
    harness.time.hour = 23;
    harness.Update(0.1f);
    CHECK(harness.siege.Phase() == zl::game::BaseSiegePhase::Idle);

    harness.time.day = 2;
    harness.time.hour = 0;
    harness.Update(0.1f);
    CHECK(harness.siege.Phase() == zl::game::BaseSiegePhase::Idle);

    harness.time.hour = 22;
    harness.Update(0.1f);
    REQUIRE(harness.siege.Phase() == zl::game::BaseSiegePhase::Warning);
    CHECK(harness.siege.LastNightKey() == 2);
}

TEST_CASE("M17-T3 waves respect max alive and finish after all event zombies die") {
    zl::game::BaseSiegeConfig config;
    config.warningSeconds = 0.0f;
    config.waveIntervalSeconds = 1.0f;
    config.waves = {4, 6, 8};
    config.maxAlive = 12;
    SiegeHarness harness(config);
    harness.time.hour = 22;

    harness.Update(0.0f);
    REQUIRE(harness.siege.Phase() == zl::game::BaseSiegePhase::Waves);
    CHECK(harness.spawned == 4);
    CHECK(harness.siege.AliveCount() == 4);
    CHECK(harness.siege.PendingSpawn() == 0);

    harness.Update(1.0f);
    CHECK(harness.spawned == 10);
    CHECK(harness.siege.WaveIndex() == 1);

    harness.Update(1.0f);
    CHECK(harness.spawned == 12);
    CHECK(harness.siege.AliveCount() == 12);
    CHECK(harness.siege.PendingSpawn() == 6);
    CHECK_FALSE(harness.overCapacity);

    auto killOne = [&] {
        auto it = harness.alive.begin();
        if (it != harness.alive.end()) {
            harness.alive.erase(it);
        }
    };
    killOne();
    killOne();
    harness.Update(0.01f);
    CHECK(harness.spawned == 14);
    CHECK(harness.siege.AliveCount() == 12);
    CHECK(harness.siege.PendingSpawn() == 4);

    harness.KillAll();
    harness.Update(0.01f);
    CHECK(harness.spawned == 18);
    CHECK(harness.siege.AliveCount() == 4);
    CHECK(harness.siege.PendingSpawn() == 0);

    harness.KillAll();
    harness.Update(0.01f);
    CHECK(harness.siege.Phase() == zl::game::BaseSiegePhase::Finished);
    CHECK(harness.siege.AliveCount() == 0);
    CHECK(harness.siege.HudText().empty());
    CHECK_FALSE(harness.notices.empty());
    CHECK(harness.notices.back().find("守夜结束") != std::string::npos);
    CHECK_FALSE(harness.overCapacity);
}

TEST_CASE("M17-T3 05:00 finishes and clears survivors") {
    zl::game::BaseSiegeConfig config;
    config.warningSeconds = 0.0f;
    config.waves = {3};
    config.maxAlive = 4;
    SiegeHarness harness(config);
    harness.time.day = 2;
    harness.time.hour = 22;

    harness.Update(0.0f);
    REQUIRE(harness.siege.Phase() == zl::game::BaseSiegePhase::Waves);
    REQUIRE(harness.spawned == 3);

    harness.time.hour = 5;
    harness.Update(0.1f);
    CHECK(harness.siege.Phase() == zl::game::BaseSiegePhase::Finished);
    CHECK(harness.siege.AliveCount() == 0);
    CHECK(harness.alive.empty());
    CHECK(harness.despawned == 3);
    CHECK(harness.notices.back().find("守夜结束") != std::string::npos);
}

TEST_CASE("M17-T3 warning crossing 05:00 ends without spawning") {
    zl::game::BaseSiegeConfig config;
    config.warningSeconds = 30.0f;
    config.waves = {2};
    SiegeHarness harness(config);
    harness.time.day = 1;
    harness.time.hour = 22;
    harness.Update(0.0f);
    REQUIRE(harness.siege.Phase() == zl::game::BaseSiegePhase::Warning);

    harness.time.day = 2;
    harness.time.hour = 5;
    harness.Update(0.1f);
    CHECK(harness.siege.Phase() == zl::game::BaseSiegePhase::Finished);
    CHECK(harness.spawned == 0);
    CHECK(harness.siege.HudText().empty());
    CHECK(harness.notices.back().find("守夜结束") != std::string::npos);
}

TEST_CASE("M17-T3 leaving the base aborts and clears event zombies") {
    zl::game::BaseSiegeConfig config;
    config.warningSeconds = 0.0f;
    config.waves = {3};
    config.maxAlive = 4;
    config.leaveRadius = 25.0f;
    SiegeHarness harness(config);
    harness.time.hour = 22;
    harness.Update(0.0f);
    REQUIRE(harness.siege.Phase() == zl::game::BaseSiegePhase::Waves);

    harness.player.x = 76.0f;
    harness.Update(0.1f);
    CHECK(harness.siege.Phase() == zl::game::BaseSiegePhase::Finished);
    CHECK(harness.siege.AliveCount() == 0);
    CHECK(harness.alive.empty());
    CHECK(harness.despawned == 3);
    CHECK(harness.notices.back().find("离开") != std::string::npos);
}

TEST_CASE("M17-T3 base lost, loss count and direct abort") {
    zl::game::BaseSiegeConfig config;
    config.warningSeconds = 0.0f;
    config.waves = {2};
    config.maxAlive = 2;
    SiegeHarness harness(config);
    harness.structures = 1;
    harness.barricades = 1;
    harness.time.hour = 22;
    harness.Update(0.0f);
    REQUIRE(harness.siege.Phase() == zl::game::BaseSiegePhase::Waves);
    CHECK(harness.siege.InitialBarricades() == 1);

    harness.structures = 0;
    harness.barricades = 0;
    harness.Update(0.6f);
    CHECK(harness.siege.CurrentStructures() == 0);
    CHECK(harness.siege.HudTone() == "fail");
    bool lostNotice = false;
    for (const std::string& notice : harness.notices) {
        if (notice == "基地失守") {
            lostNotice = true;
        }
    }
    CHECK(lostNotice);

    harness.KillAll();
    harness.Update(0.01f);
    REQUIRE(harness.siege.Phase() == zl::game::BaseSiegePhase::Finished);
    CHECK(harness.notices.back().find("损失 1 座栅栏") != std::string::npos);

    SiegeHarness abortHarness(config);
    abortHarness.time.hour = 22;
    abortHarness.Update(0.0f);
    REQUIRE(abortHarness.siege.Active());
    abortHarness.siege.Abort("保存中止守夜", false);
    CHECK(abortHarness.siege.Phase() == zl::game::BaseSiegePhase::Finished);
    CHECK(abortHarness.alive.empty());
    CHECK(abortHarness.despawned == 2);
    CHECK(abortHarness.notices.back().find("保存中止守夜") == std::string::npos);
}

TEST_CASE("M20-F3 siege scales by day/base with a hard cap and special waves") {
    zl::game::BaseSiegeConfig config;
    config.warningSeconds = 0.0f;
    config.waveIntervalSeconds = 0.1f;
    config.waves = {4, 4, 4};
    config.maxAlive = 16;
    config.maxWaveSize = 8;
    config.scalePerDay = 0.5f;
    config.scalePerStructure = 0.5f;
    config.specialStartWave = 1;
    config.specialCount = 1;
    SiegeHarness harness(config);
    harness.structures = 3;
    harness.time.day = 3;
    harness.time.hour = 22;

    harness.Update(0.0f);
    REQUIRE(harness.siege.Phase() == zl::game::BaseSiegePhase::Waves);
    CHECK(harness.siege.WaveCount() == 3);
    CHECK(harness.siege.WaveSize(0) == 8);  // 4 * 2 * 2 → 硬上限 8
    CHECK(harness.siege.WaveSize(1) == 8);
    CHECK(harness.siege.SpecialAliveCount() == 0);  // 第一波无特殊

    harness.KillAll();
    harness.Update(0.2f);
    REQUIRE(harness.siege.WaveIndex() == 1);
    CHECK(harness.spawnedSpecials == 1);
    CHECK(harness.siege.SpecialAlive(zl::ecs::ZombieSpecialKind::Howler) == 1);
    CHECK(harness.siege.HudText().find("特殊") != std::string::npos);
    CHECK(harness.siege.HudText().find("嚎") != std::string::npos);

    harness.KillAll();
    harness.Update(0.2f);
    REQUIRE(harness.siege.WaveIndex() == 2);
    CHECK(harness.spawnedSpecials == 2);
    CHECK(harness.siege.SpecialAlive(zl::ecs::ZombieSpecialKind::Breaker) == 1);
    CHECK(harness.siege.SpecialAliveCount() == 1);

    harness.KillAll();
    harness.Update(0.2f);
    CHECK(harness.siege.Phase() == zl::game::BaseSiegePhase::Finished);
    CHECK(harness.spawnedSpecials == 2);
}

TEST_CASE("M21-F1a director chooses a weak axis, reports intel and spawns in sector") {
    zl::game::BaseSiegeConfig config;
    config.warningSeconds = 6.0f;
    config.waveIntervalSeconds = 1.0f;
    config.waves = {3};
    config.maxAlive = 4;
    config.directorMaxAxes = 2;
    SiegeHarness harness(config, true);
    harness.structures = 2;
    harness.barricades = 2;
    zl::game::BaseSiegeStructure west;
    west.id = 11;
    west.x = 39.5f;
    west.y = 50.5f;
    west.hp = 20.0f;
    west.maxHp = 120.0f;
    west.door = true;
    west.name = "木门";
    zl::game::BaseSiegeStructure north;
    north.id = 12;
    north.x = 50.5f;
    north.y = 39.5f;
    north.hp = 100.0f;
    north.maxHp = 120.0f;
    north.name = "木墙";
    harness.eventStructures = {west, north};
    harness.time.day = 1;
    harness.time.hour = 22;

    harness.Update(0.1f);
    REQUIRE(harness.siege.Phase() == zl::game::BaseSiegePhase::Warning);
    const auto& plan = harness.siege.AttackPlan();
    CHECK(plan.primaryDirection == 4);
    CHECK(plan.secondaryDirection == 2);
    CHECK(plan.hasTarget);
    CHECK(plan.targetId == 11);
    CHECK(plan.targetHp == doctest::Approx(20.0f));
    CHECK(harness.siege.HudText().find("来袭 西") != std::string::npos);
    CHECK(harness.siege.HudText().find("西侧木门 20/120") !=
          std::string::npos);

    harness.Update(6.0f);
    REQUIRE(harness.siege.Phase() == zl::game::BaseSiegePhase::Waves);
    REQUIRE(harness.spawnCells.size() == 3);
    for (const auto& [wx, wy] : harness.spawnCells) {
        CHECK(wx < 50);
        CHECK(std::abs(wy - 50) <= std::abs(wx - 50));
    }
    CHECK(harness.siege.HudText().find("来袭 西") != std::string::npos);
    CHECK(harness.siege.HudText().find("破口 20/120") != std::string::npos);
}

TEST_CASE("M21-F1a directed spawn uses the chosen world axis") {
    zl::game::BaseSiegeConfig config;
    config.warningSeconds = 0.0f;
    config.waves = {2};
    config.maxAlive = 2;
    SiegeHarness harness(config, true);
    harness.structures = 1;
    harness.barricades = 1;
    zl::game::BaseSiegeStructure north;
    north.id = 31;
    north.x = 50.5f;
    north.y = 39.5f;
    north.hp = 10.0f;
    north.maxHp = 120.0f;
    north.name = "木墙";
    harness.eventStructures = {north};
    harness.time.hour = 22;

    harness.Update(0.0f);
    REQUIRE(harness.siege.Phase() == zl::game::BaseSiegePhase::Waves);
    CHECK(harness.siege.AttackPlan().primaryDirection == 2);
    REQUIRE(harness.spawnCells.size() == 2);
    for (const auto& [wx, wy] : harness.spawnCells) {
        CHECK(wy < 50);
        CHECK(std::abs(wx - 50) <= std::abs(wy - 50));
    }
}

TEST_CASE("M21-F1a breach switches to the secondary axis once and rewards once") {
    zl::game::BaseSiegeConfig config;
    config.warningSeconds = 0.0f;
    config.waves = {2};
    config.maxAlive = 4;
    SiegeHarness harness(config, true);
    harness.structures = 3;
    harness.barricades = 3;
    zl::game::BaseSiegeStructure west;
    west.id = 21;
    west.x = 39.5f;
    west.y = 50.5f;
    west.hp = 10.0f;
    west.maxHp = 120.0f;
    west.name = "木门";
    zl::game::BaseSiegeStructure westFallback;
    westFallback.id = 23;
    westFallback.x = 38.5f;
    westFallback.y = 50.5f;
    westFallback.hp = 50.0f;
    westFallback.maxHp = 120.0f;
    westFallback.name = "木墙";
    zl::game::BaseSiegeStructure north;
    north.id = 22;
    north.x = 50.5f;
    north.y = 39.5f;
    north.hp = 80.0f;
    north.maxHp = 120.0f;
    north.name = "木墙";
    harness.eventStructures = {west, westFallback, north};
    harness.time.hour = 22;

    harness.Update(0.0f);
    REQUIRE(harness.siege.Phase() == zl::game::BaseSiegePhase::Waves);
    REQUIRE(harness.siege.AttackPlan().targetId == 21);

    harness.eventStructures = {westFallback, north};
    harness.structures = 2;
    harness.barricades = 2;
    harness.Update(0.6f);
    REQUIRE(harness.siege.Breached());
    CHECK(harness.siege.AttackPlan().currentDirection == 4);
    CHECK(harness.siege.AttackPlan().targetId == 23);
    CHECK(harness.siege.AttackPlan().targetName == "木墙");
    CHECK(harness.siege.BreachPoint().x == doctest::Approx(39.5f));
    CHECK(harness.siege.BreachPoint().y == doctest::Approx(50.5f));
    CHECK(harness.siege.BreachDirection() == 4);
    CHECK_FALSE(std::any_of(harness.notices.begin(), harness.notices.end(),
                            [](const std::string& notice) {
                                return notice.find("尸潮转向") !=
                                       std::string::npos;
                            }));

    harness.eventStructures = {north};
    harness.structures = 1;
    harness.barricades = 1;
    harness.Update(0.6f);
    CHECK(harness.siege.AttackPlan().currentDirection == 2);
    CHECK(harness.siege.AttackPlan().targetId == 22);
    CHECK(harness.siege.AttackPlan().targetName == "木墙");
    bool breachNotice = false;
    bool turnNotice = false;
    for (const std::string& notice : harness.notices) {
        breachNotice = breachNotice ||
                       notice.find("防线被突破") != std::string::npos;
        turnNotice = turnNotice ||
                     notice.find("尸潮转向：北") != std::string::npos;
    }
    CHECK(breachNotice);
    CHECK(turnNotice);

    harness.KillAll();
    harness.Update(0.1f);
    REQUIRE(harness.siege.Phase() == zl::game::BaseSiegePhase::Finished);
    CHECK(harness.siege.Result().rating == 'B');
    CHECK(harness.siege.Result().breached);
    CHECK(harness.siege.Result().kills == 2);
    CHECK(harness.siege.Result().initialStructures == 3);
    CHECK(harness.siege.Result().currentStructures == 1);
    CHECK(harness.siege.Result().breachX == doctest::Approx(39.5f));
    CHECK(harness.siege.Result().breachY == doctest::Approx(50.5f));
    CHECK(harness.siege.Result().breachDirection == 4);
    CHECK(harness.rewards == 1);
    CHECK(harness.siege.Result().rewardText == "测试奖励");
    CHECK(harness.notices.back().find("奖励 测试奖励") !=
          std::string::npos);
    CHECK(harness.notices.back().find("波次 1/1") != std::string::npos);
    CHECK(harness.notices.back().find("结构损失 2 座（存活 33%）") !=
          std::string::npos);
    CHECK(harness.notices.back().find("防线已破口（西）") !=
          std::string::npos);

    harness.Update(0.1f);
    CHECK(harness.rewards == 1);
}

TEST_CASE("M21-F1a director disabled keeps the legacy siege path") {
    zl::game::BaseSiegeConfig config;
    config.warningSeconds = 0.0f;
    config.waves = {1};
    config.maxAlive = 1;
    config.directorEnabled = false;
    SiegeHarness harness(config, true);
    harness.structures = 1;
    zl::game::BaseSiegeStructure wall;
    wall.id = 41;
    wall.x = 50.5f;
    wall.y = 39.5f;
    wall.hp = 10.0f;
    wall.maxHp = 120.0f;
    wall.name = "木墙";
    harness.eventStructures = {wall};
    harness.time.hour = 22;

    harness.Update(0.0f);
    REQUIRE(harness.siege.Phase() == zl::game::BaseSiegePhase::Waves);
    CHECK(harness.siege.AttackPlan().primaryDirection == -1);
    CHECK_FALSE(harness.siege.AttackPlan().hasTarget);
    CHECK(harness.siege.HudText().find("来袭") == std::string::npos);

    harness.KillAll();
    harness.Update(0.1f);
    REQUIRE(harness.siege.Phase() == zl::game::BaseSiegePhase::Finished);
    CHECK(harness.siege.Result().rating == '-');
    CHECK(harness.rewards == 0);
}

TEST_CASE("M21-F1a undefended night gets a direction but no target or reward") {
    zl::game::BaseSiegeConfig config;
    config.warningSeconds = 0.0f;
    config.waves = {1};
    config.maxAlive = 2;
    SiegeHarness harness(config, true);
    harness.structures = 1;
    harness.time.hour = 22;

    harness.Update(0.0f);
    REQUIRE(harness.siege.Phase() == zl::game::BaseSiegePhase::Waves);
    CHECK(harness.siege.AttackPlan().currentDirection >= 0);
    CHECK_FALSE(harness.siege.AttackPlan().hasTarget);
    CHECK_FALSE(harness.siege.Breached());
    CHECK(harness.siege.HudText().find("无成型防线") != std::string::npos);

    harness.KillAll();
    harness.Update(0.1f);
    REQUIRE(harness.siege.Phase() == zl::game::BaseSiegePhase::Finished);
    CHECK(harness.siege.Result().rating == 'C');
    CHECK_FALSE(harness.siege.Result().hadTarget);
    CHECK(harness.rewards == 0);
}

TEST_CASE("M21-F1b shared siege reward table matches rating and wood item") {
    const auto s = zl::game::BaseSiegeRewardsFor('S', "plank");
    REQUIRE(s.size() == 3);
    CHECK(s[0].itemId == "fuel_can");
    CHECK(s[0].count == 1);
    CHECK(s[1].itemId == "repair_kit");
    CHECK(s[1].count == 1);
    CHECK(s[2].itemId == "antibiotics");
    CHECK(s[2].count == 1);

    const auto a = zl::game::BaseSiegeRewardsFor('A', "plank");
    REQUIRE(a.size() == 2);
    CHECK(a[0].itemId == "fuel_can");
    CHECK(a[1].itemId == "repair_kit");

    const auto b = zl::game::BaseSiegeRewardsFor('B', "plank");
    REQUIRE(b.size() == 2);
    CHECK(b[0].itemId == "plank");
    CHECK(b[0].count == 6);
    CHECK(b[1].itemId == "bandage");
    CHECK(b[1].count == 1);

    const auto c = zl::game::BaseSiegeRewardsFor('C', "plank");
    REQUIRE(c.size() == 1);
    CHECK(c[0].itemId == "plank");
    CHECK(c[0].count == 3);

    CHECK(zl::game::BaseSiegeRewardsFor('-').empty());
    const auto defaultWood = zl::game::BaseSiegeRewardsFor('C');
    REQUIRE(defaultWood.size() == 1);
    CHECK(defaultWood[0].itemId == "wood");
}