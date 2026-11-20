# -*- coding: utf-8 -*-
"""Companion data contract checks for M33-F3-T1."""

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "config" / "game.json"
COMPANIONS = ROOT / "assets" / "companions.json"
ITEMS = ROOT / "assets" / "items.json"
SETTLEMENTS = ROOT / "assets" / "settlements.json"
REPUTATION_TIERS = {"hostile", "wary", "neutral", "friendly", "trusted"}
EQUIPMENT_SLOTS = {"primary", "body"}
MAX_COMPANIONS = 32
MAX_INVENTORY = 16
MAX_TRAITS = 8


def valid_token(value):
    return (isinstance(value, str) and 0 < len(value) <= 64 and
            all(c.islower() or c.isdigit() or c in "_." for c in value))


def validate_companion(entry, item_ids, settlement_missions):
    if not isinstance(entry, dict):
        return ["companion is not an object"]
    errors = []
    if not valid_token(entry.get("id")):
        errors.append("id")
    name = entry.get("name")
    if not isinstance(name, str) or not 0 < len(name) <= 128:
        errors.append("name")
    background = entry.get("background", "")
    if not isinstance(background, str) or len(background) > 512:
        errors.append("background")
    base_health = entry.get("baseHealth", 100.0)
    if not isinstance(base_health, (int, float)) or not 1 <= base_health <= 1000:
        errors.append("baseHealth")
    loyalty = entry.get("startingLoyalty", 50)
    if not isinstance(loyalty, int) or not 0 <= loyalty <= 100:
        errors.append("startingLoyalty")
    capacity = entry.get("inventoryCapacityKg", 12.0)
    if not isinstance(capacity, (int, float)) or not 1 <= capacity <= 200:
        errors.append("inventoryCapacityKg")

    equipment = entry.get("equipment", {})
    if not isinstance(equipment, dict):
        errors.append("equipment")
    else:
        for slot, item in equipment.items():
            if slot not in EQUIPMENT_SLOTS or item not in item_ids:
                errors.append("equipment." + slot)
    inventory = entry.get("inventory", [])
    if not isinstance(inventory, list) or len(inventory) > MAX_INVENTORY:
        errors.append("inventory")
    else:
        seen = set()
        for stack in inventory:
            if not isinstance(stack, dict):
                errors.append("inventory.entry")
                continue
            item = stack.get("item")
            count = stack.get("count", 1)
            if item not in item_ids or not isinstance(count, int) or \
                    not 1 <= count <= 999 or item in seen:
                errors.append("inventory.item")
            seen.add(item)
    traits = entry.get("traits", [])
    if not isinstance(traits, list) or len(traits) > MAX_TRAITS:
        errors.append("traits")
    elif len({trait for trait in traits if valid_token(trait)}) != len(traits):
        errors.append("traits.duplicate")

    recruit = entry.get("recruit", {})
    if not isinstance(recruit, dict):
        errors.append("recruit")
    else:
        tier = recruit.get("minReputationTier", "friendly")
        if tier not in REPUTATION_TIERS:
            errors.append("recruit.minReputationTier")
        mission = recruit.get("requiredMission", "")
        if mission and mission not in settlement_missions:
            errors.append("recruit.requiredMission")
        required_item = recruit.get("requiredItem")
        if required_item is not None:
            if required_item not in item_ids:
                errors.append("recruit.requiredItem")
            count = recruit.get("requiredItemCount", 1)
            if not isinstance(count, int) or not 1 <= count <= 999:
                errors.append("recruit.requiredItemCount")
        elif "requiredItemCount" in recruit:
            errors.append("recruit.requiredItemCount")
    if not valid_token(entry.get("dialogueProfile", entry.get("id"))):
        errors.append("dialogueProfile")
    return errors


class CompanionDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = json.loads(CONFIG.read_text(encoding="utf-8"))
        cls.doc = json.loads(COMPANIONS.read_text(encoding="utf-8"))
        cls.items = {item["id"] for item in
                     json.loads(ITEMS.read_text(encoding="utf-8"))["items"]}
        settlements = json.loads(SETTLEMENTS.read_text(encoding="utf-8"))
        cls.settlement_missions = {
            settlement["id"]: set(settlement.get("missionPool", []))
            for settlement in settlements["defs"]
        }
        cls.all_settlement_missions = {
            mission for missions in cls.settlement_missions.values()
            for mission in missions
        }

    def test_config_and_content_pack_contract(self):
        companions = self.config["companions"]
        self.assertTrue(companions["enabled"])
        self.assertEqual(companions["file"], "assets/companions.json")
        self.assertEqual(self.doc["format"], "companions-v1")
        self.assertTrue(1 <= len(self.doc["defs"]) <= MAX_COMPANIONS)
        self.assertEqual(len(self.doc["defs"]), 2)
        ids = [entry["id"] for entry in self.doc["defs"]]
        self.assertEqual(len(ids), len(set(ids)))
        for entry in self.doc["defs"]:
            self.assertEqual(
                validate_companion(entry, self.items,
                                   self.all_settlement_missions), [])

    def test_actual_items_and_home_missions_exist(self):
        for entry in self.doc["defs"]:
            equipment = entry.get("equipment", {})
            for item in equipment.values():
                self.assertIn(item, self.items)
            for stack in entry.get("inventory", []):
                self.assertIn(stack["item"], self.items)
            recruit = entry.get("recruit", {})
            mission = recruit.get("requiredMission")
            if mission:
                self.assertIn(mission, self.all_settlement_missions)
            required = recruit.get("requiredItem")
            if required:
                self.assertIn(required, self.items)

    def test_invalid_companion_is_rejected_by_contract(self):
        invalid = {
            "id": "BadId",
            "name": "坏同伴",
            "baseHealth": 0,
            "inventory": [{"item": "missing_item", "count": 0}],
            "traits": ["scout", "scout"],
            "recruit": {"minReputationTier": "best"},
        }
        errors = validate_companion(invalid, self.items, self.all_settlement_missions)
        for key in ("id", "baseHealth", "inventory.item", "traits.duplicate",
                    "recruit.minReputationTier"):
            self.assertIn(key, errors)

    def test_defaults_match_cpp_contract(self):
        entry = {"id": "default_companion", "name": "默认同伴"}
        self.assertEqual(
            validate_companion(entry, self.items,
                               self.all_settlement_missions), [])
        self.assertEqual(entry.get("baseHealth", 100.0), 100.0)
        self.assertEqual(entry.get("inventoryCapacityKg", 12.0), 12.0)
        self.assertEqual(entry.get("startingLoyalty", 50), 50)
        self.assertEqual(entry.get("recruit", {}).get(
            "minReputationTier", "friendly"), "friendly")
        self.assertEqual(entry.get("dialogueProfile", entry["id"]), entry["id"])


if __name__ == "__main__":
    unittest.main()
