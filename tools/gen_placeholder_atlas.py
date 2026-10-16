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

from PIL import Image, ImageDraw, ImageOps

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "assets", "own")

FLOOR_CANVAS = (64, 128)
TREE_CANVAS = (192, 256)
WALL_CANVAS = (64, 224)  # 内容占画布底部 64x112：从格中心 +16 立起，顶到上一层楼面


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


def make_wood_floor(seed, base=(150, 112, 68)):
    """木地板菱形块：与草地同几何，加木板缝"""
    rng = random.Random(seed)
    canvas = new_canvas(FLOOR_CANVAS)
    tile = new_canvas((64, 32))
    d = ImageDraw.Draw(tile)
    d.rectangle([0, 0, 63, 31], fill=base + (255,))
    for x in range(0, 64, 8):
        d.line([(x, 0), (x, 31)], fill=(base[0] - 32, base[1] - 26, base[2] - 16, 255), width=1)
    px = tile.load()
    for _ in range(170):
        x = rng.randrange(64)
        y = rng.randrange(32)
        jitter = rng.randint(-14, 14)
        r, g, b, a = px[x, y]
        px[x, y] = (max(0, min(255, r + jitter)), max(0, min(255, g + jitter)),
                    max(0, min(255, b + jitter)), a)
    tile = Image.composite(tile, Image.new("RGBA", (64, 32), (0, 0, 0, 0)), diamond_mask(64, 32))
    canvas.alpha_composite(tile, (0, 96))
    return canvas, (0, 96)


