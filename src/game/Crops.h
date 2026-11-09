#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zl::items {
class ItemRegistry;
}

namespace zl::game {

// F5-T1：作物数据契约。种植参数在创建农田时写入 FarmPlot，
// 因此旧档不依赖 crops.json 仍然可以继续生长/收获。
struct CropStageDef {
    std::string name;
    float start = 0.0f;
};

struct CropDef {
    std::string id;
    std::string name;
    std::string seedItem;
    std::string produceItem;
    float growMinutes = 240.0f;
    float waterMinutes = 360.0f;
    float maxWaterMinutes = 720.0f;
    float witherMinutes = 1440.0f;
    float waterUsePerMinute = 1.0f;
    float diseaseChance = 0.0f;
    int harvestYield = 2;
    int seedYield = 1;
    std::vector<CropStageDef> stages;
};

class CropRegistry {
public:
    bool LoadFile(const std::filesystem::path& path,
                  const items::ItemRegistry& items,
                  std::string* error = nullptr);
    bool LoadFromString(std::string_view text, const std::string& source,
                        const items::ItemRegistry& items,
                        std::string* error = nullptr);

    bool MergeFile(const std::filesystem::path& path,
                   const items::ItemRegistry& items,
                   std::string* error = nullptr);
    bool MergeFromString(std::string_view text, const std::string& source,
                         const items::ItemRegistry& items,
                         std::string* error = nullptr);

    const CropDef* Find(std::string_view id) const;
    const CropDef* FindBySeed(std::string_view seedItem) const;
    const std::vector<CropDef>& All() const { return m_crops; }
    std::size_t Size() const { return m_crops.size(); }

private:
    std::vector<CropDef> m_crops;
    std::unordered_map<std::string, std::size_t> m_byId;
    std::unordered_map<std::string, std::size_t> m_bySeed;
};

// 生长进度对应的阶段下标；比例会夹在 [0,1]。
std::size_t CropGrowthStageIndex(const CropDef& crop, float growthRatio);
const char* CropGrowthStageName(const CropDef& crop, float growthRatio);

}  // namespace zl::game
