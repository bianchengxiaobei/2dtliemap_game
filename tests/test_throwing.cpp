#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "game/Throwing.h"
#include "items/InventoryOps.h"
#include "items/ItemDef.h"
#include "items/ItemRegistry.h"
#include "items/LootTable.h"
#include "items/RecipeRegistry.h"

using namespace zl;

namespace {

std::string MakeJson(const std::string& body) {
    return std::string("{\n  \"format\": \"items-v1\",\n  \"items\": [\n") +
           body + "\n  ]\n}\n";
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string ReplaceOnce(std::string text, const std::string& from,
                        const std::string& to) {
    const auto pos = text.find(from);
    if (pos != std::string::npos) {
        text.replace(pos, from.size(), to);
    }
    return text;
}

std::string ItemWithThrowable(const std::string& throwableValue) {
    return std::string(
               R"({ "id": "x", "name": "投掷物", "icon": "item.repair_kit", "weight": 0.5, "maxStack": 5, "type": "generic", "throwable": )") +
           throwableValue + " }";
}

const char* kValidThrowable =
    R"({ "range": 8.0, "damage": 8.0, "impactRadius": 0.6, "arcHeight": 1.0, "noiseRadius": 6.0, "returnable": true, "breakChance": 0.1 })";

}  // namespace

TEST_CASE("M34-F2-T1 投掷数据合法、缺省与 mod 追加") {
    items::ItemRegistry registry;
    std::string error;
    const std::string base = MakeJson(
        R"(    { "id": "plain", "name": "旧物品", "icon": "item.rag", "weight": 0.1, "maxStack": 5, "type": "generic" },
    { "id": "rock", "name": "石块", "icon": "item.repair_kit", "weight": 0.5, "maxStack": 5, "type": "generic", "throwable": { "range": 7.5, "damage": 8.0, "impactRadius": 0.6, "arcHeight": 1.0, "noiseRadius": 6.0, "returnable": true, "breakChance": 0.05 } },
    { "id": "molotov", "name": "燃烧瓶", "icon": "item.water_bottle", "weight": 0.8, "maxStack": 1, "type": "consumable", "throwable": { "range": 8.0, "damage": 6.0, "impactRadius": 1.2, "arcHeight": 1.1, "noiseRadius": 5.0, "returnable": false, "breakChance": 1.0, "fire": { "radius": 1.5, "minutes": 45.0 } } })");
    REQUIRE(registry.LoadFromString(base, "throwing-base", &error));

    const items::ItemDef* plain = registry.Find("plain");
    const items::ItemDef* rock = registry.Find("rock");
    const items::ItemDef* molotov = registry.Find("molotov");
    REQUIRE(plain != nullptr);
    REQUIRE(rock != nullptr);
    REQUIRE(molotov != nullptr);
    CHECK_FALSE(plain->throwable.has_value());

    REQUIRE(rock->throwable.has_value());
    CHECK(rock->throwable->range == doctest::Approx(7.5f));
    CHECK(rock->throwable->damage == doctest::Approx(8.0f));
    CHECK(rock->throwable->impactRadius == doctest::Approx(0.6f));
    CHECK(rock->throwable->arcHeight == doctest::Approx(1.0f));
    CHECK(rock->throwable->noiseRadius == doctest::Approx(6.0f));
    CHECK(rock->throwable->returnable);
    CHECK(rock->throwable->breakChance == doctest::Approx(0.05f));
    CHECK_FALSE(rock->throwable->fire.has_value());

    REQUIRE(molotov->throwable.has_value());
    REQUIRE(molotov->throwable->fire.has_value());
    CHECK_FALSE(molotov->throwable->returnable);
    CHECK(molotov->throwable->breakChance == doctest::Approx(1.0f));
    CHECK(molotov->throwable->fire->radius == doctest::Approx(1.5f));
    CHECK(molotov->throwable->fire->minutes == doctest::Approx(45.0f));

    const std::string mod = R"({
      "format": "items-v1",
      "items": [
        { "id": "mod_throw", "name": "模组投掷物", "icon": "item.rag", "weight": 0.2, "maxStack": 1, "type": "generic", "throwable": { "range": 5.0, "damage": 3.0, "impactRadius": 0.3, "arcHeight": 0.8, "noiseRadius": 2.0, "returnable": false, "breakChance": 0.2 } }
      ]
    })";
    std::size_t added = 0;
    REQUIRE(registry.MergeFromString(mod, "throwing-mod", &added, &error));
    CHECK(added == 1);
    const items::ItemDef* modThrow = registry.Find("mod_throw");
    REQUIRE(modThrow != nullptr);
    REQUIRE(modThrow->throwable.has_value());
    CHECK(modThrow->throwable->range == doctest::Approx(5.0f));
    CHECK(modThrow->throwable->damage == doctest::Approx(3.0f));
    CHECK_FALSE(modThrow->throwable->returnable);
}

