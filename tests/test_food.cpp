#include <string>

#include <doctest/doctest.h>

#include "game/FoodSafety.h"
#include "items/Crafting.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"
#include "items/RecipeRegistry.h"
#include "items/Spoilage.h"

namespace {

zl::items::ItemRegistry MakeRegistry() {
    static const char* kItems = R"json({
  "format": "items-v1",
  "items": [
    { "id": "empty_bottle", "name": "空瓶", "icon": "i", "weight": 0.1,
      "maxStack": 1, "type": "generic" },
    { "id": "water_bottle", "name": "净水", "icon": "i", "weight": 0.5,
      "maxStack": 1, "type": "consumable",
      "use": { "thirst": 0.4, "replaceWith": "empty_bottle" } },
    { "id": "dirty_water", "name": "脏水", "icon": "i", "weight": 0.5,
      "maxStack": 1, "type": "consumable",
      "use": { "thirst": 0.25, "healthPenalty": 4,
               "replaceWith": "empty_bottle" } },
    { "id": "cooked_beans", "name": "熟豆", "icon": "i", "weight": 0.6,
      "maxStack": 3, "type": "consumable", "use": { "hunger": 0.5 },
      "spoilDays": 1.0 }
  ]
})json";
    zl::items::ItemRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(registry.LoadFromString(kItems, "test_food_items", &error),
                    error);
    return registry;
}

zl::items::ItemRegistry MakeRecipeItems() {
    static const char* kItems = R"json({
  "format": "items-v1",
  "items": [
    { "id": "empty_bottle", "name": "空瓶", "icon": "i", "weight": 0.1,
      "maxStack": 1, "type": "generic" },
    { "id": "water_bottle", "name": "净水", "icon": "i", "weight": 0.5,
      "maxStack": 1, "type": "consumable" },
    { "id": "dirty_water", "name": "脏水", "icon": "i", "weight": 0.5,
      "maxStack": 1, "type": "consumable" }
  ]
})json";
    zl::items::ItemRegistry registry;
    std::string error;
    REQUIRE_MESSAGE(registry.LoadFromString(kItems, "test_food_recipes", &error),
                    error);
    return registry;
}

}  // namespace

TEST_CASE("M18-F2 parses liquid and spoil fields") {
    const zl::items::ItemRegistry registry = MakeRegistry();
    const zl::items::ItemDef* water = registry.Find("water_bottle");
    REQUIRE(water != nullptr);
    CHECK(water->maxStack == 1);
    CHECK(water->use.replaceWith == "empty_bottle");
    CHECK(water->use.thirst == doctest::Approx(0.4f));

    const zl::items::ItemDef* dirty = registry.Find("dirty_water");
    REQUIRE(dirty != nullptr);
    CHECK(dirty->use.healthPenalty == doctest::Approx(4.0f));
    CHECK(dirty->use.replaceWith == "empty_bottle");

    const zl::items::ItemDef* beans = registry.Find("cooked_beans");
    REQUIRE(beans != nullptr);
    CHECK(beans->spoilDays == doctest::Approx(1.0f));
}

TEST_CASE("M18-F2 bottle use returns an empty bottle") {
    const zl::items::ItemRegistry registry = MakeRegistry();
    zl::ecs::Container bag;
    bag.capacityKg = 10.0f;
    REQUIRE(zl::items::AddItem(
                bag, registry, zl::ecs::ItemInstance{"water_bottle", 1, 1.0f}) ==
            1);
    const zl::items::UseResult use = zl::items::UseAt(bag, registry, 0);
    CHECK(use.applied);
    CHECK(use.defId == "water_bottle");
    CHECK(use.replacementDefId == "empty_bottle");
    CHECK(use.remaining == 1);
    REQUIRE(bag.items.size() == 1u);
    CHECK(bag.items[0].defId == "empty_bottle");
    CHECK(bag.items[0].count == 1);

    zl::ecs::Container dirtyBag;
    dirtyBag.capacityKg = 10.0f;
    REQUIRE(zl::items::AddItem(
                dirtyBag, registry,
                zl::ecs::ItemInstance{"dirty_water", 1, 1.0f}) == 1);
    const zl::items::UseResult dirty = zl::items::UseAt(dirtyBag, registry, 0);
    CHECK(dirty.applied);
    CHECK(dirty.effect.healthPenalty == doctest::Approx(4.0f));
    REQUIRE(dirtyBag.items.size() == 1u);
    CHECK(dirtyBag.items[0].defId == "empty_bottle");
}

