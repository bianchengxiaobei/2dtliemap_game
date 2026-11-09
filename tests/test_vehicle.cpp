#include <doctest/doctest.h>

#include <cmath>
#include <string>

#include "game/VehicleSpike.h"

using namespace zl;

TEST_CASE("VehicleStep：加速到限速并消耗燃料") {
    game::VehicleMotion vehicle;
    vehicle.x = 1.0f;
    vehicle.y = 1.0f;

    for (int i = 0; i < 120; ++i) {
        game::VehicleStep(&vehicle, 1.0f, 0.0f, 1.0f / 60.0f,
                          [](int, int, int) { return false; });
    }

    CHECK(vehicle.x > 10.0f);
    CHECK(vehicle.y == doctest::Approx(1.0f));
    CHECK(game::VehicleSpeed(vehicle) == doctest::Approx(game::kVehicleMaxSpeed));
    CHECK(vehicle.fuel < 1.0f);
    CHECK(vehicle.fuel > 0.95f);
}

TEST_CASE("VehicleStep：松键制动并保留滑行") {
    game::VehicleMotion vehicle;
    vehicle.x = 0.5f;
    vehicle.vx = game::kVehicleMaxSpeed;

    for (int i = 0; i < 30; ++i) {
        game::VehicleStep(&vehicle, 0.0f, 0.0f, 1.0f / 60.0f,
                          [](int, int, int) { return false; });
    }
    CHECK(game::VehicleSpeed(vehicle) > 0.0f);
    CHECK(vehicle.x > 0.5f);

    for (int i = 0; i < 120; ++i) {
        game::VehicleStep(&vehicle, 0.0f, 0.0f, 1.0f / 60.0f,
                          [](int, int, int) { return false; });
    }
    CHECK(game::VehicleSpeed(vehicle) == doctest::Approx(0.0f));
    CHECK(vehicle.x > 1.0f);
    CHECK(vehicle.fuel < 1.0f);
}

TEST_CASE("VehicleStep：撞墙停止且不穿墙") {
    game::VehicleMotion vehicle;
    vehicle.x = 0.5f;
    vehicle.y = 0.5f;

    const auto blocked = [](int x, int, int) { return x >= 2; };
    for (int i = 0; i < 120; ++i) {
        game::VehicleStep(&vehicle, 1.0f, 0.0f, 1.0f / 60.0f, blocked);
    }

    CHECK(vehicle.x < 2.0f);
    CHECK(vehicle.vx == doctest::Approx(0.0f));
    CHECK(vehicle.vy == doctest::Approx(0.0f));
    CHECK(vehicle.y == doctest::Approx(0.5f));
}

TEST_CASE("VehicleStep：大位移目标仍不穿墙") {
    game::VehicleMotion vehicle;
    vehicle.x = 0.5f;
    vehicle.y = 0.5f;
    vehicle.vx = game::kVehicleMaxSpeed;

    game::VehicleStep(&vehicle, 1.0f, 0.0f, 0.2f,
                      [](int x, int, int) { return x >= 2; });

    CHECK(vehicle.x == doctest::Approx(0.5f));
    CHECK(game::VehicleSpeed(vehicle) == doctest::Approx(0.0f));
}

TEST_CASE("VehicleStep：无燃料不能加速") {
    game::VehicleMotion vehicle;
    vehicle.fuel = 0.0f;

    for (int i = 0; i < 60; ++i) {
        game::VehicleStep(&vehicle, 1.0f, 0.0f, 1.0f / 60.0f,
                          [](int, int, int) { return false; });
    }

    CHECK(vehicle.x == doctest::Approx(0.0f));
    CHECK(vehicle.vx == doctest::Approx(0.0f));
    CHECK(vehicle.fuel == doctest::Approx(0.0f));
}

TEST_CASE("VehicleCanEnter：距离边界") {
    game::VehicleMotion vehicle;
    CHECK(game::VehicleCanEnter(vehicle, game::kVehicleEnterRange, 0.0f));
    CHECK_FALSE(game::VehicleCanEnter(vehicle, 2.21f, 0.0f));
    CHECK_FALSE(game::VehicleCanEnter(vehicle, 0.0f, 0.0f, -1.0f));
}

TEST_CASE("M25-T3 VehicleSeatAvailable：空驾驶位可占用") {
    CHECK(game::VehicleSeatAvailable(0));
    CHECK_FALSE(game::VehicleSeatAvailable(42));
}

TEST_CASE("M25-T4-V22 VehicleSeat：驾驶员/乘客进入选择") {
    CHECK(game::VehiclePassengerSeatAvailable(0));
    CHECK(game::VehiclePassengerSeatAvailable(0, 7));
    CHECK(game::VehiclePassengerSeatAvailable(42, 0));
    CHECK_FALSE(game::VehiclePassengerSeatAvailable(42, 99));
    CHECK(game::VehicleSeatForEnter(0, 0) == game::VehicleSeat::Driver);
    CHECK(game::VehicleSeatForEnter(0, 42) == game::VehicleSeat::Driver);
    CHECK(game::VehicleSeatForEnter(42, 0) == game::VehicleSeat::Passenger);
    CHECK(game::VehicleSeatForEnter(42, 99) ==
          game::VehicleSeat::Passenger);
    CHECK(game::VehicleSeatForEnter(42, 99, 7) ==
          game::VehicleSeat::None);
    CHECK(game::VehicleSeatForEnter(0, 0) != game::VehicleSeat::None);
}

TEST_CASE("M25-T4-V27 VehicleSeat：双乘客位进入选择") {
    CHECK(game::VehicleSeatForEnter(7, 0) == game::VehicleSeat::Passenger);
    CHECK(game::VehicleSeatForEnter(7, 42) == game::VehicleSeat::Passenger);
    CHECK(game::VehicleSeatForEnter(7, 42, 99) == game::VehicleSeat::None);
    CHECK(game::VehicleCanTakeDriverSeat(0, 42, 99, 99));
    CHECK(game::VehicleCanTakeDriverSeat(0, 42, 99, 42));
    CHECK_FALSE(game::VehicleCanTakeDriverSeat(7, 42, 99, 99));
    CHECK_FALSE(game::VehicleCanTakeDriverSeat(0, 0, 0, 42));
    CHECK(game::VehicleCanGiveDriverSeatWithSecondPassenger(7, 42, 99, 7,
                                                            0.0f));
    CHECK(game::VehicleCanGiveDriverSeatWithSecondPassenger(7, 0, 99, 7, 0.0f));
    CHECK_FALSE(game::VehicleCanGiveDriverSeatWithSecondPassenger(7, 0, 0, 7,
                                                                 0.0f));
    CHECK_FALSE(game::VehicleCanGiveDriverSeatWithSecondPassenger(7, 7, 99, 7,
                                                                 0.0f));
}

TEST_CASE("M25-T4-V23 VehicleDriverSeat：空驾驶位可由乘客接管") {
    CHECK(game::VehicleCanTakeDriverSeat(0, 42, 42));
    CHECK_FALSE(game::VehicleCanTakeDriverSeat(7, 42, 42));
    CHECK_FALSE(game::VehicleCanTakeDriverSeat(0, 0, 42));
    CHECK_FALSE(game::VehicleCanTakeDriverSeat(0, 42, 0));
    CHECK_FALSE(game::VehicleCanTakeDriverSeat(0, 42, 99));
    CHECK(game::VehicleCanTakeDriverSeat(0, 42, 42) !=
          game::VehicleCanTakeDriverSeat(7, 42, 42));
}

TEST_CASE("M25-T4-V24 VehicleDriverHandoff：停车让座条件") {
    CHECK(game::VehicleCanGiveDriverSeat(7, 42, 7, 0.0f));
    CHECK(game::VehicleCanGiveDriverSeat(
        7, 42, 7, game::kVehicleSeatHandoffMaxSpeed));
    CHECK(game::VehicleCanGiveDriverSeat(7, 42, 7, 0.0f, 0.0f));
    CHECK_FALSE(game::VehicleCanGiveDriverSeat(7, 42, 7, 0.11f));
    CHECK_FALSE(game::VehicleCanGiveDriverSeat(7, 0, 7, 0.0f));
    CHECK_FALSE(game::VehicleCanGiveDriverSeat(7, 7, 7, 0.0f));
    CHECK_FALSE(game::VehicleCanGiveDriverSeat(7, 42, 42, 0.0f));
    CHECK_FALSE(game::VehicleCanGiveDriverSeat(0, 42, 7, 0.0f));
    CHECK_FALSE(game::VehicleCanGiveDriverSeat(7, 42, 7, NAN));
    CHECK_FALSE(game::VehicleCanGiveDriverSeat(7, 42, 7, -0.1f));
    CHECK_FALSE(game::VehicleCanGiveDriverSeat(7, 42, 7, 0.01f, 0.0f));
}

TEST_CASE("M25-T3 FindVehicleExitCell：就近可行走格") {
    const auto open = [](int, int) { return true; };
    float x = 0.0f;
    float y = 0.0f;
    CHECK(game::FindVehicleExitCell(5.5f, 5.5f, open, &x, &y));
    CHECK(x == doctest::Approx(4.5f));
    CHECK(y == doctest::Approx(4.5f));

    const auto onlyFar = [](int gx, int gy) { return gx == 7 && gy == 6; };
    CHECK(game::FindVehicleExitCell(5.5f, 5.5f, onlyFar, &x, &y, 3));
    CHECK(x == doctest::Approx(7.5f));
    CHECK(y == doctest::Approx(6.5f));

    const auto blocked = [](int, int) { return false; };
    CHECK_FALSE(game::FindVehicleExitCell(5.5f, 5.5f, blocked, &x, &y));
    CHECK_FALSE(game::FindVehicleExitCell(5.5f, 5.5f, open, nullptr, &y));
    CHECK_FALSE(game::FindVehicleExitCell(5.5f, 5.5f, open, &x, &y, 0));
}

TEST_CASE("M25-T3 VehiclePredictStep：预测不消耗燃料/车况") {
    game::VehicleMotion predicted;
    predicted.x = 0.5f;
    predicted.y = 0.5f;
    predicted.fuel = 0.6f;
    predicted.condition = 0.7f;
    predicted.crashLatched = true;

    for (int i = 0; i < 60; ++i) {
        const game::VehicleStepResult result = game::VehiclePredictStep(
            &predicted, 1.0f, 0.0f, 1.0f / 60.0f,
            [](int, int, int) { return false; });
        CHECK_FALSE(result.damaged);
        CHECK(result.damage == doctest::Approx(0.0f));
    }
    CHECK(predicted.x > 2.0f);
    CHECK(predicted.fuel == doctest::Approx(0.6f));
    CHECK(predicted.condition == doctest::Approx(0.7f));
    CHECK(predicted.crashLatched);
}

TEST_CASE("M25-T3 VehicleCorrectPosition：混合/吸附/非法快照") {
    game::VehicleMotion predicted;
    CHECK_FALSE(game::VehicleCorrectPosition(&predicted, 0.4f, 0.4f, 1.0f, -1.0f,
                                             1.0f, 0.5f));
    CHECK(predicted.x == doctest::Approx(0.2f));
    CHECK(predicted.y == doctest::Approx(0.2f));
    CHECK(predicted.vx == doctest::Approx(1.0f));
    CHECK(predicted.vy == doctest::Approx(-1.0f));

    CHECK(game::VehicleCorrectPosition(&predicted, 3.0f, 0.2f, 2.0f, 0.0f,
                                       1.0f, 0.5f));
    CHECK(predicted.x == doctest::Approx(3.0f));
    CHECK(predicted.y == doctest::Approx(0.2f));
    CHECK(predicted.vx == doctest::Approx(2.0f));

    const game::VehicleMotion before = predicted;
    CHECK_FALSE(game::VehicleCorrectPosition(&predicted, NAN, 0.0f, 0.0f, 0.0f));
    CHECK(predicted.x == before.x);
    CHECK(predicted.y == before.y);
}

