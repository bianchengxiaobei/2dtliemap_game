# -*- coding: utf-8 -*-
"""
ZomboidLike 地图生成器（格式 v1；生成器 v5，M19-F2/F6-T1/M24-T2 POI）

用法:
  py -3 tools/gen_map.py --name village_demo --seed 20261016 --chunks 12x12
  py -3 tools/gen_map.py --name stress_100x100 --seed 7 --chunks 100x100

输出: assets/maps/<name>/{map.json, chunks/c<cellX>_<cellY>/chunk_<cx>_<cy>.json}

原则:
  - 确定性: 同一 seed + 同一参数 => 逐字节相同的输出（random.Random + 固定遍历顺序）
  - 稀疏: 与默认地面一致且无对象/房间的 chunk 不落盘（流式加载快速路径）
  - 内容: 泥土补丁 + 横竖道路 + 村庄（砖墙木地板房子、门、房间）+ 树/草丛
          + M19-F2/F6-T1 村庄外 5 个固定 POI（加油站/诊所/农场仓/医院/军事检查站）\n          + M24-T2 新增 6 个固定 POI（超市/警察局/消防站/汽车旅馆/教堂/仓库）
"""

import argparse
import json
import math
import os
import random

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHUNK = 10
CHUNKS_PER_CELL = 30

DEFAULT_FLOOR = "tile.floor.grass"
LEGEND = {"g": "tile.floor.grass", "d": "tile.floor.dirt", "w": "tile.floor.wood",
          "a": "tile.floor.water"}
CHAR_OF = {v: k for k, v in LEGEND.items()}
DEFAULT_CHAR = CHAR_OF[DEFAULT_FLOOR]


# 阻挡移动的精灵（M4 物理/A*）：写对象时附 "solid": true（解析端缺省 false，向后兼容）
SOLID_SPRITES = {"wall.brick", "object.tree", "object.workbench", "object.campfire",
                 "object.barricade"}


