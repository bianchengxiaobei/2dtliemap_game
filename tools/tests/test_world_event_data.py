# -*- coding: utf-8 -*-
"""World event data contract checks for M33-F2-T1."""

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "config" / "game.json"
EVENTS = ROOT / "assets" / "world_events.json"
ITEMS = ROOT / "assets" / "items.json"
STAGE_KINDS = {"reach", "interact", "kill", "loot", "defend", "choice"}
MAX_CHAIN = 8
PRIMARY_EVENTS = {
    "world_caravan_ambush": "商队伏击",
    "world_survivor_distress": "幸存者求救",
    "world_migrating_horde": "迁徙尸潮",
    "world_infected_house": "感染房屋",
    "world_supply_contest": "空投争夺",
}
M24_EVENT_IDS = {
    "event_supply_drop",
    "event_wandering_horde",
    "event_infected_house",
    "event_survivor_ambush",
    "event_cold_snap",
}


def valid_token(value):
    return (isinstance(value, str) and 0 < len(value) <= 64 and
            all(c.islower() or c.isdigit() or c in "_." for c in value))


def default_event(event_id, name):
    return {
        "id": event_id,
        "name": name,
        "stages": [{"kind": "reach"}],
        "choices": [{"id": "leave", "label": "离开"}],
    }


def validate_event(entry, item_ids):
    if not isinstance(entry, dict):
        return ["event is not an object"]
    errors = []
    event_id = entry.get("id")
    if not valid_token(event_id):
        errors.append("id")
    name = entry.get("name")
    if not isinstance(name, str) or not 0 < len(name) <= 128:
        errors.append("name")
    kind = entry.get("kind", "custom")
    if not valid_token(kind):
        errors.append("kind")
    min_day = entry.get("minDay", 1)
    max_day = entry.get("maxDay", 100000)
    if not isinstance(min_day, int) or not 1 <= min_day <= 100000:
        errors.append("minDay")
    if not isinstance(max_day, int) or not 1 <= max_day <= 100000:
        errors.append("maxDay")
    if isinstance(min_day, int) and isinstance(max_day, int) and \
            max_day < min_day:
        errors.append("maxDay<minDay")
    weight = entry.get("weight", 1.0)
    if not isinstance(weight, (int, float)) or not 0.01 <= weight <= 1000:
        errors.append("weight")
    for key, low, high in (("cooldownDays", 0, 365),
                           ("warningMinutes", 0, 1440),
                           ("durationMinutes", 1, 1440)):
        value = entry.get(key, 30 if key == "durationMinutes" else 0)
        if not isinstance(value, int) or not low <= value <= high:
            errors.append(key)
    region = entry.get("region", "any")
    if region != "any" and not valid_token(region):
        errors.append("region")

    stages = entry.get("stages")
    if not isinstance(stages, list) or not 1 <= len(stages) <= 8:
        errors.append("stages")
    else:
        for stage in stages:
            if not isinstance(stage, dict) or stage.get("kind") not in STAGE_KINDS:
                errors.append("stage.kind")
                continue
            if stage["kind"] in {"interact", "kill", "loot"} and \
                    not valid_token(stage.get("target")):
                errors.append("stage.target")
            if "count" in stage and not isinstance(stage["count"], int):
                errors.append("stage.count")

    choices = entry.get("choices")
    if not isinstance(choices, list) or not 1 <= len(choices) <= 8:
        errors.append("choices")
    else:
        choice_ids = set()
        for choice in choices:
            if not isinstance(choice, dict):
                errors.append("choice")
                continue
            choice_id = choice.get("id")
            if not valid_token(choice_id) or choice_id in choice_ids:
                errors.append("choice.id")
            choice_ids.add(choice_id)
            if not isinstance(choice.get("label"), str) or \
                    not choice["label"]:
                errors.append("choice.label")
            reputation = choice.get("reputation", 0)
            if not isinstance(reputation, int) or not -100 <= reputation <= 100:
                errors.append("choice.reputation")
            reward_xp = choice.get("rewardXp", 0)
            if not isinstance(reward_xp, (int, float)) or \
                    not 0 <= reward_xp <= 1000000:
                errors.append("choice.rewardXp")
            pressure = choice.get("pressure", 0)
            if not isinstance(pressure, int) or not -100 <= pressure <= 100:
                errors.append("choice.pressure")
            reward_item = choice.get("rewardItem")
            if reward_item is not None and \
                    (not valid_token(reward_item) or reward_item not in item_ids):
                errors.append("choice.rewardItem")
            if reward_item is None and "rewardItemCount" in choice:
                errors.append("choice.rewardItemCount")
            if "rewardItemCount" in choice and \
                    not isinstance(choice["rewardItemCount"], int):
                errors.append("choice.rewardItemCount")

    failure_reputation = entry.get("failureReputation", -5)
    if not isinstance(failure_reputation, int) or \
            not -100 <= failure_reputation <= 100:
        errors.append("failureReputation")
    failure_pressure = entry.get("failurePressure", 5)
    if not isinstance(failure_pressure, int) or \
            not 0 <= failure_pressure <= 100:
        errors.append("failurePressure")

    next_event = entry.get("nextEvent", "")
    if next_event and not valid_token(next_event):
        errors.append("nextEvent")
    return errors


