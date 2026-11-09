#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "items/Crafting.h"
#include "items/ItemRegistry.h"
#include "items/RecipeRegistry.h"
#include "items/Repair.h"

using namespace zl;

namespace {

const char* kItemsJson = R"({
  "format": "items-v1",
  "items": [
    { "id": "bandage", "name": "绷带", "icon": "i", "weight": 0.1, "maxStack": 5, "type": "consumable" },
    { "id": "rag", "name": "破布", "icon": "i", "weight": 0.1, "maxStack": 10, "type": "generic" },
    { "id": "knife", "name": "刀", "icon": "i", "weight": 0.6, "maxStack": 1, "type": "weapon" },
    { "id": "jacket", "name": "夹克", "icon": "i", "weight": 1.2, "maxStack": 1, "type": "clothing", "repairEfficiency": 1.5 },
    { "id": "spear", "name": "长矛", "icon": "i", "weight": 1.1, "maxStack": 1, "type": "weapon" },
    { "id": "padded", "name": "加固夹克", "icon": "i", "weight": 1.4, "maxStack": 1, "type": "clothing" },
    { "id": "hammer", "name": "锤子", "icon": "i", "weight": 2.0, "maxStack": 1, "type": "weapon" },
    { "id": "repair_kit", "name": "维修包", "icon": "i", "weight": 0.3, "maxStack": 5, "type": "generic" }
  ]
})";

const char* kRecipesJson = R"({
  "format": "recipes-v3",
  "recipes": [
    {
      "id": "field_bandage",
      "name": "简易绷带",
      "output": { "item": "bandage", "count": 1 },
      "ingredients": [ { "item": "rag", "count": 2 } ],
      "time": 0.8
    },
    {
      "id": "makeshift_spear",
      "name": "简易长矛",
      "output": { "item": "spear", "count": 1 },
      "ingredients": [
        { "item": "knife", "count": 1 },
        { "item": "rag", "count": 1 }
      ],
      "time": 1.2
    },
    {
      "id": "padded",
      "name": "加固夹克",
      "output": { "item": "padded", "count": 1 },
      "ingredients": [
        { "item": "jacket", "count": 1 },
        { "item": "rag", "count": 2 }
      ],
      "tools": [ { "item": "hammer", "count": 1 } ],
      "station": "workbench",
      "time": 1.5
    }
  ]
})";

struct Fixture {
    items::ItemRegistry items;
    items::RecipeRegistry recipes;

    Fixture() {
        std::string error;
        REQUIRE(items.LoadFromString(kItemsJson, "test-items", &error));
        REQUIRE(recipes.LoadFromString(kRecipesJson, "test-recipes", items, &error));
    }
};

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

ecs::ItemInstance Item(const char* id, int count) {
    return ecs::ItemInstance{id, count, 1.0f};
}

}  // namespace

TEST_CASE("配方注册表：合法加载与查找") {
    Fixture f;
    REQUIRE(f.recipes.Size() == 3);
    const items::RecipeDef* bandage = f.recipes.Find("field_bandage");
    REQUIRE(bandage != nullptr);
    CHECK(bandage->name == "简易绷带");
    CHECK(bandage->outputDefId == "bandage");
    CHECK(bandage->outputCount == 1);
    REQUIRE(bandage->ingredients.size() == 1);
    CHECK(bandage->ingredients[0].defId == "rag");
    CHECK(bandage->ingredients[0].count == 2);
    CHECK(bandage->tools.empty());
    CHECK(bandage->station.empty());
    CHECK(bandage->craftSeconds == doctest::Approx(0.8f));
    const items::RecipeDef* padded = f.recipes.Find("padded");
    REQUIRE(padded != nullptr);
    REQUIRE(padded->tools.size() == 1);
    CHECK(padded->tools[0].defId == "hammer");
    CHECK(padded->tools[0].count == 1);
    CHECK(padded->tools[0].wear == doctest::Approx(0.1f));
    CHECK(padded->station == "workbench");
    CHECK(padded->craftSeconds == doctest::Approx(1.5f));
    CHECK(f.recipes.At(1) != nullptr);
    CHECK(f.recipes.At(1)->id == "makeshift_spear");
    CHECK(f.recipes.At(3) == nullptr);
    CHECK(f.recipes.Find("missing") == nullptr);
}

TEST_CASE("配方注册表：兼容 recipes-v1") {
    items::ItemRegistry items;
    items::RecipeRegistry registry;
    std::string error;
    REQUIRE(items.LoadFromString(kItemsJson, "test-items", &error));
    REQUIRE(registry.LoadFromString(R"({ "format": "recipes-v1", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ] }
    ] })", "test-recipes", items, &error));
    REQUIRE(registry.Size() == 1);
    const items::RecipeDef* recipe = registry.Find("x");
    REQUIRE(recipe != nullptr);
    CHECK(recipe->tools.empty());
    CHECK(recipe->craftSeconds == doctest::Approx(1.0f));
}

