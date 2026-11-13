#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <string_view>

#include "game/RegionalSiege.h"

using namespace zl::game;

namespace {

struct FakeSiege {
    int enemies = 0;
    int guards = 2;
    int spawned = 0;
    int beginCalls = 0;
    int beginReclaimCalls = 0;
    int endCalls = 0;
    int endReclaimCalls = 0;
    ContestedFaction lastFaction = ContestedFaction::Infected;
    int resultCalls = 0;
    bool endFallen = false;
    bool breached = false;
    std::string beginId;
    std::string spawnId;
    std::string endId;
    std::string endReason;
    RegionalSiegeResult lastResult;
};

RegionalSiegeCallbacks MakeCallbacks(FakeSiege& fake) {
    RegionalSiegeCallbacks callbacks;
    callbacks.beginDefense = [&fake](std::string_view id) {
        ++fake.beginCalls;
        fake.beginId = std::string(id);
        return true;
    };
    callbacks.beginReclaim = [&fake](std::string_view id) {
        ++fake.beginReclaimCalls;
        fake.beginId = std::string(id);
        return true;
    };
    callbacks.spawnWave = [&fake](std::string_view id, int count, int,
                                  ContestedFaction faction) {
        fake.lastFaction = faction;
        fake.spawnId = std::string(id);
        const int spawned = std::clamp(count, 0, 32);
        fake.enemies += spawned;
        fake.spawned += spawned;
        return spawned;
    };
    callbacks.aliveEnemies = [&fake](std::string_view) {
        return fake.enemies;
    };
    callbacks.aliveGuards = [&fake](std::string_view) {
        return fake.guards;
    };
    callbacks.raidersWithin = [&fake](std::string_view, float, float,
                                      float) {
        return fake.breached ? 1 : 0;
    };
    callbacks.endDefense = [&fake](std::string_view id, bool fallen,
                                   std::string_view reason) {
        ++fake.endCalls;
        fake.endId = std::string(id);
        fake.endFallen = fallen;
        fake.endReason = std::string(reason);
    };
    callbacks.endReclaim = [&fake](std::string_view id, bool held,
                                   std::string_view reason) {
        ++fake.endReclaimCalls;
        fake.endId = std::string(id);
        fake.endFallen = !held;
        fake.endReason = std::string(reason);
    };
    callbacks.onResult = [&fake](const RegionalSiegeResult& result) {
        ++fake.resultCalls;
        fake.lastResult = result;
    };
    return callbacks;
}

RegionalSiegeConfig TestConfig() {
    RegionalSiegeConfig config;
    config.warmupSeconds = 5.0f;
    config.waveIntervalSeconds = 10.0f;
    config.waves = {2, 3};
    config.maxAlive = 2;
    config.maxWaveSize = 5;
    config.breachRadius = 2.0f;
    config.lossDelaySeconds = 0.5f;
    return config;
}

RegionalSiegeStart TestStart() {
    RegionalSiegeStart start;
    start.regionId = "south_farmland";
    start.regionName = "南部农场";
    start.centerX = 110.0f;
    start.centerY = 210.0f;
    start.attackMinute = 90;
    start.defenseScore = 35;
    start.enemyStrength = 28;
    start.primaryDirection = 2;
    return start;
}

}  // namespace

TEST_CASE("M32-T22 单机围攻结果生成守城战报") {
    RegionalSiegeResult result;
    result.regionId = "south_farmland";
    result.regionName = "南部农场";
    result.held = true;
    result.wavesSpawned = 3;
    result.waveCount = 3;
    result.kills = 12;
    result.guardsStart = 6;
    result.guardsEnd = 4;
    result.breached = true;
    result.rating = 'A';

    const std::string text = RegionalSiegeReportText(result);
    CHECK(text.rfind("守城战报", 0) == 0);
    CHECK(text.find("南部农场") != std::string::npos);
    CHECK(text.find("守住") != std::string::npos);
    CHECK(text.find("波次 3/3") != std::string::npos);
    CHECK(text.find("击杀 12") != std::string::npos);
    CHECK(text.find("守卫 4/6") != std::string::npos);
    CHECK(text.find("破口") != std::string::npos);
    CHECK(text.find("评级 A") != std::string::npos);

    result.held = false;
    result.reclaim = true;
    result.breached = false;
    result.guardsStart = 0;
    result.rating = '-';
    const std::string failed = RegionalSiegeReportText(result);
    CHECK(failed.find("失守") != std::string::npos);
    CHECK(failed.find("反攻") != std::string::npos);
    CHECK(failed.find("守卫") == std::string::npos);
    CHECK(failed.find("破口") == std::string::npos);
    CHECK(failed.find("评级") == std::string::npos);
}

