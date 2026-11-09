# M25-T1 载具驾驶 Spike

> 状态：**M25-T1/M25-T2/M25-T3 已完成；M25-T4 V1–V27 已完成自动化+1280 烟测（2026-11-07）。**
>
> 当前基线：Debug **782/782（133914 assertions）**、Release **782/782（133928 assertions）**、Python **89/89**；载具过滤 59/59（750 assertions）。M26 多车辆、载具间碰撞、客户端撞击反馈、推挤、单机多车、每车远端视觉、多车远端引擎音、等质量碰撞冲量、碰撞乘员伤害、分级质量、显式车型与车身朝向、轮胎转向极限与悬挂姿态、低速接触冲量见 `docs/13-M26多车辆支持.md`。
>
> 本文是 M25 载具方向的第一个任务，只验证“能不能开、能不能撞、能不能上下车”，不是完整载具系统。范围、数值和验收以本文为准；实现中发现需要改范围，先改本文再写代码。

## 0. 结论

先做一个**单机、单车、可上下车、可驾驶、可碰撞、有燃料 HUD**的最小 spike：

- 在玩家出生点附近生成一辆皮卡（占位图）。
- `ENTER` 上车/下车，`WASD` 驾驶，`ESC` 暂停。
- 车辆有加速、减速、滑行、圆形碰撞和燃料消耗。
- 玩家在车内时隐藏角色精灵，位置跟随车辆，相机继续跟随玩家。
- 不做存档、联机、车辆耐久、座位、储物、僵尸碰撞、正式美术和音效。

预计 **1~2 个工作日**（单人全职），按业余时间乘 1.5~2。目标是在 1280×720 窗口内实际开动车辆、撞墙停下、燃料下降并截到一张驾驶画面。

本任务完成后，再根据结果决定是否进入 M25-T2“载具耐久/维修/储物”或回到其他方向；**本任务不承诺完整载具系统**。

## 1. 背景与非目标

### 1.1 为什么现在做

M24 已把“可去、可搜、可打、可讲”的内容密度补到 31 条任务/11 个 POI；当前玩家移动仍只有步行和点击寻路。车辆是 Zomboid-like 类型的高价值大功能，但直接全量开工风险过高：

- 需要新的物理/碰撞/驾驶手感；
- 需要座位、储物、油量、维修、噪音、僵尸交互；
- 需要存档、联机权威和旧档兼容；
- 需要正式车辆美术、动画和音效。

因此先用一个 spike 验证最核心的驾驶手感与接入点，避免在未验证前改存档/网络协议。

### 1.2 本任务范围

- 单机 `WorldDemoState`。
- 一辆车，固定生成在玩家附近。
- 正常操作上车/下车，不依赖 F6/F7 调试键。
- 车辆移动、碰撞、燃料、HUD 提示。
- 占位图 + 1280 烟测截图。
- 单元测试覆盖纯驾驶逻辑。

### 1.3 本任务不做

- 联机同步、服务器权威、客户端预测。
- 存档持久化、旧档迁移、读档恢复车辆。
- 车辆耐久、损坏、维修、爆炸、火灾。
- 座位、乘客、后备箱、车内储物。
- 僵尸/动物与车辆碰撞、车辆碾压伤害。
- 车辆噪音引怪、喇叭、车灯、引擎音效。
- 换挡、离合器、轮胎、悬挂、转向轮、道路附着。
- 多辆车、车辆刷新、车辆钥匙/油桶/加油。
- 正式车辆美术、动画、UI 面板。
- 修改内容指纹、网络协议、存档 schema。

## 2. 玩家体验与验收

### 2.1 体验目标

1. 出生后 10 秒内能在附近看到一辆车，并有“按 Enter 上车”的提示。
2. 上车后角色消失，车辆可前进/后退/转向，速度明显快于步行。
3. 撞到墙/树/家具时车辆停下，不会穿墙。
4. 燃料条/HUD 百分比随时间下降；耗尽后不能继续加速。
5. 下车后角色出现在车辆旁的空地，可继续步行。
6. 全程不崩溃、不卡死、不误触发其他 E 交互。

### 2.2 完成定义（DoD）

- [x] 代码按本文任务顺序实现，未提前做存档/联机/耐久。
- [x] 纯逻辑单测覆盖：加速/限速、松键减速、碰撞停止、无燃料、上下车距离、3×3 生成搜索。
- [x] 单机 1280×720 窗口可实际生成、上车、驾驶、下车；撞墙由纯逻辑单测覆盖。
- [x] 日志包含 `[vehicle]` 生成/上车/下车记录。
- [x] HUD 在接近车辆和驾驶中显示明确提示。
- [x] Debug、Release、Python 全绿，记录实际计数。
- [x] 不降低现有测试；不改内容指纹/存档/网络协议。
- [x] 更新本文、`docs/01`、`docs/05`、`docs/devlog.md`、README 状态。

### 2.3 验收证据

- 单测日志：`build/m25_t1_vehicle_debug.log`、`build/m25_t1_vehicle_release.log`、`build/m25_t1_vehicle_python.log`。
- 烟测目录：`build/m25_t1_vehicle_smoke/`。
- 至少 2 张 1280×720 截图：
  1. 车辆停在玩家附近，HUD 显示“按 Enter 上车”；
  2. 驾驶中 HUD 显示燃料，车辆位置已改变。
- `logs/zomboidlike.log` 中可见 `载具生成`、`上车`、`下车` 三条日志。

## 3. 设计方案

### 3.1 模块与文件

| 文件 | 作用 |
|---|---|
| `src/game/VehicleSpike.h` | 纯函数/数据结构：车辆状态、驾驶步进、碰撞探测、生成点搜索、上车距离；header-only，便于单测 |
| `src/game/states/WorldDemoState.h` | 车辆实体句柄、驾驶状态、玩家精灵备份、车辆相关方法声明 |
| `src/game/states/WorldDemoState.cpp` | 生成车辆、上下车、每 tick 驾驶、玩家位置同步、HUD 提示、存档过滤 |
| `src/ecs/components/CommonComponents.h` | 新增通用 `SpriteScale` 组件（运行时缩放，不存档） |
| `src/game/RenderCollectSystem.cpp` | 读取 `SpriteScale` 缩放车辆占位图 |
| `assets/asset_manifest.json` | 新增逻辑资源 `object.vehicle`，复用现有 `crate_1` 占位帧 |
| `config/game.json` | 新增 `input.bindings.Vehicle = ENTER` |
| `src/game/GameApp.cpp` | 默认键位表增加 `Vehicle = ENTER` |
| `tests/test_vehicle.cpp` | 纯逻辑单测 |
| `CMakeLists.txt` | 测试目标加入 `tests/test_vehicle.cpp` |

### 3.2 车辆状态

```cpp
struct VehicleMotion {
    float x = 0.0f;
    float y = 0.0f;
    int z = 0;
    float vx = 0.0f;
    float vy = 0.0f;
    float fuel = 1.0f;
};
```

`VehicleMotion` 是运行时状态，不进入 ECS 存档组件；ECS 实体只负责位置、渲染和空间索引。

### 3.3 数值（spike 固定值）

| 参数 | 值 | 说明 |
|---|---:|---|
| `kVehicleMaxSpeed` | 10 格/秒 | 玩家步行 4 格/秒，车辆约 2.5 倍 |
| `kVehicleAcceleration` | 8 格/秒² | 约 1.25 秒到满速 |
| `kVehicleBraking` | 14 格/秒² | 松键/无燃料时较快停下 |
| `kVehicleRadius` | 0.65 格 | 圆形碰撞半径 |
| `kVehicleEnterRange` | 2.2 格 | 上车距离 |
| `kVehicleFuelPerSecond` | 0.012 /秒 | 满速约 83 秒耗尽；spike 可接受 |
| 初始燃料 | 1.0 | 不提供加油 |
| 生成搜索 | 玩家东南 +6,+6，半径 3 | 要求 3×3 空地，避免生成在脚下 |

所有数值先硬编码在 `VehicleSpike.h`；本任务不新增配置项。若 spike 通过，M25-T2 再迁移到 `config/game.json` 并加平衡测试。

### 3.4 更新顺序

```text
GameApp::Tick
  └─ WorldSimulation::Tick        // 玩家物理/AI 先跑
  └─ WorldDemoState::OnTick
       └─ UpdateVehicle           // 车辆移动 + 玩家跟随
       └─ UpdateRunStats/...
  └─ Flush

WorldDemoState::OnFrame
  └─ EnsurePlayerSpawned
  └─ EnsureVehicleSpawned         // 玩家就绪后生成，失败则下帧重试
  └─ CollectWorld / RenderCollect
  └─ RefreshHud                   // 车辆提示
```

- 车辆移动在 `OnTick` 末尾段执行，晚于 `WorldSimulation::Tick` 一个 tick；spike 接受这个延迟。
- 车辆没有 `RigidBody`，由 `VehicleStep` 手动积分；玩家 `RigidBody` 每 tick 用 `PhysicsWorld::TeleportBody` 同步到车辆位置。
- 玩家 `MoveInput/MoveIntent/Velocity` 在驾驶时清零，避免 `MovementSystem` 把玩家从车里带走。

### 3.5 生成

- 仅单机；`m_networkMode` 为 true 时不生成。
- 玩家实体存在后，以玩家东南 +6,+6 为锚点、半径 3 格螺旋搜索一个 **3×3 全可走** 的中心格，并跳过距玩家小于 2 格的候选，避免生成在脚下。
- 可走判定使用 `game::IsSquareBlocked(world, wx, wy, 0)`；未加载 chunk 视为阻挡。
- 找到后在格中心创建实体：
  - `Position`
  - `SpriteRef{HashName("object.vehicle"), 0, 0xFFFFFFFF}`
  - `SpriteScale{1.7f}`
  - `Collider{0.65f, 1.2f, kColliderCharacter, ...}`
  - **不加 `RigidBody`/`Velocity`**；位置由 `VehicleMotion` 驱动，避免物理体冲突与二次积分。
- 车辆实体加入空间索引；保存时按实体句柄过滤，不写入存档。
- 找不到 3×3 空地时不生成，等待下一帧重试；不刷错误日志。

### 3.6 上下车

- 按 `Vehicle`（默认 `ENTER`）：
  - 已在驾驶 → 下车；
  - 未驾驶且玩家到车辆距离 ≤ 2.2 格 → 上车；
  - 否则无操作，不弹错误。
- 上车：
  - 备份玩家 `SpriteRef` 并移除，隐藏角色；
  - `m_driving = true`；
  - 同步玩家位置到车辆，清零移动输入/速度，传送物理体；
  - 记录日志并提示“WASD 驾驶 · Enter 下车”。
- 下车：
  - 在车辆周围 1~3 格寻找最近可走格；
  - 找不到则提示“无法下车：周围没有空地”，保持驾驶；
  - 找到后停止车辆、恢复玩家 `SpriteRef`、传送玩家、`m_driving = false`。
- 驾驶中按 `ESC` 打开暂停菜单；不响应攻击/使用/拾取/建造/背包等游戏输入。
- 玩家死亡时不自动下车；本局结束后由 `OnExit` 统一清理。

### 3.7 驾驶与碰撞

输入映射：

- `W/S/A/D` 是屏幕语义，复用 `InputSystem::ScreenDirToGridDir` 转世界格方向。
- 有输入且有燃料：向目标速度加速；目标速度 = 方向 × 10 格/秒。
- 无输入或无燃料：向 0 速度制动；保留短距离滑行。
- 燃料只在车辆实际移动时消耗，按当前速度/满速比例扣减。

碰撞：

- 目标位置用圆形 8 点采样（中心 + 8 个方向）。
- 任一点所在格 `IsSquareBlocked` 为真 → 该方向不可走。
- 先尝试完整位移；失败后尝试仅 X、仅 Y，实现贴墙滑动；都失败则速度归零。
- 不允许穿墙、穿树、穿家具；不处理僵尸/动物碰撞。

### 3.8 渲染与 HUD

- 逻辑资源 `object.vehicle` 复用 `assets/own/atlas_own.atlas.json` 的 `crate_1` 帧，`SpriteScale=1.7` 放大；这是明确的占位方案。
- 新增 `ecs::SpriteScale` 纯运行时组件，`RenderCollectSystem` 在实体锚点底部缩放；不存档、不复制。
- 不新增 HUD 字段/RML 节点，复用 `HudState.actionHint`：
  - 接近：`按 Enter 上车：皮卡 · 燃料 xx%`
  - 驾驶：`驾驶中：WASD 移动 · Enter 下车 · 燃料 xx%`
- 不新增车辆面板/速度表；速度只写日志，HUD 暂不显示。

### 3.9 存档/联机边界

- 车辆实体不写入存档；`CaptureEntities` 过滤 `entity == m_vehicle`。
- 读档后车辆按当前玩家位置重新生成，不恢复旧位置/燃料。
- 联机模式不生成车辆；`ToggleVehicle` 在 `m_networkMode` 下直接返回 false。
- 不改 `PlayerState`、`ZLR1`、内容指纹、`kContentHashVersion`。
- 旧档加载不崩溃；车辆不存在不影响任何旧流程。

## 4. 任务拆解与执行顺序

> 执行纪律：一次只做一个任务；完成代码 + 测试 + 文档勾选后停下来汇报，等用户说“继续”再做下一个。不得跳过任务或提前做后续范围。

### M25-T1-V1 文档（本任务）

- 产出：本文。
- 验收：用户确认范围、数值、键位、非目标。
- 状态：**已确认（2026-11-05）**。

### M25-T1-V2 纯逻辑与单测

- 文件：`src/game/VehicleSpike.h`、`tests/test_vehicle.cpp`、`CMakeLists.txt`。
- 内容：
  - `VehicleMotion`、`VehicleSpeed`、`VehicleCanEnter`；
  - `VehicleBlocked`（8 点圆形采样）；
  - `VehicleStep`（加速/制动/燃料/轴滑动）；
  - `FindVehicleSpawnCell`（3×3 空地搜索）。
- 测试：
  1. 加速到限速，燃料下降；
  2. 松键减速并保留滑行；
  3. 前方阻挡停止且不穿墙；
  4. 大位移目标不穿墙；
  5. 无燃料不能加速；
  6. 上车距离边界；
  7. 3×3 生成搜索成功/失败。
- 验收：只跑 `ZomboidLikeTests.exe -tc="*Vehicle*"` 通过；不接游戏状态。
- 状态：**已完成（2026-11-05）**；7 用例/31 assertions；过滤运行 7/7；全量 Debug 705/705（132272）。
- 实测：7 用例，31 assertions。

### M25-T1-V3 单机生成/上下车/驾驶

- 文件：`WorldDemoState.h/.cpp`。
- 内容：
  - `EnsureVehicleSpawned`、`UpdateVehicle`、`ToggleVehicle`、`EnterVehicle`、`ExitVehicle`、`SyncPlayerToVehicle`；
  - 玩家精灵隐藏/恢复；
  - 物理体传送、移动输入清零；
  - 驾驶中屏蔽其他游戏交互，保留 ESC 暂停；
  - 存档过滤车辆实体。
- 验收：单机可生成、上车、移动、撞墙、下车；不崩。
- 状态：**已完成（2026-11-05）**。载具在玩家东南侧 3×3 空地生成（Debug 74,28 / Release 75,29）；Debug 1280 实测上车距离 1.92、驾驶后下车格 (76.5,30.5)；车辆实体不写入存档，无 `RigidBody`/`Velocity`，由 `VehicleMotion` 驱动。

### M25-T1-V4 输入/资源/渲染/HUD

- 文件：`GameApp.cpp`、`config/game.json`、`assets/asset_manifest.json`、`CommonComponents.h`、`RenderCollectSystem.cpp`。
- 内容：
  - 默认键位 `Vehicle = ENTER`；
  - `object.vehicle` 逻辑资源；
  - `SpriteScale` 组件与实体缩放；
  - `HudState.actionHint` 接近/驾驶提示；
  - 启动日志补充 Enter 上车。
- 验收：1280×720 窗口内可见车辆和提示。
- 状态：**已完成（2026-11-05）**。`object.vehicle` 复用 `crate_1` 并 `SpriteScale=1.7`；`Vehicle=ENTER` 已写入默认键位与配置；接近/驾驶 HUD 截图通过；未新增 HUD 字段/RML 节点。

### M25-T1-V5 全量测试

- 命令：
  - `.\tools\dev.ps1 cmake --build --preset x64-debug`
  - `.\build\x64-debug\ZomboidLikeTests.exe`
  - `.\tools\dev.ps1 cmake --build --preset x64-release`
  - `.\build\x64-release\ZomboidLikeTests.exe`
  - `py -3 -X utf8 -m unittest discover -s tools\tests -p "test_*.py"`
- 验收：Debug/Release/Python 全绿；记录实际 case/assertion 计数；不出现新增失败。
- 状态：**已完成（2026-11-05）**。Debug **705/705（132272）**、Release **705/705（132286）**、Python **88/88**；日志见 `build/m25_t1_vehicle_debug.log`、`build/m25_t1_vehicle_release.log`、`build/m25_t1_vehicle_python.log`。

### M25-T1-V6 实机烟测与文档收口

