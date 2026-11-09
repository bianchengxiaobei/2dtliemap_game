# -*- coding: utf-8 -*-
"""fire.json 可燃材料契约：范围、唯一性与基础内容覆盖。"""

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIRE = ROOT / "assets" / "fire.json"
CONFIG = ROOT / "config" / "game.json"
ITEMS = ROOT / "assets" / "items.json"
RECIPES = ROOT / "assets" / "recipes.json"


class FireDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = json.loads(FIRE.read_text(encoding="utf-8"))
        cls.materials = cls.data["materials"]
        cls.config = json.loads(CONFIG.read_text(encoding="utf-8"))["fire"]
        cls.items = {
            item["id"]: item
            for item in json.loads(ITEMS.read_text(encoding="utf-8"))["items"]
        }
        cls.recipes = {
            recipe["id"]: recipe
            for recipe in json.loads(
                RECIPES.read_text(encoding="utf-8"))["recipes"]
        }

    def test_material_contract(self):
        self.assertEqual(self.data["format"], "fire-v1")
        self.assertTrue(self.materials)
        ids = set()
        sprites = set()
        for material in self.materials:
            self.assertNotIn(material["id"], ids)
            ids.add(material["id"])
            self.assertTrue(material["name"])
            self.assertTrue(material["sprites"])
            for sprite in material["sprites"]:
                self.assertTrue(sprite)
                self.assertNotIn(sprite, sprites)
                sprites.add(sprite)
            flam = material["flammability"]
            self.assertGreaterEqual(flam["ignitionThreshold"], 0.0)
            self.assertLessEqual(flam["ignitionThreshold"], 1.0)
            self.assertGreaterEqual(flam["burnSeconds"], 1.0)
            self.assertLessEqual(flam["burnSeconds"], 7200.0)
            self.assertGreaterEqual(flam["smokePerSecond"], 0.0)
            self.assertLessEqual(flam["smokePerSecond"], 100.0)
            self.assertGreaterEqual(flam["heatRadius"], 0.0)
            self.assertLessEqual(flam["heatRadius"], 64.0)
            drops = material.get("charredDrops", [])
            self.assertLessEqual(len(drops), 8)
            seen_items = set()
            for drop in drops:
                self.assertIn(drop["item"], self.items)
                self.assertNotIn(drop["item"], seen_items)
                seen_items.add(drop["item"])
                self.assertGreaterEqual(drop["count"], 1)
                self.assertLessEqual(drop["count"], 99)

    def test_spread_config_is_bounded(self):
        cfg = self.config
        self.assertGreater(cfg["ignitionSeconds"], 0.0)
        self.assertGreaterEqual(cfg["wetnessPenalty"], 0.0)
        self.assertGreater(cfg["smolderSeconds"], 0.0)
        self.assertGreater(cfg["spreadIntervalSeconds"], 0.0)
        self.assertGreater(cfg["spreadChanceScale"], 0.0)
        self.assertLessEqual(cfg["spreadChanceScale"], 1.0)
        self.assertGreaterEqual(cfg["rainBurnDecayScale"], 0.0)
        self.assertLessEqual(cfg["rainBurnDecayScale"], 10.0)
        self.assertGreater(cfg["maxActiveSources"], 0)
        self.assertGreaterEqual(cfg["maxSpreadsPerTick"], 0)
        self.assertGreater(cfg["extinguishRange"], 0.0)
        self.assertGreaterEqual(cfg["bareHandsChance"], 0.0)
        self.assertGreater(cfg["fireDamagePerSecond"], 0.0)
        self.assertGreater(cfg["fireDamageRadius"], 0.0)
        self.assertGreater(cfg["fireDamageIntervalSeconds"], 0.0)
        self.assertGreater(cfg["zombieBurnSeconds"], 0.0)
        self.assertGreater(cfg["zombieBurnDamagePerSecond"], 0.0)
        self.assertGreater(cfg["zombieBurnDamageIntervalSeconds"], 0.0)
        self.assertGreater(cfg["zombieSpreadIntervalSeconds"], 0.0)
        self.assertGreaterEqual(cfg["zombieSpreadChance"], 0.0)
        self.assertLessEqual(cfg["zombieSpreadChance"], 1.0)
        self.assertGreaterEqual(cfg["zombiePanicSeconds"], 0.0)
        self.assertGreater(cfg["zombiePanicSpeedScale"], 1.0)
        self.assertGreater(cfg["zombiePanicChaseRadius"], 0.0)

    def test_extinguish_items_and_recipe(self):
        extinguisher = self.items["fire_extinguisher"]["use"]
        water = self.items["water_bottle"]["use"]
        wet_rag = self.items["wet_rag"]["use"]
        self.assertGreater(extinguisher["extinguish"], water["extinguish"])
        self.assertGreater(water["extinguish"], 0.0)
        self.assertGreater(wet_rag["extinguish"], 0.0)
        self.assertGreater(wet_rag["extinguishWear"], 0.0)
        self.assertEqual(water["replaceWith"], "empty_bottle")
        self.assertIn("wet_rag", self.items)
        self.assertEqual(
            self.recipes["craft_wet_rag"]["output"]["item"], "wet_rag")

    def test_default_sources_and_profiles(self):
        by_sprite = {
            sprite: material
            for material in self.materials
            for sprite in material["sprites"]
        }
        expected = {
            "object.campfire": "campfire",
            "object.wall_h": "wood_wall",
            "object.wall_v": "wood_wall",
            "tile.floor.wood": "wood_floor",
            "object.tree": "tree",
            "decor.grass_tuft": "grass_tuft",
        }
        for sprite, material_id in expected.items():
            self.assertIn(sprite, by_sprite)
            self.assertEqual(by_sprite[sprite]["id"], material_id)

        wall = by_sprite["object.wall_h"]["flammability"]
        floor = by_sprite["tile.floor.wood"]["flammability"]
        tree = by_sprite["object.tree"]["flammability"]
        self.assertGreater(wall["ignitionThreshold"], floor["ignitionThreshold"])
        self.assertGreater(wall["burnSeconds"], floor["burnSeconds"])
        self.assertGreater(tree["burnSeconds"], wall["burnSeconds"])
        self.assertEqual(by_sprite["object.campfire"]["flammability"]["ignitionThreshold"], 0.0)


if __name__ == "__main__":
    unittest.main()
