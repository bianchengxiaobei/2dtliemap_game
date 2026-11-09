#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "net/PlayerState.h"

using namespace zl;

TEST_CASE("M13-T7 玩家状态协议：背包/装备/配方/制作往返") {
    net::PlayerState state;
    state.playerStableId = 42;
    state.serverTick = 1234;
    state.capacityKg = 25.5f;
    state.health = 37.0f;
    state.healthMax = 120.0f;
    state.alive = true;
    state.respawnSeconds = 0.0f;
    state.bag.push_back(net::PlayerStateItem{"bandage", 3, 0.75f});
    state.bag.push_back(net::PlayerStateItem{"axe", 1, 0.5f});
    state.equipment[0].present = true;
    state.equipment[0].item = net::PlayerStateItem{"jacket", 1, 1.0f};
    state.equipment[1].present = true;
    state.equipment[1].item = net::PlayerStateItem{"pistol", 1, 0.8f};
    state.equipment[1].loadedAmmo = 5;
    state.ranged.valid = true;
    state.ranged.loaded = 5;
    state.ranged.magazine = 8;
    state.ranged.reserve = 12;
    state.ranged.reloading = true;
    state.ranged.reloadElapsed = 0.5f;
    state.ranged.reloadDuration = 2.0f;
    state.ranged.reloadAmount = 3;
    state.ranged.shotSerial = 17;
    state.knownRecipes = {"campfire_beans", "field_bandage"};
    state.craftingActive = true;
    state.craftingRecipeId = "field_bandage";
    state.craftingElapsed = 0.25f;
    state.craftingDuration = 0.8f;
    state.craftingToolEfficiency = 0.9f;
    state.batchTotal = 3;
    state.batchCompleted = 1;
    state.craftQueue.push_back(net::PlayerStateCraftQueue{"field_bandage", 2});
    state.statusSequence = 7;
    state.statusTone = 1;
    state.status = "拾取了绷带 ×3";
    state.progressionLevel = 3;
    state.progressionXp = 42.0f;
    state.progressionXpNext = 120.0f;
    state.perkPoints = 1;
    state.skills[0] = net::PlayerStateSkill{"melee", 2, 10.0f, 80.0f};
    state.skills[1] = net::PlayerStateSkill{"building", 1, 5.0f, 60.0f};
    state.skills[2] = net::PlayerStateSkill{"crafting", 0, 3.0f, 60.0f};
    state.skills[3] = net::PlayerStateSkill{"survival", 1, 20.0f, 100.0f};
    state.perks = {"bruiser", "fleet"};
    state.perkChoices = {"builder", "veteran", "learner"};
    state.infectionStage = 2;
    state.infectionProgress = 0.52f;
    state.infectionTimeToNextStage = 120.0f;
    state.infectionSlowStrength = 0.5f;
    state.infectionSlowRemaining = 45.0f;
    state.infectionSlowDuration = 180.0f;
    state.climateBodyC = 35.6f;
    state.climateWetness = 0.42f;
    state.siege.phase = 2;
    state.siege.primaryDirection = 4;
    state.siege.secondaryDirection = 0;
    state.siege.currentDirection = 6;
    state.siege.breached = true;
    state.siege.waveIndex = 1;
    state.siege.waveCount = 3;
    state.siege.tone = 2;
    state.siege.targetValid = true;
    state.siege.targetX = 72.5f;
    state.siege.targetY = 24.5f;
    state.siege.breachValid = true;
    state.siege.breachX = 71.5f;
    state.siege.breachY = 25.5f;
    state.siege.status = "守夜 第 2/3 波 | 来袭 西 已破口";
    state.power.fuelMinutes = 320.0f;
    state.power.load = 2.5f;
    state.power.lowFuel = true;
    state.power.outage = false;
    state.needs.valid = true;
    state.needs.hunger = 0.42f;
    state.needs.thirst = 0.33f;
    state.needs.fatigue = 0.61f;
    state.needs.warning = 1;
    state.food.valid = true;
    state.food.nutrition = 0.35f;
    state.food.poisonRemaining = 12.5f;
    state.food.poisonDamagePerSecond = 0.1f;
    state.vehicle.valid = true;
    state.vehicle.vehicleStableId = 0x1122334455667788ull;
    state.vehicle.driving = true;
    state.vehicle.fuel = 0.42f;
    state.vehicle.condition = 0.63f;
    state.vehicle.vehicleType = 2;
    state.vehicle.heading = 1.0f;
    state.combat.valid = true;
    state.combat.stamina = 62.5f;
    state.combat.staminaMax = 100.0f;
    state.combat.stance = net::kPlayerCombatStanceBlocking;
    state.combat.parryWindowLeft = 0.12f;
    state.combat.guardBreakLeft = 0.3f;
    state.combat.dodgeInvulnLeft = 0.05f;
    state.sleep.phase = net::kPlayerSleepPhaseSleeping;
    state.sleep.tone = 1;
    state.sleep.voted = true;
    state.sleep.yesCount = 2;
    state.sleep.eligibleCount = 2;
    state.sleep.initiator = 7;
    state.sleep.startMinutes = 120.0f;
    state.sleep.targetMinutes = 480.0f;
    state.sleep.voteSeconds = 0.0f;
    state.sleep.progress = 0.25f;
    state.sleep.status = "睡眠中：4 小时 · 1.0/6.0 小时 · ESC 取消";
    state.objective.valid = true;
    state.objective.step = 2;

    std::string error;
    const std::vector<std::uint8_t> bytes = net::EncodePlayerState(state, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    CHECK(bytes.size() <= net::kMaxPlayerStateBytes);

    net::PlayerState decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(bytes, &decoded, &error), error);
    CHECK(decoded.playerStableId == 42);
    CHECK(decoded.serverTick == 1234);
    CHECK(decoded.capacityKg == doctest::Approx(25.5f));
    CHECK(decoded.health == doctest::Approx(37.0f));
    CHECK(decoded.healthMax == doctest::Approx(120.0f));
    CHECK(decoded.alive);
    CHECK(decoded.respawnSeconds == doctest::Approx(0.0f));
    REQUIRE(decoded.bag.size() == 2);
    CHECK(decoded.bag[0].defId == "bandage");
    CHECK(decoded.bag[0].count == 3);
    CHECK(decoded.bag[0].condition == doctest::Approx(0.75f));
    CHECK(decoded.equipment[0].present);
    CHECK(decoded.equipment[0].item.defId == "jacket");
    CHECK(decoded.equipment[1].present);
    CHECK(decoded.equipment[1].item.defId == "pistol");
    CHECK(decoded.equipment[1].loadedAmmo == 5);
    CHECK(decoded.ranged.valid);
    CHECK(decoded.ranged.loaded == 5);
    CHECK(decoded.ranged.magazine == 8);
    CHECK(decoded.ranged.reserve == 12);
    CHECK(decoded.ranged.reloading);
    CHECK(decoded.ranged.reloadElapsed == doctest::Approx(0.5f));
    CHECK(decoded.ranged.reloadDuration == doctest::Approx(2.0f));
    CHECK(decoded.ranged.reloadAmount == 3);
    CHECK(decoded.ranged.shotSerial == 17);
    CHECK(decoded.knownRecipes.size() == 2);
    CHECK(decoded.craftingActive);
    CHECK(decoded.craftingRecipeId == "field_bandage");
    CHECK(decoded.craftingElapsed == doctest::Approx(0.25f));
    CHECK(decoded.craftingDuration == doctest::Approx(0.8f));
    CHECK(decoded.craftingToolEfficiency == doctest::Approx(0.9f));
    CHECK(decoded.batchTotal == 3);
    CHECK(decoded.batchCompleted == 1);
    REQUIRE(decoded.craftQueue.size() == 1);
    CHECK(decoded.craftQueue[0].recipeId == "field_bandage");
    CHECK(decoded.craftQueue[0].count == 2);
    CHECK(decoded.statusSequence == 7);
    CHECK(decoded.statusTone == 1);
    CHECK(decoded.status == "拾取了绷带 ×3");
    CHECK(decoded.progressionLevel == 3);
    CHECK(decoded.progressionXp == doctest::Approx(42.0f));
    CHECK(decoded.progressionXpNext == doctest::Approx(120.0f));
    CHECK(decoded.perkPoints == 1);
    REQUIRE(decoded.skills.size() == 4u);
    CHECK(decoded.skills[0].id == "melee");
    CHECK(decoded.skills[0].level == 2);
    CHECK(decoded.skills[0].xpNext == doctest::Approx(80.0f));
    CHECK(decoded.skills[3].id == "survival");
    REQUIRE(decoded.perks.size() == 2u);
    CHECK(decoded.perks[0] == "bruiser");
    REQUIRE(decoded.perkChoices.size() == 3u);
    CHECK(decoded.perkChoices[2] == "learner");
    CHECK(decoded.infectionStage == 2);
    CHECK(decoded.infectionProgress == doctest::Approx(0.52f));
    CHECK(decoded.infectionTimeToNextStage == doctest::Approx(120.0f));
    CHECK(decoded.infectionSlowStrength == doctest::Approx(0.5f));
    CHECK(decoded.infectionSlowRemaining == doctest::Approx(45.0f));
    CHECK(decoded.infectionSlowDuration == doctest::Approx(180.0f));
    CHECK(decoded.climateBodyC == doctest::Approx(35.6f));
    CHECK(decoded.climateWetness == doctest::Approx(0.42f));
    CHECK(decoded.siege.phase == 2);
    CHECK(decoded.siege.primaryDirection == 4);
    CHECK(decoded.siege.secondaryDirection == 0);
    CHECK(decoded.siege.currentDirection == 6);
    CHECK(decoded.siege.breached);
    CHECK(decoded.siege.waveIndex == 1);
    CHECK(decoded.siege.waveCount == 3);
    CHECK(decoded.siege.tone == 2);
    CHECK(decoded.siege.targetValid);
    CHECK(decoded.siege.targetX == doctest::Approx(72.5f));
    CHECK(decoded.siege.targetY == doctest::Approx(24.5f));
    CHECK(decoded.siege.breachValid);
    CHECK(decoded.siege.breachX == doctest::Approx(71.5f));
    CHECK(decoded.siege.breachY == doctest::Approx(25.5f));
    CHECK(decoded.siege.status == "守夜 第 2/3 波 | 来袭 西 已破口");
    CHECK(decoded.power.fuelMinutes == doctest::Approx(320.0f));
    CHECK(decoded.power.load == doctest::Approx(2.5f));
    CHECK(decoded.power.lowFuel);
    CHECK_FALSE(decoded.power.outage);
    CHECK(decoded.needs.valid);
    CHECK(decoded.needs.hunger == doctest::Approx(0.42f));
    CHECK(decoded.needs.thirst == doctest::Approx(0.33f));
    CHECK(decoded.needs.fatigue == doctest::Approx(0.61f));
    CHECK(decoded.needs.warning == 1);
    CHECK(decoded.food.valid);
    CHECK(decoded.food.nutrition == doctest::Approx(0.35f));
    CHECK(decoded.food.poisonRemaining == doctest::Approx(12.5f));
    CHECK(decoded.food.poisonDamagePerSecond == doctest::Approx(0.1f));
    CHECK(decoded.vehicle.valid);
    CHECK(decoded.vehicle.vehicleStableId == 0x1122334455667788ull);
    CHECK(decoded.vehicle.driving);
    CHECK(decoded.vehicle.fuel == doctest::Approx(0.42f));
    CHECK(decoded.vehicle.condition == doctest::Approx(0.63f));
    CHECK(decoded.vehicle.vehicleType == 2);
    CHECK(decoded.vehicle.heading == doctest::Approx(1.0f));
    CHECK(decoded.combat.valid);
    CHECK(decoded.combat.stamina == doctest::Approx(62.5f));
    CHECK(decoded.combat.staminaMax == doctest::Approx(100.0f));
    CHECK(decoded.combat.stance == net::kPlayerCombatStanceBlocking);
    CHECK(decoded.combat.parryWindowLeft == doctest::Approx(0.12f));
    CHECK(decoded.combat.guardBreakLeft == doctest::Approx(0.3f));
    CHECK(decoded.combat.dodgeInvulnLeft == doctest::Approx(0.05f));
    CHECK(decoded.sleep.phase == net::kPlayerSleepPhaseSleeping);
    CHECK(decoded.sleep.tone == 1);
    CHECK(decoded.sleep.voted);
    CHECK(decoded.sleep.yesCount == 2);
    CHECK(decoded.sleep.eligibleCount == 2);
    CHECK(decoded.sleep.initiator == 7);
    CHECK(decoded.sleep.startMinutes == doctest::Approx(120.0f));
    CHECK(decoded.sleep.targetMinutes == doctest::Approx(480.0f));
    CHECK(decoded.sleep.voteSeconds == doctest::Approx(0.0f));
    CHECK(decoded.sleep.progress == doctest::Approx(0.25f));
    CHECK(decoded.sleep.status == "睡眠中：4 小时 · 1.0/6.0 小时 · ESC 取消");
    CHECK(decoded.objective.valid);
    CHECK(decoded.objective.step == 2);

    // v20 尾部为战斗体力/姿态；剥离后得到 v19 负载。
    const std::size_t combatTail = 1 + 4 + 4 + 1 + 4 + 4 + 4;
    REQUIRE(bytes.size() > combatTail);
    std::vector<std::uint8_t> legacyV19(
        bytes.begin(),
        bytes.end() - static_cast<std::ptrdiff_t>(combatTail));
    legacyV19[4] = 19;
    legacyV19[5] = 0;
    net::PlayerState legacyV19Decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(legacyV19, &legacyV19Decoded,
                                           &error),
                    error);
    CHECK_FALSE(legacyV19Decoded.combat.valid);
    CHECK(legacyV19Decoded.combat.stance == net::kPlayerCombatStanceNone);

    // v19 尾部为载具驾驶/乘客态 + 车型/朝向；再剥离后得到 v16 负载。
    const std::size_t vehicleTail = 1 + 8 + 1 + 1 + 4 + 4 + 1 + 4;
    REQUIRE(legacyV19.size() > vehicleTail);
    std::vector<std::uint8_t> legacyV16(
        legacyV19.begin(),
        legacyV19.end() - static_cast<std::ptrdiff_t>(vehicleTail));
    legacyV16[4] = 16;
    legacyV16[5] = 0;
    net::PlayerState legacyV16Decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(legacyV16, &legacyV16Decoded,
                                           &error),
                    error);
    CHECK_FALSE(legacyV16Decoded.vehicle.valid);
    CHECK(legacyV16Decoded.vehicle.vehicleStableId == 0);
    CHECK_FALSE(legacyV16Decoded.vehicle.driving);
    CHECK(legacyV16Decoded.vehicle.fuel == doctest::Approx(0.0f));
    CHECK(legacyV16Decoded.vehicle.condition == doctest::Approx(0.0f));

    // v16 尾部为聚落状态 + 服务器提示；先剥离后得到 v15 负载。
    const std::size_t settlementTail =
        2 + 4 + 1 + 2 + state.settlementNotice.size();
    REQUIRE(legacyV16.size() > settlementTail);
    std::vector<std::uint8_t> legacyV15(
        legacyV16.begin(),
        legacyV16.end() - static_cast<std::ptrdiff_t>(settlementTail));
    legacyV15[4] = 15;
    legacyV15[5] = 0;
    net::PlayerState legacyV15Decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(legacyV15, &legacyV15Decoded,
                                           &error),
                    error);
    CHECK(legacyV15Decoded.settlements.empty());
    CHECK(legacyV15Decoded.settlementNotice.empty());

    // v15 尾部为食物营养/中毒镜像；剥离后得到 v14 负载。
    const std::size_t foodTail = 13;
    REQUIRE(legacyV15.size() > foodTail);
    std::vector<std::uint8_t> legacyV14(
        legacyV15.begin(),
        legacyV15.end() - static_cast<std::ptrdiff_t>(foodTail));
    legacyV14[4] = 14;
    legacyV14[5] = 0;
    net::PlayerState legacyV14Decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(legacyV14, &legacyV14Decoded, &error),
                    error);
    CHECK_FALSE(legacyV14Decoded.food.valid);

    // v14 尾部为动物剥取动作；再剥离后得到 v13 负载。
    const std::size_t animalTail = 33;
    REQUIRE(legacyV14.size() > animalTail);
    std::vector<std::uint8_t> legacyV13(
        legacyV14.begin(),
        legacyV14.end() - static_cast<std::ptrdiff_t>(animalTail));
    legacyV13[4] = 13;
    legacyV13[5] = 0;

    // v13 尾部为生存动作 + 附近节点/钓点；先剥离后得到 v12 负载。
    const std::size_t survivalTail = 50;
    REQUIRE(legacyV13.size() > survivalTail);
    std::vector<std::uint8_t> legacyV12(
        legacyV13.begin(),
        legacyV13.end() - static_cast<std::ptrdiff_t>(survivalTail));
    legacyV12[4] = 12;
    legacyV12[5] = 0;

    // v12 尾部为 2 个装备弹匣 + 远程状态；v11 旧负载不含该尾部。
    const std::size_t rangedTail = 4 + 22;
    REQUIRE(legacyV12.size() > rangedTail);
    std::vector<std::uint8_t> legacyV11(
        legacyV12.begin(),
        legacyV12.end() - static_cast<std::ptrdiff_t>(rangedTail));
    legacyV11[4] = 11;
    legacyV11[5] = 0;
    net::PlayerState legacyV11Decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(legacyV11, &legacyV11Decoded, &error),
                    error);
    CHECK(legacyV11Decoded.equipment[1].loadedAmmo == 0);
    CHECK_FALSE(legacyV11Decoded.ranged.valid);
    CHECK(legacyV11Decoded.ranged.magazine == 0);

    // v11 尾部为 valid+step；v10 旧负载不含主目标，回退 invalid/0。
    const std::size_t objectiveTail = 2;
    REQUIRE(legacyV11.size() > objectiveTail);
    std::vector<std::uint8_t> legacyV10(
        legacyV11.begin(),
        legacyV11.end() - static_cast<std::ptrdiff_t>(objectiveTail));
    legacyV10[4] = 10;
    legacyV10[5] = 0;
    net::PlayerState legacyV10Decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(legacyV10, &legacyV10Decoded, &error),
                    error);
    CHECK_FALSE(legacyV10Decoded.objective.valid);
    CHECK(legacyV10Decoded.objective.step == 0);

    // v10 尾部为需求 + 睡眠状态；v9 测试先剥离该尾部，再剥离 POI/任务尾部。
    const std::size_t sleepTail =
        14 + 31 + state.sleep.status.size();
    REQUIRE(legacyV10.size() > sleepTail);
    std::vector<std::uint8_t> legacyV9(
        legacyV10.begin(),
        legacyV10.end() - static_cast<std::ptrdiff_t>(sleepTail));
    legacyV9[4] = 9;
    legacyV9[5] = 0;
    net::PlayerState legacyV9Decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(legacyV9, &legacyV9Decoded, &error),
                    error);
    CHECK(legacyV9Decoded.sleep.phase == net::kPlayerSleepPhaseNone);
    CHECK(legacyV9Decoded.sleep.status.empty());

    const std::size_t poiMissionTail = 12;
    REQUIRE(legacyV9.size() > poiMissionTail);
    const std::vector<std::uint8_t> legacyBase(
        legacyV9.begin(),
        legacyV9.end() - static_cast<std::ptrdiff_t>(poiMissionTail));

    // v7 旧负载不含目标/破口坐标尾部；坐标回退默认无效。
    const std::size_t reportTail = 18;
    std::vector<std::uint8_t> legacyV7 = legacyBase;
    REQUIRE(legacyV7.size() > reportTail);
    legacyV7.resize(legacyV7.size() - reportTail);
    legacyV7[4] = 7;
    legacyV7[5] = 0;
    net::PlayerState legacyV7Decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(legacyV7, &legacyV7Decoded, &error),
                    error);
    CHECK(legacyV7Decoded.siege.phase == 2);
    CHECK_FALSE(legacyV7Decoded.siege.targetValid);
    CHECK_FALSE(legacyV7Decoded.siege.breachValid);
    CHECK(legacyV7Decoded.siege.targetX == doctest::Approx(0.0f));
    CHECK(legacyV7Decoded.power.load == doctest::Approx(2.5f));

    // v6 旧负载不含电力/坐标尾部；电力回退 0/无告警。
    const std::size_t powerTail = 10;
    std::vector<std::uint8_t> legacyV6 = legacyBase;
    REQUIRE(legacyV6.size() > powerTail + reportTail);
    legacyV6.resize(legacyV6.size() - powerTail - reportTail);
    legacyV6[4] = 6;
    legacyV6[5] = 0;
    net::PlayerState legacyV6Decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(legacyV6, &legacyV6Decoded, &error),
                    error);
    CHECK(legacyV6Decoded.siege.phase == 2);
    CHECK_FALSE(legacyV6Decoded.siege.targetValid);
    CHECK(legacyV6Decoded.power.fuelMinutes == doctest::Approx(0.0f));
    CHECK(legacyV6Decoded.power.load == doctest::Approx(0.0f));
    CHECK_FALSE(legacyV6Decoded.power.lowFuel);
    CHECK_FALSE(legacyV6Decoded.power.outage);

    // v5 旧负载不含 siege + 电力 + 坐标尾部；守夜/电力回退默认值。
    const std::size_t siegeTail = 10 + state.siege.status.size();
    std::vector<std::uint8_t> legacyV5 = legacyBase;
    REQUIRE(legacyV5.size() > siegeTail + powerTail + reportTail);
    legacyV5.resize(legacyV5.size() - siegeTail - powerTail - reportTail);
    legacyV5[4] = 5;
    legacyV5[5] = 0;
    net::PlayerState legacyV5Decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(legacyV5, &legacyV5Decoded, &error),
                    error);
    CHECK(legacyV5Decoded.siege.phase == 0);
    CHECK(legacyV5Decoded.siege.status.empty());
    CHECK(legacyV5Decoded.power.fuelMinutes == doctest::Approx(0.0f));

    // v4 旧负载不含气候 + siege + 电力 + 坐标尾部；体温/潮湿/守夜/电力回退默认值。
    std::vector<std::uint8_t> legacyV4 = legacyBase;
    REQUIRE(legacyV4.size() > 8 + siegeTail + powerTail + reportTail);
    legacyV4.resize(legacyV4.size() - 8 - siegeTail - powerTail - reportTail);
    legacyV4[4] = 4;
    legacyV4[5] = 0;
    net::PlayerState legacyV4Decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(legacyV4, &legacyV4Decoded, &error),
                    error);
    CHECK(legacyV4Decoded.infectionStage == 2);
    CHECK(legacyV4Decoded.climateBodyC == doctest::Approx(37.0f));
    CHECK(legacyV4Decoded.climateWetness == doctest::Approx(0.0f));
    CHECK(legacyV4Decoded.siege.phase == 0);

    // v3 旧负载不含感染 + 气候 + siege + 电力 + 坐标尾部；全部回退默认值。
    std::vector<std::uint8_t> legacy = legacyBase;
    REQUIRE(legacy.size() > 29 + siegeTail + powerTail + reportTail);
    legacy.resize(legacy.size() - 29 - siegeTail - powerTail - reportTail);
    legacy[4] = 3;
    legacy[5] = 0;
    net::PlayerState legacyDecoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(legacy, &legacyDecoded, &error), error);
    CHECK(legacyDecoded.infectionStage == 0);
    CHECK(legacyDecoded.infectionProgress == doctest::Approx(0.0f));
    CHECK(legacyDecoded.climateBodyC == doctest::Approx(37.0f));
    CHECK(legacyDecoded.climateWetness == doctest::Approx(0.0f));
    CHECK(legacyDecoded.siege.phase == 0);
    CHECK(legacyDecoded.power.fuelMinutes == doctest::Approx(0.0f));
}