TEST_CASE("FindVehicleSpawnCell：3×3 空地搜索") {
    const auto open = [](int, int) { return true; };
    int x = -99;
    int y = -99;
    CHECK(game::FindVehicleSpawnCell(0, 0, 4, open, &x, &y));
    CHECK(x == 0);
    CHECK(y == 0);

    const auto blockedCenter = [](int gx, int gy) { return !(gx == 0 && gy == 0); };
    CHECK(game::FindVehicleSpawnCell(0, 0, 4, blockedCenter, &x, &y));
    CHECK(x == -2);
    CHECK(y == -2);
    CHECK(game::VehicleAreaWalkable(x, y, blockedCenter));

    const auto wall = [](int gx, int) { return gx == 0; };
    CHECK_FALSE(game::FindVehicleSpawnCell(0, 0, 4, wall, &x, &y));

    const auto blocked = [](int, int) { return false; };
    CHECK_FALSE(game::FindVehicleSpawnCell(0, 0, 4, blocked, &x, &y));
}


TEST_CASE("Vehicle crash damage applies once per contact") {
    game::VehicleMotion vehicle;
    vehicle.x = 0.5f;
    vehicle.y = 0.5f;
    vehicle.vx = game::kVehicleMaxSpeed;

    const auto blocked = [](int x, int, int) { return x >= 2; };
    const game::VehicleStepResult first =
        game::VehicleStep(&vehicle, 1.0f, 0.0f, 0.2f, blocked);

    REQUIRE(first.collided);
    REQUIRE(first.damaged);
    CHECK(first.impactSpeed == doctest::Approx(game::kVehicleMaxSpeed));
    CHECK(first.damage == doctest::Approx(game::kVehicleMaxCrashDamage));
    CHECK(vehicle.condition == doctest::Approx(1.0f - game::kVehicleMaxCrashDamage));
    CHECK(vehicle.crashLatched);

    const float conditionAfterFirst = vehicle.condition;
    vehicle.vx = game::kVehicleMaxSpeed;
    const game::VehicleStepResult second =
        game::VehicleStep(&vehicle, 1.0f, 0.0f, 0.2f, blocked);
    CHECK(second.collided);
    CHECK_FALSE(second.damaged);
    CHECK(vehicle.condition == doctest::Approx(conditionAfterFirst));
}

TEST_CASE("Vehicle low speed impact does not damage") {
    game::VehicleMotion vehicle;
    vehicle.x = 1.3f;
    vehicle.y = 0.5f;
    vehicle.vx = 2.0f;

    const auto blocked = [](int x, int, int) { return x >= 2; };
    const game::VehicleStepResult result =
        game::VehicleStep(&vehicle, 0.0f, 0.0f, 0.05f, blocked);

    CHECK(result.collided);
    CHECK_FALSE(result.damaged);
    CHECK(vehicle.condition == doctest::Approx(1.0f));
}

TEST_CASE("Vehicle zero condition cannot accelerate") {
    game::VehicleMotion vehicle;
    vehicle.condition = 0.0f;

    for (int i = 0; i < 60; ++i) {
        game::VehicleStep(&vehicle, 1.0f, 0.0f, 1.0f / 60.0f,
                          [](int, int, int) { return false; });
    }

    CHECK(vehicle.x == doctest::Approx(0.0f));
    CHECK(vehicle.vx == doctest::Approx(0.0f));
    CHECK(vehicle.fuel == doctest::Approx(1.0f));
}

TEST_CASE("Vehicle repair and fuel helpers clamp to full") {
    game::VehicleMotion vehicle;
    vehicle.condition = 0.2f;
    vehicle.fuel = 0.2f;

    CHECK(game::VehicleAddCondition(&vehicle, 0.4f));
    CHECK(vehicle.condition == doctest::Approx(0.6f));
    CHECK(game::VehicleAddCondition(&vehicle, 0.4f));
    CHECK(vehicle.condition == doctest::Approx(1.0f));
    CHECK_FALSE(game::VehicleAddCondition(&vehicle, 0.4f));

    CHECK(game::VehicleAddFuel(&vehicle, 0.5f));
    CHECK(vehicle.fuel == doctest::Approx(0.7f));
    CHECK(game::VehicleAddFuel(&vehicle, 0.5f));
    CHECK(vehicle.fuel == doctest::Approx(1.0f));
    CHECK_FALSE(game::VehicleAddFuel(&vehicle, 0.5f));
}

TEST_CASE("Vehicle tuning controls acceleration and top speed") {
    game::VehicleMotion vehicle;
    game::VehicleTuning tuning;
    tuning.maxSpeed = 4.0f;
    tuning.acceleration = 100.0f;

    for (int i = 0; i < 10; ++i) {
        game::VehicleStep(&vehicle, 1.0f, 0.0f, 0.1f,
                          [](int, int, int) { return false; }, tuning);
    }

    CHECK(game::VehicleSpeed(vehicle) == doctest::Approx(4.0f));
    CHECK(vehicle.x == doctest::Approx(4.0f));
}

TEST_CASE("Vehicle service prioritizes repair then fuel and reports missing items") {
    game::VehicleMotion vehicle;
    vehicle.condition = 0.5f;
    vehicle.fuel = 0.5f;
    CHECK(game::PlanVehicleService(vehicle, 1, 1) ==
          game::VehicleServiceAction::Repair);
    CHECK(game::PlanVehicleService(vehicle, 0, 1) ==
          game::VehicleServiceAction::MissingRepairKit);

    vehicle.condition = 1.0f;
    CHECK(game::PlanVehicleService(vehicle, 0, 1) ==
          game::VehicleServiceAction::Refuel);
    CHECK(game::PlanVehicleService(vehicle, 0, 0) ==
          game::VehicleServiceAction::MissingFuelCan);

    vehicle.fuel = 1.0f;
    CHECK(game::PlanVehicleService(vehicle, 0, 0) ==
          game::VehicleServiceAction::None);
}

TEST_CASE("M25-T3-V5 VehiclePredict/Correct：重复快照校正收敛") {
    game::VehicleMotion predicted;
    predicted.fuel = 0.55f;
    predicted.condition = 0.65f;
    const auto open = [](int, int, int) { return false; };

    for (int i = 0; i < 12; ++i) {
        (void)game::VehiclePredictStep(&predicted, 1.0f, 0.0f, 1.0f / 60.0f,
                                       open);
    }
    CHECK(predicted.x > 0.05f);
    const float fuelBefore = predicted.fuel;
    const float conditionBefore = predicted.condition;

    const float serverX = predicted.x - 0.40f;
    CHECK_FALSE(game::VehicleCorrectPosition(&predicted, serverX,
                                             predicted.y, 1.5f, 0.0f, 1.0f,
                                             0.25f));
    float previousDistance = std::fabs(predicted.x - serverX);
    CHECK(previousDistance > 0.0f);
    for (int i = 0; i < 12; ++i) {
        CHECK_FALSE(game::VehicleCorrectPosition(
            &predicted, serverX, predicted.y, 1.5f, 0.0f, 1.0f, 0.25f));
        const float distance = std::fabs(predicted.x - serverX);
        CHECK(distance <= previousDistance + 1.0e-6f);
        previousDistance = distance;
    }
    CHECK(previousDistance < 0.02f);
    CHECK(predicted.fuel == doctest::Approx(fuelBefore));
    CHECK(predicted.condition == doctest::Approx(conditionBefore));

    CHECK(game::VehicleCorrectPosition(&predicted, serverX + 3.0f,
                                       predicted.y, 0.0f, 0.0f, 1.0f, 0.25f));
    CHECK(predicted.x == doctest::Approx(serverX + 3.0f));
}
TEST_CASE("M25-T4-V19 VehicleRamTargetKind：僵尸/动物目标筛选") {
    using game::VehicleRamTargetKind;
    CHECK(game::VehicleRamTargetKindFor(true, false, false, false, 10.0f, 0,
                                        0) == VehicleRamTargetKind::Zombie);
    CHECK(game::VehicleRamTargetKindFor(false, true, false, false, 10.0f, 0,
                                        0) == VehicleRamTargetKind::Animal);
    CHECK(game::VehicleRamTargetKindFor(true, true, false, false, 10.0f, 0,
                                        0) == VehicleRamTargetKind::Animal);
    CHECK(game::VehicleRamTargetKindFor(false, false, false, false, 10.0f, 0,
                                        0) == VehicleRamTargetKind::None);
    CHECK(game::VehicleRamTargetKindFor(true, false, true, false, 10.0f, 0,
                                        0) == VehicleRamTargetKind::None);
    CHECK(game::VehicleRamTargetKindFor(true, false, false, true, 10.0f, 0,
                                        0) == VehicleRamTargetKind::None);
    CHECK(game::VehicleRamTargetKindFor(true, false, false, false, 0.0f, 0,
                                        0) == VehicleRamTargetKind::None);
    CHECK(game::VehicleRamTargetKindFor(true, false, false, false, NAN, 0,
                                        0) == VehicleRamTargetKind::None);
    CHECK(game::VehicleRamTargetKindFor(true, false, false, false, 10.0f, 1,
                                        0) == VehicleRamTargetKind::None);
}

TEST_CASE("M25-T4-V21 VehicleRamTargetKind：PvP 玩家筛选") {
    using game::VehicleRamTargetKind;
    CHECK(game::VehicleRamTargetKindFor(false, false, true, false, 100.0f, 0,
                                        0, true) ==
          VehicleRamTargetKind::Player);
    CHECK(game::VehicleRamTargetKindFor(false, false, true, false, 100.0f, 0,
                                        0, false) ==
          VehicleRamTargetKind::None);
    CHECK(game::VehicleRamTargetKindFor(false, false, true, false, 100.0f, 0,
                                        0) == VehicleRamTargetKind::None);
    CHECK(game::VehicleRamTargetKindFor(false, false, true, true, 100.0f, 0,
                                        0, true) ==
          VehicleRamTargetKind::None);
    CHECK(game::VehicleRamTargetKindFor(false, false, true, false, 0.0f, 0, 0,
                                        true) ==
          VehicleRamTargetKind::None);
    CHECK(game::VehicleRamTargetKindFor(false, false, true, false, NAN, 0, 0,
                                        true) ==
          VehicleRamTargetKind::None);
    CHECK(game::VehicleRamTargetKindFor(false, false, true, false, 100.0f, 1,
                                        0, true) ==
          VehicleRamTargetKind::None);
    CHECK(game::VehicleRamTargetKindFor(true, false, true, false, 100.0f, 0,
                                        0, true) ==
          VehicleRamTargetKind::Player);
    CHECK(game::VehicleRamTargetKindFor(false, true, true, false, 100.0f, 0,
                                        0, true) ==
          VehicleRamTargetKind::Player);
    CHECK(game::VehicleRamTargetKindFor(false, false, false, false, 100.0f, 0,
                                        0, true) ==
          VehicleRamTargetKind::None);
}

