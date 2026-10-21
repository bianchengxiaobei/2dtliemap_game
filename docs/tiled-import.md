# Tiled 地图导入器

> `tools/tiled_import.py` 把 Tiled 地图转换为 `zomboidlike-map-v1`（格式见
> [mapformat.md](mapformat.md)）。运行时加载器不需要任何 Tiled 依赖，导入是一次性离线步骤。

## 快速使用

```powershell
# TMX + 外部 TSX，输出到 assets/maps/village
py -3 tools/tiled_import.py path/to/village.tmx --name village

# TMJ + 内嵌/外部 TSJ，覆盖已有导入结果
py -3 tools/tiled_import.py path/to/village.tmj --name village --force

# 输出到指定目录；源文件不会被打包进地图
py -3 tools/tiled_import.py path/to/demo.tmj --out build/tiled_demo
```

默认输出 `assets/maps/<name>/`；`--force` 只清理已有的 `map.json` 和 `chunks/`，
如果输出目录含其他文件会拒绝覆盖。解析/校验失败不会写输出；`--force` 会先清理
旧地图文件再写新文件。

| 参数 | 说明 |
|---|---|
| `--name NAME` | 地图名，也是 `map.json.name` 和默认输出目录名 |
| `--out DIR` | 自定义输出目录 |
| `--default-floor NAME` | 空白地面逻辑名，缺省 `tile.floor.grass` |
| `--force` | 覆盖 `map.json`/`chunks` |
| `--allow-unmapped` | 未配置 sprite 的图块使用 `tile.<tileset>.<id>` 占位名 |
| `--include-hidden` | 导入 Tiled 隐藏图层（默认跳过） |

## 支持范围

- Tiled JSON：`.tmj` / `.json`，图块集 `.tsj` / `.json`（内嵌或 `source` 外部引用）；
- Tiled XML：`.tmx` / `.xml`，图块集 `.tsx` / `.xml`（内嵌或外部引用）；
- 有限、正交（orthogonal）地图；
- XML 图块数据：CSV、无压缩 base64、zlib、gzip；JSON 使用 Tiled 的 `data` 数组；
- 图块层、对象层、图层组（group）；
- 对象层中的矩形、图块对象（`gid`）和 `class=room` 矩形。

暂不支持：infinite/hex/isometric 地图、Tiled 图像层、点/多边形/椭圆对象、
对象模板（template）、对象旋转/图块翻转的视觉语义、透明度/染色、只用于碰撞的
无 sprite 对象、多个 `z=0` 地面层；图层组的 offset 不会累加到子图层。地面
legend 使用单字节字符，最多 62 种不同地面。

## Tiled 侧配置

### 1. 图块逻辑名

项目通过 `asset_manifest.json` 的逻辑名取资源；Tiled 的 tile id 不会直接变成
资源名。每个实际使用的 tile 至少配置一种映射：

- tile 自定义属性 `sprite`：对象层使用的逻辑名，如 `wall.brick`；
- tile 自定义属性 `floor`：地面层使用的逻辑名，如 `tile.floor.wood`；
- tile 的 `class` / 旧版 `type`：没有属性时作为逻辑名；如把 tile class 设为
  `tile.floor.grass` 或 `object.tree`；
- 图块集属性 `sprite` / `floor`：整个图块集的默认值。

地面层优先使用 `floor`，其它层优先使用 `sprite`。如果某个 tile 既没有属性也没有
class，导入器会报错并指出 GID；确认要先用占位名时可加 `--allow-unmapped`：

```
tile.<tileset_name>.<local_tile_id>
```

逻辑名最终必须存在于 `assets/asset_manifest.json`；导入器不做 manifest 校验，
避免把内容工具绑定到某个 provider。

### 2. 图层映射

图块层和对象层都可以用自定义属性控制：

| 属性 | 类型 | 说明 |
|---|---|---|
| `zlayer` | string | `floor` / `wall` / `furniture` / `item` / `mark`；优先于按名称推断 |
| `layer` / `projectLayer` | string | `zlayer` 的别名 |
| `z` | int | 0..7，缺省 0 |
| `solid` | bool | 该层所有对象默认阻挡；对象自己的 `solid` 优先 |

没有属性时按名称推断（不区分大小写）：

