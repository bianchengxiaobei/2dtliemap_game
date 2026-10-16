# -*- coding: utf-8 -*-
"""
生成自制占位图集（assets/own/atlas_own.png + atlas_own.atlas.json）。

- 输出格式与 tools/pz_pack_unpack.py 完全一致（pz-atlas-v1），
  使引擎在 own / placeholder_pz 两种 provider 下走同一条代码路径。
- 画布与偏移语义对齐 PZ 约定：
    * 地面块：画布 64x128，菱形内容在画布底部 64x32（ox=0, oy=96）
    * 高物体（树）：画布 192x256，内容贴近画布底部，枢轴 ≈ (fx/2, fy-16)
- 用法: py -3 tools/gen_placeholder_atlas.py
"""

import json
import os
import random

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "assets", "own")

FLOOR_CANVAS = (64, 128)
TREE_CANVAS = (192, 256)


def new_canvas(size):
    return Image.new("RGBA", size, (0, 0, 0, 0))


def diamond_mask(w, h):
    """等距菱形（2:1）遮罩，按"像素中心在理想菱形内"逐点判定。
    这是平面菱形剖分的精确离散化：无论相邻菱形如何划分，
    每个纹理像素都有归属，tile 之间不会出现 1px 缝隙
    （PIL polygon 光栅化会排除右/下边界像素，造成缝隙）。"""
    mask = Image.new("L", (w, h), 0)
    px = mask.load()
    cx, cy = w / 2.0, h / 2.0
    for y in range(h):
        for x in range(w):
            if abs(x + 0.5 - cx) / cx + abs(y + 0.5 - cy) / cy <= 1.0 + 1e-6:
                px[x, y] = 255
    return mask


def make_floor(base_rgb, seed, speckle=18):
    """地面菱形块：画布 64x128，内容为底部 64x32 的菱形"""
    rng = random.Random(seed)
    canvas = new_canvas(FLOOR_CANVAS)
    tile = new_canvas((64, 32))
    d = ImageDraw.Draw(tile)
    mask = diamond_mask(64, 32)
    color = Image.new("RGBA", (64, 32), base_rgb + (255,))
    # 噪点
    px = color.load()
    for _ in range(260):
        x = rng.randrange(64)
        y = rng.randrange(32)
        jitter = rng.randint(-speckle, speckle)
        r, g, b, a = px[x, y]
        px[x, y] = (max(0, min(255, r + jitter)), max(0, min(255, g + jitter)),
                    max(0, min(255, b + jitter)), a)
    tile = Image.composite(color, Image.new("RGBA", (64, 32), (0, 0, 0, 0)), mask)
    canvas.alpha_composite(tile, (0, 96))
    return canvas, (0, 96)


def make_tuft(seed):
    """草丛装饰：画布 64x128，内容为格中心附近的几根草"""
    rng = random.Random(seed)
    canvas = new_canvas(FLOOR_CANVAS)
    d = ImageDraw.Draw(canvas)
    base_x, base_y = 32, 112  # 格中心带
    for i in range(7):
        x = base_x + rng.randint(-12, 12)
        h = rng.randint(6, 14)
        col = (60 + rng.randint(0, 30), 130 + rng.randint(0, 40), 50 + rng.randint(0, 30), 255)
        d.line([(x, base_y), (x + rng.randint(-3, 3), base_y - h)], fill=col, width=1)
    return canvas


def make_tree(seed):
    """树：画布 192x256，树干底部中心 + 三层椭圆树冠"""
    rng = random.Random(seed)
    canvas = new_canvas(TREE_CANVAS)
    d = ImageDraw.Draw(canvas)
    trunk_w = 12 + rng.randint(0, 6)
    trunk_top = 150
    trunk_bottom = 244
    cx = 96
    d.rectangle([cx - trunk_w // 2, trunk_top, cx + trunk_w // 2, trunk_bottom],
                fill=(96, 72, 52, 255))
    d.rectangle([cx - trunk_w // 2, trunk_top, cx - trunk_w // 2 + 3, trunk_bottom],
                fill=(120, 92, 66, 255))
    greens = [(42, 92, 44), (52, 112, 52), (62, 132, 62)]
    radii = [(70, 60), (56, 52), (42, 40)]
    centers = [(cx, 120), (cx - 26, 96), (cx + 24, 88)]
    for (gx, gy), (gw, gh), col in zip(centers, radii, greens):
        d.ellipse([gx - gw, gy - gh, gx + gw, gy + gh], fill=col + (255,))
    # 高光
    d.ellipse([cx - 40, 60, cx + 10, 105], fill=(84, 150, 80, 255))
    return canvas


def pack_frames(frames, margin=1):
    """shelf 打包：返回合并后的页图与子图矩形表"""
    # 每行限宽 1024
    max_w = 1024
    placements = []
    x = margin
    y = margin
    row_h = 0
    for name, canvas in frames:
        w, h = canvas.size
        if x + w + margin > max_w:
            x = margin
            y += row_h + margin
            row_h = 0
        placements.append((name, canvas, x, y))
        x += w + margin
        row_h = max(row_h, h)
    total_h = y + row_h + margin
    page = Image.new("RGBA", (max_w, total_h), (0, 0, 0, 0))
    for name, canvas, px, py in placements:
        page.alpha_composite(canvas, (px, py))
    return page, placements


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    rng = random.Random(20261016)

    # (逻辑帧名, 画布, 子图在画布内的 (x, y, w, h), ox, oy, fx, fy)
    specs = []

    grass_colors = [(78, 128, 66), (72, 122, 60), (84, 134, 72), (68, 118, 58)]
    for i, col in enumerate(grass_colors):
        canvas, (ox, oy) = make_floor(col, 1000 + i)
        specs.append((f"floor_grass_{i + 1}", canvas, (ox, oy, 64, 32), ox, oy, 64, 128))

    dirt_colors = [(122, 96, 64), (112, 88, 58)]
    for i, col in enumerate(dirt_colors):
        canvas, (ox, oy) = make_floor(col, 2000 + i, speckle=22)
        specs.append((f"floor_dirt_{i + 1}", canvas, (ox, oy, 64, 32), ox, oy, 64, 128))

    for i in range(3):
        canvas = make_tuft(3000 + i)
        bbox = canvas.getbbox()
        specs.append((f"decor_tuft_{i + 1}", canvas, (bbox[0], bbox[1],
                     bbox[2] - bbox[0], bbox[3] - bbox[1]), bbox[0], bbox[1], 64, 128))

    for i in range(3):
        canvas = make_tree(4000 + i)
        bbox = canvas.getbbox()
        specs.append((f"tree_{i + 1}", canvas, (bbox[0], bbox[1],
                     bbox[2] - bbox[0], bbox[3] - bbox[1]), bbox[0], bbox[1], 192, 256))

    white = new_canvas((1, 1))
    white.putpixel((0, 0), (255, 255, 255, 255))
    specs.append(("white", white, (0, 0, 1, 1), 0, 0, 1, 1))

    frames = [(name, canvas) for name, canvas, *_ in specs]
    page, placements = pack_frames(frames)
    page_path = os.path.join(OUT_DIR, "atlas_own.png")
    page.save(page_path)

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
    for name, st in subtextures.items():
        print(f"  {name}: {st}")


if __name__ == "__main__":
    main()
