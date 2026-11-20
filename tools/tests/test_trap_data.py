# -*- coding: utf-8 -*-
"""traps-v1 数据契约：建造引用、触发掩码、效果字段与内置数值。"""

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TRAPS = ROOT / "assets" / "traps.json"
CONFIG = ROOT / "config" / "game.json"


class TrapDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = json.loads(TRAPS.read_text(encoding="utf-8"))
        cls.config = json.loads(CONFIG.read_text(encoding="utf-8"))
        cls.by_id = {entry["id"]: entry for entry in cls.data["defs"]}
        cls.buildings = {
            entry["id"]: entry for entry in cls.config["building"]["types"]
        }

    def test_format_and_builtin_ids(self):
        self.assertEqual(self.data["format"], "traps-v1")
        self.assertTrue(self.config["trapDefs"]["enabled"])
        self.assertEqual(self.config["trapDefs"]["file"], "assets/traps.json")
        self.assertEqual(
            set(self.by_id), {"bear_trap", "tripwire_alarm", "fuel_trap"}
        )

    def test_building_references_and_costs(self):
        for trap_id, cost in (
            ("bear_trap", 3),
            ("tripwire_alarm", 1),
            ("fuel_trap", 4),
        ):
            entry = self.by_id[trap_id]
            self.assertEqual(entry["buildingId"], trap_id)
            self.assertIn(entry["buildingId"], self.buildings)
            self.assertEqual(self.buildings[entry["buildingId"]]["woodCost"], cost)
            self.assertEqual(
                self.buildings[entry["buildingId"]]["sprite"], "object.trap"
            )

    def test_trigger_mask_and_effects(self):
        bear = self.by_id["bear_trap"]
        self.assertEqual(bear["damage"], 55.0)
        self.assertEqual(bear["uses"], 4)
        self.assertEqual(bear["staggerSeconds"], 2.0)
        self.assertEqual(set(bear["triggerMask"]), {"zombie", "animal"})
        self.assertEqual(bear["armSeconds"], 1.0)

        tripwire = self.by_id["tripwire_alarm"]
        self.assertEqual(tripwire["damage"], 0.0)
        self.assertEqual(tripwire["noiseRadius"], 18.0)
        self.assertEqual(tripwire["uses"], 8)
        self.assertEqual(set(tripwire["triggerMask"]), {"zombie", "animal"})

        fuel = self.by_id["fuel_trap"]
        self.assertEqual(fuel["uses"], 1)
        self.assertEqual(fuel["fire"]["radius"], 1.5)
        self.assertEqual(fuel["fire"]["minutes"], 45.0)
        self.assertEqual(set(fuel["triggerMask"]), {"zombie", "animal"})
        self.assertEqual(fuel["armSeconds"], 1.0)

    def test_all_defs_have_effect_and_arm_time(self):
        for entry in self.data["defs"]:
            self.assertGreaterEqual(entry["armSeconds"], 0.0)
            self.assertGreaterEqual(entry["cooldownSeconds"], 0.0)
            has_effect = (
                entry["damage"] > 0.0
                or entry["noiseRadius"] > 0.0
                or "fire" in entry
            )
            self.assertTrue(has_effect, entry["id"])


if __name__ == "__main__":
    unittest.main()
