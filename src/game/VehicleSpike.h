#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string_view>
#include <utility>
#include <vector>

#include "game/VehicleComponents.h"

namespace zl::game {

struct VehicleMotion {
    float x = 0.0f;
    float y = 0.0f;
    int z = 0;
    float vx = 0.0f;
    float vy = 0.0f;
    float fuel = 1.0f;
    float condition = 1.0f;
    // M26-V12：车身朝向（弧度，0=+X）与角速度；碰撞力矩直接改变朝向。
    float heading = 0.0f;
    float angularVelocity = 0.0f;
    // M26-V13：前轮转角、轮胎横向加速度与悬挂车身姿态（运行时，不存档/不复制）。
    float steerAngle = 0.0f;
    float lateralAcceleration = 0.0f;
    float longitudinalAcceleration = 0.0f;
    float bodyRoll = 0.0f;
    float bodyRollVelocity = 0.0f;
    float bodyPitch = 0.0f;
    float bodyPitchVelocity = 0.0f;
    ecs::VehicleType type = ecs::VehicleType::Pickup;
    bool crashLatched = false;
    bool headlights = false;
};

inline constexpr float kVehicleMaxSpeed = 10.0f;
inline constexpr float kVehicleAcceleration = 8.0f;
inline constexpr float kVehicleBraking = 14.0f;
inline constexpr float kVehicleRadius = 0.65f;
inline constexpr float kVehicleEnterRange = 2.2f;
inline constexpr float kVehicleFuelPerSecond = 0.012f;
inline constexpr float kVehicleCrashMinSpeed = 3.0f;
inline constexpr float kVehicleCrashDamagePerSpeed = 0.08f;
inline constexpr float kVehicleMaxCrashDamage = 0.56f;
inline constexpr float kVehicleRepairAmount = 0.40f;
inline constexpr float kVehicleFuelPerCan = 0.50f;
inline constexpr float kVehicleRamMinSpeed = 2.0f;
inline constexpr float kVehicleRamBaseDamage = 20.0f;
inline constexpr float kVehicleRamDamagePerSpeed = 18.0f;
inline constexpr float kVehicleRamKnockbackPerSpeed = 0.45f;
inline constexpr float kVehicleRamKnockbackBase = 1.2f;
inline constexpr float kVehicleRamStaggerBase = 0.25f;
inline constexpr float kVehicleRamStaggerPerSpeed = 0.02f;
inline constexpr float kVehicleRamCooldownSeconds = 0.75f;
inline constexpr float kVehicleRamConditionDamageBase = 0.015f;
inline constexpr float kVehicleRamConditionDamagePerSpeed = 0.008f;
inline constexpr float kVehicleRamConditionDamageMax = 0.12f;
inline constexpr float kVehicleCollisionMinSpeed = 2.0f;
// M26-V14：低于车损门槛的持续接触仍传递速度；阈值取很小但不为零，避免
// 静止叠车时每 tick 互相推挤。
inline constexpr float kVehicleContactMinSpeed = 0.15f;
inline constexpr float kVehicleCollisionConditionDamageBase = 0.010f;
inline constexpr float kVehicleCollisionConditionDamagePerSpeed = 0.008f;
inline constexpr float kVehicleCollisionConditionDamageMax = 0.10f;
// M26-V10：载具间碰撞对车内乘员的生命伤害。
inline constexpr float kVehicleCollisionOccupantDamageBase = 3.0f;
inline constexpr float kVehicleCollisionOccupantDamagePerSpeed = 1.2f;
inline constexpr float kVehicleCollisionOccupantDamageMax = 20.0f;
inline constexpr float kVehicleCollisionCooldownSeconds = 0.75f;
inline constexpr float kVehicleCollisionPushPerSpeed = 0.12f;
inline constexpr float kVehicleCollisionPushMax = 0.80f;
// M26-V9：等质量载具碰撞的恢复系数与无人车残余滑行减速。
inline constexpr float kVehicleCollisionRestitution = 0.20f;
inline constexpr float kVehicleCoastBraking = 3.0f;
inline constexpr float kVehicleCoastStopSpeed = 0.05f;
inline constexpr float kVehicleMassLight = 1100.0f;
inline constexpr float kVehicleMassMedium = 1600.0f;
inline constexpr float kVehicleMassHeavy = 2400.0f;
inline constexpr float kVehicleCollisionFriction = 0.35f;
inline constexpr float kVehicleAngularDamping = 3.0f;
inline constexpr float kVehicleMaxAngularVelocity = 6.0f;
inline constexpr float kVehicleTurnRate = 3.2f;
inline constexpr float kVehicleLateralGrip = 8.0f;
inline constexpr float kVehicleHeadingSnapSpeed = 0.35f;
inline constexpr float kVehicleHeadingCorrectionBlend = 0.20f;
// M26-V13：前轮转角/轮胎横向加速度上限与悬挂姿态。数值是代码基线，
// 车型表可覆盖；姿态只影响运行时表现，不写存档、不复制。
inline constexpr float kVehicleWheelbase = 1.15f;
inline constexpr float kVehicleTrackWidth = 0.90f;
inline constexpr float kVehicleMaxSteerAngle = 0.85f;
inline constexpr float kVehicleMaxLateralAcceleration = 18.0f;
inline constexpr float kVehicleSuspensionFrequency = 1.75f;
inline constexpr float kVehicleSuspensionDampingRatio = 0.65f;
inline constexpr float kVehicleSuspensionRollGain = 1.0f;
inline constexpr float kVehicleSuspensionPitchGain = 1.0f;
inline constexpr float kVehicleBodyMaxRoll = 0.12f;
inline constexpr float kVehicleBodyMaxPitch = 0.06f;
inline constexpr float kVehicleDirtLateralAccelerationScale = 0.90f;
inline constexpr float kVehicleGrassLateralAccelerationScale = 0.62f;
// M25-T4-V9：驾驶位对僵尸爪/咬的减伤；车辆仍是可被攻击的载具，不做完全免疫。
inline constexpr float kVehicleDriverDamageReduction = 0.85f;
// M25-T4-V15：僵尸每次成功爪/咬驾驶位时对车况的固定磨损与结算间隔。
inline constexpr float kVehicleZombieAttackConditionDamage = 0.015f;
inline constexpr float kVehicleZombieAttackCooldownSeconds = 0.75f;
// M25-T4-V24：驾驶员只能把驾驶位交给乘客，且交接时车辆必须停稳。
inline constexpr float kVehicleSeatHandoffMaxSpeed = 0.10f;
inline constexpr float kVehicleHitFeedbackReferenceDamage = 0.05f;
inline constexpr float kVehicleHitSparkMinLifetime = 0.50f;
inline constexpr float kVehicleHitSparkMaxLifetime = 0.90f;
// M25-T4-V18：受击/碰撞的短促镜头反馈与金属碎片。
inline constexpr float kVehicleScreenShakeMaxOffset = 5.0f;
inline constexpr float kVehicleScreenShakeMinDuration = 0.18f;
inline constexpr float kVehicleScreenShakeMaxDuration = 0.36f;
inline constexpr float kVehicleMetalDebrisMinLifetime = 0.45f;
inline constexpr float kVehicleMetalDebrisMaxLifetime = 0.90f;
inline constexpr float kVehicleConditionFullPerformance = 0.75f;
inline constexpr float kVehicleConditionMinPerformance = 0.55f;
inline constexpr float kVehicleNoiseRadius = 12.0f;
inline constexpr float kVehicleNoiseIntervalSeconds = 0.45f;
inline constexpr float kVehicleNoiseMinSpeed = 1.5f;
inline constexpr float kVehicleHornNoiseRadius = 24.0f;
inline constexpr float kVehicleHornCooldownSeconds = 1.5f;
inline constexpr float kVehicleDirtMaxSpeedScale = 0.92f;
inline constexpr float kVehicleDirtAccelerationScale = 0.88f;
inline constexpr float kVehicleDirtBrakingScale = 0.92f;
inline constexpr float kVehicleGrassMaxSpeedScale = 0.62f;
inline constexpr float kVehicleGrassAccelerationScale = 0.55f;
inline constexpr float kVehicleGrassBrakingScale = 0.72f;
inline constexpr float kVehicleEngineIdleVolume = 0.18f;
inline constexpr float kVehicleEngineMaxVolume = 0.62f;
inline constexpr float kVehicleEngineMinPitch = 0.82f;
inline constexpr float kVehicleEngineMaxPitch = 1.28f;
inline constexpr float kVehicleEngineAudioMaxDistance = 18.0f;
inline constexpr int kVehicleRemoteEngineMaxVoices = 4;
inline constexpr float kVehicleRamSoundVolume = 0.90f;
inline constexpr float kVehicleSampleAngle = 0.7853981633974483f;
inline constexpr int kVehicleSpawnHalfExtent = 1;
inline constexpr int kVehicleSpawnOffsetX = 6;
inline constexpr int kVehicleSpawnOffsetY = 6;
inline constexpr int kVehicleSpawnSearchRadius = 3;

enum class VehicleSurface {
    Paved,
    Dirt,
    Grass,
};

enum class VehicleMassClass : std::uint8_t {
    Light = 0,
    Medium = 1,
    Heavy = 2,
};

// M26-V12：显式车型数据。质量使用 kg，半径/尺寸使用格；性能倍率相对基础
// VehicleTuning，损伤倍率用于车况与乘员伤害。所有客户端/服务器共享这张表。
struct VehicleProfile {
    ecs::VehicleType type = ecs::VehicleType::Pickup;
    std::string_view name = "标准皮卡";
    std::string_view shortName = "皮卡";
    float curbMassKg = kVehicleMassMedium;
    float radius = kVehicleRadius;
    float length = 2.35f;
    float width = 1.10f;
    float maxSpeedScale = 1.0f;
    float accelerationScale = 1.0f;
    float brakingScale = 1.0f;
    float turnRate = kVehicleTurnRate;
    float lateralGrip = kVehicleLateralGrip;
    float conditionDamageScale = 1.0f;
    float occupantDamageScale = 1.0f;
    float wheelbase = kVehicleWheelbase;
    float trackWidth = kVehicleTrackWidth;
    float maxSteerAngle = kVehicleMaxSteerAngle;
    float maxLateralAcceleration = kVehicleMaxLateralAcceleration;
    float suspensionFrequency = kVehicleSuspensionFrequency;
    float suspensionDampingRatio = kVehicleSuspensionDampingRatio;
    float suspensionRollGain = kVehicleSuspensionRollGain;
    float suspensionPitchGain = kVehicleSuspensionPitchGain;
};

inline constexpr VehicleProfile kVehicleProfileLight{
    ecs::VehicleType::LightPickup, "轻型皮卡", "轻卡", kVehicleMassLight,
    0.60f, 2.10f, 1.00f, 1.15f, 1.15f, 1.05f, 3.8f, 7.5f, 1.12f, 1.10f,
    1.02f, 0.84f, 0.92f, 20.0f, 1.90f, 0.62f, 1.10f, 1.00f};
inline constexpr VehicleProfile kVehicleProfilePickup{
    ecs::VehicleType::Pickup, "标准皮卡", "皮卡", kVehicleMassMedium,
    0.65f, 2.35f, 1.10f, 1.00f, 1.00f, 1.00f, 3.2f, 8.0f, 1.00f, 1.00f,
    1.15f, 0.90f, 0.85f, 18.0f, 1.75f, 0.65f, 1.00f, 1.00f};
inline constexpr VehicleProfile kVehicleProfileHeavy{
    ecs::VehicleType::HeavyPickup, "重型皮卡", "重卡", kVehicleMassHeavy,
    0.65f, 2.60f, 1.20f, 0.88f, 0.82f, 0.95f, 2.6f, 9.0f, 0.82f, 0.85f,
    1.28f, 0.98f, 0.72f, 15.0f, 1.55f, 0.70f, 0.90f, 0.90f};

inline const VehicleProfile& VehicleProfileFor(ecs::VehicleType type) {
    switch (type) {
        case ecs::VehicleType::LightPickup:
            return kVehicleProfileLight;
        case ecs::VehicleType::HeavyPickup:
            return kVehicleProfileHeavy;
        case ecs::VehicleType::Pickup:
        default:
            return kVehicleProfilePickup;
    }
}

inline bool VehicleTypeIsValid(ecs::VehicleType type) {
    return type == ecs::VehicleType::LightPickup ||
           type == ecs::VehicleType::Pickup ||
           type == ecs::VehicleType::HeavyPickup;
}

inline ecs::VehicleType VehicleTypeFromByte(std::uint8_t value) {
    switch (value) {
        case 0:
            return ecs::VehicleType::LightPickup;
        case 2:
            return ecs::VehicleType::HeavyPickup;
        case 1:
        default:
            return ecs::VehicleType::Pickup;
    }
}

inline ecs::VehicleType VehicleTypeForStableId(std::uint64_t stableId) {
    if (stableId == 0) {
        return ecs::VehicleType::Pickup;
    }
    switch (stableId % 3) {
        case 0:
            return ecs::VehicleType::LightPickup;
        case 2:
            return ecs::VehicleType::HeavyPickup;
        case 1:
        default:
            return ecs::VehicleType::Pickup;
    }
}

inline VehicleMassClass VehicleMassClassForType(ecs::VehicleType type) {
    switch (type) {
        case ecs::VehicleType::LightPickup:
            return VehicleMassClass::Light;
        case ecs::VehicleType::HeavyPickup:
            return VehicleMassClass::Heavy;
        case ecs::VehicleType::Pickup:
        default:
            return VehicleMassClass::Medium;
    }
}

inline VehicleMassClass VehicleMassClassForStableId(std::uint64_t stableId) {
    return VehicleMassClassForType(VehicleTypeForStableId(stableId));
}

inline VehicleMassClass VehicleMassClassFor(ecs::VehicleType type) {
    return VehicleMassClassForType(type);
}

inline float VehicleMassForClass(VehicleMassClass massClass) {
    switch (massClass) {
        case VehicleMassClass::Light:
            return kVehicleMassLight;
        case VehicleMassClass::Heavy:
            return kVehicleMassHeavy;
        case VehicleMassClass::Medium:
        default:
            return kVehicleMassMedium;
    }
}

inline float VehicleMassForType(ecs::VehicleType type) {
    return VehicleProfileFor(type).curbMassKg;
}

inline float VehicleMassForStableId(std::uint64_t stableId) {
    return VehicleMassForType(VehicleTypeForStableId(stableId));
}

inline std::string_view VehicleTypeName(ecs::VehicleType type) {
    return VehicleProfileFor(type).name;
}

inline std::string_view VehicleTypeShortName(ecs::VehicleType type) {
    return VehicleProfileFor(type).shortName;
}

inline float VehicleRadiusForType(ecs::VehicleType type) {
    return VehicleProfileFor(type).radius;
}

inline std::string_view VehicleMassLabel(VehicleMassClass massClass) {
    switch (massClass) {
        case VehicleMassClass::Light:
            return "轻";
        case VehicleMassClass::Heavy:
            return "重";
        case VehicleMassClass::Medium:
        default:
            return "标准";
    }
}

inline std::string_view VehicleMassLabelForType(ecs::VehicleType type) {
    return VehicleMassLabel(VehicleMassClassForType(type));
}

enum class VehicleDamageSmokeLevel : std::uint8_t {
    None = 0,
    Light = 1,
    Heavy = 2,
    Critical = 3,
};

inline VehicleDamageSmokeLevel VehicleDamageSmokeLevelFor(float condition) {
    if (!std::isfinite(condition) || condition > 0.50f) {
        return VehicleDamageSmokeLevel::None;
    }
    if (condition > 0.25f) {
        return VehicleDamageSmokeLevel::Light;
    }
    if (condition > 0.0f) {
        return VehicleDamageSmokeLevel::Heavy;
    }
    return VehicleDamageSmokeLevel::Critical;
}

inline float VehicleDamageSmokeInterval(VehicleDamageSmokeLevel level) {
    switch (level) {
        case VehicleDamageSmokeLevel::Light:
            return 0.55f;
        case VehicleDamageSmokeLevel::Heavy:
            return 0.28f;
        case VehicleDamageSmokeLevel::Critical:
            return 0.14f;
        case VehicleDamageSmokeLevel::None:
        default:
            return 0.0f;
    }
}

inline float VehicleDamageSmokeLifetime(VehicleDamageSmokeLevel level) {
    switch (level) {
        case VehicleDamageSmokeLevel::Light:
            return 1.50f;
        case VehicleDamageSmokeLevel::Heavy:
            return 1.90f;
        case VehicleDamageSmokeLevel::Critical:
            return 2.40f;
        case VehicleDamageSmokeLevel::None:
        default:
            return 0.0f;
    }
}

inline const char* VehicleDamageSmokeLabel(VehicleDamageSmokeLevel level) {
    switch (level) {
        case VehicleDamageSmokeLevel::Light:
            return "轻";
        case VehicleDamageSmokeLevel::Heavy:
            return "重";
        case VehicleDamageSmokeLevel::Critical:
            return "报废";
        case VehicleDamageSmokeLevel::None:
        default:
            return "无";
    }
}

enum class VehicleDamageVisualLevel : std::uint8_t {
    None = 0,
    Scuffed = 1,
    Dented = 2,
    Wrecked = 3,
};

inline constexpr float kVehicleDamageVisualScuffedThreshold = 0.75f;
inline constexpr float kVehicleDamageVisualDentedThreshold = 0.50f;
inline constexpr float kVehicleDamageVisualWreckedThreshold = 0.25f;

inline VehicleDamageVisualLevel VehicleDamageVisualLevelFor(
    float condition) {
    if (!std::isfinite(condition) ||
        condition > kVehicleDamageVisualScuffedThreshold) {
        return VehicleDamageVisualLevel::None;
    }
    if (condition > kVehicleDamageVisualDentedThreshold) {
        return VehicleDamageVisualLevel::Scuffed;
    }
    if (condition > kVehicleDamageVisualWreckedThreshold) {
        return VehicleDamageVisualLevel::Dented;
    }
    return VehicleDamageVisualLevel::Wrecked;
}

inline int VehicleDamageVisualMarkCount(VehicleDamageVisualLevel level) {
    switch (level) {
        case VehicleDamageVisualLevel::Scuffed:
            return 3;
        case VehicleDamageVisualLevel::Dented:
            return 6;
        case VehicleDamageVisualLevel::Wrecked:
            return 10;
        case VehicleDamageVisualLevel::None:
        default:
            return 0;
    }
}

inline const char* VehicleDamageVisualLabel(VehicleDamageVisualLevel level) {
    switch (level) {
        case VehicleDamageVisualLevel::Scuffed:
            return "划痕";
        case VehicleDamageVisualLevel::Dented:
            return "凹陷";
        case VehicleDamageVisualLevel::Wrecked:
            return "破损";
        case VehicleDamageVisualLevel::None:
        default:
            return "无";
    }
}

inline float VehicleSmokeParticleScale(float baseSize, float t) {
    if (!std::isfinite(baseSize) || baseSize <= 0.0f ||
        !std::isfinite(t)) {
        return 0.0f;
    }
    t = std::clamp(t, 0.0f, 1.0f);
    return baseSize * (0.70f + 0.90f * t);
}

inline float VehicleSmokeParticleAlpha(VehicleDamageSmokeLevel level,
                                       float t) {
    float peak = 0.0f;
    switch (level) {
        case VehicleDamageSmokeLevel::Light:
            peak = 0.75f;
            break;
        case VehicleDamageSmokeLevel::Heavy:
            peak = 0.85f;
            break;
        case VehicleDamageSmokeLevel::Critical:
            peak = 0.95f;
            break;
        case VehicleDamageSmokeLevel::None:
        default:
            return 0.0f;
    }
    if (!std::isfinite(t)) {
        return 0.0f;
    }
    t = std::clamp(t, 0.0f, 1.0f);
    const float fadeOut = std::clamp((1.0f - t) / 0.35f, 0.0f, 1.0f);
    const float smoothOut = fadeOut * fadeOut * (3.0f - 2.0f * fadeOut);
    return peak * smoothOut;
}

inline bool VehicleHornReady(float condition, float cooldown) {
    return std::isfinite(condition) && std::isfinite(cooldown) &&
           condition > 0.0f && cooldown <= 0.0f;
}

struct VehicleSurfaceProfile {
    float maxSpeedScale = 1.0f;
    float accelerationScale = 1.0f;
    float brakingScale = 1.0f;
    float lateralAccelerationScale = 1.0f;
};

inline VehicleSurface VehicleSurfaceForFloor(std::string_view floorSprite) {
    if (floorSprite == "tile.floor.wood") {
        return VehicleSurface::Paved;
    }
    if (floorSprite == "tile.floor.dirt") {
        return VehicleSurface::Dirt;
    }
    return VehicleSurface::Grass;
}

inline std::string_view VehicleSurfaceLabel(VehicleSurface surface) {
    switch (surface) {
        case VehicleSurface::Paved:
            return "铺装路面";
        case VehicleSurface::Dirt:
            return "泥土路";
        case VehicleSurface::Grass:
        default:
            return "草地";
    }
}

enum class VehicleVisualDirection : std::uint8_t {
    East = 0,
    Southeast = 1,
    South = 2,
    Southwest = 3,
    West = 4,
    Northwest = 5,
    North = 6,
    Northeast = 7,
};

inline constexpr float kVehicleVisualSectorDegrees = 45.0f;
inline constexpr float kVehicleVisualHalfSectorDegrees = 22.5f;
inline constexpr float kVehicleVisualDefaultHysteresisDegrees = 8.0f;

inline VehicleVisualDirection VehicleVisualDirectionFor(
    float vx, float vy, VehicleVisualDirection current,
    float hysteresisDegrees = kVehicleVisualDefaultHysteresisDegrees) {
    if (!std::isfinite(vx) || !std::isfinite(vy)) {
        return current;
    }
    if (std::fabs(vx) + std::fabs(vy) < 1.0e-4f) {
        return current;
    }
    constexpr float kRadiansToDegrees = 57.29577951308232f;
    const float angle = std::atan2(vy, vx) * kRadiansToDegrees;
    int index = static_cast<int>(
        std::lround(angle / kVehicleVisualSectorDegrees));
    index %= 8;
    if (index < 0) {
        index += 8;
    }
    const int currentIndex = static_cast<int>(current);
    if (currentIndex >= 0 && currentIndex < 8) {
        const float currentAngle =
            static_cast<float>(currentIndex) * kVehicleVisualSectorDegrees;
        const float difference =
            std::fabs(std::remainder(angle - currentAngle, 360.0f));
        const float hysteresis =
            std::isfinite(hysteresisDegrees)
                ? std::clamp(hysteresisDegrees, 0.0f, 22.0f)
                : kVehicleVisualDefaultHysteresisDegrees;
        if (difference <= kVehicleVisualHalfSectorDegrees + hysteresis) {
            return current;
        }
    }
    return static_cast<VehicleVisualDirection>(index);
}

inline std::string_view VehicleVisualSprite(VehicleVisualDirection direction) {
    switch (direction) {
        case VehicleVisualDirection::East:
            return "object.vehicle_east";
        case VehicleVisualDirection::Southeast:
            return "object.vehicle_southeast";
        case VehicleVisualDirection::Southwest:
            return "object.vehicle_southwest";
        case VehicleVisualDirection::West:
            return "object.vehicle_west";
        case VehicleVisualDirection::Northwest:
            return "object.vehicle_northwest";
        case VehicleVisualDirection::North:
            return "object.vehicle_north";
        case VehicleVisualDirection::Northeast:
            return "object.vehicle_northeast";
        case VehicleVisualDirection::South:
        default:
            return "object.vehicle_south";
    }
}

inline void VehicleVisualDirectionVector(VehicleVisualDirection direction,
                                         float* x, float* y) {
    if (x == nullptr || y == nullptr) {
        return;
    }
    const int index =
        std::clamp(static_cast<int>(direction), 0, 7);
    const float angle = static_cast<float>(index) * kVehicleSampleAngle;
    *x = std::cos(angle);
    *y = std::sin(angle);
}

inline void VehicleRotateVector(float x, float y, float angle, float* outX,
                                float* outY) {
    if (outX == nullptr || outY == nullptr) {
        return;
    }
    if (!std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(angle)) {
        *outX = x;
        *outY = y;
        return;
    }
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    *outX = x * cosine - y * sine;
    *outY = x * sine + y * cosine;
}

inline void VehicleHeadingVector(const VehicleMotion& motion, float* x,
                                 float* y) {
    if (x == nullptr || y == nullptr) {
        return;
    }
    if (!std::isfinite(motion.heading)) {
        *x = 1.0f;
        *y = 0.0f;
        return;
    }
    *x = std::cos(motion.heading);
    *y = std::sin(motion.heading);
}

inline VehicleVisualDirection VehicleVisualDirectionForMotion(
    const VehicleMotion& motion, VehicleVisualDirection current) {
    if (!std::isfinite(motion.heading)) {
        return current;
    }
    float hx = 0.0f;
    float hy = 0.0f;
    VehicleHeadingVector(motion, &hx, &hy);
    return VehicleVisualDirectionFor(hx, hy, current);
}

inline std::string_view VehicleVisualLabel(VehicleVisualDirection direction) {
    switch (direction) {
        case VehicleVisualDirection::East:
            return "东";
        case VehicleVisualDirection::Southeast:
            return "东南";
        case VehicleVisualDirection::Southwest:
            return "西南";
        case VehicleVisualDirection::West:
            return "西";
        case VehicleVisualDirection::Northwest:
            return "西北";
        case VehicleVisualDirection::North:
            return "北";
        case VehicleVisualDirection::Northeast:
            return "东北";
        case VehicleVisualDirection::South:
        default:
            return "南";
    }
}

struct VehicleEngineTone {
    float volume = kVehicleEngineIdleVolume;
    float pitch = kVehicleEngineMinPitch;
};

inline VehicleEngineTone VehicleEngineToneForSpeed(
    float speed, float maxSpeed,
    float idleVolume = kVehicleEngineIdleVolume,
    float maxVolume = kVehicleEngineMaxVolume,
    float minPitch = kVehicleEngineMinPitch,
    float maxPitch = kVehicleEngineMaxPitch) {
    VehicleEngineTone tone;
    const float ratio =
        maxSpeed > 0.01f && std::isfinite(speed)
            ? std::clamp(speed / maxSpeed, 0.0f, 1.0f)
            : 0.0f;
    tone.volume = std::clamp(
        idleVolume + (maxVolume - idleVolume) * ratio, 0.0f, 1.0f);
    tone.pitch = std::max(0.25f, minPitch + (maxPitch - minPitch) * ratio);
    return tone;
}

enum class VehicleEngineAudioSource {
    None,
    Local,
    Remote,
};

// 本地驾驶优先；车况归零或无人驾驶时不播放引擎循环。
inline VehicleEngineAudioSource VehicleEngineAudioSourceFor(
    bool localDriving, bool remoteDriven, float condition) {
    if (!(condition > 0.0f)) {
        return VehicleEngineAudioSource::None;
    }
    if (localDriving) {
        return VehicleEngineAudioSource::Local;
    }
    return remoteDriven ? VehicleEngineAudioSource::Remote
                        : VehicleEngineAudioSource::None;
}

struct VehicleEngineAudioCandidate {
    std::uint64_t stableId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float speed = 0.0f;
    float condition = 1.0f;
    bool remoteDriven = false;
};

inline std::vector<std::uint64_t> SelectRemoteVehicleEngineAudio(
    const std::vector<VehicleEngineAudioCandidate>& candidates,
    float listenerX, float listenerY,
    float maxDistance = kVehicleEngineAudioMaxDistance,
    int maxVoices = kVehicleRemoteEngineMaxVoices) {
    std::vector<std::uint64_t> selected;
    if (!(maxDistance > 0.0f) || maxVoices <= 0 ||
        !std::isfinite(listenerX) || !std::isfinite(listenerY)) {
        return selected;
    }
    const float maxDistanceSq = maxDistance * maxDistance;
    std::vector<std::pair<float, std::uint64_t>> ranked;
    ranked.reserve(candidates.size());
    for (const VehicleEngineAudioCandidate& candidate : candidates) {
        if (candidate.stableId == 0 || !candidate.remoteDriven ||
            !(candidate.condition > 0.0f) || !std::isfinite(candidate.x) ||
            !std::isfinite(candidate.y)) {
            continue;
        }
        const float dx = candidate.x - listenerX;
        const float dy = candidate.y - listenerY;
        const float distanceSq = dx * dx + dy * dy;
        if (!std::isfinite(distanceSq) || distanceSq > maxDistanceSq) {
            continue;
        }
        ranked.emplace_back(distanceSq, candidate.stableId);
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& left, const auto& right) {
                  if (left.first != right.first) {
                      return left.first < right.first;
                  }
                  return left.second < right.second;
              });
    for (const auto& entry : ranked) {
        if (std::find(selected.begin(), selected.end(), entry.second) !=
            selected.end()) {
            continue;
        }
        selected.push_back(entry.second);
        if (static_cast<int>(selected.size()) >= maxVoices) {
            break;
        }
    }
    return selected;
}