TEST_CASE("M13-T7 玩家状态协议：拒绝截断、缺少玩家与超长提示") {
    net::PlayerState state;
    state.playerStableId = 1;
    state.capacityKg = 20.0f;
    std::string error;
    const std::vector<std::uint8_t> bytes = net::EncodePlayerState(state, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);

    net::PlayerState decoded;
    const std::vector<std::uint8_t> truncated(bytes.begin(), bytes.begin() + 12);
    CHECK_FALSE(net::DecodePlayerState(truncated, &decoded, &error));

    net::PlayerState missingPlayer;
    missingPlayer.capacityKg = 20.0f;
    CHECK(net::EncodePlayerState(missingPlayer, &error).empty());

    net::PlayerState longStatus;
    longStatus.playerStableId = 1;
    longStatus.capacityKg = 20.0f;
    longStatus.status.assign(net::kMaxPlayerStateStatusBytes + 1, 'x');
    CHECK(net::EncodePlayerState(longStatus, &error).empty());

    net::PlayerState badItem;
    badItem.playerStableId = 1;
    badItem.capacityKg = 20.0f;
    badItem.bag.push_back(net::PlayerStateItem{"bandage", 0, 1.0f});
    CHECK(net::EncodePlayerState(badItem, &error).empty());

    net::PlayerState badDeath;
    badDeath.playerStableId = 1;
    badDeath.capacityKg = 20.0f;
    badDeath.alive = false;
    badDeath.health = 10.0f;
    CHECK(net::EncodePlayerState(badDeath, &error).empty());

    net::PlayerState badProgress;
    badProgress.playerStableId = 1;
    badProgress.capacityKg = 20.0f;
    badProgress.progressionLevel = 0;
    CHECK(net::EncodePlayerState(badProgress, &error).empty());

    net::PlayerState duplicatePerk;
    duplicatePerk.playerStableId = 1;
    duplicatePerk.capacityKg = 20.0f;
    duplicatePerk.perks = {"fleet", "fleet"};
    CHECK(net::EncodePlayerState(duplicatePerk, &error).empty());

    net::PlayerState badInfection;
    badInfection.playerStableId = 1;
    badInfection.capacityKg = 20.0f;
    badInfection.infectionStage = 4;
    CHECK(net::EncodePlayerState(badInfection, &error).empty());

    net::PlayerState badSlow;
    badSlow.playerStableId = 1;
    badSlow.capacityKg = 20.0f;
    badSlow.infectionStage = 2;
    badSlow.infectionProgress = 0.5f;
    badSlow.infectionSlowRemaining = 20.0f;
    badSlow.infectionSlowDuration = 10.0f;
    CHECK(net::EncodePlayerState(badSlow, &error).empty());

    net::PlayerState badClimate;
    badClimate.playerStableId = 1;
    badClimate.capacityKg = 20.0f;
    badClimate.climateBodyC = 10.0f;
    CHECK(net::EncodePlayerState(badClimate, &error).empty());

    net::PlayerState badSiege;
    badSiege.playerStableId = 1;
    badSiege.capacityKg = 20.0f;
    badSiege.siege.phase = 4;
    CHECK(net::EncodePlayerState(badSiege, &error).empty());

    net::PlayerState badSiegeWave;
    badSiegeWave.playerStableId = 1;
    badSiegeWave.capacityKg = 20.0f;
    badSiegeWave.siege.waveIndex = 2;
    badSiegeWave.siege.waveCount = 1;
    CHECK(net::EncodePlayerState(badSiegeWave, &error).empty());

    net::PlayerState badPower;
    badPower.playerStableId = 1;
    badPower.capacityKg = 20.0f;
    badPower.power.load = -1.0f;
    CHECK(net::EncodePlayerState(badPower, &error).empty());

    net::PlayerState badBreachPoint;
    badBreachPoint.playerStableId = 1;
    badBreachPoint.capacityKg = 20.0f;
    badBreachPoint.siege.breachValid = true;
    badBreachPoint.siege.breached = true;
    badBreachPoint.siege.breachX = 1.0e7f;
    CHECK(net::EncodePlayerState(badBreachPoint, &error).empty());

    net::PlayerState breachWithoutFlag;
    breachWithoutFlag.playerStableId = 1;
    breachWithoutFlag.capacityKg = 20.0f;
    breachWithoutFlag.siege.breachValid = true;
    CHECK(net::EncodePlayerState(breachWithoutFlag, &error).empty());

    net::PlayerState badFoodNutrition;
    badFoodNutrition.playerStableId = 1;
    badFoodNutrition.capacityKg = 20.0f;
    badFoodNutrition.food.valid = true;
    badFoodNutrition.food.nutrition = 1.5f;
    CHECK(net::EncodePlayerState(badFoodNutrition, &error).empty());

    net::PlayerState badFoodPoison;
    badFoodPoison.playerStableId = 1;
    badFoodPoison.capacityKg = 20.0f;
    badFoodPoison.food.valid = true;
    badFoodPoison.food.poisonRemaining = -1.0f;
    CHECK(net::EncodePlayerState(badFoodPoison, &error).empty());

    net::PlayerState strayFood;
    strayFood.playerStableId = 1;
    strayFood.capacityKg = 20.0f;
    strayFood.food.nutrition = 0.2f;
    CHECK(net::EncodePlayerState(strayFood, &error).empty());
}