- 烟测：
  1. 启动 Release（`--game.startState=world` 调试直达），1280×720 窗口化；
  2. 等待玩家生成，按住 `S` 走向载具；
  3. 截图车辆与“按 Enter 上车”；
  4. Enter 上车，按住 `S` 驾驶 1~2 秒，确认位置改变；撞墙由纯逻辑单测覆盖；
  5. 截图驾驶 HUD；
  6. Enter 下车，确认角色恢复（Debug 指令已验证）；
  7. 检查 `logs/zomboidlike.log` 的 `[vehicle]` 日志。
- 产出：`build/m25_t1_vehicle_smoke/`、截图、`SUMMARY.md`。
- 文档：更新本文勾选、`docs/01`、`docs/05`、`docs/devlog.md`、README。
- 状态：**已完成（2026-11-05）**；Release 截图 `release_near2.png` / `release_driving2.png` / `release_moved2.png`，Debug 下车日志 `[vehicle] 下车：格 (76.5, 30.5)，燃料 99.5%`；用户以“继续下一步”确认进入 M25-T2。
- 验收：用户确认进入下一任务（2026-11-05）。

## 5. 风险与已知限制

| 风险 | 影响 | 本任务处理 |
|---|---|---|
| 车辆没有物理体 | 僵尸/动物可穿过车辆，不能碾压 | 明确 spike 限制；M25-T2 再评估 |
| 玩家物理体每 tick 传送 | 与僵尸推挤/碰撞可能抖动 | 驾驶时隐藏角色并清速度；只验证不崩 |
| 占位图是木箱放大 | 视觉不像车 | 明确占位；正式美术单独排期 |
| Enter 与联机聊天冲突 | 联机误触 | 联机不生成车辆；聊天输入框聚焦时 UI 阻断游戏输入 |
| 读档后车辆重置 | 燃料/位置丢失 | 本任务明确不存档；M25-T2 再设计 |
| 生成点找不到 3×3 空地 | 无车可开 | 下帧重试；日志只记一次失败 |
| 车辆移动晚一个 tick | 相机/碰撞轻微滞后 | spike 接受；后续接物理或预测再优化 |

## 6. 后续候选（不在本任务）

- **M25-T2 载具耐久/维修/储物**：车辆 HP、轮胎、后备箱、加油、维修包。
- **M25-T3 载具联机**：服务器权威位置/燃料、上下车命令、客户端预测/校正、重连恢复；V1 设计见 `docs/12-M25T3载具联机.md`。
- **M25-T4 车辆与世界交互**：碾压/撞击僵尸与动物、噪音引怪、车灯、道路附着、载具音效、撞击车况磨损、低车况动力衰减、撞击击杀经验、远端载具引擎音、正式车辆美术/音效（V1 碾压/噪音见 6.1；V2 车灯见 6.2；V3 路面附着见 6.3；V4 引擎/撞击占位音效见 6.4；V5 撞击车况磨损见 6.5；V6 低车况动力衰减见 6.6；V7 撞击击杀经验见 6.7；V8 远端载具引擎音见 6.8；V9 自然试玩与驾驶员防护见 6.9；V10 皮卡四向外观见 6.10；V11 低车况/归零烟雾见 6.11；V12 八向皮卡外观见 6.12；V13 软粒子烟雾见 6.13；V14 喇叭/主动噪音见 6.14；V15 僵尸攻击载具见 6.15；V16 载具受击音效/火花见 6.16；V17 载具破损外观见 6.17；V18 载具受击镜头反馈与金属碎片见 6.18；V19 载具撞击动物见 6.19；V20 危险动物反击载具见 6.20；V21 联机 PvP 载具撞玩家见 6.21；V22 联机载具乘客位见 6.22；V23 乘客接管驾驶位见 6.23；V24 驾驶员停车让座见 6.24；V25 乘客车内背包/后备箱见 6.25；V26 乘客快捷使用与装备见 6.26；V27 双乘客位见 6.27；正式手绘/音效和更完整自然试玩未做）。
- **多层建筑/正式美术/音频**：仍为独立大功能，需另写立项文档。

### 6.1 M25-T4 V1：碾压/撞击僵尸与噪音（2026-11-05）

- 范围：单机与联机服务器权威的载具撞击僵尸、击退/硬直、车辆噪音引怪；不做车灯、道路附着、动物/玩家碾压、正式美术/音效。
- 纯逻辑：`VehicleSpike.h` 新增 `VehicleRamDamage/VehicleRamKnockback/VehicleRamStagger/VehicleRamOverlap`，速度阈值、伤害、击退、硬直与冷却全部可配置；默认 `ramMinSpeed=2`、基础伤害 20 + 18/速度、击退 1.2 + 0.45/速度、冷却 0.75s，噪音半径 12 格、间隔 0.45s、最低速度 1.5。
- 单机：`WorldDemoState` 驾驶时对同层、非友好/玩家的 `AiState+Health` 目标做重叠判定，走现有 `CombatSystem::ApplyDamage`（Blunt），按目标冷却避免每 tick 重复命中；速度达到阈值后按间隔向 `OnSound` 发送 `SoundType::Vehicle` 引怪。目标死亡/复制/任务/掉落继续走现有管线。
- 联机：`ServerHost` 在服务器车辆 tick 后执行同一套撞击判定并权威扣血，`ZLR1` Health 复制给客户端；噪音由服务器广播 `WorldSound`（`SoundType::Vehicle`）并投递 `OnSound`，客户端只播放/显示，不本地扣血。服务器断线/死亡/无人驾驶时清空撞击冷却与噪音计时。
- 配置：`config/game.json` 的 `vehicle` 段新增 `ramMinSpeed`、`ramBaseDamage`、`ramDamagePerSpeed`、`ramKnockbackBase/PerSpeed`、`ramStaggerBase/PerSpeed`、`ramCooldownSeconds`、`noiseRadius/noiseIntervalSeconds/noiseMinSpeed`；单机与服务器均在启动时解析并钳制。
- 测试：`M25-T4 VehicleRam` 纯逻辑 + `M25-T4 服务器载具撞击/噪音与车灯复制` 集成用例，共 **2/2（29 assertions）**；覆盖速度阈值、伤害/击退/重叠边界、服务器扣血复制、噪音广播与 AI `sinceHeard` 引怪。全量 Debug **726/726（132845 assertions）**、Release **726/726（132859 assertions）**、Python **88/88**；日志 `build/m25_t4_targeted.log`、`build/m25_t4_debug.log`、`build/m25_t4_release.log`、`build/m25_t4_python.log`。
- 烟测：`build/m25_t4_vehicle_world_smoke/`，Release 单机 1280×720，构造 12 个僵尸围绕皮卡驾驶；日志出现 `载具撞僵尸` 5 次与车辆碰撞，截图 `m25t4_near_vehicle.png`、`m25t4_driving.png`、`m25t4_ram.png`、`m25t4_after_ram.png`；脚本 `build/m25_t4_smoke.ps1`（临时，不入库）。这是构造烟测，不是自然多人试玩。
- 已知限制：1280 烟测为传送/脚本刷僵尸的构造场景；V1 当时无专用引擎音效、`Vehicle` 暂用 Impact 占位（V4 已补引擎/撞击占位音）；车辆撞僵尸已由 V5 按速度扣车况，低车况动力衰减与归零引擎音由 V6 补齐，撞击击杀经验由 V7 补齐（任务击杀继续走全局 kills）；车灯、路面附着、占位音效、撞击车况磨损、低车况反馈与撞杀经验已由 6.2/6.3/6.4/6.5/6.6/6.7 补齐，正式美术/音效仍待后续。

### 6.2 M25-T4 V2：车灯（2026-11-05）

- 范围：皮卡车灯开关、夜间可见的占位光束、状态存档与联机权威同步；不做真实动态光照/阴影、道路附着、动物/玩家碾压、正式车辆美术/音效。
- 键位：驾驶中 `L` 切换车灯；非驾驶状态 `L` 仍为任务焦点（驾驶分支先返回，不冲突）。驾驶 HUD 显示 `L 车灯开/关`。
- 状态：`VehicleMotion.headlights` 与可存档 `ecs::Vehicle.headlights`；`Vehicle` 存档组件升 v2，旧 v1 缺失字段按 `false` 读取（`minimumVersion=1`）。
- 联机：新增 `CommandType::ToggleHeadlights = 43`，仅驾驶员可发送；服务器校验驾驶位后切换、写 ECS 并回 `status`，客户端不做本地权威切换。ZLR1 `VHCL` 负载 16→17 字节（fuel/condition/driver/headlights），所有观察者同步车灯状态；`PlayerState` 结构不变。
- 单机：切换后立即写 `ecs::Vehicle` 并存档；读档/服务器重启恢复车灯状态。
- 渲染：`DrawVehicleHeadlights` 在车辆前方绘制 18 条低透明光束射线、远端弧线与两个灯点；夜间强度 1.0、白天 0.28，方向由最近速度方向保持（停车时保留最后朝向）。这是占位体积光，不改变世界亮度/阴影/碰撞。
- 测试：新增 `M25-T4-V2 车灯状态在驾驶步进后保留`；扩展车辆存档、ZLR1 车辆复制、命令编解码与服务器载具集成用例。目标用例 **26/26（544 assertions）**；全量 Debug **728/728（132881 assertions）**、Release **728/728（132895 assertions）**、Python **88/88**；日志 `build/m25_t4_v2_targeted.log`、`build/m25_t4_v2_debug.log`、`build/m25_t4_v2_release.log`、`build/m25_t4_v2_python.log`。
- 烟测：`build/m25_t4_v2_headlights_smoke/`，Release 单机 1280×720、`game.startHour=22`；截图 `v2_vehicle_dark.png`、`v2_driving_lights_off.png`、`v2_headlights_on.png`、`v2_headlights_driving.png`、`v2_headlights_off.png`，日志确认 `车灯：开`、`车灯：关` 与 HUD 状态。脚本 `build/m25_t4_v2_headlights_smoke.ps1`（临时，不入库）。
- 已知限制：光束是屏幕空间的半透明扇形/弧线，不是真实光照或昼夜调色；无阴影、遮挡和正式车灯音效；路面附着由 6.3 补齐；烟测为构造传送，不是自然试玩。

### 6.3 M25-T4 V3：路面附着（2026-11-05）

- 范围：按车辆中心所在地面识别铺装/泥土路/草地，调整极速、加速与制动（低附着草地更慢、更滑）；不做四轮独立采样、轮胎/悬挂、真实漂移、坡道/湿度、正式路面美术。
- 分类：`tile.floor.wood` = 铺装路面（1.0/1.0/1.0）；`tile.floor.dirt` = 泥土路（极速 x0.92、加速 x0.88、制动 x0.92，现有地图的泥土路即此类）；`tile.floor.grass` 与未知地面 = 草地（x0.62/x0.55/x0.72）。倍率可在 `vehicle.dirt*/grass*` 配置并钳制到 0..1。
- 纯逻辑：`VehicleSurfaceForFloor`、`VehicleSurfaceProfileFor`、`VehicleSurfaceLabel`；`VehicleStep`/`VehiclePredictStep` 接受可选 `VehicleSurface`，默认铺装，旧调用行为不变。
- 单机：`WorldDemoState::VehicleSurfaceAt` 取车辆中心格；`UpdateVehicle` 每 tick 按所在路面步进；驾驶 HUD 追加 `路面 草地/泥土路/铺装路面`，路面变化写 `[vehicle] 路面：...`。
- 联机：服务器 `ServerVehicleSurfaceAt` 权威采样并用于服务器 `VehicleStep`，变化时写 `[vehicle] 服务器路面：...`；客户端预测使用同一分类，HUD 只显示本地采样。不新增命令、复制字段或存档 schema。
- 测试：新增 `M25-T4-V3 VehicleSurface` 分类/倍率、极速/加速排序、制动/预测 3 个用例；目标 **29/29（576 assertions）**；全量 Debug **731/731（132913 assertions）**、Release **731/731（132927 assertions）**、Python **88/88**；日志 `build/m25_t4_v3_targeted.log`、`build/m25_t4_v3_debug.log`、`build/m25_t4_v3_release.log`、`build/m25_t4_v3_python.log`。
- 烟测：`build/m25_t4_v3_surface_smoke/`，Release 单机 1280×720；同一方向各驾驶 1s，草地起点 (82.5,20.5) 移动 3.16 格、泥土路起点 (82.5,26.5) 移动 5.44 格；HUD/日志确认 `路面 草地`/`路面 泥土路`，共 6 张截图（`v3_grass_*`、`v3_dirt_*`）。脚本 `build/m25_t4_v3_surface_smoke.ps1`（临时，不入库）。
- 已知限制：按车辆中心单格判定，跨路缘/斜向行驶可能提前降速；从高附着进入低附着时极速上限立即截断而非渐进；不改变玩家步行；烟测为构造起点与键盘直行，不是自然试玩。

### 6.4 M25-T4 V4：载具音效占位（2026-11-05）

- 范围：驾驶时本地引擎循环音（音量/音高随速度）、撞击僵尸的短促撞击音；不做正式车辆音效、远端车辆引擎声、轮胎/悬挂/路面材质音、音量面板与真实设备听感验收。
- 资源：新增程序合成的 `assets/audio/vehicle_engine.wav`（2s 无缝循环）与 `vehicle_ram.wav`（0.3s 短撞击）；按现有 M11 占位音约定随仓库分发，后续可替换为正式素材。
- 音频引擎：`AudioEngine::Play3D` 新增可选 `allowPitch`，并提供 `SetVolume/SetPitch/SetPosition` 更新长音；默认调用仍禁用 pitch，旧音效行为不变。
- 单机/联机驾驶：`UpdateVehicleAudio` 在本地驾驶时启动引擎循环，按 `VehicleEngineToneForSpeed` 把速度映射到音量 0.18→0.62、音高倍率 x0.82→x1.28，每帧更新位置/音量/音高；下车、车辆失效、断线或状态退出时停止。配置项为 `vehicle.engineIdleVolume/engineMaxVolume/engineMinPitch/engineMaxPitch`。
- 撞击：单机撞中僵尸时播放 `vehicle_ram`；服务器在权威撞击成功后广播 `WorldSound`（`Impact` 类型、`vehicle_ram` clip、半径 18），客户端统一播放。音量由 `vehicle.ramSoundVolume` 控制。
- 测试：新增 `M25-T4-V4 VehicleAudio` 速度→音量/音高/钳制用例；扩展 AudioEngine null device 用例验证引擎循环、位置/音量/音高更新与撞击 clip 加载；服务器集成用例验证撞击后客户端收到 `vehicle_ram` 声音。目标 **32/32（607 assertions）**；全量 Debug **732/732（132931 assertions）**、Release **732/732（132944 assertions）**、Python **88/88**；日志 `build/m25_t4_v4_targeted.log`、`build/m25_t4_v4_debug.log`、`build/m25_t4_v4_release.log`、`build/m25_t4_v4_python.log`。
- 烟测：`build/m25_t4_v4_audio_smoke/`，Release 单机 1280×720、`audio.device=null`；草地/泥土路各完成上车→引擎启动→下车停止→再次上车启动，日志分别出现 2 次 `引擎音效启动`/1 次 `引擎音效停止`，并复跑路面烟测（草地 3.16 格、泥土路 5.63 格），6 张截图。脚本 `build/m25_t4_v4_audio_smoke.ps1`（临时，不入库）。null device 只验证句柄/资源/状态，不代表实际听感。
- 已知限制：引擎声只播放本地驾驶者车辆，其他玩家车辆无引擎循环（远端引擎循环由 6.8 补齐）；撞击音是合成占位，未做路面材质/速度分层、遮挡/混响或正式音量混音；未做自然听感验收与正式音频资源替换。

### 6.5 M25-T4 V5：撞击车况磨损（2026-11-05）

- 范围：载具成功撞击僵尸后，按撞击速度扣车况；单机与服务器权威使用同一规则。不做动物/玩家碾压、撞击击杀 XP/任务归属、分部件耐久、车况归零后的起火/报废演出。
- 纯逻辑：`VehicleRamConditionDamage` 在速度达到 `ramMinSpeed` 后返回基础 `1.5%` + 超过阈值的速度 × `0.8%/格`，上限 `12%`；`VehicleApplyRamConditionDamage` 负责按当前车况夹取并返回实际扣减。非有限速度、低于阈值或车况已为 0 时返回 0。
- 限频：单机与服务器各维护 `ramWearCooldown`，每次实际扣车况后重置为 `ramCooldownSeconds`；同一窗口内命中多个僵尸只扣一次，避免一帧穿过尸群瞬间报废。
- 单机：`ApplyLocalVehicleRam` 命中后扣车况，HUD 车况当帧下降并显示 `载具撞击：车况 xx%`；日志 `[vehicle] 撞击车况：速度 x，扣车况 y%，剩余 z%`。车况仍由 `SyncVehicleStateToEcs` 写入 `ecs::Vehicle`，随存档保存。
- 联机：服务器 `ApplyServerVehicleRam` 权威扣车况并返回是否命中，`SyncServerVehicleToEcs` 通过既有 ZLR1 `VHCL` 的 condition 字段复制；服务器回 `status` “撞击：车况 xx%”。不在客户端重复扣减。
- 配置：`config/game.json` 的 `vehicle` 段新增 `ramConditionDamageBase=0.015`、`ramConditionDamagePerSpeed=0.008`、`ramConditionDamageMax=0.12`；单机与服务器均在启动时解析并钳制。不新增协议、存档 schema 或内容指纹字段（沿用已有 condition）。
- 测试：新增 `M25-T4-V5 VehicleRamCondition：速度映射与车况钳制`，覆盖阈值、基础/速度项、上限、NaN、当前车况夹取与空指针；服务器载具撞击集成用例追加“撞击后 condition 下降”断言。目标 **33/33（620 assertions）**；全量 Debug **733/733（132943 assertions）**、Release **733/733（132957 assertions）**、Python **88/88**；日志 `build/m25_t4_v5_targeted.log`、`build/m25_t4_v5_debug.log`、`build/m25_t4_v5_release.log`、`build/m25_t4_v5_python.log`。
- 烟测：`build/m25_t4_v5_ram_wear_smoke/`，Release 单机 1280×720；构造 12 个僵尸，驾驶撞击后日志出现 `撞击车况：速度 2.1，扣车况 1.5%，剩余 98%`，HUD 车况 98%；同一冷却窗口内后续 2.7/4.2 速度命中未重复扣车况，验证限频。4 张截图 `v5_near_vehicle.png`、`v5_driving.png`、`v5_ram.png`、`v5_after_ram.png`；脚本 `build/m25_t4_v5_ram_wear_smoke.ps1`（临时，不入库）。
- 已知限制：只对当前载具撞击目标（僵尸）生效，不含动物/玩家；撞击击杀经验由 6.7 补齐，但任务专属归属仍走全局 kills；车况磨损是整车单值，无轮胎/悬挂/前脸等分部件；烟测为低速构造撞击，未覆盖车况归零、连续尸群、自然驾驶与正式平衡；低车况动力衰减与归零引擎音停止由 6.6 补齐。