struct VehicleTuning {
    float maxSpeed = kVehicleMaxSpeed;
    float acceleration = kVehicleAcceleration;
    float braking = kVehicleBraking;
    float radius = kVehicleRadius;
    float enterRange = kVehicleEnterRange;
    float fuelPerSecond = kVehicleFuelPerSecond;
    int spawnOffsetX = kVehicleSpawnOffsetX;
    int spawnOffsetY = kVehicleSpawnOffsetY;
    int spawnSearchRadius = kVehicleSpawnSearchRadius;
    float crashMinSpeed = kVehicleCrashMinSpeed;
    float crashDamagePerSpeed = kVehicleCrashDamagePerSpeed;
    float maxCrashDamage = kVehicleMaxCrashDamage;
    float ramMinSpeed = kVehicleRamMinSpeed;
    float ramBaseDamage = kVehicleRamBaseDamage;
    float ramDamagePerSpeed = kVehicleRamDamagePerSpeed;
    float ramKnockbackPerSpeed = kVehicleRamKnockbackPerSpeed;
    float ramKnockbackBase = kVehicleRamKnockbackBase;
    float ramStaggerBase = kVehicleRamStaggerBase;
    float ramStaggerPerSpeed = kVehicleRamStaggerPerSpeed;
    float ramCooldownSeconds = kVehicleRamCooldownSeconds;
    float ramConditionDamageBase = kVehicleRamConditionDamageBase;
    float ramConditionDamagePerSpeed = kVehicleRamConditionDamagePerSpeed;
    float ramConditionDamageMax = kVehicleRamConditionDamageMax;
    float vehicleCollisionMinSpeed = kVehicleCollisionMinSpeed;
    float vehicleCollisionConditionDamageBase =
        kVehicleCollisionConditionDamageBase;
    float vehicleCollisionConditionDamagePerSpeed =
        kVehicleCollisionConditionDamagePerSpeed;
    float vehicleCollisionConditionDamageMax =
        kVehicleCollisionConditionDamageMax;
    float vehicleCollisionOccupantDamageBase =
        kVehicleCollisionOccupantDamageBase;
    float vehicleCollisionOccupantDamagePerSpeed =
        kVehicleCollisionOccupantDamagePerSpeed;
    float vehicleCollisionOccupantDamageMax =
        kVehicleCollisionOccupantDamageMax;
    float vehicleCollisionCooldownSeconds =
        kVehicleCollisionCooldownSeconds;
    float vehicleCollisionPushPerSpeed = kVehicleCollisionPushPerSpeed;
    float vehicleCollisionPushMax = kVehicleCollisionPushMax;
    float vehicleCollisionRestitution = kVehicleCollisionRestitution;
    float vehicleCoastBraking = kVehicleCoastBraking;
    float vehicleCollisionFriction = kVehicleCollisionFriction;
    float vehicleAngularDamping = kVehicleAngularDamping;
    float turnRate = kVehicleTurnRate;
    float lateralGrip = kVehicleLateralGrip;
    float headingSnapSpeed = kVehicleHeadingSnapSpeed;
    float wheelbase = kVehicleWheelbase;
    float trackWidth = kVehicleTrackWidth;
    float maxSteerAngle = kVehicleMaxSteerAngle;
    float maxLateralAcceleration = kVehicleMaxLateralAcceleration;
    float suspensionFrequency = kVehicleSuspensionFrequency;
    float suspensionDampingRatio = kVehicleSuspensionDampingRatio;
    float suspensionRollGain = kVehicleSuspensionRollGain;
    float suspensionPitchGain = kVehicleSuspensionPitchGain;
    float driverDamageReduction = kVehicleDriverDamageReduction;
    float seatHandoffMaxSpeed = kVehicleSeatHandoffMaxSpeed;
    float zombieAttackConditionDamage =
        kVehicleZombieAttackConditionDamage;
    float zombieAttackCooldownSeconds =
        kVehicleZombieAttackCooldownSeconds;
    float noiseRadius = kVehicleNoiseRadius;
    float noiseIntervalSeconds = kVehicleNoiseIntervalSeconds;
    float noiseMinSpeed = kVehicleNoiseMinSpeed;
    float hornNoiseRadius = kVehicleHornNoiseRadius;
    float hornCooldownSeconds = kVehicleHornCooldownSeconds;
    float dirtMaxSpeedScale = kVehicleDirtMaxSpeedScale;
    float dirtAccelerationScale = kVehicleDirtAccelerationScale;
    float dirtBrakingScale = kVehicleDirtBrakingScale;
    float dirtLateralAccelerationScale = kVehicleDirtLateralAccelerationScale;
    float grassMaxSpeedScale = kVehicleGrassMaxSpeedScale;
    float grassAccelerationScale = kVehicleGrassAccelerationScale;
    float grassBrakingScale = kVehicleGrassBrakingScale;
    float grassLateralAccelerationScale =
        kVehicleGrassLateralAccelerationScale;
    float engineIdleVolume = kVehicleEngineIdleVolume;
    float engineMaxVolume = kVehicleEngineMaxVolume;
    float engineMinPitch = kVehicleEngineMinPitch;
    float engineMaxPitch = kVehicleEngineMaxPitch;
    float ramSoundVolume = kVehicleRamSoundVolume;
    float repairAmount = kVehicleRepairAmount;
    float fuelPerCan = kVehicleFuelPerCan;
    float trunkCapacityKg = 40.0f;
    int trunkStacks = 6;
};