TEST_CASE("M23-F1-T7 玩家状态协议：弹匣/备弹/换弹进度与非法值") {
    net::PlayerState state;
    state.playerStableId = 9;
    state.capacityKg = 20.0f;
    state.equipment[0].present = true;
    state.equipment[0].item = net::PlayerStateItem{"pistol", 1, 0.9f};
    state.equipment[0].loadedAmmo = 6;
    state.ranged.valid = true;
    state.ranged.loaded = 6;
    state.ranged.magazine = 8;
    state.ranged.reserve = 10;
    state.ranged.reloading = true;
    state.ranged.reloadElapsed = 1.0f;
    state.ranged.reloadDuration = 2.0f;
    state.ranged.reloadAmount = 2;
    state.ranged.shotSerial = 3;

    std::string error;
    const std::vector<std::uint8_t> bytes = net::EncodePlayerState(state, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::PlayerState decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(bytes, &decoded, &error), error);
    CHECK(decoded.equipment[0].loadedAmmo == 6);
    CHECK(decoded.ranged.loaded == 6);
    CHECK(decoded.ranged.reserve == 10);
    CHECK(decoded.ranged.reloading);
    CHECK(decoded.ranged.reloadElapsed == doctest::Approx(1.0f));
    CHECK(decoded.ranged.reloadDuration == doctest::Approx(2.0f));
    CHECK(decoded.ranged.reloadAmount == 2);
    CHECK(decoded.ranged.shotSerial == 3);

    net::PlayerState negativeAmmo = state;
    negativeAmmo.equipment[0].loadedAmmo = -1;
    CHECK(net::EncodePlayerState(negativeAmmo, &error).empty());

    net::PlayerState overMagazine = state;
    overMagazine.ranged.loaded = 9;
    CHECK(net::EncodePlayerState(overMagazine, &error).empty());

    net::PlayerState missingDuration = state;
    missingDuration.ranged.reloadDuration = 0.0f;
    missingDuration.ranged.reloadElapsed = 0.0f;
    CHECK(net::EncodePlayerState(missingDuration, &error).empty());

    net::PlayerState staleReload = state;
    staleReload.ranged.reloading = false;
    CHECK(net::EncodePlayerState(staleReload, &error).empty());
}

