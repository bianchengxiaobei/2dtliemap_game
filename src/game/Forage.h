#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zl::items {
class LootTableRegistry;
}

namespace zl::world {
class World;
}

namespace zl::game {

struct PoiConfig;

// M23-F2a-T1：觅食区域与钓鱼点的数据契约。
// 地块、材料和刷新参数都在 assets/forage.json；运行时只做确定性规划，
// 不把节点位置写进地图母文件，避免流式加载改变地图内容。
struct ForageZoneDef {
    std::string id;
    std::string name;
    std::string tableId;
    std::vector<std::string> floors;
    std::vector<std::string> requiredNearbySprites;
    std::vector<std::string> excludedNearbySprites;
    std::vector<std::string> poiIds;
    float nearbyRadius = 4.0f;
    float poiRadius = 0.0f;
    float density = 0.08f;
    int spacing = 4;
    int maxNodes = 64;
    int respawnMinutes = 180;
    float searchSeconds = 2.5f;
    std::string toolDefId;
    float toolWear = 0.0f;
    float survivalXp = 6.0f;
};

struct FishingSpotDef {
    std::string id;
    std::string name;
    std::string tableId;
    std::string trashTableId;
    std::vector<std::string> waterFloors;
    std::vector<std::string> poiIds;
    int bankRadius = 3;
    float poiRadius = 0.0f;
    float density = 0.3f;
    int spacing = 4;
    int maxSpots = 12;
    int respawnMinutes = 360;
    float waitSeconds = 5.0f;
    float biteWindowSeconds = 2.0f;
    float catchChance = 0.6f;
    float survivalXp = 10.0f;
    std::string rodDefId = "fishing_rod";
    float rodWear = 0.02f;
    std::string baitDefId = "fish_bait";
    int baitCost = 1;
};

struct ForageNode {
    std::string zoneId;
    std::string tableId;
    int x = 0;
    int y = 0;
    int z = 0;
};

struct FishingSpot {
    std::string spotId;
    std::string tableId;
    int x = 0;
    int y = 0;
    int z = 0;
    int waterX = 0;
    int waterY = 0;
    int waterZ = 0;
};

struct ForageBounds {
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
};

class ForageRegistry {
public:
    bool LoadFile(const std::filesystem::path& path,
                  const items::LootTableRegistry& lootTables,
                  std::string* error = nullptr);
    bool LoadFromString(std::string_view text, const std::string& source,
                        const items::LootTableRegistry& lootTables,
                        std::string* error = nullptr);

    bool MergeFile(const std::filesystem::path& path,
                   const items::LootTableRegistry& lootTables,
                   std::string* error = nullptr);
    bool MergeFromString(std::string_view text, const std::string& source,
                         const items::LootTableRegistry& lootTables,
                         std::string* error = nullptr);

    const ForageZoneDef* FindZone(std::string_view id) const;
    const FishingSpotDef* FindSpot(std::string_view id) const;
    const std::vector<ForageZoneDef>& Zones() const { return m_zones; }
    const std::vector<FishingSpotDef>& Spots() const { return m_spots; }
    std::size_t ZoneCount() const { return m_zones.size(); }
    std::size_t SpotCount() const { return m_spots.size(); }
    std::uint64_t Seed() const { return m_seed; }

private:
    std::vector<ForageZoneDef> m_zones;
    std::vector<FishingSpotDef> m_spots;
    std::unordered_map<std::string, std::size_t> m_zoneById;
    std::unordered_map<std::string, std::size_t> m_spotById;
    std::uint64_t m_seed = 20261029u;
};

// 在已加载地图的矩形范围内按地块/邻近物体/POI 确定性生成资源节点。
// 同一 world 与 seed 下结果稳定；房间内、阻挡格和间距不足的位置不会生成。
std::vector<ForageNode> PlanForageNodes(const ForageRegistry& registry,
                                        const world::World& world,
                                        const PoiConfig& pois,
                                        const ForageBounds& bounds);

// 水面旁的可走岸格生成钓鱼点；waterX/waterY 指向最近水面。
std::vector<FishingSpot> PlanFishingSpots(const ForageRegistry& registry,
                                          const world::World& world,
                                          const PoiConfig& pois,
                                          const ForageBounds& bounds);

// 水面仍与数据契约一致（地图热重载/水域变化后交互应失效）。
bool FishingSpotWaterValid(const FishingSpotDef& def, const world::World& world,
                           const FishingSpot& spot);

}  // namespace zl::game
