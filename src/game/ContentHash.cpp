#include "game/ContentHash.h"

#include "game/Animals.h"
#include "game/Crops.h"
#include "game/Forage.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "game/BuildingRules.h"
#include "game/Missions.h"
#include "game/Poi.h"
#include "game/Settlement.h"
#include "items/ItemDef.h"
#include "items/ItemRegistry.h"
#include "items/LootTable.h"
#include "items/RecipeDef.h"
#include "items/RecipeRegistry.h"

namespace zl::game {
namespace {

class ContentHasher {
public:
    void Bytes(const void* data, std::size_t size) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            m_hash ^= bytes[i];
            m_hash *= 1099511628211ull;
        }
    }

    void U8(std::uint8_t value) { Bytes(&value, sizeof(value)); }

    void U32(std::uint32_t value) {
        const std::uint8_t bytes[4] = {
            static_cast<std::uint8_t>(value & 0xffu),
            static_cast<std::uint8_t>((value >> 8u) & 0xffu),
            static_cast<std::uint8_t>((value >> 16u) & 0xffu),
            static_cast<std::uint8_t>((value >> 24u) & 0xffu),
        };
        Bytes(bytes, sizeof(bytes));
    }

    void I32(std::int32_t value) {
        U32(static_cast<std::uint32_t>(value));
    }

    void U64(std::uint64_t value) {
        const std::uint8_t bytes[8] = {
            static_cast<std::uint8_t>(value & 0xffu),
            static_cast<std::uint8_t>((value >> 8u) & 0xffu),
            static_cast<std::uint8_t>((value >> 16u) & 0xffu),
            static_cast<std::uint8_t>((value >> 24u) & 0xffu),
            static_cast<std::uint8_t>((value >> 32u) & 0xffu),
            static_cast<std::uint8_t>((value >> 40u) & 0xffu),
            static_cast<std::uint8_t>((value >> 48u) & 0xffu),
            static_cast<std::uint8_t>((value >> 56u) & 0xffu),
        };
        Bytes(bytes, sizeof(bytes));
    }

    void F32(float value) {
        if (value == 0.0f) {
            value = 0.0f;
        }
        U32(std::bit_cast<std::uint32_t>(value));
    }

    void Bool(bool value) { U8(value ? 1u : 0u); }

    void String(std::string_view value) {
        U32(static_cast<std::uint32_t>(value.size()));
        if (!value.empty()) {
            Bytes(value.data(), value.size());
        }
    }

    std::uint64_t Value() const { return m_hash; }

private:
    std::uint64_t m_hash = 14695981039346656037ull;
};

void HashOptionalFloat(ContentHasher& hasher,
                       const std::optional<float>& value) {
    hasher.Bool(value.has_value());
    if (value.has_value()) {
        hasher.F32(*value);
    }
}

void HashUseEffect(ContentHasher& hasher, const items::UseEffect& use) {
    hasher.F32(use.heal);
    hasher.F32(use.hunger);
    hasher.F32(use.thirst);
    hasher.F32(use.fatigue);
    hasher.F32(use.bandage);
    hasher.F32(use.painRelief);
    hasher.F32(use.painReliefSeconds);
    hasher.F32(use.healthPenalty);
    hasher.F32(use.infectionCure);
    hasher.F32(use.infectionSlow);
    hasher.F32(use.infectionSlowSeconds);
    hasher.F32(use.infectionRisk);
    hasher.F32(use.extinguish);
    hasher.F32(use.extinguishWear);
    hasher.String(use.replaceWith);
    hasher.F32(use.nutrition);
    hasher.F32(use.foodPoisonChance);
}

void HashWeaponCombatFields(ContentHasher& hasher,
                            const items::WeaponCombatFields& combat) {
    HashOptionalFloat(hasher, combat.damage);
    HashOptionalFloat(hasher, combat.range);
    HashOptionalFloat(hasher, combat.arcDegrees);
    HashOptionalFloat(hasher, combat.cooldownSeconds);
    HashOptionalFloat(hasher, combat.knockback);
    HashOptionalFloat(hasher, combat.staggerSeconds);
    HashOptionalFloat(hasher, combat.wearPerHit);
    HashOptionalFloat(hasher, combat.staminaCost);
    HashOptionalFloat(hasher, combat.windupSeconds);
    HashOptionalFloat(hasher, combat.poiseDamage);
}