TEST_CASE("M19-F2/F3 玩家状态协议：POI 进度与任务日志往返") {
    net::PlayerState state;
    state.playerStableId = 7;
    state.capacityKg = 20.0f;
    state.poiProgress.push_back(net::PlayerPoiProgress{
        "gas_station", true, true, 0});
    state.poiProgress.push_back(net::PlayerPoiProgress{
        "clinic", false, true, 2});
    state.missionLastDay = 3;
    state.missionRandomDay = 2;
    state.missions.push_back(net::PlayerMissionProgress{
        "radio_signal", {1}, 1, 1, true});
    state.missions.push_back(net::PlayerMissionProgress{
        "daily_patrol", {2}, 0, 3, false});

    std::string error;
    const std::vector<std::uint8_t> bytes = net::EncodePlayerState(state, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::PlayerState decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(bytes, &decoded, &error), error);
    REQUIRE(decoded.poiProgress.size() == 2u);
    CHECK(decoded.poiProgress[0].id == "gas_station");
    CHECK(decoded.poiProgress[0].completed);
    CHECK(decoded.poiProgress[1].liveGuards == 2);
    CHECK(decoded.missionLastDay == 3);
    CHECK(decoded.missionRandomDay == 2);
    REQUIRE(decoded.missions.size() == 2u);
    CHECK(decoded.missions[0].id == "radio_signal");
    CHECK(decoded.missions[0].status == 1);
    CHECK(decoded.missions[0].progress == std::vector<int>{1});
    CHECK(decoded.missions[1].day == 3);
    CHECK_FALSE(decoded.missions[1].rewardClaimed);
}