TEST_CASE("M34-F2-T1 投掷字段越界、缺省与未知字段被拒绝") {
    items::ItemRegistry registry;
    std::string error;
    auto load = [&](const std::string& throwableValue) {
        error.clear();
        return registry.LoadFromString(
            MakeJson("    " + ItemWithThrowable(throwableValue)), "throwing",
            &error);
    };
    const std::string base = kValidThrowable;
    auto reject = [&](const std::string& value, const std::string& expected) {
        CHECK_FALSE(load(value));
        CHECK(Contains(error, expected));
        CHECK(Contains(error, "第 4 行"));
    };

    REQUIRE(load(base));
    CHECK(registry.Size() == 1);
    reject("7", "必须是对象");
    reject(ReplaceOnce(base, "\"range\": 8.0, ", ""), "缺少字段");
    reject(ReplaceOnce(base, "\"range\": 8.0", "\"range\": 0.5"),
           "range 取值非法");
    reject(ReplaceOnce(base, "\"range\": 8.0", "\"range\": 17.0"),
           "range 取值非法");
    reject(ReplaceOnce(base, "\"damage\": 8.0", "\"damage\": 81.0"),
           "damage 取值非法");
    reject(ReplaceOnce(base, "\"impactRadius\": 0.6", "\"impactRadius\": 3.1"),
           "impactRadius 取值非法");
    reject(ReplaceOnce(base, "\"arcHeight\": 1.0", "\"arcHeight\": 4.0"),
           "arcHeight 取值非法");
    reject(ReplaceOnce(base, "\"noiseRadius\": 6.0", "\"noiseRadius\": 33.0"),
           "noiseRadius 取值非法");
    reject(ReplaceOnce(base, "\"breakChance\": 0.1", "\"breakChance\": 1.1"),
           "breakChance 取值非法");
    reject(ReplaceOnce(base, "\"returnable\": true", "\"returnable\": 1"),
           "必须是布尔值");
    reject(ReplaceOnce(base, "\"returnable\": true, ", ""), "缺少字段");
    reject(ReplaceOnce(base, "\"breakChance\": 0.1",
                       "\"breakChance\": 0.1, \"unknown\": 1"),
           "未知字段");

    const std::string fireBase =
        R"({ "range": 8.0, "damage": 8.0, "impactRadius": 0.6, "arcHeight": 1.0, "noiseRadius": 6.0, "returnable": false, "breakChance": 1.0, "fire": { "radius": 1.0, "minutes": 10.0 } })";
    REQUIRE(load(fireBase));
    const items::ItemDef* fireItem = registry.Find("x");
    REQUIRE(fireItem != nullptr);
    REQUIRE(fireItem->throwable.has_value());
    REQUIRE(fireItem->throwable->fire.has_value());
    CHECK(fireItem->throwable->fire->radius == doctest::Approx(1.0f));
    reject(ReplaceOnce(fireBase,
                       "\"fire\": { \"radius\": 1.0, \"minutes\": 10.0 }",
                       "\"fire\": 3"),
           "必须是对象");
    reject(ReplaceOnce(fireBase, "\"radius\": 1.0", "\"radius\": 4.0"),
           "radius 取值非法");
    reject(ReplaceOnce(fireBase, "\"minutes\": 10.0", "\"minutes\": 241.0"),
           "minutes 取值非法");
    reject(ReplaceOnce(fireBase, "\"minutes\": 10.0",
                       "\"minutes\": 10.0, \"spread\": 1"),
           "未知字段");

    CHECK(registry.Size() == 1);
}