TEST_CASE("配方注册表：结构、引用与重复项校验") {
    items::ItemRegistry items;
    std::string error;
    REQUIRE(items.LoadFromString(kItemsJson, "test-items", &error));
    items::RecipeRegistry registry;

    auto load = [&](const std::string& text) {
        error.clear();
        return registry.LoadFromString(text, "test-recipes", items, &error);
    };

    CHECK_FALSE(load(R"({ "format": "recipes-v1", "recipes": [] })"));
    CHECK(Contains(error, "数组为空"));

    CHECK_FALSE(load(R"({ "format": "recipes-v5", "recipes": [{}] })"));
    CHECK(Contains(error, "format 不受支持"));

    CHECK_FALSE(load(R"({ "format": "recipes-v1", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 } }
    ] })"));
    CHECK(Contains(error, "ingredients"));

    CHECK_FALSE(load(R"({ "format": "recipes-v1", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "ghost", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ] }
    ] })"));
    CHECK(Contains(error, "未注册物品 'ghost'"));

    CHECK_FALSE(load(R"({ "format": "recipes-v1", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "ghost", "count": 1 } ] }
    ] })"));
    CHECK(Contains(error, "未注册物品 'ghost'"));

    CHECK_FALSE(load(R"({ "format": "recipes-v1", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 }, { "item": "rag", "count": 1 } ] }
    ] })"));
    CHECK(Contains(error, "重复材料"));

    CHECK_FALSE(load(R"({ "format": "recipes-v1", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ] },
        { "id": "x", "name": "y", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 2 } ] }
    ] })"));
    CHECK(Contains(error, "重复"));

    CHECK_FALSE(load(R"({ "format": "recipes-v1", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 0 },
          "ingredients": [ { "item": "rag", "count": 1 } ] }
    ] })"));
    CHECK(Contains(error, "count"));

    CHECK_FALSE(load(R"({ "format": "recipes-v2", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ], "time": 0 }
    ] })"));
    CHECK(Contains(error, "time"));

    CHECK_FALSE(load(R"({ "format": "recipes-v2", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ], "time": 61 }
    ] })"));
    CHECK(Contains(error, "time"));

    CHECK_FALSE(load(R"({ "format": "recipes-v2", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ], "time": "fast" }
    ] })"));
    CHECK(Contains(error, "time"));

    CHECK_FALSE(load(R"({ "format": "recipes-v1", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ], "time": 1.0 }
    ] })"));
    CHECK(Contains(error, "recipes-v1 不支持 time"));

    CHECK_FALSE(load(R"({ "format": "recipes-v1", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1, "tool": true } ] }
    ] })"));
    CHECK(Contains(error, "未知字段"));

    CHECK_FALSE(load(R"({ "format": "recipes-v1", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ],
          "tools": [ { "item": "hammer", "count": 1 } ] }
    ] })"));
    CHECK(Contains(error, "recipes-v1 不支持 tools"));

    CHECK_FALSE(load(R"({ "format": "recipes-v2", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ],
          "station": "workbench" }
    ] })"));
    CHECK(Contains(error, "recipes-v2 不支持 station"));

    CHECK_FALSE(load(R"({ "format": "recipes-v3", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ], "station": "" }
    ] })"));
    CHECK(Contains(error, "station 长度非法"));

    CHECK_FALSE(load(R"({ "format": "recipes-v3", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ], "station": "Workbench" }
    ] })"));
    CHECK(Contains(error, "station"));

    CHECK_FALSE(load(R"({ "format": "recipes-v2", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ], "tools": [] }
    ] })"));
    CHECK(Contains(error, "必须是非空数组"));

    CHECK_FALSE(load(R"({ "format": "recipes-v2", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ],
          "tools": [ { "item": "ghost", "count": 1 } ] }
    ] })"));
    CHECK(Contains(error, "未注册物品 'ghost'"));

    CHECK_FALSE(load(R"({ "format": "recipes-v2", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ],
          "tools": [ { "item": "hammer", "count": 1 }, { "item": "hammer", "count": 1 } ] }
    ] })"));
    CHECK(Contains(error, "重复工具"));

    CHECK_FALSE(load(R"({ "format": "recipes-v2", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ],
          "tools": [ { "item": "rag", "count": 1 } ] }
    ] })"));
    CHECK(Contains(error, "工具与材料重复"));

    CHECK_FALSE(load(R"({ "format": "recipes-v2", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "jacket", "count": 1 } ],
          "tools": [ { "item": "rag", "count": 1 } ] }
    ] })"));
    CHECK(Contains(error, "不可堆叠"));

    CHECK_FALSE(load(R"({ "format": "recipes-v2", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ],
          "tools": [ { "item": "hammer", "count": 1, "wear": -0.1 } ] }
    ] })"));
    CHECK(Contains(error, "wear"));

    CHECK_FALSE(load(R"({ "format": "recipes-v2", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ],
          "tools": [ { "item": "hammer", "count": 1, "wear": 1.1 } ] }
    ] })"));
    CHECK(Contains(error, "wear"));

    CHECK_FALSE(load(R"({ "format": "recipes-v2", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ],
          "tools": [ { "item": "hammer", "count": 1, "wear": "fast" } ] }
    ] })"));
    CHECK(Contains(error, "wear"));

    REQUIRE(load(R"({ "format": "recipes-v2", "recipes": [
        { "id": "x", "name": "x", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ],
          "tools": [ { "item": "hammer", "count": 1, "wear": 0.25 } ] }
    ] })"));
    const items::RecipeDef* explicitWear = registry.Find("x");
    REQUIRE(explicitWear != nullptr);
    REQUIRE(explicitWear->tools.size() == 1);
    CHECK(explicitWear->tools[0].wear == doctest::Approx(0.25f));

    CHECK(registry.Size() == 1);
}