TEST_CASE("M22-F1 玩家状态协议：拒绝非法睡眠状态") {
    net::PlayerState badPhase;
    badPhase.playerStableId = 1;
    badPhase.capacityKg = 20.0f;
    badPhase.sleep.phase = 3;
    CHECK(net::EncodePlayerState(badPhase).empty());

    net::PlayerState missingInitiator;
    missingInitiator.playerStableId = 1;
    missingInitiator.capacityKg = 20.0f;
    missingInitiator.sleep.phase = net::kPlayerSleepPhaseVote;
    missingInitiator.sleep.yesCount = 1;
    missingInitiator.sleep.eligibleCount = 2;
    CHECK(net::EncodePlayerState(missingInitiator).empty());

    net::PlayerState notAllVoted;
    notAllVoted.playerStableId = 1;
    notAllVoted.capacityKg = 20.0f;
    notAllVoted.sleep.phase = net::kPlayerSleepPhaseSleeping;
    notAllVoted.sleep.initiator = 1;
    notAllVoted.sleep.startMinutes = 0.0f;
    notAllVoted.sleep.targetMinutes = 60.0f;
    notAllVoted.sleep.yesCount = 1;
    notAllVoted.sleep.eligibleCount = 2;
    CHECK(net::EncodePlayerState(notAllVoted).empty());
}