TEST_CASE("M25-T4-V20 VehicleAttackerKind：僵尸/危险动物攻击来源筛选") {
    using game::VehicleAttackerKind;
    CHECK(game::VehicleAttackerKindFor(true, false, false, false, false) ==
          VehicleAttackerKind::Zombie);
    CHECK(game::VehicleAttackerKindFor(false, true, true, false, false) ==
          VehicleAttackerKind::Animal);
    CHECK(game::VehicleAttackerKindFor(true, true, true, false, false) ==
          VehicleAttackerKind::Animal);
    CHECK(game::VehicleAttackerKindFor(false, true, false, false, false) ==
          VehicleAttackerKind::None);
    CHECK(game::VehicleAttackerKindFor(false, false, false, false, false) ==
          VehicleAttackerKind::None);
    CHECK(game::VehicleAttackerKindFor(true, false, false, true, false) ==
          VehicleAttackerKind::None);
    CHECK(game::VehicleAttackerKindFor(true, false, false, false, true) ==
          VehicleAttackerKind::None);
    CHECK(game::VehicleAttackerKindFor(false, true, true, false, true) ==
          VehicleAttackerKind::None);
    CHECK(game::VehicleAttackerKindFor(false, true, true, true, false) ==
          VehicleAttackerKind::None);
}

TEST_CASE("M25-T4 VehicleRam：速度阈值、伤害、击退与重叠判定") {
    const game::VehicleTuning tuning = game::kVehicleDefaultTuning;
    CHECK(game::VehicleRamDamage(1.99f, tuning) == doctest::Approx(0.0f));
    CHECK(game::VehicleRamDamage(5.0f, tuning) ==
          doctest::Approx(tuning.ramBaseDamage +
                          (5.0f - tuning.ramMinSpeed) *
                              tuning.ramDamagePerSpeed));
    CHECK(game::VehicleRamDamage(10.0f, tuning) > 100.0f);
    CHECK(game::VehicleRamKnockback(5.0f, tuning) ==
          doctest::Approx(tuning.ramKnockbackBase +
                          5.0f * tuning.ramKnockbackPerSpeed));
    CHECK(game::VehicleRamStagger(5.0f, tuning) ==
          doctest::Approx(tuning.ramStaggerBase +
                          (5.0f - tuning.ramMinSpeed) *
                              tuning.ramStaggerPerSpeed));

    game::VehicleMotion vehicle;
    vehicle.x = 1.0f;
    vehicle.y = 1.0f;
    CHECK(game::VehicleRamOverlap(vehicle, 1.7f, 1.0f, 0.35f, 0.65f));
    CHECK_FALSE(game::VehicleRamOverlap(vehicle, 2.1f, 1.0f, 0.35f, 0.65f));
    CHECK_FALSE(game::VehicleRamOverlap(vehicle, 1.7f, 1.0f, -1.0f, 0.65f));
    CHECK_FALSE(game::VehicleRamOverlap(vehicle, 0.0f, 0.0f, 0.35f, 0.65f));
}

TEST_CASE("M26-V2 VehicleStep：其他载具动态阻挡与相对冲击") {
    const game::VehicleTuning tuning = game::kVehicleDefaultTuning;
    game::VehicleMotion moving;
    moving.x = 1.0f;
    moving.y = 0.0f;
    const game::VehicleMotion parked{5.0f, 0.0f, 0, 0.0f, 0.0f, 1.0f, 1.0f,
                                     false, false};
    const auto open = [](int, int, int) { return false; };
    const auto bodyBlocked = [&](float x, float y, int z, float radius) {
        return z == 0 &&
               game::VehicleBodiesOverlap(x, y, radius, parked.x, parked.y,
                                          tuning.radius);
    };
    bool sawBodyCollision = false;
    bool sawBodyContact = false;
    bool collisionHadNoLocalDamage = false;
    for (int i = 0; i < 180; ++i) {
        const game::VehicleStepResult result = game::VehicleStep(
            &moving, 1.0f, 0.0f, 1.0f / 60.0f, open, tuning,
            game::VehicleSurface::Paved, bodyBlocked);
        sawBodyContact = sawBodyContact || result.bodyContact;
        if (result.collided && result.bodyCollision) {
            sawBodyCollision = true;
            collisionHadNoLocalDamage = !result.damaged && result.damage == 0.0f;
        }
    }
    CHECK(sawBodyCollision);
    CHECK(sawBodyContact);
    CHECK(collisionHadNoLocalDamage);
    CHECK(moving.x > 2.0f);
    CHECK(game::VehicleBodiesOverlap(moving.x, moving.y, tuning.radius,
                                     parked.x, parked.y, tuning.radius) ==
          false);
    CHECK(moving.x + tuning.radius + tuning.radius <=
          doctest::Approx(parked.x).epsilon(0.01));
    CHECK(game::VehicleSpeed(moving) == doctest::Approx(0.0f));

    game::VehicleMotion first;
    first.vx = 4.0f;
    game::VehicleMotion second;
    second.vx = -3.0f;
    CHECK(game::VehicleCollisionImpactSpeed(first, second) ==
          doctest::Approx(7.0f));
    first.vx = NAN;
    CHECK(game::VehicleCollisionImpactSpeed(first, second) ==
          doctest::Approx(0.0f));

    CHECK(game::VehicleCollisionConditionDamage(1.99f, tuning) ==
          doctest::Approx(0.0f));
    CHECK(game::VehicleCollisionConditionDamage(5.0f, tuning) ==
          doctest::Approx(tuning.vehicleCollisionConditionDamageBase +
                          (5.0f - tuning.vehicleCollisionMinSpeed) *
                              tuning.vehicleCollisionConditionDamagePerSpeed));
    CHECK(game::VehicleCollisionConditionDamage(20.0f, tuning) ==
          doctest::Approx(tuning.vehicleCollisionConditionDamageMax));
    CHECK(game::VehicleCollisionConditionDamage(NAN, tuning) ==
          doctest::Approx(0.0f));

    game::VehicleMotion a;
    a.condition = 0.5f;
    game::VehicleMotion b;
    b.condition = 0.5f;
    const float wear = game::VehicleApplyCollisionConditionDamage(
        &a, 5.0f, tuning);
    const float otherWear = game::VehicleApplyCollisionConditionDamage(
        &b, 5.0f, tuning);
    CHECK(wear > 0.0f);
    CHECK(wear == doctest::Approx(otherWear));
    CHECK(a.condition == doctest::Approx(0.5f - wear));
    CHECK(b.condition == doctest::Approx(0.5f - otherWear));
}

TEST_CASE("M26-V4 VehicleCollisionPush：相对速度推挤距离") {
    game::VehicleTuning tuning = game::kVehicleDefaultTuning;
    CHECK(game::VehicleCollisionPushDistance(1.9f, tuning) ==
          doctest::Approx(0.0f));
    CHECK(game::VehicleCollisionPushDistance(2.0f, tuning) ==
          doctest::Approx(0.0f));
    CHECK(game::VehicleCollisionPushDistance(5.0f, tuning) ==
          doctest::Approx(0.36f).epsilon(0.001f));
    CHECK(game::VehicleCollisionPushDistance(10.0f, tuning) ==
          doctest::Approx(0.8f));
    CHECK(game::VehicleCollisionPushDistance(NAN, tuning) ==
          doctest::Approx(0.0f));
    tuning.vehicleCollisionPushMax = 0.0f;
    CHECK(game::VehicleCollisionPushDistance(5.0f, tuning) ==
          doctest::Approx(0.0f));
    tuning.vehicleCollisionPushMax = 0.8f;
    tuning.vehicleCollisionPushPerSpeed = NAN;
    CHECK(game::VehicleCollisionPushDistance(5.0f, tuning) ==
          doctest::Approx(0.0f));
}

TEST_CASE("M26-V6 VehiclePushBody：分步推挤与阻挡") {
    const auto open = [](int, int, int) { return false; };
    const auto noBody = [](float, float, int, float) { return false; };
    game::VehiclePushResult result =
        game::VehiclePushBody(0.0f, 0.0f, 0, 1.0f, 0.0f, 0.8f,
                              game::kVehicleDefaultTuning.radius, open,
                              noBody);
    CHECK(result.x == doctest::Approx(0.8f).epsilon(0.001f));
    CHECK(result.y == doctest::Approx(0.0f));
    CHECK(result.distance == doctest::Approx(0.8f).epsilon(0.001f));

    result = game::VehiclePushBody(
        1.0f, 1.0f, 0, 1.0f, 1.0f, 0.36f,
        game::kVehicleDefaultTuning.radius, open, noBody);
    const float diagonal = 0.36f / std::sqrt(2.0f);
    CHECK(result.x == doctest::Approx(1.0f + diagonal).epsilon(0.001f));
    CHECK(result.y == doctest::Approx(1.0f + diagonal).epsilon(0.001f));
    CHECK(result.distance == doctest::Approx(0.36f).epsilon(0.001f));

    const auto wall = [](int x, int, int) { return x >= 1; };
    result = game::VehiclePushBody(0.5f, 0.0f, 0, 1.0f, 0.0f, 0.8f,
                                   game::kVehicleDefaultTuning.radius, wall,
                                   noBody);
    CHECK(result.x == doctest::Approx(0.95f).epsilon(0.001f));
    CHECK(result.distance == doctest::Approx(0.45f).epsilon(0.001f));

    const auto thirdVehicle = [](float x, float, int, float) {
        return x >= 0.7f;
    };
    result = game::VehiclePushBody(0.5f, 0.0f, 0, 1.0f, 0.0f, 0.8f,
                                   game::kVehicleDefaultTuning.radius, open,
                                   thirdVehicle);
    CHECK(result.x == doctest::Approx(0.65f).epsilon(0.001f));
    CHECK(result.distance == doctest::Approx(0.15f).epsilon(0.001f));

    result = game::VehiclePushBody(3.0f, 4.0f, 0, 0.0f, 0.0f, 0.8f,
                                   game::kVehicleDefaultTuning.radius, open,
                                   noBody);
    CHECK(result.x == doctest::Approx(3.0f));
    CHECK(result.y == doctest::Approx(4.0f));
    CHECK(result.distance == doctest::Approx(0.0f));
}

TEST_CASE("M26-V9 VehicleCollisionImpulse：等质量冲量与质量比") {
    game::VehicleTuning tuning = game::kVehicleDefaultTuning;
    tuning.vehicleCollisionRestitution = 0.2f;
    game::VehicleMotion first;
    first.x = 0.0f;
    first.y = 0.0f;
    first.vx = 5.0f;
    game::VehicleMotion second;
    second.x = 1.0f;
    second.y = 0.0f;
    const game::VehicleCollisionImpulseResult impulse =
        game::VehicleCollisionImpulseFor(first, second, tuning);
    CHECK(impulse.applied);
    CHECK(impulse.closingSpeed == doctest::Approx(5.0f));
    CHECK(impulse.normalX == doctest::Approx(1.0f));
    CHECK(impulse.normalY == doctest::Approx(0.0f));
    CHECK(impulse.firstVx == doctest::Approx(2.0f));
    CHECK(impulse.secondVx == doctest::Approx(3.0f));
    CHECK(impulse.firstVx + impulse.secondVx == doctest::Approx(5.0f));

    tuning.vehicleCollisionRestitution = 0.0f;
    const game::VehicleCollisionImpulseResult inelastic =
        game::VehicleCollisionImpulseFor(first, second, tuning);
    CHECK(inelastic.firstVx == doctest::Approx(2.5f));
    CHECK(inelastic.secondVx == doctest::Approx(2.5f));

    const game::VehicleCollisionImpulseResult massRatio =
        game::VehicleCollisionImpulseFor(first, second, tuning, 1.0f, 2.0f);
    CHECK(massRatio.firstVx == doctest::Approx(5.0f / 3.0f));
    CHECK(massRatio.secondVx == doctest::Approx(5.0f / 3.0f));
    CHECK(massRatio.firstVx + massRatio.secondVx * 2.0f ==
          doctest::Approx(5.0f));

    second.vx = -3.0f;
    first.vx = 4.0f;
    tuning.vehicleCollisionRestitution = 0.2f;
    const game::VehicleCollisionImpulseResult headOn =
        game::VehicleCollisionImpulseFor(first, second, tuning);
    CHECK(headOn.closingSpeed == doctest::Approx(7.0f));
    CHECK(headOn.firstVx == doctest::Approx(-0.2f));
    CHECK(headOn.secondVx == doctest::Approx(1.2f));

    first.vx = 1.9f;
    second.vx = 0.0f;
    const game::VehicleCollisionImpulseResult belowThreshold =
        game::VehicleCollisionImpulseFor(first, second, tuning);
    CHECK_FALSE(belowThreshold.applied);
    CHECK(belowThreshold.firstVx == doctest::Approx(1.9f));
    CHECK(belowThreshold.secondVx == doctest::Approx(0.0f));

    first.vx = NAN;
    const game::VehicleCollisionImpulseResult invalid =
        game::VehicleCollisionImpulseFor(first, second, tuning);
    CHECK_FALSE(invalid.applied);
}

