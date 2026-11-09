# -*- coding: utf-8 -*-
"""forage.json 资源契约：区域、水面、战利品表与物品引用。"""

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FORAGE = ROOT / "assets" / "forage.json"
LOOT = ROOT / "assets" / "loot.json"
ITEMS = ROOT / "assets" / "items.json"


class ForageDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.forage = json.loads(FORAGE.read_text(encoding="utf-8"))
        cls.loot = json.loads(LOOT.read_text(encoding="utf-8"))
        cls.items = {
            item["id"]: item
            for item in json.loads(ITEMS.read_text(encoding="utf-8"))["items"]
        }
        cls.tables = {table["id"]: table for table in cls.loot["tables"]}

    def test_format_and_seed(self):
        self.assertEqual(self.forage["format"], "forage-v1")
        self.assertIsInstance(self.forage["seed"], int)
        self.assertGreaterEqual(self.forage["seed"], 0)
        self.assertIsInstance(self.forage["forageZones"], list)
        self.assertIsInstance(self.forage["fishingSpots"], list)
        self.assertTrue(self.forage["forageZones"])
        self.assertTrue(self.forage["fishingSpots"])

    def test_zone_contract(self):
        ids = set()
        for zone in self.forage["forageZones"]:
            self.assertNotIn(zone["id"], ids)
            ids.add(zone["id"])
            self.assertTrue(zone["name"])
            self.assertIn(zone["tableId"], self.tables)
            self.assertTrue(zone["floors"])
            self.assertGreaterEqual(zone["density"], 0.0)
            self.assertLessEqual(zone["density"], 1.0)
            self.assertGreater(zone["maxNodes"], 0)
            self.assertGreaterEqual(zone["spacing"], 0)
            self.assertGreaterEqual(zone["searchSeconds"], 0.5)
            self.assertLessEqual(zone["searchSeconds"], 30.0)
            self.assertGreaterEqual(zone["survivalXp"], 0.0)
            self.assertLessEqual(zone["survivalXp"], 200.0)
            tool_id = zone.get("toolDefId")
            if tool_id:
                self.assertIn(tool_id, self.items)
                self.assertGreater(zone.get("toolWear", 0.0), 0.0)
                self.assertLessEqual(zone["toolWear"], 1.0)
            else:
                self.assertEqual(zone.get("toolWear", 0.0), 0.0)
            required = set(zone.get("requiredNearbySprites", []))
            excluded = set(zone.get("excludedNearbySprites", []))
            self.assertFalse(required & excluded)
        self.assertIn("forest_understory", ids)
        self.assertIn("meadow_grass", ids)
        meadow = next(
            zone for zone in self.forage["forageZones"] if zone["id"] == "meadow_grass"
        )
        self.assertEqual(meadow["toolDefId"], "forage_stick")

    def test_fishing_contract(self):
        ids = set()
        for spot in self.forage["fishingSpots"]:
            self.assertNotIn(spot["id"], ids)
            ids.add(spot["id"])
            self.assertTrue(spot["waterFloors"])
            self.assertIn(spot["tableId"], self.tables)
            self.assertIn(spot["trashTableId"], self.tables)
            self.assertGreaterEqual(spot["bankRadius"], 1)
            self.assertGreater(spot["maxSpots"], 0)
            self.assertGreaterEqual(spot["density"], 0.0)
            self.assertLessEqual(spot["density"], 1.0)
            self.assertGreaterEqual(spot["waitSeconds"], 1.0)
            self.assertLessEqual(spot["waitSeconds"], 60.0)
            self.assertGreaterEqual(spot["biteWindowSeconds"], 0.5)
            self.assertLessEqual(spot["biteWindowSeconds"], 10.0)
            self.assertGreaterEqual(spot["catchChance"], 0.05)
            self.assertLessEqual(spot["catchChance"], 0.95)
            self.assertGreaterEqual(spot["survivalXp"], 0.0)
            self.assertLessEqual(spot["survivalXp"], 200.0)
            self.assertIn(spot["rodDefId"], self.items)
            self.assertGreater(spot["rodWear"], 0.0)
            self.assertLessEqual(spot["rodWear"], 1.0)
            self.assertIn(spot["baitDefId"], self.items)
            self.assertGreaterEqual(spot["baitCost"], 1)
        self.assertIn("still_water", ids)

    def test_map_contains_fishable_water(self):
        meta = json.loads(
            (ROOT / "assets" / "maps" / "village_demo" / "map.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(meta["legend"].get("a"), "tile.floor.water")
        water_rows = []
        chunks = ROOT / "assets" / "maps" / "village_demo" / "chunks"
        for path in chunks.rglob("chunk_*.json"):
            data = json.loads(path.read_text(encoding="utf-8"))
            for row in data.get("floors", []):
                if "a" in row:
                    water_rows.append(row)
        self.assertTrue(water_rows)

    def test_tables_reference_real_items(self):
        for table_id in (
            "forage_forest",
            "forage_meadow",
            "forage_shore",
            "fish_water",
            "fish_water_trash",
        ):
            table = self.tables[table_id]
            for entry in table.get("guaranteed", []):
                self.assertIn(entry["defId"], self.items)
            for entry in table.get("entries", []):
                self.assertIn(entry["defId"], self.items)
                self.assertGreaterEqual(entry["weight"], 1)
                self.assertLessEqual(entry["min"], entry["max"])

    def test_expected_food_output_rate(self):
        raw_hunger = {
            item_id: float(item.get("use", {}).get("hunger", 0.0))
            for item_id, item in self.items.items()
        }
        cooked_hunger = dict(raw_hunger)
        for raw_id, cooked_id in (
            ("wild_berries", "cooked_wild_berries"),
            ("wild_mushroom", "cooked_wild_mushroom"),
            ("small_fish", "cooked_small_fish"),
        ):
            cooked_hunger[raw_id] = raw_hunger[cooked_id]

        def expected_hunger(table_id, hunger_by_id):
            table = self.tables[table_id]
            total_weight = sum(entry["weight"] for entry in table["entries"])
            return sum(
                entry["weight"]
                / total_weight
                * (entry["min"] + entry["max"])
                / 2.0
                * table["rolls"]
                * hunger_by_id.get(entry["defId"], 0.0)
                for entry in table["entries"]
            )

        self.assertAlmostEqual(
            expected_hunger("forage_forest", raw_hunger), 0.0662, delta=1e-4
        )
        self.assertAlmostEqual(
            expected_hunger("forage_meadow", raw_hunger), 0.0600, delta=1e-4
        )
        self.assertAlmostEqual(
            expected_hunger("forage_forest", cooked_hunger), 0.1723, delta=1e-4
        )
        self.assertAlmostEqual(
            expected_hunger("forage_meadow", cooked_hunger), 0.1600, delta=1e-4
        )

        spot = next(
            spot
            for spot in self.forage["fishingSpots"]
            if spot["id"] == "still_water"
        )
        self.assertAlmostEqual(
            expected_hunger("fish_water", raw_hunger) * spot["catchChance"],
            0.1116,
            delta=1e-4,
        )
        self.assertAlmostEqual(
            expected_hunger("fish_water", cooked_hunger) * spot["catchChance"],
            0.2976,
            delta=1e-4,
        )

    def test_forage_items_exist(self):
        for item_id in (
            "forage_stick",
            "forage_stone",
            "wild_berries",
            "wild_mushroom",
            "medicinal_herb",
            "small_fish",
            "fish_bait",
            "fishing_rod",
            "river_trash",
        ):
            self.assertIn(item_id, self.items)
        meadow_entries = {
            entry["defId"] for entry in self.tables["forage_meadow"]["entries"]
        }
        self.assertIn("fish_bait", meadow_entries)


if __name__ == "__main__":
    unittest.main()