TEST_CASE("M34-F2-T1 内置投掷物与 craft_molotov 配方") {
    const std::string root = std::string(ZL_SOURCE_DIR);
    items::ItemRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(registry.LoadFile(root + "/assets/items.json", &error),
                    error);
    CHECK(registry.Size() == 99);

    const items::ItemDef* stone = registry.Find("forage_stone");
    const items::ItemDef* spear = registry.Find("makeshift_spear");
    const items::ItemDef* boneSpear = registry.Find("bone_spear");
    const items::ItemDef* molotov = registry.Find("molotov");
    const items::ItemDef* rag = registry.Find("rag");
    REQUIRE(stone != nullptr);
    REQUIRE(spear != nullptr);
    REQUIRE(boneSpear != nullptr);
    REQUIRE(molotov != nullptr);
    REQUIRE(rag != nullptr);
    CHECK_FALSE(rag->throwable.has_value());

    REQUIRE(stone->throwable.has_value());
    REQUIRE(spear->throwable.has_value());
    REQUIRE(boneSpear->throwable.has_value());
    REQUIRE(molotov->throwable.has_value());
    CHECK(stone->throwable->returnable);
    CHECK(spear->throwable->returnable);
    CHECK(boneSpear->throwable->returnable);
    CHECK_FALSE(molotov->throwable->returnable);
    CHECK(molotov->throwable->damage == doctest::Approx(6.0f));
    CHECK(molotov->throwable->impactRadius == doctest::Approx(1.2f));
    REQUIRE(molotov->throwable->fire.has_value());
    CHECK(molotov->throwable->fire->radius == doctest::Approx(1.5f));
    CHECK(molotov->throwable->fire->minutes == doctest::Approx(45.0f));

    items::RecipeRegistry recipes;
    REQUIRE_MESSAGE(
        recipes.LoadFile(root + "/assets/recipes.json", registry, &error),
        error);
    const items::RecipeDef* recipe = recipes.Find("craft_molotov");
    REQUIRE(recipe != nullptr);
    CHECK(recipe->outputDefId == "molotov");
    CHECK(recipe->outputCount == 1);
    CHECK(recipe->station == "workbench");
    CHECK(recipe->craftSeconds == doctest::Approx(1.5f));
    REQUIRE(recipe->ingredients.size() == 3);
    for (const items::RecipeIngredient& ingredient : recipe->ingredients) {
        CHECK(registry.Find(ingredient.defId) != nullptr);
    }
    const auto hasIngredient = [&](const std::string& defId, int count) {
        for (const items::RecipeIngredient& ingredient : recipe->ingredients) {
            if (ingredient.defId == defId && ingredient.count == count) {
                return true;
            }
        }
        return false;
    };
    CHECK(hasIngredient("empty_bottle", 1));
    CHECK(hasIngredient("rag", 1));
    CHECK(hasIngredient("fuel_can", 1));

    const std::string badRecipe = R"({
      "format": "recipes-v4",
      "recipes": [
        {
          "id": "bad_molotov",
          "name": "坏燃烧瓶",
          "category": "crafting",
          "output": { "item": "molotov", "count": 1 },
          "ingredients": [ { "item": "missing_item", "count": 1 } ],
          "station": "workbench",
          "time": 1.5
        }
      ]
    })";
    items::RecipeRegistry badRecipes;
    error.clear();
    CHECK_FALSE(badRecipes.LoadFromString(badRecipe, "bad-throwing-recipe",
                                          registry, &error));
    CHECK(Contains(error, "missing_item"));

    ecs::Container bag;
    ecs::ItemInstance molotovInstance;
    molotovInstance.defId = "molotov";
    CHECK(items::AddItem(bag, registry, molotovInstance) == 1);
    CHECK(items::CountOf(bag, "molotov") == 1);
}