TEST_CASE("M18-F2 spoilage advances with game time and marks spoiled food") {
    const zl::items::ItemRegistry registry = MakeRegistry();
    zl::ecs::Container bag;
    bag.capacityKg = 10.0f;
    REQUIRE(zl::items::AddItem(
                bag, registry, zl::ecs::ItemInstance{"cooked_beans", 1, 1.0f}) ==
            1);
    const zl::items::ItemDef* beans = registry.Find("cooked_beans");
    REQUIRE(beans != nullptr);

    zl::items::AdvanceSpoilage(bag, registry, 720.0f);
    CHECK(bag.items[0].condition == doctest::Approx(0.5f));
    CHECK_FALSE(zl::items::IsSpoiled(bag.items[0], *beans));

    zl::items::AdvanceSpoilage(bag, registry, 720.0f);
    CHECK(bag.items[0].condition == doctest::Approx(0.0f));
    CHECK(zl::items::IsSpoiled(bag.items[0], *beans));

    const zl::items::UseResult use = zl::items::UseAt(bag, registry, 0);
    CHECK(use.applied);
    CHECK(use.spoiled);
    CHECK(bag.items.empty());
}

TEST_CASE("M18-F2 water recipes chain through dirty and clean water") {
    const zl::items::ItemRegistry items = MakeRecipeItems();
    static const char* kRecipes = R"json({
  "format": "recipes-v3",
  "recipes": [
    {
      "id": "fill_bottle",
      "name": "装水",
      "output": { "item": "dirty_water", "count": 1 },
      "ingredients": [ { "item": "empty_bottle", "count": 1 } ],
      "station": "locker",
      "time": 0.8
    },
    {
      "id": "boil_water",
      "name": "烧水",
      "output": { "item": "water_bottle", "count": 1 },
      "ingredients": [ { "item": "dirty_water", "count": 1 } ],
      "station": "campfire",
      "time": 1.5
    }
  ]
})json";
    zl::items::RecipeRegistry recipes;
    std::string error;
    REQUIRE_MESSAGE(recipes.LoadFromString(kRecipes, "test_food_recipes", items,
                                          &error),
                    error);

    zl::ecs::Container bag;
    bag.capacityKg = 10.0f;
    REQUIRE(zl::items::AddItem(
                bag, items, zl::ecs::ItemInstance{"empty_bottle", 1, 1.0f}) == 1);
    const zl::items::RecipeDef* fill = recipes.Find("fill_bottle");
    REQUIRE(fill != nullptr);
    CHECK(fill->station == "locker");
    const zl::items::CraftResult filled = zl::items::Craft(bag, items, *fill);
    REQUIRE(filled.ok);
    CHECK(zl::items::CountOf(bag, "dirty_water") == 1);
    CHECK(zl::items::CountOf(bag, "empty_bottle") == 0);

    const zl::items::RecipeDef* boil = recipes.Find("boil_water");
    REQUIRE(boil != nullptr);
    CHECK(boil->station == "campfire");
    CHECK(zl::items::CanCraft(bag, items, *boil));
    const zl::items::CraftResult boiled = zl::items::Craft(bag, items, *boil);
    REQUIRE(boiled.ok);
    CHECK(zl::items::CountOf(bag, "water_bottle") == 1);
    CHECK(zl::items::CountOf(bag, "dirty_water") == 0);
}

