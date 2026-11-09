#include "items/Crafting.h"

#include <algorithm>
#include <utility>

namespace zl::items {
namespace {

struct CraftAttempt {
    bool ok = false;
    std::string error;
    ecs::Container result;
    std::vector<std::string> brokenToolDefIds;
};

bool WearOneTool(ecs::Container& container, std::string_view defId, float wear,
                 std::vector<std::string>* brokenToolDefIds) {
    std::size_t selected = container.items.size();
    float selectedCondition = 0.0f;
    for (std::size_t i = 0; i < container.items.size(); ++i) {
        const ecs::ItemInstance& entry = container.items[i];
        if (entry.defId != defId || !(entry.condition > 0.0f)) {
            continue;
        }
        if (selected == container.items.size() || entry.condition < selectedCondition) {
            selected = i;
            selectedCondition = entry.condition;
        }
    }
    if (selected == container.items.size()) {
        return false;
    }
    ecs::ItemInstance& entry = container.items[selected];
    entry.condition = std::max(0.0f, entry.condition - std::clamp(wear, 0.0f, 1.0f));
    if (entry.condition <= 1e-4f) {
        if (brokenToolDefIds != nullptr) {
            brokenToolDefIds->push_back(entry.defId);
        }
        container.items.erase(container.items.begin() +
                              static_cast<std::ptrdiff_t>(selected));
    }
    return true;
}

CraftAttempt TryCraft(const ecs::Container& container, const ItemRegistry& registry,
                      const RecipeDef& recipe) {
    CraftAttempt attempt;
    attempt.result = container;

    if (recipe.ingredients.empty()) {
        attempt.error = "配方没有材料";
        return attempt;
    }
    for (const RecipeIngredient& ingredient : recipe.ingredients) {
        if (registry.Find(ingredient.defId) == nullptr) {
            attempt.error = "配方材料未注册：" + ingredient.defId;
            return attempt;
        }
    }
    for (const RecipeIngredient& tool : recipe.tools) {
        if (tool.count <= 0 || registry.Find(tool.defId) == nullptr) {
            attempt.error = "配方工具无效：" + tool.defId;
            return attempt;
        }
    }
    if (recipe.cookware.has_value() &&
        (recipe.cookware->count <= 0 ||
         registry.Find(recipe.cookware->defId) == nullptr)) {
        attempt.error = "配方锅具无效：" + recipe.cookware->defId;
        return attempt;
    }
    if (recipe.fuel.has_value() &&
        (recipe.fuel->count <= 0 ||
         registry.Find(recipe.fuel->defId) == nullptr)) {
        attempt.error = "配方燃料无效：" + recipe.fuel->defId;
        return attempt;
    }

    const ItemDef* output = registry.Find(recipe.outputDefId);
    if (output == nullptr || recipe.outputCount <= 0) {
        attempt.error = "配方输出无效：" + recipe.outputDefId;
        return attempt;
    }

    for (const RecipeIngredient& tool : recipe.tools) {
        const int have = CountUsableTools(attempt.result, tool.defId);
        if (have < tool.count) {
            const ItemDef* def = registry.Find(tool.defId);
            const std::string label = def != nullptr ? def->name : tool.defId;
            attempt.error = "缺少工具：" + label + " 需要 " +
                            std::to_string(tool.count) + "，现有 " +
                            std::to_string(have);
            return attempt;
        }
    }

    if (recipe.cookware.has_value()) {
        const int have =
            CountUsableTools(attempt.result, recipe.cookware->defId);
        if (have < recipe.cookware->count) {
            const ItemDef* def = registry.Find(recipe.cookware->defId);
            const std::string label =
                def != nullptr ? def->name : recipe.cookware->defId;
            attempt.error = "缺少锅具：" + label + " 需要 " +
                            std::to_string(recipe.cookware->count) + "，现有 " +
                            std::to_string(have);
            return attempt;
        }
    }

    if (recipe.fuel.has_value()) {
        const int have = CountOf(attempt.result, recipe.fuel->defId);
        if (have < recipe.fuel->count) {
            const ItemDef* def =
                registry.Find(recipe.fuel->defId);
            const std::string label =
                def != nullptr ? def->name : recipe.fuel->defId;
            attempt.error = "缺少燃料：" + label + " 需要 " +
                            std::to_string(recipe.fuel->count) + "，现有 " +
                            std::to_string(have);
            return attempt;
        }
    }

    for (const RecipeIngredient& ingredient : recipe.ingredients) {
        const int have = CountOf(attempt.result, ingredient.defId);
        if (have < ingredient.count) {
            const ItemDef* def = registry.Find(ingredient.defId);
            const std::string label = def != nullptr ? def->name : ingredient.defId;
            attempt.error = "材料不足：" + label + " 需要 " +
                            std::to_string(ingredient.count) + "，现有 " +
                            std::to_string(have);
            return attempt;
        }
    }

    for (const RecipeIngredient& ingredient : recipe.ingredients) {
        if (RemoveItem(attempt.result, ingredient.defId, ingredient.count) !=
            ingredient.count) {
            attempt.error = "材料扣除失败：" + ingredient.defId;
            return attempt;
        }
    }

    if (recipe.fuel.has_value() &&
        RemoveItem(attempt.result, recipe.fuel->defId,
                   recipe.fuel->count) != recipe.fuel->count) {
        attempt.error = "燃料扣除失败：" + recipe.fuel->defId;
        return attempt;
    }

    for (const RecipeIngredient& tool : recipe.tools) {
        for (int used = 0; used < tool.count; ++used) {
            if (!WearOneTool(attempt.result, tool.defId, tool.wear,
                             &attempt.brokenToolDefIds)) {
                attempt.error = "工具磨损失败：" + tool.defId;
                return attempt;
            }
        }
    }

    const int added = AddItem(attempt.result, registry,
                              ecs::ItemInstance{recipe.outputDefId, recipe.outputCount,
                                                1.0f});
    if (added != recipe.outputCount) {
        attempt.error = "背包空间不足：无法放入 " + output->name + " ×" +
                        std::to_string(recipe.outputCount);
        return attempt;
    }

    attempt.ok = true;
    return attempt;
}

}  // namespace

float ToolEfficiency(const ecs::Container& container, const RecipeDef& recipe) {
    if (recipe.tools.empty() && !recipe.cookware.has_value()) {
        return 1.0f;
    }
    float lowestCondition = 1.0f;
    auto account = [&](const RecipeIngredient& requirement) {
        if (requirement.count <= 0) {
            return false;
        }
        int found = 0;
        float toolLowest = 1.0f;
        for (const ecs::ItemInstance& entry : container.items) {
            if (entry.defId != requirement.defId ||
                !(entry.condition > 0.0f)) {
                continue;
            }
            toolLowest = std::min(toolLowest, entry.condition);
            if (++found >= requirement.count) {
                break;
            }
        }
        if (found < requirement.count) {
            return false;
        }
        lowestCondition = std::min(lowestCondition, toolLowest);
        return true;
    };
    for (const RecipeIngredient& tool : recipe.tools) {
        if (!account(tool)) {
            return 0.0f;
        }
    }
    if (recipe.cookware.has_value() && !account(*recipe.cookware)) {
        return 0.0f;
    }
    return 0.5f + 0.5f * std::clamp(lowestCondition, 0.0f, 1.0f);
}

float EffectiveCraftSeconds(const RecipeDef& recipe, float efficiency) {
    if (!(efficiency > 0.0f)) {
        return 0.0f;
    }
    return recipe.craftSeconds / std::clamp(efficiency, 0.5f, 1.0f);
}

bool CanCraft(const ecs::Container& container, const ItemRegistry& registry,
              const RecipeDef& recipe, std::string* reason) {
    CraftAttempt attempt = TryCraft(container, registry, recipe);
    if (reason != nullptr) {
        *reason = attempt.ok ? std::string() : attempt.error;
    }
    return attempt.ok;
}

CraftResult Craft(ecs::Container& container, const ItemRegistry& registry,
                  const RecipeDef& recipe) {
    CraftAttempt attempt = TryCraft(container, registry, recipe);
    CraftResult result;
    if (!attempt.ok) {
        result.error = std::move(attempt.error);
        return result;
    }
    container = std::move(attempt.result);
    result.ok = true;
    result.outputDefId = recipe.outputDefId;
    result.outputCount = recipe.outputCount;
    result.brokenToolDefIds = std::move(attempt.brokenToolDefIds);
    return result;
}

int MaxCraftCount(const ecs::Container& container, const ItemRegistry& registry,
                  const RecipeDef& recipe, int limit) {
    if (limit <= 0) {
        return 0;
    }
    ecs::Container probe = container;
    int count = 0;
    while (count < limit) {
        const CraftResult result = Craft(probe, registry, recipe);
        if (!result.ok) {
            break;
        }
        ++count;
    }
    return count;
}

void CraftingProcess::Start(std::string id, float seconds, float efficiency) {
    active = true;
    recipeId = std::move(id);
    duration = std::max(seconds, 0.01f);
    elapsed = 0.0f;
    toolEfficiency = std::clamp(efficiency, 0.0f, 1.0f);
}

void CraftingProcess::Cancel() {
    active = false;
    recipeId.clear();
    duration = 0.0f;
    elapsed = 0.0f;
    toolEfficiency = 1.0f;
}

bool CraftingProcess::Advance(float dt) {
    if (!active) {
        return false;
    }
    elapsed = std::min(duration, elapsed + std::max(dt, 0.0f));
    if (elapsed + 1e-4f < duration) {
        return false;
    }
    active = false;
    return true;
}

float CraftingProcess::Progress() const {
    if (!active || duration <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(elapsed / duration, 0.0f, 1.0f);
}

}  // namespace zl::items
