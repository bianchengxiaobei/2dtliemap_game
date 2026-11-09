# -*- coding: utf-8 -*-
"""M20-F1 感染数据契约：配置范围、治疗物品与诊所获取路径。"""

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "config" / "game.json"
ITEMS = ROOT / "assets" / "items.json"
LOOT = ROOT / "assets" / "loot.json"


class InfectionDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = json.loads(CONFIG.read_text(encoding="utf-8"))["infection"]
        cls.items = json.loads(ITEMS.read_text(encoding="utf-8"))["items"]
        cls.by_id = {item["id"]: item for item in cls.items}
        cls.loot = {
            table["id"]: table
            for table in json.loads(LOOT.read_text(encoding="utf-8"))["tables"]
        }

    def test_config_is_playable_and_tunable(self):
        cfg = self.config
        self.assertTrue(cfg["enabled"])
        for key in ("clawChance", "biteChance", "woundRiskBonus", "maxChance",
                    "incubatingEnd", "feverEnd"):
            self.assertGreaterEqual(cfg[key], 0.0, key)
        self.assertLessEqual(cfg["maxChance"], 1.0)
        self.assertLess(cfg["incubatingEnd"], cfg["feverEnd"])
        self.assertLess(cfg["feverEnd"], 1.0)
        self.assertGreater(cfg["totalSeconds"], 0.0)
        self.assertGreater(cfg["severeHealthDrainPerSecond"], 0.0)

    def test_treatment_items_exist_and_are_reachable(self):
        for item_id in ("antibiotics", "disinfectant"):
            item = self.by_id[item_id]
            self.assertEqual(item["type"], "consumable")
            use = item["use"]
            self.assertGreater(use.get("infectionCure", 0.0), 0.0)
            self.assertGreater(use.get("infectionSlow", 0.0), 0.0)
            self.assertGreater(use.get("infectionSlowSeconds", 0.0), 0.0)

        clinic = self.loot["poi_clinic_medicine"]
        guaranteed = {entry["defId"] for entry in clinic.get("guaranteed", [])}
        entries = {entry["defId"] for entry in clinic.get("entries", [])}
        self.assertIn("antibiotics", guaranteed)
        self.assertIn("antibiotics", entries)
        self.assertIn("disinfectant", entries)
        self.assertIn("infectionSlow", self.by_id["bandage"]["use"])

        hospital = self.loot["poi_hospital_pharmacy"]
        hospital_entries = {entry["defId"]
                            for entry in hospital.get("entries", [])}
        self.assertIn("tainted_bandage", hospital_entries)
        tainted = self.by_id["tainted_bandage"]
        self.assertEqual(tainted["type"], "consumable")
        self.assertGreater(tainted["use"]["bandage"], 0.0)
        self.assertGreater(tainted["use"]["infectionRisk"], 0.0)
        self.assertLess(tainted["use"]["infectionRisk"], 1.0)

    def test_treatment_values_are_bounded(self):
        for item_id in ("antibiotics", "disinfectant"):
            use = self.by_id[item_id]["use"]
            self.assertLessEqual(use["infectionCure"], 1.0)
            self.assertLessEqual(use["infectionSlow"], 1.0)
            self.assertLessEqual(use["infectionSlowSeconds"], 600.0)


if __name__ == "__main__":
    unittest.main()