TEST_CASE("M23-F2a-T4 cooked food and fishing supply chain") {
    zl::items::ItemRegistry items;
    std::string error;
    REQUIRE_MESSAGE(
        items.LoadFile(std::string(ZL_SOURCE_DIR) + "/assets/items.json", &error),
        error);
    zl::items::RecipeRegistry recipes;
    REQUIRE_MESSAGE(
        recipes.LoadFile(std::string(ZL_SOURCE_DIR) + "/assets/recipes.json", items,
                         &error),
        error);

    const zl::items::ItemDef* rawMushroom = items.Find("wild_mushroom");
    const zl::items::ItemDef* cookedFish = items.Find("cooked_small_fish");
    const zl::items::ItemDef* cookedBerries = items.Find("cooked_wild_berries");
    const zl::items::ItemDef* cookedMushroom = items.Find("cooked_wild_mushroom");
    const zl::items::ItemDef* tea = items.Find("herbal_tea");
    REQUIRE(rawMushroom != nullptr);
    REQUIRE(cookedFish != nullptr);
    REQUIRE(cookedBerries != nullptr);
    REQUIRE(cookedMushroom != nullptr);
    REQUIRE(tea != nullptr);
    CHECK(rawMushroom->use.healthPenalty == doctest::Approx(2.0f));
    CHECK(cookedMushroom->use.healthPenalty == doctest::Approx(0.0f));
    CHECK(cookedFish->weight == doctest::Approx(0.25f));
    CHECK(cookedFish->maxStack == 3);
    CHECK(cookedFish->spoilDays == doctest::Approx(0.75f));
    CHECK(cookedBerries->maxStack == 5);
    CHECK(cookedBerries->spoilDays == doctest::Approx(0.75f));
    CHECK(tea->maxStack == 1);
    CHECK(tea->use.replaceWith == "empty_bottle");
    CHECK(tea->use.infectionSlowSeconds == doctest::Approx(90.0f));

    const zl::items::RecipeDef* cookFish = recipes.Find("cook_small_fish");
    const zl::items::RecipeDef* cookBerries = recipes.Find("cook_wild_berries");
    const zl::items::RecipeDef* cookMushroom = recipes.Find("cook_wild_mushroom");
    const zl::items::RecipeDef* brewTea = recipes.Find("brew_herbal_tea");
    const zl::items::RecipeDef* craftRod = recipes.Find("craft_fishing_rod");
    const zl::items::RecipeDef* craftBait = recipes.Find("craft_fish_bait");
    REQUIRE(cookFish != nullptr);
    REQUIRE(cookBerries != nullptr);
    REQUIRE(cookMushroom != nullptr);
    REQUIRE(brewTea != nullptr);
    REQUIRE(craftRod != nullptr);
    REQUIRE(craftBait != nullptr);
    CHECK(cookFish->station == "campfire");
    CHECK(cookBerries->station == "campfire");
    CHECK(cookMushroom->station == "campfire");
    CHECK(brewTea->station == "campfire");
    CHECK(craftRod->station.empty());
    CHECK(craftBait->station.empty());
    CHECK(craftBait->outputCount == 3);

    zl::ecs::Container bag;
    bag.capacityKg = 20.0f;
    REQUIRE(zl::items::AddItem(
                bag, items, zl::ecs::ItemInstance{"small_fish", 2, 1.0f}) == 2);
    REQUIRE(zl::items::AddItem(
                bag, items, zl::ecs::ItemInstance{"wild_berries", 1, 1.0f}) == 1);
    REQUIRE(zl::items::AddItem(
                bag, items, zl::ecs::ItemInstance{"wild_mushroom", 1, 1.0f}) == 1);
    REQUIRE(zl::items::AddItem(
                bag, items, zl::ecs::ItemInstance{"medicinal_herb", 1, 1.0f}) == 1);
    REQUIRE(zl::items::AddItem(
                bag, items, zl::ecs::ItemInstance{"water_bottle", 1, 1.0f}) == 1);
    REQUIRE(zl::items::AddItem(
                bag, items, zl::ecs::ItemInstance{"forage_stick", 2, 1.0f}) == 2);
    REQUIRE(zl::items::AddItem(
                bag, items, zl::ecs::ItemInstance{"rag", 1, 1.0f}) == 1);

    const zl::items::CraftResult fish = zl::items::Craft(bag, items, *cookFish);
    REQUIRE(fish.ok);
    CHECK(zl::items::CountOf(bag, "small_fish") == 1);
    CHECK(zl::items::CountOf(bag, "cooked_small_fish") == 1);
    REQUIRE(zl::items::Craft(bag, items, *cookBerries).ok);
    CHECK(zl::items::CountOf(bag, "cooked_wild_berries") == 1);
    REQUIRE(zl::items::Craft(bag, items, *cookMushroom).ok);
    CHECK(zl::items::CountOf(bag, "cooked_wild_mushroom") == 1);
    REQUIRE(zl::items::Craft(bag, items, *brewTea).ok);
    CHECK(zl::items::CountOf(bag, "herbal_tea") == 1);
    CHECK(zl::items::CountOf(bag, "water_bottle") == 0);
    REQUIRE(zl::items::Craft(bag, items, *craftRod).ok);
    CHECK(zl::items::CountOf(bag, "fishing_rod") == 1);
    CHECK(zl::items::CountOf(bag, "forage_stick") == 0);
    REQUIRE(zl::items::Craft(bag, items, *craftBait).ok);
    CHECK(zl::items::CountOf(bag, "small_fish") == 0);
    CHECK(zl::items::CountOf(bag, "fish_bait") == 3);

    int teaIndex = -1;
    for (std::size_t i = 0; i < bag.items.size(); ++i) {
        if (bag.items[i].defId == "herbal_tea") {
            teaIndex = static_cast<int>(i);
            break;
        }
    }
    REQUIRE(teaIndex >= 0);
    const zl::items::UseResult teaUse =
        zl::items::UseAt(bag, items, teaIndex);
    CHECK(teaUse.applied);
    CHECK(teaUse.replacementDefId == "empty_bottle");
    CHECK(teaUse.effect.thirst == doctest::Approx(0.3f));
    CHECK(teaUse.effect.heal == doctest::Approx(3.0f));
    CHECK(zl::items::CountOf(bag, "herbal_tea") == 0);
    CHECK(zl::items::CountOf(bag, "empty_bottle") == 1);
}