# M19-F2/M24-T2：村庄外的固定小型 POI。坐标与 config/game.json 的 pois.defs 交叉校验；
# 建筑复用现有墙/门/家具图集，不新增 tile 集。每个 POI 有独立搜刮表与目标容器。
POI_SPECS = [
    {
        "id": "gas_station",
        "name": "废弃加油站",
        "room": "poi_gas_station",
        "x0": 27, "y0": 18, "w": 9, "h": 8,
        "door": "south",
        "props": [(28, 20, "object.workbench")],
        "apron": [(x, y) for y in (25, 26) for x in range(29, 34)],
    },
    {
        "id": "clinic",
        "name": "废弃诊所",
        "room": "poi_clinic",
        "x0": 19, "y0": 52, "w": 8, "h": 7,
        "door": "west",
        "props": [(21, 53, "object.bed")],
        "apron": [(x, y) for x in (18, 19) for y in range(54, 57)],
    },
    {
        "id": "farm_barn",
        "name": "废弃农场仓",
        "room": "poi_farm_barn",
        "x0": 82, "y0": 58, "w": 9, "h": 9,
        "door": "west",
        "props": [(84, 59, "object.workbench")],
        "apron": [(x, y) for x in (81, 82) for y in range(61, 64)],
    },
    {
        "id": "hospital",
        "name": "圣玛丽医院",
        "room": "poi_hospital",
        "x0": 40, "y0": 80, "w": 12, "h": 9,
        "door": "south",
        "props": [(42, 82, "object.bed"), (42, 83, "object.bed"),
                  (42, 86, "object.bed"), (49, 82, "object.bed"),
                  (49, 83, "object.bed"), (49, 86, "object.bed")],
        "interiorWalls": ([(44, y) for y in range(81, 87) if y != 84] +
                          [(47, y) for y in range(81, 87) if y != 84]),
        "apron": [(x, y) for y in range(89, 94) for x in (45, 46)],
    },
    {
        "id": "military_checkpoint",
        "name": "军事检查站",
        "room": "poi_military_checkpoint",
        "x0": 100, "y0": 18, "w": 10, "h": 7,
        "door": "south",
        "props": [(102, 20, "object.barricade"), (107, 20, "object.barricade"),
                  (102, 23, "object.barricade"), (107, 23, "object.barricade"),
                  (104, 20, "object.workbench"), (106, 23, "object.crate")],
        "apron": [(x, y) for y in (25, 26, 27) for x in (104, 105)],
    },
    # M24-T2：6 个新固定 POI。全部避开道路/村庄/既有 POI；固定坐标不消耗 RNG，
    # 保证旧地图内容与新增 POI 的生成结果确定。
    {
        "id": "supermarket",
        "name": "废弃超市",
        "room": "poi_supermarket",
        "x0": 40, "y0": 6, "w": 12, "h": 8,
        "door": "south",
        "props": [(42, 8, "object.fridge"), (49, 8, "object.fridge"),
                  (45, 7, "object.workbench")],
        "apron": [(46, y) for y in range(14, 18)],
    },
    {
        "id": "police_station",
        "name": "废弃警察局",
        "room": "poi_police_station",
        "x0": 86, "y0": 6, "w": 11, "h": 9,
        "door": "west",
        "props": [(88, 8, "object.workbench"), (87, 7, "object.locker"),
                  (95, 10, "object.locker")],
        "interiorWalls": [(90, y) for y in (7, 8, 10, 11, 12)],
        "apron": [(x, 10) for x in range(82, 86)],
    },
    {
        "id": "fire_station",
        "name": "废弃消防站",
        "room": "poi_fire_station",
        "x0": 48, "y0": 34, "w": 11, "h": 9,
        "door": "south",
        "props": [(50, 36, "object.workbench"), (56, 36, "object.workbench")],
        "interiorWalls": [(x, 38) for x in (49, 50, 51, 52, 54, 55, 56, 57)],
        "apron": [(53, y) for y in range(43, 46)],
    },
    {
        "id": "motel",
        "name": "废弃汽车旅馆",
        "room": "poi_motel",
        "x0": 4, "y0": 36, "w": 12, "h": 8,
        "door": "south",
        "props": [(5, 37, "object.bed"), (5, 38, "object.bed"),
                  (14, 37, "object.fridge"), (14, 38, "object.bed")],
        "interiorWalls": [(8, y) for y in (37, 38, 40, 41)],
        "apron": [(10, y) for y in range(44, 47)],
    },
    {
        "id": "church",
        "name": "废弃教堂",
        "room": "poi_church",
        "x0": 50, "y0": 104, "w": 10, "h": 12,
        "door": "south",
        "props": [(52, 109, "object.barricade"), (53, 109, "object.barricade"),
                  (57, 109, "object.barricade"), (58, 109, "object.barricade")],
        "interiorWalls": [(x, 111) for x in (51, 52, 53, 57, 58)],
        "apron": [(55, y) for y in range(116, 119)],
    },
    {
        "id": "warehouse",
        "name": "废弃仓库",
        "room": "poi_warehouse",
        "x0": 100, "y0": 84, "w": 12, "h": 10,
        "door": "west",
        "props": [(102, 86, "object.workbench"), (105, 85, "object.crate"),
                  (106, 92, "object.crate"), (101, 92, "object.crate")],
        "interiorWalls": [(104, y) for y in (85, 86, 87, 90, 91, 92)],
        "apron": [(x, 89) for x in range(96, 100)],
    },
]


def _object_json(o, x0, y0):
    """对象 tuple → chunk 文件 JSON 条目（solid 仅在 true 时写出，保持文件精简）"""
    entry = {"x": o[0] - x0, "y": o[1] - y0, "z": o[2], "layer": o[3], "sprite": o[4]}
    if o[4] in SOLID_SPRITES:
        entry["solid"] = True
    return entry


class MapData:
    def __init__(self, chunks_x, chunks_y):
        self.chunks_x = chunks_x
        self.chunks_y = chunks_y
        self.w = chunks_x * CHUNK
        self.h = chunks_y * CHUNK
        self.floor = {}  # (x, y) -> legend char（只存非默认）
        self.objects = []  # (x, y, z, layer, sprite)
        self.rooms = []  # (x, y, w, h, id, name)
        self.blocked = set()  # (x, y) 不可放树/草丛
        self.next_room_id = 1
        self.next_house_id = 1  # 房号（命名用）与房间 id 解耦：楼上层也会占用 room id

    def in_bounds(self, x, y):
        return 0 <= x < self.w and 0 <= y < self.h

    def set_floor(self, x, y, ch):
        if self.in_bounds(x, y) and ch != DEFAULT_CHAR:
            self.floor[(x, y)] = ch

    def add_object(self, x, y, z, layer, sprite):
        self.objects.append((x, y, z, layer, sprite))

    def add_room(self, x, y, w, h, name, z=0):
        rid = self.next_room_id
        self.next_room_id += 1
        self.rooms.append((x, y, w, h, rid, name, z))
        return rid