TEST_CASE("制作：成功扣除材料并加入产物") {
    Fixture f;
    const items::RecipeDef* bandage = f.recipes.Find("field_bandage");
    REQUIRE(bandage != nullptr);

    ecs::Container bag;
    bag.capacityKg = 20.0f;
    REQUIRE(items::AddItem(bag, f.items, Item("rag", 3)) == 3);

    std::string reason;
    CHECK(items::CanCraft(bag, f.items, *bandage, &reason));
    CHECK(reason.empty());

    const items::CraftResult result = items::Craft(bag, f.items, *bandage);
    CHECK(result.ok);
    CHECK(result.outputDefId == "bandage");
    CHECK(result.outputCount == 1);
    CHECK(items::CountOf(bag, "rag") == 1);
    CHECK(items::CountOf(bag, "bandage") == 1);
    CHECK(bag.items.size() == 2);
}

TEST_CASE("制作：材料不足时不修改容器") {
    Fixture f;
    const items::RecipeDef* spear = f.recipes.Find("makeshift_spear");
    REQUIRE(spear != nullptr);

    ecs::Container bag;
    bag.capacityKg = 20.0f;
    REQUIRE(items::AddItem(bag, f.items, Item("rag", 3)) == 3);

    std::string reason;
    CHECK_FALSE(items::CanCraft(bag, f.items, *spear, &reason));
    CHECK(Contains(reason, "材料不足"));
    CHECK(Contains(reason, "刀"));

    const items::CraftResult result = items::Craft(bag, f.items, *spear);
    CHECK_FALSE(result.ok);
    CHECK(items::CountOf(bag, "rag") == 3);
    CHECK(items::CountOf(bag, "knife") == 0);
    CHECK(bag.items.size() == 1);
}

TEST_CASE("制作：工具必须携带并磨损但不消耗") {
    Fixture f;
    const items::RecipeDef* padded = f.recipes.Find("padded");
    REQUIRE(padded != nullptr);

    ecs::Container bag;
    bag.capacityKg = 20.0f;
    REQUIRE(items::AddItem(bag, f.items, Item("jacket", 1)) == 1);
    REQUIRE(items::AddItem(bag, f.items, Item("rag", 2)) == 2);

    std::string reason;
    CHECK_FALSE(items::CanCraft(bag, f.items, *padded, &reason));
    CHECK(Contains(reason, "缺少工具"));
    CHECK(Contains(reason, "锤子"));

    const items::CraftResult failed = items::Craft(bag, f.items, *padded);
    CHECK_FALSE(failed.ok);
    CHECK(items::CountOf(bag, "jacket") == 1);
    CHECK(items::CountOf(bag, "rag") == 2);
    CHECK(items::CountOf(bag, "padded") == 0);

    REQUIRE(items::AddItem(bag, f.items, Item("hammer", 1)) == 1);
    CHECK(items::CanCraft(bag, f.items, *padded, &reason));
    CHECK(reason.empty());

    const items::CraftResult result = items::Craft(bag, f.items, *padded);
    CHECK(result.ok);
    CHECK(result.brokenToolDefIds.empty());
    CHECK(items::CountOf(bag, "jacket") == 0);
    CHECK(items::CountOf(bag, "rag") == 0);
    CHECK(items::CountOf(bag, "hammer") == 1);
    CHECK(items::CountOf(bag, "padded") == 1);
    const auto hammer = std::find_if(
        bag.items.begin(), bag.items.end(),
        [](const ecs::ItemInstance& item) { return item.defId == "hammer"; });
    REQUIRE(hammer != bag.items.end());
    CHECK(hammer->condition == doctest::Approx(0.9f));
}

TEST_CASE("制作：工具磨损耗尽后移除") {
    Fixture f;
    const items::RecipeDef* padded = f.recipes.Find("padded");
    REQUIRE(padded != nullptr);

    ecs::Container bag;
    bag.capacityKg = 20.0f;
    REQUIRE(items::AddItem(bag, f.items, Item("jacket", 1)) == 1);
    REQUIRE(items::AddItem(bag, f.items, Item("rag", 2)) == 2);
    REQUIRE(items::AddItem(bag, f.items, ecs::ItemInstance{"hammer", 1, 0.05f}) == 1);

    std::string reason;
    CHECK(items::CanCraft(bag, f.items, *padded, &reason));

    const items::CraftResult result = items::Craft(bag, f.items, *padded);
    CHECK(result.ok);
    REQUIRE(result.brokenToolDefIds.size() == 1);
    CHECK(result.brokenToolDefIds[0] == "hammer");
    CHECK(items::CountOf(bag, "hammer") == 0);
    CHECK(items::CountUsableTools(bag, "hammer") == 0);
    CHECK(items::CountOf(bag, "padded") == 1);
    CHECK_FALSE(items::CanCraft(bag, f.items, *padded, &reason));
    CHECK(Contains(reason, "缺少工具"));
}