TEST_CASE("M23-F2a-T6 玩家状态生存动作/节点复制") {
    net::PlayerState state;
    state.playerStableId = 77;
    state.forageNodes.push_back(net::PlayerForageNodeState{
        "meadow_grass", 10, 12, 0, false, 4.5f, 2.0f});
    state.fishingSpots.push_back(net::PlayerFishingSpotState{
        "still_water", 20, 21, 0, 21, 21, 0, 5.0f});
    state.survival.mode = net::kPlayerSurvivalModeFishing;
    state.survival.phase = net::kPlayerSurvivalPhaseBiting;
    state.survival.name = "静水岸边";
    state.survival.tableId = "fish_water";
    state.survival.spotId = "still_water";
    state.survival.x = 20;
    state.survival.y = 21;
    state.survival.z = 0;
    state.survival.waterX = 21;
    state.survival.waterY = 21;
    state.survival.waterZ = 0;
    state.survival.progress = 1.0f;
    state.survival.duration = 5.0f;
    state.survival.biteRemaining = 1.2f;

    std::string error;
    const std::vector<std::uint8_t> bytes =
        net::EncodePlayerState(state, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::PlayerState decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(bytes, &decoded, &error), error);
    REQUIRE(decoded.forageNodes.size() == 1u);
    CHECK(decoded.forageNodes[0].zoneId == "meadow_grass");
    CHECK(decoded.forageNodes[0].x == 10);
    CHECK_FALSE(decoded.forageNodes[0].ready);
    CHECK(decoded.forageNodes[0].respawnMinutes == doctest::Approx(4.5f));
    REQUIRE(decoded.fishingSpots.size() == 1u);
    CHECK(decoded.fishingSpots[0].spotId == "still_water");
    CHECK(decoded.fishingSpots[0].waterX == 21);
    CHECK(decoded.survival.mode == net::kPlayerSurvivalModeFishing);
    CHECK(decoded.survival.phase == net::kPlayerSurvivalPhaseBiting);
    CHECK(decoded.survival.name == "静水岸边");
    CHECK(decoded.survival.progress == doctest::Approx(1.0f));
    CHECK(decoded.survival.biteRemaining == doctest::Approx(1.2f));
}

