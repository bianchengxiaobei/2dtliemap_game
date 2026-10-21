# -*- coding: utf-8 -*-
"""Tiled 导入器测试：JSON/TMJ（内嵌/外部 tileset）、TMX + 外部 TSX。"""

import base64
import json
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

import tiled_import  # noqa: E402


class TiledImportTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def _json_map(self):
        width, height = 25, 12
        ground = [1] * (width * height)
        ground[0] = 2
        walls = [0] * (width * height)
        walls[1 * width + 1] = 3
        walls[1 * width + 9] = 3
        doc = {
            "type": "map",
            "version": "1.10",
            "orientation": "orthogonal",
            "renderorder": "right-down",
            "width": width,
            "height": height,
            "tilewidth": 16,
            "tileheight": 16,
            "infinite": False,
            "layers": [
                {
                    "type": "tilelayer", "name": "Ground", "width": width,
                    "height": height, "data": ground,
                },
                {
                    "type": "tilelayer", "name": "Walls", "width": width,
                    "height": height, "data": walls,
                },
                {
                    "type": "objectgroup", "name": "Props", "objects": [
                        {
                            "id": 7, "name": "tree", "x": 32, "y": 32,
                            "width": 16, "height": 16, "class": "object.tree",
                            "properties": [
                                {"name": "solid", "type": "bool", "value": True},
                            ],
                        },
                        {
                            "id": 8, "name": "Room A", "x": 144, "y": 0,
                            "width": 48, "height": 32, "class": "room",
                            "properties": [
                                {"name": "id", "type": "int", "value": 42},
                                {"name": "name", "type": "string", "value": "Room A"},
                            ],
                        },
                    ],
                },
            ],
            "tilesets": [{
                "firstgid": 1, "name": "terrain", "tilewidth": 16,
                "tileheight": 16, "tilecount": 3, "columns": 3,
                "tiles": [
                    {"id": 0, "class": "tile.floor.grass"},
                    {"id": 1, "class": "tile.floor.wood"},
                    {
                        "id": 2, "class": "wall.brick",
                        "properties": [{"name": "solid", "type": "bool", "value": True}],
                    },
                ],
            }],
        }
        path = self.root / "demo.tmj"
        path.write_text(json.dumps(doc), encoding="utf-8")
        return path

    def test_json_embedded_tileset_and_sparse_chunks(self):
        source = self._json_map()
        out = self.root / "out"
        result = tiled_import.import_map(source, out, name="tiled_demo")
        self.assertEqual(result["width"], 25)
        self.assertEqual(result["height"], 12)
        self.assertEqual(result["chunksX"], 3)
        self.assertEqual(result["chunksY"], 2)
        self.assertEqual(result["rooms"], 1)
        self.assertEqual(result["objects"], 3)
        self.assertEqual(result["floorOverrides"], 1)

        meta = json.loads((out / "map.json").read_text(encoding="utf-8"))
        self.assertEqual(meta["format"], "zomboidlike-map-v1")
        self.assertEqual(meta["name"], "tiled_demo")
        self.assertEqual(meta["bounds"], {
            "chunkX0": 0, "chunkY0": 0, "chunksX": 3, "chunksY": 2,
        })
        self.assertEqual(meta["legend"]["g"], "tile.floor.grass")
        self.assertEqual(meta["legend"]["d"], "tile.floor.wood")

        chunk0 = json.loads(
            (out / "chunks" / "c0_0" / "chunk_0_0.json").read_text(encoding="utf-8"))
        self.assertEqual(chunk0["cx"], 0)
        self.assertEqual(chunk0["cy"], 0)
        self.assertEqual(len(chunk0["floors"]), 10)
        self.assertTrue(all(len(row) == 10 for row in chunk0["floors"]))
        self.assertEqual(chunk0["floors"][0][0], "d")
        self.assertEqual(chunk0["floors"][1][0], "g")
        self.assertEqual(
            [(obj["x"], obj["y"], obj["layer"], obj["sprite"], obj.get("solid"))
             for obj in chunk0["objects"]],
            [
                (1, 1, "wall", "wall.brick", True),
                (9, 1, "wall", "wall.brick", True),
                (2, 2, "furniture", "object.tree", True),
            ])
        self.assertEqual(chunk0["rooms"], [
            {"x": 9, "y": 0, "w": 1, "h": 2, "id": 42, "name": "Room A"},
        ])

        chunk1 = json.loads(
            (out / "chunks" / "c0_0" / "chunk_1_0.json").read_text(encoding="utf-8"))
        self.assertNotIn("floors", chunk1)
        self.assertEqual(chunk1["rooms"], [
            {"x": 0, "y": 0, "w": 2, "h": 2, "id": 42, "name": "Room A"},
        ])
        self.assertFalse((out / "chunks" / "c0_0" / "chunk_2_0.json").exists())
        self.assertFalse((out / "chunks" / "c0_0" / "chunk_0_1.json").exists())

    def test_xml_external_tsx_and_base64_zlib(self):
        tsx = """<?xml version="1.0" encoding="UTF-8"?>
<tileset version="1.10" tiledversion="1.11.0" name="terrain"
         tilewidth="16" tileheight="16" tilecount="3" columns="3">
  <tile id="0" type="tile.floor.grass"/>
  <tile id="1" type="tile.floor.wood"/>
  <tile id="2" type="wall.brick">
    <properties>
      <property name="solid" type="bool" value="true"/>
    </properties>
  </tile>
</tileset>
"""
        (self.root / "terrain.tsx").write_text(tsx, encoding="utf-8")
        ground = [1] * 100
        raw = struct.pack("<100I", *ground)
        encoded = base64.b64encode(zlib.compress(raw)).decode("ascii")
        tmx = """<?xml version="1.0" encoding="UTF-8"?>
<map version="1.10" tiledversion="1.11.0" orientation="orthogonal"
     renderorder="right-down" width="10" height="10" tilewidth="16"
     tileheight="16" infinite="0">
  <tileset firstgid="1" source="terrain.tsx"/>
  <layer name="Ground" width="10" height="10">
    <data encoding="base64" compression="zlib">%s</data>
  </layer>
  <objectgroup name="Walls">
    <object id="7" x="16" y="32" width="16" height="16">
      <properties>
        <property name="sprite" value="wall.brick"/>
        <property name="solid" type="bool" value="true"/>
      </properties>
    </object>
  </objectgroup>
</map>
""" % encoded
        source = self.root / "demo.tmx"
        source.write_text(tmx, encoding="utf-8")
        out = self.root / "xml-out"
        result = tiled_import.import_map(source, out)
        self.assertEqual(result["writtenChunks"], 1)
        chunk = json.loads(
            (out / "chunks" / "c0_0" / "chunk_0_0.json").read_text(encoding="utf-8"))
        self.assertEqual(chunk["objects"], [
            {"x": 1, "y": 2, "z": 0, "layer": "wall",
             "sprite": "wall.brick", "solid": True},
        ])
        meta = json.loads((out / "map.json").read_text(encoding="utf-8"))
        self.assertEqual(meta["name"], "demo")
        self.assertEqual(meta["legend"], {"g": "tile.floor.grass"})

    def test_json_external_tsj(self):
        tileset = {
            "type": "tileset", "name": "terrain", "tilewidth": 16,
            "tileheight": 16, "tiles": [
                {"id": 0, "class": "tile.floor.wood"},
            ],
        }
        (self.root / "terrain.tsj").write_text(
            json.dumps(tileset), encoding="utf-8")
        doc = {
            "type": "map", "orientation": "orthogonal", "width": 1,
            "height": 1, "tilewidth": 16, "tileheight": 16, "infinite": False,
            "layers": [{
                "type": "tilelayer", "name": "Ground", "width": 1,
                "height": 1, "data": [1],
            }],
            "tilesets": [{"firstgid": 1, "source": "terrain.tsj"}],
        }
        source = self.root / "external.tmj"
        source.write_text(json.dumps(doc), encoding="utf-8")
        out = self.root / "external-out"
        tiled_import.import_map(source, out)
        chunk = json.loads(
            (out / "chunks" / "c0_0" / "chunk_0_0.json").read_text(encoding="utf-8"))
        self.assertEqual(chunk["floors"][0][0], "d")
        meta = json.loads((out / "map.json").read_text(encoding="utf-8"))
        self.assertEqual(meta["legend"]["d"], "tile.floor.wood")

    def test_unnamed_ground_layer_and_tile_object(self):
        doc = {
            "type": "map", "orientation": "orthogonal", "width": 4,
            "height": 4, "tilewidth": 16, "tileheight": 16, "infinite": False,
            "layers": [
                {
                    "type": "tilelayer", "name": "Tile Layer 1", "width": 4,
                    "height": 4, "data": [2, 0, 0, 0] + [0] * 12,
                },
                {
                    "type": "objectgroup", "name": "Walls", "objects": [{
                        "id": 10, "name": "wall", "x": 16, "y": 32,
                        "width": 16, "height": 16, "gid": 3,
                    }],
                },
            ],
            "tilesets": [{
                "firstgid": 1, "name": "terrain", "tilewidth": 16,
                "tileheight": 16, "tiles": [
                    {"id": 0, "class": "tile.floor.grass"},
                    {"id": 1, "class": "tile.floor.wood"},
                    {"id": 2, "class": "wall.brick"},
                ],
            }],
        }
        source = self.root / "unnamed.tmj"
        source.write_text(json.dumps(doc), encoding="utf-8")
        out = self.root / "unnamed-out"
        tiled_import.import_map(source, out)
        chunk = json.loads(
            (out / "chunks" / "c0_0" / "chunk_0_0.json").read_text(encoding="utf-8"))
        self.assertEqual(chunk["floors"][0][0], "d")
        self.assertEqual(chunk["objects"], [{
            "x": 1, "y": 1, "z": 0, "layer": "wall", "sprite": "wall.brick",
        }])

    def test_missing_tile_mapping_is_an_error(self):
        doc = {
            "type": "map", "orientation": "orthogonal", "width": 1,
            "height": 1, "tilewidth": 16, "tileheight": 16, "infinite": False,
            "layers": [{
                "type": "tilelayer", "name": "Ground", "width": 1,
                "height": 1, "data": [1],
            }],
            "tilesets": [{
                "firstgid": 1, "name": "terrain", "tilewidth": 16,
                "tileheight": 16, "tiles": [{"id": 0}],
            }],
        }
        source = self.root / "bad.tmj"
        source.write_text(json.dumps(doc), encoding="utf-8")
        with self.assertRaises(tiled_import.ToolError):
            tiled_import.import_map(source, self.root / "bad-out")

    def test_import_is_deterministic(self):
        source = self._json_map()
        first = self.root / "first"
        second = self.root / "second"
        tiled_import.import_map(source, first)
        tiled_import.import_map(source, second)
        first_files = sorted(path.relative_to(first) for path in first.rglob("*") if path.is_file())
        second_files = sorted(path.relative_to(second) for path in second.rglob("*") if path.is_file())
        self.assertEqual(first_files, second_files)
        for relative in first_files:
            self.assertEqual((first / relative).read_bytes(), (second / relative).read_bytes())


if __name__ == "__main__":
    unittest.main()
