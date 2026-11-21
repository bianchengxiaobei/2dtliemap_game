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
            names = list(own["frames"])
            for clip in own.get("animations", {}).values():
                names.extend(clip["frames"])
            for name in names:
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
                     "wall_door_1", "wall_door_2",
                     "wall_window_h_1", "wall_window_v_1",
                     "wall_wood_h_1", "wall_wood_v_1",
                     "door_wood_closed_h_1", "door_wood_closed_v_1",
                     "door_wood_open_h_1", "door_wood_open_v_1"):
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

    def test_character_animation_v2_contract(self):
        specs = {
            "idle": (2, 3, True, []),
            "walk": (2, 5, True, [(1, "footstep")]),
            "attack": (3, 10, False, [(1, "hit")]),
            "hit": (1, 8, False, []),
            "death": (2, 6, False, []),
        }
        for logical, prefix in (("character.placeholder", "character"),
                                ("character.zombie", "character_zombie")):
            own = self.manifest["logical"][logical]["own"]
            animations = own["animations"]
            self.assertEqual(set(animations), set(specs), logical)
            for clip, (per_dir, fps, loop, events) in specs.items():
                data = animations[clip]
                self.assertEqual(data["framesPerDir"], per_dir, clip)
                self.assertEqual(data["fps"], fps, clip)
                self.assertEqual(data["loop"], loop, clip)
                expected_events = [{"frame": f, "type": t} for f, t in events]
                self.assertEqual(data["events"], expected_events, clip)
                expected = []
                for direction in DIRECTIONS:
                    for frame in range(per_dir):
                        if clip == "walk":
                            expected.append("%s_%s_%d" % (prefix, direction, frame))
                        else:
                            expected.append("%s_%s_%s_%d" %
                                            (prefix, clip, direction, frame))
                self.assertEqual(data["frames"], expected, clip)
                for name in expected:
                    self.assertIn(name, self.frames, name)

    def test_slice_specific_frames_exist(self):
        required = (
            "character_zombie_down_0",
            "character_zombie_up_1",
            "wall_brick_3",
            "wall_door_2",
            "wall_window_h_1",
            "wall_window_v_1",
            "wall_wood_h_1",
            "wall_wood_v_1",
            "door_wood_closed_h_1",
            "door_wood_closed_v_1",
            "door_wood_open_h_1",
            "door_wood_open_v_1",
            "item_makeshift_spear",
            "item_padded_jacket",
            "item_repair_kit",
            "crate_1",
            "vehicle_north",
            "vehicle_northeast",
            "vehicle_east",
            "vehicle_southeast",
            "vehicle_south",
            "vehicle_southwest",
            "vehicle_west",
            "vehicle_northwest",
            "locker_1",
            "bed_1",
            "barricade_1",
            "radio_1",
            "extraction_1",
            "item_radio_part",
            "item_wood",
            "projectile_bullet",
            "effect_impact",
        )
        for name in required:
            self.assertIn(name, self.frames)

    def test_vehicle_eight_direction_contract(self):
        expected = {
            "object.vehicle": "vehicle_south",
            "object.vehicle_north": "vehicle_north",
            "object.vehicle_northeast": "vehicle_northeast",
            "object.vehicle_east": "vehicle_east",
            "object.vehicle_southeast": "vehicle_southeast",
            "object.vehicle_south": "vehicle_south",
            "object.vehicle_southwest": "vehicle_southwest",
            "object.vehicle_west": "vehicle_west",
            "object.vehicle_northwest": "vehicle_northwest",
        }
        for logical, frame_name in expected.items():
            own = self.manifest["logical"][logical]["own"]
            self.assertEqual(own["frames"], [frame_name], logical)
            frame = self.frames[frame_name]
            self.assertEqual((frame["fx"], frame["fy"]), (160, 128), frame_name)
            self.assertGreater(frame["w"], 0, frame_name)
            self.assertGreater(frame["h"], 0, frame_name)
            self.assertLessEqual(frame["ox"] + frame["w"], 160, frame_name)
            self.assertLessEqual(frame["oy"] + frame["h"], 128, frame_name)

    def test_item_icons_resolve(self):
        with (ROOT / "assets" / "items.json").open(encoding="utf-8") as f:
            items = json.load(f)["items"]
        logical = self.manifest["logical"]
        for item in items:
            self.assertIn(item["icon"], logical, item["id"])
            self.assertIn("own", logical[item["icon"]], item["id"])


if __name__ == "__main__":
    unittest.main()