TEST_CASE("M23-F2b-T6 玩家状态动物剥取动作复制") {
    net::PlayerState state;
    state.playerStableId = 78;
    state.animalHarvest.active = true;
    state.animalHarvest.corpseStableId = 9001;
    state.animalHarvest.speciesId = "deer";
    state.animalHarvest.name = "鹿尸体";
    state.animalHarvest.x = 12;
    state.animalHarvest.y = 13;
    state.animalHarvest.z = 0;
    state.animalHarvest.progress = 0.5f;
    state.animalHarvest.duration = 1.5f;

    std::string error;
    const std::vector<std::uint8_t> bytes =
        net::EncodePlayerState(state, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::PlayerState decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(bytes, &decoded, &error), error);
    CHECK(decoded.animalHarvest.active);
    CHECK(decoded.animalHarvest.corpseStableId == 9001);
    CHECK(decoded.animalHarvest.speciesId == "deer");
    CHECK(decoded.animalHarvest.name == "鹿尸体");
    CHECK(decoded.animalHarvest.progress == doctest::Approx(0.5f));
    CHECK(decoded.animalHarvest.duration == doctest::Approx(1.5f));

    state.animalHarvest.corpseStableId = 0;
    CHECK(net::EncodePlayerState(state).empty());
}


TEST_CASE("M23-F4-T6 玩家状态协议：聚落权威状态往返") {
    net::PlayerState state;
    state.playerStableId = 7;
    state.settlements.push_back(net::PlayerSettlementProgress{
        "village_haven", false, true, 73.0f, 120.0f});
    state.settlements.push_back(net::PlayerSettlementProgress{
        "fallen_camp", true, false, 0.0f, 90.0f});
    state.settlementNoticeSerial = 42;
    state.settlementNoticeTone = 2;
    state.settlementNotice = "村庄营地失守：守卫倒下，交易与委托暂停";

    std::string error;
    const std::vector<std::uint8_t> bytes =
        net::EncodePlayerState(state, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::PlayerState decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(bytes, &decoded, &error), error);
    REQUIRE(decoded.settlements.size() == 2u);
    CHECK(decoded.settlements[0].id == "village_haven");
    CHECK_FALSE(decoded.settlements[0].fallen);
    CHECK(decoded.settlements[0].raidActive);
    CHECK(decoded.settlements[0].guardHealth == doctest::Approx(73.0f));
    CHECK(decoded.settlements[0].guardHealthMax == doctest::Approx(120.0f));
    CHECK(decoded.settlements[1].id == "fallen_camp");
    CHECK(decoded.settlements[1].fallen);
    CHECK_FALSE(decoded.settlements[1].raidActive);
    CHECK(decoded.settlementNoticeSerial == 42);
    CHECK(decoded.settlementNoticeTone == 2);
    CHECK(decoded.settlementNotice ==
          "村庄营地失守：守卫倒下，交易与委托暂停");

    net::PlayerState invalid = state;
    invalid.settlements[0].fallen = true;
    error.clear();
    CHECK(net::EncodePlayerState(invalid, &error).empty());
    CHECK_FALSE(error.empty());
}

