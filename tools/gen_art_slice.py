# -*- coding: utf-8 -*-
"""生成美术垂直切片图集（assets/own/atlas_own.png）。

切片目标：统一地面/墙/角色/僵尸/场景物件/代表性物品图标的比例、色板与锚点，
验证正式美术可以零代码替换现有图集。未列入切片的帧继续使用
tools/gen_placeholder_atlas.py 的占位绘制，避免 manifest 出现缺帧。

用法:
  py -3 tools/gen_art_slice.py

输出格式与 pz-atlas-v1 一致；运行时仍只通过 asset_manifest.json 的逻辑名取资源。
"""

import json
import math
import os
import random

from PIL import Image, ImageDraw, ImageOps

import gen_placeholder_atlas as base

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "assets", "own")

FLOOR_CANVAS = (64, 128)
WALL_CANVAS = (64, 224)
TREE_CANVAS = (192, 256)
VEHICLE_CANVAS = (160, 128)
OUTLINE = (27, 26, 24, 255)

# 色板集中在这里，正式手绘替换时按 docs/art-pipeline.md 的同一角色取色。
GRASS = ((80, 107, 60), (113, 140, 76), (52, 71, 42))
GRASS_BASES = ((80, 107, 60), (82, 109, 62), (79, 106, 59), (83, 110, 61))
DIRT = ((115, 91, 61), (146, 118, 79), (74, 57, 40))
DIRT_BASES = ((115, 91, 61), (114, 90, 60))
WOOD = ((139, 93, 56), (180, 123, 74), (78, 52, 36))
WOOD_BASES = ((139, 93, 56), (140, 94, 57))
BRICK = ((122, 74, 57), (158, 100, 80), (74, 43, 34))
MORTAR = (91, 76, 64, 255)
SKIN = ((210, 161, 123), (231, 190, 152), (169, 113, 82))
ZSKIN = ((124, 138, 88), (154, 165, 108), (80, 94, 60))
SHIRT = ((63, 96, 120), (92, 130, 155), (42, 67, 84))
ZSHIRT = ((98, 92, 80), (124, 116, 100), (64, 60, 53))
PANTS = ((62, 65, 76), (82, 86, 100), (42, 44, 54))
ZBLOOD = ((110, 31, 28), (142, 48, 40), (74, 18, 16))
HAIR = ((55, 43, 34), (82, 64, 48), (35, 28, 23))


def new_canvas(size):
    return Image.new("RGBA", size, (0, 0, 0, 0))


def color(triple, alpha=255):
    return (triple[0], triple[1], triple[2], alpha)


def shade(triple, delta):
    return tuple(max(0, min(255, c + delta)) for c in triple)


def _diamond_mask():
    # 精确菱形遮罩是地面无 1px 缝的关键，复用占位生成器的同一条规则。
    return base.diamond_mask(64, 32)


_DIAMOND = None


def _masked_floor(tile):
    global _DIAMOND
    if _DIAMOND is None:
        _DIAMOND = _diamond_mask()
    out = Image.new("RGBA", (64, 32), (0, 0, 0, 0))
    out.paste(tile, (0, 0), _DIAMOND)
    return out


def make_grass_floor(seed, variant):
    rng = random.Random(seed)
    base_col = GRASS_BASES[variant % len(GRASS_BASES)]
    tile = Image.new("RGBA", (64, 32), color(base_col))
    d = ImageDraw.Draw(tile)
    # 三个横向色带，模拟受压/受光的草皮而不是纯色填充。
    d.rectangle([0, 0, 63, 8], fill=color(shade(base_col, 12)))
    d.rectangle([0, 9, 63, 23], fill=color(base_col))
    d.rectangle([0, 24, 63, 31], fill=color(shade(base_col, -12)))
    for _ in range(180):
        x, y = rng.randrange(64), rng.randrange(32)
        j = rng.randint(-16, 18)
        d.point((x, y), fill=color(shade(base_col, j)))
    # 草叶：深色根部 + 亮色叶尖，2px 宽以内，1x 缩放下仍可读。
    for _ in range(rng.randint(9, 15)):
        x, y = rng.randrange(5, 59), rng.randrange(7, 29)
        h = rng.randint(3, 7)
        lean = rng.choice((-1, 0, 1))
        d.line([(x, y), (x + lean, y - h)], fill=color(GRASS[2]), width=1)
        d.line([(x + lean, y - h + 1), (x + lean * 2, y - h - 1)],
               fill=color(GRASS[1]), width=1)
    if variant == 1:
        for _ in range(3):
            x, y = rng.randrange(12, 52), rng.randrange(10, 25)
            d.point((x, y), fill=(225, 218, 164, 255))
            d.point((x + 1, y), fill=(176, 190, 116, 255))
    return _masked_floor(tile)


def make_dirt_floor(seed, variant):
    rng = random.Random(seed)
    base_col = DIRT_BASES[variant % len(DIRT_BASES)]
    tile = Image.new("RGBA", (64, 32), color(base_col))
    d = ImageDraw.Draw(tile)
    d.rectangle([0, 0, 63, 9], fill=color(shade(base_col, 13)))
    d.rectangle([0, 24, 63, 31], fill=color(shade(base_col, -14)))
    for _ in range(240):
        x, y = rng.randrange(64), rng.randrange(32)
        d.point((x, y), fill=color(shade(base_col, rng.randint(-18, 16))))
    for _ in range(18):
        x, y = rng.randrange(4, 60), rng.randrange(6, 29)
        w, h = rng.randint(1, 3), rng.randint(1, 2)
        d.ellipse([x - w, y - h, x + w, y + h],
                  fill=color(shade(base_col, rng.choice((-30, -18, 24)))))
    # 干裂纹：短、分叉、低对比。
    for _ in range(3):
        x, y = rng.randrange(12, 52), rng.randrange(10, 24)
        d.line([(x, y), (x + rng.randint(-6, 6), y + rng.randint(2, 6))],
               fill=color(shade(base_col, -26)), width=1)
        d.line([(x, y), (x + rng.randint(-4, 4), y - rng.randint(1, 4))],
               fill=color(shade(base_col, 16)), width=1)
    return _masked_floor(tile)


def make_wood_floor(seed, variant):
    rng = random.Random(seed)
    base_col = WOOD_BASES[variant % len(WOOD_BASES)]
    tile = Image.new("RGBA", (64, 32), color(base_col))
    d = ImageDraw.Draw(tile)
    d.rectangle([0, 0, 63, 7], fill=color(shade(base_col, 14)))
    d.rectangle([0, 24, 63, 31], fill=color(shade(base_col, -17)))
    # 板缝沿等距木纹方向（屏幕斜率约 0.5），比竖线更接近木地板。
    for x0 in range(-48, 80, 13):
        y0 = 1
        y1 = 31
        d.line([(x0, y0), (x0 + 16, y1)], fill=color(shade(base_col, -42)), width=1)
        d.line([(x0 + 1, y0), (x0 + 17, y1)], fill=color(shade(base_col, 18)), width=1)
    for _ in range(140):
        x, y = rng.randrange(64), rng.randrange(32)
        d.point((x, y), fill=color(shade(base_col, rng.randint(-13, 14))))
    for x, y in ((16, 12), (44, 22), (54, 11)):
        d.ellipse([x - 2, y - 1, x + 2, y + 1], outline=color(shade(base_col, -34)))
        d.point((x, y), fill=color(shade(base_col, 16)))
    return _masked_floor(tile)


