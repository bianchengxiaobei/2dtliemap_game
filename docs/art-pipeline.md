# 美术规范与垂直切片

> 状态：v1（2026-10-22）。当前切片是**程序化像素稿**，用于锁定比例、锚点、色彩和
> 资源接口，不是最终手绘成品。全部资源位于 `assets/own/`，不使用 PZ 贴图。

## 1. 目标

用一套可替换的资源验证“正式美术能否无缝进入现有玩法”：

- 地面、墙、角色、僵尸、尸体、场景物件和代表性物品图标使用同一套画风；
- 保持现有运行时尺寸/锚点，替换图集不需要改渲染代码；
- 用 Tiled/图集/`asset_manifest.json` 的现有管线验收；
- 把“正式美术还缺什么”写成可执行约定，而不是只停留在口头方向。

本切片不包含：八方向角色、攻击/受击/死亡动画帧、正式 UI 皮肤、全量物品图标、
正式环境音和最终手绘细节。它们属于后续生产阶段。

## 2. 视觉方向

**关键词**：低饱和、林地生存、旧砖木建筑、清晰轮廓、轻微脏旧、暖色室内对比。

- 视角：2:1 等距（`64×32`/格），不改变现有投影；
- 光源：左上偏冷，右下阴影；墙脚和树冠底部有接触阴影；
- 轮廓：角色、僵尸和可交互物体使用 1px 深色描边，地面不描边；
- 细节：像素级噪点/裂纹/草叶，不使用抗锯齿、模糊或大块渐变；
- 可读性优先：角色头部/肩部与地面明度差 ≥ 2 级，门/工作台/篝火在 1x 缩放下可辨认。

### 2.1 基础色板

| 用途 | 主色 | 亮部 | 暗部 |
|---|---|---|---|
| 草地 | `#506B3C` | `#718C4C` | `#34472A` |
| 泥土 | `#735B3D` | `#92764F` | `#4A3928` |
| 木地板 | `#8B5D38` | `#B47B4A` | `#4E3424` |
| 砖墙 | `#7A4A39` | `#9E6450` | `#4A2B22` |
| 玩家上衣 | `#3F6078` | `#5C829B` | `#2A4354` |
| 玩家皮肤 | `#D2A17B` | `#E7BE98` | `#A97152` |
| 僵尸皮肤 | `#7C8A58` | `#9AA56C` | `#505E3C` |
| 僵尸衣物 | `#625C50` | `#7C7464` | `#403C35` |
| 血 | `#6E1F1C` | `#8E3028` | `#4A1210` |
| 描边 | `#1B1A18` | - | - |

单帧内色阶不超过 5 个；同一材质跨帧使用同一色板。新增颜色先补进本表，再进资源。

## 3. 技术规格

### 3.1 画布、锚点与像素

| 资源 | 画布 | 可见内容 | 锚点/说明 |
|---|---|---|---|
| 地面（草地/泥土/木） | `64×128` | 底部 `64×32` 菱形 | `ox=0, oy=96`；菱形严格铺满，不留 1px 缝 |
| 墙/门 | `64×224` | 底部 `64×112` | `ox=0, oy=112`；墙脚落在格中心 |
| 玩家/僵尸 | `64×128` | 约 `40×54`，脚底 `y=112` | 帧按 `dir*2+frame` 排列；见 §5 |
| 尸体 | `64×128` | 脚底线附近横躺 | 不挂 `AnimState`/`Facing`，按 StableId 选变体 |
| 树 | `192×256` | 树干底部中心约 `y=244` | 树冠可高于格，使用全局画家序 |
| 草丛/工作台/篝火 | `64×128` | 格中心带，底部约 `y=112` | 与角色共用地面锚点 |
| 物品图标 | `64×128` | 图案围绕 `(32,104)` | 地面物品和手持图标共用 |
| `ui.pixel` | `1×1` | 纯白 | UI 色块/动作条 |

- 所有帧使用 RGBA，透明背景；不得把“透明黑”当描边；
- 地面菱形必须复用 `tools/gen_placeholder_atlas.py` 的精确菱形遮罩算法，
  避免相邻块出现缝隙；
