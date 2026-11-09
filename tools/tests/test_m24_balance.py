# -*- coding: utf-8 -*-
"""M24-T6 平衡校验：新 POI 食物/弹药上限、武器与医疗取舍。"""

import collections
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "config" / "game.json"
ITEMS = ROOT / "assets" / "items.json"
LOOT = ROOT / "assets" / "loot.json"

NEW_POIS = {
    "supermarket", "motel", "police_station",
    "fire_station", "church", "warehouse",
}
FOOD_IDS = {
    "canned_beans", "canned_soup", "instant_noodles", "canned_fruit",
    "protein_bar", "energy_bar", "fresh_carrot", "fresh_potato",
}
AMMO_IDS = {"ammo_9mm", "ammo_shells"}
MEDICAL_IDS = {
    "bandage", "painkillers", "first_aid_kit", "antibiotics",
    "disinfectant", "herbal_salve", "tainted_bandage",
}


class M24BalanceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = json.loads(CONFIG.read_text(encoding="utf-8"))
        cls.items = {item["id"]: item for item in
                     json.loads(ITEMS.read_text(encoding="utf-8"))["items"]}
        cls.loot = {table["id"]: table for table in
                    json.loads(LOOT.read_text(encoding="utf-8"))["tables"]}
        cls.pois = {poi["id"]: poi for poi in
                    cls.config["pois"]["defs"]}

    def expected(self, table_id):
        table = self.loot[table_id]
        result = collections.defaultdict(float)
        for entry in table.get("guaranteed", []):
            result[entry["defId"]] += entry["count"]
        entries = table.get("entries", [])
        total_weight = sum(max(entry.get("weight", 1), 1)
                           for entry in entries)
        for entry in entries:
            weight = max(entry.get("weight", 1), 1)
            average = (entry.get("min", 1) + entry.get("max", 1)) / 2.0
            result[entry["defId"]] += (
                weight / total_weight * average * table.get("rolls", 0))
        return result

    def poi_resources(self, poi_id):
        result = collections.defaultdict(float)
        for container in self.pois[poi_id]["containers"]:
            for item_id, count in self.expected(container["tableId"]).items():
                result[item_id] += count
        return result

    def hunger(self, resources):
        return sum(
            count * self.items[item_id].get("use", {}).get("hunger", 0.0)
            for item_id, count in resources.items()
            if item_id in FOOD_IDS
        )

    def test_new_poi_food_stays_under_one_and_a_half_days(self):
        hunger_per_day = self.config["needs"]["hungerPerDay"]
        for poi_id in sorted(NEW_POIS):
            days = self.hunger(self.poi_resources(poi_id)) / hunger_per_day
            self.assertLessEqual(days, 1.5, poi_id)

    def test_new_poi_food_total_matches_two_day_target(self):
        hunger_per_day = self.config["needs"]["hungerPerDay"]
        total = sum(self.hunger(self.poi_resources(poi_id))
                    for poi_id in NEW_POIS) / hunger_per_day
        self.assertGreaterEqual(total, 1.5)
        self.assertLessEqual(total, 2.5)

    def test_new_poi_ammo_stays_within_police_and_warehouse_targets(self):
        police = self.poi_resources("police_station")
        warehouse = self.poi_resources("warehouse")
        military = self.poi_resources("military_checkpoint")
        self.assertGreaterEqual(police["ammo_9mm"], 12.0)
        self.assertLessEqual(police["ammo_9mm"], 24.0)
        self.assertGreaterEqual(warehouse["ammo_shells"], 6.0)
        self.assertLessEqual(warehouse["ammo_shells"], 10.0)
        self.assertLessEqual(warehouse["ammo_9mm"],
                             military["ammo_9mm"])
        self.assertLessEqual(warehouse["ammo_shells"],
                             military["ammo_shells"])

    def test_weapons_and_medical_keep_tradeoffs(self):
        baton = self.items["police_baton"]["combat"]
        sledge = self.items["sledgehammer"]["combat"]
        axe = self.items["axe"]["combat"]
        self.assertLess(baton["damage"], axe["damage"])
        self.assertLess(baton["cooldown"], axe["cooldown"])
        self.assertLess(baton["range"], axe["range"])
        self.assertLess(baton["arc"], axe["arc"])
        self.assertGreater(sledge["damage"], axe["damage"])
        self.assertGreater(sledge["cooldown"], axe["cooldown"])
        self.assertGreater(self.items["sledgehammer"]["weight"],
                           self.items["axe"]["weight"])
        self.assertGreater(sledge["knockback"], axe["knockback"])

        police_vest = self.items["police_vest"]["armor"]
        riot_vest = self.items["riot_vest"]["armor"]
        self.assertLess(police_vest["bite"], riot_vest["bite"])
        self.assertGreater(police_vest["blunt"], riot_vest["blunt"])
        self.assertLess(self.items["police_vest"]["weight"],
                        self.items["riot_vest"]["weight"])

        guaranteed_kits = 0
        for poi_id in NEW_POIS:
            for container in self.pois[poi_id]["containers"]:
                for entry in self.loot[container["tableId"]].get(
                        "guaranteed", []):
                    if entry["defId"] == "first_aid_kit":
                        guaranteed_kits += entry["count"]
        self.assertLessEqual(guaranteed_kits, 1)
        for poi_id in NEW_POIS:
            for container in self.pois[poi_id]["containers"]:
                for entry in self.loot[container["tableId"]].get(
                        "entries", []):
                    if entry["defId"] in ("first_aid_kit", "antibiotics"):
                        self.assertLessEqual(entry["weight"], 2, poi_id)


if __name__ == "__main__":
    unittest.main()
