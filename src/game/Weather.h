#pragma once

#include <cstdint>

namespace zl::game {

// M20-F2：天气由游戏时间确定性驱动，不保存额外随机状态。
// 同一存档时间重建同一段天气；强度在块边界按 transitionMinutes 插值，避免硬切。
enum class WeatherKind : std::uint8_t {
    Clear = 0,
    Rain = 1,
    Fog = 2,
    ColdSnap = 3,
};

struct WeatherParams {
    bool enabled = true;
    std::uint32_t seed = 20261024u;
    float periodMinutes = 180.0f;
    float transitionMinutes = 30.0f;

    float clearWeight = 0.45f;
    float rainWeight = 0.25f;
    float fogWeight = 0.17f;
    float coldSnapWeight = 0.13f;

    float clearTemperatureC = 24.0f;
    float rainTemperatureC = 13.0f;
    float fogTemperatureC = 16.0f;
    float coldSnapTemperatureC = -6.0f;
    float nightTemperatureDropC = 6.0f;

    float rainSightScale = 0.80f;
    float fogSightScale = 0.55f;
    float coldSnapSightScale = 0.90f;
    float rainMoveScale = 0.95f;
    float fogMoveScale = 0.90f;
    float coldSnapMoveScale = 0.88f;

    float rainWetness = 0.80f;
    float fogWetness = 0.18f;
    float coldSnapWetness = 0.08f;
};

struct WeatherState {
    WeatherKind kind = WeatherKind::Clear;
    float intensity = 1.0f;
    float ambientC = 24.0f;
    float rainIntensity = 0.0f;
    float wetnessIntensity = 0.0f;
    float fogDensity = 0.0f;
    float sightScale = 1.0f;
    float moveScale = 1.0f;
    bool night = false;
};

class WeatherSystem {
public:
    explicit WeatherSystem(WeatherParams params = {});

    // totalGameMinutes：从开局起累计游戏分钟；hour：0~23。
    // 第一个天气块固定晴，避免新档开局立刻淋雨。
    WeatherState Sample(double totalGameMinutes, int hour) const;

    // M24-T4：事件临时覆盖；until 为累计游戏分钟。覆盖不写入存档。
    void SetOverride(WeatherKind kind, double untilTotalGameMinutes);
    void ClearOverride();

    const WeatherParams& Params() const { return m_params; }

    static const char* KindName(WeatherKind kind);
    static const char* KindClass(WeatherKind kind);

private:
    WeatherParams m_params;
    WeatherKind m_overrideKind = WeatherKind::Clear;
    double m_overrideUntil = -1.0;
};

}  // namespace zl::game