void HashWeaponCombat(ContentHasher& hasher,
                      const items::WeaponCombat& combat) {
    HashWeaponCombatFields(hasher, combat);
    HashOptionalFloat(hasher, combat.blockReduction);
    HashOptionalFloat(hasher, combat.blockStaminaCost);
}

void HashRangedCombat(ContentHasher& hasher,
                      const items::RangedCombat& ranged) {
    hasher.F32(ranged.damage);
    hasher.F32(ranged.range);
    hasher.F32(ranged.spreadDegrees);
    hasher.I32(ranged.pellets);
    hasher.F32(ranged.cooldownSeconds);
    hasher.I32(ranged.magazine);
    hasher.F32(ranged.reloadSeconds);
    hasher.String(ranged.ammoDefId);
    hasher.F32(ranged.noiseRadius);
    hasher.F32(ranged.wearPerShot);
    hasher.F32(ranged.projectileSpeed);
    hasher.F32(ranged.falloffStart);
    hasher.F32(ranged.falloffEnd);
    hasher.F32(ranged.falloffScale);
}

void HashArmorStats(ContentHasher& hasher, const items::ArmorStats& armor) {
    hasher.F32(armor.clawReduction);
    hasher.F32(armor.biteReduction);
    hasher.F32(armor.bluntReduction);
    hasher.F32(armor.wearPerHit);
    hasher.F32(armor.moveScale);
    hasher.F32(armor.insulation);
    hasher.F32(armor.waterproof);
}

void HashItemDef(ContentHasher& hasher, const items::ItemDef& item) {
    hasher.String(item.id);
    hasher.String(item.name);
    hasher.String(item.icon);
    hasher.F32(item.weight);
    hasher.I32(item.maxStack);
    hasher.U8(static_cast<std::uint8_t>(item.type));
    HashUseEffect(hasher, item.use);
    HashWeaponCombat(hasher, item.combat);
    hasher.Bool(item.heavy.has_value());
    if (item.heavy.has_value()) {
        HashWeaponCombatFields(hasher, *item.heavy);
    }
    hasher.Bool(item.ranged.has_value());
    if (item.ranged.has_value()) {
        HashRangedCombat(hasher, *item.ranged);
    }
    HashArmorStats(hasher, item.armor);
    hasher.F32(item.repairEfficiency);
    hasher.F32(item.spoilDays);
    hasher.U32(static_cast<std::uint32_t>(item.unlocksRecipes.size()));
    for (const std::string& id : item.unlocksRecipes) {
        hasher.String(id);
    }
}

void HashIngredient(ContentHasher& hasher,
                    const items::RecipeIngredient& ingredient) {
    hasher.String(ingredient.defId);
    hasher.I32(ingredient.count);
    hasher.F32(ingredient.wear);
}

void HashRecipeDef(ContentHasher& hasher, const items::RecipeDef& recipe) {
    hasher.String(recipe.id);
    hasher.String(recipe.name);
    hasher.String(recipe.outputDefId);
    hasher.I32(recipe.outputCount);
    hasher.F32(recipe.craftSeconds);
    hasher.String(recipe.station);
    hasher.U32(static_cast<std::uint32_t>(recipe.ingredients.size()));
    for (const items::RecipeIngredient& ingredient : recipe.ingredients) {
        HashIngredient(hasher, ingredient);
    }
    hasher.U32(static_cast<std::uint32_t>(recipe.tools.size()));
    for (const items::RecipeIngredient& tool : recipe.tools) {
        HashIngredient(hasher, tool);
    }
    hasher.U8(static_cast<std::uint8_t>(recipe.category));
    hasher.Bool(recipe.cookware.has_value());
    if (recipe.cookware.has_value()) {
        HashIngredient(hasher, *recipe.cookware);
    }
    hasher.Bool(recipe.fuel.has_value());
    if (recipe.fuel.has_value()) {
        HashIngredient(hasher, *recipe.fuel);
    }
}

