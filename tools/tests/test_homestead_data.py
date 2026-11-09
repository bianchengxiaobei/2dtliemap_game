# -*- coding: utf-8 -*-
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "config" / "game.json"
ITEMS = ROOT / "assets" / "items.json"
LOOT = ROOT / "assets" / "loot.json"
MANIFEST = ROOT / "assets" / "asset_manifest.json"


class HomesteadDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = json.loads(CONFIG.read_text(encoding="utf-8"))
        cls.items = {
            item["id"]: item
            for item in json.loads(ITEMS.read_text(encoding="utf-8"))["items"]
        }
        cls.loot = json.loads(LOOT.read_text(encoding="utf-8"))
        cls.manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))

    def test_config_enables_and_bounds_systems(self):
        farming = self.config["farming"]
        self.assertTrue(farming["enabled"])
        self.assertGreater(farming["growMinutes"], 0)
        self.assertGreater(farming["waterMinutes"], 0)
        self.assertGreaterEqual(farming["maxWaterMinutes"], farming["waterMinutes"])
        self.assertIn(farming["seedItem"], self.items)
        self.assertIn(farming["produceItem"], self.items)
        self.assertIn(farming["waterItem"], self.items)

        power = self.config["power"]
        self.assertTrue(power["enabled"])
        self.assertGreater(power["fuelMinutesPerCan"], 0)
        self.assertIn(power["fuelItem"], self.items)
        self.assertGreater(power["baseLoad"], 0)
        self.assertGreater(power["floodlightRadius"], 0)
        self.assertGreater(power["attractRadius"], 0)
        self.assertGreater(power["electricFenceDamage"], 0)
        self.assertGreater(power["electricFenceCooldownSeconds"], 0)
        self.assertGreater(power["lowFuelWarningMinutes"], 0)

        traps = self.config["traps"]
        self.assertTrue(traps["enabled"])
        self.assertGreater(traps["damage"], 0)
        self.assertGreater(traps["uses"], 0)

        trading = self.config["trading"]
        self.assertTrue(trading["enabled"])
        self.assertGreaterEqual(trading["restockDays"], 1)

    def test_buildables_and_item_icons_resolve(self):
        by_id = {
            entry["id"]: entry for entry in self.config["building"]["types"]
        }
        for build_id, sprite in {
            "farm_plot": "object.farm_plot",
            "generator": "object.generator",
            "fridge": "object.fridge",
            "spike_trap": "object.trap",
            "floodlight": "object.floodlight",
            "electric_fence": "object.electric_fence",
        }.items():
            self.assertIn(build_id, by_id)
            self.assertEqual(by_id[build_id]["sprite"], sprite)
            self.assertIn(sprite, self.manifest["logical"])
            self.assertTrue(self.manifest["logical"][sprite]["own"]["frames"])

        for item_id in ("cabbage_seeds", "fresh_cabbage", "fuel_can"):
            self.assertIn(item_id, self.items)
            icon = self.items[item_id]["icon"]
            self.assertIn(icon, self.manifest["logical"])

        loot_ids = {
            entry["defId"]
            for table in self.loot["tables"]
            for group in ("guaranteed", "entries")
            for entry in table.get(group, [])
        }
        self.assertIn("cabbage_seeds", loot_ids)
        self.assertIn("fuel_can", loot_ids)


if __name__ == "__main__":
    unittest.main()
