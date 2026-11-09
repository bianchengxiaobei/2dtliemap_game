# -*- coding: utf-8 -*-
"""M30 逐武器平衡基线工具测试。"""

import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import combat_balance_report as balance


class CombatBalanceReportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = balance.build_report()
        cls.by_id = {row["id"]: row for row in cls.report["weapons"]}

    def test_weapon_count_and_sources(self):
        self.assertEqual(self.report["format"], "m30-combat-balance-v1")
        self.assertEqual(len(self.by_id), 11)
        self.assertNotIn("pistol", self.by_id)
        self.assertNotIn("shotgun", self.by_id)
        self.assertEqual(self.report["sources"]["config"], "config/game.json")
        self.assertEqual(self.report["sources"]["items"], "assets/items.json")

    def test_fire_axe_contract(self):
        axe = self.by_id["axe"]
        self.assertTrue(axe["heavy_explicit"])
        self.assertAlmostEqual(axe["light"]["damage"], 65.0)
        self.assertAlmostEqual(axe["light"]["stamina"], 15.0)
        self.assertAlmostEqual(axe["light"]["poise"], 11.0)
        self.assertAlmostEqual(axe["heavy"]["damage"], 92.0)
        self.assertAlmostEqual(axe["heavy"]["stamina"], 28.0)
        self.assertAlmostEqual(axe["heavy"]["poise"], 38.0)
        self.assertAlmostEqual(axe["block_damage_scale"], 0.45)
        self.assertAlmostEqual(axe["block_stamina"], 16.0)
        self.assertEqual(axe["metrics"]["light"]["attacks_before_exhaust"], 6)
        normal = axe["metrics"]["light"]["variants"]["普通"]
        armored = axe["metrics"]["light"]["variants"]["重甲"]
        self.assertEqual(normal["hits_to_kill"], 2)
        self.assertAlmostEqual(normal["ttk"], 0.80)
        self.assertEqual(armored["hits_to_kill"], 5)
        self.assertEqual(axe["metrics"]["heavy"]["variants"]["重甲"]["hits_to_kill"], 4)

    def test_legacy_weapon_derivation(self):
        machete = self.by_id["machete"]
        self.assertFalse(machete["heavy_explicit"])
        self.assertAlmostEqual(machete["light"]["stamina"], 10.2)
        self.assertAlmostEqual(machete["light"]["windup"], 0.051)
        self.assertAlmostEqual(machete["light"]["poise"], 7.65)
        self.assertAlmostEqual(machete["heavy"]["damage"], 79.2)
        self.assertAlmostEqual(machete["heavy"]["range"], 1.7)
        self.assertAlmostEqual(machete["heavy"]["arc"], 67.5)
        self.assertAlmostEqual(machete["heavy"]["cooldown"], 0.651)
        self.assertAlmostEqual(machete["heavy"]["stamina"], 18.36)
        self.assertAlmostEqual(machete["heavy"]["windup"], 0.30)
        self.assertAlmostEqual(machete["heavy"]["poise"], 19.125)
        self.assertAlmostEqual(machete["block_damage_scale"], 0.35)
        self.assertAlmostEqual(machete["block_stamina"], 11.3)

    def test_markdown_and_json_are_deterministic(self):
        markdown = balance.render_markdown(self.report)
        self.assertEqual(markdown, balance.render_markdown(self.report))
        snapshot = (ROOT / "docs" / "15-M30战斗平衡基线.md").read_text(encoding="utf-8")
        self.assertEqual(markdown, snapshot)
        self.assertIn("# M30 逐武器战斗平衡基线", markdown)
        self.assertIn("| 消防斧 |", markdown)
        self.assertIn("重甲轻破防", markdown)
        self.assertEqual(len(self.report["findings"]), 4)
        self.assertIn("厨刀", self.report["findings"][0])
        self.assertEqual(self.report["findings"][0], balance.build_report()["findings"][0])
        encoded = json.dumps(self.report, ensure_ascii=False, allow_nan=False)
        self.assertIn('"m30-combat-balance-v1"', encoded)


if __name__ == "__main__":
    unittest.main()
