#pragma once

#include <string>
#include <vector>

#include "game/BuildingRules.h"

namespace zl::core {
class Config;
}

namespace zl::items {
class ItemRegistry;
class RecipeRegistry;
}

namespace zl::game {

struct ModContentSource {
    std::string id;
    std::string root;
};

struct ModContentResult {
    int found = 0;
    int applied = 0;
    int skipped = 0;
    int itemsAdded = 0;
    int recipesAdded = 0;
    int buildablesSkipped = 0;
    std::string error;
    std::vector<BuildableDef> buildables;
    std::vector<std::string> assetManifests;
    std::vector<std::string> lootFiles;
    std::vector<std::string> forageFiles;
    std::vector<std::string> poiFiles;
    std::vector<std::string> missionFiles;
    std::vector<std::string> cropFiles;
    std::vector<std::string> settlementFiles;
    std::vector<std::string> worldEventFiles;
    std::vector<std::string> companionFiles;
    std::vector<std::string> vehicleModFiles;
    std::vector<std::string> trapFiles;
    std::vector<std::string> skippedIds;
    std::vector<ModContentSource> sources;
};

ModContentResult LoadModContent(const core::Config& config,
                                items::ItemRegistry& items,
                                items::RecipeRegistry& recipes);

}  // namespace zl::game