TEST_CASE("M23-F2b-T5 animal meat cooking and hide/bone crafting") {
    zl::items::ItemRegistry items;
    std::string error;
    REQUIRE_MESSAGE(
        items.LoadFile(std::string(ZL_SOURCE_DIR) + "/assets/items.json", &error),
        error);
    zl::items::RecipeRegistry recipes;
    REQUIRE_MESSAGE(
        recipes.LoadFile(std::string(ZL_SOURCE_DIR) + "/assets/recipes.json", items,
                         &error),
        error);

    struct MeatRecipe {
        const char* raw;
        const char* cooked;
        const char* recipe;
        float hunger;
    };
    static constexpr MeatRecipe kMeats[] = {
        {"raw_venison", "cooked_venison", "cook_venison", 0.42f},
        {"raw_rabbit_meat", "cooked_rabbit_meat", "cook_rabbit_meat", 0.3f},
        {"raw_boar_meat", "cooked_boar_meat", "cook_boar_meat", 0.55f},
    };
    for (const MeatRecipe& meat : kMeats) {
        const zl::items::ItemDef* raw = items.Find(meat.raw);
        const zl::items::ItemDef* cooked = items.Find(meat.cooked);
        const zl::items::RecipeDef* recipe = recipes.Find(meat.recipe);
        REQUIRE(raw != nullptr);
        REQUIRE(cooked != nullptr);
        REQUIRE(recipe != nullptr);
        CHECK(cooked->use.hunger > raw->use.hunger);
        CHECK(cooked->use.hunger == doctest::Approx(meat.hunger));
        CHECK(cooked->weight > 0.0f);
        CHECK(cooked->maxStack == 3);
        CHECK(cooked->spoilDays > 0.0f);
        CHECK(recipe->outputDefId == meat.cooked);
        CHECK(recipe->outputCount == 1);
        CHECK(recipe->station == "campfire");
        REQUIRE(recipe->ingredients.size() == 1u);
        CHECK(recipe->ingredients[0].defId == meat.raw);
        CHECK(recipe->ingredients[0].count == 1);
    }

    const zl::items::RecipeDef* leather = recipes.Find("craft_leather_jacket");
    const zl::items::RecipeDef* boneSpear = recipes.Find("craft_bone_spear");
    REQUIRE(leather != nullptr);
    REQUIRE(boneSpear != nullptr);
    CHECK(leather->outputDefId == "leather_jacket");
    CHECK(leather->station == "workbench");
    REQUIRE(leather->ingredients.size() == 2u);
    CHECK(leather->ingredients[0].defId == "animal_hide");
    CHECK(leather->ingredients[0].count == 3);
    CHECK(leather->ingredients[1].defId == "rag");
    REQUIRE(leather->tools.size() == 1u);
    CHECK(leather->tools[0].defId == "kitchen_knife");
    CHECK(boneSpear->outputDefId == "bone_spear");
    CHECK(boneSpear->station == "workbench");
    CHECK(items.Find("bone_spear") != nullptr);
    REQUIRE(boneSpear->ingredients.size() == 3u);
    CHECK(boneSpear->ingredients[0].defId == "bone");
    CHECK(boneSpear->ingredients[0].count == 2);
    CHECK(boneSpear->ingredients[1].defId == "wood");
    CHECK(boneSpear->ingredients[1].count == 2);

    zl::ecs::Container bag;
    bag.capacityKg = 30.0f;
    for (const MeatRecipe& meat : kMeats) {
        REQUIRE(zl::items::AddItem(
                    bag, items, zl::ecs::ItemInstance{meat.raw, 1, 1.0f}) == 1);
    }
    REQUIRE(zl::items::AddItem(
                bag, items, zl::ecs::ItemInstance{"animal_hide", 3, 1.0f}) == 3);
    REQUIRE(zl::items::AddItem(bag, items,
                               zl::ecs::ItemInstance{"bone", 2, 1.0f}) == 2);
    REQUIRE(zl::items::AddItem(bag, items,
                               zl::ecs::ItemInstance{"wood", 2, 1.0f}) == 2);
    REQUIRE(zl::items::AddItem(bag, items,
                               zl::ecs::ItemInstance{"rag", 3, 1.0f}) == 3);
    REQUIRE(zl::items::AddItem(
                bag, items, zl::ecs::ItemInstance{"kitchen_knife", 1, 0.8f}) == 1);

    for (const MeatRecipe& meat : kMeats) {
        const zl::items::RecipeDef* recipe = recipes.Find(meat.recipe);
        REQUIRE(recipe != nullptr);
        const zl::items::CraftResult cooked =
            zl::items::Craft(bag, items, *recipe);
        REQUIRE(cooked.ok);
        CHECK(zl::items::CountOf(bag, meat.raw) == 0);
        CHECK(zl::items::CountOf(bag, meat.cooked) == 1);
    }

    const zl::items::CraftResult jacket =
        zl::items::Craft(bag, items, *leather);
    REQUIRE(jacket.ok);
    CHECK(zl::items::CountOf(bag, "animal_hide") == 0);
    CHECK(zl::items::CountOf(bag, "leather_jacket") == 1);
    CHECK(zl::items::CountOf(bag, "rag") == 1);
    bool knifeFound = false;
    for (const zl::ecs::ItemInstance& entry : bag.items) {
        if (entry.defId == "kitchen_knife") {
            knifeFound = true;
            CHECK(entry.condition == doctest::Approx(0.75f));
        }
    }
    CHECK(knifeFound);

    const zl::items::CraftResult spear =
        zl::items::Craft(bag, items, *boneSpear);
    REQUIRE(spear.ok);
    CHECK(zl::items::CountOf(bag, "bone") == 0);
    CHECK(zl::items::CountOf(bag, "wood") == 0);
    CHECK(zl::items::CountOf(bag, "rag") == 0);
    CHECK(zl::items::CountOf(bag, "bone_spear") == 1);
}

