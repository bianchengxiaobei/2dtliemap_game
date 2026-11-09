#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "world/GridSquare.h"

namespace zl::game {

struct Flammability {
    float ignitionThreshold = 0.35f;
    float burnSeconds = 60.0f;
    float smokePerSecond = 0.5f;
    float heatRadius = 2.0f;
};

struct FireDrop {
    std::string itemId;
    int count = 1;
};

struct FireMaterialDef {
    std::string id;
    std::string name;
    std::vector<std::string> sprites;
    Flammability flammability;
    std::vector<FireDrop> charredDrops;
};

class FireRegistry {
public:
    bool LoadFile(const std::filesystem::path& path,
                  std::string* error = nullptr);
    bool LoadFromString(std::string_view text, const std::string& source,
                        std::string* error = nullptr);

    const FireMaterialDef* Find(std::string_view id) const;
    const FireMaterialDef* FindBySprite(std::string_view sprite) const;
    const std::vector<FireMaterialDef>& All() const { return m_materials; }
    std::size_t Size() const { return m_materials.size(); }

private:
    std::vector<FireMaterialDef> m_materials;
    std::unordered_map<std::string, std::size_t> m_byId;
    std::unordered_map<std::string, std::size_t> m_bySprite;
};

const FireMaterialDef* FindFireMaterial(const FireRegistry& registry,
                                        const world::GridSquare& square);
const FireMaterialDef* FindFireMaterial(const FireRegistry& registry,
                                        const world::GridSquare& square,
                                        world::SquareLayer layer);

}  // namespace zl::game
