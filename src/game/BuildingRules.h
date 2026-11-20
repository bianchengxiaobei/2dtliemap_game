#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "world/GridSquare.h"

namespace zl::core {
class Config;
}

namespace zl::game {

struct BuildableDef {
    std::string id;
    std::string name;
    std::string sprite;
    std::string spriteVertical;
    int woodCost = 0;
    float maxHp = 0.0f;
    bool blocksMovement = false;
    float storageKg = 0.0f;
    int storageSlots = 0;
    std::string openSprite;
    std::string openSpriteVertical;
    bool door = false;
    world::SquareLayer layer = world::SquareLayer::Furniture;
    std::string category = "facility";
    std::string requiresMission;
};

struct BuiltBarricade {
    std::string sprite;
    float hp = 0.0f;
    float maxHp = 0.0f;
};

inline std::uint64_t BuildCellKey(int wx, int wy) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(wx)) << 32U) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(wy));
}

inline int BuildCellX(std::uint64_t key) {
    return static_cast<std::int32_t>(key >> 32U);
}

inline int BuildCellY(std::uint64_t key) {
    return static_cast<std::int32_t>(key & 0xffffffffU);
}

// C6：无主结构（owner=0）允许操作；有主结构只允许同一稳定 ID 的玩家。
inline bool CanEditStructure(std::uint64_t ownerStableId,
                             std::uint64_t localStableId) {
    return ownerStableId == 0 ||
           (localStableId != 0 && ownerStableId == localStableId);
}

// X-F1 阶段 2b：玩家地图结构的稳定 ID。由格坐标 + 层确定性生成，
// 不依赖 chunk 重载顺序或服务端进程生命周期；同一格同层重建会复用该 ID。
inline std::uint64_t MakeStructureId(int wx, int wy, int z, int layer) {
    std::uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    mix(static_cast<std::uint32_t>(wx));
    mix(static_cast<std::uint32_t>(wy));
    mix(static_cast<std::uint32_t>(z));
    mix(static_cast<std::uint32_t>(layer));
    return hash == 0 ? 1 : hash;
}

inline std::optional<world::SquareLayer> BuildLayerFromName(
    std::string_view name) {
    if (name == "floor") {
        return world::SquareLayer::Floor;
    }
    if (name == "wall") {
        return world::SquareLayer::Wall;
    }
    if (name == "furniture" || name.empty()) {
        return world::SquareLayer::Furniture;
    }
    return std::nullopt;
}

inline std::string_view BuildCategoryLabel(std::string_view category) {
    if (category == "defense") {
        return "防御结构";
    }
    if (category == "storage") {
        return "仓库";
    }
    if (category == "facility") {
        return "设施";
    }
    return "其他";
}

inline bool IsFloorBuildable(const BuildableDef& def) {
    return def.layer == world::SquareLayer::Floor;
}

inline bool BuildableSupportsVertical(const BuildableDef& def) {
    return !def.spriteVertical.empty();
}

inline std::string_view BuildableSprite(const BuildableDef& def, bool vertical,
                                        bool open = false) {
    if (open) {
        if (vertical && !def.openSpriteVertical.empty()) {
            return def.openSpriteVertical;
        }
        if (!def.openSprite.empty()) {
            return def.openSprite;
        }
    }
    if (vertical && !def.spriteVertical.empty()) {
        return def.spriteVertical;
    }
    return def.sprite;
}

inline bool BuildableMatchesSprite(const BuildableDef& def,
                                   std::string_view sprite) {
    return sprite == def.sprite ||
           (!def.spriteVertical.empty() && sprite == def.spriteVertical) ||
           (!def.openSprite.empty() && sprite == def.openSprite) ||
           (!def.openSpriteVertical.empty() &&
            sprite == def.openSpriteVertical);
}

inline bool IsOpenDoorSprite(const BuildableDef& def, std::string_view sprite) {
    return (!def.openSprite.empty() && sprite == def.openSprite) ||
           (!def.openSpriteVertical.empty() &&
            sprite == def.openSpriteVertical);
}

inline bool IsVerticalBuildSprite(const BuildableDef& def,
                                  std::string_view sprite) {
    return (!def.spriteVertical.empty() && sprite == def.spriteVertical) ||
           (!def.openSpriteVertical.empty() &&
            sprite == def.openSpriteVertical);
}