void HashBuildableDef(ContentHasher& hasher, const BuildableDef& buildable) {
    hasher.String(buildable.id);
    hasher.String(buildable.name);
    hasher.String(buildable.sprite);
    hasher.String(buildable.spriteVertical);
    hasher.I32(buildable.woodCost);
    hasher.F32(buildable.maxHp);
    hasher.Bool(buildable.blocksMovement);
    hasher.F32(buildable.storageKg);
    hasher.I32(buildable.storageSlots);
    hasher.String(buildable.openSprite);
    hasher.String(buildable.openSpriteVertical);
    hasher.Bool(buildable.door);
    hasher.U8(static_cast<std::uint8_t>(buildable.layer));
    hasher.String(buildable.category);
    hasher.String(buildable.requiresMission);
}

void HashLootEntry(ContentHasher& hasher, const items::LootEntry& entry) {
    hasher.String(entry.defId);
    hasher.I32(entry.weight);
    hasher.I32(entry.minCount);
    hasher.I32(entry.maxCount);
}

void HashLootTable(ContentHasher& hasher, const items::LootTable& table) {
    hasher.String(table.id);
    hasher.String(table.name);
    hasher.F32(table.capacityKg);
    hasher.I32(table.rolls);
    hasher.U32(static_cast<std::uint32_t>(table.guaranteed.size()));
    for (const items::LootEntry& entry : table.guaranteed) {
        HashLootEntry(hasher, entry);
    }
    hasher.U32(static_cast<std::uint32_t>(table.entries.size()));
    for (const items::LootEntry& entry : table.entries) {
        HashLootEntry(hasher, entry);
    }
}

void HashPoiDef(ContentHasher& hasher, const PoiDef& poi) {
    hasher.String(poi.id);
    hasher.String(poi.name);
    hasher.I32(poi.threat);
    hasher.F32(poi.markerX);
    hasher.F32(poi.markerY);
    hasher.U32(static_cast<std::uint32_t>(poi.containers.size()));
    for (const PoiContainerDef& container : poi.containers) {
        hasher.F32(container.x);
        hasher.F32(container.y);
        hasher.String(container.tableId);
        hasher.String(container.sprite);
    }
    hasher.I32(poi.enemies.count);
    hasher.F32(poi.enemies.radius);
    hasher.U8(static_cast<std::uint8_t>(poi.enemies.kind));
    hasher.I32(poi.enemies.armoredCount);
    hasher.String(poi.task.description);
    hasher.String(poi.task.targetTable);
    hasher.F32(poi.task.rewardXp);
    hasher.String(poi.task.rewardItem);
    hasher.I32(poi.task.rewardCount);
}

void HashSettlementDef(ContentHasher& hasher, const SettlementDef& def) {
    hasher.String(def.id);
    hasher.String(def.name);
    hasher.F32(def.centerX);
    hasher.F32(def.centerY);
    hasher.F32(def.safeRadius);
    hasher.U32(static_cast<std::uint32_t>(def.npcPoints.size()));
    for (const SettlementNpcPoint& point : def.npcPoints) {
        hasher.String(point.id);
        hasher.U32(static_cast<std::uint32_t>(point.kind));
        hasher.F32(point.x);
        hasher.F32(point.y);
        hasher.F32(point.wanderRadius);
    }
    hasher.U32(static_cast<std::uint32_t>(def.merchantStock.size()));
    for (const int stock : def.merchantStock) {
        hasher.I32(stock);
    }
    hasher.I32(def.restockDays);
    hasher.U32(static_cast<std::uint32_t>(def.missionPool.size()));
    for (const std::string& mission : def.missionPool) {
        hasher.String(mission);
    }
    hasher.U8(def.defense.enabled ? 1u : 0u);
    hasher.I32(def.defense.minDay);
    hasher.I32(def.defense.nightStartHour);
    hasher.I32(def.defense.nightEndHour);
    hasher.F32(def.defense.activationRadius);
    hasher.F32(def.defense.spawnRadius);
    hasher.I32(def.defense.baseRaiders);
    hasher.I32(def.defense.raidersPerNight);
    hasher.I32(def.defense.maxRaiders);
    hasher.U8(static_cast<std::uint8_t>(def.defense.guardBehavior));
    hasher.F32(def.defense.guardHealth);
    hasher.F32(def.defense.guardDamage);
    hasher.F32(def.defense.guardRange);
    hasher.F32(def.defense.guardAttackSeconds);
    hasher.F32(def.defense.raiderHealth);
    hasher.F32(def.defense.raiderDamage);
    hasher.F32(def.defense.raiderRange);
    hasher.F32(def.defense.raiderAttackSeconds);
    hasher.F32(def.defense.raiderSpeed);
}