TEST_CASE("制作：工具状态决定效率与实际耗时") {
    Fixture f;
    const items::RecipeDef* padded = f.recipes.Find("padded");
    const items::RecipeDef* bandage = f.recipes.Find("field_bandage");
    REQUIRE(padded != nullptr);
    REQUIRE(bandage != nullptr);

    ecs::Container empty;
    empty.capacityKg = 20.0f;
    CHECK(items::ToolEfficiency(empty, *padded) == doctest::Approx(0.0f));
    CHECK(items::ToolEfficiency(empty, *bandage) == doctest::Approx(1.0f));
    CHECK(items::EffectiveCraftSeconds(*padded, 0.0f) == doctest::Approx(0.0f));

    auto hammerBag = [&](float condition) {
        ecs::Container bag;
        bag.capacityKg = 20.0f;
        REQUIRE(items::AddItem(bag, f.items,
                              ecs::ItemInstance{"hammer", 1, condition}) == 1);
        return bag;
    };

    ecs::Container full = hammerBag(1.0f);
    CHECK(items::ToolEfficiency(full, *padded) == doctest::Approx(1.0f));
    CHECK(items::EffectiveCraftSeconds(*padded, 1.0f) == doctest::Approx(1.5f));

    ecs::Container half = hammerBag(0.5f);
    CHECK(items::ToolEfficiency(half, *padded) == doctest::Approx(0.75f));
    CHECK(items::EffectiveCraftSeconds(*padded, 0.75f) ==
          doctest::Approx(2.0f));

    ecs::Container worn = hammerBag(0.1f);
    CHECK(items::ToolEfficiency(worn, *padded) == doctest::Approx(0.55f));
    CHECK(items::EffectiveCraftSeconds(*padded, 0.55f) ==
          doctest::Approx(1.5f / 0.55f));

    ecs::Container broken = hammerBag(0.0f);
    CHECK(items::ToolEfficiency(broken, *padded) == doctest::Approx(0.0f));
    CHECK(items::EffectiveCraftSeconds(*padded, 0.1f) == doctest::Approx(3.0f));

    items::CraftingProcess process;
    process.Start("padded", 2.0f, 0.75f);
    CHECK(process.toolEfficiency == doctest::Approx(0.75f));
    process.Cancel();
    CHECK(process.toolEfficiency == doctest::Approx(1.0f));
}

TEST_CASE("制作：输出失败不磨损工具") {
    Fixture f;
    items::RecipeDef recipe;
    recipe.id = "heavy";
    recipe.name = "重物";
    recipe.outputDefId = "spear";
    recipe.outputCount = 1;
    recipe.ingredients.push_back(items::RecipeIngredient{"rag", 1});
    recipe.tools.push_back(items::RecipeIngredient{"hammer", 1, 0.5f});

    ecs::Container bag;
    bag.capacityKg = 2.3f;
    REQUIRE(items::AddItem(bag, f.items, Item("hammer", 1)) == 1);
    REQUIRE(items::AddItem(bag, f.items, Item("rag", 1)) == 1);

    const items::CraftResult result = items::Craft(bag, f.items, recipe);
    CHECK_FALSE(result.ok);
    CHECK(Contains(result.error, "背包空间不足"));
    REQUIRE(bag.items.size() == 2);
    CHECK(bag.items[0].condition == doctest::Approx(1.0f));
    CHECK(items::CountOf(bag, "rag") == 1);
    CHECK(items::CountOf(bag, "spear") == 0);
}

TEST_CASE("制作：计时推进、完成与取消") {
    items::CraftingProcess process;
    CHECK_FALSE(process.active);
    CHECK(process.Progress() == doctest::Approx(0.0f));

    process.Start("padded", 1.5f);
    CHECK(process.active);
    CHECK(process.recipeId == "padded");
    CHECK(process.duration == doctest::Approx(1.5f));
    CHECK_FALSE(process.Advance(0.5f));
    CHECK(process.Progress() == doctest::Approx(1.0f / 3.0f));
    CHECK_FALSE(process.Advance(0.9f));
    CHECK(process.Progress() == doctest::Approx(1.4f / 1.5f));
    CHECK(process.Advance(0.2f));
    CHECK_FALSE(process.active);

    process.Start("bandage", 1.0f);
    CHECK(process.active);
    process.Advance(0.4f);
    process.Cancel();
    CHECK_FALSE(process.active);
    CHECK(process.recipeId.empty());
    CHECK(process.elapsed == doctest::Approx(0.0f));
    CHECK(process.Progress() == doctest::Approx(0.0f));
}