### 6.6 M25-T4 V6：低车况动力衰减与失效反馈（2026-11-05）

- 范围：车况低于 75% 后按车况线性降低极速与加速，HUD/日志提示“动力下降”；车况归零时停止本地引擎循环音，保留现有“车辆损坏/按 E 维修”提示。不做分部件耐久、轮胎/悬挂、起火/爆炸/报废演出、油门迟滞、正式音效。
- 纯逻辑：`VehicleConditionPerformanceScale(condition)` 返回 0..1 动力倍率；车况 75% 及以上为 1.0，50% 为 0.85，25% 为 0.70，0% 为 0，0..75% 之间线性；非有限/负数按 0 处理。`VehicleStep` 把倍率乘到极速与加速，制动不受影响；`VehiclePredictStep` 复用同一规则。
- 单机：驾驶 HUD 在 `0 < 车况 < 75%` 时追加“动力下降”，首次跌破阈值显示提示并写 `[vehicle] 车况低：动力 x...`；车况归零时 `UpdateVehicleAudio` 停止引擎循环，维修后恢复。现有 `ecs::Vehicle.condition` 存档与维修流程不变。
- 联机：服务器 `VehicleStep` 与客户端预测使用同一纯逻辑；不新增命令、复制字段、协议或存档 schema。客户端 HUD 使用已复制的车况镜像。
- 测试：新增 `M25-T4-V6 VehicleCondition：低车况动力倍率与极速衰减`，覆盖 100/75/50/25/0%、非法值、极速/加速上限与预测复用；目标 **34/34（634 assertions）**；全量 Debug **734/734（132957 assertions）**、Release **734/734（132971 assertions）**、Python **88/88**；日志 `build/m25_t4_v6_targeted_all.log`、`build/m25_t4_v6_debug.log`、`build/m25_t4_v6_release.log`、`build/m25_t4_v6_python.log`。
- 烟测：`build/m25_t4_v6_low_condition_smoke/`，Release 单机 1280×720；临时将单次撞击磨损调到 60%，构造 1 个僵尸，速度 5.5 撞击后日志 `撞击车况：速度 5.5，扣车况 60.0%，剩余 40%`、`车况低：动力 x0.79（车况 40%）`，HUD 显示 `车况 39% · 路面 草地 · 动力下降`；4 张截图 `v6_near_vehicle.png`、`v6_driving.png`、`v6_ram_low_condition.png`、`v6_low_condition_driving.png`；脚本 `build/m25_t4_v6_low_condition_smoke.ps1`（临时，不入库）。
- 已知限制：动力衰减是整车单值且立即截断当前极速，不做分部件/渐进失速；75% 以上无手感变化；烟测为构造高磨损撞击，不是自然驾驶平衡；车况归零的引擎音停止为代码路径，未做单独听感验收。

### 6.7 M25-T4 V7：撞击击杀经验归属（2026-11-05）

- 范围：载具撞击造成僵尸死亡时，给驾驶员发放一次近战击杀经验；单机与服务器权威同规则。不做动物/玩家碾压、载具专属技能、撞击命中经验、任务专属协议或分部件结算。
- 规则：只有 `CombatSystem::ApplyDamage` 返回成功且目标生命降至 0 才发放；沿用 `progression.meleeXpPerKill`（默认 25），每次击杀一次，不额外发放 `meleeXpPerHit`。无新增技能/配置/存档字段。
- 单机：`ApplyLocalVehicleRam` 在命中致死后调用现有 `GrantProgressionXp(Melee, meleeXpPerKill, "载具撞击")`，并写 `[vehicle] 载具撞杀：目标 ...，+x 近战经验`；非致死撞击不发经验。
- 联机：`ApplyServerVehicleRam` 致死后通过 `driverStableId` 找驾驶员的 `Progression`，调用服务器 `AwardProgressionXp`，由既有 PlayerState 同步；客户端不重复发放。
- 任务：载具击杀已通过 `CombatSystem` 的 `kills` 统计进入 `Kill any` 任务进度，随机事件目标也按目标死亡检测；本切片不新增任务协议，仅补 XP。
- 测试：扩展服务器载具撞击集成用例，在载具位置构造 1 HP 目标并断言驾驶员 `Melee` 经验上升；目标 **34/34（639 assertions）**；全量 Debug **734/734（132962 assertions）**、Release **734/734（132976 assertions）**、Python **88/88**；日志 `build/m25_t4_v7_targeted_all.log`、`build/m25_t4_v7_debug.log`、`build/m25_t4_v7_release.log`、`build/m25_t4_v7_python.log`。
- 烟测：`build/m25_t4_v7_ram_xp_smoke/`，Release 单机 1280×720；临时把 `ramBaseDamage` 调到 200，构造 1 个僵尸，速度 5.5 撞击日志 `伤害 200`、`载具撞杀：...+25 近战经验`、`撞击车况...剩余 96%`；HUD `XP 25/180`、车况 95%；4 张截图 `v7_near_vehicle.png`、`v7_driving.png`、`v7_ram_kill.png`、`v7_after_ram.png`；脚本 `build/m25_t4_v7_ram_xp_smoke.ps1`（临时，不入库）。
- 已知限制：只对僵尸生效；近战经验是临时归类，没有驾驶技能；不发放撞击命中经验，不处理载具击杀的任务专属归属/分配；烟测为构造单目标撞击，不是自然战斗平衡。

### 6.8 M25-T4 V8：远端载具引擎音（2026-11-05）

- 范围：联机客户端在非本人驾驶时，听到已绑定远端载具的引擎循环音；不改变本地驾驶引擎音、不新增正式音效/轮胎音/多车混音。
- 数据：直接使用 ZLR1 `VHCL` 已复制的载具位置、速度、车况和 `vehicleDriverStableId`；不新增命令、复制字段、协议或存档 schema。
- 播放：本地未驾驶且绑定的载具存在远端驾驶员（`vehicleDriverStableId` 非 0 且不是本地玩家）时，启动/更新同一条 `vehicle_engine` 3D 循环；音量/音高沿用 `VehicleEngineToneForSpeed` 与现有配置。
- 停止：远端驾驶员下车、载具离开视野/解绑、断线、车况归零或本地重新驾驶时停止；本地驾驶仍优先，避免双重引擎声。
- 测试：新增 `M25-T4-V8 VehicleEngineAudioSource：本地优先与远端/归零`，覆盖无源、本地、远端、本地优先、归零/NaN；目标 **35/35（645 assertions）**；全量 Debug **735/735（132968 assertions）**、Release **735/735（132982 assertions）**、Python **88/88**；日志 `build/m25_t4_v8_targeted_all.log`、`build/m25_t4_v8_debug.log`、`build/m25_t4_v8_release.log`、`build/m25_t4_v8_python.log`。
- 烟测：`build/m25_t4_v8_remote_audio_smoke/`，Release 双客户端 1280×720，客户端 B 使用 `audio.device=null`；A 上车后 B 日志 `引擎音效启动（远端，占位循环，音量/音高随速度）`，A 下车后 B 日志 `引擎音效停止`；截图 `b_remote_engine.png`、`b_remote_stopped.png`、`a_driving.png`、`a_remote_stopped.png`；脚本 `build/m25_t4_v8_remote_audio_smoke.ps1`（临时，早期停止版，未跑完整 T3 流程）。
- 已知限制：只支持单服务器单车；不做多车实例化、远端撞击音重复播放、正式混音/遮挡/听感验收；远端引擎音依赖服务器复制频率；null device 只验证句柄/资源/状态，不代表实际听感。

### 6.9 M25-T4 V9：自然试玩与驾驶员防护（2026-11-05）

- 范围：用默认出生点、真实 WASD/Enter 和默认 16 个 demo 游荡者做自然驾驶，不传送、不脚本刷僵尸；根据尸群压力补驾驶位防护与平衡收口。不做完整人工试玩、正式车辆美术/音效、分部件耐久、多车或车辆刚体。
- 自然试玩：默认 Release 1280×720，从 `(69.2,23.5)` 走到村庄皮卡，沿东侧开阔方向驾驶；`build/m25_t4_v9_natural_smoke/` 记录 6 张截图、`game.log` 与 `SUMMARY.md`。默认种子游荡者 id 10 被车辆噪音吸引，驾驶撞击并击杀；末次自然跑车况 84%、驾驶中生命 70.3、3 次撞杀、首次撞杀 XP 25，日志有 `驾驶员防护 85%`。探针只记录位置/生命/僵尸状态，不调用 `Player.Teleport` 或 `Zombies.Spawn`。
- 平衡问题：75% 驾驶位减伤时，默认尸群可在 30 秒内把驾驶员从 100 HP 打到约 3 HP；75% 仍不足以让自然驾驶有可读的安全余量。最终把 `vehicle.driverDamageReduction` 收紧到 **0.85**，同路线驾驶中剩余 70.3 HP，仍保留被围困致死的风险。
- 实现：`config/game.json` 新增 `vehicle.driverDamageReduction=0.85`；`VehicleTuning` 增加字段；单机/服务器解析并钳制到 0..0.95。运行时 `ecs::VehicleOccupant{damageReduction}` 只在驾驶位存在，上车挂载、下车/载具消失/服务器释放座位时移除；不写存档、不复制、不新增协议。
- 减伤规则：`CombatSystem::ApplyDamage` 仅对爪子/咬击应用车辆防护，且与护甲减伤取最大值、上限 95%；火焰、钝击、调试伤害和 PvP 不吃该减伤。驾驶 HUD 显示 `防护 85%`，服务器上车日志/状态也显示防护；护甲磨损只在护甲本身产生减伤时触发，避免车辆防护白嫖磨损。
- 测试：新增 `M25-T4-V9 驾驶位对爪/咬获得车辆减伤，火焰/钝击不受影响` **1/1（9 assertions）**；`M25-T4*` 过滤 **12/12（140 assertions）**；T3 服务器上下车用例补 `VehicleOccupant` 挂载/移除断言。全量 Debug **736/736（132980 assertions）**、Release **736/736（132994 assertions）**、Python **88/88**；日志 `build/m25_t4_v9_targeted.log`、`build/m25_t4_v9_targeted_all.log`、`build/m25_t4_v9_debug.log`、`build/m25_t4_v9_release.log`、`build/m25_t4_v9_python.log`。
- 烟测：`build/m25_t4_v9_natural_smoke/`，Release 单机 1280×720、默认生命/燃料/车况；真实按键走到皮卡并按 Enter，沿村庄东侧驾驶到开阔地，默认游荡者 id 10 追击。日志有 `载具撞僵尸`、`撞击车况：速度 2.1，扣车况 1.6%，剩余 84%`、`载具撞杀：...+25 近战经验`、`驾驶员防护 85%`；截图 `v9_start.png`、`v9_near_vehicle.png`、`v9_driving.png`、`v9_target_near.png`、`v9_ram.png`、`v9_after_ram.png`。
- 平衡结论：85% 是当前“能开、会被咬、但不会一次尸群就秒杀”的折中；仍不做完全免疫、车辆护甲耐久或撞击后司机硬直保护。自然烟测中开阔地路径曾触发移动超时回退，说明车辆仍会被建筑/地形卡住；这属于后续刚体/寻路/正式地图工作。
- 已知限制：只保护驾驶位，不保护乘客/车外玩家；火焰、钝击与 PvP 不减伤；没有车辆受击/护甲耐久表现；自然跑仍是自动按键，不代表完整人类试玩；正式美术/音效和动物/玩家碾压仍未做。

### 6.10 M25-T4 V10：皮卡四向外观（2026-11-05）

- 范围：把 `crate_1` 木箱占位替换为程序化等距皮卡外观，并按车辆速度主轴选择四向帧；不改变碰撞、动力、载具状态、协议或存档；不做八向/轮胎转向、正式手绘、车身损伤分层和正式音效。
- 美术：`tools/gen_art_slice.py` 新增 `make_vehicle` 与 `vehicle_north/east/south/west` 四帧，画布 `160×128`，包含车身、驾驶室、开放货斗、玻璃、四轮、前灯/尾灯、锈斑和接地阴影；`SpriteScale` 从 1.7 改为 1.0，保持脚底锚点。
- 资源：`assets/asset_manifest.json` 新增 `object.vehicle_north/east/south/west`；`object.vehicle` 保留为 `vehicle_south` 别名，旧调用/旧测试不需要迁移。
- 纯逻辑：新增 `VehicleVisualDirection`、`VehicleVisualDirectionFor`、`VehicleVisualSprite`、`VehicleVisualLabel`。方向按世界速度 `|vx|/|vy|` 主轴判定，`switchRatio=1.2` 滞回，低于 0.0001 或非有限速度保持当前朝向；停车保留最后朝向。
- 接入：`WorldDemoState::UpdateVehicleVisual` 在 `EnsureVehicleSpawned` 后每帧执行，本地与网络客户端共用已复制的速度/heading 选择资源；单机 spawn/adopt 默认南向，网络目标切换时重置南向。只改 `SpriteRef.sprite`，不写存档、不复制、不新增命令。
- 测试：新增 `M25-T4-V10 VehicleVisual：四向主轴与切换滞回` **1/1（13 assertions）**；`M25-T4*` 过滤 **13/13（153 assertions）**；全量 Debug **737/737（132993 assertions）**、Release **737/737（133008 assertions）**、Python **89/89**；日志 `build/m25_t4_v10_targeted_final.log`、`build/m25_t4_v10_targeted_all_final.log`、`build/m25_t4_v10_debug_final.log`、`build/m25_t4_v10_release_final.log`、`build/m25_t4_v10_python_final.log`。
- 烟测：`build/m25_t4_v10_vehicle_visual_smoke/`，Release 单机 1280×720；默认出生点真实 WASD 走到皮卡，按 Enter 后依次向东/南/西/北行驶并截图。日志出现 `外观朝向：东/南/西/北`，驾驶中生命 96，8 张截图 `v10_start.png`、`v10_near_vehicle.png`、`v10_entered_south.png`、`v10_open_field.png`、`v10_east.png`、`v10_south.png`、`v10_west.png`、`v10_north.png`；脚本 `build/m25_t4_v10_vehicle_visual_smoke.ps1`（临时）。为专注外观关闭默认游荡者/动物，不替代自然试玩。
- 限制：当时四向而非八向（V12 已在 6.12 补八向），斜向/贴墙时仍可能按主轴翻向；外观尺寸大于 `0.65` 碰撞半径，视觉与碰撞体不完全一致；没有轮胎转向、车身损伤分层、正式手绘/音效；联机外观与本地共用代码，未单独做双客户端外观截图；到达 84,20 的自动航点可能因建筑碰撞超时，验收以四向日志/截图为准。

### 6.11 M25-T4 V11：低车况/归零烟雾（2026-11-06）