inline constexpr VehicleTuning kVehicleDefaultTuning{};

inline VehicleTuning VehicleTuningForType(ecs::VehicleType type,
                                          VehicleTuning tuning) {
    const VehicleProfile& profile = VehicleProfileFor(type);
    tuning.maxSpeed *= profile.maxSpeedScale;
    tuning.acceleration *= profile.accelerationScale;
    tuning.braking *= profile.brakingScale;
    tuning.radius = profile.radius;
    tuning.turnRate = profile.turnRate;
    tuning.lateralGrip = profile.lateralGrip;
    tuning.wheelbase = profile.wheelbase;
    tuning.trackWidth = profile.trackWidth;
    tuning.maxSteerAngle = profile.maxSteerAngle;
    tuning.maxLateralAcceleration = profile.maxLateralAcceleration;
    tuning.suspensionFrequency = profile.suspensionFrequency;
    tuning.suspensionDampingRatio = profile.suspensionDampingRatio;
    tuning.suspensionRollGain = profile.suspensionRollGain;
    tuning.suspensionPitchGain = profile.suspensionPitchGain;
    return tuning;
}

inline VehicleSurfaceProfile VehicleSurfaceProfileFor(
    VehicleSurface surface, const VehicleTuning& tuning) {
    VehicleSurfaceProfile profile;
    if (surface == VehicleSurface::Dirt) {
        profile.maxSpeedScale = std::clamp(tuning.dirtMaxSpeedScale, 0.0f, 1.0f);
        profile.accelerationScale =
            std::clamp(tuning.dirtAccelerationScale, 0.0f, 1.0f);
        profile.brakingScale = std::clamp(tuning.dirtBrakingScale, 0.0f, 1.0f);
        profile.lateralAccelerationScale =
            std::clamp(tuning.dirtLateralAccelerationScale, 0.0f, 1.0f);
    } else if (surface == VehicleSurface::Grass) {
        profile.maxSpeedScale = std::clamp(tuning.grassMaxSpeedScale, 0.0f, 1.0f);
        profile.accelerationScale =
            std::clamp(tuning.grassAccelerationScale, 0.0f, 1.0f);
        profile.brakingScale = std::clamp(tuning.grassBrakingScale, 0.0f, 1.0f);
        profile.lateralAccelerationScale =
            std::clamp(tuning.grassLateralAccelerationScale, 0.0f, 1.0f);
    }
    return profile;
}

