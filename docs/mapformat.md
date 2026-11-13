# 地图格式 v1（`zomboidlike-map-v1`）

> M3-T2 定稿；运行时解析见 M3-T3 `MapLoader`；生成器 `tools/gen_map.py`。
> 设计原则：JSON 文本、可手改、可 git diff；**稀疏存储**——与程序默认地形一致的 chunk 不落盘；
> 缺失 chunk 文件 = 默认地形 + 无对象（流式加载的快速路径）。

## 1. 目录结构

```
assets/maps/<mapName>/
├─ map.json                                      # 元数据（必选）
├─ world.json                                    # 可选：zomboidlike-world-v1 区域/道路/POI
├─ templates/<templateId>.json                   # 可选：POI 模板锚点（T1 只校验存在）
└─ chunks/c<cellX>_<cellY>/chunk_<cx>_<cy>.json  # 仅"非默认" chunk（可选，可缺）
assets/maps/village_demo_m31_content.json         # T10 垂直切片内容 profile（不参与 mapHash）
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

## 7. 世界 sidecar（`world.json`，M31-T1）

`world.json` 是 M31 的区域/道路/POI 内容源，**不参与 chunk 坐标解析**；`map.json` 仍是唯一 chunk 加载入口。M31-T1 只定义数据契约与只读校验，不生成世界、不改变 chunk 加载。

### 7.1 文件

```
assets/maps/<mapName>/
├─ map.json
├─ world.json
├─ templates/<templateId>.json
└─ chunks/...
```

- `world.json` 缺失 = 旧地图，按现有 `MapLoader` 路径加载；状态为 `Missing`，不报错。
- `world.json` 存在但任一校验失败 = 启动前明确失败；不会降级成旧地图或猜测字段。
- `templates/<templateId>.json` 在 T1 只要求文件存在；T2 的模板行格式、锚点、对象/房间校验见 §7.4。

### 7.2 契约

```json
{
  "format": "zomboidlike-world-v1",
  "mapId": "village_demo_m31",
  "seed": 20261109,
  "generatorVersion": 1,
  "bounds": { "chunkX0": 0, "chunkY0": 0, "chunksX": 36, "chunksY": 36 },
  "regions": [
    {
      "id": "village_core",
      "rect": { "chunkX": 2, "chunkY": 2, "chunksW": 8, "chunksH": 8 },
      "biome": "village",
      "poiIds": ["village_haven", "clinic"]
    }
  ],
  "nodes": [
    { "id": "village_haven", "kind": "settlement", "x": 24.5, "y": 26.5 },
    { "id": "road_junction", "kind": "junction", "x": 120.0, "y": 120.0 },
    { "id": "lookout", "kind": "landmark", "x": 300.0, "y": 300.0, "isolated": true }
  ],
  "edges": [
    { "a": "village_haven", "b": "road_junction", "kind": "road", "width": 3.0, "vehicle": true }
  ],
  "pois": [
    {
      "id": "clinic",
      "template": "clinic_v1",
      "x": 92.0,
      "y": 78.0,
      "radius": 12.0,
      "threat": 2,
      "lootProfile": "poi_clinic"
    }
  ],
  "spawn": {
    "defaultZombieDensity": 0.012,
    "biomes": { "forest": 0.008, "village": 0.020 }
  }
}
```

| 字段 | 类型 | 必选 | 规则 |
|---|---|---|---|
| `format` | string | ✔ | 固定 `"zomboidlike-world-v1"`；不认识直接失败 |
| `mapId` | string | ✔ | 非空；存档 manifest 身份之一 |
| `seed` | int64 | ✔ | 生成种子；与 generatorVersion 一起决定基础地形 |
| `generatorVersion` | uint32 | ✔ | `>= 1`；生成规则变化时递增 |
| `bounds` | object | ✔ | 必须与 `map.json.bounds` 完全一致 |
| `regions` | array | ✔ | 非空；矩形不重叠且完整落在 bounds 内 |
| `nodes` | array | ✔ | 非空；`id` 唯一，`x/y` 在 world bounds 内 |
| `edges` | array | ✔ | 可为空；端点必须存在，不能自环；`width > 0` |
| `pois` | array | ✔ | 可为空；`id` 唯一，`template` 文件必须存在 |
| `spawn` | object | | 缺省全部为 0；密度必须为非负有限数 |

区域 `rect` 使用 `chunkX/chunkY/chunksW/chunksH`（chunk 单位），彼此只允许边界相接，不允许面积重叠。`regions[].poiIds` 可选，但引用的 POI 必须存在。

节点 `kind`、边 `kind` 是开放字符串（`settlement/junction/landmark`、`road/trail/bridge` 等），T1 不限制枚举。非 `isolated` 节点必须组成一个连通分量；显式 `"isolated": true` 的节点不参与连通性检查，也不能出现在任何边中。

POI 的 `template` 只允许 `[A-Za-z0-9_.-]` 文件名片段，对应 `templates/<template>.json`；`radius > 0`，且 `x ± radius`、`y ± radius` 不越出 world bounds。`threat` 可选，范围 0–5；`lootProfile` 可选。

未知字段忽略并 `warn`；必选字段缺失、类型错误、坐标/范围非法、引用缺失均为硬失败。T1 的 C++ 加载入口为 `world::LoadWorldDefinition()`，返回 `Loaded/Missing/Invalid`，旧地图不构造 `WorldDefinition`。

### 7.3 `mapHash`、身份与旧档文案

`mapHash` 是地图内容指纹，不存进 `world.json`（避免自引用），由加载器计算：

1. 递归收集地图目录下所有普通文件；
2. 按相对路径（`generic_string()`，UTF-8）字典序排序；
3. FNV-1a 64 位从偏移 `14695981039346656037` 开始；
4. 每个文件依次写入：路径长度（U32 LE）+ 路径字节 + 文件大小（U64 LE）+ 文件字节；
5. 输出 16 位小写十六进制。

它覆盖 `map.json`、`world.json`、`chunks/**.json` 与 `templates/**`；任何基础地形或模板改动都会改变指纹。它是兼容性检查，不是加密/防篡改。

存档 manifest 记录 `mapId + seed + generatorVersion + mapHash`。兼容规则：

- 无 manifest 的旧档 = `LegacyNoManifest`，继续按旧地图路径读取；
- 四项全部一致 = 兼容；
- 任一不一致 = 明确失败，文案包含旧值/当前值，并以“为避免把旧建筑/容器套到新地形，已停止加载。”结尾；不做自动迁移。

### 7.4 POI 模板（`zomboidlike-poi-template-v1`，M31-T2）

生成器 `tools/gen_world.py` 把作者/程序化的局部 POI 模板写成 `templates/<id>.json`，再按 `world.json` 的锚点盖章到 `chunks/*.json`。模板是离线内容源，运行时不需要单独加载；T1 只检查文件存在，T2 生成时校验结构。

```json
{
  "format": "zomboidlike-poi-template-v1",
  "id": "clinic_v1",
  "name": "废弃诊所",
  "w": 14,
  "h": 10,
  "anchor": { "x": 7, "y": 5 },
  "floorLegend": { "g": "tile.floor.grass", "d": "tile.floor.dirt", "w": "tile.floor.wood" },
  "floorRows": ["gggggggggggggg", "..."],
  "objectLegend": {
    "#": { "layer": "wall", "sprite": "wall.brick" },
    "D": { "layer": "wall", "sprite": "wall.door" },
    "B": { "layer": "furniture", "sprite": "object.bed" }
  },
  "objectRows": ["..............", "..."],
  "rooms": [{ "x": 2, "y": 2, "w": 10, "h": 6, "name": "poi_clinic" }]
}
```

- `w/h` 为模板局部格数；`anchor` 是模板内锚点，`world.json.pois[].x/y` 对应锚点，不要求是模板中心。
- `floorRows`/`objectRows` 必须各恰有 `h` 行，每行恰有 `w` 个字符；`objectRows` 的 `.` 为空，其他字符查 `objectLegend` 的 `{layer,sprite}`。
- `floorLegend` 必须能映射到地图 `legend`；模板可以使用 `tile.floor.grass/dirt/wood`，水面/桥不进入模板。
- `rooms` 是模板局部坐标，盖章时加上模板偏移；名称由生成器保证唯一。
- 生成器校验：行宽/字符、anchor/房间/对象越界、至少一扇门、模板足迹不与道路/村庄/其他 POI 重叠、POI 间距与半径、世界边界闭合。
- 模板与 `world.json` 一起参与 `mapHash`；修改模板后必须重新生成并让存档 manifest 失配，禁止静默套用旧存档建筑。

T2 的正式地图为 `assets/maps/village_demo_m31`：`tools/gen_world.py --name village_demo_m31 --seed 20261109 --chunks 36x36`，输出 `map.json`、`world.json`、`chunks/**.json` 与 `templates/*.json`；`mapHash` 由 `ComputeMapHash()` 计算，不写进 `world.json`。

### 7.5 存档 manifest 与区域实体桶（M31-T4）

新档在 `<save>/world/world_manifest_v1.bin` 保存 `mapId + seed + generatorVersion + mapHash`、`worldSaveVersion`、修改 chunk 坐标、区域相位、实体桶索引、区域事件状态、世界事件状态和幂等结果账本；当前版本为 `v6`，仍可读取 `v1`（事件列表为空）、`v2`（无战略状态）、`v3`（无恢复字段）、`v4`（无战略贡献账本）与 `v5`（无世界事件状态）。二进制段格式为 `ZLWM`，严格校验长度/数量/重复 id。没有该文件的存档按旧格式读取，不强制迁移。

区域实体桶写入 `<save>/world/entities_<region>_<hash>.bin`，每个文件是现有 `EntitySnapshot` 编码。玩家和无 `Position` 的世界状态（如动物世界状态、聚落状态）写入全局 `entities.bin`；车辆、尸体、动物、聚落 NPC、地面物品等按 `world.json` 区域矩形分桶。桶索引文件缺失、数量不一致或 StableId 重复会被视为损坏存档，并走既有主档/备份回退。

加载前必须比较 manifest 的地图身份：四项任一不同即停止加载并保持存档只读，不把旧墙/容器套到新地形；旧档无 manifest 继续按旧地图路径读取。

### 7.6 道路图、地形通行与 spawn 密度（M31-T5）

`WorldGraph` 不要求新增 `world.json` 字段，直接消费 T1 的 `nodes/edges/regions/spawn`：

| 查询 | 规则 |
|---|---|
| `PlanRoute(start, goal, vehicleOnly)` | 起终点吸附最近非 isolated 节点；Dijkstra 使用边长；`vehicleOnly` 跳过 `vehicle:false`；返回节点序列、道路距离、接地距离和是否经过 `bridge` |
| `OnRoad/OnBridge(x,y)` | 点到边线段的距离 ≤ `width/2 + 0.5`；桥梁取 `kind:"bridge"` |
| `TerrainKind` | `water → blocked → bridge → road → dirt → forest → paved → grass` 优先级；水面不可走/不可载具，桥梁与道路可走，桥面实体仍按 `blocked` 拒绝，`vehicle:false` 边对载具不可通行 |
| `ZombieDensityAt` | 优先 `spawn.biomes[region.biome]`，缺失/区域外回退 `spawn.defaultZombieDensity`；`DensityScaleAt = density/default` 并夹取 `[0,4]` |

道路图只承担长距离走廊；起点/终点到最近道路节点的接地、以及最终室内/障碍绕行继续由 `PathfindingService` 的局部 A* 完成。载具仍可在草地/森林低速通行，水面与实体阻挡始终拒绝；桥面 `tile.floor.wood` 不单独写通行字段，由 `kind:"bridge"` 边派生。`spawn` 的 biome 值用于 `SpawnLodSystem` 目标缩放，远端仍只保存虚拟尸群，不实体化全图。

### 7.7 区域事件状态（M31-T6）

`RegionEventDirector` 不新增 `world.json` 字段：每个 `regions[]` 项每天生成 1 个确定性事件（`seed + region.id + day`），站点优先使用 `poiIds` 的首个 POI，否则使用区域中心，并夹取在区域与世界边界内。事件类型为 `Horde/Caravan/SupplyDrop/Outbreak`，分别对应尸潮、商队/巡逻、空投和感染爆发。

| 状态 | 推进规则 |
|---|---|
| `Scheduled` | 区域 `Active` 且事件点到焦点距离 ≤ `world.regionEvents.contentRadius`（默认 24 格）才进入 `Active` 并生成内容 |
| `Active` | 生成一次内容并推进 `remainingSeconds`；生成后再次 Update 不重复刷 |
| `Frozen` | 区域 `Ambient/Frozen` 或事件点超出近距半径时冻结计时；不后台刷怪，HUD 仍可显示状态/方向 |
| `Resolved` | 计时到 0；内容作为普通世界实体保留，不重复生成 |

`world_manifest_v1.bin` v2–v5 的事件记录字段：`id/regionId/kind/status/day/x/y/remainingSeconds/targetCount/spawnedCount/contentSpawned`；读档时校验事件 id、region 是否存在、kind/status 范围、坐标/计时有限性和数量上限；已 `contentSpawned` 的事件恢复后不再生成。测试覆盖区域冻结/恢复、远端不生成、manifest 往返和未知区域拒绝；T8 起服务器 `SaveWorld` 也写入同一 manifest/事件并跨重启恢复。HUD 复用 `HudState.eventGuide` 显示“区域事件 · 类型 · 状态 · 目标方向/距离”。

### 7.8 世界地图与导航（M31-T7）

世界地图不新增 `world.json` 字段；`WorldMap` 直接消费 T1 的 `regions/nodes/edges/pois` 与 T5 的 `WorldGraph`：区域/道路/路标来自 sidecar，河流带由 `kind:"bridge"` 边的两个端点派生，POI 名称/威胁来自运行时 `pois.defs`。玩家/事件/任务目标为运行时快照，不写入地图母文件。`world.map.discoverRadius` 默认 36 格，只影响当前会话发现状态；`M` 打开地图并暂停单机时间，点击 POI 用 `WorldGraph::PlanRoute` 生成驾车路线，路线只显示/HUD 提示，不传送或自动行驶。旧地图无 `world.json` 时地图不可用并提示。

### 7.9 联机兴趣与服务器世界存档（M31-T8）

服务器不新增地图文件字段。`ChunkManager::UpdateFocuses()` 对每个在线玩家（以及尚未生成时的档案位置/复活默认点）生成 chunk 兴趣窗口并取并集；任一焦点窗口内的 chunk 保持加载，离开全部窗口才卸载。`RegionStreaming` 以同一并集计算 `Active/Ambient/Frozen`，`RegionEventDirector` 只在任一近距焦点满足内容半径时激活事件。结构/墓碑 `ZLCD` 按会话位置与 `net.structureInterestRadius`（默认 48 格）过滤；实体复制继续使用既有 AOI，不修改 `SessionProtocol` 或命令包版本。

服务器 `SaveWorld` 使用与单机相同的 `world_manifest_v1.bin` v6：地图身份、区域相位、chunk 索引、区域实体桶、区域事件状态、聚合战略贡献账本、世界事件状态和世界事件结果账本；读取时沿用 T4 身份校验、桶合并和 T6 事件恢复。旧地图无 `world.json` 时保持原 `entities.bin`/chunk 布局。当前启动仍合并全部存档实体桶，未实现按区域实体懒加载。

M32-T25 起 manifest v5 追加 `strategicContributions`（StableId/区域 id/点数，最多 1024 条）；只持久化聚合贡献，目标级守城伤害明细仍在运行时。M33-F2-T5 起 manifest v6 追加 `worldEvents`（世界事件快照：id/defId/name/kind/region/坐标/状态/阶段/阶段进度/超时分钟/选项/事件链/`contentSpawned` 等，最多 128 条）和 `worldEventOutcomes`（事件 id+玩家 StableId 的幂等账本键，最多 512 条）；v5 读取时两者为空。事件内容实体仍通过既有区域实体桶保存，读档后按确定性 StableId 幂等重放，不重复生成。T9 增加只读的 `SaveManager::MeasureStorage()`：递归统计存档文件数/总字节，并区分 `world/chunk_*.bin`、`world/entities_*.bin`、`world/world_manifest_v1.bin` 与 `entities.bin`；后台写由 `SaveService::ChunkWriteStats` 记录完成/失败/重试和最大延迟。chunk 仍写 `world/chunk_<cx>_<cy>.bin`（一文件一块）；1024 个脏 chunk 实测 1027 文件/280,649 bytes，低于 1296 文件目标，因此当前不启用按 cell 打包；该统计只读，不改变存档格式。

### 7.10 垂直切片内容 profile 与地图选择（M31-T10）

M31 的运行时容器/守军/任务不能塞进 `world.json` 的地形契约，T10 新增伴随文件 `assets/maps/village_demo_m31_content.json`（`format:"zomboidlike-world-content-v1"`，含 `mapId`、`pois`、`missions`）。它**位于地图目录之外**，因此不改变 `mapHash`；内容变化由客户端/服务器内容指纹覆盖。文件复用现有 `config/game.json` 的 `pois`/`missions` 结构，由 `GameApp`/`ServerHost` 通过临时 `core::Config` 加载，mod 的 POI/任务仍在其后合并。

当前内容：
- 4 个作者/运行时 POI：`clinic`（诊所）、`farmstead`（农场）、`river_hut`（河畔补给点，资源/危险区）、`checkpoint`（检查站）；容器表复用 `poi_clinic_*`/`poi_farm_*`/`poi_military_*`。
- 5 段主线 `m31_clinic_supply → m31_farm_supply → m31_river_cache → m31_checkpoint_armory → m31_return`；另有 `farm_tools` 支线（解锁雨水收集器）和 4 条村庄 `settlement_*` 支线。
- 主线任务在 `MissionFocusPriority` 中固定最高优先级；M31 下旧电台/撤离 Objective 的 HUD/推进被隔离，任务链成为唯一主目标。

选择规则（`WorldSelection`）：
| 配置 | 行为 |
|---|---|
| `world.m31.enabled=true` + 单机（`GameApp`） | 默认 `world.m31.mapDir` + `contentFile` |
| `world.m31.enabled=true` + 专用服务器 | 默认仍用 `world.mapDir`（旧图）；显式 `--world.mapDir=...village_demo_m31` 或 `world.m31.serverEnabled=true` 才用 M31 |
| `world.m31.enabled=false` | 使用 `world.m31.legacyMapDir`（默认 `assets/maps/village_demo`）+ 基础 `config/game.json` 内容 |
| `ServerHost::Options.mapDir/contentFile` | 内置主机/测试可显式覆盖；空值走上述自动规则 |

`ServerHost` 和 `GameApp` 只把选择结果写入 `WorldSimulation::Options.mapDir`，不修改配置文件；因此设置保存不会把 M31 内容写回 `game.json`。旧 `village_demo` 存档不会自动迁移；默认 M31 下会因 mapId/manifest 不匹配而拒绝写入，需以 `--world.m31.enabled=false` 启动后读取。M31 自身存档继续按 `mapId + seed + generatorVersion + mapHash` 校验，内容 profile 不参与 mapHash。

### 7.11 战略状态（M32-T1）

`assets/strategic.json`（`format:"zomboidlike-strategic-v1"`）由 `config/game.json` 的 `strategic.file` 指定；当前含 4 个区域，region id 必须存在于 M31 `world.json`。字段包括 `strategicValue/initialOwner/garrison/fortification/defense/supply/population/pressure/morale/intelLevel/coreSafe`，以及可选的 `attackRoutes`（最多 16 条，T1 只校验）。

`WorldManifest` v4/v5/v6 在每个区域 manifest 项之后保存 `strategicTickMinute`、全局四资源、战略区域摘要、恢复字段和事件日志；区域摘要字段为 `id/owner/siegePhase/intelLevel/coreSafe/garrison/fortification/population/pressure/defense/supply/morale/nextAttackMinute/lastEventMinute`，T5 追加 `facilityDamage/refugees/infectionCores/recoveryStage/recoveryReadyMinute/refugeeHost`；事件字段为 `id/regionId/kind/ownerAfter/gameMinute/people/supplies/ammo/fuel`，T5 kind 增加难民事件。v1/v2 无战略段时 `RestoreStrategicState` 按 `strategic.json` 初始化，不追溯历史；v3 恢复字段默认 `None/0/-1/空`；v4/v5 缺失/未知/重复区域、非法 owner/phase/recovery/数值或事件 id 冲突会拒绝读档；v5 追加 `strategicContributions`（StableId/区域/点数），v4 无该段时为空。事件 id 幂等，避免同一战报重复扣资源/奖励。

T1 不增加网络字段或命令；`PlayerState`/`SessionProtocol` 不变。`strategic.json` 尚未纳入 `ComputeContentHash`，联机内容一致性与客户端只读战略状态留 T7。
