#pragma once

#include <cstdint>

#include "characters/components/CharacterComponents.h"

namespace zl::game {

struct SleepConfig {
    int minHours = 1;
    int maxHours = 8;
    int dawnHour = 7;
    float gameMinutesPerSecond = 120.0f;
    float fatigueRecoveryPerHour = 0.15f;
    float zombieRadius = 8.0f;
    bool interruptOnDamage = true;
    bool interruptOnZombie = true;
};

enum class SleepStopReason : std::uint8_t {
    None = 0,
    Completed,
    Cancelled,
    Damaged,
    ZombieNear,
    SiegeStarted,
    NeedsCritical,
    Died,
};

const char* SleepStopText(SleepStopReason reason);

class SleepSession {
public:
    void Start(double startMinutes, double durationMinutes);
    void Stop();
    void Advance(double minutes);

    bool Active() const { return m_active; }
    bool Completed() const { return m_completed; }
    double StartMinutes() const { return m_startMinutes; }
    double TargetMinutes() const { return m_startMinutes + m_durationMinutes; }
    double DurationMinutes() const { return m_durationMinutes; }
    double ElapsedMinutes() const { return m_elapsedMinutes; }
    double RemainingMinutes() const;
    float Progress() const;

private:
    bool m_active = false;
    bool m_completed = false;
    double m_startMinutes = 0.0;
    double m_durationMinutes = 0.0;
    double m_elapsedMinutes = 0.0;
};

void ApplySleepNeeds(ecs::Needs& needs, float hungerPerDay, float thirstPerDay,
                     float gameMinutes, float fatigueRecoveryPerHour);

}