TEST_CASE("M26-V12 VehicleProfile：车型质量/尺寸与性能数据") {
    CHECK(game::VehicleMassClassForStableId(0) ==
          game::VehicleMassClass::Medium);
    CHECK(game::VehicleMassClassForStableId(1) ==
          game::VehicleMassClass::Medium);
    CHECK(game::VehicleMassClassForStableId(2) ==
          game::VehicleMassClass::Heavy);
    CHECK(game::VehicleMassClassForStableId(3) ==
          game::VehicleMassClass::Light);
    CHECK(game::VehicleTypeForStableId(3) ==
          ecs::VehicleType::LightPickup);
    CHECK(game::VehicleTypeForStableId(4) == ecs::VehicleType::Pickup);
    CHECK(game::VehicleTypeForStableId(5) ==
          ecs::VehicleType::HeavyPickup);
    CHECK(game::VehicleMassForType(ecs::VehicleType::LightPickup) ==
          doctest::Approx(game::kVehicleMassLight));
    CHECK(game::VehicleMassForType(ecs::VehicleType::Pickup) ==
          doctest::Approx(game::kVehicleMassMedium));
    CHECK(game::VehicleMassForType(ecs::VehicleType::HeavyPickup) ==
          doctest::Approx(game::kVehicleMassHeavy));
    CHECK(game::VehicleMassForStableId(3) ==
          doctest::Approx(game::kVehicleMassLight));
    CHECK(game::VehicleRadiusForType(ecs::VehicleType::LightPickup) <
          game::VehicleRadiusForType(ecs::VehicleType::HeavyPickup));
    CHECK(game::VehicleProfileFor(ecs::VehicleType::HeavyPickup)
              .conditionDamageScale < 1.0f);
    CHECK(game::VehicleTuningForType(ecs::VehicleType::HeavyPickup,
                                     game::kVehicleDefaultTuning)
              .maxSpeed < game::kVehicleMaxSpeed);
    CHECK(std::string(game::VehicleTypeName(
              ecs::VehicleType::HeavyPickup)) == "重型皮卡");
    CHECK(std::string(game::VehicleMassLabel(
              game::VehicleMassClass::Light)) == "轻");
    CHECK(std::string(game::VehicleMassLabel(
              game::VehicleMassClass::Medium)) == "标准");
    CHECK(std::string(game::VehicleMassLabel(
              game::VehicleMassClass::Heavy)) == "重");

    game::VehicleTuning tuning = game::kVehicleDefaultTuning;
    tuning.vehicleCollisionRestitution = 0.2f;
    game::VehicleMotion first;
    first.vx = 5.0f;
    game::VehicleMotion second;
    second.x = 1.0f;
    const float firstMass =
        game::VehicleMassForType(ecs::VehicleType::HeavyPickup);
    const float secondMass =
        game::VehicleMassForType(ecs::VehicleType::LightPickup);
    const game::VehicleCollisionImpulseResult impulse =
        game::VehicleCollisionImpulseFor(first, second, tuning, firstMass,
                                         secondMass);
    CHECK(impulse.applied);
    CHECK(impulse.firstVx * firstMass + impulse.secondVx * secondMass ==
          doctest::Approx(5.0f * firstMass));
    CHECK(impulse.secondVx > impulse.firstVx);

    game::VehicleTuning pushTuning = game::kVehicleDefaultTuning;
    const float heavyPush = game::VehicleCollisionPushDistanceForMasses(
        5.0f, pushTuning, firstMass, secondMass);
    const float lightPush = game::VehicleCollisionPushDistanceForMasses(
        5.0f, pushTuning, secondMass, firstMass);
    CHECK(heavyPush > lightPush);
}

TEST_CASE("M26-V11 VehicleCollisionImpulse：切向摩擦与角速度") {
    game::VehicleTuning tuning = game::kVehicleDefaultTuning;
    tuning.vehicleCollisionRestitution = 0.0f;
    tuning.vehicleCollisionFriction = 0.5f;
    game::VehicleMotion first;
    first.vx = 5.0f;
    game::VehicleMotion second;
    second.x = 1.0f;
    second.vy = 3.0f;
    const game::VehicleCollisionImpulseResult impulse =
        game::VehicleCollisionImpulseFor(first, second, tuning);
    CHECK(impulse.applied);
    CHECK(impulse.firstVy > 0.0f);
    CHECK(impulse.secondVy < 3.0f);
    CHECK(impulse.firstAngularVelocity > 0.0f);
    CHECK(impulse.secondAngularVelocity > 0.0f);

    tuning.vehicleCollisionFriction = 0.0f;
    const game::VehicleCollisionImpulseResult noFriction =
        game::VehicleCollisionImpulseFor(first, second, tuning);
    CHECK(noFriction.firstVy == doctest::Approx(0.0f));
    CHECK(noFriction.secondVy == doctest::Approx(3.0f));
    CHECK(noFriction.firstAngularVelocity == doctest::Approx(0.0f));
    CHECK(noFriction.secondAngularVelocity == doctest::Approx(0.0f));

    game::VehicleMotion spun;
    spun.heading = 1.5707963267948966f;
    spun.vx = 1.0f;
    CHECK(game::VehicleVisualDirectionForMotion(
              spun, game::VehicleVisualDirection::East) ==
          game::VehicleVisualDirection::South);
}

TEST_CASE("M26-V12 VehicleIntegrateAngular：车身朝向积分与角速度衰减") {
    game::VehicleTuning tuning = game::kVehicleDefaultTuning;
    tuning.vehicleAngularDamping = 3.0f;
    game::VehicleMotion motion;
    motion.angularVelocity = 2.0f;
    game::VehicleIntegrateAngular(&motion, 1.0f / 60.0f, tuning);
    CHECK(std::fabs(motion.heading) > 0.0f);
    CHECK(motion.angularVelocity < 2.0f);
    const float headingAfterFirst = motion.heading;
    for (int i = 0; i < 600; ++i) {
        game::VehicleIntegrateAngular(&motion, 1.0f / 60.0f, tuning);
    }
    CHECK(std::fabs(motion.angularVelocity) < 0.01f);
    CHECK(std::fabs(motion.heading - headingAfterFirst) > 0.01f);

    game::VehicleMotion invalid;
    invalid.angularVelocity = NAN;
    invalid.heading = NAN;
    game::VehicleIntegrateAngular(&invalid, 1.0f / 60.0f, tuning);
    CHECK(invalid.angularVelocity == doctest::Approx(0.0f));
    CHECK(invalid.heading == doctest::Approx(0.0f));
}

TEST_CASE("M26-V12 VehicleStep：车身转向、侧向抓地与低速对齐") {
    game::VehicleTuning tuning = game::kVehicleDefaultTuning;
    const auto open = [](int, int, int) { return false; };

    game::VehicleMotion rest;
    game::VehicleStep(&rest, 0.0f, 1.0f, 1.0f / 60.0f, open, tuning);
    CHECK(rest.heading ==
          doctest::Approx(1.5707963267948966f));

    game::VehicleMotion moving;
    moving.vx = 5.0f;
    moving.heading = 0.0f;
    const float before = moving.heading;
    game::VehicleStep(&moving, 0.0f, 1.0f, 1.0f / 60.0f, open, tuning);
    CHECK(moving.heading > before);
    CHECK(moving.heading < 1.5707963267948966f);
    CHECK(std::fabs(moving.vy) > 0.0f);

    game::VehicleMotion gripped;
    gripped.vx = 5.0f;
    gripped.vy = 3.0f;
    gripped.heading = 0.0f;
    game::VehicleStep(&gripped, 1.0f, 0.0f, 0.1f, open, tuning);
    CHECK(std::fabs(gripped.vy) < 3.0f);
}

TEST_CASE("M26-V13 VehicleProfile：轴距/转向角/横向抓地数据") {
    const game::VehicleProfile& light =
        game::VehicleProfileFor(ecs::VehicleType::LightPickup);
    const game::VehicleProfile& pickup =
        game::VehicleProfileFor(ecs::VehicleType::Pickup);
    const game::VehicleProfile& heavy =
        game::VehicleProfileFor(ecs::VehicleType::HeavyPickup);
    CHECK(light.wheelbase < pickup.wheelbase);
    CHECK(pickup.wheelbase < heavy.wheelbase);
    CHECK(light.maxSteerAngle > pickup.maxSteerAngle);
    CHECK(pickup.maxSteerAngle > heavy.maxSteerAngle);
    CHECK(light.maxLateralAcceleration > pickup.maxLateralAcceleration);
    CHECK(pickup.maxLateralAcceleration > heavy.maxLateralAcceleration);
    const game::VehicleTuning heavyTuning = game::VehicleTuningForType(
        ecs::VehicleType::HeavyPickup, game::kVehicleDefaultTuning);
    CHECK(heavyTuning.wheelbase == doctest::Approx(heavy.wheelbase));
    CHECK(heavyTuning.maxSteerAngle == doctest::Approx(heavy.maxSteerAngle));
    const game::VehicleSurfaceProfile dirt =
        game::VehicleSurfaceProfileFor(game::VehicleSurface::Dirt,
                                       game::kVehicleDefaultTuning);
    const game::VehicleSurfaceProfile grass =
        game::VehicleSurfaceProfileFor(game::VehicleSurface::Grass,
                                       game::kVehicleDefaultTuning);
    CHECK(grass.lateralAccelerationScale <
          dirt.lateralAccelerationScale);
    CHECK(dirt.lateralAccelerationScale < 1.0f);
}

TEST_CASE("M26-V13 VehicleTire：自行车模型与横向加速度上限") {
    CHECK(game::VehicleTireYawRate(0.0f, 0.85f, 1.15f, 10.0f, 18.0f) ==
          doctest::Approx(0.0f));
    const float lowSpeed =
        game::VehicleTireYawRate(5.0f, 0.85f, 1.15f, 10.0f, 1000.0f);
    CHECK(lowSpeed > 0.0f);
    CHECK(lowSpeed == doctest::Approx(
                         5.0f * std::tan(0.85f) / 1.15f));
    CHECK(game::VehicleTireYawRate(5.0f, -0.85f, 1.15f, 10.0f, 1000.0f) <
          0.0f);
    CHECK(game::VehicleTireYawRate(5.0f, 0.85f, 1.15f, 1.0f, 1000.0f) ==
          doctest::Approx(1.0f));
    CHECK(game::VehicleTireYawRate(5.0f, 0.85f, 1.15f, 10.0f, 2.0f) ==
          doctest::Approx(0.4f));
    CHECK(game::VehicleTireYawRate(NAN, 0.85f, 1.15f, 10.0f, 18.0f) ==
          doctest::Approx(0.0f));
    CHECK(game::VehicleTireYawRate(5.0f, 0.85f, 1.15f, 10.0f, 0.0f) ==
          doctest::Approx(0.0f));
    CHECK(game::VehicleTireLateralAcceleration(5.0f, 2.0f) ==
          doctest::Approx(10.0f));
}

