#pragma once

#include <string>

#include "ecs/EcsWorld.h"
#include "items/ItemComponents.h"

namespace zl::items {
class ItemRegistry;
class LootTableRegistry;
}  // namespace zl::items

namespace zl::game {

struct AnimalDef;
class AnimalRegistry;

struct AnimalHarvestResult {
    bool found = false;
    bool harvested = false;
    bool alreadyHarvested = false;
    bool missingTool = false;
    bool toolBroke = false;
    int itemStacks = 0;
    int itemsAdded = 0;
    std::string speciesId;
    std::string tableId;
    std::string toolId;
    std::string error;
};

bool AnimalCorpseToolAvailable(const AnimalDef& def,
                               const ecs::Container* bag,
                               const ecs::Equipment* equipment,
                               std::string* toolId = nullptr);

bool WearAnimalCorpseTool(const AnimalDef& def, ecs::Container* bag,
                          ecs::Equipment* equipment, bool* broke = nullptr);

AnimalHarvestResult HarvestAnimalCorpse(
    ecs::EcsWorld& world, const items::ItemRegistry& items,
    const items::LootTableRegistry& lootTables, const AnimalRegistry& animals,
    ecs::Entity corpse, ecs::Container* bag,
    ecs::Equipment* equipment);

}  // namespace zl::game
