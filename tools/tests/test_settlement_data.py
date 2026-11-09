# -*- coding: utf-8 -*-
"""M23-F4-T1 聚落数据契约：安全区、NPC 点位、交易/任务池与地图连通。"""

import json
import math
import unittest
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "config" / "game.json"
SETTLEMENTS = ROOT / "assets" / "settlements.json"
MAP_DIR = ROOT / "assets" / "maps" / "village_demo"
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
            for obj in data.get("objects", []):
                if obj.get("solid", False) and obj.get("z", 0) == 0:
                    cls.blocked.add((x0 + obj["x"], y0 + obj["y"]))

    def test_settlement_contract_and_safe_zone(self):
        self.assertTrue(self.settlement_config["enabled"])
        self.assertEqual(self.settlement_config["file"],
                         "assets/settlements.json")
        self.assertEqual(self.doc["format"], "settlements-v1")
        self.assertEqual(len(self.defs), 1)
        village = self.defs[0]
        self.assertEqual(village["id"], "village_haven")
        self.assertEqual(village["name"], "村庄营地")
        self.assertGreater(village["safeRadius"], 0)
        self.assertEqual(village["restockDays"], 1)
        self.assertEqual(len(village["merchantStock"]), 5)
        for stock in village["merchantStock"]:
            self.assertGreaterEqual(stock, 0)
            self.assertLessEqual(stock, 99)

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

        points = village["npcPoints"]
        self.assertEqual(len(points), 3)
        self.assertEqual({point["kind"] for point in points},
                         {"merchant", "guard", "requester"})
        self.assertEqual(len({point["id"] for point in points}), len(points))
        for point in points:
            distance = math.hypot(point["x"] - village["centerX"],
                                  point["y"] - village["centerY"])
            self.assertLessEqual(distance, village["safeRadius"] + 1e-6)
            self.assertGreaterEqual(point["wanderRadius"], 0)
            self.assertLessEqual(point["wanderRadius"], 16)

    def test_trade_and_mission_pool_are_real(self):
        village = self.defs[0]
        self.assertEqual(village["merchantStock"], [4, 2, 1, 2, 1])
        self.assertEqual(len(village["missionPool"]), 4)
        self.assertEqual(len(set(village["missionPool"])),
                         len(village["missionPool"]))
        for mission_id in village["missionPool"]:
            self.assertIn(mission_id, self.missions)
        self.assertEqual(village["missionPool"],
                         ["settlement_help", "settlement_escort",
                          "settlement_clear", "settlement_supply"])
        for mission_id in village["missionPool"]:
            mission = self.missions[mission_id]
            self.assertEqual(mission["source"], "npc")
            self.assertFalse(mission["autoActivate"])
            self.assertIn(mission["task"],
                          ("help", "escort", "clear", "supply"))

    def test_npc_points_are_walkable_and_reachable(self):
        start = self._nearest_free((70, 24))
        reachable = self._flood(start)
        for point in self.defs[0]["npcPoints"]:
            cell = (int(point["x"]), int(point["y"]))
            self.assertNotIn(cell, self.blocked,
                             "NPC %s 在阻挡格" % point["id"])
            self.assertIn(cell, reachable,
                          "村庄到 NPC %s 不可达" % point["id"])

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