TEST_CASE("制作：输出放不下时原子失败") {
    Fixture f;
    const items::RecipeDef* spear = f.recipes.Find("makeshift_spear");
    REQUIRE(spear != nullptr);

    ecs::Container bag;
    bag.capacityKg = 0.75f;
    REQUIRE(items::AddItem(bag, f.items, Item("knife", 1)) == 1);
    REQUIRE(items::AddItem(bag, f.items, Item("rag", 1)) == 1);

    const items::CraftResult result = items::Craft(bag, f.items, *spear);
    CHECK_FALSE(result.ok);
    CHECK(Contains(result.error, "背包空间不足"));
    CHECK(items::CountOf(bag, "knife") == 1);
    CHECK(items::CountOf(bag, "rag") == 1);
    CHECK(items::CountOf(bag, "spear") == 0);
}

TEST_CASE("制作：批量预检受材料与工具限制") {
    Fixture f;
    const items::RecipeDef* bandage = f.recipes.Find("field_bandage");
    REQUIRE(bandage != nullptr);
    ecs::Container bag;
    bag.capacityKg = 20.0f;
    REQUIRE(items::AddItem(bag, f.items, Item("rag", 5)) == 5);

    CHECK(items::MaxCraftCount(bag, f.items, *bandage, 99) == 2);
    CHECK(items::MaxCraftCount(bag, f.items, *bandage, 1) == 1);
    CHECK(items::CountOf(bag, "rag") == 5);
    CHECK(items::CountOf(bag, "bandage") == 0);

    ecs::Container empty;
    empty.capacityKg = 20.0f;
    CHECK(items::MaxCraftCount(empty, f.items, *bandage, 99) == 0);

    const items::RecipeDef* padded = f.recipes.Find("padded");
    REQUIRE(padded != nullptr);
    ecs::Container tools;
    tools.capacityKg = 20.0f;
    REQUIRE(items::AddItem(tools, f.items, Item("jacket", 1)) == 1);
    REQUIRE(items::AddItem(tools, f.items, Item("rag", 2)) == 2);
    REQUIRE(items::AddItem(tools, f.items, Item("hammer", 1)) == 1);
    tools.items.back().condition = 0.05f;
    CHECK(items::MaxCraftCount(tools, f.items, *padded, 99) == 1);
    CHECK(items::CountOf(tools, "hammer") == 1);
    CHECK(tools.items.back().condition == doctest::Approx(0.05f));
}

TEST_CASE("制作：无材料配方被拒绝") {
    Fixture f;
    items::RecipeDef recipe;
    recipe.id = "free";
    recipe.outputDefId = "bandage";
    recipe.outputCount = 1;

    ecs::Container bag;
    bag.capacityKg = 20.0f;
    const items::CraftResult result = items::Craft(bag, f.items, recipe);
    CHECK_FALSE(result.ok);
    CHECK(Contains(result.error, "没有材料"));
}

