#include "core/GameTime.h"

#include <cstdio>

#include "core/Constants.h"

namespace zl::core {
namespace {

constexpr int kMinutesPerHour = 60;
constexpr int kHoursPerDay = 24;
constexpr int kMonthsPerYear = 12;
constexpr int kNightStartHour = 20;
constexpr int kDayStartHour = 7;

constexpr int kMonthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

bool IsLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

}  // namespace

int DaysInMonth(int year, int month) {
    if (month < 1 || month > kMonthsPerYear) {
        return 30;
    }
    if (month == 2 && IsLeapYear(year)) {
        return 29;
    }
    return kMonthDays[month - 1];
}

std::string FormatDateTime(const DateTime& dt) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", dt.year, dt.month,
                  dt.day, dt.hour, dt.minute);
    return buf;
}

void GameTime::Configure(double gameMinutesPerRealSecond, const DateTime& start) {
    m_minutesPerTick = gameMinutesPerRealSecond * kTickDt;
    m_now = start;
    m_totalMinutes = 0.0;
    m_minuteFraction = 0.0;
}

void GameTime::Tick() {
    m_minuteFraction += m_minutesPerTick;
    while (m_minuteFraction >= 1.0) {
        m_minuteFraction -= 1.0;
        AdvanceMinute();
    }
    m_totalMinutes += m_minutesPerTick;
}

void GameTime::AdvanceMinute() {
    if (++m_now.minute < kMinutesPerHour) {
        return;
    }
    m_now.minute = 0;
    if (++m_now.hour < kHoursPerDay) {
        return;
    }
    m_now.hour = 0;
    if (++m_now.day <= DaysInMonth(m_now.year, m_now.month)) {
        return;
    }
    m_now.day = 1;
    if (++m_now.month <= kMonthsPerYear) {
        return;
    }
    m_now.month = 1;
    ++m_now.year;
}

float GameTime::DayFraction() const {
    const int minutesOfDay = m_now.hour * kMinutesPerHour + m_now.minute;
    return static_cast<float>(minutesOfDay) /
           static_cast<float>(kHoursPerDay * kMinutesPerHour);
}

bool GameTime::IsNight() const {
    return m_now.hour < kDayStartHour || m_now.hour >= kNightStartHour;
}

double GameTime::SpeedScale() const {
    switch (m_speed) {
        case Speed::Paused: return 0.0;
        case Speed::X1: return 1.0;
        case Speed::X2: return 2.0;
        case Speed::X3: return 3.0;
    }
    return 1.0;
}

const char* GameTime::SpeedName(Speed speed) {
    switch (speed) {
        case Speed::Paused: return "暂停";
        case Speed::X1: return "1x";
        case Speed::X2: return "2x";
        case Speed::X3: return "3x";
    }
    return "?";
}

void GameTime::SetSpeed(Speed speed) {
    m_speed = speed;
}

void GameTime::CycleSpeed() {
    switch (m_speed) {
        case Speed::Paused: m_speed = Speed::X1; break;
        case Speed::X1: m_speed = Speed::X2; break;
        case Speed::X2: m_speed = Speed::X3; break;
        case Speed::X3: m_speed = Speed::X1; break;
    }
}

}  // namespace zl::core