void HashMissionDef(ContentHasher& hasher, const MissionDef& mission) {
    hasher.String(mission.id);
    hasher.String(mission.name);
    hasher.String(mission.chain);
    hasher.I32(mission.order);
    hasher.U8(static_cast<std::uint8_t>(mission.kind));
    hasher.U8(static_cast<std::uint8_t>(mission.source));
    hasher.U8(static_cast<std::uint8_t>(mission.task));
    hasher.String(mission.prerequisite);
    hasher.I32(mission.minDay);
    hasher.F32(mission.chance);
    hasher.U8(mission.autoActivate ? 1u : 0u);
    hasher.I32(mission.timeLimitDays);
    hasher.I32(mission.retryDays);
    hasher.U32(static_cast<std::uint32_t>(mission.objectives.size()));
    for (const MissionObjectiveDef& objective : mission.objectives) {
        hasher.U8(static_cast<std::uint8_t>(objective.kind));
        hasher.String(objective.target);
        hasher.I32(objective.count);
        hasher.F32(objective.x);
        hasher.F32(objective.y);
        hasher.F32(objective.radius);
        hasher.String(objective.description);
    }
    hasher.F32(mission.reward.xp);
    hasher.String(mission.reward.skill);
    hasher.String(mission.reward.item);
    hasher.I32(mission.reward.itemCount);
    hasher.String(mission.reward.extraItem);
    hasher.I32(mission.reward.extraItemCount);
    hasher.String(mission.reward.blueprint);
    hasher.String(mission.reward.label);
}

void HashStringList(ContentHasher& hasher,
                    const std::vector<std::string>& values) {
    hasher.U32(static_cast<std::uint32_t>(values.size()));
    for (const std::string& value : values) {
        hasher.String(value);
    }
}

void HashForageZoneDef(ContentHasher& hasher, const ForageZoneDef& zone) {
    hasher.String(zone.id);
    hasher.String(zone.name);
    hasher.String(zone.tableId);
    HashStringList(hasher, zone.floors);
    HashStringList(hasher, zone.requiredNearbySprites);
    HashStringList(hasher, zone.excludedNearbySprites);
    HashStringList(hasher, zone.poiIds);
    hasher.F32(zone.nearbyRadius);
    hasher.F32(zone.poiRadius);
    hasher.F32(zone.density);
    hasher.I32(zone.spacing);
    hasher.I32(zone.maxNodes);
    hasher.I32(zone.respawnMinutes);
    hasher.F32(zone.searchSeconds);
    hasher.String(zone.toolDefId);
    hasher.F32(zone.toolWear);
    hasher.F32(zone.survivalXp);
}

void HashFishingSpotDef(ContentHasher& hasher, const FishingSpotDef& spot) {
    hasher.String(spot.id);
    hasher.String(spot.name);
    hasher.String(spot.tableId);
    hasher.String(spot.trashTableId);
    HashStringList(hasher, spot.waterFloors);
    HashStringList(hasher, spot.poiIds);
    hasher.I32(spot.bankRadius);
    hasher.F32(spot.poiRadius);
    hasher.F32(spot.density);
    hasher.I32(spot.spacing);
    hasher.I32(spot.maxSpots);
    hasher.I32(spot.respawnMinutes);
    hasher.F32(spot.waitSeconds);
    hasher.F32(spot.biteWindowSeconds);
    hasher.F32(spot.catchChance);
    hasher.F32(spot.survivalXp);
    hasher.String(spot.rodDefId);
    hasher.F32(spot.rodWear);
    hasher.String(spot.baitDefId);
    hasher.I32(spot.baitCost);
}