TEST_CASE("F5-T3 食物营养与中毒风险") {
    zl::items::ItemRegistry items;
    std::string error;
    REQUIRE_MESSAGE(
        items.LoadFile(std::string(ZL_SOURCE_DIR) + "/assets/items.json", &error),
        error);
    const zl::items::ItemDef* raw = items.Find("raw_boar_meat");
    const zl::items::ItemDef* stew = items.Find("hearty_stew");
    const zl::items::ItemDef* smoked = items.Find("smoked_boar_meat");
    REQUIRE(raw != nullptr);
    REQUIRE(stew != nullptr);
    REQUIRE(smoked != nullptr);
    CHECK(raw->use.nutrition > 0.0f);
    CHECK(raw->use.foodPoisonChance > 0.0f);
    CHECK(stew->use.nutrition > raw->use.nutrition);
    CHECK(stew->use.foodPoisonChance == doctest::Approx(0.0f));
    CHECK(smoked->spoilDays > items.Find("cooked_boar_meat")->spoilDays);

    zl::game::FoodSafetyParams params;
    params.spoiledPoisonChance = 1.0f;
    params.poisonDurationSeconds = 10.0f;
    params.poisonDamagePerSecond = 0.5f;
    params.nutritionDecayPerDay = 0.5f;
    params.nutritionPoisonReduction = 0.5f;

    zl::ecs::FoodState state;
    const std::uint64_t seed =
        zl::game::DeriveFoodPoisonSeed(42, 1, raw->id);
    CHECK_FALSE(zl::game::FoodPoisonRoll(0.0f, seed));
    CHECK(zl::game::FoodPoisonRoll(1.0f, seed));
    const zl::game::FoodConsumption result =
        zl::game::ApplyFoodConsumption(state, *raw, true, params, seed);
    CHECK(result.poisonChance == doctest::Approx(1.0f));
    CHECK(result.poisoned);
    CHECK(result.nutritionGained > 0.0f);
    CHECK(state.poisonRemaining == doctest::Approx(10.0f));
    CHECK(state.nutrition > 0.0f);

    const float damage = zl::game::AdvanceFoodState(state, 4.0f, 0.0f, params);
    CHECK(damage == doctest::Approx(2.0f));
    CHECK(state.poisonRemaining == doctest::Approx(6.0f));
    zl::game::AdvanceFoodState(state, 6.0f, 0.0f, params);
    CHECK(state.poisonRemaining == doctest::Approx(0.0f));
    CHECK(state.poisonDamagePerSecond == doctest::Approx(0.0f));

    zl::ecs::FoodState full;
    full.nutrition = 0.8f;
    const float reduced = zl::game::FoodPoisonChance(full, *raw, false, params);
    const float base = zl::game::FoodPoisonChance(zl::ecs::FoodState{}, *raw,
                                                  false, params);
    CHECK(reduced < base);
}

