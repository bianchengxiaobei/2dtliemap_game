#include "game/AnimalHarvest.h"

#include <algorithm>
#include <utility>

#include "characters/components/AnimalComponents.h"
#include "core/Log.h"
#include "game/Animals.h"
#include "items/InventoryOps.h"
#include "items/ItemRegistry.h"
#include "items/LootTable.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "animals";

bool HasToolInEquipment(const ecs::Equipment& equipment,
                        const std::string& toolId) {
    for (const auto& slot : equipment.slots) {
        if (slot.has_value() && slot->defId == toolId &&
            slot->condition > 0.0f) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool AnimalCorpseToolAvailable(const AnimalDef& def,
                               const ecs::Container* bag,
                               const ecs::Equipment* equipment,
                               std::string* toolId) {
    if (toolId != nullptr) {
        *toolId = def.corpseTool;
    }
    if (def.corpseTool.empty()) {
        return true;
    }
    if (bag != nullptr &&
        items::CountUsableTools(*bag, def.corpseTool) > 0) {
        return true;
    }
    return equipment != nullptr &&
           HasToolInEquipment(*equipment, def.corpseTool);
}

bool WearAnimalCorpseTool(const AnimalDef& def, ecs::Container* bag,
                          ecs::Equipment* equipment, bool* broke) {
    if (broke != nullptr) {
        *broke = false;
    }
    if (def.corpseTool.empty() || def.corpseToolWear <= 0.0f) {
        return true;
    }
    if (bag != nullptr) {
        for (auto it = bag->items.begin(); it != bag->items.end(); ++it) {
            if (it->defId != def.corpseTool || it->condition <= 0.0f) {
                continue;
            }
            it->condition = std::max(0.0f, it->condition - def.corpseToolWear);
            if (it->condition <= 0.0f) {
                if (it->count > 1) {
                    --it->count;
                    it->condition = 1.0f;
                } else {
                    bag->items.erase(it);
                }
                if (broke != nullptr) {
                    *broke = true;
                }
            }
            return true;
        }
    }
    if (equipment != nullptr) {
        for (auto& slot : equipment->slots) {
            if (!slot.has_value() || slot->defId != def.corpseTool ||
                slot->condition <= 0.0f) {
                continue;
            }
            slot->condition =
                std::max(0.0f, slot->condition - def.corpseToolWear);
            if (slot->condition <= 0.0f) {
                slot.reset();
                if (broke != nullptr) {
                    *broke = true;
                }
            }
            return true;
        }
    }
    return false;
}

AnimalHarvestResult HarvestAnimalCorpse(
    ecs::EcsWorld& world, const items::ItemRegistry& items,
    const items::LootTableRegistry& lootTables, const AnimalRegistry& animals,
    ecs::Entity corpse, ecs::Container* bag,
    ecs::Equipment* equipment) {
    AnimalHarvestResult result;
    if (!world.Alive(corpse)) {
        return result;
    }
    entt::registry& registry = world.Registry();
    auto* data = registry.try_get<ecs::AnimalCorpse>(corpse);
    if (data == nullptr) {
        return result;
    }
    result.found = true;
    result.speciesId = data->speciesId;
    if (data->harvested) {
        result.alreadyHarvested = true;
        return result;
    }

    const AnimalDef* def = animals.Find(data->speciesId);
    if (def == nullptr) {
        result.error = "动物物种不存在: " + data->speciesId;
        return result;
    }
    const items::LootTable* table = lootTables.Find(def->lootTableId);
    if (table == nullptr) {
        result.error = "动物战利品表不存在: " + def->lootTableId;
        return result;
    }
    result.tableId = table->id;
    result.toolId = def->corpseTool;
    if (!AnimalCorpseToolAvailable(*def, bag, equipment, &result.toolId)) {
        result.missingTool = true;
        return result;
    }

    ecs::Container rolled;
    rolled.capacityKg = table->capacityKg;
    const std::uint64_t seed = items::DeriveLootSeed(
        lootTables.Seed(), table->id, world.IdOf(corpse).id);
    (void)items::RollLootTable(*table, items, rolled, seed);
    result.itemStacks = static_cast<int>(rolled.items.size());
    for (const ecs::ItemInstance& item : rolled.items) {
        result.itemsAdded += item.count;
    }

    auto& container = registry.get_or_emplace<ecs::Container>(corpse);
    container = std::move(rolled);
    ecs::LootContainer loot;
    loot.tableId = table->id;
    loot.rollSeed = seed;
    loot.searched = true;
    registry.emplace_or_replace<ecs::LootContainer>(corpse, std::move(loot));
    data->harvested = true;

    bool broke = false;
    if (!WearAnimalCorpseTool(*def, bag, equipment, &broke)) {
        result.error = "工具已不在背包中";
    }
    result.toolBroke = broke;
    result.harvested = true;
    ZL_INFO(kLogModule,
            "剥取动物尸体：物种={}，实体={}，seed={}，{} 堆/{} 件，工具={}，损坏={}",
            data->speciesId, world.IdOf(corpse).id, seed, result.itemStacks,
            result.itemsAdded, def->corpseTool, broke);
    return result;
}

}  // namespace zl::game