void HashAnimalDef(ContentHasher& hasher, const AnimalDef& animal) {
    hasher.String(animal.id);
    hasher.String(animal.name);
    hasher.U8(static_cast<std::uint8_t>(animal.kind));
    hasher.String(animal.sprite);
    hasher.F32(animal.maxHealth);
    hasher.F32(animal.wanderSpeed);
    hasher.F32(animal.fleeSpeed);
    hasher.F32(animal.alertRadius);
    hasher.F32(animal.fleeRadius);
    hasher.F32(animal.hearingRadius);
    hasher.F32(animal.fleeSeconds);
    hasher.F32(animal.damage);
    hasher.F32(animal.attackRange);
    hasher.F32(animal.attackCooldown);
    hasher.String(animal.corpseSprite);
    hasher.F32(animal.corpseSearchSeconds);
    hasher.String(animal.corpseTool);
    hasher.F32(animal.corpseToolWear);
    hasher.String(animal.lootTableId);
    hasher.F32(animal.spawn.forest);
    hasher.F32(animal.spawn.meadow);
    hasher.F32(animal.spawn.poi);
    hasher.I32(animal.spawn.maxPerChunk);
    HashStringList(hasher, animal.spawn.poiIds);
}

void HashCropDef(ContentHasher& hasher, const CropDef& crop) {
    hasher.String(crop.id);
    hasher.String(crop.name);
    hasher.String(crop.seedItem);
    hasher.String(crop.produceItem);
    hasher.F32(crop.growMinutes);
    hasher.F32(crop.waterMinutes);
    hasher.F32(crop.maxWaterMinutes);
    hasher.F32(crop.witherMinutes);
    hasher.F32(crop.waterUsePerMinute);
    hasher.F32(crop.diseaseChance);
    hasher.I32(crop.harvestYield);
    hasher.I32(crop.seedYield);
    hasher.U32(static_cast<std::uint32_t>(crop.stages.size()));
    for (const CropStageDef& stage : crop.stages) {
        hasher.String(stage.name);
        hasher.F32(stage.start);
    }
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

bool IsIgnoredTreeDirectory(const std::filesystem::path& path) {
    const std::string name = LowerAscii(path.filename().string());
    static constexpr std::array<std::string_view, 10> kIgnored{
        ".git",         ".svn",        ".hg",          "__pycache__",
        ".pytest_cache", ".mypy_cache", ".ruff_cache", ".vs",
        ".idea",        "node_modules",
    };
    return std::find(kIgnored.begin(), kIgnored.end(), name) != kIgnored.end();
}

bool IsIgnoredTreeFile(const std::filesystem::path& path) {
    const std::string filename = LowerAscii(path.filename().string());
    if (filename.empty() || filename == ".ds_store" ||
        filename == "thumbs.db" || filename == "desktop.ini" ||
        filename.rfind("~$", 0) == 0 || filename.back() == '~') {
        return true;
    }
    const std::string extension = LowerAscii(path.extension().string());
    return extension == ".log" || extension == ".tmp" || extension == ".swp" ||
           extension == ".swo" || extension == ".swn" ||
           extension == ".bak" || extension == ".pyc" || extension == ".pyo";
}

bool HashFileInto(ContentHasher& hasher, const std::filesystem::path& path) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        return false;
    }
    hasher.U64(static_cast<std::uint64_t>(size));
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    std::array<char, 64 * 1024> buffer{};
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = stream.gcount();
        if (count > 0) {
            hasher.Bytes(buffer.data(), static_cast<std::size_t>(count));
        }
    }
    return stream.eof();
}

}  // namespace

std::uint64_t ComputeTreeHash(std::string_view root) {
    if (root.empty()) {
        return 0;
    }
    const std::filesystem::path rootPath{std::string(root)};
    std::error_code ec;
    if (!std::filesystem::is_directory(rootPath, ec) || ec) {
        return 0;
    }

    std::vector<std::pair<std::string, std::filesystem::path>> files;
    std::filesystem::recursive_directory_iterator iterator(
        rootPath, std::filesystem::directory_options::skip_permission_denied,
        ec);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        if (ec) {
            return 0;
        }
        if (iterator->is_directory(ec)) {
            if (ec) {
                return 0;
            }
            if (IsIgnoredTreeDirectory(iterator->path())) {
                iterator.disable_recursion_pending();
            }
        } else if (iterator->is_regular_file(ec)) {
            if (ec) {
                return 0;
            }
            if (!IsIgnoredTreeFile(iterator->path())) {
                const std::filesystem::path relative =
                    std::filesystem::relative(iterator->path(), rootPath, ec);
                if (ec) {
                    return 0;
                }
                files.emplace_back(relative.generic_string(),
                                   iterator->path());
            }
        } else if (ec) {
            return 0;
        }
        iterator.increment(ec);
    }

    std::sort(files.begin(), files.end(),
              [](const auto& lhs, const auto& rhs) {
                  return lhs.first < rhs.first;
              });

    ContentHasher hasher;
    hasher.String("ZomboidLike.Tree");
    hasher.U32(static_cast<std::uint32_t>(files.size()));
    for (const auto& [relative, path] : files) {
        hasher.String(relative);
        if (!HashFileInto(hasher, path)) {
            return 0;
        }
    }
    const std::uint64_t value = hasher.Value();
    return value == 0 ? 1 : value;
}

