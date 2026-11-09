# 地图格式 v1（`zomboidlike-map-v1`）

> M3-T2 定稿；运行时解析见 M3-T3 `MapLoader`；生成器 `tools/gen_map.py`。
> 设计原则：JSON 文本、可手改、可 git diff；**稀疏存储**——与程序默认地形一致的 chunk 不落盘；
> 缺失 chunk 文件 = 默认地形 + 无对象（流式加载的快速路径）。

## 1. 目录结构

```
assets/maps/<mapName>/
├─ map.json                                      # 元数据（必选）
└─ chunks/c<cellX>_<cellY>/chunk_<cx>_<cy>.json  # 仅"非默认" chunk（可选，可缺）
```

- 文件名中的 `<cx>/<cy>` 是**全局 chunk 坐标**（允许负数，如 `chunk_-3_7.json`）；
- 目录按 cell（30×30 chunks）分组，避免万级文件挤在同一目录；
- 文件内 `cx/cy` 与文件名不一致 → 加载报错（防放错位置）；
- 地图范围由 `map.json` 的 `bounds` 声明；范围外的 chunk 一律按默认地形处理。

## 2. `map.json`

```json
{
  "format": "zomboidlike-map-v1",
  "name": "village_demo",
  "generator": { "tool": "tools/gen_map.py", "version": 1, "seed": 20261016,
                 "params": { "chunks": "12x12", "villages": 2, "roads": 2 } },
  "bounds": { "chunkX0": 0, "chunkY0": 0, "chunksX": 12, "chunksY": 12 },
  "terrain": { "defaultFloor": "tile.floor.grass" },
  "legend": { "g": "tile.floor.grass", "d": "tile.floor.dirt", "w": "tile.floor.wood" }
}
```

| 字段 | 类型 | 必选 | 说明 |
|---|---|---|---|
| `format` | string | ✔ | 固定 `"zomboidlike-map-v1"`，不认识直接报错 |
| `name` | string | ✔ | 地图名 |
| `generator` | object | | 生成器元信息（只读用途：tool/version/seed/params） |
| `bounds` | object | ✔ | `chunkX0/chunkY0/chunksX/chunksY`（chunk 单位，可负起点） |
| `terrain.defaultFloor` | string | | 默认地面逻辑名，缺省 `"tile.floor.grass"` |
| `legend` | object | ✔ | 单字符 → 地面逻辑名（chunk 文件 `floors` 字段用） |

## 3. chunk 文件（`chunk_<cx>_<cy>.json`）

```json
{
  "cx": 3,
  "cy": 5,
  "floors": [
    "gggggggggg",
    "ggggddgggg",
    "gwwwwwwggg",
    "... 共 10 行"
  ],
  "objects": [
    { "x": 1, "y": 2, "z": 0, "layer": "wall", "sprite": "wall.brick" },
    { "x": 2, "y": 3, "z": 0, "layer": "wall", "sprite": "wall.door" },
    { "x": 7, "y": 6, "z": 0, "layer": "furniture", "sprite": "object.tree" }
  ],
  "rooms": [
    { "x": 1, "y": 1, "w": 5, "h": 3, "id": 1, "name": "house_1" }
  ]
}
```

| 字段 | 类型 | 必选 | 说明 |
|---|---|---|---|
| `cx`, `cy` | int | ✔ | 全局 chunk 坐标，必须与文件名一致 |
| `floors` | string[10] | | 10 行 × 10 字符，第 `y` 行是 `floors[y]`，`floors[y][x]` 为该格地面字符；缺省 = 全默认地面 |
| `objects` | array | | 对象列表；缺省 = 空 |
| `rooms` | array | | 房间矩形列表；缺省 = 空 |

**坐标**：一律为 chunk 内局部坐标，`x ∈ [0,10)` 向西→东，`y ∈ [0,10)` 向北→南；
z 为世界层（0=地面，见 `core::kMaxZ`）。

**objects 条目**：