TEST_CASE("M32-T4 详细围攻预热、波次上限与守住战报") {
    FakeSiege fake;
    RegionalSiege siege;
    siege.Configure(TestConfig(), MakeCallbacks(fake));
    RegionalSiegeStart start = TestStart();
    start.contestedFaction = ContestedFaction::Raiders;
    REQUIRE(siege.Start(start, 100));
    CHECK(fake.beginCalls == 1);
    CHECK(siege.Active());
    CHECK(siege.Snapshot().phase == RegionalSiegePhase::Warmup);

    siege.Update(5.0f, 105, true);
    CHECK(siege.Snapshot().phase == RegionalSiegePhase::Waves);
    CHECK(fake.enemies == 2);
    CHECK(fake.spawned == 2);
    CHECK(fake.lastFaction == ContestedFaction::Raiders);
    CHECK(siege.Snapshot().wave == 1);
    CHECK(siege.Snapshot().waveCount == 2);

    siege.Update(10.0f, 115, true);
    CHECK(fake.enemies == 2);
    CHECK(fake.spawned == 2);

    fake.enemies = 0;
    siege.Update(0.0f, 116, true);
    CHECK(fake.enemies == 2);
    CHECK(fake.spawned == 4);

    fake.enemies = 0;
    siege.Update(0.0f, 117, true);
    CHECK(fake.enemies == 1);
    CHECK(fake.spawned == 5);

    fake.enemies = 0;
    siege.Update(0.0f, 118, true);
    CHECK_FALSE(siege.Active());
    CHECK(fake.endCalls == 1);
    CHECK_FALSE(fake.endFallen);
    CHECK(fake.resultCalls == 1);
    REQUIRE(fake.lastResult.held);
    CHECK(fake.lastResult.kills == 5);
    CHECK(fake.lastResult.wavesSpawned == 2);
    CHECK(fake.lastResult.waveCount == 2);
    CHECK(fake.lastResult.guardsEnd == 2);
    CHECK(fake.lastResult.rating == 'S');

    siege.Update(1.0f, 119, true);
    CHECK(fake.resultCalls == 1);
}

TEST_CASE("M32-T4 破口与守卫倒下只结算一次") {
    FakeSiege fake;
    fake.guards = 1;
    fake.breached = true;
    RegionalSiegeConfig config = TestConfig();
    config.warmupSeconds = 0.0f;
    config.waves = {1};
    config.lossDelaySeconds = 0.5f;
    RegionalSiege siege;
    siege.Configure(config, MakeCallbacks(fake));
    REQUIRE(siege.Start(TestStart(), 100));
    siege.Update(0.0f, 100, true);
    REQUIRE(fake.enemies == 1);
    fake.guards = 0;
    siege.Update(0.6f, 101, true);

    CHECK_FALSE(siege.Active());
    CHECK(siege.Result().breached);
    CHECK_FALSE(siege.Result().held);
    CHECK(fake.endCalls == 1);
    CHECK(fake.endFallen);
    CHECK(fake.resultCalls == 1);
    CHECK(fake.endReason.find("突破") != std::string::npos);
    siege.Update(1.0f, 102, true);
    CHECK(fake.resultCalls == 1);
}

TEST_CASE("M32-T4 玩家离开中止详细围攻但不回写战略结果") {
    FakeSiege fake;
    RegionalSiege siege;
    siege.Configure(TestConfig(), MakeCallbacks(fake));
    REQUIRE(siege.Start(TestStart(), 100));
    siege.Update(6.0f, 106, true);
    REQUIRE(fake.enemies > 0);

    siege.Update(0.1f, 107, false);
    CHECK_FALSE(siege.Active());
    CHECK(siege.Finished());
    CHECK(fake.endCalls == 1);
    CHECK_FALSE(fake.endFallen);
    CHECK(fake.resultCalls == 0);
    CHECK(fake.endReason.find("离开") != std::string::npos);
}

TEST_CASE("M32-T4 战略区域映射到不同聚落守卫") {
    FakeSiege fake;
    RegionalSiege siege;
    siege.Configure(TestConfig(), MakeCallbacks(fake));
    RegionalSiegeStart start = TestStart();
    start.regionId = "village_core";
    start.settlementId = "village_haven";
    REQUIRE(siege.Start(start, 100));
    CHECK(fake.beginId == "village_haven");
    siege.Update(5.0f, 105, true);
    CHECK(fake.spawnId == "village_haven");
    fake.enemies = 0;
    siege.Update(10.0f, 115, true);
    fake.enemies = 0;
    siege.Update(0.0f, 116, true);
    fake.enemies = 0;
    siege.Update(0.0f, 117, true);
    CHECK(siege.Result().regionId == "village_core");
    CHECK(fake.endId == "village_haven");
}

TEST_CASE("M32-T5 反攻波次无需守卫并可守住") {
    FakeSiege fake;
    fake.guards = 0;
    RegionalSiegeConfig config = TestConfig();
    config.warmupSeconds = 0.0f;
    config.waves = {1};
    config.maxAlive = 1;
    RegionalSiege siege;
    siege.Configure(TestConfig(), MakeCallbacks(fake));
    siege.ConfigureReclaim(config);
    RegionalSiegeStart start = TestStart();
    start.reclaim = true;
    start.settlementId = "south_farmland";
    REQUIRE(siege.Start(start, 100));
    CHECK(fake.beginCalls == 0);
    CHECK(fake.beginReclaimCalls == 1);
    CHECK(siege.Snapshot().reclaim);
    CHECK(siege.Snapshot().statusText.find("反攻") != std::string::npos);

    siege.Update(0.0f, 100, true);
    REQUIRE(fake.enemies == 1);
    fake.enemies = 0;
    siege.Update(0.0f, 101, true);
    CHECK_FALSE(siege.Active());
    CHECK(siege.Result().reclaim);
    CHECK(siege.Result().held);
    CHECK(siege.Result().settlementId == "south_farmland");
    CHECK(fake.endReclaimCalls == 1);
    CHECK(fake.endCalls == 0);
    CHECK_FALSE(fake.endFallen);
}