- 范围：车况 50% 及以下由表现层冒烟，50~25% 轻烟、25~0% 重烟、0% 报废级黑烟与火星；只做屏幕空间占位烟团，不改变耐久/动力/碰撞/协议/存档，不做真实粒子、起火/爆炸/焦黑贴图、软粒子或正式音效。
- 纯逻辑：`VehicleDamageSmokeLevelFor` 按 50/25/0% 阈值返回 `None/Light/Heavy/Critical`；`VehicleDamageSmokeInterval` 为 0.55/0.28/0.14s，`VehicleDamageSmokeLifetime` 为 1.5/1.9/2.4s；非有限车况返回无烟。
- 表现：`WorldDemoState` 用 `m_vehicleSmokePuffs` 保存世界位置、寿命、尺寸与漂移；`UpdateVehicleDamageSmoke` 每帧老化/补充烟团，`DrawVehicleDamageSmoke` 在车辆后绘制三层半透明矩形并随年龄上浮；报废级额外绘制少量橙色火星。烟源在车头前方，本地与网络客户端共用已复制的车况/位置，不写存档、不复制、不新增命令。
- 日志：烟级变化输出 `载具烟雾：车况 x%，等级 …（smoke level=n）`，便于烟测用 ASCII `smoke level=` 断言。
- 测试：新增 `M25-T4-V11 VehicleDamageSmoke：车况烟雾等级与间隔` **1/1（14 assertions）**；`M25-T4*` 过滤 **14/14（167 assertions）**；载具过滤 **29/29（378 assertions）**；全量 Debug **738/738（133007 assertions）**、Release **738/738（133021 assertions）**、Python **89/89**；日志 `build/m25_t4_v11_targeted_all_final.log`、`build/m25_t4_v11_debug_final.log`、`build/m25_t4_v11_release_final2.log`、`build/m25_t4_v11_python_final.log`。
- 烟测：`build/m25_t4_v11_condition_smoke/`，Release 1280×720；默认出生点走到皮卡，构造 1 个僵尸并撞击两次，车况 50% 时 `v11_smoke_light.png`、0% 时 `v11_smoke_critical.png`，另有近车/驾驶两张；日志两次 `撞击车况`，最终 `载具烟雾…smoke level=3`。默认游荡者/动物关闭，不替代自然试玩。
- 限制：烟团是屏幕空间矩形占位，非软粒子/体积烟（V13 已改为软粒子）；50% 轻烟在车旁/僵尸遮挡时较淡；没有焦黑车身、起火、爆炸、烟雾遮挡/风向与正式音效；联机外观/烟雾共用代码，未单独做双客户端截图。首跑 Release 全量曾在 `M23-F2b-T6` 动物剥取状态超时，单独重跑和二次全量均通过，与本切片改动无直接关系。

### 6.12 M25-T4 V12：八向皮卡外观（2026-11-06）

- 范围：在 V10 四向基础上补齐东南/西南/西北/东北四帧，按世界速度方向落入 45° 扇区选帧；只改表现，不改变碰撞、动力、耐久、协议或存档；不做轮胎转向、16 向、正式手绘/车身损伤、软粒子或正式音效。
- 美术：`tools/gen_art_slice.py` 的 `_vehicle_axes` 增加四个对角方向，生成 `vehicle_northeast/east/southeast/south/southwest/west/northwest/north` 八帧（160×128，脚底锚点不变）；manifest 新增 4 个 `object.vehicle_*` 逻辑名，`object.vehicle` 继续指向南向；图集扩至 188 帧。
- 纯逻辑：`VehicleVisualDirection` 扩展为 8 值；`VehicleVisualDirectionFor` 用 `atan2(vy, vx)` 映射到 45° 扇区，默认保留 8° 额外滞回（`kVehicleVisualDefaultHysteresisDegrees`），零速度/非有限速度保持当前方向；`VehicleVisualSprite/Label` 返回对应资源名与中文方位。
- 接入：`WorldDemoState::UpdateVehicleVisual` 继续按本地/网络复制的 heading 选择 `SpriteRef`，生成/绑定/网络目标切换时默认南向；不写存档、不复制、不新增命令。日志仍为 `外观朝向：…`，八向标签为东/东南/南/西南/西/西北/北/东北。
- 测试：`M25-T4-V12 VehicleVisual：八向扇区与切换滞回` **1/1（29 assertions）**；`M25-T4*` 过滤 **14/14（183 assertions）**；载具过滤 **29/29（394 assertions）**；全量 Debug **738/738（133024 assertions）**、Release **738/738（133037 assertions）**、Python **89/89**；日志 `build/m25_t4_v12_targeted.log`、`build/m25_t4_v12_targeted_all.log`、`build/m25_t4_v12_vehicle_all.log`、`build/m25_t4_v12_debug.log`、`build/m25_t4_v12_release.log`、`build/m25_t4_v12_python.log`。
- 烟测：`build/m25_t4_v12_vehicle_visual_smoke/`，Release 1280×720；探针把玩家放到开阔地皮卡旁（车辆偏移 13,-3，约 82.5,20.5），真实 Enter 上车后以八个短脉冲依次验证八向并截图；11 张 `v12_*.png`；日志完整出现八向 `外观朝向`。关闭 spawner 补充与动物，地图既有 27 个僵尸保留；玩家 100000 HP 仅防干扰，未使用 `Zombies.Spawn`；构造起点，不替代自然行走试玩。
- 限制：八向来自 45° 扇区，斜向帧仍可能呈端视/窄侧视；方向切换有 8° 滞回，极慢斜向转向/贴墙时可能短暂保留上一方向；外观尺寸仍大于 `0.65` 碰撞半径；无轮胎转向、16 向、车身损伤分层和正式音效；联机本地/网络共用选帧代码，未单独做双客户端外观截图（软粒子由 V13 补齐）。

### 6.13 M25-T4 V13：软粒子烟雾（2026-11-06）

- 范围：把 V11 的屏幕空间矩形烟团替换为程序化软粒子烟团，保留轻/重/报废三级与火星；只改表现，不改变耐久/动力/碰撞/协议/存档，不做体积烟/风向/遮挡、焦黑车身、起火/爆炸或正式音效。
- 纹理：`render::SpriteBatch` 初始化时生成独立 96×96 RGBA 径向 alpha 纹理（中心高 alpha、边缘渐隐、带轻微角度扰动）；不写入图集/manifest，不新增资源文件；纹理创建失败时保留 V11 矩形烟团回退。
- 纯逻辑：`VehicleSpike.h` 新增 `VehicleSmokeParticleScale` 与 `VehicleSmokeParticleAlpha`；烟团尺寸随寿命从 0.70× 长到 1.60×，alpha 峰值轻/重/报废为 0.75/0.85/0.95，寿命最后 35% 淡出；非有限参数返回 0。
- 表现：`WorldDemoState::DrawVehicleDamageSmoke` 每个烟团绘制两层软精灵（outer/core），报废级额外绘暖色软火星；位置、漂移、间隔、寿命沿用 V11；首次绘制输出 `载具烟雾：软粒子（程序纹理，puffs=n）`；本地/网络客户端共用已复制车况/位置，不写存档、不复制、不新增命令。
- 测试：新增 `M25-T4-V13 VehicleSoftSmoke：软粒子尺寸与淡出` **1/1（15 assertions）**；`M25-T4*` **15/15（198 assertions）**；载具过滤 **30/30（409 assertions）**；全量 Debug **739/739（133038 assertions）**、Release **739/739（133052 assertions）**、Python **89/89**；日志 `build/m25_t4_v13_targeted.log`、`build/m25_t4_v13_targeted_all.log`、`build/m25_t4_v13_vehicle_all.log`、`build/m25_t4_v13_debug.log`、`build/m25_t4_v13_release.log`、`build/m25_t4_v13_python.log`。
- 烟测：`build/m25_t4_v13_soft_smoke/`，Release 1280×720；构造起点皮卡旁，真实 Enter 上车；为稳定截取三档，烟测配置 `ramBaseDamage=1`、`ramConditionDamageBase/Max=0.25`、`ramCooldownSeconds=5`、`crashDamage=0`、`enterRange=4`，通过探针目标依次得到 75%→50%→25%→0%；每档下车后截图，避免停车继续碰撞。5 张 `v13_near_vehicle.png`、`v13_driving.png`、`v13_soft_light.png`、`v13_soft_heavy.png`、`v13_soft_critical.png`；日志含 `smoke level=1/2/3`、4 次 `撞击车况` 和 `载具烟雾：软粒子`。玩家 100000 HP，地图既有 27 个僵尸保留，探针额外 1 个目标；关闭 spawner 补充/动物；构造烟测，不替代自然试玩。
- 限制：软纹理为屏幕空间 billboard，不随相机旋转/参与光照，仍有少量点状缩放和固定生命淡出；烟源固定在车头前方，不按风向漂移；轻烟在浅色车身/遮挡下较淡；没有体积烟、烟雾遮挡、焦黑车身、起火/爆炸和正式音效；联机未单独做双客户端烟雾截图。

### 6.14 M25-T4 V14：载具喇叭与主动噪音（2026-11-06）

- 范围：驾驶中按 `H` 鸣笛；单机本地播放 `vehicle_horn` 并向 `OnSound` 发 24 格载具噪音，联机由服务器权威限频、广播 `WorldSound` 并触发 AI 听觉；HUD 追加 `H 喇叭`。不改变车况/动力/碰撞/存档/PlayerState，不做多车、喇叭蓄力/长按、车外声源、音量面板或正式音效。
- 键位与配置：`H` 在驾驶分支复用 `RepairStructure` 键（驾驶时优先喇叭，步行时仍是维修键）；`vehicle.hornNoiseRadius=24`、`hornCooldownSeconds=1.5`；车辆损坏或冷却中不响。
- 逻辑：`VehicleHornReady(condition, cooldown)` 统一校验有限值、车况 > 0、冷却 <= 0；单机与服务器各自扣冷却。联机新增 `CommandType::HonkHorn=44`，驾驶中命令白名单放行；服务器校验驾驶位/车况/冷却后广播 `vehicle_horn`（类型 `Vehicle`）并投递 `OnSound`，客户端不本地权威触发，收到 `WorldSound` 后播放。旧服务器会拒绝未知命令，不新增存档字段或复制字段。
- 资源：新增 `assets/audio/vehicle_horn.wav`（0.48s、22050Hz、单声道、双音占位），`test_audio` 的 null-device 用例加载并播放该 clip。
- 测试：新增 `M25-T4-V14 VehicleHorn：可用性与冷却` **1/1（7 assertions）**；`M25-T4*` **16/16（209 assertions）**；载具过滤 **31/31（416 assertions）**；全量 Debug **740/740（133064 assertions）**、Release **740/740（133078 assertions）**、Python **89/89**；日志 `build/m25_t4_v14_targeted.log`、`build/m25_t4_v14_targeted_all.log`、`build/m25_t4_v14_vehicle_all.log`、`build/m25_t4_v14_debug.log`、`build/m25_t4_v14_release.log`、`build/m25_t4_v14_python.log`。服务器集成用例扩展 `HonkHorn` 命令并断言收到 `vehicle_horn` 广播。
- 烟测：`build/m25_t4_v14_horn_smoke/`，Release 1280×720；探针把玩家放到开阔地皮卡旁（约 `82.5,20.5`），真实 Enter 上车，生成 1 个目标（约 `72.5,17.5`，初始距离 10.58 格）；按 `H` 后日志出现 `载具喇叭：吸引半径 24 格`，目标距离降到约 1 格并在车旁攻击玩家。1.5s 内再次按 `H` 只产生 1 条喇叭日志。4 张 `v14_near_vehicle.png`、`v14_driving_horn.png`、`v14_horn_cooldown.png`、`v14_zombie_attracted.png`；HUD 显示 `H 喇叭`。玩家 100000 HP、目标由探针生成、地图既有 27 个僵尸保留、关闭 spawner 补充/动物；`--audio.device=off`，构造烟测，不替代自然试听。
- 限制：`H` 复用维修键，未做独立键位面板项；无喇叭冷却 UI、无喇叭光环/车外声源和正式音频；单机 `OnSound` 由 SoundManager 默认音量组播放；只测 1 车 1 目标，不做多车/多人喇叭叠加；联机未单独做双客户端喇叭截图；烟测目标为探针生成且玩家高生命，不替代自然尸群体验。

### 6.15 M25-T4 V15：僵尸攻击载具与车况损耗（2026-11-06）

- 范围：僵尸对驾驶位的成功爪/咬命中同时磨损车况；保留 V9 的驾驶位 85% 爪/咬减伤，不新增协议/存档字段，不做僵尸主动把车辆当目标、车辆被摧毁/爆炸、乘客或动物攻击载具。单机与服务器权威同规则。
- 纯逻辑：`VehicleSpike.h` 新增 `VehicleZombieAttackConditionDamage` 与 `VehicleApplyZombieAttackConditionDamage`；默认每次成功攻击磨损 1.5%，`zombieAttackCooldownSeconds=0.75` 限制多只僵尸同 tick 叠加；非有限/已归零车况安全返回 0。
- 单机：`WorldDemoState::ApplyLocalVehicleZombieAttacks` 在本 tick `CombatSystem` 命中记录中找“目标是驾驶中的本地玩家、来源带 `AiState` 且非友方 NPC”的爪/咬命中，扣 `VehicleMotion.condition` 并显示 `载具受击：车况 X%`；不绕过现有伤害/减伤管线。
- 联机：服务器 `ApplyServerVehicleZombieAttacks` 用同一命中记录和冷却权威扣车况，写 `ecs::Vehicle.condition` 并回 `status`；驾驶员仍按 V9 承伤，客户端只显示复制状态，不本地扣车况。旧档兼容，`PlayerState`/ZLR1 不变。
- 配置：`config/game.json` 的 `vehicle` 段新增 `zombieAttackConditionDamage` 与 `zombieAttackCooldownSeconds`；单机/服务器启动时钳制。
- 测试：新增 `M25-T4-V15 VehicleZombieAttack：车况损耗与钳制` 与服务器集成用例 `M25-T4-V15 服务器载具受僵尸攻击扣车况`，合计 **2/2（25 assertions）**；`M25-T4*` **18/18（234 assertions）**；载具过滤 **32/32（426 assertions）**；全量 Debug **742/742（133089 assertions）**、Release **742/742（133103 assertions）**、Python **89/89**；日志 `build/m25_t4_v15_targeted.log`、`build/m25_t4_v15_all.log`、`build/m25_t4_v15_vehicle_all.log`、`build/m25_t4_v15_debug.log`、`build/m25_t4_v15_release.log`、`build/m25_t4_v15_python.log`；`targeted.log` 同时包含单元与服务器集成两个 V15 用例。
- 烟测：`build/m25_t4_v15_vehicle_attack_smoke/`，Release 1280×720；探针真实 Enter 上车后在车旁生成 1 个僵尸（约 `83.9,20.5`，距离约 0.97 格）；日志 `载具受僵尸攻击` 3 次，车况从 95%→90%→85%，HUD 同步显示 `车况 85%`；4 张 `v15_near_vehicle.png`、`v15_zombie_attack.png`、`v15_vehicle_hit.png`、`v15_condition_damaged.png`。玩家 100000 HP、地图既有 28 个僵尸保留、关闭 spawner 补充；烟测临时配置 5% 磨损/0.5s 冷却，构造烟测，不替代自然尸群试玩。
- 限制：只统计已在 `CombatSystem` 成功命中的 AI 攻击，不覆盖被闪避/未命中的僵尸；友方 NPC 与被动动物攻击不扣车况，危险动物攻击由 V20 补齐；只有一个固定每击损耗与全车共用一个冷却，不区分攻击部位、僵尸变体或车身位置；V15 当时无载具受击音效/火花/破损外观（V16 已补音效/火花，V17 已补屏幕空间破损外观，仍无车身凹陷贴图）；车况归零沿用 V6 动力衰减，不触发爆炸/报废动画。V20 将 `ApplyLocalVehicleZombieAttacks`/`ApplyServerVehicleZombieAttacks` 更名为 `ApplyLocalVehicleAttacks`/`ApplyServerVehicleAttacks`。

### 6.16 M25-T4 V16：载具受击音效与火花（2026-11-06）

- 范围：V15 僵尸攻击造成车况损耗时补短促受击音效与橙黄色火花，单机直接播放，联机由服务器广播 `WorldSound` 给客户端统一播放/显示；不改变伤害、车况数值、驾驶防护、协议或存档，不做破损外观、爆炸/报废演出、镜头震动、正式音频或自然尸群调参。
- 纯逻辑：`VehicleSpike.h` 新增 `VehicleHitFeedbackStrength`、`VehicleHitSparkCount`、`VehicleHitSparkLifetime`；以 5% 磨损为满强度，1.5% 默认磨损返回 5 个火花、寿命约 0.62s，5% 返回 8 个、寿命 0.90s，非法/零磨损返回 0。
- 单机：`ApplyLocalVehicleZombieAttacks` 扣车况后调用 `SpawnVehicleHitFeedback` 生成朝外扩散的火花，并用 `AudioEngine::Play3D` 播放 `vehicle_hit`；HUD 仍显示 `载具受击：车况 X%`，日志新增 `载具受击火花` 与 `载具受击音效`。
- 联机：服务器 `ApplyServerVehicleZombieAttacks` 在权威扣车况后广播 `WorldSound`（`Impact`、`vehicle_hit`、半径 18）；客户端 `ApplyNetworkWorldSounds` 播放音频并在事件位置生成火花，不从本地条件变化猜测受击。`PlayerState`/ZLR1/存档不变。
- 资源：新增程序合成 `assets/audio/vehicle_hit.wav`（0.26s、22050Hz、单声道、金属短击占位）；注册 `audio::kVehicleHit`，`test_audio` null-device 加载并播放。
- 测试：新增 `M25-T4-V16 VehicleHitFeedback：磨损强度/火花数量与寿命` **1/1（15 assertions）**；`M25-T4*` **19/19（251 assertions）**；载具过滤 **33/33（441 assertions）**；服务器 V15 集成用例追加“客户端收到 `vehicle_hit` WorldSound”断言；全量 Debug **743/743（133107 assertions）**、Release **743/743（133121 assertions）**、Python **89/89**；日志 `build/m25_t4_v16_targeted.log`、`build/m25_t4_v16_all.log`、`build/m25_t4_v16_vehicle_all.log`、`build/m25_t4_v16_debug.log`、`build/m25_t4_v16_release.log`、`build/m25_t4_v16_python.log`。
- 烟测：`build/m25_t4_v16_vehicle_hit_smoke/`，Release 1280×720、`--audio.device=null`；探针真实 Enter 上车并在车旁生成 1 个僵尸；5 次攻击使车况 95%→90%→85%→80%→75%，日志 5 次 `载具受击火花`（sparks=8，寿命 0.90s）与 5 次 `载具受击音效：vehicle_hit`；第二击截图可见车旁橙黄火花与 HUD `车况 90%`。4 张 `v16_near_vehicle.png`、`v16_zombie_attack.png`、`v16_vehicle_hit.png`、`v16_condition_damaged.png`；玩家 100000 HP、地图既有 28 个僵尸保留、关闭 spawner 补充；临时配置 5% 磨损/0.5s 冷却，构造烟测，不替代自然尸群试玩。null device 只验证音频句柄/资源/日志，不代表实际听感。
- 限制：火花是屏幕空间软粒子占位，不随相机旋转/参与光照；位置在车辆中心附近，不区分攻击部位；没有镜头震动、车身凹陷/破损贴图、金属碎片、正式混音与音量面板；联机只验证单客户端 WorldSound，未做双客户端受击截图；构造烟测不替代自然尸群围攻平衡。

