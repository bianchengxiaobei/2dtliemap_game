# -*- coding: utf-8 -*-
"""M19-F2/M24-T2 POI 数据契约：配置、独立搜刮表、地图房间、路线连通与资源存在。"""

import json
import unittest
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAP_DIR = ROOT / "assets" / "maps" / "village_demo"
CONFIG = ROOT / "config" / "game.json"
LOOT = ROOT / "assets" / "loot.json"
MANIFEST = ROOT / "assets" / "asset_manifest.json"
CHUNK = 10


class PoiDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = json.loads(CONFIG.read_text(encoding="utf-8"))
        cls.pois = cls.config["pois"]
        cls.defs = cls.pois["defs"]
        cls.loot = {table["id"]: table
                    for table in json.loads(LOOT.read_text(encoding="utf-8"))["tables"]}
        cls.manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))["logical"]
        cls.map_meta = json.loads((MAP_DIR / "map.json").read_text(encoding="utf-8"))
        cls.width = cls.map_meta["bounds"]["chunksX"] * CHUNK
        cls.height = cls.map_meta["bounds"]["chunksY"] * CHUNK
        cls.blocked = set()
        cls.rooms = set()
        cls.objects = []
        for path in sorted((MAP_DIR / "chunks").glob("*/chunk_*.json")):
            data = json.loads(path.read_text(encoding="utf-8"))
            x0 = data["cx"] * CHUNK
            y0 = data["cy"] * CHUNK
            for obj in data.get("objects", []):
                cell = (x0 + obj["x"], y0 + obj["y"])
                cls.objects.append((cell, obj.get("sprite", ""),
                                    obj.get("layer", ""), obj.get("z", 0)))
                if obj.get("solid", False) and obj.get("z", 0) == 0:
                    cls.blocked.add(cell)
            for room in data.get("rooms", []):
                cls.rooms.add(room["name"])

    def test_poi_count_and_fields(self):
        self.assertTrue(self.pois["enabled"])
        self.assertEqual(len(self.defs), 11)
        self.assertEqual(self.map_meta["generator"]["version"], 5)
        self.assertEqual(self.map_meta["generator"]["params"]["pois"], len(self.defs))
        ids = set()
        for poi in self.defs:
            self.assertNotIn(poi["id"], ids)
            ids.add(poi["id"])
            self.assertTrue(poi["name"])
            self.assertIn(poi["threat"], (1, 2, 3))
            self.assertGreaterEqual(poi["enemies"]["count"], 1)
            self.assertGreater(poi["enemies"]["radius"], 0)
            self.assertIn(poi["enemies"]["kind"],
                          ("normal", "fast", "strong", "mixed", "armored"))
            armored = poi["enemies"].get("armoredCount", 0)
            self.assertIsInstance(armored, int)
            self.assertGreaterEqual(armored, 0)
            self.assertLessEqual(armored, poi["enemies"]["count"])
            self.assertTrue(poi["task"]["description"])
            self.assertGreater(poi["task"]["rewardXp"], 0)
        self.assertEqual(ids, {
            "gas_station", "clinic", "farm_barn", "hospital", "military_checkpoint",
            "supermarket", "motel", "police_station", "fire_station", "church",
            "warehouse",
        })
        by_id = {poi["id"]: poi for poi in self.defs}
        self.assertEqual(by_id["hospital"]["task"]["targetTable"],
                         "poi_hospital_pharmacy")
        self.assertEqual(by_id["military_checkpoint"]["task"]["targetTable"],
                         "poi_military_armory")
        self.assertEqual(by_id["military_checkpoint"]["enemies"]["kind"], "strong")
        for poi_id in ("supermarket", "motel"):
            self.assertEqual(by_id[poi_id]["threat"], 1)
            self.assertEqual(len(by_id[poi_id]["containers"]), 3)
        for poi_id in ("police_station", "fire_station", "church"):
            self.assertEqual(by_id[poi_id]["threat"], 2)
            self.assertEqual(len(by_id[poi_id]["containers"]), 3)
            self.assertEqual(by_id[poi_id]["enemies"].get("armoredCount"), 1)
        self.assertEqual(by_id["warehouse"]["threat"], 3)
        self.assertEqual(by_id["warehouse"]["enemies"]["kind"], "strong")
        self.assertEqual(by_id["warehouse"]["enemies"].get("armoredCount"), 2)
        self.assertEqual(len(by_id["warehouse"]["containers"]), 4)
        self.assertIn("poi_warehouse_relay",
                      [container["tableId"]
                       for container in by_id["warehouse"]["containers"]])

    def test_loot_tables_are_independent_and_real(self):
        target_tables = set()
        for poi in self.defs:
            tables = [container["tableId"] for container in poi["containers"]]
            minimum = 3 if poi["id"] in {
                "supermarket", "motel", "police_station", "fire_station",
                "church", "warehouse"} else 2
            self.assertGreaterEqual(len(tables), minimum)
            self.assertEqual(len(tables), len(set(tables)))
            for table in tables:
                self.assertIn(table, self.loot)
            self.assertIn(poi["task"]["targetTable"], tables)
            target_tables.add(poi["task"]["targetTable"])
        self.assertEqual(len(target_tables), len(self.defs))

    def test_m24_new_tables_and_event_tables_are_complete(self):
        expected = {
            "poi_supermarket_food", "poi_supermarket_household",
            "poi_supermarket_storage",
            "poi_motel_office", "poi_motel_room", "poi_motel_storage",
            "poi_police_armory", "poi_police_evidence", "poi_police_supplies",
            "poi_fire_gear", "poi_fire_tools", "poi_fire_medical",
            "poi_church_supplies", "poi_church_medical", "poi_church_archive",
            "poi_warehouse_tools", "poi_warehouse_food",
            "poi_warehouse_relay", "poi_warehouse_valuables",
        }
        referenced = {container["tableId"]
                      for poi in self.defs
                      for container in poi["containers"]}
        self.assertEqual(expected, referenced & expected)
        for table_id in expected:
            table = self.loot[table_id]
            self.assertGreater(table["capacityKg"], 0)
            self.assertGreater(table["rolls"], 0)
            self.assertTrue(table["entries"])
        for table_id in ("event_supply_crate", "event_infected_cache"):
            self.assertIn(table_id, self.loot)
            table = self.loot[table_id]
            self.assertGreater(table["rolls"], 0)
            self.assertTrue(table["entries"])
            self.assertTrue(table.get("guaranteed"))
        self.assertIn("tainted_bandage", {
            entry["defId"]
            for entry in self.loot["event_infected_cache"]["entries"]
        })
        relay = self.loot["poi_warehouse_relay"]
        self.assertEqual([entry["defId"] for entry in relay["guaranteed"]],
                         ["relay_part"])
        self.assertEqual(relay["guaranteed"][0]["count"], 1)

    def test_m24_low_threat_tables_do_not_guarantee_firearms(self):
        by_id = {poi["id"]: poi for poi in self.defs}
        for poi_id in ("supermarket", "motel"):
            for container in by_id[poi_id]["containers"]:
                table = self.loot[container["tableId"]]
                def_ids = {entry["defId"] for entry in table.get("entries", [])}
                def_ids.update(entry["defId"]
                               for entry in table.get("guaranteed", []))
                self.assertNotIn("pistol", def_ids)
                self.assertNotIn("shotgun", def_ids)
        armory = self.loot["poi_police_armory"]
        guaranteed = {entry["defId"]: entry["count"]
                      for entry in armory.get("guaranteed", [])}
        self.assertLessEqual(guaranteed.get("ammo_9mm", 0), 12)
        for entry in armory.get("entries", []):
            if entry["defId"].startswith("ammo_"):
                self.assertLessEqual(entry["max"], 12)
        warehouse = self.loot["poi_warehouse_valuables"]
        guaranteed = {entry["defId"]: entry["count"]
                      for entry in warehouse.get("guaranteed", [])}
        self.assertLessEqual(guaranteed.get("ammo_9mm", 0), 12)
        self.assertLessEqual(guaranteed.get("ammo_shells", 0), 6)
        for entry in warehouse.get("entries", []):
            if entry["defId"].startswith("ammo_"):
                self.assertLessEqual(entry["max"], 12)

    def test_map_rooms_and_sprites_exist(self):
        for poi in self.defs:
            self.assertIn("poi_" + poi["id"], self.rooms)
        for poi in self.defs:
            for container in poi["containers"]:
                self.assertIn(container["sprite"], self.manifest)
                self.assertNotIn(
                    (int(container["x"]), int(container["y"])), self.blocked,
                    "容器不能在墙/家具阻挡格")
        for sprite in ("wall.brick", "wall.door", "object.workbench", "object.bed"):
            self.assertIn(sprite, self.manifest)

    def test_f6_threat_and_military_loot_are_bounded(self):
        by_id = {poi["id"]: poi for poi in self.defs}
        hospital = by_id["hospital"]
        military = by_id["military_checkpoint"]
        self.assertEqual(hospital["threat"], 2)
        self.assertEqual(military["threat"], 3)
        self.assertGreater(hospital["enemies"]["count"], 5)
        self.assertGreater(military["enemies"]["count"], hospital["enemies"]["count"])
        self.assertEqual(military["enemies"]["kind"], "strong")
        self.assertEqual(military["enemies"].get("armoredCount"), 2)
        self.assertEqual(hospital["enemies"].get("armoredCount", 0), 0)

        armory = self.loot["poi_military_armory"]
        guaranteed = {entry["defId"]: entry["count"]
                      for entry in armory.get("guaranteed", [])}
        self.assertIn("pistol", guaranteed)
        self.assertIn("ammo_9mm", guaranteed)
        self.assertIn("ammo_shells", guaranteed)
        self.assertLessEqual(guaranteed["ammo_9mm"], 30)
        self.assertLessEqual(guaranteed["ammo_shells"], 12)
        for entry in armory.get("entries", []):
            if entry["defId"].startswith("ammo_"):
                self.assertLessEqual(entry["max"], 20)

    def test_f6_layouts_are_distinct_and_reuse_existing_sprites(self):
        hospital = {(x, y): sprite for (x, y), sprite, layer, z in self.objects
                    if z == 0 and 40 <= x < 52 and 80 <= y < 89}
        military = {(x, y): sprite for (x, y), sprite, layer, z in self.objects
                    if z == 0 and 100 <= x < 110 and 18 <= y < 25}
        self.assertEqual(hospital.get((44, 82)), "wall.brick")
        self.assertEqual(hospital.get((47, 82)), "wall.brick")
        self.assertNotIn((44, 84), hospital)
        self.assertNotIn((47, 84), hospital)
        self.assertEqual(sum(sprite == "object.bed"
                             for sprite in hospital.values()), 6)
        self.assertEqual(military.get((105, 24)), "wall.door")
        self.assertGreaterEqual(sum(sprite == "object.barricade"
                                    for sprite in military.values()), 4)

    def test_routes_from_village_are_walkable(self):
        start = self._nearest_free((70, 24))
        reachable = self._flood(start)
        for poi in self.defs:
            marker = (int(poi["markerX"]), int(poi["markerY"]))
            self.assertNotIn(marker, self.blocked, poi["id"])
            self.assertIn(marker, reachable, "村庄到 %s 标记不可达" % poi["id"])
            for container in poi["containers"]:
                cell = (int(container["x"]), int(container["y"]))
                self.assertIn(cell, reachable,
                              "村庄到 %s 容器 %s 不可达" % (poi["id"], cell))

    def _nearest_free(self, cell):
        if cell not in self.blocked:
            return cell
        q = deque([cell])
        seen = {cell}
        while q:
            x, y = q.popleft()
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = (x + dx, y + dy)
                if 0 <= n[0] < self.width and 0 <= n[1] < self.height and n not in seen:
                    if n not in self.blocked:
                        return n
                    seen.add(n)
                    q.append(n)
        raise AssertionError("出生点附近无可行走格")

    def _flood(self, start):
        q = deque([start])
        seen = {start}
        while q:
            x, y = q.popleft()
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = (x + dx, y + dy)
                if (0 <= n[0] < self.width and 0 <= n[1] < self.height and
                        n not in seen and n not in self.blocked):
                    seen.add(n)
                    q.append(n)
        return seen


if __name__ == "__main__":
    unittest.main()
