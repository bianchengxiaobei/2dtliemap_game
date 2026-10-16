# -*- coding: utf-8 -*-
"""
ZomboidLike 地图生成器 v1（M3-T2；格式规范见 docs/mapformat.md）

用法:
  py -3 tools/gen_map.py --name village_demo --seed 20261016 --chunks 12x12
  py -3 tools/gen_map.py --name stress_100x100 --seed 7 --chunks 100x100

输出: assets/maps/<name>/{map.json, chunks/c<cellX>_<cellY>/chunk_<cx>_<cy>.json}

原则:
  - 确定性: 同一 seed + 同一参数 => 逐字节相同的输出（random.Random + 固定遍历顺序）
  - 稀疏: 与默认地面一致且无对象/房间的 chunk 不落盘（流式加载快速路径）
  - 内容: 泥土补丁 + 横竖道路 + 村庄（砖墙木地板房子、门、房间）+ 树/草丛
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
LEGEND = {"g": "tile.floor.grass", "d": "tile.floor.dirt", "w": "tile.floor.wood"}
CHAR_OF = {v: k for k, v in LEGEND.items()}
DEFAULT_CHAR = CHAR_OF[DEFAULT_FLOOR]


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

    def in_bounds(self, x, y):
        return 0 <= x < self.w and 0 <= y < self.h

    def set_floor(self, x, y, ch):
        if self.in_bounds(x, y) and ch != DEFAULT_CHAR:
            self.floor[(x, y)] = ch

    def add_object(self, x, y, z, layer, sprite):
        self.objects.append((x, y, z, layer, sprite))

    def add_room(self, x, y, w, h, name):
        rid = self.next_room_id
        self.next_room_id += 1
        self.rooms.append((x, y, w, h, rid, name))
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


def gen_house(mapd, rng, x0, y0, w, h, house_rects):
    """一栋房子：砖墙一圈 + 一扇南侧门 + 木地板 + 单个房间"""
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
            else:
                mapd.set_floor(x, y, "w")
            mapd.blocked.add((x, y))

    house_rects.append((x0, y0, w, h))
    mapd.add_room(x0 + 1, y0 + 1, w - 2, h - 2, "house_%d" % mapd.next_room_id)
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
        if gen_house(mapd, rng, x0, y0, w, h, house_rects):
            placed += 1
    return placed


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
            for rx0, ry0, rw, rh, rid, rname in sorted(rooms, key=lambda r: r[4]):
                ix0 = max(rx0, x0)
                iy0 = max(ry0, y0)
                ix1 = min(rx0 + rw, x0 + CHUNK)
                iy1 = min(ry0 + rh, y0 + CHUNK)
                if ix0 < ix1 and iy0 < iy1:
                    rooms_in.append({
                        "x": ix0 - x0, "y": iy0 - y0,
                        "w": ix1 - ix0, "h": iy1 - iy0,
                        "id": rid, "name": rname,
                    })

            payload = {"cx": cx, "cy": cy}
            if has_floor_override:
                payload["floors"] = floors
            if objs:
                payload["objects"] = [
                    {"x": o[0] - x0, "y": o[1] - y0, "z": o[2], "layer": o[3], "sprite": o[4]}
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

    meta = {
        "format": "zomboidlike-map-v1",
        "name": args.name,
        "generator": {
            "tool": "tools/gen_map.py",
            "version": 1,
            "seed": args.seed,
            "params": {
                "chunks": "%dx%d" % (chunks_x, chunks_y),
                "villages": villages,
                "roads": args.roads,
                "patches": patches,
                "forests": forests,
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
    print("content: %d houses | %d rooms | %d objects | %d floor overrides"
          % (total_houses, len(mapd.rooms), len(mapd.objects), len(mapd.floor)))
    print("chunks : %d/%d written (sparse %d skipped)" % (written, total_chunks, total_chunks - written))


if __name__ == "__main__":
    main()