inline const std::array<BuildableDef, 18>& DefaultBuildables() {
    static const std::array<BuildableDef, 18> kDefaults{{
        BuildableDef{.id = "barricade",
                     .name = "木栅栏",
                     .sprite = "object.barricade",
                     .woodCost = 3,
                     .maxHp = 120.0f,
                     .blocksMovement = true,
                     .category = "defense"},
        BuildableDef{.id = "wall",
                     .name = "木墙",
                     .sprite = "object.wall_h",
                     .spriteVertical = "object.wall_v",
                     .woodCost = 3,
                     .maxHp = 200.0f,
                     .blocksMovement = true,
                     .layer = world::SquareLayer::Wall,
                     .category = "defense"},
        BuildableDef{.id = "door",
                     .name = "木门",
                     .sprite = "object.door_closed_h",
                     .spriteVertical = "object.door_closed_v",
                     .woodCost = 3,
                     .maxHp = 120.0f,
                     .blocksMovement = true,
                     .openSprite = "object.door_open_h",
                     .openSpriteVertical = "object.door_open_v",
                     .door = true,
                     .layer = world::SquareLayer::Wall,
                     .category = "defense"},
        BuildableDef{.id = "floor",
                     .name = "木地板",
                     .sprite = "tile.floor.wood",
                     .woodCost = 1,
                     .maxHp = 0.0f,
                     .blocksMovement = false,
                     .layer = world::SquareLayer::Floor,
                     .category = "defense"},
        BuildableDef{.id = "campfire",
                     .name = "篝火",
                     .sprite = "object.campfire",
                     .woodCost = 2,
                     .maxHp = 0.0f,
                     .blocksMovement = false},
        BuildableDef{.id = "workbench",
                     .name = "工作台",
                     .sprite = "object.workbench",
                     .woodCost = 4,
                     .maxHp = 0.0f,
                     .blocksMovement = false},
        BuildableDef{.id = "rain_collector",
                     .name = "雨水收集器",
                     .sprite = "object.locker",
                     .woodCost = 5,
                     .maxHp = 0.0f,
                     .blocksMovement = false},
        BuildableDef{.id = "bed",
                     .name = "床",
                     .sprite = "object.bed",
                     .woodCost = 4,
                     .maxHp = 0.0f,
                     .blocksMovement = false},
        BuildableDef{.id = "storage",
                     .name = "木箱",
                     .sprite = "object.crate",
                     .woodCost = 4,
                     .maxHp = 0.0f,
                     .blocksMovement = false,
                     .storageKg = 50.0f,
                     .storageSlots = 6,
                     .category = "storage"},
        BuildableDef{.id = "farm_plot",
                     .name = "农田",
                     .sprite = "object.farm_plot",
                     .woodCost = 2,
                     .maxHp = 0.0f,
                     .blocksMovement = false},
        BuildableDef{.id = "generator",
                     .name = "发电机",
                     .sprite = "object.generator",
                     .woodCost = 4,
                     .maxHp = 0.0f,
                     .blocksMovement = false},
        BuildableDef{.id = "fridge",
                     .name = "冷藏柜",
                     .sprite = "object.fridge",
                     .woodCost = 4,
                     .maxHp = 0.0f,
                     .blocksMovement = false,
                     .storageKg = 30.0f,
                     .storageSlots = 4,
                     .category = "storage"},
        BuildableDef{.id = "spike_trap",
                     .name = "尖刺陷阱",
                     .sprite = "object.trap",
                     .woodCost = 2,
                     .maxHp = 0.0f,
                     .blocksMovement = false,
                     .category = "defense"},
        BuildableDef{.id = "floodlight",
                     .name = "泛光灯",
                     .sprite = "object.floodlight",
                     .woodCost = 2,
                     .maxHp = 80.0f,
                     .blocksMovement = false,
                     .category = "facility"},
        BuildableDef{.id = "electric_fence",
                     .name = "鐢电綉",
                     .sprite = "object.electric_fence",
                     .woodCost = 3,
                     .maxHp = 100.0f,
                     .blocksMovement = true,
                     .category = "defense"},
        BuildableDef{.id = "bear_trap",
                     .name = "捕兽夹",
                     .sprite = "object.trap",
                     .woodCost = 3,
                     .maxHp = 0.0f,
                     .blocksMovement = false,
                     .category = "defense"},
        BuildableDef{.id = "tripwire_alarm",
                     .name = "绊线警报",
                     .sprite = "object.trap",
                     .woodCost = 1,
                     .maxHp = 0.0f,
                     .blocksMovement = false,
                     .category = "defense"},
        BuildableDef{.id = "fuel_trap",
                     .name = "燃油陷阱",
                     .sprite = "object.trap",
                     .woodCost = 4,
                     .maxHp = 0.0f,
                     .blocksMovement = false,
                     .category = "defense"},
    }};
    return kDefaults;
}