def gen_dirt_patches(mapd, rng, count):
    for _ in range(count):
        cx = rng.randrange(mapd.w)
        cy = rng.randrange(mapd.h)
        r = rng.randint(8, 16)
        r2 = r * r
        for y in range(cy - r, cy + r + 1):
            for x in range(cx - r, cx + r + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r2:
                    mapd.set_floor(x, y, "d")


def gen_roads(mapd, rng, count):
    """横竖泥土路（宽 3），返回 (横路 y 列表, 竖路 x 列表) 供村庄定位"""
    horiz, vert = [], []
    for _ in range(count):
        y = rng.randrange(4, mapd.h - 4)
        horiz.append(y)
        for x in range(mapd.w):
            for dy in (0, 1, 2):
                mapd.set_floor(x, y + dy, "d")
                mapd.blocked.add((x, y + dy))
        x0 = rng.randrange(4, mapd.w - 4)
        vert.append(x0)
        for y in range(mapd.h):
            for dx in (0, 1, 2):
                mapd.set_floor(x0 + dx, y, "d")
                mapd.blocked.add((x0 + dx, y))
    return horiz, vert


def rects_overlap(ax, ay, aw, ah, bx, by, bw, bh, margin=1):
    return not (
        ax + aw + margin <= bx
        or bx + bw + margin <= ax
        or ay + ah + margin <= by
        or by + bh + margin <= ay
    )


def gen_house(mapd, rng, x0, y0, w, h, house_rects, stories=1):
    """一栋房子：砖墙一圈 + 一扇南侧门 + 木地板 + 单个房间；stories>=2 时加楼上（z=1）。
    楼上不消耗 rng（复用地面门位置），保证加层不影响其余布局的确定性。"""
    for rx, ry, rw, rh in house_rects:
        if rects_overlap(x0, y0, w, h, rx, ry, rw, rh):
            return False
    if not (mapd.in_bounds(x0, y0) and mapd.in_bounds(x0 + w - 1, y0 + h - 1)):
        return False
    if x0 < 2 or y0 < 2 or x0 + w > mapd.w - 2 or y0 + h > mapd.h - 2:
        return False

    door_x = x0 + 1 + rng.randrange(max(1, w - 3))
    for ly in range(h):
        for lx in range(w):
            x, y = x0 + lx, y0 + ly
            perimeter = lx == 0 or ly == 0 or lx == w - 1 or ly == h - 1
            if perimeter:
                if ly == h - 1 and x == door_x:
                    mapd.add_object(x, y, 0, "wall", "wall.door")
                else:
                    mapd.add_object(x, y, 0, "wall", "wall.brick")
            mapd.set_floor(x, y, "w")
            mapd.blocked.add((x, y))

    house_no = mapd.next_house_id
    mapd.next_house_id += 1
    house_rects.append((x0, y0, w, h))
    mapd.add_room(x0 + 1, y0 + 1, w - 2, h - 2, "house_%d" % house_no)

    # 楼上（z=1）：同footprint 砖墙 + 木地板（objects 的 floor 层表达，v1 格式）
    for story in range(2, stories + 1):
        z = story - 1
        for ly in range(h):
            for lx in range(w):
                x, y = x0 + lx, y0 + ly
                perimeter = lx == 0 or ly == 0 or lx == w - 1 or ly == h - 1
                if perimeter:
                    if ly == h - 1 and x == door_x:
                        mapd.add_object(x, y, z, "wall", "wall.door")
                    else:
                        mapd.add_object(x, y, z, "wall", "wall.brick")
                else:
                    mapd.add_object(x, y, z, "floor", "tile.floor.wood")
        mapd.add_room(x0 + 1, y0 + 1, w - 2, h - 2, "house_%d_upper" % house_no, z=1)
    return True


def gen_village(mapd, rng, center_x, center_y, house_rects):
    target = rng.randint(2, 5)
    placed = 0
    for _ in range(target * 8):
        if placed >= target:
            break
        w = rng.randint(5, 7)
        h = rng.randint(4, 6)
        x0 = center_x + rng.randint(-9, 6 - w)
        y0 = center_y + rng.randint(-8, 4 - h)
        stories = 2 if placed == 0 else 1  # 每村第一栋带楼上（z=1），演示多 z 绘制
        if gen_house(mapd, rng, x0, y0, w, h, house_rects, stories):
            placed += 1
    return placed


def gen_objective_poi(mapd):
    for x, y, sprite in ((70, 25, "object.workbench"),
                         (74, 25, "object.campfire"),
                         (72, 20, "object.radio"),
                         (66, 28, "object.extraction")):
        if mapd.in_bounds(x, y):
            mapd.add_object(x, y, 0, "furniture", sprite)


def gen_pois(mapd):
    """M19-F2/F6-T1：生成村庄外固定 POI（固定坐标、无 RNG 消耗）。

    先清掉建筑占地及周边树/草丛，再铺木地板、砌墙、开门、加房间和
    固定家具；`interiorWalls` 可加带门洞的室内隔墙，门外铺一小块泥地
    接入现有道路。POI 容器由运行时按 config/game.json 生成，避免把
    “可搜刮状态”写进母地图。
    """
    for poi in POI_SPECS:
        x0 = poi["x0"]
        y0 = poi["y0"]
        w = poi["w"]
        h = poi["h"]
        clear = (x0 - 3, y0 - 3, w + 6, h + 6)
        cx0, cy0, cw, ch = clear
        mapd.objects = [
            o for o in mapd.objects
            if not (o[2] == 0 and cx0 <= o[0] < cx0 + cw and
                    cy0 <= o[1] < cy0 + ch)
        ]
        for ly in range(h):
            for lx in range(w):
                mapd.floor.pop((x0 + lx, y0 + ly), None)

        door_x = x0 + w // 2
        door_y = y0 + h - 1
        if poi["door"] == "west":
            door_x = x0
            door_y = y0 + h // 2
        elif poi["door"] != "south":
            raise SystemExit("未知 POI 门朝向: %s" % poi["door"])

        for ly in range(h):
            for lx in range(w):
                x, y = x0 + lx, y0 + ly
                perimeter = lx == 0 or ly == 0 or lx == w - 1 or ly == h - 1
                if perimeter:
                    sprite = "wall.door" if (x, y) == (door_x, door_y) else "wall.brick"
                    mapd.add_object(x, y, 0, "wall", sprite)
                    mapd.blocked.add((x, y))
                mapd.set_floor(x, y, "w")
        for x, y in poi["apron"]:
            mapd.set_floor(x, y, "d")
        for x, y in poi.get("interiorWalls", ()):
            mapd.add_object(x, y, 0, "wall", "wall.brick")
            mapd.blocked.add((x, y))
        for x, y, sprite in poi["props"]:
            mapd.add_object(x, y, 0, "furniture", sprite)
            if sprite in SOLID_SPRITES:
                mapd.blocked.add((x, y))
        mapd.add_room(x0 + 1, y0 + 1, w - 2, h - 2, poi["room"])


def gen_pond(mapd, cx=80, cy=34, radius=4):
    """固定小池塘（M23-F2a-T3）：水面 + 一圈可走泥岸，保证出生点附近可钓鱼。
    不消耗 RNG，后置清理池塘/岸边的树与草丛，不改变既有地图布局。"""
    shore = radius + 1
    for y in range(cy - shore, cy + shore + 1):
        for x in range(cx - shore, cx + shore + 1):
            d2 = (x - cx) ** 2 + (y - cy) ** 2
            if d2 > shore * shore:
                continue
            mapd.objects = [
                o for o in mapd.objects
                if not (o[2] == 0 and o[0] == x and o[1] == y)
            ]
            if d2 <= radius * radius:
                mapd.set_floor(x, y, "a")
                mapd.blocked.add((x, y))
            else:
                mapd.set_floor(x, y, "d")


def gen_trees(mapd, rng, forests, scatter_divisor):
    """森林斑块（概率密植）+ 稀疏散布"""
    for _ in range(forests):
        cx = rng.randrange(mapd.w)
        cy = rng.randrange(mapd.h)
        r = rng.randint(5, 9)
        r2 = r * r
        for y in range(cy - r, cy + r + 1):
            for x in range(cx - r, cx + r + 1):
                if (x, y) in mapd.blocked or not mapd.in_bounds(x, y):
                    continue
                if (x - cx) ** 2 + (y - cy) ** 2 <= r2 and rng.random() < 0.45:
                    mapd.add_object(x, y, 0, "furniture", "object.tree")
                    mapd.blocked.add((x, y))

    singles = max(1, (mapd.w * mapd.h) // scatter_divisor)
    placed = 0
    for _ in range(singles * 6):
        if placed >= singles:
            break
        x = rng.randrange(mapd.w)
        y = rng.randrange(mapd.h)
        if (x, y) in mapd.blocked:
            continue
        mapd.add_object(x, y, 0, "furniture", "object.tree")
        mapd.blocked.add((x, y))
        placed += 1


def gen_tufts(mapd, rng, divisor):
    count = max(0, (mapd.w * mapd.h) // divisor)
    placed = 0
    for _ in range(count * 4):
        if placed >= count:
            break
        x = rng.randrange(mapd.w)
        y = rng.randrange(mapd.h)
        if (x, y) in mapd.blocked or (x, y) in mapd.floor:
            continue
        mapd.add_object(x, y, 0, "furniture", "decor.grass_tuft")
        placed += 1


def write_map(mapd, out_dir, meta):
    chunks_dir = os.path.join(out_dir, "chunks")
    os.makedirs(chunks_dir, exist_ok=True)

    objects_by_chunk = {}
    for x, y, z, layer, sprite in mapd.objects:
        key = (x // CHUNK, y // CHUNK)
        objects_by_chunk.setdefault(key, []).append((x, y, z, layer, sprite))

    files_written = 0
    for cy in range(mapd.chunks_y):
        for cx in range(mapd.chunks_x):
            x0, y0 = cx * CHUNK, cy * CHUNK

            floors = []
            has_floor_override = False
            for ly in range(CHUNK):
                row = []
                for lx in range(CHUNK):
                    ch = mapd.floor.get((x0 + lx, y0 + ly), DEFAULT_CHAR)
                    if ch != DEFAULT_CHAR:
                        has_floor_override = True
                    row.append(ch)
                floors.append("".join(row))

            objs = sorted(
                (o for o in objects_by_chunk.get((cx, cy), [])
                 if x0 <= o[0] < x0 + CHUNK and y0 <= o[1] < y0 + CHUNK),
                key=lambda o: (o[1], o[0], o[2], o[4]),
            )
            rooms = [
                r for r in mapd.rooms
                if r[0] < x0 + CHUNK and r[0] + r[2] > x0 and r[1] < y0 + CHUNK and r[1] + r[3] > y0
            ]

            if not has_floor_override and not objs and not rooms:
                continue  # 稀疏：默认 chunk 不落盘

            # 房间矩形按本 chunk 裁剪（文件自包含；跨 chunk 的房间在每个相交文件中各有一份）
            rooms_in = []
            for rx0, ry0, rw, rh, rid, rname, rz in sorted(rooms, key=lambda r: r[4]):
                ix0 = max(rx0, x0)
                iy0 = max(ry0, y0)
                ix1 = min(rx0 + rw, x0 + CHUNK)
                iy1 = min(ry0 + rh, y0 + CHUNK)
                if ix0 < ix1 and iy0 < iy1:
                    entry = {
                        "x": ix0 - x0, "y": iy0 - y0,
                        "w": ix1 - ix0, "h": iy1 - iy0,
                        "id": rid, "name": rname,
                    }
                    if rz:
                        entry["z"] = rz
                    rooms_in.append(entry)

            payload = {"cx": cx, "cy": cy}
            if has_floor_override:
                payload["floors"] = floors
            if objs:
                payload["objects"] = [
                    _object_json(o, x0, y0)
                    for o in objs
                ]
            if rooms_in:
                payload["rooms"] = rooms_in

            cell_dir = os.path.join(chunks_dir, "c%d_%d" % (cx // CHUNKS_PER_CELL, cy // CHUNKS_PER_CELL))
            os.makedirs(cell_dir, exist_ok=True)
            path = os.path.join(cell_dir, "chunk_%d_%d.json" % (cx, cy))
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                json.dump(payload, f, ensure_ascii=False, indent=1)
                f.write("\n")
            files_written += 1

    with open(os.path.join(out_dir, "map.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(meta, f, ensure_ascii=False, indent=1)
        f.write("\n")
    return files_written


def main():
    parser = argparse.ArgumentParser(description="ZomboidLike 地图生成器 v1")
    parser.add_argument("--name", required=True, help="地图名（输出目录名）")
    parser.add_argument("--seed", type=int, default=20261016)
    parser.add_argument("--chunks", default="12x12", help="尺寸，如 100x100（chunk 单位）")
    parser.add_argument("--villages", type=int, default=0, help="村庄数（0=按面积自动）")
    parser.add_argument("--roads", type=int, default=2, help="横竖路对数（每对各一条）")
    parser.add_argument("--patches", type=int, default=0, help="泥土补丁数（0=按面积自动）")
    parser.add_argument("--forests", type=int, default=0, help="森林斑块数（0=按面积自动）")
    parser.add_argument("--objective-poi", action="store_true",
                        help="添加 T6 任务目标物（无线电/撤离点，固定坐标）")
    parser.add_argument("--out", default="", help="输出目录（缺省 assets/maps/<name>）")
    args = parser.parse_args()

    chunks_x, chunks_y = (int(v) for v in args.chunks.lower().split("x"))
    if chunks_x <= 0 or chunks_y <= 0:
        raise SystemExit("--chunks 尺寸非法: %s" % args.chunks)

    out_dir = args.out or os.path.join(ROOT, "assets", "maps", args.name)
    area_chunks = chunks_x * chunks_y
    villages = args.villages or max(1, area_chunks // 60)
    patches = args.patches or max(2, area_chunks // 40)
    forests = args.forests or max(2, area_chunks // 60)

    rng = random.Random(args.seed)
    mapd = MapData(chunks_x, chunks_y)

    gen_dirt_patches(mapd, rng, patches)
    horiz, vert = gen_roads(mapd, rng, args.roads)

    house_rects = []
    total_houses = 0
    anchors = [(rng.randrange(6, max(7, mapd.w - 12)), hy + rng.choice((-4, 5)))
               for hy in horiz] + \
              [(vx + rng.choice((-4, 5)), rng.randrange(6, max(7, mapd.h - 12)))
               for vx in vert]
    if not anchors:
        anchors = [(mapd.w // 2, mapd.h // 2)]
    for i in range(villages):
        ax, ay = anchors[i % len(anchors)]
        ax += rng.randint(-8, 8) * (i // len(anchors))
        total_houses += gen_village(mapd, rng, ax, ay, house_rects)

    gen_trees(mapd, rng, forests, scatter_divisor=1500)
    if area_chunks <= 400:  # 小地图铺草丛；大地图交给森林/散布即可
        gen_tufts(mapd, rng, divisor=90)
    if args.objective_poi:
        gen_objective_poi(mapd)
    gen_pois(mapd)
    gen_pond(mapd)

    meta = {
        "format": "zomboidlike-map-v1",
        "name": args.name,
        "generator": {
            "tool": "tools/gen_map.py",
            "version": 5,
            "seed": args.seed,
            "params": {
                "chunks": "%dx%d" % (chunks_x, chunks_y),
                "villages": villages,
                "roads": args.roads,
                "patches": patches,
                "forests": forests,
                "objectivePoi": bool(args.objective_poi),
                "pois": len(POI_SPECS),
            },
        },
        "bounds": {"chunkX0": 0, "chunkY0": 0, "chunksX": chunks_x, "chunksY": chunks_y},
        "terrain": {"defaultFloor": DEFAULT_FLOOR},
        "legend": dict(LEGEND),
    }

    written = write_map(mapd, out_dir, meta)
    total_chunks = chunks_x * chunks_y
    print("map    : %s" % out_dir)
    print("size   : %dx%d chunks (%dx%d squares)" % (chunks_x, chunks_y, mapd.w, mapd.h))
    print("content: %d houses | %d POIs | %d rooms | %d objects | %d floor overrides"
          % (total_houses, len(POI_SPECS), len(mapd.rooms), len(mapd.objects),
             len(mapd.floor)))
    print("chunks : %d/%d written (sparse %d skipped)" % (written, total_chunks, total_chunks - written))


if __name__ == "__main__":
    main()
