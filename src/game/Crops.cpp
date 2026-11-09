#include "game/Crops.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "items/ItemRegistry.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "crops";
constexpr const char* kFormat = "crops-v1";
constexpr std::size_t kMaxIdLength = 64;
constexpr std::size_t kMaxNameLength = 64;
constexpr std::size_t kMaxCrops = 64;
constexpr std::size_t kMaxStages = 6;

bool ValidId(std::string_view value) {
    return !value.empty() && value.size() <= kMaxIdLength &&
           std::all_of(value.begin(), value.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '-';
           });
}

bool CheckKeys(const nlohmann::json& entry,
               const std::unordered_set<std::string>& allowed,
               const std::string& where, std::string* error) {
    for (auto it = entry.begin(); it != entry.end(); ++it) {
        if (!allowed.contains(it.key())) {
            if (error != nullptr) {
                *error = where + " 未知字段 '" + it.key() + "'";
            }
            return false;
        }
    }
    return true;
}

bool ReadString(const nlohmann::json& node, const char* key, std::string* out,
                std::string* error, const std::string& where) {
    if (!node.contains(key) || !node[key].is_string()) {
        if (error != nullptr) {
            *error = where + " 缺少字符串字段 '" + key + "'";
        }
        return false;
    }
    const std::string value = node[key].get<std::string>();
    if (value.empty() || value.size() > kMaxNameLength) {
        if (error != nullptr) {
            *error = where + " 字段 '" + key + "' 长度非法";
        }
        return false;
    }
    *out = value;
    return true;
}

bool ReadId(const nlohmann::json& node, const char* key, std::string* out,
            std::string* error, const std::string& where) {
    if (!ReadString(node, key, out, error, where)) {
        return false;
    }
    if (!ValidId(*out)) {
        if (error != nullptr) {
            *error = where + " id 非法（1~64，仅小写字母/数字/下划线/连字符）";
        }
        return false;
    }
    return true;
}

bool ReadItemId(const nlohmann::json& node, const char* key,
                const items::ItemRegistry& items, std::string* out,
                std::string* error, const std::string& where) {
    if (!ReadString(node, key, out, error, where)) {
        return false;
    }
    if (!ValidId(*out)) {
        if (error != nullptr) {
            *error = where + " 字段 '" + key + "' 不是有效物品 id";
        }
        return false;
    }
    if (items.Find(*out) == nullptr) {
        if (error != nullptr) {
            *error = where + " 引用了未知物品 '" + *out + "'";
        }
        return false;
    }
    return true;
}

bool ReadFloat(const nlohmann::json& node, const char* key, float lo, float hi,
               float default_value, float* out, std::string* error,
               const std::string& where) {
    if (!node.contains(key)) {
        *out = default_value;
        return true;
    }
    if (!node[key].is_number()) {
        if (error != nullptr) {
            *error = where + " 字段 '" + key + "' 必须是数字";
        }
        return false;
    }
    const double value = node[key].get<double>();
    if (!std::isfinite(value) || value < static_cast<double>(lo) ||
        value > static_cast<double>(hi)) {
        if (error != nullptr) {
            *error = where + " 字段 '" + key + "' 超出范围";
        }
        return false;
    }
    *out = static_cast<float>(value);
    return true;
}

bool ReadInt(const nlohmann::json& node, const char* key, int lo, int hi,
             int default_value, int* out, std::string* error,
             const std::string& where) {
    if (!node.contains(key)) {
        *out = default_value;
        return true;
    }
    if (!node[key].is_number_integer()) {
        if (error != nullptr) {
            *error = where + " 字段 '" + key + "' 必须是整数";
        }
        return false;
    }
    const long long value = node[key].get<long long>();
    if (value < lo || value > hi) {
        if (error != nullptr) {
            *error = where + " 字段 '" + key + "' 超出范围";
        }
        return false;
    }
    *out = static_cast<int>(value);
    return true;
}

