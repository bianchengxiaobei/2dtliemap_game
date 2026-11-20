# -*- coding: utf-8 -*-
"""M34-F2-T1 投掷物品与配方契约。"""

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ITEMS = ROOT / "assets" / "items.json"
RECIPES = ROOT / "assets" / "recipes.json"


class ThrowingDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.item_doc = json.loads(ITEMS.read_text(encoding="utf-8"))
        cls.recipe_doc = json.loads(RECIPES.read_text(encoding="utf-8"))
        cls.items = {item["id"]: item for item in cls.item_doc["items"]}
        cls.recipes = {
            recipe["id"]: recipe for recipe in cls.recipe_doc["recipes"]
        }

    def test_throwable_contract_and_ranges(self):
        self.assertEqual(self.item_doc["format"], "items-v1")
        self.assertEqual(len(self.item_doc["items"]), 99)
        self.assertEqual(
            set(self.items["forage_stone"]["throwable"]),
            {
                "range", "damage", "impactRadius", "arcHeight",
                "noiseRadius", "returnable", "breakChance",
            },
        )
        limits = {
            "range": (1.0, 16.0),
            "damage": (0.0, 80.0),
            "impactRadius": (0.0, 3.0),
            "arcHeight": (0.0, 3.0),
            "noiseRadius": (0.0, 32.0),
            "breakChance": (0.0, 1.0),
        }
        for item_id in (
            "forage_stone", "makeshift_spear", "bone_spear", "molotov",
        ):
            throwable = self.items[item_id]["throwable"]
            self.assertIsInstance(throwable["returnable"], bool, item_id)
            for key, (minimum, maximum) in limits.items():
                value = throwable[key]
                self.assertIsInstance(value, (int, float), (item_id, key))
                self.assertGreaterEqual(value, minimum, (item_id, key))
                self.assertLessEqual(value, maximum, (item_id, key))

    def test_molotov_fire_and_other_throwables_are_distinct(self):
        stone = self.items["forage_stone"]["throwable"]
        spear = self.items["makeshift_spear"]["throwable"]
        bone = self.items["bone_spear"]["throwable"]
        molotov = self.items["molotov"]["throwable"]
        self.assertTrue(stone["returnable"])
        self.assertTrue(spear["returnable"])
        self.assertTrue(bone["returnable"])
        self.assertFalse(molotov["returnable"])
        self.assertNotIn("fire", stone)
        self.assertNotIn("fire", spear)
        self.assertNotIn("fire", bone)
        self.assertEqual(set(molotov["fire"]), {"radius", "minutes"})
        self.assertGreater(molotov["fire"]["radius"], 0)
        self.assertGreater(molotov["fire"]["minutes"], 0)
        self.assertGreater(molotov["breakChance"], 0.9)

    def test_legacy_items_stay_non_throwable(self):
        for item_id in ("bandage", "rag", "water_bottle", "fuel_can"):
            self.assertNotIn("throwable", self.items[item_id], item_id)

    def test_molotov_recipe_uses_real_items(self):
        recipe = self.recipes["craft_molotov"]
        self.assertEqual(recipe["output"], {"item": "molotov", "count": 1})
        self.assertEqual(recipe["station"], "workbench")
        self.assertEqual(recipe["category"], "crafting")
        self.assertEqual(
            {entry["item"] for entry in recipe["ingredients"]},
            {"empty_bottle", "rag", "fuel_can"},
        )
        for entry in recipe["ingredients"]:
            self.assertIn(entry["item"], self.items, entry["item"])
            self.assertGreaterEqual(entry["count"], 1, entry["item"])


if __name__ == "__main__":
    unittest.main()
