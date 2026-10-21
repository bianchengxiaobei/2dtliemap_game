#include "items/RecipeRegistry.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "core/Log.h"

namespace zl::items {
namespace {

constexpr const char* kLogModule = "items";
constexpr const char* kFormatV1 = "recipes-v1";
constexpr const char* kFormatV2 = "recipes-v2";
constexpr const char* kFormatV3 = "recipes-v3";
constexpr std::size_t kMaxIdLength = 64;
constexpr std::size_t kMaxStationIdLength = 32;
constexpr int kMaxCount = 9999;
constexpr double kMinCraftSeconds = 0.1;
constexpr double kMaxCraftSeconds = 60.0;
constexpr double kDefaultCraftSeconds = 1.0;
constexpr double kMaxToolWear = 1.0;
constexpr double kDefaultToolWear = 0.1;

bool ParseCount(const nlohmann::json& value, int* out) {
    if (out == nullptr || (!value.is_number_integer() && !value.is_number_unsigned())) {
        return false;
    }
    const double raw = value.get<double>();
    if (!(raw >= 1.0) || raw > static_cast<double>(kMaxCount) ||
        raw != std::floor(raw)) {
        return false;
    }
    *out = static_cast<int>(raw);
    return true;
}

bool ParseItemRef(const nlohmann::json& value, const ItemRegistry& items,
                  std::string* defId, int* count, float* wear, std::string* error,
                  const std::string& where) {
    if (!value.is_object()) {
        *error = where + " 必须是对象";
        return false;
    }
    const bool wearAllowed = wear != nullptr;
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (it.key() != "item" && it.key() != "count" &&
            !(wearAllowed && it.key() == "wear")) {
            *error = where + " 未知字段 '" + it.key() + "'（允许: item/count" +
                     (wearAllowed ? "/wear" : "") + "）";
            return false;
        }
    }
    if (!value.contains("item") || !value["item"].is_string() ||
        value["item"].get<std::string>().empty()) {
        *error = where + " 缺少非空字符串字段 'item'";
        return false;
    }
    if (!value.contains("count")) {
        *error = where + " 缺少整数字段 'count'";
        return false;
    }
    int parsedCount = 0;
    if (!ParseCount(value["count"], &parsedCount)) {
        *error = where + " count 必须是 1~" + std::to_string(kMaxCount) + " 的整数";
        return false;
    }
    float parsedWear = wear != nullptr ? *wear : 0.0f;
    if (wearAllowed && value.contains("wear")) {
        if (!value["wear"].is_number()) {
            *error = where + " 字段 'wear' 必须是数字";
            return false;
        }
        const double rawWear = value["wear"].get<double>();
        if (!std::isfinite(rawWear) || rawWear < 0.0 || rawWear > kMaxToolWear) {
            *error = where + " wear 必须是 0~1 的数字";
            return false;
        }
        parsedWear = static_cast<float>(rawWear);
    }
    const std::string parsedId = value["item"].get<std::string>();
    if (items.Find(parsedId) == nullptr) {
        *error = where + " 引用了未注册物品 '" + parsedId + "'";
        return false;
    }
    if (defId != nullptr) {
        *defId = parsedId;
    }
    if (count != nullptr) {
        *count = parsedCount;
    }
    if (wear != nullptr) {
        *wear = parsedWear;
    }
    return true;
}

}  // namespace