TEST_CASE("M26-V13 VehicleSuspension：车身 roll/pitch 弹簧阻尼") {
    game::VehicleTuning tuning = game::kVehicleDefaultTuning;
    game::VehicleMotion vehicle;
    game::VehicleSuspensionStep(&vehicle, 1.0f / 60.0f, 18.0f, 8.0f,
                                tuning);
    CHECK(vehicle.bodyRoll < 0.0f);
    CHECK(vehicle.bodyPitch < 0.0f);
    for (int i = 0; i < 600; ++i) {
        game::VehicleSuspensionStep(&vehicle, 1.0f / 60.0f, 18.0f, 8.0f,
                                    tuning);
    }
    CHECK(vehicle.bodyRoll ==
          doctest::Approx(-game::kVehicleBodyMaxRoll).epsilon(0.05));
    CHECK(vehicle.bodyPitch ==
          doctest::Approx(-game::kVehicleBodyMaxPitch).epsilon(0.05));
    CHECK(std::fabs(vehicle.bodyRollVelocity) < 0.01f);
    CHECK(std::fabs(vehicle.bodyPitchVelocity) < 0.01f);
    game::VehicleSuspensionStep(&vehicle, 1.0f / 60.0f, NAN, 0.0f, tuning);
    CHECK(vehicle.bodyRoll == doctest::Approx(0.0f));
    CHECK(vehicle.bodyPitch == doctest::Approx(0.0f));
}

TEST_CASE("M26-V13 VehicleStep：前轮转角与悬挂姿态接入") {
    game::VehicleTuning tuning = game::kVehicleDefaultTuning;
    const auto open = [](int, int, int) { return false; };
    game::VehicleMotion moving;
    moving.vx = 5.0f;
    moving.heading = 0.0f;
    game::VehicleStep(&moving, 0.0f, 1.0f, 1.0f / 60.0f, open, tuning);
    CHECK(moving.steerAngle > 0.0f);
    CHECK(moving.steerAngle <= tuning.maxSteerAngle);
    CHECK(moving.bodyRoll < 0.0f);
    CHECK(std::isfinite(moving.bodyPitch));
}

TEST_CASE("M26-V14 VehicleContactImpulse：低速持续接触冲量") {
    game::VehicleTuning tuning = game::kVehicleDefaultTuning;
    game::VehicleMotion first;
    first.x = 0.0f;
    first.y = 0.0f;
    first.vx = 1.0f;
    game::VehicleMotion second;
    second.x = 1.5f;
    second.y = 0.0f;

    const game::VehicleCollisionImpulseResult normal =
        game::VehicleCollisionImpulseFor(first, second, tuning);
    CHECK_FALSE(normal.applied);

    const game::VehicleCollisionImpulseResult contact =
        game::VehicleContactImpulseFor(first, second, tuning);
    CHECK(contact.applied);
    CHECK(contact.closingSpeed == doctest::Approx(1.0f));
    CHECK(contact.firstVx == doctest::Approx(0.5f));
    CHECK(contact.secondVx == doctest::Approx(0.5f));
    CHECK(contact.firstAngularVelocity == doctest::Approx(0.0f));
    CHECK(contact.secondAngularVelocity == doctest::Approx(0.0f));
    CHECK(game::VehicleCollisionConditionDamage(1.0f, tuning) ==
          doctest::Approx(0.0f));
    CHECK(game::VehicleCollisionOccupantDamage(1.0f, tuning) ==
          doctest::Approx(0.0f));

    const game::VehicleCollisionImpulseResult massRatio =
        game::VehicleContactImpulseFor(first, second, tuning, 1.0f, 2.0f);
    CHECK(massRatio.firstVx == doctest::Approx(1.0f / 3.0f));
    CHECK(massRatio.secondVx == doctest::Approx(1.0f / 3.0f));
    CHECK(massRatio.firstVx + massRatio.secondVx * 2.0f ==
          doctest::Approx(1.0f));

    second.vx = 1.0f;
    const game::VehicleCollisionImpulseResult separating =
        game::VehicleContactImpulseFor(first, second, tuning);
    CHECK_FALSE(separating.applied);

    second.vx = 0.0f;
    first.vx = 0.1f;
    const game::VehicleCollisionImpulseResult belowThreshold =
        game::VehicleContactImpulseFor(first, second, tuning);
    CHECK_FALSE(belowThreshold.applied);

    first.vx = NAN;
    const game::VehicleCollisionImpulseResult invalid =
        game::VehicleContactImpulseFor(first, second, tuning);
    CHECK_FALSE(invalid.applied);
}

TEST_CASE("M26-V12 VehicleCorrectHeading：服务器朝向平滑校正") {
    game::VehicleMotion motion;
    motion.heading = 0.0f;
    CHECK_FALSE(game::VehicleCorrectHeading(&motion, 0.0f));
    CHECK(game::VehicleCorrectHeading(&motion, 1.0f, 0.5f));
    CHECK(motion.heading == doctest::Approx(0.5f));
    CHECK_FALSE(game::VehicleCorrectHeading(&motion, NAN));
}

TEST_CASE("M26-V10 VehicleCollisionOccupantDamage：速度映射与配置上限") {
    game::VehicleTuning tuning = game::kVehicleDefaultTuning;
    CHECK(game::VehicleCollisionOccupantDamage(1.9f, tuning) ==
          doctest::Approx(0.0f));
    CHECK(game::VehicleCollisionOccupantDamage(2.0f, tuning) ==
          doctest::Approx(3.0f));
    CHECK(game::VehicleCollisionOccupantDamage(5.0f, tuning) ==
          doctest::Approx(6.6f));
    CHECK(game::VehicleCollisionOccupantDamage(10.0f, tuning) ==
          doctest::Approx(12.6f));

    tuning.vehicleCollisionOccupantDamageMax = 5.0f;
    CHECK(game::VehicleCollisionOccupantDamage(10.0f, tuning) ==
          doctest::Approx(5.0f));

    tuning = game::kVehicleDefaultTuning;
    tuning.vehicleCollisionOccupantDamageBase = 0.0f;
    CHECK(game::VehicleCollisionOccupantDamage(2.0f, tuning) ==
          doctest::Approx(0.0f));

    tuning.vehicleCollisionOccupantDamageBase = NAN;
    CHECK(game::VehicleCollisionOccupantDamage(5.0f, tuning) ==
          doctest::Approx(0.0f));
    tuning = game::kVehicleDefaultTuning;
    tuning.vehicleCollisionOccupantDamagePerSpeed = -1.0f;
    CHECK(game::VehicleCollisionOccupantDamage(5.0f, tuning) ==
          doctest::Approx(0.0f));
    tuning = game::kVehicleDefaultTuning;
    tuning.vehicleCollisionOccupantDamageMax = 0.0f;
    CHECK(game::VehicleCollisionOccupantDamage(5.0f, tuning) ==
          doctest::Approx(0.0f));
    CHECK(game::VehicleCollisionOccupantDamage(NAN, tuning) ==
          doctest::Approx(0.0f));
}

TEST_CASE("M26-V7 VehicleVisualDirectionVector：朝向单位向量") {
    using game::VehicleVisualDirection;
    const auto checkDirection = [](VehicleVisualDirection direction,
                                   float expectedX, float expectedY) {
        float x = 0.0f;
        float y = 0.0f;
        game::VehicleVisualDirectionVector(direction, &x, &y);
        CHECK(x == doctest::Approx(expectedX).epsilon(0.001f));
        CHECK(y == doctest::Approx(expectedY).epsilon(0.001f));
        CHECK(std::hypot(x, y) == doctest::Approx(1.0f).epsilon(0.001f));
    };
    const float diagonal = std::sqrt(0.5f);
    checkDirection(VehicleVisualDirection::East, 1.0f, 0.0f);
    checkDirection(VehicleVisualDirection::Southeast, diagonal, diagonal);
    checkDirection(VehicleVisualDirection::South, 0.0f, 1.0f);
    checkDirection(VehicleVisualDirection::Southwest, -diagonal, diagonal);
    checkDirection(VehicleVisualDirection::West, -1.0f, 0.0f);
    checkDirection(VehicleVisualDirection::Northwest, -diagonal, -diagonal);
    checkDirection(VehicleVisualDirection::North, 0.0f, -1.0f);
    checkDirection(VehicleVisualDirection::Northeast, diagonal, -diagonal);
    float x = 0.0f;
    float y = 0.0f;
    game::VehicleVisualDirectionVector(VehicleVisualDirection::South,
                                       nullptr, &y);
    CHECK(y == doctest::Approx(0.0f).epsilon(0.001f));
    game::VehicleVisualDirectionVector(VehicleVisualDirection::South, &x,
                                       nullptr);
    CHECK(x == doctest::Approx(0.0f).epsilon(0.001f));
}

TEST_CASE("M26-V5 单机多车碰撞：停放车辆阻挡与双方车损") {
    const game::VehicleTuning tuning = game::kVehicleDefaultTuning;
    game::VehicleMotion moving;
    moving.x = 1.0f;
    game::VehicleMotion parkedA;
    parkedA.x = 5.0f;
    parkedA.condition = 0.8f;
    game::VehicleMotion parkedB;
    parkedB.x = 5.0f;
    parkedB.y = 3.0f;
    parkedB.condition = 0.9f;
    const auto open = [](int, int, int) { return false; };
    const auto bodyBlocked = [&](float x, float y, int z, float radius) {
        return z == 0 &&
               (game::VehicleBodiesOverlap(x, y, radius, parkedA.x, parkedA.y,
                                           tuning.radius) ||
                game::VehicleBodiesOverlap(x, y, radius, parkedB.x, parkedB.y,
                                           tuning.radius));
    };
    bool sawBodyCollision = false;
    bool automaticDamage = false;
    float impactSpeed = 0.0f;
    for (int i = 0; i < 180; ++i) {
        const game::VehicleStepResult result = game::VehicleStep(
            &moving, 1.0f, 0.0f, 1.0f / 60.0f, open, tuning,
            game::VehicleSurface::Paved, bodyBlocked);
        if (result.collided && result.bodyCollision) {
            if (!sawBodyCollision) {
                impactSpeed = result.impactSpeed;
            }
            sawBodyCollision = true;
            automaticDamage = automaticDamage || result.damaged ||
                              result.damage > 0.0f;
        }
    }
    CHECK(sawBodyCollision);
    CHECK_FALSE(automaticDamage);
    CHECK(impactSpeed >= tuning.vehicleCollisionMinSpeed);
    CHECK(moving.condition == doctest::Approx(1.0f));
    CHECK(game::VehicleSpeed(moving) == doctest::Approx(0.0f));
    CHECK_FALSE(game::VehicleBodiesOverlap(moving.x, moving.y, tuning.radius,
                                           parkedA.x, parkedA.y,
                                           tuning.radius));
    CHECK(moving.x + tuning.radius * 2.0f <=
          doctest::Approx(parkedA.x).epsilon(0.01));
    CHECK(parkedA.x == doctest::Approx(5.0f));
    CHECK(parkedB.x == doctest::Approx(5.0f));
    CHECK(parkedB.y == doctest::Approx(3.0f));

    const float activeWear = game::VehicleApplyCollisionConditionDamage(
        &moving, impactSpeed, tuning);
    const float parkedWear = game::VehicleApplyCollisionConditionDamage(
        &parkedA, impactSpeed, tuning);
    CHECK(activeWear > 0.0f);
    CHECK(activeWear == doctest::Approx(parkedWear));
    CHECK(moving.condition == doctest::Approx(1.0f - activeWear));
    CHECK(parkedA.condition == doctest::Approx(0.8f - parkedWear));
    CHECK(game::VehicleCollisionConditionDamage(1.0f, tuning) ==
          doctest::Approx(0.0f));
}