TEST_CASE("F5-T3 炖菜锅具/燃料与批量制作") {
    zl::items::ItemRegistry items;
    zl::items::RecipeRegistry recipes;
    std::string error;
    REQUIRE_MESSAGE(
        items.LoadFile(std::string(ZL_SOURCE_DIR) + "/assets/items.json", &error),
        error);
    REQUIRE_MESSAGE(
        recipes.LoadFile(std::string(ZL_SOURCE_DIR) + "/assets/recipes.json", items,
                         &error),
        error);
    const zl::items::RecipeDef* stew = recipes.Find("cook_hearty_stew");
    const zl::items::RecipeDef* smoke = recipes.Find("smoke_boar_meat");
    REQUIRE(stew != nullptr);
    REQUIRE(smoke != nullptr);
    CHECK(stew->category == zl::items::RecipeCategory::Cooking);
    CHECK(smoke->category == zl::items::RecipeCategory::Preservation);
    REQUIRE(stew->cookware.has_value());
    CHECK(stew->cookware->defId == "cooking_pot");
    REQUIRE(stew->fuel.has_value());
    CHECK(stew->fuel->defId == "wood");
    CHECK(smoke->category == zl::items::RecipeCategory::Preservation);
    REQUIRE(smoke->fuel.has_value());
    CHECK(smoke->fuel->count == 2);

    zl::ecs::Container bag;
    bag.capacityKg = 30.0f;
    REQUIRE(zl::items::AddItem(
                bag, items, zl::ecs::ItemInstance{"fresh_potato", 2, 1.0f}) ==
            2);
    REQUIRE(zl::items::AddItem(
                bag, items, zl::ecs::ItemInstance{"fresh_cabbage", 2, 1.0f}) ==
            2);
    REQUIRE(zl::items::AddItem(
                bag, items, zl::ecs::ItemInstance{"water_bottle", 2, 1.0f}) ==
            2);
    REQUIRE(zl::items::AddItem(
                bag, items, zl::ecs::ItemInstance{"cooking_pot", 1, 1.0f}) == 1);
    REQUIRE(zl::items::AddItem(bag, items,
                               zl::ecs::ItemInstance{"wood", 2, 1.0f}) == 2);
    CHECK(zl::items::MaxCraftCount(bag, items, *stew, 5) == 2);
    const zl::items::CraftResult first = zl::items::Craft(bag, items, *stew);
    REQUIRE(first.ok);
    CHECK(zl::items::CountOf(bag, "hearty_stew") == 1);
    CHECK(zl::items::CountOf(bag, "wood") == 1);
    CHECK(zl::items::CountOf(bag, "cooking_pot") == 1);
    CHECK(zl::items::CountOf(bag, "water_bottle") == 1);
    const zl::items::CraftResult second = zl::items::Craft(bag, items, *stew);
    REQUIRE(second.ok);
    CHECK(zl::items::CountOf(bag, "wood") == 0);
    CHECK(zl::items::CountOf(bag, "cooking_pot") == 1);
    CHECK(zl::items::MaxCraftCount(bag, items, *stew, 5) == 0);
}