bool ParseStages(const nlohmann::json& entry, std::vector<CropStageDef>* out,
                 std::string* error, const std::string& where) {
    if (!entry.contains("stages") || !entry["stages"].is_array()) {
        if (error != nullptr) {
            *error = where + " 缺少数组字段 'stages'";
        }
        return false;
    }
    const nlohmann::json& stages = entry["stages"];
    if (stages.size() < 2 || stages.size() > kMaxStages) {
        if (error != nullptr) {
            *error = where + " stages 数量必须为 2~" +
                     std::to_string(kMaxStages);
        }
        return false;
    }
    out->clear();
    out->reserve(stages.size());
    std::unordered_set<std::string> names;
    float previous = -1.0f;
    for (std::size_t i = 0; i < stages.size(); ++i) {
        const std::string at =
            where + ".stages[" + std::to_string(i) + "]";
        if (!stages[i].is_object()) {
            if (error != nullptr) {
                *error = at + " 必须是对象";
            }
            return false;
        }
        const std::unordered_set<std::string> allowed = {"name", "start"};
        if (!CheckKeys(stages[i], allowed, at, error)) {
            return false;
        }
        CropStageDef stage;
        if (!ReadString(stages[i], "name", &stage.name, error, at) ||
            !ReadFloat(stages[i], "start", 0.0f, 1.0f, 0.0f, &stage.start,
                       error, at)) {
            return false;
        }
        if (!(stage.start > previous)) {
            if (error != nullptr) {
                *error = at + ".start 必须严格递增";
            }
            return false;
        }
        if (i == 0 && !(std::fabs(stage.start) <= 1e-5f)) {
            if (error != nullptr) {
                *error = at + ".start 第一阶段必须为 0";
            }
            return false;
        }
        if (i + 1 == stages.size() &&
            !(std::fabs(stage.start - 1.0f) <= 1e-5f)) {
            if (error != nullptr) {
                *error = at + ".start 最后阶段必须为 1";
            }
            return false;
        }
        if (!names.insert(stage.name).second) {
            if (error != nullptr) {
                *error = at + " 阶段名称重复";
            }
            return false;
        }
        previous = stage.start;
        out->push_back(std::move(stage));
    }
    return true;
}

bool ParseCrop(const nlohmann::json& entry, const items::ItemRegistry& items,
               CropDef* out, std::string* error, const std::string& where) {
    const std::unordered_set<std::string> allowed = {
        "id",              "name",          "seedItem",
        "produceItem",     "growMinutes",   "waterMinutes",
        "maxWaterMinutes", "witherMinutes", "waterUsePerMinute",
        "diseaseChance",   "harvestYield",  "seedYield",
        "stages"};
    if (!CheckKeys(entry, allowed, where, error)) {
        return false;
    }
    CropDef crop;
    if (!ReadId(entry, "id", &crop.id, error, where) ||
        !ReadString(entry, "name", &crop.name, error, where) ||
        !ReadItemId(entry, "seedItem", items, &crop.seedItem, error, where) ||
        !ReadItemId(entry, "produceItem", items, &crop.produceItem, error,
                    where)) {
        return false;
    }
    if (crop.seedItem == crop.produceItem) {
        if (error != nullptr) {
            *error = where + " seedItem 与 produceItem 不能相同";
        }
        return false;
    }
    if (!ReadFloat(entry, "growMinutes", 10.0f, 100000.0f, 240.0f,
                   &crop.growMinutes, error, where) ||
        !ReadFloat(entry, "waterMinutes", 1.0f, 100000.0f, 360.0f,
                   &crop.waterMinutes, error, where) ||
        !ReadFloat(entry, "maxWaterMinutes", 1.0f, 100000.0f, 720.0f,
                   &crop.maxWaterMinutes, error, where) ||
        !ReadFloat(entry, "witherMinutes", 1.0f, 100000.0f, 1440.0f,
                   &crop.witherMinutes, error, where) ||
        !ReadFloat(entry, "waterUsePerMinute", 0.1f, 5.0f, 1.0f,
                   &crop.waterUsePerMinute, error, where) ||
        !ReadFloat(entry, "diseaseChance", 0.0f, 1.0f, 0.0f,
                   &crop.diseaseChance, error, where) ||
        !ReadInt(entry, "harvestYield", 1, 20, 2, &crop.harvestYield, error,
                 where) ||
        !ReadInt(entry, "seedYield", 0, 5, 1, &crop.seedYield, error, where)) {
        return false;
    }
    if (crop.maxWaterMinutes < crop.waterMinutes) {
        if (error != nullptr) {
            *error = where + " maxWaterMinutes 不能小于 waterMinutes";
        }
        return false;
    }
    if (!ParseStages(entry, &crop.stages, error, where)) {
        return false;
    }
    *out = std::move(crop);
    return true;
}

