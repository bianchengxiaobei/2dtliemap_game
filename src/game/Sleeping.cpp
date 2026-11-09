#include "game/Sleeping.h"

#include <algorithm>
#include <cmath>

namespace zl::game {
namespace {

float Clamp01(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

float NonNegative(float value) {
    return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
}

}

const char* SleepStopText(SleepStopReason reason) {
    switch (reason) {
        case SleepStopReason::Completed: return "睡醒了";
        case SleepStopReason::Cancelled: return "玩家取消";
        case SleepStopReason::Damaged: return "受到伤害";
        case SleepStopReason::ZombieNear: return "附近有僵尸";
        case SleepStopReason::SiegeStarted: return "守夜事件开始";
        case SleepStopReason::NeedsCritical: return "饥饿或脱水";
        case SleepStopReason::Died: return "死亡";
        case SleepStopReason::None: break;
    }
    return "未知原因";
}

void SleepSession::Start(double startMinutes, double durationMinutes) {
    m_active = false;
    m_completed = false;
    m_startMinutes = 0.0;
    m_durationMinutes = 0.0;
    m_elapsedMinutes = 0.0;
    if (!std::isfinite(startMinutes) || startMinutes < 0.0 ||
        !std::isfinite(durationMinutes) || durationMinutes <= 0.0) {
        return;
    }
    m_startMinutes = startMinutes;
    m_durationMinutes = durationMinutes;
    m_active = true;
}

void SleepSession::Stop() {
    m_active = false;
    m_completed = false;
}

void SleepSession::Advance(double minutes) {
    if (!m_active || !std::isfinite(minutes) || minutes <= 0.0) {
        return;
    }
    m_elapsedMinutes =
        std::min(m_durationMinutes, m_elapsedMinutes + minutes);
    if (m_elapsedMinutes >= m_durationMinutes - 1e-9) {
        m_elapsedMinutes = m_durationMinutes;
        m_active = false;
        m_completed = true;
    }
}

double SleepSession::RemainingMinutes() const {
    return std::max(0.0, m_durationMinutes - m_elapsedMinutes);
}

float SleepSession::Progress() const {
    if (!(m_durationMinutes > 0.0)) {
        return 0.0f;
    }
    return static_cast<float>(
        std::clamp(m_elapsedMinutes / m_durationMinutes, 0.0, 1.0));
}

void ApplySleepNeeds(ecs::Needs& needs, float hungerPerDay, float thirstPerDay,
                     float gameMinutes, float fatigueRecoveryPerHour) {
    if (!std::isfinite(gameMinutes) || gameMinutes <= 0.0f) {
        return;
    }
    const float days = gameMinutes / 1440.0f;
    needs.hunger =
        Clamp01(needs.hunger + NonNegative(hungerPerDay) * days);
    needs.thirst =
        Clamp01(needs.thirst + NonNegative(thirstPerDay) * days);
    needs.fatigue =
        Clamp01(needs.fatigue -
                NonNegative(fatigueRecoveryPerHour) * gameMinutes / 60.0f);
}

}
