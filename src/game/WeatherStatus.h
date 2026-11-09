#pragma once

#include <cmath>
#include <cstdio>
#include <string>

#include "characters/components/CharacterComponents.h"
#include "game/Weather.h"

namespace zl::game {

inline std::string WeatherStatusText(const WeatherState& weather,
                                     const ecs::Climate& climate,
                                     std::string* tone = nullptr,
                                     std::string* overlayClass = nullptr) {
    if (tone != nullptr) {
        if (climate.bodyC < 35.0f) {
            *tone = "fail";
        } else if (climate.bodyC < 36.0f || climate.wetness >= 0.60f) {
            *tone = "warn";
        } else {
            *tone = "info";
        }
    }
    if (overlayClass != nullptr) {
        *overlayClass = WeatherSystem::KindClass(weather.kind);
    }

    char text[160];
    std::snprintf(text, sizeof(text), "天气：%s %d°C · 体温 %.1f°C",
                  WeatherSystem::KindName(weather.kind),
                  static_cast<int>(std::lround(weather.ambientC)),
                  static_cast<double>(climate.bodyC));
    std::string out = text;
    if (climate.wetness > 0.01f) {
        char wet[32];
        std::snprintf(wet, sizeof(wet), " · 潮湿 %d%%",
                      static_cast<int>(std::lround(climate.wetness * 100.0f)));
        out += wet;
    }
    if (climate.indoor) {
        out += " · 室内";
    }
    if (climate.nearFire) {
        out += " · 火边";
    }
    return out;
}

}  // namespace zl::game