def chain_ok(event, defs):
    by_id = {item["id"]: item for item in defs if "id" in item}
    seen = set()
    current = event
    for _ in range(MAX_CHAIN + 1):
        current_id = current.get("id")
        if current_id in seen:
            return False
        seen.add(current_id)
        next_id = current.get("nextEvent", "")
        if not next_id:
            return True
        current = by_id.get(next_id)
        if current is None:
            return False
    return False


def merge_events(base, incoming, item_ids):
    result = list(base)
    parsed = []
    skipped = 0
    for entry in incoming:
        errors = validate_event(entry, item_ids)
        known = result + parsed
        if errors or any(item["id"] == entry.get("id") for item in known):
            skipped += 1
            continue
        parsed.append(entry)
    candidate = result + parsed
    base_ids = {item["id"] for item in base}
    for entry in candidate:
        if entry["id"] in base_ids:
            continue
        if not chain_ok(entry, candidate):
            skipped += 1
            continue
        result.append(entry)
    return result, skipped


class WorldEventDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = json.loads(CONFIG.read_text(encoding="utf-8"))
        cls.doc = json.loads(EVENTS.read_text(encoding="utf-8"))
        cls.items = {item["id"] for item in
                     json.loads(ITEMS.read_text(encoding="utf-8"))["items"]}

    def test_content_pack_and_config_contract(self):
        world_events = self.config["worldEvents"]
        self.assertTrue(world_events["enabled"])
        self.assertEqual(world_events["file"], "assets/world_events.json")
        self.assertIsInstance(world_events["randomSeed"], int)
        self.assertEqual(self.doc["format"], "world-events-v1")
        self.assertEqual(len(self.doc["defs"]), 6)
        by_id = {event["id"]: event for event in self.doc["defs"]}
        self.assertEqual(set(PRIMARY_EVENTS), set(by_id) - {
            "world_caravan_aftermath"})
        for event_id, name in PRIMARY_EVENTS.items():
            event = by_id[event_id]
            self.assertEqual(event["name"], name)
            self.assertEqual(len(event["stages"]), 2)
            self.assertIn(len(event["choices"]), (2, 3))
            self.assertEqual(validate_event(event, self.items), [])
            self.assertFalse(set(by_id) & M24_EVENT_IDS)
            for stage in event["stages"]:
                target = stage.get("target", "")
                if target:
                    self.assertNotIn(target, M24_EVENT_IDS)
        self.assertEqual(by_id["world_caravan_ambush"]["nextEvent"],
                         "world_caravan_aftermath")
        self.assertEqual(by_id["world_caravan_aftermath"]["stages"],
                         [{"kind": "choice", "radius": 6,
                           "description": "与商队处理后续"}])
        for event in by_id.values():
            self.assertIn(event["durationMinutes"], range(72, 241))
            self.assertGreaterEqual(event["failureReputation"], -8)
            for choice in event["choices"]:
                self.assertLessEqual(choice.get("rewardXp", 0), 140)
                self.assertLessEqual(choice.get("rewardItemCount", 1), 12)
                self.assertLessEqual(abs(choice.get("reputation", 0)), 20)

    def test_legal_event_and_defaults(self):
        caravan = {
            "id": "caravan_ambush",
            "name": "商队伏击",
            "kind": "caravan",
            "minDay": 3,
            "maxDay": 60,
            "weight": 1.5,
            "cooldownDays": 5,
            "warningMinutes": 120,
            "durationMinutes": 30,
            "region": "any",
            "stages": [
                {"kind": "reach", "radius": 8},
                {"kind": "kill", "target": "event_caravan_raider",
                 "count": 3},
            ],
            "choices": [
                {"id": "rescue", "label": "护送商队", "reputation": 15,
                 "rewardXp": 80, "pressure": -12},
                {"id": "rob", "label": "抢走货物", "reputation": -25,
                 "rewardItem": "ammo_9mm", "rewardItemCount": 2,
                 "pressure": 8},
            ],
            "nextEvent": "caravan_aftermath",
            "failureReputation": -7,
            "failurePressure": 9,
        }
        aftermath = default_event("caravan_aftermath", "商队余波")
        accepted, skipped = merge_events([], [caravan, aftermath], self.items)
        self.assertEqual(skipped, 0)
        self.assertEqual([e["id"] for e in accepted],
                         ["caravan_ambush", "caravan_aftermath"])
        self.assertEqual(aftermath.get("kind", "custom"), "custom")
        self.assertEqual(aftermath.get("region", "any"), "any")
        self.assertEqual(aftermath.get("minDay", 1), 1)
        self.assertEqual(aftermath.get("maxDay", 100000), 100000)

    def test_out_of_range_and_references_are_skipped(self):
        valid_a = default_event("valid_a", "有效 A")
        valid_a["nextEvent"] = "valid_b"
        valid_b = default_event("valid_b", "有效 B")
        bad_range = default_event("bad_range", "坏范围")
        bad_range["weight"] = 0
        bad_stage = default_event("bad_stage", "坏阶段")
        bad_stage["stages"] = [{"kind": "kill"}]
        bad_ref = default_event("bad_ref", "坏引用")
        bad_ref["nextEvent"] = "missing_event"
        cycle_a = default_event("cycle_a", "环 A")
        cycle_a["nextEvent"] = "cycle_b"
        cycle_b = default_event("cycle_b", "环 B")
        cycle_b["nextEvent"] = "cycle_a"
        duplicate = default_event("valid_a", "重复")
        bad_pressure = default_event("bad_pressure", "坏压力")
        bad_pressure["choices"][0]["pressure"] = 101

        accepted, skipped = merge_events(
            [], [valid_a, valid_b, bad_range, bad_stage, bad_ref, cycle_a,
                 cycle_b, duplicate, bad_pressure], self.items)
        self.assertEqual([e["id"] for e in accepted],
                         ["valid_a", "valid_b"])
        self.assertEqual(skipped, 7)

    def test_reward_item_reference(self):
        good = default_event("good_reward", "好奖励")
        good["choices"][0]["rewardItem"] = "ammo_9mm"
        bad = default_event("bad_reward", "坏奖励")
        bad["choices"][0]["rewardItem"] = "missing_item"
        accepted, skipped = merge_events([], [good, bad], self.items)
        self.assertEqual([e["id"] for e in accepted], ["good_reward"])
        self.assertEqual(skipped, 1)

    def test_mod_append_rejects_duplicate_and_bad_reference(self):
        base = [default_event("base_event", "基础事件")]
        forward = default_event("mod_event", "模组事件")
        forward["nextEvent"] = "base_event"
        duplicate = default_event("base_event", "重复基础")
        bad_ref = default_event("mod_bad_ref", "坏引用")
        bad_ref["nextEvent"] = "not_defined"
        accepted, skipped = merge_events(
            base, [forward, duplicate, bad_ref], self.items)
        self.assertEqual([e["id"] for e in accepted],
                         ["base_event", "mod_event"])
        self.assertEqual(skipped, 2)


if __name__ == "__main__":
    unittest.main()
