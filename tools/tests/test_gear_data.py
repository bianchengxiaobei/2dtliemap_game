# -*- coding: utf-8 -*-
"""M19-F4 装备数据契约：武器取舍、衣物护甲与维修效率。"""

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ITEMS = ROOT / "assets" / "items.json"
LOOT = ROOT / "assets" / "loot.json"


class GearDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.items = json.loads(ITEMS.read_text(encoding="utf-8"))["items"]
        cls.by_id = {item["id"]: item for item in cls.items}
        cls.loot = json.loads(LOOT.read_text(encoding="utf-8"))["tables"]

    def test_new_melee_weapons_have_distinct_tradeoffs(self):
        expected = {"machete", "baseball_bat", "crowbar"}
        self.assertTrue(expected.issubset(self.by_id))
        for item_id in expected:
            item = self.by_id[item_id]
            self.assertEqual(item["type"], "weapon")
            combat = item["combat"]
            for key in ("damage", "range", "arc", "cooldown", "knockback",
                        "stagger", "wear"):
                self.assertIn(key, combat, item_id)
            self.assertGreater(item.get("repairEfficiency", 1.0), 0.0)
        self.assertLess(self.by_id["machete"]["combat"]["cooldown"],
                        self.by_id["baseball_bat"]["combat"]["cooldown"])
        self.assertGreater(self.by_id["baseball_bat"]["combat"]["knockback"],
                           self.by_id["machete"]["combat"]["knockback"])
        self.assertLess(self.by_id["crowbar"]["combat"]["wear"],
                        self.by_id["machete"]["combat"]["wear"])
        self.assertGreater(self.by_id["crowbar"]["repairEfficiency"],
                           self.by_id["axe"]["repairEfficiency"])

    def test_clothing_armor_is_valid_and_tradeoffs_exist(self):
        for item in self.items:
            if item["type"] != "clothing" or "armor" not in item:
                continue
            armor = item["armor"]
            for key in ("claw", "bite", "blunt"):
                self.assertGreaterEqual(armor.get(key, 0.0), 0.0, item["id"])
                self.assertLessEqual(armor.get(key, 0.0), 0.95, item["id"])
            self.assertGreaterEqual(armor.get("moveScale", 1.0), 0.5, item["id"])
            self.assertLessEqual(armor.get("moveScale", 1.0), 1.0, item["id"])
            self.assertGreaterEqual(armor.get("insulation", 0.0), 0.0, item["id"])
            self.assertLessEqual(armor.get("insulation", 0.0), 1.0, item["id"])
            self.assertGreaterEqual(armor.get("waterproof", 0.0), 0.0, item["id"])
            self.assertLessEqual(armor.get("waterproof", 0.0), 1.0, item["id"])

        vest = self.by_id["riot_vest"]["armor"]
        jacket = self.by_id["jacket"]["armor"]
        raincoat = self.by_id["raincoat"]["armor"]
        self.assertGreater(vest["bite"], jacket["bite"])
        self.assertGreater(vest["blunt"], jacket["blunt"])
        self.assertLess(vest["moveScale"], jacket["moveScale"])
        self.assertGreater(raincoat["waterproof"], jacket["waterproof"])

    def test_ranged_weapons_and_ammo_contract(self):
        ranged_keys = {
            "damage", "range", "spreadDegrees", "pellets",
            "cooldownSeconds", "magazine", "reloadSeconds", "ammoDefId",
            "noiseRadius", "wearPerShot", "projectileSpeed",
            "falloffStart", "falloffEnd", "falloffScale",
        }
        weapons = {"pistol": "ammo_9mm", "shotgun": "ammo_shells"}
        for weapon_id, ammo_id in weapons.items():
            item = self.by_id[weapon_id]
            self.assertEqual(item["type"], "weapon", weapon_id)
            ranged = item["ranged"]
            self.assertEqual(set(ranged), ranged_keys, weapon_id)
            self.assertEqual(ranged["ammoDefId"], ammo_id)
            self.assertIn(ammo_id, self.by_id)
            ammo = self.by_id[ammo_id]
            self.assertEqual(ammo["type"], "generic", ammo_id)
            self.assertGreater(ammo["maxStack"], 1)
            self.assertGreater(ranged["damage"], 0)
            self.assertGreater(ranged["range"], 0)
            self.assertGreaterEqual(ranged["spreadDegrees"], 0)
            self.assertLessEqual(ranged["spreadDegrees"], 90)
            self.assertGreaterEqual(ranged["pellets"], 1)
            self.assertGreater(ranged["cooldownSeconds"], 0)
            self.assertGreaterEqual(ranged["magazine"], 1)
            self.assertGreater(ranged["reloadSeconds"], 0)
            self.assertGreaterEqual(ranged["noiseRadius"], 0)
            self.assertGreaterEqual(ranged["wearPerShot"], 0)
            self.assertLessEqual(ranged["wearPerShot"], 1)
            self.assertGreater(ranged["projectileSpeed"], 0)
            self.assertGreaterEqual(ranged["falloffStart"], 0)
            self.assertLessEqual(ranged["falloffStart"], ranged["falloffEnd"])
            self.assertLessEqual(ranged["falloffEnd"], ranged["range"])
            self.assertGreaterEqual(ranged["falloffScale"], 0)
            self.assertLessEqual(ranged["falloffScale"], 1)

        for item in self.items:
            if item["type"] == "weapon":
                self.assertTrue("combat" in item or "ranged" in item, item["id"])

        pistol = self.by_id["pistol"]["ranged"]
        shotgun = self.by_id["shotgun"]["ranged"]
        self.assertGreater(shotgun["pellets"], pistol["pellets"])
        self.assertGreater(shotgun["spreadDegrees"], pistol["spreadDegrees"])
        self.assertGreater(shotgun["noiseRadius"], pistol["noiseRadius"])
        self.assertGreater(pistol["magazine"], shotgun["magazine"])

    def test_m30_combat_data_contract(self):
        expected = {"axe", "kitchen_knife", "baseball_bat",
                    "sledgehammer", "makeshift_spear"}
        for item_id in expected:
            item = self.by_id[item_id]
            self.assertEqual(item["type"], "weapon", item_id)
            combat = item["combat"]
            for key in ("stamina", "windup", "poise",
                        "blockReduction", "blockStamina", "heavy"):
                self.assertIn(key, combat, item_id)
            self.assertGreater(combat["stamina"], 0, item_id)
            self.assertGreater(combat["windup"], 0, item_id)
            self.assertGreater(combat["poise"], 0, item_id)
            self.assertGreaterEqual(combat["blockReduction"], 0, item_id)
            self.assertLessEqual(combat["blockReduction"], 0.95, item_id)
            self.assertGreater(combat["blockStamina"], 0, item_id)
            heavy = combat["heavy"]
            for key in ("damage", "range", "arc", "cooldown",
                        "knockback", "stagger", "wear",
                        "stamina", "windup", "poise"):
                self.assertIn(key, heavy, item_id)
            self.assertNotIn("blockReduction", heavy, item_id)
            self.assertNotIn("blockStamina", heavy, item_id)

        axe = self.by_id["axe"]["combat"]
        sledge = self.by_id["sledgehammer"]["combat"]
        knife = self.by_id["kitchen_knife"]["combat"]
        self.assertGreater(sledge["stamina"], knife["stamina"])
        self.assertGreater(sledge["heavy"]["poise"], axe["heavy"]["poise"])
        self.assertGreater(self.by_id["baseball_bat"]["combat"]["heavy"]["damage"],
                           self.by_id["baseball_bat"]["combat"]["damage"])
        self.assertGreater(self.by_id["makeshift_spear"]["combat"]["heavy"]["range"],
                           self.by_id["makeshift_spear"]["combat"]["range"])
        self.assertNotIn("heavy", self.by_id["machete"]["combat"])

    def test_new_gear_is_obtainable_from_loot(self):
        referenced = {
            entry["defId"]
            for table in self.loot
            for entry in table.get("entries", [])
        }
        for item_id in ("machete", "baseball_bat", "crowbar",
                        "leather_jacket", "riot_vest", "raincoat"):
            self.assertIn(item_id, referenced, item_id)


if __name__ == "__main__":
    unittest.main()
