#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace zl::core {
class Config;
}

namespace zl::items {
class ItemRegistry;
}

namespace zl::ecs {
struct Container;
struct VehicleMods;
}

namespace zl::game {

inline constexpr std::size_t kMaxVehicleModDefs = 64;
inline constexpr std::size_t kMaxVehicleModIdBytes = 64;
inline constexpr std::size_t kMaxVehicleModNameBytes = 96;
inline constexpr std::size_t kMaxVehicleModTextBytes = 384;
inline constexpr std::size_t kMaxVehicleModMaterials = 4;
inline constexpr std::size_t kMaxVehicleModExcludes = 4;

enum class VehicleModSlot : std::uint8_t {
    Armor = 0,
    Storage = 1,
    Plow = 2,
    Lights = 3,
    Engine = 4,
};

struct VehicleModEffects {
    float crashConditionScale = 1.0f;
    float zombieConditionScale = 1.0f;
    float ramConditionScale = 1.0f;
    float ramDamageScale = 1.0f;
    float trunkCapacityKg = 0.0f;
    int trunkStacks = 0;
    float headlightRangeScale = 1.0f;
    float headlightBrightnessScale = 1.0f;
    float maxSpeedScale = 1.0f;
    float accelerationScale = 1.0f;
    float fuelUseScale = 1.0f;
    float conditionWearScale = 1.0f;
};

struct VehicleModMaterial {
    std::string item;
    int count = 1;
};

struct VehicleModDef {
    std::string id;
    std::string name;
    std::string description;
    VehicleModSlot slot = VehicleModSlot::Armor;
    VehicleModEffects effects;
    std::vector<VehicleModMaterial> materials;
    float installSeconds = 10.0f;
    float weightKg = 0.0f;
    std::vector<std::string> excludes;
};

struct VehicleModMergeStats {
    int filesLoaded = 0;
    int defsAdded = 0;
    int defsSkipped = 0;
};

struct VehicleModsConfig {
    bool enabled = false;
    std::vector<VehicleModDef> defs;
    float removeRefundRatio = 0.5f;
    float removeSecondsScale = 0.5f;
    float weightAccelerationKg = 400.0f;
    float weightFuelKg = 600.0f;

    const VehicleModDef* Find(std::string_view id) const;
};

struct VehicleModStats {
    VehicleModEffects effects;
    float totalWeightKg = 0.0f;
    float accelerationWeightScale = 1.0f;
    float fuelWeightScale = 1.0f;
};

const char* VehicleModSlotName(VehicleModSlot slot);
bool ParseVehicleModSlot(std::string_view value, VehicleModSlot* out);
bool IsValidVehicleModToken(std::string_view value);
std::size_t VehicleModSlotIndex(VehicleModSlot slot);
const std::string& InstalledVehicleModId(const ecs::VehicleMods& mods,
                                         VehicleModSlot slot);
const VehicleModDef* InstalledVehicleMod(const VehicleModsConfig& config,
                                         const ecs::VehicleMods& mods,
                                         VehicleModSlot slot);
std::vector<const VehicleModDef*> VehicleModCandidatesForSlot(
    const VehicleModsConfig& config, VehicleModSlot slot);
bool CanInstallVehicleMod(const VehicleModsConfig& config,
                          const ecs::VehicleMods& mods, std::string_view id,
                          std::string* error = nullptr);
bool InstallVehicleMod(const VehicleModsConfig& config,
                       ecs::VehicleMods* mods, std::string_view id,
                       std::string* error = nullptr);
std::string RemoveVehicleMod(ecs::VehicleMods* mods, VehicleModSlot slot);
bool VehicleModsHasAny(const ecs::VehicleMods& mods);
float VehicleModRemoveSeconds(const VehicleModsConfig& config,
                              const VehicleModDef& def);
std::vector<VehicleModMaterial> VehicleModRefundMaterials(
    const VehicleModDef& def, float ratio);
bool VehicleModMaterialCountsMet(const ecs::Container& container,
                                 const items::ItemRegistry& items,
                                 const VehicleModDef& def,
                                 std::string* summary = nullptr);
bool ConsumeVehicleModMaterials(ecs::Container& container,
                                const items::ItemRegistry& items,
                                const VehicleModDef& def,
                                std::string* error = nullptr);
std::vector<VehicleModMaterial> AddVehicleModRefund(
    ecs::Container& container, const items::ItemRegistry& items,
    const VehicleModDef& def, float ratio);

VehicleModStats VehicleModStatsFor(const VehicleModsConfig& config,
                                   const ecs::VehicleMods* mods);
VehicleModStats VehicleModStatsFor(const VehicleModsConfig& config,
                                   const ecs::VehicleMods& mods);
float VehicleModMaxSpeedScale(const VehicleModStats& stats);
float VehicleModAccelerationScale(const VehicleModStats& stats);
float VehicleModFuelUseScale(const VehicleModStats& stats);
float VehicleModCrashWearScale(const VehicleModStats& stats);
float VehicleModZombieWearScale(const VehicleModStats& stats);
float VehicleModRamWearScale(const VehicleModStats& stats);
float VehicleModRamDamageScale(const VehicleModStats& stats);
float VehicleModHeadlightRangeScale(const VehicleModStats& stats);
float VehicleModHeadlightBrightnessScale(const VehicleModStats& stats);
float VehicleModTrunkCapacityKg(const VehicleModStats& stats,
                                float baseCapacityKg);
int VehicleModTrunkStacks(const VehicleModStats& stats, int baseStacks);
bool ApplyVehicleModTrunkCapacity(const VehicleModStats& stats,
                                  float baseCapacityKg,
                                  ecs::Container* container);
std::string VehicleModEffectSummary(const VehicleModDef& def);
std::string VehicleModStatsSummary(const VehicleModStats& stats,
                                   float baseCapacityKg);
bool VehicleModTargetInRange(float playerX, float playerY, float playerZ,
                             float targetX, float targetY, float targetZ,
                             float range, bool targetAlive,
                             bool targetIsVehicle);

bool LoadVehicleModConfig(const core::Config& config, VehicleModsConfig* out,
                          std::string* error,
                          const items::ItemRegistry* items = nullptr);
bool MergeVehicleModJson(std::string_view json, const std::string& source,
                         VehicleModsConfig* out,
                         VehicleModMergeStats* stats, std::string* error,
                         const items::ItemRegistry* items = nullptr);
bool MergeVehicleModFile(const std::filesystem::path& path,
                         VehicleModsConfig* out,
                         VehicleModMergeStats* stats, std::string* error,
                         const items::ItemRegistry* items = nullptr);

}  // namespace zl::game