bool ReadDoc(std::string_view text, const std::string& source,
             const items::ItemRegistry& items, std::vector<CropDef>* out,
             std::string* error) {
    const auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = source + ": " + message;
        }
        return false;
    };
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(text.begin(), text.end());
    } catch (const std::exception& e) {
        return fail(std::string("JSON 解析失败：") + e.what());
    }
    if (!doc.is_object()) {
        return fail("根节点必须是对象");
    }
    const std::unordered_set<std::string> rootKeys = {"format", "crops"};
    if (!CheckKeys(doc, rootKeys, "根节点", error)) {
        return false;
    }
    if (!doc.contains("format") || !doc["format"].is_string() ||
        doc["format"].get<std::string>() != kFormat) {
        return fail("format 必须是 " + std::string(kFormat));
    }
    if (!doc.contains("crops") || !doc["crops"].is_array() ||
        doc["crops"].empty()) {
        return fail("crops 必须是非空数组");
    }
    if (doc["crops"].size() > kMaxCrops) {
        return fail("crops 条目超过上限 " + std::to_string(kMaxCrops));
    }

    std::vector<CropDef> crops;
    crops.reserve(doc["crops"].size());
    std::unordered_set<std::string> ids;
    std::unordered_set<std::string> seeds;
    for (std::size_t i = 0; i < doc["crops"].size(); ++i) {
        const std::string where = "crops[" + std::to_string(i) + "]";
        if (!doc["crops"][i].is_object()) {
            return fail(where + " 必须是对象");
        }
        CropDef crop;
        if (!ParseCrop(doc["crops"][i], items, &crop, error, where)) {
            return fail(error != nullptr ? *error : where + " 非法");
        }
        if (!ids.insert(crop.id).second) {
            return fail(where + " id 重复: " + crop.id);
        }
        if (!seeds.insert(crop.seedItem).second) {
            return fail(where + " seedItem 重复: " + crop.seedItem);
        }
        crops.push_back(std::move(crop));
    }
    *out = std::move(crops);
    return true;
}

}  // namespace

bool CropRegistry::LoadFile(const std::filesystem::path& path,
                            const items::ItemRegistry& items,
                            std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        if (error != nullptr) {
            *error = "无法打开作物数据文件: " + path.string();
        }
        ZL_ERROR(kLogModule, "无法打开作物数据文件: {}", path.string());
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return LoadFromString(buffer.str(), path.string(), items, error);
}

bool CropRegistry::LoadFromString(std::string_view text,
                                  const std::string& source,
                                  const items::ItemRegistry& items,
                                  std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    std::vector<CropDef> crops;
    if (!ReadDoc(text, source, items, &crops, error)) {
        return false;
    }
    CropRegistry result;
    result.m_crops = std::move(crops);
    for (std::size_t i = 0; i < result.m_crops.size(); ++i) {
        result.m_byId.emplace(result.m_crops[i].id, i);
        result.m_bySeed.emplace(result.m_crops[i].seedItem, i);
    }
    *this = std::move(result);
    return true;
}

bool CropRegistry::MergeFile(const std::filesystem::path& path,
                             const items::ItemRegistry& items,
                             std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        if (error != nullptr) {
            *error = "无法打开作物数据文件: " + path.string();
        }
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return MergeFromString(buffer.str(), path.string(), items, error);
}

bool CropRegistry::MergeFromString(std::string_view text,
                                   const std::string& source,
                                   const items::ItemRegistry& items,
                                   std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    CropRegistry parsed;
    if (!parsed.LoadFromString(text, source, items, error)) {
        return false;
    }
    for (const CropDef& crop : parsed.m_crops) {
        if (m_byId.contains(crop.id)) {
            if (error != nullptr) {
                *error = source + ": 作物 id 冲突: " + crop.id;
            }
            return false;
        }
        if (m_bySeed.contains(crop.seedItem)) {
            if (error != nullptr) {
                *error = source + ": 作物种子冲突: " + crop.seedItem;
            }
            return false;
        }
    }
    for (CropDef& crop : parsed.m_crops) {
        const std::string id = crop.id;
        const std::string seed = crop.seedItem;
        const std::size_t index = m_crops.size();
        m_byId.emplace(id, index);
        m_bySeed.emplace(seed, index);
        m_crops.push_back(std::move(crop));
    }
    return true;
}

const CropDef* CropRegistry::Find(std::string_view id) const {
    const auto it = m_byId.find(std::string(id));
    return it == m_byId.end() ? nullptr : &m_crops[it->second];
}

const CropDef* CropRegistry::FindBySeed(std::string_view seedItem) const {
    const auto it = m_bySeed.find(std::string(seedItem));
    return it == m_bySeed.end() ? nullptr : &m_crops[it->second];
}

std::size_t CropGrowthStageIndex(const CropDef& crop, float growthRatio) {
    if (crop.stages.empty()) {
        return 0;
    }
    const float ratio = std::clamp(growthRatio, 0.0f, 1.0f);
    std::size_t index = 0;
    for (std::size_t i = 0; i < crop.stages.size(); ++i) {
        if (ratio + 1e-5f >= crop.stages[i].start) {
            index = i;
        }
    }
    return index;
}

const char* CropGrowthStageName(const CropDef& crop, float growthRatio) {
    if (crop.stages.empty()) {
        return "";
    }
    return crop.stages[CropGrowthStageIndex(crop, growthRatio)].name.c_str();
}

}  // namespace zl::game
