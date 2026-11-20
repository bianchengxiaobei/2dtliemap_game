#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace zl::items {
class ItemRegistry;
class LootTableRegistry;
class RecipeRegistry;
}  // namespace zl::items

namespace zl::game {

struct BuildableDef;
class AnimalRegistry;
class CropRegistry;
class ForageRegistry;
struct MissionConfig;
struct PoiConfig;
struct SettlementConfig;
struct WorldEventConfig;
struct CompanionConfig;
struct TrapDefsConfig;
struct VehicleModsConfig;

inline constexpr std::uint32_t kContentHashVersion = 23;

std::uint64_t ComputeContentHash(
    const items::ItemRegistry& items, const items::RecipeRegistry& recipes,
    const std::vector<BuildableDef>& buildables,
    const items::LootTableRegistry& lootTables, const PoiConfig& pois,
    const MissionConfig& missions, std::uint64_t extraHash = 0,
    const ForageRegistry* forage = nullptr,
    const AnimalRegistry* animals = nullptr,
    const CropRegistry* crops = nullptr,
    const SettlementConfig* settlements = nullptr,
    const WorldEventConfig* worldEvents = nullptr,
    const CompanionConfig* companions = nullptr,
    const VehicleModsConfig* vehicleMods = nullptr,
    const TrapDefsConfig* traps = nullptr);

std::uint64_t ComputeTreeHash(std::string_view root);

}  // namespace zl::game
