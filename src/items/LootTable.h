#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"

namespace zl::items {

struct LootEntry {
    std::string defId;
    int weight = 1;
    int minCount = 1;
    int maxCount = 1;
};

struct LootTable {
    std::string id;
    std::string name;
    float capacityKg = 20.0f;
    int rolls = 0;
    std::vector<LootEntry> guaranteed;
    std::vector<LootEntry> entries;
};

struct LootRollResult {
    int stacksAdded = 0;
    int itemsAdded = 0;
};

class LootTableRegistry {
public:
    bool LoadFile(const std::filesystem::path& path, const ItemRegistry& items,
                  std::string* error = nullptr);

    bool LoadFromString(std::string_view text, const std::string& source,
                        const ItemRegistry& items, std::string* error = nullptr);

    const LootTable* Find(std::string_view id) const;
    const std::vector<LootTable>& All() const { return m_tables; }
    std::size_t Size() const { return m_tables.size(); }
    std::uint64_t Seed() const { return m_seed; }

private:
    std::vector<LootTable> m_tables;
    std::unordered_map<std::string, std::size_t> m_byId;
    std::uint64_t m_seed = 20261022u;
};

std::uint64_t DeriveLootSeed(std::uint64_t baseSeed, std::string_view tableId,
                             std::uint64_t salt);

LootRollResult RollLootTable(const LootTable& table, const ItemRegistry& items,
                             ecs::Container& target, std::uint64_t seed);

}  // namespace zl::items
