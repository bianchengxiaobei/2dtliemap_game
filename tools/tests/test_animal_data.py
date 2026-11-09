# -*- coding: utf-8 -*-
"""animals.json 动物契约：速度/警觉/伤害、尸体与掉落表引用。"""

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ANIMALS = ROOT / "assets" / "animals.json"
CONFIG = ROOT / "config" / "game.json"
ITEMS = ROOT / "assets" / "items.json"
LOOT = ROOT / "assets" / "loot.json"


class AnimalDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = json.loads(ANIMALS.read_text(encoding="utf-8"))
        cls.animals = cls.data["animals"]
        cls.config = json.loads(CONFIG.read_text(encoding="utf-8"))["animals"]
        cls.pois = {
            poi["id"]
            for poi in json.loads(CONFIG.read_text(encoding="utf-8"))["pois"]["defs"]
        }
        cls.items = {
            item["id"]: item
            for item in json.loads(ITEMS.read_text(encoding="utf-8"))["items"]
        }
        cls.loot = {
            table["id"]: table
            for table in json.loads(LOOT.read_text(encoding="utf-8"))["tables"]
        }

    def test_species_contract(self):
        self.assertEqual(self.data["format"], "animals-v1")
        self.assertTrue(self.animals)
        ids = set()
        for animal in self.animals:
            self.assertNotIn(animal["id"], ids)
            ids.add(animal["id"])
            self.assertTrue(animal["name"])
            self.assertIn(animal["kind"], ("passive", "dangerous"))
            self.assertTrue(animal["sprite"])
            self.assertGreaterEqual(animal["maxHealth"], 1.0)
            self.assertLessEqual(animal["maxHealth"], 10000.0)
            self.assertGreaterEqual(animal["wanderSpeed"], 0.1)
            self.assertLessEqual(animal["wanderSpeed"], 20.0)
            self.assertGreaterEqual(animal["fleeSpeed"], animal["wanderSpeed"])
            self.assertLessEqual(animal["fleeSpeed"], 30.0)
            self.assertGreaterEqual(animal["alertRadius"], 0.1)
            self.assertLessEqual(animal["alertRadius"], 64.0)
            self.assertGreaterEqual(animal["fleeRadius"], 0.1)
            self.assertLessEqual(animal["fleeRadius"], 64.0)
            self.assertGreaterEqual(animal["hearingRadius"], 0.0)
            self.assertLessEqual(animal["hearingRadius"], 128.0)
            self.assertGreaterEqual(animal["fleeSeconds"], 0.1)
            self.assertLessEqual(animal["fleeSeconds"], 60.0)
            if animal["kind"] == "dangerous":
                self.assertGreater(animal["damage"], 0.0)
                self.assertGreater(animal["attackRange"], 0.0)
                self.assertGreater(animal["attackCooldown"], 0.0)
            else:
                self.assertEqual(animal["damage"], 0.0)
                self.assertEqual(animal["attackRange"], 0.0)
                self.assertEqual(animal["attackCooldown"], 0.0)

    def test_corpse_and_loot_references(self):
        self.assertEqual(self.config["file"], "assets/animals.json")
        for animal in self.animals:
            self.assertTrue(animal["corpseSprite"])
            self.assertGreaterEqual(animal["corpseSearchSeconds"], 0.1)
            self.assertLessEqual(animal["corpseSearchSeconds"], 30.0)
            if "corpseTool" in animal:
                self.assertIn(animal["corpseTool"], self.items)
                self.assertGreaterEqual(animal["corpseToolWear"], 0.0)
                self.assertLessEqual(animal["corpseToolWear"], 1.0)
            table = self.loot.get(animal["lootTableId"])
            self.assertIsNotNone(table, animal["id"])
            for entry in table.get("entries", []):
                self.assertIn(entry["defId"], self.items)
                item = self.items[entry["defId"]]
                self.assertGreaterEqual(entry["min"], 1)
                self.assertLessEqual(entry["min"], entry["max"])
                self.assertLessEqual(entry["max"], item["maxStack"])

    def test_spawn_contract(self):
        spawn = self.config["spawn"]
        self.assertTrue(spawn["enabled"])
        self.assertGreaterEqual(spawn["maxLive"], 1)
        self.assertGreater(spawn["minPlayerDistance"], 0.0)
        self.assertGreater(spawn["poiRadius"], 0.0)
        for animal in self.animals:
            rule = animal.get("spawn")
            self.assertIsNotNone(rule, animal["id"])
            for key in ("forest", "meadow", "poi"):
                self.assertGreaterEqual(rule[key], 0.0)
                self.assertLessEqual(rule[key], 1.0)
            weight = rule["forest"] + rule["meadow"] + rule["poi"]
            if weight > 0.0:
                self.assertGreaterEqual(rule["maxPerChunk"], 1)
                self.assertLessEqual(rule["maxPerChunk"], 8)
            else:
                self.assertEqual(rule["maxPerChunk"], 0)
            for poi_id in rule.get("poiIds", []):
                self.assertIn(poi_id, self.pois)
        boar = next(animal for animal in self.animals if animal["id"] == "boar")
        self.assertEqual(boar["spawn"]["poiIds"], ["farm_barn"])

    def test_expected_species_and_balance(self):
        by_id = {animal["id"]: animal for animal in self.animals}
        self.assertEqual(set(by_id),
                         {"deer", "rabbit", "boar", "chicken", "goat"})
        self.assertEqual(by_id["deer"]["kind"], "passive")
        self.assertEqual(by_id["rabbit"]["kind"], "passive")
        self.assertEqual(by_id["boar"]["kind"], "dangerous")
        self.assertEqual(by_id["chicken"]["kind"], "passive")
        self.assertEqual(by_id["goat"]["kind"], "dangerous")
        self.assertGreater(by_id["rabbit"]["fleeSpeed"],
                           by_id["deer"]["fleeSpeed"])
        self.assertGreater(by_id["boar"]["maxHealth"],
                           by_id["deer"]["maxHealth"])
        self.assertGreater(by_id["boar"]["damage"], 0.0)
        self.assertGreater(by_id["goat"]["damage"], 0.0)
        meat = {
            "deer": "raw_venison",
            "rabbit": "raw_rabbit_meat",
            "boar": "raw_boar_meat",
            "chicken": "raw_chicken",
            "goat": "raw_goat_meat",
        }
        for species, item_id in meat.items():
            self.assertIn(item_id, self.items)
            self.assertIn(
                item_id,
                {entry["defId"] for entry in
                 self.loot[by_id[species]["lootTableId"]]["entries"]},
            )


    def test_hunting_yield_balance(self):
        by_id = {animal["id"]: animal for animal in self.animals}
        meat = {
            "deer": ("raw_venison", "cooked_venison"),
            "rabbit": ("raw_rabbit_meat", "cooked_rabbit_meat"),
            "boar": ("raw_boar_meat", "cooked_boar_meat"),
            "chicken": ("raw_chicken", "cooked_chicken"),
            "goat": ("raw_goat_meat", "cooked_goat_meat"),
        }
        expected_meat = {}
        for species, (raw_id, cooked_id) in meat.items():
            table = self.loot[by_id[species]["lootTableId"]]
            total_weight = sum(entry["weight"] for entry in table["entries"])
            entry = next(
                item for item in table["entries"] if item["defId"] == raw_id
            )
            expected_meat[species] = (
                table["rolls"] * entry["weight"] / total_weight * entry["min"]
            )
            self.assertGreater(
                self.items[cooked_id]["use"]["hunger"],
                self.items[raw_id]["use"]["hunger"],
            )
        self.assertGreaterEqual(expected_meat["rabbit"], 1.0)
        self.assertGreaterEqual(expected_meat["deer"], 2.5)
        self.assertGreaterEqual(expected_meat["boar"], 3.5)
        self.assertGreaterEqual(expected_meat["chicken"], 1.0)
        self.assertGreaterEqual(expected_meat["goat"], 2.5)
        self.assertGreater(expected_meat["boar"], expected_meat["deer"])
        for species in ("deer", "boar", "goat"):
            table = self.loot[by_id[species]["lootTableId"]]
            ids = {entry["defId"] for entry in table["entries"]}
            self.assertIn("animal_hide", ids)
            self.assertIn("bone", ids)

if __name__ == "__main__":
    unittest.main()