### 6.17 M25-T4 V17：载具破损外观（2026-11-06）

- 范围：按车况 75%/50%/25% 三档叠加橙色划痕与深色/锈色凹陷，让玩家不看 HUD 也能感知车辆在变差；只改表现层，不改变耐久/动力/碰撞/协议/存档，不做真实车身贴图、分部件变形、爆炸/报废演出、镜头震动或正式音效。
- 纯逻辑：`VehicleSpike.h` 新增 `VehicleDamageVisualLevel`（None/Scuffed/Dented/Wrecked）、`VehicleDamageVisualLevelFor`、`VehicleDamageVisualMarkCount`、`VehicleDamageVisualLabel`；车况 >75% 无痕迹，50~75% 划痕（3 条），25~50% 凹陷（6 条），≤25% 破损（10 条）；非法/NaN 返回 None。
- 表现：`WorldDemoState::UpdateVehicleDamageVisual` 在车况跨档时写 `载具破损外观：车况 X%，等级 ...（marks=N）`；`DrawVehicleDamageVisual` 从 `m_items` 找到载具 DrawItem，在其后绘制深色描边+橙色划痕、软粒子深色凹陷与锈色芯；本地/网络客户端共用已复制车况，不写存档、不复制、不新增命令。伤害标记按车辆精灵顺序插入，避免覆盖后方物体。
- 测试：新增 `M25-T4-V17 VehicleDamageVisual：车况分级与痕迹数量` **1/1（16 assertions）**；`M25-T4*` **20/20（267 assertions）**；载具过滤 **34/34（457 assertions）**；全量 Debug **744/744（133123 assertions）**、Release **744/744（133137 assertions）**、Python **89/89**；日志 `build/m25_t4_v17_targeted.log`、`build/m25_t4_v17_all.log`、`build/m25_t4_v17_vehicle_all.log`、`build/m25_t4_v17_debug.log`、`build/m25_t4_v17_release.log`、`build/m25_t4_v17_python.log`。
- 烟测：`build/m25_t4_v17_vehicle_damage_smoke/`，Release 1280×720、`--audio.device=null`；探针真实 Enter 上车并在车旁生成 1 个僵尸，临时每击 `25%` 车况磨损/0.5s 冷却，4 次攻击使车况 100%→75%→50%→25%→0%；日志确认 75% 划痕（marks=3）、50% 凹陷（marks=6）、25% 破损（marks=10）与 0% 报废烟雾；6 张截图 `v17_vehicle_pristine.png`、`v17_vehicle_attack_start.png`、`v17_vehicle_scuffed.png`、`v17_vehicle_dented.png`、`v17_vehicle_wrecked.png`、`v17_vehicle_zero.png`；`game.err.log` 为空。构造烟测，不替代自然尸群围攻平衡。
- 限制：划痕/凹陷是屏幕空间绘制，不随相机旋转/参与光照，不区分攻击部位与八向车身；25%/0% 共用“破损”档，0% 主要靠 V11/V13 报废烟雾区分；没有真实凹坑贴图、裂纹玻璃、金属碎片、爆炸或正式音效；联机未单独做双客户端破损截图；构造烟测，不替代自然尸群试玩。

### 6.18 M25-T4 V18：载具受击镜头反馈与金属碎片（2026-11-06）

- 范围：僵尸攻击扣车况、载具撞僵尸扣车况、撞墙造成车况损失时，本地驾驶客户端叠加短促世界视图震动并生成金属碎片；只改表现层，不改变伤害、车况、动力、碰撞、协议或存档，不做真实凹坑贴图、玻璃裂纹、爆炸/报废演出或正式音效。
- 纯逻辑：`VehicleSpike.h` 新增 `VehicleScreenShake`、`VehicleHitShakeStrength`、`VehicleCrashShakeStrength`、`VehicleShakeDuration`、`VehicleScreenShakeOffset`、`VehicleMetalDebrisCount`、`VehicleMetalDebrisLifetime`；受击强度沿用 5% 磨损满强度，撞墙按 3~10 格/秒速度映射 0.25~1.0，震动时长 0.18~0.36s、最大偏移 5px，碎片 2~6 片、寿命 0.45~0.90s；非法/零强度安全返回空。
- 单机：`SpawnVehicleHitFeedback` 在既有火花/音效路径上生成金属碎片，并在驾驶中触发镜头震动；`ApplyLocalVehicleRam` 的车况磨损与 `UpdateVehicle` 的 `vehicleStep.damaged` 碰撞路径复用同一反馈；`DrawWorld` 只对世界视图矩阵施加震动偏移，HUD/UI 不抖；碎片在车辆火花之后绘制、按寿命上浮淡出；不写存档、不复制、不新增命令。
- 联机：服务器仍只复制车况，客户端收到既有 `vehicle_hit` `WorldSound` 时生成碎片；仅本地驾驶客户端触发镜头震动，非驾驶观察者不震屏。`PlayerState`/ZLR1/协议/存档不变。
- 测试：新增 `M25-T4-V18 VehicleImpactFeedback：镜头震动与金属碎片` **1/1（28 assertions）**；`M25-T4*` **21/21（295 assertions）**；载具过滤 **35/35（485 assertions）**；全量 Debug **745/745（133151 assertions）**、Release **745/745（133165 assertions）**、Python **89/89**；日志 `build/m25_t4_v18_targeted.log`、`build/m25_t4_v18_all.log`、`build/m25_t4_v18_vehicle_all.log`、`build/m25_t4_v18_debug.log`、`build/m25_t4_v18_release.log`、`build/m25_t4_v18_python.log`。
- 烟测：`build/m25_t4_v18_vehicle_impact_smoke/`，Release 1280×720、`--audio.device=null`；探针真实 Enter 上车并在车旁生成 1 个僵尸，临时每击 25% 车况磨损/0.5s 冷却，4 次攻击使 100%→75%→50%→25%→0%；日志 4 次 `载具金属碎片：强度 1.00，pieces=6，寿命 0.90s` 与 4 次 `载具镜头震动：强度 1.00，时长 0.36s`，并保留 `marks=3/6/10` 与 0% 报废烟雾；7 张截图 `v18_vehicle_pristine.png`、`v18_vehicle_attack_start.png`、`v18_impact_debris.png`、`v18_vehicle_scuffed.png`、`v18_vehicle_dented.png`、`v18_vehicle_wrecked.png`、`v18_vehicle_zero.png`；`game.err.log` 为空；构造烟测，不替代自然尸群围攻平衡。
- 限制：震动是短暂世界视图偏移，不改变相机中心/鼠标拾取/调试叠层，截图无法完整表达体感；碎片是屏幕空间矩形占位，不随相机旋转/参与光照，不区分攻击部位与八向车身，也没有玻璃裂纹、凹坑贴图、爆炸或正式音效；撞墙反馈复用受击火花/撞击音，未单独做撞墙烟测；联机仅验证既有 WorldSound 路径，未单独做双客户端震屏截图。

### 6.19 M25-T4 V19：载具撞击动物（2026-11-06）

- 范围：载具驾驶中撞击 `Animal` 实体，复用现有速度阈值、伤害、击退、硬直与车况磨损；单机与服务器权威同规则，动物死亡继续走既有 `DeathSystem -> AnimalCorpse` 与剥取管线。不做玩家碾压、动物专属伤害/经验/任务归属、分部件撞击、血液/尸体物理或正式音效。
- 纯逻辑：`VehicleRamTargetKind` 与 `VehicleRamTargetKindFor` 统一筛选僵尸/动物；忽略玩家、友方 NPC、已死亡/NaN 生命、异层目标；同一实体同时带 `Animal` 与 `AiState` 时优先按动物处理，避免单机/服务器双路径重复结算。
- 单机：`ApplyLocalVehicleRam` 先处理 `AiState` 目标，再处理 `Animal + Position + Health`；动物受到与僵尸相同的 `Blunt` 伤害/击退/硬直，并触发 `HitReaction` 让被动动物逃跑、危险动物反击。命中日志为 `载具撞动物`/`载具撞杀动物`；动物击杀不发放 `Melee` 经验（与现有近战猎杀一致）。车况磨损、`vehicle_ram` 音效、V16 火花与 V18 镜头震动/金属碎片复用既有路径。
- 联机：服务器 `ApplyServerVehicleRam` 权威处理动物并写 `Health`；客户端通过既有 ZLR1 `Animal`/`Health`/`AnimalCorpse` 复制看到受伤、死亡与尸体。不新增命令、复制字段、`PlayerState`、协议或存档 schema；服务器日志为 `服务器载具撞动物`/`服务器载具撞杀动物`。
- 测试：新增 `M25-T4-V19 VehicleRamTargetKind：僵尸/动物目标筛选` **1/1（9 assertions）**；扩展 `M25-T4 服务器载具撞击/噪音与车灯复制` 用例，构造 `deer` 动物并断言服务器扣血、客户端 `Animal`/`Health` 复制（新增 7 assertions）；`M25-T4*` **22/22（311 assertions）**；载具过滤 **36/36（494 assertions）**；全量 Debug **746/746（133167 assertions）**、Release **746/746（133181 assertions）**、Python **89/89**；日志 `build/m25_t4_v19_targeted.log`、`build/m25_t4_v19_all.log`、`build/m25_t4_v19_vehicle_all.log`、`build/m25_t4_v19_debug.log`、`build/m25_t4_v19_release.log`、`build/m25_t4_v19_python.log`。
- 烟测：`build/m25_t4_v19_animal_smoke/`，Release 1280×720、`--audio.device=null`；临时 `animals.json` 加载 `smoke_deer`（20 HP、低感知/低移动）、`vehicle.ramCooldownSeconds=0.5`，24 只动物；车辆生成于 (69,7)，真实 Enter 上车后 W+D 行驶约 3.6s，命中 2 只并撞杀 2 只；日志 `载具撞动物`/`载具撞杀动物` 各 2 次，车况 100%→95%→90%，另有 2 次撞击车况、金属碎片、镜头震动与受击火花；5 张截图 `v19_ready.png`、`v19_driving.png`、`v19_animal_hit.png`、`v19_animal_corpse.png`、`v19_animal_after.png`；`game.err.log` 为空；构造烟测，不替代自然动物分布/试玩。
- 限制：不支持玩家/乘客被撞；动物与僵尸共用整车单值伤害/磨损，没有按物种质量、撞击部位或速度阈值分级；动物击杀不发经验、不单独归属任务；烟测使用低感知 `smoke_deer` 自定义物种与传送起点，未做自然动物分布、双客户端动物尸体/剥取截图或正式血液/尸体物理；动物尸体仍是占位视觉。

### 6.20 M25-T4 V20：危险动物反击载具（2026-11-06）

- 范围：危险动物（`ecs::AnimalKind::Dangerous`）成功攻击驾驶位时，除保留原有对驾驶员的伤害外，同时磨损 `ecs::Vehicle.condition` 并复用 V16/V18 受击反馈；单机与服务器权威同规则。不做被动/友方动物攻击、玩家/乘客被撞、动物把车辆当主动目标、车辆爆炸/报废演出或物种专属伤害/经验/任务。
- 纯逻辑：`VehicleSpike.h` 新增 `VehicleAttackerKind` 与 `VehicleAttackerKindFor(hasAiState, hasAnimal, animalDangerous, isPlayer, isFriendly)`；危险动物优先于僵尸，玩家/友方/被动动物/未知来源返回 `None`。
- 攻击记录：动物 AI 直接调用 `CombatSystem::ApplyDamage`，不会进入玩家近战用的 `CombatSystem::HitInfo` 列表；`AnimalSystem` 新增 `AttackInfo`/`GetAttacks()`，每 tick 记录成功命中的攻击者、目标、物种与伤害，单机 `WorldDemoState` 和服务器 `ServerHost` 读取本 tick 记录。
- 单机：`ApplyLocalVehicleAttacks`（V15 的 `ApplyLocalVehicleZombieAttacks` 更名）先检查动物攻击记录，再检查 `CombatSystem` 命中；目标是驾驶中的本地玩家且来源为危险动物/僵尸时扣 1.5% 车况，0.75s 全车冷却，显示 `载具受击：车况 X%`，并生成 V16 火花/音效与 V18 镜头震动/金属碎片；动物分支日志为 `载具受动物攻击`。
- 联机：服务器 `ApplyServerVehicleAttacks`（V15 的 `ApplyServerVehicleZombieAttacks` 更名）用同一 `AnimalSystem::GetAttacks()` 与冷却扣 `ecs::Vehicle.condition`，回 `status` 并广播既有 `vehicle_hit` `WorldSound`；不新增命令、复制字段、`PlayerState`、协议或存档 schema，客户端沿用复制状态 + `WorldSound`。
- 测试：新增 `M25-T4-V20 VehicleAttackerKind：僵尸/危险动物攻击来源筛选` **1/1（9 assertions）**；扩展 `M25-T4-V15/V20 服务器载具受僵尸/危险动物攻击扣车况`，构造 `boar` 并断言服务器扣血、车况下降与客户端 `vehicle_hit`（新增 5 assertions）；`M25-T4*` **23/23（325 assertions）**；载具过滤 **37/37（503 assertions）**；全量 Debug **747/747（133181 assertions）**、Release **747/747（133195 assertions）**、Python **89/89**；日志 `build/m25_t4_v20_targeted.log`、`build/m25_t4_v20_all.log`、`build/m25_t4_v20_vehicle_all.log`、`build/m25_t4_v20_debug.log`、`build/m25_t4_v20_release.log`、`build/m25_t4_v20_python.log`。
- 烟测：`build/m25_t4_v20_animal_attack_smoke/`，Release 1280×720、`--audio.device=null`；临时 `animals.json` 加载 4 只 `smoke_boar`（200 HP、危险、1 伤害、1.0s 攻击冷却），玩家 100000 HP，真实 Enter 上车后危险动物靠近攻击；日志 5 次 `载具受动物攻击`，车况 100%→93%，5 次 `载具受击火花`/`载具受击音效`，并出现 `载具镜头震动`/`载具金属碎片`；4 张截图 `v20_ready.png`、`v20_driving.png`、`v20_animal_attack.png`、`v20_vehicle_damaged.png`；`game.err.log` 为空；构造烟测，不替代自然动物围攻/平衡试玩。
- 限制：只支持危险动物攻击驾驶位，不覆盖被动动物、友方动物、乘客/车外玩家；动物攻击仍按原 `Blunt` 伤害结算给驾驶员（V9 仅对僵尸爪/咬提供 85% 减伤），不做动物专属车损/击退/部位；车况磨损复用 V15 固定 1.5%/0.75s，不按物种质量或攻击伤害缩放；只有 `AnimalSystem` 的攻击记录才触发，其他直接调用 `CombatSystem` 的动物攻击不会；服务器测试仅单客户端，未做双客户端震屏/火花截图或正式动物围攻自然试玩；联机车外玩家 PvP 由 V21 补齐，乘客位由 V22 补齐，但动物攻击仍只结算驾驶位。

### 6.21 M25-T4 V21：联机 PvP 载具撞玩家（2026-11-06）

