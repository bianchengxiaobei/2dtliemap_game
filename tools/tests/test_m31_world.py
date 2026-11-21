# -*- coding: utf-8 -*-
"""M31-T2 世界生成器、地图内容和确定性测试。"""

import hashlib
import json
import math
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import gen_world

MAP_DIR = ROOT / "assets" / "maps" / "village_demo_m31"
CHUNK = 10


class M31WorldDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.meta = json.loads((MAP_DIR / "map.json").read_text(encoding="utf-8"))
        cls.world = json.loads((MAP_DIR / "world.json").read_text(encoding="utf-8"))
        cls.templates = {}
        for path in sorted((MAP_DIR / "templates").glob("*.json")):
            cls.templates[path.stem] = json.loads(path.read_text(encoding="utf-8"))
        cls.chunks = []
        cls.rooms = []
        cls.objects = []
        cls.floor_overrides = 0
        cls.with_content = 0
        cls.floors = {}
        for path in sorted((MAP_DIR / "chunks").glob("*/chunk_*.json")):
            data = json.loads(path.read_text(encoding="utf-8"))
            cls.chunks.append(data)
            x0 = int(data["cx"]) * CHUNK
            y0 = int(data["cy"]) * CHUNK
            if data.get("floors"):
                cls.floor_overrides += 1
            if data.get("floors") or data.get("objects") or data.get("rooms"):
                cls.with_content += 1
            for ly, row in enumerate(data.get("floors", [])):
                for lx, char in enumerate(row):
                    cls.floors[(x0 + lx, y0 + ly)] = char
            for obj in data.get("objects", []):
                global_x = x0 + int(obj["x"])
                global_y = y0 + int(obj["y"])
                cls.objects.append((global_x, global_y, obj["sprite"], obj.get("z", 0)))
            for room in data.get("rooms", []):
                cls.rooms.append((x0 + int(room["x"]), y0 + int(room["y"]),
                                  room.get("name", "")))
        cls.width = int(cls.meta["bounds"]["chunksX"]) * CHUNK
        cls.height = int(cls.meta["bounds"]["chunksY"]) * CHUNK

    def test_world_contract_regions_roads_and_pois(self):
        world = self.world
        meta = self.meta
        self.assertEqual(meta["format"], "zomboidlike-map-v1")
        self.assertEqual(world["format"], "zomboidlike-world-v1")
        self.assertEqual(world["mapId"], meta["name"])
        self.assertEqual(world["seed"], 20261109)
        self.assertEqual(world["generatorVersion"], 1)
        self.assertEqual(world["bounds"], meta["bounds"])
        self.assertEqual(world["bounds"]["chunksX"], 36)
        self.assertEqual(world["bounds"]["chunksY"], 36)
        self.assertEqual(meta["generator"]["version"], 1)
        self.assertEqual(meta["generator"]["params"]["forests"], 48)
        self.assertEqual(meta["generator"]["params"]["fields"], 8)
        self.assertEqual(meta["generator"]["params"]["ruins"], 7)
        self.assertEqual(len(world["regions"]), 10)
        self.assertEqual(len(world["nodes"]), 11)
        self.assertEqual(len(world["edges"]), 10)
        self.assertEqual(len(world["pois"]), 3)

        covered = set()
        for region in world["regions"]:
            rect = region["rect"]
            self.assertGreater(rect["chunksW"], 0)
            self.assertGreater(rect["chunksH"], 0)
            self.assertGreaterEqual(rect["chunkX"], 0)
            self.assertGreaterEqual(rect["chunkY"], 0)
            self.assertLessEqual(rect["chunkX"] + rect["chunksW"], 36)
            self.assertLessEqual(rect["chunkY"] + rect["chunksH"], 36)
            for y in range(rect["chunkY"], rect["chunkY"] + rect["chunksH"]):
                for x in range(rect["chunkX"], rect["chunkX"] + rect["chunksW"]):
                    self.assertNotIn((x, y), covered)
                    covered.add((x, y))
            for poi_id in region.get("poiIds", []):
                self.assertIn(poi_id, {poi["id"] for poi in world["pois"]})
        self.assertEqual(covered, {(x, y) for y in range(36) for x in range(36)})

        nodes = {node["id"]: node for node in world["nodes"]}
        adjacency = {node_id: [] for node_id in nodes}
        for edge in world["edges"]:
            self.assertIn(edge["a"], nodes)
            self.assertIn(edge["b"], nodes)
            self.assertNotEqual(edge["a"], edge["b"])
            self.assertGreater(edge["width"], 0)
            adjacency[edge["a"]].append(edge["b"])
            adjacency[edge["b"]].append(edge["a"])
        seen = set()
        stack = [world["nodes"][0]["id"]]
        while stack:
            node_id = stack.pop()
            if node_id in seen:
                continue
            seen.add(node_id)
            stack.extend(adjacency[node_id])
        self.assertEqual(seen, set(nodes))

        centers = []
        for poi in world["pois"]:
            template = self.templates[poi["template"]]
            self.assertEqual(template["format"], "zomboidlike-poi-template-v1")
            self.assertGreater(poi["radius"], 0)
            self.assertGreaterEqual(poi["x"] - poi["radius"], 0)
            self.assertGreaterEqual(poi["y"] - poi["radius"], 0)
            self.assertLessEqual(poi["x"] + poi["radius"], self.width)
            self.assertLessEqual(poi["y"] + poi["radius"], self.height)
            centers.append((poi["x"], poi["y"], poi["id"]))
        for index, (x, y, poi_id) in enumerate(centers):
            for ox, oy, other_id in centers[index + 1:]:
                self.assertGreaterEqual(
                    math.hypot(x - ox, y - oy), 80.0,
                    "%s/%s 距离不足" % (poi_id, other_id))
        self.assertEqual(gen_world.compute_map_hash(MAP_DIR),
                         "736948ac7902ed3e")
        windows = [obj for obj in self.objects
                   if obj[2] in ("wall.window_h", "wall.window_v")]
        self.assertEqual(len(windows), 7)

    def test_templates_are_valid_and_stamped(self):
        self.assertEqual(set(self.templates), {
            "clinic_v1", "farmstead_v1", "checkpoint_v1"})
        all_rooms = {room[2] for room in self.rooms}
        object_sprites = {obj[2] for obj in self.objects}
        for template_id, template in self.templates.items():
            width = template["w"]
            height = template["h"]
            self.assertGreater(width, 0)
            self.assertGreater(height, 0)
            self.assertEqual(len(template["floorRows"]), height)
            self.assertEqual(len(template["objectRows"]), height)
            self.assertIn(template["anchor"]["x"], range(width))
            self.assertIn(template["anchor"]["y"], range(height))
            self.assertEqual(set(template["floorLegend"]), {"g", "d", "w"})
            self.assertIn("D", template["objectLegend"])
            door_count = 0
            for row in template["floorRows"]:
                self.assertEqual(len(row), width)
                for char in row:
                    self.assertIn(char, template["floorLegend"])
            for row in template["objectRows"]:
                self.assertEqual(len(row), width)
                for char in row:
                    if char == ".":
                        continue
                    self.assertIn(char, template["objectLegend"])
                    if char == "D":
                        door_count += 1
            self.assertGreaterEqual(door_count, 1)
            for room in template["rooms"]:
                self.assertGreater(room["w"], 0)
                self.assertGreater(room["h"], 0)
                self.assertLessEqual(room["x"] + room["w"], width)
                self.assertLessEqual(room["y"] + room["h"], height)
                self.assertIn(room["name"], all_rooms)
            for char in template["objectLegend"]:
                if char == "D":
                    self.assertIn("wall.door", object_sprites)
        for poi in self.world["pois"]:
            template = self.templates[poi["template"]]
            x0 = int(math.floor(poi["x"] - template["anchor"]["x"]))
            y0 = int(math.floor(poi["y"] - template["anchor"]["y"]))
            x1 = x0 + template["w"]
            y1 = y0 + template["h"]
            self.assertGreaterEqual(x0, 0)
            self.assertGreaterEqual(y0, 0)
            self.assertLessEqual(x1, self.width)
            self.assertLessEqual(y1, self.height)
            local_objects = [obj for obj in self.objects
                             if x0 <= obj[0] < x1 and y0 <= obj[1] < y1]
            self.assertTrue(any(obj[2] == "wall.brick" for obj in local_objects), poi["id"])
            self.assertTrue(any(obj[2] == "wall.door" for obj in local_objects), poi["id"])
            for room in template["rooms"]:
                self.assertIn(room["name"], all_rooms)

    def test_map_chunks_density_and_boundary(self):
        self.assertEqual(len(self.chunks), 1025)
        self.assertGreaterEqual(self.with_content, 900)
        self.assertGreaterEqual(len(self.objects), 6000)
        self.assertGreaterEqual(len(self.rooms), 17)
        self.assertGreaterEqual(len({room[2] for room in self.rooms}), 17)
        self.assertTrue(any(name.startswith("house_")
                            for name in {room[2] for room in self.rooms}))
        self.assertGreaterEqual(len({room[2] for room in self.rooms
                                     if room[2].startswith("ruin_")}), 7)
        for data in self.chunks:
            self.assertIn(data["cx"], range(36))
            self.assertIn(data["cy"], range(36))
            for obj in data.get("objects", []):
                self.assertGreaterEqual(obj["x"], 0)
                self.assertGreaterEqual(obj["y"], 0)
                self.assertLess(obj["x"], CHUNK)
                self.assertLess(obj["y"], CHUNK)
                self.assertEqual(obj.get("z", 0), 0)
            for row in data.get("floors", []):
                self.assertEqual(len(row), CHUNK)
        for x in range(self.width):
            self.assertEqual(self.floors.get((x, 0)), "a")
            self.assertEqual(self.floors.get((x, self.height - 1)), "a")
        for y in range(self.height):
            self.assertEqual(self.floors.get((0, y)), "a")
            self.assertEqual(self.floors.get((self.width - 1, y)), "a")

    def test_same_seed_and_version_is_byte_deterministic(self):
        with tempfile.TemporaryDirectory() as first, \
                tempfile.TemporaryDirectory() as second:
            gen_world.generate(first, clean=True)
            gen_world.generate(second, clean=True)
            first_files = sorted(path.relative_to(first)
                                 for path in Path(first).rglob("*") if path.is_file())
            second_files = sorted(path.relative_to(second)
                                  for path in Path(second).rglob("*") if path.is_file())
            self.assertEqual(first_files, second_files)
            for relative in first_files:
                left = hashlib.sha256((Path(first) / relative).read_bytes()).hexdigest()
                right = hashlib.sha256((Path(second) / relative).read_bytes()).hexdigest()
                self.assertEqual(left, right, str(relative))


if __name__ == "__main__":
    unittest.main()