struct VehicleStepResult {
    bool collided = false;
    bool damaged = false;
    bool bodyCollision = false;
    bool bodyContact = false;
    float impactSpeed = 0.0f;
    float damage = 0.0f;
};

using VehicleBlockedGrid = std::function<bool(int, int, int)>;
using VehicleWalkableGrid = std::function<bool(int, int)>;
using VehicleBodyBlocked =
    std::function<bool(float, float, int, float)>;

inline float VehicleSpeed(const VehicleMotion& vehicle) {
    return std::sqrt(vehicle.vx * vehicle.vx + vehicle.vy * vehicle.vy);
}

inline float VehicleCollisionImpactSpeed(const VehicleMotion& first,
                                         const VehicleMotion& second) {
    if (!std::isfinite(first.vx) || !std::isfinite(first.vy) ||
        !std::isfinite(second.vx) || !std::isfinite(second.vy)) {
        return 0.0f;
    }
    const float dx = first.vx - second.vx;
    const float dy = first.vy - second.vy;
    return std::sqrt(dx * dx + dy * dy);
}

// 车况 75% 以上动力完整；0..75% 线性降到 55%，归零后切断动力。
inline float VehicleConditionPerformanceScale(float condition) {
    if (!std::isfinite(condition) || condition <= 0.0f) {
        return 0.0f;
    }
    const float ratio =
        std::clamp(condition / kVehicleConditionFullPerformance, 0.0f, 1.0f);
    return std::clamp(kVehicleConditionMinPerformance +
                          (1.0f - kVehicleConditionMinPerformance) * ratio,
                      0.0f, 1.0f);
}

inline float VehicleRamDamage(float speed, const VehicleTuning& tuning) {
    if (!std::isfinite(speed) || speed < tuning.ramMinSpeed) {
        return 0.0f;
    }
    return std::max(0.0f, tuning.ramBaseDamage +
                              (speed - tuning.ramMinSpeed) *
                                  tuning.ramDamagePerSpeed);
}

inline float VehicleRamKnockback(float speed, const VehicleTuning& tuning) {
    return std::max(0.0f, tuning.ramKnockbackBase +
                              std::max(0.0f, speed) *
                                  tuning.ramKnockbackPerSpeed);
}

inline float VehicleRamStagger(float speed, const VehicleTuning& tuning) {
    return std::max(0.0f, tuning.ramStaggerBase +
                              (speed - tuning.ramMinSpeed) *
                                  tuning.ramStaggerPerSpeed);
}

enum class VehicleRamTargetKind : std::uint8_t {
    None = 0,
    Zombie = 1,
    Animal = 2,
    Player = 3,
};

// M25-T4-V20：单机与服务器共用的“攻击驾驶位”来源分类。危险动物优先于
// 僵尸；玩家、友方、被动动物和未知来源不磨损车况。
enum class VehicleAttackerKind : std::uint8_t {
    None = 0,
    Zombie = 1,
    Animal = 2,
};

inline VehicleAttackerKind VehicleAttackerKindFor(
    bool hasAiState, bool hasAnimal, bool animalDangerous, bool isPlayer,
    bool isFriendly) {
    if (isPlayer || isFriendly) {
        return VehicleAttackerKind::None;
    }
    if (hasAnimal) {
        return animalDangerous ? VehicleAttackerKind::Animal
                               : VehicleAttackerKind::None;
    }
    if (hasAiState) {
        return VehicleAttackerKind::Zombie;
    }
    return VehicleAttackerKind::None;
}

// M25-T4-V19/V21：单机与服务器共用的撞击目标筛选。动物优先于僵尸，
// 玩家仅在 PvP 允许时参与；友方/死亡/异层目标不参与。
inline VehicleRamTargetKind VehicleRamTargetKindFor(
    bool hasAiState, bool hasAnimal, bool isPlayer, bool isFriendly,
    float health, int targetZ, int vehicleZ, bool allowPlayer = false) {
    if (!std::isfinite(health) || health <= 0.0f || targetZ != vehicleZ ||
        isFriendly) {
        return VehicleRamTargetKind::None;
    }
    if (isPlayer) {
        return allowPlayer ? VehicleRamTargetKind::Player
                           : VehicleRamTargetKind::None;
    }
    if (hasAnimal) {
        return VehicleRamTargetKind::Animal;
    }
    if (hasAiState) {
        return VehicleRamTargetKind::Zombie;
    }
    return VehicleRamTargetKind::None;
}

// 每次成功撞击对车况的磨损；调用方用 ramCooldownSeconds 限制实际结算频率。
inline float VehicleRamConditionDamage(float speed,
                                       const VehicleTuning& tuning) {
    if (!std::isfinite(speed) || speed < tuning.ramMinSpeed) {
        return 0.0f;
    }
    return std::clamp(
        tuning.ramConditionDamageBase +
            (speed - tuning.ramMinSpeed) * tuning.ramConditionDamagePerSpeed,
        0.0f, std::max(0.0f, tuning.ramConditionDamageMax));
}

inline float VehicleApplyRamConditionDamage(VehicleMotion* vehicle,
                                            float speed,
                                            const VehicleTuning& tuning) {
    if (vehicle == nullptr || !std::isfinite(vehicle->condition) ||
        vehicle->condition <= 0.0f) {
        return 0.0f;
    }
    const float damage = VehicleRamConditionDamage(speed, tuning);
    if (damage <= 0.0f) {
        return 0.0f;
    }
    const float before = std::clamp(vehicle->condition, 0.0f, 1.0f);
    vehicle->condition = std::max(0.0f, before - damage);
    return before - vehicle->condition;
}

inline float VehicleCollisionConditionDamage(float impactSpeed,
                                             const VehicleTuning& tuning) {
    if (!std::isfinite(impactSpeed) ||
        impactSpeed < tuning.vehicleCollisionMinSpeed) {
        return 0.0f;
    }
    return std::clamp(
        tuning.vehicleCollisionConditionDamageBase +
            (impactSpeed - tuning.vehicleCollisionMinSpeed) *
                tuning.vehicleCollisionConditionDamagePerSpeed,
        0.0f, std::max(0.0f, tuning.vehicleCollisionConditionDamageMax));
}

inline float VehicleApplyCollisionConditionDamage(
    VehicleMotion* vehicle, float impactSpeed, const VehicleTuning& tuning) {
    if (vehicle == nullptr || !std::isfinite(vehicle->condition) ||
        vehicle->condition <= 0.0f) {
        return 0.0f;
    }
    const float damage = VehicleCollisionConditionDamage(impactSpeed, tuning);
    if (damage <= 0.0f) {
        return 0.0f;
    }
    const float before = std::clamp(vehicle->condition, 0.0f, 1.0f);
    vehicle->condition = std::max(0.0f, before - damage);
    return before - vehicle->condition;
}