- 范围：仅在 `combat.pvp=true` 的联机服务器，载具撞击驾驶员以外的存活车外玩家，服务器权威结算攻击者/伤害/击退/硬直；不改变僵尸/动物、单机、乘客/多座位、友方 NPC、安全区、死亡掉落或击杀归属。
- 纯逻辑：`VehicleRamTargetKind` 新增 `Player`；`VehicleRamTargetKindFor(..., bool allowPlayer = false)` 仅在目标是玩家且允许 PvP 时返回 `Player`，友方/死亡/NaN 生命/异层目标仍返回 `None`；默认 `false` 保持单机与旧调用行为。
- 联机：`ApplyServerVehicleRam` 遍历 `PlayerTag + Position + Health`，跳过驾驶员、`VehicleOccupant` 与 `NoCombatTarget`，并用 `m_pvpEnabled` 控制是否参与；命中后沿用载具撞击伤害/击退/硬直，伤害来源写驾驶员 StableId，日志为 `服务器载具撞玩家`/`服务器载具撞杀玩家`；不新增命令、复制字段、`PlayerState`、协议或存档 schema。
- 测试：新增 `M25-T4-V21 VehicleRamTargetKind：PvP 玩家筛选` **1/1（10 assertions）**；新增双客户端服务器用例 `M25-T4-V21 服务器载具撞击玩家` 与 `M25-T4-V21 服务器载具撞击玩家受 PvP 开关限制` **2/2（49 assertions）**；`M25-T4*` **26/26（384 assertions）**；载具过滤 **38/38（513 assertions）**；全量 Debug **750/750（133240 assertions）**、Release **750/750（133254 assertions）**、Python **89/89**；日志 `build/m25_t4_v21_targeted.log`、`build/m25_t4_v21_all.log`、`build/m25_t4_v21_vehicle_all.log`、`build/m25_t4_v21_debug.log`、`build/m25_t4_v21_release.log`、`build/m25_t4_v21_python.log`。
- 烟测：`build/m25_t4_v21_pvp_smoke/`，Release 1280×720，双客户端 A/B + 无窗口服务器，`--combat.pvp=true`；A 上车后撞击 B，服务器日志 `服务器载具撞玩家`（伤害 8、速度 5.3），B 生命 100→92、车况 100%→99%；6 张截图 `v21_a_ready.png`、`v21_b_target.png`、`v21_a_driving.png`、`v21_b_before_hit.png`、`v21_a_after_hit.png`、`v21_b_after_hit.png`；三份 stderr 为空；构造双客户端烟测，不替代自然 PvP 试玩。
- 限制：仅一个驾驶员/一个车外玩家，不覆盖乘客、致死/击杀归属、安全区保护、双客户端震屏或正式 PvP 平衡；PvP 关闭时无玩家目标伤害；构造烟测的车辆/路线不是自然遭遇。乘客座位由 V22 补齐，但乘客不会被撞、多乘客仍不支持。

### 6.22 M25-T4 V22：联机载具乘客位（2026-11-06）

- 范围：在现有单车/单驾驶位基础上增加**一个联机乘客位**；驾驶员与乘客都随服务器权威车辆位置移动，乘客不能驾驶、鸣笛或开关车灯；不做多乘客、NPC 乘客、乘客专属伤害/经验/任务、正式 PvP 平衡或乘客引擎音。
- 纯逻辑：`VehicleSpike.h` 新增 `VehicleSeat{None,Driver,Passenger}`、`VehiclePassengerSeatAvailable` 与 `VehicleSeatForEnter(driver, passenger)`；空驾驶位优先给驾驶员，驾驶位已占用且乘客位空时给乘客，两个座位都占用返回 `None`。
- 协议：`CommandType::EnterVehiclePassenger=45` 显式请求乘客位；`EnterVehicle=40` 仍只请求驾驶位，旧行为不变。`PlayerState` 升 **v18**，`PlayerVehicleState` 增加 `passenger`，与 `driving` 互斥；ZLR1 `VHCL` 扩为 **25B**（fuel/condition/driver/passenger/headlights），客户端按 passenger id 隐藏乘客精灵。
- 服务器：`ServerVehicleRuntime.passengerStableId` 与 `ecs::VehiclePassenger` 保存运行时座位；乘客上下车、跟随、死亡/断线/接管释放复用 `SyncVehiclePassenger`/`ReleaseServerVehicleSeat`。驾驶中 `Move`/`ToggleHeadlights`/`HonkHorn` 仍只允许驾驶员；乘客只允许 `ExitVehicle`/快捷栏/聊天菜单。驾驶员下车后车辆停止但乘客保留，直到乘客下车或失效；PvP 撞击和车辆攻击都排除 `VehicleOccupant`。
- 客户端：`m_riding` 区分驾驶/乘坐；网络 Enter 在驾驶位已占用时自动发 `EnterVehiclePassenger`；乘客 HUD 显示 `乘客中：Enter 下车`，输入层阻止驾驶/攻击并隐藏本地精灵；记录被隐藏精灵并在下车后恢复，修复驾驶员/乘客精灵恢复路径。单机仍只有驾驶位。
- 测试：`M25-T4-V22 VehicleSeat：驾驶员/乘客进入选择` **1/1（7 assertions）**；`M25-T4-V22 玩家状态协议：乘客位往返与座位互斥` **1/1（9 assertions）**；`M25-T4-V22 双客户端载具乘客位：上车、跟随、限制与下车` **1/1（53 assertions；V23 追加接管段后当前 78）**；V22 目标 **3/3（69 assertions）**，`M25-T4*` **29/29（453 assertions）**，载具过滤 **39/39（520 assertions）**；全量 Debug **753/753（133309 assertions）**、Release **753/753（133323 assertions）**、Python **89/89**；日志 `build/m25_t4_v22_targeted.log`、`build/m25_t4_v22_all.log`、`build/m25_t4_v22_vehicle_all.log`、`build/m25_t4_v22_debug.log`、`build/m25_t4_v22_release.log`、`build/m25_t4_v22_python.log`。
- 烟测：`build/m25_t4_v22_passenger_smoke/`，Release 1280×720，双客户端 A/B + 无窗口服务器；A 真实 Enter 驾驶，B 走到车旁后 Enter 自动进入乘客位；A 短距离驾驶，B 位置跟随 **1.19 格**；B 再 Enter 下车。服务器日志 `服务器上乘客位`/`服务器乘客下车`/`乘客位`，车辆 stableId=6、驾驶员 ...5809、乘客 ...5810；9 张截图 `v22_a_ready.png`、`v22_b_near.png`、`v22_a_driver.png`、`v22_b_before_board.png`、`v22_b_passenger.png`、`v22_a_with_passenger.png`、`v22_a_driving.png`、`v22_b_riding.png`、`v22_b_after_exit.png`；三份 stderr 为空；构造双客户端烟测，不替代自然多人试玩。
- 限制：只支持一个驾驶员 + 一个乘客，不支持多乘客/乘客换座/乘客驾驶；乘客不播放本地引擎音，不做乘客受击/死亡/经验/任务或安全区；ZLR1/PlayerState 版本已提升，旧客户端不能加入；构造烟测的车辆/路线不是自然遭遇。

### 6.23 M25-T4 V23：乘客接管驾驶位（2026-11-06）

- 范围：联机驾驶员下车后，原乘客可以在空驾驶位直接接管，不必先下车绕行；接管后沿用既有驾驶输入、预测、HUD、引擎音与车辆状态。不做驾驶中换座、把驾驶位转移给车外玩家、多乘客、乘客专属权限或座位存档。
- 纯逻辑：`VehicleSpike.h` 新增 `VehicleCanTakeDriverSeat(driverStableId, passengerStableId, requesterStableId)`；仅请求者非 0、驾驶位为空、且请求者正是当前乘客时返回 `true`。
- 协议/服务器：新增 `CommandType::TakeDriverSeat=46`；服务器仅允许乘客位请求，校验会话仍在载具、驾驶位为空、请求者就是乘客；成功后清空 `passengerStableId`、把请求者写入 `driverStableId`，清空车辆输入/速度，调用 `SyncServerVehicleToEcs`/`SyncVehicleDriver` 并回 status。`PlayerState`/ZLR1/存档 schema 不变；驾驶位已占用时拒绝。
- 客户端：乘客 HUD 在驾驶位为空时显示 `F 接管驾驶`；乘客输入分支按 F 发送命令；接管后服务器回传 `driving=true`，驾驶输入/HUD/预测/音频自动走既有路径。驾驶位仍占用时 HUD 不显示、F 不发命令。
- 测试：新增 `M25-T4-V23 VehicleDriverSeat：空驾驶位可由乘客接管` **1/1（6 assertions）**；扩展 V22 双客户端用例为 `M25-T4-V22/V23`，新增接管、复制清空、状态与再次驾驶断言，现 **3/3（78 assertions）**；`M25-T4*` **30/30（468 assertions）**，载具过滤 **40/40（526 assertions）**；全量 Debug **754/754（133338 assertions）**、Release **754/754（133352 assertions）**、Python **89/89**；日志 `build/m25_t4_v23_targeted.log`、`build/m25_t4_v23_v22.log`、`build/m25_t4_v23_all.log`、`build/m25_t4_v23_vehicle_all.log`、`build/m25_t4_v23_debug.log`、`build/m25_t4_v23_release.log`、`build/m25_t4_v23_python.log`。
- 烟测：`build/m25_t4_v23_seat_switch_smoke/`，Release 1280×720，双客户端 A/B + 无窗口服务器；A 真实 Enter 驾驶、B Enter 进入乘客位；A 短距离驾驶后乘客跟随 **1.19 格**；A Enter 下车；B 按 F 接管空驾驶位，随后真实按键驾驶移动 **2.42 格**，B 再 Enter 下车。服务器日志 `服务器上车`、`服务器上乘客位`、`服务器下车`（A）、`服务器接管驾驶位`（B）、`服务器下车`（B）；14 张截图，含 `v23_b_driver_seat.png`、`v23_a_after_takeover.png`、`v23_b_driving.png`；三份 stderr 为空；构造双客户端烟测，不替代自然多人试玩。
- 限制：只支持驾驶员下车后的空位接管；不支持驾驶中换座、驾驶员主动让座、多乘客、座位持久化、乘客受击/死亡/经验/任务或安全区；接管期间车辆已停，若车辆损坏/无燃料仍沿用既有驾驶限制；构造烟测的车辆/路线不是自然遭遇。

### 6.24 M25-T4 V24：驾驶员停车让座（2026-11-07）

- 范围：联机车辆停稳后，驾驶员可把驾驶位直接交给当前乘客；原驾驶员变为乘客，不必两人都下车。仅支持单车单驾驶员+单乘客；不做驾驶中换座、多乘客、座位持久化或让座给车外玩家。
- 纯逻辑：`VehicleSpike.h` 新增 `VehicleCanGiveDriverSeat(driver, passenger, requester, speed, maxSpeed)`；仅请求者是当前驾驶员、当前有乘客、车辆速度非负且不超过 `maxSpeed` 时返回 `true`。默认上限 `kVehicleSeatHandoffMaxSpeed=0.10`，配置 `vehicle.seatHandoffMaxSpeed` 可调。
- 协议/服务器：新增 `CommandType::GiveDriverSeat=47`；服务器驾驶位白名单放行，校验座位、乘客存活与车辆停稳；成功后交换 `driverStableId`/`passengerStableId`，清空车辆输入和速度，`SyncServerVehicleToEcs` 后分别同步新驾驶员/旧驾驶员，双方 status 与状态标记更新。`PlayerState`/ZLR1/存档 schema 不变；乘客或无乘客时拒绝。
- 客户端：驾驶员 HUD 在停车且存在乘客时显示 `F 让座`；驾驶输入分支按 F 发送命令。乘客端沿用 V23 的 `F 接管驾驶` 与 Enter 下车逻辑；让座后客户端由服务器 `driving/passenger` 状态自动切换输入、HUD、引擎音与隐藏精灵。
- 测试：新增 `M25-T4-V24 VehicleDriverHandoff：停车让座条件` **1/1（11 assertions）**；扩展 V22/V23 双客户端用例为 `M25-T4-V22/V23/V24`，新增乘客回座、让座、状态/复制、旧驾驶员越权与再次下车断言，现 **1/1（74 assertions）**；`M25-T4*` **31/31（493 assertions）**，载具过滤 **41/41（537 assertions）**；全量 Debug **755/755（133391 assertions）**、Release **755/755（133405 assertions）**、Python **89/89**；日志 `build/m25_t4_v24_targeted.log`、`build/m25_t4_v24_v22v23v24.log`、`build/m25_t4_v24_all.log`、`build/m25_t4_v24_vehicle_all.log`、`build/m25_t4_v24_debug.log`、`build/m25_t4_v24_release.log`、`build/m25_t4_v24_python.log`、`build/m25_t4_v24_net.log`。
- 烟测：`build/m25_t4_v24_seat_handoff_smoke/`，Release 1280×720，双客户端 A/B + 无窗口服务器；A 真实 Enter 驾驶、B Enter 上乘客位；A 停车后按 F 让座，B 成为驾驶员并真实按键驾驶移动 **2.42 格**，A 作为乘客跟随 **1.10 格**；B Enter 下车，A 保留乘客位。服务器日志 `服务器上车`、`服务器上乘客位`、`服务器让座`、`服务器下车`；14 张截图，三份 stderr 为空；结果 `m25-t4-v24-seat-handoff-smoke-pass follow=1.1 handoff=2.42`。
- 限制：只支持停稳后的驾驶位让座；不支持驾驶中换座、多乘客、让座给车外玩家、座位持久化、乘客受击/死亡/经验/任务或安全区；让座不改变车辆燃料/车况/车灯/后备箱；构造烟测不替代自然多人试玩。

### 6.25 M25-T4 V25：乘客车内背包与后备箱（2026-11-07）

- 范围：联机乘客在车内按 `I` 打开背包/车辆后备箱，通过既有服务器权威 `InventoryAction` 存取、整理或使用物品；驾驶员仍在驾驶中打开背包的限制不变。不新增命令、复制字段、`PlayerState`/ZLR1/存档 schema，不做多乘客、物品所有权、座位持久化或车内专属动作。
- 客户端：乘客输入分支不再强制关闭背包，打开后本地 `FindNearbyStorage` 将同位置的车辆识别为后备箱；乘客 HUD 增加 `I 背包/后备箱`。背包打开时乘客输入不再触发 F 接管驾驶或 Enter 下车；关闭背包后恢复原接管/下车逻辑。
- 服务器：`ExecuteSessionCommand` 的乘客白名单放行 `InventoryAction`，仍拒绝驾驶输入、攻击、建造、交易等动作；`PlayerInventoryService` 继续按距离、容量、堆数、结构所有权校验。车辆没有 `StructureOwner`，因此后备箱按 T3 约定共享；驾驶员驾驶中发送 `InventoryAction` 仍会被拒绝。
- 测试：扩展双客户端用例为 `M25-T4-V22/V23/V24/V25`，新增驾驶员取后备箱被拒、乘客 `TakeAll` 将 2 个绷带从车辆后备箱取入背包、状态断言，现 **1/1（80 assertions）**；`M25-T4*` **31/31（499 assertions）**，载具过滤 **41/41（537 assertions）**；全量 Debug **755/755（133397 assertions）**、Release **755/755（133411 assertions）**、Python **89/89**；日志 `build/m25_t4_v25_targeted.log`、`build/m25_t4_v25_all.log`、`build/m25_t4_v25_all_release.log`、`build/m25_t4_v25_vehicle.log`、`build/m25_t4_v25_debug.log`、`build/m25_t4_v25_release.log`、`build/m25_t4_v25_python.log`、`build/m25_t4_v25_net.log`、`build/m25_t4_v25_smoke.out`。
- 烟测：`build/m25_t4_v25_passenger_inventory_smoke/`，Release 1280×720，双客户端 A/B + 无窗口服务器；A 真实 Enter 驾驶、B Enter 上乘客位；B 按 `I` 打开背包，探针记录 `inv=1 title=后备箱：皮卡（0/40 kg）（空）`，关闭后按 Enter 下车；服务器日志 `服务器上车`、`服务器上乘客位`、`服务器乘客下车`。11 张 1280 截图，三份 stderr 为空；结果 `m25-t4-v25-passenger-inventory-smoke-pass title=后备箱`。
- 限制：只支持单乘客；驾驶员仍不能在驾驶中打开背包；烟测只验证打开/关闭后备箱，物品存取由双客户端集成测试覆盖，未做真实鼠标拖拽、多乘客、物品所有权或移动中后备箱自然体验；构造烟测不替代自然多人试玩。

### 6.26 M25-T4 V26：乘客快捷使用与装备（2026-11-07）