def make_wall(seed, door=False):
    """墙：画布 64x224，砖面块占画布底部 64x112（一格楼高）。
    door=True 时画成门板（有门框/把手）。"""
    rng = random.Random(seed)
    canvas = new_canvas(WALL_CANVAS)
    d = ImageDraw.Draw(canvas)
    top, bottom, left, right = 112, 223, 0, 63

    d.rectangle([left, top, right, bottom], fill=(122, 58, 44, 255))
    mortar = (94, 76, 66, 255)
    brick_h, brick_w = 16, 24
    for i, y in enumerate(range(top, bottom, brick_h)):
        d.line([(left, y), (right, y)], fill=mortar, width=1)
        offset = 0 if i % 2 == 0 else brick_w // 2
        for x in range(left + offset, right, brick_w):
            d.line([(x, y), (x, min(y + brick_h, bottom))], fill=mortar, width=1)
    # 顶缘亮边 / 底缘暗边（提示"块"的厚度）
    d.line([(left, top), (right, top)], fill=(168, 96, 74, 255), width=2)
    d.line([(left, bottom), (right, bottom)], fill=(66, 30, 22, 255), width=2)

    if door:
        dw = 36
        dx0 = 32 - dw // 2
        dt = bottom - 92
        d.rectangle([dx0, dt, dx0 + dw, bottom], fill=(104, 72, 46, 255))
        d.rectangle([dx0, dt, dx0 + dw, bottom], outline=(58, 38, 22, 255), width=2)
        d.line([(dx0 + dw // 2, dt + 2), (dx0 + dw // 2, bottom - 2)], fill=(58, 38, 22, 255), width=1)
        for y in range(dt + 6, bottom, 10):  # 门板横纹
            d.line([(dx0 + 2, y), (dx0 + dw - 2, y)], fill=(92, 62, 40, 255), width=1)
        d.ellipse([dx0 + dw - 11, dt + 40, dx0 + dw - 5, dt + 46], fill=(220, 200, 120, 255))
    return canvas


def make_character(direction="down", walk_frame=0):
    """小人占位（M4-T4：4 向 × 2 帧行走）：画布 64x128，脚底对齐 y=112（格中心）。
    简单色块：裤子/上衣/手臂/头 + 描边，保证和地面/树对比清晰。
    direction: down（面向镜头）/ up（背向）/ left（朝屏幕左）；right 由 left 镜像得到。
    walk_frame: 0 站立 / 1 行走（腿部摆动 + 手臂微摆）。"""
    canvas = new_canvas(FLOOR_CANVAS)
    d = ImageDraw.Draw(canvas)
    cx, feet = 32, 112
    skin = (224, 184, 148, 255)
    hair = (66, 50, 38, 255)
    shirt = (58, 96, 172, 255)
    pants = (52, 54, 74, 255)
    dark = (30, 28, 34, 255)
    swing = 1 if walk_frame else 0

    if direction in ("down", "up"):
        # 正/背面：双腿并立；行走帧左腿抬起、左臂前摆
        left_top = feet - 22 + (3 if swing else 0)
        d.rectangle([cx - 7, left_top, cx - 1, feet - 1], fill=pants, outline=dark)
        d.rectangle([cx + 1, feet - 22, cx + 7, feet - 1], fill=pants, outline=dark)
        d.rectangle([cx - 9, feet - 44, cx + 9, feet - 20], fill=shirt, outline=dark)
        arm_dy = 2 if swing else 0
        d.rectangle([cx - 13, feet - 43 + arm_dy, cx - 9, feet - 24 + arm_dy], fill=shirt,
                    outline=dark)
        d.rectangle([cx + 9, feet - 43, cx + 13, feet - 24], fill=shirt, outline=dark)
        if direction == "down":
            d.ellipse([cx - 8, feet - 60, cx + 8, feet - 44], fill=skin, outline=dark)
            d.chord([cx - 8, feet - 60, cx + 8, feet - 44], 180, 360, fill=hair)
            d.rectangle([cx - 4, feet - 53, cx - 3, feet - 52], fill=dark)
            d.rectangle([cx + 3, feet - 53, cx + 4, feet - 52], fill=dark)
        else:
            # 背向：后脑勺全头发（无脸），领口一横
            d.ellipse([cx - 8, feet - 60, cx + 8, feet - 44], fill=hair, outline=dark)
            d.line([(cx - 6, feet - 45), (cx + 6, feet - 45)], fill=dark, width=1)
    else:
        # 侧面（朝屏幕左）：躯干略窄、单臂、头前移；行走帧前后腿分开
        lean = 3 if swing else 0
        d.rectangle([cx - 6 - lean, feet - 22, cx - lean, feet - 1], fill=pants, outline=dark)
        d.rectangle([cx + lean, feet - 22, cx + 6 + lean, feet - 1], fill=pants, outline=dark)
        d.rectangle([cx - 7, feet - 44, cx + 7, feet - 20], fill=shirt, outline=dark)
        d.rectangle([cx + 2, feet - 43 + (2 * swing), cx + 6, feet - 26], fill=shirt,
                    outline=dark)
        hx = cx - 2
        d.ellipse([hx - 7, feet - 60, hx + 7, feet - 44], fill=skin, outline=dark)
        d.chord([hx - 7, feet - 60, hx + 7, feet - 44], 180, 360, fill=hair)
        d.rectangle([hx + 1, feet - 60, hx + 7, feet - 50], fill=hair)  # 后脑
        d.rectangle([hx - 5, feet - 53, hx - 4, feet - 52], fill=dark)  # 单眼（朝前）
    return canvas


def make_corpse(seed, flip=False):
    """尸体占位（M5-T1）：画布 64x128，横躺身体 + 身下血泊，基准线与角色一致（脚底 y=112）。
    flip=True 时整体镜像（另一个倒地方向）。"""
    rng = random.Random(seed)
    canvas = new_canvas(FLOOR_CANVAS)
    d = ImageDraw.Draw(canvas)
    cx, base = 32, 110

    # 血泊（暗红双层：边缘大而淡 + 核心小而深）
    blood_edge = (118, 22, 18, 170)
    blood_core = (88, 14, 12, 220)
    for _ in range(7):
        bx = cx + rng.randint(-18, 18)
        by = base + rng.randint(-9, 7)
        bw = rng.randint(7, 15)
        bh = rng.randint(3, 6)
        d.ellipse([bx - bw, by - bh, bx + bw, by + bh], fill=blood_edge)
    d.ellipse([cx - 15, base - 7, cx + 17, base + 7], fill=blood_core)

    skin = (196, 172, 150, 255)
    hair = (52, 40, 32, 255)
    shirt = (86, 92, 106, 255)   # 灰蓝（褪色衣物，与活人蓝衣区分）
    pants = (58, 56, 62, 255)
    dark = (26, 24, 28, 255)

    # 横躺：腿（左）→ 躯干（中）→ 头（右），整体压在血泊上
    d.line([(12, base - 2), (28, base - 5)], fill=pants, width=6)
    d.line([(13, base + 3), (28, base + 1)], fill=pants, width=5)
    d.ellipse([26, base - 13, 48, base + 3], fill=shirt, outline=dark)   # 躯干
    d.line([(30, base - 2), (20, base - 9)], fill=shirt, width=4)        # 压下的手臂
    d.ellipse([46, base - 12, 60, base + 2], fill=skin, outline=dark)    # 头
    d.chord([46, base - 12, 60, base + 2], 90, 270, fill=hair)           # 侧脸头发
    d.rectangle([57, base - 7, 58, base - 6], fill=dark)                 # 闭眼线

    if flip:
        canvas = ImageOps.mirror(canvas)
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

    wood_colors = [(150, 112, 68), (142, 104, 62)]
    for i, col in enumerate(wood_colors):
        canvas, (ox, oy) = make_wood_floor(2100 + i, base=col)
        specs.append((f"floor_wood_{i + 1}", canvas, (ox, oy, 64, 32), ox, oy, 64, 128))

    specs.append(("wall_brick_1", make_wall(2200), (0, 112, 64, 112), 0, 112, 64, 224))
    specs.append(("wall_door_1", make_wall(2201, door=True), (0, 112, 64, 112), 0, 112, 64, 224))

    # 小人占位（M4-T4：4 向 × 2 帧）。
    # 帧序约定（不可乱序！）：index = dir * 2 + frame，dir 枚举 down=0, left=1, right=2, up=3，
    # 与 ecs::Direction / AnimState（framesPerDir=2）一致；right 由 left 镜像。
    for dir_name in ("down", "left", "right", "up"):
        for frame in range(2):
            if dir_name == "right":
                char_canvas = ImageOps.mirror(make_character("left", frame))
            else:
                char_canvas = make_character(dir_name, frame)
            cbbox = char_canvas.getbbox()
            specs.append((f"character_{dir_name}_{frame}", char_canvas,
                          (cbbox[0], cbbox[1], cbbox[2] - cbbox[0], cbbox[3] - cbbox[1]),
                          cbbox[0], cbbox[1], 64, 128))

    for i in range(3):
        canvas = make_tuft(3000 + i)
        bbox = canvas.getbbox()
        specs.append((f"decor_tuft_{i + 1}", canvas, (bbox[0], bbox[1],
                     bbox[2] - bbox[0], bbox[3] - bbox[1]), bbox[0], bbox[1], 64, 128))

    # 尸体占位（M5-T1：两个倒地方向变体；含身下血泊）
    for i, flip in enumerate((False, True)):
        canvas = make_corpse(5000 + i, flip=flip)
        bbox = canvas.getbbox()
        specs.append((f"corpse_{i + 1}", canvas, (bbox[0], bbox[1],
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
