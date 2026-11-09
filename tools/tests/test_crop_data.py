# -*- coding: utf-8 -*-
"""crops.json 作物契约：阶段、水耗、病害、产量、种子与物品引用。"""

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CROPS = ROOT / "assets" / "crops.json"
ITEMS = ROOT / "assets" / "items.json"
CONFIG = ROOT / "config" / "game.json"


class CropDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = json.loads(CROPS.read_text(encoding="utf-8"))
        cls.crops = cls.data["crops"]
        cls.config = json.loads(CONFIG.read_text(encoding="utf-8"))
        cls.items = {
            item["id"]: item
            for item in json.loads(ITEMS.read_text(encoding="utf-8"))["items"]
        }

    def test_format_and_identity(self):
        self.assertEqual(self.data["format"], "crops-v1")
        self.assertEqual(self.config["crops"]["file"], "assets/crops.json")
        self.assertEqual(len(self.crops), 5)
        ids = {crop["id"] for crop in self.crops}
        seeds = {crop["seedItem"] for crop in self.crops}
        self.assertEqual(ids, {"cabbage", "potato", "tomato", "corn", "carrot"})
        self.assertEqual(len(seeds), len(self.crops))

    def test_item_and_stage_contract(self):
        for crop in self.crops:
            self.assertIn(crop["seedItem"], self.items)
            self.assertIn(crop["produceItem"], self.items)
            self.assertNotEqual(crop["seedItem"], crop["produceItem"])
            self.assertGreaterEqual(crop["growMinutes"], 10.0)
            self.assertGreaterEqual(crop["waterMinutes"], 1.0)
            self.assertGreaterEqual(crop["maxWaterMinutes"],
                                    crop["waterMinutes"])
            self.assertGreaterEqual(crop["witherMinutes"], 1.0)
            self.assertGreaterEqual(crop["waterUsePerMinute"], 0.1)
            self.assertLessEqual(crop["waterUsePerMinute"], 5.0)
            self.assertGreaterEqual(crop["diseaseChance"], 0.0)
            self.assertLessEqual(crop["diseaseChance"], 1.0)
            self.assertGreaterEqual(crop["harvestYield"], 1)
            self.assertGreaterEqual(crop["seedYield"], 0)
            stages = crop["stages"]
            self.assertGreaterEqual(len(stages), 2)
            self.assertLessEqual(len(stages), 6)
            self.assertAlmostEqual(stages[0]["start"], 0.0)
            self.assertAlmostEqual(stages[-1]["start"], 1.0)
            starts = [stage["start"] for stage in stages]
            self.assertEqual(starts, sorted(starts))
            self.assertEqual(len(starts), len(set(starts)))
            for stage in stages:
                self.assertTrue(stage["name"])

    def test_crops_have_tradeoffs(self):
        by_id = {crop["id"]: crop for crop in self.crops}
        self.assertLess(by_id["cabbage"]["growMinutes"],
                        by_id["corn"]["growMinutes"])
        self.assertGreater(by_id["corn"]["harvestYield"],
                           by_id["cabbage"]["harvestYield"])
        self.assertGreater(by_id["tomato"]["diseaseChance"],
                           by_id["cabbage"]["diseaseChance"])
        for crop in self.crops:
            produce = self.items[crop["produceItem"]]
            self.assertIn("use", produce)
            self.assertGreater(produce["use"].get("hunger", 0.0), 0.0)


    def test_f5_t2_farm_supplies_and_season_config(self):
        self.assertIn("fertilizer", self.items)
        self.assertIn("fungicide", self.items)
        farming = self.config["farming"]
        self.assertEqual(farming["fertilizerItem"], "fertilizer")
        self.assertEqual(farming["fungicideItem"], "fungicide")
        self.assertGreater(farming["fertilizerYieldBonus"], 0)
        self.assertGreaterEqual(farming["diseaseDryFraction"], 0.05)
        self.assertLessEqual(farming["diseaseDryFraction"], 0.9)
        self.assertGreaterEqual(farming["diseaseYieldPenalty"], 0.0)
        self.assertLessEqual(farming["diseaseYieldPenalty"], 0.9)
        self.assertGreater(farming["rainWaterPerMinute"], 0.0)
        season = farming["season"]
        for key in ("springGrowth", "summerGrowth", "autumnGrowth",
                    "winterGrowth", "springDisease", "summerDisease",
                    "autumnDisease", "winterDisease"):
            self.assertGreater(season[key], 0.0)
        self.assertLess(season["winterGrowth"], season["summerGrowth"])
        self.assertGreater(season["summerDisease"], season["winterDisease"])


if __name__ == "__main__":
    unittest.main()
