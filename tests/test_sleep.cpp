#include <doctest/doctest.h>

#include <cmath>
#include <string>

#include "characters/components/CharacterComponents.h"
#include "core/GameTime.h"
#include "game/Sleeping.h"

using namespace zl;

TEST_CASE("M18-F3 GameTime advances by sleep minutes and finds the next dawn") {
    core::GameTime time;
    time.Configure(0.4, core::DateTime{2000, 7, 9, 21, 30});
    CHECK(time.MinutesUntilHour(7) == doctest::Approx(570.0));

    time.AdvanceGameMinutes(600.0);
    CHECK(time.TotalGameMinutes() == doctest::Approx(600.0));
    CHECK(time.Now().day == 10);
    CHECK(time.Now().hour == 7);
    CHECK(time.Now().minute == 30);

    CHECK(time.MinutesUntilHour(7) == doctest::Approx(1410.0));
    time.AdvanceGameMinutes(1.0);
    CHECK(time.Now().minute == 31);
}

TEST_CASE("M18-F3 SleepSession tracks duration, partial progress, and completion") {
    game::SleepSession sleep;
    CHECK_FALSE(sleep.Active());
    sleep.Start(120.0, 360.0);
    REQUIRE(sleep.Active());
    CHECK(sleep.StartMinutes() == doctest::Approx(120.0));
    CHECK(sleep.TargetMinutes() == doctest::Approx(480.0));
    CHECK(sleep.DurationMinutes() == doctest::Approx(360.0));
    CHECK(sleep.RemainingMinutes() == doctest::Approx(360.0));

    sleep.Advance(90.0);
    CHECK(sleep.ElapsedMinutes() == doctest::Approx(90.0));
    CHECK(sleep.RemainingMinutes() == doctest::Approx(270.0));
    CHECK(sleep.Progress() == doctest::Approx(0.25f));
    CHECK_FALSE(sleep.Completed());

    sleep.Advance(500.0);
    CHECK_FALSE(sleep.Active());
    CHECK(sleep.Completed());
    CHECK(sleep.ElapsedMinutes() == doctest::Approx(360.0));
    CHECK(sleep.RemainingMinutes() == doctest::Approx(0.0));
}

TEST_CASE("M18-F3 SleepSession rejects invalid durations and stop is not completion") {
    game::SleepSession sleep;
    sleep.Start(10.0, 0.0);
    CHECK_FALSE(sleep.Active());
    sleep.Start(10.0, -5.0);
    CHECK_FALSE(sleep.Active());

    sleep.Start(10.0, 60.0);
    REQUIRE(sleep.Active());
    sleep.Stop();
    CHECK_FALSE(sleep.Active());
    CHECK_FALSE(sleep.Completed());
}

TEST_CASE("M18-F3 sleep advances hunger/thirst and recovers fatigue") {
    ecs::Needs needs{0.2f, 0.2f, 0.8f};
    game::ApplySleepNeeds(needs, 1.4f, 1.8f, 360.0f, 0.15f);
    CHECK(needs.hunger == doctest::Approx(0.55f));
    CHECK(needs.thirst == doctest::Approx(0.65f));
    CHECK(needs.fatigue == doctest::Approx(0.0f));

    game::ApplySleepNeeds(needs, 1.4f, 1.8f, 0.0f, 0.15f);
    CHECK(needs.hunger == doctest::Approx(0.55f));
    game::ApplySleepNeeds(needs, -1.0f, -1.0f, 60.0f, -1.0f);
    CHECK(needs.hunger == doctest::Approx(0.55f));
    CHECK(needs.thirst == doctest::Approx(0.65f));
    CHECK(needs.fatigue == doctest::Approx(0.0f));
}

TEST_CASE("M18-F3 sleep stop reasons have stable user-facing text") {
    CHECK(std::string(game::SleepStopText(game::SleepStopReason::Completed)) ==
          "睡醒了");
    CHECK(std::string(game::SleepStopText(game::SleepStopReason::ZombieNear)) ==
          "附近有僵尸");
    CHECK(std::string(game::SleepStopText(game::SleepStopReason::None)) ==
          "未知原因");
}