TEST_CASE("M25-T4-V2 车灯状态在驾驶步进后保留") {
    game::VehicleMotion vehicle;
    vehicle.headlights = true;
    const auto open = [](int, int, int) { return false; };
    (void)game::VehicleStep(&vehicle, 0.0f, 0.0f, 1.0f / 60.0f, open);
    CHECK(vehicle.headlights);
    const game::VehicleMotion predicted = vehicle;
    vehicle.headlights = false;
    CHECK_FALSE(vehicle.headlights);
    CHECK(predicted.headlights);
}

TEST_CASE("M25-T4-V3 VehicleSurface：地面分类与倍率") {
    CHECK(game::VehicleSurfaceForFloor("tile.floor.wood") ==
          game::VehicleSurface::Paved);
    CHECK(game::VehicleSurfaceForFloor("tile.floor.dirt") ==
          game::VehicleSurface::Dirt);
    CHECK(game::VehicleSurfaceForFloor("tile.floor.grass") ==
          game::VehicleSurface::Grass);
    CHECK(game::VehicleSurfaceForFloor("tile.floor.water") ==
          game::VehicleSurface::Grass);
    CHECK(std::string(game::VehicleSurfaceLabel(game::VehicleSurface::Paved)) ==
          "铺装路面");
    CHECK(std::string(game::VehicleSurfaceLabel(game::VehicleSurface::Dirt)) ==
          "泥土路");
    CHECK(std::string(game::VehicleSurfaceLabel(game::VehicleSurface::Grass)) ==
          "草地");

    const game::VehicleTuning tuning = game::kVehicleDefaultTuning;
    const game::VehicleSurfaceProfile paved =
        game::VehicleSurfaceProfileFor(game::VehicleSurface::Paved, tuning);
    CHECK(paved.maxSpeedScale == doctest::Approx(1.0f));
    CHECK(paved.accelerationScale == doctest::Approx(1.0f));
    CHECK(paved.brakingScale == doctest::Approx(1.0f));

    const game::VehicleSurfaceProfile dirt =
        game::VehicleSurfaceProfileFor(game::VehicleSurface::Dirt, tuning);
    CHECK(dirt.maxSpeedScale ==
          doctest::Approx(game::kVehicleDirtMaxSpeedScale));
    CHECK(dirt.accelerationScale ==
          doctest::Approx(game::kVehicleDirtAccelerationScale));
    CHECK(dirt.brakingScale ==
          doctest::Approx(game::kVehicleDirtBrakingScale));

    const game::VehicleSurfaceProfile grass =
        game::VehicleSurfaceProfileFor(game::VehicleSurface::Grass, tuning);
    CHECK(grass.maxSpeedScale ==
          doctest::Approx(game::kVehicleGrassMaxSpeedScale));
    CHECK(grass.accelerationScale ==
          doctest::Approx(game::kVehicleGrassAccelerationScale));
    CHECK(grass.brakingScale ==
          doctest::Approx(game::kVehicleGrassBrakingScale));

    game::VehicleTuning clamped = tuning;
    clamped.grassMaxSpeedScale = 2.0f;
    clamped.grassAccelerationScale = -1.0f;
    const game::VehicleSurfaceProfile limited =
        game::VehicleSurfaceProfileFor(game::VehicleSurface::Grass, clamped);
    CHECK(limited.maxSpeedScale == doctest::Approx(1.0f));
    CHECK(limited.accelerationScale == doctest::Approx(0.0f));
    CHECK(limited.brakingScale == doctest::Approx(game::kVehicleGrassBrakingScale));
}

TEST_CASE("M25-T4-V3 VehicleSurface：草地极速与加速低于泥路和铺装") {
    const auto run = [](game::VehicleSurface surface) {
        const game::VehicleTuning tuning = game::kVehicleDefaultTuning;
        game::VehicleMotion vehicle;
        vehicle.x = 0.5f;
        vehicle.y = 0.5f;
        const auto open = [](int, int, int) { return false; };
        for (int i = 0; i < 600; ++i) {
            (void)game::VehicleStep(&vehicle, 1.0f, 0.0f, 1.0f / 60.0f,
                                    open, tuning, surface);
        }
        return vehicle;
    };

    const game::VehicleMotion paved = run(game::VehicleSurface::Paved);
    const game::VehicleMotion dirt = run(game::VehicleSurface::Dirt);
    const game::VehicleMotion grass = run(game::VehicleSurface::Grass);
    const game::VehicleTuning tuning = game::kVehicleDefaultTuning;
    CHECK(game::VehicleSpeed(paved) == doctest::Approx(tuning.maxSpeed));
    CHECK(game::VehicleSpeed(dirt) ==
          doctest::Approx(tuning.maxSpeed * tuning.dirtMaxSpeedScale));
    CHECK(game::VehicleSpeed(grass) ==
          doctest::Approx(tuning.maxSpeed * tuning.grassMaxSpeedScale));
    CHECK(game::VehicleSpeed(grass) < game::VehicleSpeed(dirt));
    CHECK(game::VehicleSpeed(dirt) < game::VehicleSpeed(paved));
    CHECK(grass.x < dirt.x);
    CHECK(dirt.x < paved.x);

    game::VehicleMotion earlyGrass;
    earlyGrass.x = 0.5f;
    game::VehicleMotion earlyDirt = earlyGrass;
    const auto open = [](int, int, int) { return false; };
    for (int i = 0; i < 30; ++i) {
        (void)game::VehicleStep(&earlyGrass, 1.0f, 0.0f, 1.0f / 60.0f,
                                open, tuning, game::VehicleSurface::Grass);
        (void)game::VehicleStep(&earlyDirt, 1.0f, 0.0f, 1.0f / 60.0f,
                                open, tuning, game::VehicleSurface::Dirt);
    }
    CHECK(game::VehicleSpeed(earlyGrass) < game::VehicleSpeed(earlyDirt));
}

TEST_CASE("M25-T4-V3 VehicleSurface：低附着草地制动更弱且预测保留状态") {
    game::VehicleMotion paved;
    game::VehicleMotion grass;
    paved.vx = game::kVehicleMaxSpeed * game::kVehicleGrassMaxSpeedScale;
    grass.vx = paved.vx;
    const auto open = [](int, int, int) { return false; };
    const game::VehicleTuning tuning = game::kVehicleDefaultTuning;
    for (int i = 0; i < 30; ++i) {
        (void)game::VehicleStep(&paved, 0.0f, 0.0f, 1.0f / 60.0f, open,
                                tuning, game::VehicleSurface::Paved);
        (void)game::VehicleStep(&grass, 0.0f, 0.0f, 1.0f / 60.0f, open,
                                tuning, game::VehicleSurface::Grass);
    }
    CHECK(game::VehicleSpeed(grass) > game::VehicleSpeed(paved));
    CHECK(game::VehicleSpeed(paved) < 0.01f);

    game::VehicleMotion predicted;
    predicted.fuel = 0.6f;
    predicted.condition = 0.7f;
    const float fuelBefore = predicted.fuel;
    const float conditionBefore = predicted.condition;
    for (int i = 0; i < 60; ++i) {
        (void)game::VehiclePredictStep(&predicted, 1.0f, 0.0f, 1.0f / 60.0f,
                                       open, tuning,
                                       game::VehicleSurface::Grass);
    }
    CHECK(predicted.fuel == doctest::Approx(fuelBefore));
    CHECK(predicted.condition == doctest::Approx(conditionBefore));
    CHECK(game::VehicleSpeed(predicted) <=
          doctest::Approx(tuning.maxSpeed * tuning.grassMaxSpeedScale));
}

TEST_CASE("M25-T4-V12 VehicleVisual：八向扇区与切换滞回") {
    using game::VehicleVisualDirection;
    const auto south = VehicleVisualDirection::South;
    const auto east = VehicleVisualDirection::East;
    const auto southeast = VehicleVisualDirection::Southeast;
    CHECK(game::VehicleVisualDirectionFor(1.0f, 0.0f, south) == east);
    CHECK(game::VehicleVisualDirectionFor(-1.0f, 0.0f, south) ==
          VehicleVisualDirection::West);
    CHECK(game::VehicleVisualDirectionFor(0.0f, 1.0f, east) == south);
    CHECK(game::VehicleVisualDirectionFor(0.0f, -1.0f, east) ==
          VehicleVisualDirection::North);
    CHECK(game::VehicleVisualDirectionFor(1.0f, 1.0f, south) == southeast);
    CHECK(game::VehicleVisualDirectionFor(-1.0f, 1.0f, east) ==
          VehicleVisualDirection::Southwest);
    CHECK(game::VehicleVisualDirectionFor(-1.0f, -1.0f, south) ==
          VehicleVisualDirection::Northwest);
    CHECK(game::VehicleVisualDirectionFor(1.0f, -1.0f, south) ==
          VehicleVisualDirection::Northeast);

    CHECK(game::VehicleVisualDirectionFor(1.0f, 0.5f, east) == east);
    CHECK(game::VehicleVisualDirectionFor(1.0f, 1.0f, east) == southeast);
    CHECK(game::VehicleVisualDirectionFor(1.0f, 0.5f, southeast) == southeast);
    CHECK(game::VehicleVisualDirectionFor(1.0f, 0.0f, southeast) == east);
    CHECK(game::VehicleVisualDirectionFor(0.2f, 1.0f, south) == south);
    CHECK(game::VehicleVisualDirectionFor(0.8f, 1.0f, south) == southeast);

    CHECK(game::VehicleVisualDirectionFor(0.0f, 0.0f, south) == south);
    CHECK(game::VehicleVisualDirectionFor(
              std::nanf(""), 1.0f, VehicleVisualDirection::West) ==
          VehicleVisualDirection::West);

    CHECK(std::string(game::VehicleVisualSprite(south)) ==
          "object.vehicle_south");
    CHECK(std::string(game::VehicleVisualSprite(east)) ==
          "object.vehicle_east");
    CHECK(std::string(game::VehicleVisualSprite(southeast)) ==
          "object.vehicle_southeast");
    CHECK(std::string(game::VehicleVisualSprite(
              VehicleVisualDirection::Southwest)) ==
          "object.vehicle_southwest");
    CHECK(std::string(game::VehicleVisualSprite(
              VehicleVisualDirection::West)) ==
          "object.vehicle_west");
    CHECK(std::string(game::VehicleVisualSprite(
              VehicleVisualDirection::Northwest)) ==
          "object.vehicle_northwest");
    CHECK(std::string(game::VehicleVisualSprite(
              VehicleVisualDirection::North)) ==
          "object.vehicle_north");
    CHECK(std::string(game::VehicleVisualSprite(
              VehicleVisualDirection::Northeast)) ==
          "object.vehicle_northeast");
    CHECK(std::string(game::VehicleVisualLabel(southeast)) == "东南");
    CHECK(std::string(game::VehicleVisualLabel(
              VehicleVisualDirection::Southwest)) == "西南");
    CHECK(std::string(game::VehicleVisualLabel(
              VehicleVisualDirection::Northwest)) == "西北");
    CHECK(std::string(game::VehicleVisualLabel(
              VehicleVisualDirection::Northeast)) == "东北");
    CHECK(std::string(game::VehicleVisualLabel(
              VehicleVisualDirection::North)) == "北");
}