- 高物体允许超过画布顶部（树/墙），但可见内容必须落在画布内；
- 资源名、帧名只用小写 ASCII、数字、下划线，不带空格/中文。

### 3.2 图层与 z

沿用 `docs/mapformat.md`：

- `z=0..7`；`floor` 是地面层，`wall`/`furniture`/`item`/`mark` 是对象层；
- 地面走 stage 0 批，其它对象和实体走全局画家序；
- 同格排序依次由 `z`、对角线、`x`、层序决定，实体再用 StableId 决胜；
- 树、墙等高物体不要靠“负 offset”伪造遮挡，使用正确层高和锚点。

## 4. 命名、图集与 manifest

### 4.1 逻辑名

运行时只认逻辑名，不认 Tiled tile id 或图集矩形：

- 地面：`tile.floor.grass` / `tile.floor.dirt` / `tile.floor.wood`；
- 墙：`wall.brick` / `wall.door` / `wall.window_h` / `wall.window_v`；
- 玩家建造：`object.wall_h` / `object.wall_v` / `object.door_closed_h` /
  `object.door_closed_v` / `object.door_open_h` / `object.door_open_v`；
- 角色：`character.placeholder`（玩家，保留旧名以兼容存档/测试）、
  `character.zombie`（僵尸）；
- 尸体：`corpse.placeholder`；
- 场景：`object.tree` / `object.workbench` / `object.campfire` /
  `object.crate` / `object.locker` / `object.bed` / `decor.grass_tuft`；
- 物品：`item.<id>`，id 与 `assets/items.json` 一致；
- 投射物/特效：`projectile.bullet` / `effect.impact`。

新增资源必须同时更新 `assets/asset_manifest.json` 的 `own` 帧表；不得只把 PNG
丢进目录。Mod 资源改为更新 `mods/<id>/assets/asset_manifest.json`，图集与 PNG 放在
mod 自己的 `assets/` 下，启动时叠加到基础清单（X-F2b-2a）。

### 4.2 帧顺序

- 角色/僵尸：`down_0, down_1, left_0, left_1, right_0, right_1, up_0, up_1`，
  对应 `index = dir*2+frame`，`dir` 依次为 Down/Left/Right/Up；
- 墙砖/门：静态变体按数组顺序由 StableId 哈希选择，可增删但需保持确定性；
- 物品图标：每个逻辑名至少 1 帧；新增变体不改变 `ItemDef.icon`；
- 所有图集输出为 `pz-atlas-v1`，页宽上限 1024，帧矩形不得重叠。

### 4.3 新增/替换流程

1. 在 `assets/own` 下放同尺寸帧，或修改 `tools/gen_art_slice.py`；
2. 运行生成器/打包器，更新 `atlas_own.atlas.json`；
3. 更新 `asset_manifest.json` 的逻辑名与帧表；
4. 运行 `py -3 tools/tests/test_art_slice.py -v`；
5. 实机加载 `village_demo`，检查地面接缝、角色脚底、门/树遮挡和 UI 图标；
6. 在 `docs/devlog.md` 记录范围、未完成项和截图路径。

## 5. 动画契约

当前运行时使用动画 v2（M14-T2/T3），manifest 的每个角色 provider 可带 `animations`：

| 片段 | 每向帧数 | fps | 循环 | 事件帧 | 资源 |
| --- | --- | --- | --- | --- | --- |
| `idle` | 2 | 3 | 是 | 无 | `character_idle_<dir>_<frame>` |
| `walk` | 2 | 5 | 是 | 1（footstep） | 复用旧 `character_<dir>_<frame>` |
| `attack` | 3 | 10 | 否 | 1（hit） | `character_attack_<dir>_<frame>` |
| `hit` | 1 | 8 | 否 | 无 | `character_hit_<dir>_<frame>` |
| `death` | 2 | 6 | 否 | 无 | `character_death_<dir>_<frame>` |

