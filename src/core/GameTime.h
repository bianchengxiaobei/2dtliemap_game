#pragma once

#include <cstdint>
#include <string>

namespace zl::core {

struct DateTime {
    int year = 2000;
    int month = 7;
    int day = 9;
    int hour = 0;
    int minute = 0;
};

std::string FormatDateTime(const DateTime& dt);  // "2000-07-09 08:35"
int DaysInMonth(int year, int month);

// 游戏日历（对齐 PZ GameTime 思路）：tick → 游戏分钟；倍速档位；白天/黑夜判定。
class GameTime {
public:
    enum class Speed { Paused = 0, X1, X2, X3 };

    // gameMinutesPerRealSecond：现实 1 秒 = 游戏多少分钟（由"一天多少现实分钟"换算）
    void Configure(double gameMinutesPerRealSecond, const DateTime& start);

    void Tick();  // 推进一个固定 tick

    const DateTime& Now() const { return m_now; }
    double TotalGameMinutes() const { return m_totalMinutes; }
    float DayFraction() const;               // 当天 0..1
    bool IsNight() const;                    // 简化规则：20:00~次日 07:00

    Speed GetSpeed() const { return m_speed; }
    double SpeedScale() const;
    static const char* SpeedName(Speed speed);
    void SetSpeed(Speed speed);
    void CycleSpeed();  // 暂停→1x→2x→3x→暂停（F2）

private:
    void AdvanceMinute();

    DateTime m_now;
    double m_totalMinutes = 0.0;
    double m_minutesPerTick = 0.0;
    double m_minuteFraction = 0.0;
    Speed m_speed = Speed::X1;
};

}  // namespace zl::core