inline float VehicleCollisionConditionDamageForType(
    float impactSpeed, const VehicleTuning& tuning, ecs::VehicleType type) {
    const float damage = VehicleCollisionConditionDamage(impactSpeed, tuning);
    const float scale = VehicleProfileFor(type).conditionDamageScale;
    if (!std::isfinite(damage) || !std::isfinite(scale) || scale <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(damage * scale, 0.0f,
                      std::max(0.0f, tuning.vehicleCollisionConditionDamageMax));
}

inline float VehicleApplyCollisionConditionDamageForType(
    VehicleMotion* vehicle, float impactSpeed, const VehicleTuning& tuning) {
    if (vehicle == nullptr || !std::isfinite(vehicle->condition) ||
        vehicle->condition <= 0.0f) {
        return 0.0f;
    }
    const float damage = VehicleCollisionConditionDamageForType(
        impactSpeed, tuning, vehicle->type);
    if (damage <= 0.0f) {
        return 0.0f;
    }
    const float before = std::clamp(vehicle->condition, 0.0f, 1.0f);
    vehicle->condition = std::max(0.0f, before - damage);
    return before - vehicle->condition;
}

inline float VehicleCollisionOccupantDamage(float impactSpeed,
                                             const VehicleTuning& tuning) {
    if (!std::isfinite(impactSpeed) ||
        !std::isfinite(tuning.vehicleCollisionMinSpeed) ||
        !std::isfinite(tuning.vehicleCollisionOccupantDamageBase) ||
        !std::isfinite(tuning.vehicleCollisionOccupantDamagePerSpeed) ||
        !std::isfinite(tuning.vehicleCollisionOccupantDamageMax) ||
        impactSpeed < tuning.vehicleCollisionMinSpeed ||
        tuning.vehicleCollisionOccupantDamageBase < 0.0f ||
        tuning.vehicleCollisionOccupantDamagePerSpeed < 0.0f ||
        tuning.vehicleCollisionOccupantDamageMax <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(
        tuning.vehicleCollisionOccupantDamageBase +
            (impactSpeed - tuning.vehicleCollisionMinSpeed) *
                tuning.vehicleCollisionOccupantDamagePerSpeed,
        0.0f, tuning.vehicleCollisionOccupantDamageMax);
}

inline float VehicleCollisionOccupantDamageForType(
    float impactSpeed, const VehicleTuning& tuning, ecs::VehicleType type) {
    const float damage = VehicleCollisionOccupantDamage(impactSpeed, tuning);
    const float scale = VehicleProfileFor(type).occupantDamageScale;
    if (!std::isfinite(damage) || !std::isfinite(scale) || scale <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(damage * scale, 0.0f,
                      std::max(0.0f, tuning.vehicleCollisionOccupantDamageMax));
}

inline float VehicleCollisionPushDistance(float impactSpeed,
                                          const VehicleTuning& tuning) {
    if (!std::isfinite(impactSpeed) ||
        !std::isfinite(tuning.vehicleCollisionPushPerSpeed) ||
        !std::isfinite(tuning.vehicleCollisionPushMax) ||
        impactSpeed < tuning.vehicleCollisionMinSpeed ||
        tuning.vehicleCollisionPushPerSpeed <= 0.0f ||
        tuning.vehicleCollisionPushMax <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(
        (impactSpeed - tuning.vehicleCollisionMinSpeed) *
            tuning.vehicleCollisionPushPerSpeed,
        0.0f, tuning.vehicleCollisionPushMax);
}

inline float VehicleCollisionPushDistanceForMasses(
    float impactSpeed, const VehicleTuning& tuning, float sourceMass,
    float targetMass) {
    const float base = VehicleCollisionPushDistance(impactSpeed, tuning);
    if (base <= 0.0f || !std::isfinite(sourceMass) ||
        !std::isfinite(targetMass) || sourceMass <= 0.0f ||
        targetMass <= 0.0f) {
        return 0.0f;
    }
    const float massScale =
        std::clamp(sourceMass / targetMass, 0.5f, 2.0f);
    return std::min(base * massScale,
                    std::max(0.0f, tuning.vehicleCollisionPushMax));
}

struct VehicleCollisionImpulseResult {
    float firstVx = 0.0f;
    float firstVy = 0.0f;
    float secondVx = 0.0f;
    float secondVy = 0.0f;
    float firstAngularVelocity = 0.0f;
    float secondAngularVelocity = 0.0f;
    float normalX = 1.0f;
    float normalY = 0.0f;
    float closingSpeed = 0.0f;
    bool applied = false;
};

inline float VehicleAngularInertia(float mass, float radius) {
    if (!std::isfinite(mass) || !std::isfinite(radius) || mass <= 0.0f ||
        radius <= 0.0f) {
        return 0.0f;
    }
    return 0.5f * mass * radius * radius;
}

// M26-V9：等质量/可传质量的碰撞冲量；沿两车中心法线传递速度，保留切向分量。
// 位置重合时用第一辆车速度方向或 +X 兜底；低于阈值不改变速度。
// M26-V14：minClosingSpeed 允许低速接触使用更低门槛；负值沿用 tuning 配置。
inline VehicleCollisionImpulseResult VehicleCollisionImpulseFor(
    const VehicleMotion& first, const VehicleMotion& second,
    const VehicleTuning& tuning, float firstMass = 1.0f,
    float secondMass = 1.0f, float firstRadius = kVehicleRadius,
    float secondRadius = kVehicleRadius, float minClosingSpeed = -1.0f) {
    VehicleCollisionImpulseResult result;
    result.firstVx = first.vx;
    result.firstVy = first.vy;
    result.secondVx = second.vx;
    result.secondVy = second.vy;
    if (!std::isfinite(first.vx) || !std::isfinite(first.vy) ||
        !std::isfinite(second.vx) || !std::isfinite(second.vy) ||
        !std::isfinite(first.x) || !std::isfinite(first.y) ||
        !std::isfinite(second.x) || !std::isfinite(second.y) ||
        !std::isfinite(firstMass) || !std::isfinite(secondMass) ||
        firstMass <= 0.0f || secondMass <= 0.0f) {
        return result;
    }
    const float safeFirstRadius =
        std::isfinite(firstRadius) && firstRadius > 0.0f ? firstRadius
                                                         : tuning.radius;
    const float safeSecondRadius =
        std::isfinite(secondRadius) && secondRadius > 0.0f ? secondRadius
                                                           : tuning.radius;
    float normalX = second.x - first.x;
    float normalY = second.y - first.y;
    float length = std::sqrt(normalX * normalX + normalY * normalY);
    if (length <= 1.0e-4f) {
        const float firstSpeed = VehicleSpeed(first);
        normalX = firstSpeed > 1.0e-4f ? first.vx / firstSpeed : 1.0f;
        normalY = firstSpeed > 1.0e-4f ? first.vy / firstSpeed : 0.0f;
    } else {
        normalX /= length;
        normalY /= length;
    }
    const float relativeNormal =
        (second.vx - first.vx) * normalX +
        (second.vy - first.vy) * normalY;
    const float closingSpeed = -relativeNormal;
    const float requiredClosingSpeed =
        std::isfinite(minClosingSpeed) && minClosingSpeed >= 0.0f
            ? minClosingSpeed
            : tuning.vehicleCollisionMinSpeed;
    if (!std::isfinite(closingSpeed) ||
        !std::isfinite(requiredClosingSpeed) ||
        closingSpeed < requiredClosingSpeed) {
        return result;
    }
    const float restitution =
        std::isfinite(tuning.vehicleCollisionRestitution)
            ? std::clamp(tuning.vehicleCollisionRestitution, 0.0f, 1.0f)
            : 0.0f;
    const float inverseMassSum = 1.0f / firstMass + 1.0f / secondMass;
    const float impulse =
        (1.0f + restitution) * closingSpeed / inverseMassSum;
    if (!std::isfinite(impulse) || impulse <= 0.0f) {
        return result;
    }
    result.firstVx = first.vx - impulse / firstMass * normalX;
    result.firstVy = first.vy - impulse / firstMass * normalY;
    result.secondVx = second.vx + impulse / secondMass * normalX;
    result.secondVy = second.vy + impulse / secondMass * normalY;

    const float tangentX = -normalY;
    const float tangentY = normalX;
    const float relativeTangent =
        (second.vx - first.vx) * tangentX +
        (second.vy - first.vy) * tangentY;
    const float friction =
        std::isfinite(tuning.vehicleCollisionFriction)
            ? std::clamp(tuning.vehicleCollisionFriction, 0.0f, 1.0f)
            : 0.0f;
    if (friction > 0.0f && std::isfinite(relativeTangent)) {
        const float maxFrictionImpulse = friction * impulse;
        const float tangentialImpulse = std::clamp(
            relativeTangent / inverseMassSum, -maxFrictionImpulse,
            maxFrictionImpulse);
        if (std::isfinite(tangentialImpulse) &&
            std::fabs(tangentialImpulse) > 1.0e-5f) {
            result.firstVx +=
                tangentialImpulse / firstMass * tangentX;
            result.firstVy +=
                tangentialImpulse / firstMass * tangentY;
            result.secondVx -=
                tangentialImpulse / secondMass * tangentX;
            result.secondVy -=
                tangentialImpulse / secondMass * tangentY;
            const float firstInertia =
                VehicleAngularInertia(firstMass, safeFirstRadius);
            const float secondInertia =
                VehicleAngularInertia(secondMass, safeSecondRadius);
            if (firstInertia > 0.0f) {
                result.firstAngularVelocity = std::clamp(
                    safeFirstRadius * tangentialImpulse / firstInertia,
                    -kVehicleMaxAngularVelocity, kVehicleMaxAngularVelocity);
            }
            if (secondInertia > 0.0f) {
                result.secondAngularVelocity = std::clamp(
                    safeSecondRadius * tangentialImpulse / secondInertia,
                    -kVehicleMaxAngularVelocity, kVehicleMaxAngularVelocity);
            }
        }
    }
    result.normalX = normalX;
    result.normalY = normalY;
    result.closingSpeed = closingSpeed;
    result.applied = true;
    return result;
}

// M26-V14：低于车损门槛的持续接触使用完全非弹性、无摩擦的法线冲量。
// 只负责把接近速度按质量分配，不产生车损、乘员伤害、音效或冷却。
inline VehicleCollisionImpulseResult VehicleContactImpulseFor(
    const VehicleMotion& first, const VehicleMotion& second,
    const VehicleTuning& tuning, float firstMass = 1.0f,
    float secondMass = 1.0f, float firstRadius = kVehicleRadius,
    float secondRadius = kVehicleRadius) {
    VehicleTuning contactTuning = tuning;
    contactTuning.vehicleCollisionRestitution = 0.0f;
    contactTuning.vehicleCollisionFriction = 0.0f;
    const float collisionMin =
        std::isfinite(tuning.vehicleCollisionMinSpeed)
            ? std::max(0.0f, tuning.vehicleCollisionMinSpeed)
            : kVehicleCollisionMinSpeed;
    const float contactMin = std::min(kVehicleContactMinSpeed, collisionMin);
    return VehicleCollisionImpulseFor(first, second, contactTuning, firstMass,
                                      secondMass, firstRadius, secondRadius,
                                      contactMin);
}

inline float VehicleNormalizeAngle(float angle) {
    if (!std::isfinite(angle)) {
        return 0.0f;
    }
    return std::remainder(angle, 6.283185307179586f);
}

inline float VehicleAngleDelta(float current, float target) {
    return VehicleNormalizeAngle(target - current);
}

inline float VehicleApproachAngle(float current, float target,
                                  float maxDelta) {
    if (!std::isfinite(current) || !std::isfinite(target) ||
        !std::isfinite(maxDelta) || maxDelta <= 0.0f) {
        return current;
    }
    const float delta = VehicleAngleDelta(current, target);
    if (std::fabs(delta) <= maxDelta) {
        return VehicleNormalizeAngle(target);
    }
    return VehicleNormalizeAngle(current + (delta > 0.0f ? maxDelta
                                                         : -maxDelta));
}

inline void VehicleIntegrateAngular(VehicleMotion* vehicle, float dt,
                                    const VehicleTuning& tuning) {
    if (vehicle == nullptr || !std::isfinite(dt) || dt <= 0.0f) {
        return;
    }
    if (!std::isfinite(vehicle->angularVelocity) ||
        !std::isfinite(vehicle->heading)) {
        vehicle->angularVelocity = 0.0f;
        vehicle->heading = 0.0f;
        return;
    }
    const float damping =
        std::isfinite(tuning.vehicleAngularDamping)
            ? std::clamp(tuning.vehicleAngularDamping, 0.0f, 20.0f)
            : kVehicleAngularDamping;
    const float decay = std::exp(-damping * dt);
    vehicle->heading = VehicleNormalizeAngle(
        vehicle->heading + vehicle->angularVelocity * dt);
    vehicle->angularVelocity *= decay;
    if (std::fabs(vehicle->angularVelocity) < 0.01f) {
        vehicle->angularVelocity = 0.0f;
    }
}

// M26-V13：自行车模型的前轮转角 -> 目标横摆角速度；轮胎横向加速度上限
// 会限制高速转向（转向不足），非法输入返回 0。
inline float VehicleTireYawRate(float speed, float steerAngle,
                                float wheelbase, float maxYawRate,
                                float maxLateralAcceleration) {
    if (!std::isfinite(speed) || !std::isfinite(steerAngle) ||
        !std::isfinite(wheelbase) || speed <= 0.0f || wheelbase <= 0.0f) {
        return 0.0f;
    }
    const float safeWheelbase = std::clamp(wheelbase, 0.20f, 5.0f);
    const float safeSteer = std::clamp(steerAngle, -1.20f, 1.20f);
    float yawRate =
        speed * std::tan(safeSteer) / safeWheelbase;
    if (std::isfinite(maxYawRate) && maxYawRate > 0.0f) {
        yawRate = std::clamp(yawRate, -maxYawRate, maxYawRate);
    }
    if (std::isfinite(maxLateralAcceleration)) {
        if (maxLateralAcceleration <= 0.0f) {
            return 0.0f;
        }
        const float gripYawRate =
            maxLateralAcceleration / std::max(speed, 0.5f);
        yawRate = std::clamp(yawRate, -gripYawRate, gripYawRate);
    }
    return std::isfinite(yawRate) ? yawRate : 0.0f;
}

inline float VehicleTireLateralAcceleration(float speed, float yawRate) {
    if (!std::isfinite(speed) || !std::isfinite(yawRate) ||
        speed <= 0.0f) {
        return 0.0f;
    }
    return speed * yawRate;
}

// M26-V13：车身悬挂姿态用弹簧-阻尼趋近目标 roll/pitch；只改变运行时
// 姿态状态，不参与碰撞/存档/复制。非法输入时安全归零。
inline void VehicleSuspensionStep(VehicleMotion* vehicle, float dt,
                                  float lateralAcceleration,
                                  float longitudinalAcceleration,
                                  const VehicleTuning& tuning) {
    if (vehicle == nullptr) {
        return;
    }
    if (!std::isfinite(dt) || dt <= 0.0f ||
        !std::isfinite(lateralAcceleration) ||
        !std::isfinite(longitudinalAcceleration)) {
        vehicle->bodyRoll = 0.0f;
        vehicle->bodyRollVelocity = 0.0f;
        vehicle->bodyPitch = 0.0f;
        vehicle->bodyPitchVelocity = 0.0f;
        return;
    }
    const float step = std::clamp(dt, 0.0f, 0.10f);
    const float maxLateral =
        std::isfinite(tuning.maxLateralAcceleration)
            ? std::clamp(tuning.maxLateralAcceleration, 0.1f, 50.0f)
            : kVehicleMaxLateralAcceleration;
    const float maxLongitudinal =
        std::isfinite(tuning.acceleration)
            ? std::clamp(tuning.acceleration, 0.1f, 50.0f)
            : kVehicleAcceleration;
    const float frequency =
        std::isfinite(tuning.suspensionFrequency)
            ? std::clamp(tuning.suspensionFrequency, 0.1f, 5.0f)
            : kVehicleSuspensionFrequency;
    const float dampingRatio =
        std::isfinite(tuning.suspensionDampingRatio)
            ? std::clamp(tuning.suspensionDampingRatio, 0.05f, 2.0f)
            : kVehicleSuspensionDampingRatio;
    const float rollGain =
        std::isfinite(tuning.suspensionRollGain)
            ? std::clamp(tuning.suspensionRollGain, 0.0f, 2.0f)
            : kVehicleSuspensionRollGain;
    const float pitchGain =
        std::isfinite(tuning.suspensionPitchGain)
            ? std::clamp(tuning.suspensionPitchGain, 0.0f, 2.0f)
            : kVehicleSuspensionPitchGain;

    const float targetRoll =
        std::clamp(-lateralAcceleration / maxLateral, -1.0f, 1.0f) *
        kVehicleBodyMaxRoll * rollGain;
    const float targetPitch =
        std::clamp(-longitudinalAcceleration / maxLongitudinal, -1.0f, 1.0f) *
        kVehicleBodyMaxPitch * pitchGain;
    const float omega = 6.283185307179586f * frequency;
    const float damping = 2.0f * dampingRatio * omega;
    const float rollAcceleration =
        (targetRoll - vehicle->bodyRoll) * omega * omega -
        vehicle->bodyRollVelocity * damping;
    const float pitchAcceleration =
        (targetPitch - vehicle->bodyPitch) * omega * omega -
        vehicle->bodyPitchVelocity * damping;

    vehicle->bodyRollVelocity = std::clamp(
        vehicle->bodyRollVelocity + rollAcceleration * step, -8.0f, 8.0f);
    vehicle->bodyPitchVelocity = std::clamp(
        vehicle->bodyPitchVelocity + pitchAcceleration * step, -8.0f, 8.0f);
    vehicle->bodyRoll = std::clamp(
        vehicle->bodyRoll + vehicle->bodyRollVelocity * step,
        -kVehicleBodyMaxRoll * 2.0f, kVehicleBodyMaxRoll * 2.0f);
    vehicle->bodyPitch = std::clamp(
        vehicle->bodyPitch + vehicle->bodyPitchVelocity * step,
        -kVehicleBodyMaxPitch * 2.0f, kVehicleBodyMaxPitch * 2.0f);
    if (std::fabs(vehicle->bodyRollVelocity) < 1.0e-4f) {
        vehicle->bodyRollVelocity = 0.0f;
    }
    if (std::fabs(vehicle->bodyPitchVelocity) < 1.0e-4f) {
        vehicle->bodyPitchVelocity = 0.0f;
    }
}

struct VehiclePushResult {
    float x = 0.0f;
    float y = 0.0f;
    float distance = 0.0f;
};

inline VehiclePushResult VehiclePushBody(
    float startX, float startY, int z, float directionX, float directionY,
    float maxDistance, float radius, const VehicleBlockedGrid& blocked,
    const VehicleBodyBlocked& bodyBlocked) {
    VehiclePushResult result{startX, startY, 0.0f};
    if (!std::isfinite(startX) || !std::isfinite(startY) ||
        !std::isfinite(directionX) || !std::isfinite(directionY) ||
        !std::isfinite(maxDistance) || !std::isfinite(radius) ||
        maxDistance <= 0.0f) {
        return result;
    }
    const float directionLength =
        std::sqrt(directionX * directionX + directionY * directionY);
    if (!std::isfinite(directionLength) || directionLength <= 1.0e-4f) {
        return result;
    }
    directionX /= directionLength;
    directionY /= directionLength;
    constexpr float kPushStep = 0.05f;
    while (result.distance + 1.0e-5f < maxDistance) {
        const float step = std::min(kPushStep, maxDistance - result.distance);
        const float nextX = result.x + directionX * step;
        const float nextY = result.y + directionY * step;
        if (blocked != nullptr &&
            blocked(static_cast<int>(std::floor(nextX)),
                    static_cast<int>(std::floor(nextY)), z)) {
            break;
        }
        if (bodyBlocked != nullptr &&
            bodyBlocked(nextX, nextY, z, radius)) {
            break;
        }
        result.x = nextX;
        result.y = nextY;
        result.distance += step;
    }
    return result;
}

// M25-T4-V15：僵尸攻击驾驶位造成的车况磨损；调用方用
// zombieAttackCooldownSeconds 限制多只僵尸同 tick 叠加。
inline float VehicleZombieAttackConditionDamage(
    const VehicleTuning& tuning) {
    if (!std::isfinite(tuning.zombieAttackConditionDamage)) {
        return 0.0f;
    }
    return std::clamp(tuning.zombieAttackConditionDamage, 0.0f, 1.0f);
}

inline float VehicleApplyZombieAttackConditionDamage(
    VehicleMotion* vehicle, const VehicleTuning& tuning) {
    if (vehicle == nullptr || !std::isfinite(vehicle->condition) ||
        vehicle->condition <= 0.0f) {
        return 0.0f;
    }
    const float damage = VehicleZombieAttackConditionDamage(tuning);
    if (damage <= 0.0f) {
        return 0.0f;
    }
    const float before = std::clamp(vehicle->condition, 0.0f, 1.0f);
    vehicle->condition = std::max(0.0f, before - damage);
    return before - vehicle->condition;
}

inline float VehicleHitFeedbackStrength(float wear) {
    if (!std::isfinite(wear) || wear <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(wear / kVehicleHitFeedbackReferenceDamage, 0.25f, 1.0f);
}

inline int VehicleHitSparkCount(float wear) {
    const float strength = VehicleHitFeedbackStrength(wear);
    if (strength <= 0.0f) {
        return 0;
    }
    return std::clamp(3 + static_cast<int>(std::ceil(strength * 5.0f)),
                      3, 8);
}

inline float VehicleHitSparkLifetime(float wear) {
    const float strength = VehicleHitFeedbackStrength(wear);
    if (strength <= 0.0f) {
        return 0.0f;
    }
    return kVehicleHitSparkMinLifetime +
           (kVehicleHitSparkMaxLifetime - kVehicleHitSparkMinLifetime) *
               strength;
}

struct VehicleScreenShake {
    float x = 0.0f;
    float y = 0.0f;
};

inline float VehicleHitShakeStrength(float wear) {
    return VehicleHitFeedbackStrength(wear);
}

inline float VehicleCrashShakeStrength(float impactSpeed) {
    if (!std::isfinite(impactSpeed) || impactSpeed <= kVehicleCrashMinSpeed) {
        return 0.0f;
    }
    const float span = std::max(kVehicleMaxSpeed - kVehicleCrashMinSpeed, 0.001f);
    const float t = std::clamp(
        (impactSpeed - kVehicleCrashMinSpeed) / span, 0.0f, 1.0f);
    return 0.25f + 0.75f * t;
}

inline float VehicleShakeDuration(float strength) {
    if (!std::isfinite(strength) || strength <= 0.0f) {
        return 0.0f;
    }
    return kVehicleScreenShakeMinDuration +
           (kVehicleScreenShakeMaxDuration - kVehicleScreenShakeMinDuration) *
               std::clamp(strength, 0.0f, 1.0f);
}

inline VehicleScreenShake VehicleScreenShakeOffset(float strength, float age,
                                                   float duration) {
    if (!std::isfinite(strength) || !std::isfinite(age) ||
        !std::isfinite(duration) || strength <= 0.0f || duration <= 0.0f ||
        age < 0.0f || age >= duration) {
        return {};
    }
    const float t = std::clamp(age / duration, 0.0f, 1.0f);
    const float envelope = (1.0f - t) * (1.0f - t);
    const float amplitude =
        kVehicleScreenShakeMaxOffset * std::clamp(strength, 0.0f, 1.0f) *
        envelope;
    return {amplitude * std::sin(age * 70.0f),
            amplitude * 0.65f * std::cos(age * 53.0f)};
}

inline int VehicleMetalDebrisCount(float strength) {
    if (!std::isfinite(strength) || strength <= 0.0f) {
        return 0;
    }
    return 2 + static_cast<int>(
                   std::ceil(std::clamp(strength, 0.0f, 1.0f) * 4.0f));
}

inline float VehicleMetalDebrisLifetime(float strength) {
    if (!std::isfinite(strength) || strength <= 0.0f) {
        return 0.0f;
    }
    return kVehicleMetalDebrisMinLifetime +
           (kVehicleMetalDebrisMaxLifetime - kVehicleMetalDebrisMinLifetime) *
               std::clamp(strength, 0.0f, 1.0f);
}

inline bool VehicleBodiesOverlap(float firstX, float firstY,
                                 float firstRadius, float secondX,
                                 float secondY, float secondRadius) {
    if (!std::isfinite(firstX) || !std::isfinite(firstY) ||
        !std::isfinite(firstRadius) || !std::isfinite(secondX) ||
        !std::isfinite(secondY) || !std::isfinite(secondRadius)) {
        return false;
    }
    const float dx = firstX - secondX;
    const float dy = firstY - secondY;
    const float reach = std::max(0.0f, firstRadius) +
                        std::max(0.0f, secondRadius);
    return dx * dx + dy * dy <= reach * reach;
}

inline bool VehicleRamOverlap(const VehicleMotion& vehicle, float targetX,
                              float targetY, float targetRadius,
                              float vehicleRadius = kVehicleRadius) {
    return VehicleBodiesOverlap(vehicle.x, vehicle.y, vehicleRadius, targetX,
                                targetY, targetRadius);
}

inline bool VehicleCanEnter(const VehicleMotion& vehicle, float playerX, float playerY,
                            float range = kVehicleEnterRange) {
    if (range < 0.0f) {
        return false;
    }
    const float dx = vehicle.x - playerX;
    const float dy = vehicle.y - playerY;
    return dx * dx + dy * dy <= range * range;
}

inline bool VehicleSeatAvailable(std::uint64_t driverStableId) {
    return driverStableId == 0;
}

inline constexpr std::size_t kVehicleMaxPassengers = 2;

inline bool VehiclePassengerSeatAvailable(std::uint64_t passengerStableId,
                                          std::uint64_t secondPassengerStableId = 0) {
    return passengerStableId == 0 || secondPassengerStableId == 0;
}

enum class VehicleSeat : std::uint8_t {
    None = 0,
    Driver = 1,
    Passenger = 2,
};

inline VehicleSeat VehicleSeatForEnter(std::uint64_t driverStableId,
                                       std::uint64_t passengerStableId,
                                       std::uint64_t secondPassengerStableId = 0) {
    if (driverStableId == 0) {
        return VehicleSeat::Driver;
    }
    if (passengerStableId == 0 || secondPassengerStableId == 0) {
        return VehicleSeat::Passenger;
    }
    return VehicleSeat::None;
}

inline bool VehicleCanTakeDriverSeat(std::uint64_t driverStableId,
                                     std::uint64_t passengerStableId,
                                     std::uint64_t secondPassengerStableId,
                                     std::uint64_t requesterStableId) {
    return requesterStableId != 0 && driverStableId == 0 &&
           (passengerStableId == requesterStableId ||
            secondPassengerStableId == requesterStableId);
}

inline bool VehicleCanTakeDriverSeat(std::uint64_t driverStableId,
                                     std::uint64_t passengerStableId,
                                     std::uint64_t requesterStableId) {
    return VehicleCanTakeDriverSeat(driverStableId, passengerStableId, 0,
                                    requesterStableId);
}

inline bool VehicleCanGiveDriverSeatWithSecondPassenger(
    std::uint64_t driverStableId, std::uint64_t passengerStableId,
    std::uint64_t secondPassengerStableId, std::uint64_t requesterStableId,
    float speed, float maxSpeed = kVehicleSeatHandoffMaxSpeed) {
    const bool hasPassenger =
        (passengerStableId != 0 || secondPassengerStableId != 0) &&
        passengerStableId != driverStableId &&
        secondPassengerStableId != driverStableId;
    return requesterStableId != 0 && requesterStableId == driverStableId &&
           hasPassenger && std::isfinite(speed) && std::isfinite(maxSpeed) &&
           maxSpeed >= 0.0f && speed >= 0.0f && speed <= maxSpeed;
}

inline bool VehicleCanGiveDriverSeat(
    std::uint64_t driverStableId, std::uint64_t passengerStableId,
    std::uint64_t requesterStableId, float speed,
    float maxSpeed = kVehicleSeatHandoffMaxSpeed) {
    return VehicleCanGiveDriverSeatWithSecondPassenger(
        driverStableId, passengerStableId, 0, requesterStableId, speed,
        maxSpeed);
}

inline bool FindVehicleExitCell(float vehicleX, float vehicleY,
                                const VehicleWalkableGrid& walkable,
                                float* outX, float* outY, int maxRadius = 3) {
    if (!walkable || outX == nullptr || outY == nullptr || maxRadius < 1 ||
        !std::isfinite(vehicleX) || !std::isfinite(vehicleY)) {
        return false;
    }
    const int baseX = static_cast<int>(std::floor(vehicleX));
    const int baseY = static_cast<int>(std::floor(vehicleY));
    for (int radius = 1; radius <= maxRadius; ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != radius) {
                    continue;
                }
                const int cellX = baseX + dx;
                const int cellY = baseY + dy;
                if (!walkable(cellX, cellY)) {
                    continue;
                }
                *outX = static_cast<float>(cellX) + 0.5f;
                *outY = static_cast<float>(cellY) + 0.5f;
                return true;
            }
        }
    }
    return false;
}

inline bool VehicleAddCondition(VehicleMotion* vehicle, float amount) {
    if (vehicle == nullptr || amount <= 0.0f || vehicle->condition >= 1.0f) {
        return false;
    }
    vehicle->condition = std::clamp(vehicle->condition + amount, 0.0f, 1.0f);
    return true;
}

inline bool VehicleAddFuel(VehicleMotion* vehicle, float amount) {
    if (vehicle == nullptr || amount <= 0.0f || vehicle->fuel >= 1.0f) {
        return false;
    }
    vehicle->fuel = std::clamp(vehicle->fuel + amount, 0.0f, 1.0f);
    return true;
}

enum class VehicleServiceAction {
    None,
    Repair,
    Refuel,
    MissingRepairKit,
    MissingFuelCan,
};

inline VehicleServiceAction PlanVehicleService(const VehicleMotion& vehicle,
                                               int repairItems, int fuelItems) {
    if (vehicle.condition < 1.0f) {
        return repairItems > 0 ? VehicleServiceAction::Repair
                               : VehicleServiceAction::MissingRepairKit;
    }
    if (vehicle.fuel < 1.0f) {
        return fuelItems > 0 ? VehicleServiceAction::Refuel
                             : VehicleServiceAction::MissingFuelCan;
    }
    return VehicleServiceAction::None;
}

inline bool VehiclePointBlocked(float x, float y, int z, const VehicleBlockedGrid& blocked) {
    if (!blocked) {
        return false;
    }
    return blocked(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)), z);
}

