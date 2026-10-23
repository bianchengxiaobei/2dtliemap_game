#pragma once

#include <cstdint>

#include "characters/components/CharacterComponents.h"

namespace zl::game {

inline constexpr float kZombieHousePoiX0 = 67.0f;
inline constexpr float kZombieHousePoiX1 = 79.0f;
inline constexpr float kZombieHousePoiY0 = 16.0f;
inline constexpr float kZombieHousePoiY1 = 28.0f;
inline constexpr int kZombieNightStartHour = 20;
inline constexpr int kZombieNightEndHour = 6;

struct ZombieVariantProfile {
    float healthScale = 1.0f;
    float speedScale = 1.0f;
    float damageScale = 1.0f;
    float visualScale = 1.0f;
    std::uint32_t tint = 0xFFFFFFFFu;
};

struct ZombieVariantWeights {
    int normal = 70;
    int fast = 20;
    int strong = 10;

    int Total() const { return normal + fast + strong; }
};

inline const ZombieVariantProfile& ZombieVariantProfileFor(ecs::ZombieVariantKind kind) {
    static constexpr ZombieVariantProfile kNormal{1.0f, 1.0f, 1.0f, 1.0f, 0xFFFFFFFFu};
    static constexpr ZombieVariantProfile kFast{0.7f, 1.55f, 0.8f, 0.9f, 0xFF9FC8FFu};
    static constexpr ZombieVariantProfile kStrong{1.7f, 0.78f, 1.55f, 1.16f, 0xFFFF8070u};
    switch (kind) {
        case ecs::ZombieVariantKind::Fast:
            return kFast;
        case ecs::ZombieVariantKind::Strong:
            return kStrong;
        case ecs::ZombieVariantKind::Normal:
        default:
            return kNormal;
    }
}

inline const char* ZombieVariantName(ecs::ZombieVariantKind kind) {
    switch (kind) {
        case ecs::ZombieVariantKind::Fast:
            return "fast";
        case ecs::ZombieVariantKind::Strong:
            return "strong";
        case ecs::ZombieVariantKind::Normal:
        default:
            return "normal";
    }
}

inline bool IsZombieHousePoi(float x, float y) {
    return x >= kZombieHousePoiX0 && x <= kZombieHousePoiX1 &&
           y >= kZombieHousePoiY0 && y <= kZombieHousePoiY1;
}

inline bool IsZombieNightHour(int hour) {
    return hour >= kZombieNightStartHour || hour < kZombieNightEndHour;
}

inline ZombieVariantWeights ZombieVariantWeightsFor(float x, float y, int hour) {
    const bool house = IsZombieHousePoi(x, y);
    const bool night = IsZombieNightHour(hour);
    if (house && night) {
        return ZombieVariantWeights{35, 30, 35};
    }
    if (house) {
        return ZombieVariantWeights{55, 25, 20};
    }
    if (night) {
        return ZombieVariantWeights{45, 35, 20};
    }
    return ZombieVariantWeights{};
}

inline std::uint64_t MixZombieVariantSeed(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31U);
}

inline std::uint64_t HashZombieVariant(float x, float y, int hour, std::uint64_t seed) {
    const std::int32_t gx = static_cast<std::int32_t>(x * 4.0f);
    const std::int32_t gy = static_cast<std::int32_t>(y * 4.0f);
    std::uint64_t hash = seed ^ 0x9E3779B97F4A7C15ull;
    hash ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(gx)) *
            0xBF58476D1CE4E5B9ull;
    hash ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(gy)) *
            0x94D049BB133111EBull;
    hash ^= static_cast<std::uint64_t>(hour) * 0xD6E8FEB86659FD93ull;
    return MixZombieVariantSeed(hash);
}

inline ecs::ZombieVariantKind PickZombieVariant(float x, float y, int hour,
                                                std::uint64_t seed) {
    const ZombieVariantWeights weights = ZombieVariantWeightsFor(x, y, hour);
    const int total = weights.Total();
    const std::uint64_t roll = HashZombieVariant(x, y, hour, seed) % static_cast<std::uint64_t>(total);
    if (roll < static_cast<std::uint64_t>(weights.normal)) {
        return ecs::ZombieVariantKind::Normal;
    }
    if (roll < static_cast<std::uint64_t>(weights.normal + weights.fast)) {
        return ecs::ZombieVariantKind::Fast;
    }
    return ecs::ZombieVariantKind::Strong;
}

inline ecs::ZombieVariant MakeZombieVariant(ecs::ZombieVariantKind kind) {
    const ZombieVariantProfile& profile = ZombieVariantProfileFor(kind);
    ecs::ZombieVariant variant;
    variant.kind = kind;
    variant.speedScale = profile.speedScale;
    variant.damageScale = profile.damageScale;
    variant.visualScale = profile.visualScale;
    return variant;
}

}  // namespace zl::game