TEST_CASE("M34-F2-T6 投掷物数值、用途与平衡") {
    const std::string root = std::string(ZL_SOURCE_DIR);
    items::ItemRegistry items;
    items::RecipeRegistry recipes;
    items::LootTableRegistry loot;
    std::string error;
    REQUIRE_MESSAGE(items.LoadFile(root + "/assets/items.json", &error),
                    error);
    REQUIRE_MESSAGE(
        recipes.LoadFile(root + "/assets/recipes.json", items, &error),
        error);
    REQUIRE_MESSAGE(loot.LoadFile(root + "/assets/loot.json", items, &error),
                    error);

    const items::ItemDef* stone = items.Find("forage_stone");
    const items::ItemDef* spear = items.Find("makeshift_spear");
    const items::ItemDef* boneSpear = items.Find("bone_spear");
    const items::ItemDef* molotov = items.Find("molotov");
    const items::ItemDef* pistol = items.Find("pistol");
    REQUIRE(items.Find("empty_bottle") != nullptr);
    REQUIRE(items.Find("rag") != nullptr);
    REQUIRE(items.Find("fuel_can") != nullptr);
    REQUIRE(stone != nullptr);
    REQUIRE(spear != nullptr);
    REQUIRE(boneSpear != nullptr);
    REQUIRE(molotov != nullptr);
    REQUIRE(pistol != nullptr);
    REQUIRE(stone->throwable.has_value());
    REQUIRE(spear->throwable.has_value());
    REQUIRE(boneSpear->throwable.has_value());
    REQUIRE(molotov->throwable.has_value());
    REQUIRE(pistol->ranged.has_value());
    const items::ThrowableDef& rock = *stone->throwable;
    const items::ThrowableDef& stick = *spear->throwable;
    const items::ThrowableDef& bone = *boneSpear->throwable;
    const items::ThrowableDef& fire = *molotov->throwable;

    CHECK(rock.range == doctest::Approx(7.5f));
    CHECK(rock.damage == doctest::Approx(8.0f));
    CHECK(rock.impactRadius == doctest::Approx(0.6f));
    CHECK(rock.arcHeight == doctest::Approx(1.0f));
    CHECK(rock.noiseRadius == doctest::Approx(6.0f));
    CHECK(rock.returnable);
    CHECK(rock.breakChance == doctest::Approx(0.05f));
    CHECK(stick.range == doctest::Approx(9.0f));
    CHECK(stick.damage == doctest::Approx(28.0f));
    CHECK(stick.impactRadius == doctest::Approx(0.45f));
    CHECK(stick.arcHeight == doctest::Approx(1.2f));
    CHECK(stick.noiseRadius == doctest::Approx(5.0f));
    CHECK(stick.returnable);
    CHECK(stick.breakChance == doctest::Approx(0.35f));
    CHECK(bone.range == doctest::Approx(8.5f));
    CHECK(bone.damage == doctest::Approx(24.0f));
    CHECK(bone.impactRadius == doctest::Approx(0.45f));
    CHECK(bone.arcHeight == doctest::Approx(1.1f));
    CHECK(bone.noiseRadius == doctest::Approx(4.5f));
    CHECK(bone.breakChance == doctest::Approx(0.45f));
    CHECK(fire.range == doctest::Approx(8.0f));
    CHECK(fire.damage == doctest::Approx(6.0f));
    CHECK(fire.impactRadius == doctest::Approx(1.2f));
    CHECK(fire.arcHeight == doctest::Approx(1.1f));
    CHECK(fire.noiseRadius == doctest::Approx(5.0f));
    CHECK_FALSE(fire.returnable);
    CHECK(fire.breakChance == doctest::Approx(1.0f));
    REQUIRE(fire.fire.has_value());
    CHECK(fire.fire->radius == doctest::Approx(1.5f));
    CHECK(fire.fire->minutes == doctest::Approx(45.0f));

    CHECK(game::ThrowableHotbarTag(rock) == "投7.5 回95%");
    CHECK(game::ThrowableHotbarTag(stick) == "投9 回65%");
    CHECK(game::ThrowableHotbarTag(bone) == "投8.5 回55%");
    CHECK(game::ThrowableHotbarTag(fire) == "投8 火1.5");
    CHECK(game::ThrowableInventoryTag(rock) == "可投7.5 回95%");
    CHECK(game::ThrowableInventoryTag(stick) == "可投9 回65%");
    CHECK(game::ThrowableInventoryTag(bone) == "可投8.5 回55%");
    CHECK(game::ThrowableInventoryTag(fire) == "可投8 火1.5");

    // 石头是安静、低伤害、几乎不损失的应急投掷，不替代枪械。
    const float stoneDps = rock.damage / 0.7f;
    const float pistolDps = pistol->ranged->damage /
                            std::max(0.01f, pistol->ranged->cooldownSeconds);
    CHECK(rock.damage < pistol->ranged->damage);
    CHECK(rock.range < pistol->ranged->range);
    CHECK(rock.noiseRadius < pistol->ranged->noiseRadius);
    CHECK(stoneDps < pistolDps * 0.5f);
    // 长矛是高风险高单体伤害，命中后仍有明显损失风险。
    CHECK(stick.damage > rock.damage * 3.0f);
    CHECK(stick.range > rock.range);
    CHECK(stick.breakChance >= 0.30f);
    CHECK(bone.breakChance >= 0.40f);
    CHECK((1.0f - stick.breakChance) < 0.70f);
    // 燃烧瓶是低直接伤害、高成本的一次性区域火源。
    CHECK(fire.damage < rock.damage);
    CHECK_FALSE(fire.returnable);
    REQUIRE(fire.fire.has_value());
    CHECK(fire.fire->radius > rock.impactRadius * 2.0f);
    const float stoneWeight = stone->weight;
    const float molotovCost = items.Find("empty_bottle")->weight +
                              items.Find("rag")->weight +
                              items.Find("fuel_can")->weight;
    CHECK(molotovCost > stoneWeight * 2.0f);

    const items::RecipeDef* spearRecipe = recipes.Find("makeshift_spear");
    const items::RecipeDef* boneRecipe = recipes.Find("craft_bone_spear");
    const items::RecipeDef* molotovRecipe = recipes.Find("craft_molotov");
    REQUIRE(spearRecipe != nullptr);
    REQUIRE(boneRecipe != nullptr);
    REQUIRE(molotovRecipe != nullptr);
    CHECK(molotovRecipe->ingredients.size() >= 3);
    const auto hasIngredient = [](const items::RecipeDef& recipe,
                                  const std::string& defId, int count) {
        return std::any_of(recipe.ingredients.begin(), recipe.ingredients.end(),
                           [&](const items::RecipeIngredient& ingredient) {
                               return ingredient.defId == defId &&
                                      ingredient.count == count;
                           });
    };
    CHECK(hasIngredient(*molotovRecipe, "empty_bottle", 1));
    CHECK(hasIngredient(*molotovRecipe, "rag", 1));
    CHECK(hasIngredient(*molotovRecipe, "fuel_can", 1));

    const auto hasLootSource = [&](const std::string& defId) {
        return std::any_of(loot.All().begin(), loot.All().end(),
                           [&](const items::LootTable& table) {
                               const auto matches = [&](const items::LootEntry& entry) {
                                   return entry.defId == defId;
                               };
                               return std::any_of(table.guaranteed.begin(),
                                                  table.guaranteed.end(), matches) ||
                                      std::any_of(table.entries.begin(),
                                                  table.entries.end(), matches);
                           });
    };
    CHECK(hasLootSource("empty_bottle"));
    CHECK(hasLootSource("rag"));
    CHECK(hasLootSource("fuel_can"));
    CHECK(hasLootSource("molotov"));
    CHECK(hasLootSource("forage_stone"));
    const items::LootTable* gas = loot.Find("poi_gas_station");
    REQUIRE(gas != nullptr);
    const auto molotovEntry = std::find_if(
        gas->entries.begin(), gas->entries.end(),
        [](const items::LootEntry& entry) { return entry.defId == "molotov"; });
    REQUIRE(molotovEntry != gas->entries.end());
    CHECK(molotovEntry->weight == 1);
}