bool RecipeRegistry::LoadFromString(std::string_view text, const std::string& source,
                                    const ItemRegistry& items, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    auto fail = [&](const std::string& message) {
        const std::string full = source + ": " + message;
        if (error != nullptr) {
            *error = full;
        }
        ZL_ERROR(kLogModule, "{}", full);
        return false;
    };

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& e) {
        return fail("JSON 语法错误 — " + std::string(e.what()));
    } catch (const std::exception& e) {
        return fail("JSON 解析失败 — " + std::string(e.what()));
    }

    if (!doc.is_object()) {
        return fail("根节点必须是对象");
    }
    if (!doc.contains("format")) {
        return fail(std::string("缺少 format 字段（期望 \"") + kFormatV3 +
                    "\"，兼容 \"" + kFormatV2 + "\"/\"" + kFormatV1 + "\"）");
    }
    if (!doc["format"].is_string()) {
        return fail("format 必须是字符串");
    }
    const std::string format = doc["format"].get<std::string>();
    if (format != kFormatV1 && format != kFormatV2 && format != kFormatV3) {
        return fail("format 不受支持: \"" + format + "\"（期望 \"" + kFormatV3 +
                    "\"，兼容 \"" + kFormatV2 + "\"/\"" + kFormatV1 + "\"）");
    }
    const bool toolsAllowed = format != kFormatV1;
    const bool timeAllowed = format != kFormatV1;
    const bool stationAllowed = format == kFormatV3;
    if (!doc.contains("recipes")) {
        return fail("缺少 recipes 数组");
    }
    if (!doc["recipes"].is_array()) {
        return fail("recipes 必须是数组");
    }
    if (doc["recipes"].empty()) {
        return fail("recipes 数组为空（至少需要 1 条配方）");
    }

    std::vector<RecipeDef> defs;
    std::unordered_map<std::string, std::size_t> byId;
    const nlohmann::json& recipes = doc["recipes"];
    for (std::size_t i = 0; i < recipes.size(); ++i) {
        const std::string where = "recipes[" + std::to_string(i) + "]";
        const nlohmann::json& entry = recipes[i];
        if (!entry.is_object()) {
            return fail(where + " 必须是对象");
        }
        if (entry.contains("tools") && !toolsAllowed) {
            return fail(where + " recipes-v1 不支持 tools（请升级 format 为 " +
                        kFormatV2 + "）");
        }
        if (entry.contains("time") && !timeAllowed) {
            return fail(where + " recipes-v1 不支持 time（请升级 format 为 " +
                        kFormatV2 + "）");
        }
        if (entry.contains("station") && !stationAllowed) {
            return fail(where + " " + format + " 不支持 station（请升级 format 为 " +
                        kFormatV3 + "）");
        }
        std::unordered_set<std::string> allowedKeys = {
            "id", "name", "output", "ingredients"};
        std::string allowedHint = "id/name/output/ingredients";
        if (toolsAllowed) {
            allowedKeys.insert("tools");
            allowedHint += "/tools";
        }
        if (timeAllowed) {
            allowedKeys.insert("time");
            allowedHint += "/time";
        }
        if (stationAllowed) {
            allowedKeys.insert("station");
            allowedHint += "/station";
        }
        for (auto it = entry.begin(); it != entry.end(); ++it) {
            if (!allowedKeys.contains(it.key())) {
                return fail(where + " 未知字段 '" + it.key() + "'（允许: " +
                            allowedHint + "）");
            }
        }

        RecipeDef def;
        def.craftSeconds = static_cast<float>(kDefaultCraftSeconds);
        if (entry.contains("time")) {
            if (!entry["time"].is_number()) {
                return fail(where + " 字段 'time' 必须是数字（秒）");
            }
            const double seconds = entry["time"].get<double>();
            if (!std::isfinite(seconds) || seconds < kMinCraftSeconds ||
                seconds > kMaxCraftSeconds) {
                return fail(where + " time 必须是 0.1~60 秒");
            }
            def.craftSeconds = static_cast<float>(seconds);
        }
        if (entry.contains("station")) {
            if (!entry["station"].is_string()) {
                return fail(where + " 字段 'station' 必须是字符串");
            }
            def.station = entry["station"].get<std::string>();
            if (def.station.empty() || def.station.size() > kMaxStationIdLength) {
                return fail(where + " station 长度非法（1~32 字符）: \"" +
                            def.station + "\"");
            }
            const bool stationCharsetOk =
                std::all_of(def.station.begin(), def.station.end(), [](char c) {
                    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                           c == '_' || c == '-' || c == '.';
                });
            if (!stationCharsetOk) {
                return fail(where + " station \"" + def.station +
                            "\" 非法：仅允许小写字母/数字/下划线/连字符/点");
            }
        }
        if (!entry.contains("id") || !entry["id"].is_string()) {
            return fail(where + " 缺少字符串字段 'id'");
        }
        def.id = entry["id"].get<std::string>();
        if (def.id.empty() || def.id.size() > kMaxIdLength) {
            return fail(where + " id 长度非法（1~64 字符）: \"" + def.id + "\"");
        }
        const bool idCharsetOk = std::all_of(def.id.begin(), def.id.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
                   c == '.';
        });
        if (!idCharsetOk) {
            return fail(where + " id \"" + def.id +
                        "\" 非法：仅允许小写字母/数字/下划线/点");
        }
        if (byId.contains(def.id)) {
            return fail(where + " 配方 id \"" + def.id + "\" 重复");
        }

        if (!entry.contains("name") || !entry["name"].is_string() ||
            entry["name"].get<std::string>().empty()) {
            return fail(where + " 缺少非空字符串字段 'name'");
        }
        def.name = entry["name"].get<std::string>();

        if (!entry.contains("output")) {
            return fail(where + " 缺少 output 对象");
        }
        std::string outputError;
        if (!ParseItemRef(entry["output"], items, &def.outputDefId,
                          &def.outputCount, nullptr, &outputError,
                          where + ".output")) {
            return fail(outputError);
        }

        if (!entry.contains("ingredients") || !entry["ingredients"].is_array() ||
            entry["ingredients"].empty()) {
            return fail(where + " 缺少非空 ingredients 数组");
        }
        std::unordered_set<std::string> ingredientIds;
        for (std::size_t j = 0; j < entry["ingredients"].size(); ++j) {
            const std::string ingredientWhere =
                where + ".ingredients[" + std::to_string(j) + "]";
            RecipeIngredient ingredient;
            std::string ingredientError;
            if (!ParseItemRef(entry["ingredients"][j], items,
                              &ingredient.defId, &ingredient.count, nullptr,
                              &ingredientError, ingredientWhere)) {
                return fail(ingredientError);
            }
            if (!ingredientIds.insert(ingredient.defId).second) {
                return fail(ingredientWhere + " 重复材料 '" + ingredient.defId + "'");
            }
            def.ingredients.push_back(std::move(ingredient));
        }

        if (entry.contains("tools")) {
            if (!entry["tools"].is_array() || entry["tools"].empty()) {
                return fail(where + " 字段 'tools' 必须是非空数组（无工具请省略）");
            }
            std::unordered_set<std::string> toolIds;
            for (std::size_t j = 0; j < entry["tools"].size(); ++j) {
                const std::string toolWhere =
                    where + ".tools[" + std::to_string(j) + "]";
                RecipeIngredient tool;
                tool.wear = static_cast<float>(kDefaultToolWear);
                std::string toolError;
                if (!ParseItemRef(entry["tools"][j], items, &tool.defId, &tool.count,
                                  &tool.wear, &toolError, toolWhere)) {
                    return fail(toolError);
                }
                if (!toolIds.insert(tool.defId).second) {
                    return fail(toolWhere + " 重复工具 '" + tool.defId + "'");
                }
                if (ingredientIds.contains(tool.defId)) {
                    return fail(toolWhere + " 工具与材料重复 '" + tool.defId + "'");
                }
                const ItemDef* toolDef = items.Find(tool.defId);
                if (toolDef != nullptr && toolDef->maxStack != 1) {
                    return fail(toolWhere + " 工具必须是不可堆叠物品（maxStack=1）: '" +
                                tool.defId + "'");
                }
                def.tools.push_back(std::move(tool));
            }
        }

        byId.emplace(def.id, defs.size());
        defs.push_back(std::move(def));
    }

    for (const ItemDef& item : items.All()) {
        for (const std::string& recipeId : item.unlocksRecipes) {
            if (!byId.contains(recipeId)) {
                return fail("物品 '" + item.id + "' 的 unlocksRecipes 引用未知配方 '" +
                            recipeId + "'");
            }
        }
    }

    m_defs = std::move(defs);
    m_byId = std::move(byId);
    ZL_INFO(kLogModule, "配方注册表已加载: {} 条定义 <- {}", m_defs.size(), source);
    return true;
}

bool RecipeRegistry::LoadFile(const std::filesystem::path& path, const ItemRegistry& items,
                              std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        const std::string message = "配方定义文件不存在: " + path.string();
        if (error != nullptr) {
            *error = message;
        }
        ZL_ERROR(kLogModule, "{}", message);
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return LoadFromString(buffer.str(), path.string(), items, error);
}

const RecipeDef* RecipeRegistry::Find(std::string_view id) const {
    const auto it = m_byId.find(std::string(id));
    if (it == m_byId.end()) {
        return nullptr;
    }
    return &m_defs[it->second];
}

const RecipeDef* RecipeRegistry::At(std::size_t index) const {
    return index < m_defs.size() ? &m_defs[index] : nullptr;
}

}  // namespace zl::items