inline bool VehicleBlocked(float x, float y, int z, const VehicleBlockedGrid& blocked,
                           float radius = kVehicleRadius,
                           const VehicleBodyBlocked& bodyBlocked = {},
                           bool* blockedByBody = nullptr) {
    if (blockedByBody != nullptr) {
        *blockedByBody = false;
    }
    if (VehiclePointBlocked(x, y, z, blocked)) {
        return true;
    }
    if (bodyBlocked && bodyBlocked(x, y, z, radius)) {
        if (blockedByBody != nullptr) {
            *blockedByBody = true;
        }
        return true;
    }
    for (int i = 0; i < 8; ++i) {
        const float angle = static_cast<float>(i) * kVehicleSampleAngle;
        if (VehiclePointBlocked(x + std::cos(angle) * radius,
                                y + std::sin(angle) * radius, z, blocked)) {
            return true;
        }
    }
    return false;
}

inline VehicleStepResult VehicleStep(
    VehicleMotion* vehicle, float inputX, float inputY, float dt,
    const VehicleBlockedGrid& blocked,
    const VehicleTuning& tuning = kVehicleDefaultTuning,
    VehicleSurface surface = VehicleSurface::Paved,
    const VehicleBodyBlocked& bodyBlocked = {}) {
    VehicleStepResult result;
    if (vehicle == nullptr || dt <= 0.0f) {
        return result;
    }
    if (!std::isfinite(vehicle->heading)) {
        vehicle->heading = 0.0f;
    }

    const VehicleSurfaceProfile surfaceProfile =
        VehicleSurfaceProfileFor(surface, tuning);
    const float conditionScale =
        VehicleConditionPerformanceScale(vehicle->condition);
    const float maxSpeed = std::max(
        0.01f, tuning.maxSpeed * surfaceProfile.maxSpeedScale * conditionScale);
    const float acceleration = std::max(
        0.01f,
        tuning.acceleration * surfaceProfile.accelerationScale * conditionScale);
    const float braking =
        std::max(0.01f, tuning.braking * surfaceProfile.brakingScale);

    float directionX = inputX;
    float directionY = inputY;
    const float inputLength =
        std::sqrt(directionX * directionX + directionY * directionY);
    if (inputLength > 1.0e-4f) {
        directionX /= inputLength;
        directionY /= inputLength;
    }
    const bool hasInput = inputLength > 1.0e-4f;
    const bool hasFuel = vehicle->fuel > 0.0f;
    const bool hasCondition = vehicle->condition > 0.0f;
    const bool powered = hasInput && hasFuel && hasCondition;
    const float startSpeed = VehicleSpeed(*vehicle);
    float yawRate = 0.0f;

    // M26-V13：前轮转角由车身朝向误差产生，再经自行车模型和轮胎横向
    // 加速度上限求横摆角速度；低速仍直接对齐，避免起步时无法转向。
    if (hasInput && hasCondition) {
        const float desiredHeading = std::atan2(directionY, directionX);
        const float turnRate =
            std::isfinite(tuning.turnRate)
                ? std::clamp(tuning.turnRate, 0.1f, 20.0f)
                : kVehicleTurnRate;
        const float snapSpeed =
            std::isfinite(tuning.headingSnapSpeed)
                ? std::clamp(tuning.headingSnapSpeed, 0.0f, 2.0f)
                : kVehicleHeadingSnapSpeed;
        const float maxSteer =
            std::isfinite(tuning.maxSteerAngle)
                ? std::clamp(tuning.maxSteerAngle, 0.10f, 1.20f)
                : kVehicleMaxSteerAngle;
        const float steerAngle = std::clamp(
            VehicleAngleDelta(vehicle->heading, desiredHeading), -maxSteer,
            maxSteer);
        vehicle->steerAngle = steerAngle;
        if (startSpeed <= snapSpeed) {
            vehicle->heading = VehicleNormalizeAngle(desiredHeading);
        } else {
            const float speedRatio =
                std::clamp(startSpeed / maxSpeed, 0.0f, 1.0f);
            const float speedTurnScale =
                std::clamp(1.0f - speedRatio * 0.45f, 0.55f, 1.0f);
            const float maxLateralAcceleration =
                tuning.maxLateralAcceleration *
                surfaceProfile.lateralAccelerationScale;
            yawRate = VehicleTireYawRate(
                startSpeed, steerAngle, tuning.wheelbase,
                turnRate * speedTurnScale, maxLateralAcceleration);
            vehicle->heading = VehicleNormalizeAngle(
                vehicle->heading + yawRate * dt);
        }
    } else {
        const float steerReturn = 6.0f * dt;
        if (!std::isfinite(vehicle->steerAngle)) {
            vehicle->steerAngle = 0.0f;
        } else if (std::fabs(vehicle->steerAngle) <= steerReturn) {
            vehicle->steerAngle = 0.0f;
        } else {
            vehicle->steerAngle -=
                (vehicle->steerAngle > 0.0f ? steerReturn : -steerReturn);
        }
    }

    float headingX = 0.0f;
    float headingY = 0.0f;
    VehicleHeadingVector(*vehicle, &headingX, &headingY);
    const float targetVx = powered ? headingX * maxSpeed : 0.0f;
    const float targetVy = powered ? headingY * maxSpeed : 0.0f;

    const float deltaVx = targetVx - vehicle->vx;
    const float deltaVy = targetVy - vehicle->vy;
    const float deltaLength = std::sqrt(deltaVx * deltaVx + deltaVy * deltaVy);
    const float rate = powered ? acceleration : braking;
    const float maxDelta = rate * dt;

    if (deltaLength <= maxDelta) {
        vehicle->vx = targetVx;
        vehicle->vy = targetVy;
    } else if (deltaLength > 0.0f) {
        vehicle->vx += deltaVx / deltaLength * maxDelta;
        vehicle->vy += deltaVy / deltaLength * maxDelta;
    }

    float speed = VehicleSpeed(*vehicle);
    if (speed > maxSpeed) {
        const float scale = maxSpeed / speed;
        vehicle->vx *= scale;
        vehicle->vy *= scale;
        speed = maxSpeed;
    }

    if (powered) {
        vehicle->heading = VehicleNormalizeAngle(vehicle->heading);
        VehicleHeadingVector(*vehicle, &headingX, &headingY);
        const float tangentX = -headingY;
        const float tangentY = headingX;
        const float forwardSpeed =
            vehicle->vx * headingX + vehicle->vy * headingY;
        const float lateralSpeed =
            vehicle->vx * tangentX + vehicle->vy * tangentY;
        const float grip =
            std::isfinite(tuning.lateralGrip)
                ? std::clamp(tuning.lateralGrip, 0.0f, 40.0f)
                : kVehicleLateralGrip;
        const float lateralScale = std::max(0.0f, 1.0f - grip * dt);
        vehicle->vx = headingX * forwardSpeed +
                      tangentX * lateralSpeed * lateralScale;
        vehicle->vy = headingY * forwardSpeed +
                      tangentY * lateralSpeed * lateralScale;
        speed = VehicleSpeed(*vehicle);
    }

    const float startX = vehicle->x;
    const float startY = vehicle->y;
    const float nextX = startX + vehicle->vx * dt;
    const float nextY = startY + vehicle->vy * dt;

    bool anyBodyBlocked = false;
    const auto blockedAt = [&](float x, float y, int z) {
        bool blockedByBody = false;
        const bool hit = VehicleBlocked(x, y, z, blocked, tuning.radius,
                                        bodyBlocked, &blockedByBody);
        anyBodyBlocked = anyBodyBlocked || blockedByBody;
        return hit;
    };

    bool fullyBlocked = false;
    if (!blockedAt(nextX, nextY, vehicle->z)) {
        vehicle->x = nextX;
        vehicle->y = nextY;
    } else {
        bool movedX = false;
        bool movedY = false;
        if (std::abs(nextX - startX) > 1.0e-6f &&
            !blockedAt(nextX, startY, vehicle->z)) {
            vehicle->x = nextX;
            movedX = true;
        }
        if (std::abs(nextY - startY) > 1.0e-6f &&
            !blockedAt(vehicle->x, nextY, vehicle->z)) {
            vehicle->y = nextY;
            movedY = true;
        }
        if (!movedX && !movedY) {
            fullyBlocked = true;
            result.collided = true;
            result.bodyCollision = anyBodyBlocked;
            result.impactSpeed = speed;
            if (!result.bodyCollision && !vehicle->crashLatched &&
                speed >= tuning.crashMinSpeed) {
                result.damage = std::min(
                    (speed - tuning.crashMinSpeed) * tuning.crashDamagePerSpeed,
                    tuning.maxCrashDamage);
                if (result.damage > 0.0f) {
                    vehicle->condition =
                        std::clamp(vehicle->condition - result.damage, 0.0f, 1.0f);
                    result.damaged = true;
                }
            }
            vehicle->crashLatched = true;
            vehicle->vx = 0.0f;
            vehicle->vy = 0.0f;
        } else {
            if (!movedX) {
                vehicle->vx = 0.0f;
            }
            if (!movedY) {
                vehicle->vy = 0.0f;
            }
        }
    }
    if (!fullyBlocked) {
        vehicle->crashLatched = false;
    }

    VehicleIntegrateAngular(vehicle, dt, tuning);
    result.bodyContact = anyBodyBlocked;
    const float movedSpeed = VehicleSpeed(*vehicle);
    vehicle->lateralAcceleration =
        VehicleTireLateralAcceleration(movedSpeed, yawRate);
    vehicle->longitudinalAcceleration = std::clamp(
        (movedSpeed - startSpeed) / std::max(dt, 1.0e-4f), -50.0f, 50.0f);
    VehicleSuspensionStep(vehicle, dt, vehicle->lateralAcceleration,
                          vehicle->longitudinalAcceleration, tuning);
    if (vehicle->fuel > 0.0f && movedSpeed > 0.01f) {
        const float load = std::clamp(movedSpeed / maxSpeed, 0.0f, 1.0f);
        vehicle->fuel -= tuning.fuelPerSecond * load * dt;
    }
    vehicle->fuel = std::clamp(vehicle->fuel, 0.0f, 1.0f);
    return result;
}