TEST_CASE("M25-T4-V4 VehicleAudio：引擎音量/音高随速度且钳制") {
    const game::VehicleTuning tuning = game::kVehicleDefaultTuning;
    const game::VehicleEngineTone idle =
        game::VehicleEngineToneForSpeed(0.0f, tuning.maxSpeed);
    const game::VehicleEngineTone mid =
        game::VehicleEngineToneForSpeed(tuning.maxSpeed * 0.5f, tuning.maxSpeed);
    const game::VehicleEngineTone full =
        game::VehicleEngineToneForSpeed(tuning.maxSpeed, tuning.maxSpeed);
    CHECK(idle.volume == doctest::Approx(tuning.engineIdleVolume));
    CHECK(idle.pitch == doctest::Approx(tuning.engineMinPitch));
    CHECK(full.volume == doctest::Approx(tuning.engineMaxVolume));
    CHECK(full.pitch == doctest::Approx(tuning.engineMaxPitch));
    CHECK(mid.volume > idle.volume);
    CHECK(mid.volume < full.volume);
    CHECK(mid.pitch > idle.pitch);
    CHECK(mid.pitch < full.pitch);

    const game::VehicleEngineTone clamped =
        game::VehicleEngineToneForSpeed(tuning.maxSpeed * 4.0f, tuning.maxSpeed);
    CHECK(clamped.volume == doctest::Approx(tuning.engineMaxVolume));
    CHECK(clamped.pitch == doctest::Approx(tuning.engineMaxPitch));

    const game::VehicleEngineTone invalid =
        game::VehicleEngineToneForSpeed(1.0f, 0.0f);
    CHECK(invalid.volume == doctest::Approx(tuning.engineIdleVolume));
    CHECK(invalid.pitch == doctest::Approx(tuning.engineMinPitch));
}

TEST_CASE("M25-T4-V5 VehicleRamCondition：速度映射与车况钳制") {
    const game::VehicleTuning tuning = game::kVehicleDefaultTuning;
    CHECK(game::VehicleRamConditionDamage(1.9f, tuning) ==
          doctest::Approx(0.0f));
    CHECK(game::VehicleRamConditionDamage(tuning.ramMinSpeed, tuning) ==
          doctest::Approx(0.015f));
    CHECK(game::VehicleRamConditionDamage(10.0f, tuning) ==
          doctest::Approx(0.079f));
    CHECK(game::VehicleRamConditionDamage(20.0f, tuning) ==
          doctest::Approx(tuning.ramConditionDamageMax));
    CHECK(game::VehicleRamConditionDamage(NAN, tuning) ==
          doctest::Approx(0.0f));

    game::VehicleMotion vehicle;
    vehicle.condition = 0.5f;
    CHECK(game::VehicleApplyRamConditionDamage(&vehicle, 10.0f, tuning) ==
          doctest::Approx(0.079f));
    CHECK(vehicle.condition == doctest::Approx(0.421f));

    vehicle.condition = 0.03f;
    CHECK(game::VehicleApplyRamConditionDamage(&vehicle, 10.0f, tuning) ==
          doctest::Approx(0.03f));
    CHECK(vehicle.condition == doctest::Approx(0.0f));

    const float conditionBefore = vehicle.condition;
    CHECK(game::VehicleApplyRamConditionDamage(&vehicle, 10.0f, tuning) ==
          doctest::Approx(0.0f));
    CHECK(vehicle.condition == doctest::Approx(conditionBefore));
    CHECK(game::VehicleApplyRamConditionDamage(nullptr, 10.0f, tuning) ==
          doctest::Approx(0.0f));
}

TEST_CASE("M25-T4-V11 VehicleDamageSmoke：车况烟雾等级与间隔") {
    using game::VehicleDamageSmokeLevel;
    CHECK(game::VehicleDamageSmokeLevelFor(1.0f) ==
          VehicleDamageSmokeLevel::None);
    CHECK(game::VehicleDamageSmokeLevelFor(0.51f) ==
          VehicleDamageSmokeLevel::None);
    CHECK(game::VehicleDamageSmokeLevelFor(0.50f) ==
          VehicleDamageSmokeLevel::Light);
    CHECK(game::VehicleDamageSmokeLevelFor(0.26f) ==
          VehicleDamageSmokeLevel::Light);
    CHECK(game::VehicleDamageSmokeLevelFor(0.25f) ==
          VehicleDamageSmokeLevel::Heavy);
    CHECK(game::VehicleDamageSmokeLevelFor(0.01f) ==
          VehicleDamageSmokeLevel::Heavy);
    CHECK(game::VehicleDamageSmokeLevelFor(0.0f) ==
          VehicleDamageSmokeLevel::Critical);
    CHECK(game::VehicleDamageSmokeLevelFor(-0.2f) ==
          VehicleDamageSmokeLevel::Critical);
    CHECK(game::VehicleDamageSmokeLevelFor(NAN) ==
          VehicleDamageSmokeLevel::None);

    const float light = game::VehicleDamageSmokeInterval(
        VehicleDamageSmokeLevel::Light);
    const float heavy = game::VehicleDamageSmokeInterval(
        VehicleDamageSmokeLevel::Heavy);
    const float critical = game::VehicleDamageSmokeInterval(
        VehicleDamageSmokeLevel::Critical);
    CHECK(game::VehicleDamageSmokeInterval(VehicleDamageSmokeLevel::None) ==
          doctest::Approx(0.0f));
    CHECK(light > heavy);
    CHECK(heavy > critical);
    CHECK(critical > 0.0f);
    CHECK(game::VehicleDamageSmokeLifetime(VehicleDamageSmokeLevel::Critical) >
          game::VehicleDamageSmokeLifetime(VehicleDamageSmokeLevel::Light));
}

TEST_CASE("M25-T4-V13 VehicleSoftSmoke：软粒子尺寸与淡出") {
    using game::VehicleDamageSmokeLevel;
    CHECK(game::VehicleSmokeParticleScale(24.0f, 0.0f) ==
          doctest::Approx(16.8f));
    CHECK(game::VehicleSmokeParticleScale(24.0f, 0.5f) >
          game::VehicleSmokeParticleScale(24.0f, 0.0f));
    CHECK(game::VehicleSmokeParticleScale(24.0f, 1.0f) ==
          doctest::Approx(38.4f));
    CHECK(game::VehicleSmokeParticleScale(24.0f, 2.0f) ==
          doctest::Approx(38.4f));
    CHECK(game::VehicleSmokeParticleScale(24.0f, NAN) ==
          doctest::Approx(0.0f));
    CHECK(game::VehicleSmokeParticleScale(-1.0f, 0.5f) ==
          doctest::Approx(0.0f));

    CHECK(game::VehicleSmokeParticleAlpha(VehicleDamageSmokeLevel::None, 0.5f) ==
          doctest::Approx(0.0f));
    CHECK(game::VehicleSmokeParticleAlpha(VehicleDamageSmokeLevel::Light, 0.0f) ==
          doctest::Approx(0.75f));
    CHECK(game::VehicleSmokeParticleAlpha(VehicleDamageSmokeLevel::Light, 0.06f) >
          0.0f);
    CHECK(game::VehicleSmokeParticleAlpha(VehicleDamageSmokeLevel::Light, 0.5f) >
          0.0f);
    CHECK(game::VehicleSmokeParticleAlpha(VehicleDamageSmokeLevel::Light, 0.95f) <
          game::VehicleSmokeParticleAlpha(VehicleDamageSmokeLevel::Light, 0.5f));
    CHECK(game::VehicleSmokeParticleAlpha(VehicleDamageSmokeLevel::Light, 1.0f) ==
          doctest::Approx(0.0f));
    CHECK(game::VehicleSmokeParticleAlpha(VehicleDamageSmokeLevel::Critical, 0.5f) >
          game::VehicleSmokeParticleAlpha(VehicleDamageSmokeLevel::Heavy, 0.5f));
    CHECK(game::VehicleSmokeParticleAlpha(VehicleDamageSmokeLevel::Heavy, 0.5f) >
          game::VehicleSmokeParticleAlpha(VehicleDamageSmokeLevel::Light, 0.5f));
    CHECK(game::VehicleSmokeParticleAlpha(VehicleDamageSmokeLevel::Light, NAN) ==
          doctest::Approx(0.0f));
}

TEST_CASE("M25-T4-V14 VehicleHorn：可用性与冷却") {
    CHECK(game::VehicleHornReady(1.0f, 0.0f));
    CHECK(game::VehicleHornReady(0.1f, -1.0f));
    CHECK_FALSE(game::VehicleHornReady(0.0f, 0.0f));
    CHECK_FALSE(game::VehicleHornReady(NAN, 0.0f));
    CHECK_FALSE(game::VehicleHornReady(1.0f, NAN));
    CHECK_FALSE(game::VehicleHornReady(1.0f, 0.01f));
    CHECK(game::kVehicleHornNoiseRadius >
          game::kVehicleNoiseRadius);
}

TEST_CASE("M25-T4-V15 VehicleZombieAttack：车况损耗与钳制") {
    game::VehicleTuning tuning = game::kVehicleDefaultTuning;
    tuning.zombieAttackConditionDamage = 0.015f;
    CHECK(game::VehicleZombieAttackConditionDamage(tuning) ==
          doctest::Approx(0.015f));

    game::VehicleMotion vehicle;
    vehicle.condition = 0.5f;
    CHECK(game::VehicleApplyZombieAttackConditionDamage(&vehicle, tuning) ==
          doctest::Approx(0.015f));
    CHECK(vehicle.condition == doctest::Approx(0.485f));

    vehicle.condition = 0.01f;
    CHECK(game::VehicleApplyZombieAttackConditionDamage(&vehicle, tuning) ==
          doctest::Approx(0.01f));
    CHECK(vehicle.condition == doctest::Approx(0.0f));

    CHECK(game::VehicleApplyZombieAttackConditionDamage(&vehicle, tuning) ==
          doctest::Approx(0.0f));
    vehicle.condition = NAN;
    CHECK(game::VehicleApplyZombieAttackConditionDamage(&vehicle, tuning) ==
          doctest::Approx(0.0f));
    CHECK(game::VehicleApplyZombieAttackConditionDamage(nullptr, tuning) ==
          doctest::Approx(0.0f));

    tuning.zombieAttackConditionDamage = NAN;
    CHECK(game::VehicleZombieAttackConditionDamage(tuning) ==
          doctest::Approx(0.0f));
    tuning.zombieAttackConditionDamage = 2.0f;
    CHECK(game::VehicleZombieAttackConditionDamage(tuning) ==
          doctest::Approx(1.0f));
}

TEST_CASE("M25-T4-V16 VehicleHitFeedback：磨损强度/火花数量与寿命") {
    CHECK(game::VehicleHitFeedbackStrength(0.0f) == doctest::Approx(0.0f));
    CHECK(game::VehicleHitFeedbackStrength(NAN) == doctest::Approx(0.0f));
    CHECK(game::VehicleHitFeedbackStrength(-0.02f) == doctest::Approx(0.0f));
    CHECK(game::VehicleHitFeedbackStrength(0.005f) == doctest::Approx(0.25f));
    CHECK(game::VehicleHitFeedbackStrength(0.015f) == doctest::Approx(0.30f));
    CHECK(game::VehicleHitFeedbackStrength(0.05f) == doctest::Approx(1.0f));
    CHECK(game::VehicleHitFeedbackStrength(0.5f) == doctest::Approx(1.0f));

    CHECK(game::VehicleHitSparkCount(0.0f) == 0);
    CHECK(game::VehicleHitSparkCount(NAN) == 0);
    CHECK(game::VehicleHitSparkCount(0.015f) == 5);
    CHECK(game::VehicleHitSparkCount(0.05f) == 8);
    CHECK(game::VehicleHitSparkCount(0.5f) == 8);

    CHECK(game::VehicleHitSparkLifetime(0.0f) == doctest::Approx(0.0f));
    CHECK(game::VehicleHitSparkLifetime(0.015f) ==
          doctest::Approx(0.62f).epsilon(0.001f));
    CHECK(game::VehicleHitSparkLifetime(0.05f) ==
          doctest::Approx(game::kVehicleHitSparkMaxLifetime));
}

