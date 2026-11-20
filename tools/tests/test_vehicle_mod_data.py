# -*- coding: utf-8 -*-
"""Vehicle mod data contract checks for M33-F4-T1."""

import copy
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "config" / "game.json"
VEHICLE_MODS = ROOT / "assets" / "vehicle_mods.json"
ITEMS = ROOT / "assets" / "items.json"
LOOT = ROOT / "assets" / "loot.json"

SLOTS = {"armor", "storage", "plow", "lights", "engine"}
SCALES = {
    "crashConditionScale": (0.25, 2.0, 1.0),
    "zombieConditionScale": (0.25, 2.0, 1.0),
    "ramConditionScale": (0.25, 2.0, 1.0),
    "ramDamageScale": (0.25, 3.0, 1.0),
    "headlightRangeScale": (0.5, 3.0, 1.0),
    "headlightBrightnessScale": (0.5, 3.0, 1.0),
    "maxSpeedScale": (0.5, 2.0, 1.0),
    "accelerationScale": (0.5, 2.0, 1.0),
    "fuelUseScale": (0.5, 3.0, 1.0),
    "conditionWearScale": (0.25, 2.0, 1.0),
}
MAX_DEFS = 64
MAX_MATERIALS = 4
MAX_EXCLUDES = 4


def valid_token(value):
    return (isinstance(value, str) and 0 < len(value) <= 64 and
            all(c.islower() or c.isdigit() or c in "_." for c in value))


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def validate_vehicle_mod(entry, item_ids, known_ids=None):
    if not isinstance(entry, dict):
        return ["definition"]
    errors = []
    if not valid_token(entry.get("id")):
        errors.append("id")
    name = entry.get("name")
    if not isinstance(name, str) or not 0 < len(name) <= 96:
        errors.append("name")
    description = entry.get("description", "")
    if not isinstance(description, str) or len(description) > 384:
        errors.append("description")
    if entry.get("slot") not in SLOTS:
        errors.append("slot")

    effects = entry.get("effects", {})
    if not isinstance(effects, dict):
        errors.append("effects")
    else:
        unknown = set(effects) - set(SCALES) - {"trunkCapacityKg", "trunkStacks"}
        if unknown:
            errors.append("effects.unknown")
        for key, (lo, hi, _) in SCALES.items():
            if key in effects:
                value = effects[key]
                if not is_number(value) or not lo <= value <= hi:
                    errors.append("effects." + key)
        capacity = effects.get("trunkCapacityKg", 0.0)
        if not is_number(capacity) or not -20 <= capacity <= 80:
            errors.append("effects.trunkCapacityKg")
        stacks = effects.get("trunkStacks", 0)
        if not isinstance(stacks, int) or isinstance(stacks, bool) or \
                not -2 <= stacks <= 6:
            errors.append("effects.trunkStacks")

    materials = entry.get("materials")
    if not isinstance(materials, list) or not 1 <= len(materials) <= MAX_MATERIALS:
        errors.append("materials")
    else:
        seen = set()
        for material in materials:
            if not isinstance(material, dict):
                errors.append("materials.entry")
                continue
            item = material.get("item")
            count = material.get("count", 1)
            if item not in item_ids or not isinstance(count, int) or \
                    isinstance(count, bool) or not 1 <= count <= 99:
                errors.append("materials.item")
            if item in seen:
                errors.append("materials.duplicate")
            seen.add(item)

    install = entry.get("installSeconds", 10.0)
    if not is_number(install) or not 0.1 <= install <= 600:
        errors.append("installSeconds")
    weight = entry.get("weightKg", 0.0)
    if not is_number(weight) or not 0 <= weight <= 250:
        errors.append("weightKg")

    excludes = entry.get("excludes", [])
    if not isinstance(excludes, list) or len(excludes) > MAX_EXCLUDES:
        errors.append("excludes")
    else:
        seen = set()
        for target in excludes:
            if not valid_token(target) or target == entry.get("id") or \
                    target in seen:
                errors.append("excludes.entry")
            if known_ids is not None and target not in known_ids:
                errors.append("excludes.unknown")
            seen.add(target)
    return errors


def effective_effects(entry):
    effects = {
        "crashConditionScale": 1.0,
        "zombieConditionScale": 1.0,
        "ramConditionScale": 1.0,
        "ramDamageScale": 1.0,
        "trunkCapacityKg": 0.0,
        "trunkStacks": 0,
        "headlightRangeScale": 1.0,
        "headlightBrightnessScale": 1.0,
        "maxSpeedScale": 1.0,
        "accelerationScale": 1.0,
        "fuelUseScale": 1.0,
        "conditionWearScale": 1.0,
    }
    effects.update(entry.get("effects", {}))
    return effects


class VehicleModDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = json.loads(CONFIG.read_text(encoding="utf-8"))
        cls.doc = json.loads(VEHICLE_MODS.read_text(encoding="utf-8"))
        cls.items = {item["id"] for item in
                     json.loads(ITEMS.read_text(encoding="utf-8"))["items"]}
        cls.loot = json.loads(LOOT.read_text(encoding="utf-8"))

    def test_config_and_content_pack_contract(self):
        node = self.config["vehicleMods"]
        self.assertTrue(node["enabled"])
        self.assertEqual(node["file"], "assets/vehicle_mods.json")
        self.assertEqual(self.doc["format"], "vehicle_mods-v1")
        self.assertTrue(1 <= len(self.doc["defs"]) <= MAX_DEFS)
        self.assertEqual(len(self.doc["defs"]), 5)
        ids = [entry["id"] for entry in self.doc["defs"]]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual({entry["slot"] for entry in self.doc["defs"]}, SLOTS)
        for entry in self.doc["defs"]:
            self.assertEqual(validate_vehicle_mod(entry, self.items, set(ids)),
                             [])

    def test_actual_materials_and_tradeoffs_exist(self):
        for entry in self.doc["defs"]:
            for material in entry["materials"]:
                self.assertIn(material["item"], self.items)
            self.assertGreater(entry.get("weightKg", 0.0), 0.0)
            effects = effective_effects(entry)
            non_neutral = any(effects[key] != default
                              for key, (_, _, default) in SCALES.items())
            non_neutral = non_neutral or effects["trunkCapacityKg"] != 0.0 or \
                effects["trunkStacks"] != 0
            self.assertTrue(non_neutral)
            self.assertTrue(
                effects["fuelUseScale"] > 1.0 or
                effects["conditionWearScale"] > 1.0 or
                entry.get("weightKg", 0.0) >= 20.0)

    def test_materials_come_from_existing_loot(self):
        loot_ids = set()
        for table in self.loot["tables"]:
            for entry in table.get("guaranteed", []):
                loot_ids.add(entry["defId"])
            for entry in table.get("entries", []):
                loot_ids.add(entry["defId"])
        for entry in self.doc["defs"]:
            for material in entry["materials"]:
                self.assertIn(material["item"], loot_ids,
                              f"{entry['id']} material not in loot")

    def test_defaults_match_cpp_contract(self):
        entry = {
            "id": "default_mod",
            "name": "默认改装",
            "slot": "armor",
            "materials": [{"item": "rag", "count": 1}],
        }
        self.assertEqual(validate_vehicle_mod(entry, self.items, {"default_mod"}),
                         [])
        effects = effective_effects(entry)
        self.assertEqual(effects["crashConditionScale"], 1.0)
        self.assertEqual(effects["trunkCapacityKg"], 0.0)
        self.assertEqual(effects["trunkStacks"], 0)
        self.assertEqual(entry.get("installSeconds", 10.0), 10.0)
        self.assertEqual(entry.get("weightKg", 0.0), 0.0)
        self.assertEqual(entry.get("excludes", []), [])

    def test_invalid_values_and_references_are_rejected(self):
        invalid = {
            "id": "BadId",
            "name": "坏改装",
            "slot": "turret",
            "materials": [
                {"item": "missing_item", "count": 0},
                {"item": "missing_item", "count": 1},
            ],
            "effects": {
                "maxSpeedScale": 9.0,
                "unknownEffect": 1.0,
            },
            "installSeconds": 0,
            "weightKg": 999,
            "excludes": ["BadId", "missing_mod"],
        }
        errors = validate_vehicle_mod(invalid, self.items,
                                      {"default_mod"})
        for key in ("id", "slot", "materials.item", "materials.duplicate",
                    "effects.maxSpeedScale", "effects.unknown",
                    "installSeconds", "weightKg", "excludes.entry",
                    "excludes.unknown"):
            self.assertIn(key, errors)

        missing_materials = {
            "id": "missing_materials",
            "name": "无材料",
            "slot": "engine",
        }
        self.assertIn(
            "materials",
            validate_vehicle_mod(missing_materials, self.items,
                                 {"missing_materials"}))

    def test_mod_merge_skips_duplicates_and_bad_excludes(self):
        base = [{
            "id": "base_armor",
            "name": "基础装甲",
            "slot": "armor",
            "materials": [{"item": "rag", "count": 1}],
        }]
        incoming = [
            {
                "id": "mod_lights",
                "name": "模组车灯",
                "slot": "lights",
                "materials": [{"item": "rag", "count": 1}],
                "excludes": ["base_armor"],
            },
            {
                "id": "orphan",
                "name": "坏互斥",
                "slot": "engine",
                "materials": [{"item": "rag", "count": 1}],
                "excludes": ["missing_mod"],
            },
            copy.deepcopy(base[0]),
        ]
        merged = list(base)
        known = {entry["id"] for entry in merged}
        for entry in incoming:
            if entry["id"] in known:
                continue
            if validate_vehicle_mod(entry, self.items, known):
                continue
            merged.append(entry)
            known.add(entry["id"])
        self.assertEqual([entry["id"] for entry in merged],
                         ["base_armor", "mod_lights"])


if __name__ == "__main__":
    unittest.main()