| 图层名 | 映射 |
|---|---|
| `Ground` / `Floor` / `Terrain` / `地面` / `地板` | `floor` |
| `Wall` / `Walls` / `墙` / `墙壁` | `wall` |
| `Furniture` / `Object(s)` / `Prop(s)` / `Decor` / `家具` / `物体` | `furniture` |
| `Item(s)` / `物品` | `item` |
| `Mark(s)` / `Marker(s)` / `标记` | `mark` |

规则：

- 地面层必须是唯一的 `z=0` 图块层；其它图块层生成对象；
- 若没有显式地面层，最底层的“未命名图块层”（例如默认的 `Tile Layer 1`）会被
  自动当作地面；名字已明确为 `Walls`/`Objects` 等的层不会被误判；
- `floor` 且 `z>0` 的图块层会生成 `layer=floor` 的楼上地面对象；
- 隐藏图层默认跳过，可用 `--include-hidden` 导入。

### 3. 对象层

对象层名称/`zlayer` 决定默认对象层，也支持层属性 `z`、`solid`。普通对象支持：

| 对象属性 | 说明 |
|---|---|
| `sprite` | 逻辑名；有 `gid` 时可省略，使用该 tile 的 `sprite` |
| `zlayer` / `layer` / `projectLayer` | 覆盖对象层 |
| `z` | 0..7，覆盖对象层 |
| `solid` | 覆盖对象层/tile 的 solid |
| `ox`, `oy` | 像素绘制偏移，-4096..4096 |
| `cellX`, `cellY` | 直接指定格坐标，跳过像素换算 |
| `cellW`, `cellH` | 与 `fill` 配合的填充尺寸 |
| `fill` | true 时按 `cellW`/`cellH` 填充多格 |

- **矩形对象**的 `x/y` 是 Tiled 的左上角；按 `x/tilewidth`、`y/tileheight` 向下取整。
- **图块对象（带 `gid`）** 的 `x/y` 是 Tiled 的底边锚点；导入器用
  `(y - height)/tileheight` 换算顶部格。
- 对象像素位置不要求严格对齐（普通对象会取整）；房间矩形则要求对齐。
- 一个多格矩形默认只生成左上角一个对象；需要铺满时设置 `fill=true`。
- 无 `gid` 且无 `sprite` 的对象会被跳过并计数；只做碰撞的 Tiled 对象无法用当前
  地图格式表达。

### 4. 房间

把对象 class（或旧版 type）设为 `room`，或设置属性 `kind=room`：

```json
{
  "id": 8,
  "name": "House A",
  "x": 32, "y": 32,
  "width": 160, "height": 96,
  "class": "room",
  "properties": [
    { "name": "id", "type": "int", "value": 42 },
    { "name": "z", "type": "int", "value": 0 }
  ]
}
```

- 像素矩形必须对齐 tile 网格；也可用 `cellX/cellY/cellW/cellH` 直接给格坐标；
- 房间 id 优先取属性 `id`，否则取 Tiled object id（需在 1..32767），否则自动分配；
  地图内必须唯一，显式 id 越界会报错；
- 跨 chunk 的房间会自动裁剪并写入每个相交 chunk；
- 房间越界会被跳过并给出 warning；对象越界同样跳过。

## 输出

```text
assets/maps/<name>/
├─ map.json
└─ chunks/c<cellX>_<cellY>/chunk_<cx>_<cy>.json
```

- `bounds` 从格坐标 `(0,0)` 开始，尺寸按 10×10 chunk 向上取整；
- 纯默认地面、无对象/房间的 chunk 不落盘（与 `tools/gen_map.py` 相同的稀疏策略）；
- 输出 JSON 为 UTF-8 无 BOM、LF、`indent=1`，同一输入重复导入结果逐字节一致；
- `map.json.generator` 记录 `tools/tiled_import.py`、源文件名和源格式（JSON/XML）。

## 测试

```powershell
py -3 tools/tests/test_tiled_import.py -v
```

测试覆盖 TMJ 内嵌/外部 tileset、TMX 外部 TSX、base64+zlib 数据、无命名地面层、
图块对象、跨 chunk 房间、稀疏输出、确定性以及未映射 tile 错误（6 个用例）。
