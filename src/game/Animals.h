#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "characters/components/AnimalComponents.h"

namespace zl::items {
class ItemRegistry;
class LootTableRegistry;
}  // namespace zl::items

namespace zl::game {

struct AnimalSpawnRule {
    float forest = 0.0f;
    float meadow = 0.0f;
    float poi = 0.0f;
    int maxPerChunk = 0;
    std::vector<std::string> poiIds;
};

struct AnimalDef {
    std::string id;
    std::string name;
    ecs::AnimalKind kind = ecs::AnimalKind::Passive;
    std::string sprite;
    float maxHealth = 40.0f;
    float wanderSpeed = 2.0f;
    float fleeSpeed = 4.0f;
    float alertRadius = 8.0f;
    float fleeRadius = 6.0f;
    float hearingRadius = 10.0f;
    float fleeSeconds = 4.0f;
    float damage = 0.0f;
    float attackRange = 0.0f;
    float attackCooldown = 0.0f;
    std::string corpseSprite = "corpse.placeholder";
    float corpseSearchSeconds = 1.5f;
    std::string corpseTool;
    float corpseToolWear = 0.0f;
    std::string lootTableId;
    AnimalSpawnRule spawn;
};

class AnimalRegistry {
public:
    bool LoadFile(const std::filesystem::path& path,
                  const items::ItemRegistry& items,
                  const items::LootTableRegistry& lootTables,
                  std::string* error = nullptr);
    bool LoadFromString(std::string_view text, const std::string& source,
                        const items::ItemRegistry& items,
                        const items::LootTableRegistry& lootTables,
                        std::string* error = nullptr);

    const AnimalDef* Find(std::string_view id) const;
    const std::vector<AnimalDef>& All() const { return m_animals; }
    std::size_t Size() const { return m_animals.size(); }

private:
    std::vector<AnimalDef> m_animals;
    std::unordered_map<std::string, std::size_t> m_byId;
};

const char* AnimalKindName(ecs::AnimalKind kind);

float AnimalVisualScale(std::string_view speciesId);

}
