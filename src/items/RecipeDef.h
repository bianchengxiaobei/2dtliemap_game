#pragma once

#include <string>
#include <vector>

namespace zl::items {

struct RecipeIngredient {
    std::string defId;
    int count = 0;
    float wear = 0.0f;
};

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
};

}  // namespace zl::items