inline VehicleStepResult VehiclePredictStep(
    VehicleMotion* vehicle, float inputX, float inputY, float dt,
    const VehicleBlockedGrid& blocked,
    const VehicleTuning& tuning = kVehicleDefaultTuning,
    VehicleSurface surface = VehicleSurface::Paved,
    const VehicleBodyBlocked& bodyBlocked = {}) {
    VehicleStepResult result;
    if (vehicle == nullptr) {
        return result;
    }
    const float fuel = vehicle->fuel;
    const float condition = vehicle->condition;
    const bool crashLatched = vehicle->crashLatched;
    result = VehicleStep(vehicle, inputX, inputY, dt, blocked, tuning, surface,
                         bodyBlocked);
    vehicle->fuel = fuel;
    vehicle->condition = condition;
    vehicle->crashLatched = crashLatched;
    result.damaged = false;
    result.damage = 0.0f;
    return result;
}

inline bool VehicleCorrectPosition(VehicleMotion* predicted, float serverX,
                                   float serverY, float serverVx, float serverVy,
                                   float snapDistance = 1.0f,
                                   float blend = 0.25f) {
    if (predicted == nullptr || !std::isfinite(serverX) ||
        !std::isfinite(serverY) || !std::isfinite(serverVx) ||
        !std::isfinite(serverVy) || !std::isfinite(snapDistance) ||
        !std::isfinite(blend)) {
        return false;
    }
    const float dx = serverX - predicted->x;
    const float dy = serverY - predicted->y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    const bool snap = distance > std::max(0.0f, snapDistance);
    if (snap) {
        predicted->x = serverX;
        predicted->y = serverY;
    } else {
        const float weight = std::clamp(blend, 0.0f, 1.0f);
        predicted->x += dx * weight;
        predicted->y += dy * weight;
    }
    predicted->vx = serverVx;
    predicted->vy = serverVy;
    return snap;
}

