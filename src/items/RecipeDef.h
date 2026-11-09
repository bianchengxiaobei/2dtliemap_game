#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace zl::items {

struct RecipeIngredient {
    std::string defId;
    int count = 0;
    float wear = 0.0f;
};

// recipes-v4：食谱分类用于 UI 筛选与后续解锁/制作站策略。
enum class RecipeCategory : std::uint8_t {
    General = 0,
    Cooking = 1,
    Preservation = 2,
    Medical = 3,
    Crafting = 4,
};

inline const char* RecipeCategoryToString(RecipeCategory category) {
    switch (category) {
        case RecipeCategory::Cooking:
            return "cooking";
        case RecipeCategory::Preservation:
            return "preservation";
        case RecipeCategory::Medical:
            return "medical";
        case RecipeCategory::Crafting:
            return "crafting";
        case RecipeCategory::General:
            return "general";
    }
    return "general";
}

inline const char* RecipeCategoryLabel(RecipeCategory category) {
    switch (category) {
        case RecipeCategory::Cooking:
            return "烹饪";
        case RecipeCategory::Preservation:
            return "保存";
        case RecipeCategory::Medical:
            return "医疗";
        case RecipeCategory::Crafting:
            return "制作";
        case RecipeCategory::General:
            return "通用";
    }
    return "通用";
}

inline bool ParseRecipeCategory(std::string_view text, RecipeCategory* out) {
    if (out == nullptr) {
        return false;
    }
    if (text == "general") {
        *out = RecipeCategory::General;
        return true;
    }
    if (text == "cooking") {
        *out = RecipeCategory::Cooking;
        return true;
    }
    if (text == "preservation") {
        *out = RecipeCategory::Preservation;
        return true;
    }
    if (text == "medical") {
        *out = RecipeCategory::Medical;
        return true;
    }
    if (text == "crafting") {
        *out = RecipeCategory::Crafting;
        return true;
    }
    return false;
}

struct RecipeDef {
    std::string id;
    std::string name;
    std::string outputDefId;
    int outputCount = 1;
    float craftSeconds = 1.0f;
    // recipes-v3：可选工作台/制作站 id（空 = 任意地点）。世界对象按
    // "object.<station>" 逻辑名提供站点（内置 workbench -> object.workbench）。
    std::string station;
    std::vector<RecipeIngredient> ingredients;
    std::vector<RecipeIngredient> tools;
    // recipes-v4：可选分类；缺省为 General，兼容 v1~v3。
    RecipeCategory category = RecipeCategory::General;
    // 锅具不消耗且不被磨损；燃料每次制作消耗。两者都必须携带。
    std::optional<RecipeIngredient> cookware;
    std::optional<RecipeIngredient> fuel;
};

}  // namespace zl::items
