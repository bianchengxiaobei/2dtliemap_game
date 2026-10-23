# -*- coding: utf-8 -*-
"""loot.json 资源契约：表/条目引用真实物品，数量与权重合法。"""

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOOT = ROOT / "assets" / "loot.json"
ITEMS = ROOT / "assets" / "items.json"


class LootDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.loot = json.loads(LOOT.read_text(encoding="utf-8"))
        cls.items = {
            item["id"]: item
            for item in json.loads(ITEMS.read_text(encoding="utf-8"))["items"]
        }

    def test_format_and_seed(self):
        self.assertEqual(self.loot["format"], "loot-v1")
        self.assertIsInstance(self.loot["seed"], int)
        self.assertGreaterEqual(self.loot["seed"], 0)

    def test_tables_are_unique_and_reference_real_items(self):
        self.assertIsInstance(self.loot["tables"], list)
        self.assertTrue(self.loot["tables"])
        ids = set()
        for table in self.loot["tables"]:
            self.assertNotIn(table["id"], ids)
            ids.add(table["id"])
            self.assertTrue(table["name"])
            self.assertGreater(table["capacityKg"], 0)
            self.assertGreaterEqual(table["rolls"], 0)
            if table["rolls"] > 0:
                self.assertTrue(table.get("entries"))
            for entry in table.get("guaranteed", []):
                self._check_entry(entry, guaranteed=True)
            for entry in table.get("entries", []):
                self._check_entry(entry, guaranteed=False)

    def _check_entry(self, entry, guaranteed):
        item = self.items.get(entry.get("defId"))
        self.assertIsNotNone(item, entry)
        if guaranteed:
            self.assertGreaterEqual(entry["count"], 1)
            self.assertLessEqual(entry["count"], item["maxStack"])
        else:
            self.assertGreaterEqual(entry["weight"], 1)
            self.assertGreaterEqual(entry["min"], 1)
            self.assertLessEqual(entry["min"], entry["max"])
            self.assertLessEqual(entry["max"], item["maxStack"])

    def test_expected_tables_exist(self):
        ids = {table["id"] for table in self.loot["tables"]}
        self.assertIn("village_crate", ids)
        self.assertIn("village_locker", ids)
        self.assertIn("house_kitchen", ids)
        self.assertIn("house_workshop", ids)
        self.assertIn("radio_part", self.items)


if __name__ == "__main__":
    unittest.main()