TEST_CASE("M23-F5-T5 batch cooking respects fuel and keeps cookware") {
    zl::items::ItemRegistry items;
    zl::items::RecipeRegistry recipes;
    std::string error;
    REQUIRE_MESSAGE(
        items.LoadFile(std::string(ZL_SOURCE_DIR) + "/assets/items.json", &error),
        error);
    REQUIRE_MESSAGE(
        recipes.LoadFile(std::string(ZL_SOURCE_DIR) + "/assets/recipes.json", items,
                         &error),
        error);
    const zl::items::RecipeDef* stew = recipes.Find("cook_hearty_stew");
    REQUIRE(stew != nullptr);

    zl::ecs::Container bag;
    bag.capacityKg = 40.0f;
    REQUIRE(zl::items::AddItem(
                bag, items, zl::ecs::ItemInstance{"fresh_potato", 5, 1.0f}) ==
            5);
    REQUIRE(zl::items::AddItem(
                bag, items, zl::ecs::ItemInstance{"fresh_cabbage", 5, 1.0f}) ==
            5);
    REQUIRE(zl::items::AddItem(
                bag, items, zl::ecs::ItemInstance{"water_bottle", 5, 1.0f}) ==
            5);
    REQUIRE(zl::items::AddItem(
                bag, items, zl::ecs::ItemInstance{"cooking_pot", 1, 0.8f}) == 1);
    REQUIRE(zl::items::AddItem(bag, items,
                               zl::ecs::ItemInstance{"wood", 3, 1.0f}) == 3);

    CHECK(zl::items::MaxCraftCount(bag, items, *stew, 99) == 3);
    for (int i = 0; i < 3; ++i) {
        const zl::items::CraftResult crafted =
            zl::items::Craft(bag, items, *stew);
        REQUIRE(crafted.ok);
    }
    CHECK(zl::items::CountOf(bag, "hearty_stew") == 3);
    CHECK(zl::items::CountOf(bag, "wood") == 0);
    CHECK(zl::items::CountOf(bag, "cooking_pot") == 1);
    CHECK(zl::items::CountOf(bag, "fresh_potato") == 2);
    CHECK(zl::items::CountOf(bag, "fresh_cabbage") == 2);
    CHECK(zl::items::CountOf(bag, "water_bottle") == 2);
    std::string reason;
    CHECK_FALSE(zl::items::CanCraft(bag, items, *stew, &reason));
    CHECK(reason.find("燃料") != std::string::npos);
    CHECK(zl::items::MaxCraftCount(bag, items, *stew, 99) == 0);

    REQUIRE(zl::items::AddItem(bag, items,
                               zl::ecs::ItemInstance{"wood", 2, 1.0f}) == 2);
    CHECK(zl::items::MaxCraftCount(bag, items, *stew, 99) == 2);
}

TEST_CASE("M23-F5-T5 cooked meals outperform their crop inputs") {
    zl::items::ItemRegistry items;
    std::string error;
    REQUIRE_MESSAGE(
        items.LoadFile(std::string(ZL_SOURCE_DIR) + "/assets/items.json", &error),
        error);
    const zl::items::ItemDef* potato = items.Find("fresh_potato");
    const zl::items::ItemDef* cabbage = items.Find("fresh_cabbage");
    const zl::items::ItemDef* stew = items.Find("hearty_stew");
    const zl::items::ItemDef* rawBoar = items.Find("raw_boar_meat");
    const zl::items::ItemDef* cookedBoar = items.Find("cooked_boar_meat");
    const zl::items::ItemDef* smokedBoar = items.Find("smoked_boar_meat");
    REQUIRE(potato != nullptr);
    REQUIRE(cabbage != nullptr);
    REQUIRE(stew != nullptr);
    REQUIRE(rawBoar != nullptr);
    REQUIRE(cookedBoar != nullptr);
    REQUIRE(smokedBoar != nullptr);

    CHECK(stew->use.hunger >= potato->use.hunger + cabbage->use.hunger);
    CHECK(stew->use.nutrition >=
          potato->use.nutrition + cabbage->use.nutrition - 1e-5f);
    CHECK(stew->use.heal > 0.0f);
    CHECK(stew->spoilDays >= potato->spoilDays);
    CHECK(stew->use.foodPoisonChance == doctest::Approx(0.0f));
    CHECK(smokedBoar->use.hunger > cookedBoar->use.hunger);
    CHECK(smokedBoar->use.nutrition > cookedBoar->use.nutrition);
    CHECK(smokedBoar->use.foodPoisonChance == doctest::Approx(0.0f));
    CHECK(rawBoar->use.foodPoisonChance > 0.0f);
}