TEST_CASE("配方注册表：物品蓝图引用必须存在") {
    items::ItemRegistry items;
    std::string error;
    REQUIRE(items.LoadFromString(R"({
      "format": "items-v1",
      "items": [
        { "id": "rag", "name": "rag", "icon": "i", "weight": 0.1, "maxStack": 10, "type": "generic" },
        { "id": "bandage", "name": "bandage", "icon": "i", "weight": 0.1, "maxStack": 5, "type": "generic" },
        { "id": "magazine", "name": "magazine", "icon": "i", "weight": 0.2, "maxStack": 1, "type": "generic", "unlocksRecipes": ["missing_recipe"] }
      ]
    })", "test-items", &error));

    items::RecipeRegistry registry;
    CHECK_FALSE(registry.LoadFromString(R"({
      "format": "recipes-v1",
      "recipes": [
        { "id": "field_bandage", "name": "bandage", "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 2 } ] }
      ]
    })", "test-recipes", items, &error));
    CHECK(Contains(error, "unlocksRecipes"));
    CHECK(Contains(error, "missing_recipe"));
}

TEST_CASE("仓库内置配方文件与物品定义一致") {
    items::ItemRegistry items;
    std::string error;
    const std::string itemsPath = std::string(ZL_SOURCE_DIR) + "/assets/items.json";
    REQUIRE(items.LoadFile(itemsPath, &error));
    CHECK(items.Size() == 94);
    const items::ItemDef* magazine = items.Find("magazine_cooking");
    REQUIRE(magazine != nullptr);
    REQUIRE(magazine->unlocksRecipes.size() == 1u);
    CHECK(magazine->unlocksRecipes[0] == "campfire_beans");

    items::RecipeRegistry recipes;
    const std::string recipesPath = std::string(ZL_SOURCE_DIR) + "/assets/recipes.json";
    REQUIRE(recipes.LoadFile(recipesPath, items, &error));
    CHECK(recipes.Size() >= 12);
    const items::RecipeDef* campfireBeans = recipes.Find("campfire_beans");
    REQUIRE(campfireBeans != nullptr);
    CHECK(campfireBeans->station == "campfire");
    CHECK(campfireBeans->outputDefId == "cooked_beans");
    for (const items::RecipeDef& recipe : recipes.All()) {
        CHECK_FALSE(recipe.id.empty());
        CHECK_FALSE(recipe.name.empty());
        CHECK(items.Find(recipe.outputDefId) != nullptr);
        const bool stationValid = recipe.station.empty() ||
                                  recipe.station == "workbench" ||
                                  recipe.station == "campfire" ||
                                  recipe.station == "locker";
        CHECK(stationValid);
        CHECK_FALSE(recipe.ingredients.empty());
        CHECK(recipe.craftSeconds >= 0.1f);
        CHECK(recipe.craftSeconds <= 60.0f);
        for (const items::RecipeIngredient& ingredient : recipe.ingredients) {
            CHECK(items.Find(ingredient.defId) != nullptr);
            CHECK(ingredient.count >= 1);
        }
        for (const items::RecipeIngredient& tool : recipe.tools) {
            const items::ItemDef* toolDef = items.Find(tool.defId);
            CHECK(toolDef != nullptr);
            CHECK(tool.count >= 1);
            CHECK(tool.wear >= 0.0f);
            CHECK(tool.wear <= 1.0f);
            if (toolDef != nullptr) {
                CHECK(toolDef->maxStack == 1);
            }
        }
    }
}

TEST_CASE("维修：消耗维修包并恢复耐久") {
    Fixture f;
    ecs::Container bag;
    bag.capacityKg = 20.0f;
    REQUIRE(items::AddItem(bag, f.items, Item("repair_kit", 1)) == 1);
    REQUIRE(items::AddItem(bag, f.items, ecs::ItemInstance{"hammer", 1, 0.4f}) == 1);

    const items::RepairResult result =
        items::Repair(bag, f.items, 1, "repair_kit", 1, 0.5f);
    CHECK(result.ok);
    CHECK(result.defId == "hammer");
    CHECK(result.before == doctest::Approx(0.4f));
    CHECK(result.after == doctest::Approx(0.9f));
    CHECK(result.materialDefId == "repair_kit");
    CHECK(items::CountOf(bag, "repair_kit") == 0);
    CHECK(items::CountUsableTools(bag, "hammer") == 1);
    REQUIRE(bag.items.size() == 1);
    CHECK(bag.items[0].defId == "hammer");
    CHECK(bag.items[0].condition == doctest::Approx(0.9f));
}

TEST_CASE("维修：衣物可维修且按 repairEfficiency 恢复") {
    Fixture f;
    ecs::Container bag;
    bag.capacityKg = 20.0f;
    REQUIRE(items::AddItem(bag, f.items, Item("repair_kit", 1)) == 1);
    REQUIRE(items::AddItem(bag, f.items, ecs::ItemInstance{"jacket", 1, 0.4f}) == 1);

    const items::RepairResult result =
        items::Repair(bag, f.items, 1, "repair_kit", 1, 0.2f);
    CHECK(result.ok);
    CHECK(result.defId == "jacket");
    CHECK(result.after == doctest::Approx(0.7f));
    CHECK(items::CountOf(bag, "repair_kit") == 0);
}

TEST_CASE("维修：耐久上限与失败不改容器") {
    Fixture f;
    ecs::Container bag;
    bag.capacityKg = 20.0f;
    REQUIRE(items::AddItem(bag, f.items, ecs::ItemInstance{"hammer", 1, 0.8f}) == 1);
    REQUIRE(items::AddItem(bag, f.items, Item("repair_kit", 1)) == 1);

    const items::RepairResult clamped =
        items::Repair(bag, f.items, 0, "repair_kit", 1, 0.5f);
    CHECK(clamped.ok);
    CHECK(clamped.after == doctest::Approx(1.0f));
    CHECK(items::CountOf(bag, "repair_kit") == 0);

    REQUIRE(items::AddItem(bag, f.items, Item("repair_kit", 1)) == 1);
    ecs::Container before = bag;
    const items::RepairResult full =
        items::Repair(bag, f.items, 0, "repair_kit", 1, 0.5f);
    CHECK_FALSE(full.ok);
    CHECK(Contains(full.error, "耐久已满"));
    CHECK(items::CountOf(bag, "repair_kit") == 1);
    CHECK(bag.items[0].condition == doctest::Approx(1.0f));
    CHECK(bag.items.size() == before.items.size());
}

TEST_CASE("维修：缺材料、损坏与非工具被拒绝") {
    Fixture f;
    ecs::Container bag;
    bag.capacityKg = 20.0f;
    REQUIRE(items::AddItem(bag, f.items, ecs::ItemInstance{"hammer", 1, 0.5f}) == 1);

    const items::RepairResult missing =
        items::Repair(bag, f.items, 0, "repair_kit", 1, 0.5f);
    CHECK_FALSE(missing.ok);
    CHECK(Contains(missing.error, "缺少维修材料"));
    CHECK(bag.items[0].condition == doctest::Approx(0.5f));

    bag.items[0].condition = 0.0f;
    const items::RepairResult broken =
        items::Repair(bag, f.items, 0, "repair_kit", 1, 0.5f);
    CHECK_FALSE(broken.ok);
    CHECK(Contains(broken.error, "损坏"));

    bag.items[0] = ecs::ItemInstance{"rag", 2, 0.5f};
    const items::RepairResult nonTool =
        items::Repair(bag, f.items, 0, "repair_kit", 1, 0.5f);
    CHECK_FALSE(nonTool.ok);
    CHECK(Contains(nonTool.error, "不可维修"));

    const items::RepairResult invalidIndex =
        items::Repair(bag, f.items, 9, "repair_kit", 1, 0.5f);
    CHECK_FALSE(invalidIndex.ok);
    CHECK(Contains(invalidIndex.error, "物品不存在"));
}

TEST_CASE("维修：材料定义与参数校验") {
    Fixture f;
    ecs::Container bag;
    bag.capacityKg = 20.0f;
    REQUIRE(items::AddItem(bag, f.items, ecs::ItemInstance{"hammer", 1, 0.5f}) == 1);

    const items::RepairResult unknown =
        items::Repair(bag, f.items, 0, "ghost", 1, 0.5f);
    CHECK_FALSE(unknown.ok);
    CHECK(Contains(unknown.error, "未注册"));

    const items::RepairResult zeroCount =
        items::Repair(bag, f.items, 0, "repair_kit", 0, 0.5f);
    CHECK_FALSE(zeroCount.ok);
    CHECK(Contains(zeroCount.error, "数量无效"));

    const items::RepairResult self =
        items::Repair(bag, f.items, 0, "hammer", 1, 0.5f);
    CHECK_FALSE(self.ok);
    CHECK(Contains(self.error, "不能是工具本身"));

    const items::RepairResult badRestore =
        items::Repair(bag, f.items, 0, "repair_kit", 1, 1.1f);
    CHECK_FALSE(badRestore.ok);
    CHECK(Contains(badRestore.error, "维修量无效"));
}

TEST_CASE("X-F2b-2e：配方注册表事务式追加") {
    Fixture f;
    const std::size_t baseSize = f.recipes.Size();
    std::string error;
    std::size_t added = 0;

    const std::string dynamic = R"({
      "format": "recipes-v3",
      "recipes": [
        {
          "id": "dynamic_bandage",
          "name": "动态绷带",
          "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ],
          "time": 0.3
        }
      ]
    })";
    REQUIRE(f.recipes.MergeFromString(dynamic, "dynamic", f.items, &added, &error));
    CHECK(added == 1);
    CHECK(f.recipes.Size() == baseSize + 1);
    REQUIRE(f.recipes.Find("dynamic_bandage") != nullptr);

    added = 99;
    CHECK_FALSE(f.recipes.MergeFromString(dynamic, "duplicate", f.items,
                                          &added, &error));
    CHECK(added == 0);
    CHECK(f.recipes.Size() == baseSize + 1);

    const std::string badReference = R"({
      "format": "recipes-v3",
      "recipes": [
        {
          "id": "bad_reference",
          "name": "坏引用",
          "output": { "item": "ghost", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ]
        }
      ]
    })";
    CHECK_FALSE(f.recipes.MergeFromString(badReference, "bad", f.items,
                                          &added, &error));
    CHECK(f.recipes.Find("bad_reference") == nullptr);
    CHECK(f.recipes.Size() == baseSize + 1);

    const std::string partial = R"({
      "format": "recipes-v3",
      "recipes": [
        {
          "id": "partial_recipe",
          "name": "部分配方",
          "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ]
        },
        {
          "id": "broken_recipe",
          "name": "坏配方",
          "output": { "item": "ghost", "count": 1 },
          "ingredients": [ { "item": "rag", "count": 1 } ]
        }
      ]
    })";
    CHECK_FALSE(f.recipes.MergeFromString(partial, "partial", f.items,
                                          &added, &error));
    CHECK(f.recipes.Find("partial_recipe") == nullptr);
    CHECK(f.recipes.Size() == baseSize + 1);
}

