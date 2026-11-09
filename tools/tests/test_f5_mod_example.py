# -*- coding: utf-8 -*-
"""M23-F5-T6：example_content 的作物到烹饪示例契约。"""

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MOD = ROOT / "mods" / "example_content"


class F5ModExampleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.items = {
            item["id"]: item
            for item in json.loads(
                (MOD / "content" / "items.json").read_text(encoding="utf-8")
            )["items"]
        }
        cls.crop_doc = json.loads(
            (MOD / "content" / "crops.json").read_text(encoding="utf-8")
        )
        cls.recipe_doc = json.loads(
            (MOD / "content" / "recipes.json").read_text(encoding="utf-8")
        )
        cls.recipes = {
            recipe["id"]: recipe for recipe in cls.recipe_doc["recipes"]
        }
        cls.loot = json.loads(
            (MOD / "content" / "loot.json").read_text(encoding="utf-8")
        )

    def test_crop_to_cooking_chain(self):
        self.assertEqual(self.crop_doc["format"], "crops-v1")
        crops = {crop["id"]: crop for crop in self.crop_doc["crops"]}
        pumpkin = crops["mod_pumpkin"]
        self.assertEqual(pumpkin["seedItem"], "pumpkin_seeds")
        self.assertEqual(pumpkin["produceItem"], "fresh_pumpkin")
        for item_id in (pumpkin["seedItem"], pumpkin["produceItem"]):
            self.assertIn(item_id, self.items)
        self.assertGreater(self.items["fresh_pumpkin"]["use"]["nutrition"], 0)
        self.assertGreater(pumpkin["diseaseChance"], 0)

        soup = self.recipes["cook_pumpkin_soup"]
        self.assertEqual(soup["category"], "cooking")
        self.assertEqual(soup["output"]["item"], "pumpkin_soup")
        self.assertEqual(soup["cookware"],
                         {"item": "cooking_pot", "count": 1})
        self.assertEqual(soup["fuel"], {"item": "wood", "count": 1})
        soup_item = self.items["pumpkin_soup"]
        pumpkin_item = self.items["fresh_pumpkin"]
        self.assertGreater(soup_item["use"]["hunger"],
                           pumpkin_item["use"]["hunger"])
        self.assertGreater(soup_item["use"]["nutrition"],
                           pumpkin_item["use"]["nutrition"])
        self.assertGreater(soup_item["spoilDays"], 0.0)

        tables = {
            table["id"]: table
            for table in self.loot["tables"]
        }
        seeds = tables["mod_camp_cache"]["entries"]
        self.assertTrue(any(entry["defId"] == "pumpkin_seeds"
                            for entry in seeds))


if __name__ == "__main__":
    unittest.main()