- 范围：联机乘客在车内可直接用 `E` 使用背包中第一件可用消耗品、用 `R` 切换主手武器、用 `1–5` 选择快捷栏，不需要先打开背包；驾驶员仍不能在驾驶中打开背包，也不获得快捷使用权限。不新增命令、复制字段、`PlayerState`/ZLR1/存档 schema，不做攻击/换弹/建造、车内专属 UI、多乘客或物品所有权。
- 客户端：乘客输入分支在背包关闭时处理 `1–5`、`E`、`R`，分别发送既有 `SelectHotbar`、`UseBag`、`EquipBag`；背包打开时仍由背包 UI 处理，避免与 `F` 接管/Enter 下车冲突。乘客 HUD 增加 `E 使用 · R 装备 · 1-5 快捷栏`，驾驶员 HUD 不变。
- 服务器：乘客白名单在既有 `ExitVehicle`/`SelectHotbar`/`TakeDriverSeat`/`InventoryAction` 之外放行 `UseBag`/`EquipBag`，仍拒绝驾驶输入、攻击、建造、交易等动作；驾驶员驾驶中 `UseBag`/`EquipBag` 继续被拒绝，背包 `InventoryAction` 边界也不变。
- 测试：扩展双客户端用例为 `M25-T4-V22/V23/V24/V25/V26`，新增驾驶员 `UseBag` 被拒、乘客 `UseBag` 消耗罐头豆子（饥饿 1.00→0.65、状态含“使用”）、乘客 `EquipBag` 装备 `kitchen_knife` 断言，现 **1/1（89 assertions）**；`M25-T4*` **31/31（508 assertions）**，载具过滤 **41/41（537 assertions）**；全量 Debug **755/755（133406 assertions）**、Release **755/755（133420 assertions）**、Python **89/89**；日志 `build/m25_t4_v26_targeted.log`、`build/m25_t4_v26_all.log`、`build/m25_t4_v26_all_release.log`、`build/m25_t4_v26_vehicle.log`、`build/m25_t4_v26_command.log`、`build/m25_t4_v26_debug.log`、`build/m25_t4_v26_debug_rerun.log`、`build/m25_t4_v26_release.log`、`build/m25_t4_v26_python.log`。
- 烟测：`build/m25_t4_v26_quick_use_smoke/`，Release 1280×720，双客户端 A/B + 无窗口服务器；B 首连接后拾取出生点旁绷带（`stacks=1 items=2`），A 真实 Enter 驾驶、B Enter 上乘客位；B 在乘客位按 `E` 使用消耗品，探针从 `items=2` 变为 `items=1`，服务器日志 `服务器上车`、`服务器上乘客位`、`服务器乘客下车`；10 张 1280 截图，三份 stderr 为空；结果 `m25-t4-v26-quick-use-smoke-pass quickuse=1`。
- 限制：只支持单乘客；驾驶员仍不能打开背包或使用快捷栏；烟测使用出生点演示地面绷带，只验证 `E` 快捷使用，`R` 装备由双客户端集成测试覆盖；自动化数字键注入未稳定触发 `1–5` 选择，快捷栏选择未单独截图；构造烟测不替代自然多人试玩。

### 6.27 M25-T4 V27：双乘客位（2026-11-07）

- 范围：联机车辆从“驾驶员 + 单乘客”扩展为“驾驶员 + 最多 2 名乘客”；第二名乘客复用既有 `EnterVehiclePassenger=45` 自动占空位，不新增命令、`PlayerState` 字段或存档字段。驾驶员下车后两名乘客都保留，任一乘客可接管空驾驶位，让座默认与第一位乘客交换；不做后排独立座位/偏移、座位持久化、多车辆、乘客攻击或正式多人平衡。
- 服务器：`ServerVehicleRuntime` 追加 `secondPassengerStableId`；`EnterServerVehiclePassenger` 选择第一个空乘客位，乘客下车后压缩到第一空位；`TakeDriverSeat` 接受任一乘客，`GiveDriverSeat` 选择第一位乘客交换；驾驶位释放、死亡、断线、重连仍统一释放对应座位。`PlayerState` 仍只表达 `passenger=true/false`，不暴露具体座位号。
- 复制/客户端：ZLR1 `VHCL` 从 25B 扩为 33B，在驾驶员/第一乘客后追加第二乘客 StableId，再追加车灯；解码拒绝驾驶员与乘客重复、两名乘客重复、第一乘客为空但第二乘客非空。客户端隐藏驾驶员/两名乘客精灵，车外 HUD 仅在两名乘客都满时显示“载具已满”，`F` 接管/让座判断同时考虑两名乘客。旧 25B ZLR1 客户端不兼容新服务器，`PlayerState` v18 不变。
- 测试：新增 `M25-T4-V27 VehicleSeat：双乘客位进入选择` **1/1（11 assertions）** 与 `M25-T4-V27 三客户端载具双乘客位：占用、接管与让座` **1/1（60 assertions）**，V27 合计 **2/2（71 assertions）**；`M25-T4*` **33/33（582 assertions）**，载具过滤 **42/42（551 assertions）**；全量 Debug **757/757（133485 assertions）**、Release **757/757（133498 assertions）**、Python **89/89**；日志 `build/m25_t4_v27_targeted.log`、`build/m25_t4_v27_all.log`、`build/m25_t4_v27_vehicle.log`、`build/m25_t4_v27_v26_regression.log`、`build/m25_t4_v27_replication.log`、`build/m25_t4_v27_debug.log`、`build/m25_t4_v27_release.log`、`build/m25_t4_v27_python.log`。三客户端用例覆盖 B/C 依次上车、ZLR1 两名乘客一致、驾驶员在位时抢座拒绝、驾驶员下车后第二乘客 C 接管、A 回座、C 让座给 B、B/C/A 依次下车与座位清空。
- 烟测：`build/m25_t4_v27_double_passenger_smoke/`，Release 1280×720，三客户端 A/B/C + 无窗口服务器；A 真实 Enter 驾驶，B/C 分别 Enter 上乘客位，服务器日志出现两条不同 `playerStableId` 的 `服务器上乘客位`；C Enter 下车后再次上车，A Enter 下车，C 按 `F` 接管驾驶位，再按 `F` 让座给 B；20 张 1280 截图，四份 stderr 为空；结果 `m25-t4-v27-double-passenger-smoke-pass passengers=2`。
- 限制：只支持 2 名乘客，没有后排座位号 UI、每座位独立位置/朝向/碰撞、座位持久化、多车辆或乘客战斗/攻击；车内仍只允许下车、快捷栏、背包/快捷使用与接管/让座；ZLR1 版本已扩展，旧客户端不能加入；构造三客户端烟测不替代自然多人试玩；正式音效仍待后续。

### 6.28 M26-V1/V2/V3/V4/V5 多车辆、碰撞、反馈、推挤与单机多车（2026-11-07）

- 范围：服务器从单辆 `m_serverVehicle` 改为 `std::vector<ServerVehicleRuntime> m_serverVehicles`，接管存档中全部 `ecs::Vehicle`，并按 `vehicle.count=3` 在锚点附近补足；每辆车独立座位、燃料、车况、后备箱、冷却与撞击记录。
- 命令路由：`EnterVehicle`/`EnterVehiclePassenger`/`ServiceVehicle` 按 `payload.entityId` 查找目标；`ExitVehicle`/`TakeDriverSeat`/`GiveDriverSeat`/`ToggleHeadlights`/`HonkHorn` 按 `SessionPlayer::drivingVehicleId` 查找当前车辆；`Move` 只写当前驾驶车辆。
- 客户端：新增 `FindNearestVehicle(range)` 扫描同层车辆用于上下车、维修/加油、后备箱和 HUD；非乘坐时 `SyncNetworkVehicleBinding` 只绑定 `enterRange` 内最近车辆，不再选复制流中的任意第一辆；乘坐第二辆/第三辆时预测、HUD、引擎音和乘客隐藏继续跟随目标。
- 兼容/配置：`VehicleStableId()` 返回最小 StableId，`VehicleStableIds()` 升序；`vehicle.count` 默认 3、范围 1–16；ZLR1 仍按车辆实体复制，无新命令/`PlayerState`/存档 schema。
- V2 载具间碰撞：`VehicleStep`/`VehiclePredictStep` 接受其他载具动态车身回调；服务器按相对速度结算双方车况并广播 `kVehicleRam`，有冷却；客户端驾驶预测用复制载具阻挡；V2 不做推挤/动量传递与乘员伤害，V4 补服务器位置推挤。
- V3 客户端反馈：`ApplyNetworkWorldSounds` 处理 `kVehicleRam` 时在撞击位置生成火花/金属碎片；只有本地驾驶且撞击点距本地车辆 8 格内才触发镜头震动，网络 `kVehicleHit` 复用同一门控；不改变服务器结算、协议或存档。
- V4 位置推挤：`VehicleCollisionPushDistance` 按相对速度和 `vehicleCollisionPushPerSpeed/Max` 计算推挤距离；服务器沿撞击方向以 0.05 格步进推开被撞车，检查目标格可走且不撞第三辆车，随后同步载具与乘员位置；复用碰撞冷却，不做质量、速度继承或持续冲量。
- V5 单机多车：`EnsureVehicleSpawned` 按 `vehicle.count` 补足车辆，`BindNearestLocalVehicle` 在 `enterRange` 内切换当前车；其余单机车作为动态车身阻挡，完全阻挡时按既有碰撞参数给双方扣车况并进入冷却；不做单机车推挤、质量/动量或乘员伤害。
- 测试：新增 `M26 多载具：两车独立驾驶与命令目标` **1/1（40 assertions）**、`M26-V2 VehicleStep：其他载具动态阻挡与相对冲击` **1/1（16 assertions）**、`M26-V2 服务器载具间碰撞：阻挡、双方车损与撞击音` **1/1（36 assertions）**、`M26-V4 VehicleCollisionPush：相对速度推挤距离` **1/1（7 assertions）**、`M26-V5 单机多车碰撞/存档` **2/2（47 assertions）**；`M26*` **6/6（146 assertions）**；全量 Debug **763/763（133631 assertions）**、Release **763/763（133645 assertions）**、Python **89/89**；`M25-T4*` **33/33（582 assertions）**、载具过滤 **44/44（574 assertions）**、`M25-T4* + M26*` **39/39（728 assertions）**；日志 `build/m26_v5_targeted.log`、`build/m26_v5_targeted_all.log`、`build/m26_v5_vehicle.log`、`build/m26_v5_debug.log`、`build/m26_v5_release.log`、`build/m26_v5_python.log`。
- 烟测：`build/m26_v1_multi_vehicle_smoke/`，Release 1280×720 双客户端 A/B + 无窗口服务器；3 辆车 StableId 6/7/8，A 进入 6、B 进入 8；A 首次驾驶 4.03 格、B 驾驶 2.67 格、A 二次驾驶 2.55 格；A 驾驶时 B 位移 0、B 驾驶时 A 位移 0；两次下车成功，三份 stderr 为空；14 张截图，结果 `m26-v1-multi-vehicle-smoke-pass cars=3 drivers=2`。V2–V5 尚无 1280 碰撞烟测。单机 V5 烟测 `build/m26_v5_single_player_multi_vehicle_smoke/`：3 辆车，A 驾驶 3.15 格、B 驾驶 4.78 格，走回 A 漂移 0.00 格，10 张截图，结果 `m26-v5-single-player-multi-vehicle-smoke-pass`。
- 限制：单机已支持多车生成/换车，但同一时刻只保留一个活动驾驶运行时，非活动车不模拟移动/推挤；非当前绑定车辆只有基础复制精灵，无每车远端车灯/破损叠加；客户端只保留一个引擎音循环；V2/V4 不做完整刚体动量/质量传递或碰撞乘员伤害，慢速接触不扣车况；V5 单机不推挤停放车辆；1280 构造/单机烟测已通过，未做多车自然多人试玩验收。详见 `docs/13-M26多车辆支持.md`。

### 6.29 M26-V6 单机停放车推挤与共用步进（2026-11-07）

- 范围：单机驾驶撞击停放车后，沿“本车 → 目标车”方向把被撞车推开一次；单机与服务器共用分步推挤逻辑。仅做位置推挤，不做质量/动量/持续冲量/乘员伤害。
- 实现：`VehicleSpike.h` 新增 `VehiclePushResult` 与 `VehiclePushBody`，按 `VehicleCollisionPushDistance` 结果以 0.05 格步进，每步检查目标格/第三辆车，方向自动归一化；`WorldDemoState::PushLocalVehicleOnCollision` 在双方车损结算后更新目标车 `ecs::Position` 并记录 `单机载具推挤` 日志；`ServerHost` 的 V4 推挤改为复用同一 helper，保留原行为。
- 测试：新增 `M26-V6 VehiclePushBody：分步推挤与阻挡` **1/1（13 assertions）**；`M26*` **7/7（159 assertions）**、`M25-T4*` **33/33（582 assertions）**、载具过滤 **45/45（587 assertions）**、`M25-T4* + M26*` **40/40（741 assertions）**；全量 Debug **764/764（133644 assertions）**、Release **764/764（133658 assertions）**、Python **89/89**；日志 `build/m26_v6_targeted.log`、`build/m26_v6_targeted_all.log`、`build/m26_v6_vehicle.log`、`build/m26_v6_debug.log`、`build/m26_v6_release.log`、`build/m26_v6_python.log`。
- 限制：V6 未新增 1280 碰撞烟测（自然驾驶路线受地图障碍影响），由纯逻辑测试 + 服务器碰撞集成用例 + V5 单机烟测回归覆盖；Release 1280 重跑 V5 多车烟测 `build/m26_v6_v5_regression_smoke/`，A/B 驾驶 2.59/2.42 格、停车漂移 0.00 格，未破坏生成/换车/停车。非活动车仍不持续模拟移动/推挤，不做完整刚体动量/质量传递或碰撞乘员伤害。

### 6.30 M26-V7 每车远端朝向/车灯/破损/烟雾（2026-11-08）

- 范围：为多车辆补齐每车远端视觉。服务器按速度更新并复制八向载具朝向；客户端让所有车辆分别显示已开启车灯、程序化破损叠加与低车况烟雾，不再只服务当前绑定车辆。只改复制/表现，不改碰撞、推挤、车况、命令、协议或存档 schema。
- 服务器：`ServerVehicleRuntime` 新增 `visualDirection`，`SyncServerVehicleToEcs` 按速度调用 `VehicleVisualDirectionFor`，停车时保留最后方向并写入对应 `object.vehicle_*` 的 `SpriteRef`；读档接管时从现有精灵恢复方向。
- 客户端：`m_vehicleVisualDirections` 按 StableId 缓存方向，本地绑定车用平滑朝向/滞回，其他车从复制精灵和速度恢复；`DrawVehicleHeadlights` 遍历视野内所有开灯车辆；`m_vehicleDamageVisuals` 让每辆载具的 DrawItem 叠加自己的划痕/凹陷；`m_vehicleSmokeRuntimes` 按 StableId 独立计时，软粒子烟雾全局上限 96。
- 测试：新增 `M26-V7 VehicleVisualDirectionVector：朝向单位向量` **1/1（26 assertions）** 与 `M26-V7 服务器载具朝向：移动后复制并在停车后保持` **1/1（21 assertions）**；`M26*` **9/9（206 assertions）**、`M25-T4*` **33/33（582 assertions）**、载具过滤 **46/46（613 assertions）**、`M25-T4* + M26*` **42/42（788 assertions）**；全量 Debug **766/766（133691 assertions）**、Release **766/766（133705 assertions）**、Python **89/89**；日志 `build/m26_v7_targeted.log`、`build/m26_v7_targeted_release.log`、`build/m26_v7_targeted_all.log`、`build/m26_v7_vehicle.log`、`build/m26_v7_debug.log`、`build/m26_v7_release.log`、`build/m26_v7_python.log`。
- 回归：Release 1280 重跑 V5 单机多车烟测 `build/m26_v7_v5_visual_regression_smoke/`，A/B 驾驶 4.02/5.02 格、停车漂移 0.00 格、10 张截图、stderr 为空；未新增夜间车灯/低车况破损专用截图。
- 限制：破损仍是划痕/凹陷占位表现，车灯光束无墙体遮挡/阴影；多辆远端车仍共用最近一辆的引擎音（V8 已补多车远端声部）；未做多车自然多人视觉验收；V9 已补等质量线性冲量，V10 已补碰撞乘员伤害，V11 已补分级质量与角速度。

### 6.31 M26-V8 多车远端引擎音（2026-11-08）

- 范围：监听客户端不再只播放当前绑定车的远端引擎音；按 StableId 同时维护最近最多 4 辆、18 格内、车况大于 0 的远端载具循环，本地驾驶声部优先。只改客户端音频混音状态，不改服务器、协议、存档、碰撞、推挤或车况。
- 实现：`VehicleSpike.h` 新增 `VehicleEngineAudioCandidate` 与 `SelectRemoteVehicleEngineAudio`（远端/车况/距离筛选、同距 StableId 排序、去重、4 声部上限、NaN/非法参数安全）；`WorldDemoState` 用 `m_vehicleEngineVoices` 按 StableId 保存句柄，逐个更新位置/音量/音高；载具退出驾驶、车况归零、超出 18 格/声部上限或断线时停止对应声部。复用 `vehicle_engine` 资源与 `Play3D`，不新增配置/资源/协议/存档字段。
- 测试：新增 `M26-V8 VehicleEngineAudio：远端多车距离筛选与声部上限` **1/1（12 assertions）**；`M26*` **10/10（218 assertions）**、`M25-T4*` **33/33（582 assertions）**、载具过滤 **47/47（625 assertions）**、`M25-T4* + M26*` **43/43（800 assertions）**；全量 Debug **767/767（133703 assertions）**、Release **767/767（133717 assertions）**、Python **89/89**；日志 `build/m26_v8_targeted.log`、`build/m26_v8_targeted_release_all.log`、`build/m26_v8_vehicle_only.log`、`build/m26_v8_combined.log`、`build/m26_v8_debug_final.log`、`build/m26_v8_release_final.log`、`build/m26_v8_python_final.log`。
- 烟测：`build/m26_v8_multi_engine_smoke/`，Release 1280×720 三客户端 A/B/C + 无窗口服务器；服务器 3 辆车 6/7/8，A 驾驶 6、C 驾驶 8，B 用 null device 监听；B 同时启动/停止载具 6/8 两个远端声部，A/C 分别驾驶 2.67/2.55 格，4 张截图、stderr 为空；脚本 `build/m26_v8_multi_engine_smoke.ps1`。null device 只验证声部句柄/资源/状态，不代表实际听感。
- 限制：最多 4 辆远端声部，超出按距离截断；不做正式混音、遮挡、距离曲线、音量面板或多车听感验收；未做多车自然多人试玩；V9 已补等质量线性冲量，V10 已补碰撞乘员伤害，V11 已补分级质量与角速度。

