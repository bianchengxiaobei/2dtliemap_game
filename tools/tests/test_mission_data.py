# -*- coding: utf-8 -*-
"""M19-F3 任务数据契约：任务链、来源、目标引用与奖励可达性。"""

import json
import unittest
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAP_DIR = ROOT / "assets" / "maps" / "village_demo"
CONFIG = ROOT / "config" / "game.json"
ITEMS = ROOT / "assets" / "items.json"
RECIPES = ROOT / "assets" / "recipes.json"
CHUNK = 10


class MissionDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        config = json.loads(CONFIG.read_text(encoding="utf-8"))
        cls.config = config
        cls.missions = config["missions"]
        cls.defs = cls.missions["defs"]
        cls.items = {item["id"] for item in
                     json.loads(ITEMS.read_text(encoding="utf-8"))["items"]}
        cls.recipes = {recipe["id"] for recipe in
                       json.loads(RECIPES.read_text(encoding="utf-8"))["recipes"]}
        cls.poi_ids = {poi["id"] for poi in config["pois"]["defs"]}
        cls.loot = {table["id"] for table in
                    json.loads((ROOT / "assets" / "loot.json")
                               .read_text(encoding="utf-8"))["tables"]}
        cls.by_id = {mission["id"]: mission for mission in cls.defs}
        cls.width = 0
        cls.height = 0
        cls.blocked = set()
        meta = json.loads((MAP_DIR / "map.json").read_text(encoding="utf-8"))
        cls.width = meta["bounds"]["chunksX"] * CHUNK
        cls.height = meta["bounds"]["chunksY"] * CHUNK
        for path in sorted((MAP_DIR / "chunks").glob("*/chunk_*.json")):
            data = json.loads(path.read_text(encoding="utf-8"))
            x0 = data["cx"] * CHUNK
            y0 = data["cy"] * CHUNK
            for obj in data.get("objects", []):
                if obj.get("solid", False) and obj.get("z", 0) == 0:
                    cls.blocked.add((x0 + obj["x"], y0 + obj["y"]))

    def test_mission_chains_and_event_types(self):
        self.assertTrue(self.missions["enabled"])
        self.assertEqual(len(self.defs), 35)
        chains = {}
        for mission in self.defs:
            chains.setdefault(mission["chain"], []).append(mission)
        once_chains = [chain for chain, defs in chains.items()
                       if all(mission["kind"] == "once" for mission in defs)]
        self.assertEqual(len(once_chains), 13)
        self.assertEqual(len(chains), 22)
        for chain in ("radio", "clinic", "farm", "hospital", "military",
                      "supermarket", "police", "motel", "relay"):
            self.assertEqual(len(chains[chain]), 2)
            orders = sorted(mission["order"] for mission in chains[chain])
            self.assertEqual(orders, [1, 2])
        self.assertEqual(len(chains["settlement"]), 5)
        self.assertEqual(sorted(mission["order"]
                                for mission in chains["settlement"]),
                         [1, 2, 3, 4, 5])
        self.assertEqual(sum(mission["kind"] == "daily"
                             for mission in self.defs), 1)
        self.assertEqual(sum(mission["kind"] == "random"
                             for mission in self.defs), 8)
        self.assertEqual(sum(mission["kind"] == "once"
                             for mission in self.defs), 26)
        self.assertEqual(self.by_id["hospital_quarantine"]["prerequisite"],
                         "hospital_supply")
        self.assertEqual(self.by_id["military_extract"]["prerequisite"],
                         "military_armory")
        self.assertEqual(self.by_id["relay_recover"]["prerequisite"],
                         "radio_repair")
        self.assertEqual(self.by_id["relay_recover"]["source"], "radio")
        self.assertEqual(self.by_id["relay_extract"]["prerequisite"],
                         "relay_recover")
        self.assertEqual(self.by_id["super_return"]["prerequisite"],
                         "super_stock")
        self.assertEqual(self.by_id["police_return"]["prerequisite"],
                         "police_case")
        self.assertEqual(self.by_id["motel_return"]["prerequisite"],
                         "motel_guest")
        self.assertEqual(self.by_id["hospital_quarantine"]["objectives"][0]
                         ["target"], "poi_hospital_supplies")
        self.assertEqual(self.by_id["military_extract"]["objectives"][0]
                         ["target"], "poi_military_supplies")
        self.assertEqual(self.by_id["super_stock"]["objectives"][0]["kind"],
                         "poi")
        self.assertEqual(self.by_id["super_stock"]["objectives"][0]["target"],
                         "supermarket")
        self.assertEqual(self.by_id["police_case"]["objectives"][0]["kind"],
                         "loot")
        self.assertEqual(self.by_id["police_case"]["objectives"][0]["target"],
                         "poi_police_evidence")
        self.assertEqual(self.by_id["motel_guest"]["objectives"][0]["target"],
                         "motel")
        relay = self.by_id["relay_recover"]["objectives"]
        self.assertEqual([objective["kind"] for objective in relay],
                         ["loot", "collect"])
        self.assertEqual(relay[0]["target"], "poi_warehouse_relay")
        self.assertEqual(relay[1]["target"], "relay_part")
        self.assertEqual(relay[1]["count"], 1)
        extract = self.by_id["relay_extract"]["objectives"][0]
        self.assertEqual(extract["kind"], "reach")
        self.assertEqual((extract["x"], extract["y"], extract["radius"]),
                         (66.5, 28.5, 1.5))
        for mission_id, xp, item in (
                ("super_stock", 70, "canned_soup"),
                ("super_return", 50, "instant_noodles"),
                ("police_case", 110, "ammo_9mm"),
                ("police_return", 60, "first_aid_kit"),
                ("motel_guest", 70, "canned_fruit"),
                ("motel_return", 50, "sewing_kit"),
                ("relay_recover", 120, "toolbox"),
                ("relay_extract", 150, "ammo_shells")):
            reward = self.by_id[mission_id]["reward"]
            self.assertEqual(reward["xp"], xp)
            self.assertEqual(reward["item"], item)
        self.assertIn("event_hospital_outbreak", self.by_id)
        self.assertIn("event_military_patrol", self.by_id)
        for mission_id, min_day, chance in (
                ("event_wandering_horde", 3, 0.50),
                ("event_infected_house", 2, 0.55),
                ("event_survivor_ambush", 4, 0.40),
                ("event_cold_snap", 2, 0.35)):
            event = self.by_id[mission_id]
            self.assertEqual(event["kind"], "random")
            self.assertEqual(event["minDay"], min_day)
            self.assertEqual(event["chance"], chance)
        self.assertEqual(self.by_id["event_wandering_horde"]["objectives"][0]
                         ["target"], "event_wandering_horde")
        self.assertEqual(self.by_id["event_wandering_horde"]["objectives"][0]
                         ["count"], 8)
        house = self.by_id["event_infected_house"]["objectives"]
        self.assertEqual([objective["kind"] for objective in house],
                         ["kill", "loot"])
        self.assertEqual(house[0]["target"], "event_infected_house")
        self.assertEqual(house[1]["target"], "event_infected_cache")
        self.assertEqual(self.by_id["event_survivor_ambush"]["objectives"][0]
                         ["target"], "event_survivor_ambush")
        self.assertEqual(self.by_id["event_survivor_ambush"]["reward"]
                         ["extraItem"], "painkillers")
        cold = self.by_id["event_cold_snap"]["objectives"][0]
        self.assertEqual(cold["kind"], "reach")
        self.assertEqual((cold["x"], cold["y"], cold["radius"]),
                         (70.0, 24.0, 3.0))
        settlement = {mission["id"]: mission
                      for mission in chains["settlement"]}
        self.assertEqual(settlement["settlement_help"]["task"], "help")
        self.assertEqual(settlement["settlement_escort"]["task"], "escort")
        self.assertEqual(settlement["settlement_clear"]["task"], "clear")
        self.assertEqual(settlement["settlement_supply"]["task"], "supply")
        self.assertEqual(settlement["refugee_relief"]["task"], "help")
        self.assertEqual(settlement["settlement_escort"]["prerequisite"],
                         "settlement_help")
        self.assertEqual(settlement["settlement_clear"]["prerequisite"],
                         "settlement_escort")
        self.assertEqual(settlement["settlement_supply"]["prerequisite"],
                         "settlement_clear")
        for mission in settlement.values():
            self.assertFalse(mission["autoActivate"])
            self.assertGreater(mission["timeLimitDays"], 0)
            self.assertGreaterEqual(mission["retryDays"], 1)
            self.assertEqual(mission["source"], "npc")
        for mission_id, minimum in (
                ("settlement_help", -39),
                ("settlement_escort", 0),
                ("settlement_clear", 20),
                ("settlement_supply", 30),
                ("refugee_relief", -100)):
            self.assertEqual(
                settlement[mission_id].get("minSettlementReputation", -100),
                minimum)
        for mission in self.defs:
            minimum = mission.get("minSettlementReputation", -100)
            self.assertGreaterEqual(minimum, -100)
            self.assertLessEqual(minimum, 100)
        for mission_id, chain, task, minimum in (
                ("north_blood_drive", "north_clinic", "help", -39),
                ("south_harvest_help", "south_farm", "supply", 0),
                ("river_fish_supply", "river_camp", "supply", -39)):
            mission = self.by_id[mission_id]
            self.assertEqual(mission["chain"], chain)
            self.assertEqual(mission["task"], task)
            self.assertEqual(mission["source"], "npc")
            self.assertFalse(mission["autoActivate"])
            self.assertGreater(mission["timeLimitDays"], 0)
            self.assertGreaterEqual(mission["retryDays"], 1)
            self.assertEqual(mission.get("minSettlementReputation", -100),
                             minimum)

    def test_sources_and_prerequisites(self):
        sources = {mission["source"] for mission in self.defs}
        self.assertEqual(sources, {"radio", "note", "npc", "base"})
        for mission in self.defs:
            prerequisite = mission.get("prerequisite", "")
            if prerequisite:
                self.assertIn(prerequisite, self.by_id)
                if self.by_id[prerequisite]["chain"] == mission["chain"]:
                    self.assertLess(self.by_id[prerequisite]["order"],
                                    mission["order"])
        for mission in self.defs:
            self.assertTrue(mission["name"])
            self.assertTrue(mission["objectives"])
            self.assertTrue(mission["reward"]["label"])
            self.assertIn(mission["kind"], ("once", "daily", "random"))
            if "task" in mission:
                self.assertIn(mission["task"],
                              ("generic", "help", "escort", "clear",
                               "supply"))
            if not mission.get("autoActivate", True):
                self.assertEqual(mission["kind"], "once")
                self.assertGreater(mission.get("timeLimitDays", 0), 0)
                self.assertGreaterEqual(mission.get("retryDays", 1), 1)

    def test_m24_narrative_and_relay_targets(self):
        poi_text = {poi["id"]: poi["task"]["description"]
                    for poi in self.config["pois"]["defs"]}
        for poi_id, text in (
                ("supermarket", "仓库门还锁着"),
                ("police_station", "枪柜却还满着"),
                ("fire_station", "出警记录停在三天前"),
                ("motel", "登记簿最后一页"),
                ("church", "未点完的蜡烛"),
                ("warehouse", "中继零件已转移到东侧货架"),
                ("military_checkpoint", "东侧商业区信号中断")):
            self.assertIn(text, poi_text[poi_id])
        def narrative(mission_id):
            return " ".join(objective.get("description", "")
                            for objective in self.by_id[mission_id]["objectives"])
        self.assertIn("最后一批罐头在仓库", narrative("super_stock"))
        self.assertIn("六个新地点里至少有三个", narrative("super_return"))
        self.assertIn("案卷被人翻过", narrative("police_case"))
        self.assertIn("登记簿最后一页", narrative("motel_guest"))
        self.assertIn("如果你听到这段广播", narrative("relay_recover"))
        for mission_id, text in (
                ("event_wandering_horde", "从公路方向"),
                ("event_infected_house", "西侧空屋"),
                ("event_survivor_ambush", "有人跟着你"),
                ("event_cold_snap", "气温骤降"),
                ("event_supply_drop", "任意两个容器")):
            self.assertIn(text, narrative(mission_id))
        tables = {table["id"]: table for table in
                  json.loads((ROOT / "assets" / "loot.json")
                             .read_text(encoding="utf-8"))["tables"]}
        self.assertEqual(tables["poi_warehouse_relay"]["guaranteed"],
                         [{"defId": "relay_part", "count": 1}])

    def test_objective_targets_exist(self):
        for mission in self.defs:
            for objective in mission["objectives"]:
                kind = objective["kind"]
                self.assertIn(kind, ("collect", "interact", "kill",
                                     "loot", "poi", "reach"))
                target = objective.get("target", "")
                if kind == "collect":
                    self.assertIn(target, self.items)
                elif kind == "interact":
                    self.assertEqual(target, "radio")
                elif kind == "loot" and target != "any":
                    self.assertIn(target, self.loot)
                elif kind == "poi":
                    self.assertIn(target, self.poi_ids)
                elif kind == "reach":
                    self.assertGreater(objective.get("radius", 0), 0)
                    radius = int(objective["radius"])
                    cells = {(int(objective["x"]) + dx,
                              int(objective["y"]) + dy)
                             for dx in range(-radius, radius + 1)
                             for dy in range(-radius, radius + 1)}
                    self.assertTrue(cells & self._reachable(),
                                    "任务 %s 的返回点不可达" % mission["id"])
                elif kind == "kill":
                    self.assertIn(target, ("any", "event_wandering_horde",
                                           "event_infected_house",
                                           "event_survivor_ambush"))

    def test_rewards_exist(self):
        for mission in self.defs:
            reward = mission["reward"]
            self.assertGreaterEqual(reward.get("xp", 0), 0)
            self.assertIn(reward.get("skill", "survival"),
                          {"melee", "building", "crafting", "survival"})
            if reward.get("item"):
                self.assertIn(reward["item"], self.items)
                self.assertGreaterEqual(reward.get("itemCount", 1), 1)
            if reward.get("extraItem"):
                self.assertIn(reward["extraItem"], self.items)
                self.assertGreaterEqual(reward.get("extraItemCount", 1), 1)
            if reward.get("blueprint"):
                self.assertIn(reward["blueprint"], self.recipes)

    def test_base_facility_unlock_is_connected(self):
        facilities = json.loads(CONFIG.read_text(encoding="utf-8"))["building"]["types"]
        rain = {item["id"]: item for item in facilities}["rain_collector"]
        self.assertEqual(rain.get("requiresMission"), "farm_tools")
        self.assertIn("farm_tools", self.by_id)
        self.assertIn(self.by_id["farm_tools"]["reward"]["blueprint"],
                      self.recipes)

    def test_random_and_daily_availability(self):
        for mission in self.defs:
            if mission["kind"] == "random":
                self.assertGreaterEqual(mission.get("minDay", 1), 1)
                self.assertGreaterEqual(mission.get("chance", 0), 0)
                self.assertLessEqual(mission.get("chance", 0), 1)
            if mission["kind"] == "daily":
                self.assertEqual(mission.get("minDay", 1), 1)

    def test_f6_mission_balance_bounds(self):
        hospital = (self.by_id["hospital_supply"]["reward"].get("xp", 0) +
                    self.by_id["hospital_quarantine"]["reward"].get("xp", 0))
        military = (self.by_id["military_armory"]["reward"].get("xp", 0) +
                    self.by_id["military_extract"]["reward"].get("xp", 0))
        self.assertEqual(hospital, 190)
        self.assertEqual(military, 240)
        self.assertLessEqual(military, 260)
        for mission in self.defs:
            if mission["kind"] != "once":
                continue
            self.assertGreaterEqual(mission["reward"].get("xp", 0), 40)
            self.assertLessEqual(mission["reward"].get("xp", 0), 160)
        for mission in self.defs:
            if mission["kind"] != "random":
                continue
            self.assertGreaterEqual(mission.get("minDay", 1), 2)
            self.assertGreaterEqual(mission["chance"], 0.35)
            self.assertLessEqual(mission["chance"], 0.65)
            self.assertLessEqual(mission["reward"].get("xp", 0), 90)
            for objective in mission["objectives"]:
                if objective["kind"] == "kill":
                    self.assertLessEqual(objective["count"], 8)
        self.assertEqual(self.by_id["daily_patrol"]["reward"].get("xp"), 30)

    def _reachable(self):
        if hasattr(type(self), "_reach_cache"):
            return type(self)._reach_cache
        start = (70, 24)
        if start in self.blocked:
            start = self._nearest_free(start)
        queue = deque([start])
        seen = {start}
        while queue:
            x, y = queue.popleft()
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                cell = (x + dx, y + dy)
                if (0 <= cell[0] < self.width and
                        0 <= cell[1] < self.height and
                        cell not in seen and cell not in self.blocked):
                    seen.add(cell)
                    queue.append(cell)
        type(self)._reach_cache = seen
        return seen

    def _nearest_free(self, cell):
        for radius in range(1, 8):
            for dx in range(-radius, radius + 1):
                for dy in range(-radius, radius + 1):
                    candidate = (cell[0] + dx, cell[1] + dy)
                    if candidate not in self.blocked:
                        return candidate
        return cell


if __name__ == "__main__":
    unittest.main()