- 4 方向起步：`dir: down=0, left=1, right=2, up=3`；帧序固定
  `index = dir * framesPerDir + frame`，禁止在渲染侧另写顺序；
- `animations` 出现时必须完整包含五项；每片段 `frames` 数量 = `framesPerDir × 4`；
  `AnimationManifest` 会校验帧数/fps/循环/事件帧与 `AnimationSystem::Spec` 一致；
- `AnimState.clip/lastEvent/eventSerial/eventMaskDone` 是运行时字段，不进入存档或
  M13 复制负载；旧快照仍按 `frame/framesPerDir/fps/time` 解码，远端从复制的速度/
  动作/生命推导片段；
- 缺少 v2 片段或旧 provider 时，渲染回退旧 4 向 × 2 帧（攻击/受击帧索引夹到 0/1）；
- 武器/衣物挂点、阴影/遮挡层仍为后续项；当前主手武器是随朝向/动作/帧变化的图标叠加。

`zombie` 使用相同片段表，资源前缀为 `character_zombie_`；死亡第 2 帧直接复用尸体
画布，保证倒地窗口与之后生成的 corpse 实体视觉连续。

## 6. 垂直切片范围

本切片替换/新增：

- 地面：4 个草地变体、2 个泥土、2 个木地板；
- 墙：3 个砖墙变体、2 个门变体、2 个木墙朝向、4 个木门开关/朝向变体；
- 玩家/僵尸：旧 4 向 × 2 帧 + v2 的 idle/attack/hit/death 新帧（walk 复用旧帧）；图集共 188 帧（含 M14-T5 木箱/储物柜、M14-T6 电台/撤离信标/无线电零件图标、M18-F4 木墙/木门、M25-T4 V12 八向皮卡）；`wall.door` 为可通行 open-door 占位；
- 尸体 2 变体；树 3 变体；草丛 3 变体；工作台/篝火/木箱/储物柜各 1；僵尸变体复用同一套角色帧，通过 `ZombieVariant` 的色调与脚底锚点缩放区分，不新增帧；
- 物品图标：代表性 9 个（斧、绷带、罐头、水、背包、锤、长矛、加固夹克、维修包）；
- 投射物和命中特效各 1 个独立帧；
- 其余图标/UI/音频暂保留占位，后续逐项替换。

切片必须在 `village_demo` 的一张实机截图中同时出现：草地/泥路、砖房、门、
玩家、僵尸、树、草丛、工作台、篝火和至少 2 个物品图标；木地板/尸体等由其
余场景或图集预览覆盖。

## 7. 工具与验收

```powershell
py -3 tools/gen_art_slice.py
py -3 tools/tests/test_art_slice.py -v
.\tools\dev.ps1 cmake --build --preset x64-debug
.\build\x64-debug\ZomboidLike.exe --game.startState=world --game.startHour=14 --audio.device=off
```

验收清单：

- [x] 图集/清单加载无缺失帧、无 0 尺寸帧、无越界矩形；
- [x] 所有地面块在 1x/2x 下无缝，角色脚底落在格中心；
- [x] 玩家/僵尸/尸体在四种朝向下可区分；
- [x] 五态动画片段可由 manifest 加载，帧序/事件帧通过 Python 与 C++ 校验；
- [x] 墙体不穿透地面，树/墙遮挡顺序正确；
- [x] 门、工作台、篝火在 1x 缩放可辨认；
- [x] 资源全部来自 `assets/own`，不包含 PZ 贴图/地图/音效；
- [x] 实机截图记录到 `build/art-slice.png`、`build/m14-v2-idle.png`（不提交构建产物）。

## 8. 后续顺序

1. ~~角色动画 v2（片段 + 挂点 + 阴影）~~ — M14-T2/T3 已完成基础片段/事件/主手挂点，骨骼蒙皮与阴影待正式美术；
2. 生存内容垂直切片（需求衰减、loot、僵尸变体、POI）；
3. 全量物品图标与 UI 皮肤；
4. 正式音频；
5. 最终手绘替换程序化切片。
