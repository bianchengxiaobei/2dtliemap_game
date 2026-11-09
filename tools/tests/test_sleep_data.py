# -*- coding: utf-8 -*-
"""M18-F3 睡眠/床位资源契约。"""

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "config" / "game.json"
MANIFEST = ROOT / "assets" / "asset_manifest.json"


class SleepDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = json.loads(CONFIG.read_text(encoding="utf-8"))
        cls.manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))

    def test_sleep_config(self):
        sleep = self.config["sleep"]
        self.assertTrue(sleep["enabled"])
        self.assertGreaterEqual(sleep["minHours"], 1)
        self.assertLessEqual(sleep["maxHours"], 8)
        self.assertLessEqual(sleep["minHours"], sleep["maxHours"])
        self.assertEqual(sleep["dawnHour"], 7)
        self.assertGreater(sleep["gameMinutesPerSecond"], 0)
        self.assertGreater(sleep["fatigueRecoveryPerHour"], 0)
        self.assertGreater(sleep["zombieRadius"], 0)

    def test_bed_buildable_and_asset(self):
        types = {entry["id"]: entry for entry in self.config["building"]["types"]}
        self.assertIn("bed", types)
        bed = types["bed"]
        self.assertEqual(bed["sprite"], "object.bed")
        self.assertGreater(bed["woodCost"], 0)
        self.assertFalse(bed["blocksMovement"])
        self.assertIn("object.bed", self.manifest["logical"])
        self.assertEqual(
            self.manifest["logical"]["object.bed"]["own"]["frames"], ["bed_1"]
        )


if __name__ == "__main__":
    unittest.main()
