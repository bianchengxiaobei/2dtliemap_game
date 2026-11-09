#include "game/BuildingRules.h"

#include <cmath>
#include <unordered_set>
#include <utility>

#include "core/Config.h"
#include "core/Log.h"
#include "game/PlayerStorage.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "building";

bool AnySpriteMatches(const BuildableDef& def, std::string_view sprite) {
    return BuildableMatchesSprite(def, sprite);
}

bool BuildableSpritesOverlap(const BuildableDef& a, const BuildableDef& b) {
    return AnySpriteMatches(a, b.sprite) ||
           (!b.spriteVertical.empty() &&
            AnySpriteMatches(a, b.spriteVertical)) ||
           (!b.openSprite.empty() && AnySpriteMatches(a, b.openSprite)) ||
           (!b.openSpriteVertical.empty() &&
            AnySpriteMatches(a, b.openSpriteVertical));
}

bool StorageDefsAreDistinct(const BuildableDef& a, const BuildableDef& b) {
    return IsStorageBuildable(a) && IsStorageBuildable(b) &&
           (a.storageSlots != b.storageSlots ||
            std::abs(a.storageKg - b.storageKg) > 0.01f);
}

}  // namespace

std::vector<BuildableDef> ParseBuildableDefs(const nlohmann::json& types,
                                             std::string_view source) {
    std::vector<BuildableDef> defs;
    if (!types.is_array()) {
        ZL_WARN(kLogModule, "{}: building types 必须是数组", source);
        return defs;
    }
    std::unordered_set<std::string> ids;
    for (const auto& item : types) {
        if (!item.is_object()) {
            ZL_WARN(kLogModule, "{}: 忽略非对象建造定义", source);
            continue;
        }
        BuildableDef def;
        def.id = item.value("id", std::string());
        def.name = item.value("name", std::string());
        def.sprite = item.value("sprite", std::string());
        def.spriteVertical = item.value("spriteVertical", std::string());
        def.openSprite = item.value("openSprite", std::string());
        def.openSpriteVertical =
            item.value("openSpriteVertical", std::string());
        def.door = item.value("door", false);
        def.category = item.value("category", std::string("facility"));
        def.requiresMission =
            item.value("requiresMission", std::string());
        const auto layer =
            BuildLayerFromName(item.value("layer", std::string("furniture")));
        if (layer.has_value()) {
            def.layer = *layer;
        }
        def.woodCost = std::clamp(item.value("woodCost", 0), 1, 99);
        def.maxHp = std::clamp(item.value("maxHp", 0.0f), 0.0f, 100000.0f);
        def.blocksMovement = item.value("blocksMovement", false);
        def.storageKg =
            std::clamp(item.value("storageKg", 0.0f), 0.0f, 1000.0f);
        def.storageSlots = std::clamp(item.value("storageSlots", 0), 0,
                                      kMaxPlayerStorageStacks);
        if (def.storageKg > 0.0f && def.storageSlots <= 0) {
            def.storageSlots = kMaxPlayerStorageStacks;
        }
        const bool duplicateId =
            !def.id.empty() && ids.contains(def.id);
        if (def.id.empty() || duplicateId || def.name.empty() ||
            def.sprite.empty() || !layer.has_value() || def.woodCost <= 0 ||
            (def.blocksMovement && def.maxHp <= 0.0f) ||
            (def.door && !def.blocksMovement) ||
            (def.door &&
             (def.openSprite.empty() ||
              (!def.spriteVertical.empty() &&
               def.openSpriteVertical.empty()))) ||
            (def.layer == world::SquareLayer::Floor && def.blocksMovement) ||
            (def.storageKg > 0.0f && def.storageSlots <= 0)) {
            ZL_WARN(kLogModule, "{}: 忽略非法建造定义：{}", source, def.id);
            continue;
        }
        ids.insert(def.id);
        defs.push_back(std::move(def));
    }
    return defs;
}

std::vector<BuildableDef> LoadBuildableDefs(const core::Config& config) {
    std::vector<BuildableDef> defs;
    const auto& root = config.Raw();
    const auto building = root.find("building");
    const std::string source =
        config.Path().empty() ? "config/game.json" : config.Path().string();
    if (building != root.end() && building->is_object()) {
        const auto types = building->find("types");
        if (types != building->end()) {
            defs = ParseBuildableDefs(*types, source);
        }
    }
    if (defs.empty()) {
        defs.assign(DefaultBuildables().begin(), DefaultBuildables().end());
        ZL_WARN(kLogModule, "building.types 缺失或无效，使用内置建造列表");
    }
    return defs;
}

int AppendBuildableDefs(std::vector<BuildableDef>& defs,
                        const std::vector<BuildableDef>& extra) {
    int added = 0;
    for (const BuildableDef& def : extra) {
        bool duplicateId = false;
        bool spriteConflict = false;
        for (const BuildableDef& existing : defs) {
            if (existing.id == def.id) {
                duplicateId = true;
                break;
            }
            if (BuildableSpritesOverlap(existing, def) &&
                !StorageDefsAreDistinct(existing, def)) {
                spriteConflict = true;
                break;
            }
        }
        if (duplicateId) {
            ZL_WARN(kLogModule, "忽略重复建造 id：{}", def.id);
            continue;
        }
        if (spriteConflict) {
            ZL_WARN(kLogModule, "忽略 sprite 冲突的建造项：{}（{}）", def.id,
                    def.sprite);
            continue;
        }
        defs.push_back(def);
        ++added;
    }
    return added;
}

}  // namespace zl::game