// M26-V12：服务器碰撞会改变车身朝向；本地预测只做平滑校正，不硬切。
inline bool VehicleCorrectHeading(VehicleMotion* predicted,
                                  float serverHeading,
                                  float blend = kVehicleHeadingCorrectionBlend) {
    if (predicted == nullptr || !std::isfinite(serverHeading) ||
        !std::isfinite(predicted->heading) || !std::isfinite(blend)) {
        return false;
    }
    const float delta = VehicleAngleDelta(predicted->heading, serverHeading);
    if (std::fabs(delta) < 1.0e-4f) {
        return false;
    }
    const float weight = std::clamp(blend, 0.0f, 1.0f);
    predicted->heading =
        VehicleNormalizeAngle(predicted->heading + delta * weight);
    return true;
}

inline bool VehicleAreaWalkable(int centerX, int centerY,
                                const VehicleWalkableGrid& walkable) {
    if (!walkable) {
        return false;
    }
    for (int y = centerY - kVehicleSpawnHalfExtent;
         y <= centerY + kVehicleSpawnHalfExtent; ++y) {
        for (int x = centerX - kVehicleSpawnHalfExtent;
             x <= centerX + kVehicleSpawnHalfExtent; ++x) {
            if (!walkable(x, y)) {
                return false;
            }
        }
    }
    return true;
}

inline bool FindVehicleSpawnCell(int centerX, int centerY, int maxRadius,
                                 const VehicleWalkableGrid& walkable, int* outX,
                                 int* outY) {
    if (!walkable || maxRadius < 0) {
        return false;
    }
    for (int radius = 0; radius <= maxRadius; ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != radius) {
                    continue;
                }
                const int x = centerX + dx;
                const int y = centerY + dy;
                if (!VehicleAreaWalkable(x, y, walkable)) {
                    continue;
                }
                if (outX != nullptr) {
                    *outX = x;
                }
                if (outY != nullptr) {
                    *outY = y;
                }
                return true;
            }
        }
    }
    return false;
}

}  // namespace zl::game
