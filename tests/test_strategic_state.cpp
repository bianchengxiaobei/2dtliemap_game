#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "game/StrategicDirector.h"
#include "game/StrategicMap.h"
#include "game/StrategicPersistence.h"
#include "game/StrategicState.h"
#include "save/SaveManager.h"
#include "save/WorldManifest.h"
#include "world/WorldDefinition.h"

using namespace zl;

namespace {

struct TempRoot {
    std::filesystem::path path;

    explicit TempRoot(const std::string& name)
        : path(std::filesystem::temp_directory_path() /
               (name + "_" + std::to_string(static_cast<std::uint64_t>(
                                   std::chrono::steady_clock::now()
                                       .time_since_epoch()
                                       .count())))) {
        std::filesystem::remove_all(path);
    }

    ~TempRoot() { std::filesystem::remove_all(path); }
};

std::string StrategicJson() {
    return std::string(R"({
  "format": "zomboidlike-strategic-v1",
  "seed": 4242,
  "tickMinutes": 30,
  "warningHours": 1,
  "remoteWarningHours": 2,
  "autoResolveGraceMinutes": 120,
  "pressurePerHour": 0,
  "attackPressure": 50,
  "autoResolve": {
    "defensePerGarrison": 8,
    "defensePerFortification": 2,
    "defensePerSupply": 0.25,
    "enemyStrengthPerPressure": 0.7,
    "heldPressureRelief": 35,
    "lostPressureRelief": 15,
    "heldSupplies": -8,
    "heldAmmo": -10,
    "lostPeople": -1,
    "lostSupplies": -15,
    "lostAmmo": -10,
    "heldGarrisonLossMax": 2,
    "heldFortificationLoss": 1,
    "heldFacilityDamageMax": 40,
    "coreFacilityDamage": 50,
    "coreGarrisonLossPercent": 50,
    "heldRegionSupplyLoss": 5,
    "lostRegionSupplyLoss": 100,
    "coreRegionSupplyLoss": 50
  },
  "economy": {
    "enabled": true,
    "intervalMinutes": 1440,
    "suppliesPerStrategicValue": 2,
    "supplyCostPerPopulation": 1,
    "supplyCostPerGarrison": 1,
    "supplyCostPerRefugee": 1,
    "regionSupplyRestorePerDay": 5,
    "tradeAmmoPerDay": 1,
    "tradeSuppliesPerAmmo": 4,
    "tradeFuelPerDay": 0,
    "tradeSuppliesPerFuel": 8,
    "tradeReserveSupplies": 20,
    "fortificationRepairPerDay": 1,
    "shortagePressurePerSupply": 1,
    "shortageMoralePerSupply": 1,
    "missionRewards": [
      { "mission": "escort", "region": "farm", "supplies": 20, "ammo": 5, "regionSupply": 20, "regionGarrison": 1, "regionPopulation": 1 },
      { "mission": "relief", "region": "core", "supplies": 5, "relieveRefugees": 2 }
    ]
  },
  "initialResources": {
    "people": 10,
    "supplies": 20,
    "ammo": 30,
    "fuel": 4
  },
  "regions": [
    {
      "id": "core",
      "strategicValue": 5,
      "initialOwner": "Human",
      "garrison": 3,
      "fortification": 3,
      "defense": 70,
      "supply": 70,
      "population": 3,
      "pressure": 10,
      "morale": 85,
      "intelLevel": 3,
      "coreSafe": true
    },
    {
      "id": "farm",
      "strategicValue": 3,
      "initialOwner": "Human",
      "garrison": 1,
      "fortification": 1,
      "defense": 35,
      "supply": 45,
      "population": 4,
      "pressure": 30,
      "morale": 65,
      "intelLevel": 1
    }
  ]
})");
}

world::WorldDefinition MakeWorld() {
    world::WorldDefinition world;
    world.format = world::kWorldFormat;
    world.mapId = "strategic_test";
    world.seed = 7;
    world.generatorVersion = 1;
    world.mapHash = 0x1234u;
    world.regions.push_back(
        world::WorldRegion{"core", world::WorldRect{0, 0, 2, 2}, "village", {}});
    world.regions.push_back(world::WorldRegion{
        "farm", world::WorldRect{2, 0, 2, 2}, "farmland", {}});
    return world;
}

game::StrategicConfig MakeConfig() {
    game::StrategicConfig config;
    REQUIRE(game::ParseStrategicConfig(StrategicJson(), "test",
                                      &config, nullptr));
    return config;
}

game::StrategicTickReport Tick(game::StrategicState& state,
                               const game::StrategicConfig& config,
                               std::int64_t gameMinute,
                               bool anyPlayerOnline = true,
                               bool detailedEnabled = false,
                               std::vector<std::string> playerRegions = {}) {
    game::StrategicTickInput input;
    input.gameMinute = gameMinute;
    input.anyPlayerOnline = anyPlayerOnline;
    input.detailedEnabled = detailedEnabled;
    input.playerRegions = std::move(playerRegions);
    game::StrategicTickReport report;
    std::string error;
    REQUIRE(game::AdvanceStrategic(state, config, input, &report, &error));
    CHECK(error.empty());
    return report;
}

}  // namespace

TEST_CASE("M32-T1 战略配置加载并与 world.json 对齐") {
    const std::string root = std::string(ZL_SOURCE_DIR);
    game::StrategicConfig config;
    std::string error;
    REQUIRE(game::LoadStrategicConfig(root + "/assets/strategic.json", &config,
                                      &error));
    CHECK(error.empty());
    CHECK(config.enabled);
    CHECK(config.tickMinutes == 30);
    CHECK(config.warningHours == 48);
    CHECK(config.remoteWarningHours == 84);
    CHECK(config.autoResolveGraceMinutes == 2880);
    REQUIRE(config.regions.size() == 5u);
    const game::StrategicRegionConfig* village =
        config.Find("village_core");
    REQUIRE(village != nullptr);
    CHECK(village->initialOwner == game::StrategicOwner::Human);
    CHECK(village->coreSafe);
    CHECK(village->defense == 70);
    CHECK(village->hasAnchor);
    CHECK(village->anchorX == doctest::Approx(75.0f));
    CHECK(village->anchorY == doctest::Approx(26.5f));
    const game::StrategicRegionConfig* farmConfig =
        config.Find("south_farmland");
    REQUIRE(farmConfig != nullptr);
    CHECK(farmConfig->hasAnchor);
    CHECK(farmConfig->anchorX == doctest::Approx(110.0f));
    CHECK(farmConfig->anchorY == doctest::Approx(210.0f));
    CHECK(config.Find("east_wilds")->initialOwner ==
          game::StrategicOwner::Infested);

    world::WorldDefinition world;
    REQUIRE(game::LoadStrategicConfig(root + "/assets/strategic.json", &config,
                                      &error));
    REQUIRE(world::LoadWorldDefinition(
                root + "/assets/maps/village_demo_m31", &world, &error) ==
            world::WorldDefinitionStatus::Loaded);
    CHECK(game::ValidateStrategicAgainstWorld(config, world, &error));
    CHECK(error.empty());

    game::StrategicState state;
    REQUIRE(state.Initialize(config, &error));
    CHECK(state.regions.size() == 5u);
    CHECK(state.resources.people == 12);
    CHECK(state.resources.supplies == 180);
    CHECK(state.resources.ammo == 96);
    CHECK(state.resources.fuel == 3);
    const game::StrategicRegionState* farm =
        state.FindRegion("south_farmland");
    REQUIRE(farm != nullptr);
    CHECK(farm->owner == game::StrategicOwner::Human);
    CHECK(farm->defense == doctest::Approx(35.0f));
    CHECK(farm->population == 4);
}

