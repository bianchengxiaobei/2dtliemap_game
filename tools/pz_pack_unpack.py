#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PZ .pack 解包器（本地占位资源工具）

把 Project Zomboid 的 media/texturepacks/*.pack 解包为：
    <out>/<pack 名>/<页名>.png          页纹理图（原样 PNG 字节）
    <out>/<pack 名>/<页名>.atlas.json   子图元数据（供引擎图集系统使用）

容器格式（两种，均已由反编译源码 TexturePackDevice.readPage 与实测字节确认）：

v1（文件头含魔数）：
    u32  magic "PZPK" / u32 version=1 / u32 pageCount
    page × {
        str 页名 (u32 长度 + UTF-8), u32 子图数, u32 flag（1=该页有 alpha mask）,
        子图数 × { str 子图名, i32 x,y,w,h, i32 ox,oy, i32 fx,fy },  # 图内矩形/绘制偏移/原始尺寸
        u32 图片长度 N, N 字节 PNG 数据
    }

v0（旧格式，无魔数）：
    u32 pageCount
    page × {
        str 页名, u32 子图数, u32 flag,
        子图数 × { str 子图名, i32 x,y,w,h, i32 ox,oy, i32 fx,fy },
        PNG 数据（按 PNG chunk 自边界，至 IEND 结束）,
        u32 页尾标记 0xDEADBEEF（磁盘字节序 EF BE AD DE）
    }

用法示例：
    py -3 tools/pz_pack_unpack.py --only Tiles1x.floor
    py -3 tools/pz_pack_unpack.py Tiles1x.pack --out assets_placeholder_pz/texturepacks
    py -3 tools/pz_pack_unpack.py            # 解包 --pack-dir 下全部

合规提醒：解出的资产版权归 The Indie Stone 所有，仅限本地个人研究/开发占位，
禁止提交到公开仓库、禁止随发行包分发。
"""

import argparse
import json
import os
import struct
import sys

PZPK_MAGIC = b"PZPK"
PNG_SIG = b"\x89PNG\r\n\x1a\n"
PAGE_MARKER = 0xDEADBEEF  # v0 页尾标记；磁盘字节序为 EF BE AD DE

DEFAULT_PACK_DIR = r"E:\Game\Project.Zomboid.v41.78.16\Project.Zomboid.v41.78.16\media\texturepacks"
DEFAULT_OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets_placeholder_pz", "texturepacks")


def read_u32(f):
    b = f.read(4)
    if len(b) < 4:
        raise ValueError("文件截断（读 u32 失败）")
    return struct.unpack("<I", b)[0]


def read_str(f):
    n = read_u32(f)
    b = f.read(n)
    if len(b) < n:
        raise ValueError("文件截断（读字符串失败）")
    return b.decode("utf-8", errors="replace")


def read_image_data(f, keep):
    """读 u32 长度 + N 字节 PNG 数据；keep=False 时只前进不保留内存。"""
    n = read_u32(f)
    if keep:
        png = f.read(n)
        if len(png) < n:
            raise ValueError("PNG 数据截断（期望 %d 字节）" % n)
        if not png.startswith(PNG_SIG):
            raise ValueError("图片数据不是 PNG（头字节 %r）" % png[:4])
        if png[-8:-4] != b"IEND":
            raise ValueError("PNG 缺少 IEND 结尾")
        return png
    remaining = n
    while remaining > 0:
        chunk = f.read(min(remaining, 1 << 20))
        if not chunk:
            raise ValueError("PNG 数据截断（剩余 %d 字节）" % remaining)
        remaining -= len(chunk)
    return None


def read_png_until_iend(f, keep):
    """v0：PNG 自带边界（chunk 走到 IEND）；keep=False 只前进不保留内存。"""
    sig = f.read(8)
    if sig != PNG_SIG:
        raise ValueError("图片数据不是 PNG（头字节 %r）" % sig[:4])
    parts = [sig] if keep else None
    while True:
        hdr = f.read(8)
        if len(hdr) < 8:
            raise ValueError("PNG 截断（chunk 头）")
        length, ctype = struct.unpack(">I4s", hdr)
        body = f.read(length + 4)
        if len(body) < length + 4:
            raise ValueError("PNG 截断（chunk 数据）")
        if keep:
            parts.append(hdr)
            parts.append(body)
        if ctype == b"IEND":
            return b"".join(parts) if keep else None


def unpack_pack(path, out_root, only=None):
    pack_name = os.path.splitext(os.path.basename(path))[0]
    out_dir = os.path.join(out_root, pack_name)
    written, errors = 0, 0
    with open(path, "rb") as f:
        first = f.read(4)
        if first == PZPK_MAGIC:
            version = read_u32(f)
            if version != 1:
                raise ValueError("不支持的 .pack 版本 %d" % version)
            n_pages = read_u32(f)
            container = "v1"
            print("[%s] PZPK v1，%d 页" % (pack_name, n_pages))
        else:
            n_pages = struct.unpack("<I", first)[0]
            container = "v0"
            print("[%s] 旧格式 v0（无魔数），%d 页" % (pack_name, n_pages))
        for _ in range(n_pages):
            try:
                page = read_str(f)
                n_sub = read_u32(f)
                flag = read_u32(f)
                subs = {}
                for _s in range(n_sub):
                    name = read_str(f)
                    x, y, w, h, ox, oy, fx, fy = (read_u32(f) for _ in range(8))
                    subs[name] = {"x": x, "y": y, "w": w, "h": h,
                                  "ox": ox, "oy": oy, "fx": fx, "fy": fy}
                wanted = (only is None) or (only.lower() in page.lower())
                if container == "v1":
                    png = read_image_data(f, keep=wanted)
                else:
                    png = read_png_until_iend(f, keep=wanted)
                    marker = read_u32(f)
                    if marker != PAGE_MARKER:
                        raise ValueError("页尾标记错误 0x%08X（期望 0xDEADBEEF）" % marker)
                if not wanted:
                    print("  - 跳过 %s（%d 子图）" % (page, n_sub))
                    continue
                os.makedirs(out_dir, exist_ok=True)
                png_path = os.path.join(out_dir, page + ".png")
                atlas_path = os.path.join(out_dir, page + ".atlas.json")
                with open(png_path, "wb") as pf:
                    pf.write(png)
                atlas = {
                    "format": "pz-atlas-v1",
                    "source_pack": os.path.basename(path),
                    "page": page,
                    "image": page + ".png",
                    "has_mask": bool(flag),
                    "subtextures": subs,
                }
                with open(atlas_path, "w", encoding="utf-8") as af:
                    json.dump(atlas, af, ensure_ascii=False, indent=1)
                written += 1
                print("  + %s：%d 子图，PNG %d KB" % (page, n_sub, len(png) // 1024))
            except Exception as e:
                errors += 1
                print("  ! 页解析失败（第 %d 页起）：%s" % (written + errors, e))
                break  # 流位置已不可信，放弃该 pack 剩余页
    return written, errors


def main():
    ap = argparse.ArgumentParser(description="Project Zomboid .pack 解包器（本地占位资源用途）")
    ap.add_argument("packs", nargs="*", help=".pack 文件或目录（可多个）；缺省解包 --pack-dir 全部")
    ap.add_argument("--pack-dir", default=DEFAULT_PACK_DIR, help="PZ texturepacks 目录（默认：本机 PZ 安装）")
    ap.add_argument("--out", default=os.path.normpath(DEFAULT_OUT), help="输出根目录（默认 assets_placeholder_pz/texturepacks）")
    ap.add_argument("--only", default=None, help="只导出页名包含该子串的页（如 Tiles1x0 / floor）")
    args = ap.parse_args()

    targets = []
    for p in args.packs:
        if os.path.isdir(p):
            targets += [os.path.join(p, x) for x in sorted(os.listdir(p)) if x.lower().endswith(".pack")]
        else:
            targets.append(p)
    if not targets:
        if not os.path.isdir(args.pack_dir):
            print("找不到 PZ texturepacks 目录：%s\n请用 --pack-dir 指定。" % args.pack_dir)
            return 2
        targets = [os.path.join(args.pack_dir, x) for x in sorted(os.listdir(args.pack_dir)) if x.lower().endswith(".pack")]

    total_pages, total_err = 0, 0
    for t in targets:
        if not os.path.isfile(t):
            print("文件不存在：%s" % t)
            total_err += 1
            continue
        try:
            w, e = unpack_pack(t, args.out, args.only)
            total_pages += w
            total_err += e
        except Exception as ex:
            total_err += 1
            print("[%s] 失败：%s" % (os.path.basename(t), ex))

    print("\n完成：导出 %d 页 -> %s（错误 %d）" % (total_pages, args.out, total_err))
    return 1 if total_err else 0


if __name__ == "__main__":
    sys.exit(main())