### 6.32 M26-V9 等质量碰撞冲量与无人车滑行（2026-11-08）

- 范围：在 V2/V4/V6 的位置阻挡、推挤与双方车况基础上，增加等质量线性碰撞冲量。撞击车与被撞车按中心法线分配碰撞后速度，被撞无人车可短时滑行；只做位置/速度表现与碰撞触发完善，不改命令、复制字段、协议或存档 schema。
- 实现：`VehicleSpike.h` 新增 `kVehicleCollisionRestitution=0.20`、`kVehicleCoastBraking=3.0`、`VehicleCollisionImpulseFor` 与 `VehicleStepResult::bodyContact`；冲量保留切向分量，NaN/非法质量/低于最小速度返回未应用，位置重合时用速度方向或 +X 兜底。服务器在 V2/V4 结算后写回双方 `VehicleMotion`，无人物车用 `VehiclePredictStep` 按 3.0 格/秒² 滑行，不消耗燃料、不追加车损；单机新增 `m_localParkedVehicleMotions`，按 StableId 保存被撞车残余运动并同步 ECS `Position`/`Velocity`。`bodyContact` 让轴滑动接触也进入碰撞结算，避免侧向接触绕过 V2/V6。
- 测试：新增 `M26-V9 VehicleCollisionImpulse：等质量冲量与质量比` **1/1（19 assertions）** 与 `M26-V9 服务器载具碰撞：冲量继承与无人车滑行` **1/1（23 assertions）**；`M26*` **12/12（261 assertions）**、`M25-T4*` **33/33（582 assertions）**、载具过滤 **48/48（645 assertions）**、`M25-T4* + M26*` **45/45（843 assertions）**；全量 Debug **769/769（133746 assertions）**、Release **769/769（133760 assertions）**、Python **89/89**；日志 `build/m26_v9_targeted_final.log`、`build/m26_v9_only.log`、`build/m26_v9_vehicle_final.log`、`build/m26_v9_combined_final.log`、`build/m26_v9_debug_final4.log`、`build/m26_v9_release_final6.log`、`build/m26_v9_python_final.log`。
- 限制：V9 未新增 1280 碰撞烟测，自动化覆盖冲量、目标速度、滑行与服务器无人物车；不做角动量/旋转、分级质量、持续冲量、乘员伤害或轮胎抓地模型，1280 碰撞与自然多人手感仍待补。

### 6.33 M26-V10 载具间碰撞乘员伤害（2026-11-08）

- 范围：在 V2/V4/V9 载具间碰撞、推挤和冲量基础上，对碰撞双方车内乘员结算生命伤害；只改乘员生命与提示，不改命令、复制字段、协议或存档 schema。
- 实现：`VehicleSpike.h` 新增 `VehicleCollisionOccupantDamage`，按 `vehicleCollisionMinSpeed` 门槛、基础值、接近速度系数和最大值计算 HP；服务器遍历双方驾驶位/第一乘客位/第二乘客位，以 `Blunt` 伤害写入存活乘员，不施加击退/硬直，保留既有护甲钝击减伤；单机对当前驾驶员使用同一规则并显示 `碰撞受伤：-X HP`。
- 配置：`vehicleCollisionOccupantDamageBase=3.0`、`vehicleCollisionOccupantDamagePerSpeed=1.2`、`vehicleCollisionOccupantDamageMax=20.0`；客户端与服务器均钳制解析。
- 测试：新增 `M26-V10 VehicleCollisionOccupantDamage：速度映射与配置上限` **1/1（10 assertions）**；V2 服务器三客户端碰撞用例追加双驾驶员+乘客 HP 下降断言（V2 服务器用例 **49 assertions**）；`M26*` **13/13（284 assertions）**、`M25-T4*` **33/33（582 assertions）**、载具过滤 **49/49（655 assertions）**、`M25-T4* + M26*` **46/46（866 assertions）**；全量 Debug **770/770（133769 assertions）**、Release **770/770（133783 assertions）**、Python **89/89**；日志 `build/m26_v10_targeted.log`、`build/m26_v10_passenger_v2.log`、`build/m26_v10_m26_passenger.log`、`build/m26_v10_vehicle_passenger.log`、`build/m26_v10_combined_passenger.log`、`build/m26_v10_debug_passenger_full.log`、`build/m26_v10_release_passenger_full.log`、`build/m26_v10_python.log`。
- 限制：V10 未新增 1280 碰撞烟测，但 V2 三客户端集成已覆盖乘客槽位伤害；撞墙/撞僵尸/撞动物仍走各自既有路径，不把乘员伤害扩展到这些场景；分级质量与角速度由 V11 补齐，完整刚体朝向、持续冲量与轮胎抓地仍待后续。

### 6.34 M26-V11 分级质量与碰撞角动量（2026-11-08）

- 范围：在 V9/V10 的线性冲量与乘员伤害基础上，把质量参数接入载具间碰撞，并加入切向摩擦产生的角速度与短时外观偏转；不改命令、复制字段、协议或存档 schema。
- 实现：`VehicleSpike.h` 新增 `VehicleMassClass`（轻/标准/重）与 `VehicleMassClassForStableId`，质量 0.85/1.0/1.35 按 StableId 稳定分配；`VehicleCollisionImpulseFor` 用双方质量计算法线冲量并保持动量守恒。新增 `vehicleCollisionFriction` 切向摩擦冲量与 `VehicleAngularInertia`，产生双方 `angularVelocity`；`VehicleIntegrateAngular` 在步进/预测中积分 `spinAngle` 并按 `vehicleAngularDamping` 回正。服务器写回双方角速度并通过既有 `visualDirection`/`SpriteRef` 复制；单机活动车与停放车复用同一角速度与八向外观；HUD 显示质量档位。
- 配置：`vehicleCollisionFriction=0.35`、`vehicleAngularDamping=3.0`；客户端与服务器均钳制解析。质量档位与质量值是代码常量，不写存档。
- 测试：新增 `M26-V11 VehicleMass：StableId 分级与碰撞质量比` **1/1（13 assertions）**、`M26-V11 VehicleCollisionImpulse：切向摩擦与角速度` **1/1（10 assertions）**、`M26-V11 VehicleIntegrateAngular：角速度偏转与阻尼回正` **1/1（6 assertions）**、`M26-V11 服务器载具碰撞：分级质量与角速度` **1/1（24 assertions）**；`M26*` **17/17（337 assertions）**、`M25-T4*` **33/33（582 assertions）**、载具过滤 **52/52（684 assertions）**、`M25-T4* + M26*` **50/50（919 assertions）**；全量 Debug **774/774（133822 assertions）**、Release **774/774（133836 assertions）**、Python **89/89**；日志 `build/m26_v11_targeted.log`、`build/m26_v11_m26.log`、`build/m26_v11_vehicle.log`、`build/m26_v11_combined.log`、`build/m26_v11_debug_full.log`、`build/m26_v11_release_full.log`、`build/m26_v11_python.log`。服务器日志实测质量 1.35/0.85、接近速度 4.0、角速度 1.12/1.79。
- 限制：角速度只驱动短时八向外观偏转/阻尼回正，不改变驾驶输入目标方向；联机本地驾驶仍以本地预测朝向为主，服务器视觉方向主要覆盖远端/停放车；没有完整刚体朝向、车轮/悬挂/轮胎抓地、持续接触冲量或 1280 碰撞烟测，自动化不替代真实碰撞手感验收。

### 6.35 M26-V12 显式车型与完整车身朝向（2026-11-08）

- 范围：把 V11 的 StableId 质量档升级为显式车型表，并把碰撞角速度从短时 `spinAngle` 外观升级为持久 `heading` 车身朝向；加入转向速率、侧向抓地与车型性能/损伤倍率，补齐存档/ZLR1/PlayerState 复制。
- 实现：`ecs::VehicleType` 为轻/标准/重皮卡，`VehicleProfile` 含质量 1100/1600/2400 kg、半径 0.60/0.65/0.65 格、长宽、极速/加速/制动/转向/抓地/车况伤害/乘员伤害倍率；`VehicleMotion.heading` 驱动 `VehicleStep` 转向、侧向抓地、角速度积分与八向视觉，`spinAngle` 移除。单机/服务器按 `VehicleTuningForType` 使用各自车型参数，质量影响冲量，半径影响碰撞/角惯量，质量比影响推挤距离，车型倍率影响车况/乘员伤害；HUD 与生成日志显示车型/质量。
- 持久化/复制：`ecs::Vehicle` 追加 type/heading，存档 Vehicle 组件 v3（旧档回退标准皮卡/南向），ZLR1 `VHCL` 37B、复制组件 v4/40B，`PlayerState` v19 追加 type/heading；客户端驾驶预测用 `VehicleCorrectHeading` 平滑校正。
- 测试：`M26-V12 VehicleProfile` 1/1（22）、`M26-V12 VehicleIntegrateAngular` 1/1（6）、`M26-V12 VehicleStep` 1/1（5）、`M26-V12 VehicleCorrectHeading` 1/1（4）；`M26*` 19/19（355）、`M25-T4*` 33/33（582）、载具过滤 54/54（704）、`M25-T4* + M26*` 52/52（937）；全量 Debug 776/133848、Release 776/133862、Python 89/89。
- 1280 回归：Release 重跑 V5 单机多车烟测 `build/m26_v12_v5_regression_smoke/`，A/B 驾驶 3.57/3.92 格、走回 A 漂移 0.00 格，HUD 实测 `标准皮卡 1600kg`/`重型皮卡 2400kg`，stderr 为空。
- 限制：没有车轮/悬挂/轮胎受力、持续接触冲量或完整刚体求解；视觉仍是八向资源；重型碰撞半径不放大到标准车以上以保持狭窄道路通行；未新增 1280 碰撞烟测，自动化不替代真实手感验收。

### 6.36 M26-V13 轮胎转向极限与悬挂姿态（2026-11-08）

- 范围：在 V12 显式车型/heading 基础上，补每车型轴距、轮距、最大前轮转角、轮胎横向加速度上限、悬挂频率/阻尼与 roll/pitch 增益；用自行车模型限制高速转向，并积分运行时车身姿态。
- 实现：`VehicleProfile` 轻/标准/重轴距 1.02/1.15/1.28 格、最大前轮转角 0.92/0.85/0.72 rad、最大横向加速度 20/18/15 格/秒²；`VehicleTuningForType` 继承到单机/服务器/客户端预测。`VehicleTireYawRate` 以 `speed*tan(steer)/wheelbase` 计算横摆，并用 `turnRate` 与 `maxLateralAcceleration/max(speed,0.5)` 双重钳制；`VehicleStep` 先求前轮转角再更新 heading，低速仍直接对齐。`VehicleSuspensionStep` 用弹簧-阻尼趋近由横向/纵向加速度映射的 roll/pitch；`VehicleMotion` 新增运行时 `steerAngle/lateralAcceleration/longitudinalAcceleration/bodyRoll/bodyRollVelocity/bodyPitch/bodyPitchVelocity`，不写存档、不复制、不改协议。
- 路面：`VehicleSurfaceProfile` 追加横向附着倍率，泥土路 0.90、草地 0.62；只限制轮胎横向加速度，与 V3 的极速/加速/制动倍率独立。
- 测试：`M26-V13 VehicleProfile` 1/1（10）、`M26-V13 VehicleTire` 1/1（9）、`M26-V13 VehicleSuspension` 1/1（8）、`M26-V13 VehicleStep` 1/1（4）；`M26*` 23/23（386）、`M25-T4*` 33/33（582）、载具过滤 58/58（735）、`M25-T4* + M26*` 56/56（968）；全量 Debug 780/133879、Release 780/133893、Python 89/89。
- 1280 回归：Release 重跑 V5 单机多车烟测 `build/m26_v13_v5_regression_smoke/`，A/B 驾驶 2.57/3.95 格、走回 A 漂移 0.00 格，stderr 为空；新转向/悬挂路径未破坏生成、换车、停车与燃料 HUD。
- 限制：没有逐车轮接地采样、轮胎纵向滑移/ABS、悬挂碰撞、车身姿态渲染旋转或持续接触冲量；姿态只是运行时数据；未补稳定的 1280 双车碰撞/乘客构造烟测，自动化不替代真实转向手感验收。

### 6.37 M26-V14 低速持续接触冲量（2026-11-08）

- 范围：载具动态车身已接触但接近速度低于 `vehicleCollisionMinSpeed` 时，按质量传递完全非弹性法线冲量，让低速顶推也能推动停放/无人物车；不改变高速碰撞的车况/乘员伤害/推挤/音效/冷却。
- 纯逻辑：新增 `kVehicleContactMinSpeed=0.15f`、`VehicleContactImpulseFor`，并给 `VehicleCollisionImpulseFor` 增加可选 `minClosingSpeed`。接触路径恢复系数/切向摩擦为 0；低于 0.15 格/秒、分离、NaN 不应用；1.0 格/秒等质量接触得到双方 0.5 格/秒，目标车况/乘员伤害为 0。
- 服务器：`ApplyServerVehicleVehicleCollisions` 在 `bodyContact`/`bodyCollision` 时先尝试接触冲量；无车损时只写双方速度、同步目标 `Position`/`Velocity`/乘员，不扣车况、不伤乘员、不广播 `kVehicleRam`、不设置冷却。无人目标继续用 `vehicleCoastBraking` 滑行。达到车损门槛仍走原路径。
- 单机：`ApplyLocalVehicleVehicleCollision` 复用同一接触逻辑，低速目标写入 `m_localParkedVehicleMotions` 并滑行；无伤害时不显示碰撞提示、不播放撞击音/火花/震屏。接触日志为 debug 级别。
- 测试：新增 `M26-V14 VehicleContactImpulse` 1/1（15）、`M26-V14 服务器载具低速接触` 1/1（20）；`M26*` 25/25（421）、`M25-T4*` 33/33（582）、载具过滤 59/59（750）、`M25-T4* + M26*` 58/58（1003）；全量 Debug 782/133914、Release 782/133928、Python 89/89。
- 1280 回归：Release 重跑 V5 单机多车烟测 `build/m26_v14_v5_regression_smoke/`，A/B 驾驶 4.02/5.06 格、走回 A 漂移 0.00 格，stderr 为空；未新增 1280 低速接触构造烟测。
- 限制：只做速度传递，不做接触位置分离/持续法向约束；目标被墙体或第三辆车卡住时不会强制挤出；无独立接触音效/车损/乘员伤害；阈值是代码常量，不新增协议/存档；低速推挤仍由服务器/单机逐 tick 重新评估。

## 7. 用户确认项

已在 2026-11-05 确认：

- [x] 载具类型：皮卡（占位图）可以。
- [x] 键位：`ENTER` 上车/下车；`WASD` 驾驶；`ESC` 暂停。
- [x] 范围：仅单机、单车、无存档/联机/耐久/储物可以。
- [x] 数值：最高 10 格/秒、燃料约 83 秒可以。
- [x] 验收：2 张 1280 截图 + 全量测试即可，不要求正式美术。

当前进度：**M25-T1/M25-T2/M25-T3 已完成**；M25-T4 V1（碾压/噪音）、V2（车灯）、V3（路面附着）、V4（引擎/撞击占位音效）、V5（撞击车况磨损）、V6（低车况动力衰减）、V7（撞击击杀经验）、V8（远端载具引擎音）、V9（自然试玩与驾驶员防护）、V10（皮卡四向外观）、V11（低车况/归零烟雾）、V12（八向皮卡外观）、V13（软粒子烟雾）、V14（载具喇叭/主动噪音）、V15（僵尸攻击载具扣车况）、V16（载具受击音效/火花）、V17（载具破损外观）、V18（载具受击镜头反馈/金属碎片）、V19（载具撞击动物）、V20（危险动物反击载具）、V21（联机 PvP 载具撞玩家）、V22（联机载具乘客位）、V23（乘客接管驾驶位）、V24（驾驶员停车让座）、V25（乘客车内背包与后备箱）、V26（乘客快捷使用与装备）、V27（双乘客位）已完成自动化+1280 构造/自然/外观/烟雾/软粒子/喇叭/受击/受击反馈/破损/冲击反馈/动物/动物反击/PvP/乘客/接管驾驶/让座/后备箱/快捷使用/双乘客烟测；M26-V1（联机多车辆）、M26-V2（载具间碰撞）、M26-V3（客户端撞击反馈）、M26-V4（位置推挤）、M26-V5（单机多车生成/换车/停放阻挡）、M26-V6（单机停放车推挤/共用步进）、M26-V7（每车远端朝向/车灯/破损/烟雾）、M26-V8（多车远端引擎音）、M26-V9（等质量碰撞冲量/无人车滑行）、M26-V10（碰撞乘员伤害）、M26-V11（分级质量/切向摩擦角速度）、M26-V12（显式车型/车身朝向/转向/侧向抓地）、M26-V13（轮胎转向极限/悬挂姿态）与 M26-V14（低速接触冲量）已完成自动化/接入，V5/V7 另补 1280 单机烟测回归、V8 补三客户端 1280 声部烟测；正式音效、逐车轮接地/轮胎纵向滑移、悬挂碰撞、持续接触求解和更完整自然试玩待后续（2026-11-08）。
