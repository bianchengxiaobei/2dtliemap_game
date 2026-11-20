# -*- coding: utf-8 -*-
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "config" / "game.json"
MANIFEST = ROOT / "assets" / "asset_manifest.json"


class BuildingDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = json.loads(CONFIG.read_text(encoding="utf-8"))
        cls.manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))

    def test_buildables_are_grouped_and_layered(self):
        types = self.config["building"]["types"]
        self.assertEqual(
            [entry["id"] for entry in types],
            [
                "barricade",
                "wall",
                "door",
                "floor",
                "campfire",
                "workbench",
                "rain_collector",
                "bed",
                "storage",
                "farm_plot",
                "generator",
                "fridge",
                "spike_trap",
                "floodlight",
                "electric_fence",
                "bear_trap",
                "tripwire_alarm",
                "fuel_trap",
            ],
        )
        by_id = {entry["id"]: entry for entry in types}
        self.assertEqual(by_id["bear_trap"]["woodCost"], 3)
        self.assertEqual(by_id["tripwire_alarm"]["woodCost"], 1)
        self.assertEqual(by_id["fuel_trap"]["woodCost"], 4)
        for trap_id in ("bear_trap", "tripwire_alarm", "fuel_trap"):
            self.assertEqual(by_id[trap_id]["sprite"], "object.trap")
            self.assertEqual(by_id[trap_id]["category"], "defense")
        self.assertEqual(by_id["wall"]["layer"], "wall")
        self.assertEqual(by_id["wall"]["category"], "defense")
        self.assertEqual(by_id["wall"]["spriteVertical"], "object.wall_v")
        self.assertEqual(by_id["door"]["layer"], "wall")
        self.assertTrue(by_id["door"]["door"])
        self.assertEqual(by_id["door"]["openSprite"], "object.door_open_h")
        self.assertEqual(
            by_id["door"]["openSpriteVertical"], "object.door_open_v"
        )
        self.assertEqual(by_id["floor"]["layer"], "floor")
        self.assertFalse(by_id["floor"]["blocksMovement"])
        self.assertEqual(by_id["storage"]["category"], "storage")
        self.assertFalse(by_id["floodlight"]["blocksMovement"])
        self.assertGreater(by_id["floodlight"]["maxHp"], 0)
        self.assertEqual(by_id["electric_fence"]["category"], "defense")
        self.assertTrue(by_id["electric_fence"]["blocksMovement"])
        self.assertGreater(by_id["electric_fence"]["maxHp"], 0)
        self.assertEqual(
            self.config["input"]["bindings"]["RepairAllStructures"], "J"
        )

    def test_door_floor_assets_resolve(self):
        logical = self.manifest["logical"]
        for name in (
            "object.wall_h",
            "object.wall_v",
            "object.door_closed_h",
            "object.door_closed_v",
            "object.door_open_h",
            "object.door_open_v",
            "tile.floor.wood",
            "object.farm_plot",
            "object.generator",
            "object.fridge",
            "object.trap",
            "object.floodlight",
            "object.electric_fence",
        ):
            self.assertIn(name, logical)
            self.assertTrue(logical[name]["own"]["frames"], name)


if __name__ == "__main__":
    unittest.main()