TEST_CASE("F5-T3：recipes-v4 分类、锅具与燃料") {
    static const char* kItems = R"json({
      "format": "items-v1",
      "items": [
        { "id": "pot", "name": "锅", "icon": "i", "weight": 1.0, "maxStack": 1, "type": "generic" },
        { "id": "wood", "name": "木材", "icon": "i", "weight": 0.4, "maxStack": 20, "type": "generic" },
        { "id": "food", "name": "食材", "icon": "i", "weight": 0.3, "maxStack": 10, "type": "generic" },
        { "id": "meal", "name": "炖菜", "icon": "i", "weight": 0.6, "maxStack": 3, "type": "consumable" }
      ]
    })json";
    static const char* kRecipes = R"json({
      "format": "recipes-v4",
      "recipes": [
        {
          "id": "stew",
          "name": "炖菜",
          "category": "cooking",
          "output": { "item": "meal", "count": 1 },
          "ingredients": [ { "item": "food", "count": 1 } ],
          "cookware": { "item": "pot", "count": 1 },
          "fuel": { "item": "wood", "count": 1 },
          "station": "campfire",
          "time": 1.0
        }
      ]
    })json";
    items::ItemRegistry items;
    items::RecipeRegistry recipes;
    std::string error;
    REQUIRE_MESSAGE(items.LoadFromString(kItems, "f5t3-items", &error), error);
    REQUIRE_MESSAGE(
        recipes.LoadFromString(kRecipes, "f5t3-recipes", items, &error),
        error);
    const items::RecipeDef* stew = recipes.Find("stew");
    REQUIRE(stew != nullptr);
    CHECK(stew->category == items::RecipeCategory::Cooking);
    CHECK(std::string(items::RecipeCategoryToString(stew->category)) ==
          "cooking");
    CHECK(std::string(items::RecipeCategoryLabel(stew->category)) == "烹饪");
    REQUIRE(stew->cookware.has_value());
    CHECK(stew->cookware->defId == "pot");
    CHECK(stew->cookware->count == 1);
    REQUIRE(stew->fuel.has_value());
    CHECK(stew->fuel->defId == "wood");
    CHECK(stew->fuel->count == 1);

    ecs::Container bag;
    bag.capacityKg = 20.0f;
    REQUIRE(items::AddItem(bag, items, ecs::ItemInstance{"food", 2, 1.0f}) ==
            2);
    REQUIRE(items::AddItem(bag, items, ecs::ItemInstance{"wood", 2, 1.0f}) ==
            2);
    std::string reason;
    CHECK_FALSE(items::CanCraft(bag, items, *stew, &reason));
    CHECK(Contains(reason, "锅具"));

    REQUIRE(items::AddItem(bag, items,
                           ecs::ItemInstance{"pot", 1, 0.8f}) == 1);
    CHECK(items::CanCraft(bag, items, *stew, &reason));
    CHECK(reason.empty());
    CHECK(items::ToolEfficiency(bag, *stew) == doctest::Approx(0.9f));
    const items::CraftResult crafted = items::Craft(bag, items, *stew);
    REQUIRE(crafted.ok);
    CHECK(items::CountOf(bag, "food") == 1);
    CHECK(items::CountOf(bag, "wood") == 1);
    CHECK(items::CountOf(bag, "meal") == 1);
    CHECK(items::CountOf(bag, "pot") == 1);
    CHECK(items::MaxCraftCount(bag, items, *stew, 5) == 1);

    items::RecipeRegistry invalid;
    CHECK_FALSE(invalid.LoadFromString(
        R"({ "format": "recipes-v3", "recipes": [
          { "id": "x", "name": "x", "category": "cooking",
            "output": { "item": "meal", "count": 1 },
            "ingredients": [ { "item": "food", "count": 1 } ] }
        ] })",
        "f5t3-v3", items, &error));
    CHECK(Contains(error, "recipes-v3 不支持 category"));
    CHECK_FALSE(invalid.LoadFromString(
        R"({ "format": "recipes-v4", "recipes": [
          { "id": "x", "name": "x", "category": "soup",
            "output": { "item": "meal", "count": 1 },
            "ingredients": [ { "item": "food", "count": 1 } ] }
        ] })",
        "f5t3-v4", items, &error));
    CHECK(Contains(error, "category"));
}

