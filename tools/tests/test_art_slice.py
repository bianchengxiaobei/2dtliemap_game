# -*- coding: utf-8 -*-
"""美术切片资源契约测试：图集、manifest、尺寸/锚点、帧序与页面边界。"""

import json
import unittest
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "assets" / "asset_manifest.json"
ATLAS_JSON = ROOT / "assets" / "own" / "atlas_own.atlas.json"
ATLAS_PNG = ROOT / "assets" / "own" / "atlas_own.png"

DIRECTIONS = ("down", "left", "right", "up")


class ArtSliceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with MANIFEST.open(encoding="utf-8") as f:
            cls.manifest = json.load(f)
        with ATLAS_JSON.open(encoding="utf-8") as f:
            cls.atlas = json.load(f)
        cls.image = Image.open(ATLAS_PNG)
        cls.frames = cls.atlas["subtextures"]

    def test_atlas_format_and_page(self):
        self.assertEqual(self.atlas["format"], "pz-atlas-v1")
        self.assertEqual(self.atlas["image"], "atlas_own.png")
        self.assertGreater(self.image.width, 0)
        self.assertGreater(self.image.height, 0)
        self.assertGreater(len(self.frames), 60)

    def test_all_own_manifest_frames_exist(self):
        for logical, providers in self.manifest["logical"].items():
            own = providers.get("own")
            if own is None:
                continue
            self.assertTrue(own["atlas"].startswith("assets/own/"), logical)
            self.assertTrue(own["frames"], logical)
            for name in own["frames"]:
                self.assertIn(name, self.frames, "%s / %s" % (logical, name))
                frame = self.frames[name]
                self.assertGreater(frame["w"], 0, name)
                self.assertGreater(frame["h"], 0, name)
                self.assertLessEqual(frame["x"] + frame["w"], self.image.width, name)
                self.assertLessEqual(frame["y"] + frame["h"], self.image.height, name)

    def test_atlas_frames_do_not_overlap(self):
        rects = []
        for name, frame in self.frames.items():
            rects.append((name, frame["x"], frame["y"], frame["w"], frame["h"]))
        for i, (a_name, ax, ay, aw, ah) in enumerate(rects):
            for b_name, bx, by, bw, bh in rects[i + 1:]:
                overlap = (ax < bx + bw and bx < ax + aw and
                           ay < by + bh and by < ay + ah)
                self.assertFalse(overlap, "%s overlaps %s" % (a_name, b_name))

    def test_floor_anchor_contract(self):
        for name in ("floor_grass_%d" % i for i in range(1, 5)):
            frame = self.frames[name]
            self.assertEqual((frame["w"], frame["h"]), (64, 32), name)
            self.assertEqual((frame["fx"], frame["fy"]), (64, 128), name)
            self.assertEqual((frame["ox"], frame["oy"]), (0, 96), name)

    def test_wall_and_door_contract(self):
        for name in ("wall_brick_1", "wall_brick_2", "wall_brick_3",
                     "wall_door_1", "wall_door_2"):
            frame = self.frames[name]
            self.assertEqual((frame["w"], frame["h"]), (64, 112), name)
            self.assertEqual((frame["fx"], frame["fy"]), (64, 224), name)
            self.assertEqual((frame["ox"], frame["oy"]), (0, 112), name)

    def test_character_frame_order_and_anchor(self):
        expected = [
            "character_%s_%d" % (direction, frame)
            for direction in DIRECTIONS for frame in (0, 1)
        ]
        self.assertEqual(
            self.manifest["logical"]["character.placeholder"]["own"]["frames"],
            expected,
        )
        zombie = ["character_zombie_%s_%d" % (direction, frame)
                  for direction in DIRECTIONS for frame in (0, 1)]
        self.assertEqual(
            self.manifest["logical"]["character.zombie"]["own"]["frames"],
            zombie,
        )
        for name in expected + zombie:
            frame = self.frames[name]
            self.assertEqual((frame["fx"], frame["fy"]), (64, 128), name)
            self.assertGreaterEqual(frame["ox"], 0, name)
            self.assertGreaterEqual(frame["oy"], 0, name)
            self.assertLessEqual(frame["ox"] + frame["w"], 64, name)
            self.assertLessEqual(frame["oy"] + frame["h"], 128, name)

    def test_slice_specific_frames_exist(self):
        required = (
            "character_zombie_down_0",
            "character_zombie_up_1",
            "wall_brick_3",
            "wall_door_2",
            "item_makeshift_spear",
            "item_padded_jacket",
            "item_repair_kit",
            "projectile_bullet",
            "effect_impact",
        )
        for name in required:
            self.assertIn(name, self.frames)

    def test_item_icons_resolve(self):
        with (ROOT / "assets" / "items.json").open(encoding="utf-8") as f:
            items = json.load(f)["items"]
        logical = self.manifest["logical"]
        for item in items:
            self.assertIn(item["icon"], logical, item["id"])
            self.assertIn("own", logical[item["icon"]], item["id"])


if __name__ == "__main__":
    unittest.main()