TEST_CASE("M25-T3 玩家状态协议：载具驾驶态往返与校验") {
    net::PlayerState state;
    state.playerStableId = 7;
    state.vehicle.valid = true;
    state.vehicle.vehicleStableId = 0xABCDEF0123456789ull;
    state.vehicle.driving = true;
    state.vehicle.fuel = 0.55f;
    state.vehicle.condition = 0.75f;

    std::string error;
    const std::vector<std::uint8_t> bytes =
        net::EncodePlayerState(state, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::PlayerState decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(bytes, &decoded, &error), error);
    CHECK(decoded.vehicle.valid);
    CHECK(decoded.vehicle.vehicleStableId == 0xABCDEF0123456789ull);
    CHECK(decoded.vehicle.driving);
    CHECK(decoded.vehicle.fuel == doctest::Approx(0.55f));
    CHECK(decoded.vehicle.condition == doctest::Approx(0.75f));

    net::PlayerState invalid = state;
    invalid.vehicle.vehicleStableId = 0;
    CHECK(net::EncodePlayerState(invalid, &error).empty());

    invalid = state;
    invalid.vehicle.fuel = 1.01f;
    CHECK(net::EncodePlayerState(invalid, &error).empty());

    invalid = state;
    invalid.vehicle.valid = false;
    CHECK(net::EncodePlayerState(invalid, &error).empty());

    state.vehicle.valid = false;
    state.vehicle.vehicleStableId = 0;
    state.vehicle.driving = false;
    state.vehicle.fuel = 0.0f;
    state.vehicle.condition = 0.0f;
    CHECK(!net::EncodePlayerState(state, &error).empty());
}

TEST_CASE("M25-T4-V22 玩家状态协议：乘客位往返与座位互斥") {
    net::PlayerState state;
    state.playerStableId = 8;
    state.vehicle.valid = true;
    state.vehicle.vehicleStableId = 0x1234567890ABCDEFull;
    state.vehicle.passenger = true;
    state.vehicle.fuel = 0.42f;
    state.vehicle.condition = 0.63f;

    std::string error;
    const std::vector<std::uint8_t> bytes =
        net::EncodePlayerState(state, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::PlayerState decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(bytes, &decoded, &error), error);
    CHECK(decoded.vehicle.valid);
    CHECK(decoded.vehicle.vehicleStableId == 0x1234567890ABCDEFull);
    CHECK_FALSE(decoded.vehicle.driving);
    CHECK(decoded.vehicle.passenger);
    CHECK(decoded.vehicle.fuel == doctest::Approx(0.42f));
    CHECK(decoded.vehicle.condition == doctest::Approx(0.63f));

    net::PlayerState invalid = state;
    invalid.vehicle.driving = true;
    CHECK(net::EncodePlayerState(invalid, &error).empty());

    invalid = state;
    invalid.vehicle.valid = false;
    CHECK(net::EncodePlayerState(invalid, &error).empty());

    invalid = state;
    invalid.vehicle.passenger = false;
    CHECK(!net::EncodePlayerState(invalid, &error).empty());
}

TEST_CASE("M30-T6 玩家状态协议：战斗体力/姿态往返与非法值") {
    net::PlayerState state;
    state.playerStableId = 9;
    state.combat.valid = true;
    state.combat.stamina = 37.5f;
    state.combat.staminaMax = 100.0f;
    state.combat.stance = net::kPlayerCombatStanceDodge;
    state.combat.dodgeInvulnLeft = 0.12f;

    std::string error;
    const std::vector<std::uint8_t> bytes =
        net::EncodePlayerState(state, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::PlayerState decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(bytes, &decoded, &error), error);
    CHECK(decoded.combat.valid);
    CHECK(decoded.combat.stamina == doctest::Approx(37.5f));
    CHECK(decoded.combat.staminaMax == doctest::Approx(100.0f));
    CHECK(decoded.combat.stance == net::kPlayerCombatStanceDodge);
    CHECK(decoded.combat.dodgeInvulnLeft == doctest::Approx(0.12f));

    net::PlayerState invalid = state;
    invalid.combat.stamina = 101.0f;
    CHECK(net::EncodePlayerState(invalid, &error).empty());

    invalid = state;
    invalid.combat.stance = 3;
    CHECK(net::EncodePlayerState(invalid, &error).empty());

    invalid = state;
    invalid.combat.valid = false;
    CHECK(net::EncodePlayerState(invalid, &error).empty());
}