TEST_CASE("M24-T1：新增十个配方与工具/锅具引用") {
    const std::string root = std::string(ZL_SOURCE_DIR);
    items::ItemRegistry items;
    std::string error;
    REQUIRE_MESSAGE(items.LoadFile(root + "/assets/items.json", &error), error);
    items::RecipeRegistry recipes;
    REQUIRE_MESSAGE(recipes.LoadFile(root + "/assets/recipes.json", items,
                                     &error),
                    error);
    CHECK(recipes.Size() == 30);

    for (const char* id : {"roast_carrot", "cook_chicken", "cook_goat",
                           "carrot_stew", "chicken_stew", "goat_stew",
                           "first_aid_kit", "herbal_salve", "sledgehammer",
                           "fire_suit"}) {
        INFO("recipe id: " << id);
        REQUIRE(recipes.Find(id) != nullptr);
    }

    const items::RecipeDef* carrotStew = recipes.Find("carrot_stew");
    REQUIRE(carrotStew != nullptr);
    CHECK(carrotStew->station == "campfire");
    REQUIRE(carrotStew->cookware.has_value());
    CHECK(carrotStew->cookware->defId == "cooking_pot");
    REQUIRE(carrotStew->fuel.has_value());
    CHECK(carrotStew->fuel->defId == "wood");

    const items::RecipeDef* firstAid = recipes.Find("first_aid_kit");
    REQUIRE(firstAid != nullptr);
    CHECK(firstAid->category == items::RecipeCategory::Medical);
    CHECK(firstAid->station == "workbench");

    const items::RecipeDef* sledge = recipes.Find("sledgehammer");
    REQUIRE(sledge != nullptr);
    REQUIRE(sledge->tools.size() == 1u);
    CHECK(sledge->tools[0].defId == "toolbox");
    CHECK(sledge->tools[0].wear == doctest::Approx(0.10f));

    const items::RecipeDef* suit = recipes.Find("fire_suit");
    REQUIRE(suit != nullptr);
    REQUIRE(suit->tools.size() == 1u);
    CHECK(suit->tools[0].defId == "sewing_kit");
    CHECK(suit->outputDefId == "fire_suit");
}
