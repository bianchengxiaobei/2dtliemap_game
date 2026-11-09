# -*- coding: utf-8 -*-
"""M18-F2 饮水/烹饪/保鲜资源契约。"""

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ITEMS = ROOT / "assets" / "items.json"
RECIPES = ROOT / "assets" / "recipes.json"


class FoodDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.item_doc = json.loads(ITEMS.read_text(encoding="utf-8"))
        cls.recipe_doc = json.loads(RECIPES.read_text(encoding="utf-8"))
        cls.items = {
            item["id"]: item
            for item in cls.item_doc["items"]
        }
        cls.recipes = {
            recipe["id"]: recipe
            for recipe in cls.recipe_doc["recipes"]
        }

    def test_water_chain_is_complete(self):
        for item_id in ("empty_bottle", "dirty_water", "water_bottle"):
            self.assertIn(item_id, self.items)
        self.assertEqual(self.items["water_bottle"]["maxStack"], 1)
        self.assertEqual(
            self.items["water_bottle"]["use"]["replaceWith"], "empty_bottle"
        )
        self.assertEqual(
            self.items["dirty_water"]["use"]["replaceWith"], "empty_bottle"
        )
        self.assertGreater(self.items["dirty_water"]["use"]["healthPenalty"], 0)
        self.assertIn("fill_bottle", self.recipes)
        self.assertIn("boil_water", self.recipes)
        self.assertEqual(self.recipes["fill_bottle"]["station"], "locker")
        self.assertEqual(self.recipes["boil_water"]["station"], "campfire")

    def test_food_spoil_days_are_valid(self):
        self.assertGreater(self.items["cooked_beans"]["spoilDays"], 0)
        for item_id, item in self.items.items():
            if "spoilDays" in item:
                self.assertIsInstance(item["spoilDays"], (int, float), item_id)
                self.assertGreaterEqual(item["spoilDays"], 0, item_id)

    def test_replace_with_references_real_items(self):
        for item_id, item in self.items.items():
            target = item.get("use", {}).get("replaceWith")
            if target is not None:
                self.assertIn(target, self.items, item_id)

    def test_cooked_forage_food_and_fishing_supply_chain(self):
        for raw_id, cooked_id, recipe_id, hunger in (
            ("small_fish", "cooked_small_fish", "cook_small_fish", 0.32),
            ("wild_berries", "cooked_wild_berries", "cook_wild_berries", 0.16),
            ("wild_mushroom", "cooked_wild_mushroom", "cook_wild_mushroom", 0.2),
        ):
            self.assertIn(raw_id, self.items)
            self.assertIn(cooked_id, self.items)
            self.assertIn(recipe_id, self.recipes)
            cooked = self.items[cooked_id]
            recipe = self.recipes[recipe_id]
            self.assertEqual(recipe["output"]["item"], cooked_id)
            self.assertEqual(recipe["ingredients"][0]["item"], raw_id)
            self.assertEqual(recipe["station"], "campfire")
            self.assertGreater(cooked["weight"], 0)
            self.assertGreaterEqual(cooked["maxStack"], 1)
            self.assertGreater(cooked["spoilDays"], 0)
            self.assertAlmostEqual(cooked["use"]["hunger"], hunger)

        tea_recipe = self.recipes["brew_herbal_tea"]
        self.assertEqual(tea_recipe["output"]["item"], "herbal_tea")
        self.assertEqual(tea_recipe["station"], "campfire")
        self.assertEqual(
            {entry["item"] for entry in tea_recipe["ingredients"]},
            {"medicinal_herb", "water_bottle"},
        )
        self.assertEqual(self.items["herbal_tea"]["use"]["replaceWith"], "empty_bottle")
        self.assertGreater(self.items["herbal_tea"]["use"]["infectionSlowSeconds"], 0)

        rod = self.recipes["craft_fishing_rod"]
        self.assertEqual(rod["output"]["item"], "fishing_rod")
        self.assertEqual(rod["ingredients"][0]["item"], "forage_stick")
        bait = self.recipes["craft_fish_bait"]
        self.assertEqual(bait["output"]["item"], "fish_bait")
        self.assertEqual(bait["output"]["count"], 3)
        self.assertEqual(bait["ingredients"][0]["item"], "small_fish")

    def test_animal_meat_cooking_chain(self):
        for raw_id, cooked_id, recipe_id, hunger in (
            ("raw_venison", "cooked_venison", "cook_venison", 0.42),
            ("raw_rabbit_meat", "cooked_rabbit_meat", "cook_rabbit_meat", 0.3),
            ("raw_boar_meat", "cooked_boar_meat", "cook_boar_meat", 0.55),
        ):
            raw = self.items[raw_id]
            cooked = self.items[cooked_id]
            recipe = self.recipes[recipe_id]
            self.assertEqual(recipe["output"]["item"], cooked_id)
            self.assertEqual(recipe["ingredients"], [{"item": raw_id, "count": 1}])
            self.assertEqual(recipe["station"], "campfire")
            self.assertGreater(cooked["weight"], 0)
            self.assertEqual(cooked["maxStack"], 3)
            self.assertGreater(cooked["spoilDays"], 0)
            self.assertGreater(cooked["use"]["hunger"], raw["use"]["hunger"])
            self.assertAlmostEqual(cooked["use"]["hunger"], hunger)

    def test_animal_hide_and_bone_recipes(self):
        leather = self.recipes["craft_leather_jacket"]
        self.assertEqual(leather["output"]["item"], "leather_jacket")
        self.assertEqual(leather["station"], "workbench")
        self.assertIn(
            {"item": "animal_hide", "count": 3}, leather["ingredients"]
        )
        self.assertEqual(leather["tools"], [{"item": "kitchen_knife", "count": 1,
                                             "wear": 0.05}])

        bone = self.recipes["craft_bone_spear"]
        self.assertEqual(bone["output"]["item"], "bone_spear")
        self.assertEqual(bone["station"], "workbench")
        self.assertIn({"item": "bone", "count": 2}, bone["ingredients"])
        self.assertIn({"item": "wood", "count": 2}, bone["ingredients"])
        self.assertIn("bone_spear", self.items)
        self.assertGreater(self.items["bone_spear"]["combat"]["damage"], 0)

    def test_f5t3_categories_cookware_fuel_and_nutrition(self):
        self.assertEqual(self.recipe_doc["format"], "recipes-v4")
        self.assertEqual(len(self.item_doc["items"]), 94)
        valid_categories = {
            "general", "cooking", "preservation", "medical", "crafting",
        }
        for recipe_id, recipe in self.recipes.items():
            self.assertIn(recipe["category"], valid_categories, recipe_id)

        stew = self.recipes["cook_hearty_stew"]
        self.assertEqual(stew["category"], "cooking")
        self.assertEqual(stew["cookware"], {"item": "cooking_pot", "count": 1})
        self.assertEqual(stew["fuel"], {"item": "wood", "count": 1})
        self.assertIn("cooking_pot", self.items)
        self.assertEqual(self.items["cooking_pot"]["maxStack"], 1)

        smoked = self.recipes["smoke_boar_meat"]
        self.assertEqual(smoked["category"], "preservation")
        self.assertEqual(smoked["fuel"], {"item": "wood", "count": 2})
        self.assertIn("smoked_boar_meat", self.items)
        self.assertGreater(
            self.items["smoked_boar_meat"]["spoilDays"],
            self.items["cooked_boar_meat"]["spoilDays"],
        )

        self.assertGreater(
            self.items["raw_boar_meat"]["use"]["foodPoisonChance"], 0)
        self.assertGreater(self.items["hearty_stew"]["use"]["nutrition"], 0)
        self.assertNotIn(
            "foodPoisonChance", self.items["hearty_stew"]["use"])


if __name__ == "__main__":
    unittest.main()
