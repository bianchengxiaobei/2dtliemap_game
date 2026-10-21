#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Tiled 地图导入器（M12-T4 后续；输出 zomboidlike-map-v1）。

支持：
  - Tiled JSON 地图/图块集：.tmj/.json + .tsj/.json
  - Tiled XML 地图/图块集：.tmx/.xml + .tsx/.xml
  - 有限正交地图（finite orthogonal map；暂不支持 hex/isometric/infinite）
  - 图块层 → 地面 floors；其他图块层 → wall/furniture/item/mark 对象
  - 对象层矩形/图块对象 → 对象；class=room 的矩形 → rooms
  - 外部 tileset、CSV/XML/base64(zlib/gzip) 图块数据

图层映射约定见 docs/tiled-import.md。地图坐标以 Tiled 图块为单位；图块像素
尺寸只用于把 Tiled 对象层的像素坐标换算为格坐标，不做缩放。

示例：
  py -3 tools/tiled_import.py maps/village.tmx --out assets/maps/village
  py -3 tools/tiled_import.py maps/village.tmj --name village --force
"""

import argparse
import base64
import gzip
import json
import math
import os
import shutil
import struct
import sys
import xml.etree.ElementTree as ET
import zlib
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CHUNK = 10
CHUNKS_PER_CELL = 30
MAX_Z = 8
MAX_ROOM_ID = 32767
FORMAT = "zomboidlike-map-v1"
DEFAULT_FLOOR = "tile.floor.grass"
GID_MASK = 0x0FFFFFFF
GID_FLAGS = 0xF0000000
PROJECT_LAYERS = ("floor", "wall", "furniture", "item", "mark")
# 单字节 legend 字符；地面默认字符固定为 g，保持与 gen_map.py 一致。
LEGEND_CHARS = "gdw" + "".join(
    char for char in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    if char not in "gdw")
GROUND_NAMES = ("ground", "floor", "floors", "terrain", "groundfloor", "地面", "地板")
WALL_NAMES = ("wall", "walls", "墙", "墙壁")
FURNITURE_NAMES = ("furniture", "object", "objects", "prop", "props", "decor",
                   "家具", "物体", "物件", "装饰")
ITEM_NAMES = ("item", "items", "物品")
MARK_NAMES = ("mark", "marks", "marker", "markers", "标记")
ROOM_CLASSES = ("room", "rooms", "房间")


class ToolError(Exception):
    """输入或输出错误；main() 会转成简短错误信息。"""


@dataclass
class TileInfo:
    sprite: str = ""
    floor: str = ""
    solid: bool = False
    ox: int = 0
    oy: int = 0


@dataclass
class TilesetInfo:
    name: str
    firstgid: int
    source: str
    tiles: dict = field(default_factory=dict)
    default_sprite: str = ""
    default_floor: str = ""
    default_solid: bool = False


@dataclass
class LayerInfo:
    kind: str
    name: str
    visible: bool = True
    properties: dict = field(default_factory=dict)
    offset_x: float = 0.0
    offset_y: float = 0.0
    width: int = 0
    height: int = 0
    data: list = field(default_factory=list)
    objects: list = field(default_factory=list)
    opacity: float = 1.0


@dataclass
class ParsedMap:
    source: Path
    fmt: str
    width: int
    height: int
    tile_width: int
    tile_height: int
    properties: dict
    layers: list
    tilesets: list


def _as_bool(value, default=False):
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        text = value.strip().lower()
        if text in ("1", "true", "yes", "on"):
            return True
        if text in ("0", "false", "no", "off"):
            return False
    return default


def _as_int(value, default=0, label="整数"):
    try:
        if isinstance(value, bool):
            raise ValueError
        if isinstance(value, str) and not value.strip():
            raise ValueError
        number = float(value)
        if not math.isfinite(number) or not number.is_integer():
            raise ValueError
        return int(number)
    except (TypeError, ValueError):
        return default


def _as_float(value, default=0.0):
    try:
        number = float(value)
        return number if math.isfinite(number) else default
    except (TypeError, ValueError):
        return default


def _required_int(value, label):
    result = _as_int(value, None, label)
    if result is None:
        raise ToolError("%s 必须是整数：%r" % (label, value))
    return result


def _required_float(value, label, default=0.0):
    if value is None:
        return default
    result = _as_float(value, None)
    if result is None:
        raise ToolError("%s 必须是数字：%r" % (label, value))
    return result


def _string(value, default=""):
    return value.strip() if isinstance(value, str) else default


def _int_property(props, key, default, label):
    if key not in props or props[key] is None:
        return default
    value = _as_int(props[key], None)
    if value is None:
        raise ToolError("%s 必须是整数：%r" % (label, props[key]))
    return value


def _properties_json(raw):
    if isinstance(raw, dict):
        return dict(raw)
    result = {}
    if not isinstance(raw, list):
        return result
    for item in raw:
        if not isinstance(item, dict):
            continue
        name = item.get("name")
        if not isinstance(name, str) or not name:
            continue
        result[name] = item.get("value")
    return result


def _properties_xml(element):
    result = {}
    if element is None:
        return result
    props = element.find("properties")
    if props is None:
        return result
    for prop in props.findall("property"):
        name = prop.get("name")
        if not name:
            continue
        value = prop.get("value")
        if value is None:
            value = prop.text if prop.text is not None else ""
        kind = (prop.get("type") or "string").lower()
        if kind in ("int", "uint"):
            value = _as_int(value, 0)
        elif kind == "float":
            value = _as_float(value, 0.0)
        elif kind == "bool":
            value = _as_bool(value, False)
        result[name] = value
    return result


def _class_of(raw):
    value = raw.get("class")
    if not value:
        value = raw.get("type")
    return _string(value, "")


def _resolve_source(value, base):
    if not isinstance(value, str) or not value:
        raise ToolError("Tiled 资源引用为空")
    path = Path(value.replace("\\", os.sep))
    if not path.is_absolute():
        path = base.parent / path
    return path.resolve()


def _read_text(path):
    try:
        return path.read_text(encoding="utf-8-sig")
    except OSError as exc:
        raise ToolError("无法读取 %s：%s" % (path, exc))
    except UnicodeDecodeError as exc:
        raise ToolError("文件不是 UTF-8：%s（%s）" % (path, exc))


def _looks_xml(path, text):
    if path.suffix.lower() in (".tmx", ".tsx", ".xml"):
        return True
    if path.suffix.lower() in (".tmj", ".tsj"):
        return False
    return text.lstrip().startswith("<")


class TiledImporter:
    def __init__(self, source, *, name=None, default_floor=DEFAULT_FLOOR,
                 allow_unmapped=False, include_hidden=False):
        self.source = Path(source).resolve()
        self.name = name or self.source.stem
        if not self.name:
            raise ToolError("地图名不能为空")
        self.default_floor = default_floor
        self.allow_unmapped = allow_unmapped
        self.include_hidden = include_hidden
        self.map = None
        self.floor_cells = {}
        self.objects = []
        self.rooms = []
        self.legend = {}
        self._char_by_sprite = {}
        self._room_ids = set()
        self._next_auto_room_id = 1
        self._warnings = []
        self._warning_set = set()
        self._flip_count = 0
        self._skipped_objects = 0
        self._unmapped = 0

    # ------------------------------------------------------------------ 输入

    def run(self, out_dir, force=False):
        self._load()
        self._build()
        self._write(out_dir, force)
        return self.summary(out_dir)

    def warn(self, message):
        if message not in self._warning_set:
            self._warning_set.add(message)
            self._warnings.append(message)

    def summary(self, out_dir):
        return {
            "out": str(out_dir),
            "width": self.map.width,
            "height": self.map.height,
            "chunksX": (self.map.width + CHUNK - 1) // CHUNK,
            "chunksY": (self.map.height + CHUNK - 1) // CHUNK,
            "objects": len(self.objects),
            "rooms": len(self.rooms),
            "floorOverrides": len(self.floor_cells),
            "warnings": list(self._warnings),
            "flips": self._flip_count,
            "skippedObjects": self._skipped_objects,
            "unmapped": self._unmapped,
        }

    def _load(self):
        if not self.source.is_file():
            raise ToolError("输入地图不存在：%s" % self.source)
        text = _read_text(self.source)
        if _looks_xml(self.source, text):
            self._load_xml(text)
        else:
            self._load_json(text)
        if self.map.width <= 0 or self.map.height <= 0:
            raise ToolError("地图尺寸必须大于 0：%dx%d" % (self.map.width, self.map.height))
        if self.map.tile_width <= 0 or self.map.tile_height <= 0:
            raise ToolError("tilewidth/tileheight 必须大于 0")
        if not self.default_floor:
            raise ToolError("--default-floor 不能为空")
        seen = set()
        for tileset in self.map.tilesets:
            if tileset.firstgid <= 0:
                raise ToolError("tileset firstgid 必须大于 0：%s" % tileset.source)
            if tileset.firstgid in seen:
                raise ToolError("tileset firstgid 重复：%d" % tileset.firstgid)
            seen.add(tileset.firstgid)
        self.map.tilesets.sort(key=lambda ts: ts.firstgid)
        if not self.map.tilesets and any(
                layer.kind == "tilelayer" and any(layer.data) for layer in self.map.layers):
            raise ToolError("地图有图块数据但没有 tileset")

    def _load_json(self, text):
        try:
            doc = json.loads(text)
        except ValueError as exc:
            raise ToolError("Tiled JSON 解析失败：%s" % exc)
        if not isinstance(doc, dict):
            raise ToolError("Tiled JSON 根节点必须是对象")
        kind = doc.get("type")
        if kind not in (None, "map"):
            raise ToolError("输入不是 Tiled 地图（type=%r）" % kind)
        self._validate_common_json(doc)
        layers = self._json_layers(doc.get("layers") or [])
        tilesets = []
        for entry in doc.get("tilesets") or []:
            if not isinstance(entry, dict):
                raise ToolError("tilesets 条目必须是对象")
            tilesets.append(self._load_json_tileset_entry(entry))
        self.map = ParsedMap(
            source=self.source,
            fmt="json",
            width=_required_int(doc.get("width"), "地图 width"),
            height=_required_int(doc.get("height"), "地图 height"),
            tile_width=_required_int(doc.get("tilewidth"), "地图 tilewidth"),
            tile_height=_required_int(doc.get("tileheight"), "地图 tileheight"),
            properties=_properties_json(doc.get("properties")),
            layers=layers,
            tilesets=tilesets,
        )

    def _validate_common_json(self, doc):
        orientation = _string(doc.get("orientation"), "orthogonal").lower()
        if orientation != "orthogonal":
            raise ToolError("暂只支持 orthogonal 地图，当前 orientation=%s" % orientation)
        if _as_bool(doc.get("infinite"), False):
            raise ToolError("暂不支持 infinite 地图，请在 Tiled 中改为固定尺寸后导出")

    def _load_xml(self, text):
        try:
            root = ET.fromstring(text)
        except ET.ParseError as exc:
            raise ToolError("Tiled XML 解析失败：%s" % exc)
        if self._tag(root) != "map":
            raise ToolError("输入不是 Tiled 地图（根节点 %s）" % self._tag(root))
        orientation = _string(root.get("orientation"), "orthogonal").lower()
        if orientation != "orthogonal":
            raise ToolError("暂只支持 orthogonal 地图，当前 orientation=%s" % orientation)
        if _as_bool(root.get("infinite"), False):
            raise ToolError("暂不支持 infinite 地图，请在 Tiled 中改为固定尺寸后导出")
        layers = self._xml_layers(root)
        tilesets = []
        for node in root.findall("tileset"):
            tilesets.append(self._load_xml_tileset_entry(node))
        self.map = ParsedMap(
            source=self.source,
            fmt="xml",
            width=_required_int(root.get("width"), "地图 width"),
            height=_required_int(root.get("height"), "地图 height"),
            tile_width=_required_int(root.get("tilewidth"), "地图 tilewidth"),
            tile_height=_required_int(root.get("tileheight"), "地图 tileheight"),
            properties=_properties_xml(root),
            layers=layers,
            tilesets=tilesets,
        )

    @staticmethod
    def _tag(node):
        return node.tag.rsplit("}", 1)[-1]

    # ---------------------------------------------------------------- 图层

    def _json_layers(self, nodes, inherited_visible=True):
        layers = []
        if not isinstance(nodes, list):
            raise ToolError("Tiled layers 必须是数组")
        for raw in nodes:
            if not isinstance(raw, dict):
                continue
            kind = _string(raw.get("type"), "")
            visible = inherited_visible and _as_bool(raw.get("visible"), True)
            if kind == "group":
                layers.extend(self._json_layers(raw.get("layers") or [], visible))
                continue
            props = _properties_json(raw.get("properties"))
            offset_x = _required_float(raw.get("offsetx"), "图层 offsetx", 0.0)
            offset_y = _required_float(raw.get("offsety"), "图层 offsety", 0.0)
            opacity = _as_float(raw.get("opacity"), 1.0)
            if kind == "tilelayer":
                width = _as_int(raw.get("width"), 0)
                height = _as_int(raw.get("height"), 0)
                data = raw.get("data")
                if raw.get("chunks"):
                    raise ToolError("暂不支持 infinite/分块图块层：%s" % raw.get("name", ""))
                if not isinstance(data, list):
                    raise ToolError("图块层 %s 缺少 data 数组" % raw.get("name", ""))
                layers.append(LayerInfo(
                    kind="tilelayer", name=_string(raw.get("name"), "tilelayer"),
                    visible=visible, properties=props, offset_x=offset_x,
                    offset_y=offset_y, width=width, height=height,
                    data=[_required_int(gid, "图块 GID") for gid in data],
                    opacity=opacity,
                ))
            elif kind == "objectgroup":
                objects = []
                for obj in raw.get("objects") or []:
                    if not isinstance(obj, dict):
                        continue
                    normalized = dict(obj)
                    normalized["properties"] = _properties_json(obj.get("properties"))
                    normalized["class"] = _class_of(obj)
                    for shape in ("ellipse", "point", "polygon", "polyline"):
                        if shape in obj:
                            normalized["shape"] = shape
                            break
                    objects.append(normalized)
                layers.append(LayerInfo(
                    kind="objectgroup", name=_string(raw.get("name"), "objectgroup"),
                    visible=visible, properties=props, offset_x=offset_x,
                    offset_y=offset_y, objects=objects, opacity=opacity,
                ))
            elif kind == "imagelayer":
                self.warn("跳过 Tiled 图像层（Tiled image layer）：%s" % raw.get("name", ""))
            else:
                self.warn("跳过未知图层类型 %r：%s" % (kind, raw.get("name", "")))
        return layers

    def _xml_layers(self, parent, inherited_visible=True):
        layers = []
        for child in parent:
            tag = self._tag(child)
            if tag == "group":
                visible = inherited_visible and _as_bool(child.get("visible"), True)
                layers.extend(self._xml_layers(child, visible))
            elif tag == "layer":
                visible = inherited_visible and _as_bool(child.get("visible"), True)
                width = _as_int(child.get("width"), 0)
                height = _as_int(child.get("height"), 0)
                layers.append(LayerInfo(
                    kind="tilelayer", name=child.get("name") or "tilelayer",
                    visible=visible, properties=_properties_xml(child),
                    offset_x=_required_float(child.get("offsetx"), "图层 offsetx", 0.0),
                    offset_y=_required_float(child.get("offsety"), "图层 offsety", 0.0),
                    width=width, height=height, data=self._xml_tile_data(child),
                    opacity=_as_float(child.get("opacity"), 1.0),
                ))
            elif tag == "objectgroup":
                visible = inherited_visible and _as_bool(child.get("visible"), True)
                objects = []
                for obj in child.findall("object"):
                    normalized = dict(obj.attrib)
                    normalized["properties"] = _properties_xml(obj)
                    normalized["class"] = _string(obj.get("class") or obj.get("type"), "")
                    for shape in ("ellipse", "point", "polygon", "polyline"):
                        if obj.find(shape) is not None:
                            normalized["shape"] = shape
                            break
                    objects.append(normalized)
                layers.append(LayerInfo(
                    kind="objectgroup", name=child.get("name") or "objectgroup",
                    visible=visible, properties=_properties_xml(child),
                    offset_x=_required_float(child.get("offsetx"), "图层 offsetx", 0.0),
                    offset_y=_required_float(child.get("offsety"), "图层 offsety", 0.0),
                    objects=objects, opacity=_as_float(child.get("opacity"), 1.0),
                ))
            elif tag == "imagelayer":
                self.warn("跳过 Tiled 图像层（Tiled image layer）：%s" % (child.get("name") or ""))
        return layers

    def _xml_tile_data(self, layer):
        data = layer.find("data")
        if data is None:
            raise ToolError("图块层 %s 缺少 <data>" % (layer.get("name") or ""))
        if data.find("chunk") is not None:
            raise ToolError("暂不支持 infinite/分块图块层：%s" % (layer.get("name") or ""))
        encoding = (data.get("encoding") or "").lower()
        if not encoding:
            values = []
            for tile in data.findall("tile"):
                values.append(_required_int(tile.get("gid", "0"), "图块 GID"))
            return values
        text = (data.text or "").strip()
        if encoding == "csv":
            values = []
            for item in text.split(","):
                item = item.strip()
                if item:
                    values.append(_required_int(item, "CSV 图块 GID"))
            return values
        if encoding == "base64":
            try:
                raw = base64.b64decode("".join(text.split()), validate=True)
            except (ValueError, TypeError) as exc:
                raise ToolError("base64 图块数据解码失败：%s" % exc)
            compression = (data.get("compression") or "").lower()
            try:
                if compression == "zlib":
                    raw = zlib.decompress(raw)
                elif compression == "gzip":
                    raw = gzip.decompress(raw)
                elif compression not in ("", "none"):
                    raise ToolError("不支持的图块压缩：%s" % compression)
            except (OSError, zlib.error) as exc:
                raise ToolError("图块解压失败：%s" % exc)
            if len(raw) % 4:
                raise ToolError("base64 图块数据长度不是 4 的倍数")
            return list(struct.unpack("<%dI" % (len(raw) // 4), raw))
        raise ToolError("不支持的图块数据编码：%s" % encoding)

    # --------------------------------------------------------------- 图块集

    def _load_json_tileset_entry(self, entry):
        firstgid = _required_int(entry.get("firstgid"), "tileset firstgid")
        source = entry.get("source")
        if source:
            path = _resolve_source(source, self.source)
            text = _read_text(path)
            if _looks_xml(path, text):
                try:
                    root = ET.fromstring(text)
                except ET.ParseError as exc:
                    raise ToolError("TSX 解析失败 %s：%s" % (path, exc))
                if self._tag(root) != "tileset":
                    raise ToolError("外部图块集不是 <tileset>：%s" % path)
                return self._parse_tileset_xml(root, firstgid, str(path))
            try:
                doc = json.loads(text)
            except ValueError as exc:
                raise ToolError("TSJ 解析失败 %s：%s" % (path, exc))
            return self._parse_tileset_json(doc, firstgid, str(path))
        return self._parse_tileset_json(entry, firstgid, "<embedded>")

    def _load_xml_tileset_entry(self, node):
        firstgid = _required_int(node.get("firstgid"), "tileset firstgid")
        source = node.get("source")
        if source:
            path = _resolve_source(source, self.source)
            text = _read_text(path)
            if not _looks_xml(path, text):
                raise ToolError("XML 地图引用了非 XML 图块集：%s" % path)
            try:
                root = ET.fromstring(text)
            except ET.ParseError as exc:
                raise ToolError("TSX 解析失败 %s：%s" % (path, exc))
            if self._tag(root) != "tileset":
                raise ToolError("外部图块集不是 <tileset>：%s" % path)
            return self._parse_tileset_xml(root, firstgid, str(path))
        return self._parse_tileset_xml(node, firstgid, "<embedded>")

    def _parse_tileset_json(self, doc, firstgid, source):
        if not isinstance(doc, dict):
            raise ToolError("图块集 JSON 根节点必须是对象：%s" % source)
        name = _string(doc.get("name"), Path(source).stem)
        props = _properties_json(doc.get("properties"))
        default_sprite = _string(props.get("sprite") or props.get("logical"), "")
        default_floor = _string(props.get("floor"), default_sprite)
        default_solid = _as_bool(props.get("solid"), False)
        tiles = {}
        for raw in doc.get("tiles") or []:
            if not isinstance(raw, dict):
                continue
            tile_id = _required_int(raw.get("id"), "图块 id")
            if tile_id < 0:
                raise ToolError("图块 id 不能为负：%s" % source)
            if tile_id in tiles:
                raise ToolError("图块 id 重复：%s/%d" % (source, tile_id))
            tile_props = _properties_json(raw.get("properties"))
            tile_class = _class_of(raw)
            sprite = _string(tile_props.get("sprite"), "") or tile_class or default_sprite
            floor = _string(tile_props.get("floor"), "") or sprite or default_floor
            tiles[tile_id] = TileInfo(
                sprite=sprite,
                floor=floor,
                solid=_as_bool(tile_props.get("solid"), default_solid),
                ox=_as_int(tile_props.get("ox"), 0),
                oy=_as_int(tile_props.get("oy"), 0),
            )
        return TilesetInfo(
            name=name, firstgid=firstgid, source=source, tiles=tiles,
            default_sprite=default_sprite, default_floor=default_floor,
            default_solid=default_solid,
        )

    def _parse_tileset_xml(self, root, firstgid, source):
        name = root.get("name") or Path(source).stem
        props = _properties_xml(root)
        default_sprite = _string(props.get("sprite") or props.get("logical"), "")
        default_floor = _string(props.get("floor"), default_sprite)
        default_solid = _as_bool(props.get("solid"), False)
        tiles = {}
        for raw in root.findall("tile"):
            tile_id = _required_int(raw.get("id"), "图块 id")
            if tile_id < 0:
                raise ToolError("图块 id 不能为负：%s" % source)
            if tile_id in tiles:
                raise ToolError("图块 id 重复：%s/%d" % (source, tile_id))
            tile_props = _properties_xml(raw)
            tile_class = _string(raw.get("class") or raw.get("type"), "")
            sprite = _string(tile_props.get("sprite"), "") or tile_class or default_sprite
            floor = _string(tile_props.get("floor"), "") or sprite or default_floor
            tiles[tile_id] = TileInfo(
                sprite=sprite,
                floor=floor,
                solid=_as_bool(tile_props.get("solid"), default_solid),
                ox=_as_int(tile_props.get("ox"), 0),
                oy=_as_int(tile_props.get("oy"), 0),
            )
        return TilesetInfo(
            name=name, firstgid=firstgid, source=source, tiles=tiles,
            default_sprite=default_sprite, default_floor=default_floor,
            default_solid=default_solid,
        )

    def _tileset_for_gid(self, gid):
        result = None
        for tileset in self.map.tilesets:
            if tileset.firstgid <= gid:
                result = tileset
            else:
                break
        if result is None:
            raise ToolError("GID %d 找不到对应 tileset" % gid)
        return result

    def _tile_for_gid(self, gid, context):
        tileset = self._tileset_for_gid(gid)
        local_id = gid - tileset.firstgid
        info = tileset.tiles.get(local_id)
        if info is None:
            info = TileInfo(
                sprite=tileset.default_sprite,
                floor=tileset.default_floor or tileset.default_sprite,
                solid=tileset.default_solid,
            )
        return tileset, local_id, info

    def _sprite_for_tile(self, gid, context, floor=False):
        tileset, local_id, info = self._tile_for_gid(gid, context)
        logical = (info.floor if floor else info.sprite) or info.sprite or info.floor
        if not logical:
            if self.allow_unmapped:
                self._unmapped += 1
                safe_name = (tileset.name or "tileset").replace(" ", "_").replace("/", "_")
                logical = "tile.%s.%d" % (safe_name, local_id)
                self.warn("图块 %s/%d 未配置 sprite/floor/class，使用占位逻辑名 %s"
                          % (tileset.name, local_id, logical))
            else:
                raise ToolError(
                    "图层 %s 的图块 GID %d（%s/%d）未配置 sprite/floor/class；"
                    "请在 Tiled 图块属性中设置 sprite，或使用 --allow-unmapped"
                    % (context, gid, tileset.name, local_id))
        return info, logical

    # --------------------------------------------------------------- 图层映射

    @staticmethod
    def _infer_mode(name):
        low = name.strip().lower().replace("-", "_").replace(" ", "_")
        if low in GROUND_NAMES or low.startswith("ground_") or low.startswith("floor_"):
            return "floor"
        if low in WALL_NAMES or low.startswith("wall_") or low.startswith("walls_"):
            return "wall"
        if low in ITEM_NAMES or low.startswith("item_"):
            return "item"
        if low in MARK_NAMES or low.startswith("mark_"):
            return "mark"
        if low in FURNITURE_NAMES or low.startswith("object_") or low.startswith("prop_") \
                or low.startswith("props_"):
            return "furniture"
        return ""

    def _layer_options(self, layer, default_mode):
        raw = layer.properties.get("zlayer")
        if raw in (None, ""):
            raw = layer.properties.get("layer")
        if raw in (None, ""):
            raw = layer.properties.get("projectLayer")
        mode = ""
        if raw not in (None, ""):
            mode = _string(raw, "").lower()
            if mode not in PROJECT_LAYERS:
                raise ToolError("图层 %s 的 zlayer 非法：%r（可选 %s）"
                                % (layer.name, raw, "/".join(PROJECT_LAYERS)))
        else:
            mode = self._infer_mode(layer.name)
        if not mode:
            mode = default_mode or ""
        z = _int_property(layer.properties, "z", 0, "图层 %s 的 z" % layer.name)
        if z < 0 or z >= MAX_Z:
            raise ToolError("图层 %s 的 z=%d 越界（0..%d）" % (layer.name, z, MAX_Z - 1))
        solid = _as_bool(layer.properties.get("solid"), False)
        if layer.opacity < 0.999:
            self.warn("图层 %s opacity=%g；项目格式不支持透明度，按不透明导入"
                      % (layer.name, layer.opacity))
        return mode, z, solid

    def _layer_offset_cells(self, layer):
        ox = layer.offset_x / float(self.map.tile_width)
        oy = layer.offset_y / float(self.map.tile_height)
        if abs(ox - round(ox)) > 1e-6 or abs(oy - round(oy)) > 1e-6:
            raise ToolError("图层 %s 的像素偏移必须对齐 tile 尺寸：offsetx=%g offsety=%g"
                            % (layer.name, layer.offset_x, layer.offset_y))
        return int(round(ox)), int(round(oy))

    def _build(self):
        tile_layers = [layer for layer in self.map.layers if layer.kind == "tilelayer"]
        visible_tiles = [layer for layer in tile_layers if layer.visible or self.include_hidden]
        if not visible_tiles:
            self.warn("没有可见图块层，地图将使用默认地面")
        configs = {}
        ground = None
        candidates = []
        for layer in visible_tiles:
            configs[id(layer)] = self._layer_options(layer, "")
            mode, z, _ = configs[id(layer)]
            if mode == "floor" and z == 0:
                candidates.append(layer)
        if len(candidates) > 1:
            raise ToolError("多个 z=0 地面图块层，项目格式只能有一个：%s"
                            % ", ".join(layer.name for layer in candidates))
        if candidates:
            ground = candidates[0]
            mode, z, solid = configs[id(ground)]
            configs[id(ground)] = ("floor", z, solid)
        else:
            # 没有显式/命名地面层时，把最底层的无名图块层当作地面。
            # 名字已明确为 wall/item/furniture 的图层不会被误当成地面。
            for layer in visible_tiles:
                mode, z, solid = configs[id(layer)]
                if not mode and z == 0:
                    ground = layer
                    configs[id(layer)] = ("floor", 0, solid)
                    self.warn("自动选择图层 %s 作为地面；建议设置 zlayer=floor"
                              % ground.name)
                    break
        for layer in visible_tiles:
            mode, z, solid = configs[id(layer)]
            if not mode:
                configs[id(layer)] = ("furniture", z, solid)
        self._read_tile_layers(ground, configs)
        for layer in self.map.layers:
            if layer.kind != "objectgroup":
                continue
            if not layer.visible and not self.include_hidden:
                self.warn("跳过隐藏对象层：%s" % layer.name)
                continue
            self._read_object_layer(layer)
        self._check_room_ids()

    def _read_tile_layers(self, ground, configs):
        for layer in self.map.layers:
            if layer.kind != "tilelayer":
                continue
            if not layer.visible and not self.include_hidden:
                self.warn("跳过隐藏图块层：%s" % layer.name)
                continue
            mode, z, layer_solid = configs.get(id(layer), ("furniture", 0, False))
            width = layer.width or self.map.width
            height = layer.height or self.map.height
            if width <= 0 or height <= 0:
                width, height = self.map.width, self.map.height
            if len(layer.data) != width * height:
                raise ToolError("图块层 %s 数据长度 %d != %dx%d"
                                % (layer.name, len(layer.data), width, height))
            off_x, off_y = self._layer_offset_cells(layer)
            is_ground = layer is ground
            for index, raw_gid in enumerate(layer.data):
                if not raw_gid:
                    continue
                gid = raw_gid
                if gid & GID_FLAGS:
                    self._flip_count += 1
                    self.warn("图层 %s 含翻转/旋转图块；项目格式不支持翻转，已忽略标志位"
                              % layer.name)
                    gid &= GID_MASK
                if not gid:
                    continue
                local_x = index % width + off_x
                local_y = index // width + off_y
                if not (0 <= local_x < self.map.width and 0 <= local_y < self.map.height):
                    raise ToolError("图层 %s 的图块 (%d,%d) 超出地图范围"
                                    % (layer.name, local_x, local_y))
                if is_ground:
                    info, sprite = self._sprite_for_tile(gid, layer.name, floor=True)
                    if info.solid:
                        self.warn("地面图块 %s/%d 标记了 solid，地面层忽略该标记"
                                  % (gid, self._tileset_for_gid(gid).name))
                    if sprite != self.default_floor:
                        self.floor_cells[(local_x, local_y)] = sprite
                else:
                    info, sprite = self._sprite_for_tile(gid, layer.name, floor=False)
                    solid = layer_solid or info.solid
                    self._add_object(local_x, local_y, z, mode, sprite, solid,
                                     info.ox, info.oy)

    def _read_object_layer(self, layer):
        mode, z, layer_solid = self._layer_options(layer, "furniture")
        self._layer_offset_cells(layer)
        for obj in layer.objects:
            if not _as_bool(obj.get("visible"), True):
                continue
            if obj.get("template"):
                self.warn("跳过 Tiled 模板对象（template）：%s" % obj.get("template"))
                self._skipped_objects += 1
                continue
            props = obj.get("properties") or {}
            class_name = _string(obj.get("class") or obj.get("type"), "")
            room_kind = _string(props.get("kind"), "").lower()
            if class_name.lower() in ROOM_CLASSES or room_kind in ROOM_CLASSES:
                self._read_room(obj, props, layer, z)
                continue
            if obj.get("shape"):
                self.warn("跳过不支持的 Tiled 对象形状 %s（id=%s）：%s"
                          % (obj.get("shape"), obj.get("id"), obj.get("name") or ""))
                self._skipped_objects += 1
                continue
            self._read_object(obj, props, class_name, layer, mode, z, layer_solid)
        return

    def _read_object(self, obj, props, class_name, layer, layer_mode, layer_z,
                     layer_solid):
        x = _required_float(obj.get("x"), "对象 x") + layer.offset_x
        y = _required_float(obj.get("y"), "对象 y") + layer.offset_y
        width = _required_float(obj.get("width"), "对象 width", 0.0)
        height = _required_float(obj.get("height"), "对象 height", 0.0)
        raw_gid = _as_int(obj.get("gid"), 0)
        gid = raw_gid
        if gid & GID_FLAGS:
            self._flip_count += 1
            self.warn("对象层 %s 含翻转/旋转图块；项目格式不支持翻转，已忽略标志位"
                      % layer.name)
            gid &= GID_MASK
        info = None
        if gid:
            info, tile_sprite = self._sprite_for_tile(gid, "对象层 " + layer.name, floor=False)
            sprite = _string(props.get("sprite"), "") or tile_sprite
        else:
            sprite = _string(props.get("sprite"), "") or class_name
            if not sprite:
                self.warn("跳过没有 sprite/gid 的对象（id=%s）：%s"
                          % (obj.get("id"), obj.get("name") or ""))
                self._skipped_objects += 1
                return
        if info is None:
            info = TileInfo()
        mode = _string(props.get("zlayer") or props.get("layer")
                       or props.get("projectLayer"), "").lower()
        if mode:
            if mode not in PROJECT_LAYERS:
                raise ToolError("对象 %s 的 layer 非法：%s" % (obj.get("id"), mode))
        else:
            mode = layer_mode
        z = _int_property(props, "z", layer_z, "对象 %s 的 z" % obj.get("id"))
        if z < 0 or z >= MAX_Z:
            raise ToolError("对象 %s 的 z=%d 越界（0..%d）"
                            % (obj.get("id"), z, MAX_Z - 1))
        if mode == "floor" and z == 0:
            raise ToolError("对象 %s 使用 floor 层但 z=0；地面请使用图块层"
                            % obj.get("id"))
        if "solid" in props:
            solid = _as_bool(props.get("solid"), False)
        else:
            solid = layer_solid or info.solid
        ox = _int_property(props, "ox", info.ox, "对象 %s 的 ox" % obj.get("id"))
        oy = _int_property(props, "oy", info.oy, "对象 %s 的 oy" % obj.get("id"))
        if abs(ox) > 4096 or abs(oy) > 4096:
            raise ToolError("对象 %s 的 ox/oy 超出 -4096..4096" % obj.get("id"))
        cell_x = _int_property(props, "cellX", None, "对象 %s 的 cellX" % obj.get("id"))
        cell_y = _int_property(props, "cellY", None, "对象 %s 的 cellY" % obj.get("id"))
        if cell_x is None or cell_y is None:
            object_height = height if height > 0 else self.map.tile_height
            top_y = y - object_height if gid else y
            cell_x = int(math.floor(x / float(self.map.tile_width) + 1e-6))
            cell_y = int(math.floor(top_y / float(self.map.tile_height) + 1e-6))
        fill = _as_bool(props.get("fill"), False)
        default_w = max(1, int(round(width / self.map.tile_width))) if width > 0 else 1
        default_h = max(1, int(round(height / self.map.tile_height))) if height > 0 else 1
        cell_w = _int_property(props, "cellW", default_w, "对象 %s 的 cellW" % obj.get("id"))
        cell_h = _int_property(props, "cellH", default_h, "对象 %s 的 cellH" % obj.get("id"))
        if cell_w < 1 or cell_h < 1:
            raise ToolError("对象 %s 的 cellW/cellH 必须大于 0" % obj.get("id"))
        if not fill:
            cell_w = cell_h = 1
        rotation = _as_float(obj.get("rotation"), 0.0)
        if abs(rotation) > 0.001:
            self.warn("对象 %s 的 rotation=%g 未支持，已按无旋转导入"
                      % (obj.get("id"), rotation))
        for yy in range(cell_y, cell_y + cell_h):
            for xx in range(cell_x, cell_x + cell_w):
                if not (0 <= xx < self.map.width and 0 <= yy < self.map.height):
                    self.warn("跳过越界对象 %s 的格子 (%d,%d)"
                              % (obj.get("id"), xx, yy))
                    self._skipped_objects += 1
                    continue
                self._add_object(xx, yy, z, mode, sprite, solid, ox, oy)

    def _read_room(self, obj, props, layer, layer_z):
        x = _required_float(obj.get("x"), "房间 x") + layer.offset_x
        y = _required_float(obj.get("y"), "房间 y") + layer.offset_y
        width = _required_float(obj.get("width"), "房间 width", 0.0)
        height = _required_float(obj.get("height"), "房间 height", 0.0)
        cell_x = _int_property(props, "cellX", None, "房间 %s 的 cellX" % obj.get("id"))
        cell_y = _int_property(props, "cellY", None, "房间 %s 的 cellY" % obj.get("id"))
        cell_w = _int_property(props, "cellW", None, "房间 %s 的 cellW" % obj.get("id"))
        cell_h = _int_property(props, "cellH", None, "房间 %s 的 cellH" % obj.get("id"))
        if cell_x is None or cell_y is None or cell_w is None or cell_h is None:
            for value, label in ((x, "x"), (y, "y"), (width, "width"), (height, "height")):
                if value is None:
                    raise ToolError("房间 %s 缺少 %s" % (obj.get("id"), label))
            col = x / float(self.map.tile_width)
            row = y / float(self.map.tile_height)
            cols = width / float(self.map.tile_width)
            rows = height / float(self.map.tile_height)
            if any(abs(v - round(v)) > 1e-4 for v in (col, row, cols, rows)):
                raise ToolError("房间 %s 未对齐 tile 网格（x/y/width/height 需为 tile 的整数倍）"
                                % obj.get("id"))
            cell_x, cell_y = int(round(col)), int(round(row))
            cell_w, cell_h = int(round(cols)), int(round(rows))
        cell_w = 1 if cell_w is None else cell_w
        cell_h = 1 if cell_h is None else cell_h
        if cell_w <= 0 or cell_h <= 0:
            raise ToolError("房间 %s 的 cellW/cellH 必须大于 0" % obj.get("id"))
        z = _int_property(props, "z", layer_z, "房间 %s 的 z" % obj.get("id"))
        if z < 0 or z >= MAX_Z:
            raise ToolError("房间 %s 的 z=%d 越界（0..%d）"
                            % (obj.get("id"), z, MAX_Z - 1))
        raw_id = props.get("id")
        room_id = _as_int(raw_id, None)
        if "id" in props and room_id is None:
            raise ToolError("房间 %s 的 id 必须是整数：%r" % (obj.get("id"), raw_id))
        if "id" in props and (room_id <= 0 or room_id > MAX_ROOM_ID):
            raise ToolError("房间 %s 的 id=%d 超出 1..%d"
                            % (obj.get("id"), room_id, MAX_ROOM_ID))
        if room_id is None:
            room_id = _as_int(obj.get("id"), None)
            if room_id is not None and not (1 <= room_id <= MAX_ROOM_ID):
                self.warn("房间 %s 的 Tiled object id=%d 超出 1..%d，改用自动 id"
                          % (obj.get("id"), room_id, MAX_ROOM_ID))
                room_id = None
        if room_id is None or room_id <= 0:
            while self._next_auto_room_id in self._room_ids:
                self._next_auto_room_id += 1
            if self._next_auto_room_id > MAX_ROOM_ID:
                raise ToolError("房间数量超过 %d" % MAX_ROOM_ID)
            room_id = self._next_auto_room_id
            self._next_auto_room_id += 1
        if room_id in self._room_ids:
            raise ToolError("房间 id 重复：%d（对象 %s）" % (room_id, obj.get("id")))
        if cell_x < 0 or cell_y < 0 or cell_x + cell_w > self.map.width \
                or cell_y + cell_h > self.map.height:
            self.warn("跳过越界房间 %s：(%d,%d) %dx%d"
                      % (obj.get("id"), cell_x, cell_y, cell_w, cell_h))
            self._skipped_objects += 1
            return
        name = _string(props.get("name"), "") or _string(obj.get("name"), "") \
            or "room_%d" % room_id
        self._room_ids.add(room_id)
        self.rooms.append((cell_x, cell_y, cell_w, cell_h, room_id, name, z))

    def _add_object(self, x, y, z, layer, sprite, solid, ox, oy):
        self.objects.append((x, y, z, layer, sprite, bool(solid), ox, oy))

    def _check_room_ids(self):
        if not self.rooms:
            return
        seen = set()
        for room in self.rooms:
            if room[4] in seen:
                raise ToolError("房间 id 重复：%d" % room[4])
            seen.add(room[4])

    # ------------------------------------------------------------------ 输出

    def _legend_char(self, sprite):
        if sprite in self._char_by_sprite:
            return self._char_by_sprite[sprite]
        for char in LEGEND_CHARS:
            if char not in self.legend:
                self.legend[char] = sprite
                self._char_by_sprite[sprite] = char
                return char
        raise ToolError("地面种类超过 %d 种，legend 单字符不足" % len(LEGEND_CHARS))

    def _build_legend(self):
        self.legend = {}
        self._char_by_sprite = {}
        default_char = None
        for char in LEGEND_CHARS:
            if char not in self.legend:
                default_char = char
                break
        self.legend[default_char] = self.default_floor
        self._char_by_sprite[self.default_floor] = default_char
        for sprite in sorted(set(self.floor_cells.values())):
            self._legend_char(sprite)
        return self.legend

    def _write(self, out_dir, force):
        out = Path(out_dir)
        out_resolved = out.resolve()
        source = self.source.resolve()
        for protected in (out_resolved / "map.json", out_resolved / "chunks"):
            try:
                source.relative_to(protected)
            except ValueError:
                continue
            raise ToolError("源地图位于输出覆盖范围（%s），请换一个 --out：%s"
                            % (protected, source))
        if out.exists():
            if not out.is_dir():
                raise ToolError("输出路径不是目录：%s" % out)
            entries = list(out.iterdir())
            if entries:
                if not force:
                    raise ToolError("输出目录非空：%s（如确认覆盖，加 --force）" % out)
                allowed = {"map.json", "chunks"}
                unexpected = [entry.name for entry in entries if entry.name not in allowed]
                if unexpected:
                    raise ToolError("输出目录含非地图文件，拒绝覆盖：%s"
                                    % ", ".join(sorted(unexpected)))
                for entry in entries:
                    if entry.is_dir():
                        shutil.rmtree(entry)
                    else:
                        entry.unlink()
        out.mkdir(parents=True, exist_ok=True)
        chunks_dir = out / "chunks"
        chunks_dir.mkdir(parents=True, exist_ok=True)
        legend = self._build_legend()
        chunks_x = (self.map.width + CHUNK - 1) // CHUNK
        chunks_y = (self.map.height + CHUNK - 1) // CHUNK
        objects_by_chunk = {}
        for obj in self.objects:
            x, y = obj[0], obj[1]
            key = (x // CHUNK, y // CHUNK)
            objects_by_chunk.setdefault(key, []).append(obj)
        rooms_by_chunk = {}
        for room in self.rooms:
            x, y, w, h = room[:4]
            cx0, cy0 = x // CHUNK, y // CHUNK
            cx1, cy1 = (x + w - 1) // CHUNK, (y + h - 1) // CHUNK
            for cy in range(cy0, cy1 + 1):
                for cx in range(cx0, cx1 + 1):
                    rooms_by_chunk.setdefault((cx, cy), []).append(room)
        written = 0
        for cy in range(chunks_y):
            for cx in range(chunks_x):
                x0, y0 = cx * CHUNK, cy * CHUNK
                floor_rows = []
                has_floor = False
                for ly in range(CHUNK):
                    row = []
                    for lx in range(CHUNK):
                        sprite = self.floor_cells.get((x0 + lx, y0 + ly))
                        if sprite is None:
                            sprite = self.default_floor
                        else:
                            has_floor = True
                        row.append(self._char_by_sprite[sprite])
                    floor_rows.append("".join(row))
                objs = sorted(
                    (obj for obj in objects_by_chunk.get((cx, cy), [])
                     if x0 <= obj[0] < x0 + CHUNK and y0 <= obj[1] < y0 + CHUNK),
                    key=lambda obj: (obj[1], obj[0], obj[2], obj[3], obj[4], obj[6], obj[7]))
                rooms = []
                for room in sorted(rooms_by_chunk.get((cx, cy), []), key=lambda r: r[4]):
                    rx, ry, rw, rh, rid, rname, rz = room
                    ix0, iy0 = max(rx, x0), max(ry, y0)
                    ix1, iy1 = min(rx + rw, x0 + CHUNK), min(ry + rh, y0 + CHUNK)
                    if ix0 >= ix1 or iy0 >= iy1:
                        continue
                    entry = {"x": ix0 - x0, "y": iy0 - y0,
                             "w": ix1 - ix0, "h": iy1 - iy0,
                             "id": rid, "name": rname}
                    if rz:
                        entry["z"] = rz
                    rooms.append(entry)
                if not has_floor and not objs and not rooms:
                    continue
                payload = {"cx": cx, "cy": cy}
                if has_floor:
                    payload["floors"] = floor_rows
                if objs:
                    payload["objects"] = [self._object_payload(obj, x0, y0) for obj in objs]
                if rooms:
                    payload["rooms"] = rooms
                cell_dir = chunks_dir / ("c%d_%d" % (cx // CHUNKS_PER_CELL,
                                                     cy // CHUNKS_PER_CELL))
                cell_dir.mkdir(parents=True, exist_ok=True)
                path = cell_dir / ("chunk_%d_%d.json" % (cx, cy))
                self._write_json(path, payload)
                written += 1
        meta = {
            "format": FORMAT,
            "name": self.name,
            "generator": {
                "tool": "tools/tiled_import.py",
                "version": 1,
                "source": self._source_label(),
                "format": self.map.fmt,
            },
            "bounds": {"chunkX0": 0, "chunkY0": 0,
                       "chunksX": chunks_x, "chunksY": chunks_y},
            "terrain": {"defaultFloor": self.default_floor},
            "legend": legend,
        }
        self._write_json(out / "map.json", meta)
        self.written_chunks = written
        return written

    @staticmethod
    def _object_payload(obj, x0, y0):
        x, y, z, layer, sprite, solid, ox, oy = obj
        entry = {"x": x - x0, "y": y - y0, "z": z, "layer": layer, "sprite": sprite}
        if solid:
            entry["solid"] = True
        if ox:
            entry["ox"] = ox
        if oy:
            entry["oy"] = oy
        return entry

    @staticmethod
    def _write_json(path, payload):
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(payload, handle, ensure_ascii=False, indent=1)
            handle.write("\n")

    def _source_label(self):
        try:
            return str(self.source.relative_to(ROOT)).replace(os.sep, "/")
        except ValueError:
            return self.source.name


def import_map(source, out_dir, *, name=None, default_floor=DEFAULT_FLOOR, force=False,
               allow_unmapped=False, include_hidden=False):
    importer = TiledImporter(source, name=name, default_floor=default_floor,
                             allow_unmapped=allow_unmapped,
                             include_hidden=include_hidden)
    result = importer.run(out_dir, force=force)
    result["writtenChunks"] = importer.written_chunks
    result["legend"] = importer.legend
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Tiled (TMX/TSX/TMJ/TSJ) -> ZomboidLike map importer")
    parser.add_argument("source", help="Tiled 地图文件（.tmx/.tmj/.json）")
    parser.add_argument("--out", default="", help="输出地图目录（缺省 assets/maps/<name>）")
    parser.add_argument("--name", default="", help="地图名（缺省输入文件名）")
    parser.add_argument("--default-floor", default=DEFAULT_FLOOR,
                        help="未放置地面时的默认逻辑名（缺省 %s）" % DEFAULT_FLOOR)
    parser.add_argument("--force", action="store_true",
                        help="覆盖输出目录中已有的 map.json/chunks")
    parser.add_argument("--allow-unmapped", action="store_true",
                        help="未配置 sprite 的图块使用 tile.<tileset>.<id> 占位逻辑名")
    parser.add_argument("--include-hidden", action="store_true",
                        help="也导入 Tiled 中隐藏的图层")
    args = parser.parse_args(argv)
    source = Path(args.source)
    name = args.name or source.stem
    if not name or any(ch in name for ch in ("/", "\\")):
        print("错误：--name 不能为空或包含路径分隔符")
        return 2
    out = Path(args.out) if args.out else ROOT / "assets" / "maps" / name
    try:
        result = import_map(source, out, name=name, default_floor=args.default_floor,
                            force=args.force, allow_unmapped=args.allow_unmapped,
                            include_hidden=args.include_hidden)
    except ToolError as exc:
        print("错误：%s" % exc)
        return 1
    print("map    : %s" % result["out"])
    print("size   : %dx%d squares (%dx%d chunks)"
          % (result["width"], result["height"], result["chunksX"], result["chunksY"]))
    print("content: %d objects | %d rooms | %d floor overrides"
          % (result["objects"], result["rooms"], result["floorOverrides"]))
    print("chunks : %d written" % result["writtenChunks"])
    print("legend : %s" % ", ".join("%s=%s" % item for item in sorted(result["legend"].items())))
    if result["flips"]:
        print("flips  : %d（标志位已忽略）" % result["flips"])
    if result["unmapped"]:
        print("unmapped: %d（已使用占位逻辑名）" % result["unmapped"])
    if result["skippedObjects"]:
        print("skipped: %d objects" % result["skippedObjects"])
    for warning in result["warnings"]:
        print("warning: %s" % warning)
    return 0


if __name__ == "__main__":
    sys.exit(main())