TEST_CASE("M25-T4-V17 VehicleDamageVisual：车况分级与痕迹数量") {
    using game::VehicleDamageVisualLevel;
    CHECK(game::VehicleDamageVisualLevelFor(1.0f) ==
          VehicleDamageVisualLevel::None);
    CHECK(game::VehicleDamageVisualLevelFor(0.76f) ==
          VehicleDamageVisualLevel::None);
    CHECK(game::VehicleDamageVisualLevelFor(0.75f) ==
          VehicleDamageVisualLevel::Scuffed);
    CHECK(game::VehicleDamageVisualLevelFor(0.51f) ==
          VehicleDamageVisualLevel::Scuffed);
    CHECK(game::VehicleDamageVisualLevelFor(0.50f) ==
          VehicleDamageVisualLevel::Dented);
    CHECK(game::VehicleDamageVisualLevelFor(0.26f) ==
          VehicleDamageVisualLevel::Dented);
    CHECK(game::VehicleDamageVisualLevelFor(0.25f) ==
          VehicleDamageVisualLevel::Wrecked);
    CHECK(game::VehicleDamageVisualLevelFor(0.0f) ==
          VehicleDamageVisualLevel::Wrecked);
    CHECK(game::VehicleDamageVisualLevelFor(-0.1f) ==
          VehicleDamageVisualLevel::Wrecked);
    CHECK(game::VehicleDamageVisualLevelFor(NAN) ==
          VehicleDamageVisualLevel::None);

    CHECK(game::VehicleDamageVisualMarkCount(
              VehicleDamageVisualLevel::None) == 0);
    CHECK(game::VehicleDamageVisualMarkCount(
              VehicleDamageVisualLevel::Scuffed) == 3);
    CHECK(game::VehicleDamageVisualMarkCount(
              VehicleDamageVisualLevel::Dented) == 6);
    CHECK(game::VehicleDamageVisualMarkCount(
              VehicleDamageVisualLevel::Wrecked) == 10);
    CHECK(std::string(game::VehicleDamageVisualLabel(
              VehicleDamageVisualLevel::Scuffed)) == "划痕");
    CHECK(std::string(game::VehicleDamageVisualLabel(
              VehicleDamageVisualLevel::Wrecked)) == "破损");
}

TEST_CASE("M25-T4-V18 VehicleImpactFeedback：镜头震动与金属碎片") {
    CHECK(game::VehicleHitShakeStrength(0.0f) == doctest::Approx(0.0f));
    CHECK(game::VehicleHitShakeStrength(NAN) == doctest::Approx(0.0f));
    CHECK(game::VehicleHitShakeStrength(-0.01f) == doctest::Approx(0.0f));
    CHECK(game::VehicleHitShakeStrength(0.005f) == doctest::Approx(0.25f));
    CHECK(game::VehicleHitShakeStrength(0.015f) == doctest::Approx(0.30f));
    CHECK(game::VehicleHitShakeStrength(0.05f) == doctest::Approx(1.0f));

    CHECK(game::VehicleCrashShakeStrength(2.0f) == doctest::Approx(0.0f));
    CHECK(game::VehicleCrashShakeStrength(3.0f) == doctest::Approx(0.0f));
    CHECK(game::VehicleCrashShakeStrength(6.5f) == doctest::Approx(0.625f));
    CHECK(game::VehicleCrashShakeStrength(10.0f) == doctest::Approx(1.0f));
    CHECK(game::VehicleCrashShakeStrength(NAN) == doctest::Approx(0.0f));

    CHECK(game::VehicleShakeDuration(0.0f) == doctest::Approx(0.0f));
    CHECK(game::VehicleShakeDuration(1.0f) ==
          doctest::Approx(game::kVehicleScreenShakeMaxDuration));
    CHECK(game::VehicleShakeDuration(0.30f) ==
          doctest::Approx(0.234f).epsilon(0.001f));

    const auto start = game::VehicleScreenShakeOffset(1.0f, 0.0f, 0.30f);
    CHECK(start.x == doctest::Approx(0.0f));
    CHECK(std::abs(start.y) > 0.0f);
    const auto mid = game::VehicleScreenShakeOffset(1.0f, 0.15f, 0.30f);
    CHECK(std::abs(mid.y) < std::abs(start.y));
    const auto end = game::VehicleScreenShakeOffset(1.0f, 0.30f, 0.30f);
    CHECK(end.x == doctest::Approx(0.0f));
    CHECK(end.y == doctest::Approx(0.0f));
    const auto invalid =
        game::VehicleScreenShakeOffset(NAN, 0.1f, 0.3f);
    CHECK(invalid.x == doctest::Approx(0.0f));
    CHECK(invalid.y == doctest::Approx(0.0f));

    CHECK(game::VehicleMetalDebrisCount(0.0f) == 0);
    CHECK(game::VehicleMetalDebrisCount(NAN) == 0);
    CHECK(game::VehicleMetalDebrisCount(0.30f) == 4);
    CHECK(game::VehicleMetalDebrisCount(1.0f) == 6);
    CHECK(game::VehicleMetalDebrisLifetime(0.0f) == doctest::Approx(0.0f));
    CHECK(game::VehicleMetalDebrisLifetime(0.30f) ==
          doctest::Approx(0.585f).epsilon(0.001f));
    CHECK(game::VehicleMetalDebrisLifetime(1.0f) ==
          doctest::Approx(game::kVehicleMetalDebrisMaxLifetime));
}

TEST_CASE("M25-T4-V6 VehicleCondition：低车况动力倍率与极速衰减") {
    const game::VehicleTuning tuning = game::kVehicleDefaultTuning;
    CHECK(game::VehicleConditionPerformanceScale(1.0f) ==
          doctest::Approx(1.0f));
    CHECK(game::VehicleConditionPerformanceScale(0.75f) ==
          doctest::Approx(1.0f));
    CHECK(game::VehicleConditionPerformanceScale(0.50f) ==
          doctest::Approx(0.85f));
    CHECK(game::VehicleConditionPerformanceScale(0.25f) ==
          doctest::Approx(0.70f));
    CHECK(game::VehicleConditionPerformanceScale(0.01f) ==
          doctest::Approx(0.556f).epsilon(0.001f));
    CHECK(game::VehicleConditionPerformanceScale(0.0f) ==
          doctest::Approx(0.0f));
    CHECK(game::VehicleConditionPerformanceScale(-1.0f) ==
          doctest::Approx(0.0f));
    CHECK(game::VehicleConditionPerformanceScale(NAN) ==
          doctest::Approx(0.0f));

    const auto open = [](int, int, int) { return false; };
    game::VehicleMotion full;
    game::VehicleMotion half;
    game::VehicleMotion quarter;
    game::VehicleMotion predicted;
    half.condition = 0.5f;
    quarter.condition = 0.25f;
    predicted.condition = 0.25f;
    for (int i = 0; i < 180; ++i) {
        (void)game::VehicleStep(&full, 1.0f, 0.0f, 1.0f / 60.0f, open);
        (void)game::VehicleStep(&half, 1.0f, 0.0f, 1.0f / 60.0f, open);
        (void)game::VehicleStep(&quarter, 1.0f, 0.0f, 1.0f / 60.0f, open);
        (void)game::VehiclePredictStep(&predicted, 1.0f, 0.0f, 1.0f / 60.0f,
                                       open);
    }
    CHECK(full.vx == doctest::Approx(tuning.maxSpeed));
    CHECK(half.vx == doctest::Approx(tuning.maxSpeed * 0.85f).epsilon(0.001f));
    CHECK(quarter.vx ==
          doctest::Approx(tuning.maxSpeed * 0.70f).epsilon(0.001f));
    CHECK(predicted.vx ==
          doctest::Approx(tuning.maxSpeed * 0.70f).epsilon(0.001f));
    CHECK(quarter.x < half.x);
    CHECK(half.x < full.x);
}
TEST_CASE("M25-T4-V8 VehicleEngineAudioSource：本地优先与远端/归零") {
    CHECK(game::VehicleEngineAudioSourceFor(false, false, 1.0f) ==
          game::VehicleEngineAudioSource::None);
    CHECK(game::VehicleEngineAudioSourceFor(true, false, 1.0f) ==
          game::VehicleEngineAudioSource::Local);
    CHECK(game::VehicleEngineAudioSourceFor(false, true, 1.0f) ==
          game::VehicleEngineAudioSource::Remote);
    CHECK(game::VehicleEngineAudioSourceFor(true, true, 1.0f) ==
          game::VehicleEngineAudioSource::Local);
    CHECK(game::VehicleEngineAudioSourceFor(false, true, 0.0f) ==
          game::VehicleEngineAudioSource::None);
    CHECK(game::VehicleEngineAudioSourceFor(false, true, NAN) ==
          game::VehicleEngineAudioSource::None);
}
TEST_CASE("M26-V8 VehicleEngineAudio：远端多车距离筛选与声部上限") {
    const std::vector<game::VehicleEngineAudioCandidate> candidates = {
        {1, 0.0f, 0.0f, 0.0f, 1.0f, true},
        {2, 3.0f, 4.0f, 2.0f, 0.5f, true},
        {6, 2.0f, 0.0f, 1.0f, 1.0f, true},
        {8, 10.0f, 0.0f, 3.0f, 0.8f, true},
        {9, 17.0f, 0.0f, 4.0f, 0.7f, true},
        {10, 18.0f, 0.0f, 5.0f, 0.6f, true},
        {11, 18.1f, 0.0f, 6.0f, 1.0f, true},
        {4, 1.0f, 0.0f, 0.0f, 0.0f, true},
        {5, 2.0f, 0.0f, 0.0f, 1.0f, false},
        {7, 2.0f, 0.0f, 0.0f, NAN, true},
        {1, 1.0f, 0.0f, 0.0f, 1.0f, true},
        {13, NAN, 0.0f, 0.0f, 1.0f, true},
        {0, 0.0f, 0.0f, 0.0f, 1.0f, true},
    };
    CHECK(game::SelectRemoteVehicleEngineAudio(candidates, 0.0f, 0.0f) ==
          std::vector<std::uint64_t>{1, 6, 2, 8});
    CHECK(game::SelectRemoteVehicleEngineAudio(candidates, 0.0f, 0.0f, 18.0f,
                                               2) ==
          std::vector<std::uint64_t>{1, 6});
    CHECK(game::SelectRemoteVehicleEngineAudio(candidates, 0.0f, 0.0f, 18.0f,
                                               10) ==
          std::vector<std::uint64_t>{1, 6, 2, 8, 9, 10});
    CHECK(game::SelectRemoteVehicleEngineAudio(candidates, 0.0f, 0.0f, 18.0f,
                                               0)
              .empty());
    CHECK(game::SelectRemoteVehicleEngineAudio(candidates, 0.0f, 0.0f, 0.0f, 4)
              .empty());
    CHECK(game::SelectRemoteVehicleEngineAudio(candidates, 0.0f, 0.0f, NAN, 4)
              .empty());
    CHECK(game::SelectRemoteVehicleEngineAudio(candidates, NAN, 0.0f, 18.0f, 4)
              .empty());
    CHECK(game::SelectRemoteVehicleEngineAudio(candidates, 0.0f, NAN, 18.0f, 4)
              .empty());
    CHECK(game::SelectRemoteVehicleEngineAudio({}, 0.0f, 0.0f).empty());
    const auto selected = game::SelectRemoteVehicleEngineAudio(
        candidates, 0.0f, 0.0f, 18.0f, 10);
    CHECK(selected.size() == 6);
    CHECK(selected.back() == 10);
    CHECK(std::find(selected.begin(), selected.end(), 11) == selected.end());
}