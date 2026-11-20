# -*- coding: utf-8 -*-
"""M33-F5-T6 staged POI content contract for the default M31 world."""

import json
import unittest
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "assets" / "maps" / "village_demo_m31_content.json"
MAP_DIR = ROOT / "assets" / "maps" / "village_demo_m31"
ITEMS = ROOT / "assets" / "items.json"
LOOT = ROOT / "assets" / "loot.json"
CHUNK = 10


class F5PoiContentTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.content = json.loads(CONTENT.read_text(encoding="utf-8"))
        cls.pois = cls.content["pois"]["defs"]
        cls.items = {
            item["id"]: item
            for item in json.loads(ITEMS.read_text(encoding="utf-8"))["items"]
        }
        cls.loot = {
            table["id"]: table
            for table in json.loads(LOOT.read_text(encoding="utf-8"))["tables"]
        }
        cls.by_id = {poi["id"]: poi for poi in cls.pois}
        cls.width = 0
        cls.height = 0
        cls.blocked = set()
        cls.reachable = set()
        cls._load_map()

    @classmethod
    def _load_map(cls):
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
        start = (70, 24)
        if start in cls.blocked:
            start = cls._nearest_free(start)
        queue = deque([start])
        cls.reachable = {start}
        while queue:
            x, y = queue.popleft()
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                cell = (x + dx, y + dy)
                if (0 <= cell[0] < cls.width and
                        0 <= cell[1] < cls.height and
                        cell not in cls.reachable and
                        cell not in cls.blocked):
                    cls.reachable.add(cell)
                    queue.append(cell)

    @classmethod
    def _nearest_free(cls, cell):
        queue = deque([cell])
        seen = {cell}
        while queue:
            x, y = queue.popleft()
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nxt = (x + dx, y + dy)
                if nxt in seen:
                    continue
                if nxt not in cls.blocked:
                    return nxt
                seen.add(nxt)
                queue.append(nxt)
        raise AssertionError("no free map cell")

    def _stage_index(self, poi):
        return {stage["id"]: i for i, stage in enumerate(poi.get("stages", []))}

    def _table_ids(self, poi):
        return {entry["tableId"] for entry in poi.get("containers", [])}

    def _guaranteed(self, table_id, item_id):
        return any(entry["defId"] == item_id for entry in
                   self.loot[table_id].get("guaranteed", []))

    def test_three_default_pois_are_staged(self):
        self.assertEqual(self.content["format"], "zomboidlike-world-content-v1")
        self.assertEqual(self.content["mapId"], "village_demo_m31")
        staged = [poi for poi in self.pois if poi.get("stages")]
        self.assertGreaterEqual(len(staged), 3)
        self.assertIn("clinic", {poi["id"] for poi in staged})
        self.assertIn("farmstead", {poi["id"] for poi in staged})
        self.assertIn("checkpoint", {poi["id"] for poi in staged})
        for poi in staged:
            self.assertIn(len(poi["stages"]), (2, 3), poi["id"])
        self.assertFalse(self.by_id["river_hut"].get("stages", []))
        self.assertFalse(self.by_id["river_hut"].get("boss"))

    def test_stage_branch_key_boss_and_reward_references(self):
        staged = [poi for poi in self.pois if poi.get("stages")]
        self.assertTrue(any(poi.get("branches") for poi in staged))
        self.assertTrue(any(poi.get("boss", {}).get("stage") for poi in staged))
        for poi in staged:
            stages = poi["stages"]
            index = self._stage_index(poi)
            self.assertEqual(len(index), len(stages), poi["id"])
            tables = self._table_ids(poi)
            self.assertIn(poi["task"]["targetTable"], tables)
            for i, stage in enumerate(stages):
                self.assertIn(stage["kind"], {
                    "reach", "interact", "kill", "loot", "power", "choice"})
                self.assertTrue(stage.get("hint"), (poi["id"], stage["id"]))
                reward = stage.get("reward", {})
                if reward.get("item"):
                    self.assertIn(reward["item"], self.items,
                                  (poi["id"], stage["id"]))
                    self.assertGreaterEqual(reward.get("count", 1), 1)
                for next_id in stage.get("next", []):
                    self.assertIn(next_id, index, (poi["id"], stage["id"]))
                    self.assertGreater(index[next_id], i,
                                       (poi["id"], stage["id"], next_id))
                if stage["kind"] == "choice":
                    self.assertGreaterEqual(len(stage.get("next", [])), 2)
                else:
                    self.assertLessEqual(len(stage.get("next", [])), 1)
                if stage["kind"] == "loot":
                    target = stage["target"]
                    self.assertIn(target, self.items, (poi["id"], stage["id"]))
                    self.assertTrue(any(self._guaranteed(table, target)
                                        for table in tables),
                                    (poi["id"], stage["id"], target))
            for branch in poi.get("branches", []):
                self.assertIn(branch.get("from"), index, (poi["id"], branch["id"]))
                self.assertTrue(branch.get("stages"), (poi["id"], branch["id"]))
                for stage_id in branch["stages"]:
                    self.assertIn(stage_id, index, (poi["id"], branch["id"]))
            for key in poi.get("keys", []):
                self.assertIn(key["item"], self.items, (poi["id"], key["id"]))
                if key.get("source"):
                    self.assertIn(key["source"], index, (poi["id"], key["id"]))
                for stage in stages:
                    if key["id"] in stage.get("requires", []):
                        self.assertLess(index[key["source"]], index[stage["id"]],
                                        (poi["id"], key["id"], stage["id"]))
            boss = poi.get("boss", {})
            if boss.get("stage"):
                self.assertIn(boss["stage"], index, poi["id"])
                boss_stage = stages[index[boss["stage"]]]
                self.assertEqual(boss_stage["kind"], "kill",
                                 (poi["id"], boss["stage"]))
                self.assertIn(boss.get("mechanic", ""), {
                    "", "summon", "charge", "fortify"})
            for note in poi.get("narrative", []):
                if note.get("stage"):
                    self.assertIn(note["stage"], index, (poi["id"], note["id"]))
                if note.get("item"):
                    self.assertIn(note["item"], self.items, (poi["id"], note["id"]))

    def test_risk_reward_and_key_flow(self):
        clinic = self.by_id["clinic"]
        branch_stages = {
            branch["id"]: branch["stages"][0]
            for branch in clinic["branches"]
        }
        ward = clinic["stages"][
            self._stage_index(clinic)[branch_stages["ward_route"]]]
        pharmacy = clinic["stages"][
            self._stage_index(clinic)[branch_stages["pharmacy_route"]]]
        self.assertGreater(ward["reward"]["xp"], pharmacy["reward"]["xp"])
        self.assertEqual(ward["reward"]["item"], "first_aid_kit")
        self.assertEqual(pharmacy["reward"]["item"], "antibiotics")

        checkpoint = self.by_id["checkpoint"]
        key = checkpoint["keys"][0]
        self.assertEqual(key["item"], "checkpoint_keycard")
        self.assertTrue(key["consumed"])
        gate = next(stage for stage in checkpoint["stages"]
                    if stage["id"] == "armory_gate")
        self.assertEqual(gate["requires"], [key["id"]])
        self.assertEqual(gate["kind"], "interact")
        self.assertGreaterEqual(
            sum(stage.get("reward", {}).get("xp", 0)
                for stage in checkpoint["stages"]),
            200.0)
        farm = self.by_id["farmstead"]
        self.assertGreaterEqual(
            sum(stage.get("reward", {}).get("xp", 0)
                for stage in farm["stages"]),
            180.0)

    def test_stage_locations_are_walkable(self):
        for poi in self.pois:
            for stage in poi.get("stages", []):
                x = int(stage.get("x", poi["markerX"]))
                y = int(stage.get("y", poi["markerY"]))
                self.assertNotIn((x, y), self.blocked,
                                 (poi["id"], stage["id"], x, y))
                self.assertIn((x, y), self.reachable,
                              (poi["id"], stage["id"], x, y))

    def test_staged_rewards_do_not_reuse_key_items(self):
        for poi in self.pois:
            if not poi.get("stages"):
                continue
            key_items = {key["item"] for key in poi.get("keys", [])}
            for stage in poi["stages"]:
                item = stage.get("reward", {}).get("item")
                if item:
                    self.assertNotIn(item, key_items,
                                     (poi["id"], stage["id"]))


if __name__ == "__main__":
    unittest.main()