def _wall_shell(seed, variant):
    rng = random.Random(seed)
    canvas = new_canvas(WALL_CANVAS)
    d = ImageDraw.Draw(canvas)
    x0, y0, x1, y1 = 0, 112, 63, 223
    d.rectangle([x0, y0, x1, y1], fill=color(BRICK[2]))
    # 每块砖单独画，带 1px 灰缝和轻微色差；顶/底各留一条亮/暗边。
    for row, y in enumerate(range(y0 + 4, y1, 12)):
        offset = 0 if row % 2 == 0 else 10
        for x in range(-offset, 64, 20):
            bx0 = max(x0 + 1, x + 1)
            bx1 = min(x1 - 1, x + 19)
            if bx1 <= bx0:
                continue
            b = tuple(max(0, min(255, c + rng.randint(-12, 12))) for c in BRICK[0])
            d.rectangle([bx0, y, bx1, min(y + 10, y1 - 1)], fill=color(b),
                        outline=color(MORTAR))
    d.line([(x0, y0), (x1, y0)], fill=color(BRICK[1]), width=2)
    d.line([(x0, y0 + 2), (x1, y0 + 2)], fill=color(shade(BRICK[0], -22)), width=1)
    d.line([(x0, y1), (x1, y1)], fill=color(shade(BRICK[2], -18)), width=2)
    d.line([(x0, y0), (x0, y1)], fill=color(shade(BRICK[2], -12)), width=1)
    d.line([(x1, y0), (x1, y1)], fill=color(shade(BRICK[2], -12)), width=1)
    if variant == 1:
        for x, y in ((15, 142), (37, 170), (49, 198)):
            d.line([(x, y), (x + 4, y + 7)], fill=(58, 35, 29, 255), width=1)
            d.line([(x + 4, y + 7), (x + 2, y + 12)], fill=(58, 35, 29, 255), width=1)
    elif variant == 2:
        for x, y, r in ((7, 211, 8), (52, 219, 10), (30, 222, 7)):
            d.ellipse([x - r, y - 4, x + r, y + 2], fill=(54, 83, 47, 255))
            d.ellipse([x - r // 2, y - 5, x + r // 2, y], fill=(71, 104, 55, 255))
    return canvas


def make_water_floor(seed):
    rng = random.Random(seed)
    tile = Image.new("RGBA", (64, 32), (37, 80, 116, 255))
    d = ImageDraw.Draw(tile)
    d.rectangle([0, 0, 63, 7], fill=(44, 93, 129, 255))
    d.rectangle([0, 8, 63, 22], fill=(39, 85, 119, 255))
    d.rectangle([0, 23, 63, 31], fill=(31, 70, 101, 255))
    for y in (4, 13, 20, 27):
        for x in range(-8, 72, 18):
            d.arc([x, y - 3, x + 14, y + 3], 200, 340,
                  fill=(94, 151, 177, 180), width=1)
    for _ in range(30):
        x, y = rng.randrange(64), rng.randrange(32)
        d.point((x, y), fill=(72, 124, 150, 150))
    out = Image.new("RGBA", (64, 32), (0, 0, 0, 0))
    out.paste(tile, (0, 0), base.diamond_mask(64, 32))
    canvas = new_canvas(FLOOR_CANVAS)
    canvas.alpha_composite(out, (0, 96))
    return canvas


def make_wall(seed, variant=0, door=False):
    canvas = _wall_shell(seed, variant)
    if not door:
        return canvas
    d = ImageDraw.Draw(canvas)
    x0, y0, x1, y1 = 17, 132, 47, 223
    d.rectangle([x0 - 4, y0 - 4, x1 + 4, y1], fill=(54, 35, 26, 255),
                outline=(38, 25, 19, 255), width=2)
    d.rectangle([x0, y0, x1, y1], fill=(16, 14, 12, 255))
    d.rectangle([x0 - 4, y0 - 4, x0 + 3, y1], fill=(76, 49, 33, 255),
                outline=OUTLINE)
    d.rectangle([x1 - 3, y0 - 4, x1 + 4, y1], fill=(76, 49, 33, 255),
                outline=OUTLINE)
    d.rectangle([x0 - 4, y0 - 4, x1 + 4, y0 + 3], fill=(76, 49, 33, 255),
                outline=OUTLINE)
    d.polygon([(x0 - 13, y0 + 6), (x0 - 4, y0 + 13), (x0 - 4, y1 - 2),
               (x0 - 13, y1 - 9)], fill=(111, 72, 43, 255), outline=OUTLINE)
    d.ellipse([x0 - 8, y0 + 48, x0 - 4, y0 + 52], fill=(222, 190, 104, 255),
              outline=OUTLINE)
    d.line([(x0 + 2, y1 - 2), (x1 - 2, y1 - 2)], fill=(28, 21, 17, 210), width=2)
    return canvas


def _wood_wall_shell(seed, variant=0):
    rng = random.Random(seed)
    canvas = new_canvas(WALL_CANVAS)
    d = ImageDraw.Draw(canvas)
    x0, y0, x1, y1 = 0, 112, 63, 223
    d.rectangle([x0, y0, x1, y1], fill=color(WOOD[2]))
    for index, y in enumerate(range(y0 + 3, y1 - 2, 13)):
        plank = shade(WOOD[0], (index % 3 - 1) * 6 + rng.randint(-5, 5))
        d.rectangle([x0 + 1, y, x1 - 1, min(y + 11, y1 - 2)],
                    fill=color(plank), outline=color(shade(WOOD[2], -8)))
        for _ in range(5):
            gx = rng.randrange(x0 + 3, x1 - 3)
            gy_lo = y + 2
            gy_hi = max(gy_lo + 1, min(y + 10, y1 - 2))
            gy = rng.randrange(gy_lo, gy_hi)
            d.point((gx, gy), fill=color(shade(plank, -16)))
    d.rectangle([x0, y0, x0 + 4, y1], fill=color(shade(WOOD[0], -10)),
                outline=OUTLINE)
    d.rectangle([x1 - 4, y0, x1, y1], fill=color(shade(WOOD[0], -10)),
                outline=OUTLINE)
    d.line([(x0, y0), (x1, y0)], fill=color(WOOD[1]), width=3)
    d.line([(x0, y1), (x1, y1)], fill=color(shade(WOOD[2], -18)), width=2)
    if variant == 1:
        d.line([(11, 142), (53, 202)], fill=(83, 57, 38, 255), width=2)
        d.line([(11, 202), (53, 142)], fill=(96, 67, 44, 255), width=2)
    return canvas


def make_wood_wall(seed, variant=0):
    return _wood_wall_shell(seed, variant)


def make_built_door(seed, open_door=False):
    canvas = _wood_wall_shell(seed, 0)
    d = ImageDraw.Draw(canvas)
    x0, y0, x1, y1 = 16, 132, 47, 223
    d.rectangle([x0 - 4, y0 - 4, x1 + 4, y1], fill=(54, 35, 26, 255),
                outline=(38, 25, 19, 255), width=2)
    if open_door:
        d.rectangle([x0, y0, x1, y1], fill=(16, 14, 12, 255))
        d.polygon([(x0 - 18, y0 + 8), (x0 - 4, y0 + 16), (x0 - 4, y1 - 4),
                   (x0 - 18, y1 - 12)], fill=color(WOOD[0]), outline=OUTLINE)
        d.line([(x0 - 12, y0 + 22), (x0 - 7, y0 + 27)],
               fill=color(WOOD[1]), width=2)
        d.line([(x0 - 12, y1 - 24), (x0 - 7, y1 - 29)],
               fill=color(WOOD[1]), width=2)
        d.rectangle([x0 - 4, y0 - 4, x0 + 3, y1], fill=color(shade(WOOD[0], -8)),
                    outline=OUTLINE)
        d.rectangle([x1 - 3, y0 - 4, x1 + 4, y1], fill=color(shade(WOOD[0], -8)),
                    outline=OUTLINE)
        d.rectangle([x0 - 4, y0 - 4, x1 + 4, y0 + 3],
                    fill=color(shade(WOOD[0], -8)), outline=OUTLINE)
    else:
        d.rectangle([x0, y0, x1, y1], fill=color(shade(WOOD[0], 5)),
                    outline=OUTLINE)
        d.rectangle([x0 + 3, y0 + 4, x1 - 3, y0 + 22],
                    fill=color(shade(WOOD[0], -10)), outline=OUTLINE)
        d.rectangle([x0 + 3, y0 + 28, x1 - 3, y1 - 4],
                    fill=color(shade(WOOD[0], -10)), outline=OUTLINE)
        d.line([(x0 + 3, y0 + 4), (x1 - 3, y1 - 4)],
               fill=color(WOOD[1]), width=2)
        d.line([(x0 + 3, y1 - 4), (x1 - 3, y0 + 4)],
               fill=color(WOOD[1]), width=2)
        d.ellipse([x1 - 11, y0 + 48, x1 - 6, y0 + 55],
                  fill=(222, 190, 104, 255), outline=OUTLINE)
    return canvas


def _shadow(canvas, box, alpha=70):
    d = ImageDraw.Draw(canvas)
    d.ellipse(box, fill=(0, 0, 0, alpha))


def _animal_colors(kind):
    if kind == "deer":
        return ((164, 116, 67), (202, 151, 91), (101, 68, 43))
    if kind == "rabbit":
        return ((218, 211, 191), (244, 238, 220), (151, 139, 117))
    if kind == "chicken":
        return ((196, 168, 118), (238, 220, 174), (120, 82, 52))
    if kind == "goat":
        return ((150, 143, 130), (205, 198, 184), (92, 82, 72))
    return ((111, 73, 54), (151, 100, 69), (67, 43, 32))


def _animal_legs(d, kind, frame, body, palette, outline):
    bob = 1 if frame else 0
    left, top, right, bottom = body
    if kind == "rabbit":
        positions = ((left + 5, bottom - 8), (right - 8, bottom - 7))
        width = 3
    elif kind == "chicken":
        positions = ((left + 7, bottom - 6), (right - 10, bottom - 6))
        width = 2
    elif kind == "boar":
        positions = ((left + 6, bottom - 10), (left + 13, bottom - 10),
                     (right - 17, bottom - 10), (right - 10, bottom - 10))
        width = 4
    elif kind == "goat":
        positions = ((left + 7, bottom - 11), (left + 14, bottom - 11),
                     (right - 17, bottom - 11), (right - 9, bottom - 11))
        width = 3
    else:
        positions = ((left + 6, bottom - 11), (left + 13, bottom - 11),
                     (right - 17, bottom - 11), (right - 9, bottom - 11))
        width = 3
    for i, (x, y) in enumerate(positions):
        dx = bob if (i + frame) % 2 == 0 else -bob
        d.rectangle([x + dx, y, x + dx + width, bottom + 4],
                    fill=color(palette[2]), outline=outline)


def make_animal(kind, direction, frame):
    canvas = new_canvas(FLOOR_CANVAS)
    d = ImageDraw.Draw(canvas)
    body_color, light, dark = _animal_colors(kind)
    outline = OUTLINE
    if direction in ("left", "right"):
        if kind == "rabbit":
            _shadow(canvas, [20, 104, 46, 116], 55)
            bob = 1 if frame else 0
            d.ellipse([23, 88 - bob, 42, 108 - bob], fill=color(body_color), outline=outline)
            d.ellipse([27, 78 - bob, 40, 92 - bob], fill=color(light), outline=outline)
            d.ellipse([28, 68 - bob, 32, 84 - bob], fill=color(light), outline=outline)
            d.ellipse([35, 69 - bob, 39, 85 - bob], fill=color(light), outline=outline)
            d.ellipse([18, 91 - bob, 25, 99 - bob], fill=color(light), outline=outline)
            d.ellipse([31, 82 - bob, 34, 85 - bob], fill=(34, 29, 24, 255))
            _animal_legs(d, kind, frame, (23, 88 - bob, 42, 108 - bob), (body_color, light, dark), outline)
        elif kind == "deer":
            _shadow(canvas, [12, 104, 52, 118], 60)
            bob = 1 if frame else 0
            d.ellipse([15, 78 - bob, 42, 102 - bob], fill=color(body_color), outline=outline)
            d.polygon([(36, 78 - bob), (42, 62 - bob), (47, 64 - bob), (42, 82 - bob)],
                      fill=color(body_color), outline=outline)
            d.ellipse([40, 56 - bob, 53, 70 - bob], fill=color(light), outline=outline)
            d.ellipse([42, 60 - bob, 45, 63 - bob], fill=(31, 27, 23, 255))
            d.line([(43, 56 - bob), (40, 44 - bob)], fill=outline, width=2)
            d.line([(39, 47 - bob), (34, 40 - bob)], fill=outline, width=2)
            d.line([(39, 47 - bob), (45, 38 - bob)], fill=outline, width=2)
            d.line([(50, 57 - bob), (55, 49 - bob)], fill=outline, width=2)
            d.ellipse([13, 86 - bob, 19, 94 - bob], fill=color(light), outline=outline)
            _animal_legs(d, kind, frame, (15, 78 - bob, 42, 102 - bob),
                         (body_color, light, dark), outline)
        elif kind == "chicken":
            _shadow(canvas, [18, 104, 47, 116], 55)
            bob = 1 if frame else 0
            d.ellipse([18, 84 - bob, 42, 104 - bob], fill=color(body_color), outline=outline)
            d.ellipse([36, 74 - bob, 50, 90 - bob], fill=color(light), outline=outline)
            d.polygon([(49, 80 - bob), (57, 83 - bob), (49, 87 - bob)],
                      fill=(218, 155, 55, 255), outline=outline)
            d.polygon([(36, 75 - bob), (38, 68 - bob), (41, 75 - bob)],
                      fill=(190, 55, 50, 255), outline=outline)
            d.polygon([(40, 75 - bob), (42, 67 - bob), (45, 75 - bob)],
                      fill=(190, 55, 50, 255), outline=outline)
            d.ellipse([43, 79 - bob, 45, 81 - bob], fill=(31, 27, 23, 255))
            d.polygon([(19, 88 - bob), (10, 80 - bob), (12, 94 - bob), (18, 100 - bob)],
                      fill=color(light), outline=outline)
            _animal_legs(d, kind, frame, (18, 84 - bob, 42, 104 - bob),
                         (body_color, light, dark), outline)
        elif kind == "goat":
            _shadow(canvas, [10, 104, 55, 118], 60)
            bob = 1 if frame else 0
            d.ellipse([12, 80 - bob, 44, 104 - bob], fill=color(body_color), outline=outline)
            d.ellipse([38, 66 - bob, 53, 84 - bob], fill=color(light), outline=outline)
            d.polygon([(49, 75 - bob), (58, 78 - bob), (51, 83 - bob)],
                      fill=color(light), outline=outline)
            d.line([(42, 68 - bob), (37, 54 - bob)], fill=outline, width=2)
            d.line([(38, 56 - bob), (33, 50 - bob)], fill=outline, width=2)
            d.line([(47, 68 - bob), (52, 54 - bob)], fill=outline, width=2)
            d.line([(52, 56 - bob), (57, 50 - bob)], fill=outline, width=2)
            d.ellipse([46, 72 - bob, 49, 75 - bob], fill=(31, 27, 23, 255))
            d.line([(50, 82 - bob), (49, 90 - bob)], fill=(232, 226, 190, 255), width=2)
            d.ellipse([11, 88 - bob, 17, 96 - bob], fill=color(light), outline=outline)
            _animal_legs(d, kind, frame, (12, 80 - bob, 44, 104 - bob),
                         (body_color, light, dark), outline)
        else:
            _shadow(canvas, [10, 104, 55, 118], 65)
            bob = 1 if frame else 0
            d.ellipse([11, 80 - bob, 44, 106 - bob], fill=color(body_color), outline=outline)
            d.ellipse([38, 78 - bob, 57, 99 - bob], fill=color(light), outline=outline)
            d.polygon([(52, 84 - bob), (61, 88 - bob), (57, 94 - bob), (50, 94 - bob)],
                      fill=color(light), outline=outline)
            d.ellipse([40, 82 - bob, 43, 85 - bob], fill=(31, 27, 23, 255))
            d.line([(53, 94 - bob), (58, 101 - bob)], fill=(232, 226, 190, 255), width=2)
            d.line([(47, 78 - bob), (52, 68 - bob)], fill=outline, width=2)
            _animal_legs(d, kind, frame, (11, 80 - bob, 44, 106 - bob),
                         (body_color, light, dark), outline)
        if direction == "right":
            canvas = ImageOps.mirror(canvas)
        return canvas
    if kind == "rabbit":
        _shadow(canvas, [22, 105, 42, 116], 55)
        d.ellipse([25, 90, 39, 108], fill=color(body_color), outline=outline)
        d.ellipse([28, 78, 37, 91], fill=color(light), outline=outline)
        d.ellipse([28, 68, 31, 82], fill=color(light), outline=outline)
        d.ellipse([34, 68, 37, 82], fill=color(light), outline=outline)
        d.ellipse([20, 91, 25, 98], fill=color(light), outline=outline)
        if direction == "down":
            d.ellipse([30, 84, 33, 87], fill=(34, 29, 24, 255))
        else:
            d.ellipse([30, 81, 33, 84], fill=(34, 29, 24, 255))
    elif kind == "deer":
        _shadow(canvas, [16, 105, 50, 118], 60)
        d.ellipse([19, 82, 45, 106], fill=color(body_color), outline=outline)
        d.ellipse([25, 72, 39, 87], fill=color(light), outline=outline)
        d.ellipse([25, 69, 29, 80], fill=color(light), outline=outline)
        d.ellipse([35, 69, 39, 80], fill=color(light), outline=outline)
        d.line([(27, 71), (21, 58)], fill=outline, width=2)
        d.line([(23, 63), (17, 57)], fill=outline, width=2)
        d.line([(37, 71), (43, 58)], fill=outline, width=2)
        d.ellipse([29, 78, 32, 81], fill=(31, 27, 23, 255)) if direction == "down" else d.ellipse([29, 76, 32, 79], fill=(31, 27, 23, 255))
        d.ellipse([16, 91, 22, 99], fill=color(light), outline=outline)
        _animal_legs(d, kind, 0, (19, 82, 45, 106), (body_color, light, dark), outline)
    elif kind == "chicken":
        _shadow(canvas, [20, 105, 44, 116], 55)
        d.ellipse([20, 88, 43, 108], fill=color(body_color), outline=outline)
        d.ellipse([27, 74, 38, 92], fill=color(light), outline=outline)
        d.polygon([(28, 76), (30, 67), (32, 76)], fill=(190, 55, 50, 255),
                  outline=outline)
        d.polygon([(33, 76), (35, 67), (37, 76)], fill=(190, 55, 50, 255),
                  outline=outline)
        if direction == "down":
            d.polygon([(30, 92), (35, 98), (29, 98)], fill=(218, 155, 55, 255),
                      outline=outline)
            d.ellipse([29, 86, 32, 89], fill=(31, 27, 23, 255))
            d.ellipse([35, 86, 38, 89], fill=(31, 27, 23, 255))
        else:
            d.polygon([(30, 74), (35, 68), (29, 68)], fill=(218, 155, 55, 255),
                      outline=outline)
            d.ellipse([29, 82, 32, 85], fill=(31, 27, 23, 255))
            d.ellipse([35, 82, 38, 85], fill=(31, 27, 23, 255))
        _animal_legs(d, kind, 0, (20, 88, 43, 108), (body_color, light, dark), outline)
    elif kind == "goat":
        _shadow(canvas, [12, 105, 54, 118], 60)
        d.ellipse([14, 84, 48, 108], fill=color(body_color), outline=outline)
        d.ellipse([25, 70, 42, 90], fill=color(light), outline=outline)
        d.ellipse([24, 74, 28, 84], fill=color(light), outline=outline)
        d.ellipse([38, 74, 42, 84], fill=color(light), outline=outline)
        d.line([(27, 72), (22, 58)], fill=outline, width=2)
        d.line([(23, 60), (18, 54)], fill=outline, width=2)
        d.line([(39, 72), (44, 58)], fill=outline, width=2)
        d.line([(43, 60), (48, 54)], fill=outline, width=2)
        if direction == "down":
            d.ellipse([29, 80, 32, 83], fill=(31, 27, 23, 255))
            d.ellipse([35, 80, 38, 83], fill=(31, 27, 23, 255))
            d.line([(32, 89), (32, 96)], fill=(232, 226, 190, 255), width=2)
        else:
            d.ellipse([29, 78, 32, 81], fill=(31, 27, 23, 255))
            d.ellipse([35, 78, 38, 81], fill=(31, 27, 23, 255))
            d.line([(32, 72), (32, 66)], fill=(232, 226, 190, 255), width=2)
        _animal_legs(d, kind, 0, (14, 84, 48, 108), (body_color, light, dark), outline)
    else:
        _shadow(canvas, [10, 105, 57, 118], 65)
        d.ellipse([13, 84, 50, 108], fill=color(body_color), outline=outline)
        d.ellipse([24, 72, 42, 90], fill=color(light), outline=outline)
        d.ellipse([26, 68, 29, 77], fill=color(light), outline=outline)
        d.ellipse([37, 68, 40, 77], fill=color(light), outline=outline)
        d.ellipse([28, 77, 31, 80], fill=(31, 27, 23, 255)) if direction == "down" else d.ellipse([28, 75, 31, 78], fill=(31, 27, 23, 255))
        d.line([(24, 88), (18, 93)], fill=(232, 226, 190, 255), width=2)
        d.line([(42, 88), (48, 93)], fill=(232, 226, 190, 255), width=2)
        _animal_legs(d, kind, 0, (13, 84, 50, 108), (body_color, light, dark), outline)
    return canvas


def _humanoid(direction, walk_frame, zombie=False):
    canvas = new_canvas(FLOOR_CANVAS)
    d = ImageDraw.Draw(canvas)
    feet = 112
    bob = 1 if walk_frame else 0
    skin = ZSKIN[0] if zombie else SKIN[0]
    shirt = ZSHIRT[0] if zombie else SHIRT[0]
    pants = (58, 59, 57) if zombie else PANTS[0]
    hair = (47, 44, 34) if zombie else HAIR[0]
    outline = (22, 21, 19, 255)
    swing = 1 if walk_frame else 0
    lean = 1 if zombie else 0

    _shadow(canvas, [16, 106, 48, 118], 62)
    # 腿：两帧分别表现并步/跨步；僵尸腿更窄、更弯。
    if direction in ("down", "up"):
        if swing:
            legs = [((22, 89, 28, 110), True), ((36, 87, 42, 109), False)]
        else:
            legs = [((24, 89, 30, 111), False), ((34, 89, 40, 111), False)]
        for box, lifted in legs:
            d.rectangle(box, fill=color(pants), outline=outline)
            bx = box[0] + 1
            d.rectangle([bx, box[3] - 5, box[2] + 1, box[3]], fill=(52, 39, 30, 255),
                        outline=outline)
            if lifted:
                d.rectangle([box[0] - 1, box[3] - 1, box[2] + 2, box[3] + 2],
                            fill=(72, 51, 37, 255), outline=outline)
    else:
        if swing:
            legs = [((22, 89, 29, 110), True), ((36, 88, 42, 111), False)]
        else:
            legs = [((24, 89, 30, 111), False), ((34, 89, 40, 111), False)]
        for box, lifted in legs:
            d.rectangle(box, fill=color(pants), outline=outline)
            d.rectangle([box[0], box[3] - 4, box[2], box[3]], fill=(52, 39, 30, 255),
                        outline=outline)

    torso_top = 61 + bob - lean
    torso_bottom = 90
    d.rectangle([20, torso_top, 44, torso_bottom], fill=color(shirt), outline=outline)
    d.line([(24, torso_top + 3), (24, torso_bottom - 3)], fill=color(shade(shirt, 22)),
           width=1)
    d.line([(39, torso_top + 4), (39, torso_bottom - 4)], fill=color(shade(shirt, -16)),
           width=1)
    d.rectangle([20, torso_bottom - 4, 44, torso_bottom], fill=(74, 58, 43, 255),
                outline=outline)
    d.rectangle([31, torso_bottom - 4, 34, torso_bottom], fill=(196, 161, 78, 255))

    if direction == "down":
        # 正脸：头发/眉眼/鼻口，肩部略宽，双臂自然下垂。
        d.rectangle([14, torso_top + 2 - swing, 21, torso_bottom - 4],
                    fill=color(shade(shirt, -8)), outline=outline)
        d.rectangle([43, torso_top + 1, 50, torso_bottom - 4 - swing],
                    fill=color(shade(shirt, 8)), outline=outline)
        d.rectangle([15, torso_bottom - 8, 20, torso_bottom - 4], fill=color(skin),
                    outline=outline)
        d.rectangle([44, torso_bottom - 10, 49, torso_bottom - 5], fill=color(skin),
                    outline=outline)
        d.rectangle([23, 42, 41, 61], fill=color(skin), outline=outline)
        d.rectangle([23, 42, 41, 48], fill=color(hair), outline=outline)
        d.point((27, 53), fill=outline)
        d.point((36, 53), fill=outline)
        d.line([(31, 56), (33, 56)], fill=(124, 79, 62, 255), width=1)
        if zombie:
            d.line([(27, 58), (36, 57)], fill=(88, 31, 28, 255), width=1)
            d.line([(24, 61), (28, 65)], fill=(110, 31, 28, 255), width=1)
            d.rectangle([42, torso_top + 1, 49, torso_bottom - 2], fill=color(skin),
                        outline=outline)
            d.rectangle([12, torso_top + 8, 19, torso_bottom - 4], fill=color(skin), outline=outline)
        else:
            d.rectangle([13, torso_top + 1, 20, torso_bottom - 4], fill=color(shade(shirt, -6)),
                        outline=outline)
            d.rectangle([44, torso_top + 1, 51, torso_bottom - 4], fill=color(shade(shirt, 6)),
                        outline=outline)
    elif direction == "up":
        # 背面：后脑勺、衣摆；背包只给玩家，避免僵尸从背面误读成玩家。
        d.rectangle([23, 42, 41, 61], fill=color(hair), outline=outline)
        d.line([(26, 45), (38, 45)], fill=color(shade(hair, 18)), width=1)
        if zombie:
            d.rectangle([24, 58, 32, 68], fill=color(shade(shirt, -12)), outline=outline)
            d.rectangle([34, 70, 41, 80], fill=color(shade(shirt, -16)), outline=outline)
            d.line([(27, 63), (31, 66)], fill=(110, 31, 28, 255), width=1)
            d.line([(35, 74), (40, 78)], fill=(110, 31, 28, 255), width=1)
        else:
            d.rectangle([22, 57, 42, 88], fill=(91, 68, 45, 255), outline=outline)
            d.rectangle([23, 58, 41, 85], fill=(112, 84, 52, 255), outline=outline)
            d.rectangle([25, 61, 39, 77], fill=(96, 71, 45, 255), outline=outline)
            d.line([(27, 60), (27, 86)], fill=(67, 49, 34, 255), width=2)
            d.line([(37, 60), (37, 86)], fill=(67, 49, 34, 255), width=2)
        d.rectangle([14, torso_top + 2, 21, torso_bottom - 4],
                    fill=color(shade(shirt, -8)), outline=outline)
        d.rectangle([43, torso_top + 2, 50, torso_bottom - 4],
                    fill=color(shade(shirt, 8)), outline=outline)
        d.rectangle([15, torso_bottom - 8, 20, torso_bottom - 5], fill=color(skin),
                    outline=outline)
        d.rectangle([44, torso_bottom - 8, 49, torso_bottom - 5], fill=color(skin),
                    outline=outline)
        if zombie:
            d.rectangle([15, torso_top + 5, 20, torso_bottom - 4], fill=color(skin), outline=outline)
            d.rectangle([44, torso_top + 5, 49, torso_bottom - 4], fill=color(skin), outline=outline)
    else:
        # 侧脸：鼻尖朝向屏幕左；右向在 main 中镜像。
        d.rectangle([24, 44, 39, 61], fill=color(skin), outline=outline)
        d.rectangle([27, 42, 40, 49], fill=color(hair), outline=outline)
        d.rectangle([23, 49, 26, 53], fill=color(skin), outline=outline)
        d.point((26, 53), fill=outline)
        d.line([(24, 57), (29, 57)], fill=(124, 79, 62, 255), width=1)
        d.rectangle([24, torso_top, 43, torso_bottom], fill=color(shirt), outline=outline)
        d.rectangle([18 + swing, torso_top + 2, 25 + swing, torso_bottom - 6],
                    fill=color(shade(shirt, 8)), outline=outline)
        d.rectangle([19 + swing, torso_bottom - 9, 24 + swing, torso_bottom - 4],
                    fill=color(skin), outline=outline)
        if zombie:
            d.rectangle([12, torso_top + 6, 20, torso_bottom - 4], fill=color(skin), outline=outline)
            d.point((14, 54), fill=(198, 66, 58, 255))
        else:
            d.rectangle([43, torso_top + 2, 49, torso_bottom - 8],
                        fill=color(shade(shirt, -8)), outline=outline)
            d.rectangle([44, torso_top + 11, 48, torso_top + 16], fill=color(skin),
                        outline=outline)
        d.rectangle([24, 90, 30, 111], fill=color(pants), outline=outline)
        d.rectangle([34, 90, 40, 111], fill=color(shade(pants, -7)),
                    outline=outline)
        if swing:
            d.rectangle([28, 92, 36, 110], fill=color(pants), outline=outline)

    return canvas


def make_character(direction, walk_frame):
    return _humanoid(direction, walk_frame, zombie=False)


def make_zombie(direction, walk_frame):
    return _humanoid(direction, walk_frame, zombie=True)


def _offset_canvas(canvas, dx, dy):
    out = new_canvas(canvas.size)
    out.alpha_composite(canvas, (dx, dy))
    return out


def _attack_overlay(canvas, direction, frame, zombie=False):
    d = ImageDraw.Draw(canvas)
    skin = color(ZSKIN[0] if zombie else SKIN[0])
    sleeve = color(ZSHIRT[0] if zombie else SHIRT[0])
    reach = (0, 7, 3)[frame]
    if direction == "down":
        d.line([(45, 68), (51 + reach, 74)], fill=sleeve, width=6)
        d.rectangle([49 + reach, 70, 55 + reach, 77], fill=skin, outline=OUTLINE)
    elif direction == "up":
        d.line([(19, 68), (12 - reach, 64)], fill=sleeve, width=6)
        d.rectangle([7 - reach, 60, 13 - reach, 68], fill=skin, outline=OUTLINE)
    else:
        d.line([(18, 68), (10 - reach, 65)], fill=sleeve, width=6)
        d.rectangle([5 - reach, 61, 11 - reach, 69], fill=skin, outline=OUTLINE)
    if frame == 1:
        arc = (238, 214, 160, 170)
        if direction == "down":
            d.line([(52, 62), (57, 70), (55, 80)], fill=arc, width=2)
        elif direction == "up":
            d.line([(8, 56), (4, 66), (8, 76)], fill=arc, width=2)
        else:
            d.line([(4, 58), (0, 67), (4, 77)], fill=arc, width=2)
    return canvas


def _hit_overlay(canvas, direction, zombie=False):
    d = ImageDraw.Draw(canvas)
    skin = color(ZSKIN[0] if zombie else SKIN[0])
    streak = (196, 62, 52, 210)
    if direction == "down":
        d.line([(46, 58), (54, 52)], fill=streak, width=2)
        d.line([(47, 66), (56, 64)], fill=streak, width=2)
        d.rectangle([18, 64, 22, 72], fill=skin, outline=OUTLINE)
    elif direction == "up":
        d.line([(10, 58), (4, 52)], fill=streak, width=2)
        d.line([(9, 67), (2, 66)], fill=streak, width=2)
        d.rectangle([42, 66, 46, 74], fill=skin, outline=OUTLINE)
    else:
        d.line([(9, 58), (2, 53)], fill=streak, width=2)
        d.line([(8, 67), (1, 66)], fill=streak, width=2)
        d.rectangle([40, 64, 44, 73], fill=skin, outline=OUTLINE)
    return canvas


def make_character_clip(direction, clip, frame, zombie=False):
    """M14-T2：idle/attack/hit/death 新帧；walk 复用旧 8 帧。

    所有画布仍为 64x128、脚底 y=112。death 第 2 帧直接用尸体画布，
    保证倒地窗口与后续 corpse 实体视觉连续。
    """
    if clip == "idle":
        canvas = _humanoid(direction, 0, zombie=zombie)
        if frame == 1:
            canvas = _offset_canvas(canvas, 0, 1)
        return canvas
    if clip == "walk":
        return _humanoid(direction, frame % 2, zombie=zombie)
    if clip == "attack":
        return _attack_overlay(_humanoid(direction, 0, zombie=zombie), direction,
                               frame, zombie)
    if clip == "hit":
        return _hit_overlay(_humanoid(direction, 0, zombie=zombie), direction, zombie)
    if clip == "death":
        if frame == 1:
            return make_corpse(7300 + frame, flip=(direction == "right"))
        canvas = _humanoid(direction, 0, zombie=zombie)
        d = ImageDraw.Draw(canvas)
        d.line([(27, 74), (38, 80)], fill=(142, 36, 30, 230), width=2)
        d.line([(31, 82), (35, 92)], fill=(106, 24, 22, 210), width=1)
        return canvas
    return _humanoid(direction, 0, zombie=zombie)


def make_corpse(seed, flip=False):
    rng = random.Random(seed)
    canvas = new_canvas(FLOOR_CANVAS)
    d = ImageDraw.Draw(canvas)
    cx, base = 32, 108
    _shadow(canvas, [10, 101, 54, 119], 80)
    for _ in range(8):
        x = cx + rng.randint(-18, 18)
        y = base + rng.randint(-7, 8)
        r = rng.randint(6, 14)
        d.ellipse([x - r, y - r // 3, x + r, y + r // 3],
                  fill=(96, 26, 22, 145))
    d.ellipse([cx - 17, base - 7, cx + 18, base + 8], fill=(78, 19, 17, 220))
    d.line([(10, base - 2), (27, base - 6)], fill=(58, 57, 62, 255), width=6)
    d.line([(11, base + 4), (27, base + 1)], fill=(45, 45, 50, 255), width=5)
    d.ellipse([24, base - 14, 48, base + 4], fill=(91, 92, 86, 255), outline=OUTLINE)
    d.line([(28, base - 3), (18, base - 10)], fill=(82, 83, 78, 255), width=4)
    d.ellipse([45, base - 13, 61, base + 3], fill=(145, 105, 82, 255), outline=OUTLINE)
    d.chord([45, base - 13, 61, base + 3], 90, 270, fill=(49, 40, 32, 255))
    d.line([(56, base - 7), (59, base - 7)], fill=(33, 28, 24, 255))
    if flip:
        canvas = ImageOps.mirror(canvas)
    return canvas


def make_tree(seed, variant):
    rng = random.Random(seed)
    canvas = new_canvas(TREE_CANVAS)
    d = ImageDraw.Draw(canvas)
    _shadow(canvas, [34, 234, 158, 254], 78)
    # 树干/树根：底部略宽，保证在等距格中心站稳。
    trunk = [(88, 238), (104, 238), (100, 156), (92, 156)]
    d.polygon(trunk, fill=(91, 65, 42, 255), outline=OUTLINE)
    d.line([(95, 236), (97, 160)], fill=(133, 95, 59, 255), width=3)
    d.line([(104, 234), (102, 162)], fill=(67, 47, 33, 255), width=2)
    d.line([(89, 236), (80, 244)], fill=(75, 52, 36, 255), width=3)
    d.line([(103, 236), (116, 244)], fill=(75, 52, 36, 255), width=3)
    # 树冠由多个带描边的叶簇叠成；暗部先画，亮部后画。
    if variant == 0:
        blobs = [(96, 110, 67, 49), (63, 103, 43, 35), (129, 100, 41, 32),
                 (81, 77, 35, 27), (112, 75, 32, 25)]
    elif variant == 1:
        blobs = [(94, 92, 56, 46), (68, 84, 37, 30), (123, 81, 35, 26),
                 (92, 61, 31, 23)]
    else:
        blobs = [(99, 111, 50, 39), (70, 101, 31, 27), (128, 105, 30, 26),
                 (98, 83, 26, 20)]
    for i, (bx, by, rx, ry) in enumerate(blobs):
        dark = shade((43, 82, 43), -8 - i * 2)
        mid = shade((62, 105, 51), -4 + i * 3)
        d.ellipse([bx - rx - 2, by - ry - 2, bx + rx + 2, by + ry + 2],
                  fill=(31, 52, 32, 255))
        d.ellipse([bx - rx, by - ry, bx + rx, by + ry], fill=color(dark))
        d.ellipse([bx - rx + 3, by - ry + 3, bx + rx - 3, by + ry - 2],
                  fill=color(mid))
        d.ellipse([bx - rx + 7, by - ry + 7, bx + rx - 7, by + ry - 8],
                  fill=color((78, 122, 60)))
    for _ in range(38):
        x, y = rng.randrange(34, 158), rng.randrange(40, 145)
        # 只在外缘附近点叶子，避免整块树冠变噪。
        if (x - 96) ** 2 / 70.0 ** 2 + (y - 96) ** 2 / 58.0 ** 2 > 0.62:
            d.point((x, y), fill=color((89, 137, 66)))
    return canvas


def make_tuft(seed, variant):
    rng = random.Random(seed)
    canvas = new_canvas(FLOOR_CANVAS)
    d = ImageDraw.Draw(canvas)
    base_x, base_y = 32, 110
    _shadow(canvas, [20, 104, 44, 115], 38)
    for i in range(7 + variant):
        x = base_x + rng.randint(-12, 12)
        top = base_y - rng.randint(5, 15)
        lean = rng.choice((-2, -1, 0, 1, 2))
        d.line([(x, base_y), (x + lean, top)], fill=OUTLINE, width=2)
        d.line([(x + 1, base_y - 1), (x + lean + 1, top + 1)],
               fill=color(GRASS[1] if i % 2 else GRASS[0]), width=1)
    return canvas


def make_workbench():
    canvas = new_canvas(FLOOR_CANVAS)
    d = ImageDraw.Draw(canvas)
    _shadow(canvas, [7, 102, 57, 117], 74)
    dark, wood, light = OUTLINE, (137, 97, 59, 255), (187, 140, 86, 255)
    d.rectangle([12, 88, 18, 112], fill=(100, 69, 43, 255), outline=dark)
    d.rectangle([45, 88, 51, 112], fill=(100, 69, 43, 255), outline=dark)
    d.rectangle([18, 97, 45, 102], fill=(116, 81, 49, 255), outline=dark)
    d.rectangle([7, 78, 57, 89], fill=wood, outline=dark)
    d.rectangle([7, 87, 57, 94], fill=(109, 75, 46, 255), outline=dark)
    d.line([(10, 81), (54, 81)], fill=light, width=2)
    for y in (84, 86):
        d.line([(12, y), (52, y)], fill=(119, 84, 51, 255), width=1)
    # 台钳、锤子、木料：让工作台在 1x 下不是一块棕色矩形。
    d.rectangle([43, 69, 53, 83], fill=(112, 120, 124, 255), outline=dark)
    d.rectangle([47, 64, 50, 73], fill=(74, 82, 88, 255), outline=dark)
    d.line([(15, 77), (31, 77)], fill=(83, 57, 36, 255), width=3)
    d.rectangle([29, 73, 37, 81], fill=(157, 161, 168, 255), outline=dark)
    return canvas


def make_campfire():
    canvas = new_canvas(FLOOR_CANVAS)
    d = ImageDraw.Draw(canvas)
    _shadow(canvas, [9, 101, 55, 117], 78)
    for i in range(8):
        a = i * math.pi / 4.0
        x = 32 + int(17 * math.cos(a))
        y = 108 + int(6 * math.sin(a))
        d.ellipse([x - 5, y - 3, x + 5, y + 4], fill=(126, 122, 113, 255),
                  outline=OUTLINE)
    d.ellipse([19, 100, 45, 110], fill=(54, 42, 34, 255), outline=OUTLINE)
    d.line([(18, 105), (45, 101)], fill=(105, 70, 43, 255), width=4)
    d.line([(20, 101), (44, 108)], fill=(132, 92, 54, 255), width=4)
    d.line([(21, 109), (42, 100)], fill=(78, 52, 34, 255), width=3)
    d.polygon([(24, 101), (28, 84), (32, 93), (36, 79), (40, 101)],
              fill=(189, 73, 32, 255), outline=OUTLINE)
    d.polygon([(28, 101), (31, 91), (34, 96), (37, 88), (38, 101)],
              fill=(239, 151, 42, 255))
    d.polygon([(31, 101), (33, 96), (36, 101)], fill=(252, 219, 105, 255))
    for x, y in ((25, 96), (38, 87), (33, 72)):
        d.point((x, y), fill=(210, 210, 194, 120))
    return canvas


def _vehicle_axes(direction):
    if direction == "east":
        return (1.0, 0.0), (0.0, -1.0)
    if direction == "west":
        return (-1.0, 0.0), (0.0, 1.0)
    if direction == "north":
        return (0.0, -1.0), (-1.0, 0.0)
    if direction == "southeast":
        diagonal = math.sqrt(0.5)
        return (diagonal, diagonal), (diagonal, -diagonal)
    if direction == "southwest":
        diagonal = math.sqrt(0.5)
        return (-diagonal, diagonal), (diagonal, diagonal)
    if direction == "northwest":
        diagonal = math.sqrt(0.5)
        return (-diagonal, -diagonal), (-diagonal, diagonal)
    if direction == "northeast":
        diagonal = math.sqrt(0.5)
        return (diagonal, -diagonal), (-diagonal, -diagonal)
    return (0.0, 1.0), (1.0, 0.0)


def _vehicle_point(f, r, z, forward, right):
    wx = f * forward[0] + r * right[0]
    wy = f * forward[1] + r * right[1]
    return (80 + int(round((wx - wy) * 32.0)),
            81 + int(round((wx + wy) * 16.0 - z * 24.0)))


def _vehicle_quad(d, points, fill):
    d.polygon(points, fill=fill, outline=OUTLINE)


def _vehicle_visible(normal):
    # 等距相机从左上看；法线朝向屏幕下方（+x/+y 分量）的面才可见。
    return normal[0] + normal[1] > 0.0001


def _vehicle_box(d, forward, right, f0, f1, r0, r1, z0, z1, side, top,
                 side_light=None, side_dark=None):
    if side_light is None:
        side_light = color(shade(side[:3], 12))
    if side_dark is None:
        side_dark = color(shade(side[:3], -14))
    base = {
        "rf": _vehicle_point(f0, r0, z0, forward, right),
        "rb": _vehicle_point(f0, r1, z0, forward, right),
        "ff": _vehicle_point(f1, r0, z0, forward, right),
        "fb": _vehicle_point(f1, r1, z0, forward, right),
    }
    upper = {
        "rf": _vehicle_point(f0, r0, z1, forward, right),
        "rb": _vehicle_point(f0, r1, z1, forward, right),
        "ff": _vehicle_point(f1, r0, z1, forward, right),
        "fb": _vehicle_point(f1, r1, z1, forward, right),
    }
    faces = (
        ("rear", (-forward[0], -forward[1]), "rf", "rb", side_dark),
        ("front", forward, "ff", "fb", side_light),
        ("left", (-right[0], -right[1]), "rf", "ff", side_dark),
        ("right", right, "rb", "fb", side),
    )
    for _name, normal, a, b, fill in faces:
        if _vehicle_visible(normal):
            _vehicle_quad(d, [base[a], base[b], upper[b], upper[a]], fill)
    _vehicle_quad(d, [upper["rf"], upper["rb"], upper["fb"], upper["ff"]], top)


def _vehicle_face(d, forward, right, f, r0, r1, z0, z1, fill,
                  normal=None, f0=None, f1=None):
    if normal is not None and not _vehicle_visible(normal):
        return
    if f is not None:
        points = [
            _vehicle_point(f, r0, z0, forward, right),
            _vehicle_point(f, r1, z0, forward, right),
            _vehicle_point(f, r1, z1, forward, right),
            _vehicle_point(f, r0, z1, forward, right),
        ]
    else:
        points = [
            _vehicle_point(f0, r0, z0, forward, right),
            _vehicle_point(f1, r0, z0, forward, right),
            _vehicle_point(f1, r0, z1, forward, right),
            _vehicle_point(f0, r0, z1, forward, right),
        ]
    _vehicle_quad(d, points, fill)


def make_vehicle(direction):
    forward, right = _vehicle_axes(direction)
    canvas = new_canvas(VEHICLE_CANVAS)
    d = ImageDraw.Draw(canvas)
    body = (90, 108, 110, 255)
    body_top = (111, 131, 130, 255)
    body_dark = (59, 74, 77, 255)
    body_light = (132, 151, 148, 255)
    tire = (38, 38, 36, 255)
    tire_top = (57, 57, 52, 255)
    rim = (125, 124, 112, 255)
    glass = (61, 91, 102, 255)
    glass_light = (105, 143, 148, 255)
    bed_floor = (54, 54, 48, 255)
    bed_rail = (78, 88, 87, 255)
    bumper = (68, 72, 72, 255)
    headlight = (238, 220, 151, 255)
    taillight = (155, 48, 40, 255)
    rust = (121, 72, 45, 255)

    # 接地阴影：比车身略宽，避免车辆看起来悬空。
    shadow = []
    for f, r in ((-1.48, -0.68), (1.48, -0.68), (1.48, 0.68), (-1.48, 0.68)):
        x, y = _vehicle_point(f, r, 0.0, forward, right)
        shadow.append((x, y + 3))
    d.polygon(shadow, fill=(0, 0, 0, 70))

    # 四个轮子先画，车身覆盖内侧；外露部分形成轮胎/轮眉的层次。
    for wheel_f in (-0.86, 0.86):
        for wheel_r in (-0.55, 0.55):
            _vehicle_box(d, forward, right, wheel_f - 0.23, wheel_f + 0.23,
                         wheel_r - 0.08, wheel_r + 0.08, 0.0, 0.30,
                         tire, tire_top)

    # 底盘/货斗下方车身。
    _vehicle_box(d, forward, right, -1.30, 1.28, -0.52, 0.52, 0.20, 0.54,
                 body, body_top, body_light, body_dark)

    # 前保险杠、后保险杠。
    _vehicle_box(d, forward, right, 1.27, 1.38, -0.55, 0.55, 0.25, 0.42,
                 bumper, color(shade(bumper[:3], 10)),
                 color(shade(bumper[:3], 16)), color(shade(bumper[:3], -12)))
    _vehicle_box(d, forward, right, -1.38, -1.27, -0.55, 0.55, 0.25, 0.42,
                 bumper, color(shade(bumper[:3], 6)),
                 color(shade(bumper[:3], 10)), color(shade(bumper[:3], -16)))

    # 引擎盖：比货斗略低，前缘略微收窄。
    _vehicle_box(d, forward, right, 0.58, 1.27, -0.50, 0.50, 0.54, 0.64,
                 body_dark, body_top, body_light, body_dark)
    d.line([_vehicle_point(0.72, 0.0, 0.65, forward, right),
            _vehicle_point(1.20, 0.0, 0.65, forward, right)],
           fill=color(shade(body_top[:3], 14)), width=1)

    # 开放货斗：先铺深色底板，再画三面栏板。
    _vehicle_quad(d, [
        _vehicle_point(-1.28, -0.49, 0.55, forward, right),
        _vehicle_point(-1.28, 0.49, 0.55, forward, right),
        _vehicle_point(-0.28, 0.49, 0.55, forward, right),
        _vehicle_point(-0.28, -0.49, 0.55, forward, right),
    ], bed_floor)
    _vehicle_box(d, forward, right, -1.34, -1.23, -0.58, 0.58, 0.55, 0.73,
                 bed_rail, color(shade(bed_rail[:3], 8)),
                 color(shade(bed_rail[:3], 14)), color(shade(bed_rail[:3], -12)))
    _vehicle_box(d, forward, right, -1.23, -0.28, -0.58, -0.50, 0.55, 0.69,
                 bed_rail, color(shade(bed_rail[:3], 8)),
                 color(shade(bed_rail[:3], 14)), color(shade(bed_rail[:3], -12)))
    _vehicle_box(d, forward, right, -1.23, -0.28, 0.50, 0.58, 0.55, 0.69,
                 bed_rail, color(shade(bed_rail[:3], 8)),
                 color(shade(bed_rail[:3], 14)), color(shade(bed_rail[:3], -12)))
    for f in (-1.10, -0.82, -0.54):
        d.line([_vehicle_point(f, -0.49, 0.56, forward, right),
                _vehicle_point(f, 0.49, 0.56, forward, right)],
               fill=color(shade(bed_floor[:3], 12)), width=1)

    # 驾驶室：车顶高于货斗栏板，前排有挡风玻璃。
    _vehicle_box(d, forward, right, -0.24, 0.58, -0.46, 0.46, 0.54, 1.01,
                 body, body_top, body_light, body_dark)

    # 玻璃只画朝相机的面，避免背面窗浮在车顶轮廓外。
    if _vehicle_visible(forward):
        _vehicle_face(d, forward, right, 0.585, -0.37, 0.37, 0.64, 0.94,
                      glass)
        d.line([_vehicle_point(0.59, 0.0, 0.64, forward, right),
                _vehicle_point(0.59, 0.0, 0.94, forward, right)],
               fill=color(glass_light), width=1)
    if _vehicle_visible((-forward[0], -forward[1])):
        _vehicle_face(d, forward, right, -0.245, -0.34, 0.34, 0.68, 0.94,
                      glass)
    if _vehicle_visible(right):
        for f0, f1 in ((-0.10, 0.16), (0.24, 0.48)):
            _vehicle_face(d, forward, right, None, 0.465, 0.465,
                          0.66, 0.92, glass,
                          normal=right, f0=f0, f1=f1)
    if _vehicle_visible((-right[0], -right[1])):
        for f0, f1 in ((-0.10, 0.16), (0.24, 0.48)):
            _vehicle_face(d, forward, right, None, -0.465, -0.465,
                          0.66, 0.92, glass,
                          normal=(-right[0], -right[1]), f0=f0, f1=f1)

    # 车门缝、把手与车顶高光。
    for side_r, normal in ((0.465, right), (-0.465, (-right[0], -right[1]))):
        if not _vehicle_visible(normal):
            continue
        d.line([_vehicle_point(-0.02, side_r, 0.26, forward, right),
                _vehicle_point(-0.02, side_r, 0.86, forward, right)],
               fill=color(shade(body_dark[:3], -8)), width=1)
        d.line([_vehicle_point(0.06, side_r, 0.62, forward, right),
                _vehicle_point(0.20, side_r, 0.62, forward, right)],
               fill=color(shade(body_dark[:3], 18)), width=1)
    d.line([_vehicle_point(-0.12, 0.22, 1.02, forward, right),
            _vehicle_point(0.46, 0.22, 1.02, forward, right)],
           fill=color(shade(body_top[:3], 16)), width=1)

    # 前灯/尾灯：只在对应的面可见时出现。
    if _vehicle_visible(forward):
        for r in (-0.34, 0.34):
            _vehicle_face(d, forward, right, 1.275, r - 0.10, r + 0.10,
                          0.43, 0.50, headlight)
    if _vehicle_visible((-forward[0], -forward[1])):
        for r in (-0.36, 0.36):
            _vehicle_face(d, forward, right, -1.335, r - 0.08, r + 0.08,
                          0.41, 0.48, taillight)

    # 旧车质感：引擎盖/货斗边缘的锈斑与泥点，保持低饱和。
    for f, r, dx, dy in ((0.88, -0.34, 4, 1), (1.12, 0.18, 3, 1),
                         (-0.70, 0.44, 4, 1), (0.38, -0.50, 3, 1)):
        x, y = _vehicle_point(f, r, 0.66, forward, right)
        d.line([(x, y), (x + dx, y + dy)], fill=color(rust), width=1)
    for f, r in ((0.74, 0.30), (1.10, -0.22), (-0.94, -0.52)):
        x, y = _vehicle_point(f, r, 0.67, forward, right)
        d.point((x, y), fill=color(shade(rust[:3], 20)))
    return canvas


def make_crate():
    canvas = new_canvas(FLOOR_CANVAS)
    d = ImageDraw.Draw(canvas)
    _shadow(canvas, [8, 100, 56, 118], 78)
    dark = OUTLINE
    wood = (137, 94, 54, 255)
    light = (184, 132, 76, 255)
    side = (103, 69, 42, 255)
    d.rectangle([11, 78, 53, 111], fill=wood, outline=dark)
    d.rectangle([11, 76, 53, 85], fill=light, outline=dark)
    d.rectangle([15, 88, 49, 108], fill=side, outline=dark)
    for y in (93, 99, 105):
        d.line([(16, y), (48, y)], fill=(120, 81, 48, 255), width=1)
    d.line([(16, 89), (48, 107)], fill=(168, 118, 69, 255), width=2)
    d.line([(48, 89), (16, 107)], fill=(168, 118, 69, 255), width=2)
    d.rectangle([27, 91, 37, 97], fill=(92, 96, 98, 255), outline=dark)
    d.point((30, 94), fill=(190, 190, 180, 255))
    return canvas


def make_barricade():
    canvas = new_canvas(FLOOR_CANVAS)
    d = ImageDraw.Draw(canvas)
    _shadow(canvas, [7, 102, 57, 118], 76)
    dark = OUTLINE
    wood = (137, 94, 54, 255)
    light = (184, 132, 76, 255)
    side = (103, 69, 42, 255)
    d.rectangle([12, 66, 20, 110], fill=side, outline=dark)
    d.rectangle([44, 66, 52, 110], fill=side, outline=dark)
    for y in (76, 88, 100):
        d.rectangle([8, y, 56, y + 8], fill=wood, outline=dark)
        d.line([(11, y + 2), (53, y + 2)], fill=light, width=1)
        d.line([(11, y + 7), (53, y + 7)], fill=side, width=1)
    d.line([(15, 105), (49, 74)], fill=side, width=4)
    d.line([(16, 105), (50, 74)], fill=light, width=1)
    for x, y in ((15, 79), (49, 79), (15, 103), (49, 103)):
        d.rectangle([x - 1, y - 1, x + 1, y + 1], fill=(194, 186, 135, 255))
    return canvas


def make_locker():
    canvas = new_canvas(FLOOR_CANVAS)
    d = ImageDraw.Draw(canvas)
    _shadow(canvas, [12, 101, 52, 118], 78)
    dark = OUTLINE
    body = (91, 103, 105, 255)
    shade_body = (69, 80, 84, 255)
    d.rectangle([17, 58, 47, 111], fill=body, outline=dark)
    d.rectangle([17, 55, 47, 63], fill=(117, 128, 128, 255), outline=dark)
    d.rectangle([20, 65, 44, 108], fill=shade_body, outline=dark)
    d.line([(32, 65), (32, 108)], fill=(39, 46, 48, 255), width=1)
    d.line([(35, 65), (35, 108)], fill=(112, 122, 121, 255), width=1)
    for y in (74, 82):
        d.line([(22, y), (30, y)], fill=(46, 54, 56, 255), width=1)
        d.line([(36, y), (42, y)], fill=(46, 54, 56, 255), width=1)
    d.rectangle([27, 85, 30, 92], fill=(191, 174, 112, 255), outline=dark)
    d.rectangle([38, 85, 41, 92], fill=(191, 174, 112, 255), outline=dark)
    d.line([(20, 106), (44, 106)], fill=(49, 58, 60, 255), width=2)
    return canvas


def make_bed():
    canvas = new_canvas(FLOOR_CANVAS)
    d = ImageDraw.Draw(canvas)
    _shadow(canvas, [8, 101, 56, 118], 78)
    dark = OUTLINE
    frame = (112, 75, 46, 255)
    frame_light = (157, 108, 66, 255)
    sheet = (216, 210, 190, 255)
    sheet_shadow = (177, 171, 151, 255)
    blanket = (93, 112, 128, 255)
    blanket_light = (126, 145, 159, 255)
    pillow = (238, 234, 218, 255)
    d.rectangle([10, 69, 54, 112], fill=frame, outline=dark)
    d.rectangle([12, 66, 52, 78], fill=frame_light, outline=dark)
    d.rectangle([14, 79, 50, 109], fill=sheet, outline=dark)
    d.rectangle([14, 101, 50, 109], fill=sheet_shadow, outline=dark)
    d.rectangle([17, 82, 31, 94], fill=pillow, outline=dark)
    d.line([(19, 86), (29, 86)], fill=(208, 203, 188, 255), width=1)
    d.rectangle([30, 87, 50, 109], fill=blanket, outline=dark)
    d.line([(33, 90), (47, 90)], fill=blanket_light, width=2)
    d.line([(33, 96), (47, 96)], fill=blanket_light, width=1)
    d.rectangle([10, 108, 15, 115], fill=frame, outline=dark)
    d.rectangle([49, 108, 54, 115], fill=frame, outline=dark)
    return canvas


def make_radio():
    canvas = new_canvas(FLOOR_CANVAS)
    d = ImageDraw.Draw(canvas)
    _shadow(canvas, [11, 102, 53, 118], 76)
    dark = OUTLINE
    body = (88, 76, 55, 255)
    panel = (122, 106, 73, 255)
    d.line([(31, 84), (24, 57)], fill=(88, 88, 82, 255), width=3)
    d.line([(31, 84), (39, 59)], fill=(88, 88, 82, 255), width=3)
    d.rectangle([12, 79, 52, 112], fill=body, outline=dark)
    d.rectangle([16, 83, 48, 100], fill=panel, outline=dark)
    d.rectangle([20, 87, 34, 97], fill=(48, 55, 56, 255), outline=dark)
    d.line([(23, 91), (31, 91)], fill=(191, 174, 112, 255), width=1)
    d.ellipse([37, 87, 45, 95], fill=(54, 66, 70, 255), outline=dark)
    d.ellipse([40, 90, 42, 92], fill=(177, 188, 177, 255))
    d.rectangle([16, 103, 48, 108], fill=(60, 58, 51, 255), outline=dark)
    d.line([(20, 106), (44, 106)], fill=(139, 125, 89, 255), width=1)
    return canvas


def make_extraction():
    canvas = new_canvas(FLOOR_CANVAS)
    d = ImageDraw.Draw(canvas)
    _shadow(canvas, [16, 103, 48, 117], 72)
    d.ellipse([13, 100, 51, 116], fill=(33, 75, 44, 120), outline=OUTLINE)
    d.ellipse([21, 104, 43, 113], fill=(68, 142, 83, 130), outline=(105, 188, 116, 255))
    d.line([(32, 106), (32, 61)], fill=(61, 64, 59, 255), width=3)
    d.line([(33, 106), (33, 62)], fill=(128, 134, 122, 255), width=1)
    d.polygon([(34, 64), (51, 70), (34, 78)], fill=(103, 214, 117, 255),
              outline=OUTLINE)
    d.polygon([(34, 68), (46, 71), (34, 75)], fill=(164, 239, 150, 255))
    for x, y in ((26, 82), (38, 88), (25, 95)):
        d.point((x, y), fill=(178, 239, 167, 180))
    return canvas


def make_icon_canvas():
    canvas = new_canvas(FLOOR_CANVAS)
    d = ImageDraw.Draw(canvas)
    _shadow(canvas, [18, 106, 46, 116], 42)
    return canvas, d


def make_icon(kind):
    """切片内的代表性图标；其余仍回落到占位图标。"""
    canvas, d = make_icon_canvas()
    cx, cy = 32, 102
    if kind == "axe":
        d.line([(17, 118), (41, 81)], fill=(104, 72, 44, 255), width=4)
        d.line([(18, 117), (42, 80)], fill=(157, 113, 67, 255), width=2)
        d.polygon([(35, 73), (52, 78), (49, 94), (33, 87)], fill=(176, 183, 190, 255),
                  outline=OUTLINE)
        d.polygon([(38, 77), (48, 81), (46, 89), (37, 85)], fill=(210, 216, 220, 255))
    elif kind == "bandage":
        d.ellipse([17, 86, 47, 109], fill=(233, 232, 218, 255), outline=OUTLINE)
        d.ellipse([22, 90, 42, 105], fill=(245, 243, 229, 255), outline=(170, 168, 156, 255))
        d.rectangle([29, 84, 35, 111], fill=(182, 65, 58, 255), outline=OUTLINE)
        d.rectangle([18, 95, 46, 100], fill=(182, 65, 58, 255), outline=OUTLINE)
    elif kind == "canned_beans":
        d.rectangle([20, 86, 44, 110], fill=(183, 187, 192, 255), outline=OUTLINE)
        d.ellipse([20, 82, 44, 91], fill=(212, 215, 219, 255), outline=OUTLINE)
        d.rectangle([20, 92, 44, 104], fill=(166, 88, 55, 255), outline=OUTLINE)
        d.rectangle([24, 96, 40, 100], fill=(236, 226, 192, 255))
        d.point((27, 98), fill=(142, 71, 42, 255))
        d.point((35, 98), fill=(142, 71, 42, 255))
    elif kind == "water_bottle":
        d.rectangle([23, 86, 41, 112], fill=(112, 175, 205, 255), outline=OUTLINE)
        d.rectangle([27, 76, 37, 87], fill=(76, 132, 158, 255), outline=OUTLINE)
        d.rectangle([25, 72, 39, 77], fill=(216, 217, 207, 255), outline=OUTLINE)
        d.rectangle([25, 90, 28, 109], fill=(184, 226, 238, 255))
        d.line([(31, 93), (31, 108)], fill=(68, 139, 174, 255), width=1)
    elif kind == "backpack":
        d.arc([22, 72, 42, 91], 180, 360, fill=OUTLINE, width=4)
        d.rectangle([18, 84, 46, 112], fill=(78, 105, 67, 255), outline=OUTLINE)
        d.rectangle([21, 91, 43, 102], fill=(61, 84, 53, 255), outline=OUTLINE)
        d.rectangle([24, 103, 40, 111], fill=(56, 75, 48, 255), outline=OUTLINE)
        d.line([(21, 88), (43, 88)], fill=(157, 176, 120, 255), width=2)
    elif kind == "hammer":
        d.line([(20, 117), (37, 88)], fill=(105, 71, 43, 255), width=4)
        d.line([(21, 116), (38, 87)], fill=(163, 117, 68, 255), width=2)
        d.polygon([(28, 77), (51, 82), (49, 94), (27, 88)], fill=(167, 174, 181, 255),
                  outline=OUTLINE)
        d.polygon([(31, 81), (47, 85), (46, 89), (30, 85)], fill=(210, 216, 220, 255))
    elif kind == "makeshift_spear":
        d.line([(14, 119), (48, 73)], fill=(102, 71, 45, 255), width=3)
        d.line([(15, 118), (49, 72)], fill=(169, 124, 75, 255), width=1)
        d.polygon([(46, 69), (57, 59), (53, 78)], fill=(191, 197, 201, 255),
                  outline=OUTLINE)
        d.rectangle([43, 70, 49, 75], fill=(176, 74, 61, 255), outline=OUTLINE)
        d.line([(40, 81), (48, 86)], fill=(188, 172, 135, 255), width=2)
    elif kind == "padded_jacket":
        d.polygon([(18, 86), (28, 80), (36, 80), (46, 86), (44, 108), (35, 112),
                   (29, 112), (20, 108)], fill=(75, 101, 128, 255), outline=OUTLINE)
        d.line([(32, 81), (32, 111)], fill=(42, 65, 82, 255), width=2)
        d.line([(22, 92), (42, 92)], fill=(172, 144, 94, 255), width=2)
        d.line([(24, 99), (40, 99)], fill=(172, 144, 94, 255), width=2)
        d.rectangle([30, 84, 34, 108], fill=(105, 121, 134, 255))
    elif kind == "repair_kit":
        d.rectangle([15, 88, 49, 111], fill=(156, 94, 52, 255), outline=OUTLINE)
        d.rectangle([19, 83, 45, 89], fill=(181, 110, 60, 255), outline=OUTLINE)
        d.arc([24, 76, 40, 88], 180, 360, fill=OUTLINE, width=3)
        d.rectangle([29, 94, 35, 105], fill=(190, 194, 198, 255), outline=OUTLINE)
        d.line([(23, 98), (27, 102)], fill=(92, 74, 52, 255), width=2)
        d.point((44, 94), fill=(74, 54, 39, 255))
    elif kind == "radio_part":
        d.rectangle([15, 86, 49, 110], fill=(54, 91, 73, 255), outline=OUTLINE)
        d.rectangle([20, 91, 44, 105], fill=(38, 66, 54, 255), outline=OUTLINE)
        d.rectangle([25, 94, 39, 102], fill=(164, 174, 159, 255), outline=OUTLINE)
        for x in (21, 27, 33, 39):
            d.line([(x, 84), (x, 89)], fill=(194, 186, 135, 255), width=1)
            d.line([(x, 107), (x, 112)], fill=(194, 186, 135, 255), width=1)
        d.rectangle([42, 88, 46, 92], fill=(208, 111, 63, 255), outline=OUTLINE)
        d.line([(18, 98), (14, 98)], fill=(194, 186, 135, 255), width=2)
    elif kind == "wood":
        for i, y in enumerate((108, 98, 88)):
            x0 = 15 + i * 2
            x1 = 49 - i * 2
            d.rectangle([x0, y, x1, y + 8], fill=(137, 94, 54, 255),
                        outline=OUTLINE)
            d.line([(x0 + 3, y + 2), (x1 - 3, y + 2)],
                   fill=(184, 132, 76, 255), width=1)
            d.line([(x0 + 2, y + 6), (x1 - 2, y + 6)],
                   fill=(103, 69, 42, 255), width=1)
    elif kind == "projectile_bullet":
        d.line([(13, 94), (28, 94)], fill=(243, 226, 160, 160), width=2)
        d.line([(17, 100), (25, 100)], fill=(222, 192, 116, 140), width=1)
        d.rectangle([28, 92, 40, 101], fill=(172, 139, 66, 255), outline=OUTLINE)
        d.polygon([(40, 92), (48, 96), (40, 101)], fill=(218, 190, 106, 255),
                  outline=OUTLINE)
        d.line([(30, 94), (37, 94)], fill=(235, 214, 150, 255), width=1)
    elif kind == "effect_impact":
        for dx, dy, l in ((0, -14, 7), (10, -8, 7), (-10, -8, 7),
                          (13, 6, 6), (-13, 6, 6), (0, 12, 6)):
            d.line([(32, 101), (32 + dx, 101 + dy)], fill=(230, 139, 52, 255),
                   width=2)
        d.ellipse([26, 95, 38, 107], fill=(249, 203, 102, 255), outline=OUTLINE)
        d.ellipse([29, 98, 35, 104], fill=(255, 244, 180, 255))
    else:
        return base.make_item_icon(kind)
    return canvas


def _bbox(canvas):
    bbox = canvas.getbbox()
    if bbox is None:
        raise RuntimeError("empty art frame")
    return (bbox[0], bbox[1], bbox[2] - bbox[0], bbox[3] - bbox[1])


def build_specs():
    specs = []

    for i in range(4):
        canvas = make_grass_floor(1100 + i, i)
        specs.append((f"floor_grass_{i + 1}", canvas, (0, 0, 64, 32),
                      0, 96, 64, 128))
    for i in range(2):
        canvas = make_dirt_floor(2100 + i, i)
        specs.append((f"floor_dirt_{i + 1}", canvas, (0, 0, 64, 32),
                      0, 96, 64, 128))
    for i in range(2):
        canvas = make_wood_floor(3100 + i, i)
        specs.append((f"floor_wood_{i + 1}", canvas, (0, 0, 64, 32),
                      0, 96, 64, 128))
    canvas = make_water_floor(4100)
    specs.append(("floor_water_1", canvas, (0, 0, 64, 32),
                  0, 96, 64, 128))

    for i in range(3):
        canvas = make_wall(2200 + i, i)
        specs.append((f"wall_brick_{i + 1}", canvas, (0, 112, 64, 112),
                      0, 112, 64, 224))
    for i in range(2):
        canvas = make_wall(2300 + i, i, door=True)
        specs.append((f"wall_door_{i + 1}", canvas, (0, 112, 64, 112),
                      0, 112, 64, 224))

    wall_wood = make_wood_wall(2400, 0)
    door_closed = make_built_door(2500, open_door=False)
    door_open = make_built_door(2600, open_door=True)
    for name, canvas in (
        ("wall_wood_h_1", wall_wood),
        ("wall_wood_v_1", ImageOps.mirror(wall_wood)),
        ("door_wood_closed_h_1", door_closed),
        ("door_wood_closed_v_1", ImageOps.mirror(door_closed)),
        ("door_wood_open_h_1", door_open),
        ("door_wood_open_v_1", ImageOps.mirror(door_open)),
    ):
        specs.append((name, canvas, (0, 112, 64, 112), 0, 112, 64, 224))

    for dir_name in ("down", "left", "right", "up"):
        for frame in range(2):
            if dir_name == "right":
                canvas = ImageOps.mirror(make_character("left", frame))
            else:
                canvas = make_character(dir_name, frame)
            bbox = _bbox(canvas)
            specs.append((f"character_{dir_name}_{frame}", canvas, bbox,
                          bbox[0], bbox[1], 64, 128))
            if dir_name == "right":
                zcanvas = ImageOps.mirror(make_zombie("left", frame))
            else:
                zcanvas = make_zombie(dir_name, frame)
            zbbox = _bbox(zcanvas)
            specs.append((f"character_zombie_{dir_name}_{frame}", zcanvas, zbbox,
                          zbbox[0], zbbox[1], 64, 128))

    # M14-T2：v2 新帧。walk 仍引用上面的旧 8 帧，idle/attack/hit/death 使用新姿势。
    for dir_name in ("down", "left", "right", "up"):
        for clip, frame_count in (("idle", 2), ("attack", 3), ("hit", 1), ("death", 2)):
            for frame in range(frame_count):
                if dir_name == "right":
                    canvas = ImageOps.mirror(
                        make_character_clip("left", clip, frame, zombie=False))
                else:
                    canvas = make_character_clip(dir_name, clip, frame, zombie=False)
                bbox = _bbox(canvas)
                specs.append((f"character_{clip}_{dir_name}_{frame}", canvas, bbox,
                              bbox[0], bbox[1], 64, 128))
                if dir_name == "right":
                    zcanvas = ImageOps.mirror(
                        make_character_clip("left", clip, frame, zombie=True))
                else:
                    zcanvas = make_character_clip(dir_name, clip, frame, zombie=True)
                zbbox = _bbox(zcanvas)
                specs.append((f"character_zombie_{clip}_{dir_name}_{frame}", zcanvas,
                              zbbox, zbbox[0], zbbox[1], 64, 128))

    for i in range(3):
        canvas = make_tuft(4100 + i, i)
        specs.append((f"decor_tuft_{i + 1}", canvas, _bbox(canvas),
                      _bbox(canvas)[0], _bbox(canvas)[1], 64, 128))

    for i, flip in enumerate((False, True)):
        canvas = make_corpse(5100 + i, flip)
        specs.append((f"corpse_{i + 1}", canvas, _bbox(canvas),
                      _bbox(canvas)[0], _bbox(canvas)[1], 64, 128))

    for kind in ("deer", "rabbit", "boar", "chicken", "goat"):
        for direction in ("down", "left", "right", "up"):
            for frame in (0, 1):
                canvas = make_animal(kind, direction, frame)
                bbox = _bbox(canvas)
                specs.append((f"animal_{kind}_{direction}_{frame}", canvas, bbox,
                              bbox[0], bbox[1], 64, 128))

    for i in range(3):
        canvas = make_tree(6100 + i, i)
        bbox = _bbox(canvas)
        specs.append((f"tree_{i + 1}", canvas, bbox, bbox[0], bbox[1], 192, 256))

    for direction in ("north", "northeast", "east", "southeast",
                      "south", "southwest", "west", "northwest"):
        canvas = make_vehicle(direction)
        bbox = _bbox(canvas)
        specs.append((f"vehicle_{direction}", canvas, bbox, bbox[0], bbox[1],
                      160, 128))

    for name, canvas in (("workbench_1", make_workbench()),
                         ("campfire_1", make_campfire()),
                         ("crate_1", make_crate()),
                         ("locker_1", make_locker()),
                         ("bed_1", make_bed()),
                         ("barricade_1", make_barricade()),
                         ("radio_1", make_radio()),
                         ("extraction_1", make_extraction())):
        bbox = _bbox(canvas)
        specs.append((name, canvas, bbox, bbox[0], bbox[1], 64, 128))

    slice_icons = ("bandage", "canned_beans", "axe", "water_bottle", "backpack",
                   "hammer", "makeshift_spear", "padded_jacket", "repair_kit",
                   "radio_part", "wood")
    for kind in ("bandage", "canned_beans", "axe", "kitchen_knife", "rag", "jacket",
                 "tote_bag", "water_bottle", "painkillers", "hammer", "screwdriver",
                 "energy_bar", "hiking_boots", "backpack", "cooked_beans",
                 "magazine_cooking", "makeshift_spear", "padded_jacket", "repair_kit",
                 "radio_part", "wood"):
        canvas = make_icon(kind) if kind in slice_icons else base.make_item_icon(kind)
        bbox = _bbox(canvas)
        specs.append((f"item_{kind}", canvas, bbox, bbox[0], bbox[1], 64, 128))

    for name, kind in (("projectile_bullet", "projectile_bullet"),
                       ("effect_impact", "effect_impact")):
        canvas = make_icon(kind)
        bbox = _bbox(canvas)
        specs.append((name, canvas, bbox, bbox[0], bbox[1], 64, 128))

    white = new_canvas((1, 1))
    white.putpixel((0, 0), (255, 255, 255, 255))
    specs.append(("white", white, (0, 0, 1, 1), 0, 0, 1, 1))
    return specs


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    specs = build_specs()
    page, placements = base.pack_frames([(name, canvas) for name, canvas, *_ in specs])
    page_path = os.path.join(OUT_DIR, "atlas_own.png")
    page.save(page_path, optimize=True)

    place = {name: (px, py) for name, _, px, py in placements}
    subtextures = {}
    for name, canvas, (cx, cy, cw, ch), ox, oy, fx, fy in specs:
        px, py = place[name]
        subtextures[name] = {
            "x": px + cx, "y": py + cy, "w": cw, "h": ch,
            "ox": ox, "oy": oy, "fx": fx, "fy": fy,
        }

    meta = {
        "format": "pz-atlas-v1",
        "source_pack": "own",
        "page": "atlas_own0",
        "image": "atlas_own.png",
        "has_mask": False,
        "subtextures": subtextures,
    }
    json_path = os.path.join(OUT_DIR, "atlas_own.atlas.json")
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False, indent=1)

    print(f"page  : {page_path} ({page.size[0]}x{page.size[1]})")
    print(f"atlas : {json_path} ({len(subtextures)} frames)")


if __name__ == "__main__":
    main()