std::uint64_t ComputeContentHash(
    const items::ItemRegistry& items, const items::RecipeRegistry& recipes,
    const std::vector<BuildableDef>& buildables,
    const items::LootTableRegistry& lootTables, const PoiConfig& pois,
    const MissionConfig& missions, std::uint64_t extraHash,
    const ForageRegistry* forage, const AnimalRegistry* animals,
    const CropRegistry* crops, const SettlementConfig* settlements) {
    ContentHasher hasher;
    hasher.String("ZomboidLike.Content");
    hasher.U32(kContentHashVersion);

    hasher.U32(static_cast<std::uint32_t>(items.Size()));
    for (const items::ItemDef& item : items.All()) {
        HashItemDef(hasher, item);
    }

    hasher.U32(static_cast<std::uint32_t>(recipes.Size()));
    for (const items::RecipeDef& recipe : recipes.All()) {
        HashRecipeDef(hasher, recipe);
    }

    hasher.U32(static_cast<std::uint32_t>(buildables.size()));
    for (const BuildableDef& buildable : buildables) {
        HashBuildableDef(hasher, buildable);
    }

    hasher.U64(lootTables.Seed());
    hasher.U32(static_cast<std::uint32_t>(lootTables.Size()));
    for (const items::LootTable& table : lootTables.All()) {
        HashLootTable(hasher, table);
    }

    hasher.Bool(pois.enabled);
    hasher.F32(pois.guideRange);
    hasher.U32(static_cast<std::uint32_t>(pois.defs.size()));
    for (const PoiDef& poi : pois.defs) {
        HashPoiDef(hasher, poi);
    }

    hasher.Bool(missions.enabled);
    hasher.U32(missions.randomSeed);
    hasher.U32(static_cast<std::uint32_t>(missions.defs.size()));
    for (const MissionDef& mission : missions.defs) {
        HashMissionDef(hasher, mission);
    }

    hasher.Bool(settlements != nullptr);
    if (settlements != nullptr) {
        hasher.Bool(settlements->enabled);
        hasher.U32(static_cast<std::uint32_t>(settlements->defs.size()));
        for (const SettlementDef& settlement : settlements->defs) {
            HashSettlementDef(hasher, settlement);
        }
    }

    hasher.Bool(forage != nullptr);
    if (forage != nullptr) {
        hasher.U64(forage->Seed());
        hasher.U32(static_cast<std::uint32_t>(forage->ZoneCount()));
        for (const ForageZoneDef& zone : forage->Zones()) {
            HashForageZoneDef(hasher, zone);
        }
        hasher.U32(static_cast<std::uint32_t>(forage->SpotCount()));
        for (const FishingSpotDef& spot : forage->Spots()) {
            HashFishingSpotDef(hasher, spot);
        }
    }

    hasher.Bool(animals != nullptr);
    if (animals != nullptr) {
        hasher.U32(static_cast<std::uint32_t>(animals->Size()));
        for (const AnimalDef& animal : animals->All()) {
            HashAnimalDef(hasher, animal);
        }
    }

    hasher.Bool(crops != nullptr);
    if (crops != nullptr) {
        hasher.U32(static_cast<std::uint32_t>(crops->Size()));
        for (const CropDef& crop : crops->All()) {
            HashCropDef(hasher, crop);
        }
    }

    hasher.U64(extraHash);

    const std::uint64_t value = hasher.Value();
    return value == 0 ? 1 : value;
}

}  // namespace zl::game