// X-F1：客户端与无窗口服务器共用同一份建造目录解析，保证命令里的 index 一致。
std::vector<BuildableDef> LoadBuildableDefs(const core::Config& config);

std::vector<BuildableDef> ParseBuildableDefs(
    const nlohmann::json& types, std::string_view source);

int AppendBuildableDefs(std::vector<BuildableDef>& defs,
                        const std::vector<BuildableDef>& extra);

inline world::SquareObject MakeBuiltObject(const BuildableDef& def,
                                           bool vertical = false,
                                           bool open = false) {
    world::SquareObject object{
        std::string(BuildableSprite(def, vertical, open)), 0, 0,
        def.blocksMovement && !(def.door && open)};
    object.hp = def.maxHp > 0.0f ? def.maxHp : -1.0f;
    object.playerBuilt = true;
    return object;
}

inline float ResolveBarricadeHp(const world::SquareObject& object, float maxHp) {
    if (maxHp <= 0.0f) {
        return 0.0f;
    }
    if (object.hp < 0.0f) {
        return maxHp;
    }
    return std::min(object.hp, maxHp);
}

inline bool IsRepairableBarricade(const world::SquareObject& object,
                                  float maxHp) {
    return object.playerBuilt && maxHp > 0.0f &&
           ResolveBarricadeHp(object, maxHp) < maxHp;
}

inline float RepairBarricadeHp(float hp, float maxHp, float amount) {
    if (maxHp <= 0.0f || amount <= 0.0f) {
        return std::min(hp, maxHp);
    }
    return std::min(maxHp, hp + amount);
}

inline int BuildWoodRefund(int woodCost, float ratio) {
    if (woodCost <= 0 || ratio <= 0.0f) {
        return 0;
    }
    const float clamped = std::clamp(ratio, 0.0f, 1.0f);
    return static_cast<int>(
        std::ceil(static_cast<float>(woodCost) * clamped));
}

inline bool IsStorageBuildable(const BuildableDef& def) {
    return def.storageKg > 0.0f;
}

template <std::size_t N>
inline const BuildableDef* FindBuildableBySprite(
    const std::array<BuildableDef, N>& defs, std::string_view sprite) {
    for (const BuildableDef& def : defs) {
        if (BuildableMatchesSprite(def, sprite)) {
            return &def;
        }
    }
    return nullptr;
}

inline const BuildableDef* FindBuildableBySprite(
    const std::vector<BuildableDef>& defs, std::string_view sprite) {
    for (const BuildableDef& def : defs) {
        if (BuildableMatchesSprite(def, sprite)) {
            return &def;
        }
    }
    return nullptr;
}

inline bool IsBuildCellClear(const world::GridSquare& square) {
    if (!square.HasFloor()) {
        return false;
    }
    for (int layer = static_cast<int>(world::SquareLayer::Wall);
         layer < world::kSquareLayerCount; ++layer) {
        for (const world::SquareObject& object :
             square.Layer(static_cast<world::SquareLayer>(layer))) {
            if (object.blocksMovement ||
                object.sprite.rfind("object.", 0) == 0) {
                return false;
            }
        }
    }
    return true;
}

inline bool IsBuildCellFreeFor(const BuildableDef& def,
                               const world::GridSquare& square) {
    if (!square.HasFloor()) {
        return false;
    }
    if (def.layer == world::SquareLayer::Floor) {
        const world::SquareObject* floor = square.Floor();
        if (floor != nullptr && floor->playerBuilt) {
            return false;
        }
        if (!square.Layer(world::SquareLayer::Wall).empty()) {
            return false;
        }
        for (int layer = static_cast<int>(world::SquareLayer::Wall);
             layer < world::kSquareLayerCount; ++layer) {
            for (const world::SquareObject& object :
                 square.Layer(static_cast<world::SquareLayer>(layer))) {
                if (object.blocksMovement ||
                    object.sprite.rfind("object.", 0) == 0) {
                    return false;
                }
            }
        }
        return true;
    }
    if (!IsBuildCellClear(square)) {
        return false;
    }
    if (!square.Layer(world::SquareLayer::Wall).empty()) {
        return false;
    }
    return true;
}

}  // namespace zl::game
