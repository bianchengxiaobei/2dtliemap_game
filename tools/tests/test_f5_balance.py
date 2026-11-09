# -*- coding: utf-8 -*-
"""M23-F5-T5：作物周期取舍、烹饪产物价值与食物安全参数回归。"""

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ITEMS = ROOT / "assets" / "items.json"
CROPS = ROOT / "assets" / "crops.json"
RECIPES = ROOT / "assets" / "recipes.json"
CONFIG = ROOT / "config" / "game.json"


class F5BalanceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.item_doc = json.loads(ITEMS.read_text(encoding="utf-8"))
        cls.items = {item["id"]: item for item in cls.item_doc["items"]}
        cls.crops = {
            crop["id"]: crop
            for crop in json.loads(CROPS.read_text(encoding="utf-8"))["crops"]
        }
        cls.recipe_doc = json.loads(RECIPES.read_text(encoding="utf-8"))
        cls.recipes = {
            recipe["id"]: recipe
            for recipe in cls.recipe_doc["recipes"]
        }
        cls.config = json.loads(CONFIG.read_text(encoding="utf-8"))

    def test_crop_cycle_tradeoffs(self):
        self.assertEqual(set(self.crops),
                         {"cabbage", "potato", "tomato", "corn", "carrot"})
        fastest = min(self.crops.values(), key=lambda crop: crop["growMinutes"])
        slowest = max(self.crops.values(), key=lambda crop: crop["growMinutes"])
        self.assertEqual(fastest["id"], "cabbage")
        self.assertEqual(slowest["id"], "corn")

        highest_yield = max(self.crops.values(),
                            key=lambda crop: crop["harvestYield"])
        highest_disease = max(self.crops.values(),
                              key=lambda crop: crop["diseaseChance"])
        highest_seed_yield = max(self.crops.values(),
                                 key=lambda crop: crop["seedYield"])
        self.assertEqual(highest_yield["id"], "corn")
        self.assertEqual(highest_disease["id"], "tomato")
        self.assertEqual(highest_seed_yield["id"], "corn")

        for crop in self.crops.values():
            produce = self.items[crop["produceItem"]]
            self.assertIn("use", produce)
            self.assertGreater(produce["use"].get("hunger", 0.0), 0.0)
            self.assertGreater(produce["use"].get("nutrition", 0.0), 0.0)
            self.assertGreater(produce.get("spoilDays", 0.0), 0.0)
            self.assertGreaterEqual(crop["seedYield"], 1)
            self.assertGreaterEqual(crop["maxWaterMinutes"],
                                    crop["waterMinutes"])
            self.assertGreaterEqual(crop["growMinutes"] / 60.0, 3.0)
            self.assertLessEqual(crop["growMinutes"] / 60.0, 8.0)

        water_per_produce = {
            crop["id"]: crop["growMinutes"] * crop["waterUsePerMinute"] /
            crop["harvestYield"]
            for crop in self.crops.values()
        }
        self.assertEqual(max(water_per_produce, key=water_per_produce.get),
                         "corn")
        total_nutrition = {
            crop["id"]: crop["harvestYield"] *
            self.items[crop["produceItem"]]["use"]["nutrition"]
            for crop in self.crops.values()
        }
        self.assertEqual(max(total_nutrition, key=total_nutrition.get), "corn")
        self.assertEqual(min(total_nutrition, key=total_nutrition.get),
                         "cabbage")

    def test_cooked_meal_value_and_spoilage(self):
        stew = self.recipes["cook_hearty_stew"]
        self.assertEqual(stew["category"], "cooking")
        self.assertEqual(stew["cookware"], {"item": "cooking_pot", "count": 1})
        self.assertEqual(stew["fuel"], {"item": "wood", "count": 1})

        ingredients = {entry["item"]: entry["count"]
                       for entry in stew["ingredients"]}
        potato = self.items["fresh_potato"]
        cabbage = self.items["fresh_cabbage"]
        stew_item = self.items[stew["output"]["item"]]
        self.assertEqual(ingredients["fresh_potato"], 1)
        self.assertEqual(ingredients["fresh_cabbage"], 1)

        input_hunger = (potato["use"]["hunger"] + cabbage["use"]["hunger"])
        input_nutrition = (potato["use"]["nutrition"] +
                           cabbage["use"]["nutrition"])
        self.assertGreater(stew_item["use"]["hunger"], input_hunger)
        self.assertGreaterEqual(stew_item["use"]["nutrition"],
                                input_nutrition - 1e-9)
        self.assertGreater(stew_item["use"].get("heal", 0.0), 0.0)
        self.assertGreaterEqual(stew_item["spoilDays"],
                                max(potato["spoilDays"],
                                    cabbage["spoilDays"]))
        self.assertEqual(stew_item["use"].get("foodPoisonChance", 0.0), 0.0)

        raw = self.items["raw_boar_meat"]
        cooked = self.items["cooked_boar_meat"]
        smoked = self.items["smoked_boar_meat"]
        self.assertGreater(raw["use"]["foodPoisonChance"], 0.0)
        self.assertGreater(smoked["use"]["hunger"], cooked["use"]["hunger"])
        self.assertGreater(smoked["use"]["nutrition"],
                           cooked["use"]["nutrition"])
        self.assertGreater(smoked["spoilDays"], cooked["spoilDays"])
        self.assertEqual(smoked["use"].get("foodPoisonChance", 0.0), 0.0)

    def test_food_safety_bounds(self):
        food = self.config["food"]
        player_hp = self.config["health"]["playerMaxHp"]
        poison_duration = food["poisonDurationSeconds"]
        poison_dps = food["poisonDamagePerSecond"]
        self.assertGreater(poison_duration, 0.0)
        self.assertLessEqual(poison_duration, 120.0)
        self.assertGreater(poison_dps, 0.0)
        self.assertLessEqual(poison_dps, 1.0)
        self.assertLess(poison_duration * poison_dps, player_hp * 0.25)
        self.assertGreater(food["nutritionDecayPerDay"], 0.0)
        self.assertLessEqual(food["nutritionDecayPerDay"], 1.0)
        self.assertGreater(food["nutritionPoisonReduction"], 0.0)
        self.assertLessEqual(food["nutritionPoisonReduction"], 1.0)
        self.assertGreaterEqual(food["spoiledPoisonChance"], 0.5)

        farming = self.config["farming"]
        season = farming["season"]
        self.assertLess(season["winterGrowth"], season["summerGrowth"])
        self.assertGreater(season["summerDisease"], season["winterDisease"])
        self.assertGreater(farming["rainWaterPerMinute"], 0.0)


if __name__ == "__main__":
    unittest.main()