TEST_CASE("M32-T1 战略状态转换、资源与事件幂等") {
    const game::StrategicConfig config = MakeConfig();
    game::StrategicState state;
    std::string error;
    REQUIRE(state.Initialize(config, &error));

    game::StrategicResources loss;
    loss.people = -1;
    loss.supplies = -10;
    loss.ammo = -5;
    REQUIRE(state.ApplyOwnerChange("farm", game::StrategicOwner::Infested,
                                   "siege-farm-1", 100, loss, &error));
    CHECK(error.empty());
    const game::StrategicRegionState* farm = state.FindRegion("farm");
    REQUIRE(farm != nullptr);
    CHECK(farm->owner == game::StrategicOwner::Infested);
    CHECK(farm->lastEventMinute == 100);
    CHECK(state.resources.people == 9);
    CHECK(state.resources.supplies == 10);
    CHECK(state.resources.ammo == 25);
    CHECK(state.events.size() == 1u);

    REQUIRE(state.ApplyOwnerChange("farm", game::StrategicOwner::Infested,
                                   "siege-farm-1", 100, loss, &error));
    CHECK(state.events.size() == 1u);
    CHECK(state.resources.people == 9);
    CHECK(state.resources.supplies == 10);
    CHECK_FALSE(state.ApplyOwnerChange("farm", game::StrategicOwner::Human,
                                       "siege-farm-1", 101, {}, &error));
    CHECK_FALSE(error.empty());
    CHECK(state.events.size() == 1u);

    game::StrategicResources supply;
    supply.people = 1;
    supply.supplies = 5;
    supply.ammo = 2;
    supply.fuel = 1;
    REQUIRE(state.ApplyResourceDelta("farm", supply, "loot-farm-1", 110,
                                     &error));
    CHECK(state.resources.people == 10);
    CHECK(state.resources.supplies == 15);
    CHECK(state.resources.ammo == 27);
    CHECK(state.resources.fuel == 5);
    CHECK(state.events.size() == 2u);
    REQUIRE(state.ApplyResourceDelta("farm", supply, "loot-farm-1", 110,
                                     &error));
    CHECK(state.events.size() == 2u);
    CHECK(state.resources.fuel == 5);

    REQUIRE(state.AdvanceTick(120, &error));
    CHECK_FALSE(state.AdvanceTick(119, &error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("M32-T1 战略配置和 world 对齐的非法输入") {
    const game::StrategicConfig config = MakeConfig();
    const world::WorldDefinition world = MakeWorld();
    std::string error;

    CHECK_FALSE(game::ParseStrategicConfig(
        R"({ "format": "strategic-v2", "regions": [] })", "bad format",
        nullptr, &error));
    CHECK_FALSE(error.empty());

    std::string duplicate = StrategicJson();
    duplicate.replace(duplicate.find("\"id\": \"farm\""), 12,
                      "\"id\": \"core\"");
    game::StrategicConfig bad;
    CHECK_FALSE(game::ParseStrategicConfig(duplicate, "duplicate", &bad,
                                           &error));
    CHECK(error.find("重复") != std::string::npos);

    std::string badOwner = StrategicJson();
    const std::size_t ownerPos = badOwner.find("Human");
    REQUIRE(ownerPos != std::string::npos);
    badOwner.replace(ownerPos, 5, "Unknown");
    CHECK_FALSE(game::ParseStrategicConfig(badOwner, "owner", &bad, &error));
    CHECK(error.find("initialOwner") != std::string::npos);

    std::string badDefense = StrategicJson();
    const std::size_t defensePos = badDefense.find("\"defense\": 35");
    REQUIRE(defensePos != std::string::npos);
    badDefense.replace(defensePos, 13, "\"defense\": 101");
    CHECK_FALSE(game::ParseStrategicConfig(badDefense, "defense", &bad,
                                           &error));
    CHECK(error.find("defense") != std::string::npos);

    game::StrategicConfig unknown = config;
    unknown.regions[0].id = "missing_region";
    CHECK_FALSE(game::ValidateStrategicAgainstWorld(unknown, world, &error));
    CHECK(error.find("world.json") != std::string::npos);
}

TEST_CASE("M32-T1 战略状态写入 manifest v5 并可恢复旧档") {
    const game::StrategicConfig config = MakeConfig();
    game::StrategicState state;
    std::string error;
    REQUIRE(state.Initialize(config, &error));
    game::StrategicResources loss;
    loss.people = -1;
    loss.supplies = -4;
    REQUIRE(state.ApplyOwnerChange("farm", game::StrategicOwner::Contested,
                                   "retake-farm", 200, loss, &error));
    REQUIRE(state.ApplyResourceDelta("global", {1, 2, 3, 1},
                                     "supply-global", 210, &error));

    save::WorldManifest manifest;
    manifest.identity.mapId = "strategic_test";
    manifest.identity.seed = 7;
    manifest.identity.generatorVersion = 1;
    manifest.identity.mapHash = 0x1234u;
    REQUIRE(game::CaptureStrategicState(state, &manifest, &error));
    CHECK(manifest.strategicRegions.size() == 2u);
    CHECK(manifest.strategicEvents.size() == 2u);
    CHECK(manifest.strategicResources.people == 10);

    const std::vector<std::uint8_t> bytes =
        save::EncodeWorldManifest(manifest, &error);
    REQUIRE_FALSE(bytes.empty());
    save::WorldManifest decoded;
    REQUIRE(save::DecodeWorldManifest(bytes, &decoded, &error));
    CHECK(decoded.version == save::kWorldManifestVersion);
    REQUIRE(decoded.strategicRegions.size() == 2u);
    CHECK(decoded.strategicRegions[1].id == "farm");
    CHECK(decoded.strategicRegions[1].owner == 2u);
    CHECK(decoded.strategicEvents[1].regionId == "global");
    CHECK(decoded.strategicTickMinute == 210);

    game::StrategicState restored;
    REQUIRE(game::RestoreStrategicState(decoded, config, &restored, &error));
    CHECK(restored.regions.size() == 2u);
    CHECK(restored.FindRegion("farm")->owner ==
          game::StrategicOwner::Contested);
    CHECK(restored.FindRegion("farm")->lastEventMinute == 200);
    CHECK(restored.resources.people == 10);
    CHECK(restored.events.size() == 2u);

    save::WorldManifest oldManifest;
    oldManifest.version = 2;
    oldManifest.identity = manifest.identity;
    REQUIRE(game::RestoreStrategicState(oldManifest, config, &restored,
                                        &error));
    CHECK(restored.FindRegion("farm")->owner == game::StrategicOwner::Human);
    CHECK(restored.FindRegion("core")->owner == game::StrategicOwner::Human);
    CHECK(restored.events.empty());
}

TEST_CASE("M32-T1 SaveManager 战略 manifest 跨重载恢复") {
    TempRoot root("zl_m32_t1_manifest");
    const game::StrategicConfig config = MakeConfig();
    game::StrategicState state;
    std::string error;
    REQUIRE(state.Initialize(config, &error));
    REQUIRE(state.ApplyOwnerChange("farm", game::StrategicOwner::Infested,
                                   "siege-farm-save", 300, {-1, -5, -2, 0},
                                   &error));

    save::WorldManifest manifest;
    manifest.identity.mapId = "strategic_save_test";
    manifest.identity.seed = 11;
    manifest.identity.generatorVersion = 1;
    manifest.identity.mapHash = 0x8899u;
    REQUIRE(game::CaptureStrategicState(state, &manifest, &error));
    save::EntitySnapshot empty;
    save::SaveManager manager({root.path, "m32-test"});
    REQUIRE(manager.EnsureRoot(&error));
    save::SaveManager::CreateOptions options;
    options.name = "M32 T1";
    options.mapId = manifest.identity.mapId;
    options.gameVersion = "m32-test";
    options.entities = &empty;
    options.manifest = &manifest;
    std::string id;
    REQUIRE(manager.Create(options, &id, &error));

    save::SaveManager restarted({root.path, "m32-test"});
    save::WorldManifest loaded;
    REQUIRE(restarted.ReadWorldManifest(id, &loaded, &error) ==
            save::SaveManager::LoadResult::Ok);
    game::StrategicState restored;
    REQUIRE(game::RestoreStrategicState(loaded, config, &restored, &error));
    REQUIRE(restored.FindRegion("farm") != nullptr);
    CHECK(restored.FindRegion("farm")->owner ==
          game::StrategicOwner::Infested);
    CHECK(restored.FindRegion("farm")->lastEventMinute == 300);
    CHECK(restored.resources.people == 9);
    CHECK(restored.resources.supplies == 15);
    CHECK(restored.events.size() == 1u);
}

TEST_CASE("M32-T2 战略威胁配置与自动结算参数") {
    const std::string root = std::string(ZL_SOURCE_DIR);
    game::StrategicConfig config;
    std::string error;
    REQUIRE(game::LoadStrategicConfig(root + "/assets/strategic.json", &config,
                                      &error));
    CHECK(error.empty());
    CHECK(config.seed == 20261110u);
    CHECK(config.pressurePerHour == doctest::Approx(0.25f));
    CHECK(config.attackPressure == doctest::Approx(70.0f));
    CHECK(config.autoResolve.defensePerGarrison == doctest::Approx(8.0f));
    CHECK(config.autoResolve.defensePerFortification == doctest::Approx(2.0f));
    CHECK(config.autoResolve.defensePerSupply == doctest::Approx(0.25f));
    CHECK(config.autoResolve.enemyStrengthPerPressure ==
          doctest::Approx(0.7f));
    CHECK(config.economy.enabled);
    CHECK(config.economy.intervalMinutes == 1440);
    CHECK(config.economy.missionRewards.size() == 4u);
    CHECK(config.economy.FindMission("settlement_escort") != nullptr);
    CHECK(config.autoResolve.heldSupplies == -8);
    CHECK(config.autoResolve.lostPeople == -1);
    const game::StrategicRegionConfig* east = config.Find("east_wilds");
    REQUIRE(east != nullptr);
    CHECK(east->remote);
}

TEST_CASE("M32-T2 预警、情报精度、固定 seed 与重复 tick") {
    const game::StrategicConfig config = MakeConfig();
    game::StrategicState state;
    std::string error;
    REQUIRE(state.Initialize(config, &error));
    state.FindRegion("farm")->pressure = 90.0f;

    const game::StrategicTickReport warning = Tick(state, config, 30);
    REQUIRE(warning.warnings.size() == 1u);
    const game::StrategicWarningInfo& info = warning.warnings[0];
    CHECK(info.regionId == "farm");
    CHECK(info.warningMinute == 30);
    CHECK(info.attackMinute == 90);
    CHECK(info.warningMinutes == 60);
    CHECK(info.intelLevel == 1);
    CHECK(info.enemyStrength == 63);
    CHECK(info.reportedEnemyStrength >= 45);
    CHECK(info.reportedEnemyStrength <= 80);
    CHECK(info.reportedAttackMinute >= 0);
    CHECK(info.reportedAttackMinute >= info.attackMinute - 360);
    CHECK(info.reportedAttackMinute <= info.attackMinute + 360);
    REQUIRE(state.FindRegion("farm") != nullptr);
    CHECK(state.FindRegion("farm")->siegePhase ==
          game::StrategicSiegePhase::Warning);
    CHECK(state.FindRegion("farm")->nextAttackMinute == 90);
    CHECK(state.events.size() == 1u);
    CHECK(state.events[0].kind == game::StrategicEventKind::Warning);

    const game::StrategicTickReport paused = Tick(state, config, 30);
    CHECK(paused.warnings.empty());
    CHECK(paused.resolutions.empty());
    CHECK(state.events.size() == 1u);

    game::StrategicState repeated;
    REQUIRE(repeated.Initialize(config, &error));
    repeated.FindRegion("farm")->pressure = 90.0f;
    const game::StrategicTickReport repeatedWarning = Tick(repeated, config, 30);
    REQUIRE(repeatedWarning.warnings.size() == 1u);
    CHECK(repeatedWarning.warnings[0].reportedEnemyStrength ==
          info.reportedEnemyStrength);
    CHECK(repeatedWarning.warnings[0].reportedAttackMinute ==
          info.reportedAttackMinute);
}

TEST_CASE("M32-T2 自动结算守住、失守、核心保护与防双结算") {
    const game::StrategicConfig config = MakeConfig();
    game::StrategicState state;
    std::string error;
    REQUIRE(state.Initialize(config, &error));
    state.FindRegion("farm")->pressure = 90.0f;
    REQUIRE(Tick(state, config, 30).warnings.size() == 1u);

    const game::StrategicTickReport lost = Tick(state, config, 90);
    REQUIRE(lost.resolutions.size() == 1u);
    const game::StrategicResolutionInfo& lostInfo = lost.resolutions[0];
    CHECK(lostInfo.regionId == "farm");
    CHECK(lostInfo.attackMinute == 90);
    CHECK(lostInfo.resolvedMinute == 90);
    CHECK(lostInfo.defenseScore == 56);
    CHECK(lostInfo.enemyStrength == 63);
    CHECK_FALSE(lostInfo.held);
    CHECK_FALSE(lostInfo.coreProtected);
    CHECK(lostInfo.ownerAfter == game::StrategicOwner::Infested);
    CHECK(lostInfo.resourceDelta.people == -1);
    CHECK(lostInfo.resourceDelta.supplies == -15);
    CHECK(state.FindRegion("farm")->owner ==
          game::StrategicOwner::Infested);
    CHECK(state.FindRegion("farm")->siegePhase ==
          game::StrategicSiegePhase::None);
    CHECK(state.FindRegion("farm")->nextAttackMinute == -1);
    CHECK(state.resources.people == 9);
    CHECK(state.resources.supplies == 5);
    CHECK(state.resources.ammo == 20);
    CHECK(state.events.size() == 3u);
    CHECK(state.events[1].kind == game::StrategicEventKind::Siege);
    CHECK(state.events[2].kind == game::StrategicEventKind::Refugee);

    const game::StrategicTickReport repeated = Tick(state, config, 120);
    CHECK(repeated.resolutions.empty());
    CHECK(state.events.size() == 3u);
    CHECK(state.resources.people == 9);
    CHECK(state.resources.supplies == 5);

    game::StrategicState heldState;
    REQUIRE(heldState.Initialize(config, &error));
    heldState.FindRegion("core")->pressure = 90.0f;
    const game::StrategicTickReport heldWarning = Tick(heldState, config, 30);
    REQUIRE(heldWarning.warnings.size() == 1u);
    CHECK(heldWarning.warnings[0].reportedEnemyStrength ==
          heldWarning.warnings[0].enemyStrength);
    CHECK(heldWarning.warnings[0].reportedAttackMinute ==
          heldWarning.warnings[0].attackMinute);
    const game::StrategicTickReport held = Tick(heldState, config, 90);
    REQUIRE(held.resolutions.size() == 1u);
    CHECK(held.resolutions[0].held);
    CHECK(held.resolutions[0].defenseScore == 118);
    CHECK(held.resolutions[0].enemyStrength == 63);
    CHECK(heldState.FindRegion("core")->owner ==
          game::StrategicOwner::Human);
    CHECK(heldState.FindRegion("core")->pressure == doctest::Approx(55.0f));
    CHECK(heldState.resources.people == 10);
    CHECK(heldState.resources.supplies == 12);
    CHECK(heldState.resources.ammo == 20);

    game::StrategicState boundary;
    REQUIRE(boundary.Initialize(config, &error));
    game::StrategicRegionState* boundaryFarm = boundary.FindRegion("farm");
    REQUIRE(boundaryFarm != nullptr);
    boundaryFarm->pressure = 100.0f;
    boundaryFarm->defense = 70.0f;
    boundaryFarm->garrison = 0;
    boundaryFarm->supply = 0.0f;
    REQUIRE(Tick(boundary, config, 30).warnings.size() == 1u);
    const game::StrategicTickReport tie = Tick(boundary, config, 90);
    REQUIRE(tie.resolutions.size() == 1u);
    CHECK(tie.resolutions[0].held);
    CHECK(tie.resolutions[0].defenseScore == 72);
    CHECK(tie.resolutions[0].enemyStrength == 70);

    game::StrategicState core;
    REQUIRE(core.Initialize(config, &error));
    game::StrategicRegionState* coreRegion = core.FindRegion("core");
    REQUIRE(coreRegion != nullptr);
    coreRegion->pressure = 100.0f;
    coreRegion->defense = 0.0f;
    coreRegion->garrison = 0;
    coreRegion->supply = 0.0f;
    REQUIRE(Tick(core, config, 30).warnings.size() == 1u);
    const game::StrategicTickReport protectedCore = Tick(core, config, 90);
    REQUIRE(protectedCore.resolutions.size() == 1u);
    CHECK(protectedCore.resolutions[0].coreProtected);
    CHECK_FALSE(protectedCore.resolutions[0].held);
    CHECK(protectedCore.resolutions[0].ownerAfter ==
          game::StrategicOwner::Contested);
    CHECK(protectedCore.warnings.size() == 1u);
    CHECK(coreRegion->owner == game::StrategicOwner::Contested);
    CHECK(coreRegion->siegePhase == game::StrategicSiegePhase::Warning);
    CHECK(coreRegion->nextAttackMinute == 150);
    CHECK(core.events.size() == 3u);
    CHECK(core.events.back().kind == game::StrategicEventKind::Warning);
}

TEST_CASE("M32-T2 提前到达、离线宽限与暂停") {
    const game::StrategicConfig config = MakeConfig();
    game::StrategicState state;
    std::string error;
    REQUIRE(state.Initialize(config, &error));
    state.FindRegion("farm")->pressure = 90.0f;
    REQUIRE(Tick(state, config, 30).warnings.size() == 1u);

    const game::StrategicTickReport detailed =
        Tick(state, config, 90, true, true, {"farm"});
    REQUIRE(detailed.detailedRegions.size() == 1u);
    CHECK(detailed.detailedRegions[0] == "farm");
    CHECK(detailed.resolutions.empty());
    CHECK(state.FindRegion("farm")->siegePhase ==
          game::StrategicSiegePhase::Active);

    const game::StrategicTickReport stillThere =
        Tick(state, config, 120, true, true, {"farm"});
    CHECK(stillThere.resolutions.empty());
    CHECK(state.FindRegion("farm")->siegePhase ==
          game::StrategicSiegePhase::Active);

    const game::StrategicTickReport leftEarly =
        Tick(state, config, 150, true, true, {});
    CHECK(leftEarly.resolutions.empty());
    const game::StrategicTickReport leftLate =
        Tick(state, config, 240, true, true, {});
    REQUIRE(leftLate.resolutions.size() == 1u);
    CHECK(leftLate.resolutions[0].resolvedMinute == 240);

    game::StrategicState offline;
    REQUIRE(offline.Initialize(config, &error));
    offline.FindRegion("farm")->pressure = 80.0f;
    REQUIRE(Tick(offline, config, 30).warnings.size() == 1u);
    const game::StrategicTickReport offlineAtAttack =
        Tick(offline, config, 90, false);
    CHECK(offlineAtAttack.resolutions.empty());
    CHECK(offline.FindRegion("farm")->siegePhase ==
          game::StrategicSiegePhase::Warning);
    const game::StrategicTickReport offlineGrace =
        Tick(offline, config, 180, false);
    CHECK(offlineGrace.resolutions.empty());
    const game::StrategicTickReport offlineLate =
        Tick(offline, config, 210, false);
    REQUIRE(offlineLate.resolutions.size() == 1u);
}

TEST_CASE("M32-T2 预警状态持久化后继续结算") {
    const game::StrategicConfig config = MakeConfig();
    game::StrategicState state;
    std::string error;
    REQUIRE(state.Initialize(config, &error));
    state.FindRegion("farm")->pressure = 90.0f;
    REQUIRE(Tick(state, config, 30).warnings.size() == 1u);

    save::WorldManifest manifest;
    REQUIRE(game::CaptureStrategicState(state, &manifest, &error));
    game::StrategicState restored;
    REQUIRE(game::RestoreStrategicState(manifest, config, &restored, &error));
    REQUIRE(restored.FindRegion("farm") != nullptr);
    CHECK(restored.FindRegion("farm")->siegePhase ==
          game::StrategicSiegePhase::Warning);
    CHECK(restored.FindRegion("farm")->nextAttackMinute == 90);
    CHECK(restored.events.size() == 1u);

    const game::StrategicTickReport late = Tick(restored, config, 90);
    REQUIRE(late.resolutions.size() == 1u);
    CHECK(restored.FindRegion("farm")->owner ==
          game::StrategicOwner::Infested);
    CHECK(restored.events.size() == 3u);
}

TEST_CASE("M32-T4 详细围攻结果回写且只结算一次") {
    const game::StrategicConfig config = MakeConfig();
    game::StrategicState heldState;
    std::string error;
    REQUIRE(heldState.Initialize(config, &error));
    heldState.FindRegion("farm")->pressure = 80.0f;
    REQUIRE(Tick(heldState, config, 30).warnings.size() == 1u);
    REQUIRE(Tick(heldState, config, 90, true, true, {"farm"})
                .detailedRegions.size() == 1u);
    const std::size_t eventsBefore = heldState.events.size();
    const game::StrategicResources resourcesBefore = heldState.resources;

    game::StrategicDetailedResolution held;
    held.regionId = "farm";
    held.attackMinute = 90;
    held.resolvedMinute = 120;
    held.held = true;
    held.rating = 'A';
    REQUIRE(game::ResolveStrategicSiege(heldState, config, held, &error));
    CHECK(error.empty());
    const game::StrategicRegionState* farm = heldState.FindRegion("farm");
    REQUIRE(farm != nullptr);
    CHECK(farm->owner == game::StrategicOwner::Human);
    CHECK(farm->siegePhase == game::StrategicSiegePhase::None);
    CHECK(farm->nextAttackMinute == -1);
    CHECK(heldState.events.size() == eventsBefore + 1);
    REQUIRE(heldState.events.back().kind == game::StrategicEventKind::Siege);
    CHECK(heldState.events.back().id == "siege.farm.90");
    CHECK(heldState.resources.supplies == resourcesBefore.supplies - 8);
    CHECK(heldState.resources.ammo == resourcesBefore.ammo - 10);

    const std::size_t eventsAfter = heldState.events.size();
    const game::StrategicResources resourcesAfter = heldState.resources;
    REQUIRE(game::ResolveStrategicSiege(heldState, config, held, &error));
    CHECK(heldState.events.size() == eventsAfter);
    CHECK(heldState.resources.supplies == resourcesAfter.supplies);

    game::StrategicState lostState;
    REQUIRE(lostState.Initialize(config, &error));
    lostState.FindRegion("farm")->pressure = 90.0f;
    REQUIRE(Tick(lostState, config, 30).warnings.size() == 1u);
    REQUIRE(Tick(lostState, config, 90, true, true, {"farm"})
                .resolutions.empty());
    game::StrategicDetailedResolution lost = held;
    lost.held = false;
    lost.breached = true;
    lost.rating = 'C';
    REQUIRE(game::ResolveStrategicSiege(lostState, config, lost, &error));
    farm = lostState.FindRegion("farm");
    REQUIRE(farm != nullptr);
    CHECK(farm->owner == game::StrategicOwner::Infested);
    CHECK(farm->garrison == 0);
    CHECK(farm->population == 0);
    CHECK(farm->siegePhase == game::StrategicSiegePhase::None);
}

TEST_CASE("M32-T2 远程区域使用远程预警时长") {
    game::StrategicConfig config = MakeConfig();
    config.regions[1].remote = true;
    game::StrategicState state;
    std::string error;
    REQUIRE(state.Initialize(config, &error));
    state.FindRegion("farm")->pressure = 90.0f;

    const game::StrategicTickReport warning = Tick(state, config, 30);
    REQUIRE(warning.warnings.size() == 1u);
    CHECK(warning.warnings[0].warningMinutes == 120);
    CHECK(warning.warnings[0].attackMinute == 150);
    CHECK(state.FindRegion("farm")->nextAttackMinute == 150);
}

TEST_CASE("M32-T2 压力增长不会跳过预警") {
    game::StrategicConfig config = MakeConfig();
    config.pressurePerHour = 20.0f;
    game::StrategicState state;
    std::string error;
    REQUIRE(state.Initialize(config, &error));
    state.FindRegion("farm")->pressure = 0.0f;

    const game::StrategicTickReport first = Tick(state, config, 30);
    CHECK(first.warnings.empty());
    const game::StrategicTickReport second = Tick(state, config, 60);
    CHECK(second.warnings.empty());
    CHECK(state.FindRegion("farm")->owner == game::StrategicOwner::Human);

    const game::StrategicTickReport warned = Tick(state, config, 300);
    CHECK(warned.warnings.size() == 2u);
    CHECK(warned.resolutions.empty());
    CHECK(state.FindRegion("farm")->owner == game::StrategicOwner::Human);
    CHECK(state.FindRegion("core")->owner == game::StrategicOwner::Human);
}

TEST_CASE("M32-T5 失守难民、核心清理、设施修复、反攻与再次预警") {
    game::StrategicConfig config = MakeConfig();
    config.regions[1].refugeeTarget = "core";
    config.recovery.delayMinutes = 0;
    config.recovery.repairSupplies = 0;
    config.recovery.repairAmmo = 0;
    config.recovery.restoreMinutes = 0;
    config.recovery.retryMinutes = 0;
    game::StrategicState state;
    std::string error;
    REQUIRE(state.Initialize(config, &error));
    game::StrategicRegionState* farm = state.FindRegion("farm");
    game::StrategicRegionState* core = state.FindRegion("core");
    REQUIRE(farm != nullptr);
    REQUIRE(core != nullptr);
    farm->pressure = 90.0f;

    REQUIRE(Tick(state, config, 30).warnings.size() == 1u);
    const game::StrategicTickReport lost = Tick(state, config, 90);
    REQUIRE(lost.resolutions.size() == 1u);
    CHECK(farm->owner == game::StrategicOwner::Infested);
    CHECK(farm->recoveryStage == game::StrategicRecoveryStage::ClearCore);
    CHECK(farm->facilityDamage == 100);
    CHECK(farm->infectionCores == 1);
    CHECK(farm->recoveryReadyMinute == 90);
    CHECK(core->refugees == 3);
    CHECK(core->pressure > 10.0f);
    CHECK(state.events.back().kind == game::StrategicEventKind::Refugee);

    save::WorldManifest manifest;
    manifest.identity.mapId = "strategic_test";
    manifest.identity.seed = 7;
    manifest.identity.generatorVersion = 1;
    manifest.identity.mapHash = 0x1234u;
    REQUIRE(game::CaptureStrategicState(state, &manifest, &error));
    CHECK(manifest.version >= 4u);
    const std::vector<std::uint8_t> bytes =
        save::EncodeWorldManifest(manifest, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    save::WorldManifest decoded;
    REQUIRE(save::DecodeWorldManifest(bytes, &decoded, &error));
    const auto savedFarm = std::find_if(
        decoded.strategicRegions.begin(), decoded.strategicRegions.end(),
        [](const save::ManifestStrategicRegion& region) {
            return region.id == "farm";
        });
    REQUIRE(savedFarm != decoded.strategicRegions.end());
    CHECK(savedFarm->recoveryStage ==
          static_cast<std::uint8_t>(game::StrategicRecoveryStage::ClearCore));
    CHECK(savedFarm->facilityDamage == 100);
    CHECK(savedFarm->infectionCores == 1);
    CHECK(savedFarm->refugees == 0);
    CHECK(savedFarm->refugeeHost == "core");
    game::StrategicState restoredState;
    REQUIRE(game::RestoreStrategicState(decoded, config, &restoredState,
                                        &error));
    CHECK(restoredState.FindRegion("farm")->recoveryStage ==
          game::StrategicRecoveryStage::ClearCore);
    CHECK(restoredState.FindRegion("farm")->recoveryReadyMinute == 90);
    CHECK(restoredState.FindRegion("core")->refugees == 3);

    REQUIRE(game::ClearStrategicRecoveryCore(state, config, "farm", 90,
                                             &error));
    CHECK(farm->recoveryStage == game::StrategicRecoveryStage::Repair);
    CHECK(farm->infectionCores == 0);
    REQUIRE(game::RepairStrategicRecoveryFacility(state, config, "farm", 90,
                                                  &error));
    CHECK(farm->recoveryStage ==
          game::StrategicRecoveryStage::Counterattack);
    CHECK(farm->facilityDamage == 0);

    game::StrategicReclaimResolution reclaim;
    reclaim.regionId = "farm";
    reclaim.attackMinute = 100;
    reclaim.resolvedMinute = 100;
    reclaim.held = true;
    reclaim.rating = 'S';
    REQUIRE(game::ResolveStrategicReclaim(state, config, reclaim, &error));
    CHECK(farm->owner == game::StrategicOwner::Contested);
    CHECK(farm->recoveryStage == game::StrategicRecoveryStage::Restoring);
    CHECK(state.FindEvent("reclaim.farm.100") != nullptr);

    const game::StrategicTickReport restored = Tick(state, config, 130);
    CHECK(restored.completedRecoveries.size() == 1u);
    CHECK(farm->owner == game::StrategicOwner::Human);
    CHECK(farm->recoveryStage == game::StrategicRecoveryStage::None);
    CHECK(farm->population == 4);
    CHECK(core->refugees == 1);
    CHECK(state.FindEvent("recovery.farm.130") != nullptr);

    farm->pressure = 90.0f;
    const game::StrategicTickReport again = Tick(state, config, 160);
    REQUIRE(again.warnings.size() == 1u);
    CHECK(again.warnings[0].regionId == "farm");
    CHECK(farm->siegePhase == game::StrategicSiegePhase::Warning);
}

TEST_CASE("M32-T6 防御评分包含守军/工事/补给，结算伤亡与设施损失") {
    const game::StrategicConfig config = MakeConfig();
    game::StrategicState lostState;
    std::string error;
    REQUIRE(lostState.Initialize(config, &error));
    game::StrategicRegionState* farm = lostState.FindRegion("farm");
    REQUIRE(farm != nullptr);
    farm->pressure = 90.0f;
    REQUIRE(Tick(lostState, config, 30).warnings.size() == 1u);
    const game::StrategicTickReport lost = Tick(lostState, config, 90);
    REQUIRE(lost.resolutions.size() == 1u);
    const game::StrategicResolutionInfo& lostInfo = lost.resolutions[0];
    CHECK(lostInfo.defenseScore == 56);
    CHECK(lostInfo.defenseFromGarrison == 8);
    CHECK(lostInfo.defenseFromFortification == 2);
    CHECK(lostInfo.defenseFromSupply == 11);
    CHECK(lostInfo.garrisonBefore == 1);
    CHECK(lostInfo.garrisonAfter == 0);
    CHECK(lostInfo.populationBefore == 4);
    CHECK(lostInfo.populationAfter == 0);
    CHECK(lostInfo.fortificationBefore == 1);
    CHECK(lostInfo.fortificationAfter == 0);
    CHECK(lostInfo.facilityDamageBefore == 0);
    CHECK(lostInfo.facilityDamageAfter == 100);
    CHECK(lostInfo.regionSupplyBefore == 45);
    CHECK(lostInfo.regionSupplyAfter == 0);
    CHECK(farm->recoveryStage == game::StrategicRecoveryStage::ClearCore);

    game::StrategicState heldState;
    REQUIRE(heldState.Initialize(config, &error));
    game::StrategicRegionState* core = heldState.FindRegion("core");
    REQUIRE(core != nullptr);
    core->pressure = 90.0f;
    REQUIRE(Tick(heldState, config, 30).warnings.size() == 1u);
    const game::StrategicTickReport held = Tick(heldState, config, 90);
    REQUIRE(held.resolutions.size() == 1u);
    const game::StrategicResolutionInfo& heldInfo = held.resolutions[0];
    CHECK(heldInfo.held);
    CHECK(heldInfo.defenseScore == 118);
    CHECK(heldInfo.defenseFromGarrison == 24);
    CHECK(heldInfo.defenseFromFortification == 6);
    CHECK(heldInfo.defenseFromSupply == 18);
    CHECK(heldInfo.garrisonBefore == 3);
    CHECK(heldInfo.garrisonAfter == 1);
    CHECK(heldInfo.fortificationAfter == 2);
    CHECK(heldInfo.facilityDamageAfter == 22);
    CHECK(heldInfo.regionSupplyAfter == 65);
    CHECK(core->owner == game::StrategicOwner::Human);

    save::WorldManifest manifest;
    manifest.identity.mapId = "strategic_test";
    manifest.identity.seed = 7;
    manifest.identity.generatorVersion = 1;
    manifest.identity.mapHash = 0x1234u;
    REQUIRE(game::CaptureStrategicState(heldState, &manifest, &error));
    const std::vector<std::uint8_t> bytes =
        save::EncodeWorldManifest(manifest, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    save::WorldManifest decoded;
    REQUIRE(save::DecodeWorldManifest(bytes, &decoded, &error));
    game::StrategicState restoredState;
    REQUIRE(game::RestoreStrategicState(decoded, config, &restoredState,
                                        &error));
    const game::StrategicRegionState* restoredCore =
        restoredState.FindRegion("core");
    REQUIRE(restoredCore != nullptr);
    CHECK(restoredCore->garrison == 1);
    CHECK(restoredCore->fortification == 2);
    CHECK(restoredCore->facilityDamage == 22);
    CHECK(restoredCore->supply == doctest::Approx(65.0f));

    game::StrategicState coreState;
    REQUIRE(coreState.Initialize(config, &error));
    game::StrategicRegionState* weakCore = coreState.FindRegion("core");
    REQUIRE(weakCore != nullptr);
    weakCore->pressure = 100.0f;
    weakCore->defense = 0.0f;
    weakCore->garrison = 2;
    weakCore->fortification = 0;
    weakCore->supply = 0.0f;
    REQUIRE(Tick(coreState, config, 30).warnings.size() == 1u);
    const game::StrategicTickReport protectedCore = Tick(coreState, config, 90);
    REQUIRE(protectedCore.resolutions.size() == 1u);
    const game::StrategicResolutionInfo& coreInfo =
        protectedCore.resolutions[0];
    CHECK(coreInfo.coreProtected);
    CHECK(coreInfo.ownerAfter == game::StrategicOwner::Contested);
    CHECK(coreInfo.garrisonBefore == 2);
    CHECK(coreInfo.garrisonAfter == 1);
    CHECK(coreInfo.facilityDamageAfter == 50);
    CHECK(weakCore->owner == game::StrategicOwner::Contested);
    CHECK(weakCore->siegePhase == game::StrategicSiegePhase::Warning);
}

TEST_CASE("M32-T6 每日资源产消、交易、区域补给恢复与工事修复") {
    game::StrategicConfig config = MakeConfig();
    config.economy.tradeReserveSupplies = 0;
    game::StrategicState state;
    std::string error;
    REQUIRE(state.Initialize(config, &error));
    game::StrategicRegionState* core = state.FindRegion("core");
    game::StrategicRegionState* farm = state.FindRegion("farm");
    REQUIRE(core != nullptr);
    REQUIRE(farm != nullptr);
    farm->fortification = 0;

    const game::StrategicTickReport day = Tick(state, config, 1440);
    REQUIRE(day.economy.size() == 1u);
    const game::StrategicEconomyInfo& economy = day.economy[0];
    CHECK(economy.day == 1);
    CHECK(economy.days == 1);
    CHECK(economy.producedSupplies == 16);
    CHECK(economy.consumedSupplies == 11);
    CHECK(economy.shortageSupplies == 0);
    CHECK(economy.restoredRegionSupply == 10);
    CHECK(economy.repairedFortification == 1);
    CHECK(economy.tradedAmmo == 1);
    CHECK(state.resources.supplies == 11);
    CHECK(state.resources.ammo == 31);
    CHECK(core->supply == doctest::Approx(75.0f));
    CHECK(farm->supply == doctest::Approx(50.0f));
    CHECK(farm->fortification == 1);

    const game::StrategicTickReport sameDay = Tick(state, config, 1470);
    CHECK(sameDay.economy.empty());
    CHECK(state.resources.supplies == 11);

    state.resources.supplies = 0;
    farm->population = 100;
    const game::StrategicTickReport shortage = Tick(state, config, 2880);
    REQUIRE(shortage.economy.size() == 1u);
    CHECK(shortage.economy[0].shortageSupplies > 0);
    CHECK(farm->pressure > 30.0f);
    CHECK(farm->morale < 65.0f);
}

TEST_CASE("M32-T6 任务奖励转化为战略准备，重复完成不重复发放") {
    const game::StrategicConfig config = MakeConfig();
    game::StrategicState state;
    std::string error;
    REQUIRE(state.Initialize(config, &error));
    game::StrategicRegionState* core = state.FindRegion("core");
    game::StrategicRegionState* farm = state.FindRegion("farm");
    REQUIRE(core != nullptr);
    REQUIRE(farm != nullptr);
    core->refugees = 3;
    core->pressure = 50.0f;

    REQUIRE(game::ApplyStrategicMissionReward(state, config, "escort", 100,
                                              &error));
    CHECK(state.resources.supplies == 40);
    CHECK(state.resources.ammo == 35);
    CHECK(farm->supply == doctest::Approx(65.0f));
    CHECK(farm->garrison == 2);
    CHECK(farm->population == 5);
    CHECK(state.FindEvent("mission.escort") != nullptr);
    REQUIRE(game::ApplyStrategicMissionReward(state, config, "escort", 110,
                                              &error));
    CHECK(state.resources.supplies == 40);
    CHECK(farm->garrison == 2);

    REQUIRE(game::ApplyStrategicMissionReward(state, config, "relief", 200,
                                              &error));
    CHECK(core->refugees == 1);
    CHECK(core->pressure == doctest::Approx(40.0f));
    CHECK(state.resources.supplies == 45);

    farm->pressure = 90.0f;
    REQUIRE(Tick(state, config, 230).warnings.size() == 1u);
    const game::StrategicTickReport held = Tick(state, config, 290);
    REQUIRE(held.resolutions.size() == 1u);
    CHECK(held.resolutions[0].held);
    CHECK(held.resolutions[0].defenseScore == 69);
}

TEST_CASE("M32-T6 不同准备度的胜负分布且不存在必败局") {
    const game::StrategicConfig config = MakeConfig();
    int heldCount = 0;
    int lostCount = 0;
    for (int preparation = 0; preparation <= 3; ++preparation) {
        game::StrategicState state;
        std::string error;
        REQUIRE(state.Initialize(config, &error));
        game::StrategicRegionState* farm = state.FindRegion("farm");
        REQUIRE(farm != nullptr);
        farm->pressure = 90.0f;
        farm->garrison = preparation;
        farm->fortification = preparation;
        farm->supply = 45.0f + static_cast<float>(preparation) * 10.0f;
        REQUIRE(Tick(state, config, 30).warnings.size() == 1u);
        const game::StrategicTickReport result = Tick(state, config, 90);
        REQUIRE(result.resolutions.size() == 1u);
        const bool expectedHeld = result.resolutions[0].defenseScore >=
                                  result.resolutions[0].enemyStrength;
        CHECK(result.resolutions[0].held == expectedHeld);
        if (expectedHeld) {
            ++heldCount;
        } else {
            ++lostCount;
        }
    }
    CHECK(heldCount > 0);
    CHECK(lostCount > 0);
}

TEST_CASE("M32-T8 垂直切片：2 人类 + 2 争夺 + 1 沦陷区域，预警/支援/失守/夺回两次复跑") {
    const std::string source = std::string(ZL_SOURCE_DIR);
    game::StrategicConfig config;
    std::string error;
    REQUIRE(game::LoadStrategicConfig(
        source + "/assets/strategic.json", &config, &error));

    int humanRegions = 0;
    int infestedRegions = 0;
    int contestedRegions = 0;
    for (const game::StrategicRegionConfig& region : config.regions) {
        if (region.initialOwner == game::StrategicOwner::Human) {
            ++humanRegions;
        } else if (region.initialOwner == game::StrategicOwner::Infested) {
            ++infestedRegions;
        } else if (region.initialOwner == game::StrategicOwner::Contested) {
            ++contestedRegions;
        }
    }
    CHECK(humanRegions == 2);
    CHECK(contestedRegions == 2);
    CHECK(infestedRegions == 1);

    for (int run = 0; run < 2; ++run) {
        game::StrategicState heldState;
        REQUIRE(heldState.Initialize(config, &error));
        game::StrategicRegionState* farm =
            heldState.FindRegion("south_farmland");
        REQUIRE(farm != nullptr);
        farm->pressure = 70.0f;
        const game::StrategicTickReport warning =
            Tick(heldState, config, 30);
        REQUIRE(warning.warnings.size() == 1u);
        CHECK(farm->siegePhase == game::StrategicSiegePhase::Warning);
        const std::int64_t attackMinute = farm->nextAttackMinute;
        CHECK(attackMinute > 30);

        const float supplyBefore = farm->supply;
        farm->supply = std::min(100.0f, farm->supply + 20.0f);
        CHECK(farm->supply > supplyBefore);

        game::StrategicDetailedResolution held;
        held.regionId = "south_farmland";
        held.attackMinute = attackMinute;
        held.resolvedMinute = attackMinute;
        held.held = true;
        held.rating = 'A';
        REQUIRE(game::ResolveStrategicSiege(heldState, config, held, &error));
        CHECK(farm->owner == game::StrategicOwner::Human);
        CHECK(farm->siegePhase == game::StrategicSiegePhase::None);
        CHECK(heldState.FindEvent("siege.south_farmland." +
                                  std::to_string(attackMinute)) != nullptr);

        game::StrategicState lostState;
        REQUIRE(lostState.Initialize(config, &error));
        farm = lostState.FindRegion("south_farmland");
        REQUIRE(farm != nullptr);
        farm->pressure = 70.0f;
        REQUIRE(Tick(lostState, config, 30).warnings.size() == 1u);
        const std::int64_t lostAttackMinute = farm->nextAttackMinute;
        game::StrategicDetailedResolution lost;
        lost.regionId = "south_farmland";
        lost.attackMinute = lostAttackMinute;
        lost.resolvedMinute = lostAttackMinute;
        lost.held = false;
        lost.breached = true;
        lost.rating = 'C';
        REQUIRE(game::ResolveStrategicSiege(lostState, config, lost, &error));
        CHECK(farm->owner == game::StrategicOwner::Infested);
        CHECK(farm->recoveryStage ==
              game::StrategicRecoveryStage::ClearCore);
        CHECK(farm->infectionCores > 0);
        const std::int64_t readyMinute = farm->recoveryReadyMinute;
        REQUIRE(readyMinute >= lostAttackMinute);

        REQUIRE(game::ClearStrategicRecoveryCore(
            lostState, config, "south_farmland", readyMinute, &error));
        CHECK(farm->recoveryStage == game::StrategicRecoveryStage::Repair);
        REQUIRE(game::RepairStrategicRecoveryFacility(
            lostState, config, "south_farmland", readyMinute, &error));
        CHECK(farm->recoveryStage ==
              game::StrategicRecoveryStage::Counterattack);

        game::StrategicReclaimResolution reclaim;
        reclaim.regionId = "south_farmland";
        reclaim.attackMinute = readyMinute;
        reclaim.resolvedMinute = readyMinute;
        reclaim.held = true;
        reclaim.rating = 'S';
        REQUIRE(game::ResolveStrategicReclaim(lostState, config, reclaim,
                                              &error));
        CHECK(farm->owner == game::StrategicOwner::Contested);
        CHECK(farm->recoveryStage ==
              game::StrategicRecoveryStage::Restoring);
        const std::int64_t restoreMinute = farm->recoveryReadyMinute;
        REQUIRE(restoreMinute >= readyMinute);
        for (game::StrategicRegionState& region : lostState.regions) {
            region.pressure = 0.0f;
        }
        const game::StrategicTickReport restored =
            Tick(lostState, config, restoreMinute);
        CHECK(restored.completedRecoveries.size() == 1u);
        CHECK(farm->owner == game::StrategicOwner::Human);
        CHECK(farm->recoveryStage == game::StrategicRecoveryStage::None);
    }
}

TEST_CASE("M32-T9 争夺层：河谷初始争夺、守住归 Human、失守转沦陷并疏散") {
    const std::string source = std::string(ZL_SOURCE_DIR);
    game::StrategicConfig config;
    std::string error;
    REQUIRE(game::LoadStrategicConfig(
        source + "/assets/strategic.json", &config, &error));
    REQUIRE(config.regions.size() == 5u);

    const game::StrategicRegionConfig* def = config.Find("riverlands");
    REQUIRE(def != nullptr);
    CHECK(def->initialOwner == game::StrategicOwner::Contested);
    CHECK(def->contestedFaction == game::ContestedFaction::Infected);
    CHECK(def->anchorX == doctest::Approx(214.0f));
    CHECK(def->anchorY == doctest::Approx(120.0f));
    CHECK(def->strategicValue == 3);
    CHECK_FALSE(def->remote);

    int humans = 0;
    int infested = 0;
    int contested = 0;
    for (const game::StrategicRegionConfig& region : config.regions) {
        if (region.initialOwner == game::StrategicOwner::Human) {
            ++humans;
        } else if (region.initialOwner == game::StrategicOwner::Infested) {
            ++infested;
        } else if (region.initialOwner == game::StrategicOwner::Contested) {
            ++contested;
        }
    }
    CHECK(humans == 2);
    CHECK(infested == 1);
    CHECK(contested == 2);

    game::StrategicState heldState;
    REQUIRE(heldState.Initialize(config, &error));
    game::StrategicRegionState* bridge = heldState.FindRegion("riverlands");
    REQUIRE(bridge != nullptr);
    CHECK(bridge->owner == game::StrategicOwner::Contested);
    CHECK(bridge->infectionCores == 0);
    CHECK(bridge->recoveryStage == game::StrategicRecoveryStage::None);
    bridge->pressure = 70.0f;
    const game::StrategicTickReport warning = Tick(heldState, config, 30);
    REQUIRE(warning.warnings.size() == 1u);
    CHECK(bridge->siegePhase == game::StrategicSiegePhase::Warning);
    const std::int64_t attackMinute = bridge->nextAttackMinute;
    CHECK(attackMinute > 30);

    game::StrategicDetailedResolution held;
    held.regionId = "riverlands";
    held.attackMinute = attackMinute;
    held.resolvedMinute = attackMinute;
    held.held = true;
    held.rating = 'A';
    REQUIRE(game::ResolveStrategicSiege(heldState, config, held, &error));
    CHECK(bridge->owner == game::StrategicOwner::Human);
    CHECK(bridge->siegePhase == game::StrategicSiegePhase::None);

    game::StrategicState lostState;
    REQUIRE(lostState.Initialize(config, &error));
    bridge = lostState.FindRegion("riverlands");
    REQUIRE(bridge != nullptr);
    bridge->pressure = 70.0f;
    REQUIRE(Tick(lostState, config, 30).warnings.size() == 1u);
    const std::int64_t lostAttackMinute = bridge->nextAttackMinute;
    game::StrategicDetailedResolution lost;
    lost.regionId = "riverlands";
    lost.attackMinute = lostAttackMinute;
    lost.resolvedMinute = lostAttackMinute;
    lost.held = false;
    lost.breached = true;
    lost.rating = 'C';
    REQUIRE(game::ResolveStrategicSiege(lostState, config, lost, &error));
    CHECK(bridge->owner == game::StrategicOwner::Infested);
    CHECK(bridge->recoveryStage == game::StrategicRecoveryStage::ClearCore);
    CHECK(bridge->infectionCores > 0);
    CHECK(bridge->population == 0);
    CHECK(bridge->recoveryReadyMinute >= lostAttackMinute);
    const game::StrategicRegionState* village =
        lostState.FindRegion("village_core");
    REQUIRE(village != nullptr);
    CHECK(village->refugees == 1);
}
TEST_CASE("M32-T10 北部诊所第二争夺区：守住/失守/夺回") {
    const std::string source = std::string(ZL_SOURCE_DIR);
    game::StrategicConfig config;
    std::string error;
    REQUIRE(game::LoadStrategicConfig(
        source + "/assets/strategic.json", &config, &error));
    REQUIRE(config.regions.size() == 5u);

    const game::StrategicRegionConfig* def = config.Find("north_fields");
    REQUIRE(def != nullptr);
    CHECK(def->initialOwner == game::StrategicOwner::Contested);
    CHECK(def->contestedFaction == game::ContestedFaction::Raiders);
    CHECK(def->anchorX == doctest::Approx(150.0f));
    CHECK(def->anchorY == doctest::Approx(64.0f));
    CHECK(def->strategicValue == 2);
    CHECK_FALSE(def->remote);

    int humans = 0;
    int infested = 0;
    int contested = 0;
    for (const game::StrategicRegionConfig& region : config.regions) {
        if (region.initialOwner == game::StrategicOwner::Human) {
            ++humans;
        } else if (region.initialOwner == game::StrategicOwner::Infested) {
            ++infested;
        } else if (region.initialOwner == game::StrategicOwner::Contested) {
            ++contested;
        }
    }
    CHECK(humans == 2);
    CHECK(infested == 1);
    CHECK(contested == 2);

    game::StrategicState heldState;
    REQUIRE(heldState.Initialize(config, &error));
    game::StrategicRegionState* clinic = heldState.FindRegion("north_fields");
    REQUIRE(clinic != nullptr);
    CHECK(clinic->owner == game::StrategicOwner::Contested);
    CHECK(clinic->population == 2);
    CHECK(clinic->supply == doctest::Approx(50.0f));
    clinic->pressure = 70.0f;
    const game::StrategicTickReport warning = Tick(heldState, config, 30);
    REQUIRE(warning.warnings.size() == 1u);
    CHECK(clinic->siegePhase == game::StrategicSiegePhase::Warning);
    const std::int64_t attackMinute = clinic->nextAttackMinute;
    CHECK(attackMinute > 30);

    game::StrategicDetailedResolution held;
    held.regionId = "north_fields";
    held.attackMinute = attackMinute;
    held.resolvedMinute = attackMinute;
    held.held = true;
    held.rating = 'A';
    REQUIRE(game::ResolveStrategicSiege(heldState, config, held, &error));
    CHECK(clinic->owner == game::StrategicOwner::Human);
    CHECK(clinic->siegePhase == game::StrategicSiegePhase::None);

    game::StrategicState lostState;
    REQUIRE(lostState.Initialize(config, &error));
    clinic = lostState.FindRegion("north_fields");
    REQUIRE(clinic != nullptr);
    clinic->pressure = 70.0f;
    REQUIRE(Tick(lostState, config, 30).warnings.size() == 1u);
    const std::int64_t lostAttackMinute = clinic->nextAttackMinute;
    game::StrategicDetailedResolution lost;
    lost.regionId = "north_fields";
    lost.attackMinute = lostAttackMinute;
    lost.resolvedMinute = lostAttackMinute;
    lost.held = false;
    lost.breached = true;
    lost.rating = 'C';
    REQUIRE(game::ResolveStrategicSiege(lostState, config, lost, &error));
    CHECK(clinic->owner == game::StrategicOwner::Infested);
    CHECK(clinic->recoveryStage == game::StrategicRecoveryStage::ClearCore);
    CHECK(clinic->infectionCores > 0);
    CHECK(clinic->population == 0);
    CHECK(clinic->recoveryReadyMinute >= lostAttackMinute);
    const game::StrategicRegionState* village =
        lostState.FindRegion("village_core");
    REQUIRE(village != nullptr);
    CHECK(village->refugees == 1);

    const std::int64_t readyMinute = clinic->recoveryReadyMinute;
    REQUIRE(game::ClearStrategicRecoveryCore(
        lostState, config, "north_fields", readyMinute, &error));
    CHECK(clinic->recoveryStage == game::StrategicRecoveryStage::Repair);
    REQUIRE(game::RepairStrategicRecoveryFacility(
        lostState, config, "north_fields", readyMinute, &error));
    CHECK(clinic->recoveryStage ==
          game::StrategicRecoveryStage::Counterattack);

    game::StrategicReclaimResolution reclaim;
    reclaim.regionId = "north_fields";
    reclaim.attackMinute = readyMinute;
    reclaim.resolvedMinute = readyMinute;
    reclaim.held = true;
    reclaim.rating = 'S';
    REQUIRE(game::ResolveStrategicReclaim(lostState, config, reclaim, &error));
    CHECK(clinic->owner == game::StrategicOwner::Contested);
    CHECK(clinic->recoveryStage ==
          game::StrategicRecoveryStage::Restoring);
    const std::int64_t restoreMinute = clinic->recoveryReadyMinute;
    REQUIRE(restoreMinute >= readyMinute);
    for (game::StrategicRegionState& region : lostState.regions) {
        region.pressure = 0.0f;
    }
    const game::StrategicTickReport restored =
        Tick(lostState, config, restoreMinute);
    CHECK(restored.completedRecoveries.size() == 1u);
    CHECK(clinic->owner == game::StrategicOwner::Human);
    CHECK(clinic->recoveryStage == game::StrategicRecoveryStage::None);
}

TEST_CASE("M32-T11 争夺派系：配置、地图与标签") {
    const std::string source = std::string(ZL_SOURCE_DIR);
    game::StrategicConfig config;
    std::string error;
    REQUIRE(game::LoadStrategicConfig(
        source + "/assets/strategic.json", &config, &error));

    const game::StrategicRegionConfig* north = config.Find("north_fields");
    const game::StrategicRegionConfig* river = config.Find("riverlands");
    REQUIRE(north != nullptr);
    REQUIRE(river != nullptr);
    CHECK(north->contestedFaction == game::ContestedFaction::Raiders);
    CHECK(river->contestedFaction == game::ContestedFaction::Infected);
    CHECK(std::string(game::ContestedFactionLabel(
              game::ContestedFaction::Raiders)) == "掠夺者");
    CHECK(std::string(game::ContestedFactionLabel(
              game::ContestedFaction::Infected)) == "感染者");

    game::ContestedFaction parsed = game::ContestedFaction::None;
    CHECK(game::ParseContestedFaction("raiders", &parsed));
    CHECK(parsed == game::ContestedFaction::Raiders);
    CHECK(game::ParseContestedFaction("Infected", &parsed));
    CHECK(parsed == game::ContestedFaction::Infected);
    CHECK_FALSE(game::ParseContestedFaction("unknown", &parsed));

    game::StrategicState state;
    REQUIRE(state.Initialize(config, &error));
    const game::StrategicMapState map =
        game::BuildStrategicMapState(config, state, 30);
    const game::StrategicMapRegion* mapNorth =
        map.FindRegion("north_fields");
    const game::StrategicMapRegion* mapRiver =
        map.FindRegion("riverlands");
    REQUIRE(mapNorth != nullptr);
    REQUIRE(mapRiver != nullptr);
    CHECK(mapNorth->contestedFaction ==
          game::ContestedFaction::Raiders);
    CHECK(mapRiver->contestedFaction ==
          game::ContestedFaction::Infected);
}

TEST_CASE("M32-T12 战略贡献配置：经验池、权重与非法输入") {
    const std::string source = std::string(ZL_SOURCE_DIR);
    game::StrategicConfig config;
    std::string error;
    REQUIRE(game::LoadStrategicConfig(
        source + "/assets/strategic.json", &config, &error));
    CHECK(config.contribution.rewardXp == 120);
    CHECK(config.contribution.donatePerSupply == 1);
    CHECK(config.contribution.clearCore == 20);
    CHECK(config.contribution.repairFacility == 15);
    CHECK(config.contribution.reclaim == 30);
    CHECK(config.contribution.minContribution == 1);

    game::StrategicConfig defaults;
    REQUIRE(game::ParseStrategicConfig(StrategicJson(), "default", &defaults,
                                       &error));
    CHECK(defaults.contribution.rewardXp == 120);

    std::string invalidJson = StrategicJson();
    const std::size_t marker = invalidJson.find("\"regions\"");
    REQUIRE(marker != std::string::npos);
    invalidJson.insert(marker,
                       "\"contribution\": {\"rewardXp\": -1}, ");
    game::StrategicConfig invalid;
    CHECK_FALSE(game::ParseStrategicConfig(invalidJson, "invalid", &invalid,
                                           &error));
    CHECK(error.find("contribution.rewardXp") != std::string::npos);
}

TEST_CASE("M32-T13 战略任务贡献配置与区域归属") {
    const std::string source = std::string(ZL_SOURCE_DIR);
    game::StrategicConfig config;
    std::string error;
    REQUIRE(game::LoadStrategicConfig(
        source + "/assets/strategic.json", &config, &error));
    CHECK(config.contribution.mission == 25);
    const game::StrategicMissionRewardConfig* reward =
        config.economy.FindMission("settlement_supply");
    REQUIRE(reward != nullptr);
    CHECK(reward->regionId == "village_core");

    std::string invalidJson = StrategicJson();
    const std::size_t marker = invalidJson.find("\"regions\"");
    REQUIRE(marker != std::string::npos);
    invalidJson.insert(marker, "\"contribution\": {\"mission\": -1}, ");
    game::StrategicConfig invalid;
    CHECK_FALSE(game::ParseStrategicConfig(invalidJson, "invalid", &invalid,
                                           &error));
    CHECK(error.find("contribution.mission") != std::string::npos);
}

TEST_CASE("M32-T15 守城伤害贡献配置") {
    const std::string source = std::string(ZL_SOURCE_DIR);
    game::StrategicConfig config;
    std::string error;
    REQUIRE(game::LoadStrategicConfig(
        source + "/assets/strategic.json", &config, &error));
    CHECK(config.contribution.defenseDamagePerPoint == 10);

    std::string invalidJson = StrategicJson();
    const std::size_t marker = invalidJson.find("\"regions\"");
    REQUIRE(marker != std::string::npos);
    invalidJson.insert(
        marker, "\"contribution\": {\"defenseDamagePerPoint\": 0}, ");
    game::StrategicConfig invalid;
    CHECK_FALSE(game::ParseStrategicConfig(invalidJson, "invalid", &invalid,
                                           &error));
    CHECK(error.find("contribution.defenseDamagePerPoint") !=
          std::string::npos);
}

TEST_CASE("M32-T24 守卫击杀助攻配置与按伤害分配") {
    const std::string source = std::string(ZL_SOURCE_DIR);
    game::StrategicConfig config;
    std::string error;
    REQUIRE(game::LoadStrategicConfig(
        source + "/assets/strategic.json", &config, &error));
    CHECK(config.contribution.defenseKillAssistPoints == 5);

    std::unordered_map<std::uint64_t, float> damage;
    damage[1] = 75.0f;
    damage[2] = 25.0f;
    const auto points = game::AllocateSiegeKillAssist(damage, 5);
    CHECK(points.at(1) == 4);
    CHECK(points.at(2) == 1);
    CHECK(points.size() == 2);
    CHECK(game::AllocateSiegeKillAssist({}, 5).empty());

    std::string invalidJson = StrategicJson();
    const std::size_t marker = invalidJson.find("\"regions\"");
    REQUIRE(marker != std::string::npos);
    invalidJson.insert(
        marker, "\"contribution\": {\"defenseKillAssistPoints\": -1}, ");
    game::StrategicConfig invalid;
    CHECK_FALSE(game::ParseStrategicConfig(invalidJson, "invalid", &invalid,
                                           &error));
    CHECK(error.find("contribution.defenseKillAssistPoints") !=
          std::string::npos);
}
TEST_CASE("M32-T14 守城在场奖励配置") {
    const std::string source = std::string(ZL_SOURCE_DIR);
    game::StrategicConfig config;
    std::string error;
    REQUIRE(game::LoadStrategicConfig(
        source + "/assets/strategic.json", &config, &error));
    CHECK(config.contribution.defenseRewardXp == 80);

    std::string invalidJson = StrategicJson();
    const std::size_t marker = invalidJson.find("\"regions\"");
    REQUIRE(marker != std::string::npos);
    invalidJson.insert(marker,
                       "\"contribution\": {\"defenseRewardXp\": -1}, ");
    game::StrategicConfig invalid;
    CHECK_FALSE(game::ParseStrategicConfig(invalidJson, "invalid", &invalid,
                                           &error));
    CHECK(error.find("contribution.defenseRewardXp") != std::string::npos);
}
TEST_CASE("M32-T25 战略贡献账本随世界 manifest 持久化") {
    game::StrategicConfig config = MakeConfig();
    game::StrategicState state;
    std::string error;
    REQUIRE(state.Initialize(config, &error));
    state.contributions.push_back(
        game::StrategicContributionRecord{0x111u, "core", 20});
    state.contributions.push_back(
        game::StrategicContributionRecord{0x222u, "farm", 5});

    save::WorldManifest manifest;
    manifest.identity.mapId = "strategic_contribution_test";
    manifest.identity.seed = 11;
    manifest.identity.generatorVersion = 1;
    manifest.identity.mapHash = 0x5678u;
    REQUIRE(game::CaptureStrategicState(state, &manifest, &error));
    CHECK(manifest.version == save::kWorldManifestVersion);
    REQUIRE(manifest.strategicContributions.size() == 2u);

    const std::vector<std::uint8_t> bytes =
        save::EncodeWorldManifest(manifest, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    save::WorldManifest decoded;
    REQUIRE(save::DecodeWorldManifest(bytes, &decoded, &error));
    CHECK(decoded.version == save::kWorldManifestVersion);
    REQUIRE(decoded.strategicContributions.size() == 2u);

    game::StrategicState restored;
    REQUIRE(game::RestoreStrategicState(decoded, config, &restored, &error));
    REQUIRE(restored.contributions.size() == 2u);
    CHECK(restored.contributions[0].stableId == 0x111u);
    CHECK(restored.contributions[0].regionId == "core");
    CHECK(restored.contributions[0].points == 20);
    CHECK(restored.contributions[1].stableId == 0x222u);
    CHECK(restored.contributions[1].regionId == "farm");
    CHECK(restored.contributions[1].points == 5);

    decoded.strategicContributions[0].points = -1;
    CHECK_FALSE(save::ValidateWorldManifest(decoded, &error));
    CHECK(error.find("战略贡献") != std::string::npos);

    save::WorldManifest legacy;
    legacy.version = 4u;
    legacy.identity.mapId = "legacy_v4";
    legacy.identity.seed = 3;
    legacy.identity.generatorVersion = 1;
    legacy.identity.mapHash = 0x9abcu;
    const std::vector<std::uint8_t> legacyBytes =
        save::EncodeWorldManifest(legacy, &error);
    REQUIRE_MESSAGE(!legacyBytes.empty(), error);
    save::WorldManifest legacyDecoded;
    REQUIRE(save::DecodeWorldManifest(legacyBytes, &legacyDecoded, &error));
    CHECK(legacyDecoded.version == 4u);
    CHECK(legacyDecoded.strategicContributions.empty());
}