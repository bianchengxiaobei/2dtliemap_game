# -*- coding: utf-8 -*-
"""M31-T2 世界生成器：生成 36×36 有限世界、world.json 与 POI 模板。

用法:
  py -3 tools/gen_world.py --name village_demo_m31 --seed 20261109 --stats build/m31_t2/world_stats.json
  py -3 tools/gen_world.py --name village_demo_m31 --seed 20261109 --chunks 36x36 --clean
"""

import argparse
import json
import math
import os
import random
import shutil
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import gen_map as base

ROOT = TOOLS.parent
CHUNK = base.CHUNK
DEFAULT_FLOOR = base.DEFAULT_FLOOR
MAP_FORMAT = "zomboidlike-map-v1"
WORLD_FORMAT = "zomboidlike-world-v1"
TEMPLATE_FORMAT = "zomboidlike-poi-template-v1"
DEFAULT_NAME = "village_demo_m31"
DEFAULT_SEED = 20261109
DEFAULT_CHUNKS = "36x36"
GEN_VERSION = 1
POI_SPACING = 80.0
FLOOR_LEGEND = {
    "g": "tile.floor.grass",
    "d": "tile.floor.dirt",
    "w": "tile.floor.wood",
}
OBJECT_LEGEND = {
    "#": {"layer": "wall", "sprite": "wall.brick"},
    "D": {"layer": "wall", "sprite": "wall.door"},
    "B": {"layer": "furniture", "sprite": "object.bed"},
    "W": {"layer": "furniture", "sprite": "object.workbench"},
    "C": {"layer": "furniture", "sprite": "object.crate"},
    "R": {"layer": "furniture", "sprite": "object.barricade"},
}
REGION_SPECS = [
    ("west_woods", 0, 0, 4, 6, "forest"),
    ("village_core", 4, 0, 8, 6, "village"),
    ("north_woods", 12, 0, 8, 6, "forest"),
    ("riverlands", 20, 0, 6, 18, "river"),
    ("east_wilds", 26, 0, 10, 36, "wilds"),
    ("west_fields", 0, 6, 8, 6, "farmland"),
    ("north_fields", 8, 6, 12, 6, "farmland"),
    ("south_farmland", 0, 12, 20, 12, "farmland"),
    ("east_approach", 20, 18, 6, 18, "forest"),
    ("deep_south", 0, 24, 20, 12, "forest"),
]
NODE_SPECS = [
    ("village_haven", "settlement", 70.5, 24.5, False),
    ("west_gate", "junction", 6.5, 24.5, False),
    ("clinic_junction", "junction", 150.5, 24.5, False),
    ("clinic", "landmark", 150.0, 64.0, False),
    ("river_junction", "junction", 194.5, 24.5, False),
    ("bridge_west", "junction", 194.0, 120.0, False),
    ("bridge_east", "junction", 214.0, 120.0, False),
    ("east_junction", "junction", 300.0, 120.0, False),
    ("checkpoint", "landmark", 300.0, 280.0, False),
    ("farm_junction", "junction", 70.5, 210.0, False),
    ("farmstead", "landmark", 110.0, 210.0, False),
]
EDGE_SPECS = [
    ("west_gate", "village_haven", "road", 3.0, True),
    ("village_haven", "clinic_junction", "road", 3.0, True),
    ("clinic_junction", "clinic", "road", 2.0, True),
    ("village_haven", "river_junction", "road", 3.0, True),
    ("river_junction", "bridge_west", "road", 3.0, True),
    ("bridge_west", "bridge_east", "bridge", 7.0, True),
    ("bridge_east", "east_junction", "road", 3.0, True),
    ("east_junction", "checkpoint", "road", 3.0, True),
    ("village_haven", "farm_junction", "road", 3.0, True),
    ("farm_junction", "farmstead", "road", 3.0, True),
]
POI_SPECS = [
    {
        "id": "clinic",
        "template": "clinic_v1",
        "x": 150.0,
        "y": 64.0,
        "radius": 12.0,
        "threat": 2,
        "lootProfile": "poi_clinic",
        "region": "north_fields",
    },
    {
        "id": "farmstead",
        "template": "farmstead_v1",
        "x": 110.0,
        "y": 210.0,
        "radius": 16.0,
        "threat": 1,
        "lootProfile": "poi_farmstead",
        "region": "south_farmland",
    },
    {
        "id": "checkpoint",
        "template": "checkpoint_v1",
        "x": 300.0,
        "y": 280.0,
        "radius": 14.0,
        "threat": 3,
        "lootProfile": "poi_checkpoint",
        "region": "east_wilds",
    },
]
VILLAGE_HOUSES = [
    (52, 11, 8, 6),
    (61, 11, 8, 6),
    (74, 11, 8, 6),
    (52, 29, 8, 6),
    (61, 29, 8, 6),
    (74, 29, 8, 6),
]
ROAD_SPECS = [
    (6, 24, 194, 24, 3, "d"),
    (194, 24, 194, 120, 3, "d"),
    (194, 120, 214, 120, 7, "w"),
    (214, 120, 300, 120, 3, "d"),
    (300, 120, 300, 273, 3, "d"),
    (70, 24, 70, 210, 3, "d"),
    (70, 210, 100, 210, 3, "d"),
    (150, 24, 150, 58, 3, "d"),
]
FIELD_SPECS = [
    (118, 72, 24, 9),
    (118, 83, 24, 9),
    (146, 72, 24, 9),
    (146, 83, 24, 9),
    (58, 220, 30, 9),
    (90, 220, 28, 9),
    (122, 220, 28, 9),
    (8, 92, 34, 9),
]
RUIN_SPECS = [
    ("old_farm", 36, 300, 14, 9),
    ("south_camp", 168, 322, 13, 9),
    ("river_hut", 252, 220, 11, 8),
    ("east_outpost", 322, 80, 12, 8),
    ("west_shed", 30, 50, 10, 8),
    ("north_wreck", 170, 30, 10, 8),
    ("west_field_ruin", 50, 80, 10, 8),
]