| 字段 | 类型 | 必选 | 说明 |
|---|---|---|---|
| `x`, `y` | int | ✔ | 局部格坐标 |
| `z` | int | | 层，缺省 0 |
| `layer` | string | ✔ | ∈ `floor` / `wall` / `furniture` / `item` / `mark`；`floor` 仅用于 **z≥1 的楼上地面**（z=0 地面由 `floors` 字段表达） |
| `sprite` | string | ✔ | 逻辑名（AssetProvider 解析，如 `wall.brick`） |
| `ox`, `oy` | int | | 附加像素偏移（微调锚点），缺省 0 |
| `solid` | bool | | **阻挡移动**（M4 起）：true → 该格生成物理静态碰撞体（M4-T3）、参与 A* passability（M4-T6）；缺省 false；必须显式布尔（其它类型该条目整体跳过） |

制作站不新增 JSON 字段：用 `furniture`/`item` 层的逻辑名约定 `object.<stationId>` 标记
（如 `object.workbench`、`object.campfire`），配方 `station: "workbench"` 会在
`crafting.stationRange` 内查找；显示名由 `crafting.stationLabels` 配置。

**rooms 条目**：矩形 `[x, x+w) × [y, y+h)`（局部坐标），加载时把覆盖格的 `RoomId` 设为 `id`；
`id` 在地图内唯一（生成器保证），`name` 是可读标签；`z` 可选（缺省 0，楼上房间为 1）。
墙/门所在格不属于房间内部。
房间矩形跨 chunk 时，会在它相交的**每个** chunk 文件中出现（`id` 相同），
且坐标**已按各 chunk 裁剪**（文件自包含；生成器负责裁剪，加载器只接受 chunk 内矩形）。

M19-F2 起 `tools/gen_map.py` 会在 `village_demo` 额外生成固定 POI：
v3 为废弃加油站/诊所/农场仓，v4（M23-F6-T1）增加圣玛丽医院与军事检查站，
共 5 个。仍使用本文格式的 `wall.brick`、`wall.door`、`object.workbench`、
`object.bed`、`object.barricade` 和 `rooms`，不新增文件字段；医院的室内隔墙
由生成器写入普通 `wall.brick`，门洞通过缺墙格表达。POI 的运行时容器、威胁、
任务和地图标记由 `config/game.json` 的 `pois` 段配置。重新生成地图后需同步
`pois.defs[].markerX/Y` 与 `containers[]` 坐标。

F6-T1 的两处固定布局（生成器 v4）：

- **圣玛丽医院**：占地 `x=40..51, y=80..88`，南门 `(46,88)`；`x=44/47` 两道室内隔墙在 `y=84` 留门洞，形成两个 ward，6 张 `object.bed`；药房/物资箱各 1 个，地图标记 `(46.5,84.5)`，威胁 2、7 名 mixed 守军。
- **军事检查站**：占地 `x=100..109, y=18..24`，南门 `(105,24)`；四角 `object.barricade`、工作台与补给箱形成入口缓冲；军械柜/补给箱各 1 个，地图标记 `(105.5,21.5)`，威胁 3、9 名 strong 守军（前 2 名为 `armored`）。

医院/检查站的容器表、守军、任务与地图标记在 `config/game.json` 的 `pois.defs`；
对应两段任务链与随机事件在 `missions.defs`，规则与奖励见 `docs/modding.md` 3.6.4/3.6.5。

## 4. 版本与兼容策略

- `format` 做加载门禁：不认识的值 → 明确报错（不猜测）；
- **未知字段**：忽略 + warn（向前兼容）；缺失可选字段用默认值；
- 坐标越界（局部坐标不在 `[0,10)`、z 超界）→ 报错并跳过该条目；
- v2（未来如需）：二进制打包/分片压缩，字段语义不变。

## 5. 与运行时结构的对应

| 格式字段 | 运行时 |
|---|---|
| 默认地面 + `floors` | `GridSquare::SetFloor`（Floor 层对象） |
| `objects[]` | `GridSquare::Layer(layer)` 里 append `SquareObject` |
| `rooms[]` | `GridSquare::SetRoomId` |
| chunk 文件缺失 | `Chunk` 保持默认地面、无对象（T3/T4 直接产出空 ChunkData） |

## 6. Tiled 导入

Tiled 地图通过 `tools/tiled_import.py` 离线转换为本格式，运行时不需要 Tiled。支持 TMX/TSX 与 TMJ/TSJ、有限正交地图、图块层/对象层/房间；详细映射、属性和限制见 [tiled-import.md](tiled-import.md)。
