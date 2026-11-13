# -*- coding: utf-8 -*-
"""聚落数据契约：安全区、NPC 点位、交易/任务池、地图连通与 M33 声望配置。"""

import json
import math
import unittest
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "config" / "game.json"
SETTLEMENTS = ROOT / "assets" / "settlements.json"
MAP_DIR = ROOT / "assets" / "maps" / "village_demo_m31"
CHUNK = 10


class SettlementDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = json.loads(CONFIG.read_text(encoding="utf-8"))
        cls.settlement_config = cls.config["settlements"]
        cls.doc = json.loads(SETTLEMENTS.read_text(encoding="utf-8"))
        cls.defs = cls.doc["defs"]
        cls.missions = {mission["id"]: mission
                        for mission in cls.config["missions"]["defs"]}
        cls.map_meta = json.loads(
            (MAP_DIR / "map.json").read_text(encoding="utf-8"))
        cls.width = cls.map_meta["bounds"]["chunksX"] * CHUNK
        cls.height = cls.map_meta["bounds"]["chunksY"] * CHUNK
        cls.blocked = set()
        for path in sorted((MAP_DIR / "chunks").glob("*/chunk_*.json")):
            data = json.loads(path.read_text(encoding="utf-8"))
            x0 = data["cx"] * CHUNK
            y0 = data["cy"] * CHUNK
            for y, row in enumerate(data.get("floors", [])):
                for x, char in enumerate(row):
                    if char == "a":
                        cls.blocked.add((x0 + x, y0 + y))
            for obj in data.get("objects", []):
                if obj.get("solid", False) and obj.get("z", 0) == 0:
                    cls.blocked.add((x0 + obj["x"], y0 + obj["y"]))

    def test_settlement_contract_and_safe_zone(self):
        self.assertTrue(self.settlement_config["enabled"])
        self.assertEqual(self.settlement_config["file"],
                         "assets/settlements.json")
        self.assertEqual(self.doc["format"], "settlements-v1")
        self.assertEqual(len(self.defs), 4)
        self.assertEqual({definition["id"] for definition in self.defs},
                         {"village_haven", "north_fields",
                          "south_farmland", "river_camp"})
        village = next(definition for definition in self.defs
                       if definition["id"] == "village_haven")
        self.assertEqual(village["id"], "village_haven")
        self.assertEqual(village["name"], "村庄营地")
        self.assertEqual(village["regionId"], "village_core")
        self.assertEqual(village["services"],
                         ["trade", "missions", "defense", "recruit"])
        self.assertEqual(village["startingReputation"], 0)
        self.assertEqual(village["dialogueProfile"], "village_haven")
        self.assertGreater(village["safeRadius"], 0)
        self.assertEqual(village["restockDays"], 1)
        self.assertEqual(len(village["merchantStock"]), 5)
        for stock in village["merchantStock"]:
            self.assertGreaterEqual(stock, 0)
            self.assertLessEqual(stock, 99)

        farm = next(definition for definition in self.defs
                    if definition["id"] == "south_farmland")
        clinic = next(definition for definition in self.defs
                      if definition["id"] == "north_fields")
        river = next(definition for definition in self.defs
                     if definition["id"] == "river_camp")
        self.assertEqual(farm["centerX"], 110.0)
        self.assertEqual(farm["centerY"], 210.0)
        self.assertEqual(farm["regionId"], "south_farmland")
        self.assertEqual(farm["services"],
                         ["trade", "missions", "defense", "recruit"])
        self.assertEqual(clinic["centerX"], 150.0)
        self.assertEqual(clinic["centerY"], 64.0)
        self.assertEqual(clinic["regionId"], "north_fields")
        self.assertEqual(clinic["services"],
                         ["trade", "missions", "defense"])
        self.assertEqual(river["centerX"], 216.0)
        self.assertEqual(river["centerY"], 120.0)
        self.assertEqual(river["regionId"], "riverlands")
        self.assertEqual(river["services"], ["trade", "missions"])
        self.assertEqual(river["dialogueProfile"], "river_camp")
        self.assertFalse(river["defense"]["enabled"])
        self.assertEqual({point["kind"] for point in farm["npcPoints"]},
                         {"merchant", "guard", "requester"})
        self.assertEqual({point["kind"] for point in clinic["npcPoints"]},
                         {"merchant", "guard", "requester"})
        self.assertEqual({point["kind"] for point in river["npcPoints"]},
                         {"merchant", "requester"})
        self.assertEqual(farm["defense"]["minDay"], 9999)
        self.assertEqual(clinic["defense"]["minDay"], 9999)
        self.assertGreaterEqual(len({tuple(item["services"])
                                     for item in self.defs}), 2)

        defense = village["defense"]
        self.assertTrue(defense["enabled"])
        self.assertEqual(defense["minDay"], 10)
        self.assertEqual(defense["nightStartHour"], 22)
        self.assertEqual(defense["nightEndHour"], 5)
        self.assertNotEqual(defense["nightStartHour"], defense["nightEndHour"])
        self.assertEqual(defense["guardBehavior"], "defend")
        self.assertGreater(defense["activationRadius"],
                           village["safeRadius"])
        self.assertGreater(defense["spawnRadius"], 0)
        self.assertGreaterEqual(defense["baseRaiders"], 0)
        self.assertGreaterEqual(defense["maxRaiders"],
                                defense["baseRaiders"])
        for key in ("guardHealth", "guardDamage", "guardRange",
                    "guardAttackSeconds", "raiderHealth", "raiderDamage",
                    "raiderRange", "raiderAttackSeconds", "raiderSpeed"):
            self.assertGreater(defense[key], 0, key)

        self.assertEqual(len(village["npcPoints"]), 3)
        self.assertEqual({point["kind"] for point in village["npcPoints"]},
                         {"merchant", "guard", "requester"})
        for settlement in self.defs:
            points = settlement["npcPoints"]
            self.assertGreaterEqual(len(points), 2)
            self.assertEqual(len({point["id"] for point in points}),
                             len(points))
            for point in points:
                distance = math.hypot(point["x"] - settlement["centerX"],
                                      point["y"] - settlement["centerY"])
                self.assertLessEqual(distance,
                                     settlement["safeRadius"] + 1e-6)
                self.assertGreaterEqual(point["wanderRadius"], 0)
                self.assertLessEqual(point["wanderRadius"], 16)

    def test_trade_and_mission_pool_are_real(self):
        expected = {
            "village_haven": ["settlement_help", "settlement_escort",
                              "settlement_clear", "settlement_supply"],
            "north_fields": ["north_blood_drive"],
            "south_farmland": ["south_harvest_help"],
            "river_camp": ["river_fish_supply"],
        }
        for settlement in self.defs:
            pool = settlement["missionPool"]
            self.assertEqual(pool, expected[settlement["id"]])
            self.assertEqual(len(set(pool)), len(pool))
            for mission_id in pool:
                self.assertIn(mission_id, self.missions)
                mission = self.missions[mission_id]
                self.assertEqual(mission["source"], "npc")
                self.assertFalse(mission["autoActivate"])
                self.assertIn(mission["task"],
                              ("help", "escort", "clear", "supply"))
        village = next(definition for definition in self.defs
                       if definition["id"] == "village_haven")
        self.assertEqual(village["merchantStock"], [4, 2, 1, 2, 1])
        stocks = [tuple(definition["merchantStock"])
                  for definition in self.defs]
        self.assertEqual(len(set(stocks)), len(stocks))

    def test_reputation_config_contract(self):
        reputation = self.settlement_config["reputation"]
        self.assertTrue(reputation["enabled"])
        self.assertEqual(reputation["dailyTradeGainLimit"], 1)
        self.assertEqual(reputation["sources"], {
            "missionComplete": 10,
            "defenseComplete": 15,
            "trade": 1,
            "missionAbandon": -10,
            "attackNpc": -25,
            "theft": -25,
        })

        tiers = reputation["tiers"]
        self.assertEqual([tier["id"] for tier in tiers],
                         ["hostile", "wary", "neutral", "friendly",
                          "trusted"])
        self.assertEqual(tiers[0]["minScore"], -100)
        self.assertEqual(tiers[-1]["maxScore"], 100)
        for index, tier in enumerate(tiers):
            self.assertLessEqual(tier["minScore"], tier["maxScore"])
            self.assertGreaterEqual(tier["minScore"], -100)
            self.assertLessEqual(tier["maxScore"], 100)
            if index:
                self.assertEqual(tier["minScore"],
                                 tiers[index - 1]["maxScore"] + 1)

        self.assertEqual(set(reputation["priceMultipliers"]),
                         {tier["id"] for tier in tiers})
        for tier in tiers:
            multipliers = reputation["priceMultipliers"][tier["id"]]
            self.assertGreaterEqual(multipliers["buy"], 0.1)
            self.assertLessEqual(multipliers["buy"], 5.0)
            self.assertGreaterEqual(multipliers["sell"], 0.1)
            self.assertLessEqual(multipliers["sell"], 5.0)

    def test_npc_points_are_walkable_and_reachable(self):
        for settlement in self.defs:
            start = self._nearest_free(
                (int(settlement["centerX"]), int(settlement["centerY"])))
            reachable = self._flood(start)
            for point in settlement["npcPoints"]:
                cell = (int(point["x"]), int(point["y"]))
                self.assertNotIn(cell, self.blocked,
                                 "NPC %s 在阻挡格" % point["id"])
                self.assertIn(cell, reachable,
                              "%s 到 NPC %s 不可达" %
                              (settlement["id"], point["id"]))

    def _nearest_free(self, cell):
        if cell not in self.blocked:
            return cell
        queue = deque([cell])
        seen = {cell}
        while queue:
            x, y = queue.popleft()
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nxt = (x + dx, y + dy)
                if (0 <= nxt[0] < self.width and 0 <= nxt[1] < self.height
                        and nxt not in seen):
                    if nxt not in self.blocked:
                        return nxt
                    seen.add(nxt)
                    queue.append(nxt)
        raise AssertionError("出生点附近无可行走格")

    def _flood(self, start):
        queue = deque([start])
        seen = {start}
        while queue:
            x, y = queue.popleft()
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nxt = (x + dx, y + dy)
                if (0 <= nxt[0] < self.width and 0 <= nxt[1] < self.height
                        and nxt not in seen and nxt not in self.blocked):
                    seen.add(nxt)
                    queue.append(nxt)
        return seen


if __name__ == "__main__":
    unittest.main()
