#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""自制 tileset 打包工具（M12-T4）。

把目录中的 PNG 帧按 shelf 方式打进一张图集页，输出与引擎一致的
`pz-atlas-v1` JSON；可选调用 bgfx `texturec` 额外生成压缩纹理（发布用，
运行时仍读取 PNG 页，保持开发期零依赖）。

用法:
    py -3 tools/pack_tileset.py assets/my_tiles --out build/tileset --page atlas.png
    py -3 tools/pack_tileset.py assets/my_tiles --out build/tileset --texturec texturec.exe

输入帧命名即逻辑帧名（文件名去掉 .png，全小写+下划线）；画布原点/尺寸
写入 ox=0, oy=0, fx=w, fy=h，适合整图 tileset/图标。需要 PZ 式偏移的
角色/高物体请继续使用 gen_placeholder_atlas.py 或手写 atlas JSON。
"""

import argparse
import json
import os
import shutil
import subprocess
import sys

try:
    from PIL import Image
except ImportError:
    print("需要 Pillow：py -3 -m pip install pillow")
    sys.exit(2)


def parse_size(text):
    try:
        w, h = text.lower().split("x", 1)
        return int(w), int(h)
    except Exception:
        raise argparse.ArgumentTypeError("尺寸格式应为 WxH，例如 2048x2048")


def collect_frames(source_dir):
    if not os.path.isdir(source_dir):
        raise ValueError("输入目录不存在：%s" % source_dir)
    names = sorted(
        name for name in os.listdir(source_dir)
        if name.lower().endswith(".png") and os.path.isfile(os.path.join(source_dir, name))
    )
    if not names:
        raise ValueError("目录中没有 PNG：%s" % source_dir)

    frames = []
    seen = set()
    for name in names:
        logical = os.path.splitext(name)[0].lower()
        if logical in seen:
            raise ValueError("重复的逻辑帧名：%s" % logical)
        seen.add(logical)
        image = Image.open(os.path.join(source_dir, name)).convert("RGBA")
        if image.width <= 0 or image.height <= 0:
            raise ValueError("空图片：%s" % name)
        frames.append((logical, image))
    return frames


def shelf_pack(frames, max_width, margin):
    placements = []
    x = margin
    y = margin
    row_h = 0
    for name, image in frames:
        w, h = image.size
        if w + margin * 2 > max_width:
            raise ValueError("帧 %s 宽度 %d 超过页宽 %d" % (name, w, max_width))
        if x + w + margin > max_width:
            x = margin
            y += row_h + margin
            row_h = 0
        placements.append((name, image, x, y))
        x += w + margin
        row_h = max(row_h, h)
    height = y + row_h + margin
    page = Image.new("RGBA", (max_width, height), (0, 0, 0, 0))
    for name, image, px, py in placements:
        page.alpha_composite(image, (px, py))
    return page, placements


def write_atlas(out_dir, page_name, placements):
    page_path = os.path.join(out_dir, page_name)
    subtextures = {}
    for name, image, px, py in placements:
        subtextures[name] = {
            "x": px,
            "y": py,
            "w": image.width,
            "h": image.height,
            "ox": 0,
            "oy": 0,
            "fx": image.width,
            "fy": image.height,
        }
    atlas = {
        "format": "pz-atlas-v1",
        "source_pack": "pack_tileset",
        "page": os.path.splitext(page_name)[0],
        "image": page_name,
        "has_mask": True,
        "subtextures": subtextures,
    }
    atlas_path = os.path.splitext(page_path)[0] + ".atlas.json"
    with open(atlas_path, "w", encoding="utf-8") as f:
        json.dump(atlas, f, ensure_ascii=False, indent=1)
    return page_path, atlas_path, atlas


def run_texturec(texturec, png_path, out_dir):
    executable = texturec
    if os.path.isdir(executable):
        candidate = os.path.join(executable, "texturec.exe" if os.name == "nt" else "texturec")
        if os.path.isfile(candidate):
            executable = candidate
    if not os.path.isfile(executable) and shutil.which(executable) is None:
        raise ValueError("找不到 texturec：%s" % texturec)
    compressed = os.path.join(out_dir, os.path.splitext(os.path.basename(png_path))[0] + ".dds")
    result = subprocess.run(
        [executable, "-f", png_path, "-o", compressed],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError("texturec 失败（%d）：%s" % (result.returncode, result.stdout.strip()))
    return compressed


def main():
    parser = argparse.ArgumentParser(description="PNG frames -> pz-atlas-v1 tileset")
    parser.add_argument("source_dir")
    parser.add_argument("--out", default="build/tileset", help="输出目录")
    parser.add_argument("--page", default="tileset.png", help="页文件名")
    parser.add_argument("--max-width", type=int, default=2048)
    parser.add_argument("--size", type=parse_size, default=None, help="固定输出 WxH（默认按内容高度）")
    parser.add_argument("--margin", type=int, default=1)
    parser.add_argument("--texturec", default=None, help="可选 texturec 可执行文件/目录")
    args = parser.parse_args()

    if args.max_width < 16 or args.margin < 0:
        print("--max-width 至少 16，--margin 不能为负")
        return 2
    try:
        frames = collect_frames(args.source_dir)
        os.makedirs(args.out, exist_ok=True)
        page, placements = shelf_pack(frames, args.max_width, args.margin)
        if args.size is not None:
            if args.size[0] < page.width or args.size[1] < page.height:
                raise ValueError("--size 小于内容尺寸 %dx%d" % (page.width, page.height))
            canvas = Image.new("RGBA", args.size, (0, 0, 0, 0))
            canvas.alpha_composite(page, (0, 0))
            page = canvas
        page_path = os.path.join(args.out, args.page)
        page.save(page_path)
        page_path, atlas_path, atlas = write_atlas(args.out, args.page, placements)
        print("页    : %s (%dx%d)" % (page_path, page.width, page.height))
        print("图集  : %s (%d 帧)" % (atlas_path, len(atlas["subtextures"])))
        if args.texturec:
            compressed = run_texturec(args.texturec, page_path, args.out)
            print("压缩  : %s" % compressed)
    except Exception as exc:
        print("失败：%s" % exc)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