def _grid(w, h, ch):
    return [[ch for _ in range(w)] for _ in range(h)]


def _fill(grid, x, y, w, h, ch):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            if 0 <= yy < len(grid) and 0 <= xx < len(grid[yy]):
                grid[yy][xx] = ch


def _outline(grid, x, y, w, h, ch):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            if xx in (x, x + w - 1) or yy in (y, y + h - 1):
                grid[yy][xx] = ch


def _put(grid, x, y, ch):
    if 0 <= y < len(grid) and 0 <= x < len(grid[y]):
        grid[y][x] = ch


def _rows(grid):
    return ["".join(row) for row in grid]


def _building(floor, objects, x, y, w, h, door):
    _fill(floor, x, y, w, h, "w")
    _outline(objects, x, y, w, h, "#")
    if door == "north":
        _put(objects, x + w // 2, y, "D")
    elif door == "south":
        _put(objects, x + w // 2, y + h - 1, "D")
    elif door == "west":
        _put(objects, x, y + h // 2, "D")
    elif door == "east":
        _put(objects, x + w - 1, y + h // 2, "D")
    else:
        raise ValueError("unsupported door: %s" % door)


def _template(template_id, name, w, h, anchor_x, anchor_y, floor, objects, rooms):
    return {
        "format": TEMPLATE_FORMAT,
        "id": template_id,
        "name": name,
        "w": w,
        "h": h,
        "anchor": {"x": anchor_x, "y": anchor_y},
        "floorLegend": dict(FLOOR_LEGEND),
        "floorRows": _rows(floor),
        "objectLegend": {key: dict(value) for key, value in OBJECT_LEGEND.items()},
        "objectRows": _rows(objects),
        "rooms": rooms,
    }


def build_templates():
    clinic_floor = _grid(14, 10, "g")
    clinic_objects = _grid(14, 10, ".")
    _building(clinic_floor, clinic_objects, 1, 1, 12, 8, "north")
    _fill(clinic_floor, 5, 0, 5, 1, "d")
    _put(clinic_objects, 3, 3, "B")
    _put(clinic_objects, 3, 4, "B")
    _put(clinic_objects, 10, 3, "B")
    _put(clinic_objects, 10, 4, "B")
    _put(clinic_objects, 6, 5, "W")
    _put(clinic_objects, 5, 6, "C")

    farm_floor = _grid(18, 14, "g")
    farm_objects = _grid(18, 14, ".")
    _building(farm_floor, farm_objects, 1, 4, 9, 6, "west")
    _building(farm_floor, farm_objects, 12, 4, 5, 7, "west")
    _fill(farm_floor, 1, 1, 16, 2, "d")
    _fill(farm_floor, 1, 11, 16, 2, "d")
    _put(farm_floor, 0, 7, "d")
    _put(farm_floor, 10, 7, "d")
    _put(farm_floor, 11, 7, "d")
    _put(farm_objects, 2, 5, "B")
    _put(farm_objects, 3, 6, "W")
    _put(farm_objects, 6, 5, "C")
    _put(farm_objects, 13, 5, "C")
    _put(farm_objects, 15, 5, "C")
    _put(farm_objects, 14, 8, "C")

    checkpoint_floor = _grid(16, 12, "g")
    checkpoint_objects = _grid(16, 12, ".")
    _fill(checkpoint_floor, 1, 1, 14, 10, "d")
    _building(checkpoint_floor, checkpoint_objects, 3, 2, 10, 7, "north")
    _fill(checkpoint_floor, 7, 0, 3, 2, "d")
    _put(checkpoint_objects, 2, 9, "R")
    _put(checkpoint_objects, 5, 9, "R")
    _put(checkpoint_objects, 10, 9, "R")
    _put(checkpoint_objects, 13, 9, "R")
    _put(checkpoint_objects, 4, 4, "W")
    _put(checkpoint_objects, 10, 4, "C")
    _put(checkpoint_objects, 11, 6, "C")

    return [
        _template("clinic_v1", "废弃诊所", 14, 10, 7, 5, clinic_floor,
                  clinic_objects,
                  [{"x": 2, "y": 2, "w": 10, "h": 6, "name": "poi_clinic"}]),
        _template("farmstead_v1", "废弃农场", 18, 14, 9, 7, farm_floor,
                  farm_objects,
                  [{"x": 2, "y": 5, "w": 7, "h": 4, "name": "poi_farmstead_house"},
                   {"x": 13, "y": 5, "w": 3, "h": 5, "name": "poi_farm_barn"}]),
        _template("checkpoint_v1", "废弃检查站", 16, 12, 8, 6, checkpoint_floor,
                  checkpoint_objects,
                  [{"x": 4, "y": 3, "w": 8, "h": 5, "name": "poi_checkpoint"}]),
    ]


def validate_template(template):
    if template.get("format") != TEMPLATE_FORMAT:
        raise ValueError("模板格式错误: %s" % template.get("id"))
    template_id = template.get("id")
    if not template_id or template_id in (".", ".."):
        raise ValueError("模板 id 非法")
    width = int(template["w"])
    height = int(template["h"])
    if width <= 0 or height <= 0:
        raise ValueError("模板尺寸非法: %s" % template_id)
    anchor = template["anchor"]
    if not (0 <= int(anchor["x"]) < width and 0 <= int(anchor["y"]) < height):
        raise ValueError("模板 anchor 越界: %s" % template_id)
    floor_rows = template.get("floorRows", [])
    object_rows = template.get("objectRows", [])
    if len(floor_rows) != height or len(object_rows) != height:
        raise ValueError("模板行数错误: %s" % template_id)
    floor_legend = template.get("floorLegend", {})
    object_legend = template.get("objectLegend", {})
    has_door = False
    for row in floor_rows:
        if len(row) != width:
            raise ValueError("模板 floor 行宽错误: %s" % template_id)
        for ch in row:
            if ch not in floor_legend or floor_legend[ch] not in base.CHAR_OF:
                raise ValueError("模板 floor 字符非法: %s/%s" % (template_id, ch))
    for row in object_rows:
        if len(row) != width:
            raise ValueError("模板 object 行宽错误: %s" % template_id)
        for ch in row:
            if ch == ".":
                continue
            if ch not in object_legend:
                raise ValueError("模板 object 字符非法: %s/%s" % (template_id, ch))
            if ch == "D":
                has_door = True
    if not has_door:
        raise ValueError("模板没有门: %s" % template_id)
    for room in template.get("rooms", []):
        rx, ry, rw, rh = (int(room["x"]), int(room["y"]),
                          int(room["w"]), int(room["h"]))
        if rx < 0 or ry < 0 or rw <= 0 or rh <= 0 or rx + rw > width or ry + rh > height:
            raise ValueError("模板 room 越界: %s" % template_id)


def template_footprint(template, poi):
    x0 = int(math.floor(float(poi["x"]) - float(template["anchor"]["x"])))
    y0 = int(math.floor(float(poi["y"]) - float(template["anchor"]["y"])))
    return x0, y0, int(template["w"]), int(template["h"])


def _rects_overlap(a, b):
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    return ax < bx + bw and bx < ax + aw and ay < by + bh and by < ay + ah


def stamp_template(mapd, template, poi):
    x0, y0, width, height = template_footprint(template, poi)
    if x0 < 0 or y0 < 0 or x0 + width > mapd.w or y0 + height > mapd.h:
        raise ValueError("POI 模板越界: %s" % poi["id"])
    mapd.objects = [
        obj for obj in mapd.objects
        if not (obj[2] == 0 and x0 <= obj[0] < x0 + width and
                y0 <= obj[1] < y0 + height)
    ]
    for yy in range(y0, y0 + height):
        for xx in range(x0, x0 + width):
            mapd.floor.pop((xx, yy), None)
            mapd.blocked.add((xx, yy))
    for ly, row in enumerate(template["floorRows"]):
        for lx, ch in enumerate(row):
            sprite = template["floorLegend"][ch]
            if sprite != DEFAULT_FLOOR:
                mapd.set_floor(x0 + lx, y0 + ly, base.CHAR_OF[sprite])
    for ly, row in enumerate(template["objectRows"]):
        for lx, ch in enumerate(row):
            if ch == ".":
                continue
            entry = template["objectLegend"][ch]
            mapd.add_object(x0 + lx, y0 + ly, 0, entry["layer"], entry["sprite"])
            if entry["sprite"] in base.SOLID_SPRITES:
                mapd.blocked.add((x0 + lx, y0 + ly))
    for room in template.get("rooms", []):
        mapd.add_room(x0 + int(room["x"]), y0 + int(room["y"]),
                      int(room["w"]), int(room["h"]), room["name"])
    return x0, y0, width, height


def paint_rect(mapd, x, y, w, h, floor_char, blocked=False):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            mapd.set_floor(xx, yy, floor_char)
            if blocked:
                mapd.blocked.add((xx, yy))


def paint_road_segment(mapd, cells, x0, y0, x1, y1, width, floor_char):
    half = max(0, width // 2)
    if y0 == y1:
        xa, xb = min(x0, x1), max(x0, x1)
        ya, yb = y0 - half, y0 + half
    elif x0 == x1:
        xa, xb = x0 - half, x0 + half
        ya, yb = min(y0, y1), max(y0, y1)
    else:
        raise ValueError("道路必须是水平或垂直线段")
    for yy in range(ya, yb + 1):
        for xx in range(xa, xb + 1):
            mapd.set_floor(xx, yy, floor_char)
            cells.add((xx, yy))
            mapd.blocked.add((xx, yy))


def paint_village(mapd, rng):
    house_rects = []
    cells = set()
    for x, y, w, h in VILLAGE_HOUSES:
        if not base.gen_house(mapd, rng, x, y, w, h, house_rects, 1):
            raise ValueError("村庄房屋放置失败: %d,%d" % (x, y))
        cells.update((xx, yy) for yy in range(y, y + h) for xx in range(x, x + w))
    plaza = (57, 20, 26, 9)
    paint_rect(mapd, plaza[0], plaza[1], plaza[2], plaza[3], "d", blocked=True)
    cells.update((xx, yy) for yy in range(plaza[1], plaza[1] + plaza[3])
                 for xx in range(plaza[0], plaza[0] + plaza[2]))
    for x, y, sprite in ((76, 21, "object.campfire"), (63, 21, "object.workbench"),
                         (80, 27, "object.crate"), (58, 27, "object.radio")):
        mapd.add_object(x, y, 0, "furniture", sprite)
        mapd.blocked.add((x, y))
        if sprite in base.SOLID_SPRITES:
            cells.add((x, y))
    return cells


def paint_border(mapd, width):
    for y in range(mapd.h):
        for x in range(mapd.w):
            if x < width or y < width or x >= mapd.w - width or y >= mapd.h - width:
                mapd.set_floor(x, y, "a")
                mapd.blocked.add((x, y))


def paint_river(mapd, bridge_y=120):
    for x in (198, 199, 212, 213):
        for y in range(mapd.h):
            mapd.set_floor(x, y, "d")
            mapd.blocked.add((x, y))
    for x in range(200, 212):
        for y in range(mapd.h):
            mapd.set_floor(x, y, "a")
            mapd.blocked.add((x, y))
    for y in range(bridge_y - 4, bridge_y + 5):
        for x in range(196, 215):
            mapd.set_floor(x, y, "w")
            mapd.blocked.discard((x, y))


def paint_fields(mapd, limit):
    for x, y, w, h in FIELD_SPECS[:limit]:
        paint_rect(mapd, x, y, w, h, "d", blocked=True)


def paint_ruins(mapd, rng, limit):
    for index, (name, x, y, w, h) in enumerate(RUIN_SPECS[:limit]):
        mapd.objects = [
            obj for obj in mapd.objects
            if not (obj[2] == 0 and x <= obj[0] < x + w and y <= obj[1] < y + h)
        ]
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                mapd.floor.pop((xx, yy), None)
        paint_rect(mapd, x, y, w, h, "d", blocked=True)
        for xx in range(x, x + w):
            if rng.random() < 0.6:
                mapd.add_object(xx, y, 0, "wall", "wall.brick")
                mapd.blocked.add((xx, y))
            if rng.random() < 0.6:
                mapd.add_object(xx, y + h - 1, 0, "wall", "wall.brick")
                mapd.blocked.add((xx, y + h - 1))
        for yy in range(y, y + h):
            if rng.random() < 0.6:
                mapd.add_object(x, yy, 0, "wall", "wall.brick")
                mapd.blocked.add((x, yy))
        for _ in range(3):
            xx = rng.randrange(x + 1, x + w - 1)
            yy = rng.randrange(y + 1, y + h - 1)
            if (xx, yy) not in mapd.blocked:
                mapd.add_object(xx, yy, 0, "furniture", "object.crate")
        mapd.add_room(x + 1, y + 1, max(1, w - 2), max(1, h - 2), "ruin_%s" % name)


def paint_forest(mapd, rng, patches, tree_chance):
    for _ in range(patches):
        cx = rng.randrange(4, mapd.w - 4)
        cy = rng.randrange(4, mapd.h - 4)
        radius = rng.randint(6, 13)
        radius2 = radius * radius
        for yy in range(cy - radius, cy + radius + 1):
            for xx in range(cx - radius, cx + radius + 1):
                if not mapd.in_bounds(xx, yy):
                    continue
                if (xx, yy) in mapd.blocked or (xx, yy) in mapd.floor:
                    continue
                if (xx - cx) ** 2 + (yy - cy) ** 2 <= radius2 and rng.random() < tree_chance:
                    mapd.add_object(xx, yy, 0, "furniture", "object.tree")
                    mapd.blocked.add((xx, yy))
    singles = max(20, (mapd.w * mapd.h) // 700)
    placed = 0
    for _ in range(singles * 8):
        if placed >= singles:
            break
        xx = rng.randrange(2, mapd.w - 2)
        yy = rng.randrange(2, mapd.h - 2)
        if (xx, yy) in mapd.blocked or (xx, yy) in mapd.floor:
            continue
        mapd.add_object(xx, yy, 0, "furniture", "object.tree")
        mapd.blocked.add((xx, yy))
        placed += 1


def paint_tufts(mapd, rng, count):
    placed = 0
    for _ in range(count * 5):
        if placed >= count:
            break
        xx = rng.randrange(2, mapd.w - 2)
        yy = rng.randrange(2, mapd.h - 2)
        if (xx, yy) in mapd.blocked or (xx, yy) in mapd.floor:
            continue
        mapd.add_object(xx, yy, 0, "furniture", "decor.grass_tuft")
        placed += 1


def build_world(name, seed, chunks_x, chunks_y):
    bounds = {"chunkX0": 0, "chunkY0": 0, "chunksX": chunks_x, "chunksY": chunks_y}
    poi_regions = {}
    for poi in POI_SPECS:
        poi_regions.setdefault(poi["region"], []).append(poi["id"])
    regions = []
    for region_id, x, y, w, h, biome in REGION_SPECS:
        regions.append({
            "id": region_id,
            "rect": {"chunkX": x, "chunkY": y, "chunksW": w, "chunksH": h},
            "biome": biome,
            "poiIds": list(poi_regions.get(region_id, [])),
        })
    nodes = [
        {"id": node_id, "kind": kind, "x": x, "y": y, "isolated": isolated}
        for node_id, kind, x, y, isolated in NODE_SPECS
    ]
    edges = [
        {"a": a, "b": b, "kind": kind, "width": width, "vehicle": vehicle}
        for a, b, kind, width, vehicle in EDGE_SPECS
    ]
    pois = [
        {
            "id": poi["id"],
            "template": poi["template"],
            "x": poi["x"],
            "y": poi["y"],
            "radius": poi["radius"],
            "threat": poi["threat"],
            "lootProfile": poi["lootProfile"],
        }
        for poi in POI_SPECS
    ]
    return {
        "format": WORLD_FORMAT,
        "mapId": name,
        "seed": seed,
        "generatorVersion": GEN_VERSION,
        "bounds": bounds,
        "regions": regions,
        "nodes": nodes,
        "edges": edges,
        "pois": pois,
        "spawn": {
            "defaultZombieDensity": 0.012,
            "biomes": {"forest": 0.008, "village": 0.020, "farmland": 0.010,
                       "river": 0.006, "wilds": 0.014},
        },
    }


def connected_nodes(nodes, edges):
    ids = {node["id"] for node in nodes}
    adjacency = {node_id: [] for node_id in ids}
    isolated = {node["id"] for node in nodes if node.get("isolated", False)}
    for edge in edges:
        a = edge["a"]
        b = edge["b"]
        if a not in ids or b not in ids or a == b:
            return False
        adjacency[a].append(b)
        adjacency[b].append(a)
    active = [node_id for node_id in ids if node_id not in isolated]
    if not active:
        return True
    seen = set()
    stack = [active[0]]
    while stack:
        node_id = stack.pop()
        if node_id in seen:
            continue
        seen.add(node_id)
        stack.extend(adjacency[node_id])
    return seen == set(active)


def validate_world(mapd, world, templates, settlement, poi_spacing, road_cells, village_cells):
    bounds = world["bounds"]
    world_w = bounds["chunksX"] * CHUNK
    world_h = bounds["chunksY"] * CHUNK
    if bounds["chunksX"] <= 0 or bounds["chunksY"] <= 0:
        raise ValueError("world bounds 非法")
    region_rects = []
    for region in world["regions"]:
        rect = region["rect"]
        if rect["chunksW"] <= 0 or rect["chunksH"] <= 0:
            raise ValueError("region 尺寸非法: %s" % region["id"])
        if rect["chunkX"] < 0 or rect["chunkY"] < 0 or \
                rect["chunkX"] + rect["chunksW"] > bounds["chunksX"] or \
                rect["chunkY"] + rect["chunksH"] > bounds["chunksY"]:
            raise ValueError("region 越界: %s" % region["id"])
        for other in region_rects:
            if _rects_overlap((rect["chunkX"], rect["chunkY"], rect["chunksW"], rect["chunksH"]), other):
                raise ValueError("region 重叠: %s" % region["id"])
        region_rects.append((rect["chunkX"], rect["chunkY"], rect["chunksW"], rect["chunksH"]))
    covered = set()
    for x, y, w, h in region_rects:
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                if (xx, yy) in covered:
                    raise ValueError("region 覆盖重复")
                covered.add((xx, yy))
    if covered != {(x, y) for y in range(bounds["chunksY"]) for x in range(bounds["chunksX"])}:
        raise ValueError("region 未覆盖完整世界")
    if not connected_nodes(world["nodes"], world["edges"]):
        raise ValueError("道路节点图不连通")
    node_ids = {node["id"] for node in world["nodes"]}
    for edge in world["edges"]:
        if edge["a"] not in node_ids or edge["b"] not in node_ids:
            raise ValueError("edge 引用缺失节点")
        if edge["width"] <= 0:
            raise ValueError("edge width 非法")
    poi_ids = set()
    poi_rects = []
    centers = [(settlement[0], settlement[1])]
    for poi in world["pois"]:
        if poi["id"] in poi_ids:
            raise ValueError("POI id 重复: %s" % poi["id"])
        poi_ids.add(poi["id"])
        template = template_by_id(templates, poi["template"])
        rect = template_footprint(template, poi)
        x0, y0, w, h = rect
        if x0 < 0 or y0 < 0 or x0 + w > world_w or y0 + h > world_h:
            raise ValueError("POI 模板越界: %s" % poi["id"])
        if poi["radius"] <= 0 or poi["x"] - poi["radius"] < 0 or \
                poi["y"] - poi["radius"] < 0 or \
                poi["x"] + poi["radius"] > world_w or \
                poi["y"] + poi["radius"] > world_h:
            raise ValueError("POI 半径越界: %s" % poi["id"])
        if any(_rects_overlap(rect, other) for other in poi_rects):
            raise ValueError("POI 模板重叠: %s" % poi["id"])
        if any(_rects_overlap(rect, other) for other in road_cells):
            raise ValueError("POI 模板压到道路: %s" % poi["id"])
        if any(x0 <= px < x0 + w and y0 <= py < y0 + h for px, py in village_cells):
            raise ValueError("POI 模板压到村庄: %s" % poi["id"])
        poi_rects.append(rect)
        centers.append((poi["x"], poi["y"]))
    for i, a in enumerate(centers):
        for b in centers[i + 1:]:
            distance = math.hypot(a[0] - b[0], a[1] - b[1])
            if distance < poi_spacing:
                raise ValueError("主地标距离过近: %.1f" % distance)
    for region in world["regions"]:
        for poi_id in region.get("poiIds", []):
            if poi_id not in poi_ids:
                raise ValueError("region 引用缺失 POI: %s" % poi_id)
    for obj in mapd.objects:
        if not (0 <= obj[0] < mapd.w and 0 <= obj[1] < mapd.h):
            raise ValueError("对象越界: %s" % (obj,))
    for x, y in mapd.floor:
        if not (0 <= x < mapd.w and 0 <= y < mapd.h):
            raise ValueError("地块越界: %d,%d" % (x, y))
    for x in range(mapd.w):
        for y in (0, mapd.h - 1):
            if mapd.floor.get((x, y)) != "a":
                raise ValueError("边界未闭合: %d,%d" % (x, y))
    for y in range(mapd.h):
        for x in (0, mapd.w - 1):
            if mapd.floor.get((x, y)) != "a":
                raise ValueError("边界未闭合: %d,%d" % (x, y))


def template_by_id(templates, template_id):
    for template in templates:
        if template["id"] == template_id:
            return template
    raise ValueError("模板不存在: %s" % template_id)


def write_templates(out_dir, templates):
    template_dir = out_dir / "templates"
    template_dir.mkdir(parents=True, exist_ok=True)
    for template in templates:
        path = template_dir / (template["id"] + ".json")
        with path.open("w", encoding="utf-8", newline="\n") as handle:
            json.dump(template, handle, ensure_ascii=False, indent=1)
            handle.write("\n")


def write_json(path, payload):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=1)
        handle.write("\n")


def compute_map_hash(out_dir):
    hash_value = 14695981039346656037
    prime = 1099511628211
    paths = sorted(
        (path for path in Path(out_dir).rglob("*") if path.is_file()),
        key=lambda path: path.relative_to(out_dir).as_posix(),
    )
    for path in paths:
        relative = path.relative_to(out_dir).as_posix().encode("utf-8")
        data = path.read_bytes()
        for byte in len(relative).to_bytes(4, "little"):
            hash_value ^= byte
            hash_value = (hash_value * prime) & ((1 << 64) - 1)
        for byte in relative:
            hash_value ^= byte
            hash_value = (hash_value * prime) & ((1 << 64) - 1)
        for byte in len(data).to_bytes(8, "little"):
            hash_value ^= byte
            hash_value = (hash_value * prime) & ((1 << 64) - 1)
        for byte in data:
            hash_value ^= byte
            hash_value = (hash_value * prime) & ((1 << 64) - 1)
    return "%016x" % hash_value


def generate(out_dir, name=DEFAULT_NAME, seed=DEFAULT_SEED, chunks_x=36, chunks_y=36,
             forests=48, tree_chance=0.42, fields=8, ruins=7, tufts=800,
             poi_spacing=POI_SPACING, clean=False, stats_path=None):
    out_dir = Path(out_dir)
    if clean and out_dir.exists():
        shutil.rmtree(str(out_dir))
    out_dir.mkdir(parents=True, exist_ok=True)
    templates = build_templates()
    for template in templates:
        validate_template(template)
    write_templates(out_dir, templates)
    world = build_world(name, seed, chunks_x, chunks_y)
    mapd = base.MapData(chunks_x, chunks_y)
    rng = random.Random(seed)
    road_cells = set()
    paint_river(mapd, 120)
    paint_border(mapd, 2)
    village_cells = paint_village(mapd, rng)
    for x0, y0, x1, y1, width, floor_char in ROAD_SPECS:
        paint_road_segment(mapd, road_cells, x0, y0, x1, y1, width, floor_char)
    for poi in POI_SPECS:
        template = template_by_id(templates, poi["template"])
        stamp_template(mapd, template, poi)
    paint_road_segment(mapd, road_cells, 150, 24, 150, 58, 3, "d")
    paint_road_segment(mapd, road_cells, 70, 210, 100, 210, 3, "d")
    paint_road_segment(mapd, road_cells, 300, 120, 300, 273, 3, "d")
    paint_fields(mapd, fields)
    paint_ruins(mapd, rng, ruins)
    paint_forest(mapd, rng, forests, tree_chance)
    paint_tufts(mapd, rng, tufts)
    road_rects = [(x, y, 1, 1) for x, y in road_cells]
    validate_world(mapd, world, templates, (70.5, 24.5), poi_spacing, road_rects,
                   village_cells)
    map_meta = {
        "format": MAP_FORMAT,
        "name": name,
        "generator": {
            "tool": "tools/gen_world.py",
            "version": GEN_VERSION,
            "seed": seed,
            "params": {
                "chunks": "%dx%d" % (chunks_x, chunks_y),
                "forests": forests,
                "treeChance": tree_chance,
                "fields": fields,
                "ruins": ruins,
                "tufts": tufts,
                "poiSpacing": poi_spacing,
                "worldVersion": GEN_VERSION,
                "pois": len(POI_SPECS),
                "templates": len(templates),
            },
        },
        "bounds": {"chunkX0": 0, "chunkY0": 0, "chunksX": chunks_x, "chunksY": chunks_y},
        "terrain": {"defaultFloor": DEFAULT_FLOOR},
        "legend": dict(base.LEGEND),
    }
    written = base.write_map(mapd, str(out_dir), map_meta)
    write_json(out_dir / "world.json", world)
    total_chunks = chunks_x * chunks_y
    floor_counts = {"grass": 0, "dirt": 0, "wood": 0, "water": 0}
    for (x, y), char in mapd.floor.items():
        if 0 <= x < mapd.w and 0 <= y < mapd.h:
            floor_counts[{"g": "grass", "d": "dirt", "w": "wood", "a": "water"}[char]] += 1
    object_counts = {
        "trees": sum(1 for obj in mapd.objects if obj[4] == "object.tree"),
        "tufts": sum(1 for obj in mapd.objects if obj[4] == "decor.grass_tuft"),
        "walls": sum(1 for obj in mapd.objects if obj[4].startswith("wall.")),
    }
    min_distance = min(
        math.hypot(a[0] - b[0], a[1] - b[1])
        for i, a in enumerate([(poi["x"], poi["y"]) for poi in POI_SPECS] +
                               [(70.5, 24.5)])
        for b in ([(poi["x"], poi["y"]) for poi in POI_SPECS] +
                  [(70.5, 24.5)])[i + 1:]
    )
    stats = {
        "mapId": name,
        "seed": seed,
        "generatorVersion": GEN_VERSION,
        "bounds": {"chunkX0": 0, "chunkY0": 0, "chunksX": chunks_x, "chunksY": chunks_y},
        "chunksTotal": total_chunks,
        "chunksWritten": written,
        "sparseChunks": total_chunks - written,
        "nonSparseRatio": round(written / max(1, total_chunks), 4),
        "objects": len(mapd.objects),
        "objectKinds": object_counts,
        "rooms": len(mapd.rooms),
        "floorOverrides": len(mapd.floor),
        "floorKinds": floor_counts,
        "templates": [template["id"] for template in templates],
        "pois": [
            {"id": poi["id"], "x": poi["x"], "y": poi["y"],
             "radius": poi["radius"], "template": poi["template"]}
            for poi in POI_SPECS
        ],
        "poiMinDistance": round(min_distance, 1),
        "regions": len(REGION_SPECS),
        "nodes": len(NODE_SPECS),
        "edges": len(EDGE_SPECS),
        "fieldPlots": min(fields, len(FIELD_SPECS)),
        "ruins": min(ruins, len(RUIN_SPECS)),
        "boundaryClosed": True,
        "mapHash": compute_map_hash(out_dir),
    }
    if stats_path:
        write_json(Path(stats_path), stats)
    return stats


def parse_chunks(value):
    try:
        chunks_x, chunks_y = (int(part) for part in value.lower().split("x"))
    except (ValueError, TypeError):
        raise argparse.ArgumentTypeError("--chunks 必须形如 36x36")
    if chunks_x <= 0 or chunks_y <= 0:
        raise argparse.ArgumentTypeError("--chunks 尺寸必须为正")
    return chunks_x, chunks_y


def main():
    parser = argparse.ArgumentParser(description="ZomboidLike M31 世界生成器")
    parser.add_argument("--name", default=DEFAULT_NAME)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--chunks", type=parse_chunks, default=(36, 36))
    parser.add_argument("--forests", type=int, default=48)
    parser.add_argument("--tree-chance", type=float, default=0.42)
    parser.add_argument("--fields", type=int, default=8)
    parser.add_argument("--ruins", type=int, default=7)
    parser.add_argument("--tufts", type=int, default=800)
    parser.add_argument("--poi-spacing", type=float, default=POI_SPACING)
    parser.add_argument("--out", default="")
    parser.add_argument("--stats", default="")
    parser.add_argument("--clean", action="store_true")
    args = parser.parse_args()
    out_dir = Path(args.out) if args.out else ROOT / "assets" / "maps" / args.name
    stats = generate(
        out_dir, name=args.name, seed=args.seed,
        chunks_x=args.chunks[0], chunks_y=args.chunks[1],
        forests=max(0, args.forests), tree_chance=max(0.0, min(1.0, args.tree_chance)),
        fields=max(0, args.fields), ruins=max(0, args.ruins), tufts=max(0, args.tufts),
        poi_spacing=args.poi_spacing, clean=args.clean,
        stats_path=args.stats or None,
    )
    print("map    : %s" % out_dir)
    print("size   : %dx%d chunks" % (args.chunks[0], args.chunks[1]))
    print("content: %d objects | %d rooms | %d templates | %d POIs" % (
        stats["objects"], stats["rooms"], len(stats["templates"]), len(stats["pois"])))
    print("chunks : %d/%d written (sparse %d)" % (
        stats["chunksWritten"], stats["chunksTotal"], stats["sparseChunks"]))
    print("mapHash: %s" % stats["mapHash"])
    if args.stats:
        print("stats  : %s" % args.stats)


if __name__ == "__main__":
    main()
